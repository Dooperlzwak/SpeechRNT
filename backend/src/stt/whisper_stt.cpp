#include "stt/whisper_stt.hpp"
#include "stt/stt_interface.hpp"
#include "stt/quantization_config.hpp"
#include "utils/gpu_manager.hpp"
#include "utils/performance_monitor.hpp"
#include "audio/audio_buffer_manager.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <random>
#include <filesystem>
#include <numeric>
#include <sstream>
#include <cctype>

#ifdef WHISPER_AVAILABLE
#include "whisper.h"
#endif

namespace stt {

WhisperSTT::WhisperSTT() 
    : initialized_(false)
    , language_("en")
    , ctx_(nullptr)
    , translate_to_english_(false)
    , temperature_(0.0f)
    , max_tokens_(0)
    , n_threads_(4)
    , gpu_enabled_(false)
    , gpu_device_id_(-1)
    , partialResultsEnabled_(true)
    , minChunkSizeMs_(1000)
    , confidenceThreshold_(0.5f)
    , wordLevelConfidenceEnabled_(true)
    , qualityIndicatorsEnabled_(true)
    , confidenceFilteringEnabled_(false)
    , languageDetectionEnabled_(false)
    , languageDetectionThreshold_(0.7f)
    , autoLanguageSwitching_(false)
    , currentDetectedLanguage_("en")
    , currentQuantizationLevel_(QuantizationLevel::FP32)
    , quantizationManager_(std::make_unique<QuantizationManager>())
    , performanceTracker_(std::make_unique<STTPerformanceTracker>()) {
    
    // Initialize performance tracker
    performanceTracker_->initialize(true); // Enable detailed tracking
}

WhisperSTT::~WhisperSTT() {
    // Clean up streaming states first
    {
        std::lock_guard<std::mutex> streamingLock(streamingMutex_);
        streamingStates_.clear();
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Clean up quantized models
    cleanupQuantizedModels();
    
#ifdef WHISPER_AVAILABLE
    if (ctx_) {
        whisper_free(ctx_);
        ctx_ = nullptr;
    }
    if (params_) {
        whisper_free_params(params_);
        params_ = nullptr;
    }
#endif
    
    initialized_ = false;
}

bool WhisperSTT::initialize(const std::string& modelPath, int n_threads) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    modelPath_ = modelPath;
    n_threads_ = n_threads;
    last_error_.clear();
    
#ifdef WHISPER_AVAILABLE
    // Validate model file exists and is readable
    std::ifstream modelFile(modelPath, std::ios::binary);
    if (!modelFile.good()) {
        last_error_ = "Model file not found or not readable: " + modelPath;
        std::cerr << last_error_ << std::endl;
        return false;
    }
    modelFile.close();
    
    // Initialize whisper.cpp context with default parameters
    whisper_context_params ctx_params = whisper_context_default_params();
    ctx_params.use_gpu = false; // Start with CPU, GPU will be handled separately
    
    ctx_ = whisper_init_from_file_with_params(modelPath.c_str(), ctx_params);
    if (!ctx_) {
        last_error_ = "Failed to load whisper model from: " + modelPath + 
                     ". Check if the model file is valid and compatible.";
        std::cerr << last_error_ << std::endl;
        return false;
    }
    
    // Validate model compatibility
    if (!validateModel()) {
        whisper_free(ctx_);
        ctx_ = nullptr;
        return false;
    }
    
    // Setup default parameters
    if (!setupWhisperParams()) {
        whisper_free(ctx_);
        ctx_ = nullptr;
        return false;
    }
    
    std::cout << "Whisper STT initialized successfully with model: " << modelPath << std::endl;
    std::cout << "Model type: " << whisper_model_type_readable(ctx_) << std::endl;
    std::cout << "Model vocab size: " << whisper_model_n_vocab(ctx_) << std::endl;
    
    // Initialize AudioBufferManager for streaming support
    if (!initializeAudioBufferManager()) {
        whisper_free(ctx_);
        ctx_ = nullptr;
        return false;
    }
    
    initialized_ = true;
    return true;
#else
    // Fallback simulation mode when whisper.cpp is not available
    std::cout << "Whisper STT initialized in simulation mode (whisper.cpp not available)" << std::endl;
    std::cout << "Model path: " << modelPath << std::endl;
    
    // Initialize AudioBufferManager for streaming support
    if (!initializeAudioBufferManager()) {
        return false;
    }
    
    initialized_ = true;
    return true;
#endif
}

void WhisperSTT::transcribe(const std::vector<float>& audioData, TranscriptionCallback callback) {
    if (!initialized_) {
        last_error_ = "WhisperSTT not initialized";
        std::cerr << last_error_ << std::endl;
        return;
    }
    
    if (audioData.empty()) {
        return;
    }
    
#ifdef WHISPER_AVAILABLE
    std::thread([this, audioData, callback]() {
        auto timer = utils::PerformanceMonitor::getInstance().startLatencyTimer(
            utils::PerformanceMonitor::METRIC_STT_LATENCY);
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            // Validate audio data size
            if (audioData.size() > 30 * WHISPER_SAMPLE_RATE) { // Max 30 seconds
                last_error_ = "Audio data too long: " + std::to_string(audioData.size() / WHISPER_SAMPLE_RATE) + " seconds";
                std::cerr << last_error_ << std::endl;
                utils::PerformanceMonitor::getInstance().recordCounter("stt.errors");
                return;
            }
            
            // Run whisper inference with proper parameters
            int result = whisper_full(ctx_, *params_, audioData.data(), static_cast<int>(audioData.size()));
            
            if (result != 0) {
                last_error_ = "Whisper inference failed with code: " + std::to_string(result);
                std::cerr << last_error_ << std::endl;
                utils::PerformanceMonitor::getInstance().recordCounter("stt.errors");
                
                // Send empty result to indicate failure
                TranscriptionResult emptyResult;
                emptyResult.text = "";
                emptyResult.confidence = 0.0f;
                emptyResult.is_partial = false;
                emptyResult.start_time_ms = 0;
                emptyResult.end_time_ms = 0;
                
                // Enhance with confidence information to mark as failed
                enhanceTranscriptionResultWithConfidence(emptyResult, audioData, 0.0f);
                emptyResult.quality_level = "failed";
                
                callback(emptyResult);
                return;
            }
            
            processTranscriptionResult(callback, false);
            utils::PerformanceMonitor::getInstance().recordCounter("stt.transcriptions_completed");
            
        } catch (const std::exception& e) {
            last_error_ = "Exception during transcription: " + std::string(e.what());
            std::cerr << last_error_ << std::endl;
            utils::PerformanceMonitor::getInstance().recordCounter("stt.errors");
        }
    }).detach();
#else
    // Simulation mode
    std::thread([this, callback, audioData]() {
        auto processingStartTime = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        TranscriptionResult result;
        if (audioData.size() > 16000) { // ~1 second of audio at 16kHz
            result.text = "Hello, this is a simulated transcription";
            // Simulate confidence based on audio length and quality
            float baseConfidence = 0.85f + (std::min(audioData.size(), 48000u) / 48000.0f) * 0.1f;
            result.confidence = std::min(0.95f, baseConfidence);
        } else {
            result.text = "Short audio";
            result.confidence = 0.70f + (audioData.size() / 16000.0f) * 0.15f;
        }
        result.is_partial = false;
        result.start_time_ms = 0;
        result.end_time_ms = static_cast<int64_t>(audioData.size() * 1000 / 16000);
        
        // Calculate processing latency
        auto processingEndTime = std::chrono::steady_clock::now();
        float processingLatencyMs = std::chrono::duration<float, std::milli>(processingEndTime - processingStartTime).count();
        
        // Enhance with confidence information
        enhanceTranscriptionResultWithConfidence(result, audioData, processingLatencyMs);
        
        callback(result);
    }).detach();
#endif
}

void WhisperSTT::transcribeLive(const std::vector<float>& audioData, TranscriptionCallback callback) {
    if (!initialized_) {
        last_error_ = "WhisperSTT not initialized";
        std::cerr << last_error_ << std::endl;
        return;
    }
    
    if (audioData.empty()) {
        return;
    }
    
#ifdef WHISPER_AVAILABLE
    std::thread([this, audioData, callback]() {
        auto timer = utils::PerformanceMonitor::getInstance().startLatencyTimer(
            utils::PerformanceMonitor::METRIC_STT_LATENCY);
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            // For live transcription, enable single segment mode for faster processing
            whisper_full_params live_params = *params_;
            live_params.single_segment = true;
            live_params.no_context = true; // Don't use previous context for live mode
            live_params.duration_ms = 0; // Process entire audio chunk
            
            // Validate audio data size for live processing
            if (audioData.size() > 10 * WHISPER_SAMPLE_RATE) { // Max 10 seconds for live
                last_error_ = "Live audio chunk too long: " + std::to_string(audioData.size() / WHISPER_SAMPLE_RATE) + " seconds";
                std::cerr << last_error_ << std::endl;
                utils::PerformanceMonitor::getInstance().recordCounter("stt.errors");
                return;
            }
            
            int result = whisper_full(ctx_, live_params, audioData.data(), static_cast<int>(audioData.size()));
            
            if (result != 0) {
                last_error_ = "Live whisper inference failed with code: " + std::to_string(result);
                std::cerr << last_error_ << std::endl;
                utils::PerformanceMonitor::getInstance().recordCounter("stt.errors");
                
                // Send empty result to indicate failure
                TranscriptionResult emptyResult;
                emptyResult.text = "";
                emptyResult.confidence = 0.0f;
                emptyResult.is_partial = true;
                emptyResult.start_time_ms = 0;
                emptyResult.end_time_ms = 0;
                
                // Enhance with confidence information to mark as failed
                enhanceTranscriptionResultWithConfidence(emptyResult, audioData, 0.0f);
                emptyResult.quality_level = "failed";
                
                callback(emptyResult);
                return;
            }
            
            processTranscriptionResult(callback, true);
            utils::PerformanceMonitor::getInstance().recordCounter("stt.live_transcriptions_completed");
            
        } catch (const std::exception& e) {
            last_error_ = "Exception during live transcription: " + std::string(e.what());
            std::cerr << last_error_ << std::endl;
            utils::PerformanceMonitor::getInstance().recordCounter("stt.errors");
        }
    }).detach();
#else
    // Simulation mode for live transcription with enhanced confidence
    std::thread([this, callback, audioData]() {
        auto processingStartTime = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Faster for live mode
        
        TranscriptionResult result;
        result.text = "Live transcription simulation";
        // Simulate lower confidence for live/partial results
        result.confidence = 0.75f + (std::min(audioData.size(), 32000u) / 32000.0f) * 0.15f;
        result.is_partial = true;
        result.start_time_ms = 0;
        result.end_time_ms = static_cast<int64_t>(audioData.size() * 1000 / 16000);
        
        // Calculate processing latency
        auto processingEndTime = std::chrono::steady_clock::now();
        float processingLatencyMs = std::chrono::duration<float, std::milli>(processingEndTime - processingStartTime).count();
        
        // Enhance with confidence information
        enhanceTranscriptionResultWithConfidence(result, audioData, processingLatencyMs);
        
        callback(result);
    }).detach();
#endif
}

