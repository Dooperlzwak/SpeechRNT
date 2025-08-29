#include "audio/audio_processor.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace audio {

// AudioBuffer implementation
AudioBuffer::AudioBuffer(size_t maxSizeBytes) 
    : maxSizeBytes_(maxSizeBytes), currentSizeBytes_(0), nextSequenceNumber_(0) {
}

bool AudioBuffer::addChunk(const AudioChunk& chunk) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t chunkSize = chunk.samples.size() * sizeof(float);
    
    // Check if adding this chunk would exceed the buffer limit
    if (currentSizeBytes_ + chunkSize > maxSizeBytes_) {
        removeOldChunks();
        
        // If still too large after cleanup, reject the chunk
        if (currentSizeBytes_ + chunkSize > maxSizeBytes_) {
            utils::Logger::warn("AudioBuffer: Chunk too large, dropping");
            return false;
        }
    }
    
    chunks_.push(chunk);
    currentSizeBytes_ += chunkSize;
    
    return true;
}

bool AudioBuffer::addRawData(const std::vector<float>& samples) {
    AudioChunk chunk(samples, nextSequenceNumber_++);
    return addChunk(chunk);
}

std::vector<AudioChunk> AudioBuffer::getChunks(size_t maxChunks) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<AudioChunk> result;
    size_t count = (maxChunks == 0) ? chunks_.size() : std::min(maxChunks, chunks_.size());
    
    result.reserve(count);
    
    // Copy chunks without removing them from the buffer
    std::queue<AudioChunk> tempQueue = chunks_;
    for (size_t i = 0; i < count && !tempQueue.empty(); ++i) {
        result.push_back(tempQueue.front());
        tempQueue.pop();
    }
    
    return result;
}

std::vector<float> AudioBuffer::getAllSamples() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<float> result;
    std::queue<AudioChunk> tempQueue = chunks_;
    
    // Calculate total size
    size_t totalSamples = 0;
    while (!tempQueue.empty()) {
        totalSamples += tempQueue.front().samples.size();
        tempQueue.pop();
    }
    
    result.reserve(totalSamples);
    
    // Copy all samples
    tempQueue = chunks_;
    while (!tempQueue.empty()) {
        const auto& samples = tempQueue.front().samples;
        result.insert(result.end(), samples.begin(), samples.end());
        tempQueue.pop();
    }
    
    return result;
}

std::vector<float> AudioBuffer::getRecentSamples(size_t sampleCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Get all samples without acquiring mutex again (private helper)
    std::vector<float> result;
    std::queue<AudioChunk> tempQueue = chunks_;
    
    // Calculate total size
    size_t totalSamples = 0;
    while (!tempQueue.empty()) {
        totalSamples += tempQueue.front().samples.size();
        tempQueue.pop();
    }
    
    result.reserve(totalSamples);
    
    // Copy all samples
    tempQueue = chunks_;
    while (!tempQueue.empty()) {
        const auto& samples = tempQueue.front().samples;
        result.insert(result.end(), samples.begin(), samples.end());
        tempQueue.pop();
    }
    
    if (result.size() <= sampleCount) {
        return result;
    }
    
    // Return the most recent samples
    return std::vector<float>(result.end() - sampleCount, result.end());
}

void AudioBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    while (!chunks_.empty()) {
        chunks_.pop();
    }
    currentSizeBytes_ = 0;
}

size_t AudioBuffer::getChunkCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return chunks_.size();
}

size_t AudioBuffer::getTotalSamples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t total = 0;
    std::queue<AudioChunk> tempQueue = chunks_;
    while (!tempQueue.empty()) {
        total += tempQueue.front().samples.size();
        tempQueue.pop();
    }
    return total;
}

size_t AudioBuffer::getBufferSizeBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentSizeBytes_;
}

bool AudioBuffer::isFull() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentSizeBytes_ >= maxSizeBytes_;
}

std::chrono::steady_clock::time_point AudioBuffer::getOldestTimestamp() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (chunks_.empty()) {
        return std::chrono::steady_clock::now();
    }
    
    return chunks_.front().timestamp;
}

std::chrono::steady_clock::time_point AudioBuffer::getNewestTimestamp() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (chunks_.empty()) {
        return std::chrono::steady_clock::now();
    }
    
    // Get the last chunk's timestamp
    std::queue<AudioChunk> tempQueue = chunks_;
    std::chrono::steady_clock::time_point newest;
    while (!tempQueue.empty()) {
        newest = tempQueue.front().timestamp;
        tempQueue.pop();
    }
    
    return newest;
}

double AudioBuffer::getDurationSeconds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (chunks_.empty()) {
        return 0.0;
    }
    
    auto oldest = getOldestTimestamp();
    auto newest = getNewestTimestamp();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(newest - oldest);
    return duration.count() / 1000000.0;
}

