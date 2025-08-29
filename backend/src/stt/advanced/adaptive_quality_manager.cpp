#include "stt/advanced/adaptive_quality_manager.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#elif __linux__
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <fstream>
#include <unistd.h>
#elif __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#endif

namespace stt {
namespace advanced {

// ResourceMonitorImpl implementation
ResourceMonitorImpl::ResourceMonitorImpl()
    : initialized_(false)
    , monitoring_(false)
    , cpuThreshold_(0.8f)
    , memoryThreshold_(0.8f)
    , gpuThreshold_(0.8f)
    , monitoringIntervalMs_(1000)
    , lastUpdate_(std::chrono::steady_clock::now()) {
}

ResourceMonitorImpl::~ResourceMonitorImpl() {
    stopMonitoring();
}

bool ResourceMonitorImpl::initialize() {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    
    try {
        // Initialize platform-specific monitoring
        currentResources_ = collectSystemResources();
        initialized_ = true;
        
        utils::Logger::info("ResourceMonitor initialized successfully");
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to initialize ResourceMonitor: " + std::string(e.what()));
        return false;
    }
}

SystemResources ResourceMonitorImpl::getCurrentResources() {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    
    if (!initialized_) {
        return SystemResources{};
    }
    
    // Update if data is stale
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate_).count();
    
    if (elapsed > monitoringIntervalMs_ / 2) {
        currentResources_ = collectSystemResources();
        lastUpdate_ = now;
    }
    
    return currentResources_;
}

bool ResourceMonitorImpl::startMonitoring(int intervalMs) {
    if (!initialized_) {
        utils::Logger::error("ResourceMonitor not initialized");
        return false;
    }
    
    if (monitoring_) {
        utils::Logger::warning("ResourceMonitor already monitoring");
        return true;
    }
    
    monitoringIntervalMs_ = intervalMs;
    monitoring_ = true;
    
    monitoringThread_ = std::thread(&ResourceMonitorImpl::monitoringLoop, this);
    
    utils::Logger::info("ResourceMonitor started with interval: " + std::to_string(intervalMs) + "ms");
    return true;
}

void ResourceMonitorImpl::stopMonitoring() {
    if (!monitoring_) {
        return;
    }
    
    monitoring_ = false;
    monitoringCondition_.notify_all();
    
    if (monitoringThread_.joinable()) {
        monitoringThread_.join();
    }
    
    utils::Logger::info("ResourceMonitor stopped");
}

void ResourceMonitorImpl::setResourceThresholds(float cpuThreshold, float memoryThreshold, float gpuThreshold) {
    cpuThreshold_ = std::clamp(cpuThreshold, 0.0f, 1.0f);
    memoryThreshold_ = std::clamp(memoryThreshold, 0.0f, 1.0f);
    gpuThreshold_ = std::clamp(gpuThreshold, 0.0f, 1.0f);
    
    utils::Logger::info("Resource thresholds updated - CPU: " + std::to_string(cpuThreshold_) +
                       ", Memory: " + std::to_string(memoryThreshold_) +
                       ", GPU: " + std::to_string(gpuThreshold_));
}

bool ResourceMonitorImpl::areResourcesConstrained() const {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    
    return currentResources_.cpuUsage > cpuThreshold_ ||
           currentResources_.memoryUsage > memoryThreshold_ ||
           currentResources_.gpuUsage > gpuThreshold_;
}

std::vector<SystemResources> ResourceMonitorImpl::getResourceHistory(size_t samples) const {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    
    std::vector<SystemResources> history;
    size_t count = std::min(samples, resourceHistory_.size());
    
    auto it = resourceHistory_.end() - count;
    history.assign(it, resourceHistory_.end());
    
    return history;
}

bool ResourceMonitorImpl::isInitialized() const {
    return initialized_;
}

void ResourceMonitorImpl::monitoringLoop() {
    std::unique_lock<std::mutex> lock(resourceMutex_);
    
    while (monitoring_) {
        // Collect resources
        SystemResources resources = collectSystemResources();
        currentResources_ = resources;
        lastUpdate_ = std::chrono::steady_clock::now();
        
        // Add to history
        resourceHistory_.push_back(resources);
        if (resourceHistory_.size() > MAX_HISTORY_SIZE) {
            resourceHistory_.pop_front();
        }
        
        // Wait for next interval
        monitoringCondition_.wait_for(lock, std::chrono::milliseconds(monitoringIntervalMs_),
                                     [this] { return !monitoring_; });
    }
}

SystemResources ResourceMonitorImpl::collectSystemResources() {
    SystemResources resources;
    
    resources.cpuUsage = getCpuUsage();
    resources.memoryUsage = getMemoryUsage();
    resources.gpuUsage = getGpuUsage();
    resources.diskUsage = getDiskUsage();
    resources.networkLatency = getNetworkLatency();
    
    getMemoryInfo(resources.availableMemoryMB, resources.totalMemoryMB);
    
    // Determine if resources are constrained
    resources.resourceConstrained = resources.cpuUsage > cpuThreshold_ ||
                                   resources.memoryUsage > memoryThreshold_ ||
                                   resources.gpuUsage > gpuThreshold_;
    
    return resources;
}

float ResourceMonitorImpl::getCpuUsage() {
#ifdef _WIN32
    static ULARGE_INTEGER lastCPU, lastSysCPU, lastUserCPU;
    static int numProcessors = -1;
    static HANDLE self = GetCurrentProcess();
    
    if (numProcessors == -1) {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        numProcessors = sysInfo.dwNumberOfProcessors;
        
        FILETIME ftime, fsys, fuser;
        GetSystemTimeAsFileTime(&ftime);
        memcpy(&lastCPU, &ftime, sizeof(FILETIME));
        
        GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
        memcpy(&lastSysCPU, &fsys, sizeof(FILETIME));
        memcpy(&lastUserCPU, &fuser, sizeof(FILETIME));
        return 0.0f;
    }
    
    FILETIME ftime, fsys, fuser;
    ULARGE_INTEGER now, sys, user;
    
    GetSystemTimeAsFileTime(&ftime);
    memcpy(&now, &ftime, sizeof(FILETIME));
    
    GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
    memcpy(&sys, &fsys, sizeof(FILETIME));
    memcpy(&user, &fuser, sizeof(FILETIME));
    
    double percent = (sys.QuadPart - lastSysCPU.QuadPart) + (user.QuadPart - lastUserCPU.QuadPart);
    percent /= (now.QuadPart - lastCPU.QuadPart);
    percent /= numProcessors;
    
    lastCPU = now;
    lastUserCPU = user;
    lastSysCPU = sys;
    
    return static_cast<float>(std::clamp(percent, 0.0, 1.0));
    
#elif __linux__
    static unsigned long long lastTotalUser = 0, lastTotalUserLow = 0, lastTotalSys = 0, lastTotalIdle = 0;
    
    std::ifstream file("/proc/stat");
    if (!file.is_open()) {
        return 0.0f;
    }
    
    std::string line;
    std::getline(file, line);
    file.close();
    
    std::istringstream iss(line);
    std::string cpu;
    unsigned long long totalUser, totalUserLow, totalSys, totalIdle;
    
    iss >> cpu >> totalUser >> totalUserLow >> totalSys >> totalIdle;
    
    if (lastTotalUser == 0) {
        lastTotalUser = totalUser;
        lastTotalUserLow = totalUserLow;
        lastTotalSys = totalSys;
        lastTotalIdle = totalIdle;
        return 0.0f;
    }
    
    unsigned long long total = (totalUser - lastTotalUser) + (totalUserLow - lastTotalUserLow) + (totalSys - lastTotalSys);
    unsigned long long totalTime = total + (totalIdle - lastTotalIdle);
    
    float percent = totalTime > 0 ? static_cast<float>(total) / totalTime : 0.0f;
    
    lastTotalUser = totalUser;
    lastTotalUserLow = totalUserLow;
    lastTotalSys = totalSys;
    lastTotalIdle = totalIdle;
    
    return std::clamp(percent, 0.0f, 1.0f);
    
#else
    // Simplified implementation for other platforms
    return 0.5f;
#endif
}

float ResourceMonitorImpl::getMemoryUsage() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    return static_cast<float>(memInfo.dwMemoryLoad) / 100.0f;
    
#elif __linux__
    struct sysinfo memInfo;
    sysinfo(&memInfo);
    
