#include "utils/gpu_memory_pool.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <sstream>
#include <cstring>

#ifdef CUDA_AVAILABLE
#include <cuda_runtime.h>
#endif

namespace utils {

GPUMemoryPool::GPUMemoryPool(const PoolConfig& config)
    : config_(config)
    , initialized_(false)
    , poolBasePtr_(nullptr)
    , poolSizeBytes_(0)
    , peakUsage_(0)
    , totalAllocationTime_(0.0) {
}

GPUMemoryPool::~GPUMemoryPool() {
    forceCleanup();
    
    if (poolBasePtr_) {
#ifdef CUDA_AVAILABLE
        cudaFree(poolBasePtr_);
#endif
        poolBasePtr_ = nullptr;
    }
}

bool GPUMemoryPool::initialize() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    if (initialized_) {
        return true;
    }
    
    // Check if GPU is available
    auto& gpuManager = speechrnt::utils::GPUManager::getInstance();
    if (!gpuManager.isCudaAvailable()) {
        Logger::warn("GPU not available, GPU memory pool disabled");
        return false;
    }
    
    // Allocate initial pool
    poolSizeBytes_ = config_.initialPoolSizeMB * 1024 * 1024;
    
#ifdef CUDA_AVAILABLE
    cudaError_t result = cudaMalloc(&poolBasePtr_, poolSizeBytes_);
    if (result != cudaSuccess) {
        Logger::error("Failed to allocate GPU memory pool: " + 
                     std::string(cudaGetErrorString(result)));
        return false;
    }
    
    // Initialize with one large free block
    auto initialBlock = std::make_unique<MemoryBlock>(poolBasePtr_, poolSizeBytes_);
    freeBlocks_.push(initialBlock.get());
    allBlocks_.push_back(std::move(initialBlock));
    
    initialized_ = true;
    
    Logger::info("GPU memory pool initialized with " + 
                std::to_string(config_.initialPoolSizeMB) + "MB");
    
    return true;
#else
    Logger::error("CUDA not available for GPU memory pool");
    return false;
#endif
}

void* GPUMemoryPool::allocate(size_t sizeBytes, const std::string& tag) {
    if (!initialized_ || sizeBytes == 0) {
        return nullptr;
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    // Align size to configured alignment
    size_t alignedSize = alignSize(sizeBytes, config_.alignmentBytes);
    
    // Find best fit block
    MemoryBlock* block = findBestFitBlock(alignedSize);
    
    if (!block) {
        // Try to expand pool if possible
        if (expandPool(alignedSize)) {
            block = findBestFitBlock(alignedSize);
        }
    }
    
    if (!block) {
        Logger::warn("GPU memory pool allocation failed for " + 
                    std::to_string(sizeBytes) + " bytes");
        return nullptr;
    }
    
    // Split block if necessary
    if (block->sizeBytes > alignedSize + config_.alignmentBytes) {
        MemoryBlock* remainingBlock = splitBlock(block, alignedSize);
        if (remainingBlock) {
            freeBlocks_.push(remainingBlock);
        }
    }
    
    // Mark block as in use
    block->inUse = true;
    block->lastUsed = std::chrono::steady_clock::now();
    block->tag = tag;
    
    // Remove from free blocks and add to in-use blocks
    // Note: We need to remove from queue, but std::queue doesn't support removal
    // This is a limitation - in production, we'd use a different data structure
    inUseBlocks_[block->devicePtr] = block;
    
    // Update statistics
    trackAllocation(block, tag);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    // Update average allocation time
    double currentTime = totalAllocationTime_.load();
    totalAllocationTime_ = currentTime + duration.count();
    
    Logger::debug("GPU memory allocated: " + std::to_string(sizeBytes) + 
                 " bytes" + (tag.empty() ? "" : " for " + tag));
    
    return block->devicePtr;
}

bool GPUMemoryPool::deallocate(void* devicePtr) {
    if (!initialized_ || !devicePtr) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    auto it = inUseBlocks_.find(devicePtr);
    if (it == inUseBlocks_.end()) {
        Logger::warn("Attempted to deallocate unknown GPU memory pointer");
        return false;
    }
    
    MemoryBlock* block = it->second;
    block->inUse = false;
    block->lastUsed = std::chrono::steady_clock::now();
    
    // Move from in-use to free
    inUseBlocks_.erase(it);
    freeBlocks_.push(block);
    
    // Update statistics
    untrackAllocation(block);
    
    // Try to merge adjacent free blocks
    mergeAdjacentBlocks();
    
    Logger::debug("GPU memory deallocated: " + std::to_string(block->sizeBytes) + " bytes");
    
    return true;
}

void* GPUMemoryPool::allocateAligned(size_t sizeBytes, size_t alignment, const std::string& tag) {
    size_t alignedSize = alignSize(sizeBytes, alignment);
    return allocate(alignedSize, tag);
}

GPUMemoryPool::PoolStatistics GPUMemoryPool::getStatistics() const {
    std::lock_guard<std::mutex> lock1(poolMutex_);
    std::lock_guard<std::mutex> lock2(statsMutex_);
    
    PoolStatistics stats = stats_;
    
    // Calculate current usage
    size_t totalInUse = 0;
    size_t totalFree = 0;
    
    for (const auto& block : allBlocks_) {
        if (block->inUse) {
            totalInUse += block->sizeBytes;
        } else {
            totalFree += block->sizeBytes;
        }
    }
    
    stats.totalAllocatedMB = poolSizeBytes_ / (1024 * 1024);
    stats.totalInUseMB = totalInUse / (1024 * 1024);
    stats.totalFreeMB = totalFree / (1024 * 1024);
    stats.peakUsageMB = peakUsage_.load() / (1024 * 1024);
    
    if (stats.allocationCount > 0) {
        stats.averageAllocationTime = totalAllocationTime_.load() / stats.allocationCount;
    }
    
    return stats;
}

void GPUMemoryPool::cleanup() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    auto now = std::chrono::steady_clock::now();
    std::queue<MemoryBlock*> newFreeBlocks;
    
    // Check free blocks for cleanup
    while (!freeBlocks_.empty()) {
        MemoryBlock* block = freeBlocks_.front();
        freeBlocks_.pop();
        
        auto idleTime = std::chrono::duration_cast<std::chrono::minutes>(
            now - block->lastUsed);
        
        if (idleTime < config_.maxIdleTime || allBlocks_.size() <= 1) {
            // Keep this block
            newFreeBlocks.push(block);
        } else {
            // This block can be removed (in a real implementation)
            // For now, we keep it since we can't easily shrink the pool
            newFreeBlocks.push(block);
        }
    }
    
    freeBlocks_ = std::move(newFreeBlocks);
    
    // Merge adjacent blocks
    mergeAdjacentBlocks();
    
    Logger::debug("GPU memory pool cleanup completed");
}

