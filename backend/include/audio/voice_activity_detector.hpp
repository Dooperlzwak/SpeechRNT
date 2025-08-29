#pragma once

#include <vector>
#include <functional>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>
#include <queue>

namespace audio {

// VAD state machine states
enum class VadState {
    IDLE,
    SPEECH_DETECTED,
    SPEAKING,
    PAUSE_DETECTED
};

// VAD configuration parameters
struct VadConfig {
    float speechThreshold = 0.5f;      // Threshold for speech detection (0.0-1.0)
    float silenceThreshold = 0.3f;     // Threshold for silence detection (0.0-1.0)
    uint32_t minSpeechDurationMs = 100; // Minimum speech duration to trigger SPEAKING state
    uint32_t minSilenceDurationMs = 500; // Minimum silence duration to trigger utterance end
    uint32_t maxUtteranceDurationMs = 30000; // Maximum utterance duration (30 seconds)
    uint32_t windowSizeMs = 64;        // Analysis window size in milliseconds
    uint32_t sampleRate = 16000;       // Audio sample rate
    
    bool isValid() const {
        return speechThreshold >= 0.0f && speechThreshold <= 1.0f &&
               silenceThreshold >= 0.0f && silenceThreshold <= 1.0f &&
               speechThreshold > silenceThreshold &&
               minSpeechDurationMs > 0 && minSilenceDurationMs > 0 &&
               maxUtteranceDurationMs > minSpeechDurationMs &&
               windowSizeMs > 0 && sampleRate > 0;
    }
};

// VAD event data
struct VadEvent {
    VadState previousState;
    VadState currentState;
    std::chrono::steady_clock::time_point timestamp;
    float confidence;
    uint32_t utteranceId;
    
    VadEvent(VadState prev, VadState curr, float conf, uint32_t id)
        : previousState(prev), currentState(curr), 
          timestamp(std::chrono::steady_clock::now()),
          confidence(conf), utteranceId(id) {}
};

// Forward declarations for enhanced VAD integration
class SileroVadImpl;
class EnergyBasedVAD;

class VoiceActivityDetector {
public:
    using VadCallback = std::function<void(const VadEvent& event)>;
    using UtteranceCallback = std::function<void(uint32_t utteranceId, const std::vector<float>& audioData)>;
    
    explicit VoiceActivityDetector(const VadConfig& config = VadConfig{});
    ~VoiceActivityDetector();
    
    // Initialization and configuration
    bool initialize();
    void shutdown();
    bool isInitialized() const { return initialized_; }
    
    // Configuration management
    void setConfig(const VadConfig& config);
    const VadConfig& getConfig() const { return config_; }
    
    // Callback registration
    void setVadCallback(VadCallback callback);
    void setUtteranceCallback(UtteranceCallback callback);
    
    // Audio processing
    void processAudio(const std::vector<float>& audioData);
    void processAudioChunk(const std::vector<float>& audioData, std::chrono::steady_clock::time_point timestamp);
    
    // State management
    VadState getCurrentState() const { return currentState_; }
    uint32_t getCurrentUtteranceId() const { return currentUtteranceId_; }
    bool isSpeechActive() const { return currentState_ == VadState::SPEAKING; }
    
    // Enhanced VAD control
    bool isSileroModelLoaded() const;
    void setVadMode(int mode); // 0=SILERO, 1=ENERGY_BASED, 2=HYBRID
    int getCurrentVadMode() const;
    
    // Utterance management
    std::vector<float> getCurrentUtteranceAudio() const;
    void forceUtteranceEnd(); // Force end current utterance
    void reset(); // Reset to IDLE state
    
    // Statistics and monitoring
    struct Statistics {
        uint64_t totalAudioProcessed;
        uint64_t totalUtterances;
        uint64_t totalSpeechTime;
        uint64_t totalSilenceTime;
        double averageUtteranceDuration;
        double averageConfidence;
        std::chrono::steady_clock::time_point lastActivity;
    };
    
    Statistics getStatistics() const;
    void resetStatistics();
    
    // Error handling
    enum class ErrorCode {
        NONE,
        NOT_INITIALIZED,
        INVALID_CONFIG,
        PROCESSING_ERROR,
        MODEL_LOAD_ERROR
    };
    
    ErrorCode getLastError() const { return lastError_; }
    std::string getErrorMessage() const;
    
private:
    // Configuration
    VadConfig config_;
    std::atomic<bool> initialized_;
    
    // State management
    std::atomic<VadState> currentState_;
    std::atomic<uint32_t> currentUtteranceId_;
    std::atomic<uint32_t> nextUtteranceId_;
    
    // Timing
    std::chrono::steady_clock::time_point stateChangeTime_;
    std::chrono::steady_clock::time_point utteranceStartTime_;
    std::chrono::steady_clock::time_point lastAudioTime_;
    
    // Audio buffering for current utterance
    mutable std::mutex audioMutex_;
    std::vector<float> currentUtteranceAudio_;
    
    // Callbacks
    VadCallback vadCallback_;
    UtteranceCallback utteranceCallback_;
    
    // Statistics
    mutable std::mutex statsMutex_;
    Statistics stats_;
    
    // Error handling
    std::atomic<ErrorCode> lastError_;
    
    // Enhanced VAD implementations
    std::unique_ptr<SileroVadImpl> sileroVad_;
    std::unique_ptr<EnergyBasedVAD> energyBasedVad_;
    
    // Internal methods
    void transitionToState(VadState newState, float confidence);
    void processStateTransition(VadState newState, float confidence);
    void handleSpeechDetected(float confidence);
    void handleSpeechEnded(float confidence);
    void finalizeUtterance();
    void updateStatistics(bool isSpeech, float confidence);
    void setError(ErrorCode error);
    
    // Audio analysis
    float analyzeSpeechProbability(const std::vector<float>& audioData);
    bool shouldTransitionToSpeaking() const;
    bool shouldTransitionToPause() const;
    bool shouldTransitionToIdle() const;
    
    // Utility methods
    uint32_t getWindowSizeSamples() const;
    std::chrono::milliseconds getTimeSinceStateChange() const;
    std::chrono::milliseconds getTimeSinceUtteranceStart() const;
};

} // namespace audio