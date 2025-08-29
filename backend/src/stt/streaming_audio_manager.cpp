#include "stt/streaming_audio_manager.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <chrono>

namespace stt {

StreamingAudioManager::StreamingAudioManager(std::shared_ptr<WhisperSTT> whisperSTT)
    : whisperSTT_(whisperSTT)
    , isInitialized_(false) {
    
    // Configure AudioBufferManager for streaming STT
    audio::AudioBufferManager::BufferConfig config;
    config.maxBufferSizeMB = 8;           // 8MB per utterance (about 2 minutes of audio)
    config.maxUtterances = 20;            // Support up to 20 concurrent utterances
    config.cleanupIntervalMs = 2000;      // Cleanup every 2 seconds
    config.maxIdleTimeMs = 30000;         // Remove idle buffers after 30 seconds
    config.enableCircularBuffer = true;   // Use circular buffers for continuous streaming
    
    audioBufferManager_ = std::make_unique<audio::AudioBufferManager>(config);
    
    utils::Logger::info("StreamingAudioManager created");
}

StreamingAudioManager::~StreamingAudioManager() {
    stopAllTranscriptions();
    utils::Logger::info("StreamingAudioManager destroyed");
}

bool StreamingAudioManager::initialize() {
    if (!whisperSTT_ || !whisperSTT_->isInitialized()) {
        utils::Logger::error("WhisperSTT not initialized");
        return false;
    }
    
    if (!audioBufferManager_) {
        utils::Logger::error("AudioBufferManager not available");
        return false;
    }
    
    isInitialized_ = true;
    utils::Logger::info("StreamingAudioManager initialized successfully");
    return true;
}

bool StreamingAudioManager::startStreamingTranscription(uint32_t utteranceId, 
                                                       TranscriptionCallback callback,
                                                       const StreamingConfig& config) {
    if (!isInitialized_) {
        utils::Logger::error("StreamingAudioManager not initialized");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(streamingStateMutex_);
    
    // Check if utterance already exists
    if (streamingStates_.find(utteranceId) != streamingStates_.end()) {
        utils::Logger::warn("Streaming transcription already active for utterance: " + 
                           std::to_string(utteranceId));
        return false;
    }
    
    // Create utterance buffer
    size_t bufferSizeMB = config.maxBufferSizeMB > 0 ? config.maxBufferSizeMB : 8;
    if (!audioBufferManager_->createUtterance(utteranceId, bufferSizeMB)) {
        utils::Logger::error("Failed to create audio buffer for utterance: " + 
                            std::to_string(utteranceId));
        return false;
    }
    
    // Create streaming state
    StreamingState state;
    state.utteranceId = utteranceId;
    state.callback = callback;
    state.config = config;
    state.isActive = true;
    state.lastTranscriptionTime = std::chrono::steady_clock::now();
    state.totalAudioProcessed = 0;
    state.transcriptionCount = 0;
    
    streamingStates_[utteranceId] = state;
    
    utils::Logger::info("Started streaming transcription for utterance: " + 
                       std::to_string(utteranceId));
    return true;
}

bool StreamingAudioManager::addAudioChunk(uint32_t utteranceId, const std::vector<float>& audioData) {
    if (!isInitialized_ || audioData.empty()) {
        return false;
    }
    
    // Add audio to buffer
    if (!audioBufferManager_->addAudioData(utteranceId, audioData)) {
        utils::Logger::warn("Failed to add audio data to buffer for utterance: " + 
                           std::to_string(utteranceId));
        return false;
    }
    
    std::lock_guard<std::mutex> lock(streamingStateMutex_);
    
    auto it = streamingStates_.find(utteranceId);
    if (it == streamingStates_.end() || !it->second.isActive) {
        return false;
    }
    
    StreamingState& state = it->second;
    state.totalAudioProcessed += audioData.size();
    
    // Check if we should trigger a transcription
    if (shouldTriggerTranscription(state)) {
        triggerStreamingTranscription(utteranceId, state);
    }
    
    return true;
}

void StreamingAudioManager::finalizeStreamingTranscription(uint32_t utteranceId) {
    std::lock_guard<std::mutex> lock(streamingStateMutex_);
    
    auto it = streamingStates_.find(utteranceId);
    if (it == streamingStates_.end()) {
        return;
    }
    
    StreamingState& state = it->second;
    if (!state.isActive) {
        return;
    }
    
    // Perform final transcription with all accumulated audio
    triggerFinalTranscription(utteranceId, state);
    
    // Mark as inactive and finalize buffer
    state.isActive = false;
    audioBufferManager_->finalizeBuffer(utteranceId);
    
    utils::Logger::info("Finalized streaming transcription for utterance: " + 
                       std::to_string(utteranceId));
}

void StreamingAudioManager::stopStreamingTranscription(uint32_t utteranceId) {
    std::lock_guard<std::mutex> lock(streamingStateMutex_);
    
    auto it = streamingStates_.find(utteranceId);
    if (it != streamingStates_.end()) {
        it->second.isActive = false;
        audioBufferManager_->removeUtterance(utteranceId);
        streamingStates_.erase(it);
        
        utils::Logger::info("Stopped streaming transcription for utterance: " + 
                           std::to_string(utteranceId));
    }
}

void StreamingAudioManager::stopAllTranscriptions() {
    std::lock_guard<std::mutex> lock(streamingStateMutex_);
    
    for (auto& pair : streamingStates_) {
        pair.second.isActive = false;
        audioBufferManager_->removeUtterance(pair.first);
    }
    
    streamingStates_.clear();
    utils::Logger::info("Stopped all streaming transcriptions");
}

bool StreamingAudioManager::isTranscribing(uint32_t utteranceId) const {
    std::lock_guard<std::mutex> lock(streamingStateMutex_);
    
    auto it = streamingStates_.find(utteranceId);
    return it != streamingStates_.end() && it->second.isActive;
}

size_t StreamingAudioManager::getActiveTranscriptionCount() const {
    std::lock_guard<std::mutex> lock(streamingStateMutex_);
    
    size_t count = 0;
    for (const auto& pair : streamingStates_) {
        if (pair.second.isActive) {
            count++;
        }
    }
    return count;
}

StreamingAudioManager::StreamingStatistics StreamingAudioManager::getStatistics() const {
    std::lock_guard<std::mutex> lock(streamingStateMutex_);
    
    StreamingStatistics stats;
    stats.activeTranscriptions = 0;
    stats.totalTranscriptions = streamingStates_.size();
    stats.totalAudioProcessed = 0;
    stats.averageLatency = 0.0;
    
    double totalLatency = 0.0;
    size_t latencyCount = 0;
    
    for (const auto& pair : streamingStates_) {
        const StreamingState& state = pair.second;
        
        if (state.isActive) {
            stats.activeTranscriptions++;
        }
        
        stats.totalAudioProcessed += state.totalAudioProcessed;
        
        if (state.transcriptionCount > 0) {
            // Calculate average latency for this utterance
            auto now = std::chrono::steady_clock::now();
            auto timeSinceStart = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - state.startTime).count();
            
            if (timeSinceStart > 0) {
                double avgLatency = static_cast<double>(timeSinceStart) / state.transcriptionCount;
                totalLatency += avgLatency;
                latencyCount++;
            }
        }
    }
    
    if (latencyCount > 0) {
        stats.averageLatency = totalLatency / latencyCount;
    }
    
    // Get buffer manager statistics
    auto bufferStats = audioBufferManager_->getStatistics();
    stats.bufferMemoryUsageMB = bufferStats.totalMemoryUsageMB;
    stats.droppedAudioSamples = bufferStats.droppedSamples;
    
    return stats;
}

std::string StreamingAudioManager::getHealthStatus() const {
    auto stats = getStatistics();
    auto bufferHealth = audioBufferManager_->getHealthStatus();
    
    std::ostringstream oss;
    oss << "StreamingAudioManager Health Status:\n";
    oss << "  Active Transcriptions: " << stats.activeTranscriptions << "\n";
    oss << "  Total Audio Processed: " << (stats.totalAudioProcessed / 16000.0) << " seconds\n";
    oss << "  Average Latency: " << stats.averageLatency << " ms\n";
    oss << "  Buffer Memory Usage: " << stats.bufferMemoryUsageMB << " MB\n";
    oss << "  Dropped Audio Samples: " << stats.droppedAudioSamples << "\n";
    oss << "\nBuffer Manager Status:\n" << bufferHealth;
    
    return oss.str();
}

// Private helper methods
bool StreamingAudioManager::shouldTriggerTranscription(const StreamingState& state) {
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastTranscription = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - state.lastTranscriptionTime).count();
    
