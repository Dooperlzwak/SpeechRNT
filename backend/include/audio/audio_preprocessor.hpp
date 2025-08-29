#pragma once

#include "audio/audio_quality_analyzer.hpp"
#include "audio/adaptive_audio_processor.hpp"
#include <vector>
#include <memory>
#include <string>
#include <map>
#include <chrono>
#include <atomic>
#include <mutex>

namespace audio {

// Audio preprocessing configuration
struct AudioPreprocessingConfig {
    // Feature enablement
    bool enableNoiseReduction;
    bool enableVolumeNormalization;
    bool enableEchoCancellation;
    bool enableAdaptiveProcessing;
    bool enableQualityAnalysis;
    
    // Noise reduction settings
    struct {
        float spectralSubtractionAlpha;    // Over-subtraction factor
        float wienerFilterBeta;            // Smoothing factor
        float noiseGateThreshold;          // Noise gate threshold
        bool enableSpectralSubtraction;    // Use spectral subtraction
        bool enableWienerFiltering;        // Use Wiener filtering
    } noiseReduction;
    
    // Volume normalization settings
    struct {
        float targetRMS;                   // Target RMS level
        float compressionRatio;            // Dynamic range compression
        float attackTime;                  // Attack time in seconds
        float releaseTime;                 // Release time in seconds
        bool enableAGC;                    // Automatic gain control
        bool enableCompression;            // Dynamic range compression
    } volumeNormalization;
    
    // Echo cancellation settings
    struct {
        size_t adaptiveFilterLength;       // Adaptive filter length
        float convergenceRate;             // LMS convergence rate
        float echoSuppressionStrength;     // Echo suppression strength
        bool enableLMS;                    // Use LMS algorithm
        bool enableNLMS;                   // Use NLMS algorithm
    } echoCancellation;
    
    // Quality analysis settings
    AudioQualityConfig qualityConfig;
    
    // Adaptive processing settings
    AdaptiveProcessingParams adaptiveParams;
    
    AudioPreprocessingConfig()
        : enableNoiseReduction(true), enableVolumeNormalization(true),
          enableEchoCancellation(false), enableAdaptiveProcessing(true),
          enableQualityAnalysis(true) {
        
        // Initialize noise reduction settings
        noiseReduction.spectralSubtractionAlpha = 2.0f;
        noiseReduction.wienerFilterBeta = 0.1f;
        noiseReduction.noiseGateThreshold = -40.0f;
        noiseReduction.enableSpectralSubtraction = true;
        noiseReduction.enableWienerFiltering = false;
        
        // Initialize volume normalization settings
        volumeNormalization.targetRMS = 0.1f;
        volumeNormalization.compressionRatio = 2.0f;
        volumeNormalization.attackTime = 0.01f;
        volumeNormalization.releaseTime = 0.1f;
        volumeNormalization.enableAGC = true;
        volumeNormalization.enableCompression = true;
        
        // Initialize echo cancellation settings
        echoCancellation.adaptiveFilterLength = 512;
        echoCancellation.convergenceRate = 0.01f;
        echoCancellation.echoSuppressionStrength = 0.7f;
        echoCancellation.enableLMS = true;
        echoCancellation.enableNLMS = false;
    }
};

// Preprocessing result with detailed information
struct PreprocessingResult {
    std::vector<float> processedAudio;
    AudioQualityMetrics qualityBefore;
    AudioQualityMetrics qualityAfter;
    AudioCharacteristics audioCharacteristics;
    
    // Applied processing information
    std::vector<std::string> appliedFilters;
    std::map<std::string, float> processingParameters;
    
    // Performance metrics
    float processingLatencyMs;
    float qualityImprovement;
    
    // Processing metadata
    std::chrono::steady_clock::time_point timestamp;
    size_t inputSampleCount;
    size_t outputSampleCount;
    
    PreprocessingResult()
        : processingLatencyMs(0.0f), qualityImprovement(0.0f),
          inputSampleCount(0), outputSampleCount(0) {
        timestamp = std::chrono::steady_clock::now();
    }
};

// Noise reduction filter implementations
class NoiseReductionFilter {
public:
    explicit NoiseReductionFilter(int sampleRate);
    ~NoiseReductionFilter() = default;
    
