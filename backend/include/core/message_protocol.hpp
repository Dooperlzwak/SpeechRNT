#pragma once

#include <string>
#include <memory>
#include "utils/json_utils.hpp"

namespace core {

// Message types
enum class MessageType {
    UNKNOWN,
    // Client to Server
    CONFIG,
    END_SESSION,
    PING,
    // Server to Client
    TRANSCRIPTION_UPDATE,
    TRANSLATION_RESULT,
    AUDIO_START,
    STATUS_UPDATE,
    ERROR,
    PONG,
    LANGUAGE_CHANGE
};

// Base message class
class Message {
public:
    explicit Message(MessageType type) : type_(type) {}
    virtual ~Message() = default;
    
    MessageType getType() const { return type_; }
    virtual std::string serialize() const = 0;
    
protected:
    MessageType type_;
};

// Client to Server Messages
class ConfigMessage : public Message {
public:
    ConfigMessage() : Message(MessageType::CONFIG), languageDetectionEnabled_(false), autoLanguageSwitching_(false), languageDetectionThreshold_(0.7f) {}
    ConfigMessage(const std::string& sourceLang, const std::string& targetLang, const std::string& voice)
        : Message(MessageType::CONFIG), sourceLang_(sourceLang), targetLang_(targetLang), voice_(voice), 
          languageDetectionEnabled_(false), autoLanguageSwitching_(false), languageDetectionThreshold_(0.7f) {}
    
    const std::string& getSourceLang() const { return sourceLang_; }
    const std::string& getTargetLang() const { return targetLang_; }
    const std::string& getVoice() const { return voice_; }
    bool isLanguageDetectionEnabled() const { return languageDetectionEnabled_; }
    bool isAutoLanguageSwitching() const { return autoLanguageSwitching_; }
    float getLanguageDetectionThreshold() const { return languageDetectionThreshold_; }
    
    void setSourceLang(const std::string& lang) { sourceLang_ = lang; }
    void setTargetLang(const std::string& lang) { targetLang_ = lang; }
    void setVoice(const std::string& voice) { voice_ = voice; }
    void setLanguageDetectionEnabled(bool enabled) { languageDetectionEnabled_ = enabled; }
    void setAutoLanguageSwitching(bool enabled) { autoLanguageSwitching_ = enabled; }
    void setLanguageDetectionThreshold(float threshold) { languageDetectionThreshold_ = threshold; }
    
    std::string serialize() const override;
    
private:
    std::string sourceLang_;
    std::string targetLang_;
    std::string voice_;
    bool languageDetectionEnabled_;
    bool autoLanguageSwitching_;
    float languageDetectionThreshold_;
};

class EndSessionMessage : public Message {
public:
    EndSessionMessage() : Message(MessageType::END_SESSION) {}
    std::string serialize() const override;
};

class PingMessage : public Message {
public:
    PingMessage() : Message(MessageType::PING) {}
    std::string serialize() const override;
};

// Server to Client Messages
class TranscriptionUpdateMessage : public Message {
public:
    TranscriptionUpdateMessage() : Message(MessageType::TRANSCRIPTION_UPDATE), utteranceId_(0), confidence_(0.0), isPartial_(false), startTimeMs_(0), endTimeMs_(0), languageConfidence_(0.0f), languageChanged_(false) {}
    TranscriptionUpdateMessage(const std::string& text, uint32_t utteranceId, double confidence, bool isPartial = false, int64_t startTimeMs = 0, int64_t endTimeMs = 0)
        : Message(MessageType::TRANSCRIPTION_UPDATE), text_(text), utteranceId_(utteranceId), confidence_(confidence), isPartial_(isPartial), startTimeMs_(startTimeMs), endTimeMs_(endTimeMs), languageConfidence_(0.0f), languageChanged_(false) {}
    
    const std::string& getText() const { return text_; }
    uint32_t getUtteranceId() const { return utteranceId_; }
    double getConfidence() const { return confidence_; }
    bool isPartial() const { return isPartial_; }
    int64_t getStartTimeMs() const { return startTimeMs_; }
    int64_t getEndTimeMs() const { return endTimeMs_; }
    const std::string& getDetectedLanguage() const { return detectedLanguage_; }
    float getLanguageConfidence() const { return languageConfidence_; }
    bool isLanguageChanged() const { return languageChanged_; }
    
    void setText(const std::string& text) { text_ = text; }
    void setUtteranceId(uint32_t id) { utteranceId_ = id; }
    void setConfidence(double confidence) { confidence_ = confidence; }
    void setPartial(bool isPartial) { isPartial_ = isPartial; }
    void setStartTimeMs(int64_t startTimeMs) { startTimeMs_ = startTimeMs; }
    void setEndTimeMs(int64_t endTimeMs) { endTimeMs_ = endTimeMs; }
    void setDetectedLanguage(const std::string& language) { detectedLanguage_ = language; }
    void setLanguageConfidence(float confidence) { languageConfidence_ = confidence; }
    void setLanguageChanged(bool changed) { languageChanged_ = changed; }
    
