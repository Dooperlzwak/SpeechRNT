#pragma once

#include "audio/audio_quality_analyzer.hpp"
#include <vector>
#include <memory>
#include <map>
#include <string>
#include <chrono>
#include <atomic>
#include <mutex>

namespace audio {

// Audio processing parameters that can be adapted
struct AdaptiveProcessingParams {
    // Noise reduction parameters
    float noiseReductionStrength;      // 0.0 to 1.0
    float spectralSubtractionAlpha;    // Spectral subtraction over-subtraction factor
    float wienerFilterBeta;            // Wiener filter smoothing factor
    
    // Volume normalization parameters
    float targetRMS;                   // Target RMS level
    float compressionRatio;            // Dynamic range compression ratio
    float attackTime;                  // Attack time in seconds
    float releaseTime;                 // Release time in seconds
    
    // Echo cancellation parameters
    float echoSuppressionStrength;     // 0.0 to 1.0
    size_t adaptiveFilterLength;       // Adaptive filter length
    float convergenceRate;             // LMS convergence rate
    
    // Multi-channel processing
    int selectedChannel;               // -1 for auto-select, 0+ for specific channel
    float channelMixingWeight;         // Weight for channel mixing
    bool enableChannelSelection;       // Enable intelligent channel selection
    
    // Processing pipeline optimization
    bool enablePreEmphasis;            // Pre-emphasis filter
    float preEmphasisCoeff;            // Pre-emphasis coefficient
    bool enablePostProcessing;         // Post-processing enhancement
    
    AdaptiveProcessingParams()
        : noiseReductionStrength(0.5f), spectralSubtractionAlpha(2.0f), wienerFilterBeta(0.1f),
          targetRMS(0.1f), compressionRatio(2.0f), attackTime(0.01f), releaseTime(0.1f),
          echoSuppressionStrength(0.7f), adaptiveFilterLength(512), convergenceRate(0.01f),
          selectedChannel(-1), channelMixingWeight(0.5f), enableChannelSelection(true),
          enablePreEmphasis(false), preEmphasisCoeff(0.97f), enablePostProcessing(true) {}
};

// Audio type classification for processing optimization
enum class AudioType {
    UNKNOWN,
    SPEECH,
    MUSIC,
    NOISE,
    MIXED,
    SILENCE
};

// Audio characteristics detected for adaptation
struct AudioCharacteristics {
    AudioType type;
    float speechProbability;           // 0.0 to 1.0
    float musicProbability;            // 0.0 to 1.0
    float noiseProbability;            // 0.0 to 1.0
    
    // Environmental characteristics
    float reverbLevel;                 // Reverb/echo level
    float backgroundNoiseLevel;        // Background noise level
    float dynamicRange;                // Dynamic range in dB
    
    // Channel characteristics (for multi-channel)
    std::vector<float> channelQualities; // Quality score per channel
    int recommendedChannel;            // Best channel for processing
    
    // Temporal characteristics
    float stationarity;                // How stationary the signal is
    float periodicityStrength;         // Periodicity strength
    
    AudioCharacteristics()
        : type(AudioType::UNKNOWN), speechProbability(0.0f), musicProbability(0.0f),
          noiseProbability(0.0f), reverbLevel(0.0f), backgroundNoiseLevel(0.0f),
          dynamicRange(0.0f), recommendedChannel(-1), stationarity(0.0f),
          periodicityStrength(0.0f) {}
};

// Real-time quality monitoring results
struct QualityMonitoringResult {
    AudioQualityMetrics currentQuality;
    AudioCharacteristics characteristics;
    AdaptiveProcessingParams recommendedParams;
    bool parametersChanged;
    std::chrono::steady_clock::time_point timestamp;
    
    QualityMonitoringResult()
        : parametersChanged(false) {
        timestamp = std::chrono::steady_clock::now();
    }
};

// Adaptive audio processor class
class AdaptiveAudioProcessor {
public:
    explicit AdaptiveAudioProcessor(int sampleRate = 16000, size_t channels = 1);
    ~AdaptiveAudioProcessor() = default;
    
    // Main processing functions
    std::vector<float> processAudio(const std::vector<float>& audioData);
    std::vector<std::vector<float>> processMultiChannelAudio(const std::vector<std::vector<float>>& audioData);
    
