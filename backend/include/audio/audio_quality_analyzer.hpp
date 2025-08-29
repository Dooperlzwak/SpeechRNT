#pragma once

#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <complex>
#include <map>

namespace audio {

// Audio quality metrics structure
struct AudioQualityMetrics {
    // Signal quality metrics
    float signalToNoiseRatio;      // SNR in dB
    float spectralCentroid;        // Spectral centroid in Hz
    float spectralBandwidth;       // Spectral bandwidth in Hz
    float spectralRolloff;         // Spectral rolloff frequency in Hz
    float zeroCrossingRate;        // Zero crossing rate (0.0 to 1.0)
    
    // MFCC features for audio characterization
    std::vector<float> mfccFeatures; // 13 MFCC coefficients
    
    // Audio artifacts detection
    bool hasClipping;              // Audio clipping detected
    bool hasDropouts;              // Audio dropouts detected
    bool hasDistortion;            // Audio distortion detected
    bool hasEcho;                  // Echo/reverb detected
    bool hasNoise;                 // Background noise detected
    
    // Quality scores (0.0 to 1.0)
    float overallQuality;          // Overall audio quality score
    float speechQuality;           // Speech-specific quality score
    float noiseLevel;              // Noise level (0.0 = no noise, 1.0 = very noisy)
    
    // Processing metadata
    std::chrono::steady_clock::time_point timestamp;
    size_t sampleCount;
    float durationSeconds;
    
    AudioQualityMetrics() 
        : signalToNoiseRatio(0.0f), spectralCentroid(0.0f), spectralBandwidth(0.0f),
          spectralRolloff(0.0f), zeroCrossingRate(0.0f), mfccFeatures(13, 0.0f),
          hasClipping(false), hasDropouts(false), hasDistortion(false),
          hasEcho(false), hasNoise(false), overallQuality(0.0f),
          speechQuality(0.0f), noiseLevel(0.0f), sampleCount(0), durationSeconds(0.0f) {
        timestamp = std::chrono::steady_clock::now();
    }
};

// Audio artifact detection results
struct AudioArtifacts {
    struct ClippingInfo {
        bool detected;
        float percentage;          // Percentage of samples clipped
        std::vector<size_t> locations; // Sample indices where clipping occurs
    } clipping;
    
    struct DropoutInfo {
        bool detected;
        size_t count;              // Number of dropouts detected
        std::vector<std::pair<size_t, size_t>> locations; // Start/end sample indices
    } dropouts;
    
    struct DistortionInfo {
        bool detected;
        float thd;                 // Total Harmonic Distortion
        float severity;            // Distortion severity (0.0 to 1.0)
    } distortion;
    
    struct EchoInfo {
        bool detected;
        float delay;               // Echo delay in seconds
        float strength;            // Echo strength (0.0 to 1.0)
    } echo;
    
    struct NoiseInfo {
        bool detected;
        float level;               // Noise level in dB
        std::string type;          // Noise type (white, pink, brown, etc.)
    } noise;
};

// Spectral analysis results
struct SpectralAnalysis {
    std::vector<float> magnitudeSpectrum;  // Magnitude spectrum
    std::vector<float> powerSpectrum;      // Power spectrum
    std::vector<float> frequencies;        // Frequency bins
    
    float dominantFrequency;               // Dominant frequency in Hz
    float spectralCentroid;                // Spectral centroid in Hz
    float spectralBandwidth;               // Spectral bandwidth in Hz
    float spectralRolloff;                 // Spectral rolloff in Hz
    float spectralFlatness;                // Spectral flatness (Wiener entropy)
    
    // Frequency band energies
    float lowFreqEnergy;                   // 0-500 Hz
    float midFreqEnergy;                   // 500-2000 Hz
    float highFreqEnergy;                  // 2000+ Hz
    
    SpectralAnalysis() 
        : dominantFrequency(0.0f), spectralCentroid(0.0f), spectralBandwidth(0.0f),
          spectralRolloff(0.0f), spectralFlatness(0.0f), lowFreqEnergy(0.0f),
          midFreqEnergy(0.0f), highFreqEnergy(0.0f) {}
};

// Configuration for audio quality analysis
struct AudioQualityConfig {
    // Analysis parameters
    size_t fftSize;                        // FFT size for spectral analysis
    size_t hopSize;                        // Hop size for windowed analysis
    float windowOverlap;                   // Window overlap (0.0 to 1.0)
    
    // Detection thresholds
    float clippingThreshold;               // Clipping detection threshold
    float dropoutThreshold;                // Dropout detection threshold
    float distortionThreshold;             // Distortion detection threshold
    float echoThreshold;                   // Echo detection threshold
    float noiseThreshold;                  // Noise detection threshold
    
