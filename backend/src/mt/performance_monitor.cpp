#include "mt/performance_monitor.hpp"
#include "mt/translation_interface.hpp"
#include "utils/logging.hpp"
#include "utils/json_utils.hpp"
#include <algorithm>
#include <numeric>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif __linux__
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <fstream>
#elif __APPLE__
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#endif

namespace speechrnt {
namespace mt {

PerformanceMonitor::PerformanceMonitor() 
    : initialized_(false)
    , realTimeMonitoringActive_(false)
    , stopMonitoring_(false) {
}

PerformanceMonitor::~PerformanceMonitor() {
    cleanup();
}

bool PerformanceMonitor::initialize(const std::string& configPath) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    if (initialized_) {
        return true;
    }
    
    try {
        // Load configuration from JSON file
        if (!configPath.empty()) {
            std::ifstream configFile(configPath);
            if (configFile.is_open()) {
                // Parse configuration JSON
                // For now, use default configuration
                utils::Logger::info("Using default performance monitor configuration");
            }
        }
        
        // Initialize default thresholds
        thresholds_ = PerformanceThresholds();
        
        // Reserve space for metrics to avoid frequent reallocations
        translationMetrics_.reserve(config_.maxMetricsInMemory);
        resourceMetrics_.reserve(config_.maxMetricsInMemory);
        warnings_.reserve(config_.maxWarningsInMemory);
        
        initialized_ = true;
        utils::Logger::info("Performance monitor initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to initialize performance monitor: " + std::string(e.what()));
        return false;
    }
}

void PerformanceMonitor::cleanup() {
    if (!initialized_) {
        return;
    }
    
    // Stop real-time monitoring
    stopRealTimeMonitoring();
    
    // Clear all data
    {
        std::lock_guard<std::mutex> lock(metricsMutex_);
        translationMetrics_.clear();
        resourceMetrics_.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(warningsMutex_);
        warnings_.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!translationQueue_.empty()) {
            translationQueue_.pop();
        }
        sessionPriorityBoosts_.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(optimizerMutex_);
        memoryOptimizer_.reset();
    }
    
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        warningCallback_ = nullptr;
    }
    
    initialized_ = false;
    utils::Logger::info("Performance monitor cleaned up");
}

void PerformanceMonitor::recordTranslationMetrics(const TranslationMetrics& metrics) {
    if (!initialized_) {
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(metricsMutex_);
        translationMetrics_.push_back(metrics);
        
        // Cleanup old metrics if we exceed the limit
        if (translationMetrics_.size() > config_.maxMetricsInMemory) {
            cleanupOldMetrics();
        }
    }
    
    // Check thresholds and emit warnings if necessary
    checkThresholds(metrics);
    
    utils::Logger::debug("Recorded translation metrics: latency=" + 
              std::to_string(metrics.latency.count()) + "ms, " +
              "confidence=" + std::to_string(metrics.confidence));
}

void PerformanceMonitor::recordResourceMetrics(const ResourceMetrics& metrics) {
    if (!initialized_) {
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(metricsMutex_);
        resourceMetrics_.push_back(metrics);
        
        // Cleanup old metrics if we exceed the limit
        if (resourceMetrics_.size() > config_.maxMetricsInMemory) {
            cleanupOldMetrics();
        }
    }
    
    // Check resource thresholds
    checkResourceThresholds(metrics);
    
    utils::Logger::debug("Recorded resource metrics: CPU=" + 
              std::to_string(metrics.cpuUsagePercent) + "%, " +
              "Memory=" + std::to_string(metrics.memoryUsageMB) + "MB");
}

void PerformanceMonitor::recordError(const std::string& errorType, const std::string& details) {
    if (!initialized_) {
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        errorCounts_[errorType]++;
    }
    
    // Create warning for error
    PerformanceWarning warning(
        PerformanceWarning::Type::RESOURCE_EXHAUSTION,
        "Error recorded: " + errorType + " - " + details,
        0.7f
    );
    warning.metadata["error_type"] = errorType;
    warning.metadata["details"] = details;
    
    emitWarning(warning);
    
    utils::Logger::warn("Recorded error: " + errorType + " - " + details);
}

PerformanceStatistics PerformanceMonitor::getStatistics(std::chrono::seconds period) const {
    auto since = std::chrono::steady_clock::now() - period;
    return getStatisticsSince(since);
}

