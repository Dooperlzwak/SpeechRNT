#include "core/translation_pipeline.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <numeric>

namespace speechrnt {
namespace core {

TranslationPipeline::TranslationPipeline(const TranslationPipelineConfig& config)
    : config_(config)
    , initialized_(false)
    , shutdown_requested_(false)
    , statistics_{}
{
    utils::Logger::info("TranslationPipeline created with config");
}

TranslationPipeline::~TranslationPipeline() {
    shutdown();
}

bool TranslationPipeline::initialize(
    std::shared_ptr<stt::STTInterface> stt_engine,
    std::shared_ptr<mt::TranslationInterface> mt_engine,
    std::shared_ptr<mt::LanguageDetector> language_detector,
    std::shared_ptr<TaskQueue> task_queue
) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    
    if (initialized_) {
        utils::Logger::warn("TranslationPipeline already initialized");
        return true;
    }
    
    if (!stt_engine || !mt_engine || !task_queue) {
        utils::Logger::error("TranslationPipeline initialization failed: null engine or task queue");
        return false;
    }
    
    if (!language_detector) {
        utils::Logger::warn("TranslationPipeline initialized without language detector - language detection will be disabled");
        config_.enable_language_detection = false;
    }
    
    stt_engine_ = stt_engine;
    mt_engine_ = mt_engine;
    language_detector_ = language_detector;
    task_queue_ = task_queue;
    
    // Initialize performance monitoring
    try {
        performance_monitor_ = std::make_shared<utils::PerformanceMonitor>();
    } catch (const std::exception& e) {
        utils::Logger::warn("Failed to initialize performance monitor: " + std::string(e.what()));
        // Continue without performance monitoring
    }
    
    initialized_ = true;
    shutdown_requested_ = false;
    
    utils::Logger::info("TranslationPipeline initialized successfully");
    return true;
}

void TranslationPipeline::shutdown() {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    
    if (!initialized_ || shutdown_requested_) {
        return;
    }
    
    shutdown_requested_ = true;
    
    // Cancel all active operations
    for (auto& [utterance_id, operation] : active_operations_) {
        operation->is_active = false;
        operation->result.pipeline_stage = "cancelled";
        operation->result.error_message = "Pipeline shutdown requested";
    }
    
    active_operations_.clear();
    initialized_ = false;
    
    utils::Logger::info("TranslationPipeline shutdown completed");
}

void TranslationPipeline::processTranscriptionResult(
    uint32_t utterance_id,
    const std::string& session_id,
    const stt::TranscriptionResult& result,
    const std::vector<stt::TranscriptionResult>& candidates
) {
    if (!initialized_ || shutdown_requested_) {
        utils::Logger::warn("TranslationPipeline not ready for processing");
        return;
    }
    
    // Create pipeline operation
    auto operation = createPipelineOperation(utterance_id, session_id);
    if (!operation) {
        utils::Logger::error("Failed to create pipeline operation for utterance " + std::to_string(utterance_id));
        return;
    }
    
    // Schedule processing on task queue
    task_queue_->enqueue([this, operation, result, candidates]() {
        processTranscriptionInternal(operation, result, candidates);
    }, TaskPriority::HIGH);
}

void TranslationPipeline::processTranscriptionInternal(
    std::shared_ptr<PipelineOperation> operation,
    const stt::TranscriptionResult& transcription,
    const std::vector<stt::TranscriptionResult>& candidates
) {
    if (!operation || !operation->is_active) {
        return;
    }
    
    // Update operation with transcription result
    operation->result.transcription = transcription;
    operation->result.transcription_candidates = candidates;
    operation->result.transcription_complete_time = std::chrono::steady_clock::now();
    
    // Update statistics
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        statistics_.total_transcriptions_processed++;
    }
    
    // Notify transcription complete
    notifyTranscriptionComplete(operation->result);
    
    // Check if language detection should be performed
    bool should_detect_language = false;
    if (config_.enable_language_detection && language_detector_) {
        should_detect_language = shouldTriggerLanguageDetection(transcription);
    }
    
