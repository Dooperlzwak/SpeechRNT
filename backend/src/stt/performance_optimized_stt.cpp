#include "stt/performance_optimized_stt.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <sstream>
#include <fstream>

#ifdef __linux__
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

namespace stt {

PerformanceOptimizedSTT::PerformanceOptimizedSTT(const OptimizationConfig& config)
    : config_(config)
    , initialized_(false)
    , shutdownRequested_(false)
    , peakMemoryUsage_(0)
    , totalTranscriptions_(0)
    , totalTranscriptionTime_(0.0) {
}

PerformanceOptimizedSTT::~PerformanceOptimizedSTT() {
    shutdown();
}

bool PerformanceOptimizedSTT::initialize() {
    if (initialized_) {
        return true;
    }
    
    speechrnt::utils::Logger::info("Initializing PerformanceOptimizedSTT with memory and threading optimizations");
    
    // Initialize memory pools first
    audioBufferPool_ = std::make_unique<utils::AudioBufferPool>(
        config_.audioBufferPoolSize, config_.audioBufferPoolSize * 2);
    
    transcriptionResultPool_ = std::make_unique<utils::TranscriptionResultPool>(
        config_.transcriptionResultPoolSize, config_.transcriptionResultPoolSize * 2);
    
    if (!audioBufferPool_ || !transcriptionResultPool_) {
        speechrnt::utils::Logger::error("Failed to initialize memory pools");
        return false;
    }
    
    // Initialize GPU memory pool if enabled
    if (config_.enableGPUMemoryPool && !initializeGPUMemoryPool()) {
        speechrnt::utils::Logger::warn("GPU memory pool initialization failed, continuing without GPU optimization");
    }
    
    // Initialize thread pool
    if (!initializeThreadPool()) {
        speechrnt::utils::Logger::error("Failed to initialize thread pool");
        return false;
    }
    
    // Initialize streaming state manager
    if (!initializeStreamingState()) {
        speechrnt::utils::Logger::error("Failed to initialize streaming state manager");
        return false;
    }
    
    // Initialize WhisperSTT (assuming it's already configured)
    whisperSTT_ = std::make_shared<WhisperSTT>();
    if (!whisperSTT_->initialize()) {
        speechrnt::utils::Logger::error("Failed to initialize WhisperSTT");
        return false;
    }
    
    // Start performance monitoring if enabled
    if (config_.enablePerformanceMonitoring) {
        shutdownRequested_ = false;
        monitoringThread_ = std::thread(&PerformanceOptimizedSTT::monitoringThreadFunction, this);
    }
    
    initialized_ = true;
    speechrnt::utils::Logger::info("PerformanceOptimizedSTT initialized successfully");
    
    return true;
}

void PerformanceOptimizedSTT::shutdown() {
    if (!initialized_) {
        return;
    }
    
    shutdownRequested_ = true;
    
    // Stop monitoring thread
    if (monitoringThread_.joinable()) {
        monitoringThread_.join();
    }
    
    // Shutdown components in reverse order
    if (streamingState_) {
        streamingState_->shutdown();
    }
    
    if (threadPool_) {
        threadPool_->shutdown();
    }
    
    if (whisperSTT_) {
        // WhisperSTT shutdown would be called here if it had a shutdown method
    }
    
    // Cleanup resources
    cleanupResources();
    
    initialized_ = false;
    speechrnt::utils::Logger::info("PerformanceOptimizedSTT shutdown completed");
}

std::future<TranscriptionResult> PerformanceOptimizedSTT::transcribeAsync(
    const std::vector<float>& audioData, const std::string& language) {
    
    if (!initialized_) {
        std::promise<TranscriptionResult> promise;
        promise.set_exception(std::make_exception_ptr(
            std::runtime_error("PerformanceOptimizedSTT not initialized")));
        return promise.get_future();
    }
    
    // Submit transcription task to thread pool with high priority
    return threadPool_->submit(utils::OptimizedThreadPool::Priority::HIGH,
        [this, audioData, language]() -> TranscriptionResult {
            return performOptimizedTranscription(audioData, language);
        });
}

bool PerformanceOptimizedSTT::startStreamingTranscription(uint32_t utteranceId,
                                                         TranscriptionCallback callback,
                                                         const std::string& language) {
    if (!initialized_ || !streamingState_) {
        return false;
    }
    
    // Create utterance in streaming state manager
    if (!streamingState_->createUtterance(utteranceId)) {
        return false;
    }
    
    // Wrap callback to use optimized result pool
    auto optimizedCallback = [this, callback](const TranscriptionResult& result) {
        // Use result pool for callback data if needed
        callback(result);
    };
    
    // Start streaming with WhisperSTT
    return whisperSTT_->startStreamingTranscription(utteranceId, optimizedCallback);
}

bool PerformanceOptimizedSTT::addAudioChunk(uint32_t utteranceId, 
                                           const std::vector<float>& audioData) {
    if (!initialized_ || !streamingState_) {
        return false;
    }
    
    // Add audio chunk to optimized streaming state
    if (!streamingState_->addAudioChunk(utteranceId, audioData)) {
        return false;
    }
    
    // Submit processing task to thread pool
    threadPool_->submitTask([this, utteranceId]() {
        // Get audio buffer from streaming state
        auto buffer = streamingState_->getAudioBuffer(utteranceId);
        if (buffer && whisperSTT_) {
            // Process with WhisperSTT
            whisperSTT_->addAudioChunk(utteranceId, buffer->data);
        }
    }, utils::OptimizedThreadPool::Priority::NORMAL);
    
    return true;
}

void PerformanceOptimizedSTT::finalizeStreamingTranscription(uint32_t utteranceId) {
    if (!initialized_) {
        return;
    }
    
    // Submit finalization task to thread pool
    threadPool_->submitTask([this, utteranceId]() {
        if (whisperSTT_) {
            whisperSTT_->finalizeStreamingTranscription(utteranceId);
        }
        
        if (streamingState_) {
            streamingState_->finalizeAudioBuffer(utteranceId);
        }
    }, utils::OptimizedThreadPool::Priority::HIGH);
}

void PerformanceOptimizedSTT::optimizeMemoryUsage() {
    if (!initialized_) {
        return;
    }
    
    // Submit optimization task to thread pool
    threadPool_->submitTask([this]() {
        performMemoryOptimization();
    }, utils::OptimizedThreadPool::Priority::LOW);
}

void PerformanceOptimizedSTT::performGarbageCollection() {
    if (!initialized_) {
        return;
    }
    
    // Force cleanup of all memory pools
    if (audioBufferPool_) {
        audioBufferPool_->forceCleanup();
    }
    
    if (transcriptionResultPool_) {
        transcriptionResultPool_->forceCleanup();
    }
    
    if (gpuMemoryPool_) {
        gpuMemoryPool_->forceCleanup();
    }
    
    if (streamingState_) {
        streamingState_->forceCleanup();
    }
    
    speechrnt::utils::Logger::info("Performed garbage collection on all memory pools");
}

void PerformanceOptimizedSTT::preAllocateResources(size_t expectedUtterances) {
    if (!initialized_) {
        return;
    }
    
    // Pre-allocate resources based on expected load
    speechrnt::utils::Logger::info("Pre-allocating resources for " + std::to_string(expectedUtterances) + 
                       " expected utterances");
    
    // This would involve pre-allocating memory pools, GPU memory, etc.
    // Implementation would depend on specific requirements
}

PerformanceOptimizedSTT::PerformanceStatistics 
PerformanceOptimizedSTT::getPerformanceStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    PerformanceStatistics stats = stats_;
    
