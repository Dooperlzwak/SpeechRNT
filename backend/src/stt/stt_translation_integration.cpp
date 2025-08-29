#include "stt/stt_translation_integration.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <numeric>

namespace stt {

STTTranslationIntegration::STTTranslationIntegration(const STTTranslationConfig& config)
    : config_(config)
    , initialized_(false)
    , statistics_{} {
    utils::Logger::info("STTTranslationIntegration created");
}

STTTranslationIntegration::~STTTranslationIntegration() {
    shutdown();
}

bool STTTranslationIntegration::initialize(
    std::shared_ptr<WhisperSTT> sttEngine,
    std::shared_ptr<speechrnt::core::TranslationPipeline> translationPipeline
) {
    if (!sttEngine || !translationPipeline) {
        utils::Logger::error("STTTranslationIntegration: Invalid parameters for initialization");
        return false;
    }
    
    if (!sttEngine->isInitialized() || !translationPipeline->isReady()) {
        utils::Logger::error("STTTranslationIntegration: STT engine or translation pipeline not ready");
        return false;
    }
    
    sttEngine_ = sttEngine;
    translationPipeline_ = translationPipeline;
    
    // Set up transcription complete callback
    sttEngine_->setTranscriptionCompleteCallback(
        [this](uint32_t utteranceId, const TranscriptionResult& result, const std::vector<TranscriptionResult>& candidates) {
            handleTranscriptionComplete(utteranceId, result, candidates);
        }
    );
    
    initialized_ = true;
    
    utils::Logger::info("STTTranslationIntegration initialized successfully");
    return true;
}

bool STTTranslationIntegration::initializeWithStreaming(
    std::shared_ptr<WhisperSTT> sttEngine,
    std::shared_ptr<StreamingTranscriber> streamingTranscriber,
    std::shared_ptr<speechrnt::core::TranslationPipeline> translationPipeline
) {
    if (!initialize(sttEngine, translationPipeline)) {
        return false;
    }
    
    if (!streamingTranscriber) {
        utils::Logger::error("STTTranslationIntegration: Invalid streaming transcriber");
        return false;
    }
    
    streamingTranscriber_ = streamingTranscriber;
    
    utils::Logger::info("STTTranslationIntegration initialized with streaming support");
    return true;
}

void STTTranslationIntegration::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Clear callbacks
    if (sttEngine_) {
        sttEngine_->setTranscriptionCompleteCallback(nullptr);
    }
    
    // Clear references
    sttEngine_.reset();
    streamingTranscriber_.reset();
    translationPipeline_.reset();
    
    initialized_ = false;
    
    utils::Logger::info("STTTranslationIntegration shutdown completed");
}

void STTTranslationIntegration::processTranscriptionWithTranslation(
    uint32_t utteranceId,
    const std::string& sessionId,
    const std::vector<float>& audioData,
    bool generateCandidates
) {
    if (!isReady()) {
        utils::Logger::error("STTTranslationIntegration not ready for processing");
        return;
    }
    
    if (audioData.empty()) {
        utils::Logger::warn("Empty audio data provided for transcription");
        return;
    }
    
    // Perform transcription
    sttEngine_->transcribe(audioData, [this, utteranceId, sessionId, audioData, generateCandidates](const TranscriptionResult& result) {
        // Generate candidates if requested and result meets basic criteria
        std::vector<TranscriptionResult> candidates;
        if (generateCandidates && config_.enable_multiple_candidates && !result.text.empty()) {
            sttEngine_->generateTranscriptionCandidates(audioData, candidates, config_.max_transcription_candidates);
        }
        
        // Process the transcription result
        handleTranscriptionComplete(utteranceId, result, candidates);
        
        // Update statistics
        updateStatistics(result, shouldTriggerTranslation(result), candidates.size());
    });
    
    utils::Logger::debug("Started transcription with translation for utterance " + std::to_string(utteranceId));
}

