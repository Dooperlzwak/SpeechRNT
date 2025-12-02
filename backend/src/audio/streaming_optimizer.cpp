#include "audio/streaming_optimizer.hpp"
#include "utils/logging.hpp"
#include "utils/performance_monitor.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace speechrnt {
namespace audio {

StreamingAudioBuffer::StreamingAudioBuffer(size_t maxChunks, size_t chunkSizeHint)
    : maxChunks_(maxChunks)
    , chunkSizeHint_(chunkSizeHint)
    , nextSequenceNumber_(0) {
}

StreamingAudioBuffer::~StreamingAudioBuffer() {
    clear();
}

bool StreamingAudioBuffer::addChunk(const AudioChunk& chunk) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (buffer_.size() >= maxChunks_) {
        // Buffer full, remove oldest chunk
        buffer_.pop();
        speechrnt::utils::Logger::warn("Audio buffer overflow, dropping oldest chunk");
    }
    
    AudioChunk newChunk = chunk;
    if (newChunk.sequenceNumber == 0) {
        newChunk.sequenceNumber = nextSequenceNumber_++;
    }
    
    buffer_.push(newChunk);
    return true;
}

bool StreamingAudioBuffer::getNextChunk(AudioChunk& chunk) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (buffer_.empty()) {
        return false;
    }
    
    chunk = buffer_.front();
    buffer_.pop();
    return true;
}

size_t StreamingAudioBuffer::getChunks(std::vector<AudioChunk>& chunks, size_t maxChunks) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t numChunks = std::min(maxChunks, buffer_.size());
    chunks.clear();
    chunks.reserve(numChunks);
    
    for (size_t i = 0; i < numChunks; ++i) {
        chunks.push_back(buffer_.front());
        buffer_.pop();
    }
    
    return numChunks;
}

bool StreamingAudioBuffer::peekNextChunk(AudioChunk& chunk) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (buffer_.empty()) {
        return false;
    }
    
    chunk = buffer_.front();
    return true;
}

size_t StreamingAudioBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
}

bool StreamingAudioBuffer::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.empty();
}

void StreamingAudioBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!buffer_.empty()) {
        buffer_.pop();
    }
}

void StreamingAudioBuffer::setMaxSize(size_t maxChunks) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxChunks_ = maxChunks;
    
    // Remove excess chunks if necessary
    while (buffer_.size() > maxChunks_) {
        buffer_.pop();
    }
}

float StreamingAudioBuffer::getUtilization() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<float>(buffer_.size()) / static_cast<float>(maxChunks_) * 100.0f;
}

StreamingOptimizer::StreamingOptimizer()
    : sampleRate_(16000)
    , channels_(1)
    , targetLatencyMs_(50)
    , adaptiveChunking_(true)
    , chunkOverlap_(0)
    , currentChunkSize_(1024)
    , minChunkSize_(256)
    , maxChunkSize_(4096)
    , totalChunksProcessed_(0)
    , totalSamplesProcessed_(0)
    , averageLatencyMs_(0.0)
    , averageThroughput_(0.0)
    , lastStatsUpdate_(std::chrono::steady_clock::now()) {
}

StreamingOptimizer::~StreamingOptimizer() {
}

bool StreamingOptimizer::initialize(int sampleRate, int channels, int targetLatencyMs) {
    sampleRate_ = sampleRate;
    channels_ = channels;
    targetLatencyMs_ = targetLatencyMs;
    
    // Calculate optimal chunk sizes based on target latency
    minChunkSize_ = static_cast<size_t>(sampleRate * channels * 0.010); // 10ms minimum
    maxChunkSize_ = static_cast<size_t>(sampleRate * channels * 0.100); // 100ms maximum
    currentChunkSize_ = static_cast<size_t>(sampleRate * channels * targetLatencyMs / 1000.0);
    
    // Clamp to valid range
    currentChunkSize_ = std::max(minChunkSize_, std::min(maxChunkSize_, currentChunkSize_));
    
    speechrnt::utils::Logger::info("StreamingOptimizer initialized: " + 
                       std::to_string(sampleRate) + "Hz, " +
                       std::to_string(channels) + " channels, " +
                       std::to_string(targetLatencyMs) + "ms target latency");
    
    return true;
}

