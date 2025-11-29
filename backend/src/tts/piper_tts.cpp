#include "tts/piper_tts.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace speechrnt {
namespace tts {

PiperTTS::PiperTTS() : initialized_(false), speed_(1.0f), volume_(1.0f) {}

PiperTTS::~PiperTTS() { cleanup(); }

bool PiperTTS::initialize(const std::string &modelPath,
                          const std::string &voiceId) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    cleanup();
  }

  model_dir_ = modelPath;

  // Find piper binary
  if (!findPiperBinary()) {
    setError("Could not find 'piper' binary. Please ensure it is installed and "
             "in the PATH or in the models directory.");
    return false;
  }

  // Load available voices from the model directory
  if (!loadVoices()) {
    setError("Failed to load Piper voice models from " + model_dir_);
    return false;
  }

  // Set default voice
  if (!voiceId.empty() && voice_map_.find(voiceId) != voice_map_.end()) {
    default_voice_id_ = voiceId;
  } else if (!available_voices_.empty()) {
    default_voice_id_ = available_voices_[0].id;
  } else {
    setError("No voices available");
    return false;
  }

  initialized_ = true;
  std::cout << "[PiperTTS] Initialized with " << available_voices_.size()
            << " voices, default: " << default_voice_id_ << std::endl;

  return true;
}

bool PiperTTS::findPiperBinary() {
  // Check if piper is in the system path
  if (system("which piper > /dev/null 2>&1") == 0) {
    piper_binary_path_ = "piper";
    return true;
  }

  // Check local paths
  std::vector<std::string> searchPaths = {"./piper", "../piper",
                                          model_dir_ + "/../piper/piper",
                                          "/usr/local/bin/piper"};

  for (const auto &path : searchPaths) {
    if (fs::exists(path)) {
      piper_binary_path_ = path;
      return true;
    }
  }

  return false;
}

bool PiperTTS::loadVoices() {
  available_voices_.clear();
  voice_map_.clear();

  if (!fs::exists(model_dir_)) {
    return false;
  }

  // Scan directory for .onnx files
  for (const auto &entry : fs::directory_iterator(model_dir_)) {
    if (entry.path().extension() == ".onnx") {
      std::string filename = entry.path().filename().string();
      std::string voiceId = entry.path().stem().string();

      // Simple parsing of filename to extract metadata if possible
      // Format often: language_region-name-quality
      // e.g., en_US-amy-medium

      std::string language = "en"; // Default
      std::string name = voiceId;
      std::string gender = "unknown";

      size_t firstDash = voiceId.find('-');
      if (firstDash != std::string::npos) {
        std::string langRegion = voiceId.substr(0, firstDash);
        size_t underscore = langRegion.find('_');
        if (underscore != std::string::npos) {
          language = langRegion.substr(0, underscore);
        } else {
          language = langRegion;
        }

        size_t secondDash = voiceId.find('-', firstDash + 1);
        if (secondDash != std::string::npos) {
          name = voiceId.substr(firstDash + 1, secondDash - firstDash - 1);
        }
      }

      VoiceInfo voice = createVoiceInfo(voiceId, name, language, gender,
                                        entry.path().string());
      available_voices_.push_back(voice);
      voice_map_[voiceId] = voice;
    }
  }

  return !available_voices_.empty();
}

VoiceInfo PiperTTS::createVoiceInfo(const std::string &voiceId,
                                    const std::string &name,
                                    const std::string &language,
                                    const std::string &gender,
                                    const std::string &modelFile) {
  VoiceInfo voice;
  voice.id = voiceId;
  voice.name = name;
  voice.language = language;
  voice.gender = gender;
  voice.description = name + " (" + language + ")";
  voice.isAvailable = true;
  return voice;
}

SynthesisResult PiperTTS::synthesize(const std::string &text,
                                     const std::string &voiceId) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    SynthesisResult result;
    result.success = false;
    result.errorMessage = "Piper TTS not initialized";
    return result;
  }

  std::string voice = voiceId.empty() ? default_voice_id_ : voiceId;

  // Check if voice exists
  if (voice_map_.find(voice) == voice_map_.end()) {
    SynthesisResult result;
    result.success = false;
    result.errorMessage = "Voice not found: " + voice;
    return result;
  }

  return performSynthesis(text, voice);
}

