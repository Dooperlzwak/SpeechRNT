#pragma once

#include "audio/load_balanced_pipeline.hpp"
#include "audio/network_monitor.hpp"
#include "audio/packet_recovery.hpp"
#include "audio/quality_degradation.hpp"
#include "audio/streaming_optimizer.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

namespace audio {

/**
 * Advanced streaming configuration
 */
struct AdvancedStreamingConfig {
  // Network monitoring
  bool enableNetworkMonitoring;
  int networkMonitoringIntervalMs;

  // Packet recovery
  bool enablePacketRecovery;
  int packetTimeoutMs;
  int maxRetries;

  // Quality degradation
  bool enableQualityDegradation;
  float cpuThreshold;
  float memoryThreshold;

  // Load balancing
  bool enableLoadBalancing;
  size_t numWorkerThreads;
  size_t maxQueueSize;

  // Ultra-low latency settings
  int targetLatencyMs;
  bool enableUltraLowLatency;

  AdvancedStreamingConfig()
      : enableNetworkMonitoring(true), networkMonitoringIntervalMs(1000),
        enablePacketRecovery(true), packetTimeoutMs(1000), maxRetries(3),
        enableQualityDegradation(true), cpuThreshold(0.8f),
        memoryThreshold(0.8f), enableLoadBalancing(true), numWorkerThreads(4),
        maxQueueSize(1000), targetLatencyMs(200), enableUltraLowLatency(true) {}
};

/**
 * Streaming performance metrics
 */
struct StreamingPerformanceMetrics {
  float endToEndLatencyMs;
  float networkLatencyMs;
  float processingLatencyMs;
  float queueLatencyMs;
  float packetLossRate;
  float throughputMbps;
  float cpuUsage;
  float memoryUsage;
  size_t activeStreams;
  size_t queuedJobs;
  AudioQualityLevel currentQuality;
  bool ultraLowLatencyActive;
  std::chrono::steady_clock::time_point timestamp;

  StreamingPerformanceMetrics()
      : endToEndLatencyMs(0.0f), networkLatencyMs(0.0f),
        processingLatencyMs(0.0f), queueLatencyMs(0.0f), packetLossRate(0.0f),
        throughputMbps(0.0f), cpuUsage(0.0f), memoryUsage(0.0f),
        activeStreams(0), queuedJobs(0),
        currentQuality(AudioQualityLevel::MEDIUM), ultraLowLatencyActive(false),
        timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * Advanced streaming optimizer that integrates all optimization components
 */
class AdvancedStreamingOptimizer {
public:
  AdvancedStreamingOptimizer();
  ~AdvancedStreamingOptimizer();

  /**
   * Initialize the advanced streaming optimizer
   * @param config Configuration parameters
   * @return true if initialization successful
   */
  bool initialize(const AdvancedStreamingConfig &config);

  /**
   * Start all optimization services
   * @return true if services started successfully
   */
  bool start();

  /**
   * Stop all optimization services
   */
  void stop();

  /**
   * Process audio stream with all optimizations
   * @param audioData Input audio data
   * @param streamId Stream identifier
   * @param outputChunks Output optimized chunks
   * @return true if processing successful
   */
  bool processStreamWithOptimizations(const std::vector<float> &audioData,
                                      uint32_t streamId,
                                      std::vector<AudioChunk> &outputChunks);

  /**
   * Submit real-time processing job
   * @param task Processing task
   * @param priority Job priority
   * @return job ID for tracking
   */
  uint64_t
  submitRealTimeJob(std::function<void()> task,
                    ProcessingPriority priority = ProcessingPriority::CRITICAL);

  /**
   * Submit batch processing job
   * @param task Processing task
   * @param priority Job priority
   * @return job ID for tracking
   */
  uint64_t
  submitBatchJob(std::function<void()> task,
                 ProcessingPriority priority = ProcessingPriority::LOW);

  /**
   * Get current streaming performance metrics
   * @return performance metrics
   */
  StreamingPerformanceMetrics getPerformanceMetrics() const;

  /**
   * Check if ultra-low latency mode is active
   * @return true if ultra-low latency is active
   */
  bool isUltraLowLatencyActive() const;

  /**
   * Enable/disable ultra-low latency mode
   * @param enabled true to enable ultra-low latency
   */
  void setUltraLowLatencyMode(bool enabled);

  /**
   * Set target latency for streaming
   * @param latencyMs Target latency in milliseconds
   */
  void setTargetLatency(int latencyMs);

  /**
   * Get adaptive streaming parameters for current conditions
   * @return current streaming parameters
   */
  AdaptiveStreamingParams getCurrentStreamingParams() const;

  /**
   * Register callback for performance metrics updates
   * @param callback Function to call with updated metrics
   */
  void registerMetricsCallback(
      std::function<void(const StreamingPerformanceMetrics &)> callback);

  /**
   * Get comprehensive optimization statistics
   * @return map of optimization statistics
   */
  std::map<std::string, double> getOptimizationStats() const;

  /**
   * Check if all optimization services are healthy
   * @return true if all services are healthy
   */
  bool isHealthy() const;

