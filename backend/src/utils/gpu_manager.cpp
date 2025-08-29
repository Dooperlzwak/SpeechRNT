#include "utils/gpu_manager.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <sstream>

using utils::Logger;

// Include CUDA headers if available
#ifdef CUDA_AVAILABLE
#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <cudnn.h>
#endif

// Include NVML headers if available
#ifdef NVML_AVAILABLE
#include <nvml.h>
#endif

namespace speechrnt {
namespace utils {

GPUManager& GPUManager::getInstance() {
    static GPUManager instance;
    return instance;
}

GPUManager::~GPUManager() {
    cleanup();
}

bool GPUManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true;
    }
    
    lastError_.clear();
    cudaAvailable_ = false;
    deviceCount_ = 0;
    currentDevice_ = -1;
    memoryPoolEnabled_ = false;
    memoryPoolSizeMB_ = 0;
    memoryPool_ = nullptr;
    
#ifdef CUDA_AVAILABLE
    // Check CUDA runtime version
    int runtimeVersion = 0;
    cudaError_t result = cudaRuntimeGetVersion(&runtimeVersion);
    if (result != cudaSuccess) {
        lastError_ = "Failed to get CUDA runtime version: " + std::string(cudaGetErrorString(result));
        Logger::warn(lastError_);
        initialized_ = true; // Initialize without CUDA
        return true;
    }
    
    Logger::info("CUDA runtime version: " + std::to_string(runtimeVersion));
    
    // Get device count
    result = cudaGetDeviceCount(&deviceCount_);
    if (result != cudaSuccess) {
        lastError_ = "Failed to get CUDA device count: " + std::string(cudaGetErrorString(result));
        Logger::warn(lastError_);
        deviceCount_ = 0;
        initialized_ = true;
        return true;
    }
    
    if (deviceCount_ == 0) {
        Logger::info("No CUDA devices found, running in CPU-only mode");
        initialized_ = true;
        return true;
    }
    
    // Detect and initialize devices
    if (!detectDevices()) {
        Logger::warn("Failed to detect GPU devices, running in CPU-only mode");
        initialized_ = true;
        return true;
    }
    
    // Set default device to the first available one
    if (deviceCount_ > 0) {
        if (setDevice(0)) {
            cudaAvailable_ = true;
            Logger::info("GPU acceleration enabled with " + std::to_string(deviceCount_) + " device(s)");
        }
    }
    
#else
    Logger::info("CUDA not available, running in CPU-only mode");
#endif
    
    initialized_ = true;
    return true;
}

bool GPUManager::isCudaAvailable() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cudaAvailable_;
}

int GPUManager::getDeviceCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deviceCount_;
}

GPUDeviceInfo GPUManager::getDeviceInfo(int deviceId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (deviceId < 0 || deviceId >= static_cast<int>(devices_.size())) {
        return GPUDeviceInfo(); // Return empty info
    }
    
    return devices_[deviceId];
}

std::vector<GPUDeviceInfo> GPUManager::getAllDeviceInfo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return devices_;
}

bool GPUManager::setDevice(int deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!cudaAvailable_ || deviceId < 0 || deviceId >= deviceCount_) {
        lastError_ = "Invalid device ID or CUDA not available";
        return false;
    }
    
#ifdef CUDA_AVAILABLE
    cudaError_t result = cudaSetDevice(deviceId);
    if (result != cudaSuccess) {
        lastError_ = "Failed to set CUDA device " + std::to_string(deviceId) + ": " + 
                    std::string(cudaGetErrorString(result));
        return false;
    }
    
    currentDevice_ = deviceId;
    updateDeviceInfo(deviceId);
    
    Logger::info("Set active GPU device to: " + devices_[deviceId].name + 
                " (ID: " + std::to_string(deviceId) + ")");
    return true;
#else
    return false;
#endif
}

int GPUManager::getCurrentDevice() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentDevice_;
}