    unsigned long totalMem = memInfo.totalram * memInfo.mem_unit;
    unsigned long freeMem = memInfo.freeram * memInfo.mem_unit;
    unsigned long usedMem = totalMem - freeMem;
    
    return static_cast<float>(usedMem) / totalMem;
    
#else
    // Simplified implementation for other platforms
    return 0.5f;
#endif
}

float ResourceMonitorImpl::getGpuUsage() {
    // Simplified GPU usage - would need NVIDIA ML API or similar for real implementation
    return 0.3f;
}

float ResourceMonitorImpl::getDiskUsage() {
#ifdef _WIN32
    ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes;
    if (GetDiskFreeSpaceEx(L"C:\\", &freeBytesAvailable, &totalNumberOfBytes, nullptr)) {
        unsigned long long usedBytes = totalNumberOfBytes.QuadPart - freeBytesAvailable.QuadPart;
        return static_cast<float>(usedBytes) / totalNumberOfBytes.QuadPart;
    }
    return 0.0f;
    
#elif __linux__
    struct statvfs stat;
    if (statvfs("/", &stat) == 0) {
        unsigned long long total = stat.f_blocks * stat.f_frsize;
        unsigned long long free = stat.f_bavail * stat.f_frsize;
        unsigned long long used = total - free;
        return static_cast<float>(used) / total;
    }
    return 0.0f;
    
#else
    return 0.5f;
#endif
}

float ResourceMonitorImpl::getNetworkLatency() {
    // Simplified network latency - would need actual network monitoring
    return 50.0f; // 50ms default
}

void ResourceMonitorImpl::getMemoryInfo(size_t& available, size_t& total) {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    
    total = static_cast<size_t>(memInfo.ullTotalPhys / (1024 * 1024));
    available = static_cast<size_t>(memInfo.ullAvailPhys / (1024 * 1024));
    
#elif __linux__
    struct sysinfo memInfo;
    sysinfo(&memInfo);
    
    total = static_cast<size_t>((memInfo.totalram * memInfo.mem_unit) / (1024 * 1024));
    available = static_cast<size_t>((memInfo.freeram * memInfo.mem_unit) / (1024 * 1024));
    
#else
    total = 8192; // 8GB default
    available = 4096; // 4GB default
#endif
}

