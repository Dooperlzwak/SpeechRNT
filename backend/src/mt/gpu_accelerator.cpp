#include "mt/gpu_accelerator.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <thread>
#include <sstream>
#include <iomanip>

#ifdef CUDA_AVAILABLE
#include <cuda_runtime.h>
#include <cublas_v2.h>
#endif

namespace speechrnt {
namespace mt {

GPUAccelerator::GPUAccelerator()
    : initialized_(false)
    , gpuAvailable_(false)
    , currentDeviceId_(-1)
    , cpuFallbackEnabled_(true)
    , performanceMonitoringActive_(false)
    , memoryThresholdPercent_(80.0f)
    , temperatureThresholdC_(85.0f)
    , utilizationThresholdPercent_(90.0f)
    , memoryPoolSizeMB_(1024)
    , defragmentationEnabled_(true)
    , quantizationEnabled_(false)
    , quantizationPrecision_("fp16")
    , maxBatchSize_(32)
    , optimalBatchSize_(8)
    , concurrentStreamsEnabled_(true)
    , streamCount_(4) {
    
    // Initialize GPU manager
    gpuManager_ = &speechrnt::utils::GPUManager::getInstance();
}

GPUAccelerator::~GPUAccelerator() {
    cleanup();
}

bool GPUAccelerator::initialize() {
    std::lock_guard<std::mutex> lock(gpuMutex_);
    
    if (initialized_) {
        return true;
    }
    
    LOG_INFO("Initializing GPU Accelerator for MT operations");
    
    // Initialize GPU manager
    if (!gpuManager_->initialize()) {
        LOG_ERROR("Failed to initialize GPU manager");
        lastError_ = "GPU manager initialization failed";
        return false;
    }
    
    // Check if CUDA is available
    if (!gpuManager_->isCudaAvailable()) {
        LOG_WARN("CUDA not available, GPU acceleration disabled");
        gpuAvailable_ = false;
        initialized_ = true;
        return true; // Still successful, just no GPU
    }
    
    // Detect compatible GPUs
    if (!detectCompatibleGPUs()) {
        LOG_ERROR("No compatible GPUs found for MT operations");
        gpuAvailable_ = false;
        initialized_ = true;
        return true; // Still successful, just no compatible GPU
    }
    
    // Select best GPU device
    int bestDevice = getBestGPUDevice();
    if (bestDevice >= 0) {
        if (!selectGPU(bestDevice)) {
            LOG_ERROR("Failed to select best GPU device: " + std::to_string(bestDevice));
            gpuAvailable_ = false;
        } else {
            gpuAvailable_ = true;
            LOG_INFO("Selected GPU device " + std::to_string(bestDevice) + " for MT operations");
        }
    }
    
    // Initialize memory pool if GPU is available
    if (gpuAvailable_) {
        utils::GPUMemoryPool::PoolConfig poolConfig;
        poolConfig.initialPoolSizeMB = memoryPoolSizeMB_;
        poolConfig.maxPoolSizeMB = memoryPoolSizeMB_ * 2;
        poolConfig.enableDefragmentation = defragmentationEnabled_;
        
        memoryPool_ = std::make_unique<utils::GPUMemoryPool>(poolConfig);
        if (!memoryPool_->initialize()) {
            LOG_WARN("Failed to initialize GPU memory pool, using direct allocation");
            memoryPool_.reset();
        }
    }
    
    initialized_ = true;
    LOG_INFO("GPU Accelerator initialization completed. GPU available: " + 
             std::string(gpuAvailable_ ? "Yes" : "No"));
    
    return true;
}

std::vector<GPUInfo> GPUAccelerator::getAvailableGPUs() const {
    std::lock_guard<std::mutex> lock(gpuMutex_);
    return availableGPUs_;
}

bool GPUAccelerator::isGPUAvailable() const {
    return gpuAvailable_ && initialized_;
}

int GPUAccelerator::getCompatibleGPUCount() const {
    std::lock_guard<std::mutex> lock(gpuMutex_);
    return static_cast<int>(availableGPUs_.size());
}

bool GPUAccelerator::selectGPU(int deviceId) {
    std::lock_guard<std::mutex> lock(gpuMutex_);
    
    if (!initialized_) {
        lastError_ = "GPU Accelerator not initialized";
        return false;
    }
    
    if (!validateGPUDevice(deviceId)) {
        lastError_ = "Invalid or incompatible GPU device: " + std::to_string(deviceId);
        return false;
    }
    
    // Set device in GPU manager
    if (!gpuManager_->setDevice(deviceId)) {
        lastError_ = "Failed to set GPU device: " + gpuManager_->getLastError();
        return false;
    }
    
    // Initialize CUDA context for this device
    if (!initializeCudaContext(deviceId)) {
        lastError_ = "Failed to initialize CUDA context for device " + std::to_string(deviceId);
        return false;
    }
    
    // Create CUDA streams if concurrent processing is enabled
    if (concurrentStreamsEnabled_) {
        if (!createCudaStreams(streamCount_)) {
            LOG_WARN("Failed to create CUDA streams, falling back to single stream");
            concurrentStreamsEnabled_ = false;
        }
    }
    
    currentDeviceId_ = deviceId;
    gpuAvailable_ = true;
    
    LOG_INFO("Successfully selected GPU device " + std::to_string(deviceId));
    return true;
}

int GPUAccelerator::getCurrentGPUDevice() const {
    return currentDeviceId_;
}

GPUInfo GPUAccelerator::getCurrentGPUInfo() const {
    std::lock_guard<std::mutex> lock(gpuMutex_);
    
    if (currentDeviceId_ < 0 || currentDeviceId_ >= static_cast<int>(availableGPUs_.size())) {
        return GPUInfo(); // Return empty info
    }
    
    return availableGPUs_[currentDeviceId_];
}

int GPUAccelerator::getBestGPUDevice() const {
    std::lock_guard<std::mutex> lock(gpuMutex_);
    
    if (availableGPUs_.empty()) {
        return -1;
    }
    
    // Score GPUs based on memory, compute capability, and availability
    int bestDevice = -1;
    float bestScore = 0.0f;
    
    for (const auto& gpu : availableGPUs_) {
        if (!gpu.isCompatible) continue;
        
        float score = 0.0f;
        
        // Memory score (40% weight)
        score += (static_cast<float>(gpu.availableMemoryMB) / 8192.0f) * 0.4f;
        
        // Compute capability score (30% weight)
        float computeScore = (gpu.computeCapabilityMajor * 10 + gpu.computeCapabilityMinor) / 100.0f;
        score += computeScore * 0.3f;
        
        // Multiprocessor count score (20% weight)
        score += (static_cast<float>(gpu.multiProcessorCount) / 128.0f) * 0.2f;
        
        // Feature support score (10% weight)
        float featureScore = 0.0f;
        if (gpu.supportsFloat16) featureScore += 0.5f;
        if (gpu.supportsInt8) featureScore += 0.5f;
        score += featureScore * 0.1f;
        
        if (score > bestScore) {
            bestScore = score;
            bestDevice = gpu.deviceId;
        }
    }
    
    return bestDevice;
}

bool GPUAccelerator::validateGPUDevice(int deviceId) const {
    std::lock_guard<std::mutex> lock(gpuMutex_);
    
    auto it = std::find_if(availableGPUs_.begin(), availableGPUs_.end(),
                          [deviceId](const GPUInfo& gpu) { return gpu.deviceId == deviceId; });
    
    return it != availableGPUs_.end() && it->isCompatible;
}

bool GPUAccelerator::allocateGPUMemory(size_t sizeMB, const std::string& tag) {
    std::lock_guard<std::mutex> lock(gpuMutex_);
    
    if (!gpuAvailable_) {
        lastError_ = "GPU not available for memory allocation";
        return false;
    }
    
    if (memoryPool_) {
        void* ptr = memoryPool_->allocate(sizeMB * 1024 * 1024, tag);
        if (!ptr) {
            lastError_ = "Failed to allocate " + std::to_string(sizeMB) + "MB from GPU memory pool";
            return false;
        }
    } else {
        void* ptr = gpuManager_->allocateGPUMemory(sizeMB * 1024 * 1024, tag);
        if (!ptr) {
            lastError_ = "Failed to allocate " + std::to_string(sizeMB) + "MB GPU memory: " + 
                        gpuManager_->getLastError();
            return false;
        }
    }
    
    return true;
}

void GPUAccelerator::freeGPUMemory() {
    std::lock_guard<std::mutex> lock(gpuMutex_);
    
    if (memoryPool_) {
        memoryPool_->forceCleanup();
    }
    
    // Free all model memory
    for (auto& [languagePair, modelInfo] : loadedModels_) {
        if (modelInfo.gpuModelPtr) {
            unloadModelFromGPU(modelInfo.gpuModelPtr);
        }
    }
    loadedModels_.clear();
}

size_t GPUAccelerator::getAvailableGPUMemory() const {
    if (!gpuAvailable_ || !gpuManager_) {
        return 0;
    }
    
    return gpuManager_->getFreeMemoryMB();
}

size_t GPUAccelerator::getGPUMemoryUsage() const {
    if (!gpuAvailable_ || !gpuManager_) {
        return 0;
    }
    
    return gpuManager_->getCurrentMemoryUsageMB();
}

bool GPUAccelerator::hasSufficientGPUMemory(size_t requiredMB) const {
    if (!gpuAvailable_) {
        return false;
    }
    
    size_t availableMB = getAvailableGPUMemory();
    return availableMB >= requiredMB;
}

bool GPUAccelerator::optimizeGPUMemory() {
    std::lock_guard<std::mutex> lock(gpuMutex_);
    
    if (!gpuAvailable_) {
        return false;
    }
    
    bool optimized = false;
    
    // Defragment memory pool if available
    if (memoryPool_ && defragmentationEnabled_) {
        if (memoryPool_->defragment()) {
            optimized = true;
            LOG_INFO("GPU memory pool defragmented successfully");
        }
    }
    
    // Clean up unused models
    auto now = std::chrono::steady_clock::now();
    auto it = loadedModels_.begin();
    while (it != loadedModels_.end()) {
        auto timeSinceLastUse = std::chrono::duration_cast<std::chrono::minutes>(now - it->second.lastUsed);
        if (timeSinceLastUse.count() > 30 && it->second.usageCount == 0) { // 30 minutes idle
            LOG_INFO("Unloading idle model: " + it->first);
            unloadModelFromGPU(it->second.gpuModelPtr);
            it = loadedModels_.erase(it);
            optimized = true;
        } else {
            ++it;
        }
    }
    
    return optimized;
}

bool GPUAccelerator::loadModelToGPU(const std::string& modelPath, const std::string& languagePair, void** gpuModelPtr) {
    std::lock_guard<std::mutex> lock(modelsMutex_);
    
    if (!gpuAvailable_) {
        lastError_ = "GPU not available for model loading";
        return false;
    }
    
    // Check if model is already loaded
    auto it = loadedModels_.find(languagePair);
    if (it != loadedModels_.end()) {
        *gpuModelPtr = it->second.gpuModelPtr;
        it->second.lastUsed = std::chrono::steady_clock::now();
        it->second.usageCount++;
        return true;
    }
    
    // Validate model compatibility
    if (!validateModelCompatibility(modelPath)) {
        lastError_ = "Model not compatible with GPU acceleration: " + modelPath;
        return false;
    }
    
    // Estimate memory requirement
    size_t requiredMB = estimateModelMemoryRequirement(modelPath);
    if (!hasSufficientGPUMemory(requiredMB)) {
        // Try to free some memory
        if (!optimizeGPUMemory() || !hasSufficientGPUMemory(requiredMB)) {
            lastError_ = "Insufficient GPU memory for model: " + std::to_string(requiredMB) + "MB required";
            return false;
        }
    }
    
    // Load model to GPU
    if (!loadModelToDevice(modelPath, currentDeviceId_, gpuModelPtr)) {
        lastError_ = "Failed to load model to GPU device: " + modelPath;
        return false;
    }
    
    // Store model information
    GPUModelInfo modelInfo;
    modelInfo.modelPath = modelPath;
    modelInfo.languagePair = languagePair;
    modelInfo.gpuModelPtr = *gpuModelPtr;
    modelInfo.memorySizeMB = requiredMB;
    modelInfo.loadedAt = std::chrono::steady_clock::now();
    modelInfo.lastUsed = modelInfo.loadedAt;
    modelInfo.usageCount = 1;
    modelInfo.isQuantized = quantizationEnabled_;
    modelInfo.precision = quantizationPrecision_;
    
    loadedModels_[languagePair] = modelInfo;
    
    LOG_INFO("Successfully loaded model to GPU: " + languagePair + " (" + std::to_string(requiredMB) + "MB)");
    return true;
}

bool GPUAccelerator::unloadModelFromGPU(void* gpuModelPtr) {
    std::lock_guard<std::mutex> lock(modelsMutex_);
    
    if (!gpuModelPtr) {
        return true; // Already unloaded
    }
    
    // Find and remove model from loaded models
    auto it = std::find_if(loadedModels_.begin(), loadedModels_.end(),
                          [gpuModelPtr](const auto& pair) { 
                              return pair.second.gpuModelPtr == gpuModelPtr; 
                          });
    
    if (it != loadedModels_.end()) {
        unloadModelFromDevice(gpuModelPtr, currentDeviceId_);
        LOG_INFO("Unloaded model from GPU: " + it->first);
        loadedModels_.erase(it);
        return true;
    }
    
    return false;
}

std::vector<GPUModelInfo> GPUAccelerator::getLoadedModels() const {
    std::lock_guard<std::mutex> lock(modelsMutex_);
    
    std::vector<GPUModelInfo> models;
    models.reserve(loadedModels_.size());
    
    for (const auto& [languagePair, modelInfo] : loadedModels_) {
        models.push_back(modelInfo);
    }
    
    return models;
}

bool GPUAccelerator::isModelLoadedOnGPU(const std::string& languagePair) const {
    std::lock_guard<std::mutex> lock(modelsMutex_);
    return loadedModels_.find(languagePair) != loadedModels_.end();
}

void* GPUAccelerator::getGPUModelPointer(const std::string& languagePair) const {
    std::lock_guard<std::mutex> lock(modelsMutex_);
    
    auto it = loadedModels_.find(languagePair);
    if (it != loadedModels_.end()) {
        // Update last used time (mutable)
        const_cast<GPUModelInfo&>(it->second).lastUsed = std::chrono::steady_clock::now();
        return it->second.gpuModelPtr;
    }
    
    return nullptr;
}

bool GPUAccelerator::accelerateTranslation(void* gpuModel, const std::string& input, std::string& output) {
    if (!gpuAvailable_ || !gpuModel) {
        lastError_ = "GPU not available or invalid model pointer";
        return false;
    }
    
    void* stream = getAvailableCudaStream();
    bool result = performGPUTranslation(gpuModel, input, output, stream);
    
    if (stream) {
        releaseCudaStream(stream);
    }
    
    // Update statistics
    if (result) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        currentStats_.translationsProcessed++;
    }
    
