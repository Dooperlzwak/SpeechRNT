#include "audio/advanced_streaming_optimizer.hpp"
#include "utils/logging.hpp"
#include "utils/performance_monitor.hpp"
#include <algorithm>
#include <numeric>

namespace audio {

AdvancedStreamingOptimizer::AdvancedStreamingOptimizer()
    : initialized_(false), running_(false), ultraLowLatencyMode_(false),
      targetLatencyMs_(200), totalStreamsProcessed_(0), totalJobsSubmitted_(0),
      ultraLowLatencyActivations_(0) {}

AdvancedStreamingOptimizer::~AdvancedStreamingOptimizer() { stop(); }

bool AdvancedStreamingOptimizer::initialize(
    const AdvancedStreamingConfig &config) {
  config_ = config;

  try {
    // Initialize core streaming optimizer
    streamingOptimizer_ = std::make_unique<StreamingOptimizer>();
    if (!streamingOptimizer_->initialize(16000, 1, config.targetLatencyMs)) {
      speechrnt::utils::Logger::error(
          "Failed to initialize streaming optimizer");
      return false;
    }

    // Initialize network monitoring if enabled
    if (config.enableNetworkMonitoring) {
      networkMonitor_ = std::make_unique<NetworkMonitor>();
      if (!networkMonitor_->initialize(config.networkMonitoringIntervalMs)) {
        speechrnt::utils::Logger::error("Failed to initialize network monitor");
        return false;
      }

      networkAdapter_ = std::make_unique<NetworkAwareStreamingAdapter>();
      if (!networkAdapter_->initialize(networkMonitor_)) {
        speechrnt::utils::Logger::error("Failed to initialize network adapter");
        return false;
      }
    }

    // Initialize packet recovery if enabled
    if (config.enablePacketRecovery) {
      packetRecovery_ = std::make_unique<PacketRecoverySystem>();
      std::map<std::string, std::string> recoveryConfig;
      recoveryConfig["packet_timeout_ms"] =
          std::to_string(config.packetTimeoutMs);
      recoveryConfig["max_retries"] = std::to_string(config.maxRetries);

      if (!packetRecovery_->initialize(recoveryConfig)) {
        speechrnt::utils::Logger::error("Failed to initialize packet recovery");
        return false;
      }
    }

    // Initialize quality degradation if enabled
    if (config.enableQualityDegradation) {
      qualityManager_ = std::make_unique<QualityDegradationManager>();
      if (!qualityManager_->initialize(networkMonitor_)) {
        speechrnt::utils::Logger::error("Failed to initialize quality manager");
        return false;
      }

      qualityController_ = std::make_unique<AdaptiveQualityController>();
      if (!qualityController_->initialize(qualityManager_, networkMonitor_)) {
        speechrnt::utils::Logger::error(
            "Failed to initialize quality controller");
        return false;
      }

      // Register quality change callback
      qualityManager_->registerQualityChangeCallback(
          [this](const AudioQualityParams &oldParams,
                 const AudioQualityParams &newParams) {
            onQualityChange(oldParams, newParams);
          });
    }

    // Initialize load balancing if enabled
    if (config.enableLoadBalancing) {
      processingPipeline_ = std::make_unique<LoadBalancedProcessingPipeline>();
      if (!processingPipeline_->initialize(config.numWorkerThreads,
                                           config.maxQueueSize)) {
        speechrnt::utils::Logger::error(
            "Failed to initialize processing pipeline");
        return false;
      }
    }

    // Set target latency and ultra-low latency mode
    targetLatencyMs_ = config.targetLatencyMs;
    ultraLowLatencyMode_ = config.enableUltraLowLatency;

    initialized_ = true;
    speechrnt::utils::Logger::info(
        "AdvancedStreamingOptimizer initialized successfully");

    return true;

  } catch (const std::exception &e) {
    speechrnt::utils::Logger::error(
        "AdvancedStreamingOptimizer initialization failed: " +
        std::string(e.what()));
    return false;
  }
}

bool AdvancedStreamingOptimizer::start() {
  if (!initialized_) {
    speechrnt::utils::Logger::error("Cannot start: optimizer not initialized");
    return false;
  }

  if (running_) {
    speechrnt::utils::Logger::warn(
        "Advanced streaming optimizer already running");
    return true;
  }

  try {
    // Start network monitoring
    if (networkMonitor_ && !networkMonitor_->startMonitoring()) {
      speechrnt::utils::Logger::error("Failed to start network monitoring");
      return false;
    }

    // Start quality controller
    if (qualityController_ && !qualityController_->startAutoControl()) {
      speechrnt::utils::Logger::error("Failed to start quality controller");
      return false;
    }

    // Start processing pipeline
    if (processingPipeline_ && !processingPipeline_->start()) {
      speechrnt::utils::Logger::error("Failed to start processing pipeline");
      return false;
    }

    running_ = true;
    speechrnt::utils::Logger::info(
        "AdvancedStreamingOptimizer started successfully");

    return true;

  } catch (const std::exception &e) {
    speechrnt::utils::Logger::error(
        "AdvancedStreamingOptimizer start failed: " + std::string(e.what()));
    return false;
  }
}

void AdvancedStreamingOptimizer::stop() {
  if (!running_) {
    return;
  }

  running_ = false;

  // Stop all components
  if (networkMonitor_) {
    networkMonitor_->stopMonitoring();
  }

  if (qualityController_) {
    qualityController_->stopAutoControl();
  }

  if (processingPipeline_) {
    processingPipeline_->stop();
  }

  speechrnt::utils::Logger::info("AdvancedStreamingOptimizer stopped");
}

bool AdvancedStreamingOptimizer::processStreamWithOptimizations(
    const std::vector<float> &audioData, uint32_t streamId,
    std::vector<AudioChunk> &outputChunks) {

  if (!running_) {
    speechrnt::utils::Logger::error(
        "Cannot process stream: optimizer not running");
    return false;
  }

  auto startTime = std::chrono::high_resolution_clock::now();

  try {
    // Step 1: Basic streaming optimization
    std::vector<AudioChunk> basicChunks;
    if (!streamingOptimizer_->processStream(audioData, basicChunks)) {
      speechrnt::utils::Logger::error("Basic streaming optimization failed");
      return false;
    }

    // Step 2: Apply packet recovery if enabled
    std::vector<AudioChunk> recoveredChunks;
    if (packetRecovery_) {
      for (const auto &chunk : basicChunks) {
        uint32_t packetId;
        if (packetRecovery_->processOutgoingChunk(chunk, packetId)) {
          AudioChunk chunkCopy = chunk;
          chunkCopy.sequenceNumber = packetId;
          recoveredChunks.push_back(chunkCopy);
        }
      }
    } else {
      recoveredChunks = basicChunks;
    }

    // Step 3: Apply network-aware adaptations
    if (networkAdapter_) {
      AdaptiveStreamingParams params = networkAdapter_->getAdaptiveParams();

      // Adjust chunk parameters based on network conditions
      for (auto &chunk : recoveredChunks) {
        // Apply compression if enabled
        if (params.enableCompression) {
          // Placeholder for compression logic
        }

        // Adjust chunk size based on network conditions
        size_t targetSize = static_cast<size_t>(16000 * params.chunkSizeMs /
                                                1000.0f); // 16kHz sample rate

        if (chunk.data.size() > targetSize) {
          chunk.data.resize(targetSize);
        }
      }
    }

    // Step 4: Apply ultra-low latency optimizations if enabled
    if (ultraLowLatencyMode_.load()) {
      optimizeForUltraLowLatency();
    }

    outputChunks = recoveredChunks;

    auto endTime = std::chrono::high_resolution_clock::now();
    float processingTime =
        std::chrono::duration_cast<std::chrono::microseconds>(endTime -
                                                              startTime)
            .count() /
        1000.0f; // Convert to milliseconds

    // Update performance metrics
    updatePerformanceMetrics();

    // Check if latency target is met
    if (!checkLatencyTarget(processingTime)) {
      adjustOptimizationParameters();
    }

    totalStreamsProcessed_++;

    // Record performance metrics
    speechrnt::utils::PerformanceMonitor::getInstance().recordLatency(
        "advanced_streaming.processing_latency_ms", processingTime);
    speechrnt::utils::PerformanceMonitor::getInstance().recordThroughput(
        "advanced_streaming.throughput_chunks_per_sec",
        outputChunks.size() / (processingTime / 1000.0f));

    return true;

  } catch (const std::exception &e) {
    speechrnt::utils::Logger::error("Stream processing failed: " +
                                    std::string(e.what()));
    return false;
  }
}

uint64_t
AdvancedStreamingOptimizer::submitRealTimeJob(std::function<void()> task,
                                              ProcessingPriority priority) {
  if (!processingPipeline_) {
    speechrnt::utils::Logger::error(
        "Cannot submit job: processing pipeline not available");
    return 0;
  }

  uint64_t jobId =
      processingPipeline_->submitRealTimeJob(task, "Real-time streaming job");
  if (jobId > 0) {
    totalJobsSubmitted_++;
  }

  return jobId;
}

uint64_t
AdvancedStreamingOptimizer::submitBatchJob(std::function<void()> task,
                                           ProcessingPriority priority) {
  if (!processingPipeline_) {
    speechrnt::utils::Logger::error(
        "Cannot submit job: processing pipeline not available");
    return 0;
  }

  uint64_t jobId =
      processingPipeline_->submitBatchJob(task, "Batch processing job");
  if (jobId > 0) {
    totalJobsSubmitted_++;
  }

  return jobId;
}

StreamingPerformanceMetrics
AdvancedStreamingOptimizer::getPerformanceMetrics() const {
  std::lock_guard<std::mutex> lock(metricsMutex_);
  return currentMetrics_;
}

bool AdvancedStreamingOptimizer::isUltraLowLatencyActive() const {
  return ultraLowLatencyMode_.load();
}

void AdvancedStreamingOptimizer::setUltraLowLatencyMode(bool enabled) {
  bool wasEnabled = ultraLowLatencyMode_.exchange(enabled);

  if (enabled && !wasEnabled) {
    ultraLowLatencyActivations_++;
    optimizeForUltraLowLatency();
    speechrnt::utils::Logger::info("Ultra-low latency mode enabled");
  } else if (!enabled && wasEnabled) {
    speechrnt::utils::Logger::info("Ultra-low latency mode disabled");
  }
}

void AdvancedStreamingOptimizer::setTargetLatency(int latencyMs) {
  targetLatencyMs_ = latencyMs;

  if (streamingOptimizer_) {
    streamingOptimizer_->initialize(16000, 1, latencyMs);
  }

  speechrnt::utils::Logger::info("Target latency set to " +
                                 std::to_string(latencyMs) + "ms");
}

AdaptiveStreamingParams
AdvancedStreamingOptimizer::getCurrentStreamingParams() const {
  if (networkAdapter_) {
    return networkAdapter_->getAdaptiveParams();
  }
  return AdaptiveStreamingParams();
}

void AdvancedStreamingOptimizer::registerMetricsCallback(
    std::function<void(const StreamingPerformanceMetrics &)> callback) {
  std::lock_guard<std::mutex> lock(metricsMutex_);
  metricsCallbacks_.push_back(callback);
}

std::map<std::string, double>
AdvancedStreamingOptimizer::getOptimizationStats() const {
  std::map<std::string, double> stats;

  // Core statistics
  stats["total_streams_processed"] =
      static_cast<double>(totalStreamsProcessed_.load());
  stats["total_jobs_submitted"] =
      static_cast<double>(totalJobsSubmitted_.load());
  stats["ultra_low_latency_activations"] =
      static_cast<double>(ultraLowLatencyActivations_.load());
  stats["target_latency_ms"] = static_cast<double>(targetLatencyMs_);
  stats["ultra_low_latency_active"] = ultraLowLatencyMode_.load() ? 1.0 : 0.0;
  stats["initialized"] = initialized_ ? 1.0 : 0.0;
  stats["running"] = running_ ? 1.0 : 0.0;

  // Component statistics
  if (streamingOptimizer_) {
    auto streamingStats = streamingOptimizer_->getStreamingStats();
    for (const auto &[key, value] : streamingStats) {
      stats["streaming." + key] = value;
    }
  }

  if (networkMonitor_) {
    auto networkStats = networkMonitor_->getMonitoringStats();
    for (const auto &[key, value] : networkStats) {
      stats["network." + key] = value;
    }
  }

  if (networkAdapter_) {
    auto adapterStats = networkAdapter_->getAdaptationStats();
    for (const auto &[key, value] : adapterStats) {
      stats["adapter." + key] = value;
    }
  }

  if (packetRecovery_) {
    auto recoveryStats = packetRecovery_->getRecoveryStats();
    for (const auto &[key, value] : recoveryStats) {
      stats["recovery." + key] = value;
    }
  }

  if (qualityManager_) {
    auto qualityStats = qualityManager_->getDegradationStats();
    for (const auto &[key, value] : qualityStats) {
      stats["quality." + key] = value;
    }
  }

  if (qualityController_) {
    auto controlStats = qualityController_->getControlStats();
    for (const auto &[key, value] : controlStats) {
      stats["control." + key] = value;
    }
  }

  if (processingPipeline_) {
    auto pipelineStats = processingPipeline_->getPipelineStats();
    for (const auto &[key, value] : pipelineStats) {
      stats["pipeline." + key] = value;
    }
  }

  return stats;
}

bool AdvancedStreamingOptimizer::isHealthy() const {
  if (!initialized_ || !running_) {
    return false;
  }

  // Check component health
  bool networkHealthy = !networkMonitor_ || networkMonitor_->isNetworkStable();
  bool pipelineHealthy =
      !processingPipeline_ || processingPipeline_->isHealthy();

  // Check performance metrics
  StreamingPerformanceMetrics metrics = getPerformanceMetrics();
  bool latencyHealthy =
      metrics.endToEndLatencyMs <= (targetLatencyMs_ * 1.5f); // 50% tolerance
  bool resourcesHealthy = metrics.cpuUsage < 0.9f && metrics.memoryUsage < 0.9f;

  return networkHealthy && pipelineHealthy && latencyHealthy &&
         resourcesHealthy;
}

void AdvancedStreamingOptimizer::performManualAdjustment(
    const std::string &adjustmentType, float value) {
  if (adjustmentType == "target_latency") {
    setTargetLatency(static_cast<int>(value));
  } else if (adjustmentType == "ultra_low_latency") {
    setUltraLowLatencyMode(value > 0.5f);
  } else if (adjustmentType == "quality_level" && qualityManager_) {
    AudioQualityLevel level =
        static_cast<AudioQualityLevel>(static_cast<int>(value));
    qualityManager_->setTargetQualityLevel(level);
  } else if (adjustmentType == "adaptive_chunking" && streamingOptimizer_) {
    streamingOptimizer_->setAdaptiveChunking(value > 0.5f);
  } else {
    speechrnt::utils::Logger::warn("Unknown adjustment type: " +
                                   adjustmentType);
  }
}

void AdvancedStreamingOptimizer::updatePerformanceMetrics() {
  std::lock_guard<std::mutex> lock(metricsMutex_);

  currentMetrics_.timestamp = std::chrono::steady_clock::now();

  // Update network metrics
  if (networkMonitor_) {
    NetworkMetrics networkMetrics = networkMonitor_->getCurrentMetrics();
    currentMetrics_.networkLatencyMs = networkMetrics.latencyMs;
    currentMetrics_.packetLossRate = networkMetrics.packetLossRate;
  }

  // Update processing metrics
  if (processingPipeline_) {
    ProcessingStats processingStats = processingPipeline_->getProcessingStats();
    currentMetrics_.processingLatencyMs = processingStats.averageProcessingTime;
    currentMetrics_.queueLatencyMs = processingStats.averageQueueTime;
    currentMetrics_.queuedJobs = processingStats.totalJobsQueued;

    SystemResources resources = processingPipeline_->getCurrentResources();
    currentMetrics_.cpuUsage = resources.cpuUsage;
    currentMetrics_.memoryUsage = resources.memoryUsage;
    currentMetrics_.activeStreams = resources.activeThreads;
  }

  // Update quality metrics
  if (qualityManager_) {
    AudioQualityParams qualityParams =
        qualityManager_->getCurrentQualityParams();
    currentMetrics_.currentQuality = qualityParams.level;
  }

  // Calculate end-to-end latency
  currentMetrics_.endToEndLatencyMs = currentMetrics_.networkLatencyMs +
                                      currentMetrics_.processingLatencyMs +
                                      currentMetrics_.queueLatencyMs;

  // Update ultra-low latency status
  currentMetrics_.ultraLowLatencyActive = ultraLowLatencyMode_.load();

  // Notify callbacks
  notifyMetricsUpdate(currentMetrics_);
}

void AdvancedStreamingOptimizer::onNetworkConditionChange(
    const NetworkMetrics &metrics, NetworkQuality quality) {
  speechrnt::utils::Logger::debug(
      "Network condition changed: quality=" +
      std::to_string(static_cast<int>(quality)) +
      ", latency=" + std::to_string(metrics.latencyMs) + "ms");

  // Update packet recovery parameters
  if (packetRecovery_) {
    packetRecovery_->updateRecoveryParams(metrics.packetLossRate,
                                          metrics.latencyMs, metrics.jitterMs);
  }

  // Trigger metrics update
  updatePerformanceMetrics();
}

void AdvancedStreamingOptimizer::onQualityChange(
    const AudioQualityParams &oldParams, const AudioQualityParams &newParams) {
  speechrnt::utils::Logger::debug(
      "Quality changed from level " +
      std::to_string(static_cast<int>(oldParams.level)) + " to " +
      std::to_string(static_cast<int>(newParams.level)));

  // Update streaming optimizer parameters
  if (streamingOptimizer_) {
    // Adjust chunk size based on quality
    size_t newChunkSize = static_cast<size_t>(newParams.sampleRate *
                                              newParams.bufferSizeMs / 1000.0f);
    // Note: StreamingOptimizer doesn't have a direct setChunkSize method,
    // so we would need to reinitialize or add that method
  }

  // Trigger metrics update
  updatePerformanceMetrics();
}

void AdvancedStreamingOptimizer::optimizeForUltraLowLatency() {
  // Aggressive optimizations for ultra-low latency

  // Reduce buffer sizes
  if (streamingOptimizer_) {
    streamingOptimizer_->setAdaptiveChunking(true);
    // Set smaller chunk overlap for lower latency
    streamingOptimizer_->setChunkOverlap(0);
  }

  // Prioritize real-time jobs
  if (processingPipeline_) {
    processingPipeline_->setJobTypePriority(ProcessingJobType::REAL_TIME_STREAM,
                                            ProcessingPriority::CRITICAL);
  }

  // Reduce quality if necessary
  if (qualityManager_) {
    qualityManager_->setAdjustmentAggressiveness(
        1.0f); // Maximum aggressiveness
  }

  speechrnt::utils::Logger::info("Applied ultra-low latency optimizations");
}

void AdvancedStreamingOptimizer::notifyMetricsUpdate(
    const StreamingPerformanceMetrics &metrics) {
  for (const auto &callback : metricsCallbacks_) {
    try {
      callback(metrics);
    } catch (const std::exception &e) {
      speechrnt::utils::Logger::error("Metrics callback error: " +
                                      std::string(e.what()));
    }
  }
}

bool AdvancedStreamingOptimizer::checkLatencyTarget(
    float currentLatency) const {
  return currentLatency <= targetLatencyMs_;
}

void AdvancedStreamingOptimizer::adjustOptimizationParameters() {
  // Automatically adjust parameters when latency target is not met

  if (ultraLowLatencyMode_.load()) {
    // Already in ultra-low latency mode, try more aggressive optimizations
    if (qualityManager_) {
      // Force quality degradation
      qualityManager_->applyResourceBasedDegradation(
          0.9f, 0.9f, static_cast<float>(targetLatencyMs_ * 2));
    }
  } else {
    // Enable ultra-low latency mode
    setUltraLowLatencyMode(true);
  }

  speechrnt::utils::Logger::info(
      "Adjusted optimization parameters due to latency target miss");
}

UltraLowLatencyProcessor::UltraLowLatencyProcessor()
    : targetLatencyMs_(200), aggressiveOptimizations_(false),
      currentLatency_(0.0f) {}

UltraLowLatencyProcessor::~UltraLowLatencyProcessor() {}

bool UltraLowLatencyProcessor::initialize(int targetLatencyMs) {
  targetLatencyMs_ = targetLatencyMs;
  currentLatency_ = 0.0f;
  lastProcessingTime_ = std::chrono::steady_clock::now();

  speechrnt::utils::Logger::info("UltraLowLatencyProcessor initialized with " +
                                 std::to_string(targetLatencyMs) + "ms target");

  return true;
}

bool UltraLowLatencyProcessor::processChunk(const AudioChunk &chunk,
                                            AudioChunk &optimizedChunk) {
  auto startTime = std::chrono::high_resolution_clock::now();

  optimizedChunk = chunk;

  // Apply ultra-low latency optimizations
  applyUltraLowLatencyOptimizations(optimizedChunk);

  auto endTime = std::chrono::high_resolution_clock::now();
  float processingTime =
      std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)
          .count() /
      1000.0f; // Convert to milliseconds