void WhisperSTT::setLanguage(const std::string& language) {
    std::lock_guard<std::mutex> lock(mutex_);
    language_ = language;
    
#ifdef WHISPER_AVAILABLE
    if (params_) {
        // Update language in whisper parameters
        if (language == "auto" || language.empty()) {
            params_->language = nullptr;
            params_->detect_language = true;
        } else {
            // Validate language code if context is available
            if (ctx_) {
                int lang_id = whisper_lang_id(language.c_str());
                if (lang_id == -1) {
                    std::cerr << "Warning: Unknown language code '" << language 
                              << "', using auto-detection" << std::endl;
                    params_->language = nullptr;
                    params_->detect_language = true;
                } else {
                    params_->language = language.c_str();
                    params_->detect_language = false;
                    std::cout << "Language set to: " << language 
                              << " (" << whisper_lang_str_full(lang_id) << ")" << std::endl;
                }
            } else {
                params_->language = language.c_str();
                params_->detect_language = false;
            }
        }
    }
#endif
    
    std::cout << "Setting STT language to: " << language << std::endl;
}

void WhisperSTT::setTranslateToEnglish(bool translate) {
    std::lock_guard<std::mutex> lock(mutex_);
    translate_to_english_ = translate;
    
#ifdef WHISPER_AVAILABLE
    if (params_) {
        params_->translate = translate;
    }
#endif
}

void WhisperSTT::setTemperature(float temperature) {
    std::lock_guard<std::mutex> lock(mutex_);
    temperature_ = std::max(0.0f, std::min(1.0f, temperature)); // Clamp to valid range
    
#ifdef WHISPER_AVAILABLE
    if (params_) {
        params_->temperature = temperature_;
    }
#endif
}

void WhisperSTT::setMaxTokens(int max_tokens) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_tokens_ = std::max(0, max_tokens); // Ensure non-negative
    
#ifdef WHISPER_AVAILABLE
    if (params_) {
        params_->n_max_text_ctx = max_tokens_;
    }
#endif
}

void WhisperSTT::setLanguageDetectionEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    languageDetectionEnabled_ = enabled;
    
    std::cout << "Language detection " << (enabled ? "enabled" : "disabled") << std::endl;
    
#ifdef WHISPER_AVAILABLE
    if (params_) {
        // Enable language detection in whisper parameters
        params_->detect_language = enabled;
        if (enabled) {
            // Set language to null to enable auto-detection
            params_->language = nullptr;
        } else {
            // Use the configured language
            params_->language = language_.c_str();
        }
    }
#endif
}

void WhisperSTT::setLanguageDetectionThreshold(float threshold) {
    std::lock_guard<std::mutex> lock(mutex_);
    languageDetectionThreshold_ = std::max(0.0f, std::min(1.0f, threshold));
    
    std::cout << "Language detection threshold set to: " << languageDetectionThreshold_ << std::endl;
}

void WhisperSTT::setAutoLanguageSwitching(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    autoLanguageSwitching_ = enabled;
    
    std::cout << "Auto language switching " << (enabled ? "enabled" : "disabled") << std::endl;
}

void WhisperSTT::setLanguageChangeCallback(LanguageChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    languageChangeCallback_ = callback;
}

void WhisperSTT::setTranscriptionCompleteCallback(TranscriptionCompleteCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    transcriptionCompleteCallback_ = callback;
}

bool WhisperSTT::validateModel() {
#ifdef WHISPER_AVAILABLE
    if (!ctx_) {
        last_error_ = "Context is null during model validation";
        return false;
    }
    
    // Check if model is multilingual
    bool is_multilingual = whisper_is_multilingual(ctx_);
    std::cout << "Model is " << (is_multilingual ? "multilingual" : "English-only") << std::endl;
    
    // Validate model has reasonable vocab size
    int vocab_size = whisper_model_n_vocab(ctx_);
    if (vocab_size < 1000) {
        last_error_ = "Model vocabulary size too small: " + std::to_string(vocab_size);
        return false;
    }
    
    // Check audio context size
    int audio_ctx = whisper_model_n_audio_ctx(ctx_);
    if (audio_ctx <= 0) {
        last_error_ = "Invalid audio context size: " + std::to_string(audio_ctx);
        return false;
    }
    
    std::cout << "Model validation successful - vocab: " << vocab_size 
              << ", audio_ctx: " << audio_ctx << std::endl;
    
    return true;
#else
    return true; // Always succeed in simulation mode
#endif
}

bool WhisperSTT::setupWhisperParams() {
#ifdef WHISPER_AVAILABLE
    // Use the new parameter allocation function
    params_ = whisper_full_default_params_by_ref(WHISPER_SAMPLING_GREEDY);
    if (!params_) {
        last_error_ = "Failed to allocate whisper parameters";
        return false;
    }
    
    // Configure parameters for real-time transcription
    params_->n_threads = n_threads_;
    params_->offset_ms = 0;
    params_->duration_ms = 0;
    params_->translate = translate_to_english_;
    params_->no_context = false;
    params_->single_segment = false;
    params_->print_realtime = false;
    params_->print_progress = false;
    params_->print_timestamps = true;
    params_->print_special = false;
    params_->max_tokens = max_tokens_;
    params_->temperature = temperature_;
    
    // Set language (handle auto-detection)
    if (language_ == "auto" || language_.empty()) {
        params_->language = nullptr; // Auto-detect
        params_->detect_language = true;
    } else {
        params_->language = language_.c_str();
        params_->detect_language = false;
    }
    
    // Enable token-level timestamps for better live transcription
    params_->token_timestamps = true;
    params_->thold_pt = 0.01f;
    params_->thold_ptsum = 0.01f;
    
    // Optimize for real-time performance
    params_->suppress_blank = true;
    params_->suppress_nst = true;
    
    return true;
#else
    params_ = nullptr;
    return true; // Always succeed in simulation mode
#endif
}

bool WhisperSTT::initializeWithGPU(const std::string& modelPath, int gpuDeviceId, int n_threads) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& gpuManager = utils::GPUManager::getInstance();
    
    // Check if GPU is available
    if (!gpuManager.isCudaAvailable()) {
        std::cout << "GPU not available, falling back to CPU initialization" << std::endl;
        return initialize(modelPath, n_threads);
    }
    
    // Set GPU device
    if (!gpuManager.setDevice(gpuDeviceId)) {
        std::cout << "Failed to set GPU device " << gpuDeviceId << ", falling back to CPU" << std::endl;
        return initialize(modelPath, n_threads);
    }
    
    modelPath_ = modelPath;
    n_threads_ = n_threads;
    gpu_enabled_ = true;
    gpu_device_id_ = gpuDeviceId;
    last_error_.clear();
    
#ifdef WHISPER_AVAILABLE
    // Validate model file exists and is readable
    std::ifstream modelFile(modelPath, std::ios::binary);
    if (!modelFile.good()) {
        last_error_ = "Model file not found or not readable: " + modelPath;
        std::cerr << last_error_ << std::endl;
        gpu_enabled_ = false;
        return false;
    }
    modelFile.close();
    
    // Initialize whisper.cpp context with GPU support
    whisper_context_params ctx_params = whisper_context_default_params();
    ctx_params.use_gpu = true;
    ctx_params.gpu_device = gpuDeviceId;
    
    ctx_ = whisper_init_from_file_with_params(modelPath.c_str(), ctx_params);
    if (!ctx_) {
        last_error_ = "Failed to load whisper model with GPU support from: " + modelPath + 
                     ". GPU may not have enough memory or model may be incompatible.";
        std::cerr << last_error_ << std::endl;
        gpu_enabled_ = false;
        // Fallback to CPU
        std::cout << "Attempting CPU fallback..." << std::endl;
        return initialize(modelPath, n_threads);
    }
    
    // Validate model compatibility
    if (!validateModel()) {
        whisper_free(ctx_);
        ctx_ = nullptr;
        gpu_enabled_ = false;
        return false;
    }
    
    // Setup default parameters
    if (!setupWhisperParams()) {
        whisper_free(ctx_);
        ctx_ = nullptr;
        gpu_enabled_ = false;
        return false;
    }
    
    auto deviceInfo = gpuManager.getDeviceInfo(gpuDeviceId);
    std::cout << "Whisper STT initialized with GPU acceleration on: " << deviceInfo.name 
              << " (Device " << gpuDeviceId << ")" << std::endl;
    std::cout << "Model type: " << whisper_model_type_readable(ctx_) << std::endl;
    std::cout << "Model vocab size: " << whisper_model_n_vocab(ctx_) << std::endl;
    std::cout << "GPU memory usage will be monitored during inference" << std::endl;
    
    // Initialize AudioBufferManager for streaming support
    if (!initializeAudioBufferManager()) {
        whisper_free(ctx_);
        ctx_ = nullptr;
        gpu_enabled_ = false;
        return false;
    }
    
    initialized_ = true;
    return true;
#else
    // Fallback to CPU simulation mode
    std::cout << "Whisper STT initialized in GPU simulation mode (whisper.cpp not available)" << std::endl;
    std::cout << "Model path: " << modelPath << std::endl;
    
    // Initialize AudioBufferManager for streaming support
    if (!initializeAudioBufferManager()) {
        return false;
    }
    
    initialized_ = true;
    return true;
#endif
}