    if (should_detect_language) {
        // Proceed to language detection
        executeLanguageDetection(operation);
    } else {
        // Skip language detection and proceed to translation evaluation
        operation->result.pipeline_stage = "translation_evaluation";
        
        // Evaluate if translation should be triggered
        bool should_translate = false;
        
        if (config_.enable_automatic_translation) {
            if (config_.enable_confidence_gating) {
                should_translate = shouldTriggerTranslation(transcription);
                
                // Check custom confidence gate callback
                if (should_translate && confidence_gate_callback_) {
                    try {
                        should_translate = confidence_gate_callback_(transcription);
                    } catch (const std::exception& e) {
                        utils::Logger::error("Confidence gate callback failed: " + std::string(e.what()));
                        should_translate = false;
                    }
                }
                
                if (!should_translate) {
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    statistics_.confidence_gate_rejections++;
                }
            } else {
                should_translate = true;
            }
        }
        
        operation->result.confidence_gate_passed = should_translate;
        
        if (should_translate) {
            operation->result.translation_triggered = true;
            
            // Handle multiple candidates if enabled
            if (config_.enable_multiple_candidates && !candidates.empty()) {
                processMultipleCandidates(operation, candidates);
            } else {
                executeTranslation(operation);
            }
            
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            statistics_.translations_triggered++;
        } else {
            // Complete pipeline without translation
            operation->result.pipeline_stage = "complete";
            completePipelineOperation(operation->utterance_id);
        }
    }
}

void TranslationPipeline::executeTranslation(std::shared_ptr<PipelineOperation> operation) {
    if (!operation || !operation->is_active) {
        return;
    }
    
    operation->result.pipeline_stage = "translation";
    
    // Use the best transcription candidate or the main result
    stt::TranscriptionResult transcription_to_translate = operation->result.transcription;
    
    if (!operation->result.transcription_candidates.empty()) {
        transcription_to_translate = selectBestTranscriptionCandidate(
            operation->result.transcription_candidates
        );
    }
    
    // Perform translation
    try {
        auto translation_start = std::chrono::steady_clock::now();
        
        mt::TranslationResult translation_result = mt_engine_->translate(transcription_to_translate.text);
        
        auto translation_end = std::chrono::steady_clock::now();
        auto translation_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            translation_end - translation_start
        );
        
        recordTranslationLatency(translation_latency);
        
        processTranslationResult(operation, translation_result);
        
    } catch (const std::exception& e) {
        handlePipelineError(operation, "Translation failed: " + std::string(e.what()), "translation");
    }
}

