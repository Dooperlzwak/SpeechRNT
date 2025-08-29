#include "utils/performance_monitor.hpp"
#include "utils/logging.hpp"
#include "utils/gpu_manager.hpp"
#include <algorithm>
#include <numeric>
#include <sstream>
#include <fstream>
#include <cmath>

using namespace utils;

// Platform-specific includes for system metrics
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif __linux__
#include <sys/sysinfo.h>
#include <unistd.h>
#elif __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#endif

namespace speechrnt {
namespace utils {

// Static metric name constants
const std::string PerformanceMonitor::METRIC_STT_LATENCY = "stt.latency_ms";
const std::string PerformanceMonitor::METRIC_MT_LATENCY = "mt.latency_ms";
const std::string PerformanceMonitor::METRIC_TTS_LATENCY = "tts.latency_ms";
const std::string PerformanceMonitor::METRIC_END_TO_END_LATENCY = "pipeline.end_to_end_latency_ms";
const std::string PerformanceMonitor::METRIC_AUDIO_PROCESSING_LATENCY = "audio.processing_latency_ms";
const std::string PerformanceMonitor::METRIC_VAD_LATENCY = "vad.latency_ms";
const std::string PerformanceMonitor::METRIC_PIPELINE_THROUGHPUT = "pipeline.throughput_ops_per_sec";
const std::string PerformanceMonitor::METRIC_MEMORY_USAGE = "system.memory_usage_mb";
const std::string PerformanceMonitor::METRIC_GPU_MEMORY_USAGE = "gpu.memory_usage_mb";
const std::string PerformanceMonitor::METRIC_GPU_UTILIZATION = "gpu.utilization_percent";
const std::string PerformanceMonitor::METRIC_CPU_USAGE = "system.cpu_usage_percent";
const std::string PerformanceMonitor::METRIC_WEBSOCKET_LATENCY = "websocket.latency_ms";
const std::string PerformanceMonitor::METRIC_ACTIVE_SESSIONS = "sessions.active_count";
const std::string PerformanceMonitor::METRIC_ERRORS_COUNT = "errors.count";

// Enhanced STT-specific metrics
const std::string PerformanceMonitor::METRIC_STT_VAD_LATENCY = "stt.vad_latency_ms";
const std::string PerformanceMonitor::METRIC_STT_PREPROCESSING_LATENCY = "stt.preprocessing_latency_ms";
const std::string PerformanceMonitor::METRIC_STT_INFERENCE_LATENCY = "stt.inference_latency_ms";
const std::string PerformanceMonitor::METRIC_STT_POSTPROCESSING_LATENCY = "stt.postprocessing_latency_ms";
const std::string PerformanceMonitor::METRIC_STT_STREAMING_LATENCY = "stt.streaming_latency_ms";
const std::string PerformanceMonitor::METRIC_STT_CONFIDENCE_SCORE = "stt.confidence_score";
const std::string PerformanceMonitor::METRIC_STT_ACCURACY_SCORE = "stt.accuracy_score";
const std::string PerformanceMonitor::METRIC_STT_THROUGHPUT = "stt.throughput_ops_per_sec";
const std::string PerformanceMonitor::METRIC_STT_CONCURRENT_TRANSCRIPTIONS = "stt.concurrent_transcriptions";
const std::string PerformanceMonitor::METRIC_STT_QUEUE_SIZE = "stt.queue_size";
const std::string PerformanceMonitor::METRIC_STT_MODEL_LOAD_TIME = "stt.model_load_time_ms";
const std::string PerformanceMonitor::METRIC_STT_LANGUAGE_DETECTION_LATENCY = "stt.language_detection_latency_ms";
const std::string PerformanceMonitor::METRIC_STT_LANGUAGE_CONFIDENCE = "stt.language_confidence";
const std::string PerformanceMonitor::METRIC_STT_BUFFER_USAGE = "stt.buffer_usage_mb";
const std::string PerformanceMonitor::METRIC_STT_STREAMING_UPDATES = "stt.streaming_updates_count";
const std::string PerformanceMonitor::METRIC_VAD_ACCURACY = "vad.accuracy_score";
const std::string PerformanceMonitor::METRIC_VAD_RESPONSE_TIME = "vad.response_time_ms";
const std::string PerformanceMonitor::METRIC_VAD_STATE_CHANGES = "vad.state_changes_count";
const std::string PerformanceMonitor::METRIC_VAD_SPEECH_DETECTION_RATE = "vad.speech_detection_rate";

LatencyTimer::LatencyTimer(const std::string& metricName)
    : metricName_(metricName)
    , startTime_(std::chrono::steady_clock::now())
    , stopped_(false) {
}

LatencyTimer::~LatencyTimer() {
    if (!stopped_) {
        stop();
    }
}

void LatencyTimer::stop() {
    if (!stopped_) {
        double elapsedMs = getElapsedMs();
        PerformanceMonitor::getInstance().recordLatency(metricName_, elapsedMs);
        stopped_ = true;
    }
}

double LatencyTimer::getElapsedMs() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - startTime_);
    return duration.count() / 1000.0; // Convert to milliseconds
}

