#pragma once

#include "utils/gpu_manager.hpp"
#include "utils/gpu_memory_pool.hpp"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <atomic>

namespace speechrnt {
namespace mt {

/**
 * GPU information specific to MT operations
 */
struct GPUInfo {
    int deviceId;
    std::string deviceName;
    size_t totalMemoryMB;
    size_t availableMemoryMB;
    bool isCompatible;
    std::string cudaVersion;
    int computeCapabilityMajor;
    int computeCapabilityMinor;
    int multiProcessorCount;
    bool supportsFloat16;
    bool supportsInt8;
    
    GPUInfo() : deviceId(-1), totalMemoryMB(0), availableMemoryMB(0), 
               isCompatible(false), computeCapabilityMajor(0), 
               computeCapabilityMinor(0), multiProcessorCount(0),
               supportsFloat16(false), supportsInt8(false) {}
};

/**
 * GPU performance statistics for MT operations
 */
struct GPUStats {
    float utilizationPercent;
    size_t memoryUsedMB;
    float temperatureCelsius;
    size_t translationsProcessed;
    std::chrono::milliseconds averageTranslationTime;
    std::chrono::milliseconds totalProcessingTime;
    size_t modelsLoaded;
    size_t activeStreams;
    double throughputTranslationsPerSecond;
    
    GPUStats() : utilizationPercent(0.0f), memoryUsedMB(0), 
                temperatureCelsius(0.0f), translationsProcessed(0),
                averageTranslationTime(0), totalProcessingTime(0),
                modelsLoaded(0), activeStreams(0), 
                throughputTranslationsPerSecond(0.0) {}
};

/**
 * GPU model loading information
 */
struct GPUModelInfo {
    std::string modelPath;
    std::string languagePair;
    void* gpuModelPtr;
    size_t memorySizeMB;
    std::chrono::steady_clock::time_point loadedAt;
    std::chrono::steady_clock::time_point lastUsed;
    size_t usageCount;
    bool isQuantized;
    std::string precision; // "fp32", "fp16", "int8"
    
    GPUModelInfo() : gpuModelPtr(nullptr), memorySizeMB(0), 
                    usageCount(0), isQuantized(false), precision("fp32") {}
};

/**
 * CUDA context and stream management
 */
struct CudaContext {
    void* context;
    std::vector<void*> streams;
    int deviceId;
    bool isActive;
    std::chrono::steady_clock::time_point createdAt;
    
    CudaContext() : context(nullptr), deviceId(-1), isActive(false) {}
};

/**
 * GPU Accelerator for Machine Translation operations
 * Provides GPU acceleration management specifically for MT workloads
 */
class GPUAccelerator {
public:
    GPUAccelerator();
    ~GPUAccelerator();
    
    // Initialization and discovery
    
    /**
     * Initialize GPU accelerator
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * Get all available GPUs with MT compatibility information
     * @return vector of GPU information
     */
    std::vector<GPUInfo> getAvailableGPUs() const;
    
    /**
     * Check if any GPU is available for MT operations
     * @return true if GPU available
     */
    bool isGPUAvailable() const;
    
    /**
     * Get the number of compatible GPUs
     * @return number of compatible GPUs
     */
    int getCompatibleGPUCount() const;
    
    // Device management
    
    /**
     * Select and activate a specific GPU device
     * @param deviceId GPU device ID to select
     * @return true if device selected successfully
     */
    bool selectGPU(int deviceId);
    
    /**
     * Get currently active GPU device ID
     * @return current GPU device ID, -1 if none active
     */
    int getCurrentGPUDevice() const;
    
    /**
     * Get information about currently active GPU
     * @return GPU information for current device
     */
    GPUInfo getCurrentGPUInfo() const;
    
    /**
     * Get the best GPU device for MT operations
     * @return device ID of best GPU, -1 if none suitable
     */
    int getBestGPUDevice() const;
    
    /**
     * Validate GPU device for MT compatibility
     * @param deviceId GPU device ID to validate
     * @return true if device is compatible
     */
    bool validateGPUDevice(int deviceId) const;
    
    // Memory management
    
