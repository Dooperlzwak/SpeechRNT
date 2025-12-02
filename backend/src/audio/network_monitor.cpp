#include "audio/network_monitor.hpp"
#include "utils/logging.hpp"
#include "utils/performance_monitor.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif

namespace speechrnt {
namespace audio {

NetworkMonitor::NetworkMonitor()
    : monitoringIntervalMs_(1000)
    , historySize_(60)
    , monitoring_(false)
    , currentQuality_(NetworkQuality::GOOD)
    , totalMeasurements_(0)
    , qualityChanges_(0) {
}

NetworkMonitor::~NetworkMonitor() {
    stopMonitoring();
}

bool NetworkMonitor::initialize(int monitoringIntervalMs, size_t historySize) {
    monitoringIntervalMs_ = monitoringIntervalMs;
    historySize_ = historySize;
    
    // Reserve space for metrics history
    std::lock_guard<std::mutex> lock(metricsMutex_);
    metricsHistory_.reserve(historySize_);
    
    speechrnt::utils::Logger::info("NetworkMonitor initialized: " + 
                       std::to_string(monitoringIntervalMs) + "ms interval, " +
                       std::to_string(historySize) + " history size");
    
    return true;
}

bool NetworkMonitor::startMonitoring() {
    if (monitoring_.load()) {
        speechrnt::utils::Logger::warn("Network monitoring already started");
        return true;
    }
    
    monitoring_ = true;
    monitoringThread_ = std::make_unique<std::thread>(&NetworkMonitor::monitoringLoop, this);
    
    speechrnt::utils::Logger::info("Network monitoring started");
    return true;
}

void NetworkMonitor::stopMonitoring() {
    if (!monitoring_.load()) {
        return;
    }
    
    monitoring_ = false;
    
    if (monitoringThread_ && monitoringThread_->joinable()) {
        monitoringThread_->join();
    }
    
    speechrnt::utils::Logger::info("Network monitoring stopped");
}

NetworkMetrics NetworkMonitor::getCurrentMetrics() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return currentMetrics_;
}

NetworkQuality NetworkMonitor::getNetworkQuality() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return currentQuality_;
}

NetworkMetrics NetworkMonitor::getAverageMetrics(int durationMs) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    if (metricsHistory_.empty()) {
        return NetworkMetrics();
    }
    
    auto cutoffTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(durationMs);
    
    // Find metrics within the specified duration
    std::vector<NetworkMetrics> recentMetrics;
    for (const auto& metrics : metricsHistory_) {
        if (metrics.timestamp >= cutoffTime) {
            recentMetrics.push_back(metrics);
        }
    }
    
    if (recentMetrics.empty()) {
        return currentMetrics_;
    }
    
    // Calculate averages
    NetworkMetrics avgMetrics;
    float totalLatency = 0.0f;
    float totalJitter = 0.0f;
    float totalLoss = 0.0f;
    float totalBandwidth = 0.0f;
    float totalThroughput = 0.0f;
    
    for (const auto& metrics : recentMetrics) {
        totalLatency += metrics.latencyMs;
        totalJitter += metrics.jitterMs;
        totalLoss += metrics.packetLossRate;
        totalBandwidth += metrics.bandwidthKbps;
        totalThroughput += metrics.throughputKbps;
    }
    
    size_t count = recentMetrics.size();
    avgMetrics.latencyMs = totalLatency / count;
    avgMetrics.jitterMs = totalJitter / count;
    avgMetrics.packetLossRate = totalLoss / count;
    avgMetrics.bandwidthKbps = totalBandwidth / count;
    avgMetrics.throughputKbps = totalThroughput / count;
    avgMetrics.timestamp = std::chrono::steady_clock::now();
    
    return avgMetrics;
}

void NetworkMonitor::registerConditionCallback(std::function<void(const NetworkMetrics&, NetworkQuality)> callback) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    conditionCallbacks_.push_back(callback);
}

void NetworkMonitor::updateMetrics(const NetworkMetrics& metrics) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    currentMetrics_ = metrics;
    metricsHistory_.push_back(metrics);
    
    // Prune old metrics
    pruneOldMetrics();
    
    // Update quality classification
    NetworkQuality newQuality = classifyNetworkQuality(metrics);
    if (newQuality != currentQuality_) {
        currentQuality_ = newQuality;
        qualityChanges_++;
        
        // Notify callbacks outside of lock
        auto callbacks = conditionCallbacks_;
        lock.~lock_guard();
        notifyConditionChange(metrics, newQuality);
    }
    
    totalMeasurements_++;
}