    return result;
}

bool GPUAccelerator::accelerateBatchTranslation(void* gpuModel, const std::vector<std::string>& inputs, std::vector<std::string>& outputs) {
    if (!gpuAvailable_ || !gpuModel) {
        lastError_ = "GPU not available or invalid model pointer";
        return false;
    }
    
    outputs.clear();
    outputs.reserve(inputs.size());
    
    // Process in batches of optimal size
    size_t batchSize = std::min(inputs.size(), optimalBatchSize_);
    bool allSuccessful = true;
    
    for (size_t i = 0; i < inputs.size(); i += batchSize) {
        size_t currentBatchSize = std::min(batchSize, inputs.size() - i);
        
        for (size_t j = 0; j < currentBatchSize; ++j) {
            std::string output;
            if (accelerateTranslation(gpuModel, inputs[i + j], output)) {
                outputs.push_back(output);
            } else {
                outputs.push_back(""); // Empty output for failed translation
                allSuccessful = false;
            }
        }
    }
    
    return allSuccessful;
}

bool GPUAccelerator::startStreamingSession(void* gpuModel, const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(streamingSessionsMutex_);
    
    if (!gpuAvailable_ || !gpuModel) {
        lastError_ = "GPU not available or invalid model pointer";
        return false;
    }
    
    // Check if session already exists
    if (streamingSessions_.find(sessionId) != streamingSessions_.end()) {
        lastError_ = "Streaming session already exists: " + sessionId;
        return false;
    }
    
    // Create new streaming session
    StreamingSession session;
    session.sessionId = sessionId;
    session.gpuModel = gpuModel;
    session.cudaStream = getAvailableCudaStream();
    session.lastActivity = std::chrono::steady_clock::now();
    session.isActive = true;
    
    streamingSessions_[sessionId] = session;
    
    LOG_INFO("Started streaming session: " + sessionId);
    return true;
}

bool GPUAccelerator::processStreamingChunk(const std::string& sessionId, const std::string& inputChunk, std::string& outputChunk) {
    std::lock_guard<std::mutex> lock(streamingSessionsMutex_);
    
    auto it = streamingSessions_.find(sessionId);
    if (it == streamingSessions_.end()) {
        lastError_ = "Streaming session not found: " + sessionId;
        return false;
    }
    
    StreamingSession& session = it->second;
    if (!session.isActive) {
        lastError_ = "Streaming session is not active: " + sessionId;
        return false;
    }
    
    // Accumulate input
    session.accumulatedInput += inputChunk;
    session.lastActivity = std::chrono::steady_clock::now();
    
    // Perform translation on accumulated input
    bool result = performGPUTranslation(session.gpuModel, session.accumulatedInput, outputChunk, session.cudaStream);
    
    if (result) {
        // Update statistics
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        currentStats_.translationsProcessed++;
        currentStats_.activeStreams = streamingSessions_.size();
    }
    
    return result;
}

bool GPUAccelerator::endStreamingSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(streamingSessionsMutex_);
    