    /**
     * Allocate GPU memory for MT operations
     * @param sizeMB Size in megabytes to allocate
     * @param tag Optional tag for tracking
     * @return true if allocation successful
     */
    bool allocateGPUMemory(size_t sizeMB, const std::string& tag = "");
    
    /**
     * Free all allocated GPU memory
     */
    void freeGPUMemory();
    
    /**
     * Get available GPU memory
     * @return available memory in MB
     */
    size_t getAvailableGPUMemory() const;
    
    /**
     * Get total GPU memory usage by MT operations
     * @return memory usage in MB
     */
    size_t getGPUMemoryUsage() const;
    
    /**
     * Check if sufficient GPU memory is available
     * @param requiredMB Required memory in MB
     * @return true if sufficient memory available
     */
    bool hasSufficientGPUMemory(size_t requiredMB) const;
    
    /**
     * Optimize GPU memory usage
     * @return true if optimization successful
     */
    bool optimizeGPUMemory();
    
    // Model operations
    
    /**
     * Load Marian model to GPU
     * @param modelPath Path to model file
     * @param languagePair Language pair identifier (e.g., "en-es")
     * @param gpuModelPtr Output pointer to GPU model
     * @return true if model loaded successfully
     */
    bool loadModelToGPU(const std::string& modelPath, const std::string& languagePair, void** gpuModelPtr);
    
    /**
     * Unload model from GPU
     * @param gpuModelPtr GPU model pointer to unload
     * @return true if model unloaded successfully
     */
    bool unloadModelFromGPU(void* gpuModelPtr);
    
    /**
     * Get information about loaded models
     * @return vector of loaded model information
     */
    std::vector<GPUModelInfo> getLoadedModels() const;
    
    /**
     * Check if model is loaded on GPU
     * @param languagePair Language pair to check
     * @return true if model is loaded
     */
    bool isModelLoadedOnGPU(const std::string& languagePair) const;
    
    /**
     * Get GPU model pointer for language pair
     * @param languagePair Language pair identifier
     * @return GPU model pointer, nullptr if not loaded
     */
    void* getGPUModelPointer(const std::string& languagePair) const;
    
    // Translation acceleration
    
    /**
     * Perform GPU-accelerated translation
     * @param gpuModel GPU model pointer
     * @param input Input text to translate
     * @param output Output translated text
     * @return true if translation successful
     */
    bool accelerateTranslation(void* gpuModel, const std::string& input, std::string& output);
    
    /**
     * Perform batch GPU-accelerated translation
     * @param gpuModel GPU model pointer
     * @param inputs Vector of input texts
     * @param outputs Vector of output translations
     * @return true if batch translation successful
     */
    bool accelerateBatchTranslation(void* gpuModel, const std::vector<std::string>& inputs, std::vector<std::string>& outputs);
    
    /**
     * Start streaming translation session
     * @param gpuModel GPU model pointer
     * @param sessionId Unique session identifier
     * @return true if session started successfully
     */
    bool startStreamingSession(void* gpuModel, const std::string& sessionId);
    
    /**
     * Process streaming translation chunk
     * @param sessionId Session identifier
     * @param inputChunk Input text chunk
     * @param outputChunk Output translation chunk
     * @return true if chunk processed successfully
     */
    bool processStreamingChunk(const std::string& sessionId, const std::string& inputChunk, std::string& outputChunk);
    
    /**
     * End streaming translation session
     * @param sessionId Session identifier
     * @return true if session ended successfully
     */
    bool endStreamingSession(const std::string& sessionId);
    
    // CUDA context and stream management
    
    /**
     * Create CUDA context for device
     * @param deviceId GPU device ID
     * @return true if context created successfully
     */
    bool createCudaContext(int deviceId);
    
    /**
     * Destroy CUDA context
     * @param deviceId GPU device ID
     * @return true if context destroyed successfully
     */
    bool destroyCudaContext(int deviceId);
    
    /**
     * Create CUDA streams for parallel processing
     * @param streamCount Number of streams to create
     * @return true if streams created successfully
     */
    bool createCudaStreams(int streamCount);
    
    /**
     * Synchronize all CUDA streams
     * @return true if synchronization successful
     */
    bool synchronizeCudaStreams();
    