PerformanceStatistics PerformanceMonitor::getStatisticsSince(std::chrono::steady_clock::time_point since) const {
    if (!initialized_) {
        return PerformanceStatistics();
    }
    
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    PerformanceStatistics stats;
    stats.periodStart = since;
    stats.periodEnd = std::chrono::steady_clock::now();
    stats.periodDuration = std::chrono::duration_cast<std::chrono::seconds>(stats.periodEnd - stats.periodStart);
    
    // Filter metrics within the time period
    std::vector<TranslationMetrics> periodMetrics;
    std::vector<ResourceMetrics> periodResourceMetrics;
    
    for (const auto& metric : translationMetrics_) {
        if (metric.timestamp >= since) {
            periodMetrics.push_back(metric);
        }
    }
    
    for (const auto& metric : resourceMetrics_) {
        if (metric.timestamp >= since) {
            periodResourceMetrics.push_back(metric);
        }
    }
    
    if (periodMetrics.empty()) {
        return stats;
    }
    
    // Calculate latency statistics
    std::vector<std::chrono::milliseconds> latencies;
    std::vector<float> confidences;
    size_t totalChars = 0;
    size_t cacheHits = 0;
    size_t lowQualityCount = 0;
    
    for (const auto& metric : periodMetrics) {
        latencies.push_back(metric.latency);
        confidences.push_back(metric.confidence);
        totalChars += metric.inputLength + metric.outputLength;
        
        if (metric.usedCache) {
            cacheHits++;
        }
        
        if (metric.confidence < 0.7f) {
            lowQualityCount++;
        }
    }
    
    // Sort latencies for percentile calculations
    std::sort(latencies.begin(), latencies.end());
    
    stats.totalTranslations = periodMetrics.size();
    stats.totalCharactersProcessed = totalChars;
    
    if (!latencies.empty()) {
        stats.averageLatency = std::chrono::milliseconds(
            std::accumulate(latencies.begin(), latencies.end(), std::chrono::milliseconds(0)).count() / latencies.size()
        );
        stats.medianLatency = calculatePercentile(latencies, 0.5f);
        stats.p95Latency = calculatePercentile(latencies, 0.95f);
        stats.p99Latency = calculatePercentile(latencies, 0.99f);
        stats.minLatency = latencies.front();
        stats.maxLatency = latencies.back();
    }
    
    // Calculate throughput
    if (stats.periodDuration.count() > 0) {
        stats.translationsPerSecond = static_cast<float>(stats.totalTranslations) / stats.periodDuration.count();
        stats.charactersPerSecond = static_cast<float>(totalChars) / stats.periodDuration.count();
    }
    
    // Calculate confidence statistics
    if (!confidences.empty()) {
        stats.averageConfidence = std::accumulate(confidences.begin(), confidences.end(), 0.0f) / confidences.size();
        stats.minConfidence = *std::min_element(confidences.begin(), confidences.end());
        stats.maxConfidence = *std::max_element(confidences.begin(), confidences.end());
        stats.lowQualityTranslations = lowQualityCount;
    }
    
    // Calculate cache statistics
    if (stats.totalTranslations > 0) {
        stats.cacheHits = cacheHits;
        stats.cacheMisses = stats.totalTranslations - cacheHits;
        stats.cacheHitRate = (static_cast<float>(cacheHits) / stats.totalTranslations) * 100.0f;
    }
    
    // Calculate resource statistics
    if (!periodResourceMetrics.empty()) {
        std::vector<float> cpuUsages;
        std::vector<size_t> memoryUsages;
        std::vector<size_t> gpuMemoryUsages;
        std::vector<float> gpuUtilizations;
        
        for (const auto& metric : periodResourceMetrics) {
            cpuUsages.push_back(metric.cpuUsagePercent);
            memoryUsages.push_back(metric.memoryUsageMB);
            gpuMemoryUsages.push_back(metric.gpuMemoryUsageMB);
            gpuUtilizations.push_back(metric.gpuUtilizationPercent);
        }
        
        stats.averageCpuUsage = std::accumulate(cpuUsages.begin(), cpuUsages.end(), 0.0f) / cpuUsages.size();
        stats.peakCpuUsage = *std::max_element(cpuUsages.begin(), cpuUsages.end());
        
        stats.averageMemoryUsage = std::accumulate(memoryUsages.begin(), memoryUsages.end(), 0UL) / memoryUsages.size();
        stats.peakMemoryUsage = *std::max_element(memoryUsages.begin(), memoryUsages.end());
        
        stats.averageGpuMemoryUsage = std::accumulate(gpuMemoryUsages.begin(), gpuMemoryUsages.end(), 0UL) / gpuMemoryUsages.size();
        stats.peakGpuMemoryUsage = *std::max_element(gpuMemoryUsages.begin(), gpuMemoryUsages.end());
        
        stats.averageGpuUtilization = std::accumulate(gpuUtilizations.begin(), gpuUtilizations.end(), 0.0f) / gpuUtilizations.size();
        stats.peakGpuUtilization = *std::max_element(gpuUtilizations.begin(), gpuUtilizations.end());
    }
    
    // Calculate error statistics
    {
        std::lock_guard<std::mutex> errorLock(errorMutex_);
        for (const auto& [errorType, count] : errorCounts_) {
            stats.totalErrors += count;
            
            if (errorType == "timeout") {
                stats.timeoutErrors += count;
            } else if (errorType == "memory") {
                stats.memoryErrors += count;
            } else if (errorType == "gpu") {
                stats.gpuErrors += count;
            }
        }
    }
    
    return stats;
}