bool NetworkMonitor::isNetworkStable(float stabilityThreshold) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    if (metricsHistory_.size() < 5) {
        return false; // Need at least 5 measurements
    }
    
    // Calculate coefficient of variation for latency
    std::vector<float> latencies;
    for (const auto& metrics : metricsHistory_) {
        latencies.push_back(metrics.latencyMs);
    }
    
    float mean = std::accumulate(latencies.begin(), latencies.end(), 0.0f) / latencies.size();
    float variance = 0.0f;
    for (float latency : latencies) {
        variance += (latency - mean) * (latency - mean);
    }
    variance /= latencies.size();
    float stddev = std::sqrt(variance);
    
    float coefficientOfVariation = (mean > 0) ? (stddev / mean) : 1.0f;
    
    return coefficientOfVariation <= stabilityThreshold;
}

std::map<std::string, double> NetworkMonitor::getMonitoringStats() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    std::map<std::string, double> stats;
    stats["total_measurements"] = static_cast<double>(totalMeasurements_.load());
    stats["quality_changes"] = static_cast<double>(qualityChanges_.load());
    stats["current_latency_ms"] = static_cast<double>(currentMetrics_.latencyMs);
    stats["current_jitter_ms"] = static_cast<double>(currentMetrics_.jitterMs);
    stats["current_packet_loss_rate"] = static_cast<double>(currentMetrics_.packetLossRate);
    stats["current_bandwidth_kbps"] = static_cast<double>(currentMetrics_.bandwidthKbps);
    stats["current_quality"] = static_cast<double>(currentQuality_);
    stats["history_size"] = static_cast<double>(metricsHistory_.size());
    stats["is_stable"] = isNetworkStable() ? 1.0 : 0.0;
    
    return stats;
}

void NetworkMonitor::monitoringLoop() {
    speechrnt::utils::Logger::info("Network monitoring loop started");
    
    while (monitoring_.load()) {
        try {
            NetworkMetrics metrics = measureNetworkConditions();
            updateMetrics(metrics);
            
            // Record performance metrics
            speechrnt::utils::PerformanceMonitor::getInstance().recordLatency(
                "network.latency_ms", metrics.latencyMs);
            speechrnt::utils::PerformanceMonitor::getInstance().recordValue(
                "network.jitter_ms", metrics.jitterMs);
            speechrnt::utils::PerformanceMonitor::getInstance().recordValue(
                "network.packet_loss_rate", metrics.packetLossRate);
            
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Network monitoring error: " + std::string(e.what()));
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(monitoringIntervalMs_));
    }
    
    speechrnt::utils::Logger::info("Network monitoring loop stopped");
}

NetworkMetrics NetworkMonitor::measureNetworkConditions() {
    NetworkMetrics metrics;
    metrics.timestamp = std::chrono::steady_clock::now();
    
    // Simple ping-based latency measurement to localhost
    // In production, this should ping to actual server endpoints
    auto startTime = std::chrono::high_resolution_clock::now();
    
#ifdef _WIN32
    // Windows implementation using ICMP
    HANDLE hIcmpFile = IcmpCreateFile();
    if (hIcmpFile != INVALID_HANDLE_VALUE) {
        char sendData[32] = "NetworkMonitorPing";
        char replyBuffer[sizeof(ICMP_ECHO_REPLY) + 32];
        
        DWORD dwRetVal = IcmpSendEcho(hIcmpFile, 
                                     inet_addr("127.0.0.1"),
                                     sendData, sizeof(sendData),
                                     NULL, replyBuffer, sizeof(replyBuffer), 1000);
        
        if (dwRetVal != 0) {
            PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)replyBuffer;
            metrics.latencyMs = static_cast<float>(pEchoReply->RoundTripTime);
        } else {
            metrics.latencyMs = 1000.0f; // Timeout
        }
        
        IcmpCloseHandle(hIcmpFile);
    }
#else
    // Unix/Linux implementation using socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd >= 0) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(12345); // Dummy port
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        
        // Simple connect test for latency estimation
        int result = connect(sockfd, (struct sockaddr*)&addr, sizeof(addr));
        auto endTime = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            endTime - startTime).count();
        metrics.latencyMs = duration / 1000.0f; // Convert to milliseconds
        
        close(sockfd);
    }