  currentLatency_ = processingTime;
  lastProcessingTime_ = endTime;

  return processingTime <= targetLatencyMs_;
}

bool UltraLowLatencyProcessor::isLatencyTargetMet() const {
  return currentLatency_.load() <= targetLatencyMs_;
}

float UltraLowLatencyProcessor::getCurrentLatency() const {
  return currentLatency_.load();
}

void UltraLowLatencyProcessor::setAggressiveOptimizations(bool enabled) {
  aggressiveOptimizations_ = enabled;
  speechrnt::utils::Logger::info("Aggressive optimizations " +
                                 std::string(enabled ? "enabled" : "disabled"));
}

void UltraLowLatencyProcessor::applyUltraLowLatencyOptimizations(
    AudioChunk &chunk) {
  // Minimize buffering
  minimizeBuffering(chunk);

  // Optimize chunk size
  optimizeChunkSize(chunk);

  // Prioritize processing
  prioritizeProcessing(chunk);
}

void UltraLowLatencyProcessor::minimizeBuffering(AudioChunk &chunk) {
  // Reduce chunk size for minimal buffering
  if (aggressiveOptimizations_ && chunk.data.size() > 256) {
    chunk.data.resize(256); // Minimum viable chunk size
  }
}

void UltraLowLatencyProcessor::optimizeChunkSize(AudioChunk &chunk) {
  // Calculate optimal chunk size based on target latency
  size_t optimalSize = static_cast<size_t>(16000 * targetLatencyMs_ / 1000.0f /
                                           4); // Quarter of target

  if (chunk.data.size() > optimalSize) {
    chunk.data.resize(optimalSize);
  }
}