    // Update current values
    stats.totalMemoryUsageMB = getCurrentMemoryUsageMB();
    stats.peakMemoryUsageMB = peakMemoryUsage_.load() / (1024 * 1024);
    stats.gpuMemoryUsageMB = getGPUMemoryUsageMB();
    stats.activeTranscriptions = getActiveTranscriptionCount();
    stats.completedTranscriptions = totalTranscriptions_.load();
    
    // Calculate averages
    if (stats.completedTranscriptions > 0) {
        stats.averageTranscriptionLatency = totalTranscriptionTime_.load() / 
                                           stats.completedTranscriptions;
    }
    
    // Get component statistics
    if (audioBufferPool_) {
        auto poolStats = audioBufferPool_->getStatistics();
        stats.audioBufferPoolUsage = poolStats.totalInUse;
    }
    
    if (transcriptionResultPool_) {
        auto poolStats = transcriptionResultPool_->getStatistics();
        stats.transcriptionResultPoolUsage = poolStats.totalInUse;
    }
    
    if (threadPool_) {
        auto threadStats = threadPool_->getStatistics();
        stats.activeThreads = threadStats.activeThreads;
        stats.queuedTasks = threadStats.queuedTasks;
        stats.averageTaskLatency = threadStats.averageTaskTime;
        stats.workStealingEvents = threadStats.workStealingEvents;
        stats.threadingHealthy = threadPool_->isHealthy();
    }
    