void* GPUManager::allocateGPUMemory(size_t sizeBytes, const std::string& tag) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!cudaAvailable_) {
        lastError_ = "CUDA not available";
        return nullptr;
    }
    
#ifdef CUDA_AVAILABLE
    void* devicePtr = nullptr;
    cudaError_t result = cudaMalloc(&devicePtr, sizeBytes);
    
    if (result != cudaSuccess) {
        lastError_ = "Failed to allocate GPU memory (" + std::to_string(sizeBytes) + 
                    " bytes): " + std::string(cudaGetErrorString(result));
        return nullptr;
    }
    
    trackAllocation(devicePtr, sizeBytes, tag);
    
    Logger::debug("Allocated " + std::to_string(sizeBytes) + " bytes GPU memory" +
                 (tag.empty() ? "" : " for " + tag));
    
    return devicePtr;
#else
    return nullptr;
#endif
}

bool GPUManager::freeGPUMemory(void* devicePtr) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!cudaAvailable_ || !devicePtr) {
        return false;
    }
    
#ifdef CUDA_AVAILABLE
    cudaError_t result = cudaFree(devicePtr);
    
    if (result != cudaSuccess) {
        lastError_ = "Failed to free GPU memory: " + std::string(cudaGetErrorString(result));
        return false;
    }
    
    untrackAllocation(devicePtr);
    Logger::debug("Freed GPU memory allocation");
    
    return true;
#else
    return false;
#endif
}

bool GPUManager::copyHostToDevice(void* devicePtr, const void* hostPtr, size_t sizeBytes) {
    if (!cudaAvailable_ || !devicePtr || !hostPtr) {
        return false;
    }
    
#ifdef CUDA_AVAILABLE
    cudaError_t result = cudaMemcpy(devicePtr, hostPtr, sizeBytes, cudaMemcpyHostToDevice);
    
    if (result != cudaSuccess) {
        lastError_ = "Failed to copy host to device: " + std::string(cudaGetErrorString(result));
        return false;
    }
    
    return true;
#else
    return false;
#endif
}

bool GPUManager::copyDeviceToHost(void* hostPtr, const void* devicePtr, size_t sizeBytes) {
    if (!cudaAvailable_ || !hostPtr || !devicePtr) {
        return false;
    }
    
#ifdef CUDA_AVAILABLE
    cudaError_t result = cudaMemcpy(hostPtr, devicePtr, sizeBytes, cudaMemcpyDeviceToHost);
    
    if (result != cudaSuccess) {
        lastError_ = "Failed to copy device to host: " + std::string(cudaGetErrorString(result));
        return false;
    }
    
    return true;
#else
    return false;
#endif
}

bool GPUManager::synchronize() {
    if (!cudaAvailable_) {
        return false;
    }
    
#ifdef CUDA_AVAILABLE
    cudaError_t result = cudaDeviceSynchronize();
    
    if (result != cudaSuccess) {
        lastError_ = "Failed to synchronize device: " + std::string(cudaGetErrorString(result));
        return false;
    }
    
    return true;
#else
    return false;
#endif
}

bool GPUManager::resetDevice() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!cudaAvailable_) {
        return false;
    }
    
#ifdef CUDA_AVAILABLE
    // Clear all tracked allocations
    allocations_.clear();
    
    cudaError_t result = cudaDeviceReset();
    
    if (result != cudaSuccess) {
        lastError_ = "Failed to reset device: " + std::string(cudaGetErrorString(result));
        return false;
    }
    
    Logger::info("GPU device reset successfully");
    return true;
#else
    return false;
#endif
}

size_t GPUManager::getCurrentMemoryUsageMB() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t totalBytes = 0;
    for (const auto& allocation : allocations_) {
        totalBytes += allocation.sizeBytes;
    }
    
    return totalBytes / (1024 * 1024);
}