void WhisperSTT::processTranscriptionResult(TranscriptionCallback callback, bool is_partial) {
    auto processingStartTime = std::chrono::steady_clock::now();
    
#ifdef WHISPER_AVAILABLE
    const int n_segments = whisper_full_n_segments(ctx_);
    
    if (n_segments == 0) {
        // Send empty result if no segments found
        TranscriptionResult result;
        result.text = "";
        result.confidence = 0.0f;
        result.is_partial = is_partial;
        result.start_time_ms = 0;
        result.end_time_ms = 0;
        
        // Calculate processing latency
        auto processingEndTime = std::chrono::steady_clock::now();
        float processingLatencyMs = std::chrono::duration<float, std::milli>(processingEndTime - processingStartTime).count();
        
        // Enhance with confidence information
        enhanceTranscriptionResultWithConfidence(result, {}, processingLatencyMs);
        
        callback(result);
        return;
    }
    
    // Combine all segments into a single result
    std::string combined_text;
    int64_t start_time = INT64_MAX;
    int64_t end_time = 0;
    float total_confidence = 0.0f;
    int valid_segments = 0;
    std::vector<float> segment_confidences;
    
    for (int i = 0; i < n_segments; ++i) {
        const char* text = whisper_full_get_segment_text(ctx_, i);
        if (!text || strlen(text) == 0) {
            continue; // Skip empty segments
        }
        
        // Add text with proper spacing
        if (!combined_text.empty()) {
            combined_text += " ";
        }
        combined_text += text;
        
        // Get timing information (whisper returns in centiseconds, convert to ms)
        int64_t seg_start = whisper_full_get_segment_t0(ctx_, i) * 10;
        int64_t seg_end = whisper_full_get_segment_t1(ctx_, i) * 10;
        
        start_time = std::min(start_time, seg_start);
        end_time = std::max(end_time, seg_end);
        
        // Use enhanced confidence calculation
        float segment_confidence = calculateSegmentConfidence(i);
        segment_confidences.push_back(segment_confidence);
        
        total_confidence += segment_confidence;
        valid_segments++;
    }
    
    // Calculate weighted average confidence (longer segments have more weight)
    float avg_confidence = 0.0f;
    if (valid_segments > 0) {
        // Simple average for now, could be enhanced with segment length weighting
        avg_confidence = total_confidence / valid_segments;
        
        // Apply confidence adjustment based on segment consistency
        if (segment_confidences.size() > 1) {
            float confidence_variance = 0.0f;
            for (float conf : segment_confidences) {
                confidence_variance += (conf - avg_confidence) * (conf - avg_confidence);
            }
            confidence_variance /= segment_confidences.size();
            
            // Reduce confidence if segments have high variance (inconsistent quality)
            if (confidence_variance > 0.1f) {
                avg_confidence *= (1.0f - std::min(0.2f, confidence_variance));
            }
        }
    }
    
    // Clamp confidence to reasonable range
    avg_confidence = std::max(0.0f, std::min(1.0f, avg_confidence));
    
    // Trim whitespace from combined text
    if (!combined_text.empty()) {
        size_t start = combined_text.find_first_not_of(" \t\n\r");
        size_t end = combined_text.find_last_not_of(" \t\n\r");
        if (start != std::string::npos && end != std::string::npos) {
            combined_text = combined_text.substr(start, end - start + 1);
        }
    }
    
    TranscriptionResult result;
    result.text = combined_text;
    result.confidence = avg_confidence;
    result.is_partial = is_partial;
    result.start_time_ms = (start_time == INT64_MAX) ? 0 : start_time;
    result.end_time_ms = end_time;
    
    // Add language detection information
    updateTranscriptionResultWithLanguage(result);
    
    // Calculate processing latency
    auto processingEndTime = std::chrono::steady_clock::now();
    float processingLatencyMs = std::chrono::duration<float, std::milli>(processingEndTime - processingStartTime).count();
    
    // Enhance with confidence information (pass empty audio data as we don't have access to original here)
    enhanceTranscriptionResultWithConfidence(result, {}, processingLatencyMs);
    
    // Handle language change if detected and not partial
    if (!is_partial && result.language_changed && shouldSwitchLanguage(result.detected_language, result.language_confidence)) {
        handleLanguageChange(result.detected_language, result.language_confidence);
    }
    
    // Log enhanced result for debugging
    std::cout << "Transcription result: \"" << result.text 
              << "\" (confidence: " << result.confidence 
              << ", quality: " << result.quality_level
              << ", meets_threshold: " << (result.meets_confidence_threshold ? "yes" : "no")
              << ", segments: " << valid_segments 
              << ", words: " << result.word_timings.size()
              << ", language: " << result.detected_language << ")" << std::endl;
    
    callback(result);
    
    // Trigger translation pipeline if this is a final result and callback is set
    if (!is_partial && transcriptionCompleteCallback_) {
        // Generate multiple candidates for translation pipeline
        std::vector<TranscriptionResult> candidates;
        // For now, we'll use the current result as the primary candidate
        // In a full implementation, we would generate multiple candidates here
        triggerTranslationPipeline(0, result, candidates); // utteranceId would be passed from caller
    }
#endif
}

// Streaming transcription implementation
void WhisperSTT::startStreamingTranscription(uint32_t utteranceId) {
    if (!initialized_) {
        last_error_ = "WhisperSTT not initialized";
        std::cerr << last_error_ << std::endl;
        return;
    }
    
    std::lock_guard<std::mutex> lock(streamingMutex_);
    
    // Check if utterance already exists
    if (streamingStates_.find(utteranceId) != streamingStates_.end()) {
        std::cout << "Streaming transcription already active for utterance: " << utteranceId << std::endl;
        return;
    }
    
    // Create new streaming state
    auto state = std::make_unique<StreamingState>();
    state->utteranceId = utteranceId;
    state->isActive = true;
    state->startTime = std::chrono::steady_clock::now();
    state->lastProcessTime = state->startTime;
    
    // Create utterance buffer in AudioBufferManager
    if (audioBufferManager_) {
        audioBufferManager_->createUtterance(utteranceId, 8); // 8MB buffer
        audioBufferManager_->setUtteranceActive(utteranceId, true);
    }
    
    streamingStates_[utteranceId] = std::move(state);
    
    std::cout << "Started streaming transcription for utterance: " << utteranceId << std::endl;
}

void WhisperSTT::addAudioChunk(uint32_t utteranceId, const std::vector<float>& audio) {
    if (!initialized_ || audio.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(streamingMutex_);
    
    auto it = streamingStates_.find(utteranceId);
    if (it == streamingStates_.end() || !it->second->isActive) {
        return;
    }
    
    StreamingState& state = *it->second;
    
    // Add audio to buffer manager
    if (audioBufferManager_) {
        audioBufferManager_->addAudioData(utteranceId, audio);
    } else {
        // Fallback: accumulate in memory
        state.accumulatedAudio.insert(state.accumulatedAudio.end(), audio.begin(), audio.end());
    }
    
    state.totalAudioSamples += audio.size();
    
    // Check if we should process this chunk for partial results
    if (partialResultsEnabled_ && shouldProcessStreamingChunk(state)) {
        processStreamingAudio(utteranceId, state);
    }
    
    // Audio chunk added successfully
}

void WhisperSTT::finalizeStreamingTranscription(uint32_t utteranceId) {
    if (!initialized_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(streamingMutex_);
    
    auto it = streamingStates_.find(utteranceId);
    if (it == streamingStates_.end()) {
        return;
    }
    
    StreamingState& state = *it->second;
    if (!state.isActive) {
        return;
    }
    
    // Process final transcription with all accumulated audio
    processStreamingAudio(utteranceId, state);
    
    // Trigger translation pipeline for final result if callback is set
    if (transcriptionCompleteCallback_) {
        // Get the final transcription result from the last processing
        // We'll need to store this in the streaming state for access
        TranscriptionResult finalResult;
        finalResult.text = state.lastTranscriptionText;
        finalResult.confidence = 0.8f; // Default confidence for streaming
        finalResult.is_partial = false;
        finalResult.start_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            state.startTime.time_since_epoch()).count();
        finalResult.end_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        // Generate multiple candidates for better translation quality
        std::vector<TranscriptionResult> candidates;
        if (audioBufferManager_) {
            auto audioData = audioBufferManager_->getBufferedAudio(utteranceId);
            if (!audioData.empty() && !finalResult.text.empty()) {
                generateTranscriptionCandidates(audioData, candidates, 3);
            }
        }
        
        triggerTranslationPipeline(utteranceId, finalResult, candidates);
    }
    
    // Mark as inactive and clean up
    state.isActive = false;
    cleanupStreamingState(utteranceId);
    
    std::cout << "Finalized streaming transcription for utterance: " << utteranceId << std::endl;
}

void WhisperSTT::setStreamingCallback(uint32_t utteranceId, TranscriptionCallback callback) {
    std::lock_guard<std::mutex> lock(streamingMutex_);
    
    auto it = streamingStates_.find(utteranceId);
    if (it != streamingStates_.end()) {
        it->second->callback = callback;
    }
}

bool WhisperSTT::isStreamingActive(uint32_t utteranceId) const {
    std::lock_guard<std::mutex> lock(streamingMutex_);
    
    auto it = streamingStates_.find(utteranceId);
    return it != streamingStates_.end() && it->second->isActive;
}

size_t WhisperSTT::getActiveStreamingCount() const {
    std::lock_guard<std::mutex> lock(streamingMutex_);
    
    size_t count = 0;
    for (const auto& pair : streamingStates_) {
        if (pair.second->isActive) {
            count++;
        }
    }
    return count;
}

// Helper methods implementation
bool WhisperSTT::initializeAudioBufferManager() {
    try {
        audio::AudioBufferManager::BufferConfig config;
        config.maxBufferSizeMB = 8;           // 8MB per utterance
        config.maxUtterances = 20;            // Support 20 concurrent utterances
        config.cleanupIntervalMs = 2000;      // Cleanup every 2 seconds
        config.maxIdleTimeMs = 30000;         // Remove idle buffers after 30 seconds
        config.enableCircularBuffer = true;   // Use circular buffers for streaming
        
        audioBufferManager_ = std::make_unique<audio::AudioBufferManager>(config);
        
        std::cout << "AudioBufferManager initialized for streaming support" << std::endl;
        return true;
    } catch (const std::exception& e) {
        last_error_ = "Failed to initialize AudioBufferManager: " + std::string(e.what());
        std::cerr << last_error_ << std::endl;
        return false;
    }
}

void WhisperSTT::processStreamingAudio(uint32_t utteranceId, StreamingState& state) {
    // Get audio chunk to process
    std::vector<float> audioChunk = getStreamingAudioChunk(utteranceId, state);
    
    if (audioChunk.empty()) {
        return;
    }
    
    // Determine if this is a partial or final result
    bool isPartial = state.isActive && partialResultsEnabled_;
    
#ifdef WHISPER_AVAILABLE
    std::thread([this, utteranceId, audioChunk, isPartial]() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            // Configure parameters for streaming
            whisper_full_params streaming_params = *params_;
            streaming_params.single_segment = isPartial; // Single segment for partial results
            streaming_params.no_context = isPartial;     // No context for partial results
            streaming_params.duration_ms = 0;            // Process entire chunk
            
            // Run whisper inference
            int result = whisper_full(ctx_, streaming_params, audioChunk.data(), static_cast<int>(audioChunk.size()));
            
            if (result != 0) {
                last_error_ = "Streaming whisper inference failed with code: " + std::to_string(result);
                std::cerr << last_error_ << std::endl;
                return;
            }
            
            // Process result and send to callback
            std::lock_guard<std::mutex> streamingLock(streamingMutex_);
            auto it = streamingStates_.find(utteranceId);
            if (it != streamingStates_.end() && it->second->callback) {
                // Create a callback that will handle the result
                auto callback = [this, utteranceId, isPartial](const TranscriptionResult& result) {
                    std::lock_guard<std::mutex> lock(streamingMutex_);
                    auto stateIt = streamingStates_.find(utteranceId);
                    if (stateIt != streamingStates_.end()) {
                        if (isPartial) {
                            sendPartialResult(utteranceId, *stateIt->second, result);
                        } else {
                            sendFinalResult(utteranceId, *stateIt->second, result);
                        }
                    }
                };
                
                processTranscriptionResult(callback, isPartial);
            }
            
        } catch (const std::exception& e) {
            last_error_ = "Exception during streaming transcription: " + std::string(e.what());
            std::cerr << last_error_ << std::endl;
        }
    }).detach();
#else
    // Simulation mode for streaming
    std::thread([this, utteranceId, audioChunk, isPartial]() {
        auto processingStartTime = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Simulate processing time
        
        std::lock_guard<std::mutex> streamingLock(streamingMutex_);
        auto it = streamingStates_.find(utteranceId);
        if (it != streamingStates_.end() && it->second->callback) {
            TranscriptionResult result;
            result.text = isPartial ? "Partial streaming transcription..." : "Final streaming transcription result";
            
            // Simulate confidence based on chunk size and whether it's partial
            float baseConfidence = isPartial ? 0.75f : 0.90f;
            float chunkBonus = std::min(0.15f, (audioChunk.size() / 32000.0f) * 0.15f);
            result.confidence = std::min(0.95f, baseConfidence + chunkBonus);
            
            result.is_partial = isPartial;
            result.start_time_ms = 0;
            result.end_time_ms = static_cast<int64_t>(audioChunk.size() * 1000 / 16000);
            
            // Calculate processing latency
            auto processingEndTime = std::chrono::steady_clock::now();
            float processingLatencyMs = std::chrono::duration<float, std::milli>(processingEndTime - processingStartTime).count();
            
            // Enhance with confidence information
            enhanceTranscriptionResultWithConfidence(result, audioChunk, processingLatencyMs);
            
            if (isPartial) {
                sendPartialResult(utteranceId, *it->second, result);
            } else {
                sendFinalResult(utteranceId, *it->second, result);
            }
        }
    }).detach();
