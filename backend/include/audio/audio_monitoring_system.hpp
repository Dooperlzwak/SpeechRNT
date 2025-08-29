#pragma once

#include "audio/realtime_audio_analyzer.hpp"
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <queue>
#include <unordered_map>
#include <string>
#include <condition_variable>

namespace audio {

// Forward declarations
class MetricsFilter;
class MetricsAggregator;

// Callback subscription configuration
struct CallbackConfig {
    std::string callbackId;                          // Unique identifier for the callback
    std::chrono::milliseconds updateInterval;        // How often to call the callback
    bool enableFiltering;                           // Whether to apply filtering
    bool enableAggregation;                         // Whether to aggregate metrics over time
    size_t historyBufferSize;                       // Size of history buffer for this callback
    float minChangeThreshold;                       // Minimum change required to trigger callback
    std::vector<std::string> enabledMetrics;        // Which metrics to include
    
    CallbackConfig() 
        : updateInterval(std::chrono::milliseconds(100)), enableFiltering(false), 
          enableAggregation(false), historyBufferSize(100), minChangeThreshold(0.01f) {}
};

// Metrics filter for reducing callback noise
class MetricsFilter {
public:
    enum class FilterType {
        NONE,
        LOW_PASS,           // Smooth out rapid changes
        THRESHOLD,          // Only pass changes above threshold
        RATE_LIMIT,         // Limit update rate
        CHANGE_DETECTION    // Only pass when significant change detected
    };
    
    struct FilterConfig {
        FilterType type;
        float parameter1;   // Filter-specific parameter (e.g., cutoff frequency, threshold)
        float parameter2;   // Second parameter if needed
        
        FilterConfig(FilterType t = FilterType::NONE, float p1 = 0.0f, float p2 = 0.0f)
            : type(t), parameter1(p1), parameter2(p2) {}
    };
    
    explicit MetricsFilter(const FilterConfig& config);
    ~MetricsFilter() = default;
    
    // Filter operations
    bool shouldPassMetrics(const RealTimeMetrics& current, const RealTimeMetrics& previous);
    RealTimeMetrics filterMetrics(const RealTimeMetrics& metrics);
    void updateConfig(const FilterConfig& config);
    void reset();
    
private:
    FilterConfig config_;
    RealTimeMetrics lastPassedMetrics_;
    std::chrono::steady_clock::time_point lastPassTime_;
    float filterState_;
    
    bool applyLowPassFilter(const RealTimeMetrics& current, const RealTimeMetrics& previous);
    bool applyThresholdFilter(const RealTimeMetrics& current, const RealTimeMetrics& previous);
    bool applyRateLimitFilter();
    bool applyChangeDetectionFilter(const RealTimeMetrics& current, const RealTimeMetrics& previous);
    float calculateMetricsDistance(const RealTimeMetrics& a, const RealTimeMetrics& b);
};

// Metrics aggregator for combining metrics over time windows
class MetricsAggregator {
public:
    enum class AggregationType {
        NONE,
        AVERAGE,        // Average over time window
        MIN_MAX,        // Min/max over time window
        PEAK_HOLD,      // Peak hold with decay
        TREND_ANALYSIS  // Trend analysis over time window
    };
    
    struct AggregationConfig {
        AggregationType type;
        std::chrono::milliseconds timeWindow;
        float decayRate;        // For peak hold
        size_t sampleCount;     // Number of samples to aggregate
        
        AggregationConfig(AggregationType t = AggregationType::NONE, 
                         std::chrono::milliseconds window = std::chrono::milliseconds(1000))
            : type(t), timeWindow(window), decayRate(0.95f), sampleCount(10) {}
    };
    
    struct AggregatedMetrics {
        RealTimeMetrics current;
        RealTimeMetrics average;
        RealTimeMetrics minimum;
        RealTimeMetrics maximum;
        RealTimeMetrics trend;      // Trend direction and magnitude
        float stability;            // Stability measure (0.0 = very unstable, 1.0 = very stable)
        size_t sampleCount;
        std::chrono::milliseconds timeSpan;
    };
    
    explicit MetricsAggregator(const AggregationConfig& config);
    ~MetricsAggregator() = default;
    
    // Aggregation operations
    void addMetrics(const RealTimeMetrics& metrics);
    AggregatedMetrics getAggregatedMetrics() const;
    void updateConfig(const AggregationConfig& config);
    void reset();
    
    // History access
    std::vector<RealTimeMetrics> getHistory(size_t count) const;
    std::vector<RealTimeMetrics> getHistoryInTimeRange(std::chrono::milliseconds timeRange) const;
    
private:
    AggregationConfig config_;
    mutable std::mutex historyMutex_;
    std::deque<RealTimeMetrics> metricsHistory_;
    AggregatedMetrics aggregatedMetrics_;
    
