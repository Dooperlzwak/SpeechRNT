#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <queue>
#include <unordered_set>
#include <chrono>
#include <cstdint>

namespace utils {

/**
 * Generic memory pool for efficient allocation and deallocation
 * Reduces memory fragmentation and allocation overhead
 */
template<typename T>
class MemoryPool {
public:
    struct PoolStatistics {
        size_t totalAllocated;
        size_t totalInUse;
        size_t totalFree;
        size_t peakUsage;
        size_t allocationCount;
        size_t deallocationCount;
        
        PoolStatistics() : totalAllocated(0), totalInUse(0), totalFree(0), 
                          peakUsage(0), allocationCount(0), deallocationCount(0) {}
    };

private:
    struct PoolBlock {
        std::unique_ptr<T> data;
        std::chrono::steady_clock::time_point lastUsed;
        bool inUse;
        
        PoolBlock() : inUse(false) {
            lastUsed = std::chrono::steady_clock::now();
        }
    };

public:
    explicit MemoryPool(size_t initialSize = 10, size_t maxSize = 1000)
        : maxSize_(maxSize), peakUsage_(0) {
        
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.reserve(maxSize);
        
        // Pre-allocate initial blocks
        for (size_t i = 0; i < initialSize; ++i) {
            auto block = std::make_unique<PoolBlock>();
            block->data = std::make_unique<T>();
            freeBlocks_.push(block.get());
            pool_.push_back(std::move(block));
        }
    }
    
    ~MemoryPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.clear();
        while (!freeBlocks_.empty()) {
            freeBlocks_.pop();
        }
        inUseBlocks_.clear();
    }
    
    // Disable copy constructor and assignment
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    
    /**
     * Acquire an object from the pool
     */
    std::shared_ptr<T> acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        PoolBlock* block = nullptr;
        
        if (!freeBlocks_.empty()) {
            // Reuse existing block
            block = freeBlocks_.front();
            freeBlocks_.pop();
        } else if (pool_.size() < maxSize_) {
            // Create new block
            auto newBlock = std::make_unique<PoolBlock>();
            newBlock->data = std::make_unique<T>();
            block = newBlock.get();
            pool_.push_back(std::move(newBlock));
        } else {
            // Pool is full, return nullptr or throw
            return nullptr;
        }
        
        if (block) {
            block->inUse = true;
            block->lastUsed = std::chrono::steady_clock::now();
            inUseBlocks_.insert(block);
            
            stats_.allocationCount++;
            stats_.totalInUse++;
            
            if (stats_.totalInUse > peakUsage_) {
                peakUsage_ = stats_.totalInUse;
            }
            
            // Return shared_ptr with custom deleter that returns to pool
            return std::shared_ptr<T>(block->data.get(), 
                [this, block](T*) { this->release(block); });
        }
        
        return nullptr;
    }
    
    /**
     * Get pool statistics
     */
    PoolStatistics getStatistics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        PoolStatistics stats = stats_;
        stats.totalAllocated = pool_.size();
        stats.totalFree = freeBlocks_.size();
        stats.peakUsage = peakUsage_;
        
        return stats;
    }
    
    /**
     * Cleanup unused blocks (call periodically)
     */
    void cleanup(std::chrono::milliseconds maxIdleTime = std::chrono::minutes(5)) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::steady_clock::now();
        std::queue<PoolBlock*> newFreeBlocks;
        
        // Check free blocks for cleanup
        while (!freeBlocks_.empty()) {
            PoolBlock* block = freeBlocks_.front();
            freeBlocks_.pop();
            
            auto idleTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - block->lastUsed);
            
            if (idleTime < maxIdleTime || pool_.size() <= 1) {
                // Keep this block
                newFreeBlocks.push(block);
            } else {
                // Remove this block
                auto it = std::find_if(pool_.begin(), pool_.end(),
                    [block](const std::unique_ptr<PoolBlock>& p) {
                        return p.get() == block;
                    });
                if (it != pool_.end()) {
                    pool_.erase(it);
                }
            }
        }
        
        freeBlocks_ = std::move(newFreeBlocks);
    }
    
    /**
     * Force cleanup of all unused blocks
     */
    void forceCleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Remove all free blocks except one
        while (freeBlocks_.size() > 1) {
            PoolBlock* block = freeBlocks_.front();
            freeBlocks_.pop();
            
            auto it = std::find_if(pool_.begin(), pool_.end(),
                [block](const std::unique_ptr<PoolBlock>& p) {
                    return p.get() == block;
                });
            if (it != pool_.end()) {
                pool_.erase(it);
            }
        }
    }

