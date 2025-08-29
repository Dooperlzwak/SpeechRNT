#include "stt/transcription_manager.hpp"
#include "stt/whisper_stt.hpp"
#include <iostream>
#include <chrono>

namespace stt {

TranscriptionManager::TranscriptionManager()
    : running_(false)
    , should_stop_(false)
    , current_language_("en")
    , translate_to_english_(false)
    , temperature_(0.0f)
    , max_tokens_(0) {
}

TranscriptionManager::~TranscriptionManager() {
    stop();
}

bool TranscriptionManager::initialize(const std::string& model_path, const std::string& engine) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    
    last_error_.clear();
    
    // Create STT engine based on type
    if (engine == "whisper") {
        stt_engine_ = createWhisperSTT();
    } else {
        last_error_ = "Unknown STT engine: " + engine;
        std::cerr << last_error_ << std::endl;
        return false;
    }
    
    // Initialize the engine
    if (!stt_engine_->initialize(model_path)) {
        last_error_ = "Failed to initialize STT engine: " + stt_engine_->getLastError();
        std::cerr << last_error_ << std::endl;
        return false;
    }
    
    // Apply current configuration
    stt_engine_->setLanguage(current_language_);
    stt_engine_->setTranslateToEnglish(translate_to_english_);
    stt_engine_->setTemperature(temperature_);
    stt_engine_->setMaxTokens(max_tokens_);
    
    std::cout << "TranscriptionManager initialized with " << engine << " engine" << std::endl;
    return true;
}

void TranscriptionManager::submitTranscription(const TranscriptionRequest& request) {
    if (!isInitialized()) {
        std::cerr << "TranscriptionManager not initialized" << std::endl;
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        request_queue_.push(request);
    }
    
    queue_condition_.notify_one();
}

void TranscriptionManager::setLanguage(const std::string& language) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    current_language_ = language;
    
    if (stt_engine_) {
        stt_engine_->setLanguage(language);
    }
}

void TranscriptionManager::setTranslateToEnglish(bool translate) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    translate_to_english_ = translate;
    
    if (stt_engine_) {
        stt_engine_->setTranslateToEnglish(translate);
    }
}

void TranscriptionManager::setTemperature(float temperature) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    temperature_ = temperature;
    
    if (stt_engine_) {
        stt_engine_->setTemperature(temperature);
    }
}

void TranscriptionManager::setMaxTokens(int max_tokens) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    max_tokens_ = max_tokens;
    
    if (stt_engine_) {
        stt_engine_->setMaxTokens(max_tokens);
    }
}

void TranscriptionManager::start() {
    if (running_) {
        return;
    }
    
    if (!isInitialized()) {
        std::cerr << "Cannot start TranscriptionManager: not initialized" << std::endl;
        return;
    }
    
    should_stop_ = false;
    running_ = true;
    worker_thread_ = std::thread(&TranscriptionManager::workerLoop, this);
    
    std::cout << "TranscriptionManager started" << std::endl;
}

void TranscriptionManager::stop() {
    if (!running_) {
        return;
    }
    
    should_stop_ = true;
    queue_condition_.notify_all();
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    running_ = false;
    std::cout << "TranscriptionManager stopped" << std::endl;
}

bool TranscriptionManager::isInitialized() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return stt_engine_ && stt_engine_->isInitialized();
}

std::string TranscriptionManager::getLastError() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return last_error_;
}

size_t TranscriptionManager::getQueueSize() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return request_queue_.size();
}

void TranscriptionManager::workerLoop() {
    while (!should_stop_) {
        TranscriptionRequest request;
        
        // Wait for a request
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_condition_.wait(lock, [this] { 
                return !request_queue_.empty() || should_stop_; 
            });
            
            if (should_stop_) {
                break;
            }
            
            if (request_queue_.empty()) {
                continue;
            }
            
            request = std::move(request_queue_.front());
            request_queue_.pop();
        }
        
        // Process the request
        processRequest(request);
    }
}

void TranscriptionManager::processRequest(const TranscriptionRequest& request) {
    if (!stt_engine_) {
        std::cerr << "STT engine not available" << std::endl;
        return;
    }
    
    auto callback = [request](const TranscriptionResult& result) {
        if (request.callback) {
            request.callback(request.utterance_id, result);
        }
    };
    
    try {
        if (request.is_live) {
            stt_engine_->transcribeLive(request.audio_data, callback);
        } else {
            stt_engine_->transcribe(request.audio_data, callback);
        }
    } catch (const std::exception& e) {
        std::cerr << "Transcription error: " << e.what() << std::endl;
        
        // Send error result with enhanced confidence information
        TranscriptionResult error_result;
        error_result.text = "";
        error_result.confidence = 0.0f;
        error_result.is_partial = false;
        error_result.start_time_ms = 0;
        error_result.end_time_ms = 0;
        error_result.meets_confidence_threshold = false;
        error_result.quality_level = "error";
        
        if (request.callback) {
            request.callback(request.utterance_id, error_result);
        }
    }
}

std::unique_ptr<STTInterface> TranscriptionManager::createWhisperSTT() {
    auto whisper_stt = std::make_unique<WhisperSTT>();
    
    // Configure default settings for optimal performance
    whisper_stt->setPartialResultsEnabled(true);
    whisper_stt->setMinChunkSizeMs(1000);  // 1 second chunks
    whisper_stt->setConfidenceThreshold(0.5f);
    whisper_stt->setWordLevelConfidenceEnabled(true);
    whisper_stt->setQualityIndicatorsEnabled(true);
    whisper_stt->setLanguageDetectionEnabled(true);
    whisper_stt->setLanguageDetectionThreshold(0.7f);
    whisper_stt->setAutoLanguageSwitching(false); // Disable by default for stability
    
    return std::move(whisper_stt);
}

} // namespace stt