#pragma once

#include "stt/advanced/adaptive_quality_manager_interface.hpp"
#include "stt/advanced/advanced_stt_config.hpp"
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <chrono>

namespace stt {
namespace advanced {

/**
 * Resource monitor implementation
 */
class ResourceMonitorImpl : public ResourceMonitor {
public:
    ResourceMonitorImpl();
    ~ResourceMonitorImpl() override;
    
    bool initialize() override;
    SystemResources getCurrentResources() override;
    bool startMonitoring(int intervalMs = 1000) override;
    void stopMonitoring() override;
    void setResourceThresholds(float cpuThreshold, float memoryThreshold, float gpuThreshold) override;
    bool areResourcesConstrained() const override;
    std::vector<SystemResources> getResourceHistory(size_t samples) const override;
    bool isInitialized() const override;

private:
    void monitoringLoop();
    SystemResources collectSystemResources();
    float getCpuUsage();
    float getMemoryUsage();
    float getGpuUsage();
    float getDiskUsage();
    float getNetworkLatency();
    void getMemoryInfo(size_t& available, size_t& total);
    
    mutable std::mutex resourceMutex_;
    std::atomic<bool> initialized_;
    std::atomic<bool> monitoring_;
    std::thread monitoringThread_;
    std::condition_variable monitoringCondition_;
    
    // Resource thresholds
    std::atomic<float> cpuThreshold_;
    std::atomic<float> memoryThreshold_;
    std::atomic<float> gpuThreshold_;
    
    // Resource history
    std::deque<SystemResources> resourceHistory_;
    static const size_t MAX_HISTORY_SIZE = 1000;
    
    // Current resources
    SystemResources currentResources_;
    std::chrono::steady_clock::time_point lastUpdate_;
    
    // Monitoring interval
    std::atomic<int> monitoringIntervalMs_;
};

/**
 * Performance predictor implementation
 */
class PerformancePredictorImpl : public PerformancePredictor {
public:
    PerformancePredictorImpl();
    ~PerformancePredictorImpl() override;
    
    bool initialize() override;
    PerformancePrediction predictPerformance(const QualitySettings& settings,
                                           const SystemResources& resources,
                                           size_t audioLength) override;
    void updateWithActualPerformance(const QualitySettings& settings,
                                   const SystemResources& resources,
                                   size_t audioLength,
                                   float actualLatency,
                                   float actualAccuracy) override;
    QualityLevel getRecommendedQuality(const SystemResources& resources,
                                     const std::vector<TranscriptionRequest>& requests) override;
    bool isInitialized() const override;

private:
    struct PerformanceDataPoint {
        QualitySettings settings;
        SystemResources resources;
        size_t audioLength;
        float latency;
        float accuracy;
        std::chrono::steady_clock::time_point timestamp;
    };
    
    float predictLatencyForSettings(const QualitySettings& settings,
                                   const SystemResources& resources,
                                   size_t audioLength);
    float predictAccuracyForSettings(const QualitySettings& settings,
                                    const SystemResources& resources);
    float calculateResourceScore(const SystemResources& resources);
    float calculateQualityScore(const QualitySettings& settings);
    void updatePredictionModels();
    
    mutable std::mutex dataMutex_;
    std::atomic<bool> initialized_;
    
    // Performance history
    std::deque<PerformanceDataPoint> performanceHistory_;
    static const size_t MAX_PERFORMANCE_HISTORY = 500;
    
    // Prediction models (simplified linear models)
    struct PredictionModel {
        float cpuWeight = 0.3f;
        float memoryWeight = 0.2f;
        float gpuWeight = 0.4f;
        float qualityWeight = 0.1f;
        float baseLatency = 100.0f;
        float baseAccuracy = 0.85f;
        
        // Model parameters learned from data
        float latencyCoefficients[5] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        float accuracyCoefficients[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.1f};
    };
    
    PredictionModel predictionModel_;
    std::chrono::steady_clock::time_point lastModelUpdate_;
};

/**
 * Quality adaptation engine implementation
 */
class QualityAdaptationEngineImpl : public QualityAdaptationEngine {
public:
    QualityAdaptationEngineImpl();
    ~QualityAdaptationEngineImpl() override;
    
    bool initialize() override;
    QualitySettings adaptQuality(const QualitySettings& currentSettings,
                                const SystemResources& resources,
                                const std::vector<TranscriptionRequest>& requests) override;
    void setAdaptationStrategy(const std::string& strategy) override;
    void setQualityConstraints(QualityLevel minQuality, QualityLevel maxQuality) override;
    void setPredictiveAdaptationEnabled(bool enabled) override;
    std::vector<std::pair<SystemResources, QualitySettings>> getAdaptationHistory(size_t samples) const override;
    bool isInitialized() const override;

private:
    enum class AdaptationStrategy {
        CONSERVATIVE,
        BALANCED,
        AGGRESSIVE
    };
    