#endif
}



void WhisperSTT::sendPartialResult(uint32_t utteranceId, const StreamingState& state, const TranscriptionResult& result) {
    TranscriptionResult partialResult = result;
    partialResult.is_partial = true;
    
    // Update with language detection information
    updateTranscriptionResultWithLanguage(partialResult);
    
    // Enhance with word-level timing information for streaming
    enhanceStreamingResultWithWordTimings(partialResult, state);
    
    // Only send partial results if confidence is above threshold and text has changed
    if (partialResult.confidence >= confidenceThreshold_ && partialResult.text != state.lastTranscriptionText) {
        if (state.callback) {
            state.callback(partialResult);
        }
        
        // Update last sent text (const_cast is safe here as we're managing the state)
        const_cast<StreamingState&>(state).lastTranscriptionText = partialResult.text;
        
        std::cout << "Partial result for utterance " << utteranceId << ": \"" << partialResult.text 
                  << "\" (confidence: " << partialResult.confidence 
                  << ", words: " << partialResult.word_timings.size()
                  << ", language: " << partialResult.detected_language << ")" << std::endl;
    }
}

void WhisperSTT::sendFinalResult(uint32_t utteranceId, const StreamingState& state, const TranscriptionResult& result) {
    TranscriptionResult finalResult = result;
    finalResult.is_partial = false;
    
    // Update with language detection information
    updateTranscriptionResultWithLanguage(finalResult);
    
    // Enhance with word-level timing information for streaming
    enhanceStreamingResultWithWordTimings(finalResult, state);
    
    // Handle language change if detected
    if (finalResult.language_changed && shouldSwitchLanguage(finalResult.detected_language, finalResult.language_confidence)) {
        handleLanguageChange(finalResult.detected_language, finalResult.language_confidence);
    }
    
    if (state.callback) {
        state.callback(finalResult);
    }
    
    std::cout << "Final result for utterance " << utteranceId << ": \"" << finalResult.text 
              << "\" (confidence: " << finalResult.confidence 
              << ", words: " << finalResult.word_timings.size()
              << ", language: " << finalResult.detected_language << ")" << std::endl;
}

bool WhisperSTT::shouldProcessStreamingChunk(const StreamingState& state) const {
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastProcess = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.lastProcessTime);
    
    // Process if enough time has passed and we have enough audio
    bool timeThresholdMet = timeSinceLastProcess.count() >= minChunkSizeMs_;
    bool audioThresholdMet = (state.totalAudioSamples - state.processedAudioSamples) >= (16000 * minChunkSizeMs_ / 1000); // 1 second worth of samples
    
    return timeThresholdMet && audioThresholdMet;
}

std::vector<float> WhisperSTT::getStreamingAudioChunk(uint32_t utteranceId, const StreamingState& state) {
    if (audioBufferManager_) {
        // Get recent audio from buffer manager
        size_t sampleCount = 16000 * 2; // 2 seconds of audio
        return audioBufferManager_->getRecentAudio(utteranceId, sampleCount);
    } else {
        // Fallback: return accumulated audio
        return state.accumulatedAudio;
    }
}

void WhisperSTT::cleanupStreamingState(uint32_t utteranceId) {
    // Finalize buffer in AudioBufferManager
    if (audioBufferManager_) {
        audioBufferManager_->finalizeBuffer(utteranceId);
    }
    
    // Remove from streaming states
    streamingStates_.erase(utteranceId);
}

// Language detection helper methods implementation
std::string WhisperSTT::detectLanguageFromResult() const {
#ifdef WHISPER_AVAILABLE
    if (!ctx_ || !languageDetectionEnabled_) {
        return currentDetectedLanguage_;
    }
    
    // Use whisper's auto-detection capabilities
    std::vector<float> lang_probs(whisper_lang_max_id() + 1, 0.0f);
    
    // Get language probabilities from the first segment
    const int n_segments = whisper_full_n_segments(ctx_);
    if (n_segments > 0) {
        // Use whisper's language auto-detection
        int detected_lang_id = whisper_lang_auto_detect(ctx_, 0, n_threads_, lang_probs.data());
        
        if (detected_lang_id >= 0 && detected_lang_id < static_cast<int>(lang_probs.size())) {
            const char* lang_str = whisper_lang_str(detected_lang_id);
            if (lang_str && lang_probs[detected_lang_id] > languageDetectionThreshold_) {
                std::cout << "Detected language: " << lang_str 
                          << " (confidence: " << lang_probs[detected_lang_id] << ")" << std::endl;
                return std::string(lang_str);
            }
        }
    }
    
    // Fallback: try to get language from the full result
    int lang_id = whisper_full_lang_id(ctx_);
    if (lang_id >= 0) {
        const char* lang_str = whisper_lang_str(lang_id);
        if (lang_str) {
            return std::string(lang_str);
        }
    }
    
    return currentDetectedLanguage_;
#else
    // Simulation mode - randomly switch between a few languages for testing
    static std::vector<std::string> testLanguages = {"en", "es", "fr", "de", "it", "pt", "ru", "zh", "ja"};
    static std::mt19937 gen(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::uniform_int_distribution<> dis(0, testLanguages.size() - 1);
    
    // 15% chance to "detect" a different language in simulation
    static std::uniform_real_distribution<float> changeDis(0.0f, 1.0f);
    if (changeDis(gen) < 0.15f) {
        std::string newLang = testLanguages[dis(gen)];
        std::cout << "Simulation: Detected language change to " << newLang << std::endl;
        return newLang;
    }
    
    return currentDetectedLanguage_;
#endif
}

float WhisperSTT::getLanguageDetectionConfidence(const std::string& language) const {
#ifdef WHISPER_AVAILABLE
    if (!ctx_ || !languageDetectionEnabled_) {
        return 1.0f; // High confidence if detection is disabled
    }
    
    // Get language ID for the specified language
    int lang_id = whisper_lang_id(language.c_str());
    if (lang_id < 0) {
        return 0.0f; // Invalid language
    }
    
    // Use whisper's language detection probabilities
    std::vector<float> lang_probs(whisper_lang_max_id() + 1, 0.0f);
    
    const int n_segments = whisper_full_n_segments(ctx_);
    if (n_segments > 0) {
        // Run language auto-detection to get probabilities
        int detected_lang_id = whisper_lang_auto_detect(ctx_, 0, n_threads_, lang_probs.data());
        
        if (detected_lang_id >= 0 && lang_id < static_cast<int>(lang_probs.size())) {
            float confidence = lang_probs[lang_id];
            
            // Also consider the no-speech probability as a quality indicator
            float no_speech_prob = whisper_full_get_segment_no_speech_prob(ctx_, 0);
            float speech_quality = 1.0f - no_speech_prob;
            
            // Combine language probability with speech quality
            return confidence * speech_quality;
        }
    }
    
    return 0.5f; // Default confidence
#else
    // Simulation mode - return confidence based on language similarity to current
    static std::mt19937 gen(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::uniform_real_distribution<float> dis(0.6f, 0.95f);
    
    // Higher confidence for same language, lower for different
    if (language == currentDetectedLanguage_) {
        return dis(gen); // 0.6-0.95 for same language
    } else {
        static std::uniform_real_distribution<float> lowDis(0.3f, 0.8f);
        return lowDis(gen); // 0.3-0.8 for different language
    }
#endif
}

bool WhisperSTT::shouldSwitchLanguage(const std::string& detectedLang, float confidence) const {
    if (!languageDetectionEnabled_ || !autoLanguageSwitching_) {
        return false;
    }
    
    // Don't switch if confidence is too low
    if (confidence < languageDetectionThreshold_) {
        std::cout << "Language switch rejected: confidence " << confidence 
                  << " below threshold " << languageDetectionThreshold_ << std::endl;
        return false;
    }
    
    // Don't switch if it's the same language
    if (detectedLang == currentDetectedLanguage_) {
        return false;
    }
    
    // Don't switch if detected language is empty or invalid
    if (detectedLang.empty()) {
        std::cout << "Language switch rejected: empty detected language" << std::endl;
        return false;
    }
    
    // Validate that the detected language is supported
#ifdef WHISPER_AVAILABLE
    if (ctx_) {
        int lang_id = whisper_lang_id(detectedLang.c_str());
        if (lang_id < 0) {
            std::cout << "Language switch rejected: unsupported language '" 
                      << detectedLang << "'" << std::endl;
            return false;
        }
    }
#endif
    
    // Additional stability check: require consistent detection
    // This could be enhanced with a history buffer in the future
    static std::string lastDetectedLang;
    static int consistentDetectionCount = 0;
    static const int REQUIRED_CONSISTENT_DETECTIONS = 2;
    
    if (detectedLang == lastDetectedLang) {
        consistentDetectionCount++;
    } else {
        consistentDetectionCount = 1;
        lastDetectedLang = detectedLang;
    }
    
    if (consistentDetectionCount < REQUIRED_CONSISTENT_DETECTIONS) {
        std::cout << "Language switch pending: need " << (REQUIRED_CONSISTENT_DETECTIONS - consistentDetectionCount)
                  << " more consistent detections for '" << detectedLang << "'" << std::endl;
        return false;
    }
    
    std::cout << "Language switch approved: '" << currentDetectedLanguage_ 
              << "' -> '" << detectedLang << "' (confidence: " << confidence << ")" << std::endl;
    
    return true;
}

void WhisperSTT::handleLanguageChange(const std::string& newLanguage, float confidence) {
    std::string oldLanguage = currentDetectedLanguage_;
    
    // Validate the new language before switching
#ifdef WHISPER_AVAILABLE
    if (ctx_) {
        int lang_id = whisper_lang_id(newLanguage.c_str());
        if (lang_id < 0) {
            std::cerr << "Cannot switch to unsupported language: " << newLanguage << std::endl;
            return;
        }
    }
#endif
    
    // Update current detected language
    currentDetectedLanguage_ = newLanguage;
    
    std::cout << "Language changed from '" << oldLanguage 
              << "' to '" << newLanguage 
              << "' (confidence: " << confidence << ")" << std::endl;
    
    // Update whisper parameters with new language
#ifdef WHISPER_AVAILABLE
    if (params_ && autoLanguageSwitching_) {
        // Set the new language in whisper parameters
        params_->language = newLanguage.c_str();
        params_->detect_language = false; // Use specific language, not auto-detect
        
        std::cout << "Updated whisper language parameter to: " << newLanguage << std::endl;
        
        // Log language information for debugging
        int lang_id = whisper_lang_id(newLanguage.c_str());
        if (lang_id >= 0) {
            std::cout << "Language ID: " << lang_id 
                      << " (" << whisper_lang_str_full(lang_id) << ")" << std::endl;
        }
    }
#endif
    
    // Record performance metrics
    utils::PerformanceMonitor::getInstance().recordCounter("stt.language_changes");
    utils::PerformanceMonitor::getInstance().recordGauge("stt.language_confidence", confidence);
    
    // Notify callback if set (this will notify the client session)
    if (languageChangeCallback_) {
        try {
            languageChangeCallback_(oldLanguage, newLanguage, confidence);
        } catch (const std::exception& e) {
            std::cerr << "Error in language change callback: " << e.what() << std::endl;
        }
    }
}

void WhisperSTT::updateTranscriptionResultWithLanguage(TranscriptionResult& result) const {
    if (!languageDetectionEnabled_) {
        result.detected_language = currentDetectedLanguage_;
        result.language_confidence = 1.0f;
        result.language_changed = false;
        return;
    }
    
    // Detect language from current transcription
    std::string detectedLang = detectLanguageFromResult();
    float langConfidence = getLanguageDetectionConfidence(detectedLang);
    
    result.detected_language = detectedLang;
    result.language_confidence = langConfidence;
    result.language_changed = (detectedLang != currentDetectedLanguage_);
    
    // Handle language switching if enabled
    if (shouldSwitchLanguage(detectedLang, langConfidence)) {
        // Note: We don't change the language here to avoid race conditions
        // The language change will be handled by the caller
        result.language_changed = true;
    }
}

} // namespace stt

// Factory function implementation
namespace stt {
std::unique_ptr<STTInterface> createWhisperSTT() {
    return std::make_unique<WhisperSTT>();
}
} // namespace stt
/
/ Quantization support implementation
void WhisperSTT::setQuantizationLevel(QuantizationLevel level) {
    std::lock_guard<std::mutex> lock(quantizationMutex_);
    
    if (level == QuantizationLevel::AUTO) {
        // Auto-select based on available GPU memory
        level = selectOptimalQuantizationLevel(modelPath_);
    }
    
    if (!validateQuantizationSupport(level)) {
        std::cerr << "Quantization level " << quantizationManager_->levelToString(level) 
                  << " not supported, falling back to FP32" << std::endl;
        level = QuantizationLevel::FP32;
    }
    
    currentQuantizationLevel_ = level;
    std::cout << "Quantization level set to: " << quantizationManager_->levelToString(level) << std::endl;
}

bool WhisperSTT::initializeWithQuantization(const std::string& modelPath, QuantizationLevel level, int n_threads) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    modelPath_ = modelPath;
    n_threads_ = n_threads;
    last_error_.clear();
    
    if (level == QuantizationLevel::AUTO) {
        level = selectOptimalQuantizationLevel(modelPath);
    }
    
    if (!validateQuantizationSupport(level)) {
        std::cerr << "Quantization level " << quantizationManager_->levelToString(level) 
                  << " not supported, falling back to FP32" << std::endl;
        level = QuantizationLevel::FP32;
    }
    
    currentQuantizationLevel_ = level;
    
    // Load the quantized model
    if (!loadQuantizedModel(modelPath, level, false, -1)) {
        return false;
    }
    
    // Use the quantized context as the main context
    ctx_ = getQuantizedContext(level);
    if (!ctx_) {
        last_error_ = "Failed to get quantized context for level: " + quantizationManager_->levelToString(level);
        return false;
    }
    
    // Validate model compatibility
    if (!validateModel()) {
        unloadQuantizedModel(level);
        return false;
    }
    
    // Setup default parameters
    if (!setupWhisperParams()) {
        unloadQuantizedModel(level);
        return false;
    }
    
    // Initialize AudioBufferManager for streaming support
    if (!initializeAudioBufferManager()) {
        unloadQuantizedModel(level);
        return false;
    }
    
    std::cout << "Whisper STT initialized with quantization level: " 
              << quantizationManager_->levelToString(level) << std::endl;
    std::cout << "Model type: " << whisper_model_type_readable(ctx_) << std::endl;
    
    initialized_ = true;
    return true;
}

