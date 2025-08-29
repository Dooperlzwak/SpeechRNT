#pragma once

#include "stt/advanced/advanced_stt_config.hpp"
#include "stt/advanced/speaker_diarization_interface.hpp"
#include "stt/advanced/audio_preprocessor_interface.hpp"
#include "stt/advanced/contextual_transcriber_interface.hpp"
#include "stt/advanced/realtime_audio_analyzer_interface.hpp"
#include "stt/advanced/adaptive_quality_manager_interface.hpp"
#include "stt/advanced/external_service_integrator_interface.hpp"
#include "stt/stt_interface.hpp"
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <chrono>

namespace stt {
namespace advanced {

/**
 * Pipeline stage enumeration
 */
enum class PipelineStage {
    AUDIO_PREPROCESSING,
    REALTIME_ANALYSIS,
    QUALITY_ADAPTATION,
    SPEAKER_DIARIZATION,
    TRANSCRIPTION,
    CONTEXTUAL_ENHANCEMENT,
    EXTERNAL_SERVICE_FUSION,
    RESULT_FINALIZATION
};

/**
 * Pipeline stage result
 */
struct PipelineStageResult {
    PipelineStage stage;
    bool success;
    float processingTimeMs;
    std::string errorMessage;
    std::map<std::string, std::string> stageMetadata;
    
    PipelineStageResult() 
        : stage(PipelineStage::AUDIO_PREPROCESSING)
        , success(false)
        , processingTimeMs(0.0f) {}
    
    PipelineStageResult(PipelineStage s, bool succ, float time)
        : stage(s), success(succ), processingTimeMs(time) {}
};

/**
 * Pipeline execution context
 */
struct PipelineExecutionContext {
    uint32_t utteranceId;
    std::vector<float> originalAudio;
    std::vector<float> processedAudio;
    int sampleRate;
    bool isRealTime;
    
    // Stage-specific data
    AudioQualityMetrics audioQuality;
    RealTimeMetrics realtimeMetrics;
    QualitySettings qualitySettings;
    DiarizationResult speakerInfo;
    TranscriptionResult baseTranscription;
    ContextualResult contextualEnhancement;
    FusedTranscriptionResult externalServiceResult;
    
    // Processing metadata
    std::vector<PipelineStageResult> stageResults;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
    
    // Configuration
    AudioProcessingRequest originalRequest;
    
    PipelineExecutionContext() 
        : utteranceId(0)
        , sampleRate(16000)
        , isRealTime(false)
        , startTime(std::chrono::steady_clock::now())
        , endTime(std::chrono::steady_clock::now()) {}
};

/**
 * Pipeline configuration
 */
struct PipelineConfig {
    std::vector<PipelineStage> enabledStages;
    bool enableParallelProcessing;
    bool enableStageSkipping; // Skip stages on failure
    bool enableStageRetry;
    int maxRetryAttempts;
    float stageTimeoutMs;
    bool enableProfiling;
    std::map<PipelineStage, std::map<std::string, std::string>> stageConfigs;
    
    PipelineConfig() 
        : enableParallelProcessing(true)
        , enableStageSkipping(true)
        , enableStageRetry(true)
        , maxRetryAttempts(2)
        , stageTimeoutMs(5000.0f)
        , enableProfiling(false) {
        
        // Default enabled stages
        enabledStages = {
            PipelineStage::AUDIO_PREPROCESSING,
            PipelineStage::REALTIME_ANALYSIS,
            PipelineStage::QUALITY_ADAPTATION,
            PipelineStage::TRANSCRIPTION,
            PipelineStage::RESULT_FINALIZATION
        };
    }
};

/**
 * Pipeline stage processor interface
 */
class PipelineStageProcessor {
public:
    virtual ~PipelineStageProcessor() = default;
    
    /**
     * Get stage type
     * @return Pipeline stage type
     */
    virtual PipelineStage getStageType() const = 0;
    
    /**
     * Process pipeline stage
     * @param context Pipeline execution context
     * @return Stage processing result
     */
    virtual PipelineStageResult processStage(PipelineExecutionContext& context) = 0;
    
