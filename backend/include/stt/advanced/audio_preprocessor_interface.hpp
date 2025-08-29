#pragma once

#include "stt/advanced/advanced_stt_config.hpp"
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

namespace stt {
namespace advanced {

/**
 * Audio quality metrics
 */
struct AudioQualityMetrics {
    float signalToNoiseRatio;
    float spectralCentroid;
    float zeroCrossingRate;
    float spectralRolloff;
    float mfccFeatures[13];
    bool hasEcho;
    bool hasNoise;
    float overallQuality; // 0.0 to 1.0
    float dynamicRange;
    float peakLevel;
    float rmsLevel;
    bool hasClipping;
    bool hasDropouts;
    
    AudioQualityMetrics() 
        : signalToNoiseRatio(0.0f)
        , spectralCentroid(0.0f)
        , zeroCrossingRate(0.0f)
        , spectralRolloff(0.0f)
        , hasEcho(false)
        , hasNoise(false)
        , overallQuality(0.0f)
        , dynamicRange(0.0f)
        , peakLevel(0.0f)
        , rmsLevel(0.0f)
        , hasClipping(false)
        , hasDropouts(false) {
        for (int i = 0; i < 13; ++i) {
            mfccFeatures[i] = 0.0f;
        }
    }
};

/**
 * Audio preprocessing result
 */
struct PreprocessingResult {
    std::vector<float> processedAudio;
    AudioQualityMetrics qualityMetrics;
    std::vector<PreprocessingType> appliedFilters;
    float processingLatencyMs;
    bool processingSuccessful;
    std::string processingInfo; // Debug information
    
    PreprocessingResult() 
        : processingLatencyMs(0.0f)
        , processingSuccessful(false) {}
};

/**
 * Noise reduction filter interface
 */
class NoiseReductionFilter {
public:
    virtual ~NoiseReductionFilter() = default;
    
    /**
     * Initialize the noise reduction filter
     * @param sampleRate Audio sample rate
     * @param frameSize Frame size for processing
     * @return true if initialization successful
     */
    virtual bool initialize(int sampleRate, size_t frameSize) = 0;
    
    /**
     * Apply noise reduction to audio
     * @param audioData Input audio samples
     * @param strength Noise reduction strength (0.0 to 1.0)
     * @return Processed audio samples
     */
    virtual std::vector<float> applyNoiseReduction(const std::vector<float>& audioData,
                                                  float strength = 0.5f) = 0;
    
    /**
     * Estimate noise profile from audio
     * @param audioData Audio samples for noise estimation
     */
    virtual void estimateNoiseProfile(const std::vector<float>& audioData) = 0;
    
    /**
     * Reset filter state
     */
    virtual void reset() = 0;
    
    /**
     * Check if filter is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Volume normalizer interface
 */
class VolumeNormalizer {
public:
    virtual ~VolumeNormalizer() = default;
    
    /**
     * Initialize the volume normalizer
     * @param sampleRate Audio sample rate
     * @return true if initialization successful
     */
    virtual bool initialize(int sampleRate) = 0;
    
    /**
     * Normalize audio volume
     * @param audioData Input audio samples
     * @param targetLevel Target RMS level
     * @return Normalized audio samples
     */
    virtual std::vector<float> normalizeVolume(const std::vector<float>& audioData,
                                              float targetLevel = -20.0f) = 0;
    
    /**
     * Apply automatic gain control
     * @param audioData Input audio samples
     * @param maxGain Maximum gain to apply (dB)
     * @return Gain-controlled audio samples
     */
    virtual std::vector<float> applyAutomaticGainControl(const std::vector<float>& audioData,
                                                        float maxGain = 20.0f) = 0;
    
    /**
     * Apply dynamic range compression
     * @param audioData Input audio samples
     * @param ratio Compression ratio
     * @param threshold Compression threshold (dB)
     * @return Compressed audio samples
     */
    virtual std::vector<float> applyCompression(const std::vector<float>& audioData,
                                               float ratio = 4.0f,
                                               float threshold = -20.0f) = 0;
    
    /**
     * Reset normalizer state
     */
    virtual void reset() = 0;
    
    /**
     * Check if normalizer is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Echo canceller interface
 */
class EchoCanceller {
public:
    virtual ~EchoCanceller() = default;
    
    /**
     * Initialize the echo canceller
     * @param sampleRate Audio sample rate
     * @param frameSize Frame size for processing
     * @return true if initialization successful
     */
    virtual bool initialize(int sampleRate, size_t frameSize) = 0;
    
    /**
     * Cancel echo from audio
     * @param audioData Input audio samples
     * @param referenceAudio Reference audio for echo cancellation (optional)
     * @return Echo-cancelled audio samples
     */
    virtual std::vector<float> cancelEcho(const std::vector<float>& audioData,
                                         const std::vector<float>& referenceAudio = {}) = 0;
    
    /**
     * Detect echo in audio
     * @param audioData Audio samples to analyze
     * @return Echo detection confidence (0.0 to 1.0)
     */
    virtual float detectEcho(const std::vector<float>& audioData) = 0;
    
    /**
     * Set echo cancellation strength
     * @param strength Cancellation strength (0.0 to 1.0)
     */
    virtual void setEchoCancellationStrength(float strength) = 0;
    
    /**
     * Reset canceller state
     */
    virtual void reset() = 0;
    
    /**
     * Check if canceller is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Spectral processor interface
 */
class SpectralProcessor {
public:
    virtual ~SpectralProcessor() = default;
    
    /**
     * Initialize the spectral processor
     * @param sampleRate Audio sample rate
     * @param fftSize FFT size for spectral analysis
     * @return true if initialization successful
     */
    virtual bool initialize(int sampleRate, size_t fftSize) = 0;
    