    auto it = streamingSessions_.find(sessionId);
    if (it == streamingSessions_.end()) {
        return true; // Session doesn't exist, consider it ended
    }
    
    // Release CUDA stream
    if (it->second.cudaStream) {
        releaseCudaStream(it->second.cudaStream);
    }
    
    streamingSessions_.erase(it);
    
    LOG_INFO("Ended streaming session: " + sessionId);
    return true;
}

bool GPUAccelerator::createCudaContext(int deviceId) {
    return initializeCudaContext(deviceId);
}

bool GPUAccelerator::destroyCudaContext(int deviceId) {
    cleanupCudaContext(deviceId);
    return true;
}

bool GPUAccelerator::createCudaStreams(int streamCount) {
    std::lock_guard<std::mutex> lock(streamsMutex_);
    
    availableStreams_.clear();
    busyStreams_.clear();
    
    for (int i = 0; i < streamCount; ++i) {
        void* stream = nullptr;
        if (createCudaStream(&stream)) {
            availableStreams_.push_back(stream);
        } else {
            LOG_WARN("Failed to create CUDA stream " + std::to_string(i));
        }
    }
    
    LOG_INFO("Created " + std::to_string(availableStreams_.size()) + " CUDA streams");
    return !availableStreams_.empty();
}

bool GPUAccelerator::synchronizeCudaStreams() {
    std::lock_guard<std::mutex> lock(streamsMutex_);
    
    // Synchronize all streams
    for (void* stream : availableStreams_) {
        if (stream) {
#ifdef CUDA_AVAILABLE
            cudaStreamSynchronize(static_cast<cudaStream_t>(stream));
#endif
        }
    }
    
    for (void* stream : busyStreams_) {
        if (stream) {
#ifdef CUDA_AVAILABLE
            cudaStreamSynchronize(static_cast<cudaStream_t>(stream));
#endif
        }
    }
    
    return true;
}