void STTTranslationIntegration::processStreamingTranscription(
    uint32_t utteranceId,
    const std::string& sessionId,
    const std::vector<float>& audioData
) {
    if (!isReady() || !streamingTranscriber_) {
        utils::Logger::error("STTTranslationIntegration not ready for streaming processing");
        return;
    }
    
    // Start streaming transcription
    streamingTranscriber_->startTranscription(utteranceId, audioData, true);
    
    utils::Logger::debug("Started streaming transcription for utterance " + std::to_string(utteranceId));
}

void STTTranslationIntegration::triggerManualTranslation(
    uint32_t utteranceId,
    const std::string& sessionId,
    const TranscriptionResult& transcription,
    bool forceTranslation
) {
    if (!isReady()) {
        utils::Logger::error("STTTranslationIntegration not ready for manual translation");
        return;
    }
    
    // Trigger translation through the pipeline
    translationPipeline_->triggerTranslation(utteranceId, sessionId, transcription, forceTranslation);
    
    // Update statistics
    {
        std::lock_guard<std::mutex> lock(statisticsMutex_);
        statistics_.manual_translations_triggered++;
    }
    
    // Notify callback
    notifyTranslationTriggered(utteranceId, sessionId, false);
    
    utils::Logger::debug("Manually triggered translation for utterance " + std::to_string(utteranceId));
}

void STTTranslationIntegration::updateConfiguration(const STTTranslationConfig& config) {
    config_ = config;
    
    // Update translation pipeline configuration if available
    if (translationPipeline_) {
        speechrnt::core::TranslationPipelineConfig pipelineConfig;
        pipelineConfig.min_transcription_confidence = config.min_transcription_confidence;
        pipelineConfig.candidate_confidence_threshold = config.candidate_confidence_threshold;
        pipelineConfig.enable_automatic_translation = config.enable_automatic_translation;
        pipelineConfig.enable_confidence_gating = config.enable_confidence_gating;
        pipelineConfig.enable_multiple_candidates = config.enable_multiple_candidates;
        pipelineConfig.max_transcription_candidates = config.max_transcription_candidates;
        
        translationPipeline_->updateConfiguration(pipelineConfig);
    }
    
    utils::Logger::info("STTTranslationIntegration configuration updated");
}

void STTTranslationIntegration::setTranscriptionReadyCallback(TranscriptionReadyCallback callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    transcriptionReadyCallback_ = callback;
}

void STTTranslationIntegration::setTranslationTriggeredCallback(TranslationTriggeredCallback callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    translationTriggeredCallback_ = callback;
}

bool STTTranslationIntegration::isReady() const {
    return initialized_ && sttEngine_ && sttEngine_->isInitialized() && 
           translationPipeline_ && translationPipeline_->isReady();
}

STTTranslationIntegration::Statistics STTTranslationIntegration::getStatistics() const {
    std::lock_guard<std::mutex> lock(statisticsMutex_);
    return statistics_;
}

void STTTranslationIntegration::handleTranscriptionComplete(
    uint32_t utteranceId,
    const TranscriptionResult& result,
    const std::vector<TranscriptionResult>& candidates
) {
    // Notify that transcription is ready
    notifyTranscriptionReady(utteranceId, result, candidates);
    
    // Check if automatic translation should be triggered
    if (config_.enable_automatic_translation && shouldTriggerTranslation(result)) {
        // Filter candidates by confidence if multiple candidates are enabled
        std::vector<TranscriptionResult> filteredCandidates;
        if (config_.enable_multiple_candidates && !candidates.empty()) {
            filteredCandidates = filterCandidatesByConfidence(candidates);
        }
        
        // Trigger translation through the pipeline
        // We need a session ID - for now we'll generate one based on utterance ID
        std::string sessionId = "session_" + std::to_string(utteranceId);
        translationPipeline_->processTranscriptionResult(utteranceId, sessionId, result, filteredCandidates);
        
        // Update statistics
        {
            std::lock_guard<std::mutex> lock(statisticsMutex_);
            statistics_.automatic_translations_triggered++;
        }
        
        // Notify callback
        notifyTranslationTriggered(utteranceId, sessionId, true);
        
        utils::Logger::debug("Automatically triggered translation for utterance " + std::to_string(utteranceId));
    } else if (config_.enable_confidence_gating && !shouldTriggerTranslation(result)) {
        // Update statistics for confidence gate rejection
        std::lock_guard<std::mutex> lock(statisticsMutex_);
        statistics_.confidence_gate_rejections++;
        
        utils::Logger::debug("Translation not triggered due to confidence gating for utterance " + std::to_string(utteranceId) + 
                           " (confidence: " + std::to_string(result.confidence) + ")");
    }
}

