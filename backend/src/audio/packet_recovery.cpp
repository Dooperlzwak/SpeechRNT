#include "audio/packet_recovery.hpp"
#include "utils/logging.hpp"
#include "utils/performance_monitor.hpp"
#include <algorithm>
#include <numeric>

namespace speechrnt {
namespace audio {

PacketLossDetector::PacketLossDetector()
    : packetTimeoutMs_(1000)
    , maxRetries_(3) {
}

PacketLossDetector::~PacketLossDetector() {
}

bool PacketLossDetector::initialize(int timeoutMs, int maxRetries) {
    packetTimeoutMs_ = timeoutMs;
    maxRetries_ = maxRetries;
    
    // Reserve space for recent loss rates
    recentLossRates_.reserve(100);
    
    utils::Logger::info("PacketLossDetector initialized: " + 
                       std::to_string(timeoutMs) + "ms timeout, " +
                       std::to_string(maxRetries) + " max retries");
    
    return true;
}

void PacketLossDetector::registerSentPacket(uint32_t sequenceNumber, size_t dataSize) {
    std::lock_guard<std::mutex> lock(packetsMutex_);
    
    PacketInfo packet(sequenceNumber, dataSize);
    pendingPackets_[sequenceNumber] = packet;
    
    // Update statistics
    std::lock_guard<std::mutex> statsLock(statsMutex_);
    stats_.totalPacketsSent++;
}

void PacketLossDetector::acknowledgePacket(uint32_t sequenceNumber) {
    std::lock_guard<std::mutex> lock(packetsMutex_);
    
    auto it = pendingPackets_.find(sequenceNumber);
    if (it != pendingPackets_.end()) {
        it->second.acknowledged = true;
        acknowledgedPackets_.insert(sequenceNumber);
        pendingPackets_.erase(it);
    }
}

size_t PacketLossDetector::detectLostPackets(std::vector<uint32_t>& lostPackets) {
    lostPackets.clear();
    
    std::lock_guard<std::mutex> lock(packetsMutex_);
    
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = pendingPackets_.begin(); it != pendingPackets_.end();) {
        if (isPacketTimedOut(it->second)) {
            lostPackets.push_back(it->first);
            
            // Update statistics
            std::lock_guard<std::mutex> statsLock(statsMutex_);
            stats_.totalPacketsLost++;
            
            it = pendingPackets_.erase(it);
        } else {
            ++it;
        }
    }
    
    if (!lostPackets.empty()) {
        updateLossRate();
        utils::Logger::debug("Detected " + std::to_string(lostPackets.size()) + " lost packets");
    }
    
    // Cleanup old acknowledged packets
    cleanupOldPackets();
    
    return lostPackets.size();
}

bool PacketLossDetector::markForRetransmission(uint32_t sequenceNumber) {
    std::lock_guard<std::mutex> lock(packetsMutex_);
    
    auto it = pendingPackets_.find(sequenceNumber);
    if (it != pendingPackets_.end()) {
        if (it->second.retryCount < maxRetries_) {
            it->second.retryCount++;
            it->second.timestamp = std::chrono::steady_clock::now(); // Reset timeout
            
            // Update statistics
            std::lock_guard<std::mutex> statsLock(statsMutex_);
            stats_.totalRetransmissions++;
            
            return true;
        } else {
            // Max retries reached, give up on this packet
            pendingPackets_.erase(it);
            return false;
        }
    }
    
    return false;
}

PacketLossStats PacketLossDetector::getPacketLossStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void PacketLossDetector::resetStats() {
    std::lock_guard<std::mutex> statsLock(statsMutex_);
    std::lock_guard<std::mutex> packetsLock(packetsMutex_);
    
    stats_ = PacketLossStats();
    recentLossRates_.clear();
    pendingPackets_.clear();
    acknowledgedPackets_.clear();
    
    utils::Logger::info("Packet loss detector statistics reset");
}

void PacketLossDetector::setPacketTimeout(int timeoutMs) {
    packetTimeoutMs_ = timeoutMs;
    utils::Logger::info("Packet timeout set to " + std::to_string(timeoutMs) + "ms");
}

void PacketLossDetector::setMaxRetries(int maxRetries) {
    maxRetries_ = maxRetries;
    utils::Logger::info("Max retries set to " + std::to_string(maxRetries));
}

bool PacketLossDetector::isLossRateAcceptable(float threshold) const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_.currentLossRate <= threshold;
}

