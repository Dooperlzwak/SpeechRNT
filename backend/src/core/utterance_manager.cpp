#include "core/utterance_manager.hpp"
#include "core/translation_pipeline.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <thread>

namespace speechrnt {
namespace core {

UtteranceManager::UtteranceManager(const UtteranceManagerConfig& config)
    : config_(config)
    , next_utterance_id_(1)
    , running_(false)
    , total_created_(0)
    , total_completed_(0)
    , total_errors_(0) {
}

UtteranceManager::~UtteranceManager() {
    shutdown();
}

void UtteranceManager::initialize(std::shared_ptr<TaskQueue> task_queue) {
    if (!task_queue) {
        return;
    }
    
    task_queue_ = task_queue;
    running_ = true;
    
    if (config_.enable_automatic_cleanup) {
        startCleanupTimer();
    }
}

void UtteranceManager::initializeWithPipeline(
    std::shared_ptr<TaskQueue> task_queue,
    std::shared_ptr<TranslationPipeline> translation_pipeline
) {
    initialize(task_queue);
    translation_pipeline_ = translation_pipeline;
    
    if (translation_pipeline_) {
        // Set up pipeline callbacks
        translation_pipeline_->setTranscriptionCompleteCallback(
            [this](const TranslationPipeline::PipelineResult& result) {
                // Update utterance with transcription result
                setTranscription(result.utterance_id, result.transcription.text, result.transcription.confidence);
            }
        );
        
        translation_pipeline_->setTranslationCompleteCallback(
            [this](const TranslationPipeline::PipelineResult& result) {
                // Update utterance with translation result
                if (result.translation.success) {
                    setTranslation(result.utterance_id, result.translation.translatedText);
                    updateUtteranceState(result.utterance_id, UtteranceState::SYNTHESIZING);
                    
                    // Schedule TTS processing
                    if (task_queue_) {
                        task_queue_->enqueue([this, utterance_id = result.utterance_id]() {
                            processTTS(utterance_id);
                        }, TaskPriority::HIGH);
                    }
                } else {
                    setUtteranceError(result.utterance_id, "Translation failed: " + result.translation.errorMessage);
                }
            }
        );
        
        translation_pipeline_->setPipelineErrorCallback(
            [this](const TranslationPipeline::PipelineResult& result, const std::string& error) {
                setUtteranceError(result.utterance_id, error);
            }
        );
        
        utils::Logger::info("UtteranceManager initialized with translation pipeline integration");
    }
}

void UtteranceManager::initializeWithEngines(
    std::shared_ptr<TaskQueue> task_queue,
    std::shared_ptr<TranslationPipeline> translation_pipeline,
    std::shared_ptr<stt::STTInterface> stt_engine
) {
    initializeWithPipeline(task_queue, translation_pipeline);
    stt_engine_ = std::move(stt_engine);
}

void UtteranceManager::setSTTEngine(std::shared_ptr<stt::STTInterface> stt_engine) {
    stt_engine_ = std::move(stt_engine);
}

void UtteranceManager::setMTEngine(std::shared_ptr<mt::TranslationInterface> mt_engine) {
    mt_engine_ = std::move(mt_engine);
}

void UtteranceManager::setTTSEngine(std::shared_ptr<tts::TTSInterface> tts_engine) {
    tts_engine_ = std::move(tts_engine);
}

void UtteranceManager::shutdown() {
    running_ = false;
    
    stopCleanupTimer();
    
    // Clear all utterances
    {
        std::lock_guard<std::mutex> lock(utterances_mutex_);
        utterances_.clear();
    }
    
    task_queue_.reset();
}

uint32_t UtteranceManager::createUtterance(const std::string& session_id) {
    if (!canAcceptNewUtterance()) {
        return 0; // Cannot create new utterance
    }
    
    uint32_t utterance_id = next_utterance_id_.fetch_add(1);
    auto utterance = std::make_shared<UtteranceData>(utterance_id, session_id);
    
    {
        std::lock_guard<std::mutex> lock(utterances_mutex_);
        utterances_[utterance_id] = utterance;
    }
    
    total_created_++;
    notifyStateChange(*utterance);
    
    return utterance_id;
}

bool UtteranceManager::updateUtteranceState(uint32_t utterance_id, UtteranceState new_state) {
    std::shared_ptr<UtteranceData> utterance_copy;
    
    // Scope for the lock
    {
        std::lock_guard<std::mutex> lock(utterances_mutex_);
        
        auto it = utterances_.find(utterance_id);
        if (it == utterances_.end()) {
            return false;
        }
        
        auto& utterance = it->second;
        UtteranceState old_state = utterance->state;
        utterance->state = new_state;
        utterance->last_updated = std::chrono::steady_clock::now();
        
        // Update statistics
        if (new_state == UtteranceState::COMPLETE && old_state != UtteranceState::COMPLETE) {
            total_completed_++;
        } else if (new_state == UtteranceState::ERROR && old_state != UtteranceState::ERROR) {
            total_errors_++;
        }
        
        // Make a copy for callback (to avoid holding lock during callback)
        utterance_copy = std::make_shared<UtteranceData>(*utterance);
    } // Lock is automatically released here
    
    // Call callbacks without holding the lock
    notifyStateChange(*utterance_copy);
    
    if (new_state == UtteranceState::COMPLETE) {
        notifyComplete(*utterance_copy);
    }
    
    return true;
}

UtteranceState UtteranceManager::getUtteranceState(uint32_t utterance_id) const {
    std::lock_guard<std::mutex> lock(utterances_mutex_);
    
    auto it = utterances_.find(utterance_id);
    if (it == utterances_.end()) {
        return UtteranceState::ERROR;
    }
    
    return it->second->state;
}

std::shared_ptr<UtteranceData> UtteranceManager::getUtterance(uint32_t utterance_id) const {
    std::lock_guard<std::mutex> lock(utterances_mutex_);
    
    auto it = utterances_.find(utterance_id);
    if (it == utterances_.end()) {
        return nullptr;
    }
    
    // Return a copy to ensure thread safety
    return std::make_shared<UtteranceData>(*it->second);
}

bool UtteranceManager::addAudioData(uint32_t utterance_id, const std::vector<float>& audio_data) {
    std::lock_guard<std::mutex> lock(utterances_mutex_);
    
    auto it = utterances_.find(utterance_id);
    if (it == utterances_.end()) {
        return false;
    }
    
    auto& utterance = it->second;
    utterance->audio_buffer.insert(utterance->audio_buffer.end(), 
                                  audio_data.begin(), audio_data.end());
    utterance->last_updated = std::chrono::steady_clock::now();
    
    return true;
}

bool UtteranceManager::setTranscription(uint32_t utterance_id, const std::string& transcript, float confidence) {
    std::lock_guard<std::mutex> lock(utterances_mutex_);
    
    auto it = utterances_.find(utterance_id);
    if (it == utterances_.end()) {
        return false;
    }
    
    auto& utterance = it->second;
    utterance->transcript = transcript;
    utterance->transcription_confidence = confidence;
    utterance->last_updated = std::chrono::steady_clock::now();
    
    return true;
}

bool UtteranceManager::setTranslation(uint32_t utterance_id, const std::string& translation) {
    std::lock_guard<std::mutex> lock(utterances_mutex_);
    
    auto it = utterances_.find(utterance_id);
    if (it == utterances_.end()) {
        return false;
    }
    
    auto& utterance = it->second;
    utterance->translation = translation;
    utterance->last_updated = std::chrono::steady_clock::now();
    
    return true;
}

bool UtteranceManager::setSynthesizedAudio(uint32_t utterance_id, const std::vector<uint8_t>& audio_data) {
    std::lock_guard<std::mutex> lock(utterances_mutex_);
    
    auto it = utterances_.find(utterance_id);
    if (it == utterances_.end()) {
        return false;
    }
    
    auto& utterance = it->second;
    utterance->synthesized_audio = audio_data;
    utterance->last_updated = std::chrono::steady_clock::now();
    
    return true;
}

bool UtteranceManager::setUtteranceError(uint32_t utterance_id, const std::string& error_message) {
    std::shared_ptr<UtteranceData> utterance_copy;
    
    // Scope for the lock
    {
        std::lock_guard<std::mutex> lock(utterances_mutex_);
        
        auto it = utterances_.find(utterance_id);
        if (it == utterances_.end()) {
            return false;
        }
        
        auto& utterance = it->second;
        utterance->error_message = error_message;
        utterance->state = UtteranceState::ERROR;
        utterance->last_updated = std::chrono::steady_clock::now();
        
        total_errors_++;
        
        // Make a copy for callback
        utterance_copy = std::make_shared<UtteranceData>(*utterance);
    } // Lock is automatically released here
    
    // Call callbacks without holding the lock
    notifyStateChange(*utterance_copy);
    notifyError(*utterance_copy, error_message);
    
    return true;
}

bool UtteranceManager::setLanguageConfig(uint32_t utterance_id, const std::string& source_lang, 
                                        const std::string& target_lang, const std::string& voice_id) {
    std::lock_guard<std::mutex> lock(utterances_mutex_);
    
    auto it = utterances_.find(utterance_id);
    if (it == utterances_.end()) {
        return false;
    }
    
    auto& utterance = it->second;
    utterance->source_language = source_lang;
    utterance->target_language = target_lang;
    utterance->voice_id = voice_id;
    utterance->last_updated = std::chrono::steady_clock::now();
    
    return true;
}

std::vector<std::shared_ptr<UtteranceData>> UtteranceManager::getSessionUtterances(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(utterances_mutex_);
    
    std::vector<std::shared_ptr<UtteranceData>> result;
    
    for (const auto& pair : utterances_) {
        if (pair.second->session_id == session_id) {
            result.push_back(std::make_shared<UtteranceData>(*pair.second));
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<UtteranceData>> UtteranceManager::getActiveUtterances() const {
    std::lock_guard<std::mutex> lock(utterances_mutex_);
    
    std::vector<std::shared_ptr<UtteranceData>> result;
    
    for (const auto& pair : utterances_) {
        if (pair.second->state != UtteranceState::COMPLETE && 
            pair.second->state != UtteranceState::ERROR) {
            result.push_back(std::make_shared<UtteranceData>(*pair.second));
        }
    }
    
    return result;
}

size_t UtteranceManager::cleanupOldUtterances(std::chrono::seconds max_age) {
    std::lock_guard<std::mutex> lock(utterances_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    size_t removed_count = 0;
    
    auto it = utterances_.begin();
    while (it != utterances_.end()) {
        const auto& utterance = it->second;
        
        // Only cleanup completed or errored utterances
        if ((utterance->state == UtteranceState::COMPLETE || 
             utterance->state == UtteranceState::ERROR) &&
            (now - utterance->last_updated) > max_age) {
            
            it = utterances_.erase(it);
            removed_count++;
        } else {
            ++it;
        }
    }
    
    return removed_count;
}

size_t UtteranceManager::removeSessionUtterances(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(utterances_mutex_);
    
    size_t removed_count = 0;
    
    auto it = utterances_.begin();
    while (it != utterances_.end()) {
        if (it->second->session_id == session_id) {
            it = utterances_.erase(it);
            removed_count++;
        } else {
            ++it;
        }
    }
    
    return removed_count;
}

UtteranceManager::Statistics UtteranceManager::getStatistics() const {
    std::lock_guard<std::mutex> lock(utterances_mutex_);
    
    Statistics stats;
    stats.total_utterances = total_created_.load();
    stats.completed_utterances = total_completed_.load();
    stats.error_utterances = total_errors_.load();
    
    size_t active_count = 0;
    size_t concurrent_count = 0;
    std::chrono::milliseconds total_processing_time{0};
    size_t completed_with_time = 0;
    
    auto now = std::chrono::steady_clock::now();
    
    for (const auto& pair : utterances_) {
        const auto& utterance = pair.second;
        
        if (utterance->state != UtteranceState::COMPLETE && 
            utterance->state != UtteranceState::ERROR) {
            active_count++;
            
            if (utterance->state == UtteranceState::TRANSCRIBING ||
                utterance->state == UtteranceState::TRANSLATING ||
                utterance->state == UtteranceState::SYNTHESIZING) {
                concurrent_count++;
            }
        }
        
        if (utterance->state == UtteranceState::COMPLETE) {
            auto processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                utterance->last_updated - utterance->created_at);
            total_processing_time += processing_time;
            completed_with_time++;
        }
    }
    
    stats.active_utterances = active_count;
    stats.concurrent_utterances = concurrent_count;
    stats.average_processing_time = completed_with_time > 0 ? 
        total_processing_time / completed_with_time : std::chrono::milliseconds{0};
    
    return stats;
}

void UtteranceManager::setStateChangeCallback(UtteranceStateCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    state_change_callback_ = std::move(callback);
}

void UtteranceManager::setCompleteCallback(UtteranceCompleteCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    complete_callback_ = std::move(callback);
}

void UtteranceManager::setErrorCallback(UtteranceErrorCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    error_callback_ = std::move(callback);
}

bool UtteranceManager::processUtterance(uint32_t utterance_id) {
    if (!task_queue_) {
        return false;
    }
    
    // Schedule STT processing
    task_queue_->enqueue([this, utterance_id]() {
        processSTT(utterance_id);
    }, TaskPriority::HIGH);
    
    return true;
}

void UtteranceManager::processTranscriptionResult(
    uint32_t utterance_id,
    const stt::TranscriptionResult& result,
    const std::vector<stt::TranscriptionResult>& candidates
) {
    if (!translation_pipeline_) {
        utils::Logger::warn("Translation pipeline not available for utterance " + std::to_string(utterance_id));
        // Fallback to old processing method
        setTranscription(utterance_id, result.text, result.confidence);
        
        if (task_queue_) {
            task_queue_->enqueue([this, utterance_id]() {
                processMT(utterance_id);
            }, TaskPriority::HIGH);
        }
        return;
    }
    
    // Get session ID for the utterance
    auto utterance = getUtterance(utterance_id);
    if (!utterance) {
        utils::Logger::error("Utterance not found: " + std::to_string(utterance_id));
        return;
    }
    
    // Process through translation pipeline
    translation_pipeline_->processTranscriptionResult(
        utterance_id,
        utterance->session_id,
        result,
        candidates
    );
}

bool UtteranceManager::canAcceptNewUtterance() const {
    std::lock_guard<std::mutex> lock(utterances_mutex_);
    
    size_t active_count = 0;
    for (const auto& pair : utterances_) {
        if (pair.second->state != UtteranceState::COMPLETE && 
            pair.second->state != UtteranceState::ERROR) {
            active_count++;
        }
    }
    
    return active_count < config_.max_concurrent_utterances;
}

void UtteranceManager::startCleanupTimer() {
    cleanup_thread_ = std::thread([this]() {
        while (running_) {
            std::this_thread::sleep_for(config_.cleanup_interval);
            if (running_) {
                performCleanup();
            }
        }
    });
}

void UtteranceManager::stopCleanupTimer() {
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
}

void UtteranceManager::performCleanup() {
    // Cleanup old utterances
    cleanupOldUtterances(config_.utterance_timeout);
}

void UtteranceManager::notifyStateChange(const UtteranceData& utterance) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    if (state_change_callback_) {
        try {
            state_change_callback_(utterance);
        } catch (...) {
            // Ignore callback exceptions
        }
    }
}

void UtteranceManager::notifyComplete(const UtteranceData& utterance) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    if (complete_callback_) {
        try {
            complete_callback_(utterance);
        } catch (...) {
            // Ignore callback exceptions
        }
    }
}

void UtteranceManager::notifyError(const UtteranceData& utterance, const std::string& error) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    if (error_callback_) {
        try {
            error_callback_(utterance, error);
        } catch (...) {
            // Ignore callback exceptions
        }
    }
}

void UtteranceManager::processSTT(uint32_t utterance_id) {
    // Update state to transcribing
    if (!updateUtteranceState(utterance_id, UtteranceState::TRANSCRIBING)) {
        return;
    }
    
    // Get utterance data
    std::shared_ptr<UtteranceData> utterance = getUtterance(utterance_id);
    if (!utterance) {
        utils::Logger::error("Utterance not found: " + std::to_string(utterance_id));
        setUtteranceError(utterance_id, "Utterance not found");
        return;
    }
    
    // If a real STT engine is available and the utterance has audio, use it
    if (stt_engine_ && !utterance->audio_buffer.empty() && stt_engine_->isInitialized()) {
        auto audio = utterance->audio_buffer; // copy for thread safety
        
        utils::Logger::info("Processing STT for utterance " + std::to_string(utterance_id) + 
                           " with " + std::to_string(audio.size()) + " audio samples using real Whisper engine");
        
        try {
            // Use the real STT engine
            stt_engine_->transcribe(audio, [this, utterance_id](const stt::TranscriptionResult& result) {
                utils::Logger::info("STT completed for utterance " + std::to_string(utterance_id) + 
                                   ": \"" + result.text + "\" (confidence: " + std::to_string(result.confidence) + ")");
                
                if (translation_pipeline_) {
                    processTranscriptionResult(utterance_id, result);
                } else {
                    setTranscription(utterance_id, result.text, result.confidence);
                    updateUtteranceState(utterance_id, UtteranceState::TRANSLATING);
                    
                    if (task_queue_) {
                        task_queue_->enqueue([this, utterance_id]() {
                            processMT(utterance_id);
                        }, TaskPriority::HIGH);
                    }
                }
            });
            return;
        } catch (const std::exception& e) {
            utils::Logger::error("STT engine transcription failed: " + std::string(e.what()));
            setUtteranceError(utterance_id, "STT transcription failed: " + std::string(e.what()));
            return;
        } catch (...) {
            utils::Logger::error("STT engine transcription failed with unknown exception");
            setUtteranceError(utterance_id, "STT transcription failed with unknown error");
            return;
        }
    }
    
    // Log why we're falling back to simulation
    if (!stt_engine_) {
        utils::Logger::warn("No STT engine available for utterance " + std::to_string(utterance_id) + ", using simulation");
    } else if (!stt_engine_->isInitialized()) {
        utils::Logger::warn("STT engine not initialized for utterance " + std::to_string(utterance_id) + ", using simulation");
    } else if (utterance->audio_buffer.empty()) {
        utils::Logger::warn("No audio data available for utterance " + std::to_string(utterance_id) + ", using simulation");
    }

    // Fallback: simulate STT quickly to preserve previous behavior
    utils::Logger::info("Using STT simulation for utterance " + std::to_string(utterance_id));
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Slightly longer for realism
    
    stt::TranscriptionResult fallback;
    fallback.text = "Simulated transcription for utterance " + std::to_string(utterance_id);
    fallback.confidence = 0.85f; // Slightly lower confidence to indicate simulation
    fallback.is_partial = false;
    fallback.meets_confidence_threshold = true;
    fallback.quality_level = "simulated";
    fallback.detected_language = "en";
    fallback.language_confidence = 0.8f;
    
    if (translation_pipeline_) {
        processTranscriptionResult(utterance_id, fallback);
    } else {
        setTranscription(utterance_id, fallback.text, fallback.confidence);
        updateUtteranceState(utterance_id, UtteranceState::TRANSLATING);
        
        if (task_queue_) {
            task_queue_->enqueue([this, utterance_id]() {
                processMT(utterance_id);
            }, TaskPriority::HIGH);
        }
    }
}

void UtteranceManager::processMT(uint32_t utterance_id) {
    // Update state to translating
    if (!updateUtteranceState(utterance_id, UtteranceState::TRANSLATING)) {
        return;
    }
    
    // Get utterance data
    std::shared_ptr<UtteranceData> utterance = getUtterance(utterance_id);
    if (!utterance) {
        utils::Logger::error("Utterance not found for MT processing: " + std::to_string(utterance_id));
        setUtteranceError(utterance_id, "Utterance not found");
        return;
    }
    
    // Check if we have transcription to translate
    if (utterance->transcript.empty()) {
        utils::Logger::warn("No transcript available for MT processing: " + std::to_string(utterance_id));
        setUtteranceError(utterance_id, "No transcript available for translation");
        return;
    }
    
    // If a real MT engine is available, use it
    if (mt_engine_ && !utterance->source_language.empty() && !utterance->target_language.empty()) {
        utils::Logger::info("Processing MT for utterance " + std::to_string(utterance_id) + 
                           " with real Marian engine: \"" + utterance->transcript + "\" (" + 
                           utterance->source_language + " -> " + utterance->target_language + ")");
        
        try {
            // Initialize MT engine for this language pair if needed
            if (!mt_engine_->isReady() || 
                !mt_engine_->supportsLanguagePair(utterance->source_language, utterance->target_language)) {
                
                utils::Logger::info("Initializing MT engine for language pair: " + 
                                   utterance->source_language + " -> " + utterance->target_language);
                
                if (!mt_engine_->initialize(utterance->source_language, utterance->target_language)) {
                    throw std::runtime_error("Failed to initialize MT engine for language pair");
                }
            }
            
            // Perform translation
            auto translation_result = mt_engine_->translate(utterance->transcript);
            
            if (translation_result.success) {
                utils::Logger::info("MT completed for utterance " + std::to_string(utterance_id) + 
                                   ": \"" + translation_result.translatedText + "\" (confidence: " + 
                                   std::to_string(translation_result.confidence) + ")");
                
                setTranslation(utterance_id, translation_result.translatedText);
                updateUtteranceState(utterance_id, UtteranceState::SYNTHESIZING);
                
                // Schedule TTS processing
                if (task_queue_) {
                    task_queue_->enqueue([this, utterance_id]() {
                        processTTS(utterance_id);
                    }, TaskPriority::HIGH);
                }
            } else {
                throw std::runtime_error("Translation failed: " + translation_result.errorMessage);
            }
            
            return;
            
        } catch (const std::exception& e) {
            utils::Logger::error("MT engine translation failed: " + std::string(e.what()));
            setUtteranceError(utterance_id, "MT translation failed: " + std::string(e.what()));
            return;
        } catch (...) {
            utils::Logger::error("MT engine translation failed with unknown exception");
            setUtteranceError(utterance_id, "MT translation failed with unknown error");
            return;
        }
    }
    
    // Log why we're falling back to simulation
    if (!mt_engine_) {
        utils::Logger::warn("No MT engine available for utterance " + std::to_string(utterance_id) + ", using simulation");
    } else if (utterance->source_language.empty() || utterance->target_language.empty()) {
        utils::Logger::warn("Language configuration missing for utterance " + std::to_string(utterance_id) + ", using simulation");
    } else if (!mt_engine_->supportsLanguagePair(utterance->source_language, utterance->target_language)) {
        utils::Logger::warn("Unsupported language pair " + utterance->source_language + " -> " + 
                           utterance->target_language + " for utterance " + std::to_string(utterance_id) + ", using simulation");
    }
    
    // Fallback: simulate MT processing
    utils::Logger::info("Using MT simulation for utterance " + std::to_string(utterance_id));
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Realistic processing time
    
    // Create simulated translation
    std::string simulated_translation;
    if (!utterance->source_language.empty() && !utterance->target_language.empty()) {
        simulated_translation = "Simulated translation of \"" + utterance->transcript + "\" from " + 
                               utterance->source_language + " to " + utterance->target_language;
    } else {
        simulated_translation = "Simulated translation of \"" + utterance->transcript + "\"";
    }
    
    setTranslation(utterance_id, simulated_translation);
    updateUtteranceState(utterance_id, UtteranceState::SYNTHESIZING);
    
    // Schedule TTS processing
    if (task_queue_) {
        task_queue_->enqueue([this, utterance_id]() {
            processTTS(utterance_id);
        }, TaskPriority::HIGH);
    }
}

void UtteranceManager::processTTS(uint32_t utterance_id) {
    // Update state to synthesizing
    if (!updateUtteranceState(utterance_id, UtteranceState::SYNTHESIZING)) {
        return;
    }
    
    // Get utterance data
    std::shared_ptr<UtteranceData> utterance = getUtterance(utterance_id);
    if (!utterance) {
        utils::Logger::error("Utterance not found for TTS processing: " + std::to_string(utterance_id));
        setUtteranceError(utterance_id, "Utterance not found");
        return;
    }
    
    // Check if we have translation to synthesize
    if (utterance->translation.empty()) {
        utils::Logger::warn("No translation available for TTS processing: " + std::to_string(utterance_id));
        setUtteranceError(utterance_id, "No translation available for synthesis");
        return;
    }
    
    // If a real TTS engine is available, use it
    if (tts_engine_ && tts_engine_->isReady()) {
        utils::Logger::info("Processing TTS for utterance " + std::to_string(utterance_id) + 
                           " with real Coqui TTS engine: \"" + utterance->translation + "\"" +
                           (utterance->voice_id.empty() ? "" : " (voice: " + utterance->voice_id + ")"));
        
        try {
            // Set voice if specified
            std::string voice_to_use = utterance->voice_id.empty() ? tts_engine_->getDefaultVoice() : utterance->voice_id;
            
            // Validate voice availability
            auto available_voices = tts_engine_->getAvailableVoices();
            bool voice_found = false;
            for (const auto& voice : available_voices) {
                if (voice.id == voice_to_use) {
                    voice_found = true;
                    break;
                }
            }
            
            if (!voice_found && !utterance->voice_id.empty()) {
                utils::Logger::warn("Requested voice '" + utterance->voice_id + "' not available for utterance " + 
                                   std::to_string(utterance_id) + ", using default voice");
                voice_to_use = tts_engine_->getDefaultVoice();
            }
            
            // Perform synthesis
            auto synthesis_result = tts_engine_->synthesize(utterance->translation, voice_to_use);
            
            if (synthesis_result.success) {
                utils::Logger::info("TTS completed for utterance " + std::to_string(utterance_id) + 
                                   ": " + std::to_string(synthesis_result.audioData.size()) + " bytes, " +
                                   std::to_string(synthesis_result.duration) + "s duration");
                
                setSynthesizedAudio(utterance_id, synthesis_result.audioData);
                updateUtteranceState(utterance_id, UtteranceState::COMPLETE);
            } else {
                throw std::runtime_error("Synthesis failed: " + synthesis_result.errorMessage);
            }
            
            return;
            
        } catch (const std::exception& e) {
            utils::Logger::error("TTS engine synthesis failed: " + std::string(e.what()));
            setUtteranceError(utterance_id, "TTS synthesis failed: " + std::string(e.what()));
            return;
        } catch (...) {
            utils::Logger::error("TTS engine synthesis failed with unknown exception");
            setUtteranceError(utterance_id, "TTS synthesis failed with unknown error");
            return;
        }
    }
    
    // Log why we're falling back to simulation
    if (!tts_engine_) {
        utils::Logger::warn("No TTS engine available for utterance " + std::to_string(utterance_id) + ", using simulation");
    } else if (!tts_engine_->isReady()) {
        utils::Logger::warn("TTS engine not ready for utterance " + std::to_string(utterance_id) + ", using simulation");
    }
    
    // Fallback: simulate TTS processing
    utils::Logger::info("Using TTS simulation for utterance " + std::to_string(utterance_id));
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Realistic synthesis time
    
    // Create simulated audio data (larger than before to be more realistic)
    std::vector<uint8_t> simulated_audio;
    size_t audio_size = 1024 + (utterance->translation.length() * 50); // Size based on text length
    simulated_audio.resize(audio_size, 0);
    
    // Add some variation to the simulated audio
    for (size_t i = 0; i < audio_size; ++i) {
        simulated_audio[i] = static_cast<uint8_t>((i * 7 + utterance_id * 13) % 256);
    }
    
    setSynthesizedAudio(utterance_id, simulated_audio);
    updateUtteranceState(utterance_id, UtteranceState::COMPLETE);
}

} // namespace core
} // namespace speechrnt