PerformanceMonitor& PerformanceMonitor::getInstance() {
    static PerformanceMonitor instance;
    return instance;
}

PerformanceMonitor::~PerformanceMonitor() {
    cleanup();
}

bool PerformanceMonitor::initialize(bool enableSystemMetrics, int collectionIntervalMs) {
    if (initialized_.load()) {
        return true;
    }
    
    collectionIntervalMs_ = collectionIntervalMs;
    systemMetricsEnabled_ = enableSystemMetrics;
    
    Logger::info("Performance monitor initialized (system metrics: " + 
                std::string(enableSystemMetrics ? "enabled" : "disabled") + 
                ", interval: " + std::to_string(collectionIntervalMs) + "ms)");
    
    if (enableSystemMetrics) {
        startSystemMetricsCollection();
    }
    
    initialized_ = true;
    return true;
}

void PerformanceMonitor::recordMetric(const std::string& name, double value, 
                                    const std::string& unit,
                                    const std::map<std::string, std::string>& tags) {
    if (!enabled_.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    MetricDataPoint point(value, unit);
    point.tags = tags;
    
    auto& metricData = metrics_[name];
    metricData.push_back(point);
    
    // Prune old data if we exceed max points
    if (metricData.size() > maxDataPoints_.load()) {
        metricData.erase(metricData.begin(), 
                        metricData.begin() + (metricData.size() - maxDataPoints_.load()));
    }
    
    totalMetricsRecorded_++;
}

void PerformanceMonitor::recordLatency(const std::string& name, double latencyMs,
                                     const std::map<std::string, std::string>& tags) {
    recordMetric(name, latencyMs, "ms", tags);
    totalLatencyMeasurements_++;
}

void PerformanceMonitor::recordThroughput(const std::string& name, double itemsPerSecond,
                                        const std::map<std::string, std::string>& tags) {
    recordMetric(name, itemsPerSecond, "ops/sec", tags);
    totalThroughputMeasurements_++;
}

void PerformanceMonitor::recordCounter(const std::string& name, int increment,
                                     const std::map<std::string, std::string>& tags) {
    recordMetric(name, static_cast<double>(increment), "count", tags);
}

std::unique_ptr<LatencyTimer> PerformanceMonitor::startLatencyTimer(const std::string& name) {
    return std::make_unique<LatencyTimer>(name);
}

MetricStats PerformanceMonitor::getMetricStats(const std::string& name, int windowMinutes) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    auto it = metrics_.find(name);
    if (it == metrics_.end()) {
        return MetricStats();
    }
    
    auto filteredPoints = filterByTimeWindow(it->second, windowMinutes);
    return calculateStats(filteredPoints);
}

