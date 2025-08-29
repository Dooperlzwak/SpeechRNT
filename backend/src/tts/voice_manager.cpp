#include "tts/voice_manager.hpp"
#include <iostream>
#include <algorithm>

namespace speechrnt {
namespace tts {

VoiceManager::VoiceManager() : initialized_(false) {
}

VoiceManager::~VoiceManager() {
}

bool VoiceManager::initialize(std::shared_ptr<TTSInterface> ttsEngine) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!ttsEngine || !ttsEngine->isReady()) {
        std::cerr << "[VoiceManager] Invalid or uninitialized TTS engine" << std::endl;
        return false;
    }
    
    tts_engine_ = ttsEngine;
    
    if (!refreshVoices()) {
        std::cerr << "[VoiceManager] Failed to load voices from TTS engine" << std::endl;
        return false;
    }
    
    initialized_ = true;
    std::cout << "[VoiceManager] Initialized with " << all_voices_.size() << " voices" << std::endl;
    
    return true;
}

std::string VoiceManager::getBestVoiceForLanguage(const std::string& language, const std::string& preferredGender) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return "";
    }
    
    // Check if there's a user preference for this language
    auto prefIt = language_preferences_.find(language);
    if (prefIt != language_preferences_.end()) {
        // Check voice availability without calling isVoiceAvailable (to avoid deadlock)
        auto voiceIt = voice_map_.find(prefIt->second);
        if (voiceIt != voice_map_.end() && voiceIt->second.isAvailable) {
            return prefIt->second;
        }
    }
    
    // Get voices for the language
    auto langIt = language_voices_.find(language);
    if (langIt == language_voices_.end() || langIt->second.empty()) {
        std::cerr << "[VoiceManager] No voices available for language: " << language << std::endl;
        return "";
    }
    
    return selectBestVoice(langIt->second, preferredGender);
}

std::vector<VoiceInfo> VoiceManager::getVoicesForLanguage(const std::string& language) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = language_voices_.find(language);
    if (it != language_voices_.end()) {
        return it->second;
    }
    
    return {};
}

VoiceInfo VoiceManager::getVoiceInfo(const std::string& voiceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = voice_map_.find(voiceId);
    if (it != voice_map_.end()) {
        return it->second;
    }
    
    return VoiceInfo(); // Return empty VoiceInfo
}

bool VoiceManager::isVoiceAvailable(const std::string& voiceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = voice_map_.find(voiceId);
    return it != voice_map_.end() && it->second.isAvailable;
}

std::vector<VoiceInfo> VoiceManager::getAllVoices() {
    std::lock_guard<std::mutex> lock(mutex_);
    return all_voices_;
}

std::vector<std::string> VoiceManager::getSupportedLanguages() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> languages;
    for (const auto& pair : language_voices_) {
        if (!pair.second.empty()) {
            languages.push_back(pair.first);
        }
    }
    
    return languages;
}

void VoiceManager::setLanguagePreference(const std::string& language, const std::string& voiceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check voice availability without calling isVoiceAvailable (to avoid deadlock)
    auto it = voice_map_.find(voiceId);
    if (it != voice_map_.end() && it->second.isAvailable) {
        language_preferences_[language] = voiceId;
        std::cout << "[VoiceManager] Set preference for " << language << ": " << voiceId << std::endl;
    } else {
        std::cerr << "[VoiceManager] Cannot set preference - voice not available: " << voiceId << std::endl;
    }
}

std::string VoiceManager::getLanguagePreference(const std::string& language) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = language_preferences_.find(language);
    if (it != language_preferences_.end()) {
        return it->second;
    }
    
    return "";
}

bool VoiceManager::refreshVoices() {
    if (!tts_engine_) {
        return false;
    }
    
    all_voices_ = tts_engine_->getAvailableVoices();
    buildVoiceMaps();
    
    std::cout << "[VoiceManager] Refreshed voices: " << all_voices_.size() << " total" << std::endl;
    return true;
}

// Private helper methods

void VoiceManager::buildVoiceMaps() {
    voice_map_.clear();
    language_voices_.clear();
    
    for (const auto& voice : all_voices_) {
        // Build voice ID map
        voice_map_[voice.id] = voice;
        
        // Build language-to-voices map
        language_voices_[voice.language].push_back(voice);
    }
    
    // Sort voices within each language by name
    for (auto& pair : language_voices_) {
        std::sort(pair.second.begin(), pair.second.end(), 
                  [](const VoiceInfo& a, const VoiceInfo& b) {
                      return a.name < b.name;
                  });
    }
}

std::string VoiceManager::selectBestVoice(const std::vector<VoiceInfo>& voices, const std::string& preferredGender) {
    if (voices.empty()) {
        return "";
    }
    
    // If no gender preference, return the first available voice
    if (preferredGender.empty()) {
        for (const auto& voice : voices) {
            if (voice.isAvailable) {
                return voice.id;
            }
        }
        return "";
    }
    
    // Look for preferred gender first
    for (const auto& voice : voices) {
        if (voice.isAvailable && voice.gender == preferredGender) {
            return voice.id;
        }
    }
    
    // If no voice with preferred gender found, return any available voice
    for (const auto& voice : voices) {
        if (voice.isAvailable) {
            return voice.id;
        }
    }
    
    return "";
}

} // namespace tts
} // namespace speechrnt