    // Quality scoring weights
    float snrWeight;                       // SNR weight in overall quality
    float spectralWeight;                  // Spectral features weight
    float artifactWeight;                  // Artifact penalty weight
    
    // MFCC parameters
    size_t numMfccCoeffs;                  // Number of MFCC coefficients
    float melFilterBankSize;               // Mel filter bank size
    
    AudioQualityConfig() 
        : fftSize(1024), hopSize(512), windowOverlap(0.5f),
          clippingThreshold(0.95f), dropoutThreshold(0.01f),
          distortionThreshold(0.1f), echoThreshold(0.3f), noiseThreshold(-20.0f),
          snrWeight(0.4f), spectralWeight(0.3f), artifactWeight(0.3f),
          numMfccCoeffs(13), melFilterBankSize(26) {}
};

// Audio quality analyzer class
class AudioQualityAnalyzer {
public:
    explicit AudioQualityAnalyzer(const AudioQualityConfig& config = AudioQualityConfig());
    ~AudioQualityAnalyzer() = default;
    
    // Main analysis functions
    AudioQualityMetrics analyzeQuality(const std::vector<float>& audioData, int sampleRate = 16000);
    AudioArtifacts detectArtifacts(const std::vector<float>& audioData, int sampleRate = 16000);
    SpectralAnalysis performSpectralAnalysis(const std::vector<float>& audioData, int sampleRate = 16000);
    
    // Individual metric calculations
    float calculateSNR(const std::vector<float>& audioData);
    float calculateZeroCrossingRate(const std::vector<float>& audioData);
    std::vector<float> calculateMFCC(const std::vector<float>& audioData, int sampleRate = 16000);
    
    // Artifact detection methods
    AudioArtifacts::ClippingInfo detectClipping(const std::vector<float>& audioData);
    AudioArtifacts::DropoutInfo detectDropouts(const std::vector<float>& audioData);
    AudioArtifacts::DistortionInfo detectDistortion(const std::vector<float>& audioData, int sampleRate = 16000);
    AudioArtifacts::EchoInfo detectEcho(const std::vector<float>& audioData, int sampleRate = 16000);
    AudioArtifacts::NoiseInfo detectNoise(const std::vector<float>& audioData, int sampleRate = 16000);
    
    // Quality scoring
    float calculateOverallQuality(const AudioQualityMetrics& metrics);
    float calculateSpeechQuality(const AudioQualityMetrics& metrics);
    
    // Configuration management
    void setConfig(const AudioQualityConfig& config) { config_ = config; }
    const AudioQualityConfig& getConfig() const { return config_; }
    
    // Adaptive parameter adjustment
    void adaptParametersForQuality(const AudioQualityMetrics& metrics);
    AudioQualityConfig getOptimalConfig(const AudioQualityMetrics& metrics);
    
    // Real-time analysis support
    void initializeRealTimeAnalysis(int sampleRate, size_t bufferSize);
    AudioQualityMetrics analyzeRealTime(const std::vector<float>& audioChunk);
    void resetRealTimeState();
    
private:
    AudioQualityConfig config_;
    
    // FFT and spectral analysis
    std::vector<std::complex<float>> fftBuffer_;
    std::vector<float> windowFunction_;
    std::vector<float> melFilterBank_;
    
    // Real-time analysis state
    std::vector<float> analysisBuffer_;
    size_t bufferPosition_;
    bool realTimeInitialized_;
    
    // Helper methods
    void initializeFFT();
    void initializeWindowFunction();
    void initializeMelFilterBank(int sampleRate);
    
    std::vector<std::complex<float>> computeFFT(const std::vector<float>& signal);
    std::vector<float> computeMagnitudeSpectrum(const std::vector<std::complex<float>>& fft);
    std::vector<float> computePowerSpectrum(const std::vector<std::complex<float>>& fft);
    
    float computeSpectralCentroid(const std::vector<float>& spectrum, int sampleRate);
    float computeSpectralBandwidth(const std::vector<float>& spectrum, float centroid, int sampleRate);
    float computeSpectralRolloff(const std::vector<float>& spectrum, int sampleRate, float rolloffPercent = 0.85f);
    float computeSpectralFlatness(const std::vector<float>& spectrum);
    
    // Signal processing utilities
    std::vector<float> applyWindow(const std::vector<float>& signal);
    std::vector<float> computeEnvelope(const std::vector<float>& signal);
    std::vector<float> computeAutocorrelation(const std::vector<float>& signal);
    
    // Noise estimation
    float estimateNoiseFloor(const std::vector<float>& spectrum);
    float estimateSignalPower(const std::vector<float>& audioData);
    
    // Quality scoring helpers
    float scoreSpectralFeatures(const SpectralAnalysis& spectral);
    float scoreArtifacts(const AudioArtifacts& artifacts);
    float combineQualityScores(const std::vector<float>& scores, const std::vector<float>& weights);
};

} // namespace audio