private:
    void release(PoolBlock* block) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (block && inUseBlocks_.find(block) != inUseBlocks_.end()) {
            block->inUse = false;
            block->lastUsed = std::chrono::steady_clock::now();
            
            inUseBlocks_.erase(block);
            freeBlocks_.push(block);
            
            stats_.deallocationCount++;
            stats_.totalInUse--;
        }
    }
    
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<PoolBlock>> pool_;
    std::queue<PoolBlock*> freeBlocks_;
    std::unordered_set<PoolBlock*> inUseBlocks_;
    
    size_t maxSize_;
    std::atomic<size_t> peakUsage_;
    PoolStatistics stats_;
};

/**
 * Specialized audio buffer pool for streaming transcription
 */
class AudioBufferPool {
public:
    struct AudioBuffer {
        std::vector<float> data;
        size_t capacity;
        std::chrono::steady_clock::time_point lastUsed;
        
        AudioBuffer(size_t initialCapacity = 16000) : capacity(initialCapacity) {
            data.reserve(initialCapacity);
            lastUsed = std::chrono::steady_clock::now();
        }
        
        void reset() {
            data.clear();
            lastUsed = std::chrono::steady_clock::now();
        }
        
        void resize(size_t newCapacity) {
            if (newCapacity > capacity) {
                data.reserve(newCapacity);
                capacity = newCapacity;
            }
        }
    };
    
    using AudioBufferPtr = std::shared_ptr<AudioBuffer>;

public:
    explicit AudioBufferPool(size_t initialBuffers = 20, size_t maxBuffers = 200)
        : pool_(initialBuffers, maxBuffers) {}
    
    AudioBufferPtr acquireBuffer(size_t minCapacity = 16000) {
        auto buffer = pool_.acquire();
        if (buffer) {
            buffer->resize(minCapacity);
            buffer->reset();
        }
        return buffer;
    }
    
    auto getStatistics() const { return pool_.getStatistics(); }
    void cleanup() { pool_.cleanup(); }
    void forceCleanup() { pool_.forceCleanup(); }

private:
    MemoryPool<AudioBuffer> pool_;
};

/**
 * Transcription result pool for efficient result management
 */
class TranscriptionResultPool {
public:
    struct TranscriptionResult {
        std::string text;
        float confidence;
        bool is_partial;
        int64_t start_time_ms;
        int64_t end_time_ms;
        std::string detected_language;
        float language_confidence;
        std::chrono::steady_clock::time_point lastUsed;
        
        TranscriptionResult() : confidence(0.0f), is_partial(false), 
                               start_time_ms(0), end_time_ms(0), language_confidence(0.0f) {
            lastUsed = std::chrono::steady_clock::now();
        }
        
        void reset() {
            text.clear();
            confidence = 0.0f;
            is_partial = false;
            start_time_ms = 0;
            end_time_ms = 0;
            detected_language.clear();
            language_confidence = 0.0f;
            lastUsed = std::chrono::steady_clock::now();
        }
    };
    
    using TranscriptionResultPtr = std::shared_ptr<TranscriptionResult>;

public:
    explicit TranscriptionResultPool(size_t initialResults = 50, size_t maxResults = 500)
        : pool_(initialResults, maxResults) {}
    
    TranscriptionResultPtr acquireResult() {
        auto result = pool_.acquire();
        if (result) {
            result->reset();
        }
        return result;
    }
    
    auto getStatistics() const { return pool_.getStatistics(); }
    void cleanup() { pool_.cleanup(); }
    void forceCleanup() { pool_.forceCleanup(); }

private:
    MemoryPool<TranscriptionResult> pool_;
};

} // namespace utils