void GPUMemoryPool::forceCleanup() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    // Clear all tracking
    inUseBlocks_.clear();
    while (!freeBlocks_.empty()) {
        freeBlocks_.pop();
    }
    allBlocks_.clear();
    
    // Reset statistics
    std::lock_guard<std::mutex> statsLock(statsMutex_);
    stats_ = PoolStatistics();
    peakUsage_ = 0;
    totalAllocationTime_ = 0.0;
    
    Logger::info("GPU memory pool force cleanup completed");
}

bool GPUMemoryPool::defragment() {
    if (!initialized_ || !config_.enableDefragmentation) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    // Simple defragmentation: merge adjacent free blocks
    bool merged = mergeAdjacentBlocks();
    
    if (merged) {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        stats_.defragmentationCount++;
        Logger::debug("GPU memory pool defragmentation completed");
    }
    
    return merged;
}

bool GPUMemoryPool::isHealthy() const {
    auto stats = getStatistics();
    
    // Consider healthy if:
    // 1. Less than 90% memory usage
    // 2. Reasonable fragmentation
    // 3. Pool is initialized
    
    bool memoryHealthy = stats.totalInUseMB < (stats.totalAllocatedMB * 9 / 10);
    bool fragmentationHealthy = stats.fragmentationCount < (stats.allocationCount / 4);
    
    return initialized_ && memoryHealthy && fragmentationHealthy;
}

std::string GPUMemoryPool::getHealthStatus() const {
    auto stats = getStatistics();
    
    std::ostringstream oss;
    oss << "GPU Memory Pool Health Status:\n";
    oss << "  Total Allocated: " << stats.totalAllocatedMB << "MB\n";
    oss << "  In Use: " << stats.totalInUseMB << "MB\n";
    oss << "  Free: " << stats.totalFreeMB << "MB\n";
    oss << "  Peak Usage: " << stats.peakUsageMB << "MB\n";
    oss << "  Allocations: " << stats.allocationCount << "\n";
    oss << "  Deallocations: " << stats.deallocationCount << "\n";
    oss << "  Fragmentation Events: " << stats.fragmentationCount << "\n";
    oss << "  Average Allocation Time: " << stats.averageAllocationTime << "μs\n";
    oss << "  Status: " << (isHealthy() ? "HEALTHY" : "UNHEALTHY");
    
    return oss.str();
}

void GPUMemoryPool::updateConfig(const PoolConfig& config) {
    std::lock_guard<std::mutex> lock(poolMutex_);
    config_ = config;
    Logger::info("GPU memory pool configuration updated");
}

