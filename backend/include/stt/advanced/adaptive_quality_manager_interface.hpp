#pragma once

#include "stt/advanced/advanced_stt_config.hpp"
#include "stt/stt_interface.hpp"
#include <vector>
#include <string>
#include <memory>
#include <chrono>

namespace stt {
namespace advanced {

/**
 * System resource information
 */
struct SystemResources {
    float cpuUsage;           // 0.0 to 1.0
    float memoryUsage;        // 0.0 to 1.0
    float gpuUsage;           // 0.0 to 1.0
    size_t activeTranscriptions;
    float averageLatency;
    bool resourceConstrained;
    float diskUsage;          // 0.0 to 1.0
    float networkLatency;     // milliseconds
    size_t availableMemoryMB;
    size_t totalMemoryMB;
    
    SystemResources() 
        : cpuUsage(0.0f)
        , memoryUsage(0.0f)
        , gpuUsage(0.0f)
        , activeTranscriptions(0)
        , averageLatency(0.0f)
        , resourceConstrained(false)
        , diskUsage(0.0f)
        , networkLatency(0.0f)
        , availableMemoryMB(0)
        , totalMemoryMB(0) {}
};

/**
 * Quality settings for transcription
 */
struct QualitySettings {
    QualityLevel level;
    int threadCount;
    bool enableGPU;
    float confidenceThreshold;
    bool enablePreprocessing;
    size_t maxBufferSize;
    float temperatureSetting;
    int maxTokens;
    bool enableQuantization;
    std::string quantizationLevel;
    
    QualitySettings() 
        : level(QualityLevel::MEDIUM)
        , threadCount(4)
        , enableGPU(true)
        , confidenceThreshold(0.5f)
        , enablePreprocessing(true)
        , maxBufferSize(1024)
        , temperatureSetting(0.0f)
        , maxTokens(0)
        , enableQuantization(false)
        , quantizationLevel("AUTO") {}
};

/**
 * Transcription request information
 */
struct TranscriptionRequest {
    uint32_t requestId;
    size_t audioLength;
    bool isRealTime;
    QualityLevel requestedQuality;
    float maxLatencyMs;
    std::string language;
    bool enableAdvancedFeatures;
    std::chrono::steady_clock::time_point submissionTime;
    
    TranscriptionRequest() 
        : requestId(0)
        , audioLength(0)
        , isRealTime(false)
        , requestedQuality(QualityLevel::MEDIUM)
        , maxLatencyMs(2000.0f)
        , enableAdvancedFeatures(false)
        , submissionTime(std::chrono::steady_clock::now()) {}
};

/**
 * Performance prediction data
 */
struct PerformancePrediction {
    float predictedLatencyMs;
    float predictedAccuracy;
    float confidenceInPrediction;
    QualityLevel recommendedQuality;
    std::string reasoning;
    
    PerformancePrediction() 
        : predictedLatencyMs(0.0f)
        , predictedAccuracy(0.0f)
        , confidenceInPrediction(0.0f)
        , recommendedQuality(QualityLevel::MEDIUM) {}
};

/**
 * Resource monitor interface
 */
class ResourceMonitor {
public:
    virtual ~ResourceMonitor() = default;
    
    /**
     * Initialize resource monitoring
     * @return true if initialization successful
     */
    virtual bool initialize() = 0;
    
    /**
     * Get current system resources
     * @return Current resource usage
     */
    virtual SystemResources getCurrentResources() = 0;
    
    /**
     * Start continuous monitoring
     * @param intervalMs Monitoring interval in milliseconds
     * @return true if started successfully
     */
    virtual bool startMonitoring(int intervalMs = 1000) = 0;
    
    /**
     * Stop continuous monitoring
     */
    virtual void stopMonitoring() = 0;
    
