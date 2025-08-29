#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <unordered_map>

namespace speechrnt {
namespace utils {

/**
 * GPU device information
 */
struct GPUDeviceInfo {
    int deviceId;
    std::string name;
    size_t totalMemoryMB;
    size_t freeMemoryMB;
    int computeCapabilityMajor;
    int computeCapabilityMinor;
    int multiProcessorCount;
    bool isAvailable;
    
    GPUDeviceInfo() : deviceId(-1), totalMemoryMB(0), freeMemoryMB(0), 
                     computeCapabilityMajor(0), computeCapabilityMinor(0),
                     multiProcessorCount(0), isAvailable(false) {}
};

/**
 * GPU memory allocation tracking
 */
struct GPUMemoryAllocation {
    void* devicePtr;
    size_t sizeBytes;
    std::string tag;
    std::chrono::steady_clock::time_point allocatedAt;
    
    GPUMemoryAllocation() : devicePtr(nullptr), sizeBytes(0) {}
};

/**
 * GPU Manager for CUDA operations and memory management
 * Provides centralized GPU resource management and monitoring
 */
class GPUManager {
public:
    static GPUManager& getInstance();
    
    /**
     * Initialize GPU manager and detect available devices
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * Check if CUDA is available and functional
     * @return true if CUDA is available
     */
    bool isCudaAvailable() const;
    
    /**
     * Get number of available GPU devices
     * @return number of GPU devices
     */
    int getDeviceCount() const;
    
    /**
     * Get information about a specific GPU device
     * @param deviceId GPU device ID
     * @return device information
     */
    GPUDeviceInfo getDeviceInfo(int deviceId) const;
    
    /**
     * Get information about all available GPU devices
     * @return vector of device information
     */
    std::vector<GPUDeviceInfo> getAllDeviceInfo() const;
    
    /**
     * Set the active GPU device
     * @param deviceId GPU device ID to set as active
     * @return true if device set successfully
     */
    bool setDevice(int deviceId);
    
    /**
     * Get the currently active GPU device ID
     * @return active device ID, -1 if no device active
     */
    int getCurrentDevice() const;
    
    /**
     * Allocate GPU memory
     * @param sizeBytes Size in bytes to allocate
     * @param tag Optional tag for tracking
     * @return device pointer, nullptr on failure
     */
    void* allocateGPUMemory(size_t sizeBytes, const std::string& tag = "");
    
    /**
     * Free GPU memory
     * @param devicePtr Device pointer to free
     * @return true if freed successfully
     */
    bool freeGPUMemory(void* devicePtr);
    
    /**
     * Copy data from host to device
     * @param devicePtr Device pointer
     * @param hostPtr Host pointer
     * @param sizeBytes Size in bytes to copy
     * @return true if copy successful
     */
    bool copyHostToDevice(void* devicePtr, const void* hostPtr, size_t sizeBytes);
    
    /**
     * Copy data from device to host
     * @param hostPtr Host pointer
     * @param devicePtr Device pointer
     * @param sizeBytes Size in bytes to copy
     * @return true if copy successful
     */
    bool copyDeviceToHost(void* hostPtr, const void* devicePtr, size_t sizeBytes);
    
    /**
     * Synchronize GPU operations
     * @return true if synchronization successful
     */
    bool synchronize();
    
    /**
     * Reset GPU device (clears memory and state)
     * @return true if reset successful
     */
    bool resetDevice();
    
    /**
     * Get current GPU memory usage
     * @return memory usage in MB
     */
    size_t getCurrentMemoryUsageMB() const;
    
    /**
     * Get total GPU memory
     * @return total memory in MB
     */
    size_t getTotalMemoryMB() const;
    
    /**
     * Get free GPU memory
     * @return free memory in MB
     */
    size_t getFreeMemoryMB() const;
    
    /**
     * Get memory allocation statistics
     * @return vector of current allocations
     */
    std::vector<GPUMemoryAllocation> getMemoryAllocations() const;
    
    /**
     * Enable/disable GPU memory pool for faster allocations
     * @param enable true to enable memory pool
     * @param poolSizeMB initial pool size in MB
     */
    void setMemoryPool(bool enable, size_t poolSizeMB = 1024);
    
    /**
     * Get GPU utilization percentage (if supported)
     * @return utilization percentage (0-100), -1 if not supported
     */
    float getGPUUtilization() const;
    
    /**
     * Get GPU temperature in Celsius (if supported)
     * @return temperature in Celsius, -1 if not supported
     */
    float getGPUTemperature() const;
    
    /**
     * Check if GPU has sufficient memory for allocation
     * @param requiredMB Required memory in MB
     * @return true if sufficient memory available
     */
    bool hasSufficientMemory(size_t requiredMB) const;
    
    /**
     * Get recommended device for AI workloads
     * @return device ID of recommended device, -1 if none suitable
     */
    int getRecommendedDevice() const;
    
    /**
     * Cleanup and shutdown GPU manager
     */
    void cleanup();
    
    /**
     * Get last error message
     * @return error message string
     */
    std::string getLastError() const;
    
    /**
     * Get detailed GPU metrics (utilization, temperature, memory, power)
     * @param deviceId GPU device ID (-1 for current device)
     * @return map of metric names to values
     */
    std::unordered_map<std::string, float> getDetailedGPUMetrics(int deviceId = -1) const;
    
    /**
     * Get GPU power consumption in watts (if supported)
     * @param deviceId GPU device ID (-1 for current device)
     * @return power consumption in watts, -1 if not supported
     */
    float getGPUPowerUsage(int deviceId = -1) const;
    
    /**
     * Get GPU memory bandwidth utilization (if supported)
     * @param deviceId GPU device ID (-1 for current device)
     * @return memory bandwidth utilization percentage, -1 if not supported
     */
    float getGPUMemoryBandwidthUtilization(int deviceId = -1) const;
    
    /**
     * Check if NVML is available for advanced monitoring
     * @return true if NVML is available
     */
    bool isNVMLAvailable() const;

private:
    GPUManager() = default;
    ~GPUManager();
    
    // Prevent copying
    GPUManager(const GPUManager&) = delete;
    GPUManager& operator=(const GPUManager&) = delete;
    
    // Private methods
    bool detectDevices();
    void updateDeviceInfo(int deviceId);
    void trackAllocation(void* devicePtr, size_t sizeBytes, const std::string& tag);
    void untrackAllocation(void* devicePtr);
    
    // Member variables
    bool initialized_;
    bool cudaAvailable_;
    int deviceCount_;
    int currentDevice_;
    std::string lastError_;
    
    std::vector<GPUDeviceInfo> devices_;
    std::vector<GPUMemoryAllocation> allocations_;
    
    // Memory pool settings
    bool memoryPoolEnabled_;
    size_t memoryPoolSizeMB_;
    void* memoryPool_;
    
    // Thread safety
    mutable std::mutex mutex_;
};

} // namespace utils
} // namespace speechrnt