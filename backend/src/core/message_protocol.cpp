#include "core/message_protocol.hpp"
#include "utils/logging.hpp"
#include <stdexcept>

namespace core {

// ConfigMessage implementation
std::string ConfigMessage::serialize() const {
    utils::JsonValue root;
    root.setObject();
    root.setObjectProperty("type", utils::JsonValue(std::string("config")));
    
    utils::JsonValue data;
    data.setObject();
    data.setObjectProperty("sourceLang", utils::JsonValue(sourceLang_));
    data.setObjectProperty("targetLang", utils::JsonValue(targetLang_));
    data.setObjectProperty("voice", utils::JsonValue(voice_));
    data.setObjectProperty("languageDetectionEnabled", utils::JsonValue(languageDetectionEnabled_));
    data.setObjectProperty("autoLanguageSwitching", utils::JsonValue(autoLanguageSwitching_));
    data.setObjectProperty("languageDetectionThreshold", utils::JsonValue(static_cast<double>(languageDetectionThreshold_)));
    
    root.setObjectProperty("data", data);
    
    return utils::JsonParser::stringify(root);
}

// EndSessionMessage implementation
std::string EndSessionMessage::serialize() const {
    utils::JsonValue root;
    root.setObject();
    root.setObjectProperty("type", utils::JsonValue(std::string("end_session")));
    
    return utils::JsonParser::stringify(root);
}

// PingMessage implementation
std::string PingMessage::serialize() const {
    utils::JsonValue root;
    root.setObject();
    root.setObjectProperty("type", utils::JsonValue(std::string("ping")));
    
    return utils::JsonParser::stringify(root);
}

// TranscriptionUpdateMessage implementation
std::string TranscriptionUpdateMessage::serialize() const {
    utils::JsonValue root;
    root.setObject();
    root.setObjectProperty("type", utils::JsonValue(std::string("transcription_update")));
    
    utils::JsonValue data;
    data.setObject();
    data.setObjectProperty("text", utils::JsonValue(text_));
    data.setObjectProperty("utteranceId", utils::JsonValue(static_cast<double>(utteranceId_)));
    data.setObjectProperty("confidence", utils::JsonValue(confidence_));
    data.setObjectProperty("isPartial", utils::JsonValue(isPartial_));
    data.setObjectProperty("startTimeMs", utils::JsonValue(static_cast<double>(startTimeMs_)));
    data.setObjectProperty("endTimeMs", utils::JsonValue(static_cast<double>(endTimeMs_)));
    data.setObjectProperty("detectedLanguage", utils::JsonValue(detectedLanguage_));
    data.setObjectProperty("languageConfidence", utils::JsonValue(static_cast<double>(languageConfidence_)));
    data.setObjectProperty("languageChanged", utils::JsonValue(languageChanged_));
    
    root.setObjectProperty("data", data);
    
    return utils::JsonParser::stringify(root);
}

// TranslationResultMessage implementation
std::string TranslationResultMessage::serialize() const {
    utils::JsonValue root;
    root.setObject();
    root.setObjectProperty("type", utils::JsonValue(std::string("translation_result")));
    
    utils::JsonValue data;
    data.setObject();
    data.setObjectProperty("originalText", utils::JsonValue(originalText_));
    data.setObjectProperty("translatedText", utils::JsonValue(translatedText_));
    data.setObjectProperty("utteranceId", utils::JsonValue(static_cast<double>(utteranceId_)));
    
    root.setObjectProperty("data", data);
    
    return utils::JsonParser::stringify(root);
}

// AudioStartMessage implementation
std::string AudioStartMessage::serialize() const {
    utils::JsonValue root;
    root.setObject();
    root.setObjectProperty("type", utils::JsonValue(std::string("audio_start")));
    
    utils::JsonValue data;
    data.setObject();
    data.setObjectProperty("utteranceId", utils::JsonValue(static_cast<double>(utteranceId_)));
    data.setObjectProperty("duration", utils::JsonValue(duration_));
    
    root.setObjectProperty("data", data);
    
    return utils::JsonParser::stringify(root);
}

// StatusUpdateMessage implementation
std::string StatusUpdateMessage::serialize() const {
    utils::JsonValue root;
    root.setObject();
    root.setObjectProperty("type", utils::JsonValue(std::string("status_update")));
    
    utils::JsonValue data;
    data.setObject();
    data.setObjectProperty("state", utils::JsonValue(MessageProtocol::stateToString(state_)));
    if (utteranceId_ > 0) {
        data.setObjectProperty("utteranceId", utils::JsonValue(static_cast<double>(utteranceId_)));
    }
    
    root.setObjectProperty("data", data);
    
    return utils::JsonParser::stringify(root);
}

// ErrorMessage implementation
std::string ErrorMessage::serialize() const {
    utils::JsonValue root;
    root.setObject();
    root.setObjectProperty("type", utils::JsonValue(std::string("error")));
    
    utils::JsonValue data;
    data.setObject();
    data.setObjectProperty("message", utils::JsonValue(message_));
    if (!code_.empty()) {
        data.setObjectProperty("code", utils::JsonValue(code_));
    }
    if (utteranceId_ > 0) {
        data.setObjectProperty("utteranceId", utils::JsonValue(static_cast<double>(utteranceId_)));
    }
    
    root.setObjectProperty("data", data);
    
    return utils::JsonParser::stringify(root);
}

// PongMessage implementation
std::string PongMessage::serialize() const {
    utils::JsonValue root;
    root.setObject();
    root.setObjectProperty("type", utils::JsonValue(std::string("pong")));
    
    return utils::JsonParser::stringify(root);
}

// LanguageChangeMessage implementation
std::string LanguageChangeMessage::serialize() const {
    utils::JsonValue root;
    root.setObject();
    root.setObjectProperty("type", utils::JsonValue(std::string("language_change")));
    
    utils::JsonValue data;
    data.setObject();
    data.setObjectProperty("oldLanguage", utils::JsonValue(oldLanguage_));
    data.setObjectProperty("newLanguage", utils::JsonValue(newLanguage_));
    data.setObjectProperty("confidence", utils::JsonValue(static_cast<double>(confidence_)));
    if (utteranceId_ > 0) {
        data.setObjectProperty("utteranceId", utils::JsonValue(static_cast<double>(utteranceId_)));
    }
    
    root.setObjectProperty("data", data);
    
    return utils::JsonParser::stringify(root);
}

// MessageProtocol implementation
std::unique_ptr<Message> MessageProtocol::parseMessage(const std::string& json) {
    try {
        utils::JsonValue root = utils::JsonParser::parse(json);
        
        if (root.getType() != utils::JsonType::OBJECT || !root.hasProperty("type")) {
            speechrnt::utils::Logger::warn("Invalid message format: missing type field");
            return nullptr;
        }
        
        std::string typeStr = root.getProperty("type").asString();
        MessageType type = stringToMessageType(typeStr);
        
        switch (type) {
            case MessageType::CONFIG: {
                auto message = std::make_unique<ConfigMessage>();
                if (root.hasProperty("data")) {
                    const auto& data = root.getProperty("data");
                    if (data.hasProperty("sourceLang")) {
                        message->setSourceLang(data.getProperty("sourceLang").asString());
                    }
                    if (data.hasProperty("targetLang")) {
                        message->setTargetLang(data.getProperty("targetLang").asString());
                    }
                    if (data.hasProperty("voice")) {
                        message->setVoice(data.getProperty("voice").asString());
                    }
                    if (data.hasProperty("languageDetectionEnabled")) {
                        message->setLanguageDetectionEnabled(data.getProperty("languageDetectionEnabled").asBool());
                    }
                    if (data.hasProperty("autoLanguageSwitching")) {
                        message->setAutoLanguageSwitching(data.getProperty("autoLanguageSwitching").asBool());
                    }
                    if (data.hasProperty("languageDetectionThreshold")) {
                        message->setLanguageDetectionThreshold(static_cast<float>(data.getProperty("languageDetectionThreshold").asNumber()));
                    }
                }
                return std::move(message);
            }
            
            case MessageType::END_SESSION:
                return std::make_unique<EndSessionMessage>();
                
            case MessageType::PING:
                return std::make_unique<PingMessage>();
                
            case MessageType::TRANSCRIPTION_UPDATE: {
                auto message = std::make_unique<TranscriptionUpdateMessage>();
                if (root.hasProperty("data")) {
                    const auto& data = root.getProperty("data");
                    if (data.hasProperty("text")) {
                        message->setText(data.getProperty("text").asString());
                    }
                    if (data.hasProperty("utteranceId")) {
                        message->setUtteranceId(static_cast<uint32_t>(data.getProperty("utteranceId").asNumber()));
                    }
                    if (data.hasProperty("confidence")) {
                        message->setConfidence(data.getProperty("confidence").asNumber());
                    }
                    if (data.hasProperty("isPartial")) {
                        message->setPartial(data.getProperty("isPartial").asBool());
                    }
                    if (data.hasProperty("startTimeMs")) {
                        message->setStartTimeMs(static_cast<int64_t>(data.getProperty("startTimeMs").asNumber()));
                    }
                    if (data.hasProperty("endTimeMs")) {
                        message->setEndTimeMs(static_cast<int64_t>(data.getProperty("endTimeMs").asNumber()));
                    }
                    if (data.hasProperty("detectedLanguage")) {
                        message->setDetectedLanguage(data.getProperty("detectedLanguage").asString());
                    }
                    if (data.hasProperty("languageConfidence")) {
                        message->setLanguageConfidence(static_cast<float>(data.getProperty("languageConfidence").asNumber()));
                    }
                    if (data.hasProperty("languageChanged")) {
                        message->setLanguageChanged(data.getProperty("languageChanged").asBool());
                    }
                }
                return std::move(message);
            }
            
            case MessageType::TRANSLATION_RESULT: {
                auto message = std::make_unique<TranslationResultMessage>();
                if (root.hasProperty("data")) {
                    const auto& data = root.getProperty("data");
                    if (data.hasProperty("originalText")) {
                        message->setOriginalText(data.getProperty("originalText").asString());
                    }
                    if (data.hasProperty("translatedText")) {
                        message->setTranslatedText(data.getProperty("translatedText").asString());
                    }
                    if (data.hasProperty("utteranceId")) {
                        message->setUtteranceId(static_cast<uint32_t>(data.getProperty("utteranceId").asNumber()));
                    }
                }
                return std::move(message);
            }
            
            case MessageType::AUDIO_START: {
                auto message = std::make_unique<AudioStartMessage>();
                if (root.hasProperty("data")) {
                    const auto& data = root.getProperty("data");
                    if (data.hasProperty("utteranceId")) {
                        message->setUtteranceId(static_cast<uint32_t>(data.getProperty("utteranceId").asNumber()));
                    }
                    if (data.hasProperty("duration")) {
                        message->setDuration(data.getProperty("duration").asNumber());
                    }
                }
                return std::move(message);
            }
            
            case MessageType::STATUS_UPDATE: {
                auto message = std::make_unique<StatusUpdateMessage>();
                if (root.hasProperty("data")) {
                    const auto& data = root.getProperty("data");
                    if (data.hasProperty("state")) {
                        std::string stateStr = data.getProperty("state").asString();
                        if (stateStr == "idle") {
                            message->setState(StatusUpdateMessage::State::IDLE);
                        } else if (stateStr == "listening") {
                            message->setState(StatusUpdateMessage::State::LISTENING);
                        } else if (stateStr == "thinking") {
                            message->setState(StatusUpdateMessage::State::THINKING);
                        } else if (stateStr == "speaking") {
                            message->setState(StatusUpdateMessage::State::SPEAKING);
                        }
                    }
                    if (data.hasProperty("utteranceId")) {
                        message->setUtteranceId(static_cast<uint32_t>(data.getProperty("utteranceId").asNumber()));
                    }
                }
                return std::move(message);
            }
            
            case MessageType::ERROR: {
                auto message = std::make_unique<ErrorMessage>();
                if (root.hasProperty("data")) {
                    const auto& data = root.getProperty("data");
                    if (data.hasProperty("message")) {
                        message->setMessage(data.getProperty("message").asString());
                    }
                    if (data.hasProperty("code")) {
                        message->setCode(data.getProperty("code").asString());
                    }
                    if (data.hasProperty("utteranceId")) {
                        message->setUtteranceId(static_cast<uint32_t>(data.getProperty("utteranceId").asNumber()));
                    }
                }
                return std::move(message);
            }
            
            case MessageType::PONG:
                return std::make_unique<PongMessage>();
                
            case MessageType::LANGUAGE_CHANGE: {
                auto message = std::make_unique<LanguageChangeMessage>();
                if (root.hasProperty("data")) {
                    const auto& data = root.getProperty("data");
                    if (data.hasProperty("oldLanguage")) {
                        message->setOldLanguage(data.getProperty("oldLanguage").asString());
                    }
                    if (data.hasProperty("newLanguage")) {
                        message->setNewLanguage(data.getProperty("newLanguage").asString());
                    }
                    if (data.hasProperty("confidence")) {
                        message->setConfidence(static_cast<float>(data.getProperty("confidence").asNumber()));
                    }
                    if (data.hasProperty("utteranceId")) {
                        message->setUtteranceId(static_cast<uint32_t>(data.getProperty("utteranceId").asNumber()));
                    }
                }
                return std::move(message);
            }
                
            default:
                speechrnt::utils::Logger::warn("Unknown message type: " + typeStr);
                return nullptr;
        }
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to parse message: " + std::string(e.what()));
        return nullptr;
    }
}

MessageType MessageProtocol::getMessageType(const std::string& json) {
    try {
        utils::JsonValue root = utils::JsonParser::parse(json);
        
        if (root.getType() != utils::JsonType::OBJECT || !root.hasProperty("type")) {
            return MessageType::UNKNOWN;
        }
        
        std::string typeStr = root.getProperty("type").asString();
        return stringToMessageType(typeStr);
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to get message type: " + std::string(e.what()));
        return MessageType::UNKNOWN;
    }
}

bool MessageProtocol::validateMessage(const std::string& json) {
    try {
        utils::JsonValue root = utils::JsonParser::parse(json);
        
        // Must be an object with a type field
        if (root.getType() != utils::JsonType::OBJECT || !root.hasProperty("type")) {
            return false;
        }
        
        std::string typeStr = root.getProperty("type").asString();
        MessageType type = stringToMessageType(typeStr);
        
        // Must be a known message type
        if (type == MessageType::UNKNOWN) {
            return false;
        }
        
        // Additional validation based on message type
        switch (type) {
            case MessageType::CONFIG:
                if (root.hasProperty("data")) {
                    const auto& data = root.getProperty("data");
                    return data.hasProperty("sourceLang") && 
                           data.hasProperty("targetLang") && 
                           data.hasProperty("voice");
                }
                return false;
                
            case MessageType::TRANSCRIPTION_UPDATE:
                if (root.hasProperty("data")) {
                    const auto& data = root.getProperty("data");
                    return data.hasProperty("text") && 
                           data.hasProperty("utteranceId") && 
                           data.hasProperty("confidence");
                }
                return false;
                
            case MessageType::TRANSLATION_RESULT:
                if (root.hasProperty("data")) {
                    const auto& data = root.getProperty("data");
                    return data.hasProperty("originalText") && 
                           data.hasProperty("translatedText") && 
                           data.hasProperty("utteranceId");
                }
                return false;
                
            case MessageType::AUDIO_START:
                if (root.hasProperty("data")) {
                    const auto& data = root.getProperty("data");
                    return data.hasProperty("utteranceId") && 
                           data.hasProperty("duration");
                }
                return false;
                
            case MessageType::STATUS_UPDATE:
                if (root.hasProperty("data")) {
                    const auto& data = root.getProperty("data");
                    return data.hasProperty("state");
                }
                return false;
                
            case MessageType::ERROR:
                if (root.hasProperty("data")) {
                    const auto& data = root.getProperty("data");
                    return data.hasProperty("message");
                }
                return false;
                
            case MessageType::LANGUAGE_CHANGE:
                if (root.hasProperty("data")) {
                    const auto& data = root.getProperty("data");
                    return data.hasProperty("oldLanguage") && 
                           data.hasProperty("newLanguage") && 
                           data.hasProperty("confidence");
                }
                return false;
                
            default:
                return true; // Simple messages without data
        }
        
    } catch (const std::exception& e) {
        return false;
    }
}

MessageType MessageProtocol::stringToMessageType(const std::string& typeStr) {
    if (typeStr == "config") return MessageType::CONFIG;
    if (typeStr == "end_session") return MessageType::END_SESSION;
    if (typeStr == "ping") return MessageType::PING;
    if (typeStr == "transcription_update") return MessageType::TRANSCRIPTION_UPDATE;
    if (typeStr == "translation_result") return MessageType::TRANSLATION_RESULT;
    if (typeStr == "audio_start") return MessageType::AUDIO_START;
    if (typeStr == "status_update") return MessageType::STATUS_UPDATE;
    if (typeStr == "error") return MessageType::ERROR;
    if (typeStr == "pong") return MessageType::PONG;
    if (typeStr == "language_change") return MessageType::LANGUAGE_CHANGE;
    return MessageType::UNKNOWN;
}

std::string MessageProtocol::messageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::CONFIG: return "config";
        case MessageType::END_SESSION: return "end_session";
        case MessageType::PING: return "ping";
        case MessageType::TRANSCRIPTION_UPDATE: return "transcription_update";
        case MessageType::TRANSLATION_RESULT: return "translation_result";
        case MessageType::AUDIO_START: return "audio_start";
        case MessageType::STATUS_UPDATE: return "status_update";
        case MessageType::ERROR: return "error";
        case MessageType::PONG: return "pong";
        case MessageType::LANGUAGE_CHANGE: return "language_change";
        default: return "unknown";
    }
}

std::string MessageProtocol::stateToString(StatusUpdateMessage::State state) {
    switch (state) {
        case StatusUpdateMessage::State::IDLE: return "idle";
        case StatusUpdateMessage::State::LISTENING: return "listening";
        case StatusUpdateMessage::State::THINKING: return "thinking";
        case StatusUpdateMessage::State::SPEAKING: return "speaking";
        default: return "idle";
    }
}

} // namespace core