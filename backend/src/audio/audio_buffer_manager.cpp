#include "audio/audio_buffer_manager.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <sstream>
#include <cmath>

namespace audio {

// UtteranceBuffer implementation
AudioBufferManager::UtteranceBuffer::UtteranceBuffer() 
    : startTime(std::chrono::steady_clock::now())
    , lastAccessTime(std::chrono::steady_clock::now())
    , maxSizeSamples(0)
    , writePosition(0)
    , isActive(true)
    , isCircular(true) {
}

AudioBufferManager::UtteranceBuffer::UtteranceBuffer(size_t maxSamples, bool circular)
    : startTime(std::chrono::steady_clock::now())
    , lastAccessTime(std::chrono::steady_clock::now())
    , maxSizeSamples(maxSamples)
    , writePosition(0)
    , isActive(true)
    , isCircular(circular) {
    
    if (maxSamples > 0) {
        audioData.reserve(maxSamples);
    }
}

bool AudioBufferManager::UtteranceBuffer::addAudioData(const std::vector<float>& audio) {
    if (audio.empty()) {
        return true;
    }
    
    lastAccessTime = std::chrono::steady_clock::now();
    
    if (!isCircular) {
        // Linear buffer - append until max size
        size_t availableSpace = maxSizeSamples - audioData.size();
        size_t samplesToAdd = std::min(audio.size(), availableSpace);
        
        audioData.insert(audioData.end(), audio.begin(), audio.begin() + samplesToAdd);
        return samplesToAdd == audio.size();
    }
    
    // Circular buffer implementation
    if (maxSizeSamples == 0) {
        // No size limit - just append
        audioData.insert(audioData.end(), audio.begin(), audio.end());
        return true;
    }
    
    // Ensure buffer is sized to max capacity
    if (audioData.size() < maxSizeSamples) {
        audioData.resize(maxSizeSamples, 0.0f);
    }
    
    // Write audio data in circular fashion
    for (float sample : audio) {
        audioData[writePosition] = sample;
        writePosition = (writePosition + 1) % maxSizeSamples;
    }
    
    return true;
}

std::vector<float> AudioBufferManager::UtteranceBuffer::getAudioData() const {
    lastAccessTime = std::chrono::steady_clock::now();
    
    if (!isCircular || audioData.size() < maxSizeSamples) {
        return audioData;
    }
    
    // For circular buffer, return data in correct order
    std::vector<float> result;
    result.reserve(audioData.size());
    
    // Start from write position (oldest data) and wrap around
    for (size_t i = 0; i < audioData.size(); ++i) {
        size_t index = (writePosition + i) % audioData.size();
        result.push_back(audioData[index]);
    }
    
    return result;
}

std::vector<float> AudioBufferManager::UtteranceBuffer::getRecentAudioData(size_t sampleCount) const {
    lastAccessTime = std::chrono::steady_clock::now();
    
    if (sampleCount == 0) {
        return {};
    }
    
    if (!isCircular || audioData.size() < maxSizeSamples) {
        // Linear buffer - return last N samples
        if (sampleCount >= audioData.size()) {
            return audioData;
        }
        return std::vector<float>(audioData.end() - sampleCount, audioData.end());
    }
    
    // Circular buffer - return most recent samples
    std::vector<float> result;
    size_t samplesToGet = std::min(sampleCount, audioData.size());
    result.reserve(samplesToGet);
    
    // Start from the most recent position and go backwards
    for (size_t i = 0; i < samplesToGet; ++i) {
        size_t index = (writePosition - 1 - i + audioData.size()) % audioData.size();
        result.push_back(audioData[index]);
    }
    
    // Reverse to get chronological order
    std::reverse(result.begin(), result.end());
    return result;
}

void AudioBufferManager::UtteranceBuffer::clear() {
    audioData.clear();
    writePosition = 0;
    lastAccessTime = std::chrono::steady_clock::now();
}

bool AudioBufferManager::UtteranceBuffer::isFull() const {
    return maxSizeSamples > 0 && audioData.size() >= maxSizeSamples;
}

double AudioBufferManager::UtteranceBuffer::getDurationSeconds(uint32_t sampleRate) const {
    if (sampleRate == 0) {
        return 0.0;
    }
    return static_cast<double>(audioData.size()) / sampleRate;
}

size_t AudioBufferManager::UtteranceBuffer::getMemoryUsageBytes() const {
    return audioData.size() * sizeof(float) + sizeof(UtteranceBuffer);
}

// BufferStatistics implementation
AudioBufferManager::BufferStatistics::BufferStatistics()
    : totalUtterances(0)
    , activeUtterances(0)
    , totalMemoryUsageMB(0)
    , peakMemoryUsageMB(0)
    , totalAudioSamples(0)
    , droppedSamples(0)
    , averageBufferUtilization(0.0)
    , lastCleanupTime(std::chrono::steady_clock::now()) {
}

// AudioBufferManager implementation
AudioBufferManager::AudioBufferManager(const BufferConfig& config)
    : config_(config)
    , peakMemoryUsage_(0)
    , totalDroppedSamples_(0)
    , lastCleanupTime_(std::chrono::steady_clock::now()) {
    
    speechrnt::utils::Logger::info("AudioBufferManager initialized with max " + 
                       std::to_string(config_.maxBufferSizeMB) + "MB per utterance, " +
                       std::to_string(config_.maxUtterances) + " max utterances");
}

AudioBufferManager::~AudioBufferManager() {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    utteranceBuffers_.clear();
    speechrnt::utils::Logger::info("AudioBufferManager destroyed");
}

bool AudioBufferManager::addAudioData(uint32_t utteranceId, const std::vector<float>& audio) {
    if (audio.empty()) {
        return true;
    }
    
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    auto it = utteranceBuffers_.find(utteranceId);
    if (it == utteranceBuffers_.end()) {
        // Auto-create utterance buffer if it doesn't exist
        if (!createUtterance(utteranceId)) {
            speechrnt::utils::Logger::warn("Failed to create utterance buffer for ID: " + std::to_string(utteranceId));
            totalDroppedSamples_ += audio.size();
            return false;
        }
        it = utteranceBuffers_.find(utteranceId);
    }
    
    if (!it->second->isActive) {
        speechrnt::utils::Logger::debug("Attempted to add audio to inactive utterance: " + std::to_string(utteranceId));
        totalDroppedSamples_ += audio.size();
        return false;
    }
    
    bool success = it->second->addAudioData(audio);
    if (!success) {
        totalDroppedSamples_ += audio.size();
        speechrnt::utils::Logger::warn("Audio buffer full for utterance: " + std::to_string(utteranceId));
    }
    
    // Update statistics
    updateStatistics();
    
    // Perform cleanup if needed
    if (shouldCleanup()) {
        performCleanup();
    }
    
    return success;
}

std::vector<float> AudioBufferManager::getBufferedAudio(uint32_t utteranceId) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    auto it = utteranceBuffers_.find(utteranceId);
    if (it == utteranceBuffers_.end()) {
        speechrnt::utils::Logger::debug("No buffer found for utterance: " + std::to_string(utteranceId));
        return {};
    }
    