std::vector<TranslationMetrics> PerformanceMonitor::getRecentMetrics(size_t count) const {
    if (!initialized_) {
        return {};
    }
    
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    if (translationMetrics_.size() <= count) {
        return translationMetrics_;
    }
    
    return std::vector<TranslationMetrics>(
        translationMetrics_.end() - count,
        translationMetrics_.end()
    );
}

std::vector<ResourceMetrics> PerformanceMonitor::getRecentResourceMetrics(size_t count) const {
    if (!initialized_) {
        return {};
    }
    
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    if (resourceMetrics_.size() <= count) {
        return resourceMetrics_;
    }
    
    return std::vector<ResourceMetrics>(
        resourceMetrics_.end() - count,
        resourceMetrics_.end()
    );
}

void PerformanceMonitor::setThresholds(const PerformanceThresholds& thresholds) {
    thresholds_ = thresholds;
    utils::Logger::info("Performance thresholds updated");
}

PerformanceThresholds PerformanceMonitor::getThresholds() const {
    return thresholds_;
}

std::vector<PerformanceWarning> PerformanceMonitor::getActiveWarnings() const {
    if (!initialized_) {
        return {};
    }
    
    std::lock_guard<std::mutex> lock(warningsMutex_);
    
    // Return warnings from the last 5 minutes
    auto cutoff = std::chrono::steady_clock::now() - std::chrono::minutes(5);
    std::vector<PerformanceWarning> activeWarnings;
    
    for (const auto& warning : warnings_) {
        if (warning.timestamp >= cutoff) {
            activeWarnings.push_back(warning);
        }
    }
    
    return activeWarnings;
}

std::vector<PerformanceWarning> PerformanceMonitor::getRecentWarnings(std::chrono::seconds period) const {
    if (!initialized_) {
        return {};
    }
    
    std::lock_guard<std::mutex> lock(warningsMutex_);
    
    auto cutoff = std::chrono::steady_clock::now() - period;
    std::vector<PerformanceWarning> recentWarnings;
    
    for (const auto& warning : warnings_) {
        if (warning.timestamp >= cutoff) {
            recentWarnings.push_back(warning);
        }
    }
    
    return recentWarnings;
}

void PerformanceMonitor::setWarningCallback(WarningCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    warningCallback_ = callback;
}

void PerformanceMonitor::clearWarningCallback() {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    warningCallback_ = nullptr;
}

void PerformanceMonitor::enqueueTranslation(const QueuedTranslation& translation) {
    if (!initialized_) {
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        
        // Check queue size threshold
        if (translationQueue_.size() >= thresholds_.maxQueueSize) {
            PerformanceWarning warning(
                PerformanceWarning::Type::QUEUE_OVERFLOW,
                "Translation queue overflow: " + std::to_string(translationQueue_.size()) + " items",
                0.8f
            );
            warning.recommendation = "Consider increasing processing capacity or implementing backpressure";
            emitWarning(warning);
            return;
        }
        
        translationQueue_.push(translation);
    }
    
    queueCondition_.notify_one();
    
    utils::Logger::debug("Translation enqueued: " + translation.sessionId + 
              " (priority: " + std::to_string(translation.priority) + ")");
}

