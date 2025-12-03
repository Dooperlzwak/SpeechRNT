#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace audio {

/**
 * AudioBufferManager - Efficient audio buffer management for streaming
 * transcription
 *
 * This class manages per-utterance audio buffers for streaming STT processing.
 * It provides memory-bounded circular buffers, thread-safe operations, and
 * automatic cleanup to prevent memory leaks.
 *
 * Requirements addressed: 2.1, 2.2, 4.1, 4.2, 4.3
 */
class AudioBufferManager {
public:
  /**
   * Configuration for buffer management
   */
  struct BufferConfig {
    size_t maxBufferSizeMB = 16;     // Maximum buffer size per utterance in MB
    size_t maxUtterances = 10;       // Maximum concurrent utterances
    size_t cleanupIntervalMs = 5000; // Cleanup interval in milliseconds
    size_t maxIdleTimeMs =
        30000; // Max idle time before cleanup in milliseconds
    bool enableCircularBuffer = true; // Enable circular buffer behavior

    BufferConfig() = default;
  };

  /**
   * Per-utterance buffer information
   */
  struct UtteranceBuffer {
    std::vector<float> audioData;
    std::chrono::steady_clock::time_point startTime;
    mutable std::chrono::steady_clock::time_point lastAccessTime;
    size_t maxSizeSamples;
    size_t writePosition;
    bool isActive;
    bool isCircular;

    UtteranceBuffer();
    explicit UtteranceBuffer(size_t maxSamples, bool circular = true);

    // Buffer operations
    bool addAudioData(const std::vector<float> &audio);
    std::vector<float> getAudioData() const;
    std::vector<float> getRecentAudioData(size_t sampleCount) const;
    void clear();

    // Status
    size_t getCurrentSamples() const { return audioData.size(); }
    size_t getMaxSamples() const { return maxSizeSamples; }
    bool isFull() const;
    double getDurationSeconds(uint32_t sampleRate = 16000) const;
    size_t getMemoryUsageBytes() const;
  };

  /**
   * Buffer statistics for monitoring
   */
  struct BufferStatistics {
    size_t totalUtterances;
    size_t activeUtterances;
    size_t totalMemoryUsageMB;
    size_t peakMemoryUsageMB;
    size_t totalAudioSamples;
    size_t droppedSamples;
    double averageBufferUtilization;
    std::chrono::steady_clock::time_point lastCleanupTime;

    BufferStatistics();
  };

public:
  /**
   * Constructor with configuration
   */
  explicit AudioBufferManager(const BufferConfig &config);
  AudioBufferManager();

  /**
   * Destructor - ensures proper cleanup
   */
  ~AudioBufferManager();

  // Disable copy constructor and assignment
  AudioBufferManager(const AudioBufferManager &) = delete;
  AudioBufferManager &operator=(const AudioBufferManager &) = delete;

  /**
   * Audio data management (thread-safe)
   */
  bool addAudioData(uint32_t utteranceId, const std::vector<float> &audio);
  std::vector<float> getBufferedAudio(uint32_t utteranceId);
  std::vector<float> getRecentAudio(uint32_t utteranceId, size_t sampleCount);
  bool hasUtterance(uint32_t utteranceId) const;

  /**
   * Utterance lifecycle management
   */
  bool createUtterance(uint32_t utteranceId, size_t maxSizeMB = 0);
  void finalizeBuffer(uint32_t utteranceId);
  void removeUtterance(uint32_t utteranceId);
  void setUtteranceActive(uint32_t utteranceId, bool active);
  bool isUtteranceActive(uint32_t utteranceId) const;

  /**
   * Memory management and cleanup
   */
  void cleanupOldBuffers();
  void cleanupInactiveBuffers();
  void forceCleanup();
  size_t getCurrentMemoryUsage() const;
  size_t getCurrentMemoryUsageMB() const;

  /**
   * Configuration management
   */
  void updateConfig(const BufferConfig &config);
  const BufferConfig &getConfig() const { return config_; }

  /**
   * Statistics and monitoring
   */
  BufferStatistics getStatistics() const;
  void resetStatistics();
  std::vector<uint32_t> getActiveUtterances() const;
  size_t getUtteranceCount() const;

  /**
   * Health checking
   */
  bool isHealthy() const;
  std::string getHealthStatus() const;

private:
  // Configuration
  BufferConfig config_;

  // Buffer storage (thread-safe)
  mutable std::mutex bufferMutex_;
  std::unordered_map<uint32_t, std::unique_ptr<UtteranceBuffer>>
      utteranceBuffers_;

  // Statistics tracking
  mutable std::mutex statsMutex_;
  BufferStatistics stats_;
  std::atomic<size_t> peakMemoryUsage_;
  std::atomic<size_t> totalDroppedSamples_;

  // Cleanup management
  std::chrono::steady_clock::time_point lastCleanupTime_;

  // Internal helper methods
  bool shouldCleanup() const;
  void performCleanup();
  size_t calculateMaxSamples(size_t maxSizeMB) const;
  void updateStatistics();
  bool isMemoryLimitExceeded() const;
  std::vector<uint32_t> findOldestUtterances(size_t count) const;
  void removeUtteranceInternal(uint32_t utteranceId);
};

} // namespace audio