void AudioBuffer::removeOldChunks() {
    // Remove chunks until we're under 75% of max capacity
    size_t targetSize = maxSizeBytes_ * 3 / 4;
    
    while (!chunks_.empty() && currentSizeBytes_ > targetSize) {
        const auto& chunk = chunks_.front();
        currentSizeBytes_ -= chunk.samples.size() * sizeof(float);
        chunks_.pop();
    }
}

// AudioProcessor implementation
AudioProcessor::AudioProcessor(const AudioFormat& format) 
    : format_(format), totalBytesProcessed_(0), totalChunksProcessed_(0), nextSequenceNumber_(0) {
    
    if (!validateFormat(format)) {
        throw std::invalid_argument("Invalid audio format");
    }
}

bool AudioProcessor::validateFormat(const AudioFormat& format) const {
    // Validate required format for SpeechRNT
    if (format.sampleRate != 16000) {
        utils::Logger::error("Invalid sample rate: " + std::to_string(format.sampleRate) + " (expected 16000)");
        return false;
    }
    
    if (format.channels != 1) {
        utils::Logger::error("Invalid channel count: " + std::to_string(format.channels) + " (expected 1)");
        return false;
    }
    
    if (format.bitsPerSample != 16) {
        utils::Logger::error("Invalid bits per sample: " + std::to_string(format.bitsPerSample) + " (expected 16)");
        return false;
    }
    
    if (format.chunkSize == 0 || format.chunkSize > 8192) {
        utils::Logger::error("Invalid chunk size: " + std::to_string(format.chunkSize) + " (expected 1-8192)");
        return false;
    }
    
    return true;
}

bool AudioProcessor::validatePCMData(std::string_view data) const {
    // Check if data size is valid for 16-bit PCM
    if (data.size() % 2 != 0) {
        utils::Logger::warn("PCM data size is not even (16-bit samples expected)");
        return false;
    }
    
    // Check if data size matches expected chunk size
    size_t expectedBytes = format_.getChunkSizeBytes();
    if (data.size() != expectedBytes && data.size() > 0) {
        utils::Logger::debug("PCM data size (" + std::to_string(data.size()) + 
                            ") differs from expected (" + std::to_string(expectedBytes) + ")");
        // This is not necessarily an error for streaming data
    }
    
    return true;
}

std::vector<float> AudioProcessor::convertPCMToFloat(std::string_view pcmData) const {
    if (!validatePCMData(pcmData)) {
        return {};
    }
    
    const size_t sampleCount = pcmData.size() / 2; // 2 bytes per 16-bit sample
    std::vector<float> samples;
    samples.reserve(sampleCount);
    
    const int16_t* pcmSamples = reinterpret_cast<const int16_t*>(pcmData.data());
    
    for (size_t i = 0; i < sampleCount; ++i) {
        samples.push_back(convertSampleToFloat(pcmSamples[i]));
    }
    
    return samples;
}

std::vector<int16_t> AudioProcessor::convertFloatToPCM(const std::vector<float>& samples) const {
    std::vector<int16_t> pcmSamples;
    pcmSamples.reserve(samples.size());
    
    for (float sample : samples) {
        pcmSamples.push_back(convertSampleToPCM(sample));
    }
    
    return pcmSamples;
}

AudioChunk AudioProcessor::processRawData(std::string_view data) {
    totalBytesProcessed_ += data.size();
    totalChunksProcessed_++;
    
    std::vector<float> samples = convertPCMToFloat(data);
    return AudioChunk(samples, nextSequenceNumber_++);
}

std::vector<AudioChunk> AudioProcessor::processStreamingData(std::string_view data) {
    std::vector<AudioChunk> chunks;
    
    // For streaming, we might receive partial chunks or multiple chunks
    size_t expectedChunkBytes = format_.getChunkSizeBytes();
    size_t offset = 0;
    
    while (offset + expectedChunkBytes <= data.size()) {
        std::string_view chunkData = data.substr(offset, expectedChunkBytes);
        chunks.push_back(processRawData(chunkData));
        offset += expectedChunkBytes;
    }
    
    // Handle remaining partial chunk if any
    if (offset < data.size()) {
        std::string_view remainingData = data.substr(offset);
        chunks.push_back(processRawData(remainingData));
    }
    
    return chunks;
}

void AudioProcessor::setFormat(const AudioFormat& format) {
    if (!validateFormat(format)) {
        throw std::invalid_argument("Invalid audio format");
    }
    format_ = format;
}

void AudioProcessor::resetStatistics() {
    totalBytesProcessed_ = 0;
    totalChunksProcessed_ = 0;
    nextSequenceNumber_ = 0;
}

bool AudioProcessor::validatePCMChunk(std::string_view data) const {
    return validatePCMData(data);
}