    /**
     * Set resource thresholds for alerts
     * @param cpuThreshold CPU usage threshold (0.0 to 1.0)
     * @param memoryThreshold Memory usage threshold (0.0 to 1.0)
     * @param gpuThreshold GPU usage threshold (0.0 to 1.0)
     */
    virtual void setResourceThresholds(float cpuThreshold, 
                                      float memoryThreshold, 
                                      float gpuThreshold) = 0;
    
    /**
     * Check if resources are constrained
     * @return true if resources are constrained
     */
    virtual bool areResourcesConstrained() const = 0;
    
    /**
     * Get resource history
     * @param samples Number of historical samples
     * @return Vector of historical resource data
     */
    virtual std::vector<SystemResources> getResourceHistory(size_t samples) const = 0;
    
    /**
     * Check if monitor is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Performance predictor interface
 */
class PerformancePredictor {
public:
    virtual ~PerformancePredictor() = default;
    
    /**
     * Initialize performance predictor
     * @return true if initialization successful
     */
    virtual bool initialize() = 0;
    
    /**
     * Predict performance for given settings and resources
     * @param settings Quality settings
     * @param resources Current system resources
     * @param audioLength Length of audio to process
     * @return Performance prediction
     */
    virtual PerformancePrediction predictPerformance(const QualitySettings& settings,
                                                    const SystemResources& resources,
                                                    size_t audioLength) = 0;
    
    /**
     * Update predictor with actual performance data
     * @param settings Settings used
     * @param resources Resources during processing
     * @param audioLength Audio length processed
     * @param actualLatency Actual processing latency
     * @param actualAccuracy Actual transcription accuracy
     */
    virtual void updateWithActualPerformance(const QualitySettings& settings,
                                            const SystemResources& resources,
                                            size_t audioLength,
                                            float actualLatency,
                                            float actualAccuracy) = 0;
    
    /**
     * Get recommended quality level for current conditions
     * @param resources Current system resources
     * @param requests Pending transcription requests
     * @return Recommended quality level
     */
    virtual QualityLevel getRecommendedQuality(const SystemResources& resources,
                                              const std::vector<TranscriptionRequest>& requests) = 0;
    
    /**
     * Check if predictor is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Quality adaptation engine interface
 */
class QualityAdaptationEngine {
public:
    virtual ~QualityAdaptationEngine() = default;
    
    /**
     * Initialize adaptation engine
     * @return true if initialization successful
     */
    virtual bool initialize() = 0;
    
    /**
     * Adapt quality based on current conditions
     * @param currentSettings Current quality settings
     * @param resources Current system resources
     * @param requests Pending transcription requests
     * @return Adapted quality settings
     */
    virtual QualitySettings adaptQuality(const QualitySettings& currentSettings,
                                        const SystemResources& resources,
                                        const std::vector<TranscriptionRequest>& requests) = 0;
    
    /**
     * Set adaptation strategy
     * @param strategy Adaptation strategy ("conservative", "aggressive", "balanced")
     */
    virtual void setAdaptationStrategy(const std::string& strategy) = 0;
    
    /**
     * Set quality constraints
     * @param minQuality Minimum allowed quality level
     * @param maxQuality Maximum allowed quality level
     */
    virtual void setQualityConstraints(QualityLevel minQuality, QualityLevel maxQuality) = 0;
    
    /**
     * Enable or disable predictive adaptation
     * @param enabled true to enable predictive adaptation
     */
    virtual void setPredictiveAdaptationEnabled(bool enabled) = 0;
    
    /**
     * Get adaptation history
     * @param samples Number of historical adaptations
     * @return Vector of historical adaptations
     */
    virtual std::vector<std::pair<SystemResources, QualitySettings>> getAdaptationHistory(size_t samples) const = 0;
    
    /**
     * Check if engine is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Adaptive quality manager interface
 */
class AdaptiveQualityManagerInterface {
public:
    virtual ~AdaptiveQualityManagerInterface() = default;
    