void PacketLossDetector::updateLossRate() {
    if (stats_.totalPacketsSent == 0) {
        stats_.currentLossRate = 0.0f;
        return;
    }
    
    stats_.currentLossRate = static_cast<float>(stats_.totalPacketsLost) / 
                            static_cast<float>(stats_.totalPacketsSent);
    
    // Update recent loss rates for averaging
    recentLossRates_.push_back(stats_.currentLossRate);
    if (recentLossRates_.size() > 50) {
        recentLossRates_.erase(recentLossRates_.begin());
    }
    
    // Calculate average loss rate
    if (!recentLossRates_.empty()) {
        stats_.averageLossRate = std::accumulate(recentLossRates_.begin(), 
                                               recentLossRates_.end(), 0.0f) / 
                                recentLossRates_.size();
    }
    
    stats_.lastUpdate = std::chrono::steady_clock::now();
    
    // Record performance metrics
    utils::PerformanceMonitor::getInstance().recordValue(
        "audio.packet_loss_rate", stats_.currentLossRate);
}

void PacketLossDetector::cleanupOldPackets() {
    auto cutoffTime = std::chrono::steady_clock::now() - 
                     std::chrono::milliseconds(packetTimeoutMs_ * 10); // Keep 10x timeout
    
    for (auto it = acknowledgedPackets_.begin(); it != acknowledgedPackets_.end();) {
        // This is a simplified cleanup - in practice, we'd need to track timestamps
        // for acknowledged packets too
        ++it;
    }
}

bool PacketLossDetector::isPacketTimedOut(const PacketInfo& packet) const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - packet.timestamp).count();
    
    return elapsed > packetTimeoutMs_;
}

AudioChunkReorderBuffer::AudioChunkReorderBuffer()
    : maxBufferSize_(50)
    , reorderTimeoutMs_(500)
    , expectedSequenceNumber_(0)
    , totalChunksReceived_(0)
    , totalChunksReordered_(0)
    , totalChunksDropped_(0)
    , totalSequenceGaps_(0) {
}

AudioChunkReorderBuffer::~AudioChunkReorderBuffer() {
}

bool AudioChunkReorderBuffer::initialize(size_t maxBufferSize, int reorderTimeoutMs) {
    maxBufferSize_ = maxBufferSize;
    reorderTimeoutMs_ = reorderTimeoutMs;
    
    utils::Logger::info("AudioChunkReorderBuffer initialized: " + 
                       std::to_string(maxBufferSize) + " max buffer size, " +
                       std::to_string(reorderTimeoutMs) + "ms reorder timeout");
    
    return true;
}

bool AudioChunkReorderBuffer::addChunk(const AudioChunk& chunk) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    totalChunksReceived_++;
    
    // Check if buffer is full
    if (reorderBuffer_.size() >= maxBufferSize_) {
        // Remove oldest chunk to make space
        auto oldestIt = reorderBuffer_.begin();
        for (auto it = reorderBuffer_.begin(); it != reorderBuffer_.end(); ++it) {
            if (it->second.timestamp < oldestIt->second.timestamp) {
                oldestIt = it;
            }
        }
        reorderBuffer_.erase(oldestIt);
        totalChunksDropped_++;
    }
    
    // Add chunk to buffer
    reorderBuffer_[chunk.sequenceNumber] = chunk;
    
    // Check if this chunk is out of order
    if (chunk.sequenceNumber != expectedSequenceNumber_) {
        totalChunksReordered_++;
    }
    
    // Remove timed out chunks
    removeTimedOutChunks();
    
    return true;
}

bool AudioChunkReorderBuffer::getNextOrderedChunk(AudioChunk& chunk) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    auto it = reorderBuffer_.find(expectedSequenceNumber_);
    if (it != reorderBuffer_.end()) {
        chunk = it->second;
        reorderBuffer_.erase(it);
        expectedSequenceNumber_++;
        return true;
    }
    
    return false;
}

size_t AudioChunkReorderBuffer::getOrderedChunks(std::vector<AudioChunk>& chunks, size_t maxChunks) {
    chunks.clear();
    chunks.reserve(maxChunks);
    
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    while (chunks.size() < maxChunks) {
        auto it = reorderBuffer_.find(expectedSequenceNumber_);
        if (it != reorderBuffer_.end()) {
            chunks.push_back(it->second);
            reorderBuffer_.erase(it);
            expectedSequenceNumber_++;
        } else {
            break;
        }
    }
    
    return chunks.size();
}

