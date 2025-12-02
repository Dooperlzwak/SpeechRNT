#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace audio {

// Audio format specifications
enum class AudioCodec { PCM_16, PCM_24, PCM_32, FLOAT_32, UNKNOWN };

enum class SampleRate {
  SR_8000 = 8000,
  SR_16000 = 16000,
  SR_22050 = 22050,
  SR_44100 = 44100,
  SR_48000 = 48000
};

// Extended audio format with validation
struct ExtendedAudioFormat {
  SampleRate sampleRate;
  uint16_t channels;
  AudioCodec codec;
  uint32_t chunkSize;

  ExtendedAudioFormat()
      : sampleRate(SampleRate::SR_16000), channels(1),
        codec(AudioCodec::PCM_16), chunkSize(1024) {}

  ExtendedAudioFormat(SampleRate sr, uint16_t ch, AudioCodec c, uint32_t cs)
      : sampleRate(sr), channels(ch), codec(c), chunkSize(cs) {}

  bool isValid() const;
  size_t getBytesPerSample() const;
  size_t getChunkSizeBytes() const;
  std::string toString() const;
};

// Audio quality metrics
struct AudioQualityMetrics {
  float signalToNoiseRatio;      // dB
  float totalHarmonicDistortion; // %
  float dynamicRange;            // dB
  float peakAmplitude;           // normalized [-1.0, 1.0]
  float rmsLevel;                // normalized [0.0, 1.0]
  float zeroCrossingRate;        // Hz
  float spectralCentroid;        // Hz
  bool hasClipping;
  bool hasSilence;
  float noiseFloor; // dB

  AudioQualityMetrics();
  bool isGoodQuality() const;
  std::string getQualityDescription() const;
};

// Noise detection results
struct NoiseProfile {
  float noiseLevel;  // dB
  float speechLevel; // dB
  float noiseFloor;  // dB
  bool hasBackgroundNoise;
  bool hasImpulseNoise;
  bool hasPeriodicNoise;
  std::vector<float> frequencySpectrum;

  NoiseProfile();
  float getSNR() const;
  bool requiresDenoising() const;
};

// Audio format validator and converter
class AudioFormatValidator {
public:
  AudioFormatValidator() = default;
  ~AudioFormatValidator() = default;

  // Format validation
  static bool isFormatSupported(const ExtendedAudioFormat &format);
  static bool canConvertFormat(const ExtendedAudioFormat &from,
                               const ExtendedAudioFormat &to);
  static std::vector<ExtendedAudioFormat> getSupportedFormats();

  // Data validation
  static bool validateAudioData(std::string_view data,
                                const ExtendedAudioFormat &format);
  static bool validateSampleRate(uint32_t sampleRate);
  static bool validateChannelCount(uint16_t channels);
  static bool validateBitDepth(const AudioCodec &codec);

  // Format detection
  static ExtendedAudioFormat detectFormat(std::string_view data);
  static AudioCodec detectCodec(std::string_view data);

private:
  static const std::vector<SampleRate> supportedSampleRates_;
  static const std::vector<AudioCodec> supportedCodecs_;
  static const std::unordered_map<AudioCodec, size_t> codecBytesPerSample_;
};

// Audio format converter
class AudioFormatConverter {
public:
  AudioFormatConverter() = default;
  ~AudioFormatConverter() = default;

  // Sample rate conversion
  static std::vector<float> resample(const std::vector<float> &input,
                                     uint32_t inputRate, uint32_t outputRate);
  static std::vector<float> upsample(const std::vector<float> &input,
                                     uint32_t factor);
  static std::vector<float> downsample(const std::vector<float> &input,
                                       uint32_t factor);

  // Channel conversion
  static std::vector<float> stereoToMono(const std::vector<float> &stereoData);
  static std::vector<float> monoToStereo(const std::vector<float> &monoData);
  static std::vector<float> convertChannels(const std::vector<float> &input,
                                            uint16_t inputChannels,
                                            uint16_t outputChannels);

