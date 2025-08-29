#pragma once

#include "stt/stt_interface.hpp"
#include <memory>
#include <string>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace stt {

struct TranscriptionRequest {
    uint32_t utterance_id;
    std::vector<float> audio_data;
    bool is_live;
    std::function<void(uint32_t, const TranscriptionResult&)> callback;
};

class TranscriptionManager {
public:
    TranscriptionManager();
    ~TranscriptionManager();
    
    // Initialize with STT engine and model configuration
    bool initialize(const std::string& model_path, const std::string& engine = "whisper");
    
    // Submit transcription request
    void submitTranscription(const TranscriptionRequest& request);
    
    // Configuration
    void setLanguage(const std::string& language);
    void setTranslateToEnglish(bool translate);
    void setTemperature(float temperature);
    void setMaxTokens(int max_tokens);
    
    // Control
    void start();
    void stop();
    bool isRunning() const { return running_; }
    
    // Status
    bool isInitialized() const;
    std::string getLastError() const;
    size_t getQueueSize() const;
    
    // Access underlying engine (raw pointer; owned by manager)
    STTInterface* getSTTEngine() const { return stt_engine_.get(); }
    
private:
    std::unique_ptr<STTInterface> stt_engine_;
    
    // Threading
    std::thread worker_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> should_stop_;
    
    // Request queue
    std::queue<TranscriptionRequest> request_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    
    // Configuration
    std::string current_language_;
    bool translate_to_english_;
    float temperature_;
    int max_tokens_;
    
    // Status
    mutable std::mutex status_mutex_;
    std::string last_error_;
    
    // Worker thread function
    void workerLoop();
    void processRequest(const TranscriptionRequest& request);
    
    // STT engine factory methods
    std::unique_ptr<STTInterface> createWhisperSTT();
};

} // namespace stt