    QualitySettings adaptConservative(const QualitySettings& current,
                                     const SystemResources& resources,
                                     const std::vector<TranscriptionRequest>& requests);
    QualitySettings adaptBalanced(const QualitySettings& current,
                                 const SystemResources& resources,
                                 const std::vector<TranscriptionRequest>& requests);
    QualitySettings adaptAggressive(const QualitySettings& current,
                                   const SystemResources& resources,
                                   const std::vector<TranscriptionRequest>& requests);
    
    QualityLevel adjustQualityLevel(QualityLevel current, const SystemResources& resources);
    int adjustThreadCount(int current, const SystemResources& resources);
    bool shouldEnableGPU(const SystemResources& resources);
    float adjustConfidenceThreshold(float current, QualityLevel quality);
    size_t adjustBufferSize(size_t current, const SystemResources& resources);
    
    mutable std::mutex adaptationMutex_;
    std::atomic<bool> initialized_;
    
    AdaptationStrategy strategy_;
    QualityLevel minQuality_;
    QualityLevel maxQuality_;
    std::atomic<bool> predictiveAdaptationEnabled_;
    
    // Adaptation history
    std::deque<std::pair<SystemResources, QualitySettings>> adaptationHistory_;
    static const size_t MAX_ADAPTATION_HISTORY = 200;
};

/**
 * Adaptive quality manager implementation
 */
class AdaptiveQualityManager : public AdaptiveQualityManagerInterface {
public:
    AdaptiveQualityManager();
    ~AdaptiveQualityManager() override;
    
    bool initialize(const AdaptiveQualityConfig& config) override;
    QualitySettings adaptQuality(const SystemResources& resources,
                                const std::vector<TranscriptionRequest>& pendingRequests) override;
    void setQualityLevel(QualityLevel level) override;
    void setAdaptiveMode(bool enabled) override;
    SystemResources getCurrentResources() const override;
    void updateResourceSnapshot() override;
    float predictLatency(const QualitySettings& settings, size_t audioLength) const override;
    float predictAccuracy(const QualitySettings& settings) const override;
    void recordActualPerformance(const QualitySettings& settings,
                                size_t audioLength,
                                float actualLatency,
                                float actualAccuracy) override;
    QualitySettings getCurrentQualitySettings() const override;
    void setResourceThresholds(float cpuThreshold, float memoryThreshold, float gpuThreshold) override;
    void setAdaptationInterval(float intervalMs) override;
    void setPredictiveScalingEnabled(bool enabled) override;
    std::string getAdaptationStats() const override;
    std::vector<std::pair<QualitySettings, PerformancePrediction>> getPerformanceHistory(size_t samples) const override;
    bool updateConfiguration(const AdaptiveQualityConfig& config) override;
    AdaptiveQualityConfig getCurrentConfiguration() const override;
    bool isInitialized() const override;
    std::string getLastError() const override;
    void reset() override;

private:
    void startAdaptationLoop();
    void stopAdaptationLoop();
    void adaptationLoop();
    void updateCurrentSettings(const QualitySettings& newSettings);
    bool shouldAdapt(const SystemResources& resources);
    void logAdaptation(const QualitySettings& oldSettings, const QualitySettings& newSettings, const std::string& reason);
    
    mutable std::mutex managerMutex_;
    std::atomic<bool> initialized_;
    std::atomic<bool> adaptiveMode_;
    std::atomic<bool> adaptationLoopRunning_;
    
    // Configuration
    AdaptiveQualityConfig config_;
    
    // Components
    std::unique_ptr<ResourceMonitor> resourceMonitor_;
    std::unique_ptr<PerformancePredictor> performancePredictor_;
    std::unique_ptr<QualityAdaptationEngine> adaptationEngine_;
    
    // Current state
    QualitySettings currentSettings_;
    SystemResources lastResourceSnapshot_;
    std::chrono::steady_clock::time_point lastAdaptation_;
    
    // Adaptation thread
    std::thread adaptationThread_;
    std::condition_variable adaptationCondition_;
    std::atomic<float> adaptationIntervalMs_;
    
    // Performance history
    std::deque<std::pair<QualitySettings, PerformancePrediction>> performanceHistory_;
    static const size_t MAX_PERFORMANCE_HISTORY = 300;
    
    // Error handling
    mutable std::string lastError_;
    
    // Statistics
    struct AdaptationStats {
        size_t totalAdaptations = 0;
        size_t qualityUpgrades = 0;
        size_t qualityDowngrades = 0;
        float averageLatency = 0.0f;
        float averageAccuracy = 0.0f;
        std::chrono::steady_clock::time_point startTime;
        
        AdaptationStats() : startTime(std::chrono::steady_clock::now()) {}
    };
    
    AdaptationStats stats_;
};

} // namespace advanced
} // namespace stt