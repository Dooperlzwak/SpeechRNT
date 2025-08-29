#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <atomic>
#include <functional>
#include <queue>
#include <thread>
#include <condition_variable>

namespace speechrnt {
namespace mt {

/**
 * Performance metrics for translation operations
 */
struct TranslationMetrics {
    std::chrono::milliseconds latency;
    std::chrono::steady_clock::time_point timestamp;
    std::string sourceLang;
    std::string targetLang;
    size_t inputLength;
    size_t outputLength;
    float confidence;
    bool usedGPU;
    bool usedCache;
    std::string sessionId;
    
    TranslationMetrics() 
        : latency(0), inputLength(0), outputLength(0), 
          confidence(0.0f), usedGPU(false), usedCache(false) {}
};

/**
 * System resource metrics
 */
struct ResourceMetrics {
    float cpuUsagePercent;
    size_t memoryUsageMB;
    size_t gpuMemoryUsageMB;
    float gpuUtilizationPercent;
    size_t diskUsageMB;
    std::chrono::steady_clock::time_point timestamp;
    
    ResourceMetrics() 
        : cpuUsagePercent(0.0f), memoryUsageMB(0), gpuMemoryUsageMB(0),
          gpuUtilizationPercent(0.0f), diskUsageMB(0) {}
};

/**
 * Performance statistics aggregated over time
 */
struct PerformanceStatistics {
    // Latency statistics
    std::chrono::milliseconds averageLatency;
    std::chrono::milliseconds medianLatency;
    std::chrono::milliseconds p95Latency;
    std::chrono::milliseconds p99Latency;
    std::chrono::milliseconds minLatency;
    std::chrono::milliseconds maxLatency;
    
    // Throughput statistics
    float translationsPerSecond;
    float charactersPerSecond;
    size_t totalTranslations;
    size_t totalCharactersProcessed;
    
    // Resource statistics
    float averageCpuUsage;
    float peakCpuUsage;
    size_t averageMemoryUsage;
    size_t peakMemoryUsage;
    size_t averageGpuMemoryUsage;
    size_t peakGpuMemoryUsage;
    float averageGpuUtilization;
    float peakGpuUtilization;
    
    // Quality statistics
    float averageConfidence;
    float minConfidence;
    float maxConfidence;
    size_t lowQualityTranslations;
    
    // Cache statistics
    float cacheHitRate;
    size_t cacheHits;
    size_t cacheMisses;
    
    // Error statistics
    size_t totalErrors;
    size_t timeoutErrors;
    size_t memoryErrors;
    size_t gpuErrors;
    
    // Time period
    std::chrono::steady_clock::time_point periodStart;
    std::chrono::steady_clock::time_point periodEnd;
    std::chrono::seconds periodDuration;
    
    PerformanceStatistics() 
        : averageLatency(0), medianLatency(0), p95Latency(0), p99Latency(0),
          minLatency(std::chrono::milliseconds::max()), maxLatency(0),
          translationsPerSecond(0.0f), charactersPerSecond(0.0f),
          totalTranslations(0), totalCharactersProcessed(0),
          averageCpuUsage(0.0f), peakCpuUsage(0.0f),
          averageMemoryUsage(0), peakMemoryUsage(0),
          averageGpuMemoryUsage(0), peakGpuMemoryUsage(0),
          averageGpuUtilization(0.0f), peakGpuUtilization(0.0f),
          averageConfidence(0.0f), minConfidence(1.0f), maxConfidence(0.0f),
          lowQualityTranslations(0), cacheHitRate(0.0f),
          cacheHits(0), cacheMisses(0), totalErrors(0),
          timeoutErrors(0), memoryErrors(0), gpuErrors(0),
          periodDuration(0) {}
};

/**
 * Performance threshold configuration
 */
struct PerformanceThresholds {
    std::chrono::milliseconds maxLatency;
    float minThroughput; // translations per second
    float maxCpuUsage; // percentage
    size_t maxMemoryUsage; // MB
    size_t maxGpuMemoryUsage; // MB
    float minCacheHitRate; // percentage
    float minAverageConfidence; // 0.0-1.0
    size_t maxQueueSize; // maximum pending translations
    