void TranslationPipeline::executeLanguageDetection(std::shared_ptr<PipelineOperation> operation) {
    if (!operation || !operation->is_active || !language_detector_) {
        return;
    }
   

bool TranslationPipeline::shouldTriggerLanguageDetection(const stt::TranscriptionResult& result) const {
    // Check if language detection is enabled
    if (!config_.enable_language_detection || !language_detector_) {
        return false;
    }
    
    // Check if text is not empty or too short for reliable detection
    if (result.text.empty() || result.text.length() < 10) {
        return false;
    }
    
    // Check basic transcription confidence
    if (result.confidence < config_.min_transcription_confidence) {
        return false;
    }
    
    return true;
}

bool TranslationPipeline::shouldTriggerTranslation(const stt::TranscriptionResult& result) const {
    // Check basic confidence threshold
    if (result.confidence < config_.min_transcription_confidence) {
        return false;
    }
    
    // Check if transcription meets quality threshold
    if (!result.meets_confidence_threshold) {
        return false;
    }
    
    // Check if text is not empty or too short
    if (result.text.empty() || result.text.length() < 3) {
        return false;
    }
    
    // Check language detection confidence if available
    if (!result.detected_language.empty() && 
        result.language_confidence < 0.5f) {
        return false;
    }
    
    return true;
}

bool TranslationPipeline::evaluateLanguageDetectionConfidence(const mt::LanguageDetectionResult& result) const {
    return result.confidence >= config_.language_detection_confidence_threshold && result.isReliable;
}

bool TranslationPipeline::evaluateTranscriptionConfidence(const stt::TranscriptionResult& result) const {
    return result.confidence >= config_.min_transcription_confidence &&
           result.meets_confidence_threshold;
}

stt::TranscriptionResult TranslationPipeline::selectBestTranscriptionCandidate(
    const std::vector<stt::TranscriptionResult>& candidates
) const {
    if (candidates.empty()) {
        return stt::TranscriptionResult{};
    }
    
    // Find candidate with highest confidence that meets threshold
    auto best_candidate = std::max_element(candidates.begin(), candidates.end(),
        [](const stt::TranscriptionResult& a, const stt::TranscriptionResult& b) {
            return a.confidence < b.confidence;
        });
    
    return *best_candidate;
}

void TranslationPipeline::processMultipleCandidates(
    std::shared_ptr<PipelineOperation> operation,
    const std::vector<stt::TranscriptionResult>& candidates
) {
    if (!operation || !operation->is_active) {
        return;
    }
    
    // Filter candidates by confidence threshold
    std::vector<stt::TranscriptionResult> valid_candidates;
    std::copy_if(candidates.begin(), candidates.end(), 
                 std::back_inserter(valid_candidates),
                 [this](const stt::TranscriptionResult& candidate) {
                     return candidate.confidence >= config_.candidate_confidence_threshold;
                 });
    
    // Limit number of candidates
    if (valid_candidates.size() > config_.max_transcription_candidates) {
        // Sort by confidence and take top candidates
        std::sort(valid_candidates.begin(), valid_candidates.end(),
                  [](const stt::TranscriptionResult& a, const stt::TranscriptionResult& b) {
                      return a.confidence > b.confidence;
                  });
        valid_candidates.resize(config_.max_transcription_candidates);
    }
    
    if (valid_candidates.empty()) {
        // No valid candidates, use original transcription
        executeTranslation(operation);
        return;
    }
    
    // Translate multiple candidates
    try {
        auto translation_candidates = translateMultipleCandidates(valid_candidates);
        operation->result.translation_candidates = translation_candidates;
        
        // Select best translation result
        if (!translation_candidates.empty()) {
            auto best_translation = std::max_element(translation_candidates.begin(), 
                                                   translation_candidates.end(),
                [](const mt::TranslationResult& a, const mt::TranslationResult& b) {
                    return a.confidence < b.confidence;
                });
            
            processTranslationResult(operation, *best_translation);
        } else {
            handlePipelineError(operation, "No valid translation candidates", "translation");
        }
        
    } catch (const std::exception& e) {
        handlePipelineError(operation, "Multiple candidate translation failed: " + std::string(e.what()), "translation");
    }
}

std::vector<mt::TranslationResult> TranslationPipeline::translateMultipleCandidates(
    const std::vector<stt::TranscriptionResult>& transcription_candidates
) {
    std::vector<mt::TranslationResult> translation_results;
    
    for (const auto& candidate : transcription_candidates) {
        try {
            mt::TranslationResult result = mt_engine_->translate(candidate.text);
            if (result.success && result.confidence >= config_.min_translation_confidence) {
                translation_results.push_back(result);
            }
        } catch (const std::exception& e) {
            utils::Logger::warn("Failed to translate candidate: " + std::string(e.what()));
        }
    }
    
    return translation_results;
}

void TranslationPipeline::triggerTranslation(
    uint32_t utterance_id,
    const std::string& session_id,
    const stt::TranscriptionResult& transcription,
    bool force_translation
) {
    if (!initialized_ || shutdown_requested_) {
        utils::Logger::warn("TranslationPipeline not ready for manual translation trigger");
        return;
    }
    
    auto operation = createPipelineOperation(utterance_id, session_id);
    if (!operation) {
        utils::Logger::error("Failed to create pipeline operation for manual translation");
        return;
    }
    
    operation->result.transcription = transcription;
    operation->result.translation_triggered = true;
    operation->result.confidence_gate_passed = force_translation || shouldTriggerTranslation(transcription);
    
    task_queue_->enqueue([this, operation]() {
        executeTranslation(operation);
    }, TaskPriority::HIGH);
}

void TranslationPipeline::setLanguageConfiguration(
    const std::string& source_language,
    const std::string& target_language
) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    source_language_ = source_language;
    target_language_ = target_language;
    
    // Initialize MT engine with new language pair
    if (mt_engine_ && initialized_) {
        try {
            mt_engine_->initialize(source_language, target_language);
        } catch (const std::exception& e) {
            utils::Logger::error("Failed to initialize MT engine with new language pair: " + std::string(e.what()));
        }
    }
    
    utils::Logger::info("Language configuration updated: " + source_language + " -> " + target_language);
}

