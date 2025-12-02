#include "stt/optimized_streaming_state.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <sstream>

namespace stt {

OptimizedStreamingState::OptimizedStreamingState(const OptimizationConfig& config)
    : config_(config)
    , initialized_(false)
    , shutdownRequested_(false)
    , peakMemoryUsage_(0)
    , lastCleanupTime_(std::chrono::steady_clock::now()) {
}

OptimizedStreamingState::~OptimizedStreamingState() {
    shutdown();
}

bool OptimizedStreamingState::initialize() {
    if (initialized_) {
        return true;
    }
    
    // Initialize memory pools
    audioBufferPool_ = std::make_unique<utils::AudioBufferPool>(
        config_.audioBufferPoolSize, config_.audioBufferPoolSize * 2);
    
    resultPool_ = std::make_unique<utils::TranscriptionResultPool>(
        config_.resultPoolSize, config_.resultPoolSize * 2);
    
    if (!audioBufferPool_ || !resultPool_) {
        speechrnt::utils::Logger::error("Failed to initialize memory pools for streaming state");
        return false;
    }
    
    // Start worker threads if async processing is enabled
    if (config_.enableAsyncProcessing && config_.workerThreadCount > 0) {
        shutdownRequested_ = false;
        
        for (size_t i = 0; i < config_.workerThreadCount; ++i) {
            workerThreads_.emplace_back(&OptimizedStreamingState::workerThreadFunction, this);
        }
        
        speechrnt::utils::Logger::info("Started " + std::to_string(config_.workerThreadCount) + 
                           " worker threads for streaming state processing");
    }
    
    // Start cleanup thread
    cleanupThread_ = std::thread(&OptimizedStreamingState::cleanupThreadFunction, this);
    
    initialized_ = true;
    speechrnt::utils::Logger::info("OptimizedStreamingState initialized successfully");
    
    return true;
}

void OptimizedStreamingState::shutdown() {
    if (!initialized_) {
        return;
    }
    
    shutdownRequested_ = true;
    
    // Wake up all worker threads
    {
        std::lock_guard<std::mutex> lock(taskQueueMutex_);
        taskCondition_.notify_all();
    }
    
    // Wait for worker threads to finish
    for (auto& thread : workerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    workerThreads_.clear();
    
    // Wait for cleanup thread to finish
    if (cleanupThread_.joinable()) {
        cleanupThread_.join();
    }
    
    // Force cleanup all utterances
    forceCleanup();
    
    // Reset memory pools
    audioBufferPool_.reset();
    resultPool_.reset();
    
    initialized_ = false;
    speechrnt::utils::Logger::info("OptimizedStreamingState shutdown completed");
}

bool OptimizedStreamingState::createUtterance(uint32_t utteranceId) {
    if (!initialized_) {
        return false;
    }
    
    std::unique_lock<std::shared_mutex> lock(stateMapMutex_);
    
    // Check if utterance already exists
    if (utteranceStates_.find(utteranceId) != utteranceStates_.end()) {
        speechrnt::utils::Logger::debug("Utterance already exists: " + std::to_string(utteranceId));
        return true;
    }
    
    // Check utterance limit
    if (utteranceStates_.size() >= config_.maxConcurrentUtterances) {
        speechrnt::utils::Logger::warn("Maximum concurrent utterance limit reached (" + 
                           std::to_string(config_.maxConcurrentUtterances) + ")");
        
        // Find and remove oldest idle utterances
        auto idleUtterances = findIdleUtterances();
        if (!idleUtterances.empty()) {
            removeUtteranceInternal(idleUtterances[0]);
        } else {
            return false; // Cannot create new utterance
        }
    }
    
    // Create new utterance state
    auto state = std::make_shared<UtteranceState>(utteranceId);
    utteranceStates_[utteranceId] = state;
    
    updateStatistics();
    
    speechrnt::utils::Logger::debug("Created optimized utterance state for ID: " + 
                        std::to_string(utteranceId));
    
    return true;
}

bool OptimizedStreamingState::removeUtterance(uint32_t utteranceId) {
    std::unique_lock<std::shared_mutex> lock(stateMapMutex_);
    return removeUtteranceInternal(utteranceId);
}

bool OptimizedStreamingState::hasUtterance(uint32_t utteranceId) const {
    std::shared_lock<std::shared_mutex> lock(stateMapMutex_);
    return utteranceStates_.find(utteranceId) != utteranceStates_.end();
}

std::shared_ptr<OptimizedStreamingState::UtteranceState> 
OptimizedStreamingState::getUtterance(uint32_t utteranceId) {
    std::shared_lock<std::shared_mutex> lock(stateMapMutex_);
    
    auto it = utteranceStates_.find(utteranceId);
    if (it != utteranceStates_.end()) {
        it->second->updateLastActivity();
        return it->second;
    }
    
    return nullptr;
}

bool OptimizedStreamingState::addAudioChunk(uint32_t utteranceId, 
                                           const std::vector<float>& audioData) {
    if (!initialized_ || audioData.empty()) {
        return false;
    }
    
    auto state = getUtterance(utteranceId);
    if (!state || !state->isActive) {
        return false;
    }
    
    // Get audio buffer from pool
    auto buffer = audioBufferPool_->acquireBuffer(audioData.size());
    if (!buffer) {
        speechrnt::utils::Logger::warn("Failed to acquire audio buffer from pool");
        return false;
    }
    
    // Copy audio data
    buffer->data = audioData;
    buffer->lastUsed = std::chrono::steady_clock::now();
    
    // Add to utterance state
    if (!state->currentBuffer) {
        state->currentBuffer = buffer;
    } else {
        // Queue the buffer for processing
        state->audioChunks.push(buffer);
    }
    
    state->totalAudioSamples += audioData.size();
    state->processedChunks++;
    state->updateLastActivity();
    
    return true;
}

utils::AudioBufferPool::AudioBufferPtr 
OptimizedStreamingState::getAudioBuffer(uint32_t utteranceId) {
    auto state = getUtterance(utteranceId);
    if (!state) {
        return nullptr;
    }
    
    // Return current buffer and move to next queued buffer
    auto buffer = state->currentBuffer;
    
    if (!state->audioChunks.empty()) {
        state->currentBuffer = state->audioChunks.front();
        state->audioChunks.pop();
    } else {
        state->currentBuffer.reset();
    }
    
    state->updateLastActivity();
    return buffer;
}

bool OptimizedStreamingState::finalizeAudioBuffer(uint32_t utteranceId) {
    auto state = getUtterance(utteranceId);
    if (!state) {
        return false;
    }
    
    state->isActive = false;
    state->updateLastActivity();
    
    speechrnt::utils::Logger::debug("Finalized audio buffer for utterance: " + 
                        std::to_string(utteranceId));
    
    return true;
}

bool OptimizedStreamingState::setTranscriptionResult(uint32_t utteranceId,
                                                    const std::string& text,
                                                    float confidence,
                                                    bool isPartial) {
    auto state = getUtterance(utteranceId);
    if (!state) {
        return false;
    }
    
    // Get result from pool
    auto result = resultPool_->acquireResult();
    if (!result) {
        speechrnt::utils::Logger::warn("Failed to acquire transcription result from pool");
        return false;
    }
    
    // Set result data
    result->text = text;
    result->confidence = confidence;
    result->is_partial = isPartial;
    result->start_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        state->startTime.time_since_epoch()).count();
    result->end_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    // Update state
    state->lastResult = result;
    state->transcriptionCount++;
    
    // Update average confidence
    double currentAvg = state->averageConfidence.load();
    size_t count = state->transcriptionCount.load();
    double newAvg = (currentAvg * (count - 1) + confidence) / count;
    state->averageConfidence = newAvg;
    
    state->updateLastActivity();
    
    return true;
}

utils::TranscriptionResultPool::TranscriptionResultPtr 
OptimizedStreamingState::getLastResult(uint32_t utteranceId) {
    auto state = getUtterance(utteranceId);
    if (!state) {
        return nullptr;
    }
    
    state->updateLastActivity();
    return state->lastResult;
}

void OptimizedStreamingState::performCleanup() {
    if (!initialized_) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastCleanup = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastCleanupTime_).count();
    