void* GPUAccelerator::getAvailableCudaStream() {
    std::lock_guard<std::mutex> lock(streamsMutex_);
    
    if (availableStreams_.empty()) {
        return nullptr;
    }
    
    void* stream = availableStreams_.back();
    availableStreams_.pop_back();
    busyStreams_.push_back(stream);
    
    return stream;
}

void GPUAccelerator::releaseCudaStream(void* stream) {
    std::lock_guard<std::mutex> lock(streamsMutex_);
    
    auto it = std::find(busyStreams_.begin(), busyStreams_.end(), stream);
    if (it != busyStreams_.end()) {
        busyStreams_.erase(it);
        availableStreams_.push_back(stream);
    }
}

bool GPUAccelerator::isGPUOperational() const {
    if (!gpuAvailable_ || !initialized_) {
        return false;
    }
    
    // Check if current device is still available
    if (currentDeviceId_ >= 0) {
        auto deviceInfo = gpuManager_->getDeviceInfo(currentDeviceId_);
        return deviceInfo.isAvailable;
    }
    
    return false;
}

bool GPUAccelerator::handleGPUError(const std::string& error) {
    LOG_ERROR("GPU Error: " + error);
    lastError_ = error;
    
    // Attempt recovery
    if (recoverFromGPUError()) {
        LOG_INFO("Successfully recovered from GPU error");
        return true;
    }
    
    // If recovery fails, fallback to CPU if enabled
    if (cpuFallbackEnabled_) {
        return fallbackToCPU("GPU error recovery failed: " + error);
    }
    
    return false;
}

