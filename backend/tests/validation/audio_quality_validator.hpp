#pragma once

#include <vector>
#include <string>
#include <map>
#include <chrono>
#include <fftw3.h>

namespace validation {

enum class AudioArtifactType {
    CLIPPING,
    DISTORTION,
    NOISE,
    DROPOUTS,
    CLICKS_POPS,
    ROBOTIC_VOICE,
    BREATHING_NOISE,
    PITCH_ARTIFACTS,
    FREQUENCY_RESPONSE_ISSUES
};

struct AudioArtifact {
    AudioArtifactType type;
    std::string description;
    double severity; // 0.0 to 1.0
};

struct AudioQualityMetrics {
    // Basic audio metrics
    double rmsLevel = 0.0;
    double rmsLevelDb = -100.0;
    double peakLevel = 0.0;
    double peakLevelDb = -100.0;
    double crestFactor = 0.0;
    double crestFactorDb = 0.0;
    double dynamicRange = 0.0;
    double clippingPercentage = 0.0;
    
    // Spectral metrics
    double spectralCentroid = 0.0;
    double spectralBandwidth = 0.0;
    double spectralRolloff = 0.0;
    double frequencyResponseFlatness = 0.0;
    double totalHarmonicDistortion = 0.0;
    
    // Noise and artifacts
    double signalToNoiseRatio = -100.0;
    double noiseFloor = -100.0;
    double clicksAndPops = 0.0; // per second
    double silencePercentage = 0.0;
    double maxSilenceDuration = 0.0;
    double zeroCrossingRate = 0.0;
    
    // Perceptual metrics
    double loudness = -100.0; // LUFS approximation
    double sharpness = 0.0;
    double roughness = 0.0;
    
    // Speech-specific metrics
    double speechIntelligibility = -1.0; // SII approximation
    double speechNaturalness = -1.0;
    
    // Overall quality
    double overallQuality = 0.0;
    
    // Detected artifacts
    std::vector<AudioArtifact> artifacts;
    
    // Metadata
    int sampleRate = 0;
    double duration = 0.0;
    std::string audioType; // "speech", "tts", "music", "noise"
    std::chrono::system_clock::time_point evaluationTimestamp;
};

struct AudioValidationReport {
    size_t totalEvaluations = 0;
    double averageQuality = 0.0;
    double averageSNR = -100.0;
    double averageTHD = -1.0;
    double averageIntelligibility = -1.0;
    double averageNaturalness = -1.0;
    
    std::map<std::string, int> qualityDistribution;
    std::map<AudioArtifactType, std::pair<int, double>> artifactAnalysis; // count, percentage
    
    std::vector<std::string> recommendations;
    std::chrono::system_clock::time_point timestamp;
};

class AudioQualityValidator {
public:
    AudioQualityValidator();
    ~AudioQualityValidator();
    
    // Main evaluation function
    AudioQualityMetrics evaluateAudioQuality(
        const std::vector<float>& audioData,
        int sampleRate,
        const std::string& audioType = "speech"
    );
    
    // Batch evaluation
    std::vector<AudioQualityMetrics> evaluateAudioBatch(
        const std::vector<std::pair<std::vector<float>, int>>& audioBatch,
        const std::string& audioType = "speech"
    );
    
    // Generate validation report
    AudioValidationReport generateValidationReport(
        const std::vector<AudioQualityMetrics>& evaluations
    );
    
    // Configuration
    void setQualityThresholds(double snr, double thd, double intelligibility);
    void setFrequencyRange(double minFreq, double maxFreq);
    
private:
    void initializeAnalyzers();
    void cleanup();
    
    // Basic metrics calculation
    void calculateBasicMetrics(const std::vector<float>& audioData, AudioQualityMetrics& metrics);
    
    // Spectral analysis
    void performSpectralAnalysis(const std::vector<float>& audioData, int sampleRate, AudioQualityMetrics& metrics);
    void calculateSpectralMetrics(const std::vector<double>& powerSpectrum, int sampleRate, AudioQualityMetrics& metrics);
    void calculateFrequencyResponseFlatness(const std::vector<double>& powerSpectrum, double freqBinSize, AudioQualityMetrics& metrics);
    void calculateHarmonicDistortion(const std::vector<double>& powerSpectrum, double freqBinSize, AudioQualityMetrics& metrics);
    
    // Artifact detection
    void detectAudioArtifacts(const std::vector<float>& audioData, int sampleRate, AudioQualityMetrics& metrics);
    void detectClicksAndPops(const std::vector<float>& audioData, int sampleRate, AudioQualityMetrics& metrics);
    void detectDropoutsAndSilence(const std::vector<float>& audioData, int sampleRate, AudioQualityMetrics& metrics);
    void detectNoiseAndHum(const std::vector<float>& audioData, int sampleRate, AudioQualityMetrics& metrics);
    void detectDistortion(const std::vector<float>& audioData, AudioQualityMetrics& metrics);
    
    // Perceptual metrics
    void calculatePerceptualMetrics(const std::vector<float>& audioData, int sampleRate, AudioQualityMetrics& metrics);
    void calculateLoudness(const std::vector<float>& audioData, int sampleRate, AudioQualityMetrics& metrics);
    void calculateSharpnessAndRoughness(const std::vector<float>& audioData, int sampleRate, AudioQualityMetrics& metrics);
    
    // Speech-specific evaluation
    void evaluateSpeechQuality(const std::vector<float>& audioData, int sampleRate, AudioQualityMetrics& metrics);
    void calculateSpeechIntelligibility(const std::vector<float>& audioData, int sampleRate, AudioQualityMetrics& metrics);
    void calculateSpeechNaturalness(const std::vector<float>& audioData, int sampleRate, AudioQualityMetrics& metrics);
    void detectSpeechArtifacts(const std::vector<float>& audioData, int sampleRate, AudioQualityMetrics& metrics);
    void detectBreathingArtifacts(const std::vector<float>& audioData, int sampleRate, AudioQualityMetrics& metrics);
    void detectPitchArtifacts(const std::vector<float>& audioData, int sampleRate, AudioQualityMetrics& metrics);
    
    // Overall quality calculation
    double calculateOverallQuality(const AudioQualityMetrics& metrics);
    
    // FFT resources
    size_t fftSize_;
    double* fftInput_;
    fftw_complex* fftOutput_;
    fftw_plan fftPlan_;
    
    // Quality thresholds
    double snrThreshold_;
    double thresholdThreshold_;
    double distortionThreshold_;
    double frequencyRangeMin_;
    double frequencyRangeMax_;
};

} // namespace validation