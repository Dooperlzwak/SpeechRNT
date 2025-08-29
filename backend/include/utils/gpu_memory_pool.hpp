#pragma once

#include "utils/gpu_manager.hpp"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <queue>
#include <chrono>
#include <atomic>

namespace utils {

/**
 * GPU Memory Pool for efficient GPU memory allocation and reuse
 * Reduces CUDA allocation overhead and memory fragmentation
 */
class GPUMemoryPool {
public:
    struct PoolConfig {
        size_t initialPoolSizeMB = 512;     // Initial pool size
        size_t maxPoolSizeMB = 2048;        // Maximum pool size
        size_t blockSizeMB = 16;            // Standard block size
        size_t alignmentBytes = 256;        // Memory alignment
        bool enableDefragmentation = true;  // Enable memory defragmentation
        std::chrono::minutes maxIdleTime{5}; // Max idle time before cleanup
        
        PoolConfig() = default;
    };
    
    struct PoolStatistics {
        size_t totalAllocatedMB;
        size_t totalInUseMB;
        size_t totalFreeMB;
        size_t peakUsageMB;
        size_t allocationCount;
        size_t deallocationCount;
        size_t fragmentationCount;
        size_t defragmentationCount;
        double averageAllocationTime;
        
        PoolStatistics() : totalAllocatedMB(0), totalInUseMB(0), totalFreeMB(0),
                          peakUsageMB(0), allocationCount(0), deallocationCount(0),
                          fragmentationCount(0), defragmentationCount(0),
                          averageAllocationTime(0.0) {}
    };

private:
    struct MemoryBlock {
        void* devicePtr;
        size_t sizeBytes;
        bool inUse;
        std::chrono::steady_clock::time_point lastUsed;
        std::string tag;
        
        MemoryBlock(void* ptr, size_t size) 
            : devicePtr(ptr), sizeBytes(size), inUse(false) {
            lastUsed = std::chrono::steady_clock::now();
        }
    };
    
    struct AllocationRequest {
        size_t sizeBytes;
        std::string tag;
        std::chrono::steady_clock::time_point requestTime;
        
        AllocationRequest(size_t size, const std::string& t)
            : sizeBytes(size), tag(t) {
            requestTime = std::chrono::steady_clock::now();
        }
    };

public:
    explicit GPUMemoryPool(const PoolConfig& config = PoolConfig());
    ~GPUMemoryPool();
    
    // Disable copy constructor and assignment
    GPUMemoryPool(const GPUMemoryPool&) = delete;
    GPUMemoryPool& operator=(const GPUMemoryPool&) = delete;
    
    /**
     * Initialize the memory pool
     */
    bool initialize();
    
    /**
     * Allocate GPU memory from pool
     */
    void* allocate(size_t sizeBytes, const std::string& tag = "");
    
    /**
     * Deallocate GPU memory back to pool
     */
    bool deallocate(void* devicePtr);
    
    /**
     * Allocate aligned GPU memory
     */
    void* allocateAligned(size_t sizeBytes, size_t alignment, const std::string& tag = "");
    
    /**
     * Get memory statistics
     */
    PoolStatistics getStatistics() const;
    
    /**
     * Cleanup unused memory blocks
     */
    void cleanup();
    
    /**
     * Force cleanup of all unused blocks
     */
    void forceCleanup();
    
    /**
     * Defragment memory pool
     */
    bool defragment();
    
    /**
     * Check if pool is healthy
     */
    bool isHealthy() const;
    
    /**
     * Get health status report
     */
    std::string getHealthStatus() const;
    
    /**
     * Update pool configuration
     */
    void updateConfig(const PoolConfig& config);
    
    /**
     * Preallocation for known memory patterns
     */
    bool preallocateBlocks(const std::vector<size_t>& blockSizes);

private:
    // Configuration
    PoolConfig config_;
    
    // Pool state
    bool initialized_;
    void* poolBasePtr_;
    size_t poolSizeBytes_;
    
    // Memory management
    mutable std::mutex poolMutex_;
    std::vector<std::unique_ptr<MemoryBlock>> allBlocks_;
    std::queue<MemoryBlock*> freeBlocks_;
    std::unordered_map<void*, MemoryBlock*> inUseBlocks_;
    
    // Statistics
    mutable std::mutex statsMutex_;
    PoolStatistics stats_;
    std::atomic<size_t> peakUsage_;
    std::atomic<double> totalAllocationTime_;
    
    // Helper methods
    MemoryBlock* findBestFitBlock(size_t sizeBytes);
    MemoryBlock* splitBlock(MemoryBlock* block, size_t sizeBytes);
    bool mergeAdjacentBlocks();
    size_t alignSize(size_t size, size_t alignment) const;
    void updateStatistics();
    bool expandPool(size_t additionalSizeBytes);
    void trackAllocation(MemoryBlock* block, const std::string& tag);
    void untrackAllocation(MemoryBlock* block);
};

/**
 * RAII wrapper for GPU memory allocation
 */
class GPUMemoryHandle {
public:
    GPUMemoryHandle(GPUMemoryPool* pool, size_t sizeBytes, const std::string& tag = "")
        : pool_(pool), devicePtr_(nullptr), sizeBytes_(sizeBytes) {
        if (pool_) {
            devicePtr_ = pool_->allocate(sizeBytes, tag);
        }
    }
    
    ~GPUMemoryHandle() {
        if (pool_ && devicePtr_) {
            pool_->deallocate(devicePtr_);
        }
    }
    
    // Disable copy constructor and assignment
    GPUMemoryHandle(const GPUMemoryHandle&) = delete;
    GPUMemoryHandle& operator=(const GPUMemoryHandle&) = delete;
    
    // Enable move constructor and assignment
    GPUMemoryHandle(GPUMemoryHandle&& other) noexcept
        : pool_(other.pool_), devicePtr_(other.devicePtr_), sizeBytes_(other.sizeBytes_) {
        other.pool_ = nullptr;
        other.devicePtr_ = nullptr;
        other.sizeBytes_ = 0;
    }
    
    GPUMemoryHandle& operator=(GPUMemoryHandle&& other) noexcept {
        if (this != &other) {
            if (pool_ && devicePtr_) {
                pool_->deallocate(devicePtr_);
            }
            
            pool_ = other.pool_;
            devicePtr_ = other.devicePtr_;
            sizeBytes_ = other.sizeBytes_;
            
            other.pool_ = nullptr;
            other.devicePtr_ = nullptr;
            other.sizeBytes_ = 0;
        }
        return *this;
    }
    
    void* get() const { return devicePtr_; }
    size_t size() const { return sizeBytes_; }
    bool isValid() const { return devicePtr_ != nullptr; }
    
    void* release() {
        void* ptr = devicePtr_;
        devicePtr_ = nullptr;
        return ptr;
    }

private:
    GPUMemoryPool* pool_;
    void* devicePtr_;
    size_t sizeBytes_;
};

} // namespace utils