    // Trigger transcription based on time interval or audio accumulation
    bool timeThresholdMet = timeSinceLastTranscription >= state.config.transcriptionIntervalMs;
    
    // Get current audio buffer size
    auto recentAudio = audioBufferManager_->getRecentAudio(state.utteranceId, 
                                                          state.config.minAudioSamples);
    bool audioThresholdMet = recentAudio.size() >= state.config.minAudioSamples;
    
    return timeThresholdMet && audioThresholdMet;
}

void StreamingAudioManager::triggerStreamingTranscription(uint32_t utteranceId, StreamingState& state) {
    // Get recent audio for transcription
    size_t audioSamples = std::max(state.config.minAudioSamples, 
                                  static_cast<size_t>(16000 * 2)); // At least 2 seconds
    
    auto audioData = audioBufferManager_->getRecentAudio(utteranceId, audioSamples);
    if (audioData.empty()) {
        return;
    }
    
    // Create callback wrapper that handles partial results
    auto wrappedCallback = [this, utteranceId, originalCallback = state.callback]
                          (const TranscriptionResult& result) {
        handleTranscriptionResult(utteranceId, result, originalCallback, true);
    };
    
    // Perform live transcription (partial result)
    whisperSTT_->transcribeLive(audioData, wrappedCallback);
    
    // Update state
    state.lastTranscriptionTime = std::chrono::steady_clock::now();
    state.transcriptionCount++;
    
    utils::Logger::debug("Triggered streaming transcription for utterance: " + 
                        std::to_string(utteranceId) + 
                        " with " + std::to_string(audioData.size()) + " samples");
}