bool PerformanceMonitor::dequeueTranslation(QueuedTranslation& translation) {
    if (!initialized_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    if (translationQueue_.empty()) {
        return false;
    }
    
    translation = translationQueue_.top();
    translationQueue_.pop();
    
    utils::Logger::debug("Translation dequeued: " + translation.sessionId);
    return true;
}

size_t PerformanceMonitor::getQueueSize() const {
    if (!initialized_) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(queueMutex_);
    return translationQueue_.size();
}

void PerformanceMonitor::clearQueue() {
    if (!initialized_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    while (!translationQueue_.empty()) {
        translationQueue_.pop();
    }
    
    utils::Logger::info("Translation queue cleared");
}

void PerformanceMonitor::setPriorityBoost(const std::string& sessionId, int boost) {
    if (!initialized_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(queueMutex_);
    sessionPriorityBoosts_[sessionId] = boost;
    
    utils::Logger::debug("Priority boost set for session " + sessionId + ": " + std::to_string(boost));
}

void PerformanceMonitor::setMemoryOptimizer(std::unique_ptr<MemoryOptimizer> optimizer) {
    std::lock_guard<std::mutex> lock(optimizerMutex_);
    memoryOptimizer_ = std::move(optimizer);
    utils::Logger::info("Memory optimizer set");
}

MemoryOptimizer::OptimizationResult PerformanceMonitor::optimizeMemory(
    MemoryOptimizer::Strategy strategy, 
    size_t targetMemoryMB) {
    
    std::lock_guard<std::mutex> lock(optimizerMutex_);
    
    if (!memoryOptimizer_) {
        utils::Logger::warn("No memory optimizer available");
        return MemoryOptimizer::OptimizationResult();
    }
    
    auto result = memoryOptimizer_->optimize(strategy, targetMemoryMB);
    
    utils::Logger::info("Memory optimization completed: freed " + 
             std::to_string(result.memoryFreedMB) + "MB, " +
             "unloaded " + std::to_string(result.modelsUnloaded) + " models");
    
    return result;
}

bool PerformanceMonitor::isMemoryOptimizationNeeded() const {
    std::lock_guard<std::mutex> lock(optimizerMutex_);
    
    if (!memoryOptimizer_) {
        return false;
    }
    
    // Check if current memory usage exceeds threshold
    auto currentMetrics = collectCurrentResourceMetrics();
    return currentMetrics.memoryUsageMB > thresholds_.maxMemoryUsage;
}

size_t PerformanceMonitor::getMemoryOptimizationPotential() const {
    std::lock_guard<std::mutex> lock(optimizerMutex_);
    
    if (!memoryOptimizer_) {
        return 0;
    }
    
    return memoryOptimizer_->estimateOptimizationPotential();
}

PerformanceMonitor::BottleneckAnalysis PerformanceMonitor::analyzeBottlenecks() const {
    if (!initialized_) {
        return BottleneckAnalysis();
    }
    
    auto stats = getStatistics(std::chrono::seconds(300)); // Last 5 minutes
    return performBottleneckAnalysis(stats);
}

std::vector<PerformanceMonitor::LanguagePairPerformance> PerformanceMonitor::getLanguagePairPerformance() const {
    if (!initialized_) {
        return {};
    }
    
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    std::unordered_map<std::string, std::vector<TranslationMetrics>> pairMetrics;
    
    // Group metrics by language pair
    for (const auto& metric : translationMetrics_) {
        std::string pairKey = metric.sourceLang + "->" + metric.targetLang;
        pairMetrics[pairKey].push_back(metric);
    }
    
    std::vector<LanguagePairPerformance> results;
    
    for (const auto& [pairKey, metrics] : pairMetrics) {
        if (metrics.empty()) continue;
        
        LanguagePairPerformance perf;
        
        // Parse language pair
        size_t arrowPos = pairKey.find("->");
        if (arrowPos != std::string::npos) {
            perf.sourceLang = pairKey.substr(0, arrowPos);
            perf.targetLang = pairKey.substr(arrowPos + 2);
        }
        
        // Calculate statistics
        perf.translationCount = metrics.size();
        
        std::chrono::milliseconds totalLatency(0);
        float totalConfidence = 0.0f;
        size_t errorCount = 0;
        
        for (const auto& metric : metrics) {
            totalLatency += metric.latency;
            totalConfidence += metric.confidence;
            if (!metric.sessionId.empty() && metric.confidence < 0.5f) {
                errorCount++;
            }
        }
        
        perf.averageLatency = std::chrono::milliseconds(totalLatency.count() / metrics.size());
        perf.averageConfidence = totalConfidence / metrics.size();
        perf.errorRate = static_cast<float>(errorCount) / metrics.size();
        
        results.push_back(perf);
    }
    
    // Sort by translation count (most used pairs first)
    std::sort(results.begin(), results.end(), 
              [](const LanguagePairPerformance& a, const LanguagePairPerformance& b) {
                  return a.translationCount > b.translationCount;
              });
    
    return results;
}

void PerformanceMonitor::startRealTimeMonitoring(std::chrono::seconds interval) {
    if (!initialized_ || realTimeMonitoringActive_) {
        return;
    }
    
    config_.resourceMonitoringInterval = interval;
    stopMonitoring_ = false;
    realTimeMonitoringActive_ = true;
    
    monitoringThread_ = std::make_unique<std::thread>(&PerformanceMonitor::realTimeMonitoringLoop, this);
    
    utils::Logger::info("Real-time monitoring started with " + std::to_string(interval.count()) + "s interval");
}

void PerformanceMonitor::stopRealTimeMonitoring() {
    if (!realTimeMonitoringActive_) {
        return;
    }
    
    stopMonitoring_ = true;
    realTimeMonitoringActive_ = false;
    
    if (monitoringThread_ && monitoringThread_->joinable()) {
        monitoringThread_->join();
    }
    monitoringThread_.reset();
    
    utils::Logger::info("Real-time monitoring stopped");
}

bool PerformanceMonitor::isRealTimeMonitoringActive() const {
    return realTimeMonitoringActive_;
}

std::string PerformanceMonitor::exportStatisticsToJson(std::chrono::seconds period) const {
    if (!initialized_) {
        return "{}";
    }
    
    auto stats = getStatistics(period);
    return statisticsToJson(stats);
}

std::string PerformanceMonitor::exportMetricsToCSV(std::chrono::seconds period) const {
    if (!initialized_) {
        return "";
    }
    
    auto since = std::chrono::steady_clock::now() - period;
    std::vector<TranslationMetrics> periodMetrics;
    
    {
        std::lock_guard<std::mutex> lock(metricsMutex_);
        for (const auto& metric : translationMetrics_) {
            if (metric.timestamp >= since) {
                periodMetrics.push_back(metric);
            }
        }
    }
    
    return metricsToCsv(periodMetrics);
}

bool PerformanceMonitor::savePerformanceReport(const std::string& filePath, std::chrono::seconds period) const {
    if (!initialized_) {
        return false;
    }
    
    try {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            utils::Logger::error("Failed to open file for performance report: " + filePath);
            return false;
        }
        
        auto stats = getStatistics(period);
        file << statisticsToJson(stats);
        
        utils::Logger::info("Performance report saved to: " + filePath);
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to save performance report: " + std::string(e.what()));
        return false;
    }
}

void PerformanceMonitor::setMetricsRetentionPeriod(std::chrono::hours period) {
    config_.metricsRetentionPeriod = period;
    utils::Logger::info("Metrics retention period set to " + std::to_string(period.count()) + " hours");
}

void PerformanceMonitor::setResourceMonitoringInterval(std::chrono::seconds interval) {
    config_.resourceMonitoringInterval = interval;
    utils::Logger::info("Resource monitoring interval set to " + std::to_string(interval.count()) + " seconds");
}

void PerformanceMonitor::enableDetailedLogging(bool enabled) {
    config_.detailedLoggingEnabled = enabled;
    utils::Logger::info("Detailed logging " + std::string(enabled ? "enabled" : "disabled"));
}

// Private methods implementation

void PerformanceMonitor::checkThresholds(const TranslationMetrics& metrics) {
    // Check latency threshold
    if (metrics.latency > thresholds_.maxLatency) {
        PerformanceWarning warning(
            PerformanceWarning::Type::HIGH_LATENCY,
            "Translation latency exceeded threshold: " + 
            std::to_string(metrics.latency.count()) + "ms > " + 
            std::to_string(thresholds_.maxLatency.count()) + "ms",
            0.7f
        );
        warning.recommendation = "Consider optimizing model or enabling GPU acceleration";
        warning.metadata["actual_latency"] = std::to_string(metrics.latency.count());
        warning.metadata["threshold"] = std::to_string(thresholds_.maxLatency.count());
        emitWarning(warning);
    }
    
    // Check confidence threshold
    if (metrics.confidence < thresholds_.minAverageConfidence) {
        PerformanceWarning warning(
            PerformanceWarning::Type::LOW_CONFIDENCE,
            "Translation confidence below threshold: " + 
            std::to_string(metrics.confidence) + " < " + 
            std::to_string(thresholds_.minAverageConfidence),
            0.6f
        );
        warning.recommendation = "Consider using higher quality models or improving input quality";
        warning.metadata["actual_confidence"] = std::to_string(metrics.confidence);
        warning.metadata["threshold"] = std::to_string(thresholds_.minAverageConfidence);
        emitWarning(warning);
    }
}

void PerformanceMonitor::checkResourceThresholds(const ResourceMetrics& metrics) {
    // Check CPU usage
    if (metrics.cpuUsagePercent > thresholds_.maxCpuUsage) {
        PerformanceWarning warning(
            PerformanceWarning::Type::HIGH_CPU_USAGE,
            "CPU usage exceeded threshold: " + 
            std::to_string(metrics.cpuUsagePercent) + "% > " + 
            std::to_string(thresholds_.maxCpuUsage) + "%",
            0.8f
        );
        warning.recommendation = "Consider scaling up CPU resources or optimizing processing";
        emitWarning(warning);
    }
    
    // Check memory usage
    if (metrics.memoryUsageMB > thresholds_.maxMemoryUsage) {
        PerformanceWarning warning(
            PerformanceWarning::Type::HIGH_MEMORY_USAGE,
            "Memory usage exceeded threshold: " + 
            std::to_string(metrics.memoryUsageMB) + "MB > " + 
            std::to_string(thresholds_.maxMemoryUsage) + "MB",
            0.8f
        );
        warning.recommendation = "Consider memory optimization or increasing available memory";
        emitWarning(warning);
    }
    
    // Check GPU memory usage
    if (metrics.gpuMemoryUsageMB > thresholds_.maxGpuMemoryUsage) {
        PerformanceWarning warning(
            PerformanceWarning::Type::HIGH_GPU_MEMORY_USAGE,
            "GPU memory usage exceeded threshold: " + 
            std::to_string(metrics.gpuMemoryUsageMB) + "MB > " + 
            std::to_string(thresholds_.maxGpuMemoryUsage) + "MB",
            0.8f
        );
        warning.recommendation = "Consider model quantization or GPU memory optimization";
        emitWarning(warning);
    }
}

void PerformanceMonitor::emitWarning(const PerformanceWarning& warning) {
    {
        std::lock_guard<std::mutex> lock(warningsMutex_);
        warnings_.push_back(warning);
        
        // Cleanup old warnings if we exceed the limit
        if (warnings_.size() > config_.maxWarningsInMemory) {
            cleanupOldWarnings();
        }
    }
    
    // Call warning callback if set
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (warningCallback_) {
            try {
                warningCallback_(warning);
            } catch (const std::exception& e) {
                utils::Logger::error("Warning callback failed: " + std::string(e.what()));
            }
        }
    }
    
    utils::Logger::warn("Performance warning: " + warning.message);
}

void PerformanceMonitor::cleanupOldMetrics() {
    auto cutoff = std::chrono::steady_clock::now() - config_.metricsRetentionPeriod;
    
    // Remove old translation metrics
    translationMetrics_.erase(
        std::remove_if(translationMetrics_.begin(), translationMetrics_.end(),
                      [cutoff](const TranslationMetrics& m) { return m.timestamp < cutoff; }),
        translationMetrics_.end()
    );
    
    // Remove old resource metrics
    resourceMetrics_.erase(
        std::remove_if(resourceMetrics_.begin(), resourceMetrics_.end(),
                      [cutoff](const ResourceMetrics& m) { return m.timestamp < cutoff; }),
        resourceMetrics_.end()
    );
}

void PerformanceMonitor::cleanupOldWarnings() {
    auto cutoff = std::chrono::steady_clock::now() - std::chrono::hours(24); // Keep warnings for 24 hours
    
    warnings_.erase(
        std::remove_if(warnings_.begin(), warnings_.end(),
                      [cutoff](const PerformanceWarning& w) { return w.timestamp < cutoff; }),
        warnings_.end()
    );
}

void PerformanceMonitor::realTimeMonitoringLoop() {
    while (!stopMonitoring_) {
        try {
            // Collect current resource metrics
            auto resourceMetrics = collectCurrentResourceMetrics();
            recordResourceMetrics(resourceMetrics);
            
            // Sleep for the monitoring interval
            std::this_thread::sleep_for(config_.resourceMonitoringInterval);
            
        } catch (const std::exception& e) {
            utils::Logger::error("Error in real-time monitoring loop: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::seconds(5)); // Wait before retrying
        }
    }
}

ResourceMetrics PerformanceMonitor::collectCurrentResourceMetrics() const {
    ResourceMetrics metrics;
    metrics.timestamp = std::chrono::steady_clock::now();
    
    try {
#ifdef _WIN32
        // Windows implementation
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            DWORDLONG totalPhysMem = memInfo.ullTotalPhys;
            DWORDLONG physMemUsed = totalPhysMem - memInfo.ullAvailPhys;
            metrics.memoryUsageMB = static_cast<size_t>(physMemUsed / (1024 * 1024));
        }
        
        // CPU usage would require more complex implementation on Windows
        metrics.cpuUsagePercent = 0.0f; // Placeholder
        
#elif __linux__
        // Linux implementation
        struct sysinfo memInfo;
        if (sysinfo(&memInfo) == 0) {
            long long totalPhysMem = memInfo.totalram * memInfo.mem_unit;
            long long physMemUsed = (memInfo.totalram - memInfo.freeram) * memInfo.mem_unit;
            metrics.memoryUsageMB = static_cast<size_t>(physMemUsed / (1024 * 1024));
        }
        
        // Read CPU usage from /proc/stat
        std::ifstream statFile("/proc/stat");
        if (statFile.is_open()) {
            std::string line;
            std::getline(statFile, line);
            // Parse CPU usage - simplified implementation
            metrics.cpuUsagePercent = 50.0f; // Placeholder
        }
        
#elif __APPLE__
        // macOS implementation
        vm_size_t page_size;
        vm_statistics64_data_t vm_stat;
        mach_msg_type_number_t host_size = sizeof(vm_statistics64_data_t) / sizeof(natural_t);
        
        if (host_page_size(mach_host_self(), &page_size) == KERN_SUCCESS &&
            host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm_stat, &host_size) == KERN_SUCCESS) {
            
            uint64_t total_mem = (vm_stat.free_count + vm_stat.active_count + 
                                 vm_stat.inactive_count + vm_stat.wire_count) * page_size;
            uint64_t used_mem = (vm_stat.active_count + vm_stat.inactive_count + 
                                vm_stat.wire_count) * page_size;
            
            metrics.memoryUsageMB = static_cast<size_t>(used_mem / (1024 * 1024));
        }
        
        metrics.cpuUsagePercent = 0.0f; // Placeholder
#endif
        
        // GPU metrics would require CUDA or other GPU APIs
        metrics.gpuMemoryUsageMB = 0;
        metrics.gpuUtilizationPercent = 0.0f;
        metrics.diskUsageMB = 0;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to collect resource metrics: " + std::string(e.what()));
    }
    
    return metrics;
}