bool StreamingOptimizer::processStream(const std::vector<float>& audioData, 
                                     std::vector<AudioChunk>& outputChunks) {
    if (!validateAudioData(audioData)) {
        return false;
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    outputChunks.clear();
    
    size_t dataSize = audioData.size();
    size_t processedSamples = 0;
    uint32_t chunkSequence = 0;
    
    // Handle overlap from previous processing
    std::vector<float> workingData;
    if (!overlapBuffer_.empty()) {
        workingData.reserve(overlapBuffer_.size() + dataSize);
        workingData.insert(workingData.end(), overlapBuffer_.begin(), overlapBuffer_.end());
        workingData.insert(workingData.end(), audioData.begin(), audioData.end());
    } else {
        workingData = audioData;
    }
    
    // Process data in optimized chunks
    while (processedSamples < workingData.size()) {
        size_t remainingSamples = workingData.size() - processedSamples;
        size_t chunkSize = std::min(currentChunkSize_, remainingSamples);
        
        if (chunkSize < minChunkSize_ && remainingSamples < workingData.size()) {
            // Save remaining data for next processing cycle
            overlapBuffer_.assign(workingData.begin() + processedSamples, workingData.end());
            break;
        }
        
        // Create chunk
        AudioChunk chunk;
        chunk.data.assign(workingData.begin() + processedSamples, 
                         workingData.begin() + processedSamples + chunkSize);
        chunk.sequenceNumber = chunkSequence++;
        chunk.timestamp = std::chrono::steady_clock::now();
        chunk.isLast = (processedSamples + chunkSize >= workingData.size());
        
        // Apply windowing if needed
        if (chunkOverlap_ > 0) {
            applyWindowFunction(chunk.data);
        }
        
        outputChunks.push_back(chunk);
        processedSamples += chunkSize;
        
        // Adaptive chunk size adjustment
        if (adaptiveChunking_) {
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto processingTime = std::chrono::duration_cast<std::chrono::microseconds>(
                currentTime - startTime).count() / 1000.0; // Convert to ms
            
            if (outputChunks.size() > 1) { // Need at least 2 chunks to estimate
                float estimatedLatency = static_cast<float>(processingTime / outputChunks.size());
                currentChunkSize_ = optimizeChunkSize(estimatedLatency, static_cast<float>(targetLatencyMs_));
            }
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalLatency = std::chrono::duration_cast<std::chrono::microseconds>(
        endTime - startTime).count() / 1000.0;
    
    updateStats(processedSamples, totalLatency);
    
    // Record performance metrics
    speechrnt::utils::PerformanceMonitor::getInstance().recordLatency(
        "audio.streaming_processing_latency_ms", totalLatency);
    speechrnt::utils::PerformanceMonitor::getInstance().recordThroughput(
        "audio.streaming_throughput_samples_per_sec", 
        processedSamples / (totalLatency / 1000.0));
    
    return true;
}

size_t StreamingOptimizer::optimizeChunkSize(float currentLatencyMs, float targetLatencyMs) {
    if (currentLatencyMs <= 0 || targetLatencyMs <= 0) {
        return currentChunkSize_;
    }
    
    // Calculate adjustment factor
    float latencyRatio = targetLatencyMs / currentLatencyMs;
    
    // Apply conservative adjustment to avoid oscillation
    float adjustmentFactor = 0.1f * (latencyRatio - 1.0f) + 1.0f;
    
    size_t newChunkSize = static_cast<size_t>(currentChunkSize_ * adjustmentFactor);
    
    // Clamp to valid range
    newChunkSize = std::max(minChunkSize_, std::min(maxChunkSize_, newChunkSize));
    
    if (newChunkSize != currentChunkSize_) {
        speechrnt::utils::Logger::debug("Adjusted chunk size from " + std::to_string(currentChunkSize_) + 
                           " to " + std::to_string(newChunkSize) + 
                           " (latency: " + std::to_string(currentLatencyMs) + "ms)");
    }
    
    return newChunkSize;
}

void StreamingOptimizer::setAdaptiveChunking(bool enabled) {
    adaptiveChunking_ = enabled;
    speechrnt::utils::Logger::info("Adaptive chunking " + std::string(enabled ? "enabled" : "disabled"));
}

void StreamingOptimizer::setChunkOverlap(size_t overlapSamples) {
    chunkOverlap_ = overlapSamples;
    overlapBuffer_.reserve(overlapSamples);
    speechrnt::utils::Logger::info("Chunk overlap set to " + std::to_string(overlapSamples) + " samples");
}

bool StreamingOptimizer::preprocessAudio(std::vector<float>& audioData) {
    if (audioData.empty()) {
        return false;
    }
    
    // Apply basic preprocessing optimizations
    
    // 1. DC offset removal
    float dcOffset = 0.0f;
    for (float sample : audioData) {
        dcOffset += sample;
    }
    dcOffset /= audioData.size();
    
    for (float& sample : audioData) {
        sample -= dcOffset;
    }
    
    // 2. Simple noise gate (very basic)
    const float noiseThreshold = 0.001f;
    for (float& sample : audioData) {
        if (std::abs(sample) < noiseThreshold) {
            sample *= 0.1f; // Reduce noise floor
        }
    }
    
    // 3. Soft clipping to prevent overflow
    const float maxAmplitude = 0.95f;
    for (float& sample : audioData) {
        if (sample > maxAmplitude) {
            sample = maxAmplitude;
        } else if (sample < -maxAmplitude) {
            sample = -maxAmplitude;
        }
    }
    
    return true;
}

std::map<std::string, double> StreamingOptimizer::getStreamingStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    std::map<std::string, double> stats;
    stats["total_chunks_processed"] = static_cast<double>(totalChunksProcessed_.load());
    stats["total_samples_processed"] = static_cast<double>(totalSamplesProcessed_.load());
    stats["average_latency_ms"] = averageLatencyMs_.load();
    stats["average_throughput_samples_per_sec"] = averageThroughput_.load();
    stats["current_chunk_size"] = static_cast<double>(currentChunkSize_);
    stats["adaptive_chunking_enabled"] = adaptiveChunking_ ? 1.0 : 0.0;
    stats["chunk_overlap_samples"] = static_cast<double>(chunkOverlap_);
    
    return stats;
}

void StreamingOptimizer::resetStats() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    totalChunksProcessed_ = 0;
    totalSamplesProcessed_ = 0;
    averageLatencyMs_ = 0.0;
    averageThroughput_ = 0.0;
    lastStatsUpdate_ = std::chrono::steady_clock::now();
    
    speechrnt::utils::Logger::info("Streaming optimizer statistics reset");
}

size_t StreamingOptimizer::getRecommendedBufferSize() const {
    // Recommend buffer size based on target latency and sample rate
    size_t samplesPerMs = sampleRate_ * channels_ / 1000;
    size_t bufferSize = samplesPerMs * targetLatencyMs_ * 2; // 2x target latency for safety
    
    return std::max(static_cast<size_t>(1024), bufferSize);
}

size_t StreamingOptimizer::calculateOptimalChunkSize(float latencyMs) const {
    if (latencyMs <= 0) {
        return currentChunkSize_;
    }
    
    // Calculate chunk size based on desired latency
    size_t samplesPerMs = sampleRate_ * channels_ / 1000;
    size_t optimalSize = static_cast<size_t>(samplesPerMs * latencyMs);
    
    return std::max(minChunkSize_, std::min(maxChunkSize_, optimalSize));
}

void StreamingOptimizer::updateStats(size_t samplesProcessed, double latencyMs) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    totalChunksProcessed_++;
    totalSamplesProcessed_ += samplesProcessed;
    
    // Update running averages
    double alpha = 0.1; // Smoothing factor
    averageLatencyMs_ = alpha * latencyMs + (1.0 - alpha) * averageLatencyMs_.load();
    
    if (latencyMs > 0) {
        double throughput = samplesProcessed / (latencyMs / 1000.0);
        averageThroughput_ = alpha * throughput + (1.0 - alpha) * averageThroughput_.load();
    }
    
    lastStatsUpdate_ = std::chrono::steady_clock::now();
}