void StreamingAudioManager::triggerFinalTranscription(uint32_t utteranceId, StreamingState& state) {
    // Get all accumulated audio for final transcription
    auto audioData = audioBufferManager_->getBufferedAudio(utteranceId);
    if (audioData.empty()) {
        return;
    }
    
    // Create callback wrapper that handles final results
    auto wrappedCallback = [this, utteranceId, originalCallback = state.callback]
                          (const TranscriptionResult& result) {
        handleTranscriptionResult(utteranceId, result, originalCallback, false);
    };
    
    // Perform full transcription (final result)
    whisperSTT_->transcribe(audioData, wrappedCallback);
    
    utils::Logger::info("Triggered final transcription for utterance: " + 
                       std::to_string(utteranceId) + 
                       " with " + std::to_string(audioData.size()) + " samples");
}

void StreamingAudioManager::handleTranscriptionResult(uint32_t utteranceId,
                                                     const TranscriptionResult& result,
                                                     TranscriptionCallback originalCallback,
                                                     bool isPartial) {
    // Create enhanced result with utterance ID
    TranscriptionResult enhancedResult = result;
    enhancedResult.is_partial = isPartial;
    
    // Add utterance-specific metadata if needed
    // This could include buffer statistics, timing info, etc.
    
    // Call the original callback
    originalCallback(enhancedResult);
    
    utils::Logger::debug("Processed transcription result for utterance: " + 
                        std::to_string(utteranceId) + 
                        " (partial: " + (isPartial ? "true" : "false") + 
                        ", text: \"" + result.text + "\")");
}

} // namespace stt