    if (timeSinceLastCleanup < static_cast<long>(config_.stateCleanupIntervalMs)) {
        return;
    }
    
    // Find idle utterances
    auto idleUtterances = findIdleUtterances();
    
    {
        std::unique_lock<std::shared_mutex> lock(stateMapMutex_);
        
        for (uint32_t utteranceId : idleUtterances) {
            removeUtteranceInternal(utteranceId);
        }
    }
    
    // Cleanup memory pools
    if (audioBufferPool_) {
        audioBufferPool_->cleanup();
    }
    
    if (resultPool_) {
        resultPool_->cleanup();
    }
    
    lastCleanupTime_ = now;
    
    if (!idleUtterances.empty()) {
        speechrnt::utils::Logger::info("Cleaned up " + std::to_string(idleUtterances.size()) + 
                           " idle utterance states");
    }
    
    updateStatistics();
}

void OptimizedStreamingState::forceCleanup() {
    std::unique_lock<std::shared_mutex> lock(stateMapMutex_);
    
    size_t initialCount = utteranceStates_.size();
    utteranceStates_.clear();
    
    // Force cleanup memory pools
    if (audioBufferPool_) {
        audioBufferPool_->forceCleanup();
    }
    
    if (resultPool_) {
        resultPool_->forceCleanup();
    }
    
    updateStatistics();
    
    speechrnt::utils::Logger::info("Force cleanup removed " + std::to_string(initialCount) + 
                       " utterance states");
}

