#pragma once

#include "audio/network_monitor.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace audio {

/**
 * Quality levels for audio processing
 */
enum class AudioQualityLevel {
  ULTRA_HIGH, // Maximum quality, highest resource usage
  HIGH,       // High quality, high resource usage
  MEDIUM,     // Balanced quality and performance
  LOW,        // Lower quality, better performance
  ULTRA_LOW   // Minimum quality, maximum performance
};

/**
 * Quality degradation strategy
 */
enum class DegradationStrategy {
  REDUCE_SAMPLE_RATE,   // Lower sample rate
  REDUCE_BIT_DEPTH,     // Lower bit depth
  INCREASE_COMPRESSION, // Higher compression ratio
  REDUCE_CHANNELS,      // Mono instead of stereo
  SIMPLIFY_PROCESSING,  // Use simpler algorithms
  REDUCE_BUFFER_SIZE,   // Smaller buffers
  SKIP_ENHANCEMENT      // Skip audio enhancement steps
};

/**
 * Quality parameters for audio processing
 */
struct AudioQualityParams {
  int sampleRate;            // Audio sample rate (Hz)
  int bitDepth;              // Bit depth (8, 16, 24, 32)
  int channels;              // Number of channels (1=mono, 2=stereo)
  float compressionRatio;    // Compression ratio (1.0 = no compression)
  bool enableEnhancement;    // Enable audio enhancement
  bool enableNoiseReduction; // Enable noise reduction
  size_t bufferSizeMs;       // Buffer size in milliseconds
  AudioQualityLevel level;   // Overall quality level

  AudioQualityParams()
      : sampleRate(16000), bitDepth(16), channels(1), compressionRatio(1.0f),
        enableEnhancement(true), enableNoiseReduction(true), bufferSizeMs(100),
        level(AudioQualityLevel::MEDIUM) {}
};

/**
 * Quality degradation manager
 */
class QualityDegradationManager {
public:
  QualityDegradationManager();
  ~QualityDegradationManager();

  /**
   * Initialize quality degradation manager
   * @param networkMonitor Network monitor for condition awareness
   * @return true if initialization successful
   */
  bool initialize(std::shared_ptr<NetworkMonitor> networkMonitor = nullptr);

  /**
   * Get current quality parameters
   * @return current audio quality parameters
   */
  AudioQualityParams getCurrentQualityParams() const;

  /**
   * Set target quality level
   * @param level Target quality level
   */
  void setTargetQualityLevel(AudioQualityLevel level);

  /**
   * Apply quality degradation based on network conditions
   * @param networkQuality Current network quality
   * @param metrics Network metrics
   * @return true if quality was adjusted
   */
  bool applyNetworkBasedDegradation(NetworkQuality networkQuality,
                                    const NetworkMetrics &metrics);

  /**
   * Apply quality degradation based on system resources
   * @param cpuUsage CPU usage percentage (0.0-1.0)
   * @param memoryUsage Memory usage percentage (0.0-1.0)
   * @param processingLatency Current processing latency in ms
   * @return true if quality was adjusted
   */
  bool applyResourceBasedDegradation(float cpuUsage, float memoryUsage,
                                     float processingLatency);

  /**
   * Get recommended quality parameters for given conditions
   * @param networkQuality Network quality
   * @param cpuUsage CPU usage (0.0-1.0)
   * @param memoryUsage Memory usage (0.0-1.0)
   * @return recommended quality parameters
   */
  AudioQualityParams getRecommendedParams(NetworkQuality networkQuality,
                                          float cpuUsage = 0.5f,
                                          float memoryUsage = 0.5f) const;

  /**
   * Check if quality can be improved
   * @return true if quality can be safely increased
   */
  bool canImproveQuality() const;

  /**
   * Check if quality should be degraded
   * @return true if quality should be reduced
   */
  bool shouldDegradeQuality() const;

  /**
   * Register callback for quality changes
   * @param callback Function to call when quality changes
   */
  void
  registerQualityChangeCallback(std::function<void(const AudioQualityParams &,
                                                   const AudioQualityParams &)>
                                    callback);

  /**
   * Enable/disable automatic quality adjustment
   * @param enabled true to enable automatic adjustment
   */
  void setAutoAdjustment(bool enabled);

  /**
   * Set quality adjustment aggressiveness
   * @param aggressiveness Adjustment aggressiveness (0.0-1.0)
   */
  void setAdjustmentAggressiveness(float aggressiveness);

  /**
   * Get quality degradation statistics
   * @return map of degradation statistics
   */
  std::map<std::string, double> getDegradationStats() const;

  /**
   * Reset quality to default parameters
   */
  void resetToDefault();

private:
  // Network monitoring
  std::shared_ptr<NetworkMonitor> networkMonitor_;