  /**
   * Perform manual optimization adjustment
   * @param adjustmentType Type of adjustment to perform
   * @param value Adjustment value
   */
  void performManualAdjustment(const std::string &adjustmentType, float value);

private:
  // Configuration
  AdvancedStreamingConfig config_;
  bool initialized_;
  bool running_;

  // Core components
  std::shared_ptr<StreamingOptimizer> streamingOptimizer_;
  std::shared_ptr<NetworkMonitor> networkMonitor_;
  std::shared_ptr<NetworkAwareStreamingAdapter> networkAdapter_;
  std::shared_ptr<PacketRecoverySystem> packetRecovery_;
  std::shared_ptr<QualityDegradationManager> qualityManager_;
  std::shared_ptr<AdaptiveQualityController> qualityController_;
  std::shared_ptr<LoadBalancedProcessingPipeline> processingPipeline_;

  // Ultra-low latency mode
  std::atomic<bool> ultraLowLatencyMode_;
  int targetLatencyMs_;

  // Performance tracking
  mutable std::mutex metricsMutex_;
  StreamingPerformanceMetrics currentMetrics_;
  std::vector<std::function<void(const StreamingPerformanceMetrics &)>>
      metricsCallbacks_;

  // Statistics
  std::atomic<uint64_t> totalStreamsProcessed_;
  std::atomic<uint64_t> totalJobsSubmitted_;
  std::atomic<uint64_t> ultraLowLatencyActivations_;

  // Private methods
  void updatePerformanceMetrics();
  void onNetworkConditionChange(const NetworkMetrics &metrics,
                                NetworkQuality quality);
  void onQualityChange(const AudioQualityParams &oldParams,
                       const AudioQualityParams &newParams);
  void optimizeForUltraLowLatency();
  void notifyMetricsUpdate(const StreamingPerformanceMetrics &metrics);
  bool checkLatencyTarget(float currentLatency) const;
  void adjustOptimizationParameters();
};

/**
 * Ultra-low latency streaming processor
 */
class UltraLowLatencyProcessor {
public:
  UltraLowLatencyProcessor();
  ~UltraLowLatencyProcessor();

  /**
   * Initialize ultra-low latency processor
   * @param targetLatencyMs Target latency in milliseconds
   * @return true if initialization successful
   */
  bool initialize(int targetLatencyMs = 200);

  /**
   * Process audio chunk with ultra-low latency optimizations
   * @param chunk Input audio chunk
   * @param optimizedChunk Output optimized chunk
   * @return true if processing successful
   */
  bool processChunk(const AudioChunk &chunk, AudioChunk &optimizedChunk);

  /**
   * Check if latency target is being met
   * @return true if latency target is met
   */
  bool isLatencyTargetMet() const;

  /**
   * Get current processing latency
   * @return current latency in milliseconds
   */
  float getCurrentLatency() const;

  /**
   * Enable/disable aggressive optimizations
   * @param enabled true to enable aggressive optimizations
   */
  void setAggressiveOptimizations(bool enabled);

private:
  int targetLatencyMs_;
  bool aggressiveOptimizations_;
  std::atomic<float> currentLatency_;
  std::chrono::steady_clock::time_point lastProcessingTime_;

  // Optimization methods
  void applyUltraLowLatencyOptimizations(AudioChunk &chunk);
  void minimizeBuffering(AudioChunk &chunk);
  void optimizeChunkSize(AudioChunk &chunk);
  void prioritizeProcessing(AudioChunk &chunk);
};

/**
 * Intelligent audio chunk reordering with predictive algorithms
 */
class IntelligentChunkReorderer {
public:
  IntelligentChunkReorderer();
  ~IntelligentChunkReorderer();

  /**
   * Initialize intelligent reorderer
   * @param maxBufferSize Maximum buffer size
   * @param predictionWindow Prediction window size
   * @return true if initialization successful
   */
  bool initialize(size_t maxBufferSize = 100, size_t predictionWindow = 10);

  /**
   * Add chunk with intelligent reordering
   * @param chunk Audio chunk to add
   * @param orderedChunks Output vector of ordered chunks
   * @return number of chunks ready for processing
   */
  size_t addChunkIntelligent(const AudioChunk &chunk,
                             std::vector<AudioChunk> &orderedChunks);

  /**
   * Predict missing chunks and request retransmission
   * @param missingChunks Output vector of predicted missing chunks
   * @return number of missing chunks predicted
   */
  size_t predictMissingChunks(std::vector<uint32_t> &missingChunks);

  /**
   * Get reordering efficiency statistics
   * @return map of efficiency statistics
   */
  std::map<std::string, double> getReorderingStats() const;

private:
  size_t maxBufferSize_;
  size_t predictionWindow_;

  // Intelligent reordering state
  std::map<uint32_t, AudioChunk> reorderBuffer_;
  std::vector<uint32_t> sequenceHistory_;
  uint32_t expectedSequence_;

  // Statistics
  std::atomic<uint64_t> totalChunksProcessed_;
  std::atomic<uint64_t> chunksReordered_;
  std::atomic<uint64_t> predictionsCorrect_;
  std::atomic<uint64_t> predictionsMade_;

  // Private methods
  bool predictSequenceGap(uint32_t sequence) const;
  void updateSequenceHistory(uint32_t sequence);
  float calculateReorderingEfficiency() const;
};

} // namespace audio