void GPUAccelerator::enableCPUFallback(bool enabled) {
    cpuFallbackEnabled_ = enabled;
    LOG_INFO("CPU fallback " + std::string(enabled ? "enabled" : "disabled"));
}

bool GPUAccelerator::isCPUFallbackEnabled() const {
    return cpuFallbackEnabled_;
}

bool GPUAccelerator::fallbackToCPU(const std::string& reason) {
    LOG_WARN("Falling back to CPU processing: " + reason);
    
    // Disable GPU acceleration
    gpuAvailable_ = false;
    
    // Clean up GPU resources
    freeGPUMemory();
    
    return true;
}

bool GPUAccelerator::recoverFromGPUError() {
    if (currentDeviceId_ < 0) {
        return false;
    }
    
    // Try to reset the GPU device
    if (resetGPUDevice()) {
        // Reinitialize CUDA context
        if (initializeCudaContext(currentDeviceId_)) {
            // Recreate CUDA streams
            if (concurrentStreamsEnabled_) {
                createCudaStreams(streamCount_);
            }
            return true;
        }
    }
    
    return false;
}

bool GPUAccelerator::resetGPUDevice() {
    if (!gpuManager_) {
        return false;
    }
    
    LOG_INFO("Resetting GPU device " + std::to_string(currentDeviceId_));
    
    // Clean up current resources
    cleanupCudaContext(currentDeviceId_);
    
    // Reset device
    bool result = gpuManager_->resetDevice();
    
    if (result) {
        LOG_INFO("GPU device reset successful");
    } else {
        LOG_ERROR("GPU device reset failed: " + gpuManager_->getLastError());
    }
    
    return result;
}

std::string GPUAccelerator::getLastGPUError() const {
    return lastError_;
}

GPUStats GPUAccelerator::getGPUStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return currentStats_;
}

bool GPUAccelerator::startPerformanceMonitoring(int intervalMs) {
    if (performanceMonitoringActive_.load()) {
        return true; // Already running
    }
    
    performanceMonitoringActive_.store(true);
    
    performanceMonitoringThread_ = std::thread([this, intervalMs]() {
        performanceMonitoringLoop();
    });
    
    LOG_INFO("Started GPU performance monitoring with " + std::to_string(intervalMs) + "ms interval");
    return true;
}

void GPUAccelerator::stopPerformanceMonitoring() {
    if (!performanceMonitoringActive_.load()) {
        return;
    }
    
    performanceMonitoringActive_.store(false);
    
    if (performanceMonitoringThread_.joinable()) {
        performanceMonitoringThread_.join();
    }
    
    LOG_INFO("Stopped GPU performance monitoring");
}

void GPUAccelerator::updatePerformanceStatistics() {
    collectGPUMetrics();
    checkPerformanceThresholds();
}

std::vector<GPUStats> GPUAccelerator::getPerformanceHistory(int durationMinutes) const {
    std::lock_guard<std::mutex> lock(performanceHistoryMutex_);
    
    auto cutoffTime = std::chrono::steady_clock::now() - std::chrono::minutes(durationMinutes);
    
    std::vector<GPUStats> history;
    // Note: This is a simplified implementation. In a real implementation,
    // you would store timestamps with each GPUStats entry and filter accordingly.
    history = performanceHistory_;
    
    return history;
}

void GPUAccelerator::resetPerformanceStatistics() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    currentStats_ = GPUStats();
    
    std::lock_guard<std::mutex> historyLock(performanceHistoryMutex_);
    performanceHistory_.clear();
    
    LOG_INFO("Reset GPU performance statistics");
}