bool StreamingOptimizer::validateAudioData(const std::vector<float>& audioData) const {
    if (audioData.empty()) {
        speechrnt::utils::Logger::warn("Empty audio data provided to streaming optimizer");
        return false;
    }
    
    // Check for invalid values
    for (float sample : audioData) {
        if (!std::isfinite(sample)) {
            speechrnt::utils::Logger::error("Invalid audio sample detected (NaN or Inf)");
            return false;
        }
    }
    
    return true;
}

void StreamingOptimizer::applyWindowFunction(std::vector<float>& chunk) const {
    if (chunk.empty()) {
        return;
    }
    
    // Apply Hann window to reduce spectral leakage
    size_t N = chunk.size();
    for (size_t i = 0; i < N; ++i) {
        float window = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (N - 1)));
        chunk[i] *= window;
    }
}

WebSocketOptimizer::WebSocketOptimizer()
    : maxMessageSize_(65536)
    , compressionEnabled_(true)
    , totalMessagesOptimized_(0)
    , totalBytesTransmitted_(0)
    , totalBytesCompressed_(0) {
}

WebSocketOptimizer::~WebSocketOptimizer() {
}

bool WebSocketOptimizer::initialize(size_t maxMessageSize, bool compressionEnabled) {
    maxMessageSize_ = maxMessageSize;
    compressionEnabled_ = compressionEnabled;
    
    speechrnt::utils::Logger::info("WebSocketOptimizer initialized: max message size " + 
                       std::to_string(maxMessageSize) + " bytes, compression " +
                       (compressionEnabled ? "enabled" : "disabled"));
    
    return true;
}