std::pair<std::string, std::string> TranslationPipeline::getLanguageConfiguration() const {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    return {source_language_, target_language_};
}

void TranslationPipeline::updateConfiguration(const TranslationPipelineConfig& config) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    config_ = config;
    utils::Logger::info("TranslationPipeline configuration updated");
}

std::shared_ptr<TranslationPipeline::PipelineOperation> TranslationPipeline::createPipelineOperation(
    uint32_t utterance_id,
    const std::string& session_id
) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    
    // Check if operation already exists
    if (active_operations_.find(utterance_id) != active_operations_.end()) {
        utils::Logger::warn("Pipeline operation already exists for utterance " + std::to_string(utterance_id));
        return active_operations_[utterance_id];
    }
    
    // Check concurrent operation limit
    if (active_operations_.size() >= config_.max_concurrent_translations) {
        utils::Logger::warn("Maximum concurrent translations reached");
        return nullptr;
    }
    
    auto operation = std::make_shared<PipelineOperation>(utterance_id, session_id);
    active_operations_[utterance_id] = operation;
    
    return operation;
}

void TranslationPipeline::completePipelineOperation(uint32_t utterance_id) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    
    auto it = active_operations_.find(utterance_id);
    if (it != active_operations_.end()) {
        updateStatistics(*it->second);
        active_operations_.erase(it);
    }
}

std::shared_ptr<TranslationPipeline::PipelineOperation> TranslationPipeline::getPipelineOperation(uint32_t utterance_id) const {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    
    auto it = active_operations_.find(utterance_id);
    return (it != active_operations_.end()) ? it->second : nullptr;
}

void TranslationPipeline::handlePipelineError(
    std::shared_ptr<PipelineOperation> operation,
    const std::string& error_message,
    const std::string& stage
) {
    if (!operation) {
        return;
    }
    
    operation->result.pipeline_stage = "error";
    operation->result.error_message = error_message;
    operation->is_active = false;
    
    utils::Logger::error("Pipeline error in stage '" + stage + "' for utterance " + 
                        std::to_string(operation->utterance_id) + ": " + error_message);
    
    notifyPipelineError(operation->result, error_message);
    completePipelineOperation(operation->utterance_id);
}

void TranslationPipeline::recordTranslationLatency(std::chrono::milliseconds latency) {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    
    recent_translation_latencies_.push_back(latency);
    if (recent_translation_latencies_.size() > MAX_LATENCY_SAMPLES) {
        recent_translation_latencies_.erase(recent_translation_latencies_.begin());
    }
    
    // Update average latency
    if (!recent_translation_latencies_.empty()) {
        auto total_latency = std::accumulate(recent_translation_latencies_.begin(),
                                           recent_translation_latencies_.end(),
                                           std::chrono::milliseconds(0));
        statistics_.average_translation_latency = total_latency / recent_translation_latencies_.size();
    }
}

void TranslationPipeline::recordLanguageDetectionLatency(std::chrono::milliseconds latency) {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    
    recent_language_detection_latencies_.push_back(latency);
    if (recent_language_detection_latencies_.size() > MAX_LATENCY_SAMPLES) {
        recent_language_detection_latencies_.erase(recent_language_detection_latencies_.begin());
    }
    
    // Update average latency
    if (!recent_language_detection_latencies_.empty()) {
        auto total_latency = std::accumulate(recent_language_detection_latencies_.begin(),
                                           recent_language_detection_latencies_.end(),
                                           std::chrono::milliseconds(0));
        statistics_.average_language_detection_latency = total_latency / recent_language_detection_latencies_.size();
    }
}

