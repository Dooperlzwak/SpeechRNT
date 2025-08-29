#pragma once

#include <string>
#include <chrono>

namespace speechrnt {
namespace core {

/**
 * WebSocket message for notifying clients of language changes
 */
struct LanguageChangeMessage {
    std::string type = "language_change";
    std::string session_id;
    std::string previous_language;
    std::string detected_language;
    float confidence;
    std::chrono::steady_clock::time_point timestamp;
    
    LanguageChangeMessage() 
        : confidence(0.0f)
        , timestamp(std::chrono::steady_clock::now()) {}
    
    LanguageChangeMessage(
        const std::string& sid,
        const std::string& prev_lang,
        const std::string& detected_lang,
        float conf
    ) : session_id(sid)
      , previous_language(prev_lang)
      , detected_language(detected_lang)
      , confidence(conf)
      , timestamp(std::chrono::steady_clock::now()) {}
    
    /**
     * Convert to JSON string for WebSocket transmission
     */
    std::string toJson() const;
    
    /**
     * Create from JSON string
     */
    static LanguageChangeMessage fromJson(const std::string& json);
};

} // namespace core
} // namespace speechrnt