void OptimizedStreamingState::optimizeMemoryUsage() {
    if (!initialized_) {
        return;
    }
    
    // Schedule optimization task
    scheduleTask([this]() {
        performCleanup();
        
        // Additional optimization: defragment memory pools if supported
        // This would be implemented in a more advanced memory pool
        
        speechrnt::utils::Logger::debug("Memory usage optimization completed");
    });
}

OptimizedStreamingState::StateStatistics OptimizedStreamingState::getStatistics() const {
    std::shared_lock<std::shared_mutex> stateLock(stateMapMutex_);
    std::lock_guard<std::mutex> statsLock(statsMutex_);
    
    StateStatistics stats = stats_;
    
    // Update current values
    stats.totalUtterances = utteranceStates_.size();
    stats.activeUtterances = 0;
    stats.totalAudioProcessed = 0;
    
    double totalDuration = 0.0;
    double totalLatency = 0.0;
    size_t latencyCount = 0;
    
    for (const auto& pair : utteranceStates_) {
        const auto& state = pair.second;
        
        if (state->isActive) {
            stats.activeUtterances++;
        }
        
        stats.totalAudioProcessed += state->totalAudioSamples.load();
        
        // Calculate duration
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - state->startTime).count();
        totalDuration += duration;
        
        // Add to average latency calculation
        double stateLatency = state->averageLatency.load();
        if (stateLatency > 0.0) {
            totalLatency += stateLatency;
            latencyCount++;
        }
    }
    
    if (stats.totalUtterances > 0) {
        stats.averageUtteranceDuration = totalDuration / stats.totalUtterances / 1000.0; // Convert to seconds
    }
    
    if (latencyCount > 0) {
        stats.averageProcessingLatency = totalLatency / latencyCount;
    }
    
    // Calculate memory usage
    size_t totalMemory = 0;
    for (const auto& pair : utteranceStates_) {
        totalMemory += pair.second->getMemoryUsageBytes();
    }
    
    stats.totalMemoryUsageMB = totalMemory / (1024 * 1024);
    stats.peakMemoryUsageMB = peakMemoryUsage_.load() / (1024 * 1024);
    
    return stats;
}

std::vector<uint32_t> OptimizedStreamingState::getActiveUtterances() const {
    std::shared_lock<std::shared_mutex> lock(stateMapMutex_);
    
    std::vector<uint32_t> activeIds;
    for (const auto& pair : utteranceStates_) {
        if (pair.second->isActive) {
            activeIds.push_back(pair.first);
        }
    }
    
    return activeIds;
}

size_t OptimizedStreamingState::getUtteranceCount() const {
    std::shared_lock<std::shared_mutex> lock(stateMapMutex_);
    return utteranceStates_.size();
}

bool OptimizedStreamingState::isHealthy() const {
    auto stats = getStatistics();
    
    // Consider healthy if:
    // 1. Not exceeding maximum utterances
    // 2. Memory usage is reasonable
    // 3. Average latency is acceptable
    
    bool utteranceHealthy = stats.totalUtterances <= config_.maxConcurrentUtterances;
    bool memoryHealthy = stats.totalMemoryUsageMB < 1024; // Less than 1GB
    bool latencyHealthy = stats.averageProcessingLatency < 1000.0; // Less than 1 second
    
    return initialized_ && utteranceHealthy && memoryHealthy && latencyHealthy;
}