size_t AudioChunkReorderBuffer::flushBufferedChunks(std::vector<AudioChunk>& chunks) {
    chunks.clear();
    
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    // Sort chunks by sequence number
    std::vector<std::pair<uint32_t, AudioChunk>> sortedChunks;
    for (const auto& pair : reorderBuffer_) {
        sortedChunks.push_back(pair);
    }
    
    std::sort(sortedChunks.begin(), sortedChunks.end(),
              [](const auto& a, const auto& b) {
                  return a.first < b.first;
              });
    
    // Extract chunks
    chunks.reserve(sortedChunks.size());
    for (const auto& pair : sortedChunks) {
        chunks.push_back(pair.second);
    }
    
    // Update expected sequence number to the next after the last chunk
    if (!sortedChunks.empty()) {
        expectedSequenceNumber_ = sortedChunks.back().first + 1;
    }
    
    size_t flushedCount = reorderBuffer_.size();
    reorderBuffer_.clear();
    
    return flushedCount;
}

size_t AudioChunkReorderBuffer::detectSequenceGaps(std::vector<uint32_t>& missingSequences) const {
    missingSequences.clear();
    
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    if (reorderBuffer_.empty()) {
        return 0;
    }
    
    // Find the range of sequence numbers in buffer
    uint32_t minSeq = reorderBuffer_.begin()->first;
    uint32_t maxSeq = minSeq;
    
    for (const auto& pair : reorderBuffer_) {
        minSeq = std::min(minSeq, pair.first);
        maxSeq = std::max(maxSeq, pair.first);
    }
    
    // Check for gaps in the range
    for (uint32_t seq = minSeq; seq <= maxSeq; ++seq) {
        if (reorderBuffer_.find(seq) == reorderBuffer_.end()) {
            missingSequences.push_back(seq);
        }
    }
    
    return missingSequences.size();
}

std::map<std::string, double> AudioChunkReorderBuffer::getReorderStats() const {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    
    std::map<std::string, double> stats;
    stats["total_chunks_received"] = static_cast<double>(totalChunksReceived_.load());
    stats["total_chunks_reordered"] = static_cast<double>(totalChunksReordered_.load());
    stats["total_chunks_dropped"] = static_cast<double>(totalChunksDropped_.load());
    stats["total_sequence_gaps"] = static_cast<double>(totalSequenceGaps_.load());
    stats["current_buffer_size"] = static_cast<double>(reorderBuffer_.size());
    stats["expected_sequence_number"] = static_cast<double>(expectedSequenceNumber_);
    stats["max_buffer_size"] = static_cast<double>(maxBufferSize_);
    stats["reorder_timeout_ms"] = static_cast<double>(reorderTimeoutMs_);
    
    if (totalChunksReceived_.load() > 0) {
        stats["reorder_rate"] = static_cast<double>(totalChunksReordered_.load()) / 
                               static_cast<double>(totalChunksReceived_.load());
        stats["drop_rate"] = static_cast<double>(totalChunksDropped_.load()) / 
                            static_cast<double>(totalChunksReceived_.load());
    }
    
    return stats;
}

void AudioChunkReorderBuffer::clear() {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    reorderBuffer_.clear();
    expectedSequenceNumber_ = 0;
}

bool AudioChunkReorderBuffer::isChunkTimedOut(const AudioChunk& chunk) const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - chunk.timestamp).count();
    
    return elapsed > reorderTimeoutMs_;
}

void AudioChunkReorderBuffer::removeTimedOutChunks() {
    for (auto it = reorderBuffer_.begin(); it != reorderBuffer_.end();) {
        if (isChunkTimedOut(it->second)) {
            it = reorderBuffer_.erase(it);
            totalChunksDropped_++;
        } else {
            ++it;
        }
    }
}

void AudioChunkReorderBuffer::updateExpectedSequence() {
    // Find the lowest sequence number in buffer
    if (!reorderBuffer_.empty()) {
        uint32_t minSeq = reorderBuffer_.begin()->first;
        for (const auto& pair : reorderBuffer_) {
            minSeq = std::min(minSeq, pair.first);
        }
        expectedSequenceNumber_ = minSeq;
    }
}

PacketRecoverySystem::PacketRecoverySystem()
    : recoveryEnabled_(true)
    , recoveryAggressiveness_(0.5f)
    , nextPacketId_(1)
    , totalChunksProcessed_(0)
    , totalRetransmissions_(0)
    , totalRecoveredChunks_(0) {
}

