#pragma once

#include "audio/streaming_optimizer.hpp"
#include <unordered_map>
#include <set>
#include <chrono>
#include <atomic>
#include <mutex>

namespace speechrnt {
namespace audio {

/**
 * Packet information for tracking
 */
struct PacketInfo {
    uint32_t sequenceNumber;
    std::chrono::steady_clock::time_point timestamp;
    size_t dataSize;
    bool acknowledged;
    int retryCount;
    
    PacketInfo() : sequenceNumber(0), dataSize(0), acknowledged(false), retryCount(0) {}
    PacketInfo(uint32_t seq, size_t size) 
        : sequenceNumber(seq), timestamp(std::chrono::steady_clock::now())
        , dataSize(size), acknowledged(false), retryCount(0) {}
};

/**
 * Packet loss statistics
 */
struct PacketLossStats {
    uint64_t totalPacketsSent;
    uint64_t totalPacketsLost;
    uint64_t totalPacketsRecovered;
    uint64_t totalRetransmissions;
    float currentLossRate;
    float averageLossRate;
    std::chrono::steady_clock::time_point lastUpdate;
    
    PacketLossStats() 
        : totalPacketsSent(0), totalPacketsLost(0), totalPacketsRecovered(0)
        , totalRetransmissions(0), currentLossRate(0.0f), averageLossRate(0.0f)
        , lastUpdate(std::chrono::steady_clock::now()) {}
};

/**
 * Packet loss detector for audio streaming
 */
class PacketLossDetector {
public:
    PacketLossDetector();
    ~PacketLossDetector();
    
    /**
     * Initialize packet loss detector
     * @param timeoutMs Packet timeout in milliseconds
     * @param maxRetries Maximum retry attempts per packet
     * @return true if initialization successful
     */
    bool initialize(int timeoutMs = 1000, int maxRetries = 3);
    
    /**
     * Register sent packet for tracking
     * @param sequenceNumber Packet sequence number
     * @param dataSize Size of packet data
     */
    void registerSentPacket(uint32_t sequenceNumber, size_t dataSize);
    
    /**
     * Acknowledge received packet
     * @param sequenceNumber Packet sequence number
     */
    void acknowledgePacket(uint32_t sequenceNumber);
    
    /**
     * Check for lost packets and get list for retransmission
     * @param lostPackets Output vector of lost packet sequence numbers
     * @return number of lost packets detected
     */
    size_t detectLostPackets(std::vector<uint32_t>& lostPackets);
    
    /**
     * Mark packet for retransmission
     * @param sequenceNumber Packet sequence number
     * @return true if packet should be retransmitted
     */
    bool markForRetransmission(uint32_t sequenceNumber);
    
    /**
     * Get current packet loss statistics
     * @return packet loss statistics
     */
    PacketLossStats getPacketLossStats() const;
    
    /**
     * Reset packet loss statistics
     */
    void resetStats();
    
    /**
     * Set packet timeout
     * @param timeoutMs Timeout in milliseconds
     */
    void setPacketTimeout(int timeoutMs);
    
    /**
     * Set maximum retries per packet
     * @param maxRetries Maximum retry attempts
     */
    void setMaxRetries(int maxRetries);
    
    /**
     * Check if packet loss rate is acceptable
     * @param threshold Maximum acceptable loss rate (0.0-1.0)
     * @return true if loss rate is acceptable
     */
    bool isLossRateAcceptable(float threshold = 0.05f) const;

private:
    // Configuration
    int packetTimeoutMs_;
    int maxRetries_;
    
    // Packet tracking
    mutable std::mutex packetsMutex_;
    std::unordered_map<uint32_t, PacketInfo> pendingPackets_;
    std::set<uint32_t> acknowledgedPackets_;
    
    // Statistics
    mutable std::mutex statsMutex_;
    PacketLossStats stats_;
    std::vector<float> recentLossRates_;
    
    // Private methods
    void updateLossRate();
    void cleanupOldPackets();
    bool isPacketTimedOut(const PacketInfo& packet) const;
};

/**
 * Audio chunk reordering buffer for handling out-of-sequence packets
 */
class AudioChunkReorderBuffer {
public:
    AudioChunkReorderBuffer();
    ~AudioChunkReorderBuffer();
    
    /**
     * Initialize reorder buffer
     * @param maxBufferSize Maximum number of chunks to buffer
     * @param reorderTimeoutMs Timeout for reordering in milliseconds
     * @return true if initialization successful
     */
    bool initialize(size_t maxBufferSize = 50, int reorderTimeoutMs = 500);
    
    /**
     * Add audio chunk to reorder buffer
     * @param chunk Audio chunk to add
     * @return true if chunk was added successfully
     */
    bool addChunk(const AudioChunk& chunk);
    