    // Spectral subtraction methods
    std::vector<float> processSpectralSubtraction(const std::vector<float>& audioData, 
                                                 float alpha = 2.0f, float beta = 0.01f);
    
    // Wiener filtering methods
    std::vector<float> processWienerFilter(const std::vector<float>& audioData, 
                                          float smoothingFactor = 0.1f);
    
    // Noise gate
    std::vector<float> processNoiseGate(const std::vector<float>& audioData, 
                                       float threshold = -40.0f);
    
    // Noise profile management
    void updateNoiseProfile(const std::vector<float>& noiseData);
    void resetNoiseProfile();
    std::vector<float> getNoiseProfile() const { return noiseProfile_; }
    
    // Configuration
    void setSampleRate(int sampleRate) { sampleRate_ = sampleRate; }
    int getSampleRate() const { return sampleRate_; }
    
private:
    int sampleRate_;
    std::vector<float> noiseProfile_;
    std::vector<float> previousFrame_;
    size_t frameSize_;
    
    // Helper methods
    std::vector<std::complex<float>> computeFFT(const std::vector<float>& signal);
    std::vector<float> computeIFFT(const std::vector<std::complex<float>>& spectrum);
    std::vector<float> applyWindow(const std::vector<float>& signal);
    float estimateNoisePower(const std::vector<float>& spectrum, size_t bin);
};

// Volume normalizer with AGC and compression
class VolumeNormalizer {
public:
    explicit VolumeNormalizer(int sampleRate);
    ~VolumeNormalizer() = default;
    
    // Automatic gain control
    std::vector<float> processAGC(const std::vector<float>& audioData, 
                                 float targetRMS = 0.1f);
    
    // Dynamic range compression
    std::vector<float> processCompression(const std::vector<float>& audioData,
                                         float ratio = 2.0f, float threshold = 0.7f,
                                         float attack = 0.01f, float release = 0.1f);
    
    // Peak limiting
    std::vector<float> processLimiter(const std::vector<float>& audioData,
                                     float threshold = 0.95f);
    
    // State management
    void resetState();
    
    // Statistics
    float getCurrentGain() const { return currentGain_; }
    float getCurrentRMS() const { return currentRMS_; }
    
private:
    int sampleRate_;
    float currentGain_;
    float currentRMS_;
    float compressorGain_;
    
    // Envelope followers
    float attackCoeff_;
    float releaseCoeff_;
    
    // Helper methods
    float calculateRMS(const std::vector<float>& audioData);
    float updateEnvelope(float input, float current, float attack, float release);
    void updateCoefficients(float attack, float release);
};

// Echo canceller with adaptive filtering
class EchoCanceller {
public:
    explicit EchoCanceller(int sampleRate, size_t filterLength = 512);
    ~EchoCanceller() = default;
    
    // LMS adaptive filtering
    std::vector<float> processLMS(const std::vector<float>& audioData,
                                 float convergenceRate = 0.01f);
    
    // NLMS adaptive filtering
    std::vector<float> processNLMS(const std::vector<float>& audioData,
                                  float convergenceRate = 0.01f);
    
    // Echo suppression
    std::vector<float> processEchoSuppression(const std::vector<float>& audioData,
                                             float suppressionStrength = 0.7f);
    
    // Filter management
    void resetAdaptiveFilter();
    void setFilterLength(size_t length);
    size_t getFilterLength() const { return filterLength_; }
    
    // Echo detection
    bool detectEcho(const std::vector<float>& audioData, float threshold = 0.3f);
    float estimateEchoDelay(const std::vector<float>& audioData);
    
private:
    int sampleRate_;
    size_t filterLength_;
    std::vector<float> adaptiveFilter_;
    std::vector<float> inputHistory_;
    std::vector<float> errorHistory_;
    
    // Helper methods
    float dotProduct(const std::vector<float>& a, const std::vector<float>& b);
    void updateInputHistory(float sample);
    std::vector<float> computeAutocorrelation(const std::vector<float>& signal);
};

// Main audio preprocessor class
class AudioPreprocessor {
public:
    explicit AudioPreprocessor(const AudioPreprocessingConfig& config = AudioPreprocessingConfig(),
                              int sampleRate = 16000);
    ~AudioPreprocessor() = default;
    
    // Main preprocessing functions
    PreprocessingResult preprocessAudio(const std::vector<float>& audioData);
    std::vector<float> preprocessAudioSimple(const std::vector<float>& audioData);
    
