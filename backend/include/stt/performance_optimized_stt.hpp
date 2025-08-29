#pragma once

#include "stt/whisper_stt.hpp"
#include "stt/optimized_streaming_state.hpp"
#include "utils/memory_pool.hpp"
#include "utils/gpu_memory_pool.hpp"
#include "utils/optimized_thread_pool.hpp"
#include "utils/gpu_manager.hpp"
#include <memory>
#include <atomic>
#include <chrono>

namespace stt {

/**
 * Performance-optimized STT system that integrates all memory and threading optimizations
 * This class demonstrates the complete optimization implementation for task 18
 */
class PerformanceOptimizedSTT {
public:
    /**
     * Configuration for performance optimizations
     */
    struct OptimizationConfig {
        // Memory pool settings
        size_t audioBufferPoolSize = 100;
        size_t transcriptionResultPoolSize = 200;
        
        // GPU memory pool settings
        size_t gpuMemoryPoolSizeMB = 1024;
        bool enableGPUMemoryPool = true;
        
        // Thread pool settings
        size_t threadPoolSize = 0; // 0 = auto-detect
        bool enableWorkStealing = true;
        bool enablePriorityScheduling = true;
        
        // Streaming state settings
        size_t maxConcurrentUtterances = 50;
        bool enableAsyncProcessing = true;
        
        // Performance monitoring
        bool enablePerformanceMonitoring = true;
        std::chrono::milliseconds monitoringInterval{1000};
        
        OptimizationConfig() = default;
    };
    
    /**
     * Comprehensive performance statistics
     */
    struct PerformanceStatistics {
        // Memory statistics
        size_t totalMemoryUsageMB;
        size_t peakMemoryUsageMB;
        size_t audioBufferPoolUsage;
        size_t transcriptionResultPoolUsage;
        size_t gpuMemoryUsageMB;
        
        // Threading statistics
        size_t activeThreads;
        size_t queuedTasks;
        double averageTaskLatency;
        size_t workStealingEvents;
        
        // STT performance statistics
        size_t activeTranscriptions;
        size_t completedTranscriptions;
        double averageTranscriptionLatency;
        double averageConfidence;
        size_t totalAudioProcessed;
        
        // System health indicators
        bool memoryHealthy;
        bool threadingHealthy;
        bool gpuHealthy;
        bool overallHealthy;
        
        PerformanceStatistics() : totalMemoryUsageMB(0), peakMemoryUsageMB(0),
                                 audioBufferPoolUsage(0), transcriptionResultPoolUsage(0),
                                 gpuMemoryUsageMB(0), activeThreads(0), queuedTasks(0),
                                 averageTaskLatency(0.0), workStealingEvents(0),
                                 activeTranscriptions(0), completedTranscriptions(0),
                                 averageTranscriptionLatency(0.0), averageConfidence(0.0),
                                 totalAudioProcessed(0), memoryHealthy(false),
                                 threadingHealthy(false), gpuHealthy(false),
                                 overallHealthy(false) {}
    };

public:
    explicit PerformanceOptimizedSTT(const OptimizationConfig& config = OptimizationConfig());
    ~PerformanceOptimizedSTT();
    
    // Disable copy constructor and assignment
    PerformanceOptimizedSTT(const PerformanceOptimizedSTT&) = delete;
    PerformanceOptimizedSTT& operator=(const PerformanceOptimizedSTT&) = delete;
    
    /**
     * Initialize the optimized STT system
     */
    bool initialize();
    
    /**
     * Shutdown the optimized STT system
     */
    void shutdown();
    
    /**
     * Optimized transcription methods
     */
    std::future<TranscriptionResult> transcribeAsync(const std::vector<float>& audioData,
                                                    const std::string& language = "auto");
    
    bool startStreamingTranscription(uint32_t utteranceId,
                                   TranscriptionCallback callback,
                                   const std::string& language = "auto");
    
    bool addAudioChunk(uint32_t utteranceId, const std::vector<float>& audioData);
    
    void finalizeStreamingTranscription(uint32_t utteranceId);
    
    /**
     * Memory optimization methods
     */
    void optimizeMemoryUsage();
    void performGarbageCollection();
    void preAllocateResources(size_t expectedUtterances);
    
    /**
     * Performance monitoring and statistics
     */
    PerformanceStatistics getPerformanceStatistics() const;
    std::string getPerformanceReport() const;
    bool isSystemHealthy() const;
    
    /**
     * Configuration management
     */
    void updateConfig(const OptimizationConfig& config);
    const OptimizationConfig& getConfig() const { return config_; }
    
    /**
     * Resource management
     */
    size_t getCurrentMemoryUsageMB() const;
    size_t getGPUMemoryUsageMB() const;
    size_t getActiveTranscriptionCount() const;
    
    /**
     * Access to underlying components (for advanced usage)
     */
    std::shared_ptr<WhisperSTT> getWhisperSTT() const { return whisperSTT_; }
    std::shared_ptr<OptimizedStreamingState> getStreamingState() const { return streamingState_; }
    std::shared_ptr<utils::OptimizedThreadPool> getThreadPool() const { return threadPool_; }

private:
    // Configuration
    OptimizationConfig config_;
    
    // Core STT component
    std::shared_ptr<WhisperSTT> whisperSTT_;
    
    // Optimization components
    std::shared_ptr<OptimizedStreamingState> streamingState_;
    std::shared_ptr<utils::OptimizedThreadPool> threadPool_;
    std::shared_ptr<utils::GPUMemoryPool> gpuMemoryPool_;
    
    // Memory pools
    std::unique_ptr<utils::AudioBufferPool> audioBufferPool_;
    std::unique_ptr<utils::TranscriptionResultPool> transcriptionResultPool_;
    
    // System state
    bool initialized_;
    std::atomic<bool> shutdownRequested_;
    
    // Performance monitoring
    std::thread monitoringThread_;
    mutable std::mutex statsMutex_;
    PerformanceStatistics stats_;
    std::atomic<size_t> peakMemoryUsage_;
    std::atomic<size_t> totalTranscriptions_;
    std::atomic<double> totalTranscriptionTime_;
    
    // Helper methods
    void monitoringThreadFunction();
    void updatePerformanceStatistics();
    bool initializeGPUMemoryPool();
    bool initializeThreadPool();
    bool initializeStreamingState();
    void cleanupResources();
    
    // Optimized transcription implementation
    TranscriptionResult performOptimizedTranscription(const std::vector<float>& audioData,
                                                     const std::string& language);
    
    // Memory management helpers
    void performMemoryOptimization();
    void balanceMemoryPools();
    size_t calculateOptimalPoolSizes();
};

/**
 * Factory class for creating optimized STT instances
 */
class OptimizedSTTFactory {
public:
    /**
     * Create an optimized STT instance with automatic configuration
     */
    static std::unique_ptr<PerformanceOptimizedSTT> createOptimized();
    
    /**
     * Create an optimized STT instance with custom configuration
     */
    static std::unique_ptr<PerformanceOptimizedSTT> createOptimized(
        const PerformanceOptimizedSTT::OptimizationConfig& config);
    
    /**
     * Create an optimized STT instance for specific hardware
     */
    static std::unique_ptr<PerformanceOptimizedSTT> createForHardware(
        bool hasGPU, size_t availableMemoryMB, size_t cpuCores);
    
    /**
     * Get recommended configuration for current system
     */
    static PerformanceOptimizedSTT::OptimizationConfig getRecommendedConfig();

private:
    static size_t detectAvailableMemoryMB();
    static size_t detectCPUCores();
    static bool detectGPUAvailability();
};

} // namespace stt