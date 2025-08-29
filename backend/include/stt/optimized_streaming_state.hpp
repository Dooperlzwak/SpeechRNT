#pragma once

#include "utils/memory_pool.hpp"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <queue>
#include <thread>

namespace stt {

/**
 * Optimized streaming transcription state management
 * Uses memory pools and efficient data structures for better performance
 */
class OptimizedStreamingState {
public:
    /**
     * Configuration for streaming state optimization
     */
    struct OptimizationConfig {
        size_t maxConcurrentUtterances = 50;    // Maximum concurrent utterances
        size_t audioBufferPoolSize = 100;       // Audio buffer pool size
        size_t resultPoolSize = 200;            // Result pool size
        size_t stateCleanupIntervalMs = 5000;   // State cleanup interval
        size_t maxIdleTimeMs = 30000;           // Max idle time before cleanup
        bool enableAsyncProcessing = true;      // Enable async processing
        size_t workerThreadCount = 4;           // Number of worker threads
        
        OptimizationConfig() = default;
    };
    
    /**
     * Streaming utterance state with optimized memory usage
     */
    struct UtteranceState {
        uint32_t utteranceId;
        std::atomic<bool> isActive;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point lastUpdateTime;
        
        // Audio data management
        utils::AudioBufferPool::AudioBufferPtr currentBuffer;
        std::queue<utils::AudioBufferPool::AudioBufferPtr> audioChunks;
        std::atomic<size_t> totalAudioSamples;
        
        // Transcription state
        utils::TranscriptionResultPool::TranscriptionResultPtr lastResult;
        std::atomic<size_t> transcriptionCount;
        std::atomic<double> averageConfidence;
        
        // Performance metrics
        std::atomic<double> averageLatency;
        std::atomic<size_t> processedChunks;
        
        UtteranceState(uint32_t id) 
            : utteranceId(id), isActive(true), totalAudioSamples(0),
              transcriptionCount(0), averageConfidence(0.0),
              averageLatency(0.0), processedChunks(0) {
            startTime = std::chrono::steady_clock::now();
            lastUpdateTime = startTime;
        }
        
        void updateLastActivity() {
            lastUpdateTime = std::chrono::steady_clock::now();
        }
        
        double getIdleTimeSeconds() const {
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastUpdateTime);
            return duration.count() / 1000.0;
        }
        
        size_t getMemoryUsageBytes() const {
            size_t usage = sizeof(UtteranceState);
            
            if (currentBuffer) {
                usage += currentBuffer->data.capacity() * sizeof(float);
            }
            
            // Estimate queue memory usage
            usage += audioChunks.size() * 16000 * sizeof(float); // Rough estimate
            
            return usage;
        }
    };
    
    /**
     * Performance statistics for the streaming state manager
     */
    struct StateStatistics {
        size_t activeUtterances;
        size_t totalUtterances;
        size_t totalMemoryUsageMB;
        size_t peakMemoryUsageMB;
        double averageProcessingLatency;
        size_t totalAudioProcessed;
        size_t cleanupOperations;
        double averageUtteranceDuration;
        
        StateStatistics() : activeUtterances(0), totalUtterances(0),
                           totalMemoryUsageMB(0), peakMemoryUsageMB(0),
                           averageProcessingLatency(0.0), totalAudioProcessed(0),
                           cleanupOperations(0), averageUtteranceDuration(0.0) {}
    };

public:
    explicit OptimizedStreamingState(const OptimizationConfig& config = OptimizationConfig());
    ~OptimizedStreamingState();
    
    // Disable copy constructor and assignment
    OptimizedStreamingState(const OptimizedStreamingState&) = delete;
    OptimizedStreamingState& operator=(const OptimizedStreamingState&) = delete;
    
    /**
     * Initialize the streaming state manager
     */
    bool initialize();
    
    /**
     * Shutdown the streaming state manager
     */
    void shutdown();
    
    /**
     * Utterance lifecycle management
     */
    bool createUtterance(uint32_t utteranceId);
    bool removeUtterance(uint32_t utteranceId);
    bool hasUtterance(uint32_t utteranceId) const;
    std::shared_ptr<UtteranceState> getUtterance(uint32_t utteranceId);
    