std::chrono::milliseconds PerformanceMonitor::calculatePercentile(
    const std::vector<std::chrono::milliseconds>& latencies, 
    float percentile) const {
    
    if (latencies.empty()) {
        return std::chrono::milliseconds(0);
    }
    
    size_t index = static_cast<size_t>(std::ceil(percentile * latencies.size())) - 1;
    index = std::min(index, latencies.size() - 1);
    
    return latencies[index];
}

float PerformanceMonitor::calculateThroughput(
    const std::vector<TranslationMetrics>& metrics, 
    std::chrono::seconds period) const {
    
    if (metrics.empty() || period.count() == 0) {
        return 0.0f;
    }
    
    return static_cast<float>(metrics.size()) / period.count();
}

PerformanceMonitor::BottleneckAnalysis PerformanceMonitor::performBottleneckAnalysis(
    const PerformanceStatistics& stats) const {
    
    BottleneckAnalysis analysis;
    analysis.confidenceScore = 0.8f; // Default confidence
    
    // Analyze primary bottleneck based on statistics
    if (stats.averageLatency > thresholds_.maxLatency) {
        if (stats.peakCpuUsage > 90.0f) {
            analysis.primaryBottleneck = "cpu";
            analysis.contributingFactors.push_back("High CPU utilization");
            analysis.recommendations.push_back("Consider CPU scaling or optimization");
        } else if (stats.peakMemoryUsage > thresholds_.maxMemoryUsage) {
            analysis.primaryBottleneck = "memory";
            analysis.contributingFactors.push_back("High memory usage");
            analysis.recommendations.push_back("Implement memory optimization or increase available memory");
        } else if (stats.peakGpuMemoryUsage > thresholds_.maxGpuMemoryUsage) {
            analysis.primaryBottleneck = "gpu";
            analysis.contributingFactors.push_back("High GPU memory usage");
            analysis.recommendations.push_back("Consider model quantization or GPU memory optimization");
        } else {
            analysis.primaryBottleneck = "model_loading";
            analysis.contributingFactors.push_back("Model loading or inference inefficiency");
            analysis.recommendations.push_back("Optimize model loading and caching strategies");
        }
    } else if (stats.translationsPerSecond < thresholds_.minThroughput) {
        analysis.primaryBottleneck = "network";
        analysis.contributingFactors.push_back("Low throughput despite acceptable latency");
        analysis.recommendations.push_back("Check network connectivity and queue management");
    } else {
        analysis.primaryBottleneck = "none";
        analysis.recommendations.push_back("System performance is within acceptable thresholds");
    }
    
    // Add cache-related factors
    if (stats.cacheHitRate < thresholds_.minCacheHitRate) {
        analysis.contributingFactors.push_back("Low cache hit rate");
        analysis.recommendations.push_back("Optimize caching strategy and cache size");
    }
    
    // Add quality-related factors
    if (stats.averageConfidence < thresholds_.minAverageConfidence) {
        analysis.contributingFactors.push_back("Low translation confidence");
        analysis.recommendations.push_back("Consider using higher quality models or improving input preprocessing");
    }
    
    return analysis;
}