  // Configuration
  bool autoAdjustment_;
  float adjustmentAggressiveness_;
  AudioQualityLevel targetQualityLevel_;

  // Current state
  mutable std::mutex paramsMutex_;
  AudioQualityParams currentParams_;
  AudioQualityParams defaultParams_;

  // Quality change callbacks
  std::vector<std::function<void(const AudioQualityParams &,
                                 const AudioQualityParams &)>>
      qualityChangeCallbacks_;

  // Statistics
  std::atomic<uint64_t> totalQualityChanges_;
  std::atomic<uint64_t> qualityDegradations_;
  std::atomic<uint64_t> qualityImprovements_;
  std::atomic<uint64_t> networkBasedChanges_;
  std::atomic<uint64_t> resourceBasedChanges_;

  // History
  std::vector<
      std::pair<std::chrono::steady_clock::time_point, AudioQualityParams>>
      qualityHistory_;

  // Private methods
  AudioQualityParams
  calculateNetworkOptimizedParams(NetworkQuality quality,
                                  const NetworkMetrics &metrics) const;
  AudioQualityParams calculateResourceOptimizedParams(float cpuUsage,
                                                      float memoryUsage,
                                                      float latency) const;
  bool shouldApplyDegradation(const AudioQualityParams &newParams) const;
  void notifyQualityChange(const AudioQualityParams &oldParams,
                           const AudioQualityParams &newParams);
  void recordQualityChange(const AudioQualityParams &params);
  AudioQualityLevel
  getQualityLevelFromParams(const AudioQualityParams &params) const;
  AudioQualityParams getParamsForQualityLevel(AudioQualityLevel level) const;
};

/**
 * Adaptive quality controller that automatically adjusts quality
 */
class AdaptiveQualityController {
public:
  AdaptiveQualityController();
  ~AdaptiveQualityController();

  /**
   * Initialize adaptive quality controller
   * @param degradationManager Quality degradation manager
   * @param networkMonitor Network monitor
   * @return true if initialization successful
   */
  bool initialize(std::shared_ptr<QualityDegradationManager> degradationManager,
                  std::shared_ptr<NetworkMonitor> networkMonitor);

  /**
   * Start automatic quality control
   * @param updateIntervalMs Update interval in milliseconds
   * @return true if started successfully
   */
  bool startAutoControl(int updateIntervalMs = 2000);

  /**
   * Stop automatic quality control
   */
  void stopAutoControl();

  /**
   * Update system resource information
   * @param cpuUsage Current CPU usage (0.0-1.0)
   * @param memoryUsage Current memory usage (0.0-1.0)
   * @param processingLatency Current processing latency in ms
   */
  void updateSystemResources(float cpuUsage, float memoryUsage,
                             float processingLatency);

  /**
   * Set quality control thresholds
   * @param cpuThreshold CPU usage threshold for degradation
   * @param memoryThreshold Memory usage threshold for degradation
   * @param latencyThreshold Latency threshold for degradation (ms)
   */
  void setControlThresholds(float cpuThreshold = 0.8f,
                            float memoryThreshold = 0.8f,
                            float latencyThreshold = 200.0f);

  /**
   * Enable/disable network-based quality control
   * @param enabled true to enable network-based control
   */
  void setNetworkBasedControl(bool enabled);

  /**
   * Enable/disable resource-based quality control
   * @param enabled true to enable resource-based control
   */
  void setResourceBasedControl(bool enabled);

  /**
   * Get adaptive control statistics
   * @return map of control statistics
   */
  std::map<std::string, double> getControlStats() const;

private:
  // Components
  std::shared_ptr<QualityDegradationManager> degradationManager_;
  std::shared_ptr<NetworkMonitor> networkMonitor_;

  // Control thread
  std::atomic<bool> controlActive_;
  std::unique_ptr<std::thread> controlThread_;
  int updateIntervalMs_;

  // Configuration
  bool networkBasedControl_;
  bool resourceBasedControl_;
  float cpuThreshold_;
  float memoryThreshold_;
  float latencyThreshold_;

  // Current system state
  mutable std::mutex resourcesMutex_;
  float currentCpuUsage_;
  float currentMemoryUsage_;
  float currentProcessingLatency_;

  // Statistics
  std::atomic<uint64_t> totalControlCycles_;
  std::atomic<uint64_t> networkAdjustments_;
  std::atomic<uint64_t> resourceAdjustments_;

  // Private methods
  void controlLoop();
  void performQualityControl();
  bool shouldAdjustForNetwork(NetworkQuality quality,
                              const NetworkMetrics &metrics) const;
  bool shouldAdjustForResources(float cpu, float memory, float latency) const;
};

} // namespace audio