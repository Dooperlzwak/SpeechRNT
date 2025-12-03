#include "audio/quality_degradation.hpp"
#include "utils/logging.hpp"
#include "utils/performance_monitor.hpp"
#include <algorithm>
#include <iostream>
#include <thread>

namespace audio {

QualityDegradationManager::QualityDegradationManager()
    : autoAdjustment_(true), adjustmentAggressiveness_(0.5f),
      targetQualityLevel_(AudioQualityLevel::MEDIUM), totalQualityChanges_(0),
      qualityDegradations_(0), qualityImprovements_(0), networkBasedChanges_(0),
      resourceBasedChanges_(0) {

  // Set default parameters
  defaultParams_.sampleRate = 16000;
  defaultParams_.bitDepth = 16;
  defaultParams_.channels = 1;
  defaultParams_.compressionRatio = 1.0f;
  defaultParams_.enableEnhancement = true;
  defaultParams_.enableNoiseReduction = true;
  defaultParams_.bufferSizeMs = 100;
  defaultParams_.level = AudioQualityLevel::MEDIUM;

  currentParams_ = defaultParams_;
}

QualityDegradationManager::~QualityDegradationManager() {}

bool QualityDegradationManager::initialize(
    std::shared_ptr<NetworkMonitor> networkMonitor) {
  networkMonitor_ = networkMonitor;

  if (networkMonitor_) {
    // Register for network condition changes
    networkMonitor_->registerConditionCallback(
        [this](const NetworkMetrics &metrics, NetworkQuality quality) {
          if (autoAdjustment_) {
            applyNetworkBasedDegradation(quality, metrics);
          }
        });

    // speechrnt::utils::Logger::info("QualityDegradationManager initialized
    // with network monitor");
    std::cout << "QualityDegradationManager initialized with network monitor"
              << std::endl;
  } else {
    // speechrnt::utils::Logger::info("QualityDegradationManager initialized
    // without network monitor");
    std::cout << "QualityDegradationManager initialized without network monitor"
              << std::endl;
  }

  return true;
}

AudioQualityParams QualityDegradationManager::getCurrentQualityParams() const {
  std::lock_guard<std::mutex> lock(paramsMutex_);
  return currentParams_;
}

void QualityDegradationManager::setTargetQualityLevel(AudioQualityLevel level) {
  targetQualityLevel_ = level;

  std::lock_guard<std::mutex> lock(paramsMutex_);
  AudioQualityParams oldParams = currentParams_;
  currentParams_ = getParamsForQualityLevel(level);

  notifyQualityChange(oldParams, currentParams_);
  recordQualityChange(currentParams_);

  // speechrnt::utils::Logger::info("Target quality level set to " +
  // std::to_string(static_cast<int>(level)));
  std::cout << "Target quality level set to " << static_cast<int>(level)
            << std::endl;
}

bool QualityDegradationManager::applyNetworkBasedDegradation(
    NetworkQuality networkQuality, const NetworkMetrics &metrics) {
  if (!autoAdjustment_) {
    return false;
  }

  AudioQualityParams newParams =
      calculateNetworkOptimizedParams(networkQuality, metrics);

  std::lock_guard<std::mutex> lock(paramsMutex_);
  if (shouldApplyDegradation(newParams)) {
    AudioQualityParams oldParams = currentParams_;
    currentParams_ = newParams;

    networkBasedChanges_++;
    totalQualityChanges_++;

    if (getQualityLevelFromParams(newParams) <
        getQualityLevelFromParams(oldParams)) {
      qualityDegradations_++;
    } else if (getQualityLevelFromParams(newParams) >
               getQualityLevelFromParams(oldParams)) {
      qualityImprovements_++;
    }

    notifyQualityChange(oldParams, currentParams_);
    recordQualityChange(currentParams_);

    // speechrnt::utils::Logger::info("Applied network-based quality
    // degradation");
    std::cout << "Applied network-based quality degradation" << std::endl;
    return true;
  }

  return false;
}

bool QualityDegradationManager::applyResourceBasedDegradation(
    float cpuUsage, float memoryUsage, float processingLatency) {
  if (!autoAdjustment_) {
    return false;
  }

  AudioQualityParams newParams = calculateResourceOptimizedParams(
      cpuUsage, memoryUsage, processingLatency);

  std::lock_guard<std::mutex> lock(paramsMutex_);
  if (shouldApplyDegradation(newParams)) {
    AudioQualityParams oldParams = currentParams_;
    currentParams_ = newParams;

    resourceBasedChanges_++;
    totalQualityChanges_++;

    if (getQualityLevelFromParams(newParams) <
        getQualityLevelFromParams(oldParams)) {
      qualityDegradations_++;
    } else if (getQualityLevelFromParams(newParams) >
               getQualityLevelFromParams(oldParams)) {
      qualityImprovements_++;
    }

    notifyQualityChange(oldParams, currentParams_);
    recordQualityChange(currentParams_);

    // speechrnt::utils::Logger::info("Applied resource-based quality
    // degradation");
    std::cout << "Applied resource-based quality degradation" << std::endl;
    return true;
  }

  return false;
}

AudioQualityParams QualityDegradationManager::getRecommendedParams(
    NetworkQuality networkQuality, float cpuUsage, float memoryUsage) const {
  // Start with network-optimized parameters
  NetworkMetrics dummyMetrics; // Use default metrics for this calculation
  AudioQualityParams networkParams =
      calculateNetworkOptimizedParams(networkQuality, dummyMetrics);

  // Apply resource-based adjustments
  AudioQualityParams resourceParams =
      calculateResourceOptimizedParams(cpuUsage, memoryUsage, 100.0f);

  // Combine both considerations - take the more conservative (lower quality)
  // option
  AudioQualityParams combinedParams;
  combinedParams.sampleRate =
      std::min(networkParams.sampleRate, resourceParams.sampleRate);
  combinedParams.bitDepth =
      std::min(networkParams.bitDepth, resourceParams.bitDepth);
  combinedParams.channels =
      std::min(networkParams.channels, resourceParams.channels);
  combinedParams.compressionRatio =
      std::max(networkParams.compressionRatio, resourceParams.compressionRatio);
  combinedParams.enableEnhancement =
      networkParams.enableEnhancement && resourceParams.enableEnhancement;
  combinedParams.enableNoiseReduction =
      networkParams.enableNoiseReduction && resourceParams.enableNoiseReduction;
  combinedParams.bufferSizeMs =
      std::max(networkParams.bufferSizeMs, resourceParams.bufferSizeMs);

  // Determine overall quality level
  combinedParams.level = std::min(networkParams.level, resourceParams.level);

  return combinedParams;
}

bool QualityDegradationManager::canImproveQuality() const {
  std::lock_guard<std::mutex> lock(paramsMutex_);

  // Can improve if current quality is below target
  return getQualityLevelFromParams(currentParams_) < targetQualityLevel_;
}

bool QualityDegradationManager::shouldDegradeQuality() const {
  if (!networkMonitor_) {
    return false;
  }

  NetworkQuality quality = networkMonitor_->getNetworkQuality();
  return quality == NetworkQuality::POOR ||
         quality == NetworkQuality::VERY_POOR;
}

void QualityDegradationManager::registerQualityChangeCallback(
    std::function<void(const AudioQualityParams &, const AudioQualityParams &)>
        callback) {
  qualityChangeCallbacks_.push_back(callback);
}

void QualityDegradationManager::setAutoAdjustment(bool enabled) {
  autoAdjustment_ = enabled;
  speechrnt::utils::Logger::info("Quality auto-adjustment " +
                                 std::string(enabled ? "enabled" : "disabled"));
}

void QualityDegradationManager::setAdjustmentAggressiveness(
    float aggressiveness) {
  adjustmentAggressiveness_ = std::max(0.0f, std::min(1.0f, aggressiveness));
  speechrnt::utils::Logger::info("Quality adjustment aggressiveness set to " +
                                 std::to_string(adjustmentAggressiveness_));
}

std::map<std::string, double>
QualityDegradationManager::getDegradationStats() const {
  std::lock_guard<std::mutex> lock(paramsMutex_);

  std::map<std::string, double> stats;
  stats["total_quality_changes"] =
      static_cast<double>(totalQualityChanges_.load());
  stats["quality_degradations"] =
      static_cast<double>(qualityDegradations_.load());
  stats["quality_improvements"] =
      static_cast<double>(qualityImprovements_.load());
  stats["network_based_changes"] =
      static_cast<double>(networkBasedChanges_.load());
  stats["resource_based_changes"] =
      static_cast<double>(resourceBasedChanges_.load());

  stats["current_sample_rate"] = static_cast<double>(currentParams_.sampleRate);
  stats["current_bit_depth"] = static_cast<double>(currentParams_.bitDepth);
  stats["current_channels"] = static_cast<double>(currentParams_.channels);
  stats["current_compression_ratio"] =
      static_cast<double>(currentParams_.compressionRatio);
  stats["current_quality_level"] = static_cast<double>(currentParams_.level);
  stats["target_quality_level"] = static_cast<double>(targetQualityLevel_);

  stats["auto_adjustment_enabled"] = autoAdjustment_ ? 1.0 : 0.0;
  stats["adjustment_aggressiveness"] =
      static_cast<double>(adjustmentAggressiveness_);
  stats["quality_history_size"] = static_cast<double>(qualityHistory_.size());

  return stats;
}

void QualityDegradationManager::resetToDefault() {
  std::lock_guard<std::mutex> lock(paramsMutex_);

  AudioQualityParams oldParams = currentParams_;
  currentParams_ = defaultParams_;

  notifyQualityChange(oldParams, currentParams_);
  recordQualityChange(currentParams_);

  speechrnt::utils::Logger::info("Quality parameters reset to default");
}

AudioQualityParams QualityDegradationManager::calculateNetworkOptimizedParams(
    NetworkQuality quality, const NetworkMetrics &metrics) const {

  AudioQualityParams params = defaultParams_;

  switch (quality) {
  case NetworkQuality::EXCELLENT:
    // Can use highest quality
    params.sampleRate = 48000;
    params.bitDepth = 24;
    params.channels = 2;
    params.compressionRatio = 1.0f;
    params.enableEnhancement = true;
    params.enableNoiseReduction = true;
    params.bufferSizeMs = 50;
    params.level = AudioQualityLevel::ULTRA_HIGH;
    break;

  case NetworkQuality::GOOD:
    // High quality with some optimization
    params.sampleRate = 44100;
    params.bitDepth = 16;
    params.channels = 2;
    params.compressionRatio = 1.2f;
    params.enableEnhancement = true;
    params.enableNoiseReduction = true;
    params.bufferSizeMs = 75;
    params.level = AudioQualityLevel::HIGH;
    break;

  case NetworkQuality::FAIR:
    // Balanced quality
    params.sampleRate = 22050;
    params.bitDepth = 16;
    params.channels = 1;
    params.compressionRatio = 1.5f;
    params.enableEnhancement = true;
    params.enableNoiseReduction = false;
    params.bufferSizeMs = 100;
    params.level = AudioQualityLevel::MEDIUM;
    break;

  case NetworkQuality::POOR:
    // Lower quality for reliability
    params.sampleRate = 16000;
    params.bitDepth = 16;
    params.channels = 1;
    params.compressionRatio = 2.0f;
    params.enableEnhancement = false;
    params.enableNoiseReduction = false;
    params.bufferSizeMs = 150;
    params.level = AudioQualityLevel::LOW;
    break;

  case NetworkQuality::VERY_POOR:
    // Minimum quality for maximum reliability
    params.sampleRate = 8000;
    params.bitDepth = 8;
    params.channels = 1;
    params.compressionRatio = 3.0f;
    params.enableEnhancement = false;
    params.enableNoiseReduction = false;
    params.bufferSizeMs = 200;
    params.level = AudioQualityLevel::ULTRA_LOW;
    break;
  }

  // Fine-tune based on specific metrics
  if (metrics.latencyMs > 200.0f) {
    params.bufferSizeMs = std::min(params.bufferSizeMs * 1.5f, 300.0f);
  }

  if (metrics.jitterMs > 50.0f) {
    params.compressionRatio *= 1.2f;
  }

  if (metrics.packetLossRate > 2.0f) {
    params.compressionRatio *= 1.5f;
    params.enableEnhancement = false;
  }

  return params;
}

AudioQualityParams QualityDegradationManager::calculateResourceOptimizedParams(
    float cpuUsage, float memoryUsage, float latency) const {

  AudioQualityParams params = defaultParams_;

  // Adjust based on CPU usage
  if (cpuUsage > 0.9f) {
    // Very high CPU usage - minimize processing
    params.sampleRate = 8000;
    params.bitDepth = 8;
    params.channels = 1;
    params.enableEnhancement = false;
    params.enableNoiseReduction = false;
    params.level = AudioQualityLevel::ULTRA_LOW;
  } else if (cpuUsage > 0.8f) {
    // High CPU usage - reduce quality
    params.sampleRate = 16000;
    params.bitDepth = 16;
    params.channels = 1;
    params.enableEnhancement = false;
    params.enableNoiseReduction = false;
    params.level = AudioQualityLevel::LOW;
  } else if (cpuUsage > 0.6f) {
    // Moderate CPU usage - balanced quality
    params.sampleRate = 22050;
    params.bitDepth = 16;
    params.channels = 1;
    params.enableEnhancement = true;
    params.enableNoiseReduction = false;
    params.level = AudioQualityLevel::MEDIUM;
  } else if (cpuUsage > 0.4f) {
    // Low CPU usage - good quality
    params.sampleRate = 44100;
    params.bitDepth = 16;
    params.channels = 2;
    params.enableEnhancement = true;
    params.enableNoiseReduction = true;
    params.level = AudioQualityLevel::HIGH;
  } else {
    // Very low CPU usage - maximum quality
    params.sampleRate = 48000;
    params.bitDepth = 24;
    params.channels = 2;
    params.enableEnhancement = true;
    params.enableNoiseReduction = true;
    params.level = AudioQualityLevel::ULTRA_HIGH;
  }

  // Adjust based on memory usage
  if (memoryUsage > 0.8f) {
    params.bufferSizeMs = std::max(params.bufferSizeMs * 0.7f, 50.0f);
    params.compressionRatio *= 1.3f;
  }

  // Adjust based on processing latency
  if (latency > 200.0f) {
    params.bufferSizeMs = std::max(params.bufferSizeMs * 0.8f, 50.0f);
    if (latency > 500.0f) {
      params.enableEnhancement = false;
      params.enableNoiseReduction = false;
    }
  }

  return params;
}

bool QualityDegradationManager::shouldApplyDegradation(
    const AudioQualityParams &newParams) const {
  // Check if parameters have changed significantly
  const float threshold = 0.1f; // 10% change threshold

  bool significantChange =
      (std::abs(newParams.sampleRate - currentParams_.sampleRate) >
       currentParams_.sampleRate * threshold) ||
      (newParams.bitDepth != currentParams_.bitDepth) ||
      (newParams.channels != currentParams_.channels) ||
      (std::abs(newParams.compressionRatio - currentParams_.compressionRatio) >
       threshold) ||
      (newParams.enableEnhancement != currentParams_.enableEnhancement) ||
      (newParams.enableNoiseReduction != currentParams_.enableNoiseReduction) ||
      (std::abs(static_cast<float>(newParams.bufferSizeMs -
                                   currentParams_.bufferSizeMs)) >
       currentParams_.bufferSizeMs * threshold);

  return significantChange;
}

void QualityDegradationManager::notifyQualityChange(
    const AudioQualityParams &oldParams, const AudioQualityParams &newParams) {
  for (const auto &callback : qualityChangeCallbacks_) {
    try {
      callback(oldParams, newParams);
    } catch (const std::exception &e) {
      speechrnt::utils::Logger::error("Quality change callback error: " +
                                      std::string(e.what()));
    }
  }
}

void QualityDegradationManager::recordQualityChange(
    const AudioQualityParams &params) {
  qualityHistory_.push_back({std::chrono::steady_clock::now(), params});

  // Keep only recent history
  const size_t maxHistorySize = 100;
  if (qualityHistory_.size() > maxHistorySize) {
    qualityHistory_.erase(qualityHistory_.begin());
  }
}

AudioQualityLevel QualityDegradationManager::getQualityLevelFromParams(
    const AudioQualityParams &params) const {

  // Simple heuristic based on sample rate and features
  if (params.sampleRate >= 44100 && params.bitDepth >= 16 &&
      params.enableEnhancement && params.enableNoiseReduction) {
    if (params.sampleRate >= 48000 && params.bitDepth >= 24) {
      return AudioQualityLevel::ULTRA_HIGH;
    }
    return AudioQualityLevel::HIGH;
  } else if (params.sampleRate >= 22050 && params.bitDepth >= 16) {
    return AudioQualityLevel::MEDIUM;
  } else if (params.sampleRate >= 16000) {
    return AudioQualityLevel::LOW;
  } else {
    return AudioQualityLevel::ULTRA_LOW;
  }
}

AudioQualityParams QualityDegradationManager::getParamsForQualityLevel(
    AudioQualityLevel level) const {
  AudioQualityParams params;

  switch (level) {
  case AudioQualityLevel::ULTRA_HIGH:
    params.sampleRate = 48000;
    params.bitDepth = 24;
    params.channels = 2;
    params.compressionRatio = 1.0f;
    params.enableEnhancement = true;
    params.enableNoiseReduction = true;
    params.bufferSizeMs = 100;
    break;

  case AudioQualityLevel::HIGH:
    params.sampleRate = 44100;
    params.bitDepth = 16;
    params.channels = 2;
    params.compressionRatio = 1.2f;
    params.enableEnhancement = true;
    params.enableNoiseReduction = true;
    params.bufferSizeMs = 100;
    break;

  case AudioQualityLevel::MEDIUM:
    params.sampleRate = 22050;
    params.bitDepth = 16;
    params.channels = 1;
    params.compressionRatio = 1.5f;
    params.enableEnhancement = true;
    params.enableNoiseReduction = false;
    params.bufferSizeMs = 100;
    break;

  case AudioQualityLevel::LOW:
    params.sampleRate = 16000;
    params.bitDepth = 16;
    params.channels = 1;
    params.compressionRatio = 2.0f;
    params.enableEnhancement = false;
    params.enableNoiseReduction = false;
    params.bufferSizeMs = 100;
    break;

  case AudioQualityLevel::ULTRA_LOW:
    params.sampleRate = 8000;
    params.bitDepth = 8;
    params.channels = 1;
    params.compressionRatio = 3.0f;
    params.enableEnhancement = false;
    params.enableNoiseReduction = false;
    params.bufferSizeMs = 100;
    break;
  }

  params.level = level;
  return params;
}

AdaptiveQualityController::AdaptiveQualityController()
    : controlActive_(false), updateIntervalMs_(2000),
      networkBasedControl_(true), resourceBasedControl_(true),
      cpuThreshold_(0.8f), memoryThreshold_(0.8f), latencyThreshold_(200.0f),
      currentCpuUsage_(0.0f), currentMemoryUsage_(0.0f),
      currentProcessingLatency_(0.0f), totalControlCycles_(0),
      networkAdjustments_(0), resourceAdjustments_(0) {}

AdaptiveQualityController::~AdaptiveQualityController() { stopAutoControl(); }

bool AdaptiveQualityController::initialize(
    std::shared_ptr<QualityDegradationManager> degradationManager,
    std::shared_ptr<NetworkMonitor> networkMonitor) {
  degradationManager_ = degradationManager;
  networkMonitor_ = networkMonitor;

  if (degradationManager_ && networkMonitor_) {
    speechrnt::utils::Logger::info(
        "AdaptiveQualityController initialized successfully");
    return true;
  } else {
    speechrnt::utils::Logger::error(
        "AdaptiveQualityController initialization failed");
    return false;
  }
}

bool AdaptiveQualityController::startAutoControl(int updateIntervalMs) {
  if (controlActive_.load()) {
    speechrnt::utils::Logger::warn("Adaptive quality control already active");
    return true;
  }

  updateIntervalMs_ = updateIntervalMs;
  controlActive_ = true;
  controlThread_ = std::make_unique<std::thread>(
      &AdaptiveQualityController::controlLoop, this);

  speechrnt::utils::Logger::info("Adaptive quality control started with " +
                                 std::to_string(updateIntervalMs) +
                                 "ms interval");
  return true;
}

void AdaptiveQualityController::stopAutoControl() {
  if (!controlActive_.load()) {
    return;
  }

  controlActive_ = false;

  if (controlThread_ && controlThread_->joinable()) {
    controlThread_->join();
  }

  speechrnt::utils::Logger::info("Adaptive quality control stopped");
}

void AdaptiveQualityController::updateSystemResources(float cpuUsage,
                                                      float memoryUsage,
                                                      float processingLatency) {
  std::lock_guard<std::mutex> lock(resourcesMutex_);
  currentCpuUsage_ = cpuUsage;
  currentMemoryUsage_ = memoryUsage;
  currentProcessingLatency_ = processingLatency;
}

void AdaptiveQualityController::setControlThresholds(float cpuThreshold,
                                                     float memoryThreshold,
                                                     float latencyThreshold) {
  cpuThreshold_ = cpuThreshold;
  memoryThreshold_ = memoryThreshold;
  latencyThreshold_ = latencyThreshold;

  speechrnt::utils::Logger::info(
      "Quality control thresholds updated: CPU=" +
      std::to_string(cpuThreshold) +
      ", Memory=" + std::to_string(memoryThreshold) +
      ", Latency=" + std::to_string(latencyThreshold) + "ms");
}

void AdaptiveQualityController::setNetworkBasedControl(bool enabled) {
  networkBasedControl_ = enabled;
  speechrnt::utils::Logger::info("Network-based quality control " +
                                 std::string(enabled ? "enabled" : "disabled"));
}

void AdaptiveQualityController::setResourceBasedControl(bool enabled) {
  resourceBasedControl_ = enabled;
  speechrnt::utils::Logger::info("Resource-based quality control " +
                                 std::string(enabled ? "enabled" : "disabled"));
}

std::map<std::string, double>
AdaptiveQualityController::getControlStats() const {
  std::lock_guard<std::mutex> lock(resourcesMutex_);

  std::map<std::string, double> stats;
  stats["total_control_cycles"] =
      static_cast<double>(totalControlCycles_.load());
  stats["network_adjustments"] =
      static_cast<double>(networkAdjustments_.load());
  stats["resource_adjustments"] =
      static_cast<double>(resourceAdjustments_.load());

  stats["current_cpu_usage"] = static_cast<double>(currentCpuUsage_);
  stats["current_memory_usage"] = static_cast<double>(currentMemoryUsage_);
  stats["current_processing_latency"] =
      static_cast<double>(currentProcessingLatency_);

  stats["cpu_threshold"] = static_cast<double>(cpuThreshold_);
  stats["memory_threshold"] = static_cast<double>(memoryThreshold_);
  stats["latency_threshold"] = static_cast<double>(latencyThreshold_);

  stats["network_based_control"] = networkBasedControl_ ? 1.0 : 0.0;
  stats["resource_based_control"] = resourceBasedControl_ ? 1.0 : 0.0;
  stats["control_active"] = controlActive_.load() ? 1.0 : 0.0;
  stats["update_interval_ms"] = static_cast<double>(updateIntervalMs_);

  return stats;
}

void AdaptiveQualityController::controlLoop() {
  speechrnt::utils::Logger::info("Adaptive quality control loop started");

  while (controlActive_.load()) {
    try {
      performQualityControl();
      totalControlCycles_++;

    } catch (const std::exception &e) {
      speechrnt::utils::Logger::error("Quality control error: " +
                                      std::string(e.what()));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(updateIntervalMs_));
  }

  speechrnt::utils::Logger::info("Adaptive quality control loop stopped");
}

void AdaptiveQualityController::performQualityControl() {
  if (!degradationManager_ || !networkMonitor_) {
    return;
  }

  bool adjustmentMade = false;

  // Network-based adjustment
  if (networkBasedControl_) {
    NetworkQuality quality = networkMonitor_->getNetworkQuality();
    NetworkMetrics metrics = networkMonitor_->getCurrentMetrics();

    if (shouldAdjustForNetwork(quality, metrics)) {
      if (degradationManager_->applyNetworkBasedDegradation(quality, metrics)) {
        networkAdjustments_++;
        adjustmentMade = true;
      }
    }
  }

  // Resource-based adjustment
  if (resourceBasedControl_ && !adjustmentMade) {
    float cpu, memory, latency;
    {
      std::lock_guard<std::mutex> lock(resourcesMutex_);
      cpu = currentCpuUsage_;
      memory = currentMemoryUsage_;
      latency = currentProcessingLatency_;
    }

    if (shouldAdjustForResources(cpu, memory, latency)) {
      if (degradationManager_->applyResourceBasedDegradation(cpu, memory,
                                                             latency)) {
        resourceAdjustments_++;
        adjustmentMade = true;
      }
    }
  }

  if (adjustmentMade) {
    speechrnt::utils::Logger::debug("Adaptive quality control made adjustment");
  }
}

bool AdaptiveQualityController::shouldAdjustForNetwork(
    NetworkQuality quality, const NetworkMetrics &metrics) const {
  // Adjust for poor network conditions
  return quality == NetworkQuality::POOR ||
         quality == NetworkQuality::VERY_POOR || metrics.latencyMs > 200.0f ||
         metrics.packetLossRate > 2.0f;
}

bool AdaptiveQualityController::shouldAdjustForResources(float cpu,
                                                         float memory,
                                                         float latency) const {
  // Adjust if any resource threshold is exceeded
  return cpu > cpuThreshold_ || memory > memoryThreshold_ ||
         latency > latencyThreshold_;
}

} // namespace audio