#pragma once

#include "tts/tts_interface.hpp"
#include <memory>
#include <mutex>
#include <map>
#include <thread>

namespace speechrnt {
namespace tts {

/**
 * Piper TTS implementation of the TTS interface
 * Uses the 'piper' command line tool for synthesis
 */
class PiperTTS : public TTSInterface {
public:
    PiperTTS();
    ~PiperTTS() override;
    
    // TTSInterface implementation
    bool initialize(const std::string& modelPath, const std::string& voiceId = "") override;
    SynthesisResult synthesize(const std::string& text, const std::string& voiceId = "") override;
    std::future<SynthesisResult> synthesizeAsync(const std::string& text, const std::string& voiceId = "") override;
    void synthesizeWithCallback(const std::string& text, SynthesisCallback callback, const std::string& voiceId = "") override;
    
    std::vector<VoiceInfo> getAvailableVoices() const override;
    std::vector<VoiceInfo> getVoicesForLanguage(const std::string& language) const override;
    bool setDefaultVoice(const std::string& voiceId) override;
    std::string getDefaultVoice() const override;
    
    void setSynthesisParameters(float speed = 1.0f, float pitch = 0.0f, float volume = 1.0f) override;
    bool isReady() const override { return initialized_; }
    std::string getLastError() const override { return last_error_; }
    void cleanup() override;
    
private:
    bool initialized_;
    std::string model_dir_;
    std::string piper_binary_path_;
    std::string default_voice_id_;
    std::string last_error_;
    
    // Voice management
    std::vector<VoiceInfo> available_voices_;
    std::map<std::string, VoiceInfo> voice_map_;
    
    // Synthesis parameters
    float speed_;
    float volume_;
    
    // Thread safety
    mutable std::mutex mutex_;
    
    // Helper methods
    bool findPiperBinary();
    bool loadVoices();
    VoiceInfo createVoiceInfo(const std::string& voiceId, const std::string& name, 
                             const std::string& language, const std::string& gender,
                             const std::string& modelFile);
    SynthesisResult performSynthesis(const std::string& text, const std::string& voiceId);
    void setError(const std::string& error);
};

} // namespace tts
} // namespace speechrnt