bool GPUAccelerator::isPerformanceMonitoringActive() const {
    return performanceMonitoringActive_.load();
}

void GPUAccelerator::setPerformanceThresholds(float memoryThresholdPercent, float temperatureThresholdC, float utilizationThresholdPercent) {
    memoryThresholdPercent_ = memoryThresholdPercent;
    temperatureThresholdC_ = temperatureThresholdC;
    utilizationThresholdPercent_ = utilizationThresholdPercent;
    
    LOG_INFO("Updated performance thresholds - Memory: " + std::to_string(memoryThresholdPercent) + 
             "%, Temperature: " + std::to_string(temperatureThresholdC) + 
             "°C, Utilization: " + std::to_string(utilizationThresholdPercent) + "%");
}

bool GPUAccelerator::arePerformanceThresholdsExceeded() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    return (currentStats_.utilizationPercent > utilizationThresholdPercent_) ||
           (currentStats_.temperatureCelsius > temperatureThresholdC_) ||
           ((static_cast<float>(currentStats_.memoryUsedMB) / 
             static_cast<float>(getCurrentGPUInfo().totalMemoryMB)) * 100.0f > memoryThresholdPercent_);
}

std::vector<std::string> GPUAccelerator::getPerformanceAlerts() const {
    std::vector<std::string> alerts;
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    if (currentStats_.utilizationPercent > utilizationThresholdPercent_) {
        alerts.push_back("High GPU utilization: " + std::to_string(currentStats_.utilizationPercent) + "%");
    }
    
    if (currentStats_.temperatureCelsius > temperatureThresholdC_) {
        alerts.push_back("High GPU temperature: " + std::to_string(currentStats_.temperatureCelsius) + "°C");
    }
    
    float memoryUsagePercent = (static_cast<float>(currentStats_.memoryUsedMB) / 
                               static_cast<float>(getCurrentGPUInfo().totalMemoryMB)) * 100.0f;
    if (memoryUsagePercent > memoryThresholdPercent_) {
        alerts.push_back("High GPU memory usage: " + std::to_string(memoryUsagePercent) + "%");
    }
    
    return alerts;
}

bool GPUAccelerator::configureMemoryPool(size_t poolSizeMB, bool enableDefragmentation) {
    memoryPoolSizeMB_ = poolSizeMB;
    defragmentationEnabled_ = enableDefragmentation;
    
    // Reinitialize memory pool if already created
    if (memoryPool_) {
        utils::GPUMemoryPool::PoolConfig poolConfig;
        poolConfig.initialPoolSizeMB = poolSizeMB;
        poolConfig.maxPoolSizeMB = poolSizeMB * 2;
        poolConfig.enableDefragmentation = enableDefragmentation;
        
        memoryPool_ = std::make_unique<utils::GPUMemoryPool>(poolConfig);
        return memoryPool_->initialize();
    }
    
    return true;
}

bool GPUAccelerator::configureQuantization(bool enabled, const std::string& precision) {
    quantizationEnabled_ = enabled;
    quantizationPrecision_ = precision;
    
    LOG_INFO("Quantization " + std::string(enabled ? "enabled" : "disabled") + 
             " with precision: " + precision);
    
    return true;
}

bool GPUAccelerator::configureBatchProcessing(size_t maxBatchSize, size_t optimalBatchSize) {
    maxBatchSize_ = maxBatchSize;
    optimalBatchSize_ = optimalBatchSize;
    
    LOG_INFO("Batch processing configured - Max: " + std::to_string(maxBatchSize) + 
             ", Optimal: " + std::to_string(optimalBatchSize));
    
    return true;
}

bool GPUAccelerator::configureConcurrentStreams(bool enabled, int streamCount) {
    concurrentStreamsEnabled_ = enabled;
    streamCount_ = streamCount;
    
    if (enabled && gpuAvailable_) {
        return createCudaStreams(streamCount);
    }
    
    return true;
}

void GPUAccelerator::cleanup() {
    LOG_INFO("Cleaning up GPU Accelerator");
    
    // Stop performance monitoring
    stopPerformanceMonitoring();
    
    // Clean up streaming sessions
    {
        std::lock_guard<std::mutex> lock(streamingSessionsMutex_);
        for (auto& [sessionId, session] : streamingSessions_) {
            if (session.cudaStream) {
                releaseCudaStream(session.cudaStream);
            }
        }
        streamingSessions_.clear();
    }
    
    // Free GPU memory
    freeGPUMemory();
    
    // Clean up CUDA contexts
    for (auto& [deviceId, context] : cudaContexts_) {
        cleanupCudaContext(deviceId);
    }
    cudaContexts_.clear();
    
    // Clean up CUDA streams
    {
        std::lock_guard<std::mutex> lock(streamsMutex_);
        for (void* stream : availableStreams_) {
            destroyCudaStream(stream);
        }
        for (void* stream : busyStreams_) {
            destroyCudaStream(stream);
        }
        availableStreams_.clear();
        busyStreams_.clear();
    }
    
    // Clean up memory pool
    memoryPool_.reset();
    
    // Clean up GPU manager (singleton, don't delete)
    if (gpuManager_) {
        gpuManager_->cleanup();
    }
    
    initialized_ = false;
    gpuAvailable_ = false;
    currentDeviceId_ = -1;
    
    LOG_INFO("GPU Accelerator cleanup completed");
}