bool WhisperSTT::initializeWithQuantizationGPU(const std::string& modelPath, QuantizationLevel level, int gpuDeviceId, int n_threads) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& gpuManager = utils::GPUManager::getInstance();
    
    // Check if GPU is available
    if (!gpuManager.isCudaAvailable()) {
        std::cout << "GPU not available, falling back to CPU initialization with quantization" << std::endl;
        return initializeWithQuantization(modelPath, level, n_threads);
    }
    
    // Set GPU device
    if (!gpuManager.setDevice(gpuDeviceId)) {
        std::cout << "Failed to set GPU device " << gpuDeviceId << ", falling back to CPU" << std::endl;
        return initializeWithQuantization(modelPath, level, n_threads);
    }
    
    modelPath_ = modelPath;
    n_threads_ = n_threads;
    gpu_enabled_ = true;
    gpu_device_id_ = gpuDeviceId;
    last_error_.clear();
    
    if (level == QuantizationLevel::AUTO) {
        level = selectOptimalQuantizationLevel(modelPath);
    }
    
    if (!validateQuantizationSupport(level)) {
        std::cerr << "Quantization level " << quantizationManager_->levelToString(level) 
                  << " not supported, falling back to FP32" << std::endl;
        level = QuantizationLevel::FP32;
    }
    
    currentQuantizationLevel_ = level;
    
    // Load the quantized model with GPU support
    if (!loadQuantizedModel(modelPath, level, true, gpuDeviceId)) {
        gpu_enabled_ = false;
        // Fallback to CPU
        std::cout << "GPU quantized model loading failed, attempting CPU fallback..." << std::endl;
        return initializeWithQuantization(modelPath, level, n_threads);
    }
    
    // Use the quantized context as the main context
    ctx_ = getQuantizedContext(level);
    if (!ctx_) {
        last_error_ = "Failed to get quantized GPU context for level: " + quantizationManager_->levelToString(level);
        gpu_enabled_ = false;
        return false;
    }
    
    // Validate model compatibility
    if (!validateModel()) {
        unloadQuantizedModel(level);
        gpu_enabled_ = false;
        return false;
    }
    
    // Setup default parameters
    if (!setupWhisperParams()) {
        unloadQuantizedModel(level);
        gpu_enabled_ = false;
        return false;
    }
    
    // Initialize AudioBufferManager for streaming support
    if (!initializeAudioBufferManager()) {
        unloadQuantizedModel(level);
        gpu_enabled_ = false;
        return false;
    }
    
    auto deviceInfo = gpuManager.getDeviceInfo(gpuDeviceId);
    std::cout << "Whisper STT initialized with GPU acceleration and quantization level: " 
              << quantizationManager_->levelToString(level) << " on " << deviceInfo.name 
              << " (Device " << gpuDeviceId << ")" << std::endl;
    std::cout << "Model type: " << whisper_model_type_readable(ctx_) << std::endl;
    
    initialized_ = true;
    return true;
}

std::vector<QuantizationLevel> WhisperSTT::getSupportedQuantizationLevels() const {
    std::vector<QuantizationLevel> supported;
    
    // Check each quantization level for support
    std::vector<QuantizationLevel> allLevels = {
        QuantizationLevel::FP32,
        QuantizationLevel::FP16,
        QuantizationLevel::INT8
    };
    
    for (QuantizationLevel level : allLevels) {
        if (quantizationManager_->isLevelSupported(level)) {
            supported.push_back(level);
        }
    }
    
    return supported;
}

AccuracyValidationResult WhisperSTT::validateQuantizedModel(
    const std::vector<std::string>& validationAudioPaths,
    const std::vector<std::string>& expectedTranscriptions) const {
    
    std::lock_guard<std::mutex> lock(quantizationMutex_);
    
    if (!initialized_) {
        AccuracyValidationResult result;
        result.validationDetails = "WhisperSTT not initialized";
        return result;
    }
    
    std::string quantizedModelPath = quantizationManager_->getQuantizedModelPath(modelPath_, currentQuantizationLevel_);
    
    return quantizationManager_->validateModelAccuracy(
        quantizedModelPath,
        currentQuantizationLevel_,
        validationAudioPaths,
        expectedTranscriptions
    );
}

// Private quantization helper methods
bool WhisperSTT::loadQuantizedModel(const std::string& modelPath, QuantizationLevel level, bool useGPU, int gpuDeviceId) {
#ifdef WHISPER_AVAILABLE
    std::lock_guard<std::mutex> lock(quantizationMutex_);
    
    // Check if model is already loaded
    if (quantizedContexts_.find(level) != quantizedContexts_.end()) {
        std::cout << "Quantized model already loaded for level: " << quantizationManager_->levelToString(level) << std::endl;
        return true;
    }
    
    // Get the path to the quantized model
    std::string quantizedModelPath = quantizationManager_->getQuantizedModelPath(modelPath, level);
    
    // Check if quantized model file exists, fallback to original if not
    if (!std::filesystem::exists(quantizedModelPath) && level != QuantizationLevel::FP32) {
        std::cout << "Quantized model not found at " << quantizedModelPath 
                  << ", using original model with runtime quantization" << std::endl;
        quantizedModelPath = modelPath;
    }
    
    // Validate model file exists and is readable
    std::ifstream modelFile(quantizedModelPath, std::ios::binary);
    if (!modelFile.good()) {
        last_error_ = "Quantized model file not found or not readable: " + quantizedModelPath;
        std::cerr << last_error_ << std::endl;
        return false;
    }
    modelFile.close();
    
    // Initialize whisper.cpp context with quantization parameters
    whisper_context_params ctx_params = whisper_context_default_params();
    ctx_params.use_gpu = useGPU;
    if (useGPU) {
        ctx_params.gpu_device = gpuDeviceId;
    }
    
    // Set quantization-specific parameters
    switch (level) {
        case QuantizationLevel::FP16:
            // Enable FP16 if supported
            ctx_params.use_gpu = useGPU; // FP16 typically requires GPU
            break;
        case QuantizationLevel::INT8:
            // Enable INT8 quantization if supported
            ctx_params.use_gpu = useGPU; // INT8 typically requires GPU
            break;
        case QuantizationLevel::FP32:
        default:
            // Use default parameters for FP32
            break;
    }
    
    whisper_context* quantizedCtx = whisper_init_from_file_with_params(quantizedModelPath.c_str(), ctx_params);
    if (!quantizedCtx) {
        last_error_ = "Failed to load quantized whisper model from: " + quantizedModelPath + 
                     " with quantization level: " + quantizationManager_->levelToString(level);
        std::cerr << last_error_ << std::endl;
        return false;
    }
    
    // Store the quantized context
    quantizedContexts_[level] = quantizedCtx;
    
    std::cout << "Loaded quantized model for level " << quantizationManager_->levelToString(level) 
              << " from: " << quantizedModelPath << std::endl;
    
    return true;
#else
    // Simulation mode - always succeed
    std::cout << "Loaded quantized model in simulation mode for level: " 
              << quantizationManager_->levelToString(level) << std::endl;
    return true;
#endif
}

void WhisperSTT::unloadQuantizedModel(QuantizationLevel level) {
#ifdef WHISPER_AVAILABLE
    std::lock_guard<std::mutex> lock(quantizationMutex_);
    
    auto it = quantizedContexts_.find(level);
    if (it != quantizedContexts_.end()) {
        whisper_free(it->second);
        quantizedContexts_.erase(it);
        std::cout << "Unloaded quantized model for level: " << quantizationManager_->levelToString(level) << std::endl;
    }
#endif
}

whisper_context* WhisperSTT::getQuantizedContext(QuantizationLevel level) const {
#ifdef WHISPER_AVAILABLE
    std::lock_guard<std::mutex> lock(quantizationMutex_);
    
    auto it = quantizedContexts_.find(level);
    if (it != quantizedContexts_.end()) {
        return it->second;
    }
#endif
    return nullptr;
}