    /**
     * Initialize the adaptive quality manager
     * @param config Adaptive quality configuration
     * @return true if initialization successful
     */
    virtual bool initialize(const AdaptiveQualityConfig& config) = 0;
    
    /**
     * Adapt quality based on current conditions
     * @param resources Current system resources
     * @param pendingRequests Pending transcription requests
     * @return Adapted quality settings
     */
    virtual QualitySettings adaptQuality(const SystemResources& resources,
                                        const std::vector<TranscriptionRequest>& pendingRequests) = 0;
    
    /**
     * Set quality level manually
     * @param level Quality level to set
     */
    virtual void setQualityLevel(QualityLevel level) = 0;
    
    /**
     * Enable or disable adaptive mode
     * @param enabled true to enable adaptive mode
     */
    virtual void setAdaptiveMode(bool enabled) = 0;
    
    /**
     * Get current system resources
     * @return Current resource usage
     */
    virtual SystemResources getCurrentResources() const = 0;
    
    /**
     * Update resource snapshot
     */
    virtual void updateResourceSnapshot() = 0;
    
    /**
     * Predict latency for given settings
     * @param settings Quality settings
     * @param audioLength Length of audio to process
     * @return Predicted latency in milliseconds
     */
    virtual float predictLatency(const QualitySettings& settings, size_t audioLength) const = 0;
    
    /**
     * Predict accuracy for given settings
     * @param settings Quality settings
     * @return Predicted accuracy (0.0 to 1.0)
     */
    virtual float predictAccuracy(const QualitySettings& settings) const = 0;
    
    /**
     * Record actual performance for learning
     * @param settings Settings used
     * @param audioLength Audio length processed
     * @param actualLatency Actual processing latency
     * @param actualAccuracy Actual transcription accuracy
     */
    virtual void recordActualPerformance(const QualitySettings& settings,
                                        size_t audioLength,
                                        float actualLatency,
                                        float actualAccuracy) = 0;
    
    /**
     * Get current quality settings
     * @return Current quality settings
     */
    virtual QualitySettings getCurrentQualitySettings() const = 0;
    
    /**
     * Set resource thresholds
     * @param cpuThreshold CPU usage threshold (0.0 to 1.0)
     * @param memoryThreshold Memory usage threshold (0.0 to 1.0)
     * @param gpuThreshold GPU usage threshold (0.0 to 1.0)
     */
    virtual void setResourceThresholds(float cpuThreshold, 
                                      float memoryThreshold, 
                                      float gpuThreshold) = 0;
    
    /**
     * Set adaptation interval
     * @param intervalMs Adaptation check interval in milliseconds
     */
    virtual void setAdaptationInterval(float intervalMs) = 0;
    
    /**
     * Enable or disable predictive scaling
     * @param enabled true to enable predictive scaling
     */
    virtual void setPredictiveScalingEnabled(bool enabled) = 0;
    
    /**
     * Get adaptation statistics
     * @return Statistics as JSON string
     */
    virtual std::string getAdaptationStats() const = 0;
    
    /**
     * Get performance history
     * @param samples Number of historical samples
     * @return Vector of performance data
     */
    virtual std::vector<std::pair<QualitySettings, PerformancePrediction>> getPerformanceHistory(size_t samples) const = 0;
    
    /**
     * Update configuration
     * @param config New adaptive quality configuration
     * @return true if update successful
     */
    virtual bool updateConfiguration(const AdaptiveQualityConfig& config) = 0;
    
    /**
     * Get current configuration
     * @return Current adaptive quality configuration
     */
    virtual AdaptiveQualityConfig getCurrentConfiguration() const = 0;
    
    /**
     * Check if manager is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
    
    /**
     * Get last error message
     * @return Last error message
     */
    virtual std::string getLastError() const = 0;
    
    /**
     * Reset manager state
     */
    virtual void reset() = 0;
};

} // namespace advanced
} // namespace stt