// PerformancePredictorImpl implementation
PerformancePredictorImpl::PerformancePredictorImpl()
    : initialized_(false)
    , lastModelUpdate_(std::chrono::steady_clock::now()) {
}

PerformancePredictorImpl::~PerformancePredictorImpl() = default;

bool PerformancePredictorImpl::initialize() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    
    // Initialize prediction model with default values
    predictionModel_ = PredictionModel{};
    initialized_ = true;
    
    utils::Logger::info("PerformancePredictor initialized successfully");
    return true;
}

PerformancePrediction PerformancePredictorImpl::predictPerformance(const QualitySettings& settings,
                                                                  const SystemResources& resources,
                                                                  size_t audioLength) {
    if (!initialized_) {
        return PerformancePrediction{};
    }
    
    PerformancePrediction prediction;
    
    prediction.predictedLatencyMs = predictLatencyForSettings(settings, resources, audioLength);
    prediction.predictedAccuracy = predictAccuracyForSettings(settings, resources);
    prediction.confidenceInPrediction = 0.8f; // Simplified confidence
    prediction.recommendedQuality = settings.level; // Start with current level
    prediction.reasoning = "Based on current system resources and quality settings";
    
    // Adjust recommended quality based on resources
    if (resources.resourceConstrained) {
        if (prediction.recommendedQuality > QualityLevel::LOW) {
            prediction.recommendedQuality = static_cast<QualityLevel>(
                static_cast<int>(prediction.recommendedQuality) - 1);
            prediction.reasoning += "; reduced quality due to resource constraints";
        }
    } else if (resources.cpuUsage < 0.5f && resources.memoryUsage < 0.5f) {
        if (prediction.recommendedQuality < QualityLevel::ULTRA_HIGH) {
            prediction.recommendedQuality = static_cast<QualityLevel>(
                static_cast<int>(prediction.recommendedQuality) + 1);
            prediction.reasoning += "; increased quality due to available resources";
        }
    }
    
    return prediction;
}

void PerformancePredictorImpl::updateWithActualPerformance(const QualitySettings& settings,
                                                          const SystemResources& resources,
                                                          size_t audioLength,
                                                          float actualLatency,
                                                          float actualAccuracy) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    
    if (!initialized_) {
        return;
    }
    
    // Add performance data point
    PerformanceDataPoint dataPoint;
    dataPoint.settings = settings;
    dataPoint.resources = resources;
    dataPoint.audioLength = audioLength;
    dataPoint.latency = actualLatency;
    dataPoint.accuracy = actualAccuracy;
    dataPoint.timestamp = std::chrono::steady_clock::now();
    
    performanceHistory_.push_back(dataPoint);
    if (performanceHistory_.size() > MAX_PERFORMANCE_HISTORY) {
        performanceHistory_.pop_front();
    }
    
    // Update prediction models periodically
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastModelUpdate_).count();
    
    if (elapsed >= 5 && performanceHistory_.size() >= 10) { // Update every 5 minutes with at least 10 samples
        updatePredictionModels();
        lastModelUpdate_ = now;
    }
}

QualityLevel PerformancePredictorImpl::getRecommendedQuality(const SystemResources& resources,
                                                            const std::vector<TranscriptionRequest>& requests) {
    if (!initialized_) {
        return QualityLevel::MEDIUM;
    }
    
    float resourceScore = calculateResourceScore(resources);
    size_t activeRequests = requests.size();
    
    // Determine quality based on resource availability and load
    if (resourceScore > 0.8f && activeRequests <= 2) {
        return QualityLevel::ULTRA_HIGH;
    } else if (resourceScore > 0.6f && activeRequests <= 4) {
        return QualityLevel::HIGH;
    } else if (resourceScore > 0.4f && activeRequests <= 6) {
        return QualityLevel::MEDIUM;
    } else if (resourceScore > 0.2f && activeRequests <= 8) {
        return QualityLevel::LOW;
    } else {
        return QualityLevel::ULTRA_LOW;
    }
}

bool PerformancePredictorImpl::isInitialized() const {
    return initialized_;
}

float PerformancePredictorImpl::predictLatencyForSettings(const QualitySettings& settings,
                                                         const SystemResources& resources,
                                                         size_t audioLength) {
    float baseLatency = predictionModel_.baseLatency;
    float qualityMultiplier = 1.0f;
    
    // Quality level impact
    switch (settings.level) {
        case QualityLevel::ULTRA_LOW: qualityMultiplier = 0.5f; break;
        case QualityLevel::LOW: qualityMultiplier = 0.7f; break;
        case QualityLevel::MEDIUM: qualityMultiplier = 1.0f; break;
        case QualityLevel::HIGH: qualityMultiplier = 1.5f; break;
        case QualityLevel::ULTRA_HIGH: qualityMultiplier = 2.0f; break;
    }
    
    // Resource impact
    float resourceImpact = 1.0f + (resources.cpuUsage * 0.5f) + (resources.memoryUsage * 0.3f);
    
    // Audio length impact (linear approximation)
    float lengthImpact = static_cast<float>(audioLength) / 16000.0f; // Assume 16kHz sample rate
    
    return baseLatency * qualityMultiplier * resourceImpact + lengthImpact * 10.0f;
}