std::vector<MetricDataPoint> PerformanceMonitor::getRecentMetrics(const std::string& name, size_t maxPoints) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    auto it = metrics_.find(name);
    if (it == metrics_.end()) {
        return {};
    }
    
    const auto& points = it->second;
    if (points.size() <= maxPoints) {
        return points;
    }
    
    // Return the most recent points
    return std::vector<MetricDataPoint>(points.end() - maxPoints, points.end());
}

std::vector<std::string> PerformanceMonitor::getAvailableMetrics() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    std::vector<std::string> names;
    names.reserve(metrics_.size());
    
    for (const auto& pair : metrics_) {
        names.push_back(pair.first);
    }
    
    return names;
}

std::map<std::string, double> PerformanceMonitor::getSystemSummary() const {
    std::map<std::string, double> summary;
    
    // Get recent stats for key metrics
    auto endToEndStats = getMetricStats(METRIC_END_TO_END_LATENCY, 5);
    auto memoryStats = getMetricStats(METRIC_MEMORY_USAGE, 1);
    auto cpuStats = getMetricStats(METRIC_CPU_USAGE, 1);
    auto gpuMemoryStats = getMetricStats(METRIC_GPU_MEMORY_USAGE, 1);
    
    summary["end_to_end_latency_mean_ms"] = endToEndStats.mean;
    summary["end_to_end_latency_p95_ms"] = endToEndStats.p95;
    summary["memory_usage_mb"] = memoryStats.mean;
    summary["cpu_usage_percent"] = cpuStats.mean;
    summary["gpu_memory_usage_mb"] = gpuMemoryStats.mean;
    summary["total_metrics_recorded"] = static_cast<double>(totalMetricsRecorded_.load());
    
    return summary;
}

std::map<std::string, MetricStats> PerformanceMonitor::getPipelineMetrics() const {
    std::map<std::string, MetricStats> pipelineMetrics;
    
    // Get stats for all pipeline-related metrics
    pipelineMetrics["stt"] = getMetricStats(METRIC_STT_LATENCY, 10);
    pipelineMetrics["mt"] = getMetricStats(METRIC_MT_LATENCY, 10);
    pipelineMetrics["tts"] = getMetricStats(METRIC_TTS_LATENCY, 10);
    pipelineMetrics["vad"] = getMetricStats(METRIC_VAD_LATENCY, 10);
    pipelineMetrics["end_to_end"] = getMetricStats(METRIC_END_TO_END_LATENCY, 10);
    pipelineMetrics["throughput"] = getMetricStats(METRIC_PIPELINE_THROUGHPUT, 10);
    
    return pipelineMetrics;
}