std::string PerformanceMonitor::metricsToJson(const std::vector<TranslationMetrics>& metrics) const {
    std::ostringstream json;
    json << "{\n  \"metrics\": [\n";
    
    for (size_t i = 0; i < metrics.size(); ++i) {
        const auto& metric = metrics[i];
        json << "    {\n";
        json << "      \"timestamp\": " << std::chrono::duration_cast<std::chrono::milliseconds>(
                metric.timestamp.time_since_epoch()).count() << ",\n";
        json << "      \"latency_ms\": " << metric.latency.count() << ",\n";
        json << "      \"source_lang\": \"" << metric.sourceLang << "\",\n";
        json << "      \"target_lang\": \"" << metric.targetLang << "\",\n";
        json << "      \"input_length\": " << metric.inputLength << ",\n";
        json << "      \"output_length\": " << metric.outputLength << ",\n";
        json << "      \"confidence\": " << metric.confidence << ",\n";
        json << "      \"used_gpu\": " << (metric.usedGPU ? "true" : "false") << ",\n";
        json << "      \"used_cache\": " << (metric.usedCache ? "true" : "false") << ",\n";
        json << "      \"session_id\": \"" << metric.sessionId << "\"\n";
        json << "    }";
        
        if (i < metrics.size() - 1) {
            json << ",";
        }
        json << "\n";
    }
    
    json << "  ]\n}";
    return json.str();
}

