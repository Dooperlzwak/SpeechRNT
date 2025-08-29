#pragma once

#include "stt/advanced/advanced_stt_config.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <chrono>
#include <atomic>

namespace stt {
namespace advanced {

/**
 * Health status enumeration
 */
enum class HealthStatus {
    HEALTHY,
    WARNING,
    CRITICAL,
    UNKNOWN,
    DISABLED
};

/**
 * Feature health information
 */
struct FeatureHealthInfo {
    AdvancedFeature feature;
    HealthStatus status;
    float healthScore; // 0.0 to 1.0
    std::string statusMessage;
    std::vector<std::string> issues;
    std::vector<std::string> warnings;
    std::chrono::steady_clock::time_point lastHealthCheck;
    size_t consecutiveFailures;
    float uptime; // percentage
    
    FeatureHealthInfo() 
        : feature(AdvancedFeature::SPEAKER_DIARIZATION)
        , status(HealthStatus::UNKNOWN)
        , healthScore(0.0f)
        , lastHealthCheck(std::chrono::steady_clock::now())
        , consecutiveFailures(0)
        , uptime(0.0f) {}
};

/**
 * Advanced health status
 */
struct AdvancedHealthStatus {
    bool speakerDiarizationHealthy;
    bool audioPreprocessingHealthy;
    bool contextualTranscriptionHealthy;
    bool realTimeAnalysisHealthy;
    bool adaptiveQualityHealthy;
    bool externalServicesHealthy;
    bool batchProcessingHealthy;
    
    float overallAdvancedHealth; // 0.0 to 1.0
    std::vector<std::string> healthIssues;
    std::vector<std::string> performanceWarnings;
    std::map<AdvancedFeature, FeatureHealthInfo> featureHealth;
    
    AdvancedHealthStatus() 
        : speakerDiarizationHealthy(false)
        , audioPreprocessingHealthy(false)
        , contextualTranscriptionHealthy(false)
        , realTimeAnalysisHealthy(false)
        , adaptiveQualityHealthy(false)
        , externalServicesHealthy(false)
        , batchProcessingHealthy(false)
        , overallAdvancedHealth(0.0f) {}
};

/**
 * Processing metrics
 */
struct ProcessingMetrics {
    // Throughput metrics
    size_t totalProcessedRequests;
    size_t successfulRequests;
    size_t failedRequests;
    float requestsPerSecond;
    float averageProcessingTime;
    
    // Latency metrics
    float minLatency;
    float maxLatency;
    float p50Latency;
    float p95Latency;
    float p99Latency;
    
    // Quality metrics
    float averageConfidence;
    float averageAccuracy;
    size_t lowConfidenceResults;
    
    // Resource metrics
    float averageCpuUsage;
    float averageMemoryUsage;
    float averageGpuUsage;
    
    // Feature-specific metrics
    std::map<AdvancedFeature, std::map<std::string, float>> featureMetrics;
    
    // Error metrics
    std::map<std::string, size_t> errorCounts;
    std::vector<std::string> recentErrors;
    
    ProcessingMetrics() 
        : totalProcessedRequests(0)
        , successfulRequests(0)
        , failedRequests(0)
        , requestsPerSecond(0.0f)
        , averageProcessingTime(0.0f)
        , minLatency(0.0f)
        , maxLatency(0.0f)
        , p50Latency(0.0f)
        , p95Latency(0.0f)
        , p99Latency(0.0f)
        , averageConfidence(0.0f)
        , averageAccuracy(0.0f)
        , lowConfidenceResults(0)
        , averageCpuUsage(0.0f)
        , averageMemoryUsage(0.0f)
        , averageGpuUsage(0.0f) {}
};

/**
 * Health check configuration
 */
struct HealthCheckConfig {
    bool enableHealthChecks;
    int healthCheckIntervalMs;
    int healthCheckTimeoutMs;
    float healthThreshold; // 0.0 to 1.0
    float warningThreshold; // 0.0 to 1.0
    size_t maxConsecutiveFailures;
    bool enableAutoRecovery;
    bool enableHealthNotifications;
    std::vector<AdvancedFeature> monitoredFeatures;
    