    return it->second->getAudioData();
}

std::vector<float> AudioBufferManager::getRecentAudio(uint32_t utteranceId, size_t sampleCount) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    auto it = utteranceBuffers_.find(utteranceId);
    if (it == utteranceBuffers_.end()) {
        speechrnt::utils::Logger::debug("No buffer found for utterance: " + std::to_string(utteranceId));
        return {};
    }
    
    return it->second->getRecentAudioData(sampleCount);
}

bool AudioBufferManager::hasUtterance(uint32_t utteranceId) const {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    return utteranceBuffers_.find(utteranceId) != utteranceBuffers_.end();
}

bool AudioBufferManager::createUtterance(uint32_t utteranceId, size_t maxSizeMB) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    // Check if utterance already exists
    if (utteranceBuffers_.find(utteranceId) != utteranceBuffers_.end()) {
        speechrnt::utils::Logger::debug("Utterance already exists: " + std::to_string(utteranceId));
        return true;
    }
    
    // Check utterance limit
    if (utteranceBuffers_.size() >= config_.maxUtterances) {
        speechrnt::utils::Logger::warn("Maximum utterance limit reached (" + 
                           std::to_string(config_.maxUtterances) + "), cleaning up old buffers");
        
        // Force cleanup of oldest utterances
        auto oldestUtterances = findOldestUtterances(utteranceBuffers_.size() - config_.maxUtterances + 1);
        for (uint32_t oldId : oldestUtterances) {
            removeUtteranceInternal(oldId);
        }
    }
    
    // Use provided size or default from config
    size_t bufferSizeMB = (maxSizeMB > 0) ? maxSizeMB : config_.maxBufferSizeMB;
    size_t maxSamples = calculateMaxSamples(bufferSizeMB);
    
    auto buffer = std::make_unique<UtteranceBuffer>(maxSamples, config_.enableCircularBuffer);
    utteranceBuffers_[utteranceId] = std::move(buffer);
    
    speechrnt::utils::Logger::debug("Created utterance buffer for ID: " + std::to_string(utteranceId) + 
                        " with max " + std::to_string(bufferSizeMB) + "MB (" + 
                        std::to_string(maxSamples) + " samples)");
    
    updateStatistics();
    return true;
}