    /**
     * Get available CUDA stream
     * @return CUDA stream pointer, nullptr if none available
     */
    void* getAvailableCudaStream();
    
    /**
     * Release CUDA stream back to pool
     * @param stream CUDA stream pointer to release
     */
    void releaseCudaStream(void* stream);
    
    // Error handling and recovery
    
    /**
     * Check if GPU is operational
     * @return true if GPU is working correctly
     */
    bool isGPUOperational() const;
    
    /**
     * Handle GPU error and attempt recovery
     * @param error Error message
     * @return true if recovery successful
     */
    bool handleGPUError(const std::string& error);
    
    /**
     * Enable/disable CPU fallback mode
     * @param enabled true to enable CPU fallback
     */
    void enableCPUFallback(bool enabled);
    
    /**
     * Check if CPU fallback is enabled
     * @return true if CPU fallback is enabled
     */
    bool isCPUFallbackEnabled() const;
    
    /**
     * Force fallback to CPU processing
     * @param reason Reason for fallback
     * @return true if fallback successful
     */
    bool fallbackToCPU(const std::string& reason);
    
    /**
     * Attempt to recover from GPU error
     * @return true if recovery successful
     */
    bool recoverFromGPUError();
    
    /**
     * Reset GPU device and reinitialize
     * @return true if reset successful
     */
    bool resetGPUDevice();
    
    /**
     * Get last GPU error message
     * @return error message string
     */
    std::string getLastGPUError() const;
    
    // Performance monitoring and statistics
    
    /**
     * Get current GPU performance statistics
     * @return GPU performance statistics
     */
    GPUStats getGPUStatistics() const;
    
    /**
     * Start performance monitoring
     * @param intervalMs Monitoring interval in milliseconds
     * @return true if monitoring started
     */
    bool startPerformanceMonitoring(int intervalMs = 1000);
    
    /**
     * Stop performance monitoring
     */
    void stopPerformanceMonitoring();
    
    /**
     * Update performance statistics
     */
    void updatePerformanceStatistics();
    
    /**
     * Get performance history
     * @param durationMinutes Duration in minutes to retrieve
     * @return vector of historical statistics
     */
    std::vector<GPUStats> getPerformanceHistory(int durationMinutes = 60) const;
    
    /**
     * Reset performance statistics
     */
    void resetPerformanceStatistics();
    
    /**
     * Check if performance monitoring is active
     * @return true if monitoring is active
     */
    bool isPerformanceMonitoringActive() const;
    
    /**
     * Set performance alert thresholds
     * @param memoryThresholdPercent Memory usage threshold (0-100)
     * @param temperatureThresholdC Temperature threshold in Celsius
     * @param utilizationThresholdPercent GPU utilization threshold (0-100)
     */
    void setPerformanceThresholds(float memoryThresholdPercent, float temperatureThresholdC, float utilizationThresholdPercent);
    
    /**
     * Check if any performance thresholds are exceeded
     * @return true if thresholds exceeded
     */
    bool arePerformanceThresholdsExceeded() const;
    
    /**
     * Get performance alerts
     * @return vector of current performance alerts
     */
    std::vector<std::string> getPerformanceAlerts() const;
    
    // Configuration and optimization
    
    /**
     * Set GPU memory pool configuration
     * @param poolSizeMB Memory pool size in MB
     * @param enableDefragmentation Enable memory defragmentation
     * @return true if configuration applied
     */
    bool configureMemoryPool(size_t poolSizeMB, bool enableDefragmentation = true);
    
    /**
     * Enable/disable model quantization
     * @param enabled true to enable quantization
     * @param precision Quantization precision ("fp16", "int8")
     * @return true if configuration applied
     */
    bool configureQuantization(bool enabled, const std::string& precision = "fp16");
    
    /**
     * Set batch processing configuration
     * @param maxBatchSize Maximum batch size for translations
     * @param optimalBatchSize Optimal batch size for performance
     * @return true if configuration applied
     */
    bool configureBatchProcessing(size_t maxBatchSize, size_t optimalBatchSize);
    
