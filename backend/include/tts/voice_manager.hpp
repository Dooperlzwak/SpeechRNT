#pragma once

#include "tts/tts_interface.hpp"
#include <memory>
#include <map>
#include <vector>
#include <string>
#include <mutex>

namespace speechrnt {
namespace tts {

/**
 * Voice manager for handling voice selection and caching
 */
class VoiceManager {
public:
    VoiceManager();
    ~VoiceManager();
    
    /**
     * Initialize the voice manager with a TTS engine
     * @param ttsEngine Shared pointer to TTS engine
     * @return true if initialization successful
     */
    bool initialize(std::shared_ptr<TTSInterface> ttsEngine);
    
    /**
     * Get the best voice for a given language
     * @param language Language code (e.g., "en", "es")
     * @param preferredGender Preferred gender ("male", "female", "neutral", or empty for any)
     * @return Voice ID, or empty string if no suitable voice found
     */
    std::string getBestVoiceForLanguage(const std::string& language, const std::string& preferredGender = "");
    
    /**
     * Get all voices for a language
     * @param language Language code
     * @return Vector of voice information
     */
    std::vector<VoiceInfo> getVoicesForLanguage(const std::string& language);
    
    /**
     * Get voice information by ID
     * @param voiceId Voice ID
     * @return Voice information, or empty VoiceInfo if not found
     */
    VoiceInfo getVoiceInfo(const std::string& voiceId);
    
    /**
     * Check if a voice is available
     * @param voiceId Voice ID to check
     * @return true if voice is available
     */
    bool isVoiceAvailable(const std::string& voiceId);
    
    /**
     * Get all available voices
     * @return Vector of all voice information
     */
    std::vector<VoiceInfo> getAllVoices();
    
    /**
     * Get supported languages
     * @return Vector of language codes
     */
    std::vector<std::string> getSupportedLanguages();
    
    /**
     * Set voice preferences for a language
     * @param language Language code
     * @param voiceId Preferred voice ID for this language
     */
    void setLanguagePreference(const std::string& language, const std::string& voiceId);
    
    /**
     * Get voice preference for a language
     * @param language Language code
     * @return Preferred voice ID, or empty if no preference set
     */
    std::string getLanguagePreference(const std::string& language);
    
    /**
     * Refresh voice list from TTS engine
     * @return true if refresh successful
     */
    bool refreshVoices();
    
    /**
     * Check if manager is ready
     * @return true if ready
     */
    bool isReady() const { return initialized_; }
    
private:
    bool initialized_;
    std::shared_ptr<TTSInterface> tts_engine_;
    
    // Voice caching
    std::vector<VoiceInfo> all_voices_;
    std::map<std::string, VoiceInfo> voice_map_;
    std::map<std::string, std::vector<VoiceInfo>> language_voices_;
    std::map<std::string, std::string> language_preferences_;
    
    // Thread safety
    mutable std::mutex mutex_;
    
    // Helper methods
    void buildVoiceMaps();
    std::string selectBestVoice(const std::vector<VoiceInfo>& voices, const std::string& preferredGender);
};

} // namespace tts
} // namespace speechrnt