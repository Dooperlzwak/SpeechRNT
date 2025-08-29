#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <vector>
#include <atomic>
#include <functional>
#include "core/task_queue.hpp"
#include "stt/stt_interface.hpp"
#include "mt/translation_interface.hpp"
#include "tts/tts_interface.hpp"

// Forward declaration
namespace speechrnt {
namespace core {
class TranslationPipeline;
}
}

namespace speechrnt {
namespace core {

/**
 * States an utterance can be in during processing
 */
enum class UtteranceState {
    LISTENING,      // Audio is being captured
    TRANSCRIBING,   // Speech-to-text processing
    TRANSLATING,    // Machine translation processing
    SYNTHESIZING,   // Text-to-speech processing
    COMPLETE,       // All processing complete
    ERROR           // Error occurred during processing
};

/**
 * Data structure representing an utterance
 */
struct UtteranceData {
    uint32_t id;
    std::string session_id;
    UtteranceState state;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point last_updated;
    
    // Audio data
    std::vector<float> audio_buffer;
    
    // Processing results
    std::string transcript;
    float transcription_confidence;
    std::string translation;
    std::vector<uint8_t> synthesized_audio;
    
    // Error information
    std::string error_message;
    
    // Processing metadata
    std::string source_language;
    std::string target_language;
    std::string voice_id;
    
    UtteranceData(uint32_t utterance_id, const std::string& sess_id)
        : id(utterance_id)
        , session_id(sess_id)
        , state(UtteranceState::LISTENING)
        , created_at(std::chrono::steady_clock::now())
        , last_updated(std::chrono::steady_clock::now())
        , transcription_confidence(0.0f) {}
};

/**
 * Callback function types for utterance state changes
 */
using UtteranceStateCallback = std::function<void(const UtteranceData&)>;
using UtteranceCompleteCallback = std::function<void(const UtteranceData&)>;
using UtteranceErrorCallback = std::function<void(const UtteranceData&, const std::string&)>;

/**
 * Configuration for utterance management
 */
struct UtteranceManagerConfig {
    size_t max_concurrent_utterances = 10;
    std::chrono::seconds utterance_timeout = std::chrono::seconds(30);
    std::chrono::seconds cleanup_interval = std::chrono::seconds(60);
    bool enable_automatic_cleanup = true;
};

/**
 * Manages the lifecycle of utterances and coordinates concurrent processing
 */
class UtteranceManager {
public:
    explicit UtteranceManager(const UtteranceManagerConfig& config = UtteranceManagerConfig{});
    ~UtteranceManager();
    
    // Non-copyable, non-movable
    UtteranceManager(const UtteranceManager&) = delete;
    UtteranceManager& operator=(const UtteranceManager&) = delete;
    UtteranceManager(UtteranceManager&&) = delete;
    UtteranceManager& operator=(UtteranceManager&&) = delete;
    
    /**
     * Initialize the utterance manager with a task queue for processing
     */
    void initialize(std::shared_ptr<TaskQueue> task_queue);
    
    /**
     * Initialize with translation pipeline integration
     */
    void initializeWithPipeline(
        std::shared_ptr<TaskQueue> task_queue,
        std::shared_ptr<TranslationPipeline> translation_pipeline
    );
    
    /**
     * Initialize with translation pipeline and STT engine
     */
    void initializeWithEngines(
        std::shared_ptr<TaskQueue> task_queue,
        std::shared_ptr<TranslationPipeline> translation_pipeline,
        std::shared_ptr<stt::STTInterface> stt_engine
    );
    
    /**
     * Set or replace the STT engine after initialization
     */
    void setSTTEngine(std::shared_ptr<stt::STTInterface> stt_engine);
    
    /**
     * Set or replace the MT engine after initialization
     */
    void setMTEngine(std::shared_ptr<mt::TranslationInterface> mt_engine);
    
    /**
     * Set or replace the TTS engine after initialization
     */
    void setTTSEngine(std::shared_ptr<tts::TTSInterface> tts_engine);
    
    /**
     * Shutdown the utterance manager and cleanup resources
     */
    void shutdown();
    
    /**
     * Create a new utterance for the given session
     * Returns the utterance ID
     */
    uint32_t createUtterance(const std::string& session_id);
    
    /**
     * Update the state of an utterance
     */
    bool updateUtteranceState(uint32_t utterance_id, UtteranceState new_state);
    
    /**
     * Get the current state of an utterance
     */
    UtteranceState getUtteranceState(uint32_t utterance_id) const;
    