  // Codec conversion
  static std::vector<float> convertToFloat(std::string_view data,
                                           AudioCodec codec);
  static std::vector<int16_t> convertToPCM16(const std::vector<float> &samples);
  static std::vector<int32_t> convertToPCM24(const std::vector<float> &samples);
  static std::vector<int32_t> convertToPCM32(const std::vector<float> &samples);

  // Format conversion
  static std::vector<float>
  convertFormat(std::string_view input, const ExtendedAudioFormat &inputFormat,
                const ExtendedAudioFormat &outputFormat);

private:
  static float interpolate(const std::vector<float> &data, float index);
  static std::vector<float>
  applyAntiAliasingFilter(const std::vector<float> &input);
};

// Audio quality assessor
class AudioQualityAssessor {
public:
  AudioQualityAssessor() = default;
  ~AudioQualityAssessor() = default;

  // Quality assessment
  static AudioQualityMetrics assessQuality(const std::vector<float> &samples,
                                           uint32_t sampleRate);
  static float calculateSNR(const std::vector<float> &samples);
  static float calculateTHD(const std::vector<float> &samples,
                            uint32_t sampleRate);
  static float calculateDynamicRange(const std::vector<float> &samples);
  static float calculateRMSLevel(const std::vector<float> &samples);
  static float calculateZeroCrossingRate(const std::vector<float> &samples,
                                         uint32_t sampleRate);
  static float calculateSpectralCentroid(const std::vector<float> &samples,
                                         uint32_t sampleRate);

  // Quality checks
  static bool hasClipping(const std::vector<float> &samples,
                          float threshold = 0.95f);
  static bool hasSilence(const std::vector<float> &samples,
                         float threshold = 0.01f);
  static float estimateNoiseFloor(const std::vector<float> &samples);

  // Quality improvement suggestions
  static std::vector<std::string>
  getQualityIssues(const AudioQualityMetrics &metrics);
  static bool requiresPreprocessing(const AudioQualityMetrics &metrics);

  // Spectral analysis (needed by NoiseDetector)
  static std::vector<float>
  computePowerSpectrum(const std::vector<float> &samples);

private:
  static std::vector<float> computeFFT(const std::vector<float> &samples);
  static float findPeakAmplitude(const std::vector<float> &samples);
};

// Noise detector and analyzer
class NoiseDetector {
public:
  NoiseDetector() = default;
  ~NoiseDetector() = default;

  // Noise detection
  static NoiseProfile analyzeNoise(const std::vector<float> &samples,
                                   uint32_t sampleRate);
  static float detectNoiseLevel(const std::vector<float> &samples);
  static float detectSpeechLevel(const std::vector<float> &samples);
  static bool hasBackgroundNoise(const std::vector<float> &samples,
                                 float threshold = -40.0f);
  static bool hasImpulseNoise(const std::vector<float> &samples);
  static bool hasPeriodicNoise(const std::vector<float> &samples,
                               uint32_t sampleRate);

  // Noise characterization
  static std::vector<float>
  computeNoiseSpectrum(const std::vector<float> &samples);
  static float estimateNoiseFloor(const std::vector<float> &samples);
  static std::vector<float>
  identifyNoiseFrequencies(const std::vector<float> &samples,
                           uint32_t sampleRate);

  // Noise classification
  enum class NoiseType {
    NONE,
    WHITE_NOISE,
    PINK_NOISE,
    BROWN_NOISE,
    IMPULSE_NOISE,
    PERIODIC_NOISE,
    ENVIRONMENTAL_NOISE,
    UNKNOWN
  };

  static NoiseType classifyNoise(const NoiseProfile &profile);
  static std::string noiseTypeToString(NoiseType type);

private:
  static float calculateSpectralFlatness(const std::vector<float> &spectrum);
  static std::vector<float>
  detectSpectralPeaks(const std::vector<float> &spectrum);
  static bool isPeriodicPattern(const std::vector<float> &samples,
                                uint32_t sampleRate);
};