void TranslationPipeline::updateStatistics(const PipelineOperation& operation) {
    // Statistics are updated in real-time during processing
    // This method can be used for final cleanup or additional metrics
}

// Callback notification methods
void TranslationPipeline::notifyTranscriptionComplete(const PipelineResult& result) {
    std::lock_guard<std::mutex> callback_lock(callbacks_mutex_);
    if (transcription_complete_callback_) {
        try {
            transcription_complete_callback_(result);
        } catch (const std::exception& e) {
            utils::Logger::error("Transcription complete callback failed: " + std::string(e.what()));
        }
    }
}

void TranslationPipeline::notifyLanguageDetectionComplete(const PipelineResult& result) {
    std::lock_guard<std::mutex> callback_lock(callbacks_mutex_);
    if (language_detection_complete_callback_) {
        try {
            language_detection_complete_callback_(result);
        } catch (const std::exception& e) {
            utils::Logger::error("Language detection complete callback failed: " + std::string(e.what()));
        }
    }
}

void TranslationPipeline::notifyLanguageChange(const std::string& session_id, const std::string& old_lang, const std::string& new_lang, float confidence) {
    std::lock_guard<std::mutex> callback_lock(callbacks_mutex_);
    if (language_change_callback_) {
        try {
            language_change_callback_(session_id, old_lang, new_lang, confidence);
        } catch (const std::exception& e) {
            utils::Logger::error("Language change callback failed: " + std::string(e.what()));
        }
    }
}

void TranslationPipeline::notifyTranslationComplete(const PipelineResult& result) {
    std::lock_guard<std::mutex> callback_lock(callbacks_mutex_);
    if (translation_complete_callback_) {
        try {
            translation_complete_callback_(result);
        } catch (const std::exception& e) {
            utils::Logger::error("Translation complete callback failed: " + std::string(e.what()));
        }
    }
}

void TranslationPipeline::notifyPipelineError(const PipelineResult& result, const std::string& error) {
    std::lock_guard<std::mutex> callback_lock(callbacks_mutex_);
    if (pipeline_error_callback_) {
        try {
            pipeline_error_callback_(result, error);
        } catch (const std::exception& e) {
            utils::Logger::error("Pipeline error callback failed: " + std::string(e.what()));
        }
    }
}

// Callback setters
void TranslationPipeline::setTranscriptionCompleteCallback(TranscriptionCompleteCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    transcription_complete_callback_ = callback;
}

void TranslationPipeline::setLanguageDetectionCompleteCallback(LanguageDetectionCompleteCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    language_detection_complete_callback_ = callback;
}

void TranslationPipeline::setLanguageChangeCallback(LanguageChangeCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    language_change_callback_ = callback;
}

void TranslationPipeline::setTranslationCompleteCallback(TranslationCompleteCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    translation_complete_callback_ = callback;
}

void TranslationPipeline::setPipelineErrorCallback(PipelineErrorCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    pipeline_error_callback_ = callback;
}

void TranslationPipeline::setConfidenceGateCallback(ConfidenceGateCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    confidence_gate_callback_ = callback;
}

// Statistics and status methods
TranslationPipeline::Statistics TranslationPipeline::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto stats = statistics_;
    stats.active_pipeline_operations = active_operations_.size();
    return stats;
}

std::vector<uint32_t> TranslationPipeline::getActivePipelineOperations() const {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    std::vector<uint32_t> active_ids;
    for (const auto& [id, operation] : active_operations_) {
        if (operation->is_active) {
            active_ids.push_back(id);
        }
    }
    return active_ids;
}

bool TranslationPipeline::cancelPipelineOperation(uint32_t utterance_id) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    
    auto it = active_operations_.find(utterance_id);
    if (it != active_operations_.end()) {
        it->second->is_active = false;
        it->second->result.pipeline_stage = "cancelled";
        it->second->result.error_message = "Operation cancelled by user";
        active_operations_.erase(it);
        return true;
    }
    
    return false;
}

bool TranslationPipeline::isReady() const {
    return initialized_ && !shutdown_requested_ && 
           stt_engine_ && mt_engine_ && task_queue_;
}