std::string PerformanceMonitor::statisticsToJson(const PerformanceStatistics& stats) const {
    std::ostringstream json;
    json << std::fixed << std::setprecision(2);
    
    json << "{\n";
    json << "  \"period\": {\n";
    json << "    \"duration_seconds\": " << stats.periodDuration.count() << ",\n";
    json << "    \"start_timestamp\": " << std::chrono::duration_cast<std::chrono::milliseconds>(
            stats.periodStart.time_since_epoch()).count() << ",\n";
    json << "    \"end_timestamp\": " << std::chrono::duration_cast<std::chrono::milliseconds>(
            stats.periodEnd.time_since_epoch()).count() << "\n";
    json << "  },\n";
    
    json << "  \"latency\": {\n";
    json << "    \"average_ms\": " << stats.averageLatency.count() << ",\n";
    json << "    \"median_ms\": " << stats.medianLatency.count() << ",\n";
    json << "    \"p95_ms\": " << stats.p95Latency.count() << ",\n";
    json << "    \"p99_ms\": " << stats.p99Latency.count() << ",\n";
    json << "    \"min_ms\": " << stats.minLatency.count() << ",\n";
    json << "    \"max_ms\": " << stats.maxLatency.count() << "\n";
    json << "  },\n";
    
    json << "  \"throughput\": {\n";
    json << "    \"translations_per_second\": " << stats.translationsPerSecond << ",\n";
    json << "    \"characters_per_second\": " << stats.charactersPerSecond << ",\n";
    json << "    \"total_translations\": " << stats.totalTranslations << ",\n";
    json << "    \"total_characters_processed\": " << stats.totalCharactersProcessed << "\n";
    json << "  },\n";
    
    json << "  \"resources\": {\n";
    json << "    \"cpu\": {\n";
    json << "      \"average_usage_percent\": " << stats.averageCpuUsage << ",\n";
    json << "      \"peak_usage_percent\": " << stats.peakCpuUsage << "\n";
    json << "    },\n";
    json << "    \"memory\": {\n";
    json << "      \"average_usage_mb\": " << stats.averageMemoryUsage << ",\n";
    json << "      \"peak_usage_mb\": " << stats.peakMemoryUsage << "\n";
    json << "    },\n";
    json << "    \"gpu_memory\": {\n";
    json << "      \"average_usage_mb\": " << stats.averageGpuMemoryUsage << ",\n";
    json << "      \"peak_usage_mb\": " << stats.peakGpuMemoryUsage << "\n";
    json << "    },\n";
    json << "    \"gpu_utilization\": {\n";
    json << "      \"average_percent\": " << stats.averageGpuUtilization << ",\n";
    json << "      \"peak_percent\": " << stats.peakGpuUtilization << "\n";
    json << "    }\n";
    json << "  },\n";
    
    json << "  \"quality\": {\n";
    json << "    \"average_confidence\": " << stats.averageConfidence << ",\n";
    json << "    \"min_confidence\": " << stats.minConfidence << ",\n";
    json << "    \"max_confidence\": " << stats.maxConfidence << ",\n";
    json << "    \"low_quality_translations\": " << stats.lowQualityTranslations << "\n";
    json << "  },\n";
    
    json << "  \"cache\": {\n";
    json << "    \"hit_rate_percent\": " << stats.cacheHitRate << ",\n";
    json << "    \"hits\": " << stats.cacheHits << ",\n";
    json << "    \"misses\": " << stats.cacheMisses << "\n";
    json << "  },\n";
    
    json << "  \"errors\": {\n";
    json << "    \"total\": " << stats.totalErrors << ",\n";
    json << "    \"timeout_errors\": " << stats.timeoutErrors << ",\n";
    json << "    \"memory_errors\": " << stats.memoryErrors << ",\n";
    json << "    \"gpu_errors\": " << stats.gpuErrors << "\n";
    json << "  }\n";
    
    json << "}";
    return json.str();
}

std::string PerformanceMonitor::metricsToCsv(const std::vector<TranslationMetrics>& metrics) const {
    std::ostringstream csv;
    
    // CSV header
    csv << "timestamp,latency_ms,source_lang,target_lang,input_length,output_length,confidence,used_gpu,used_cache,session_id\n";
    
    // CSV data
    for (const auto& metric : metrics) {
        csv << std::chrono::duration_cast<std::chrono::milliseconds>(
                metric.timestamp.time_since_epoch()).count() << ",";
        csv << metric.latency.count() << ",";
        csv << metric.sourceLang << ",";
        csv << metric.targetLang << ",";
        csv << metric.inputLength << ",";
        csv << metric.outputLength << ",";
        csv << std::fixed << std::setprecision(3) << metric.confidence << ",";
        csv << (metric.usedGPU ? "1" : "0") << ",";
        csv << (metric.usedCache ? "1" : "0") << ",";
        csv << metric.sessionId << "\n";
    }
    
    return csv.str();
}

} // namespace mt
} // namespace speechrnt