void UltraLowLatencyProcessor::prioritizeProcessing(AudioChunk &chunk) {
  // Mark chunk for high priority processing
  // This would typically involve setting thread priorities or using real-time
  // scheduling For now, we just ensure the timestamp is current
  chunk.timestamp = std::chrono::steady_clock::now();
}

IntelligentChunkReorderer::IntelligentChunkReorderer()
    : maxBufferSize_(100), predictionWindow_(10), expectedSequence_(0),
      totalChunksProcessed_(0), chunksReordered_(0), predictionsCorrect_(0),
      predictionsMade_(0) {}

IntelligentChunkReorderer::~IntelligentChunkReorderer() {}

bool IntelligentChunkReorderer::initialize(size_t maxBufferSize,
                                           size_t predictionWindow) {
  maxBufferSize_ = maxBufferSize;
  predictionWindow_ = predictionWindow;

  sequenceHistory_.reserve(predictionWindow * 2);

  speechrnt::utils::Logger::info(
      "IntelligentChunkReorderer initialized: buffer=" +
      std::to_string(maxBufferSize) +
      ", prediction=" + std::to_string(predictionWindow));

  return true;
}

size_t IntelligentChunkReorderer::addChunkIntelligent(
    const AudioChunk &chunk, std::vector<AudioChunk> &orderedChunks) {
  orderedChunks.clear();

  totalChunksProcessed_++;

  // Add chunk to buffer
  reorderBuffer_[chunk.sequenceNumber] = chunk;

  // Update sequence history for prediction
  updateSequenceHistory(chunk.sequenceNumber);

  // Check if chunk is out of order
  if (chunk.sequenceNumber != expectedSequence_) {
    chunksReordered_++;
  }

  // Extract ordered chunks
  while (reorderBuffer_.find(expectedSequence_) != reorderBuffer_.end()) {
    orderedChunks.push_back(reorderBuffer_[expectedSequence_]);
    reorderBuffer_.erase(expectedSequence_);
    expectedSequence_++;
  }

  // Cleanup buffer if it gets too large
  if (reorderBuffer_.size() > maxBufferSize_) {
    // Remove oldest chunks
    auto oldestIt = reorderBuffer_.begin();
    for (auto it = reorderBuffer_.begin(); it != reorderBuffer_.end(); ++it) {
      if (it->second.timestamp < oldestIt->second.timestamp) {
        oldestIt = it;
      }
    }
    reorderBuffer_.erase(oldestIt);
  }

  return orderedChunks.size();
}

