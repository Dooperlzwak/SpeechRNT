#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <queue>
#include <mutex>
#include <atomic>
#include <chrono>

namespace audio {

// Audio format configuration
struct AudioFormat {
    uint32_t sampleRate;    // 16000 Hz
    uint16_t channels;      // 1 (mono)
    uint16_t bitsPerSample; // 16
    uint32_t chunkSize;     // 1024 samples
    
    AudioFormat() : sampleRate(16000), channels(1), bitsPerSample(16), chunkSize(1024) {}
    
    bool isValid() const {
        return sampleRate > 0 && channels > 0 && bitsPerSample > 0 && chunkSize > 0;
    }
    
    size_t getBytesPerSample() const {
        return bitsPerSample / 8;
    }
    
    size_t getChunkSizeBytes() const {
        return chunkSize * channels * getBytesPerSample();
    }
};

// Audio chunk with metadata
struct AudioChunk {
    std::vector<float> samples;
    std::chrono::steady_clock::time_point timestamp;
    uint32_t sequenceNumber;
    
    AudioChunk() : sequenceNumber(0) {}
    AudioChunk(const std::vector<float>& data, uint32_t seqNum) 
        : samples(data), timestamp(std::chrono::steady_clock::now()), sequenceNumber(seqNum) {}
};

// Audio buffer for continuous streaming
class AudioBuffer {
public:
    explicit AudioBuffer(size_t maxSizeBytes = 1024 * 1024); // 1MB default
    ~AudioBuffer() = default;
    
    // Add audio data (thread-safe)
    bool addChunk(const AudioChunk& chunk);
    bool addRawData(const std::vector<float>& samples);
    
    // Get audio data (thread-safe)
    std::vector<AudioChunk> getChunks(size_t maxChunks = 0);
    std::vector<float> getAllSamples();
    std::vector<float> getRecentSamples(size_t sampleCount);
    
    // Buffer management
    void clear();
    size_t getChunkCount() const;
    size_t getTotalSamples() const;
    size_t getBufferSizeBytes() const;
    bool isFull() const;
    
    // Statistics
    std::chrono::steady_clock::time_point getOldestTimestamp() const;
    std::chrono::steady_clock::time_point getNewestTimestamp() const;
    double getDurationSeconds() const;
    
private:
    mutable std::mutex mutex_;
    std::queue<AudioChunk> chunks_;
    size_t maxSizeBytes_;
    size_t currentSizeBytes_;
    uint32_t nextSequenceNumber_;
    
    void removeOldChunks();
};

// Audio format validator and converter
class AudioProcessor {
public:
    explicit AudioProcessor(const AudioFormat& format);
    ~AudioProcessor() = default;
    
    // Format validation
    bool validateFormat(const AudioFormat& format) const;
    bool validatePCMData(std::string_view data) const;
    
    // PCM conversion
    std::vector<float> convertPCMToFloat(std::string_view pcmData) const;
    std::vector<int16_t> convertFloatToPCM(const std::vector<float>& samples) const;
    
    // Audio processing
    AudioChunk processRawData(std::string_view data);
    std::vector<AudioChunk> processStreamingData(std::string_view data);
    
    // Configuration
    const AudioFormat& getFormat() const { return format_; }
    void setFormat(const AudioFormat& format);
    
    // Statistics
    uint64_t getTotalBytesProcessed() const { return totalBytesProcessed_; }
    uint64_t getTotalChunksProcessed() const { return totalChunksProcessed_; }
    void resetStatistics();
    
private:
    AudioFormat format_;
    std::atomic<uint64_t> totalBytesProcessed_;
    std::atomic<uint64_t> totalChunksProcessed_;
    uint32_t nextSequenceNumber_;
    
    bool validatePCMChunk(std::string_view data) const;
    float convertSampleToFloat(int16_t sample) const;
    int16_t convertSampleToPCM(float sample) const;
};

// Audio ingestion manager for client sessions
class AudioIngestionManager {
public:
    explicit AudioIngestionManager(const std::string& sessionId);
    ~AudioIngestionManager() = default;
    
    // Audio ingestion
    bool ingestAudioData(std::string_view data);
    bool ingestAudioChunk(const AudioChunk& chunk);
    
    // Buffer access
    std::shared_ptr<AudioBuffer> getAudioBuffer() { return audioBuffer_; }
    std::vector<float> getLatestAudio(size_t sampleCount);
    
    // Configuration
    void setAudioFormat(const AudioFormat& format);
    const AudioFormat& getAudioFormat() const;
    
    // State management
    bool isActive() const { return active_; }
    void setActive(bool active) { active_ = active; }
    
    // Statistics and monitoring
    struct Statistics {
        uint64_t totalBytesIngested;
        uint64_t totalChunksIngested;
        uint64_t droppedChunks;
        double averageChunkSize;
        double bufferUtilization;
        std::chrono::steady_clock::time_point lastActivity;
    };
    
    Statistics getStatistics() const;
    void resetStatistics();
    
    // Error handling
    enum class ErrorCode {
        NONE,
        INVALID_FORMAT,
        BUFFER_FULL,
        PROCESSING_ERROR,
        INACTIVE_SESSION
    };
    
    ErrorCode getLastError() const { return lastError_; }
    std::string getErrorMessage() const;
    
private:
    std::string sessionId_;
    std::unique_ptr<AudioProcessor> processor_;
    std::shared_ptr<AudioBuffer> audioBuffer_;
    std::atomic<bool> active_;
    
    // Statistics
    mutable std::mutex statsMutex_;
    Statistics stats_;
    mutable ErrorCode lastError_;
    
    void updateStatistics(size_t bytesProcessed, size_t chunksProcessed);
    void setError(ErrorCode error);
};

} // namespace audio