    void updateAggregation();
    void cleanupOldMetrics();
    RealTimeMetrics calculateAverage(const std::deque<RealTimeMetrics>& metrics) const;
    RealTimeMetrics calculateMinimum(const std::deque<RealTimeMetrics>& metrics) const;
    RealTimeMetrics calculateMaximum(const std::deque<RealTimeMetrics>& metrics) const;
    RealTimeMetrics calculateTrend(const std::deque<RealTimeMetrics>& metrics) const;
    float calculateStability(const std::deque<RealTimeMetrics>& metrics) const;
};

// Callback subscription manager
class CallbackSubscription {
public:
    using MetricsCallback = std::function<void(const RealTimeMetrics&)>;
    using AggregatedMetricsCallback = std::function<void(const MetricsAggregator::AggregatedMetrics&)>;
    using LevelsCallback = std::function<void(const AudioLevelMetrics&)>;
    using SpectralCallback = std::function<void(const SpectralAnalysis&)>;
    
    CallbackSubscription(const std::string& id, const CallbackConfig& config);
    ~CallbackSubscription() = default;
    
    // Callback registration
    void setMetricsCallback(MetricsCallback callback);
    void setAggregatedMetricsCallback(AggregatedMetricsCallback callback);
    void setLevelsCallback(LevelsCallback callback);
    void setSpectralCallback(SpectralCallback callback);
    
    // Configuration
    const std::string& getId() const { return id_; }
    const CallbackConfig& getConfig() const { return config_; }
    void updateConfig(const CallbackConfig& config);
    
    // Filtering and aggregation
    void setFilter(const MetricsFilter::FilterConfig& filterConfig);
    void setAggregator(const MetricsAggregator::AggregationConfig& aggregationConfig);
    
    // Metrics processing
    bool processMetrics(const RealTimeMetrics& metrics);
    void processLevels(const AudioLevelMetrics& levels);
    void processSpectral(const SpectralAnalysis& spectral);
    
    // State management
    bool isActive() const { return active_; }
    void setActive(bool active) { active_ = active; }
    
    // Statistics
    struct SubscriptionStats {
        size_t totalCallbacks;
        size_t filteredCallbacks;
        std::chrono::steady_clock::time_point lastCallback;
        float averageCallbackInterval;
        size_t droppedCallbacks;
    };
    
    SubscriptionStats getStats() const;
    void resetStats();
    
private:
    std::string id_;
    CallbackConfig config_;
    std::atomic<bool> active_;
    
    // Callbacks
    MetricsCallback metricsCallback_;
    AggregatedMetricsCallback aggregatedMetricsCallback_;
    LevelsCallback levelsCallback_;
    SpectralCallback spectralCallback_;
    
    // Processing components
    std::unique_ptr<MetricsFilter> filter_;
    std::unique_ptr<MetricsAggregator> aggregator_;
    
    // Timing control
    std::chrono::steady_clock::time_point lastCallbackTime_;
    RealTimeMetrics lastMetrics_;
    
    // Statistics
    mutable std::mutex statsMutex_;
    SubscriptionStats stats_;
    
    bool shouldTriggerCallback(const RealTimeMetrics& metrics);
    void updateStats(bool callbackTriggered);
};

// Main audio monitoring callback system
class AudioMonitoringSystem {
public:
    explicit AudioMonitoringSystem(std::shared_ptr<RealTimeAudioAnalyzer> analyzer);
    ~AudioMonitoringSystem();
    
    // System lifecycle
    bool initialize();
    void shutdown();
    bool isRunning() const { return running_; }
    
    // Subscription management
    std::string subscribe(const CallbackConfig& config, 
                         CallbackSubscription::MetricsCallback callback);
    std::string subscribeAggregated(const CallbackConfig& config,
                                   CallbackSubscription::AggregatedMetricsCallback callback);
    std::string subscribeLevels(const CallbackConfig& config,
                                CallbackSubscription::LevelsCallback callback);
    std::string subscribeSpectral(const CallbackConfig& config,
                                 CallbackSubscription::SpectralCallback callback);
    
    bool unsubscribe(const std::string& subscriptionId);
    void unsubscribeAll();
    
    // Subscription configuration
    bool updateSubscriptionConfig(const std::string& subscriptionId, const CallbackConfig& config);
    bool setSubscriptionFilter(const std::string& subscriptionId, 
                              const MetricsFilter::FilterConfig& filterConfig);
    bool setSubscriptionAggregator(const std::string& subscriptionId,
                                  const MetricsAggregator::AggregationConfig& aggregationConfig);
    
