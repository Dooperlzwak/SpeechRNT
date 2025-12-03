#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace audio {

/**
 * Enhanced Silero-VAD implementation that uses the actual silero-vad ONNX model
 * for ML-based voice activity detection with fallback to energy-based VAD.
 */
class SileroVadImpl {
public:
  enum class VadMode {
    SILERO,       // Use silero-vad ML model
    ENERGY_BASED, // Use energy-based fallback
    HYBRID        // Try silero-vad, fallback to energy-based on failure
  };

  SileroVadImpl();
  ~SileroVadImpl();

  // Initialization and configuration
  bool initialize(uint32_t sampleRate, const std::string &modelPath = "");
  void shutdown();
  bool isInitialized() const { return initialized_; }

  // VAD mode management
  void setVadMode(VadMode mode);
  VadMode getCurrentMode() const { return currentMode_; }
  bool isSileroModelLoaded() const { return sileroModelLoaded_; }

  // Audio processing
  float processSamples(const std::vector<float> &samples);
  void reset(); // Reset internal state

  // Configuration
  void setEnergyThreshold(float threshold) { energyThreshold_ = threshold; }
  float getEnergyThreshold() const { return energyThreshold_; }

  // Statistics and diagnostics
  struct Statistics {
    uint64_t totalProcessedChunks;
    uint64_t sileroSuccessCount;
    uint64_t energyFallbackCount;
    double averageProcessingTimeMs;
    double averageConfidence;
  };

  Statistics getStatistics() const;
  void resetStatistics();

private:
  // Initialization state
  bool initialized_;
  uint32_t sampleRate_;
  std::string modelPath_;

  // VAD mode and model state
  VadMode currentMode_;
  bool sileroModelLoaded_;

  // ONNX Runtime components (forward declarations to avoid header dependencies)
  class OnnxSession;
  std::unique_ptr<OnnxSession> onnxSession_;

  // Energy-based VAD fallback
  float energyThreshold_;
  std::vector<float> energyHistory_;
  size_t energyHistorySize_;

  // Statistics
  mutable std::mutex statsMutex_;
  Statistics stats_;

  // Internal methods
  bool loadSileroModel(const std::string &modelPath);
  void unloadSileroModel();

  float processSileroVad(const std::vector<float> &samples);
  float processEnergyBasedVad(const std::vector<float> &samples);

  float calculateRmsEnergy(const std::vector<float> &samples);
  float calculateSpectralCentroid(const std::vector<float> &samples);

  void updateStatistics(bool usedSilero, float confidence,
                        double processingTimeMs);

  // Audio preprocessing
  std::vector<float> preprocessAudio(const std::vector<float> &samples);
  std::vector<float> resampleIfNeeded(const std::vector<float> &samples,
                                      uint32_t targetSampleRate);
};

/**
 * Enhanced Energy-based VAD implementation as fallback
 */
class EnergyBasedVAD {
public:
  struct Config {
    float energyThreshold = 0.01f;
    float spectralThreshold = 0.5f;
    size_t windowSize = 512;
    size_t hopSize = 256;
    bool useSpectralFeatures = true;
    bool useAdaptiveThreshold = true;
    float adaptationRate = 0.1f;
  };

  explicit EnergyBasedVAD(const Config &config);
  EnergyBasedVAD();

  void configure(const Config &config);
  float detectVoiceActivity(const std::vector<float> &samples);
  void reset();

private:
  Config config_;
  float adaptiveThreshold_;
  std::vector<float> energyHistory_;
  std::vector<float> spectralHistory_;

  float calculateEnergy(const std::vector<float> &samples);
  float calculateSpectralFeatures(const std::vector<float> &samples);
  void updateAdaptiveThreshold(float currentEnergy);
};

} // namespace audio