    /**
     * Get utterance data (thread-safe copy)
     */
    std::shared_ptr<UtteranceData> getUtterance(uint32_t utterance_id) const;
    
    /**
     * Add audio data to an utterance
     */
    bool addAudioData(uint32_t utterance_id, const std::vector<float>& audio_data);
    
    /**
     * Set transcription result for an utterance
     */
    bool setTranscription(uint32_t utterance_id, const std::string& transcript, float confidence);
    
    /**
     * Set translation result for an utterance
     */
    bool setTranslation(uint32_t utterance_id, const std::string& translation);
    
    /**
     * Set synthesized audio for an utterance
     */
    bool setSynthesizedAudio(uint32_t utterance_id, const std::vector<uint8_t>& audio_data);
    
    /**
     * Set error state for an utterance
     */
    bool setUtteranceError(uint32_t utterance_id, const std::string& error_message);
    
    /**
     * Set language configuration for an utterance
     */
    bool setLanguageConfig(uint32_t utterance_id, const std::string& source_lang, 
                          const std::string& target_lang, const std::string& voice_id);
    
    /**
     * Get all utterances for a session
     */
    std::vector<std::shared_ptr<UtteranceData>> getSessionUtterances(const std::string& session_id) const;
    
    /**
     * Get all active (non-complete, non-error) utterances
     */
    std::vector<std::shared_ptr<UtteranceData>> getActiveUtterances() const;
    
    /**
     * Remove completed or errored utterances older than the specified age
     */
    size_t cleanupOldUtterances(std::chrono::seconds max_age);
    
    /**
     * Remove all utterances for a session
     */
    size_t removeSessionUtterances(const std::string& session_id);
    
    /**
     * Get statistics about utterance processing
     */
    struct Statistics {
        size_t total_utterances;
        size_t active_utterances;
        size_t completed_utterances;
        size_t error_utterances;
        std::chrono::milliseconds average_processing_time;
        size_t concurrent_utterances;
    };
    Statistics getStatistics() const;
    
    /**
     * Register callbacks for utterance events
     */
    void setStateChangeCallback(UtteranceStateCallback callback);
    void setCompleteCallback(UtteranceCompleteCallback callback);
    void setErrorCallback(UtteranceErrorCallback callback);
    
    /**
     * Process an utterance through the complete pipeline
     * This schedules the utterance for STT -> MT -> TTS processing
     */
    bool processUtterance(uint32_t utterance_id);
    
    /**
     * Process transcription result through translation pipeline
     */
    void processTranscriptionResult(
        uint32_t utterance_id,
        const stt::TranscriptionResult& result,
        const std::vector<stt::TranscriptionResult>& candidates = {}
    );
    
    /**
     * Check if the manager can accept new utterances
     */
    bool canAcceptNewUtterance() const;
    
    /**
     * Get the current configuration
     */
    const UtteranceManagerConfig& getConfig() const { return config_; }

private:
    void startCleanupTimer();
    void stopCleanupTimer();
    void performCleanup();
    void notifyStateChange(const UtteranceData& utterance);
    void notifyComplete(const UtteranceData& utterance);
    void notifyError(const UtteranceData& utterance, const std::string& error);
    
    // Internal processing methods
    void processSTT(uint32_t utterance_id);
    void processMT(uint32_t utterance_id);
    void processTTS(uint32_t utterance_id);
    
    UtteranceManagerConfig config_;
    std::shared_ptr<TaskQueue> task_queue_;
    std::shared_ptr<TranslationPipeline> translation_pipeline_;
    std::shared_ptr<stt::STTInterface> stt_engine_;
    std::shared_ptr<mt::TranslationInterface> mt_engine_;
    std::shared_ptr<tts::TTSInterface> tts_engine_;
    
    mutable std::mutex utterances_mutex_;
    std::unordered_map<uint32_t, std::shared_ptr<UtteranceData>> utterances_;
    std::atomic<uint32_t> next_utterance_id_;
    
    // Callbacks
    std::mutex callbacks_mutex_;
    UtteranceStateCallback state_change_callback_;
    UtteranceCompleteCallback complete_callback_;
    UtteranceErrorCallback error_callback_;
    
    // Cleanup timer
    std::atomic<bool> running_;
    std::thread cleanup_thread_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    std::atomic<size_t> total_created_;
    std::atomic<size_t> total_completed_;
    std::atomic<size_t> total_errors_;
};

} // namespace core
} // namespace speechrnt