    /**
     * Audio data management with memory pooling
     */
    bool addAudioChunk(uint32_t utteranceId, const std::vector<float>& audioData);
    utils::AudioBufferPool::AudioBufferPtr getAudioBuffer(uint32_t utteranceId);
    bool finalizeAudioBuffer(uint32_t utteranceId);
    
    /**
     * Transcription result management
     */
    bool setTranscriptionResult(uint32_t utteranceId, 
                               const std::string& text, 
                               float confidence, 
                               bool isPartial);
    utils::TranscriptionResultPool::TranscriptionResultPtr getLastResult(uint32_t utteranceId);
    
    /**
     * Cleanup and optimization
     */
    void performCleanup();
    void forceCleanup();
    void optimizeMemoryUsage();
    
    /**
     * Statistics and monitoring
     */
    StateStatistics getStatistics() const;
    std::vector<uint32_t> getActiveUtterances() const;
    size_t getUtteranceCount() const;
    
    /**
     * Health checking
     */
    bool isHealthy() const;
    std::string getHealthStatus() const;
    
    /**
     * Configuration management
     */
    void updateConfig(const OptimizationConfig& config);
    const OptimizationConfig& getConfig() const { return config_; }

private:
    // Configuration
    OptimizationConfig config_;
    
    // State management
    bool initialized_;
    std::atomic<bool> shutdownRequested_;
    
    // Memory pools
    std::unique_ptr<utils::AudioBufferPool> audioBufferPool_;
    std::unique_ptr<utils::TranscriptionResultPool> resultPool_;
    
    // Utterance state storage
    mutable std::shared_mutex stateMapMutex_;
    std::unordered_map<uint32_t, std::shared_ptr<UtteranceState>> utteranceStates_;
    
    // Statistics tracking
    mutable std::mutex statsMutex_;
    StateStatistics stats_;
    std::atomic<size_t> peakMemoryUsage_;
    
    // Async processing
    std::vector<std::thread> workerThreads_;
    std::queue<std::function<void()>> taskQueue_;
    std::mutex taskQueueMutex_;
    std::condition_variable taskCondition_;
    
    // Cleanup management
    std::thread cleanupThread_;
    std::chrono::steady_clock::time_point lastCleanupTime_;
    
    // Helper methods
    void workerThreadFunction();
    void cleanupThreadFunction();
    void updateStatistics();
    std::vector<uint32_t> findIdleUtterances() const;
    void removeUtteranceInternal(uint32_t utteranceId);
    void scheduleTask(std::function<void()> task);
};

/**
 * RAII helper for managing utterance lifecycle
 */
class UtteranceHandle {
public:
    UtteranceHandle(OptimizedStreamingState* manager, uint32_t utteranceId)
        : manager_(manager), utteranceId_(utteranceId), valid_(false) {
        if (manager_) {
            valid_ = manager_->createUtterance(utteranceId);
        }
    }
    
    ~UtteranceHandle() {
        if (manager_ && valid_) {
            manager_->removeUtterance(utteranceId_);
        }
    }
    
    // Disable copy constructor and assignment
    UtteranceHandle(const UtteranceHandle&) = delete;
    UtteranceHandle& operator=(const UtteranceHandle&) = delete;
    
    // Enable move constructor and assignment
    UtteranceHandle(UtteranceHandle&& other) noexcept
        : manager_(other.manager_), utteranceId_(other.utteranceId_), valid_(other.valid_) {
        other.manager_ = nullptr;
        other.valid_ = false;
    }
    
    UtteranceHandle& operator=(UtteranceHandle&& other) noexcept {
        if (this != &other) {
            if (manager_ && valid_) {
                manager_->removeUtterance(utteranceId_);
            }
            
            manager_ = other.manager_;
            utteranceId_ = other.utteranceId_;
            valid_ = other.valid_;
            
            other.manager_ = nullptr;
            other.valid_ = false;
        }
        return *this;
    }
    
    bool isValid() const { return valid_; }
    uint32_t getId() const { return utteranceId_; }
    
    std::shared_ptr<OptimizedStreamingState::UtteranceState> getState() {
        return manager_ ? manager_->getUtterance(utteranceId_) : nullptr;
    }

private:
    OptimizedStreamingState* manager_;
    uint32_t utteranceId_;
    bool valid_;
};

} // namespace stt