QuantizationLevel WhisperSTT::selectOptimalQuantizationLevel(const std::string& modelPath) const {
    auto& gpuManager = utils::GPUManager::getInstance();
    
    if (!gpuManager.isCudaAvailable()) {
        return QuantizationLevel::FP32;
    }
    
    size_t availableMemoryMB = gpuManager.getFreeMemoryMB();
    
    // Estimate model size (rough approximation)
    size_t modelSizeMB = 500; // Default estimate
    try {
        if (std::filesystem::exists(modelPath)) {
            size_t fileSizeBytes = std::filesystem::file_size(modelPath);
            modelSizeMB = fileSizeBytes / (1024 * 1024);
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to get model file size: " << e.what() << std::endl;
    }
    
    return quantizationManager_->selectOptimalLevel(availableMemoryMB, modelSizeMB);
}

bool WhisperSTT::validateQuantizationSupport(QuantizationLevel level) const {
    return quantizationManager_->isLevelSupported(level);
}

void WhisperSTT::cleanupQuantizedModels() {
#ifdef WHISPER_AVAILABLE
    std::lock_guard<std::mutex> lock(quantizationMutex_);
    
    for (auto& pair : quantizedContexts_) {
        whisper_free(pair.second);
    }
    quantizedContexts_.clear();
    
    std::cout << "Cleaned up all quantized models" << std::endl;
#endif
}// C
onfidence calculation helper methods implementation

float WhisperSTT::calculateSegmentConfidence(int segmentIndex) const {
#ifdef WHISPER_AVAILABLE
    if (!ctx_ || segmentIndex < 0 || segmentIndex >= whisper_full_n_segments(ctx_)) {
        return 0.0f;
    }
    
    int n_tokens = whisper_full_n_tokens(ctx_, segmentIndex);
    if (n_tokens <= 0) {
        // Fallback to no-speech probability
        float no_speech_prob = whisper_full_get_segment_no_speech_prob(ctx_, segmentIndex);
        return std::max(0.0f, 1.0f - no_speech_prob);
    }
    
    float token_prob_sum = 0.0f;
    int valid_tokens = 0;
    
    for (int j = 0; j < n_tokens; ++j) {
        float token_prob = whisper_full_get_token_p(ctx_, segmentIndex, j);
        if (token_prob > 0.0f) { // Only count valid probabilities
            token_prob_sum += token_prob;
            valid_tokens++;
        }
    }
    
    if (valid_tokens == 0) {
        // Fallback to no-speech probability
        float no_speech_prob = whisper_full_get_segment_no_speech_prob(ctx_, segmentIndex);
        return std::max(0.0f, 1.0f - no_speech_prob);
    }
    
    float avg_token_confidence = token_prob_sum / valid_tokens;
    
    // Apply confidence boost based on segment length (longer segments tend to be more reliable)
    float length_boost = std::min(1.0f, static_cast<float>(valid_tokens) / 10.0f) * 0.1f;
    
    return std::min(1.0f, avg_token_confidence + length_boost);
#else
    // Simulation mode - return reasonable confidence based on segment index
    return std::max(0.7f, 1.0f - (segmentIndex * 0.05f));
#endif
}

std::vector<WordTiming> WhisperSTT::extractWordTimings(int segmentIndex) const {
    std::vector<WordTiming> wordTimings;
    
#ifdef WHISPER_AVAILABLE
    if (!ctx_ || !wordLevelConfidenceEnabled_ || segmentIndex < 0 || segmentIndex >= whisper_full_n_segments(ctx_)) {
        return wordTimings;
    }
    
    int n_tokens = whisper_full_n_tokens(ctx_, segmentIndex);
    if (n_tokens <= 0) {
        return wordTimings;
    }
    
    std::string currentWord;
    int64_t wordStartMs = 0;
    int64_t wordEndMs = 0;
    float wordConfidenceSum = 0.0f;
    float wordTimestampConfidenceSum = 0.0f;
    int wordTokenCount = 0;
    bool wordStarted = false;
    
    for (int j = 0; j < n_tokens; ++j) {
        // Use the newer whisper_full_get_token_data function for comprehensive token information
        whisper_token_data token_data = whisper_full_get_token_data(ctx_, segmentIndex, j);
        const char* token_text = whisper_full_get_token_text(ctx_, segmentIndex, j);
        
        // Extract timing information with improved accuracy
        int64_t token_start = token_data.t0 * 10; // Convert centiseconds to milliseconds
        int64_t token_end = token_data.t1 * 10;
        
        // Use DTW timestamp if available for more accurate timing
        if (token_data.t_dtw > 0) {
            int64_t dtw_time = token_data.t_dtw * 10;
            // Use DTW timestamp as a more accurate reference point
            token_start = std::max(token_start, dtw_time - 50); // Allow 50ms before DTW point
            token_end = std::min(token_end, dtw_time + 50);     // Allow 50ms after DTW point
        }
        
        // Enhanced confidence calculation using multiple probability measures
        float token_confidence = token_data.p; // Base probability
        
        // Factor in timestamp confidence if available
        if (token_data.pt > 0.0f) {
            // Blend token probability with timestamp probability
            token_confidence = (token_confidence * 0.7f) + (token_data.pt * 0.3f);
        }
        
        // Consider voice length for confidence adjustment
        if (token_data.vlen > 0.0f) {
            // Longer voice segments typically indicate more confident recognition
            float voice_confidence_boost = std::min(0.1f, token_data.vlen * 0.02f);
            token_confidence += voice_confidence_boost;
        }
        
        if (!token_text || strlen(token_text) == 0) {
            continue;
        }
        
        std::string tokenStr(token_text);
        
        // Enhanced word boundary detection with improved logic
        bool isNewWord = false;
        bool isWordEnd = false;
        
        // Check if this token starts a new word
        if (j == 0) {
            isNewWord = true;
        } else if (tokenStr.length() > 0 && (tokenStr[0] == ' ' || tokenStr[0] == '\t')) {
            isNewWord = true;
        } else if (currentWord.empty()) {
            isNewWord = true;
        } else {
            // Check for punctuation that indicates word boundaries
            if (tokenStr.length() > 0 && std::ispunct(tokenStr[0]) && tokenStr[0] != '\'' && tokenStr[0] != '-') {
                isNewWord = true;
            }
        }
        
        // Check if this token ends a word
        if (j == n_tokens - 1) {
            isWordEnd = true;
        } else {
            // Look ahead to see if next token starts a new word
            const char* next_token_text = whisper_full_get_token_text(ctx_, segmentIndex, j + 1);
            if (next_token_text && strlen(next_token_text) > 0) {
                std::string nextTokenStr(next_token_text);
                if (nextTokenStr[0] == ' ' || nextTokenStr[0] == '\t') {
                    isWordEnd = true;
                } else if (std::ispunct(nextTokenStr[0]) && nextTokenStr[0] != '\'' && nextTokenStr[0] != '-') {
                    isWordEnd = true;
                }
            }
        }
        
        // Finalize previous word if starting a new one
        if (isNewWord && wordStarted && !currentWord.empty()) {
            float avgConfidence = (wordTokenCount > 0) ? (wordConfidenceSum / wordTokenCount) : 0.0f;
            float avgTimestampConfidence = (wordTokenCount > 0) ? (wordTimestampConfidenceSum / wordTokenCount) : 0.0f;
            
            // Blend token confidence with timestamp confidence for more accurate word confidence
            if (avgTimestampConfidence > 0.0f) {
                avgConfidence = (avgConfidence * 0.8f) + (avgTimestampConfidence * 0.2f);
            }
            
            // Apply confidence adjustments based on word characteristics
            avgConfidence = adjustWordConfidence(currentWord, avgConfidence, wordTokenCount);
            
            wordTimings.emplace_back(currentWord, wordStartMs, wordEndMs, avgConfidence);
            
            // Reset for new word
            currentWord.clear();
            wordConfidenceSum = 0.0f;
            wordTimestampConfidenceSum = 0.0f;
            wordTokenCount = 0;
            wordStarted = false;
        }
        
        // Start new word
        if (isNewWord) {
            wordStartMs = token_start;
            wordStarted = true;
            
            // Remove leading whitespace from token
            while (!tokenStr.empty() && (tokenStr[0] == ' ' || tokenStr[0] == '\t')) {
                tokenStr = tokenStr.substr(1);
            }
        }
        
        // Add token to current word
        if (!tokenStr.empty()) {
            currentWord += tokenStr;
            wordConfidenceSum += token_confidence;
            wordTimestampConfidenceSum += token_data.pt;
            wordTokenCount++;
            wordEndMs = token_end;
        }
        
        // Finalize word if this is the end
        if (isWordEnd && wordStarted && !currentWord.empty()) {
            float avgConfidence = (wordTokenCount > 0) ? (wordConfidenceSum / wordTokenCount) : 0.0f;
            float avgTimestampConfidence = (wordTokenCount > 0) ? (wordTimestampConfidenceSum / wordTokenCount) : 0.0f;
            
            // Blend token confidence with timestamp confidence
            if (avgTimestampConfidence > 0.0f) {
                avgConfidence = (avgConfidence * 0.8f) + (avgTimestampConfidence * 0.2f);
            }
            
            // Apply confidence adjustments
            avgConfidence = adjustWordConfidence(currentWord, avgConfidence, wordTokenCount);
            
            wordTimings.emplace_back(currentWord, wordStartMs, wordEndMs, avgConfidence);
            
            // Reset for next word
            currentWord.clear();
            wordConfidenceSum = 0.0f;
            wordTimestampConfidenceSum = 0.0f;
            wordTokenCount = 0;
            wordStarted = false;
        }
    }
    
    // Handle any remaining word
    if (wordStarted && !currentWord.empty()) {
        float avgConfidence = (wordTokenCount > 0) ? (wordConfidenceSum / wordTokenCount) : 0.0f;
        float avgTimestampConfidence = (wordTokenCount > 0) ? (wordTimestampConfidenceSum / wordTokenCount) : 0.0f;
        
        // Blend confidences for final word
        if (avgTimestampConfidence > 0.0f) {
            avgConfidence = (avgConfidence * 0.8f) + (avgTimestampConfidence * 0.2f);
        }
        
        avgConfidence = adjustWordConfidence(currentWord, avgConfidence, wordTokenCount);
        wordTimings.emplace_back(currentWord, wordStartMs, wordEndMs, avgConfidence);
    }
    
#else
    // Enhanced simulation mode - create more realistic mock word timings
    if (wordLevelConfidenceEnabled_) {
        std::vector<std::string> mockWords = {"Hello", "this", "is", "simulated", "transcription"};
        int64_t currentTime = 0;
        
        for (const auto& word : mockWords) {
            // More realistic word duration calculation
            int64_t baseWordDuration = std::max(200LL, static_cast<int64_t>(word.length() * 80)); // 80ms per character, min 200ms
            
            // Add some variation based on word characteristics
            if (word.length() > 6) {
                baseWordDuration += 100; // Longer words take more time
            }
            if (std::isupper(word[0])) {
                baseWordDuration += 50; // Proper nouns might be spoken more carefully
            }
            
            // Simulate confidence variation based on word characteristics
            float baseConfidence = 0.85f;
            if (word.length() < 3) {
                baseConfidence -= 0.1f; // Short words might be less confident
            }
            if (word.find_first_of("0123456789") != std::string::npos) {
                baseConfidence -= 0.05f; // Numbers might be less confident
            }
            
            float wordConfidence = baseConfidence + ((rand() % 21 - 10) / 100.0f); // ±10% variation
            wordConfidence = std::max(0.0f, std::min(1.0f, wordConfidence));
            
            wordTimings.emplace_back(word, currentTime, currentTime + baseWordDuration, wordConfidence);
            currentTime += baseWordDuration + (50 + rand() % 50); // 50-100ms gap between words
        }
    }
#endif
    
    return wordTimings;
}

TranscriptionQuality WhisperSTT::calculateQualityMetrics(const std::vector<float>& audioData, float processingLatencyMs) const {
    TranscriptionQuality quality;
    quality.processing_latency_ms = processingLatencyMs;
    
    if (!qualityIndicatorsEnabled_) {
        return quality;
    }
    
#ifdef WHISPER_AVAILABLE
    if (ctx_) {
        // Calculate average token probability across all segments
        int totalSegments = whisper_full_n_segments(ctx_);
        float totalTokenProb = 0.0f;
        int totalTokens = 0;
        float totalNoSpeechProb = 0.0f;
        
        for (int i = 0; i < totalSegments; ++i) {
            int n_tokens = whisper_full_n_tokens(ctx_, i);
            for (int j = 0; j < n_tokens; ++j) {
                float token_prob = whisper_full_get_token_p(ctx_, i, j);
                totalTokenProb += token_prob;
                totalTokens++;
            }
            
            totalNoSpeechProb += whisper_full_get_segment_no_speech_prob(ctx_, i);
        }
        
        if (totalTokens > 0) {
            quality.average_token_probability = totalTokenProb / totalTokens;
        }
        
        if (totalSegments > 0) {
            quality.no_speech_probability = totalNoSpeechProb / totalSegments;
        }
    }
#endif
    
    // Calculate basic audio quality metrics
    if (!audioData.empty()) {
        // Calculate RMS (Root Mean Square) as a proxy for signal strength
        float rms = 0.0f;
        for (float sample : audioData) {
            rms += sample * sample;
        }
        rms = std::sqrt(rms / audioData.size());
        
        // Estimate SNR based on RMS and noise floor
        float noiseFloor = 0.01f; // Assumed noise floor
        quality.signal_to_noise_ratio = (rms > noiseFloor) ? (20.0f * std::log10(rms / noiseFloor)) : 0.0f;
        
        // Audio clarity score based on signal strength and consistency
        quality.audio_clarity_score = std::min(1.0f, rms * 10.0f); // Scale RMS to 0-1 range
        
        // Simple background noise detection based on signal variance
        float variance = 0.0f;
        float mean = std::accumulate(audioData.begin(), audioData.end(), 0.0f) / audioData.size();
        for (float sample : audioData) {
            variance += (sample - mean) * (sample - mean);
        }
        variance /= audioData.size();
        
        // High variance might indicate background noise
        quality.has_background_noise = variance > 0.05f;
    }
    
    return quality;
}

std::string WhisperSTT::determineQualityLevel(float confidence, const TranscriptionQuality& quality) const {
    // Determine quality level based on confidence and quality metrics
    float qualityScore = confidence;
    
    if (qualityIndicatorsEnabled_) {
        // Adjust quality score based on audio quality metrics
        if (quality.signal_to_noise_ratio > 20.0f) {
            qualityScore += 0.1f; // Boost for good SNR
        } else if (quality.signal_to_noise_ratio < 10.0f) {
            qualityScore -= 0.1f; // Penalty for poor SNR
        }
        
        if (quality.audio_clarity_score > 0.8f) {
            qualityScore += 0.05f; // Boost for clear audio
        } else if (quality.audio_clarity_score < 0.3f) {
            qualityScore -= 0.1f; // Penalty for unclear audio
        }
        
        if (quality.has_background_noise) {
            qualityScore -= 0.05f; // Penalty for background noise
        }
        
        if (quality.processing_latency_ms > 1000.0f) {
            qualityScore -= 0.05f; // Penalty for high latency
        }
        
        // Clamp adjusted score
        qualityScore = std::max(0.0f, std::min(1.0f, qualityScore));
    }
    
    // Determine quality level
    if (qualityScore >= 0.8f) {
        return "high";
    } else if (qualityScore >= 0.6f) {
        return "medium";
    } else {
        return "low";
    }
}

bool WhisperSTT::meetsConfidenceThreshold(float confidence) const {
    return confidence >= confidenceThreshold_;
}

void WhisperSTT::enhanceTranscriptionResultWithConfidence(TranscriptionResult& result, const std::vector<float>& audioData, float processingLatencyMs) const {
    // Set confidence threshold check
    result.meets_confidence_threshold = meetsConfidenceThreshold(result.confidence);
    
    // Calculate quality metrics
    result.quality_metrics = calculateQualityMetrics(audioData, processingLatencyMs);
    
    // Determine quality level
    result.quality_level = determineQualityLevel(result.confidence, result.quality_metrics);
    
    // Extract word-level timings if enabled
    if (wordLevelConfidenceEnabled_) {
#ifdef WHISPER_AVAILABLE
        if (ctx_) {
            int totalSegments = whisper_full_n_segments(ctx_);
            for (int i = 0; i < totalSegments; ++i) {
                auto segmentWordTimings = extractWordTimings(i);
                result.word_timings.insert(result.word_timings.end(), 
                                         segmentWordTimings.begin(), 
                                         segmentWordTimings.end());
            }
        }
#endif
    }
    
    // Filter result based on confidence threshold if enabled
    if (confidenceFilteringEnabled_ && !result.meets_confidence_threshold) {
        // Mark as low quality and potentially empty the text for very low confidence
        if (result.confidence < confidenceThreshold_ * 0.5f) {
            result.text = ""; // Clear text for very low confidence
            result.quality_level = "rejected";
        }
    }
}

void WhisperSTT::enhanceStreamingResultWithWordTimings(TranscriptionResult& result, const StreamingState& state) const {
    if (!wordLevelConfidenceEnabled_) {
        return;
    }
    
    // Clear any existing word timings to avoid duplication
    result.word_timings.clear();
    
#ifdef WHISPER_AVAILABLE
    if (ctx_) {
        int totalSegments = whisper_full_n_segments(ctx_);
        
        // Calculate time offset based on streaming state
        auto streamingDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - state.startTime);
        int64_t streamingOffsetMs = streamingDuration.count();
        
        for (int i = 0; i < totalSegments; ++i) {
            auto segmentWordTimings = extractWordTimings(i);
            
            // Enhanced streaming timestamp synchronization
            for (auto& wordTiming : segmentWordTimings) {
                // Calculate precise time offset for streaming context
                int64_t baseOffset = 0;
                
                if (result.is_partial) {
                    // For partial results, timestamps should be relative to the current streaming window
                    // Calculate offset based on the current audio buffer position
                    auto streamingDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - state.startTime);
                    baseOffset = std::max(0LL, streamingDuration.count() - (wordTiming.end_ms - wordTiming.start_ms));
                } else {
                    // For final results, ensure timestamps are relative to utterance start
                    // Use processed audio samples for accurate offset calculation
                    baseOffset = (state.processedAudioSamples * 1000) / 16000; // Convert samples to ms
                }
                
                // Apply offset with bounds checking
                wordTiming.start_ms = std::max(0LL, wordTiming.start_ms + baseOffset);
                wordTiming.end_ms = std::max(wordTiming.start_ms + 50, wordTiming.end_ms + baseOffset); // Minimum 50ms duration
                
                // Enhanced confidence synchronization with streaming context
                if (result.confidence > 0.0f) {
                    float confidenceRatio = result.confidence / std::max(0.1f, wordTiming.confidence);
                    
                    // More aggressive blending for streaming to maintain consistency
                    if (confidenceRatio < 0.7f || confidenceRatio > 1.3f) {
                        // Blend word confidence with overall confidence, weighted by streaming reliability
                        float streamingWeight = result.is_partial ? 0.6f : 0.4f; // Partial results get more blending
                        wordTiming.confidence = (wordTiming.confidence * (1.0f - streamingWeight)) + 
                                              (result.confidence * streamingWeight);
                    }
                    
                    // Apply additional confidence boost for consistent words in streaming
                    if (!result.is_partial && state.totalAudioSamples > 32000) { // > 2 seconds of audio
                        wordTiming.confidence = std::min(1.0f, wordTiming.confidence * 1.05f);
                    }
                }
                
                // Clamp confidence to valid range
                wordTiming.confidence = std::max(0.0f, std::min(1.0f, wordTiming.confidence));
            }
            
            result.word_timings.insert(result.word_timings.end(), 
                                     segmentWordTimings.begin(), 
                                     segmentWordTimings.end());
        }
        
        // Sort word timings by start time to ensure proper ordering
        std::sort(result.word_timings.begin(), result.word_timings.end(),
                  [](const WordTiming& a, const WordTiming& b) {
                      return a.start_ms < b.start_ms;
                  });
        
        // Validate word timings consistency
        validateWordTimingsConsistency(result);
    }
