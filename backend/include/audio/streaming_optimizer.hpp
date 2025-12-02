#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace audio {

/**
 * Audio chunk for streaming processing
 */
struct AudioChunk {
  std::vector<float> data;
  std::chrono::steady_clock::time_point timestamp;
  uint32_t sequenceNumber;
  bool isLast;

  AudioChunk() : sequenceNumber(0), isLast(false) {}
  AudioChunk(const std::vector<float> &audioData, uint32_t seqNum = 0)
      : data(audioData), timestamp(std::chrono::steady_clock::now()),
        sequenceNumber(seqNum), isLast(false) {}
};

/**
 * Streaming audio buffer with optimized memory management
 */
class StreamingAudioBuffer {
public:
  StreamingAudioBuffer(size_t maxChunks = 100, size_t chunkSizeHint = 1024);
  ~StreamingAudioBuffer();

  /**
   * Add audio chunk to buffer
   * @param chunk Audio chunk to add
   * @return true if added successfully
   */
  bool addChunk(const AudioChunk &chunk);

  /**
   * Get next available chunk
   * @param chunk Output chunk
   * @return true if chunk available
   */
  bool getNextChunk(AudioChunk &chunk);

  /**
   * Get multiple chunks at once
   * @param chunks Output vector of chunks
   * @param maxChunks Maximum number of chunks to retrieve
   * @return number of chunks retrieved
   */
  size_t getChunks(std::vector<AudioChunk> &chunks, size_t maxChunks = 10);

  /**
   * Peek at next chunk without removing it
   * @param chunk Output chunk
   * @return true if chunk available
   */
  bool peekNextChunk(AudioChunk &chunk) const;

  /**
   * Get current buffer size
   * @return number of chunks in buffer
   */
  size_t size() const;

  /**
   * Check if buffer is empty
   * @return true if empty
   */
  bool empty() const;

  /**
   * Clear all chunks from buffer
   */
  void clear();

  /**
   * Set maximum buffer size
   * @param maxChunks Maximum number of chunks
   */
  void setMaxSize(size_t maxChunks);

  /**
   * Get buffer utilization percentage
   * @return utilization (0-100)
   */
  float getUtilization() const;

private:
  mutable std::mutex mutex_;
  std::queue<AudioChunk> buffer_;
  size_t maxChunks_;
  size_t chunkSizeHint_;
  std::atomic<uint32_t> nextSequenceNumber_;
};

/**
 * Audio streaming optimizer for low-latency processing
 */
class StreamingOptimizer {
public:
  StreamingOptimizer();
  ~StreamingOptimizer();

  /**
   * Initialize streaming optimizer
   * @param sampleRate Audio sample rate
   * @param channels Number of audio channels
   * @param targetLatencyMs Target latency in milliseconds
   * @return true if initialization successful
   */
  bool initialize(int sampleRate = 16000, int channels = 1,
                  int targetLatencyMs = 50);

  /**
   * Process audio stream with optimized chunking
   * @param audioData Input audio data
   * @param outputChunks Output optimized chunks
   * @return true if processing successful
   */
  bool processStream(const std::vector<float> &audioData,
                     std::vector<AudioChunk> &outputChunks);

  /**
   * Optimize chunk size based on current performance
   * @param currentLatencyMs Current processing latency
   * @param targetLatencyMs Target latency
   * @return optimized chunk size
   */
  size_t optimizeChunkSize(float currentLatencyMs, float targetLatencyMs);

  /**
   * Enable/disable adaptive chunking
   * @param enabled true to enable adaptive chunking
   */
  void setAdaptiveChunking(bool enabled);

  /**
   * Set chunk overlap for better continuity
   * @param overlapSamples Number of samples to overlap
   */
  void setChunkOverlap(size_t overlapSamples);

  /**
   * Apply audio preprocessing optimizations
   * @param audioData Input/output audio data
   * @return true if preprocessing successful
   */
  bool preprocessAudio(std::vector<float> &audioData);

  /**
   * Get current streaming statistics
   * @return map of performance metrics
   */
  std::map<std::string, double> getStreamingStats() const;

  /**
   * Reset streaming statistics
   */
  void resetStats();

  /**
   * Get recommended buffer size for current settings
   * @return recommended buffer size in samples
   */
  size_t getRecommendedBufferSize() const;

private:
  // Configuration
  int sampleRate_;
  int channels_;
  int targetLatencyMs_;
  bool adaptiveChunking_;
  size_t chunkOverlap_;

  // Optimization state
  size_t currentChunkSize_;
  size_t minChunkSize_;
  size_t maxChunkSize_;
  std::vector<float> overlapBuffer_;

  // Statistics
  mutable std::mutex statsMutex_;
  std::atomic<uint64_t> totalChunksProcessed_;
  std::atomic<uint64_t> totalSamplesProcessed_;
  std::atomic<double> averageLatencyMs_;
  std::atomic<double> averageThroughput_;
  std::chrono::steady_clock::time_point lastStatsUpdate_;

  // Private methods
  size_t calculateOptimalChunkSize(float latencyMs) const;
  void updateStats(size_t samplesProcessed, double latencyMs);
  bool validateAudioData(const std::vector<float> &audioData) const;
  void applyWindowFunction(std::vector<float> &chunk) const;
};

/**
 * WebSocket message optimizer for audio streaming
 */
class WebSocketOptimizer {
public:
  WebSocketOptimizer();
  ~WebSocketOptimizer();

  /**
   * Initialize WebSocket optimizer
   * @param maxMessageSize Maximum message size in bytes
   * @param compressionEnabled Enable message compression
   * @return true if initialization successful
   */
  bool initialize(size_t maxMessageSize = 65536,
                  bool compressionEnabled = true);

  /**
   * Optimize audio data for WebSocket transmission
   * @param audioData Input audio data
   * @param optimizedMessages Output optimized messages
   * @return true if optimization successful
   */
  bool
  optimizeForTransmission(const std::vector<float> &audioData,
                          std::vector<std::vector<uint8_t>> &optimizedMessages);

  /**
   * Batch multiple audio chunks for efficient transmission
   * @param chunks Input audio chunks
   * @param batchedMessages Output batched messages
   * @return true if batching successful
   */
  bool batchChunks(const std::vector<AudioChunk> &chunks,
                   std::vector<std::vector<uint8_t>> &batchedMessages);

  /**
   * Enable/disable message compression
   * @param enabled true to enable compression
   */
  void setCompressionEnabled(bool enabled);

  /**
   * Set maximum message size
   * @param maxSize Maximum message size in bytes
   */
  void setMaxMessageSize(size_t maxSize);

  /**
   * Get transmission statistics
   * @return map of transmission metrics
   */
  std::map<std::string, double> getTransmissionStats() const;

private:
  size_t maxMessageSize_;
  bool compressionEnabled_;

  // Statistics
  std::atomic<uint64_t> totalMessagesOptimized_;
  std::atomic<uint64_t> totalBytesTransmitted_;
  std::atomic<uint64_t> totalBytesCompressed_;

  // Private methods
  std::vector<uint8_t> compressData(const std::vector<uint8_t> &data) const;
  std::vector<uint8_t> serializeAudioChunk(const AudioChunk &chunk) const;
  bool shouldBatchChunks(const std::vector<AudioChunk> &chunks) const;
};

} // namespace audio