// Audio preprocessor
class AudioPreprocessor {
public:
  AudioPreprocessor() = default;
  ~AudioPreprocessor() = default;

  // Preprocessing operations
  static std::vector<float>
  normalizeAmplitude(const std::vector<float> &samples,
                     float targetLevel = 0.7f);
  static std::vector<float> removeClipping(const std::vector<float> &samples);
  static std::vector<float> removeSilence(const std::vector<float> &samples,
                                          float threshold = 0.01f);
  static std::vector<float> applyGainControl(const std::vector<float> &samples,
                                             float gain);

  // Filtering
  static std::vector<float>
  applyHighPassFilter(const std::vector<float> &samples, uint32_t sampleRate,
                      float cutoffHz = 80.0f);
  static std::vector<float>
  applyLowPassFilter(const std::vector<float> &samples, uint32_t sampleRate,
                     float cutoffHz = 8000.0f);
  static std::vector<float>
  applyBandPassFilter(const std::vector<float> &samples, uint32_t sampleRate,
                      float lowHz, float highHz);

  // Noise reduction
  static std::vector<float> reduceNoise(const std::vector<float> &samples,
                                        const NoiseProfile &noiseProfile);
  static std::vector<float>
  spectralSubtraction(const std::vector<float> &samples,
                      const std::vector<float> &noiseSpectrum);
  static std::vector<float>
  adaptiveNoiseReduction(const std::vector<float> &samples,
                         uint32_t sampleRate);

  // Enhancement
  static std::vector<float> enhanceSpeech(const std::vector<float> &samples,
                                          uint32_t sampleRate);
  static std::vector<float> applyCompression(const std::vector<float> &samples,
                                             float ratio = 4.0f,
                                             float threshold = -20.0f);
  static std::vector<float> applyDeEsser(const std::vector<float> &samples,
                                         uint32_t sampleRate);

private:
  static std::vector<float> applyIIRFilter(const std::vector<float> &samples,
                                           const std::vector<float> &b,
                                           const std::vector<float> &a);
  static std::vector<float>
  applyFIRFilter(const std::vector<float> &samples,
                 const std::vector<float> &coefficients);
  static std::vector<float>
  computeFilterCoefficients(float cutoff, uint32_t sampleRate, bool highPass);
};

// Audio stream validator for real-time processing
class AudioStreamValidator {
public:
  AudioStreamValidator(const ExtendedAudioFormat &expectedFormat);
  ~AudioStreamValidator() = default;

  // Stream validation
  bool validateChunk(std::string_view data);
  bool validateContinuity(const std::vector<float> &samples);
  bool validateLatency(std::chrono::milliseconds maxLatency);

  // Stream monitoring
  struct StreamHealth {
    bool isHealthy;
    float averageLatency;
    float dropoutRate;
    float qualityScore;
    std::vector<std::string> issues;
  };

  StreamHealth getStreamHealth() const;
  void resetHealth();

  // Configuration
  void setExpectedFormat(const ExtendedAudioFormat &format);
  void setQualityThresholds(float minSNR, float maxTHD);
  void setLatencyThreshold(std::chrono::milliseconds maxLatency);

private:
  ExtendedAudioFormat expectedFormat_;
  float minSNR_;
  float maxTHD_;
  std::chrono::milliseconds maxLatency_;

  // Health tracking
  mutable std::vector<float> latencyHistory_;
  mutable std::vector<float> qualityHistory_;
  mutable size_t totalChunks_;
  mutable size_t droppedChunks_;
  mutable std::chrono::steady_clock::time_point lastChunkTime_;

  void updateHealthMetrics(float latency, float quality);
  bool isWithinLatencyBounds(std::chrono::milliseconds latency) const;
};

} // namespace audio