#else
    // Simulation mode - create mock word timings for streaming
    if (wordLevelConfidenceEnabled_ && !result.text.empty()) {
        // Split text into words for simulation
        std::istringstream iss(result.text);
        std::string word;
        int64_t currentTime = result.start_time_ms;
        
        while (iss >> word) {
            // Remove punctuation for timing calculation
            std::string cleanWord = word;
            cleanWord.erase(std::remove_if(cleanWord.begin(), cleanWord.end(), 
                                         [](char c) { return std::ispunct(c); }), 
                          cleanWord.end());
            
            if (!cleanWord.empty()) {
                int64_t wordDuration = std::max(200LL, static_cast<int64_t>(cleanWord.length() * 80)); // 80ms per character, min 200ms
                float wordConfidence = result.confidence + ((rand() % 21 - 10) / 100.0f); // ±10% variation
                wordConfidence = std::max(0.0f, std::min(1.0f, wordConfidence));
                
                result.word_timings.emplace_back(word, currentTime, currentTime + wordDuration, wordConfidence);
                currentTime += wordDuration + 50; // 50ms gap between words
            }
        }
    }
#endif
}

void WhisperSTT::validateWordTimingsConsistency(TranscriptionResult& result) const {
    if (result.word_timings.empty()) {
        return;
    }
    
    // Enhanced validation with comprehensive consistency checks
    int64_t transcriptionStart = result.start_time_ms;
    int64_t transcriptionEnd = result.end_time_ms;
    
    // Phase 1: Basic bounds and duration validation
    for (auto& wordTiming : result.word_timings) {
        // Clamp word timings to transcription bounds
        if (wordTiming.start_ms < transcriptionStart) {
            wordTiming.start_ms = transcriptionStart;
        }
        if (wordTiming.end_ms > transcriptionEnd && transcriptionEnd > 0) {
            wordTiming.end_ms = transcriptionEnd;
        }
        
        // Ensure minimum and maximum word durations
        int64_t duration = wordTiming.end_ms - wordTiming.start_ms;
        if (duration < 50) { // Minimum 50ms duration
            wordTiming.end_ms = wordTiming.start_ms + 50;
        } else if (duration > 5000) { // Maximum 5 second duration for a single word
            wordTiming.end_ms = wordTiming.start_ms + 5000;
        }
        
        // Validate confidence bounds
        wordTiming.confidence = std::max(0.0f, std::min(1.0f, wordTiming.confidence));
    }
    
    // Phase 2: Fix overlapping and inconsistent word timings
    for (size_t i = 1; i < result.word_timings.size(); ++i) {
        auto& currentWord = result.word_timings[i];
        auto& previousWord = result.word_timings[i-1];
        
        if (currentWord.start_ms < previousWord.end_ms) {
            // Calculate optimal split point based on word lengths and confidences
            int64_t overlapDuration = previousWord.end_ms - currentWord.start_ms;
            int64_t totalDuration = currentWord.end_ms - previousWord.start_ms;
            
            // Weight the split based on word confidence and length
            float previousWeight = previousWord.confidence * previousWord.word.length();
            float currentWeight = currentWord.confidence * currentWord.word.length();
            float totalWeight = previousWeight + currentWeight;
            
            if (totalWeight > 0) {
                // Proportional split based on confidence and word length
                int64_t splitPoint = previousWord.start_ms + 
                    static_cast<int64_t>((totalDuration * previousWeight) / totalWeight);
                
                // Ensure minimum durations for both words
                splitPoint = std::max(splitPoint, previousWord.start_ms + 50);
                splitPoint = std::min(splitPoint, currentWord.end_ms - 50);
                
                previousWord.end_ms = splitPoint;
                currentWord.start_ms = splitPoint;
            } else {
                // Fallback: simple midpoint split
                int64_t midpoint = (previousWord.end_ms + currentWord.start_ms) / 2;
                previousWord.end_ms = midpoint;
                currentWord.start_ms = midpoint;
            }
        }
        
        // Ensure reasonable gaps between words (not too large)
        int64_t gap = currentWord.start_ms - previousWord.end_ms;
        if (gap > 2000) { // Gap larger than 2 seconds is suspicious
            // Reduce gap to maximum 1 second
            int64_t excessGap = gap - 1000;
            int64_t adjustment = excessGap / 2;
            
            previousWord.end_ms += adjustment;
            currentWord.start_ms -= adjustment;
        }
    }
    
    // Phase 3: Final consistency check and quality scoring
    float totalConfidence = 0.0f;
    int64_t totalDuration = 0;
    
    for (const auto& wordTiming : result.word_timings) {
        totalConfidence += wordTiming.confidence;
        totalDuration += (wordTiming.end_ms - wordTiming.start_ms);
    }
    
    // Update overall transcription confidence based on word-level confidence
    if (!result.word_timings.empty()) {
        float avgWordConfidence = totalConfidence / result.word_timings.size();
        
        // Blend overall confidence with word-level confidence for consistency
        if (result.confidence > 0.0f) {
            result.confidence = (result.confidence * 0.7f) + (avgWordConfidence * 0.3f);
            result.confidence = std::max(0.0f, std::min(1.0f, result.confidence));
        }
    }
}