size_t GPUManager::getTotalMemoryMB() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (currentDevice_ >= 0 && currentDevice_ < static_cast<int>(devices_.size())) {
        return devices_[currentDevice_].totalMemoryMB;
    }
    
    return 0;
}

size_t GPUManager::getFreeMemoryMB() const {
    if (!cudaAvailable_) {
        return 0;
    }
    
#ifdef CUDA_AVAILABLE
    size_t free = 0, total = 0;
    cudaError_t result = cudaMemGetInfo(&free, &total);
    
    if (result != cudaSuccess) {
        return 0;
    }
    
    return free / (1024 * 1024);
#else
    return 0;
#endif
}

std::vector<GPUMemoryAllocation> GPUManager::getMemoryAllocations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return allocations_;
}

void GPUManager::setMemoryPool(bool enable, size_t poolSizeMB) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    memoryPoolEnabled_ = enable;
    memoryPoolSizeMB_ = poolSizeMB;
    
    if (enable && cudaAvailable_) {
        Logger::info("GPU memory pool enabled with " + std::to_string(poolSizeMB) + "MB");
        // TODO: Implement memory pool allocation
    }
}

float GPUManager::getGPUUtilization() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!cudaAvailable_ || currentDevice_ < 0) {
        return -1.0f;
    }
    
#ifdef NVML_AVAILABLE
    static bool nvmlInitialized = false;
    static bool nvmlAvailable = false;
    
    // Initialize NVML on first call
    if (!nvmlInitialized) {
        nvmlReturn_t result = nvmlInit();
        if (result == NVML_SUCCESS) {
            nvmlAvailable = true;
            Logger::info("NVML initialized successfully for GPU monitoring");
        } else {
            Logger::warn("NVML initialization failed: " + std::string(nvmlErrorString(result)));
            nvmlAvailable = false;
        }
        nvmlInitialized = true;
    }
    
    if (!nvmlAvailable) {
        return -1.0f;
    }
    
    try {
        nvmlDevice_t device;
        nvmlReturn_t result = nvmlDeviceGetHandleByIndex(currentDevice_, &device);
        if (result != NVML_SUCCESS) {
            return -1.0f;
        }
        
        nvmlUtilization_t utilization;
        result = nvmlDeviceGetUtilizationRates(device, &utilization);
        if (result != NVML_SUCCESS) {
            return -1.0f;
        }
        
        return static_cast<float>(utilization.gpu);
        
    } catch (const std::exception& e) {
        Logger::warn("Failed to get GPU utilization: " + std::string(e.what()));
        return -1.0f;
    }
#else
    // Fallback: estimate utilization based on memory usage
    if (currentDevice_ >= 0 && currentDevice_ < static_cast<int>(devices_.size())) {
        const auto& device = devices_[currentDevice_];
        if (device.totalMemoryMB > 0) {
            size_t usedMemory = device.totalMemoryMB - device.freeMemoryMB;
            float memoryUtilization = static_cast<float>(usedMemory) / static_cast<float>(device.totalMemoryMB);
            // Rough estimate: assume GPU utilization correlates with memory usage
            return std::min(100.0f, memoryUtilization * 100.0f);
        }
    }
    return -1.0f;
#endif
}

float GPUManager::getGPUTemperature() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!cudaAvailable_ || currentDevice_ < 0) {
        return -1.0f;
    }
    
#ifdef NVML_AVAILABLE
    static bool nvmlInitialized = false;
    static bool nvmlAvailable = false;
    
    // Initialize NVML on first call
    if (!nvmlInitialized) {
        nvmlReturn_t result = nvmlInit();
        if (result == NVML_SUCCESS) {
            nvmlAvailable = true;
        } else {
            Logger::warn("NVML initialization failed for temperature monitoring: " + 
                        std::string(nvmlErrorString(result)));
            nvmlAvailable = false;
        }
        nvmlInitialized = true;
    }
    
    if (!nvmlAvailable) {
        return -1.0f;
    }
    
    try {
        nvmlDevice_t device;
        nvmlReturn_t result = nvmlDeviceGetHandleByIndex(currentDevice_, &device);
        if (result != NVML_SUCCESS) {
            return -1.0f;
        }
        
        unsigned int temperature;
        result = nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temperature);
        if (result != NVML_SUCCESS) {
            return -1.0f;
        }
        
        return static_cast<float>(temperature);
        
    } catch (const std::exception& e) {
        Logger::warn("Failed to get GPU temperature: " + std::string(e.what()));
        return -1.0f;
    }
