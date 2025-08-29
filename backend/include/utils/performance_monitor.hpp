#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

namespace speechrnt {
namespace utils {

/**
 * Performance metric data point
 */
struct MetricDataPoint {
    std::chrono::steady_clock::time_point timestamp;
    double value;
    std::string unit;
    std::map<std::string, std::string> tags;
    
    MetricDataPoint() : value(0.0) {}
    MetricDataPoint(double val, const std::string& u = "") 
        : timestamp(std::chrono::steady_clock::now()), value(val), unit(u) {}
};

/**
 * Performance metric statistics
 */
struct MetricStats {
    double min;
    double max;
    double mean;
    double median;
    double p95;
    double p99;
    size_t count;
    std::string unit;
    
    MetricStats() : min(0), max(0), mean(0), median(0), p95(0), p99(0), count(0) {}
};

/**
 * Latency measurement helper
 */
class LatencyTimer {
public:
    LatencyTimer(const std::string& metricName);
    ~LatencyTimer();
    
    void stop();
    double getElapsedMs() const;

private:
    std::string metricName_;
    std::chrono::steady_clock::time_point startTime_;
    bool stopped_;
};

/**
 * Performance monitoring and metrics collection system
 * Tracks system performance, latency, throughput, and resource usage
 */
class PerformanceMonitor {
public:
    static PerformanceMonitor& getInstance();
    
    /**
     * Initialize performance monitoring
     * @param enableSystemMetrics Enable system-wide metrics collection
     * @param collectionIntervalMs Metrics collection interval in milliseconds
     * @return true if initialization successful
     */
    bool initialize(bool enableSystemMetrics = true, int collectionIntervalMs = 1000);
    
    /**
     * Record a metric value
     * @param name Metric name
     * @param value Metric value
     * @param unit Optional unit string
     * @param tags Optional tags for categorization
     */
    void recordMetric(const std::string& name, double value, 
                     const std::string& unit = "",
                     const std::map<std::string, std::string>& tags = {});
    
    /**
     * Record latency measurement
     * @param name Metric name
     * @param latencyMs Latency in milliseconds
     * @param tags Optional tags
     */
    void recordLatency(const std::string& name, double latencyMs,
                      const std::map<std::string, std::string>& tags = {});
    
    /**
     * Record throughput measurement
     * @param name Metric name
     * @param itemsPerSecond Items processed per second
     * @param tags Optional tags
     */
    void recordThroughput(const std::string& name, double itemsPerSecond,
                         const std::map<std::string, std::string>& tags = {});
    
    /**
     * Record counter increment
     * @param name Counter name
     * @param increment Increment value (default 1)
     * @param tags Optional tags
     */
    void recordCounter(const std::string& name, int increment = 1,
                      const std::map<std::string, std::string>& tags = {});
    
    /**
     * Start latency measurement
     * @param name Metric name
     * @return unique timer for automatic measurement
     */
    std::unique_ptr<LatencyTimer> startLatencyTimer(const std::string& name);
    
    /**
     * Get statistics for a metric
     * @param name Metric name
     * @param windowMinutes Time window in minutes (0 for all time)
     * @return metric statistics
     */
    MetricStats getMetricStats(const std::string& name, int windowMinutes = 5) const;
    
    /**
     * Get recent metric values
     * @param name Metric name
     * @param maxPoints Maximum number of points to return
     * @return vector of recent data points
     */
    std::vector<MetricDataPoint> getRecentMetrics(const std::string& name, size_t maxPoints = 100) const;
    
    /**
     * Get all available metric names
     * @return vector of metric names
     */
    std::vector<std::string> getAvailableMetrics() const;
    
    /**
     * Get system performance summary
     * @return map of key system metrics
     */
    std::map<std::string, double> getSystemSummary() const;
    
    /**
     * Get AI pipeline performance metrics
     * @return map of pipeline-specific metrics
     */
    std::map<std::string, MetricStats> getPipelineMetrics() const;
    