std::map<std::string, MetricStats> PerformanceMonitor::getSTTMetrics(int windowMinutes) const {
    std::map<std::string, MetricStats> sttMetrics;
    
    // Pipeline stage latencies
    sttMetrics["vad_latency"] = getMetricStats(METRIC_STT_VAD_LATENCY, windowMinutes);
    sttMetrics["preprocessing_latency"] = getMetricStats(METRIC_STT_PREPROCESSING_LATENCY, windowMinutes);
    sttMetrics["inference_latency"] = getMetricStats(METRIC_STT_INFERENCE_LATENCY, windowMinutes);
    sttMetrics["postprocessing_latency"] = getMetricStats(METRIC_STT_POSTPROCESSING_LATENCY, windowMinutes);
    sttMetrics["streaming_latency"] = getMetricStats(METRIC_STT_STREAMING_LATENCY, windowMinutes);
    sttMetrics["overall_latency"] = getMetricStats(METRIC_STT_LATENCY, windowMinutes);
    
    // Quality metrics
    sttMetrics["confidence_score"] = getMetricStats(METRIC_STT_CONFIDENCE_SCORE, windowMinutes);
    sttMetrics["accuracy_score"] = getMetricStats(METRIC_STT_ACCURACY_SCORE, windowMinutes);
    sttMetrics["language_confidence"] = getMetricStats(METRIC_STT_LANGUAGE_CONFIDENCE, windowMinutes);
    
    // Throughput and concurrency
    sttMetrics["throughput"] = getMetricStats(METRIC_STT_THROUGHPUT, windowMinutes);
    sttMetrics["concurrent_transcriptions"] = getMetricStats(METRIC_STT_CONCURRENT_TRANSCRIPTIONS, windowMinutes);
    sttMetrics["queue_size"] = getMetricStats(METRIC_STT_QUEUE_SIZE, windowMinutes);
    
    // Resource usage
    sttMetrics["buffer_usage"] = getMetricStats(METRIC_STT_BUFFER_USAGE, windowMinutes);
    sttMetrics["model_load_time"] = getMetricStats(METRIC_STT_MODEL_LOAD_TIME, windowMinutes);
    
    // Streaming metrics
    sttMetrics["streaming_updates"] = getMetricStats(METRIC_STT_STREAMING_UPDATES, windowMinutes);
    sttMetrics["language_detection_latency"] = getMetricStats(METRIC_STT_LANGUAGE_DETECTION_LATENCY, windowMinutes);
    
    // VAD metrics
    sttMetrics["vad_accuracy"] = getMetricStats(METRIC_VAD_ACCURACY, windowMinutes);
    sttMetrics["vad_response_time"] = getMetricStats(METRIC_VAD_RESPONSE_TIME, windowMinutes);
    sttMetrics["vad_state_changes"] = getMetricStats(METRIC_VAD_STATE_CHANGES, windowMinutes);
    sttMetrics["vad_speech_detection_rate"] = getMetricStats(METRIC_VAD_SPEECH_DETECTION_RATE, windowMinutes);
    
    return sttMetrics;
}

void PerformanceMonitor::recordSTTStageLatency(const std::string& stage, double latencyMs, uint32_t utteranceId) {
    if (!enabled_.load()) {
        return;
    }
    
    std::map<std::string, std::string> tags;
    if (utteranceId > 0) {
        tags["utterance_id"] = std::to_string(utteranceId);
    }
    
    if (stage == "vad") {
        recordLatency(METRIC_STT_VAD_LATENCY, latencyMs, tags);
    } else if (stage == "preprocessing") {
        recordLatency(METRIC_STT_PREPROCESSING_LATENCY, latencyMs, tags);
    } else if (stage == "inference") {
        recordLatency(METRIC_STT_INFERENCE_LATENCY, latencyMs, tags);
    } else if (stage == "postprocessing") {
        recordLatency(METRIC_STT_POSTPROCESSING_LATENCY, latencyMs, tags);
    } else if (stage == "streaming") {
        recordLatency(METRIC_STT_STREAMING_LATENCY, latencyMs, tags);
    }
}

void PerformanceMonitor::recordSTTConfidence(float confidence, bool isPartial, uint32_t utteranceId) {
    if (!enabled_.load()) {
        return;
    }
    
    std::map<std::string, std::string> tags;
    tags["is_partial"] = isPartial ? "true" : "false";
    if (utteranceId > 0) {
        tags["utterance_id"] = std::to_string(utteranceId);
    }
    
    recordMetric(METRIC_STT_CONFIDENCE_SCORE, static_cast<double>(confidence), "score", tags);
}

void PerformanceMonitor::recordSTTAccuracy(float accuracy, uint32_t utteranceId) {
    if (!enabled_.load()) {
        return;
    }
    
    std::map<std::string, std::string> tags;
    if (utteranceId > 0) {
        tags["utterance_id"] = std::to_string(utteranceId);
    }
    
    recordMetric(METRIC_STT_ACCURACY_SCORE, static_cast<double>(accuracy), "score", tags);
}

void PerformanceMonitor::recordSTTThroughput(double transcriptionsPerSecond) {
    if (!enabled_.load()) {
        return;
    }
    
    recordThroughput(METRIC_STT_THROUGHPUT, transcriptionsPerSecond);
}