    // Real-time processing
    void initializeRealTimeProcessing(size_t bufferSize);
    std::vector<float> processRealTimeChunk(const std::vector<float>& audioChunk);
    void resetRealTimeState();
    
    // Adaptive parameter management
    void enableAdaptiveMode(bool enabled) { adaptiveModeEnabled_ = enabled; }
    bool isAdaptiveModeEnabled() const { return adaptiveModeEnabled_; }
    
    void setProcessingParams(const AdaptiveProcessingParams& params);
    AdaptiveProcessingParams getProcessingParams() const;
    
    // Audio characteristics analysis
    AudioCharacteristics analyzeAudioCharacteristics(const std::vector<float>& audioData);
    AudioType classifyAudioType(const std::vector<float>& audioData);
    
    // Quality monitoring
    void enableQualityMonitoring(bool enabled) { qualityMonitoringEnabled_ = enabled; }
    QualityMonitoringResult getLatestQualityReport() const;
    std::vector<QualityMonitoringResult> getQualityHistory(size_t maxEntries = 100) const;
    
    // Multi-channel processing
    void setChannelCount(size_t channels);
    size_t getChannelCount() const { return channelCount_; }
    int selectBestChannel(const std::vector<std::vector<float>>& audioData);
    std::vector<float> mixChannels(const std::vector<std::vector<float>>& audioData, const std::vector<float>& weights);
    
    // Pipeline optimization
    void optimizePipelineForAudioType(AudioType type);
    void setOptimizationPreset(const std::string& presetName);
    std::vector<std::string> getAvailablePresets() const;
    
    // Configuration and statistics
    void setSampleRate(int sampleRate) { sampleRate_ = sampleRate; }
    int getSampleRate() const { return sampleRate_; }
    
    struct ProcessingStatistics {
        uint64_t totalSamplesProcessed;
        uint64_t totalChunksProcessed;
        double averageProcessingTime;
        double adaptationCount;
        std::map<AudioType, uint64_t> audioTypeDistribution;
        std::chrono::steady_clock::time_point lastProcessingTime;
    };
    
    ProcessingStatistics getStatistics() const;
    void resetStatistics();
    
private:
    // Core configuration
    int sampleRate_;
    size_t channelCount_;
    std::atomic<bool> adaptiveModeEnabled_;
    std::atomic<bool> qualityMonitoringEnabled_;
    
    // Processing parameters
    mutable std::mutex paramsMutex_;
    AdaptiveProcessingParams currentParams_;
    
    // Quality analysis
    std::unique_ptr<AudioQualityAnalyzer> qualityAnalyzer_;
    
    // Real-time processing state
    std::vector<float> processingBuffer_;
    size_t bufferPosition_;
    bool realTimeInitialized_;
    
    // Quality monitoring
    mutable std::mutex qualityMutex_;
    std::vector<QualityMonitoringResult> qualityHistory_;
    static constexpr size_t MAX_QUALITY_HISTORY = 1000;
    
    // Statistics
    mutable std::mutex statsMutex_;
    ProcessingStatistics stats_;
    
    // Processing components
    class NoiseReductionProcessor;
    class VolumeNormalizer;
    class EchoCanceller;
    class MultiChannelProcessor;
    class AudioTypeClassifier;
    
    std::unique_ptr<NoiseReductionProcessor> noiseReducer_;
    std::unique_ptr<VolumeNormalizer> volumeNormalizer_;
    std::unique_ptr<EchoCanceller> echoCanceller_;
    std::unique_ptr<MultiChannelProcessor> multiChannelProcessor_;
    std::unique_ptr<AudioTypeClassifier> audioClassifier_;
    
    // Optimization presets
    std::map<std::string, AdaptiveProcessingParams> optimizationPresets_;
    
    // Private methods
    void initializeProcessingComponents();
    void initializeOptimizationPresets();
    
    // Adaptive parameter adjustment
    AdaptiveProcessingParams adaptParameters(const AudioQualityMetrics& quality, 
                                           const AudioCharacteristics& characteristics);
    void updateParametersBasedOnQuality(const AudioQualityMetrics& quality);
    void updateParametersBasedOnCharacteristics(const AudioCharacteristics& characteristics);
    