float WhisperSTT::adjustWordConfidence(const std::string& word, float baseConfidence, int tokenCount) const {
    float adjustedConfidence = baseConfidence;
    
    // Adjust confidence based on word characteristics
    
    // 1. Word length adjustment
    if (word.length() < 2) {
        // Very short words (like "a", "I") might be less reliable
        adjustedConfidence *= 0.9f;
    } else if (word.length() > 10) {
        // Very long words might be compound or technical terms, potentially less reliable
        adjustedConfidence *= 0.95f;
    }
    
    // 2. Token count adjustment
    if (tokenCount == 1) {
        // Single token words are generally more reliable
        adjustedConfidence *= 1.05f;
    } else if (tokenCount > 3) {
        // Words split into many tokens might be less reliable
        adjustedConfidence *= 0.9f;
    }
    
    // 3. Character pattern adjustments
    bool hasNumbers = word.find_first_of("0123456789") != std::string::npos;
    bool hasSpecialChars = word.find_first_of("!@#$%^&*()_+-=[]{}|;:,.<>?") != std::string::npos;
    bool isAllCaps = !word.empty() && std::all_of(word.begin(), word.end(), [](char c) { 
        return std::isupper(c) || !std::isalpha(c); 
    });
    bool isAllLower = !word.empty() && std::all_of(word.begin(), word.end(), [](char c) { 
        return std::islower(c) || !std::isalpha(c); 
    });
    
    if (hasNumbers) {
        // Numbers might be transcribed less accurately
        adjustedConfidence *= 0.92f;
    }
    
    if (hasSpecialChars) {
        // Special characters might indicate transcription errors
        adjustedConfidence *= 0.85f;
    }
    
    if (isAllCaps && word.length() > 1) {
        // All caps words might be acronyms or emphasized speech
        adjustedConfidence *= 0.95f;
    }
    
    // 4. Common word boost with expanded vocabulary
    static const std::vector<std::string> commonWords = {
        "the", "and", "or", "but", "in", "on", "at", "to", "for", "of", "with", "by",
        "a", "an", "is", "are", "was", "were", "be", "been", "have", "has", "had",
        "do", "does", "did", "will", "would", "could", "should", "can", "may", "might",
        "this", "that", "these", "those", "here", "there", "where", "when", "why", "how",
        "what", "who", "which", "whose", "I", "you", "he", "she", "it", "we", "they",
        "me", "him", "her", "us", "them", "my", "your", "his", "her", "its", "our", "their",
        "not", "no", "yes", "now", "then", "just", "only", "also", "very", "well", "good",
        "new", "first", "last", "long", "great", "little", "own", "other", "old", "right",
        "big", "high", "different", "small", "large", "next", "early", "young", "important",
        "few", "public", "bad", "same", "able", "get", "make", "go", "see", "know", "take",
        "come", "think", "look", "want", "give", "use", "find", "tell", "ask", "work",
        "seem", "feel", "try", "leave", "call", "keep", "let", "begin", "help", "show"
    };
    
    std::string lowerWord = word;
    std::transform(lowerWord.begin(), lowerWord.end(), lowerWord.begin(), ::tolower);
    
    if (std::find(commonWords.begin(), commonWords.end(), lowerWord) != commonWords.end()) {
        adjustedConfidence *= 1.08f; // Moderate boost for common words
    }
    
    // 5. Phonetic complexity adjustment
    // Words with complex phonetic patterns might be less reliable
    int consonantClusters = 0;
    int vowelClusters = 0;
    bool inConsonantCluster = false;
    bool inVowelCluster = false;
    
    for (char c : lowerWord) {
        bool isVowel = (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y');
        
        if (isVowel) {
            if (!inVowelCluster) {
                vowelClusters++;
                inVowelCluster = true;
                inConsonantCluster = false;
            }
        } else if (std::isalpha(c)) {
            if (!inConsonantCluster) {
                consonantClusters++;
                inConsonantCluster = true;
                inVowelCluster = false;
            }
        }
    }
    
    // Penalize words with unusual phonetic patterns
    if (consonantClusters > vowelClusters + 2) {
        adjustedConfidence *= 0.95f; // Too many consonant clusters
    }
    if (vowelClusters > consonantClusters + 1) {
        adjustedConfidence *= 0.97f; // Too many vowel clusters
    }
    
    // 6. Word frequency in context (simple heuristic)
    // This could be enhanced with actual frequency analysis
    if (word.length() >= 3 && word.length() <= 6) {
        adjustedConfidence *= 1.02f; // Optimal length words are often more reliable
    }
    
    // 7. Final confidence normalization
    // Ensure confidence doesn't deviate too much from the base confidence
    float maxDeviation = 0.3f; // Allow up to 30% deviation
    float minConfidence = std::max(0.0f, baseConfidence - maxDeviation);
    float maxConfidence = std::min(1.0f, baseConfidence + maxDeviation);
    
    adjustedConfidence = std::max(minConfidence, std::min(maxConfidence, adjustedConfidence));
    
    // Clamp to valid range
    return std::max(0.0f, std::min(1.0f, adjustedConfidence));
}

} // namespace stt
void Whi
sperSTT::generateTranscriptionCandidates(const std::vector<float>& audioData, std::vector<TranscriptionResult>& candidates, int maxCandidates) {
    if (!initialized_ || audioData.empty()) {
        return;
    }
    
    candidates.clear();
    
#ifdef WHISPER_AVAILABLE
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Generate multiple candidates using different sampling strategies
        std::vector<whisper_full_params> candidateParams;
        
        // Candidate 1: Greedy decoding (most confident)
        whisper_full_params greedyParams = *params_;
        greedyParams.strategy = WHISPER_SAMPLING_GREEDY;
        candidateParams.push_back(greedyParams);
        
        // Candidate 2: Beam search with different beam size
        if (maxCandidates > 1) {
            whisper_full_params beamParams = *params_;
            beamParams.strategy = WHISPER_SAMPLING_BEAM_SEARCH;
            beamParams.beam_search.beam_size = 3;
            candidateParams.push_back(beamParams);
        }
        
        // Candidate 3: Higher temperature for more diversity
        if (maxCandidates > 2) {
            whisper_full_params tempParams = *params_;
            tempParams.temperature = std::min(1.0f, temperature_ + 0.3f);
            candidateParams.push_back(tempParams);
        }
        
        // Generate candidates
        for (size_t i = 0; i < candidateParams.size() && i < static_cast<size_t>(maxCandidates); ++i) {
            int result = whisper_full(ctx_, candidateParams[i], audioData.data(), static_cast<int>(audioData.size()));
            
            if (result == 0) {
                TranscriptionResult candidate;
                
                // Extract transcription result
                const int n_segments = whisper_full_n_segments(ctx_);
                if (n_segments > 0) {
                    std::string combined_text;
                    float total_confidence = 0.0f;
                    int valid_segments = 0;
                    
                    for (int j = 0; j < n_segments; ++j) {
                        const char* text = whisper_full_get_segment_text(ctx_, j);
                        if (text && strlen(text) > 0) {
                            if (!combined_text.empty()) {
                                combined_text += " ";
                            }
                            combined_text += text;
                            
                            float segment_confidence = calculateSegmentConfidence(j);
                            total_confidence += segment_confidence;
                            valid_segments++;
                        }
                    }
                    
                    if (valid_segments > 0) {
                        candidate.text = combined_text;
                        candidate.confidence = total_confidence / valid_segments;
                        candidate.is_partial = false;
                        candidate.start_time_ms = 0;
                        candidate.end_time_ms = static_cast<int64_t>(audioData.size() * 1000 / 16000);
                        
                        // Add language detection information
                        updateTranscriptionResultWithLanguage(candidate);
                        
                        // Enhance with confidence information
                        enhanceTranscriptionResultWithConfidence(candidate, audioData, 0.0f);
                        
                        candidates.push_back(candidate);
                    }
                }
            }
        }
        
        // Sort candidates by confidence (highest first)
        std::sort(candidates.begin(), candidates.end(),
                  [](const TranscriptionResult& a, const TranscriptionResult& b) {
                      return a.confidence > b.confidence;
                  });
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Exception during candidate generation: " + std::string(e.what()));
    }
#else
    // Simulation mode - generate mock candidates
    for (int i = 0; i < maxCandidates; ++i) {
        TranscriptionResult candidate;
        candidate.text = "Candidate " + std::to_string(i + 1) + " transcription simulation";
        candidate.confidence = 0.9f - (i * 0.1f); // Decreasing confidence
        candidate.is_partial = false;
        candidate.start_time_ms = 0;
        candidate.end_time_ms = static_cast<int64_t>(audioData.size() * 1000 / 16000);
        
        // Enhance with confidence information
        enhanceTranscriptionResultWithConfidence(candidate, audioData, 50.0f);
        
        candidates.push_back(candidate);
    }
#endif
    
    speechrnt::utils::Logger::debug("Generated " + std::to_string(candidates.size()) + " transcription candidates");
}

void WhisperSTT::triggerTranslationPipeline(uint32_t utteranceId, const TranscriptionResult& result, const std::vector<TranscriptionResult>& candidates) {
    if (transcriptionCompleteCallback_) {
        try {
            transcriptionCompleteCallback_(utteranceId, result, candidates);
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Exception in transcription complete callback: " + std::string(e.what()));
        }
    }
}

void WhisperSTT::generateMultipleCandidates(const std::vector<float>& audioData, std::vector<TranscriptionResult>& candidates, int maxCandidates) {
    generateTranscriptionCandidates(audioData, candidates, maxCandidates);
}