bool WebSocketOptimizer::optimizeForTransmission(const std::vector<float>& audioData,
                                               std::vector<std::vector<uint8_t>>& optimizedMessages) {
    optimizedMessages.clear();
    
    if (audioData.empty()) {
        return false;
    }
    
    // Convert float audio data to bytes
    size_t audioBytes = audioData.size() * sizeof(float);
    std::vector<uint8_t> rawData(audioBytes);
    std::memcpy(rawData.data(), audioData.data(), audioBytes);
    
    // Split into appropriately sized messages
    size_t bytesPerMessage = maxMessageSize_ - 64; // Leave room for headers
    size_t offset = 0;
    
    while (offset < rawData.size()) {
        size_t messageSize = std::min(bytesPerMessage, rawData.size() - offset);
        
        std::vector<uint8_t> messageData(rawData.begin() + offset, 
                                        rawData.begin() + offset + messageSize);
        
        if (compressionEnabled_) {
            messageData = compressData(messageData);
        }
        
        optimizedMessages.push_back(messageData);
        offset += messageSize;
    }
    
    totalMessagesOptimized_ += optimizedMessages.size();
    totalBytesTransmitted_ += audioBytes;
    
    return true;
}

bool WebSocketOptimizer::batchChunks(const std::vector<AudioChunk>& chunks,
                                   std::vector<std::vector<uint8_t>>& batchedMessages) {
    batchedMessages.clear();
    
    if (chunks.empty()) {
        return false;
    }
    
    std::vector<uint8_t> currentBatch;
    
    for (const auto& chunk : chunks) {
        std::vector<uint8_t> serializedChunk = serializeAudioChunk(chunk);
        
        // Check if adding this chunk would exceed message size
        if (currentBatch.size() + serializedChunk.size() > maxMessageSize_ - 64) {
            if (!currentBatch.empty()) {
                if (compressionEnabled_) {
                    currentBatch = compressData(currentBatch);
                }
                batchedMessages.push_back(currentBatch);
                currentBatch.clear();
            }
        }
        
        currentBatch.insert(currentBatch.end(), serializedChunk.begin(), serializedChunk.end());
    }
    
    // Add final batch if not empty
    if (!currentBatch.empty()) {
        if (compressionEnabled_) {
            currentBatch = compressData(currentBatch);
        }
        batchedMessages.push_back(currentBatch);
    }
    
    totalMessagesOptimized_ += batchedMessages.size();
    
    return true;
}