float AudioProcessor::convertSampleToFloat(int16_t sample) const {
    // Convert 16-bit PCM to float [-1.0, 1.0]
    return static_cast<float>(sample) / 32768.0f;
}

int16_t AudioProcessor::convertSampleToPCM(float sample) const {
    // Clamp to [-1.0, 1.0] and convert to 16-bit PCM
    float clamped = std::max(-1.0f, std::min(1.0f, sample));
    return static_cast<int16_t>(clamped * 32767.0f);
}

// AudioIngestionManager implementation
AudioIngestionManager::AudioIngestionManager(const std::string& sessionId) 
    : sessionId_(sessionId), active_(false), lastError_(ErrorCode::NONE) {
    
    AudioFormat defaultFormat;
    processor_ = std::make_unique<AudioProcessor>(defaultFormat);
    audioBuffer_ = std::make_shared<AudioBuffer>();
    
    // Initialize statistics
    stats_ = {};
    stats_.lastActivity = std::chrono::steady_clock::now();
    
    utils::Logger::info("AudioIngestionManager created for session: " + sessionId);
}

bool AudioIngestionManager::ingestAudioData(std::string_view data) {
    if (!active_) {
        setError(ErrorCode::INACTIVE_SESSION);
        return false;
    }
    
    try {
        // Process the raw audio data
        auto chunks = processor_->processStreamingData(data);
        
        bool allSuccessful = true;
        for (const auto& chunk : chunks) {
            if (!audioBuffer_->addChunk(chunk)) {
                setError(ErrorCode::BUFFER_FULL);
                allSuccessful = false;
                updateStatistics(0, 0); // Count as dropped
            }
        }
        
        if (allSuccessful) {
            updateStatistics(data.size(), chunks.size());
            setError(ErrorCode::NONE);
        }
        
        return allSuccessful;
        
    } catch (const std::exception& e) {
        utils::Logger::error("AudioIngestionManager: Processing error for session " + 
                           sessionId_ + ": " + e.what());
        setError(ErrorCode::PROCESSING_ERROR);
        return false;
    }
}

bool AudioIngestionManager::ingestAudioChunk(const AudioChunk& chunk) {
    if (!active_) {
        setError(ErrorCode::INACTIVE_SESSION);
        return false;
    }
    
    if (audioBuffer_->addChunk(chunk)) {
        updateStatistics(chunk.samples.size() * sizeof(float), 1);
        setError(ErrorCode::NONE);
        return true;
    } else {
        setError(ErrorCode::BUFFER_FULL);
        updateStatistics(0, 0); // Count as dropped
        return false;
    }
}

std::vector<float> AudioIngestionManager::getLatestAudio(size_t sampleCount) {
    return audioBuffer_->getRecentSamples(sampleCount);
}

void AudioIngestionManager::setAudioFormat(const AudioFormat& format) {
    processor_->setFormat(format);
}

const AudioFormat& AudioIngestionManager::getAudioFormat() const {
    return processor_->getFormat();
}

AudioIngestionManager::Statistics AudioIngestionManager::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    Statistics result = stats_;
    result.bufferUtilization = static_cast<double>(audioBuffer_->getBufferSizeBytes()) / 
                              (1024 * 1024); // Assuming 1MB buffer
    
    return result;
}

void AudioIngestionManager::resetStatistics() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    stats_ = {};
    stats_.lastActivity = std::chrono::steady_clock::now();
    processor_->resetStatistics();
}

std::string AudioIngestionManager::getErrorMessage() const {
    switch (lastError_) {
        case ErrorCode::NONE:
            return "No error";
        case ErrorCode::INVALID_FORMAT:
            return "Invalid audio format";
        case ErrorCode::BUFFER_FULL:
            return "Audio buffer is full";
        case ErrorCode::PROCESSING_ERROR:
            return "Audio processing error";
        case ErrorCode::INACTIVE_SESSION:
            return "Session is not active";
        default:
            return "Unknown error";
    }
}

void AudioIngestionManager::updateStatistics(size_t bytesProcessed, size_t chunksProcessed) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    stats_.totalBytesIngested += bytesProcessed;
    stats_.totalChunksIngested += chunksProcessed;
    
    if (chunksProcessed == 0) {
        stats_.droppedChunks++;
    }
    
    if (stats_.totalChunksIngested > 0) {
        stats_.averageChunkSize = static_cast<double>(stats_.totalBytesIngested) / 
                                 stats_.totalChunksIngested;
    }
    
    stats_.lastActivity = std::chrono::steady_clock::now();
}

void AudioIngestionManager::setError(ErrorCode error) {
    lastError_ = error;
    
    if (error != ErrorCode::NONE) {
        utils::Logger::warn("AudioIngestionManager error for session " + sessionId_ + 
                           ": " + getErrorMessage());
    }
}

} // namespace audio