// Private helper methods implementation

bool GPUAccelerator::detectCompatibleGPUs() {
    availableGPUs_.clear();
    
    if (!gpuManager_) {
        return false;
    }
    
    int deviceCount = gpuManager_->getDeviceCount();
    if (deviceCount <= 0) {
        return false;
    }
    
    for (int i = 0; i < deviceCount; ++i) {
        auto deviceInfo = gpuManager_->getDeviceInfo(i);
        
        GPUInfo gpuInfo;
        gpuInfo.deviceId = deviceInfo.deviceId;
        gpuInfo.deviceName = deviceInfo.name;
        gpuInfo.totalMemoryMB = deviceInfo.totalMemoryMB;
        gpuInfo.availableMemoryMB = deviceInfo.freeMemoryMB;
        gpuInfo.computeCapabilityMajor = deviceInfo.computeCapabilityMajor;
        gpuInfo.computeCapabilityMinor = deviceInfo.computeCapabilityMinor;
        gpuInfo.multiProcessorCount = deviceInfo.multiProcessorCount;
        
        // Check compatibility for MT operations
        gpuInfo.isCompatible = (deviceInfo.computeCapabilityMajor >= 6) && // Minimum Pascal architecture
                              (deviceInfo.totalMemoryMB >= 2048) && // Minimum 2GB memory
                              deviceInfo.isAvailable;
        
        // Check feature support
        gpuInfo.supportsFloat16 = (deviceInfo.computeCapabilityMajor >= 7) || 
                                 (deviceInfo.computeCapabilityMajor == 6 && deviceInfo.computeCapabilityMinor >= 1);
        gpuInfo.supportsInt8 = (deviceInfo.computeCapabilityMajor >= 7);
        
        // Get CUDA version (simplified)
        gpuInfo.cudaVersion = "11.0+"; // This would be detected from actual CUDA runtime
        
        availableGPUs_.push_back(gpuInfo);
        
        LOG_INFO("Detected GPU " + std::to_string(i) + ": " + gpuInfo.deviceName + 
                " (Compatible: " + (gpuInfo.isCompatible ? "Yes" : "No") + ")");
    }
    
    return !availableGPUs_.empty();
}

bool GPUAccelerator::initializeCudaContext(int deviceId) {
#ifdef CUDA_AVAILABLE
    cudaError_t error = cudaSetDevice(deviceId);
    if (error != cudaSuccess) {
        lastError_ = "Failed to set CUDA device: " + std::string(cudaGetErrorString(error));
        return false;
    }
    
    // Create CUDA context (implicit with runtime API)
    CudaContext context;
    context.deviceId = deviceId;
    context.isActive = true;
    context.createdAt = std::chrono::steady_clock::now();
    
    cudaContexts_[deviceId] = context;
    return true;
#else
    lastError_ = "CUDA not available";
    return false;
#endif
}

void GPUAccelerator::cleanupCudaContext(int deviceId) {
    auto it = cudaContexts_.find(deviceId);
    if (it != cudaContexts_.end()) {
#ifdef CUDA_AVAILABLE
        cudaSetDevice(deviceId);
        cudaDeviceReset();
#endif
        cudaContexts_.erase(it);
    }
}

bool GPUAccelerator::createCudaStream(void** stream) {
#ifdef CUDA_AVAILABLE
    cudaStream_t cudaStream;
    cudaError_t error = cudaStreamCreate(&cudaStream);
    if (error != cudaSuccess) {
        lastError_ = "Failed to create CUDA stream: " + std::string(cudaGetErrorString(error));
        return false;
    }
    *stream = static_cast<void*>(cudaStream);
    return true;
#else
    *stream = nullptr;
    return false;
#endif
}

void GPUAccelerator::destroyCudaStream(void* stream) {
    if (stream) {
#ifdef CUDA_AVAILABLE
        cudaStreamDestroy(static_cast<cudaStream_t>(stream));
#endif
    }
}

bool GPUAccelerator::validateModelCompatibility(const std::string& modelPath) const {
    // This is a simplified validation. In a real implementation,
    // you would check the model format, size, and compatibility with Marian NMT
    return !modelPath.empty() && modelPath.find(".npz") != std::string::npos;
}

size_t GPUAccelerator::estimateModelMemoryRequirement(const std::string& modelPath) const {
    // This is a simplified estimation. In a real implementation,
    // you would analyze the model file to determine actual memory requirements
    
    // Default estimation based on typical Marian model sizes
    size_t baseSizeMB = 512; // Base model size
    
    // Adjust based on quantization
    if (quantizationEnabled_) {
        if (quantizationPrecision_ == "fp16") {
            baseSizeMB /= 2;
        } else if (quantizationPrecision_ == "int8") {
            baseSizeMB /= 4;
        }
    }
    
    return baseSizeMB;
}

