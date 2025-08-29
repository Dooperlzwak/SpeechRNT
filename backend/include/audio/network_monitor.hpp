#pragma once

#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <memory>
#include <functional>
#include <thread>

namespace speechrnt {
namespace audio {

/**
 * Network condition metrics
 */
struct NetworkMetrics {
    float latencyMs;           // Round-trip time
    float jitterMs;            // Latency variation
    float packetLossRate;      // Packet loss percentage (0-100)
    float bandwidthKbps;       // Available bandwidth in Kbps
    float throughputKbps;      // Current throughput in Kbps
    std::chrono::steady_clock::time_point timestamp;
    
    NetworkMetrics() 
        : latencyMs(0.0f), jitterMs(0.0f), packetLossRate(0.0f)
        , bandwidthKbps(0.0f), throughputKbps(0.0f)
        , timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * Network quality classification
 */
enum class NetworkQuality {
    EXCELLENT,  // < 50ms latency, < 5ms jitter, < 0.1% loss
    GOOD,       // < 100ms latency, < 10ms jitter, < 0.5% loss
    FAIR,       // < 200ms latency, < 20ms jitter, < 2% loss
    POOR,       // < 500ms latency, < 50ms jitter, < 5% loss
    VERY_POOR   // > 500ms latency, > 50ms jitter, > 5% loss
};

/**
 * Network condition monitor for adaptive streaming
 */
class NetworkMonitor {
public:
    NetworkMonitor();
    ~NetworkMonitor();
    
    /**
     * Initialize network monitoring
     * @param monitoringIntervalMs Interval between measurements
     * @param historySize Number of measurements to keep
     * @return true if initialization successful
     */
    bool initialize(int monitoringIntervalMs = 1000, size_t historySize = 60);
    
    /**
     * Start continuous network monitoring
     * @return true if monitoring started successfully
     */
    bool startMonitoring();
    
    /**
     * Stop network monitoring
     */
    void stopMonitoring();
    
    /**
     * Get current network metrics
     * @return current network metrics
     */
    NetworkMetrics getCurrentMetrics() const;
    
    /**
     * Get network quality classification
     * @return current network quality
     */
    NetworkQuality getNetworkQuality() const;
    
    /**
     * Get average metrics over specified duration
     * @param durationMs Duration to average over
     * @return averaged network metrics
     */
    NetworkMetrics getAverageMetrics(int durationMs = 5000) const;
    
    /**
     * Register callback for network condition changes
     * @param callback Function to call when conditions change
     */
    void registerConditionCallback(std::function<void(const NetworkMetrics&, NetworkQuality)> callback);
    
    /**
     * Manually update network metrics (for testing or external monitoring)
     * @param metrics Network metrics to record
     */
    void updateMetrics(const NetworkMetrics& metrics);
    
    /**
     * Check if network conditions are stable
     * @param stabilityThreshold Maximum acceptable variation
     * @return true if conditions are stable
     */
    bool isNetworkStable(float stabilityThreshold = 0.2f) const;
    
    /**
     * Get network monitoring statistics
     * @return map of monitoring statistics
     */
    std::map<std::string, double> getMonitoringStats() const;

private:
    // Configuration
    int monitoringIntervalMs_;
    size_t historySize_;
    
    // Monitoring state
    std::atomic<bool> monitoring_;
    std::unique_ptr<std::thread> monitoringThread_;
    
    // Metrics storage
    mutable std::mutex metricsMutex_;
    std::vector<NetworkMetrics> metricsHistory_;
    NetworkMetrics currentMetrics_;
    NetworkQuality currentQuality_;
    
    // Callbacks
    std::vector<std::function<void(const NetworkMetrics&, NetworkQuality)>> conditionCallbacks_;
    
    // Statistics
    std::atomic<uint64_t> totalMeasurements_;
    std::atomic<uint64_t> qualityChanges_;
    
