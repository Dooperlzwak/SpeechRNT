#include "core/language_change_message.hpp"
#include <sstream>
#include <iomanip>

namespace speechrnt {
namespace core {

std::string LanguageChangeMessage::toJson() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"type\": \"" << type << "\",\n";
    json << "  \"session_id\": \"" << session_id << "\",\n";
    json << "  \"previous_language\": \"" << previous_language << "\",\n";
    json << "  \"detected_language\": \"" << detected_language << "\",\n";
    json << "  \"confidence\": " << std::fixed << std::setprecision(3) << confidence << ",\n";
    json << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::milliseconds>(
               timestamp.time_since_epoch()).count() << "\n";
    json << "}";
    return json.str();
}

LanguageChangeMessage LanguageChangeMessage::fromJson(const std::string& json) {
    // Simple JSON parsing - in production, use a proper JSON library
    LanguageChangeMessage message;
    
    // Extract session_id
    size_t session_pos = json.find("\"session_id\":");
    if (session_pos != std::string::npos) {
        size_t start = json.find("\"", session_pos + 13) + 1;
        size_t end = json.find("\"", start);
        if (start != std::string::npos && end != std::string::npos) {
            message.session_id = json.substr(start, end - start);
        }
    }
    
    // Extract previous_language
    size_t prev_pos = json.find("\"previous_language\":");
    if (prev_pos != std::string::npos) {
        size_t start = json.find("\"", prev_pos + 20) + 1;
        size_t end = json.find("\"", start);
        if (start != std::string::npos && end != std::string::npos) {
            message.previous_language = json.substr(start, end - start);
        }
    }
    
    // Extract detected_language
    size_t detected_pos = json.find("\"detected_language\":");
    if (detected_pos != std::string::npos) {
        size_t start = json.find("\"", detected_pos + 20) + 1;
        size_t end = json.find("\"", start);
        if (start != std::string::npos && end != std::string::npos) {
            message.detected_language = json.substr(start, end - start);
        }
    }
    
    // Extract confidence
    size_t conf_pos = json.find("\"confidence\":");
    if (conf_pos != std::string::npos) {
        size_t start = conf_pos + 13;
        size_t end = json.find(",", start);
        if (end == std::string::npos) {
            end = json.find("}", start);
        }
        if (start != std::string::npos && end != std::string::npos) {
            try {
                std::string conf_str = json.substr(start, end - start);
                // Remove whitespace
                conf_str.erase(0, conf_str.find_first_not_of(" \t\n\r"));
                conf_str.erase(conf_str.find_last_not_of(" \t\n\r") + 1);
                message.confidence = std::stof(conf_str);
            } catch (...) {
                message.confidence = 0.0f;
            }
        }
    }
    
    return message;
}

} // namespace core
} // namespace speechrnt