void AudioBufferManager::finalizeBuffer(uint32_t utteranceId) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    auto it = utteranceBuffers_.find(utteranceId);
    if (it != utteranceBuffers_.end()) {
        it->second->isActive = false;
        speechrnt::utils::Logger::debug("Finalized utterance buffer: " + std::to_string(utteranceId));
        updateStatistics();
    }
}

void AudioBufferManager::removeUtterance(uint32_t utteranceId) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    removeUtteranceInternal(utteranceId);
}

void AudioBufferManager::setUtteranceActive(uint32_t utteranceId, bool active) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    auto it = utteranceBuffers_.find(utteranceId);
    if (it != utteranceBuffers_.end()) {
        it->second->isActive = active;
        speechrnt::utils::Logger::debug("Set utterance " + std::to_string(utteranceId) + 
                           " active: " + (active ? "true" : "false"));
        updateStatistics();
    }
}

bool AudioBufferManager::isUtteranceActive(uint32_t utteranceId) const {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    auto it = utteranceBuffers_.find(utteranceId);
    return it != utteranceBuffers_.end() && it->second->isActive;
}

void AudioBufferManager::cleanupOldBuffers() {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    auto now = std::chrono::steady_clock::now();
    std::vector<uint32_t> toRemove;
    
    for (const auto& pair : utteranceBuffers_) {
        auto timeSinceAccess = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - pair.second->lastAccessTime).count();
        
        if (timeSinceAccess > static_cast<long>(config_.maxIdleTimeMs)) {
            toRemove.push_back(pair.first);
        }
    }
    
    for (uint32_t utteranceId : toRemove) {
        removeUtteranceInternal(utteranceId);
        speechrnt::utils::Logger::debug("Cleaned up old utterance buffer: " + std::to_string(utteranceId));
    }
    
    if (!toRemove.empty()) {
        updateStatistics();
        speechrnt::utils::Logger::info("Cleaned up " + std::to_string(toRemove.size()) + " old utterance buffers");
    }
}

void AudioBufferManager::cleanupInactiveBuffers() {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    std::vector<uint32_t> toRemove;
    
    for (const auto& pair : utteranceBuffers_) {
        if (!pair.second->isActive) {
            toRemove.push_back(pair.first);
        }
    }
    
    for (uint32_t utteranceId : toRemove) {
        removeUtteranceInternal(utteranceId);
    }
    
    if (!toRemove.empty()) {
        updateStatistics();
        speechrnt::utils::Logger::info("Cleaned up " + std::to_string(toRemove.size()) + " inactive utterance buffers");
    }
}

void AudioBufferManager::forceCleanup() {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    size_t initialCount = utteranceBuffers_.size();
    utteranceBuffers_.clear();
    
    updateStatistics();
    speechrnt::utils::Logger::info("Force cleanup removed " + std::to_string(initialCount) + " utterance buffers");
}

size_t AudioBufferManager::getCurrentMemoryUsage() const {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    size_t totalBytes = 0;
    for (const auto& pair : utteranceBuffers_) {
        totalBytes += pair.second->getMemoryUsageBytes();
    }
    
    return totalBytes;
}

size_t AudioBufferManager::getCurrentMemoryUsageMB() const {
    return getCurrentMemoryUsage() / (1024 * 1024);
}

void AudioBufferManager::updateConfig(const BufferConfig& config) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    config_ = config;
    speechrnt::utils::Logger::info("AudioBufferManager configuration updated");
}

AudioBufferManager::BufferStatistics AudioBufferManager::getStatistics() const {
    std::lock_guard<std::mutex> lock1(bufferMutex_);
    std::lock_guard<std::mutex> lock2(statsMutex_);
    
    BufferStatistics stats = stats_;
    
    // Update current values
    stats.totalUtterances = utteranceBuffers_.size();
    stats.activeUtterances = 0;
    stats.totalAudioSamples = 0;
    
    for (const auto& pair : utteranceBuffers_) {
        if (pair.second->isActive) {
            stats.activeUtterances++;
        }
        stats.totalAudioSamples += pair.second->getCurrentSamples();
    }
    
    stats.totalMemoryUsageMB = getCurrentMemoryUsageMB();
    stats.peakMemoryUsageMB = peakMemoryUsage_.load() / (1024 * 1024);
    stats.droppedSamples = totalDroppedSamples_.load();
    
    // Calculate average buffer utilization
    if (stats.totalUtterances > 0) {
        size_t totalCapacity = stats.totalUtterances * calculateMaxSamples(config_.maxBufferSizeMB);
        stats.averageBufferUtilization = static_cast<double>(stats.totalAudioSamples) / totalCapacity;
    }
    
    return stats;
}

void AudioBufferManager::resetStatistics() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    stats_ = BufferStatistics();
    peakMemoryUsage_ = 0;
    totalDroppedSamples_ = 0;
    
    speechrnt::utils::Logger::info("AudioBufferManager statistics reset");
}