    std::string serialize() const override;
    
private:
    std::string text_;
    uint32_t utteranceId_;
    double confidence_;
    bool isPartial_;
    int64_t startTimeMs_;
    int64_t endTimeMs_;
    std::string detectedLanguage_;
    float languageConfidence_;
    bool languageChanged_;
};

class TranslationResultMessage : public Message {
public:
    TranslationResultMessage() : Message(MessageType::TRANSLATION_RESULT), utteranceId_(0) {}
    TranslationResultMessage(const std::string& originalText, const std::string& translatedText, uint32_t utteranceId)
        : Message(MessageType::TRANSLATION_RESULT), originalText_(originalText), translatedText_(translatedText), utteranceId_(utteranceId) {}
    
    const std::string& getOriginalText() const { return originalText_; }
    const std::string& getTranslatedText() const { return translatedText_; }
    uint32_t getUtteranceId() const { return utteranceId_; }
    
    void setOriginalText(const std::string& text) { originalText_ = text; }
    void setTranslatedText(const std::string& text) { translatedText_ = text; }
    void setUtteranceId(uint32_t id) { utteranceId_ = id; }
    
    std::string serialize() const override;
    
private:
    std::string originalText_;
    std::string translatedText_;
    uint32_t utteranceId_;
};

class AudioStartMessage : public Message {
public:
    AudioStartMessage() : Message(MessageType::AUDIO_START), utteranceId_(0), duration_(0.0) {}
    AudioStartMessage(uint32_t utteranceId, double duration)
        : Message(MessageType::AUDIO_START), utteranceId_(utteranceId), duration_(duration) {}
    
    uint32_t getUtteranceId() const { return utteranceId_; }
    double getDuration() const { return duration_; }
    
    void setUtteranceId(uint32_t id) { utteranceId_ = id; }
    void setDuration(double duration) { duration_ = duration; }
    
    std::string serialize() const override;
    
private:
    uint32_t utteranceId_;
    double duration_;
};

class StatusUpdateMessage : public Message {
public:
    enum class State {
        IDLE,
        LISTENING,
        THINKING,
        SPEAKING
    };
    
    StatusUpdateMessage() : Message(MessageType::STATUS_UPDATE), state_(State::IDLE), utteranceId_(0) {}
    StatusUpdateMessage(State state, uint32_t utteranceId = 0)
        : Message(MessageType::STATUS_UPDATE), state_(state), utteranceId_(utteranceId) {}
    
    State getState() const { return state_; }
    uint32_t getUtteranceId() const { return utteranceId_; }
    
    void setState(State state) { state_ = state; }
    void setUtteranceId(uint32_t id) { utteranceId_ = id; }
    
    std::string serialize() const override;
    
private:
    State state_;
    uint32_t utteranceId_;
};

class ErrorMessage : public Message {
public:
    ErrorMessage() : Message(MessageType::ERROR), utteranceId_(0) {}
    ErrorMessage(const std::string& message, const std::string& code = "", uint32_t utteranceId = 0)
        : Message(MessageType::ERROR), message_(message), code_(code), utteranceId_(utteranceId) {}
    
    const std::string& getMessage() const { return message_; }
    const std::string& getCode() const { return code_; }
    uint32_t getUtteranceId() const { return utteranceId_; }
    
    void setMessage(const std::string& message) { message_ = message; }
    void setCode(const std::string& code) { code_ = code; }
    void setUtteranceId(uint32_t id) { utteranceId_ = id; }
    
    std::string serialize() const override;
    
private:
    std::string message_;
    std::string code_;
    uint32_t utteranceId_;
};

class PongMessage : public Message {
public:
    PongMessage() : Message(MessageType::PONG) {}
    std::string serialize() const override;
};

class LanguageChangeMessage : public Message {
public:
    LanguageChangeMessage() : Message(MessageType::LANGUAGE_CHANGE), confidence_(0.0f), utteranceId_(0) {}
    LanguageChangeMessage(const std::string& oldLang, const std::string& newLang, float confidence, uint32_t utteranceId = 0)
        : Message(MessageType::LANGUAGE_CHANGE), oldLanguage_(oldLang), newLanguage_(newLang), confidence_(confidence), utteranceId_(utteranceId) {}
    
    const std::string& getOldLanguage() const { return oldLanguage_; }
    const std::string& getNewLanguage() const { return newLanguage_; }
    float getConfidence() const { return confidence_; }
    uint32_t getUtteranceId() const { return utteranceId_; }
    
    void setOldLanguage(const std::string& lang) { oldLanguage_ = lang; }
    void setNewLanguage(const std::string& lang) { newLanguage_ = lang; }
    void setConfidence(float confidence) { confidence_ = confidence; }
    void setUtteranceId(uint32_t id) { utteranceId_ = id; }
    
    std::string serialize() const override;
    
private:
    std::string oldLanguage_;
    std::string newLanguage_;
    float confidence_;
    uint32_t utteranceId_;
};

// Message factory and parser
class MessageProtocol {
public:
    static std::unique_ptr<Message> parseMessage(const std::string& json);
    static MessageType getMessageType(const std::string& json);
    static bool validateMessage(const std::string& json);
    static std::string stateToString(StatusUpdateMessage::State state);
    
private:
    static MessageType stringToMessageType(const std::string& typeStr);
    static std::string messageTypeToString(MessageType type);
};

} // namespace core