    /**
     * Check if stage can be skipped
     * @param context Pipeline execution context
     * @return true if stage can be skipped
     */
    virtual bool canSkipStage(const PipelineExecutionContext& context) const = 0;
    
    /**
     * Get stage dependencies
     * @return Vector of required preceding stages
     */
    virtual std::vector<PipelineStage> getStageDependencies() const = 0;
    
    /**
     * Validate stage prerequisites
     * @param context Pipeline execution context
     * @return true if prerequisites are met
     */
    virtual bool validatePrerequisites(const PipelineExecutionContext& context) const = 0;
    
    /**
     * Get estimated processing time
     * @param context Pipeline execution context
     * @return Estimated processing time in milliseconds
     */
    virtual float getEstimatedProcessingTime(const PipelineExecutionContext& context) const = 0;
    
    /**
     * Check if processor is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
    
    /**
     * Get last error message
     * @return Last error message
     */
    virtual std::string getLastError() const = 0;
};

/**
 * Pipeline execution monitor interface
 */
class PipelineExecutionMonitor {
public:
    virtual ~PipelineExecutionMonitor() = default;
    
    /**
     * Initialize monitor
     * @return true if initialization successful
     */
    virtual bool initialize() = 0;
    
    /**
     * Start monitoring pipeline execution
     * @param context Pipeline execution context
     */
    virtual void startExecution(const PipelineExecutionContext& context) = 0;
    
    /**
     * Record stage completion
     * @param stage Pipeline stage
     * @param result Stage result
     */
    virtual void recordStageCompletion(PipelineStage stage, const PipelineStageResult& result) = 0;
    
    /**
     * Finish monitoring pipeline execution
     * @param context Pipeline execution context
     */
    virtual void finishExecution(const PipelineExecutionContext& context) = 0;
    
    /**
     * Get execution statistics
     * @return Statistics as JSON string
     */
    virtual std::string getExecutionStats() const = 0;
    
    /**
     * Get stage performance metrics
     * @param stage Pipeline stage
     * @return Performance metrics for the stage
     */
    virtual std::map<std::string, float> getStageMetrics(PipelineStage stage) const = 0;
    
    /**
     * Check if monitor is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Advanced processing pipeline
 */
class AdvancedProcessingPipeline {
public:
    AdvancedProcessingPipeline();
    ~AdvancedProcessingPipeline();
    
    /**
     * Initialize the processing pipeline
     * @param config Pipeline configuration
     * @return true if initialization successful
     */
    bool initialize(const PipelineConfig& config);
    
    /**
     * Set component references
     * @param speakerEngine Speaker diarization engine
     * @param audioPreprocessor Audio preprocessor
     * @param contextualTranscriber Contextual transcriber
     * @param audioAnalyzer Real-time audio analyzer
     * @param qualityManager Adaptive quality manager
     * @param externalServices External service integrator
     * @param baseSTT Base STT engine
     */
    void setComponents(
        std::shared_ptr<SpeakerDiarizationInterface> speakerEngine,
        std::shared_ptr<AudioPreprocessorInterface> audioPreprocessor,
        std::shared_ptr<ContextualTranscriberInterface> contextualTranscriber,
        std::shared_ptr<RealTimeAudioAnalyzerInterface> audioAnalyzer,
        std::shared_ptr<AdaptiveQualityManagerInterface> qualityManager,
        std::shared_ptr<ExternalServiceIntegratorInterface> externalServices,
        std::shared_ptr<STTInterface> baseSTT
    );
    
    /**
     * Process audio through the advanced pipeline
     * @param request Audio processing request
     * @return Advanced transcription result
     */
    AdvancedTranscriptionResult processAudio(const AudioProcessingRequest& request);
    
    /**
     * Process audio asynchronously
     * @param request Audio processing request
     * @param callback Callback for result
     */
    void processAudioAsync(const AudioProcessingRequest& request,
                          std::function<void(const AdvancedTranscriptionResult&)> callback);
    