    /**
     * Get STT-specific performance metrics
     * @param windowMinutes Time window in minutes for metrics calculation
     * @return map of STT-specific metrics with statistics
     */
    std::map<std::string, MetricStats> getSTTMetrics(int windowMinutes = 10) const;
    
    /**
     * Record STT pipeline stage latency
     * @param stage Pipeline stage name (vad, preprocessing, inference, postprocessing)
     * @param latencyMs Latency in milliseconds
     * @param utteranceId Optional utterance ID for tracking
     */
    void recordSTTStageLatency(const std::string& stage, double latencyMs, uint32_t utteranceId = 0);
    
    /**
     * Record STT confidence score
     * @param confidence Confidence score (0.0-1.0)
     * @param isPartial Whether this is a partial result
     * @param utteranceId Optional utterance ID for tracking
     */
    void recordSTTConfidence(float confidence, bool isPartial = false, uint32_t utteranceId = 0);
    
    /**
     * Record STT accuracy score
     * @param accuracy Accuracy score (0.0-1.0)
     * @param utteranceId Optional utterance ID for tracking
     */
    void recordSTTAccuracy(float accuracy, uint32_t utteranceId = 0);
    
    /**
     * Record STT throughput measurement
     * @param transcriptionsPerSecond Number of transcriptions completed per second
     */
    void recordSTTThroughput(double transcriptionsPerSecond);
    
    /**
     * Record concurrent transcription count
     * @param count Number of concurrent transcriptions
     */
    void recordConcurrentTranscriptions(int count);
    
    /**
     * Record VAD performance metrics
     * @param responseTimeMs VAD response time in milliseconds
     * @param accuracy VAD accuracy score (0.0-1.0)
     * @param stateChange Whether a state change occurred
     */
    void recordVADMetrics(double responseTimeMs, float accuracy = -1.0f, bool stateChange = false);
    
    /**
     * Record streaming transcription update
     * @param updateLatencyMs Time taken to generate update
     * @param textLength Length of transcribed text
     * @param isIncremental Whether this is an incremental update
     */
    void recordStreamingUpdate(double updateLatencyMs, size_t textLength, bool isIncremental = true);
    
    /**
     * Record language detection metrics
     * @param detectionLatencyMs Time taken for language detection
     * @param confidence Language detection confidence (0.0-1.0)
     * @param detectedLanguage Detected language code
     */
    void recordLanguageDetection(double detectionLatencyMs, float confidence, const std::string& detectedLanguage);
    
    /**
     * Record audio buffer usage
     * @param bufferSizeMB Current buffer size in megabytes
     * @param utilizationPercent Buffer utilization percentage
     */
    void recordBufferUsage(double bufferSizeMB, float utilizationPercent);
    
    /**
     * Get STT performance summary for monitoring dashboards
     * @return map of key STT performance indicators
     */
    std::map<std::string, double> getSTTPerformanceSummary() const;
    
    /**
     * Export metrics to JSON format
     * @param windowMinutes Time window in minutes
     * @return JSON string with metrics data
     */
    std::string exportMetricsJSON(int windowMinutes = 60) const;
    
    /**
     * Clear all metrics data
     */
    void clearMetrics();
    
    /**
     * Enable/disable metrics collection
     * @param enabled true to enable collection
     */
    void setEnabled(bool enabled);
    
    /**
     * Check if metrics collection is enabled
     * @return true if enabled
     */
    bool isEnabled() const;
    
    /**
     * Set maximum number of data points to keep per metric
     * @param maxPoints Maximum data points
     */
    void setMaxDataPoints(size_t maxPoints);
    
    /**
     * Start background system metrics collection
     */
    void startSystemMetricsCollection();
    
    /**
     * Stop background system metrics collection
     */
    void stopSystemMetricsCollection();
    
    /**
     * Cleanup and shutdown performance monitor
     */
    void cleanup();

private:
    PerformanceMonitor() = default;
    ~PerformanceMonitor();
    
    // Prevent copying
    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;
    