bool GPUAccelerator::loadModelToDevice(const std::string& modelPath, int deviceId, void** gpuModelPtr) {
    // This is a placeholder implementation. In a real implementation,
    // you would use Marian NMT APIs to load the model to GPU
    
#ifdef CUDA_AVAILABLE
    cudaSetDevice(deviceId);
    
    // Allocate GPU memory for model (simplified)
    size_t modelSizeBytes = estimateModelMemoryRequirement(modelPath) * 1024 * 1024;
    void* devicePtr = nullptr;
    
    cudaError_t error = cudaMalloc(&devicePtr, modelSizeBytes);
    if (error != cudaSuccess) {
        lastError_ = "Failed to allocate GPU memory for model: " + std::string(cudaGetErrorString(error));
        return false;
    }
    
    // In a real implementation, you would load the actual model data here
    // For now, we just return the allocated memory pointer
    *gpuModelPtr = devicePtr;
    
    return true;
#else
    lastError_ = "CUDA not available";
    return false;
#endif
}

void GPUAccelerator::unloadModelFromDevice(void* gpuModelPtr, int deviceId) {
    if (gpuModelPtr) {
#ifdef CUDA_AVAILABLE
        cudaSetDevice(deviceId);
        cudaFree(gpuModelPtr);
#endif
    }
}

bool GPUAccelerator::performGPUTranslation(void* gpuModel, const std::string& input, std::string& output, void* stream) {
    if (!gpuModel) {
        lastError_ = "Invalid GPU model pointer";
        return false;
    }
    
    // This is a placeholder implementation. In a real implementation,
    // you would use Marian NMT APIs to perform the actual translation
    
    auto startTime = std::chrono::steady_clock::now();
    
    // Simulate GPU translation processing
    output = "[GPU] " + input + " [Translated]";
    
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // Update statistics
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        currentStats_.totalProcessingTime += duration;
        if (currentStats_.translationsProcessed > 0) {
            currentStats_.averageTranslationTime = 
                std::chrono::milliseconds(currentStats_.totalProcessingTime.count() / currentStats_.translationsProcessed);
        }
    }
    
    return true;
}

void GPUAccelerator::performanceMonitoringLoop() {
    const auto interval = std::chrono::milliseconds(1000); // 1 second interval
    
    while (performanceMonitoringActive_.load()) {
        updatePerformanceStatistics();
        
        // Store historical data
        {
            std::lock_guard<std::mutex> lock(performanceHistoryMutex_);
            performanceHistory_.push_back(currentStats_);
            
            // Keep only last hour of data (3600 samples at 1 second interval)
            if (performanceHistory_.size() > 3600) {
                performanceHistory_.erase(performanceHistory_.begin());
            }
        }
        
        std::this_thread::sleep_for(interval);
    }
}

void GPUAccelerator::collectGPUMetrics() {
    if (!gpuAvailable_ || !gpuManager_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    // Update memory usage
    currentStats_.memoryUsedMB = gpuManager_->getCurrentMemoryUsageMB();
    
    // Update GPU utilization (if supported)
    currentStats_.utilizationPercent = gpuManager_->getGPUUtilization();
    if (currentStats_.utilizationPercent < 0) {
        currentStats_.utilizationPercent = 0.0f; // Not supported
    }
    
    // Update temperature (if supported)
    currentStats_.temperatureCelsius = gpuManager_->getGPUTemperature();
    if (currentStats_.temperatureCelsius < 0) {
        currentStats_.temperatureCelsius = 0.0f; // Not supported
    }
    
    // Update model count
    {
        std::lock_guard<std::mutex> modelsLock(modelsMutex_);
        currentStats_.modelsLoaded = loadedModels_.size();
    }
    
    // Update active streams count
    {
        std::lock_guard<std::mutex> sessionsLock(streamingSessionsMutex_);
        currentStats_.activeStreams = streamingSessions_.size();
    }
    
    // Calculate throughput
    if (currentStats_.totalProcessingTime.count() > 0) {
        double totalSeconds = currentStats_.totalProcessingTime.count() / 1000.0;
        currentStats_.throughputTranslationsPerSecond = 
            static_cast<double>(currentStats_.translationsProcessed) / totalSeconds;
    }
}

void GPUAccelerator::checkPerformanceThresholds() {
    if (arePerformanceThresholdsExceeded()) {
        auto alerts = getPerformanceAlerts();
        for (const auto& alert : alerts) {
            LOG_WARN("Performance Alert: " + alert);
        }
    }
}

void GPUAccelerator::cleanupExpiredSessions() {
    std::lock_guard<std::mutex> lock(streamingSessionsMutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto it = streamingSessions_.begin();
    
    while (it != streamingSessions_.end()) {
        auto timeSinceLastActivity = std::chrono::duration_cast<std::chrono::minutes>(now - it->second.lastActivity);
        if (timeSinceLastActivity.count() > 30) { // 30 minutes timeout
            LOG_INFO("Cleaning up expired streaming session: " + it->first);
            if (it->second.cudaStream) {
                releaseCudaStream(it->second.cudaStream);
            }
            it = streamingSessions_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace mt
} // namespace speechrnt