void PerformanceMonitor::recordConcurrentTranscriptions(int count) {
    if (!enabled_.load()) {
        return;
    }
    
    recordMetric(METRIC_STT_CONCURRENT_TRANSCRIPTIONS, static_cast<double>(count), "count");
}

void PerformanceMonitor::recordVADMetrics(double responseTimeMs, float accuracy, bool stateChange) {
    if (!enabled_.load()) {
        return;
    }
    
    recordLatency(METRIC_VAD_RESPONSE_TIME, responseTimeMs);
    
    if (accuracy >= 0.0f) {
        recordMetric(METRIC_VAD_ACCURACY, static_cast<double>(accuracy), "score");
    }
    
    if (stateChange) {
        recordCounter(METRIC_VAD_STATE_CHANGES);
    }
}

void PerformanceMonitor::recordStreamingUpdate(double updateLatencyMs, size_t textLength, bool isIncremental) {
    if (!enabled_.load()) {
        return;
    }
    
    std::map<std::string, std::string> tags;
    tags["is_incremental"] = isIncremental ? "true" : "false";
    tags["text_length"] = std::to_string(textLength);
    
    recordLatency(METRIC_STT_STREAMING_LATENCY, updateLatencyMs, tags);
    recordCounter(METRIC_STT_STREAMING_UPDATES, 1, tags);
}

void PerformanceMonitor::recordLanguageDetection(double detectionLatencyMs, float confidence, const std::string& detectedLanguage) {
    if (!enabled_.load()) {
        return;
    }
    
    std::map<std::string, std::string> tags;
    tags["detected_language"] = detectedLanguage;
    
    recordLatency(METRIC_STT_LANGUAGE_DETECTION_LATENCY, detectionLatencyMs, tags);
    recordMetric(METRIC_STT_LANGUAGE_CONFIDENCE, static_cast<double>(confidence), "score", tags);
}

void PerformanceMonitor::recordBufferUsage(double bufferSizeMB, float utilizationPercent) {
    if (!enabled_.load()) {
        return;
    }
    
    std::map<std::string, std::string> tags;
    tags["utilization_percent"] = std::to_string(utilizationPercent);
    
    recordMetric(METRIC_STT_BUFFER_USAGE, bufferSizeMB, "MB", tags);
}

std::map<std::string, double> PerformanceMonitor::getSTTPerformanceSummary() const {
    std::map<std::string, double> summary;
    
    // Get recent stats for key STT metrics (last 5 minutes)
    auto sttLatencyStats = getMetricStats(METRIC_STT_LATENCY, 5);
    auto vadLatencyStats = getMetricStats(METRIC_STT_VAD_LATENCY, 5);
    auto inferenceLatencyStats = getMetricStats(METRIC_STT_INFERENCE_LATENCY, 5);
    auto confidenceStats = getMetricStats(METRIC_STT_CONFIDENCE_SCORE, 5);
    auto throughputStats = getMetricStats(METRIC_STT_THROUGHPUT, 5);
    auto concurrentStats = getMetricStats(METRIC_STT_CONCURRENT_TRANSCRIPTIONS, 1);
    auto bufferStats = getMetricStats(METRIC_STT_BUFFER_USAGE, 1);
    auto vadAccuracyStats = getMetricStats(METRIC_VAD_ACCURACY, 5);
    
    // Latency metrics
    summary["stt_latency_mean_ms"] = sttLatencyStats.mean;
    summary["stt_latency_p95_ms"] = sttLatencyStats.p95;
    summary["stt_latency_p99_ms"] = sttLatencyStats.p99;
    summary["vad_latency_mean_ms"] = vadLatencyStats.mean;
    summary["inference_latency_mean_ms"] = inferenceLatencyStats.mean;
    
    // Quality metrics
    summary["confidence_mean"] = confidenceStats.mean;
    summary["confidence_min"] = confidenceStats.min;
    summary["vad_accuracy_mean"] = vadAccuracyStats.mean;
    
    // Throughput and capacity
    summary["throughput_ops_per_sec"] = throughputStats.mean;
    summary["concurrent_transcriptions"] = concurrentStats.mean;
    summary["buffer_usage_mb"] = bufferStats.mean;
    
    // Counts
    summary["total_transcriptions"] = static_cast<double>(sttLatencyStats.count);
    summary["total_vad_operations"] = static_cast<double>(vadLatencyStats.count);
    
    return summary;
}