    // Private methods
    void collectSystemMetrics();
    void collectGPUMetrics();
    void collectMemoryMetrics();
    void collectCPUMetrics();
    void pruneOldMetrics();
    MetricStats calculateStats(const std::vector<MetricDataPoint>& points) const;
    std::vector<MetricDataPoint> filterByTimeWindow(const std::vector<MetricDataPoint>& points, 
                                                   int windowMinutes) const;
    
    // Member variables
    std::atomic<bool> initialized_{false};
    std::atomic<bool> enabled_{true};
    std::atomic<bool> systemMetricsEnabled_{false};
    std::atomic<size_t> maxDataPoints_{10000};
    
    // Metrics storage
    mutable std::mutex metricsMutex_;
    std::map<std::string, std::vector<MetricDataPoint>> metrics_;
    
    // System metrics collection
    std::unique_ptr<std::thread> systemMetricsThread_;
    std::atomic<bool> systemMetricsRunning_{false};
    int collectionIntervalMs_;
    
    // Performance counters
    std::atomic<uint64_t> totalMetricsRecorded_{0};
    std::atomic<uint64_t> totalLatencyMeasurements_{0};
    std::atomic<uint64_t> totalThroughputMeasurements_{0};
    
    // Common metric names (for consistency)
    static const std::string METRIC_STT_LATENCY;
    static const std::string METRIC_MT_LATENCY;
    static const std::string METRIC_TTS_LATENCY;
    static const std::string METRIC_END_TO_END_LATENCY;
    static const std::string METRIC_AUDIO_PROCESSING_LATENCY;
    static const std::string METRIC_VAD_LATENCY;
    static const std::string METRIC_PIPELINE_THROUGHPUT;
    static const std::string METRIC_MEMORY_USAGE;
    static const std::string METRIC_GPU_MEMORY_USAGE;
    static const std::string METRIC_GPU_UTILIZATION;
    static const std::string METRIC_CPU_USAGE;
    static const std::string METRIC_WEBSOCKET_LATENCY;
    static const std::string METRIC_ACTIVE_SESSIONS;
    static const std::string METRIC_ERRORS_COUNT;
    
    // Enhanced STT-specific metrics
    static const std::string METRIC_STT_VAD_LATENCY;
    static const std::string METRIC_STT_PREPROCESSING_LATENCY;
    static const std::string METRIC_STT_INFERENCE_LATENCY;
    static const std::string METRIC_STT_POSTPROCESSING_LATENCY;
    static const std::string METRIC_STT_STREAMING_LATENCY;
    static const std::string METRIC_STT_CONFIDENCE_SCORE;
    static const std::string METRIC_STT_ACCURACY_SCORE;
    static const std::string METRIC_STT_THROUGHPUT;
    static const std::string METRIC_STT_CONCURRENT_TRANSCRIPTIONS;
    static const std::string METRIC_STT_QUEUE_SIZE;
    static const std::string METRIC_STT_MODEL_LOAD_TIME;
    static const std::string METRIC_STT_LANGUAGE_DETECTION_LATENCY;
    static const std::string METRIC_STT_LANGUAGE_CONFIDENCE;
    static const std::string METRIC_STT_BUFFER_USAGE;
    static const std::string METRIC_STT_STREAMING_UPDATES;
    static const std::string METRIC_VAD_ACCURACY;
    static const std::string METRIC_VAD_RESPONSE_TIME;
    static const std::string METRIC_VAD_STATE_CHANGES;
    static const std::string METRIC_VAD_SPEECH_DETECTION_RATE;
};

// Convenience macros for common measurements
#define MEASURE_LATENCY(name) auto timer = PerformanceMonitor::getInstance().startLatencyTimer(name)
#define RECORD_METRIC(name, value) PerformanceMonitor::getInstance().recordMetric(name, value)
#define RECORD_LATENCY(name, ms) PerformanceMonitor::getInstance().recordLatency(name, ms)
#define RECORD_COUNTER(name) PerformanceMonitor::getInstance().recordCounter(name)

} // namespace utils
} // namespace speechrnt