void WebSocketOptimizer::setCompressionEnabled(bool enabled) {
    compressionEnabled_ = enabled;
    speechrnt::utils::Logger::info("WebSocket compression " + std::string(enabled ? "enabled" : "disabled"));
}

void WebSocketOptimizer::setMaxMessageSize(size_t maxSize) {
    maxMessageSize_ = maxSize;
    speechrnt::utils::Logger::info("WebSocket max message size set to " + std::to_string(maxSize) + " bytes");
}

std::map<std::string, double> WebSocketOptimizer::getTransmissionStats() const {
    std::map<std::string, double> stats;
    stats["total_messages_optimized"] = static_cast<double>(totalMessagesOptimized_.load());
    stats["total_bytes_transmitted"] = static_cast<double>(totalBytesTransmitted_.load());
    stats["total_bytes_compressed"] = static_cast<double>(totalBytesCompressed_.load());
    stats["compression_ratio"] = totalBytesTransmitted_.load() > 0 ? 
        static_cast<double>(totalBytesCompressed_.load()) / totalBytesTransmitted_.load() : 0.0;
    stats["max_message_size"] = static_cast<double>(maxMessageSize_);
    stats["compression_enabled"] = compressionEnabled_ ? 1.0 : 0.0;
    
    return stats;
}

std::vector<uint8_t> WebSocketOptimizer::compressData(const std::vector<uint8_t>& data) const {
    // Simple compression placeholder - in production, use a real compression library
    // For now, just return the original data
    totalBytesCompressed_ += data.size();
    return data;
}

std::vector<uint8_t> WebSocketOptimizer::serializeAudioChunk(const AudioChunk& chunk) const {
    // Simple serialization - in production, use a proper serialization format
    std::vector<uint8_t> serialized;
    
    // Add sequence number (4 bytes)
    uint32_t seqNum = chunk.sequenceNumber;
    serialized.insert(serialized.end(), 
                     reinterpret_cast<const uint8_t*>(&seqNum),
                     reinterpret_cast<const uint8_t*>(&seqNum) + sizeof(uint32_t));
    
    // Add data size (4 bytes)
    uint32_t dataSize = static_cast<uint32_t>(chunk.data.size() * sizeof(float));
    serialized.insert(serialized.end(),
                     reinterpret_cast<const uint8_t*>(&dataSize),
                     reinterpret_cast<const uint8_t*>(&dataSize) + sizeof(uint32_t));
    
    // Add audio data
    serialized.insert(serialized.end(),
                     reinterpret_cast<const uint8_t*>(chunk.data.data()),
                     reinterpret_cast<const uint8_t*>(chunk.data.data()) + dataSize);
    
    return serialized;
}

bool WebSocketOptimizer::shouldBatchChunks(const std::vector<AudioChunk>& chunks) const {
    if (chunks.empty()) {
        return false;
    }
    
    // Calculate total size if batched
    size_t totalSize = 0;
    for (const auto& chunk : chunks) {
        totalSize += chunk.data.size() * sizeof(float) + 8; // 8 bytes for headers
    }
    
    return totalSize < maxMessageSize_ - 64; // Leave room for batch headers
}

} // namespace audio
} // namespace speechrnt