// Configuration setters
void TranslationPipeline::setAutomaticTranslationEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    config_.enable_automatic_translation = enabled;
}

void TranslationPipeline::setConfidenceGatingEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    config_.enable_confidence_gating = enabled;
}

void TranslationPipeline::setConfidenceThresholds(float transcription_threshold, float translation_threshold) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    config_.min_transcription_confidence = transcription_threshold;
    config_.min_translation_confidence = translation_threshold;
}

void TranslationPipeline::setPreliminaryTranslationEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    config_.enable_preliminary_translation = enabled;
}

} // namespace core
} // namespace speechrnt    op
eration->result.pipeline_stage = "language_detection";
    
    // Check cache first if enabled
    if (config_.enable_language_detection_caching) {
        if (hasCachedLanguageDetectionResult(operation->session_id, operation->result.transcription.text)) {
            auto cached_result = getCachedLanguageDetectionResult(operation->session_id, operation->result.transcription.text);
            
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            statistics_.language_detection_cache_hits++;
            
            processLanguageDetectionResult(operation, cached_result);
            return;
        }
    }
    
    // Perform language detection
    try {
        auto detection_start = std::chrono::steady_clock::now();
        
        mt::LanguageDetectionResult detection_result;
        
        // Use hybrid detection if audio data is available, otherwise text-only
        if (!operation->result.transcription.text.empty()) {
            detection_result = language_detector_->detectLanguage(operation->result.transcription.text);
        } else {
            // Fallback to default language
            detection_result.detectedLanguage = source_language_;
            detection_result.confidence = 0.0f;
            detection_result.isReliable = false;
            detection_result.detectionMethod = "fallback";
        }
        
        auto detection_end = std::chrono::steady_clock::now();
        auto detection_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            detection_end - detection_start
        );
        
        recordLanguageDetectionLatency(detection_latency);
        
        // Cache the result if enabled
        if (config_.enable_language_detection_caching) {
            cacheLanguageDetectionResult(operation->session_id, operation->result.transcription.text, detection_result);
        }
        
        processLanguageDetectionResult(operation, detection_result);
        
    } catch (const std::exception& e) {
        handlePipelineError(operation, "Language detection failed: " + std::string(e.what()), "language_detection");
    }
}

void TranslationPipeline::processLanguageDetectionResult(
    std::shared_ptr<PipelineOperation> operation,
    const mt::LanguageDetectionResult& detection_result
) {
    if (!operation || !operation->is_active) {
        return;
    }
    
    operation->result.language_detection = detection_result;
    operation->result.language_detection_complete_time = std::chrono::steady_clock::now();
    
    // Update statistics
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        statistics_.language_detections_performed++;
    }
    
    // Evaluate language detection confidence
    bool language_detection_passed = evaluateLanguageDetectionConfidence(detection_result);
    operation->result.language_detection_passed = language_detection_passed;
    
    if (!language_detection_passed) {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        statistics_.language_detection_rejections++;
    }
    
    // Check for language change
    bool language_changed = false;
    std::string previous_language;
    
    if (config_.enable_automatic_language_switching && language_detection_passed) {
        language_changed = hasLanguageChanged(operation->session_id, detection_result.detectedLanguage);
        
        if (language_changed) {
            previous_language = getCurrentDetectedLanguage(operation->session_id);
            updateSessionLanguage(operation->session_id, detection_result.detectedLanguage);
            
            operation->result.language_changed = true;
            operation->result.previous_language = previous_language;
            
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            statistics_.language_changes_detected++;
            
            // Update MT engine with new language pair if needed
            if (mt_engine_) {
                try {
                    mt_engine_->initialize(detection_result.detectedLanguage, target_language_);
                } catch (const std::exception& e) {
                    utils::Logger::error("Failed to update MT engine with new source language: " + std::string(e.what()));
                }
            }
            
            // Notify language change
            if (config_.notify_language_changes) {
                notifyLanguageChange(operation->session_id, previous_language, detection_result.detectedLanguage, detection_result.confidence);
            }
        }
    }
    
    // Notify language detection complete
    notifyLanguageDetectionComplete(operation->result);
    
    // Proceed to translation evaluation
    operation->result.pipeline_stage = "translation_evaluation";
    
    // Evaluate if translation should be triggered
    bool should_translate = false;
    
    if (config_.enable_automatic_translation) {
        if (config_.enable_confidence_gating) {
            should_translate = shouldTriggerTranslation(operation->result.transcription) && language_detection_passed;
            
            // Check custom confidence gate callback
            if (should_translate && confidence_gate_callback_) {
                try {
                    should_translate = confidence_gate_callback_(operation->result.transcription);
                } catch (const std::exception& e) {
                    utils::Logger::error("Confidence gate callback failed: " + std::string(e.what()));
                    should_translate = false;
                }
            }
            
            if (!should_translate) {
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                if (!language_detection_passed) {
                    statistics_.language_detection_rejections++;
                } else {
                    statistics_.confidence_gate_rejections++;
                }
            }
        } else {
            should_translate = language_detection_passed;
        }
    }
    
    operation->result.confidence_gate_passed = should_translate;
    
    if (should_translate) {
        operation->result.translation_triggered = true;
        
        // Handle multiple candidates if enabled
        if (config_.enable_multiple_candidates && !operation->result.transcription_candidates.empty()) {
            processMultipleCandidates(operation, operation->result.transcription_candidates);
        } else {
            executeTranslation(operation);
        }
        
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        statistics_.translations_triggered++;
    } else {
        // Complete pipeline without translation
        operation->result.pipeline_stage = "complete";
        completePipelineOperation(operation->utterance_id);
    }
}