#else
    // No fallback available for temperature without NVML
    return -1.0f;
#endif
}

bool GPUManager::hasSufficientMemory(size_t requiredMB) const {
    size_t freeMB = getFreeMemoryMB();
    return freeMB >= requiredMB;
}

int GPUManager::getRecommendedDevice() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (devices_.empty()) {
        return -1;
    }
    
    // Find device with most free memory and highest compute capability
    int bestDevice = -1;
    size_t maxFreeMemory = 0;
    int maxComputeCapability = 0;
    
    for (int i = 0; i < static_cast<int>(devices_.size()); ++i) {
        const auto& device = devices_[i];
        if (!device.isAvailable) continue;
        
        int computeCapability = device.computeCapabilityMajor * 10 + device.computeCapabilityMinor;
        
        if (device.freeMemoryMB > maxFreeMemory || 
            (device.freeMemoryMB == maxFreeMemory && computeCapability > maxComputeCapability)) {
            bestDevice = i;
            maxFreeMemory = device.freeMemoryMB;
            maxComputeCapability = computeCapability;
        }
    }
    
    return bestDevice;
}

void GPUManager::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return;
    }
    
    // Free all tracked allocations
    for (const auto& allocation : allocations_) {
        if (allocation.devicePtr) {
#ifdef CUDA_AVAILABLE
            cudaFree(allocation.devicePtr);
#endif
        }
    }
    allocations_.clear();
    
    // Free memory pool if allocated
    if (memoryPool_) {
#ifdef CUDA_AVAILABLE
        cudaFree(memoryPool_);
#endif
        memoryPool_ = nullptr;
    }
    
    cudaAvailable_ = false;
    initialized_ = false;
    
    Logger::info("GPU manager cleaned up");
}

std::string GPUManager::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

bool GPUManager::detectDevices() {
#ifdef CUDA_AVAILABLE
    devices_.clear();
    devices_.reserve(deviceCount_);
    
    for (int i = 0; i < deviceCount_; ++i) {
        cudaDeviceProp prop;
        cudaError_t result = cudaGetDeviceProperties(&prop, i);
        
        if (result != cudaSuccess) {
            Logger::warn("Failed to get properties for device " + std::to_string(i));
            continue;
        }
        
        GPUDeviceInfo info;
        info.deviceId = i;
        info.name = prop.name;
        info.totalMemoryMB = prop.totalGlobalMem / (1024 * 1024);
        info.computeCapabilityMajor = prop.major;
        info.computeCapabilityMinor = prop.minor;
        info.multiProcessorCount = prop.multiProcessorCount;
        info.isAvailable = true;
        
        // Get current free memory
        if (cudaSetDevice(i) == cudaSuccess) {
            size_t free = 0, total = 0;
            if (cudaMemGetInfo(&free, &total) == cudaSuccess) {
                info.freeMemoryMB = free / (1024 * 1024);
            }
        }
        
        devices_.push_back(info);
        
        Logger::info("Detected GPU " + std::to_string(i) + ": " + info.name + 
                    " (" + std::to_string(info.totalMemoryMB) + "MB, " +
                    "Compute " + std::to_string(info.computeCapabilityMajor) + "." + 
                    std::to_string(info.computeCapabilityMinor) + ")");
    }
    
    return !devices_.empty();
#else
    return false;
#endif
}