#endif
    
    // Calculate jitter from recent latency measurements
    {
        std::lock_guard<std::mutex> lock(metricsMutex_);
        std::vector<float> recentLatencies;
        for (const auto& m : metricsHistory_) {
            recentLatencies.push_back(m.latencyMs);
        }
        recentLatencies.push_back(metrics.latencyMs);
        
        metrics.jitterMs = calculateJitter(recentLatencies);
    }
    
    // Simulate packet loss measurement (in production, use actual network statistics)
    metrics.packetLossRate = 0.0f; // Placeholder
    
    // Estimate bandwidth (simplified)
    metrics.bandwidthKbps = 1000.0f; // 1 Mbps default
    metrics.throughputKbps = metrics.bandwidthKbps * 0.8f; // 80% utilization
    
    return metrics;
}

NetworkQuality NetworkMonitor::classifyNetworkQuality(const NetworkMetrics& metrics) const {
    // Classification based on latency, jitter, and packet loss
    if (metrics.latencyMs < 50.0f && metrics.jitterMs < 5.0f && metrics.packetLossRate < 0.1f) {
        return NetworkQuality::EXCELLENT;
    } else if (metrics.latencyMs < 100.0f && metrics.jitterMs < 10.0f && metrics.packetLossRate < 0.5f) {
        return NetworkQuality::GOOD;
    } else if (metrics.latencyMs < 200.0f && metrics.jitterMs < 20.0f && metrics.packetLossRate < 2.0f) {
        return NetworkQuality::FAIR;
    } else if (metrics.latencyMs < 500.0f && metrics.jitterMs < 50.0f && metrics.packetLossRate < 5.0f) {
        return NetworkQuality::POOR;
    } else {
        return NetworkQuality::VERY_POOR;
    }
}

void NetworkMonitor::notifyConditionChange(const NetworkMetrics& metrics, NetworkQuality quality) {
    for (const auto& callback : conditionCallbacks_) {
        try {
            callback(metrics, quality);
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Network condition callback error: " + std::string(e.what()));
        }
    }
}

float NetworkMonitor::calculateJitter(const std::vector<float>& latencies) const {
    if (latencies.size() < 2) {
        return 0.0f;
    }
    
    // Calculate mean absolute deviation of consecutive differences
    float totalDeviation = 0.0f;
    size_t count = 0;
    
    for (size_t i = 1; i < latencies.size(); ++i) {
        totalDeviation += std::abs(latencies[i] - latencies[i-1]);
        count++;
    }
    
    return count > 0 ? (totalDeviation / count) : 0.0f;
}

void NetworkMonitor::pruneOldMetrics() {
    // Remove metrics older than history size
    while (metricsHistory_.size() > historySize_) {
        metricsHistory_.erase(metricsHistory_.begin());
    }
}

NetworkAwareStreamingAdapter::NetworkAwareStreamingAdapter()
    : adaptiveMode_(true)
    , totalAdaptations_(0)
    , qualityDegradations_(0)
    , qualityImprovements_(0) {
}

NetworkAwareStreamingAdapter::~NetworkAwareStreamingAdapter() {
}

bool NetworkAwareStreamingAdapter::initialize(std::shared_ptr<NetworkMonitor> networkMonitor) {
    networkMonitor_ = networkMonitor;
    
    if (networkMonitor_) {
        // Register for network condition changes
        networkMonitor_->registerConditionCallback(
            [this](const NetworkMetrics& metrics, NetworkQuality quality) {
                onNetworkConditionChange(metrics, quality);
            });
        
        speechrnt::utils::Logger::info("NetworkAwareStreamingAdapter initialized with network monitor");
        return true;
    }
    
    speechrnt::utils::Logger::error("NetworkAwareStreamingAdapter initialization failed: no network monitor");
    return false;
}

AdaptiveStreamingParams NetworkAwareStreamingAdapter::getAdaptiveParams() const {
    std::lock_guard<std::mutex> lock(paramsMutex_);
    return currentParams_;
}