void TranslationPipeline::processTranslationResult(
    std::shared_ptr<PipelineOperation> operation,
    const mt::TranslationResult& translation_result
) {
    if (!operation || !operation->is_active) {
        return;
    }
    
    operation->result.translation = translation_result;
    operation->result.translation_complete_time = std::chrono::steady_clock::now();
    
    if (translation_result.success) {
        operation->result.pipeline_stage = "complete";
        
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        statistics_.successful_translations++;
    } else {
        operation->result.pipeline_stage = "error";
        operation->result.error_message = translation_result.errorMessage;
        
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        statistics_.failed_translations++;
        
        // Try fallback translation if enabled
        if (config_.enable_fallback_translation) {
            utils::Logger::info("Attempting fallback translation for utterance " + 
                              std::to_string(operation->utterance_id));
            // Could implement fallback logic here (e.g., different model, simplified text)
        }
    }
    
    // Notify translation complete
    notifyTranslationComplete(operation->result);
    
    // Complete the pipeline operation
    completePipelineOperation(operation->utterance_id);
}// 
Language detection configuration setters
void TranslationPipeline::setLanguageDetectionEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    config_.enable_language_detection = enabled;
}

void TranslationPipeline::setAutomaticLanguageSwitchingEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    config_.enable_automatic_language_switching = enabled;
}

void TranslationPipeline::setLanguageDetectionConfidenceThreshold(float threshold) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    config_.language_detection_confidence_threshold = std::max(0.0f, std::min(1.0f, threshold));
}

void TranslationPipeline::setLanguageDetectionCachingEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    config_.enable_language_detection_caching = enabled;
}

void TranslationPipeline::setLanguageChangeNotificationsEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    config_.notify_language_changes = enabled;
}

// Manual language detection trigger
void TranslationPipeline::triggerLanguageDetection(
    uint32_t utterance_id,
    const std::string& session_id,
    const std::string& text,
    const std::vector<float>& audio_data
) {
    if (!initialized_ || shutdown_requested_ || !language_detector_) {
        utils::Logger::warn("TranslationPipeline not ready for manual language detection trigger");
        return;
    }
    
    auto operation = createPipelineOperation(utterance_id, session_id);
    if (!operation) {
        utils::Logger::error("Failed to create pipeline operation for manual language detection");
        return;
    }
    
    // Set up transcription result with provided text
    operation->result.transcription.text = text;
    operation->result.transcription.confidence = 1.0f; // Manual trigger assumes high confidence
    operation->result.transcription.meets_confidence_threshold = true;
    
    task_queue_->enqueue([this, operation]() {
        executeLanguageDetection(operation);
    }, TaskPriority::HIGH);
}