float PerformancePredictorImpl::predictAccuracyForSettings(const QualitySettings& settings,
                                                          const SystemResources& resources) {
    float baseAccuracy = predictionModel_.baseAccuracy;
    float qualityBonus = 0.0f;
    
    // Quality level impact on accuracy
    switch (settings.level) {
        case QualityLevel::ULTRA_LOW: qualityBonus = -0.15f; break;
        case QualityLevel::LOW: qualityBonus = -0.08f; break;
        case QualityLevel::MEDIUM: qualityBonus = 0.0f; break;
        case QualityLevel::HIGH: qualityBonus = 0.05f; break;
        case QualityLevel::ULTRA_HIGH: qualityBonus = 0.10f; break;
    }
    
    // Resource constraints can impact accuracy
    float resourcePenalty = 0.0f;
    if (resources.resourceConstrained) {
        resourcePenalty = -0.05f;
    }
    
    return std::clamp(baseAccuracy + qualityBonus + resourcePenalty, 0.0f, 1.0f);
}

float PerformancePredictorImpl::calculateResourceScore(const SystemResources& resources) {
    // Higher score means more available resources
    float cpuScore = 1.0f - resources.cpuUsage;
    float memoryScore = 1.0f - resources.memoryUsage;
    float gpuScore = 1.0f - resources.gpuUsage;
    
    return (cpuScore * 0.4f + memoryScore * 0.3f + gpuScore * 0.3f);
}

float PerformancePredictorImpl::calculateQualityScore(const QualitySettings& settings) {
    float score = 0.0f;
    
    // Quality level contribution
    score += static_cast<float>(static_cast<int>(settings.level)) / 4.0f * 0.4f;
    
    // Thread count contribution
    score += std::min(settings.threadCount / 8.0f, 1.0f) * 0.2f;
    
    // GPU usage contribution
    score += settings.enableGPU ? 0.2f : 0.0f;
    
    // Preprocessing contribution
    score += settings.enablePreprocessing ? 0.1f : 0.0f;
    
    // Quantization penalty
    score -= settings.enableQuantization ? 0.1f : 0.0f;
    
    return std::clamp(score, 0.0f, 1.0f);
}

void PerformancePredictorImpl::updatePredictionModels() {
    if (performanceHistory_.size() < 10) {
        return;
    }
    
    // Simple linear regression update (simplified)
    float totalLatency = 0.0f;
    float totalAccuracy = 0.0f;
    size_t count = 0;
    
    for (const auto& point : performanceHistory_) {
        totalLatency += point.latency;
        totalAccuracy += point.accuracy;
        count++;
    }
    
    if (count > 0) {
        predictionModel_.baseLatency = totalLatency / count;
        predictionModel_.baseAccuracy = totalAccuracy / count;
    }
    
    utils::Logger::info("Prediction model updated - Base latency: " + 
                       std::to_string(predictionModel_.baseLatency) + 
                       "ms, Base accuracy: " + 
                       std::to_string(predictionModel_.baseAccuracy));
}

// QualityAdaptationEngineImpl implementation
QualityAdaptationEngineImpl::QualityAdaptationEngineImpl()
    : initialized_(false)
    , strategy_(AdaptationStrategy::BALANCED)
    , minQuality_(QualityLevel::ULTRA_LOW)
    , maxQuality_(QualityLevel::ULTRA_HIGH)
    , predictiveAdaptationEnabled_(true) {
}

QualityAdaptationEngineImpl::~QualityAdaptationEngineImpl() = default;

bool QualityAdaptationEngineImpl::initialize() {
    std::lock_guard<std::mutex> lock(adaptationMutex_);
    
    initialized_ = true;
    utils::Logger::info("QualityAdaptationEngine initialized successfully");
    return true;
}

QualitySettings QualityAdaptationEngineImpl::adaptQuality(const QualitySettings& currentSettings,
                                                         const SystemResources& resources,
                                                         const std::vector<TranscriptionRequest>& requests) {
    if (!initialized_) {
        return currentSettings;
    }
    
    QualitySettings adaptedSettings;
    
    switch (strategy_) {
        case AdaptationStrategy::CONSERVATIVE:
            adaptedSettings = adaptConservative(currentSettings, resources, requests);
            break;
        case AdaptationStrategy::BALANCED:
            adaptedSettings = adaptBalanced(currentSettings, resources, requests);
            break;
        case AdaptationStrategy::AGGRESSIVE:
            adaptedSettings = adaptAggressive(currentSettings, resources, requests);
            break;
    }
    
    // Record adaptation
    std::lock_guard<std::mutex> lock(adaptationMutex_);
    adaptationHistory_.push_back({resources, adaptedSettings});
    if (adaptationHistory_.size() > MAX_ADAPTATION_HISTORY) {
        adaptationHistory_.pop_front();
    }
    
    return adaptedSettings;
}