    PerformanceThresholds()
        : maxLatency(2000), minThroughput(1.0f), maxCpuUsage(80.0f),
          maxMemoryUsage(8192), maxGpuMemoryUsage(6144), minCacheHitRate(50.0f),
          minAverageConfidence(0.7f), maxQueueSize(100) {}
};

/**
 * Performance warning information
 */
struct PerformanceWarning {
    enum class Type {
        HIGH_LATENCY,
        LOW_THROUGHPUT,
        HIGH_CPU_USAGE,
        HIGH_MEMORY_USAGE,
        HIGH_GPU_MEMORY_USAGE,
        LOW_CACHE_HIT_RATE,
        LOW_CONFIDENCE,
        QUEUE_OVERFLOW,
        RESOURCE_EXHAUSTION
    };
    
    Type type;
    std::string message;
    std::string recommendation;
    float severity; // 0.0-1.0
    std::chrono::steady_clock::time_point timestamp;
    std::unordered_map<std::string, std::string> metadata;
    
    PerformanceWarning(Type t, const std::string& msg, float sev = 0.5f)
        : type(t), message(msg), severity(sev) {
        timestamp = std::chrono::steady_clock::now();
    }
};

/**
 * Translation queue item with priority
 */
struct QueuedTranslation {
    std::string text;
    std::string sourceLang;
    std::string targetLang;
    std::string sessionId;
    int priority; // Higher values = higher priority
    std::chrono::steady_clock::time_point queueTime;
    std::function<void(const TranslationResult&)> callback;
    
    QueuedTranslation(const std::string& t, const std::string& src, const std::string& tgt,
                     const std::string& sid, int prio = 0)
        : text(t), sourceLang(src), targetLang(tgt), sessionId(sid), priority(prio) {
        queueTime = std::chrono::steady_clock::now();
    }
    
    // For priority queue ordering (higher priority first)
    bool operator<(const QueuedTranslation& other) const {
        if (priority != other.priority) {
            return priority < other.priority; // Higher priority first
        }
        return queueTime > other.queueTime; // Earlier submissions first for same priority
    }
};

/**
 * Memory optimization strategy
 */
class MemoryOptimizer {
public:
    enum class Strategy {
        AGGRESSIVE_CLEANUP,
        MODERATE_CLEANUP,
        CONSERVATIVE_CLEANUP,
        EMERGENCY_CLEANUP
    };
    
    struct OptimizationResult {
        size_t memoryFreedMB;
        size_t modelsUnloaded;
        size_t cacheEntriesCleared;
        std::chrono::milliseconds optimizationTime;
        std::vector<std::string> actionsPerformed;
        
        OptimizationResult() 
            : memoryFreedMB(0), modelsUnloaded(0), cacheEntriesCleared(0),
              optimizationTime(0) {}
    };
    
    virtual ~MemoryOptimizer() = default;
    virtual OptimizationResult optimize(Strategy strategy, size_t targetMemoryMB) = 0;
    virtual bool canOptimize(size_t requiredMemoryMB) const = 0;
    virtual size_t estimateOptimizationPotential() const = 0;
};

/**
 * Performance monitoring and optimization manager
 */
class PerformanceMonitor {
public:
    PerformanceMonitor();
    ~PerformanceMonitor();
    
    // Initialization and configuration
    bool initialize(const std::string& configPath = "config/performance.json");
    void cleanup();
    
    // Metrics collection
    void recordTranslationMetrics(const TranslationMetrics& metrics);
    void recordResourceMetrics(const ResourceMetrics& metrics);
    void recordError(const std::string& errorType, const std::string& details);
    
    // Statistics and reporting
    PerformanceStatistics getStatistics(std::chrono::seconds period = std::chrono::seconds(300)) const;
    PerformanceStatistics getStatisticsSince(std::chrono::steady_clock::time_point since) const;
    std::vector<TranslationMetrics> getRecentMetrics(size_t count = 100) const;
    std::vector<ResourceMetrics> getRecentResourceMetrics(size_t count = 100) const;
    
    // Threshold monitoring
    void setThresholds(const PerformanceThresholds& thresholds);
    PerformanceThresholds getThresholds() const;
    std::vector<PerformanceWarning> getActiveWarnings() const;
    std::vector<PerformanceWarning> getRecentWarnings(std::chrono::seconds period = std::chrono::seconds(3600)) const;
    
    // Warning callbacks
    using WarningCallback = std::function<void(const PerformanceWarning&)>;
    void setWarningCallback(WarningCallback callback);
    void clearWarningCallback();
    
    // Queue management
    void enqueueTranslation(const QueuedTranslation& translation);
    bool dequeueTranslation(QueuedTranslation& translation);
    size_t getQueueSize() const;
    void clearQueue();
    void setPriorityBoost(const std::string& sessionId, int boost);
    