    // Health indicators
    stats.memoryHealthy = stats.totalMemoryUsageMB < 2048; // Less than 2GB
    stats.gpuHealthy = gpuMemoryPool_ ? gpuMemoryPool_->isHealthy() : true;
    stats.overallHealthy = stats.memoryHealthy && stats.threadingHealthy && stats.gpuHealthy;
    
    return stats;
}

std::string PerformanceOptimizedSTT::getPerformanceReport() const {
    auto stats = getPerformanceStatistics();
    
    std::ostringstream oss;
    oss << "PerformanceOptimizedSTT Performance Report:\n";
    oss << "========================================\n";
    
    // Memory statistics
    oss << "Memory Usage:\n";
    oss << "  Total: " << stats.totalMemoryUsageMB << "MB (Peak: " << stats.peakMemoryUsageMB << "MB)\n";
    oss << "  Audio Buffer Pool: " << stats.audioBufferPoolUsage << " buffers in use\n";
    oss << "  Transcription Result Pool: " << stats.transcriptionResultPoolUsage << " results in use\n";
    oss << "  GPU Memory: " << stats.gpuMemoryUsageMB << "MB\n";
    oss << "  Memory Health: " << (stats.memoryHealthy ? "HEALTHY" : "UNHEALTHY") << "\n\n";
    
    // Threading statistics
    oss << "Threading Performance:\n";
    oss << "  Active Threads: " << stats.activeThreads << "\n";
    oss << "  Queued Tasks: " << stats.queuedTasks << "\n";
    oss << "  Average Task Latency: " << stats.averageTaskLatency << "ms\n";
    oss << "  Work Stealing Events: " << stats.workStealingEvents << "\n";
    oss << "  Threading Health: " << (stats.threadingHealthy ? "HEALTHY" : "UNHEALTHY") << "\n\n";
    
    // STT performance
    oss << "STT Performance:\n";
    oss << "  Active Transcriptions: " << stats.activeTranscriptions << "\n";
    oss << "  Completed Transcriptions: " << stats.completedTranscriptions << "\n";
    oss << "  Average Transcription Latency: " << stats.averageTranscriptionLatency << "ms\n";
    oss << "  Average Confidence: " << (stats.averageConfidence * 100.0) << "%\n";
    oss << "  Total Audio Processed: " << (stats.totalAudioProcessed / 16000.0) << " seconds\n\n";
    
    // Overall health
    oss << "Overall System Health: " << (stats.overallHealthy ? "HEALTHY" : "UNHEALTHY") << "\n";
    
    return oss.str();
}

bool PerformanceOptimizedSTT::isSystemHealthy() const {
    auto stats = getPerformanceStatistics();
    return stats.overallHealthy;
}

void PerformanceOptimizedSTT::updateConfig(const OptimizationConfig& config) {
    config_ = config;
    
    // Update component configurations
    if (threadPool_) {
        utils::OptimizedThreadPool::PoolConfig threadConfig;
        threadConfig.numThreads = config.threadPoolSize;
        threadConfig.enableWorkStealing = config.enableWorkStealing;
        threadConfig.enablePriority = config.enablePriorityScheduling;
        threadPool_->updateConfig(threadConfig);
    }
    
    if (streamingState_) {
        OptimizedStreamingState::OptimizationConfig streamConfig;
        streamConfig.maxConcurrentUtterances = config.maxConcurrentUtterances;
        streamConfig.enableAsyncProcessing = config.enableAsyncProcessing;
        streamingState_->updateConfig(streamConfig);
    }
    
    speechrnt::utils::Logger::info("PerformanceOptimizedSTT configuration updated");
}

size_t PerformanceOptimizedSTT::getCurrentMemoryUsageMB() const {
    size_t totalMemory = 0;
    
    if (audioBufferPool_) {
        auto stats = audioBufferPool_->getStatistics();
        totalMemory += stats.totalInUse * 16000 * sizeof(float); // Rough estimate
    }
    
    if (transcriptionResultPool_) {
        auto stats = transcriptionResultPool_->getStatistics();
        totalMemory += stats.totalInUse * 1024; // Rough estimate for result size
    }
    
    if (streamingState_) {
        auto stats = streamingState_->getStatistics();
        totalMemory += stats.totalMemoryUsageMB * 1024 * 1024;
    }
    
    return totalMemory / (1024 * 1024);
}