std::vector<uint32_t> AudioBufferManager::getActiveUtterances() const {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    std::vector<uint32_t> activeIds;
    for (const auto& pair : utteranceBuffers_) {
        if (pair.second->isActive) {
            activeIds.push_back(pair.first);
        }
    }
    
    return activeIds;
}

size_t AudioBufferManager::getUtteranceCount() const {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    return utteranceBuffers_.size();
}

bool AudioBufferManager::isHealthy() const {
    size_t currentMemoryMB = getCurrentMemoryUsageMB();
    size_t maxTotalMemoryMB = config_.maxUtterances * config_.maxBufferSizeMB;
    
    // Consider healthy if memory usage is under 90% of maximum
    return currentMemoryMB < (maxTotalMemoryMB * 9 / 10);
}

std::string AudioBufferManager::getHealthStatus() const {
    std::ostringstream oss;
    auto stats = getStatistics();
    
    oss << "AudioBufferManager Health Status:\n";
    oss << "  Active Utterances: " << stats.activeUtterances << "/" << config_.maxUtterances << "\n";
    oss << "  Memory Usage: " << stats.totalMemoryUsageMB << "MB (Peak: " << stats.peakMemoryUsageMB << "MB)\n";
    oss << "  Buffer Utilization: " << (stats.averageBufferUtilization * 100.0) << "%\n";
    oss << "  Dropped Samples: " << stats.droppedSamples << "\n";
    oss << "  Status: " << (isHealthy() ? "HEALTHY" : "UNHEALTHY");
    
    return oss.str();
}

// Private helper methods
bool AudioBufferManager::shouldCleanup() const {
    auto now = std::chrono::steady_clock::now();
    auto timeSinceCleanup = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastCleanupTime_).count();
    
    return timeSinceCleanup > static_cast<long>(config_.cleanupIntervalMs) || 
           isMemoryLimitExceeded();
}

void AudioBufferManager::performCleanup() {
    lastCleanupTime_ = std::chrono::steady_clock::now();
    
    // First cleanup inactive buffers
    cleanupInactiveBuffers();
    
    // Then cleanup old buffers if still over limit
    if (isMemoryLimitExceeded()) {
        cleanupOldBuffers();
    }
    
    // If still over limit, force cleanup of oldest buffers
    if (isMemoryLimitExceeded() && utteranceBuffers_.size() > config_.maxUtterances / 2) {
        auto oldestUtterances = findOldestUtterances(utteranceBuffers_.size() / 4);
        for (uint32_t utteranceId : oldestUtterances) {
            removeUtteranceInternal(utteranceId);
        }
        speechrnt::utils::Logger::warn("Performed aggressive cleanup due to memory pressure");
    }
}

size_t AudioBufferManager::calculateMaxSamples(size_t maxSizeMB) const {
    // Convert MB to samples (assuming 32-bit float samples)
    return (maxSizeMB * 1024 * 1024) / sizeof(float);
}

void AudioBufferManager::updateStatistics() {
    size_t currentMemory = getCurrentMemoryUsage();
    size_t currentPeak = peakMemoryUsage_.load();
    
    if (currentMemory > currentPeak) {
        peakMemoryUsage_ = currentMemory;
    }
    
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.lastCleanupTime = lastCleanupTime_;
}

bool AudioBufferManager::isMemoryLimitExceeded() const {
    size_t currentMemoryMB = getCurrentMemoryUsageMB();
    size_t maxTotalMemoryMB = config_.maxUtterances * config_.maxBufferSizeMB;
    
    return currentMemoryMB > maxTotalMemoryMB;
}

std::vector<uint32_t> AudioBufferManager::findOldestUtterances(size_t count) const {
    std::vector<std::pair<uint32_t, std::chrono::steady_clock::time_point>> utteranceAges;
    
    for (const auto& pair : utteranceBuffers_) {
        utteranceAges.emplace_back(pair.first, pair.second->lastAccessTime);
    }
    
    // Sort by access time (oldest first)
    std::sort(utteranceAges.begin(), utteranceAges.end(),
              [](const auto& a, const auto& b) {
                  return a.second < b.second;
              });
    
    std::vector<uint32_t> result;
    size_t numToReturn = std::min(count, utteranceAges.size());
    
    for (size_t i = 0; i < numToReturn; ++i) {
        result.push_back(utteranceAges[i].first);
    }
    
    return result;
}

void AudioBufferManager::removeUtteranceInternal(uint32_t utteranceId) {
    auto it = utteranceBuffers_.find(utteranceId);
    if (it != utteranceBuffers_.end()) {
        utteranceBuffers_.erase(it);
        speechrnt::utils::Logger::debug("Removed utterance buffer: " + std::to_string(utteranceId));
    }
}

} // namespace audio