void GPUManager::updateDeviceInfo(int deviceId) {
#ifdef CUDA_AVAILABLE
    if (deviceId >= 0 && deviceId < static_cast<int>(devices_.size())) {
        size_t free = 0, total = 0;
        if (cudaMemGetInfo(&free, &total) == cudaSuccess) {
            devices_[deviceId].freeMemoryMB = free / (1024 * 1024);
        }
    }
#endif
}

void GPUManager::trackAllocation(void* devicePtr, size_t sizeBytes, const std::string& tag) {
    GPUMemoryAllocation allocation;
    allocation.devicePtr = devicePtr;
    allocation.sizeBytes = sizeBytes;
    allocation.tag = tag;
    allocation.allocatedAt = std::chrono::steady_clock::now();
    
    allocations_.push_back(allocation);
}

void GPUManager::untrackAllocation(void* devicePtr) {
    allocations_.erase(
        std::remove_if(allocations_.begin(), allocations_.end(),
                      [devicePtr](const GPUMemoryAllocation& alloc) {
                          return alloc.devicePtr == devicePtr;
                      }),
        allocations_.end());
}

std::unordered_map<std::string, float> GPUManager::getDetailedGPUMetrics(int deviceId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, float> metrics;
    
    int targetDevice = (deviceId >= 0) ? deviceId : currentDevice_;
    
    if (!cudaAvailable_ || targetDevice < 0 || targetDevice >= deviceCount_) {
        return metrics; // Return empty metrics
    }
    
    // Basic metrics always available
    if (targetDevice < static_cast<int>(devices_.size())) {
        const auto& device = devices_[targetDevice];
        metrics["total_memory_mb"] = static_cast<float>(device.totalMemoryMB);
        metrics["free_memory_mb"] = static_cast<float>(device.freeMemoryMB);
        metrics["used_memory_mb"] = static_cast<float>(device.totalMemoryMB - device.freeMemoryMB);
        metrics["memory_utilization_percent"] = device.totalMemoryMB > 0 ? 
            (static_cast<float>(device.totalMemoryMB - device.freeMemoryMB) / 
             static_cast<float>(device.totalMemoryMB)) * 100.0f : 0.0f;
        metrics["compute_capability"] = static_cast<float>(device.computeCapabilityMajor) + 
                                       static_cast<float>(device.computeCapabilityMinor) / 10.0f;
        metrics["multiprocessor_count"] = static_cast<float>(device.multiProcessorCount);
    }
    
#ifdef NVML_AVAILABLE
    static bool nvmlInitialized = false;
    static bool nvmlAvailable = false;
    
    if (!nvmlInitialized) {
        nvmlReturn_t result = nvmlInit();
        nvmlAvailable = (result == NVML_SUCCESS);
        nvmlInitialized = true;
    }
    
    if (nvmlAvailable) {
        try {
            nvmlDevice_t device;
            nvmlReturn_t result = nvmlDeviceGetHandleByIndex(targetDevice, &device);
            if (result == NVML_SUCCESS) {
                // GPU utilization
                nvmlUtilization_t utilization;
                if (nvmlDeviceGetUtilizationRates(device, &utilization) == NVML_SUCCESS) {
                    metrics["gpu_utilization_percent"] = static_cast<float>(utilization.gpu);
                    metrics["memory_utilization_percent"] = static_cast<float>(utilization.memory);
                }
                
                // Temperature
                unsigned int temperature;
                if (nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temperature) == NVML_SUCCESS) {
                    metrics["temperature_celsius"] = static_cast<float>(temperature);
                }
                
                // Power usage
                unsigned int power;
                if (nvmlDeviceGetPowerUsage(device, &power) == NVML_SUCCESS) {
                    metrics["power_usage_watts"] = static_cast<float>(power) / 1000.0f; // Convert mW to W
                }
                
                // Clock speeds
                unsigned int graphicsClock, memoryClock;
                if (nvmlDeviceGetClockInfo(device, NVML_CLOCK_GRAPHICS, &graphicsClock) == NVML_SUCCESS) {
                    metrics["graphics_clock_mhz"] = static_cast<float>(graphicsClock);
                }
                if (nvmlDeviceGetClockInfo(device, NVML_CLOCK_MEM, &memoryClock) == NVML_SUCCESS) {
                    metrics["memory_clock_mhz"] = static_cast<float>(memoryClock);
                }
                
                // Fan speed
                unsigned int fanSpeed;
                if (nvmlDeviceGetFanSpeed(device, &fanSpeed) == NVML_SUCCESS) {
                    metrics["fan_speed_percent"] = static_cast<float>(fanSpeed);
                }
                
                // Performance state
                nvmlPstates_t pState;
                if (nvmlDeviceGetPerformanceState(device, &pState) == NVML_SUCCESS) {
                    metrics["performance_state"] = static_cast<float>(pState);
                }
            }
        } catch (const std::exception& e) {
            Logger::warn("Error collecting detailed GPU metrics: " + std::string(e.what()));
        }
    }