size_t IntelligentChunkReorderer::predictMissingChunks(
    std::vector<uint32_t> &missingChunks) {
  missingChunks.clear();

  if (sequenceHistory_.size() < predictionWindow_) {
    return 0; // Not enough history for prediction
  }

  // Analyze sequence patterns to predict missing chunks
  for (uint32_t seq = expectedSequence_;
       seq < expectedSequence_ + predictionWindow_; ++seq) {
    if (reorderBuffer_.find(seq) == reorderBuffer_.end()) {
      if (predictSequenceGap(seq)) {
        missingChunks.push_back(seq);
        predictionsMade_++;
      }
    }
  }

  return missingChunks.size();
}

std::map<std::string, double>
IntelligentChunkReorderer::getReorderingStats() const {
  std::map<std::string, double> stats;

  stats["total_chunks_processed"] =
      static_cast<double>(totalChunksProcessed_.load());
  stats["chunks_reordered"] = static_cast<double>(chunksReordered_.load());
  stats["predictions_made"] = static_cast<double>(predictionsMade_.load());
  stats["predictions_correct"] =
      static_cast<double>(predictionsCorrect_.load());
  stats["current_buffer_size"] = static_cast<double>(reorderBuffer_.size());
  stats["expected_sequence"] = static_cast<double>(expectedSequence_);
  stats["reordering_efficiency"] = calculateReorderingEfficiency();

  if (predictionsMade_.load() > 0) {
    stats["prediction_accuracy"] =
        static_cast<double>(predictionsCorrect_.load()) /
        static_cast<double>(predictionsMade_.load());
  } else {
    stats["prediction_accuracy"] = 0.0;
  }

  return stats;
}