bool GPUMemoryPool::preallocateBlocks(const std::vector<size_t>& blockSizes) {
    if (!initialized_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    // This is a simplified implementation
    // In practice, we'd pre-split the pool into the requested sizes
    Logger::info("GPU memory pool preallocation requested for " + 
                std::to_string(blockSizes.size()) + " blocks");
    
    return true;
}

// Private helper methods
GPUMemoryPool::MemoryBlock* GPUMemoryPool::findBestFitBlock(size_t sizeBytes) {
    MemoryBlock* bestFit = nullptr;
    size_t bestFitSize = SIZE_MAX;
    
    // Simple best-fit algorithm
    // Note: This is inefficient for std::queue, but demonstrates the concept
    std::queue<MemoryBlock*> tempQueue;
    
    while (!freeBlocks_.empty()) {
        MemoryBlock* block = freeBlocks_.front();
        freeBlocks_.pop();
        
        if (block->sizeBytes >= sizeBytes && block->sizeBytes < bestFitSize) {
            if (bestFit) {
                tempQueue.push(bestFit);
            }
            bestFit = block;
            bestFitSize = block->sizeBytes;
        } else {
            tempQueue.push(block);
        }
    }
    
    // Restore queue (except for the selected block)
    freeBlocks_ = std::move(tempQueue);
    
    return bestFit;
}

GPUMemoryPool::MemoryBlock* GPUMemoryPool::splitBlock(MemoryBlock* block, size_t sizeBytes) {
    if (!block || block->sizeBytes <= sizeBytes) {
        return nullptr;
    }
    
    size_t remainingSize = block->sizeBytes - sizeBytes;
    if (remainingSize < config_.alignmentBytes) {
        // Not worth splitting
        return nullptr;
    }
    
    // Create new block for remaining memory
    void* remainingPtr = static_cast<char*>(block->devicePtr) + sizeBytes;
    auto remainingBlock = std::make_unique<MemoryBlock>(remainingPtr, remainingSize);
    MemoryBlock* result = remainingBlock.get();
    
    // Update original block size
    block->sizeBytes = sizeBytes;
    
    // Add remaining block to pool
    allBlocks_.push_back(std::move(remainingBlock));
    
    return result;
}

bool GPUMemoryPool::mergeAdjacentBlocks() {
    // Sort blocks by address for merging
    std::vector<MemoryBlock*> freeBlockList;
    
    while (!freeBlocks_.empty()) {
        freeBlockList.push_back(freeBlocks_.front());
        freeBlocks_.pop();
    }
    
    std::sort(freeBlockList.begin(), freeBlockList.end(),
              [](const MemoryBlock* a, const MemoryBlock* b) {
                  return a->devicePtr < b->devicePtr;
              });
    
    bool merged = false;
    std::vector<MemoryBlock*> mergedBlocks;
    
    for (size_t i = 0; i < freeBlockList.size(); ++i) {
        MemoryBlock* current = freeBlockList[i];
        
        // Try to merge with next block
        if (i + 1 < freeBlockList.size()) {
            MemoryBlock* next = freeBlockList[i + 1];
            char* currentEnd = static_cast<char*>(current->devicePtr) + current->sizeBytes;
            
            if (currentEnd == next->devicePtr) {
                // Merge blocks
                current->sizeBytes += next->sizeBytes;
                
                // Remove next block from allBlocks_
                auto it = std::find_if(allBlocks_.begin(), allBlocks_.end(),
                    [next](const std::unique_ptr<MemoryBlock>& p) {
                        return p.get() == next;
                    });
                if (it != allBlocks_.end()) {
                    allBlocks_.erase(it);
                }
                
                merged = true;
                ++i; // Skip the merged block
            }
        }
        
        mergedBlocks.push_back(current);
    }
    
    // Restore free blocks queue
    for (MemoryBlock* block : mergedBlocks) {
        freeBlocks_.push(block);
    }
    
    return merged;
}

size_t GPUMemoryPool::alignSize(size_t size, size_t alignment) const {
    return ((size + alignment - 1) / alignment) * alignment;
}

void GPUMemoryPool::updateStatistics() {
    size_t currentUsage = 0;
    for (const auto& pair : inUseBlocks_) {
        currentUsage += pair.second->sizeBytes;
    }
    
    if (currentUsage > peakUsage_) {
        peakUsage_ = currentUsage;
    }
}

bool GPUMemoryPool::expandPool(size_t additionalSizeBytes) {
    // Check if we can expand
    size_t newTotalSize = poolSizeBytes_ + additionalSizeBytes;
    size_t maxPoolSize = config_.maxPoolSizeMB * 1024 * 1024;
    
    if (newTotalSize > maxPoolSize) {
        Logger::warn("Cannot expand GPU memory pool beyond maximum size");
        return false;
    }
    
    // For simplicity, we don't actually expand the pool in this implementation
    // In a real implementation, we'd allocate additional memory and manage it
    Logger::debug("GPU memory pool expansion requested but not implemented");
    return false;
}

void GPUMemoryPool::trackAllocation(MemoryBlock* block, const std::string& tag) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.allocationCount++;
    updateStatistics();
}

void GPUMemoryPool::untrackAllocation(MemoryBlock* block) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.deallocationCount++;
}

} // namespace utils