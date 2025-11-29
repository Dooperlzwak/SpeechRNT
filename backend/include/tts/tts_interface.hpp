#pragma once

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace speechrnt {
namespace tts {

/**
 * Audio synthesis result containing the synthesized audio data and metadata
 */
struct SynthesisResult {
  std::vector<uint8_t> audioData; // WAV format audio data
  float duration;                 // Duration in seconds
  int sampleRate;                 // Sample rate (typically 22050 or 16000)
  int channels;                   // Number of channels (typically 1 for mono)
  std::string voiceId;            // Voice used for synthesis
  bool success;
  std::string errorMessage;

  SynthesisResult()
      : duration(0.0f), sampleRate(22050), channels(1), success(false) {}
};

/**
 * Voice information structure
 */
struct VoiceInfo {
  std::string id;
  std::string name;
  std::string language;
  std::string gender; // "male", "female", "neutral"
  std::string description;
  bool isAvailable;

  VoiceInfo() : isAvailable(false) {}
};

/**
 * Abstract interface for Text-to-Speech engines
 */
class TTSInterface {
public:
  using SynthesisCallback = std::function<void(const SynthesisResult &result)>;

  virtual ~TTSInterface() = default;

  /**
   * Initialize the TTS engine
   * @param modelPath Path to the TTS model directory
   * @param voiceId Default voice ID to use
   * @return true if initialization successful
   */
  virtual bool initialize(const std::string &modelPath,
                          const std::string &voiceId = "") = 0;

  /**
   * Synthesize speech from text synchronously
   * @param text Text to synthesize
   * @param voiceId Voice ID to use (empty for default)
   * @return Synthesis result
   */
  virtual SynthesisResult synthesize(const std::string &text,
                                     const std::string &voiceId = "") = 0;

  /**
   * Synthesize speech from text asynchronously
   * @param text Text to synthesize
   * @param voiceId Voice ID to use (empty for default)
   * @return Future containing synthesis result
   */
  virtual std::future<SynthesisResult>
  synthesizeAsync(const std::string &text, const std::string &voiceId = "") = 0;

  /**
   * Synthesize speech with callback (for streaming)
   * @param text Text to synthesize
   * @param callback Callback function for result
   * @param voiceId Voice ID to use (empty for default)
   */
  virtual void synthesizeWithCallback(const std::string &text,
                                      SynthesisCallback callback,
                                      const std::string &voiceId = "") = 0;

  /**
   * Get list of available voices
   * @return Vector of voice information
   */
  virtual std::vector<VoiceInfo> getAvailableVoices() const = 0;

  /**
   * Get voices for a specific language
   * @param language Language code (e.g., "en", "es")
   * @return Vector of voice information for the language
   */
  virtual std::vector<VoiceInfo>
  getVoicesForLanguage(const std::string &language) const = 0;

  /**
   * Set the default voice
   * @param voiceId Voice ID to set as default
   * @return true if voice was set successfully
   */
  virtual bool setDefaultVoice(const std::string &voiceId) = 0;

  /**
   * Get the current default voice ID
   * @return Default voice ID
   */
  virtual std::string getDefaultVoice() const = 0;

  /**
   * Set synthesis parameters
   * @param speed Speech speed (0.5 to 2.0, 1.0 is normal)
   * @param pitch Pitch adjustment (-1.0 to 1.0, 0.0 is normal)
   * @param volume Volume level (0.0 to 1.0, 1.0 is maximum)
   */
  virtual void setSynthesisParameters(float speed = 1.0f, float pitch = 0.0f,
                                      float volume = 1.0f) = 0;

  /**
   * Check if the engine is ready for synthesis
   * @return true if ready
   */
  virtual bool isReady() const = 0;

  /**
   * Get the last error message
   * @return Error message string
   */
  virtual std::string getLastError() const = 0;

  /**
   * Clean up resources
   */
  virtual void cleanup() = 0;
};

/**
 * Factory function for creating Coqui TTS instances
 */
std::unique_ptr<TTSInterface> createPiperTTS();

} // namespace tts
} // namespace speechrnt