std::string PerformanceMonitor::exportMetricsJSON(int windowMinutes) const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"timestamp\": \"" << std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() << "\",\n";
    json << "  \"window_minutes\": " << windowMinutes << ",\n";
    json << "  \"metrics\": {\n";
    
    auto metricNames = getAvailableMetrics();
    for (size_t i = 0; i < metricNames.size(); ++i) {
        const auto& name = metricNames[i];
        auto stats = getMetricStats(name, windowMinutes);
        
        json << "    \"" << name << "\": {\n";
        json << "      \"count\": " << stats.count << ",\n";
        json << "      \"min\": " << stats.min << ",\n";
        json << "      \"max\": " << stats.max << ",\n";
        json << "      \"mean\": " << stats.mean << ",\n";
        json << "      \"median\": " << stats.median << ",\n";
        json << "      \"p95\": " << stats.p95 << ",\n";
        json << "      \"p99\": " << stats.p99 << ",\n";
        json << "      \"unit\": \"" << stats.unit << "\"\n";
        json << "    }";
        
        if (i < metricNames.size() - 1) {
            json << ",";
        }
        json << "\n";
    }
    
    json << "  }\n";
    json << "}\n";
    
    return json.str();
}

void PerformanceMonitor::clearMetrics() {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    metrics_.clear();
    totalMetricsRecorded_ = 0;
    totalLatencyMeasurements_ = 0;
    totalThroughputMeasurements_ = 0;
    
    Logger::info("All performance metrics cleared");
}

void PerformanceMonitor::setEnabled(bool enabled) {
    enabled_ = enabled;
    Logger::info("Performance monitoring " + std::string(enabled ? "enabled" : "disabled"));
}

bool PerformanceMonitor::isEnabled() const {
    return enabled_.load();
}

void PerformanceMonitor::setMaxDataPoints(size_t maxPoints) {
    maxDataPoints_ = maxPoints;
    
    // Prune existing metrics if needed
    std::lock_guard<std::mutex> lock(metricsMutex_);
    for (auto& pair : metrics_) {
        auto& points = pair.second;
        if (points.size() > maxPoints) {
            points.erase(points.begin(), points.begin() + (points.size() - maxPoints));
        }
    }
}

void PerformanceMonitor::startSystemMetricsCollection() {
    if (systemMetricsRunning_.load()) {
        return;
    }
    
    systemMetricsRunning_ = true;
    systemMetricsThread_ = std::make_unique<std::thread>([this]() {
        Logger::info("System metrics collection started");
        
        while (systemMetricsRunning_.load()) {
            try {
                collectSystemMetrics();
                collectGPUMetrics();
                
                std::this_thread::sleep_for(std::chrono::milliseconds(collectionIntervalMs_));
            } catch (const std::exception& e) {
                Logger::warn("Error collecting system metrics: " + std::string(e.what()));
            }
        }
        
        Logger::info("System metrics collection stopped");
    });
}

void PerformanceMonitor::stopSystemMetricsCollection() {
    if (!systemMetricsRunning_.load()) {
        return;
    }
    
    systemMetricsRunning_ = false;
    
    if (systemMetricsThread_ && systemMetricsThread_->joinable()) {
        systemMetricsThread_->join();
    }
    
    systemMetricsThread_.reset();
}

