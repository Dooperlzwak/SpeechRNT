#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace audio {

// Forward declarations
template <typename T> class CircularBuffer;
class RealTimeFFT;

// Audio level metrics for real-time monitoring
struct AudioLevelMetrics {
  float currentLevel;  // Current RMS level (0.0 to 1.0)
  float peakLevel;     // Peak level in current window (0.0 to 1.0)
  float averageLevel;  // Running average level (0.0 to 1.0)
  bool clipping;       // Audio clipping detected
  bool silence;        // Silence detected
  float peakHoldLevel; // Peak hold level for visualization

  AudioLevelMetrics()
      : currentLevel(0.0f), peakLevel(0.0f), averageLevel(0.0f),
        clipping(false), silence(true), peakHoldLevel(0.0f) {}
};

// Spectral analysis results
struct SpectralAnalysis {
  std::vector<float> frequencySpectrum; // Magnitude spectrum
  std::vector<float> powerSpectrum;     // Power spectrum
  float dominantFrequency;              // Dominant frequency in Hz
  float spectralCentroid;               // Spectral centroid in Hz
  float spectralBandwidth;              // Spectral bandwidth in Hz
  float spectralRolloff;                // Spectral rolloff frequency in Hz
  std::vector<float> mfccCoefficients;  // MFCC coefficients (13 coefficients)
  float spectralFlatness;               // Spectral flatness measure
  float spectralFlux;                   // Spectral flux (change rate)

  SpectralAnalysis()
      : dominantFrequency(0.0f), spectralCentroid(0.0f),
        spectralBandwidth(0.0f), spectralRolloff(0.0f), spectralFlatness(0.0f),
        spectralFlux(0.0f) {
    mfccCoefficients.resize(13, 0.0f);
  }
};

// Real-time audio metrics combining level and spectral analysis
struct RealTimeMetrics {
  AudioLevelMetrics levels;
  SpectralAnalysis spectral;
  float noiseLevel;         // Estimated noise level in dB
  float speechProbability;  // Probability of speech (0.0 to 1.0)
  float voiceActivityScore; // Voice activity score (0.0 to 1.0)
  int64_t timestampMs;      // Timestamp in milliseconds
  uint32_t sequenceNumber;  // Sequence number for ordering

  RealTimeMetrics()
      : noiseLevel(-60.0f), speechProbability(0.0f), voiceActivityScore(0.0f),
        timestampMs(0), sequenceNumber(0) {}
};

// Audio effects configuration
struct AudioEffectsConfig {
  bool enableCompressor;
  bool enableEqualizer;
  bool enableReverb;
  bool enableNoiseGate;

  // Compressor settings
  float compressorThreshold; // dB
  float compressorRatio;     // ratio
  float compressorAttack;    // ms
  float compressorRelease;   // ms

  // Noise gate settings
  float noiseGateThreshold; // dB
  float noiseGateRatio;     // ratio

  AudioEffectsConfig()
      : enableCompressor(false), enableEqualizer(false), enableReverb(false),
        enableNoiseGate(false), compressorThreshold(-20.0f),
        compressorRatio(4.0f), compressorAttack(5.0f), compressorRelease(50.0f),
        noiseGateThreshold(-40.0f), noiseGateRatio(10.0f) {}
};

// Circular buffer for efficient real-time audio processing
template <typename T> class CircularBuffer {
public:
  explicit CircularBuffer(size_t capacity);
  ~CircularBuffer() = default;

  // Thread-safe operations
  bool push(const T &item);
  bool push(const std::vector<T> &items);
  bool pop(T &item);
  std::vector<T> pop(size_t count);

  // Non-blocking operations
  bool tryPush(const T &item);
  bool tryPop(T &item);

  // Buffer state
  size_t size() const;
  size_t capacity() const;
  bool empty() const;
  bool full() const;
  void clear();

  // Access operations (not thread-safe, use with external synchronization)
  const T &operator[](size_t index) const;
  std::vector<T> getLatest(size_t count) const;
  std::vector<T> getAll() const;

private:
  std::vector<T> buffer_;
  size_t capacity_;
  std::atomic<size_t> head_;
  std::atomic<size_t> tail_;
  std::atomic<size_t> size_;
  mutable std::mutex mutex_;
};

// Real-time FFT processor for spectral analysis
class RealTimeFFT {
public:
  explicit RealTimeFFT(size_t fftSize = 1024);
  ~RealTimeFFT() = default;