    /**
     * Enable/disable concurrent stream processing
     * @param enabled true to enable concurrent streams
     * @param streamCount Number of concurrent streams
     * @return true if configuration applied
     */
    bool configureConcurrentStreams(bool enabled, int streamCount = 4);
    
    /**
     * Cleanup and shutdown
     */
    void cleanup();

private:
    // Private member variables
    bool initialized_;
    bool gpuAvailable_;
    int currentDeviceId_;
    bool cpuFallbackEnabled_;
    std::string lastError_;
    
    // GPU management
    speechrnt::utils::GPUManager* gpuManager_;
    std::unique_ptr<utils::GPUMemoryPool> memoryPool_;
    std::vector<GPUInfo> availableGPUs_;
    
    // Model management
    std::unordered_map<std::string, GPUModelInfo> loadedModels_;
    mutable std::mutex modelsMutex_;
    
    // CUDA context and streams
    std::unordered_map<int, CudaContext> cudaContexts_;
    std::vector<void*> availableStreams_;
    std::vector<void*> busyStreams_;
    mutable std::mutex streamsMutex_;
    
    // Performance monitoring
    std::atomic<bool> performanceMonitoringActive_;
    std::thread performanceMonitoringThread_;
    std::vector<GPUStats> performanceHistory_;
    mutable std::mutex performanceHistoryMutex_;
    GPUStats currentStats_;
    mutable std::mutex statsMutex_;
    
    // Performance thresholds
    float memoryThresholdPercent_;
    float temperatureThresholdC_;
    float utilizationThresholdPercent_;
    
    // Configuration
    size_t memoryPoolSizeMB_;
    bool defragmentationEnabled_;
    bool quantizationEnabled_;
    std::string quantizationPrecision_;
    size_t maxBatchSize_;
    size_t optimalBatchSize_;
    bool concurrentStreamsEnabled_;
    int streamCount_;
    
    // Streaming sessions
    struct StreamingSession {
        std::string sessionId;
        void* gpuModel;
        void* cudaStream;
        std::string accumulatedInput;
        std::chrono::steady_clock::time_point lastActivity;
        bool isActive;
        
        StreamingSession() : gpuModel(nullptr), cudaStream(nullptr), isActive(false) {}
    };
    std::unordered_map<std::string, StreamingSession> streamingSessions_;
    mutable std::mutex streamingSessionsMutex_;
    
    // Thread safety
    mutable std::mutex gpuMutex_;
    
    // Private helper methods
    bool detectCompatibleGPUs();
    bool initializeGPUDevice(int deviceId);
    void updateGPUInfo(int deviceId);
    bool validateModelCompatibility(const std::string& modelPath) const;
    size_t estimateModelMemoryRequirement(const std::string& modelPath) const;
    bool loadModelToDevice(const std::string& modelPath, int deviceId, void** gpuModelPtr);
    void unloadModelFromDevice(void* gpuModelPtr, int deviceId);
    bool performGPUTranslation(void* gpuModel, const std::string& input, std::string& output, void* stream = nullptr);
    void performanceMonitoringLoop();
    void collectGPUMetrics();
    void checkPerformanceThresholds();
    void cleanupExpiredSessions();
    bool recoverGPUDevice(int deviceId);
    void logGPUError(const std::string& error, int deviceId = -1);
    std::string formatGPUError(const std::string& error, int deviceId) const;
    
    // CUDA helper methods
    bool initializeCudaContext(int deviceId);
    void cleanupCudaContext(int deviceId);
    bool createCudaStream(void** stream);
    void destroyCudaStream(void* stream);
    bool synchronizeDevice(int deviceId);
    
    // Memory management helpers
    bool allocateModelMemory(const std::string& languagePair, size_t sizeMB);
    void freeModelMemory(const std::string& languagePair);
    bool defragmentGPUMemory();
    void optimizeMemoryLayout();
    
    // Performance optimization helpers
    size_t calculateOptimalBatchSize(void* gpuModel) const;
    int calculateOptimalStreamCount() const;
    bool shouldUseQuantization(const std::string& modelPath) const;
    std::string selectOptimalPrecision(const std::string& modelPath) const;
};

} // namespace mt
} // namespace speechrnt