    // Multi-channel preprocessing
    std::vector<PreprocessingResult> preprocessMultiChannelAudio(
        const std::vector<std::vector<float>>& audioData);
    
    // Real-time preprocessing
    void initializeRealTimeProcessing(size_t bufferSize);
    std::vector<float> preprocessRealTimeChunk(const std::vector<float>& audioChunk);
    void resetRealTimeState();
    
    // Individual processing components
    std::vector<float> applyNoiseReduction(const std::vector<float>& audioData);
    std::vector<float> applyVolumeNormalization(const std::vector<float>& audioData);
    std::vector<float> applyEchoCancellation(const std::vector<float>& audioData);
    
    // Quality analysis
    AudioQualityMetrics analyzeAudioQuality(const std::vector<float>& audioData);
    float calculateQualityImprovement(const AudioQualityMetrics& before,
                                     const AudioQualityMetrics& after);
    
    // Configuration management
    void setConfig(const AudioPreprocessingConfig& config);
    AudioPreprocessingConfig getConfig() const;
    void setSampleRate(int sampleRate);
    int getSampleRate() const { return sampleRate_; }
    
    // Adaptive processing
    void enableAdaptiveMode(bool enabled);
    bool isAdaptiveModeEnabled() const;
    void adaptParametersForQuality(const AudioQualityMetrics& quality);
    
    // Component access
    std::shared_ptr<NoiseReductionFilter> getNoiseReductionFilter() { return noiseFilter_; }
    std::shared_ptr<VolumeNormalizer> getVolumeNormalizer() { return volumeNormalizer_; }
    std::shared_ptr<EchoCanceller> getEchoCanceller() { return echoCanceller_; }
    std::shared_ptr<AudioQualityAnalyzer> getQualityAnalyzer() { return qualityAnalyzer_; }
    std::shared_ptr<AdaptiveAudioProcessor> getAdaptiveProcessor() { return adaptiveProcessor_; }
    
    // Statistics and monitoring
    struct PreprocessingStatistics {
        uint64_t totalSamplesProcessed;
        uint64_t totalChunksProcessed;
        double averageProcessingTime;
        double averageQualityImprovement;
        std::map<std::string, uint64_t> filterUsageCount;
        std::chrono::steady_clock::time_point lastProcessingTime;
    };
    
    PreprocessingStatistics getStatistics() const;
    void resetStatistics();
    
    // Preset management
    void loadPreset(const std::string& presetName);
    void savePreset(const std::string& presetName, const AudioPreprocessingConfig& config);
    std::vector<std::string> getAvailablePresets() const;
    
private:
    // Configuration
    mutable std::mutex configMutex_;
    AudioPreprocessingConfig config_;
    int sampleRate_;
    
    // Processing components
    std::shared_ptr<NoiseReductionFilter> noiseFilter_;
    std::shared_ptr<VolumeNormalizer> volumeNormalizer_;
    std::shared_ptr<EchoCanceller> echoCanceller_;
    std::shared_ptr<AudioQualityAnalyzer> qualityAnalyzer_;
    std::shared_ptr<AdaptiveAudioProcessor> adaptiveProcessor_;
    
    // Real-time processing state
    std::vector<float> processingBuffer_;
    size_t bufferPosition_;
    bool realTimeInitialized_;
    
    // Statistics
    mutable std::mutex statsMutex_;
    PreprocessingStatistics stats_;
    
    // Presets
    std::map<std::string, AudioPreprocessingConfig> presets_;
    
    // Private methods
    void initializeComponents();
    void initializePresets();
    
    // Processing pipeline
    std::vector<float> applyProcessingPipeline(const std::vector<float>& audioData,
                                              std::vector<std::string>& appliedFilters,
                                              std::map<std::string, float>& parameters);
    
    // Quality assessment
    bool shouldApplyNoiseReduction(const AudioQualityMetrics& quality);
    bool shouldApplyVolumeNormalization(const AudioQualityMetrics& quality);
    bool shouldApplyEchoCancellation(const AudioQualityMetrics& quality);
    
    // Statistics helpers
    void updateStatistics(size_t samplesProcessed, double processingTime,
                         const std::vector<std::string>& appliedFilters,
                         float qualityImprovement);
};

} // namespace audio