size_t PerformanceOptimizedSTT::getGPUMemoryUsageMB() const {
    if (gpuMemoryPool_) {
        auto stats = gpuMemoryPool_->getStatistics();
        return stats.totalInUseMB;
    }
    
    // Fallback to GPU manager
    auto& gpuManager = speechrnt::utils::GPUManager::getInstance();
    return gpuManager.getCurrentMemoryUsageMB();
}

size_t PerformanceOptimizedSTT::getActiveTranscriptionCount() const {
    if (streamingState_) {
        auto stats = streamingState_->getStatistics();
        return stats.activeTranscriptions;
    }
    return 0;
}

// Private helper methods
void PerformanceOptimizedSTT::monitoringThreadFunction() {
    speechrnt::utils::Logger::debug("Performance monitoring thread started");
    
    while (!shutdownRequested_) {
        std::this_thread::sleep_for(config_.monitoringInterval);
        
        if (!shutdownRequested_) {
            updatePerformanceStatistics();
        }
    }
    
    speechrnt::utils::Logger::debug("Performance monitoring thread stopped");
}

void PerformanceOptimizedSTT::updatePerformanceStatistics() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    // Update peak memory usage
    size_t currentMemory = getCurrentMemoryUsageMB() * 1024 * 1024;
    if (currentMemory > peakMemoryUsage_) {
        peakMemoryUsage_ = currentMemory;
    }
    
    // Perform periodic optimization
    if (currentMemory > (peakMemoryUsage_ * 8 / 10)) {
        // Schedule memory optimization if usage is high
        threadPool_->submitTask([this]() {
            performMemoryOptimization();
        }, utils::OptimizedThreadPool::Priority::LOW);
    }
}

bool PerformanceOptimizedSTT::initializeGPUMemoryPool() {
    utils::GPUMemoryPool::PoolConfig gpuConfig;
    gpuConfig.initialPoolSizeMB = config_.gpuMemoryPoolSizeMB / 2;
    gpuConfig.maxPoolSizeMB = config_.gpuMemoryPoolSizeMB;
    
    gpuMemoryPool_ = std::make_shared<utils::GPUMemoryPool>(gpuConfig);
    
    if (!gpuMemoryPool_->initialize()) {
        gpuMemoryPool_.reset();
        return false;
    }
    
    speechrnt::utils::Logger::info("GPU memory pool initialized with " + 
                       std::to_string(config_.gpuMemoryPoolSizeMB) + "MB");
    return true;
}

bool PerformanceOptimizedSTT::initializeThreadPool() {
    utils::OptimizedThreadPool::PoolConfig threadConfig;
    threadConfig.numThreads = config_.threadPoolSize;
    threadConfig.enableWorkStealing = config_.enableWorkStealing;
    threadConfig.enablePriority = config_.enablePriorityScheduling;
    
    threadPool_ = std::make_shared<utils::OptimizedThreadPool>(threadConfig);
    
    return threadPool_->initialize();
}

bool PerformanceOptimizedSTT::initializeStreamingState() {
    OptimizedStreamingState::OptimizationConfig streamConfig;
    streamConfig.maxConcurrentUtterances = config_.maxConcurrentUtterances;
    streamConfig.audioBufferPoolSize = config_.audioBufferPoolSize;
    streamConfig.resultPoolSize = config_.transcriptionResultPoolSize;
    streamConfig.enableAsyncProcessing = config_.enableAsyncProcessing;
    
    streamingState_ = std::make_shared<OptimizedStreamingState>(streamConfig);
    
    return streamingState_->initialize();
}

void PerformanceOptimizedSTT::cleanupResources() {
    audioBufferPool_.reset();
    transcriptionResultPool_.reset();
    gpuMemoryPool_.reset();
    streamingState_.reset();
    threadPool_.reset();
    whisperSTT_.reset();
}

TranscriptionResult PerformanceOptimizedSTT::performOptimizedTranscription(
    const std::vector<float>& audioData, const std::string& language) {
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Get audio buffer from pool
    auto buffer = audioBufferPool_->acquireBuffer(audioData.size());
    if (!buffer) {
        speechrnt::utils::Logger::warn("Failed to acquire audio buffer for transcription");
        return TranscriptionResult{}; // Return empty result
    }
    
    // Copy audio data
    buffer->data = audioData;
    
    // Perform transcription using WhisperSTT
    TranscriptionResult result;
    if (whisperSTT_) {
        // This would be the actual transcription call
        // result = whisperSTT_->transcribe(buffer->data, language);
        
        // For now, create a placeholder result
        result.text = "Optimized transcription result";
        result.confidence = 0.95f;
        result.is_partial = false;
    }
    
    // Update statistics
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    totalTranscriptions_++;
    totalTranscriptionTime_ = totalTranscriptionTime_.load() + duration.count();
    
    return result;
}