    HealthCheckConfig() 
        : enableHealthChecks(true)
        , healthCheckIntervalMs(30000)
        , healthCheckTimeoutMs(5000)
        , healthThreshold(0.8f)
        , warningThreshold(0.6f)
        , maxConsecutiveFailures(3)
        , enableAutoRecovery(true)
        , enableHealthNotifications(true) {
        
        monitoredFeatures = {
            AdvancedFeature::SPEAKER_DIARIZATION,
            AdvancedFeature::AUDIO_PREPROCESSING,
            AdvancedFeature::CONTEXTUAL_TRANSCRIPTION,
            AdvancedFeature::REALTIME_ANALYSIS,
            AdvancedFeature::ADAPTIVE_QUALITY,
            AdvancedFeature::EXTERNAL_SERVICES,
            AdvancedFeature::BATCH_PROCESSING
        };
    }
};

/**
 * Health notification
 */
struct HealthNotification {
    AdvancedFeature feature;
    HealthStatus oldStatus;
    HealthStatus newStatus;
    std::string message;
    std::chrono::steady_clock::time_point timestamp;
    std::map<std::string, std::string> metadata;
    
    HealthNotification() 
        : feature(AdvancedFeature::SPEAKER_DIARIZATION)
        , oldStatus(HealthStatus::UNKNOWN)
        , newStatus(HealthStatus::UNKNOWN)
        , timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * Feature health checker interface
 */
class FeatureHealthChecker {
public:
    virtual ~FeatureHealthChecker() = default;
    
    /**
     * Get feature type
     * @return Advanced feature type
     */
    virtual AdvancedFeature getFeatureType() const = 0;
    
    /**
     * Check feature health
     * @return Feature health information
     */
    virtual FeatureHealthInfo checkHealth() = 0;
    
    /**
     * Perform feature self-test
     * @return true if self-test passed
     */
    virtual bool performSelfTest() = 0;
    
    /**
     * Get feature metrics
     * @return Map of metric name to value
     */
    virtual std::map<std::string, float> getFeatureMetrics() const = 0;
    
    /**
     * Reset feature health state
     */
    virtual void resetHealthState() = 0;
    
    /**
     * Check if checker is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Advanced feature health monitor
 */
class AdvancedFeatureHealthMonitor {
public:
    using HealthChangeCallback = std::function<void(const HealthNotification&)>;
    
    AdvancedFeatureHealthMonitor();
    ~AdvancedFeatureHealthMonitor();
    
    /**
     * Initialize health monitor
     * @param config Health check configuration
     * @return true if initialization successful
     */
    bool initialize(const HealthCheckConfig& config);
    
    /**
     * Register feature health checker
     * @param checker Feature health checker
     * @return true if registered successfully
     */
    bool registerFeatureChecker(std::unique_ptr<FeatureHealthChecker> checker);
    
    /**
     * Start health monitoring
     * @return true if started successfully
     */
    bool startMonitoring();
    
    /**
     * Stop health monitoring
     */
    void stopMonitoring();
    
    /**
     * Check health of all features
     * @return Advanced health status
     */
    AdvancedHealthStatus checkAdvancedHealth();
    
    /**
     * Check health of specific feature
     * @param feature Feature to check
     * @return Feature health information
     */
    FeatureHealthInfo checkFeatureHealth(AdvancedFeature feature);
    
    /**
     * Enable continuous monitoring
     * @param enabled true to enable continuous monitoring
     */
    void enableContinuousMonitoring(bool enabled);
    
    /**
     * Set health thresholds
     * @param healthThreshold Health threshold (0.0 to 1.0)
     * @param warningThreshold Warning threshold (0.0 to 1.0)
     */
    void setHealthThresholds(float healthThreshold, float warningThreshold);
    
    /**
     * Register health change callback
     * @param callback Callback function
     */
    void registerHealthChangeCallback(HealthChangeCallback callback);
    
    /**
     * Get health history
     * @param feature Feature to get history for
     * @param samples Number of historical samples
     * @return Vector of historical health information
     */
    std::vector<FeatureHealthInfo> getHealthHistory(AdvancedFeature feature, size_t samples) const;
    
    /**
     * Get overall health trend
     * @param samples Number of samples for trend analysis
     * @return Health trend (-1.0 to 1.0, negative is declining)
     */
    float getHealthTrend(size_t samples = 10) const;
    
    /**
     * Force health check for all features
     */
    void forceHealthCheck();
    
    /**
     * Force health check for specific feature
     * @param feature Feature to check
     */
    void forceFeatureHealthCheck(AdvancedFeature feature);
    
    /**
     * Enable or disable auto-recovery
     * @param enabled true to enable auto-recovery
     */
    void setAutoRecoveryEnabled(bool enabled);
    
    /**
     * Trigger recovery for feature
     * @param feature Feature to recover
     * @return true if recovery initiated successfully
     */
    bool triggerFeatureRecovery(AdvancedFeature feature);
    