void QualityAdaptationEngineImpl::setAdaptationStrategy(const std::string& strategy) {
    if (strategy == "conservative") {
        strategy_ = AdaptationStrategy::CONSERVATIVE;
    } else if (strategy == "balanced") {
        strategy_ = AdaptationStrategy::BALANCED;
    } else if (strategy == "aggressive") {
        strategy_ = AdaptationStrategy::AGGRESSIVE;
    }
    
    utils::Logger::info("Adaptation strategy set to: " + strategy);
}

void QualityAdaptationEngineImpl::setQualityConstraints(QualityLevel minQuality, QualityLevel maxQuality) {
    minQuality_ = minQuality;
    maxQuality_ = maxQuality;
    
    utils::Logger::info("Quality constraints set - Min: " + std::to_string(static_cast<int>(minQuality)) +
                       ", Max: " + std::to_string(static_cast<int>(maxQuality)));
}

void QualityAdaptationEngineImpl::setPredictiveAdaptationEnabled(bool enabled) {
    predictiveAdaptationEnabled_ = enabled;
    utils::Logger::info("Predictive adaptation " + std::string(enabled ? "enabled" : "disabled"));
}

std::vector<std::pair<SystemResources, QualitySettings>> QualityAdaptationEngineImpl::getAdaptationHistory(size_t samples) const {
    std::lock_guard<std::mutex> lock(adaptationMutex_);
    
    std::vector<std::pair<SystemResources, QualitySettings>> history;
    size_t count = std::min(samples, adaptationHistory_.size());
    
    auto it = adaptationHistory_.end() - count;
    history.assign(it, adaptationHistory_.end());
    
    return history;
}

bool QualityAdaptationEngineImpl::isInitialized() const {
    return initialized_;
}

QualitySettings QualityAdaptationEngineImpl::adaptConservative(const QualitySettings& current,
                                                              const SystemResources& resources,
                                                              const std::vector<TranscriptionRequest>& requests) {
    QualitySettings adapted = current;
    
    // Conservative approach: only reduce quality when resources are heavily constrained
    if (resources.cpuUsage > 0.9f || resources.memoryUsage > 0.9f) {
        adapted.level = adjustQualityLevel(adapted.level, resources);
        adapted.threadCount = std::max(1, adapted.threadCount - 1);
        
        if (resources.memoryUsage > 0.95f) {
            adapted.maxBufferSize = std::max(adapted.maxBufferSize / 2, static_cast<size_t>(256));
        }
    }
    
    return adapted;
}

QualitySettings QualityAdaptationEngineImpl::adaptBalanced(const QualitySettings& current,
                                                          const SystemResources& resources,
                                                          const std::vector<TranscriptionRequest>& requests) {
    QualitySettings adapted = current;
    
    // Balanced approach: adjust based on resource usage and request load
    adapted.level = adjustQualityLevel(adapted.level, resources);
    adapted.threadCount = adjustThreadCount(adapted.threadCount, resources);
    adapted.enableGPU = shouldEnableGPU(resources);
    adapted.confidenceThreshold = adjustConfidenceThreshold(adapted.confidenceThreshold, adapted.level);
    adapted.maxBufferSize = adjustBufferSize(adapted.maxBufferSize, resources);
    
    // Consider request load
    if (requests.size() > 4) {
        if (adapted.level > minQuality_) {
            adapted.level = static_cast<QualityLevel>(static_cast<int>(adapted.level) - 1);
        }
    }
    
    return adapted;
}

QualitySettings QualityAdaptationEngineImpl::adaptAggressive(const QualitySettings& current,
                                                            const SystemResources& resources,
                                                            const std::vector<TranscriptionRequest>& requests) {
    QualitySettings adapted = current;
    
    // Aggressive approach: maximize performance by adjusting quality more readily
    if (resources.cpuUsage > 0.7f || resources.memoryUsage > 0.7f || requests.size() > 2) {
        adapted.level = adjustQualityLevel(adapted.level, resources);
        adapted.threadCount = adjustThreadCount(adapted.threadCount, resources);
        adapted.maxBufferSize = adjustBufferSize(adapted.maxBufferSize, resources);
        
        // More aggressive quantization
        if (resources.resourceConstrained) {
            adapted.enableQuantization = true;
            adapted.quantizationLevel = "INT8";
        }
    } else if (resources.cpuUsage < 0.4f && resources.memoryUsage < 0.4f && requests.size() <= 1) {
        // Increase quality when resources are abundant
        if (adapted.level < maxQuality_) {
            adapted.level = static_cast<QualityLevel>(static_cast<int>(adapted.level) + 1);
        }
        adapted.threadCount = std::min(adapted.threadCount + 1, 8);
    }
    
    return adapted;
}

QualityLevel QualityAdaptationEngineImpl::adjustQualityLevel(QualityLevel current, const SystemResources& resources) {
    if (resources.resourceConstrained) {
        if (current > minQuality_) {
            return static_cast<QualityLevel>(static_cast<int>(current) - 1);
        }
    } else if (resources.cpuUsage < 0.5f && resources.memoryUsage < 0.5f) {
        if (current < maxQuality_) {
            return static_cast<QualityLevel>(static_cast<int>(current) + 1);
        }
    }
    
    return current;
}