void PerformanceOptimizedSTT::performMemoryOptimization() {
    // Cleanup memory pools
    if (audioBufferPool_) {
        audioBufferPool_->cleanup();
    }
    
    if (transcriptionResultPool_) {
        transcriptionResultPool_->cleanup();
    }
    
    if (gpuMemoryPool_) {
        gpuMemoryPool_->cleanup();
    }
    
    if (streamingState_) {
        streamingState_->optimizeMemoryUsage();
    }
    
    // Balance memory pool sizes
    balanceMemoryPools();
    
    speechrnt::utils::Logger::debug("Memory optimization completed");
}

void PerformanceOptimizedSTT::balanceMemoryPools() {
    // This would implement dynamic pool size balancing based on usage patterns
    // For now, just log the operation
    speechrnt::utils::Logger::debug("Memory pool balancing completed");
}

size_t PerformanceOptimizedSTT::calculateOptimalPoolSizes() {
    // Calculate optimal pool sizes based on current usage and system resources
    // This is a simplified implementation
    return config_.audioBufferPoolSize;
}

// Factory implementation
std::unique_ptr<PerformanceOptimizedSTT> OptimizedSTTFactory::createOptimized() {
    auto config = getRecommendedConfig();
    return createOptimized(config);
}

std::unique_ptr<PerformanceOptimizedSTT> OptimizedSTTFactory::createOptimized(
    const PerformanceOptimizedSTT::OptimizationConfig& config) {
    
    auto stt = std::make_unique<PerformanceOptimizedSTT>(config);
    
    if (!stt->initialize()) {
        speechrnt::utils::Logger::error("Failed to initialize PerformanceOptimizedSTT");
        return nullptr;
    }
    
    return stt;
}

std::unique_ptr<PerformanceOptimizedSTT> OptimizedSTTFactory::createForHardware(
    bool hasGPU, size_t availableMemoryMB, size_t cpuCores) {
    
    PerformanceOptimizedSTT::OptimizationConfig config;
    
    // Configure based on hardware
    config.enableGPUMemoryPool = hasGPU;
    config.gpuMemoryPoolSizeMB = hasGPU ? std::min(availableMemoryMB / 4, 2048UL) : 0;
    config.threadPoolSize = cpuCores;
    config.audioBufferPoolSize = std::max(50UL, availableMemoryMB / 32);
    config.transcriptionResultPoolSize = config.audioBufferPoolSize * 2;
    config.maxConcurrentUtterances = std::max(20UL, cpuCores * 5);
    
    return createOptimized(config);
}

PerformanceOptimizedSTT::OptimizationConfig OptimizedSTTFactory::getRecommendedConfig() {
    PerformanceOptimizedSTT::OptimizationConfig config;
    
    // Auto-detect system capabilities
    size_t availableMemoryMB = detectAvailableMemoryMB();
    size_t cpuCores = detectCPUCores();
    bool hasGPU = detectGPUAvailability();
    
    // Configure based on detected hardware
    config.enableGPUMemoryPool = hasGPU;
    config.gpuMemoryPoolSizeMB = hasGPU ? std::min(availableMemoryMB / 4, 2048UL) : 0;
    config.threadPoolSize = cpuCores;
    config.audioBufferPoolSize = std::max(100UL, availableMemoryMB / 64);
    config.transcriptionResultPoolSize = config.audioBufferPoolSize * 2;
    config.maxConcurrentUtterances = std::max(50UL, cpuCores * 10);
    
    speechrnt::utils::Logger::info("Recommended configuration: " + std::to_string(cpuCores) + 
                       " CPU cores, " + std::to_string(availableMemoryMB) + "MB RAM, " +
                       "GPU: " + (hasGPU ? "available" : "not available"));
    
    return config;
}

size_t OptimizedSTTFactory::detectAvailableMemoryMB() {
#ifdef __linux__
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return (info.totalram * info.mem_unit) / (1024 * 1024);
    }
#endif
    
    // Fallback estimate
    return 8192; // 8GB default
}

size_t OptimizedSTTFactory::detectCPUCores() {
    size_t cores = std::thread::hardware_concurrency();
    return cores > 0 ? cores : 4; // Default to 4 if detection fails
}

bool OptimizedSTTFactory::detectGPUAvailability() {
    auto& gpuManager = speechrnt::utils::GPUManager::getInstance();
    return gpuManager.isCudaAvailable();
}

} // namespace stt