    /**
     * Get next ordered chunk if available
     * @param chunk Output chunk
     * @return true if ordered chunk is available
     */
    bool getNextOrderedChunk(AudioChunk& chunk);
    
    /**
     * Get multiple ordered chunks
     * @param chunks Output vector of chunks
     * @param maxChunks Maximum number of chunks to retrieve
     * @return number of chunks retrieved
     */
    size_t getOrderedChunks(std::vector<AudioChunk>& chunks, size_t maxChunks = 10);
    
    /**
     * Force flush of buffered chunks (in order)
     * @param chunks Output vector of chunks
     * @return number of chunks flushed
     */
    size_t flushBufferedChunks(std::vector<AudioChunk>& chunks);
    
    /**
     * Check if there are gaps in the sequence
     * @param missingSequences Output vector of missing sequence numbers
     * @return number of missing sequences
     */
    size_t detectSequenceGaps(std::vector<uint32_t>& missingSequences) const;
    
    /**
     * Get reorder buffer statistics
     * @return map of buffer statistics
     */
    std::map<std::string, double> getReorderStats() const;
    
    /**
     * Clear all buffered chunks
     */
    void clear();

private:
    // Configuration
    size_t maxBufferSize_;
    int reorderTimeoutMs_;
    
    // Buffer state
    mutable std::mutex bufferMutex_;
    std::map<uint32_t, AudioChunk> reorderBuffer_;
    uint32_t expectedSequenceNumber_;
    
    // Statistics
    std::atomic<uint64_t> totalChunksReceived_;
    std::atomic<uint64_t> totalChunksReordered_;
    std::atomic<uint64_t> totalChunksDropped_;
    std::atomic<uint64_t> totalSequenceGaps_;
    
    // Private methods
    bool isChunkTimedOut(const AudioChunk& chunk) const;
    void removeTimedOutChunks();
    void updateExpectedSequence();
};

/**
 * Comprehensive packet recovery system
 */
class PacketRecoverySystem {
public:
    PacketRecoverySystem();
    ~PacketRecoverySystem();
    
    /**
     * Initialize packet recovery system
     * @param config Recovery configuration
     * @return true if initialization successful
     */
    bool initialize(const std::map<std::string, std::string>& config = {});
    
    /**
     * Process outgoing audio chunk with loss detection
     * @param chunk Audio chunk to send
     * @param packetId Output packet ID for tracking
     * @return true if chunk processed successfully
     */
    bool processOutgoingChunk(const AudioChunk& chunk, uint32_t& packetId);
    
    /**
     * Process incoming audio chunk with reordering
     * @param chunk Incoming audio chunk
     * @param orderedChunks Output vector of ordered chunks ready for processing
     * @return true if chunk processed successfully
     */
    bool processIncomingChunk(const AudioChunk& chunk, std::vector<AudioChunk>& orderedChunks);
    
    /**
     * Acknowledge packet reception
     * @param packetId Packet ID to acknowledge
     */
    void acknowledgePacket(uint32_t packetId);
    
    /**
     * Get packets that need retransmission
     * @param retransmitChunks Output vector of chunks to retransmit
     * @return number of chunks to retransmit
     */
    size_t getRetransmissionQueue(std::vector<AudioChunk>& retransmitChunks);
    
    /**
     * Update recovery parameters based on network conditions
     * @param lossRate Current packet loss rate
     * @param latencyMs Current network latency
     * @param jitterMs Current network jitter
     */
    void updateRecoveryParams(float lossRate, float latencyMs, float jitterMs);
    
    /**
     * Get comprehensive recovery statistics
     * @return map of recovery statistics
     */
    std::map<std::string, double> getRecoveryStats() const;
    
    /**
     * Enable/disable packet recovery
     * @param enabled true to enable recovery
     */
    void setRecoveryEnabled(bool enabled);
    
    /**
     * Set recovery aggressiveness level
     * @param level Aggressiveness level (0.0-1.0)
     */
    void setRecoveryAggressiveness(float level);

private:
    // Components
    std::unique_ptr<PacketLossDetector> lossDetector_;
    std::unique_ptr<AudioChunkReorderBuffer> reorderBuffer_;
    
    // Configuration
    bool recoveryEnabled_;
    float recoveryAggressiveness_;
    
    // Packet tracking
    mutable std::mutex chunksMutex_;
    std::unordered_map<uint32_t, AudioChunk> sentChunks_;
    std::atomic<uint32_t> nextPacketId_;
    
    // Statistics
    std::atomic<uint64_t> totalChunksProcessed_;
    std::atomic<uint64_t> totalRetransmissions_;
    std::atomic<uint64_t> totalRecoveredChunks_;
    
    // Private methods
    void cleanupOldChunks();
    bool shouldRetransmit(const AudioChunk& chunk, float lossRate) const;
    void adaptRecoveryParameters(float lossRate, float latencyMs, float jitterMs);
};

} // namespace audio
} // namespace speechrnt