    // Subscription control
    bool activateSubscription(const std::string& subscriptionId);
    bool deactivateSubscription(const std::string& subscriptionId);
    std::vector<std::string> getActiveSubscriptions() const;
    std::vector<std::string> getAllSubscriptions() const;
    
    // Global configuration
    void setGlobalUpdateInterval(std::chrono::milliseconds interval);
    void setMaxSubscriptions(size_t maxSubscriptions);
    void setThreadPoolSize(size_t threadCount);
    
    // Metrics history and analysis
    std::vector<RealTimeMetrics> getGlobalMetricsHistory(size_t count) const;
    MetricsAggregator::AggregatedMetrics getGlobalAggregatedMetrics() const;
    
    // Performance monitoring
    struct SystemPerformance {
        size_t activeSubscriptions;
        size_t totalCallbacks;
        float averageProcessingTime;
        float maxProcessingTime;
        size_t droppedCallbacks;
        float cpuUsage;
        size_t memoryUsage;
    };
    
    SystemPerformance getPerformance() const;
    void resetPerformanceCounters();
    
    // Health monitoring
    struct SystemHealth {
        bool isHealthy;
        std::vector<std::string> issues;
        std::vector<std::string> warnings;
        float overallScore;  // 0.0 to 1.0
    };
    
    SystemHealth getSystemHealth() const;
    
    // Debug and diagnostics
    void enableDebugMode(bool enabled);
    std::vector<std::string> getDebugInfo() const;
    void dumpSubscriptionStates() const;
    
private:
    // Core components
    std::shared_ptr<RealTimeAudioAnalyzer> analyzer_;
    std::atomic<bool> running_;
    std::atomic<bool> debugMode_;
    
    // Configuration
    std::chrono::milliseconds globalUpdateInterval_;
    size_t maxSubscriptions_;
    size_t threadPoolSize_;
    
    // Subscription management
    mutable std::mutex subscriptionsMutex_;
    std::unordered_map<std::string, std::unique_ptr<CallbackSubscription>> subscriptions_;
    std::atomic<uint32_t> nextSubscriptionId_;
    
    // Global metrics aggregation
    std::unique_ptr<MetricsAggregator> globalAggregator_;
    mutable std::mutex globalMetricsMutex_;
    std::deque<RealTimeMetrics> globalMetricsHistory_;
    
    // Processing threads
    std::vector<std::unique_ptr<std::thread>> processingThreads_;
    std::queue<std::function<void()>> taskQueue_;
    std::mutex taskQueueMutex_;
    std::condition_variable taskCondition_;
    
    // Performance tracking
    mutable std::mutex performanceMutex_;
    SystemPerformance performance_;
    std::chrono::steady_clock::time_point lastPerformanceUpdate_;
    
    // Main processing loop
    void processingLoop();
    void processMetricsForSubscriptions(const RealTimeMetrics& metrics);
    void processLevelsForSubscriptions(const AudioLevelMetrics& levels);
    void processSpectralForSubscriptions(const SpectralAnalysis& spectral);
    
    // Thread pool management
    void initializeThreadPool();
    void shutdownThreadPool();
    void workerThread();
    void enqueueTask(std::function<void()> task);
    
    // Utility functions
    std::string generateSubscriptionId();
    void updateGlobalMetrics(const RealTimeMetrics& metrics);
    void updatePerformanceMetrics();
    void cleanupInactiveSubscriptions();
    
    // Health monitoring
    void checkSystemHealth();
    bool isSubscriptionHealthy(const CallbackSubscription& subscription) const;
    
    // Debug utilities
    void logDebugInfo(const std::string& message) const;
    std::string formatMetricsForDebug(const RealTimeMetrics& metrics) const;
};

// Convenience factory functions
namespace monitoring {

// Create a simple callback subscription for basic metrics monitoring
std::unique_ptr<AudioMonitoringSystem> createBasicMonitoringSystem(
    std::shared_ptr<RealTimeAudioAnalyzer> analyzer);

// Create a high-performance monitoring system with optimized settings
std::unique_ptr<AudioMonitoringSystem> createHighPerformanceMonitoringSystem(
    std::shared_ptr<RealTimeAudioAnalyzer> analyzer);

// Create a monitoring system optimized for real-time visualization
std::unique_ptr<AudioMonitoringSystem> createVisualizationMonitoringSystem(
    std::shared_ptr<RealTimeAudioAnalyzer> analyzer);

// Predefined callback configurations
CallbackConfig createLowLatencyConfig();
CallbackConfig createHighAccuracyConfig();
CallbackConfig createVisualizationConfig();
CallbackConfig createAnalyticsConfig();

} // namespace monitoring

} // namespace audio