    /**
     * Get monitoring statistics
     * @return Statistics as JSON string
     */
    std::string getMonitoringStats() const;
    
    /**
     * Update configuration
     * @param config New health check configuration
     * @return true if update successful
     */
    bool updateConfiguration(const HealthCheckConfig& config);
    
    /**
     * Get current configuration
     * @return Current health check configuration
     */
    HealthCheckConfig getCurrentConfiguration() const;
    
    /**
     * Check if monitor is initialized
     * @return true if initialized
     */
    bool isInitialized() const { return initialized_; }
    
    /**
     * Check if monitoring is active
     * @return true if monitoring is active
     */
    bool isMonitoring() const { return monitoring_; }
    
    /**
     * Get last error message
     * @return Last error message
     */
    std::string getLastError() const;

private:
    // Configuration and state
    HealthCheckConfig config_;
    std::atomic<bool> initialized_;
    std::atomic<bool> monitoring_;
    std::string lastError_;
    
    // Feature health checkers
    std::map<AdvancedFeature, std::unique_ptr<FeatureHealthChecker>> featureCheckers_;
    
    // Health state tracking
    std::map<AdvancedFeature, FeatureHealthInfo> currentHealth_;
    std::map<AdvancedFeature, std::vector<FeatureHealthInfo>> healthHistory_;
    
    // Callbacks
    std::vector<HealthChangeCallback> healthChangeCallbacks_;
    
    // Threading
    std::unique_ptr<std::thread> monitoringThread_;
    std::atomic<bool> shouldStop_;
    std::mutex healthMutex_;
    
    // Helper methods
    void monitoringLoop();
    void performHealthChecks();
    void updateFeatureHealth(AdvancedFeature feature, const FeatureHealthInfo& healthInfo);
    void notifyHealthChange(const HealthNotification& notification);
    bool attemptFeatureRecovery(AdvancedFeature feature);
    float calculateOverallHealth() const;
    std::string featureToString(AdvancedFeature feature) const;
    std::string healthStatusToString(HealthStatus status) const;
    void cleanupHealthHistory();
};

/**
 * Processing metrics collector
 */
class ProcessingMetricsCollector {
public:
    ProcessingMetricsCollector();
    ~ProcessingMetricsCollector();
    
    /**
     * Initialize metrics collector
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * Record processing request
     * @param success true if request was successful
     * @param processingTime Processing time in milliseconds
     * @param confidence Result confidence (0.0 to 1.0)
     * @param feature Feature that processed the request
     */
    void recordProcessingRequest(bool success, float processingTime, 
                                float confidence, AdvancedFeature feature);
    
    /**
     * Record error
     * @param errorType Error type/category
     * @param errorMessage Error message
     * @param feature Feature where error occurred
     */
    void recordError(const std::string& errorType, const std::string& errorMessage,
                    AdvancedFeature feature);
    
    /**
     * Record resource usage
     * @param cpuUsage CPU usage (0.0 to 1.0)
     * @param memoryUsage Memory usage (0.0 to 1.0)
     * @param gpuUsage GPU usage (0.0 to 1.0)
     */
    void recordResourceUsage(float cpuUsage, float memoryUsage, float gpuUsage);
    
    /**
     * Get current processing metrics
     * @return Current metrics
     */
    ProcessingMetrics getCurrentMetrics() const;
    
    /**
     * Get metrics for specific feature
     * @param feature Feature to get metrics for
     * @return Feature-specific metrics
     */
    std::map<std::string, float> getFeatureMetrics(AdvancedFeature feature) const;
    
    /**
     * Reset all metrics
     */
    void resetMetrics();
    
    /**
     * Export metrics to JSON
     * @return Metrics as JSON string
     */
    std::string exportMetricsToJson() const;
    
    /**
     * Set metrics collection interval
     * @param intervalMs Collection interval in milliseconds
     */
    void setCollectionInterval(int intervalMs);
    
    /**
     * Enable or disable metrics collection
     * @param enabled true to enable collection
     */
    void setMetricsCollectionEnabled(bool enabled);
    
    /**
     * Check if collector is initialized
     * @return true if initialized
     */
    bool isInitialized() const { return initialized_; }

private:
    std::atomic<bool> initialized_;
    std::atomic<bool> collectionEnabled_;
    mutable std::mutex metricsMutex_;
    
    // Metrics data
    ProcessingMetrics metrics_;
    std::vector<float> latencyHistory_;
    std::vector<float> confidenceHistory_;
    
    // Helper methods
    void updateLatencyPercentiles();
    void updateAverages();
    void cleanupHistory();
};

} // namespace advanced
} // namespace stt