    // Memory optimization
    void setMemoryOptimizer(std::unique_ptr<MemoryOptimizer> optimizer);
    MemoryOptimizer::OptimizationResult optimizeMemory(MemoryOptimizer::Strategy strategy, size_t targetMemoryMB);
    bool isMemoryOptimizationNeeded() const;
    size_t getMemoryOptimizationPotential() const;
    
    // Performance analysis
    struct BottleneckAnalysis {
        std::string primaryBottleneck; // "cpu", "memory", "gpu", "network", "model_loading"
        std::vector<std::string> contributingFactors;
        std::vector<std::string> recommendations;
        float confidenceScore; // 0.0-1.0
    };
    BottleneckAnalysis analyzeBottlenecks() const;
    
    // Language pair performance analysis
    struct LanguagePairPerformance {
        std::string sourceLang;
        std::string targetLang;
        std::chrono::milliseconds averageLatency;
        float averageConfidence;
        size_t translationCount;
        float errorRate;
    };
    std::vector<LanguagePairPerformance> getLanguagePairPerformance() const;
    
    // Real-time monitoring
    void startRealTimeMonitoring(std::chrono::seconds interval = std::chrono::seconds(10));
    void stopRealTimeMonitoring();
    bool isRealTimeMonitoringActive() const;
    
    // Export and reporting
    std::string exportStatisticsToJson(std::chrono::seconds period = std::chrono::seconds(3600)) const;
    std::string exportMetricsToCSV(std::chrono::seconds period = std::chrono::seconds(3600)) const;
    bool savePerformanceReport(const std::string& filePath, std::chrono::seconds period = std::chrono::seconds(3600)) const;
    
    // Configuration
    void setMetricsRetentionPeriod(std::chrono::hours period);
    void setResourceMonitoringInterval(std::chrono::seconds interval);
    void enableDetailedLogging(bool enabled);
    
private:
    struct Config {
        std::chrono::hours metricsRetentionPeriod;
        std::chrono::seconds resourceMonitoringInterval;
        bool detailedLoggingEnabled;
        size_t maxMetricsInMemory;
        size_t maxWarningsInMemory;
        
        Config() 
            : metricsRetentionPeriod(24), resourceMonitoringInterval(10),
              detailedLoggingEnabled(false), maxMetricsInMemory(10000),
              maxWarningsInMemory(1000) {}
    };
    
    // Configuration and state
    Config config_;
    PerformanceThresholds thresholds_;
    bool initialized_;
    bool realTimeMonitoringActive_;
    
    // Metrics storage
    std::vector<TranslationMetrics> translationMetrics_;
    std::vector<ResourceMetrics> resourceMetrics_;
    std::vector<PerformanceWarning> warnings_;
    mutable std::mutex metricsMutex_;
    mutable std::mutex warningsMutex_;
    
    // Queue management
    std::priority_queue<QueuedTranslation> translationQueue_;
    std::unordered_map<std::string, int> sessionPriorityBoosts_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    
    // Memory optimization
    std::unique_ptr<MemoryOptimizer> memoryOptimizer_;
    mutable std::mutex optimizerMutex_;
    
    // Warning system
    WarningCallback warningCallback_;
    mutable std::mutex callbackMutex_;
    
    // Real-time monitoring
    std::unique_ptr<std::thread> monitoringThread_;
    std::atomic<bool> stopMonitoring_;
    
    // Error tracking
    std::unordered_map<std::string, size_t> errorCounts_;
    mutable std::mutex errorMutex_;
    
    // Private methods
    void checkThresholds(const TranslationMetrics& metrics);
    void checkResourceThresholds(const ResourceMetrics& metrics);
    void emitWarning(const PerformanceWarning& warning);
    void cleanupOldMetrics();
    void cleanupOldWarnings();
    void realTimeMonitoringLoop();
    ResourceMetrics collectCurrentResourceMetrics() const;
    
    // Statistics calculation helpers
    std::chrono::milliseconds calculatePercentile(const std::vector<std::chrono::milliseconds>& latencies, float percentile) const;
    float calculateThroughput(const std::vector<TranslationMetrics>& metrics, std::chrono::seconds period) const;
    BottleneckAnalysis performBottleneckAnalysis(const PerformanceStatistics& stats) const;
    
    // Data export helpers
    std::string metricsToJson(const std::vector<TranslationMetrics>& metrics) const;
    std::string statisticsToJson(const PerformanceStatistics& stats) const;
    std::string metricsToCsv(const std::vector<TranslationMetrics>& metrics) const;
};

} // namespace mt
} // namespace speechrnt