std::string OptimizedStreamingState::getHealthStatus() const {
    auto stats = getStatistics();
    
    std::ostringstream oss;
    oss << "OptimizedStreamingState Health Status:\n";
    oss << "  Active Utterances: " << stats.activeUtterances << "/" << config_.maxConcurrentUtterances << "\n";
    oss << "  Total Utterances: " << stats.totalUtterances << "\n";
    oss << "  Memory Usage: " << stats.totalMemoryUsageMB << "MB (Peak: " << stats.peakMemoryUsageMB << "MB)\n";
    oss << "  Average Processing Latency: " << stats.averageProcessingLatency << "ms\n";
    oss << "  Total Audio Processed: " << (stats.totalAudioProcessed / 16000.0) << " seconds\n";
    oss << "  Average Utterance Duration: " << stats.averageUtteranceDuration << " seconds\n";
    oss << "  Cleanup Operations: " << stats.cleanupOperations << "\n";
    
    // Memory pool statistics
    if (audioBufferPool_) {
        auto poolStats = audioBufferPool_->getStatistics();
        oss << "  Audio Buffer Pool: " << poolStats.totalInUse << "/" << poolStats.totalAllocated << " buffers\n";
    }
    
    if (resultPool_) {
        auto poolStats = resultPool_->getStatistics();
        oss << "  Result Pool: " << poolStats.totalInUse << "/" << poolStats.totalAllocated << " results\n";
    }
    
    oss << "  Status: " << (isHealthy() ? "HEALTHY" : "UNHEALTHY");
    
    return oss.str();
}

void OptimizedStreamingState::updateConfig(const OptimizationConfig& config) {
    config_ = config;
    speechrnt::utils::Logger::info("OptimizedStreamingState configuration updated");
}

// Private helper methods
void OptimizedStreamingState::workerThreadFunction() {
    speechrnt::utils::Logger::debug("Worker thread started for streaming state processing");
    
    while (!shutdownRequested_) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(taskQueueMutex_);
            taskCondition_.wait(lock, [this] { 
                return !taskQueue_.empty() || shutdownRequested_; 
            });
            
            if (shutdownRequested_) {
                break;
            }
            
            if (!taskQueue_.empty()) {
                task = taskQueue_.front();
                taskQueue_.pop();
            }
        }
        
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                speechrnt::utils::Logger::error("Worker thread task failed: " + std::string(e.what()));
            }
        }
    }
    
    speechrnt::utils::Logger::debug("Worker thread stopped");
}

void OptimizedStreamingState::cleanupThreadFunction() {
    speechrnt::utils::Logger::debug("Cleanup thread started for streaming state");
    
    while (!shutdownRequested_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.stateCleanupIntervalMs));
        
        if (!shutdownRequested_) {
            performCleanup();
        }
    }
    
    speechrnt::utils::Logger::debug("Cleanup thread stopped");
}

void OptimizedStreamingState::updateStatistics() {
    size_t currentMemory = 0;
    
    for (const auto& pair : utteranceStates_) {
        currentMemory += pair.second->getMemoryUsageBytes();
    }
    
    if (currentMemory > peakMemoryUsage_) {
        peakMemoryUsage_ = currentMemory;
    }
    
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.cleanupOperations++;
}

std::vector<uint32_t> OptimizedStreamingState::findIdleUtterances() const {
    std::vector<uint32_t> idleUtterances;
    
    for (const auto& pair : utteranceStates_) {
        const auto& state = pair.second;
        
        if (!state->isActive && state->getIdleTimeSeconds() > (config_.maxIdleTimeMs / 1000.0)) {
            idleUtterances.push_back(pair.first);
        }
    }
    
    return idleUtterances;
}

bool OptimizedStreamingState::removeUtteranceInternal(uint32_t utteranceId) {
    auto it = utteranceStates_.find(utteranceId);
    if (it != utteranceStates_.end()) {
        utteranceStates_.erase(it);
        speechrnt::utils::Logger::debug("Removed utterance state: " + std::to_string(utteranceId));
        return true;
    }
    return false;
}

void OptimizedStreamingState::scheduleTask(std::function<void()> task) {
    if (!config_.enableAsyncProcessing) {
        // Execute immediately if async processing is disabled
        task();
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(taskQueueMutex_);
        taskQueue_.push(task);
    }
    
    taskCondition_.notify_one();
}

} // namespace stt