    // Private methods
    void monitoringLoop();
    NetworkMetrics measureNetworkConditions();
    NetworkQuality classifyNetworkQuality(const NetworkMetrics& metrics) const;
    void notifyConditionChange(const NetworkMetrics& metrics, NetworkQuality quality);
    float calculateJitter(const std::vector<float>& latencies) const;
    void pruneOldMetrics();
};

/**
 * Adaptive streaming parameters based on network conditions
 */
struct AdaptiveStreamingParams {
    size_t bufferSizeMs;       // Buffer size in milliseconds
    size_t chunkSizeMs;        // Chunk size in milliseconds
    int maxRetries;            // Maximum retry attempts
    float qualityFactor;       // Quality scaling factor (0.0-1.0)
    bool enableCompression;    // Enable data compression
    int timeoutMs;             // Network timeout
    
    AdaptiveStreamingParams()
        : bufferSizeMs(100), chunkSizeMs(50), maxRetries(3)
        , qualityFactor(1.0f), enableCompression(true), timeoutMs(5000) {}
};

/**
 * Network-aware streaming adapter
 */
class NetworkAwareStreamingAdapter {
public:
    NetworkAwareStreamingAdapter();
    ~NetworkAwareStreamingAdapter();
    
    /**
     * Initialize the adapter with network monitor
     * @param networkMonitor Shared network monitor instance
     * @return true if initialization successful
     */
    bool initialize(std::shared_ptr<NetworkMonitor> networkMonitor);
    
    /**
     * Get adaptive streaming parameters for current network conditions
     * @return optimized streaming parameters
     */
    AdaptiveStreamingParams getAdaptiveParams() const;
    
    /**
     * Update streaming parameters based on network feedback
     * @param successRate Recent transmission success rate
     * @param averageLatency Recent average latency
     */
    void updateFromFeedback(float successRate, float averageLatency);
    
    /**
     * Check if quality degradation is recommended
     * @return true if quality should be reduced
     */
    bool shouldDegradeQuality() const;
    
    /**
     * Check if quality can be improved
     * @return true if quality can be increased
     */
    bool canImproveQuality() const;
    
    /**
     * Get recommended buffer size for current conditions
     * @param baseBufferMs Base buffer size in milliseconds
     * @return recommended buffer size in milliseconds
     */
    size_t getRecommendedBufferSize(size_t baseBufferMs = 100) const;
    
    /**
     * Get recommended chunk size for current conditions
     * @param baseChunkMs Base chunk size in milliseconds
     * @return recommended chunk size in milliseconds
     */
    size_t getRecommendedChunkSize(size_t baseChunkMs = 50) const;
    
    /**
     * Enable/disable adaptive mode
     * @param enabled true to enable adaptation
     */
    void setAdaptiveMode(bool enabled);
    
    /**
     * Get adaptation statistics
     * @return map of adaptation statistics
     */
    std::map<std::string, double> getAdaptationStats() const;

private:
    std::shared_ptr<NetworkMonitor> networkMonitor_;
    bool adaptiveMode_;
    
    // Current parameters
    mutable std::mutex paramsMutex_;
    AdaptiveStreamingParams currentParams_;
    
    // Adaptation history
    std::vector<std::pair<std::chrono::steady_clock::time_point, AdaptiveStreamingParams>> adaptationHistory_;
    
    // Statistics
    std::atomic<uint64_t> totalAdaptations_;
    std::atomic<uint64_t> qualityDegradations_;
    std::atomic<uint64_t> qualityImprovements_;
    
    // Private methods
    void onNetworkConditionChange(const NetworkMetrics& metrics, NetworkQuality quality);
    AdaptiveStreamingParams calculateOptimalParams(const NetworkMetrics& metrics, NetworkQuality quality) const;
    void recordAdaptation(const AdaptiveStreamingParams& params);
    bool isAdaptationNeeded(const AdaptiveStreamingParams& newParams) const;
};

} // namespace audio
} // namespace speechrnt