    /**
     * Apply spectral subtraction
     * @param audioData Input audio samples
     * @param alpha Spectral subtraction parameter
     * @return Processed audio samples
     */
    virtual std::vector<float> applySpectralSubtraction(const std::vector<float>& audioData,
                                                       float alpha = 2.0f) = 0;
    
    /**
     * Apply Wiener filtering
     * @param audioData Input audio samples
     * @param noiseEstimate Noise power spectrum estimate
     * @return Filtered audio samples
     */
    virtual std::vector<float> applyWienerFilter(const std::vector<float>& audioData,
                                                const std::vector<float>& noiseEstimate) = 0;
    
    /**
     * Compute power spectrum
     * @param audioData Input audio samples
     * @return Power spectrum
     */
    virtual std::vector<float> computePowerSpectrum(const std::vector<float>& audioData) = 0;
    
    /**
     * Estimate noise spectrum
     * @param audioData Audio samples for noise estimation
     * @return Noise power spectrum
     */
    virtual std::vector<float> estimateNoiseSpectrum(const std::vector<float>& audioData) = 0;
    
    /**
     * Reset processor state
     */
    virtual void reset() = 0;
    
    /**
     * Check if processor is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Audio quality analyzer interface
 */
class AudioQualityAnalyzer {
public:
    virtual ~AudioQualityAnalyzer() = default;
    
    /**
     * Initialize the quality analyzer
     * @param sampleRate Audio sample rate
     * @return true if initialization successful
     */
    virtual bool initialize(int sampleRate) = 0;
    
    /**
     * Analyze audio quality
     * @param audioData Audio samples to analyze
     * @return Quality metrics
     */
    virtual AudioQualityMetrics analyzeQuality(const std::vector<float>& audioData) = 0;
    
    /**
     * Calculate signal-to-noise ratio
     * @param audioData Audio samples
     * @return SNR in dB
     */
    virtual float calculateSNR(const std::vector<float>& audioData) = 0;
    
    /**
     * Detect audio artifacts
     * @param audioData Audio samples
     * @return Artifact detection results
     */
    virtual std::vector<std::string> detectArtifacts(const std::vector<float>& audioData) = 0;
    
    /**
     * Calculate overall quality score
     * @param metrics Quality metrics
     * @return Overall quality score (0.0 to 1.0)
     */
    virtual float calculateOverallQuality(const AudioQualityMetrics& metrics) = 0;
    
    /**
     * Check if analyzer is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Audio preprocessor interface
 */
class AudioPreprocessorInterface {
public:
    virtual ~AudioPreprocessorInterface() = default;
    
    /**
     * Initialize the audio preprocessor
     * @param config Preprocessing configuration
     * @return true if initialization successful
     */
    virtual bool initialize(const AudioPreprocessingConfig& config) = 0;
    
    /**
     * Preprocess audio with all enabled filters
     * @param audioData Input audio samples
     * @param sampleRate Audio sample rate
     * @return Preprocessing result
     */
    virtual PreprocessingResult preprocessAudio(const std::vector<float>& audioData,
                                               int sampleRate = 16000) = 0;
    
    /**
     * Analyze audio quality without preprocessing
     * @param audioData Audio samples to analyze
     * @param sampleRate Audio sample rate
     * @return Quality metrics
     */
    virtual AudioQualityMetrics analyzeAudioQuality(const std::vector<float>& audioData,
                                                    int sampleRate = 16000) = 0;
    
    /**
     * Set adaptive preprocessing mode
     * @param enabled true to enable adaptive mode
     */
    virtual void setAdaptiveMode(bool enabled) = 0;
    
    /**
     * Update preprocessing parameters based on quality metrics
     * @param metrics Audio quality metrics
     */
    virtual void updatePreprocessingParameters(const AudioQualityMetrics& metrics) = 0;
    
    /**
     * Process audio chunk for real-time streaming
     * @param chunk Audio chunk to process
     * @param sampleRate Audio sample rate
     * @return Processed audio chunk
     */
    virtual std::vector<float> processAudioChunk(const std::vector<float>& chunk,
                                                int sampleRate = 16000) = 0;
    
    /**
     * Reset preprocessing state
     */
    virtual void resetProcessingState() = 0;
    
    /**
     * Enable or disable specific preprocessing type
     * @param type Preprocessing type
     * @param enabled true to enable
     */
    virtual void setPreprocessingEnabled(PreprocessingType type, bool enabled) = 0;
    
    /**
     * Set preprocessing strength for a specific type
     * @param type Preprocessing type
     * @param strength Strength value (0.0 to 1.0)
     */
    virtual void setPreprocessingStrength(PreprocessingType type, float strength) = 0;
    
    /**
     * Get enabled preprocessing types
     * @return Vector of enabled preprocessing types
     */
    virtual std::vector<PreprocessingType> getEnabledPreprocessing() const = 0;
    
    /**
     * Update configuration
     * @param config New preprocessing configuration
     * @return true if update successful
     */
    virtual bool updateConfiguration(const AudioPreprocessingConfig& config) = 0;
    
    /**
     * Get current configuration
     * @return Current preprocessing configuration
     */
    virtual AudioPreprocessingConfig getCurrentConfiguration() const = 0;
    
    /**
     * Check if preprocessor is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
    
    /**
     * Get last error message
     * @return Last error message
     */
    virtual std::string getLastError() const = 0;
    
    /**
     * Get processing statistics
     * @return Statistics as JSON string
     */
    virtual std::string getProcessingStats() const = 0;
};

} // namespace advanced
} // namespace stt