  // FFT operations
  std::vector<float> computeFFT(const std::vector<float> &samples);
  std::vector<float> computePowerSpectrum(const std::vector<float> &samples);
  std::vector<float>
  computeMagnitudeSpectrum(const std::vector<float> &samples);

  // Windowing functions
  void setWindowFunction(
      const std::string
          &windowType); // "hann", "hamming", "blackman", "rectangular"
  std::vector<float> applyWindow(const std::vector<float> &samples);

  // Configuration
  void setFFTSize(size_t size);
  size_t getFFTSize() const { return fftSize_; }

  // Frequency analysis
  float getFrequencyBin(size_t bin, uint32_t sampleRate) const;
  size_t getFrequencyBin(float frequency, uint32_t sampleRate) const;
  std::vector<float> getFrequencyAxis(uint32_t sampleRate) const;

private:
  size_t fftSize_;
  std::vector<float> window_;
  std::vector<float> fftBuffer_;
  std::vector<float> fftOutput_;
  std::string windowType_;

  void generateWindow();
  void performFFT(const float *input, float *output);
  static void cooleyTukeyFFT(float *data, size_t n);
};

// Audio effects processor for real-time effects
class AudioEffectsProcessor {
public:
  explicit AudioEffectsProcessor(const AudioEffectsConfig &config);
  ~AudioEffectsProcessor() = default;

  // Effects processing
  std::vector<float> processEffects(const std::vector<float> &input);
  void updateConfig(const AudioEffectsConfig &config);

  // Individual effects
  std::vector<float> applyCompressor(const std::vector<float> &input);
  std::vector<float> applyNoiseGate(const std::vector<float> &input);
  std::vector<float> applyEqualizer(const std::vector<float> &input);
  std::vector<float> applyReverb(const std::vector<float> &input);

  // Effect state management
  void resetEffectStates();
  bool isEffectEnabled(const std::string &effectName) const;

private:
  AudioEffectsConfig config_;

  // Effect state variables
  float compressorEnvelope_;
  float noiseGateEnvelope_;
  std::vector<float> reverbBuffer_;
  std::vector<float> equalizerState_;

  // Helper functions
  float applyCompressorGain(float input, float envelope);
  float updateCompressorEnvelope(float input);
  float updateNoiseGateEnvelope(float input);
};

// Main real-time audio analyzer class
class RealTimeAudioAnalyzer {
public:
  explicit RealTimeAudioAnalyzer(uint32_t sampleRate = 16000,
                                 size_t bufferSize = 1024);
  ~RealTimeAudioAnalyzer();

  // Initialization and configuration
  bool initialize();
  void shutdown();
  bool isInitialized() const { return initialized_; }

  // Audio processing
  void processAudioSample(float sample);
  void processAudioChunk(const std::vector<float> &chunk);
  void processAudioChunk(const float *samples, size_t count);

  // Real-time metrics access
  RealTimeMetrics getCurrentMetrics() const;
  std::vector<RealTimeMetrics> getMetricsHistory(size_t samples) const;
  AudioLevelMetrics getCurrentLevels() const;
  SpectralAnalysis getCurrentSpectralAnalysis() const;

  // Callback system for real-time notifications
  using MetricsCallback = std::function<void(const RealTimeMetrics &)>;
  using LevelsCallback = std::function<void(const AudioLevelMetrics &)>;
  using SpectralCallback = std::function<void(const SpectralAnalysis &)>;

  void registerMetricsCallback(MetricsCallback callback);
  void registerLevelsCallback(LevelsCallback callback);
  void registerSpectralCallback(SpectralCallback callback);
  void clearCallbacks();

  // Audio effects
  void enableRealTimeEffects(bool enabled);
  bool areEffectsEnabled() const { return effectsEnabled_; }
  std::vector<float> applyRealTimeEffects(const std::vector<float> &audio);
  void updateEffectsConfig(const AudioEffectsConfig &config);

  // Configuration
  void setSampleRate(uint32_t sampleRate);
  uint32_t getSampleRate() const { return sampleRate_; }
  void setBufferSize(size_t bufferSize);
  size_t getBufferSize() const { return bufferSize_; }
  void setUpdateInterval(std::chrono::milliseconds interval);

  // Analysis parameters
  void setNoiseFloorThreshold(float thresholdDb);
  void setSilenceThreshold(float threshold);
  void setClippingThreshold(float threshold);
  void setSpeechDetectionSensitivity(float sensitivity);

  // Dropout and glitch detection
  struct AudioDropout {
    int64_t timestampMs;
    float durationMs;
    float severityScore;
    std::string description;
  };