#endif
    
    return metrics;
}

float GPUManager::getGPUPowerUsage(int deviceId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    int targetDevice = (deviceId >= 0) ? deviceId : currentDevice_;
    
    if (!cudaAvailable_ || targetDevice < 0) {
        return -1.0f;
    }
    
#ifdef NVML_AVAILABLE
    static bool nvmlInitialized = false;
    static bool nvmlAvailable = false;
    
    if (!nvmlInitialized) {
        nvmlReturn_t result = nvmlInit();
        nvmlAvailable = (result == NVML_SUCCESS);
        nvmlInitialized = true;
    }
    
    if (nvmlAvailable) {
        try {
            nvmlDevice_t device;
            nvmlReturn_t result = nvmlDeviceGetHandleByIndex(targetDevice, &device);
            if (result == NVML_SUCCESS) {
                unsigned int power;
                if (nvmlDeviceGetPowerUsage(device, &power) == NVML_SUCCESS) {
                    return static_cast<float>(power) / 1000.0f; // Convert mW to W
                }
            }
        } catch (const std::exception& e) {
            Logger::warn("Error getting GPU power usage: " + std::string(e.what()));
        }
    }
#endif
    
    return -1.0f;
}

float GPUManager::getGPUMemoryBandwidthUtilization(int deviceId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    int targetDevice = (deviceId >= 0) ? deviceId : currentDevice_;
    
    if (!cudaAvailable_ || targetDevice < 0) {
        return -1.0f;
    }
    
#ifdef NVML_AVAILABLE
    static bool nvmlInitialized = false;
    static bool nvmlAvailable = false;
    
    if (!nvmlInitialized) {
        nvmlReturn_t result = nvmlInit();
        nvmlAvailable = (result == NVML_SUCCESS);
        nvmlInitialized = true;
    }
    
    if (nvmlAvailable) {
        try {
            nvmlDevice_t device;
            nvmlReturn_t result = nvmlDeviceGetHandleByIndex(targetDevice, &device);
            if (result == NVML_SUCCESS) {
                nvmlUtilization_t utilization;
                if (nvmlDeviceGetUtilizationRates(device, &utilization) == NVML_SUCCESS) {
                    return static_cast<float>(utilization.memory);
                }
            }
        } catch (const std::exception& e) {
            Logger::warn("Error getting GPU memory bandwidth utilization: " + std::string(e.what()));
        }
    }
#endif
    
    return -1.0f;
}

bool GPUManager::isNVMLAvailable() const {
#ifdef NVML_AVAILABLE
    static bool nvmlInitialized = false;
    static bool nvmlAvailable = false;
    
    if (!nvmlInitialized) {
        nvmlReturn_t result = nvmlInit();
        nvmlAvailable = (result == NVML_SUCCESS);
        nvmlInitialized = true;
    }
    
    return nvmlAvailable;
#else
    return false;
#endif
}

} // namespace utils
} // namespace speechrnt