int QualityAdaptationEngineImpl::adjustThreadCount(int current, const SystemResources& resources) {
    if (resources.cpuUsage > 0.8f) {
        return std::max(1, current - 1);
    } else if (resources.cpuUsage < 0.4f) {
        return std::min(current + 1, 8);
    }
    
    return current;
}

bool QualityAdaptationEngineImpl::shouldEnableGPU(const SystemResources& resources) {
    return resources.gpuUsage < 0.7f; // Enable GPU if not heavily utilized
}

float QualityAdaptationEngineImpl::adjustConfidenceThreshold(float current, QualityLevel quality) {
    // Lower confidence threshold for lower quality levels
    switch (quality) {
        case QualityLevel::ULTRA_LOW: return 0.3f;
        case QualityLevel::LOW: return 0.4f;
        case QualityLevel::MEDIUM: return 0.5f;
        case QualityLevel::HIGH: return 0.6f;
        case QualityLevel::ULTRA_HIGH: return 0.7f;
    }
    
    return current;
}

size_t QualityAdaptationEngineImpl::adjustBufferSize(size_t current, const SystemResources& resources) {
    if (resources.memoryUsage > 0.8f) {
        return std::max(current / 2, static_cast<size_t>(256));
    } else if (resources.memoryUsage < 0.4f) {
        return std::min(current * 2, static_cast<size_t>(4096));
    }
    
    return current;
}

// AdaptiveQualityManager implementation
AdaptiveQualityManager::AdaptiveQualityManager()
    : initialized_(false)
    , adaptiveMode_(true)
    , adaptationLoopRunning_(false)
    , adaptationIntervalMs_(1000.0f)
    , lastAdaptation_(std::chrono::steady_clock::now()) {
}

AdaptiveQualityManager::~AdaptiveQualityManager() {
    stopAdaptationLoop();
}

bool AdaptiveQualityManager::initialize(const AdaptiveQualityConfig& config) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    
    try {
        config_ = config;
        
        // Initialize components
        resourceMonitor_ = std::make_unique<ResourceMonitorImpl>();
        performancePredictor_ = std::make_unique<PerformancePredictorImpl>();
        adaptationEngine_ = std::make_unique<QualityAdaptationEngineImpl>();
        
        if (!resourceMonitor_->initialize()) {
            lastError_ = "Failed to initialize resource monitor";
            return false;
        }
        
        if (!performancePredictor_->initialize()) {
            lastError_ = "Failed to initialize performance predictor";
            return false;
        }
        
        if (!adaptationEngine_->initialize()) {
            lastError_ = "Failed to initialize adaptation engine";
            return false;
        }
        
        // Set initial configuration
        resourceMonitor_->setResourceThresholds(config_.cpuThreshold, config_.memoryThreshold, 0.8f);
        adaptationEngine_->setAdaptationStrategy("balanced");
        adaptationIntervalMs_ = config_.adaptationIntervalMs;
        
        // Initialize current settings
        currentSettings_.level = config_.defaultQuality;
        currentSettings_.threadCount = 4;
        currentSettings_.enableGPU = true;
        currentSettings_.confidenceThreshold = 0.5f;
        currentSettings_.enablePreprocessing = true;
        currentSettings_.maxBufferSize = 1024;
        
        // Start monitoring and adaptation
        resourceMonitor_->startMonitoring(static_cast<int>(config_.adaptationIntervalMs / 2));
        
        if (config_.enableAdaptation) {
            startAdaptationLoop();
        }
        
        initialized_ = true;
        stats_.startTime = std::chrono::steady_clock::now();
        
        utils::Logger::info("AdaptiveQualityManager initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        lastError_ = "Initialization failed: " + std::string(e.what());
        utils::Logger::error(lastError_);
        return false;
    }
}

QualitySettings AdaptiveQualityManager::adaptQuality(const SystemResources& resources,
                                                    const std::vector<TranscriptionRequest>& pendingRequests) {
    if (!initialized_ || !adaptiveMode_) {
        return currentSettings_;
    }
    
    try {
        QualitySettings newSettings = adaptationEngine_->adaptQuality(currentSettings_, resources, pendingRequests);
        
        // Update statistics
        if (newSettings.level > currentSettings_.level) {
            stats_.qualityUpgrades++;
        } else if (newSettings.level < currentSettings_.level) {
            stats_.qualityDowngrades++;
        }
        
        stats_.totalAdaptations++;
        
        // Log significant changes
        if (newSettings.level != currentSettings_.level) {
            logAdaptation(currentSettings_, newSettings, "Quality level changed due to resource conditions");
        }
        
        updateCurrentSettings(newSettings);
        return newSettings;
        
    } catch (const std::exception& e) {
        lastError_ = "Adaptation failed: " + std::string(e.what());
        utils::Logger::error(lastError_);
        return currentSettings_;
    }
}

void AdaptiveQualityManager::setQualityLevel(QualityLevel level) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    
    currentSettings_.level = level;
    utils::Logger::info("Quality level manually set to: " + std::to_string(static_cast<int>(level)));
}