    // Audio processing pipeline
    std::vector<float> applyNoiseReduction(const std::vector<float>& audioData);
    std::vector<float> applyVolumeNormalization(const std::vector<float>& audioData);
    std::vector<float> applyEchoCancellation(const std::vector<float>& audioData);
    std::vector<float> applyPreEmphasis(const std::vector<float>& audioData);
    std::vector<float> applyPostProcessing(const std::vector<float>& audioData);
    
    // Multi-channel processing helpers
    std::vector<float> selectChannelIntelligently(const std::vector<std::vector<float>>& audioData);
    std::vector<float> evaluateChannelQuality(const std::vector<float>& channelData);
    
    // Audio characteristics analysis helpers
    float calculateSpeechProbability(const std::vector<float>& audioData);
    float calculateMusicProbability(const std::vector<float>& audioData);
    float calculateNoiseProbability(const std::vector<float>& audioData);
    float calculateReverbLevel(const std::vector<float>& audioData);
    float calculateStationarity(const std::vector<float>& audioData);
    float calculatePeriodicity(const std::vector<float>& audioData);
    
    // Quality monitoring helpers
    void updateQualityHistory(const QualityMonitoringResult& result);
    bool shouldAdaptParameters(const AudioQualityMetrics& quality);
    
    // Statistics helpers
    void updateStatistics(size_t samplesProcessed, double processingTime, AudioType detectedType);
};

// Specialized processing components (forward declarations implemented in .cpp)

class AdaptiveAudioProcessor::NoiseReductionProcessor {
public:
    NoiseReductionProcessor(int sampleRate);
    std::vector<float> processSpectralSubtraction(const std::vector<float>& audioData, float alpha);
    std::vector<float> processWienerFilter(const std::vector<float>& audioData, float beta);
    void updateNoiseProfile(const std::vector<float>& noiseData);
    
private:
    int sampleRate_;
    std::vector<float> noiseProfile_;
    std::vector<float> previousFrame_;
};

class AdaptiveAudioProcessor::VolumeNormalizer {
public:
    VolumeNormalizer(int sampleRate);
    std::vector<float> processAGC(const std::vector<float>& audioData, float targetRMS);
    std::vector<float> processCompression(const std::vector<float>& audioData, float ratio, float attack, float release);
    void resetState();
    
private:
    int sampleRate_;
    float currentGain_;
    float currentRMS_;
    std::vector<float> delayBuffer_;
};

class AdaptiveAudioProcessor::EchoCanceller {
public:
    EchoCanceller(int sampleRate, size_t filterLength);
    std::vector<float> processLMS(const std::vector<float>& audioData, float convergenceRate);
    std::vector<float> processNLMS(const std::vector<float>& audioData, float convergenceRate);
    void resetAdaptiveFilter();
    
private:
    int sampleRate_;
    size_t filterLength_;
    std::vector<float> adaptiveFilter_;
    std::vector<float> inputHistory_;
};

class AdaptiveAudioProcessor::MultiChannelProcessor {
public:
    MultiChannelProcessor(size_t channelCount);
    std::vector<float> selectBestChannel(const std::vector<std::vector<float>>& audioData);
    std::vector<float> mixChannelsAdaptively(const std::vector<std::vector<float>>& audioData);
    std::vector<float> evaluateChannelQualities(const std::vector<std::vector<float>>& audioData);
    
private:
    size_t channelCount_;
    std::vector<float> channelWeights_;
};

class AdaptiveAudioProcessor::AudioTypeClassifier {
public:
    AudioTypeClassifier(int sampleRate);
    AudioType classifyAudio(const std::vector<float>& audioData);
    std::vector<float> calculateTypeProbabilities(const std::vector<float>& audioData);
    
private:
    int sampleRate_;
    
    // Classification features
    struct ClassificationFeatures {
        float spectralCentroid;
        float spectralBandwidth;
        float spectralRolloff;
        float zeroCrossingRate;
        float mfccVariance;
        float harmonicRatio;
        float rhythmStrength;
    };
    
    ClassificationFeatures extractFeatures(const std::vector<float>& audioData);
    AudioType classifyFromFeatures(const ClassificationFeatures& features);
};

} // namespace audio