#include "core/client_session.hpp"
#include "core/websocket_server.hpp"
#include "core/message_protocol.hpp"
#include "utils/logging.hpp"
#include "stt/whisper_stt.hpp"
#include "stt/transcription_manager.hpp"
#include "stt/streaming_transcriber.hpp"
#include <algorithm>
#include <cstring>

namespace core {

ClientSession::ClientSession(const std::string& sessionId) 
    : sessionId_(sessionId), connected_(true), server_(nullptr), sourceLang_("en"), targetLang_("es"),
      vadInitialized_(false) {
    
    // Initialize audio ingestion manager
    audioIngestion_ = std::make_unique<audio::AudioIngestionManager>(sessionId);
    audioIngestion_->setActive(true);
    
    // Initialize VAD with default configuration
    vadConfig_ = audio::VadConfig{};
    vadConfig_.speechThreshold = 0.5f;
    vadConfig_.silenceThreshold = 0.3f;
    vadConfig_.minSpeechDurationMs = 100;
    vadConfig_.minSilenceDurationMs = 500;
    vadConfig_.sampleRate = 16000;
    
    vad_ = std::make_unique<audio::VoiceActivityDetector>(vadConfig_);
    
    // Set up VAD callbacks
    vad_->setVadCallback([this](const audio::VadEvent& event) {
        handleVADEvent(event);
    });
    
    vad_->setUtteranceCallback([this](uint32_t utteranceId, const std::vector<float>& audioData) {
        handleUtteranceComplete(utteranceId, audioData);
    });
    
    speechrnt::utils::Logger::info("Created session: " + sessionId);
}

ClientSession::~ClientSession() {
    shutdownTranscription();
    shutdownVAD();
    speechrnt::utils::Logger::info("Destroyed session: " + sessionId_);
    connected_ = false;
}

void ClientSession::handleMessage(const std::string& message) {
    speechrnt::utils::Logger::debug("Session " + sessionId_ + " received JSON: " + message);
    
    if (!connected_) {
        speechrnt::utils::Logger::warn("Received message for disconnected session: " + sessionId_);
        return;
    }
    
    // Validate and parse message using protocol
    if (!MessageProtocol::validateMessage(message)) {
        speechrnt::utils::Logger::warn("Invalid message format from session " + sessionId_);
        return;
    }
    
    auto parsedMessage = MessageProtocol::parseMessage(message);
    if (!parsedMessage) {
        speechrnt::utils::Logger::warn("Failed to parse message from session " + sessionId_);
        return;
    }
    
    // Handle message based on type
    switch (parsedMessage->getType()) {
        case MessageType::CONFIG:
            processConfigMessage(static_cast<ConfigMessage*>(parsedMessage.get()));
            break;
        case MessageType::END_SESSION:
            processControlMessage(static_cast<EndSessionMessage*>(parsedMessage.get()));
            break;
        case MessageType::PING:
            processPingMessage(static_cast<PingMessage*>(parsedMessage.get()));
            break;
        default:
            speechrnt::utils::Logger::warn("Unexpected message type from client session " + sessionId_);
            break;
    }
}

void ClientSession::handleBinaryMessage(std::string_view data) {
    speechrnt::utils::Logger::debug("Session " + sessionId_ + " received binary data: " + 
                        std::to_string(data.size()) + " bytes");
    
    if (!connected_) {
        speechrnt::utils::Logger::warn("Received binary data for disconnected session: " + sessionId_);
        return;
    }
    
    processAudioData(data);
}

void ClientSession::sendMessage(const std::string& message) {
    if (connected_ && server_) {
        speechrnt::utils::Logger::debug("Session " + sessionId_ + " sending JSON: " + message);
        server_->sendMessage(sessionId_, message);
    } else {
        speechrnt::utils::Logger::warn("Attempted to send message to disconnected session or no server: " + sessionId_);
    }
}

void ClientSession::sendBinaryMessage(const std::vector<uint8_t>& data) {
    if (connected_ && server_) {
        speechrnt::utils::Logger::debug("Session " + sessionId_ + " sending binary: " + 
                            std::to_string(data.size()) + " bytes");
        server_->sendBinaryMessage(sessionId_, data);
    } else {
        speechrnt::utils::Logger::warn("Attempted to send binary data to disconnected session or no server: " + sessionId_);
    }
}

void ClientSession::setLanguageConfig(const std::string& sourceLang, const std::string& targetLang) {
    sourceLang_ = sourceLang;
    targetLang_ = targetLang;
    speechrnt::utils::Logger::info("Session " + sessionId_ + " language config: " + 
                       sourceLang + " -> " + targetLang);
}

void ClientSession::setVoiceConfig(const std::string& voiceId) {
    voiceId_ = voiceId;
    speechrnt::utils::Logger::info("Session " + sessionId_ + " voice config: " + voiceId);
}

bool ClientSession::ingestAudioData(std::string_view data) {
    if (!audioIngestion_) {
        speechrnt::utils::Logger::error("Audio ingestion manager not initialized for session " + sessionId_);
        return false;
    }
    
    return audioIngestion_->ingestAudioData(data);
}

std::shared_ptr<audio::AudioBuffer> ClientSession::getAudioBuffer() {
    if (!audioIngestion_) {
        return nullptr;
    }
    
    return audioIngestion_->getAudioBuffer();
}

void ClientSession::clearAudioBuffer() {
    if (audioIngestion_) {
        audioIngestion_->getAudioBuffer()->clear();
        speechrnt::utils::Logger::debug("Session " + sessionId_ + " audio buffer cleared");
    }
}

void ClientSession::setAudioFormat(const audio::AudioFormat& format) {
    if (audioIngestion_) {
        audioIngestion_->setAudioFormat(format);
        speechrnt::utils::Logger::info("Session " + sessionId_ + " audio format updated: " +
                           std::to_string(format.sampleRate) + "Hz, " +
                           std::to_string(format.channels) + " channels, " +
                           std::to_string(format.bitsPerSample) + "-bit");
    }
}

const audio::AudioFormat& ClientSession::getAudioFormat() const {
    static audio::AudioFormat defaultFormat;
    if (!audioIngestion_) {
        return defaultFormat;
    }
    
    return audioIngestion_->getAudioFormat();
}

audio::AudioIngestionManager::Statistics ClientSession::getAudioStatistics() const {
    if (!audioIngestion_) {
        return {};
    }
    
    return audioIngestion_->getStatistics();
}

void ClientSession::processConfigMessage(const ConfigMessage* message) {
    speechrnt::utils::Logger::info("Processing config message for session " + sessionId_);
    
    setLanguageConfig(message->getSourceLang(), message->getTargetLang());
    setVoiceConfig(message->getVoice());
    
    // Configure language detection if transcription is already initialized
    if (transcriptionManager_) {
        if (auto whisperSTT = dynamic_cast<::stt::WhisperSTT*>(transcriptionManager_->getSTTEngine())) {
            whisperSTT->setLanguageDetectionEnabled(message->isLanguageDetectionEnabled());
            whisperSTT->setAutoLanguageSwitching(message->isAutoLanguageSwitching());
            whisperSTT->setLanguageDetectionThreshold(message->getLanguageDetectionThreshold());
            
            speechrnt::utils::Logger::info("Updated language detection settings for session " + sessionId_ + 
                               ": detection=" + (message->isLanguageDetectionEnabled() ? "enabled" : "disabled") +
                               ", auto-switch=" + (message->isAutoLanguageSwitching() ? "enabled" : "disabled") +
                               ", threshold=" + std::to_string(message->getLanguageDetectionThreshold()));
        }
    }
}

void ClientSession::processControlMessage(const EndSessionMessage* message) {
    speechrnt::utils::Logger::info("Processing end session message for session " + sessionId_);
    
    speechrnt::utils::Logger::info("Session " + sessionId_ + " requested to end");
    connected_ = false;
    clearAudioBuffer();
}

void ClientSession::processPingMessage(const PingMessage* message) {
    speechrnt::utils::Logger::debug("Processing ping message for session " + sessionId_);
    
    // Send pong response
    PongMessage pong;
    sendMessage(pong.serialize());
}

bool ClientSession::initializeVAD() {
    if (vadInitialized_) {
        return true;
    }
    
    if (!vad_) {
        speechrnt::utils::Logger::error("VAD not created for session " + sessionId_);
        return false;
    }
    
    if (!vad_->initialize()) {
        speechrnt::utils::Logger::error("Failed to initialize VAD for session " + sessionId_ + ": " + 
                           vad_->getErrorMessage());
        return false;
    }
    
    vadInitialized_ = true;
    speechrnt::utils::Logger::info("VAD initialized for session " + sessionId_);
    return true;
}

void ClientSession::shutdownVAD() {
    if (vad_ && vadInitialized_) {
        vad_->shutdown();
        vadInitialized_ = false;
        speechrnt::utils::Logger::info("VAD shutdown for session " + sessionId_);
    }
}

bool ClientSession::isVADActive() const {
    return vadInitialized_ && vad_ && vad_->isInitialized();
}

audio::VadState ClientSession::getCurrentVADState() const {
    if (!isVADActive()) {
        return audio::VadState::IDLE;
    }
    return vad_->getCurrentState();
}

uint32_t ClientSession::getCurrentUtteranceId() const {
    if (!isVADActive()) {
        return 0;
    }
    return vad_->getCurrentUtteranceId();
}

void ClientSession::setVADConfig(const audio::VadConfig& config) {
    vadConfig_ = config;
    if (vad_) {
        vad_->setConfig(config);
        speechrnt::utils::Logger::info("VAD configuration updated for session " + sessionId_);
    }
}

const audio::VadConfig& ClientSession::getVADConfig() const {
    return vadConfig_;
}

void ClientSession::setPipelineCallback(PipelineCallback callback) {
    pipelineCallback_ = callback;
}

void ClientSession::handleVADEvent(const audio::VadEvent& event) {
    speechrnt::utils::Logger::debug("Session " + sessionId_ + " VAD event: " + 
                        std::to_string(static_cast<int>(event.previousState)) + " -> " +
                        std::to_string(static_cast<int>(event.currentState)) + 
                        " (confidence: " + std::to_string(event.confidence) + ")");
    
    // Start streaming transcription at speech onset
    if (event.currentState == audio::VadState::SPEECH_DETECTED) {
        if (!streamingTranscriber_) {
            if (!initializeTranscription()) {
                speechrnt::utils::Logger::error("Failed to initialize transcription at speech onset for session " + sessionId_);
            }
        }
        if (streamingTranscriber_) {
            // Start streaming with no initial audio; audio chunks are fed in processAudioData
            streamingTranscriber_->startTranscription(event.utteranceId, {}, true);
        }
    }

    // Send status update to client based on VAD state
    StatusUpdateMessage::State clientState;
    switch (event.currentState) {
        case audio::VadState::IDLE:
            clientState = StatusUpdateMessage::State::IDLE;
            break;
        case audio::VadState::SPEECH_DETECTED:
        case audio::VadState::SPEAKING:
            clientState = StatusUpdateMessage::State::LISTENING;
            break;
        case audio::VadState::PAUSE_DETECTED:
            clientState = StatusUpdateMessage::State::THINKING;
            break;
        default:
            clientState = StatusUpdateMessage::State::IDLE;
            break;
    }
    
    StatusUpdateMessage statusMsg(clientState);
    sendMessage(statusMsg.serialize());
}

void ClientSession::handleUtteranceComplete(uint32_t utteranceId, const std::vector<float>& audioData) {
    speechrnt::utils::Logger::info("Session " + sessionId_ + " utterance " + std::to_string(utteranceId) + 
                       " completed with " + std::to_string(audioData.size()) + " samples");
    
    // Feed final audio chunk and finalize streaming transcription
    if (streamingTranscriber_) {
        if (!audioData.empty()) {
            streamingTranscriber_->addAudioData(utteranceId, audioData);
        }
        streamingTranscriber_->finalizeTranscription(utteranceId);
    } else {
        // If streaming was not initialized, start and immediately finalize as fallback
        startStreamingTranscription(utteranceId, audioData);
    }
    
    // Trigger the translation pipeline if callback is set
    if (pipelineCallback_) {
        pipelineCallback_(utteranceId, audioData, sourceLang_, targetLang_, voiceId_);
    } else {
        speechrnt::utils::Logger::warn("No pipeline callback set for session " + sessionId_);
    }
}

void ClientSession::processAudioData(std::string_view data) {
    if (!ingestAudioData(data)) {
        speechrnt::utils::Logger::warn("Session " + sessionId_ + " failed to ingest audio data: " +
                           audioIngestion_->getErrorMessage());
        
        // Send error message to client
        ErrorMessage errorMsg("Audio ingestion failed: " + audioIngestion_->getErrorMessage(), 
                             "AUDIO_INGESTION_ERROR");
        sendMessage(errorMsg.serialize());
        return;
    }
    
    // Initialize VAD if not already done
    if (!vadInitialized_ && !initializeVAD()) {
        speechrnt::utils::Logger::error("Failed to initialize VAD for session " + sessionId_);
        ErrorMessage errorMsg("VAD initialization failed", "VAD_INIT_ERROR");
        sendMessage(errorMsg.serialize());
        return;
    }
    
    // Convert PCM data to float for VAD processing
    if (audioIngestion_) {
        auto processor = audioIngestion_->getAudioBuffer();
        if (processor) {
            // Get recent audio samples for VAD processing
            auto recentSamples = audioIngestion_->getLatestAudio(data.size() / 2); // 16-bit samples
            
            if (!recentSamples.empty() && vad_) {
                vad_->processAudio(recentSamples);

                // While speaking, feed audio chunks to the streaming transcriber
                if (isVADActive() && vad_->getCurrentState() == audio::VadState::SPEAKING && streamingTranscriber_) {
                    uint32_t currentUtteranceId = vad_->getCurrentUtteranceId();
                    if (currentUtteranceId != 0) {
                        streamingTranscriber_->addAudioData(currentUtteranceId, recentSamples);
                    }
                }
            }
        }
    }
    
    // Log successful ingestion
    auto stats = getAudioStatistics();
    speechrnt::utils::Logger::debug("Session " + sessionId_ + " ingested " + 
                        std::to_string(data.size()) + " bytes. " +
                        "Total: " + std::to_string(stats.totalBytesIngested) + " bytes, " +
                        std::to_string(stats.totalChunksIngested) + " chunks");
}

bool ClientSession::initializeTranscription() {
    if (transcriptionManager_ && streamingTranscriber_) {
        return true; // Already initialized
    }
    
    // Create transcription manager
    transcriptionManager_ = std::make_shared<::stt::TranscriptionManager>();
    
    // Initialize with default model (this should be configurable)
    std::string modelPath = "data/whisper/ggml-base.bin"; // Default model
    if (!transcriptionManager_->initialize(modelPath, "whisper")) {
        speechrnt::utils::Logger::error("Failed to initialize transcription manager for session " + sessionId_ + 
                           ": " + transcriptionManager_->getLastError());
        return false;
    }
    
    // Start the transcription manager
    transcriptionManager_->start();
    
    // Create streaming transcriber
    streamingTranscriber_ = std::make_unique<::stt::StreamingTranscriber>();
    
    // Create message sender callback
    auto messageSender = [this](const std::string& message) {
        sendMessage(message);
    };
    
    // Initialize streaming transcriber
    if (!streamingTranscriber_->initialize(transcriptionManager_, messageSender)) {
        speechrnt::utils::Logger::error("Failed to initialize streaming transcriber for session " + sessionId_);
        return false;
    }
    
    // Apply normalization config from STTConfigManager if available
    if (auto engine = transcriptionManager_->getSTTEngine()) {
        // There is no direct link to STTConfigManager here; optionally could be injected elsewhere.
        // For now, read defaults from file via a lightweight manager instance.
        stt::STTConfigManager cfgMgr;
        if (cfgMgr.loadFromFile("backend/config/stt.json")) {
            streamingTranscriber_->setNormalizationConfig(cfgMgr.getConfig().normalization);
        }
    }
    
    // Configure transcription manager with session language
    transcriptionManager_->setLanguage(sourceLang_);
    
    // Set up language change callback for the transcription manager
    if (auto whisperSTT = dynamic_cast<::stt::WhisperSTT*>(transcriptionManager_->getSTTEngine())) {
        whisperSTT->setLanguageChangeCallback([this](const std::string& oldLang, const std::string& newLang, float confidence) {
            handleLanguageChange(oldLang, newLang, confidence);
        });
    }
    
    speechrnt::utils::Logger::info("Transcription initialized for session " + sessionId_);
    return true;
}

void ClientSession::shutdownTranscription() {
    if (streamingTranscriber_) {
        streamingTranscriber_.reset();
        speechrnt::utils::Logger::info("Streaming transcriber shutdown for session " + sessionId_);
    }
    
    if (transcriptionManager_) {
        transcriptionManager_->stop();
        transcriptionManager_.reset();
        speechrnt::utils::Logger::info("Transcription manager shutdown for session " + sessionId_);
    }
}

void ClientSession::startStreamingTranscription(uint32_t utteranceId, const std::vector<float>& audioData) {
    if (!streamingTranscriber_) {
        if (!initializeTranscription()) {
            speechrnt::utils::Logger::error("Failed to initialize transcription for session " + sessionId_);
            
            // Send error message to client
            ErrorMessage errorMsg("Transcription initialization failed", "TRANSCRIPTION_INIT_ERROR", utteranceId);
            sendMessage(errorMsg.serialize());
            return;
        }
    }
    
    // Start streaming transcription
    streamingTranscriber_->startTranscription(utteranceId, audioData, true);
    
    speechrnt::utils::Logger::debug("Started streaming transcription for utterance " + std::to_string(utteranceId) + 
                        " in session " + sessionId_);
}

void ClientSession::handleLanguageChange(const std::string& oldLang, const std::string& newLang, float confidence) {
    speechrnt::utils::Logger::info("Session " + sessionId_ + " detected language change: " + 
                       oldLang + " -> " + newLang + " (confidence: " + std::to_string(confidence) + ")");
    
    // Update session source language if auto-switching is enabled
    sourceLang_ = newLang;
    
    // Send language change notification to client
    LanguageChangeMessage langChangeMsg(oldLang, newLang, confidence);
    sendMessage(langChangeMsg.serialize());
    
    // Update transcription manager language
    if (transcriptionManager_) {
        transcriptionManager_->setLanguage(newLang);
    }
}

} // namespace core