PacketRecoverySystem::~PacketRecoverySystem() {
}

bool PacketRecoverySystem::initialize(const std::map<std::string, std::string>& config) {
    // Initialize components
    lossDetector_ = std::make_unique<PacketLossDetector>();
    reorderBuffer_ = std::make_unique<AudioChunkReorderBuffer>();
    
    // Configure from config map
    int timeout = 1000;
    int maxRetries = 3;
    size_t bufferSize = 50;
    int reorderTimeout = 500;
    
    auto timeoutIt = config.find("packet_timeout_ms");
    if (timeoutIt != config.end()) {
        timeout = std::stoi(timeoutIt->second);
    }
    
    auto retriesIt = config.find("max_retries");
    if (retriesIt != config.end()) {
        maxRetries = std::stoi(retriesIt->second);
    }
    
    auto bufferIt = config.find("reorder_buffer_size");
    if (bufferIt != config.end()) {
        bufferSize = std::stoul(bufferIt->second);
    }
    
    auto reorderIt = config.find("reorder_timeout_ms");
    if (reorderIt != config.end()) {
        reorderTimeout = std::stoi(reorderIt->second);
    }
    
    bool lossDetectorOk = lossDetector_->initialize(timeout, maxRetries);
    bool reorderBufferOk = reorderBuffer_->initialize(bufferSize, reorderTimeout);
    
    if (lossDetectorOk && reorderBufferOk) {
        utils::Logger::info("PacketRecoverySystem initialized successfully");
        return true;
    } else {
        utils::Logger::error("PacketRecoverySystem initialization failed");
        return false;
    }
}

bool PacketRecoverySystem::processOutgoingChunk(const AudioChunk& chunk, uint32_t& packetId) {
    if (!recoveryEnabled_) {
        packetId = 0;
        return true;
    }
    
    packetId = nextPacketId_++;
    
    // Store chunk for potential retransmission
    {
        std::lock_guard<std::mutex> lock(chunksMutex_);
        AudioChunk chunkCopy = chunk;
        chunkCopy.sequenceNumber = packetId;
        sentChunks_[packetId] = chunkCopy;
    }
    
    // Register with loss detector
    lossDetector_->registerSentPacket(packetId, chunk.data.size() * sizeof(float));
    
    totalChunksProcessed_++;
    
    return true;
}

bool PacketRecoverySystem::processIncomingChunk(const AudioChunk& chunk, 
                                              std::vector<AudioChunk>& orderedChunks) {
    orderedChunks.clear();
    
    if (!recoveryEnabled_) {
        orderedChunks.push_back(chunk);
        return true;
    }
    
    // Add chunk to reorder buffer
    reorderBuffer_->addChunk(chunk);
    
    // Get ordered chunks that are ready
    reorderBuffer_->getOrderedChunks(orderedChunks, 10);
    
    totalChunksProcessed_++;
    
    return true;
}

void PacketRecoverySystem::acknowledgePacket(uint32_t packetId) {
    if (!recoveryEnabled_) {
        return;
    }
    
    lossDetector_->acknowledgePacket(packetId);
    
    // Remove from sent chunks
    std::lock_guard<std::mutex> lock(chunksMutex_);
    sentChunks_.erase(packetId);
}

size_t PacketRecoverySystem::getRetransmissionQueue(std::vector<AudioChunk>& retransmitChunks) {
    retransmitChunks.clear();
    
    if (!recoveryEnabled_) {
        return 0;
    }
    
    // Detect lost packets
    std::vector<uint32_t> lostPackets;
    lossDetector_->detectLostPackets(lostPackets);
    
    // Get chunks for retransmission
    std::lock_guard<std::mutex> lock(chunksMutex_);
    for (uint32_t packetId : lostPackets) {
        auto it = sentChunks_.find(packetId);
        if (it != sentChunks_.end()) {
            if (lossDetector_->markForRetransmission(packetId)) {
                retransmitChunks.push_back(it->second);
                totalRetransmissions_++;
            } else {
                // Max retries reached, remove chunk
                sentChunks_.erase(it);
            }
        }
    }
    
    // Cleanup old chunks periodically
    cleanupOldChunks();
    
    return retransmitChunks.size();
}