void PerformanceMonitor::cleanup() {
    if (!initialized_.load()) {
        return;
    }
    
    stopSystemMetricsCollection();
    clearMetrics();
    
    initialized_ = false;
    Logger::info("Performance monitor cleaned up");
}

void PerformanceMonitor::collectSystemMetrics() {
    collectMemoryMetrics();
    collectCPUMetrics();
}

void PerformanceMonitor::collectGPUMetrics() {
    auto& gpuManager = GPUManager::getInstance();
    
    if (gpuManager.isCudaAvailable()) {
        recordMetric(METRIC_GPU_MEMORY_USAGE, static_cast<double>(gpuManager.getCurrentMemoryUsageMB()), "MB");
        
        float utilization = gpuManager.getGPUUtilization();
        if (utilization >= 0) {
            recordMetric(METRIC_GPU_UTILIZATION, static_cast<double>(utilization), "%");
        }
    }
}

void PerformanceMonitor::collectMemoryMetrics() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        double usedMemoryMB = static_cast<double>(memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024 * 1024);
        recordMetric(METRIC_MEMORY_USAGE, usedMemoryMB, "MB");
    }
#elif __linux__
    struct sysinfo memInfo;
    if (sysinfo(&memInfo) == 0) {
        double usedMemoryMB = static_cast<double>(memInfo.totalram - memInfo.freeram) * memInfo.mem_unit / (1024 * 1024);
        recordMetric(METRIC_MEMORY_USAGE, usedMemoryMB, "MB");
    }
#elif __APPLE__
    vm_size_t page_size;
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t host_size = sizeof(vm_statistics64_data_t) / sizeof(natural_t);
    
    if (host_page_size(mach_host_self(), &page_size) == KERN_SUCCESS &&
        host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm_stat, &host_size) == KERN_SUCCESS) {
        
        double usedMemoryMB = static_cast<double>((vm_stat.active_count + vm_stat.inactive_count + 
                                                 vm_stat.wire_count) * page_size) / (1024 * 1024);
        recordMetric(METRIC_MEMORY_USAGE, usedMemoryMB, "MB");
    }
#endif
}

void PerformanceMonitor::collectCPUMetrics() {
    static auto lastCPUTime = std::chrono::steady_clock::now();
    static double lastCPUUsage = 0.0;
    
    auto currentTime = std::chrono::steady_clock::now();
    auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastCPUTime);
    
    // Only collect CPU metrics every few seconds to avoid overhead
    if (timeDiff.count() < 2000) {
        recordMetric(METRIC_CPU_USAGE, lastCPUUsage, "%");
        return;
    }
    
    double cpuUsage = 0.0;
    
#ifdef _WIN32
    static FILETIME lastIdleTime, lastKernelTime, lastUserTime;
    static bool firstCall = true;
    
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        if (!firstCall) {
            auto fileTimeToUInt64 = [](const FILETIME& ft) -> uint64_t {
                return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
            };
            
            uint64_t idle = fileTimeToUInt64(idleTime) - fileTimeToUInt64(lastIdleTime);
            uint64_t kernel = fileTimeToUInt64(kernelTime) - fileTimeToUInt64(lastKernelTime);
            uint64_t user = fileTimeToUInt64(userTime) - fileTimeToUInt64(lastUserTime);
            
            uint64_t total = kernel + user;
            if (total > 0) {
                cpuUsage = (static_cast<double>(total - idle) / static_cast<double>(total)) * 100.0;
            }
        }
        
        lastIdleTime = idleTime;
        lastKernelTime = kernelTime;
        lastUserTime = userTime;
        firstCall = false;
    }
    