bool IntelligentChunkReorderer::predictSequenceGap(uint32_t sequence) const {
  // Simple prediction based on sequence history patterns
  if (sequenceHistory_.size() < 3) {
    return false;
  }

  // Check if this sequence number fits the expected pattern
  // This is a simplified prediction algorithm
  uint32_t lastSeq = sequenceHistory_.back();
  uint32_t secondLastSeq = sequenceHistory_[sequenceHistory_.size() - 2];

  uint32_t expectedGap = lastSeq - secondLastSeq;
  uint32_t actualGap = sequence - lastSeq;

  // Predict gap if it's significantly larger than expected
  return actualGap > expectedGap * 2;
}

void IntelligentChunkReorderer::updateSequenceHistory(uint32_t sequence) {
  sequenceHistory_.push_back(sequence);

  // Keep only recent history
  if (sequenceHistory_.size() > predictionWindow_ * 2) {
    sequenceHistory_.erase(sequenceHistory_.begin());
  }
}

float IntelligentChunkReorderer::calculateReorderingEfficiency() const {
  if (totalChunksProcessed_.load() == 0) {
    return 1.0f;
  }

  // Efficiency is the percentage of chunks that didn't need reordering
  uint64_t chunksInOrder =
      totalChunksProcessed_.load() - chunksReordered_.load();
  return static_cast<float>(chunksInOrder) /
         static_cast<float>(totalChunksProcessed_.load());
}

} // namespace audio