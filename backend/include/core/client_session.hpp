#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <queue>
#include <vector>
#include <functional>
#include "audio/audio_processor.hpp"
#include "audio/voice_activity_detector.hpp"
#include "stt/streaming_transcriber.hpp"
#include "stt/transcription_manager.hpp"

namespace core {

// Forward declarations
class WebSocketServer;
class ConfigMessage;
class EndSessionMessage;
class PingMessage;

class ClientSession {
public:
    explicit ClientSession(const std::string& sessionId);
    ~ClientSession();
    
    // Set the WebSocket server reference for sending messages
    void setWebSocketServer(WebSocketServer* server) { server_ = server; }
    
    const std::string& getSessionId() const { return sessionId_; }
    bool isConnected() const { return connected_; }
    
    // Message handling
    void handleMessage(const std::string& message);
    void handleBinaryMessage(std::string_view data);
    
    // Message sending
    void sendMessage(const std::string& message);
    void sendBinaryMessage(const std::vector<uint8_t>& data);
    
    // Session configuration
    void setLanguageConfig(const std::string& sourceLang, const std::string& targetLang);
    void setVoiceConfig(const std::string& voiceId);
    
    // Audio ingestion management
    bool ingestAudioData(std::string_view data);
    std::shared_ptr<audio::AudioBuffer> getAudioBuffer();
    void clearAudioBuffer();
    
    // Audio configuration
    void setAudioFormat(const audio::AudioFormat& format);
    const audio::AudioFormat& getAudioFormat() const;
    
    // Audio statistics
    audio::AudioIngestionManager::Statistics getAudioStatistics() const;
    
    // VAD management
    bool initializeVAD();
    void shutdownVAD();
    bool isVADActive() const;
    audio::VadState getCurrentVADState() const;
    uint32_t getCurrentUtteranceId() const;
    
    // VAD configuration
    void setVADConfig(const audio::VadConfig& config);
    const audio::VadConfig& getVADConfig() const;
    
    // Pipeline triggering
    using PipelineCallback = std::function<void(uint32_t utteranceId, const std::vector<float>& audioData, 
                                               const std::string& sourceLang, const std::string& targetLang, 
                                               const std::string& voiceId)>;
    void setPipelineCallback(PipelineCallback callback);
    
private:
    std::string sessionId_;
    bool connected_;
    WebSocketServer* server_;
    
    // Session configuration
    std::string sourceLang_;
    std::string targetLang_;
    std::string voiceId_;
    
    // Audio processing
    std::unique_ptr<audio::AudioIngestionManager> audioIngestion_;
    
    // Voice Activity Detection
    std::unique_ptr<audio::VoiceActivityDetector> vad_;
    audio::VadConfig vadConfig_;
    bool vadInitialized_;
    
    // Pipeline integration
    PipelineCallback pipelineCallback_;
    
    // Streaming transcription
    std::shared_ptr<::stt::TranscriptionManager> transcriptionManager_;
    std::unique_ptr<::stt::StreamingTranscriber> streamingTranscriber_;
    
    // VAD event handlers
    void handleVADEvent(const audio::VadEvent& event);
    void handleUtteranceComplete(uint32_t utteranceId, const std::vector<float>& audioData);
    
    // Transcription management
    bool initializeTranscription();
    void shutdownTranscription();
    void startStreamingTranscription(uint32_t utteranceId, const std::vector<float>& audioData);
    
    // Message processing
    void processConfigMessage(const core::ConfigMessage* message);
    void processControlMessage(const core::EndSessionMessage* message);
    void processPingMessage(const core::PingMessage* message);
    void processAudioData(std::string_view data);
    
    // Language change handling
    void handleLanguageChange(const std::string& oldLang, const std::string& newLang, float confidence);
};

} // namespace core