void STTTranslationIntegration::generateAndProcessCandidates(
    uint32_t utteranceId,
    const std::string& sessionId,
    const std::vector<float>& audioData,
    const TranscriptionResult& primaryResult
) {
    if (!config_.enable_multiple_candidates || audioData.empty()) {
        return;
    }
    
    std::vector<TranscriptionResult> candidates;
    sttEngine_->generateTranscriptionCandidates(audioData, candidates, config_.max_transcription_candidates);
    
    // Filter candidates by confidence
    auto filteredCandidates = filterCandidatesByConfidence(candidates);
    
    // Process with translation pipeline if we have valid candidates
    if (!filteredCandidates.empty()) {
        translationPipeline_->processTranscriptionResult(utteranceId, sessionId, primaryResult, filteredCandidates);
    }
    
    utils::Logger::debug("Generated and processed " + std::to_string(filteredCandidates.size()) + 
                         " candidates for utterance " + std::to_string(utteranceId));
}

bool STTTranslationIntegration::shouldTriggerTranslation(const TranscriptionResult& result) const {
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
    if (!result.detected_language.empty() && result.language_confidence < 0.5f) {
        return false;
    }
    
    return true;
}

std::vector<TranscriptionResult> STTTranslationIntegration::filterCandidatesByConfidence(
    const std::vector<TranscriptionResult>& candidates
) const {
    std::vector<TranscriptionResult> filtered;
    
    std::copy_if(candidates.begin(), candidates.end(), std::back_inserter(filtered),
                 [this](const TranscriptionResult& candidate) {
                     return candidate.confidence >= config_.candidate_confidence_threshold &&
                            !candidate.text.empty();
                 });
    
    // Sort by confidence (highest first)
    std::sort(filtered.begin(), filtered.end(),
              [](const TranscriptionResult& a, const TranscriptionResult& b) {
                  return a.confidence > b.confidence;
              });
    
    // Limit to maximum number of candidates
    if (filtered.size() > static_cast<size_t>(config_.max_transcription_candidates)) {
        filtered.resize(config_.max_transcription_candidates);
    }
    
    return filtered;
}

void STTTranslationIntegration::updateStatistics(
    const TranscriptionResult& result,
    bool translationTriggered,
    size_t candidatesCount
) {
    std::lock_guard<std::mutex> lock(statisticsMutex_);
    
    statistics_.total_transcriptions_processed++;
    statistics_.candidates_generated += candidatesCount;
    
    // Update average confidence (running average)
    if (statistics_.total_transcriptions_processed == 1) {
        statistics_.average_transcription_confidence = result.confidence;
    } else {
        float alpha = 0.1f; // Smoothing factor
        statistics_.average_transcription_confidence = 
            alpha * result.confidence + (1.0f - alpha) * statistics_.average_transcription_confidence;
    }
}

void STTTranslationIntegration::notifyTranscriptionReady(
    uint32_t utteranceId,
    const TranscriptionResult& result,
    const std::vector<TranscriptionResult>& candidates
) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    if (transcriptionReadyCallback_) {
        try {
            transcriptionReadyCallback_(utteranceId, result, candidates);
        } catch (const std::exception& e) {
            utils::Logger::error("Exception in transcription ready callback: " + std::string(e.what()));
        }
    }
}

void STTTranslationIntegration::notifyTranslationTriggered(
    uint32_t utteranceId,
    const std::string& sessionId,
    bool automatic
) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    if (translationTriggeredCallback_) {
        try {
            translationTriggeredCallback_(utteranceId, sessionId, automatic);
        } catch (const std::exception& e) {
            utils::Logger::error("Exception in translation triggered callback: " + std::string(e.what()));
        }
    }
}

} // namespace stt