// Language detection caching methods
bool TranslationPipeline::isLanguageDetectionCacheValid(const LanguageDetectionCacheEntry& entry) const {
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - entry.timestamp);
    return age < config_.language_detection_cache_ttl;
}

std::string TranslationPipeline::calculateTextHash(const std::string& text) const {
    // Simple hash function for caching - in production, use a proper hash function
    std::hash<std::string> hasher;
    return std::to_string(hasher(text));
}

void TranslationPipeline::cacheLanguageDetectionResult(
    const std::string& session_id,
    const std::string& text,
    const mt::LanguageDetectionResult& result
) {
    std::lock_guard<std::mutex> lock(language_cache_mutex_);
    
    std::string text_hash = calculateTextHash(text);
    LanguageDetectionCacheEntry entry;
    entry.result = result;
    entry.text_hash = text_hash;
    entry.timestamp = std::chrono::steady_clock::now();
    
    language_detection_cache_[session_id][text_hash] = entry;
    
    // Clean up old entries to prevent memory growth
    auto& session_cache = language_detection_cache_[session_id];
    auto it = session_cache.begin();
    while (it != session_cache.end()) {
        if (!isLanguageDetectionCacheValid(it->second)) {
            it = session_cache.erase(it);
        } else {
            ++it;
        }
    }
}

mt::LanguageDetectionResult TranslationPipeline::getCachedLanguageDetectionResult(
    const std::string& session_id,
    const std::string& text
) const {
    std::lock_guard<std::mutex> lock(language_cache_mutex_);
    
    std::string text_hash = calculateTextHash(text);
    
    auto session_it = language_detection_cache_.find(session_id);
    if (session_it != language_detection_cache_.end()) {
        auto entry_it = session_it->second.find(text_hash);
        if (entry_it != session_it->second.end() && isLanguageDetectionCacheValid(entry_it->second)) {
            return entry_it->second.result;
        }
    }
    
    return mt::LanguageDetectionResult{}; // Return empty result if not found
}

bool TranslationPipeline::hasCachedLanguageDetectionResult(
    const std::string& session_id,
    const std::string& text
) const {
    std::lock_guard<std::mutex> lock(language_cache_mutex_);
    
    std::string text_hash = calculateTextHash(text);
    
    auto session_it = language_detection_cache_.find(session_id);
    if (session_it != language_detection_cache_.end()) {
        auto entry_it = session_it->second.find(text_hash);
        return entry_it != session_it->second.end() && isLanguageDetectionCacheValid(entry_it->second);
    }
    
    return false;
}

// Language change detection methods
bool TranslationPipeline::hasLanguageChanged(const std::string& session_id, const std::string& detected_language) const {
    std::lock_guard<std::mutex> lock(session_language_mutex_);
    
    auto it = session_languages_.find(session_id);
    if (it != session_languages_.end()) {
        return it->second != detected_language;
    }
    
    // First detection for this session
    return !detected_language.empty() && detected_language != source_language_;
}

void TranslationPipeline::updateSessionLanguage(const std::string& session_id, const std::string& language) {
    std::lock_guard<std::mutex> lock(session_language_mutex_);
    session_languages_[session_id] = language;
}

std::string TranslationPipeline::getCurrentDetectedLanguage(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(session_language_mutex_);
    
    auto it = session_languages_.find(session_id);
    if (it != session_languages_.end()) {
        return it->second;
    }
    
    return source_language_; // Default to configured source language
}

// Cache management methods
void TranslationPipeline::clearLanguageDetectionCache() {
    std::lock_guard<std::mutex> lock(language_cache_mutex_);
    language_detection_cache_.clear();
    utils::Logger::info("Language detection cache cleared");
}

void TranslationPipeline::clearLanguageDetectionCache(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(language_cache_mutex_);
    
    auto it = language_detection_cache_.find(session_id);
    if (it != language_detection_cache_.end()) {
        language_detection_cache_.erase(it);
        utils::Logger::info("Language detection cache cleared for session: " + session_id);
    }
}