#pragma once

#include "tts/tts_interface.hpp"
#include <memory>
#include <mutex>
#include <map>
#include <thread>

// Forward declarations to avoid including Coqui TTS headers in the interface
struct TTS;

namespace speechrnt {
namespace tts {

/**
 * Coqui TTS implementation of the TTS interface
 */
class CoquiTTS : public TTSInterface {
public:
    CoquiTTS();
    ~CoquiTTS() override;
    
    // TTSInterface implementation
    bool initialize(const std::string& modelPath, const std::string& voiceId = "") override;
    bool initializeWithGPU(const std::string& modelPath, int gpuDeviceId = 0, const std::string& voiceId = "");
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
    std::string model_path_;
    std::string default_voice_id_;
    std::string last_error_;
    
    // Coqui TTS context
    TTS* tts_engine_;
    
    // Voice management
    std::vector<VoiceInfo> available_voices_;
    std::map<std::string, VoiceInfo> voice_map_;
    
    // Synthesis parameters
    float speed_;
    float pitch_;
    float volume_;
    
    // Thread safety
    mutable std::mutex mutex_;
    
    // GPU configuration
    bool gpu_enabled_;
    int gpu_device_id_;
    
    // Helper methods
    bool loadModel(const std::string& modelPath);
    bool loadVoices();
    VoiceInfo createVoiceInfo(const std::string& voiceId, const std::string& name, 
                             const std::string& language, const std::string& gender);
    SynthesisResult performSynthesis(const std::string& text, const std::string& voiceId);
    std::vector<uint8_t> convertToWav(const std::vector<float>& audioData, int sampleRate, int channels);
    void setError(const std::string& error);
    
    // Mock implementation helpers (for development without Coqui TTS)
    bool initializeMock(const std::string& modelPath, const std::string& voiceId);
    SynthesisResult synthesizeMock(const std::string& text, const std::string& voiceId);
    void loadMockVoices();
};

} // namespace tts
} // namespace speechrnt