#elif __linux__
    static unsigned long long lastTotalTime = 0;
    static unsigned long long lastIdleTime = 0;
    static bool firstCall = true;
    
    std::ifstream procStat("/proc/stat");
    if (procStat.is_open()) {
        std::string line;
        if (std::getline(procStat, line)) {
            std::istringstream iss(line);
            std::string cpu;
            unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
            
            if (iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal) {
                unsigned long long totalTime = user + nice + system + idle + iowait + irq + softirq + steal;
                
                if (!firstCall && totalTime > lastTotalTime) {
                    unsigned long long totalDiff = totalTime - lastTotalTime;
                    unsigned long long idleDiff = idle - lastIdleTime;
                    
                    if (totalDiff > 0) {
                        cpuUsage = (static_cast<double>(totalDiff - idleDiff) / static_cast<double>(totalDiff)) * 100.0;
                    }
                }
                
                lastTotalTime = totalTime;
                lastIdleTime = idle;
                firstCall = false;
            }
        }
    }
    
#elif __APPLE__
    static host_cpu_load_info_data_t lastCpuInfo;
    static bool firstCall = true;
    
    host_cpu_load_info_data_t cpuInfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, 
                       reinterpret_cast<host_info_t>(&cpuInfo), &count) == KERN_SUCCESS) {
        
        if (!firstCall) {
            unsigned int totalTicks = 0;
            unsigned int idleTicks = 0;
            
            for (int i = 0; i < CPU_STATE_MAX; i++) {
                unsigned int ticksDiff = cpuInfo.cpu_ticks[i] - lastCpuInfo.cpu_ticks[i];
                totalTicks += ticksDiff;
                
                if (i == CPU_STATE_IDLE) {
                    idleTicks = ticksDiff;
                }
            }
            
            if (totalTicks > 0) {
                cpuUsage = (static_cast<double>(totalTicks - idleTicks) / static_cast<double>(totalTicks)) * 100.0;
            }
        }
        
        lastCpuInfo = cpuInfo;
        firstCall = false;
    }
#endif
    
    // Clamp CPU usage to reasonable bounds
    cpuUsage = std::max(0.0, std::min(100.0, cpuUsage));
    
    recordMetric(METRIC_CPU_USAGE, cpuUsage, "%");
    lastCPUUsage = cpuUsage;
    lastCPUTime = currentTime;
}

MetricStats PerformanceMonitor::calculateStats(const std::vector<MetricDataPoint>& points) const {
    MetricStats stats;
    
    if (points.empty()) {
        return stats;
    }
    
    stats.count = points.size();
    if (!points.empty()) {
        stats.unit = points[0].unit;
    }
    
    // Extract values and sort for percentile calculations
    std::vector<double> values;
    values.reserve(points.size());
    
    for (const auto& point : points) {
        values.push_back(point.value);
    }
    
    std::sort(values.begin(), values.end());
    
    // Calculate basic statistics
    stats.min = values.front();
    stats.max = values.back();
    stats.mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    
    // Calculate median
    size_t n = values.size();
    if (n % 2 == 0) {
        stats.median = (values[n/2 - 1] + values[n/2]) / 2.0;
    } else {
        stats.median = values[n/2];
    }
    
    // Calculate percentiles
    auto p95_idx = static_cast<size_t>(std::ceil(0.95 * n)) - 1;
    auto p99_idx = static_cast<size_t>(std::ceil(0.99 * n)) - 1;
    
    stats.p95 = values[std::min(p95_idx, n - 1)];
    stats.p99 = values[std::min(p99_idx, n - 1)];
    
    return stats;
}

std::vector<MetricDataPoint> PerformanceMonitor::filterByTimeWindow(
    const std::vector<MetricDataPoint>& points, int windowMinutes) const {
    
    if (windowMinutes <= 0) {
        return points; // Return all points
    }
    
    auto cutoffTime = std::chrono::steady_clock::now() - std::chrono::minutes(windowMinutes);
    
    std::vector<MetricDataPoint> filtered;
    for (const auto& point : points) {
        if (point.timestamp >= cutoffTime) {
            filtered.push_back(point);
        }
    }
    
    return filtered;
}

} // namespace utils
} // namespace speechrnt