void AdaptiveQualityManager::setAdaptiveMode(bool enabled) {
    adaptiveMode_ = enabled;
    
    if (enabled && initialized_ && config_.enableAdaptation && !adaptationLoopRunning_) {
        startAdaptationLoop();
    } else if (!enabled && adaptationLoopRunning_) {
        stopAdaptationLoop();
    }
    
    utils::Logger::info("Adaptive mode " + std::string(enabled ? "enabled" : "disabled"));
}

SystemResources AdaptiveQualityManager::getCurrentResources() const {
    if (!initialized_ || !resourceMonitor_) {
        return SystemResources{};
    }
    
    return resourceMonitor_->getCurrentResources();
}

void AdaptiveQualityManager::updateResourceSnapshot() {
    if (!initialized_ || !resourceMonitor_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(managerMutex_);
    lastResourceSnapshot_ = resourceMonitor_->getCurrentResources();
}

float AdaptiveQualityManager::predictLatency(const QualitySettings& settings, size_t audioLength) const {
    if (!initialized_ || !performancePredictor_) {
        return 1000.0f; // Default 1 second
    }
    
    SystemResources resources = resourceMonitor_->getCurrentResources();
    PerformancePrediction prediction = performancePredictor_->predictPerformance(settings, resources, audioLength);
    
    return prediction.predictedLatencyMs;
}

float AdaptiveQualityManager::predictAccuracy(const QualitySettings& settings) const {
    if (!initialized_ || !performancePredictor_) {
        return 0.85f; // Default accuracy
    }
    
    SystemResources resources = resourceMonitor_->getCurrentResources();
    PerformancePrediction prediction = performancePredictor_->predictPerformance(settings, resources, 16000); // 1 second of audio
    
    return prediction.predictedAccuracy;
}

void AdaptiveQualityManager::recordActualPerformance(const QualitySettings& settings,
                                                    size_t audioLength,
                                                    float actualLatency,
                                                    float actualAccuracy) {
    if (!initialized_ || !performancePredictor_) {
        return;
    }
    
    SystemResources resources = resourceMonitor_->getCurrentResources();
    performancePredictor_->updateWithActualPerformance(settings, resources, audioLength, actualLatency, actualAccuracy);
    
    // Update statistics
    std::lock_guard<std::mutex> lock(managerMutex_);
    stats_.averageLatency = (stats_.averageLatency * (stats_.totalAdaptations - 1) + actualLatency) / stats_.totalAdaptations;
    stats_.averageAccuracy = (stats_.averageAccuracy * (stats_.totalAdaptations - 1) + actualAccuracy) / stats_.totalAdaptations;
}

QualitySettings AdaptiveQualityManager::getCurrentQualitySettings() const {
    std::lock_guard<std::mutex> lock(managerMutex_);
    return currentSettings_;
}

void AdaptiveQualityManager::setResourceThresholds(float cpuThreshold, float memoryThreshold, float gpuThreshold) {
    if (!initialized_ || !resourceMonitor_) {
        return;
    }
    
    resourceMonitor_->setResourceThresholds(cpuThreshold, memoryThreshold, gpuThreshold);
    
    // Update configuration
    std::lock_guard<std::mutex> lock(managerMutex_);
    config_.cpuThreshold = cpuThreshold;
    config_.memoryThreshold = memoryThreshold;
}

void AdaptiveQualityManager::setAdaptationInterval(float intervalMs) {
    adaptationIntervalMs_ = intervalMs;
    
    std::lock_guard<std::mutex> lock(managerMutex_);
    config_.adaptationIntervalMs = intervalMs;
    
    utils::Logger::info("Adaptation interval set to: " + std::to_string(intervalMs) + "ms");
}

void AdaptiveQualityManager::setPredictiveScalingEnabled(bool enabled) {
    if (!initialized_ || !adaptationEngine_) {
        return;
    }
    
    adaptationEngine_->setPredictiveAdaptationEnabled(enabled);
    
    std::lock_guard<std::mutex> lock(managerMutex_);
    config_.enablePredictiveScaling = enabled;
}

std::string AdaptiveQualityManager::getAdaptationStats() const {
    std::lock_guard<std::mutex> lock(managerMutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - stats_.startTime).count();
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{\n";
    oss << "  \"uptime_seconds\": " << uptime << ",\n";
    oss << "  \"total_adaptations\": " << stats_.totalAdaptations << ",\n";
    oss << "  \"quality_upgrades\": " << stats_.qualityUpgrades << ",\n";
    oss << "  \"quality_downgrades\": " << stats_.qualityDowngrades << ",\n";
    oss << "  \"average_latency_ms\": " << stats_.averageLatency << ",\n";
    oss << "  \"average_accuracy\": " << stats_.averageAccuracy << ",\n";
    oss << "  \"current_quality_level\": " << static_cast<int>(currentSettings_.level) << ",\n";
    oss << "  \"adaptive_mode_enabled\": " << (adaptiveMode_ ? "true" : "false") << "\n";
    oss << "}";
    
    return oss.str();
}

std::vector<std::pair<QualitySettings, PerformancePrediction>> AdaptiveQualityManager::getPerformanceHistory(size_t samples) const {
    std::lock_guard<std::mutex> lock(managerMutex_);
    
    std::vector<std::pair<QualitySettings, PerformancePrediction>> history;
    size_t count = std::min(samples, performanceHistory_.size());
    
    auto it = performanceHistory_.end() - count;
    history.assign(it, performanceHistory_.end());
    
    return history;
}

bool AdaptiveQualityManager::updateConfiguration(const AdaptiveQualityConfig& config) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    
    try {
        config_ = config;
        
        if (resourceMonitor_) {
            resourceMonitor_->setResourceThresholds(config_.cpuThreshold, config_.memoryThreshold, 0.8f);
        }
        
        adaptationIntervalMs_ = config_.adaptationIntervalMs;
        
        utils::Logger::info("AdaptiveQualityManager configuration updated");
        return true;
        
    } catch (const std::exception& e) {
        lastError_ = "Configuration update failed: " + std::string(e.what());
        utils::Logger::error(lastError_);
        return false;
    }
}

AdaptiveQualityConfig AdaptiveQualityManager::getCurrentConfiguration() const {
    std::lock_guard<std::mutex> lock(managerMutex_);
    return config_;
}

bool AdaptiveQualityManager::isInitialized() const {
    return initialized_;
}

std::string AdaptiveQualityManager::getLastError() const {
    std::lock_guard<std::mutex> lock(managerMutex_);
    return lastError_;
}

void AdaptiveQualityManager::reset() {
    std::lock_guard<std::mutex> lock(managerMutex_);
    
    stopAdaptationLoop();
    
    // Reset statistics
    stats_ = AdaptationStats{};
    
    // Reset performance history
    performanceHistory_.clear();
    
    // Reset to default settings
    currentSettings_ = QualitySettings{};
    currentSettings_.level = config_.defaultQuality;
    
    lastError_.clear();
    
    utils::Logger::info("AdaptiveQualityManager reset");
}

void AdaptiveQualityManager::startAdaptationLoop() {
    if (adaptationLoopRunning_) {
        return;
    }
    
    adaptationLoopRunning_ = true;
    adaptationThread_ = std::thread(&AdaptiveQualityManager::adaptationLoop, this);
    
    utils::Logger::info("Adaptation loop started");
}

void AdaptiveQualityManager::stopAdaptationLoop() {
    if (!adaptationLoopRunning_) {
        return;
    }
    
    adaptationLoopRunning_ = false;
    adaptationCondition_.notify_all();
    
    if (adaptationThread_.joinable()) {
        adaptationThread_.join();
    }
    
    utils::Logger::info("Adaptation loop stopped");
}

void AdaptiveQualityManager::adaptationLoop() {
    std::unique_lock<std::mutex> lock(managerMutex_);
    
    while (adaptationLoopRunning_) {
        try {
            // Get current resources
            SystemResources resources = resourceMonitor_->getCurrentResources();
            
            // Check if adaptation is needed
            if (shouldAdapt(resources)) {
                std::vector<TranscriptionRequest> emptyRequests; // Would get actual requests in real implementation
                QualitySettings newSettings = adaptationEngine_->adaptQuality(currentSettings_, resources, emptyRequests);
                
                if (newSettings.level != currentSettings_.level) {
                    logAdaptation(currentSettings_, newSettings, "Automatic adaptation based on resource monitoring");
                    updateCurrentSettings(newSettings);
                    
                    // Record performance prediction
                    if (performancePredictor_) {
                        PerformancePrediction prediction = performancePredictor_->predictPerformance(newSettings, resources, 16000);
                        performanceHistory_.push_back({newSettings, prediction});
                        if (performanceHistory_.size() > MAX_PERFORMANCE_HISTORY) {
                            performanceHistory_.pop_front();
                        }
                    }
                }
                
                lastAdaptation_ = std::chrono::steady_clock::now();
            }
            
        } catch (const std::exception& e) {
            lastError_ = "Adaptation loop error: " + std::string(e.what());
            utils::Logger::error(lastError_);
        }
        
        // Wait for next adaptation interval
        adaptationCondition_.wait_for(lock, std::chrono::milliseconds(static_cast<int>(adaptationIntervalMs_)),
                                     [this] { return !adaptationLoopRunning_; });
    }
}

void AdaptiveQualityManager::updateCurrentSettings(const QualitySettings& newSettings) {
    currentSettings_ = newSettings;
}

bool AdaptiveQualityManager::shouldAdapt(const SystemResources& resources) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastAdaptation_).count();
    
    // Don't adapt too frequently
    if (elapsed < adaptationIntervalMs_) {
        return false;
    }
    
    // Adapt if resources are constrained or if there's significant change
    return resources.resourceConstrained || 
           std::abs(resources.cpuUsage - lastResourceSnapshot_.cpuUsage) > 0.2f ||
           std::abs(resources.memoryUsage - lastResourceSnapshot_.memoryUsage) > 0.2f;
}

void AdaptiveQualityManager::logAdaptation(const QualitySettings& oldSettings, 
                                          const QualitySettings& newSettings, 
                                          const std::string& reason) {
    utils::Logger::info("Quality adaptation: " + 
                       std::to_string(static_cast<int>(oldSettings.level)) + " -> " +
                       std::to_string(static_cast<int>(newSettings.level)) + " (" + reason + ")");
}

} // namespace advanced
} // namespace stt