    /**
     * Enable or disable pipeline stage
     * @param stage Pipeline stage
     * @param enabled true to enable stage
     */
    void setStageEnabled(PipelineStage stage, bool enabled);
    
    /**
     * Check if pipeline stage is enabled
     * @param stage Pipeline stage
     * @return true if stage is enabled
     */
    bool isStageEnabled(PipelineStage stage) const;
    
    /**
     * Set stage configuration
     * @param stage Pipeline stage
     * @param config Stage-specific configuration
     */
    void setStageConfig(PipelineStage stage, const std::map<std::string, std::string>& config);
    
    /**
     * Get stage configuration
     * @param stage Pipeline stage
     * @return Stage-specific configuration
     */
    std::map<std::string, std::string> getStageConfig(PipelineStage stage) const;
    
    /**
     * Get pipeline execution statistics
     * @return Statistics as JSON string
     */
    std::string getPipelineStats() const;
    
    /**
     * Get stage performance metrics
     * @param stage Pipeline stage
     * @return Performance metrics for the stage
     */
    std::map<std::string, float> getStageMetrics(PipelineStage stage) const;
    
    /**
     * Reset pipeline statistics
     */
    void resetStats();
    
    /**
     * Update pipeline configuration
     * @param config New pipeline configuration
     * @return true if update successful
     */
    bool updateConfiguration(const PipelineConfig& config);
    
    /**
     * Get current pipeline configuration
     * @return Current pipeline configuration
     */
    PipelineConfig getCurrentConfiguration() const;
    
    /**
     * Validate pipeline configuration
     * @param config Pipeline configuration to validate
     * @return true if configuration is valid
     */
    bool validateConfiguration(const PipelineConfig& config) const;
    
    /**
     * Get estimated processing time
     * @param request Audio processing request
     * @return Estimated processing time in milliseconds
     */
    float getEstimatedProcessingTime(const AudioProcessingRequest& request) const;
    
    /**
     * Check if pipeline is initialized
     * @return true if initialized
     */
    bool isInitialized() const { return initialized_; }
    
    /**
     * Get last error message
     * @return Last error message
     */
    std::string getLastError() const;
    
    /**
     * Shutdown pipeline gracefully
     */
    void shutdown();

private:
    // Component references
    std::shared_ptr<SpeakerDiarizationInterface> speakerEngine_;
    std::shared_ptr<AudioPreprocessorInterface> audioPreprocessor_;
    std::shared_ptr<ContextualTranscriberInterface> contextualTranscriber_;
    std::shared_ptr<RealTimeAudioAnalyzerInterface> audioAnalyzer_;
    std::shared_ptr<AdaptiveQualityManagerInterface> qualityManager_;
    std::shared_ptr<ExternalServiceIntegratorInterface> externalServices_;
    std::shared_ptr<STTInterface> baseSTT_;
    
    // Pipeline configuration and state
    PipelineConfig config_;
    std::atomic<bool> initialized_;
    std::string lastError_;
    
    // Stage processors
    std::map<PipelineStage, std::unique_ptr<PipelineStageProcessor>> stageProcessors_;
    
    // Monitoring
    std::unique_ptr<PipelineExecutionMonitor> executionMonitor_;
    
    // Helper methods
    bool initializeStageProcessors();
    bool validateStageOrder(const std::vector<PipelineStage>& stages) const;
    std::vector<PipelineStage> resolveStageDependencies(const std::vector<PipelineStage>& requestedStages) const;
    PipelineExecutionContext createExecutionContext(const AudioProcessingRequest& request);
    AdvancedTranscriptionResult finalizeResult(const PipelineExecutionContext& context);
    void handleStageError(PipelineStage stage, const std::string& error, PipelineExecutionContext& context);
    bool shouldSkipStage(PipelineStage stage, const PipelineExecutionContext& context) const;
    bool retryStage(PipelineStage stage, PipelineExecutionContext& context);
    std::string stageToString(PipelineStage stage) const;
    PipelineStage stringToStage(const std::string& stageStr) const;
};

} // namespace advanced
} // namespace stt