SynthesisResult PiperTTS::performSynthesis(const std::string &text,
                                           const std::string &voiceId) {
  SynthesisResult result;

  std::string modelFile = model_dir_ + "/" + voiceId + ".onnx";

  // Construct command: echo "text" | piper --model model.onnx --output_file -
  // Note: We need to be careful with shell escaping for the text

  // A safer way would be to write text to a temp file, but for now let's try to
  // escape quotes
  std::string escapedText = text;
  // Basic escaping of double quotes
  size_t pos = 0;
  while ((pos = escapedText.find("\"", pos)) != std::string::npos) {
    escapedText.replace(pos, 1, "\\\"");
    pos += 2;
  }

  std::string cmd = "echo \"" + escapedText + "\" | " + piper_binary_path_ +
                    " --model " + modelFile + " --output_file -";

  if (speed_ != 1.0f) {
    cmd += " --length_scale " + std::to_string(1.0f / speed_);
  }

  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    result.success = false;
    result.errorMessage = "Failed to execute piper command";
    return result;
  }

  // Read WAV data from stdout
  std::vector<uint8_t> buffer;
  std::array<uint8_t, 1024> chunk;
  size_t bytesRead;

  while ((bytesRead = fread(chunk.data(), 1, chunk.size(), pipe)) > 0) {
    buffer.insert(buffer.end(), chunk.begin(), chunk.begin() + bytesRead);
  }

  int returnCode = pclose(pipe);

  if (returnCode != 0 || buffer.empty()) {
    result.success = false;
    result.errorMessage = "Piper execution failed or produced no output";
    return result;
  }

  result.audioData = buffer;
  result.success = true;
  result.voiceId = voiceId;

  // Parse WAV header to get sample rate and duration
  if (buffer.size() > 44) {
    // Sample rate is at offset 24 (4 bytes)
    uint32_t sampleRate;
    std::memcpy(&sampleRate, &buffer[24], 4);
    result.sampleRate = sampleRate;

    // Channels at offset 22 (2 bytes)
    uint16_t channels;
    std::memcpy(&channels, &buffer[22], 2);
    result.channels = channels;

    // Data size is usually at offset 40 (4 bytes), but can vary if there are
    // extra chunks For simplicity, we'll estimate duration from total size
    uint32_t dataSize = buffer.size() - 44; // Approximate

    int bytesPerSample = 2; // 16-bit
    result.duration =
        static_cast<float>(dataSize) / (sampleRate * channels * bytesPerSample);
  }

  return result;
}

std::future<SynthesisResult>
PiperTTS::synthesizeAsync(const std::string &text, const std::string &voiceId) {
  return std::async(std::launch::async, [this, text, voiceId]() {
    return synthesize(text, voiceId);
  });
}

void PiperTTS::synthesizeWithCallback(const std::string &text,
                                      SynthesisCallback callback,
                                      const std::string &voiceId) {
  std::thread([this, text, voiceId, callback]() {
    SynthesisResult result = synthesize(text, voiceId);
    callback(result);
  }).detach();
}

std::vector<VoiceInfo> PiperTTS::getAvailableVoices() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return available_voices_;
}

std::vector<VoiceInfo>
PiperTTS::getVoicesForLanguage(const std::string &language) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<VoiceInfo> voices;

  for (const auto &voice : available_voices_) {
    if (voice.language == language) {
      voices.push_back(voice);
    }
  }

  return voices;
}

bool PiperTTS::setDefaultVoice(const std::string &voiceId) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (voice_map_.find(voiceId) != voice_map_.end()) {
    default_voice_id_ = voiceId;
    return true;
  }

  setError("Voice ID not found: " + voiceId);
  return false;
}

std::string PiperTTS::getDefaultVoice() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return default_voice_id_;
}

void PiperTTS::setSynthesisParameters(float speed, float pitch, float volume) {
  std::lock_guard<std::mutex> lock(mutex_);
  speed_ = std::clamp(speed, 0.5f, 2.0f);
  // Piper doesn't support pitch or volume directly in CLI easily without
  // complex SSML or post-processing We'll ignore pitch and volume for now in
  // the CLI command generation
  volume_ = std::clamp(volume, 0.0f, 1.0f);
}

void PiperTTS::cleanup() {
  initialized_ = false;
  available_voices_.clear();
  voice_map_.clear();
  last_error_.clear();
}

void PiperTTS::setError(const std::string &error) {
  last_error_ = error;
  std::cerr << "[PiperTTS] Error: " << error << std::endl;
}

// Factory function
std::unique_ptr<TTSInterface> createPiperTTS() {
  return std::make_unique<PiperTTS>();
}

} // namespace tts
} // namespace speechrnt
