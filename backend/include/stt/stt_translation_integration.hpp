#pragma once

#include "stt/whisper_stt.hpp"
#include "stt/streaming_transcriber.hpp"
#include "core/translation_pipeline.hpp"
#include <memory>
#include <string>
#include <functional>
#include <mutex>

namespace stt {

/**
 * Configuration for STT-Translation integration
 */
struct STTTranslationConfig {
    // Confidence thresholds
    float min_transcription_confidence = 0.7f;
    float candidate_confidence_threshold = 0.5f;
    
    // Multiple candidates configuration
    bool enable_multiple_candidates = true;
    int max_transcription_candidates = 3;
    
    // Automatic translation settings
    bool enable_automatic_translation = true;
    bool enable_confidence_gating = true;
    
    // Streaming integration settings
    bool enable_streaming_translation = false; // Translate partial results
    int streaming_translation_delay_ms = 2000; // Delay before translating partial results
};

/**
 * Callback types for integration events
 */
using TranscriptionReadyCallback = std::function<void(uint32_t utteranceId, const TranscriptionResult& result, const std::vector<TranscriptionResult>& candidates)>;
using TranslationTriggeredCallback = std::function<void(uint32_t utteranceId, const std::string& sessionId, bool automatic)>;

/**
 * STT-Translation Integration Manager
 * 
 * Manages the seamless integration between Speech-to-Text and Machine Translation systems.
 * Handles automatic translation triggering, confidence-based gating, and multiple candidate processing.
 */
class STTTranslationIntegration {
public:
    explicit STTTranslationIntegration(const STTTranslationConfig& config = STTTranslationConfig{});
    ~STTTranslationIntegration();
    
    // Non-copyable, non-movable
    STTTranslationIntegration(const STTTranslationIntegration&) = delete;
    STTTranslationIntegration& operator=(const STTTranslationIntegration&) = delete;
    STTTranslationIntegration(STTTranslationIntegration&&) = delete;
    STTTranslationIntegration& operator=(STTTranslationIntegration&&) = delete;
    
    /**
     * Initialize the integration with STT engine and translation pipeline
     */
    bool initialize(
        std::shared_ptr<WhisperSTT> sttEngine,
        std::shared_ptr<speechrnt::core::TranslationPipeline> translationPipeline
    );
    
    /**
     * Initialize with streaming transcriber support
     */
    bool initializeWithStreaming(
        std::shared_ptr<WhisperSTT> sttEngine,
        std::shared_ptr<StreamingTranscriber> streamingTranscriber,
        std::shared_ptr<speechrnt::core::TranslationPipeline> translationPipeline
    );
    
    /**
     * Shutdown the integration
     */
    void shutdown();
    
    /**
     * Process transcription with automatic translation triggering
     */
    void processTranscriptionWithTranslation(
        uint32_t utteranceId,
        const std::string& sessionId,
        const std::vector<float>& audioData,
        bool generateCandidates = true
    );
    
    /**
     * Process streaming transcription with optional translation
     */
    void processStreamingTranscription(
        uint32_t utteranceId,
        const std::string& sessionId,
        const std::vector<float>& audioData
    );
    
    /**
     * Manually trigger translation for a specific transcription
     */
    void triggerManualTranslation(
        uint32_t utteranceId,
        const std::string& sessionId,
        const TranscriptionResult& transcription,
        bool forceTranslation = false
    );
    
    /**
     * Update configuration
     */
    void updateConfiguration(const STTTranslationConfig& config);
    
    /**
     * Get current configuration
     */
    const STTTranslationConfig& getConfiguration() const { return config_; }
    
    /**
     * Set callbacks for integration events
     */
    void setTranscriptionReadyCallback(TranscriptionReadyCallback callback);
    void setTranslationTriggeredCallback(TranslationTriggeredCallback callback);
    
    /**
     * Check if integration is ready
     */
    bool isReady() const;
    
    /**
     * Get integration statistics
     */
    struct Statistics {
        size_t total_transcriptions_processed;
        size_t automatic_translations_triggered;
        size_t manual_translations_triggered;
        size_t confidence_gate_rejections;
        size_t candidates_generated;
        float average_transcription_confidence;
    };
    Statistics getStatistics() const;

private:
    // Configuration
    STTTranslationConfig config_;
    bool initialized_;
    
    // Engine references
    std::shared_ptr<WhisperSTT> sttEngine_;
    std::shared_ptr<StreamingTranscriber> streamingTranscriber_;
    std::shared_ptr<speechrnt::core::TranslationPipeline> translationPipeline_;
    
    // Callbacks
    std::mutex callbacksMutex_;
    TranscriptionReadyCallback transcriptionReadyCallback_;
    TranslationTriggeredCallback translationTriggeredCallback_;
    
    // Statistics
    mutable std::mutex statisticsMutex_;
    Statistics statistics_;
    
    // Internal methods
    void handleTranscriptionComplete(
        uint32_t utteranceId,
        const TranscriptionResult& result,
        const std::vector<TranscriptionResult>& candidates
    );
    
    void generateAndProcessCandidates(
        uint32_t utteranceId,
        const std::string& sessionId,
        const std::vector<float>& audioData,
        const TranscriptionResult& primaryResult
    );
    
    bool shouldTriggerTranslation(const TranscriptionResult& result) const;
    std::vector<TranscriptionResult> filterCandidatesByConfidence(const std::vector<TranscriptionResult>& candidates) const;
    
    void updateStatistics(const TranscriptionResult& result, bool translationTriggered, size_t candidatesCount);
    void notifyTranscriptionReady(uint32_t utteranceId, const TranscriptionResult& result, const std::vector<TranscriptionResult>& candidates);
    void notifyTranslationTriggered(uint32_t utteranceId, const std::string& sessionId, bool automatic);
};

} // namespace stt