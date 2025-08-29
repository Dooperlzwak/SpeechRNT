#pragma once

#include "stt/stt_interface.hpp"
#include "stt/transcription_manager.hpp"
#include "stt/whisper_stt.hpp"
#include "stt/advanced/advanced_stt_config.hpp"
#include "stt/advanced/speaker_diarization_interface.hpp"
#include "stt/advanced/audio_preprocessor_interface.hpp"
#include "stt/advanced/contextual_transcriber_interface.hpp"
#include "stt/advanced/realtime_audio_analyzer_interface.hpp"
#include "stt/advanced/adaptive_quality_manager_interface.hpp"
#include "stt/advanced/external_service_integrator_interface.hpp"
#include "stt/advanced/batch_processing_manager_interface.hpp"
#include "stt/advanced/advanced_processing_pipeline.hpp"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <functional>

namespace stt {
namespace advanced {

/**
 * Enhanced transcription result with advanced features
 */
struct AdvancedTranscriptionResult : public TranscriptionResult {
    // Speaker information
    std::vector<SpeakerSegment> speakerSegments;
    uint32_t primarySpeakerId = 0;
    
    // Audio quality metrics
    AudioQualityMetrics audioQuality;
    std::vector<PreprocessingType> appliedPreprocessing;
    
    // Contextual enhancements
    std::vector<ContextualCorrection> contextualCorrections;
    std::string detectedDomain;
    float contextualConfidence = 0.0f;
    
    // Real-time metrics
    RealTimeMetrics realtimeMetrics;
    
    // Quality and performance
    QualityLevel usedQualityLevel = QualityLevel::MEDIUM;
    float processingLatencyMs = 0.0f;
    
    // External service information
    bool usedExternalService = false;
    std::string externalServiceName;
    std::vector<TranscriptionResult> serviceResults; // For result fusion
    
    AdvancedTranscriptionResult() = default;
    
    // Constructor from base TranscriptionResult
    explicit AdvancedTranscriptionResult(const TranscriptionResult& base)
        : TranscriptionResult(base) {}
};

/**
 * Audio processing request with advanced feature options
 */
struct AudioProcessingRequest {
    uint32_t utteranceId = 0;
    std::vector<float> audioData;
    bool isLive = false;
    
    // Feature enablement flags
    bool enableSpeakerDiarization = false;
    bool enableAudioPreprocessing = true;
    bool enableContextualTranscription = false;
    bool enableRealTimeAnalysis = true;
    bool enableAdaptiveQuality = true;
    bool enableExternalServices = false;
    bool enableAllFeatures = false;
    
    // Context information
    std::string domainHint;
    std::string languageHint;
    std::vector<std::string> customVocabulary;
    
    // Quality preferences
    QualityLevel preferredQuality = QualityLevel::MEDIUM;
    float maxLatencyMs = 2000.0f;
    
    // Callback for results
    std::function<void(const AdvancedTranscriptionResult&)> callback;
};

/**
 * Advanced STT Orchestrator
 * Main coordinator for all advanced STT features
 */
class AdvancedSTTOrchestrator {
public:
    AdvancedSTTOrchestrator();
    ~AdvancedSTTOrchestrator();
    
    /**
     * Initialize the orchestrator with advanced features
     * @param config Advanced STT configuration
     * @return true if initialization successful
     */
    bool initializeAdvancedFeatures(const AdvancedSTTConfig& config);
    
    /**
     * Process audio with advanced features
     * @param request Audio processing request with feature options
     * @return Advanced transcription result
     */
    AdvancedTranscriptionResult processAudioWithAdvancedFeatures(const AudioProcessingRequest& request);
    
    /**
     * Process audio asynchronously with advanced features
     * @param request Audio processing request with callback
     */
    void processAudioAsync(const AudioProcessingRequest& request);
    
    /**
     * Enable or disable a specific advanced feature
     * @param feature Feature to enable/disable
     * @param config Feature-specific configuration
     * @return true if successful
     */
    bool enableFeature(AdvancedFeature feature, const FeatureConfig& config);
    
    /**
     * Disable a specific advanced feature
     * @param feature Feature to disable
     * @return true if successful
     */
    bool disableFeature(AdvancedFeature feature);
    
    /**
     * Check if a feature is enabled and healthy
     * @param feature Feature to check
     * @return true if enabled and healthy
     */
    bool isFeatureEnabled(AdvancedFeature feature) const;
    
    /**
     * Get current configuration
     * @return Current advanced STT configuration
     */
    AdvancedSTTConfig getCurrentConfig() const;
    
    /**
     * Update configuration at runtime
     * @param config New configuration
     * @return true if update successful
     */
    bool updateConfiguration(const AdvancedSTTConfig& config);
    
    /**
     * Get feature health status
     * @return Health status for all features
     */
    AdvancedHealthStatus getHealthStatus() const;
    
    /**
     * Get processing metrics
     * @return Current processing metrics
     */
    ProcessingMetrics getProcessingMetrics() const;
    
    /**
     * Reset all advanced features to default state
     */
    void resetAdvancedFeatures();
    
    /**
     * Shutdown all advanced features gracefully
     */
    void shutdown();
    
    /**
     * Check if orchestrator is initialized
     * @return true if initialized
     */
    bool isInitialized() const { return initialized_; }
    
    /**
     * Get last error message
     * @return Last error message
     */
    std::string getLastError() const;

private:
    // Core STT components (existing)
    std::shared_ptr<WhisperSTT> whisperSTT_;
    std::shared_ptr<TranscriptionManager> transcriptionManager_;
    
    // Advanced feature components
    std::unique_ptr<SpeakerDiarizationInterface> speakerEngine_;
    std::unique_ptr<AudioPreprocessorInterface> audioPreprocessor_;
    std::unique_ptr<ContextualTranscriberInterface> contextualTranscriber_;
    std::unique_ptr<RealTimeAudioAnalyzerInterface> audioAnalyzer_;
    std::unique_ptr<AdaptiveQualityManagerInterface> qualityManager_;
    std::unique_ptr<ExternalServiceIntegratorInterface> externalServices_;
    std::unique_ptr<BatchProcessingManagerInterface> batchProcessor_;
    
    // Processing pipeline
    std::unique_ptr<AdvancedProcessingPipeline> pipeline_;
    
    // Configuration and state
    AdvancedSTTConfig config_;
    std::unordered_map<AdvancedFeature, bool> featureStates_;
    std::unordered_map<AdvancedFeature, FeatureConfig> featureConfigs_;
    
    // Thread safety
    mutable std::mutex orchestratorMutex_;
    mutable std::mutex configMutex_;
    
    // Status tracking
    std::atomic<bool> initialized_;
    std::string lastError_;
    
    // Metrics tracking
    ProcessingMetrics processingMetrics_;
    mutable std::mutex metricsMutex_;
    
    // Helper methods
    bool initializeFeature(AdvancedFeature feature, const FeatureConfig& config);
    void shutdownFeature(AdvancedFeature feature);
    bool validateConfiguration(const AdvancedSTTConfig& config) const;
    void updateFeatureStates();
    void updateProcessingMetrics(const AdvancedTranscriptionResult& result);
    AdvancedTranscriptionResult processWithPipeline(const AudioProcessingRequest& request);
    void handleFeatureError(AdvancedFeature feature, const std::string& error);
    bool isFeatureHealthy(AdvancedFeature feature) const;
    void logFeatureStatus() const;
};

} // namespace advanced
} // namespace stt