void PacketRecoverySystem::updateRecoveryParams(float lossRate, float latencyMs, float jitterMs) {
    if (!recoveryEnabled_) {
        return;
    }
    
    adaptRecoveryParameters(lossRate, latencyMs, jitterMs);
}

std::map<std::string, double> PacketRecoverySystem::getRecoveryStats() const {
    std::map<std::string, double> stats;
    
    // Combine statistics from components
    auto lossStats = lossDetector_->getPacketLossStats();
    auto reorderStats = reorderBuffer_->getReorderStats();
    
    // Loss detector stats
    stats["total_packets_sent"] = static_cast<double>(lossStats.totalPacketsSent);
    stats["total_packets_lost"] = static_cast<double>(lossStats.totalPacketsLost);
    stats["total_packets_recovered"] = static_cast<double>(lossStats.totalPacketsRecovered);
    stats["current_loss_rate"] = static_cast<double>(lossStats.currentLossRate);
    stats["average_loss_rate"] = static_cast<double>(lossStats.averageLossRate);
    
    // Reorder buffer stats
    stats["total_chunks_reordered"] = reorderStats["total_chunks_reordered"];
    stats["total_chunks_dropped"] = reorderStats["total_chunks_dropped"];
    stats["reorder_rate"] = reorderStats["reorder_rate"];
    
    // Recovery system stats
    stats["total_chunks_processed"] = static_cast<double>(totalChunksProcessed_.load());
    stats["total_retransmissions"] = static_cast<double>(totalRetransmissions_.load());
    stats["total_recovered_chunks"] = static_cast<double>(totalRecoveredChunks_.load());
    stats["recovery_enabled"] = recoveryEnabled_ ? 1.0 : 0.0;
    stats["recovery_aggressiveness"] = static_cast<double>(recoveryAggressiveness_);
    
    // Calculate recovery effectiveness
    if (lossStats.totalPacketsLost > 0) {
        stats["recovery_effectiveness"] = static_cast<double>(lossStats.totalPacketsRecovered) / 
                                        static_cast<double>(lossStats.totalPacketsLost);
    } else {
        stats["recovery_effectiveness"] = 1.0;
    }
    
    return stats;
}

void PacketRecoverySystem::setRecoveryEnabled(bool enabled) {
    recoveryEnabled_ = enabled;
    utils::Logger::info("Packet recovery " + std::string(enabled ? "enabled" : "disabled"));
}

void PacketRecoverySystem::setRecoveryAggressiveness(float level) {
    recoveryAggressiveness_ = std::max(0.0f, std::min(1.0f, level));
    utils::Logger::info("Recovery aggressiveness set to " + std::to_string(recoveryAggressiveness_));
}

void PacketRecoverySystem::cleanupOldChunks() {
    auto cutoffTime = std::chrono::steady_clock::now() - std::chrono::seconds(30);
    
    for (auto it = sentChunks_.begin(); it != sentChunks_.end();) {
        if (it->second.timestamp < cutoffTime) {
            it = sentChunks_.erase(it);
        } else {
            ++it;
        }
    }
}

bool PacketRecoverySystem::shouldRetransmit(const AudioChunk& chunk, float lossRate) const {
    // More aggressive retransmission for higher loss rates
    float threshold = 0.1f + (lossRate * recoveryAggressiveness_);
    
    // Always retransmit if loss rate is high
    if (lossRate > 0.05f) {
        return true;
    }
    
    // Consider chunk age
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - chunk.timestamp).count();
    
    return age > (1000 * threshold); // Retransmit if older than threshold
}

void PacketRecoverySystem::adaptRecoveryParameters(float lossRate, float latencyMs, float jitterMs) {
    // Adjust packet timeout based on latency and jitter
    int newTimeout = static_cast<int>(latencyMs * 2 + jitterMs * 3);
    newTimeout = std::max(500, std::min(5000, newTimeout)); // Clamp between 500ms and 5s
    
    lossDetector_->setPacketTimeout(newTimeout);
    
    // Adjust max retries based on loss rate
    int newMaxRetries = 3;
    if (lossRate > 0.05f) {
        newMaxRetries = 5;
    } else if (lossRate > 0.02f) {
        newMaxRetries = 4;
    }
    
    lossDetector_->setMaxRetries(newMaxRetries);
    
    utils::Logger::debug("Adapted recovery parameters: timeout=" + std::to_string(newTimeout) + 
                        "ms, maxRetries=" + std::to_string(newMaxRetries));
}

} // namespace audio
} // namespace speechrnt