void NetworkAwareStreamingAdapter::updateFromFeedback(float successRate, float averageLatency) {
    if (!adaptiveMode_ || !networkMonitor_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(paramsMutex_);
    
    // Adjust parameters based on feedback
    if (successRate < 0.9f) {
        // Poor success rate - increase buffer, reduce quality
        currentParams_.bufferSizeMs = std::min(currentParams_.bufferSizeMs * 1.2f, 500.0f);
        currentParams_.qualityFactor = std::max(currentParams_.qualityFactor * 0.9f, 0.3f);
        currentParams_.maxRetries = std::min(currentParams_.maxRetries + 1, 10);
        qualityDegradations_++;
    } else if (successRate > 0.98f && averageLatency < 100.0f) {
        // Good performance - can improve quality
        currentParams_.bufferSizeMs = std::max(currentParams_.bufferSizeMs * 0.9f, 50.0f);
        currentParams_.qualityFactor = std::min(currentParams_.qualityFactor * 1.05f, 1.0f);
        qualityImprovements_++;
    }
    
    totalAdaptations_++;
    recordAdaptation(currentParams_);
}

bool NetworkAwareStreamingAdapter::shouldDegradeQuality() const {
    if (!networkMonitor_) {
        return false;
    }
    
    NetworkQuality quality = networkMonitor_->getNetworkQuality();
    return quality == NetworkQuality::POOR || quality == NetworkQuality::VERY_POOR;
}

bool NetworkAwareStreamingAdapter::canImproveQuality() const {
    if (!networkMonitor_) {
        return false;
    }
    
    NetworkQuality quality = networkMonitor_->getNetworkQuality();
    NetworkMetrics metrics = networkMonitor_->getCurrentMetrics();
    
    return quality == NetworkQuality::EXCELLENT || 
           (quality == NetworkQuality::GOOD && networkMonitor_->isNetworkStable());
}

size_t NetworkAwareStreamingAdapter::getRecommendedBufferSize(size_t baseBufferMs) const {
    if (!networkMonitor_) {
        return baseBufferMs;
    }
    
    NetworkMetrics metrics = networkMonitor_->getCurrentMetrics();
    NetworkQuality quality = networkMonitor_->getNetworkQuality();
    
    float multiplier = 1.0f;
    switch (quality) {
        case NetworkQuality::EXCELLENT:
            multiplier = 0.8f;
            break;
        case NetworkQuality::GOOD:
            multiplier = 1.0f;
            break;
        case NetworkQuality::FAIR:
            multiplier = 1.5f;
            break;
        case NetworkQuality::POOR:
            multiplier = 2.0f;
            break;
        case NetworkQuality::VERY_POOR:
            multiplier = 3.0f;
            break;
    }
    
    // Additional adjustment based on jitter
    if (metrics.jitterMs > 20.0f) {
        multiplier *= 1.5f;
    }
    
    return static_cast<size_t>(baseBufferMs * multiplier);
}

size_t NetworkAwareStreamingAdapter::getRecommendedChunkSize(size_t baseChunkMs) const {
    if (!networkMonitor_) {
        return baseChunkMs;
    }
    
    NetworkQuality quality = networkMonitor_->getNetworkQuality();
    
    float multiplier = 1.0f;
    switch (quality) {
        case NetworkQuality::EXCELLENT:
            multiplier = 0.8f; // Smaller chunks for lower latency
            break;
        case NetworkQuality::GOOD:
            multiplier = 1.0f;
            break;
        case NetworkQuality::FAIR:
            multiplier = 1.2f; // Larger chunks for efficiency
            break;
        case NetworkQuality::POOR:
            multiplier = 1.5f;
            break;
        case NetworkQuality::VERY_POOR:
            multiplier = 2.0f; // Much larger chunks to reduce overhead
            break;
    }
    
    return static_cast<size_t>(baseChunkMs * multiplier);
}

void NetworkAwareStreamingAdapter::setAdaptiveMode(bool enabled) {
    adaptiveMode_ = enabled;
    speechrnt::utils::Logger::info("Network-aware streaming adaptation " + 
                       std::string(enabled ? "enabled" : "disabled"));
}

std::map<std::string, double> NetworkAwareStreamingAdapter::getAdaptationStats() const {
    std::lock_guard<std::mutex> lock(paramsMutex_);
    
    std::map<std::string, double> stats;
    stats["total_adaptations"] = static_cast<double>(totalAdaptations_.load());
    stats["quality_degradations"] = static_cast<double>(qualityDegradations_.load());
    stats["quality_improvements"] = static_cast<double>(qualityImprovements_.load());
    stats["current_buffer_size_ms"] = static_cast<double>(currentParams_.bufferSizeMs);
    stats["current_chunk_size_ms"] = static_cast<double>(currentParams_.chunkSizeMs);
    stats["current_quality_factor"] = static_cast<double>(currentParams_.qualityFactor);
    stats["current_max_retries"] = static_cast<double>(currentParams_.maxRetries);
    stats["adaptive_mode_enabled"] = adaptiveMode_ ? 1.0 : 0.0;
    stats["adaptation_history_size"] = static_cast<double>(adaptationHistory_.size());
    
    return stats;
}

void NetworkAwareStreamingAdapter::onNetworkConditionChange(const NetworkMetrics& metrics, NetworkQuality quality) {
    if (!adaptiveMode_) {
        return;
    }
    
    AdaptiveStreamingParams newParams = calculateOptimalParams(metrics, quality);
    
    std::lock_guard<std::mutex> lock(paramsMutex_);
    if (isAdaptationNeeded(newParams)) {
        currentParams_ = newParams;
        totalAdaptations_++;
        recordAdaptation(newParams);
        
        speechrnt::utils::Logger::info("Adapted streaming parameters for network quality change");
    }
}

AdaptiveStreamingParams NetworkAwareStreamingAdapter::calculateOptimalParams(
    const NetworkMetrics& metrics, NetworkQuality quality) const {
    
    AdaptiveStreamingParams params;
    
    // Base parameters on network quality
    switch (quality) {
        case NetworkQuality::EXCELLENT:
            params.bufferSizeMs = 50;
            params.chunkSizeMs = 25;
            params.maxRetries = 2;
            params.qualityFactor = 1.0f;
            params.enableCompression = false; // Low latency priority
            params.timeoutMs = 2000;
            break;
            
        case NetworkQuality::GOOD:
            params.bufferSizeMs = 100;
            params.chunkSizeMs = 50;
            params.maxRetries = 3;
            params.qualityFactor = 0.9f;
            params.enableCompression = true;
            params.timeoutMs = 3000;
            break;
            
        case NetworkQuality::FAIR:
            params.bufferSizeMs = 200;
            params.chunkSizeMs = 75;
            params.maxRetries = 4;
            params.qualityFactor = 0.7f;
            params.enableCompression = true;
            params.timeoutMs = 5000;
            break;
            
        case NetworkQuality::POOR:
            params.bufferSizeMs = 300;
            params.chunkSizeMs = 100;
            params.maxRetries = 5;
            params.qualityFactor = 0.5f;
            params.enableCompression = true;
            params.timeoutMs = 8000;
            break;
            
        case NetworkQuality::VERY_POOR:
            params.bufferSizeMs = 500;
            params.chunkSizeMs = 150;
            params.maxRetries = 8;
            params.qualityFactor = 0.3f;
            params.enableCompression = true;
            params.timeoutMs = 15000;
            break;
    }
    
    // Fine-tune based on specific metrics
    if (metrics.jitterMs > 50.0f) {
        params.bufferSizeMs *= 1.5f;
    }
    
    if (metrics.packetLossRate > 2.0f) {
        params.maxRetries = std::min(params.maxRetries + 2, 10);
        params.chunkSizeMs *= 1.3f; // Larger chunks to reduce packet overhead
    }
    
    return params;
}

void NetworkAwareStreamingAdapter::recordAdaptation(const AdaptiveStreamingParams& params) {
    adaptationHistory_.push_back({std::chrono::steady_clock::now(), params});
    
    // Keep only recent history
    const size_t maxHistorySize = 100;
    if (adaptationHistory_.size() > maxHistorySize) {
        adaptationHistory_.erase(adaptationHistory_.begin());
    }
}

bool NetworkAwareStreamingAdapter::isAdaptationNeeded(const AdaptiveStreamingParams& newParams) const {
    // Check if parameters have changed significantly
    const float threshold = 0.1f; // 10% change threshold
    
    return (std::abs(static_cast<float>(newParams.bufferSizeMs - currentParams_.bufferSizeMs)) / 
            currentParams_.bufferSizeMs > threshold) ||
           (std::abs(static_cast<float>(newParams.chunkSizeMs - currentParams_.chunkSizeMs)) / 
            currentParams_.chunkSizeMs > threshold) ||
           (std::abs(newParams.qualityFactor - currentParams_.qualityFactor) > threshold) ||
           (newParams.maxRetries != currentParams_.maxRetries) ||
           (newParams.enableCompression != currentParams_.enableCompression);
}

} // namespace audio
} // namespace speechrnt