  std::vector<AudioDropout> getDetectedDropouts() const;
  void clearDropoutHistory();
  bool hasRecentDropouts(std::chrono::milliseconds timeWindow) const;

  // Performance monitoring
  struct PerformanceMetrics {
    float averageProcessingTimeMs;
    float maxProcessingTimeMs;
    float cpuUsagePercent;
    size_t droppedSamples;
    size_t totalSamplesProcessed;
  };

  PerformanceMetrics getPerformanceMetrics() const;
  void resetPerformanceMetrics();

private:
  // Configuration
  uint32_t sampleRate_;
  size_t bufferSize_;
  std::chrono::milliseconds updateInterval_;
  std::atomic<bool> initialized_;
  std::atomic<bool> running_;
  std::atomic<bool> effectsEnabled_;

  // Processing components
  std::unique_ptr<CircularBuffer<float>> audioBuffer_;
  std::unique_ptr<CircularBuffer<RealTimeMetrics>> metricsBuffer_;
  std::unique_ptr<RealTimeFFT> fftProcessor_;
  std::unique_ptr<AudioEffectsProcessor> effectsProcessor_;

  // Analysis parameters
  float noiseFloorThreshold_;
  float silenceThreshold_;
  float clippingThreshold_;
  float speechDetectionSensitivity_;

  // State variables
  mutable std::mutex metricsMutex_;
  RealTimeMetrics currentMetrics_;
  AudioLevelMetrics currentLevels_;
  SpectralAnalysis currentSpectral_;

  // Level tracking
  float runningAverage_;
  float peakHold_;
  std::chrono::steady_clock::time_point lastPeakTime_;
  std::vector<float> levelHistory_;

  // Spectral tracking
  std::vector<float> previousSpectrum_;
  float spectralFluxAccumulator_;

  // Dropout detection
  mutable std::mutex dropoutMutex_;
  std::vector<AudioDropout> detectedDropouts_;
  std::chrono::steady_clock::time_point lastAudioTime_;
  bool expectingAudio_;

  // Performance tracking
  mutable std::mutex performanceMutex_;
  PerformanceMetrics performanceMetrics_;
  std::chrono::steady_clock::time_point lastPerformanceUpdate_;

  // Callback management
  mutable std::mutex callbackMutex_;
  std::vector<MetricsCallback> metricsCallbacks_;
  std::vector<LevelsCallback> levelsCallbacks_;
  std::vector<SpectralCallback> spectralCallbacks_;

  // Processing thread
  std::unique_ptr<std::thread> processingThread_;
  void processingLoop();

  // Analysis functions
  void updateLevelMetrics(const std::vector<float> &samples);
  void updateSpectralAnalysis(const std::vector<float> &samples);
  void updateNoiseEstimation(const std::vector<float> &samples);
  void updateSpeechDetection(const AudioLevelMetrics &levels,
                             const SpectralAnalysis &spectral);
  void detectDropouts(const std::vector<float> &samples);
  void updatePerformanceMetrics(std::chrono::microseconds processingTime);

  // Callback notification
  void notifyMetricsCallbacks(const RealTimeMetrics &metrics);
  void notifyLevelsCallbacks(const AudioLevelMetrics &levels);
  void notifySpectralCallbacks(const SpectralAnalysis &spectral);

  // Helper functions
  float calculateRMS(const std::vector<float> &samples);
  float calculatePeak(const std::vector<float> &samples);
  float calculateSpectralCentroid(const std::vector<float> &spectrum,
                                  uint32_t sampleRate);
  float calculateSpectralBandwidth(const std::vector<float> &spectrum,
                                   float centroid, uint32_t sampleRate);
  float calculateSpectralRolloff(const std::vector<float> &spectrum,
                                 uint32_t sampleRate,
                                 float rolloffPercent = 0.85f);
  float calculateSpectralFlatness(const std::vector<float> &spectrum);
  float calculateSpectralFlux(const std::vector<float> &currentSpectrum,
                              const std::vector<float> &previousSpectrum);
  std::vector<float> calculateMFCC(const std::vector<float> &spectrum,
                                   uint32_t sampleRate);
  float estimateNoiseLevel(const std::vector<float> &samples);
  float calculateSpeechProbability(const AudioLevelMetrics &levels,
                                   const SpectralAnalysis &spectral);
  bool detectClipping(const std::vector<float> &samples, float threshold);
  bool detectSilence(const std::vector<float> &samples, float threshold);
};

} // namespace audio