#pragma once

#include "stt/whisper_stt.hpp"
#include "stt/stt_interface.hpp"
#include "audio/audio_buffer_manager.hpp"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <functional>
#include <sstream>

namespace stt {

/**
 * StreamingAudioManager - Integration layer between AudioBufferManager and WhisperSTT
 * 
 * This class provides streaming transcription capabilities by managing audio buffers
 * per utterance and triggering transcriptions based on configurable thresholds.
 * It demonstrates how the AudioBufferManager integrates with the existing STT system.
 * 
 * Features:
 * - Per-utterance audio buffering with memory management
 * - Configurable transcription triggers (time-based, audio-based)
 * - Partial and final transcription results
 * - Thread-safe concurrent transcription support
 * - Performance monitoring and health checking
 */
class StreamingAudioManager {
public:
    /**
     * Configuration for streaming transcription
     */
    struct StreamingConfig {
        size_t maxBufferSizeMB = 8;              // Maximum buffer size per utterance
        size_t minAudioSamples = 16000;          // Minimum samples before transcription (1 second at 16kHz)
        uint32_t transcriptionIntervalMs = 1000; // Minimum interval between transcriptions
        bool enablePartialResults = true;        // Enable partial transcription results
        float confidenceThreshold = 0.5f;       // Minimum confidence for partial results
        
        StreamingConfig() = default;
    };
    
    /**
     * Statistics for monitoring streaming performance
     */
    struct StreamingStatistics {
        size_t activeTranscriptions;
        size_t totalTranscriptions;
        size_t totalAudioProcessed;
        double averageLatency;
        size_t bufferMemoryUsageMB;
        size_t droppedAudioSamples;
        
        StreamingStatistics() 
            : activeTranscriptions(0), totalTranscriptions(0), totalAudioProcessed(0)
            , averageLatency(0.0), bufferMemoryUsageMB(0), droppedAudioSamples(0) {}
    };

private:
    /**
     * Internal state for each streaming transcription
     */
    struct StreamingState {
        uint32_t utteranceId;
        TranscriptionCallback callback;
        StreamingConfig config;
        bool isActive;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point lastTranscriptionTime;
        size_t totalAudioProcessed;
        size_t transcriptionCount;
        
        StreamingState() 
            : utteranceId(0), isActive(false), totalAudioProcessed(0), transcriptionCount(0) {
            startTime = std::chrono::steady_clock::now();
            lastTranscriptionTime = startTime;
        }
    };

public:
    /**
     * Constructor - requires initialized WhisperSTT instance
     */
    explicit StreamingAudioManager(std::shared_ptr<WhisperSTT> whisperSTT);
    
    /**
     * Destructor - ensures proper cleanup
     */
    ~StreamingAudioManager();
    
    // Disable copy constructor and assignment
    StreamingAudioManager(const StreamingAudioManager&) = delete;
    StreamingAudioManager& operator=(const StreamingAudioManager&) = delete;
    
    /**
     * Initialize the streaming manager
     */
    bool initialize();
    
    /**
     * Streaming transcription management
     */
    bool startStreamingTranscription(uint32_t utteranceId, 
                                   TranscriptionCallback callback,
                                   const StreamingConfig& config = StreamingConfig());
    
    bool addAudioChunk(uint32_t utteranceId, const std::vector<float>& audioData);
    
    void finalizeStreamingTranscription(uint32_t utteranceId);
    
    void stopStreamingTranscription(uint32_t utteranceId);
    
    void stopAllTranscriptions();
    
    /**
     * Status and monitoring
     */
    bool isTranscribing(uint32_t utteranceId) const;
    
    size_t getActiveTranscriptionCount() const;
    
    StreamingStatistics getStatistics() const;
    
    std::string getHealthStatus() const;
    
    /**
     * Access to underlying components (for advanced usage)
     */
    std::shared_ptr<audio::AudioBufferManager> getAudioBufferManager() const {
        return audioBufferManager_;
    }
    
    std::shared_ptr<WhisperSTT> getWhisperSTT() const {
        return whisperSTT_;
    }

private:
    // Core components
    std::shared_ptr<WhisperSTT> whisperSTT_;
    std::unique_ptr<audio::AudioBufferManager> audioBufferManager_;
    
    // State management
    bool isInitialized_;
    mutable std::mutex streamingStateMutex_;
    std::unordered_map<uint32_t, StreamingState> streamingStates_;
    
    // Internal helper methods
    bool shouldTriggerTranscription(const StreamingState& state);
    void triggerStreamingTranscription(uint32_t utteranceId, StreamingState& state);
    void triggerFinalTranscription(uint32_t utteranceId, StreamingState& state);
    void handleTranscriptionResult(uint32_t utteranceId,
                                 const TranscriptionResult& result,
                                 TranscriptionCallback originalCallback,
                                 bool isPartial);
};

} // namespace stt