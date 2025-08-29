#include "audio_quality_validator.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <fftw3.h>

namespace validation {

AudioQualityValidator::AudioQualityValidator() {
    initializeAnalyzers();
}

AudioQualityValidator::~AudioQualityValidator() {
    cleanup();
}

void AudioQualityValidator::initializeAnalyzers() {
    // Initialize FFT for spectral analysis
    fftSize_ = 1024;
    fftInput_ = (double*)fftw_malloc(sizeof(double) * fftSize_);
    fftOutput_ = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (fftSize_ / 2 + 1));
    fftPlan_ = fftw_plan_dft_r2c_1d(fftSize_, fftInput_, fftOutput_, FFTW_ESTIMATE);
    
    // Initialize quality thresholds
    snrThreshold_ = 20.0; // dB
    thresholdThreshold_ = -40.0; // dB
    distortionThreshold_ = 0.05; // 5%
    frequencyRangeMin_ = 80.0; // Hz
    frequencyRangeMax_ = 8000.0; // Hz
}

void AudioQualityValidator::cleanup() {
    if (fftPlan_) {
        fftw_destroy_plan(fftPlan_);
        fftPlan_ = nullptr;
    }
    if (fftInput_) {
        fftw_free(fftInput_);
        fftInput_ = nullptr;
    }
    if (fftOutput_) {
        fftw_free(fftOutput_);
        fftOutput_ = nullptr;
    }
    fftw_cleanup();
}

AudioQualityMetrics AudioQualityValidator::evaluateAudioQuality(
    const std::vector<float>& audioData,
    int sampleRate,
    const std::string& audioType
) {
    AudioQualityMetrics metrics;
    metrics.sampleRate = sampleRate;
    metrics.duration = static_cast<double>(audioData.size()) / sampleRate;
    metrics.audioType = audioType;
    metrics.evaluationTimestamp = std::chrono::system_clock::now();
    
    if (audioData.empty()) {
        metrics.overallQuality = 0.0;
        return metrics;
    }
    
    // Calculate basic audio statistics
    calculateBasicMetrics(audioData, metrics);
    
    // Perform spectral analysis
    performSpectralAnalysis(audioData, sampleRate, metrics);
    
    // Detect audio artifacts
    detectAudioArtifacts(audioData, sampleRate, metrics);
    
    // Calculate perceptual quality metrics
    calculatePerceptualMetrics(audioData, sampleRate, metrics);
    
    // Evaluate speech-specific quality (if applicable)
    if (audioType == "speech" || audioType == "tts") {
        evaluateSpeechQuality(audioData, sampleRate, metrics);
    }
    
    // Calculate overall quality score
    metrics.overallQuality = calculateOverallQuality(metrics);
    
    return metrics;
}

void AudioQualityValidator::calculateBasicMetrics(
    const std::vector<float>& audioData,
    AudioQualityMetrics& metrics
) {
    // Calculate RMS level
    double sumSquares = 0.0;
    for (float sample : audioData) {
        sumSquares += sample * sample;
    }
    metrics.rmsLevel = std::sqrt(sumSquares / audioData.size());
    metrics.rmsLevelDb = 20.0 * std::log10(metrics.rmsLevel + 1e-10);
    
    // Calculate peak level
    metrics.peakLevel = *std::max_element(audioData.begin(), audioData.end(),
        [](float a, float b) { return std::abs(a) < std::abs(b); });
    metrics.peakLevelDb = 20.0 * std::log10(std::abs(metrics.peakLevel) + 1e-10);
    
    // Calculate crest factor
    metrics.crestFactor = std::abs(metrics.peakLevel) / (metrics.rmsLevel + 1e-10);
    metrics.crestFactorDb = metrics.peakLevelDb - metrics.rmsLevelDb;
    
    // Calculate dynamic range
    std::vector<float> absAudio;
    absAudio.reserve(audioData.size());
    for (float sample : audioData) {
        absAudio.push_back(std::abs(sample));
    }
    
    std::sort(absAudio.begin(), absAudio.end());
    size_t percentile95 = static_cast<size_t>(absAudio.size() * 0.95);
    size_t percentile5 = static_cast<size_t>(absAudio.size() * 0.05);
    
    double high = absAudio[percentile95];
    double low = absAudio[percentile5];
    metrics.dynamicRange = 20.0 * std::log10((high + 1e-10) / (low + 1e-10));
    
    // Check for clipping
    int clippedSamples = 0;
    for (float sample : audioData) {
        if (std::abs(sample) > 0.99f) {
            clippedSamples++;
        }
    }
    metrics.clippingPercentage = static_cast<double>(clippedSamples) / audioData.size() * 100.0;
}

void AudioQualityValidator::performSpectralAnalysis(
    const std::vector<float>& audioData,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    if (audioData.size() < fftSize_) {
        return;
    }
    
    std::vector<double> powerSpectrum(fftSize_ / 2 + 1, 0.0);
    int numFrames = 0;
    
    // Process audio in overlapping frames
    for (size_t i = 0; i <= audioData.size() - fftSize_; i += fftSize_ / 2) {
        // Copy audio data to FFT input buffer
        for (size_t j = 0; j < fftSize_; ++j) {
            fftInput_[j] = audioData[i + j];
            
            // Apply Hanning window
            double window = 0.5 * (1.0 - std::cos(2.0 * M_PI * j / (fftSize_ - 1)));
            fftInput_[j] *= window;
        }
        
        // Perform FFT
        fftw_execute(fftPlan_);
        
        // Calculate power spectrum
        for (size_t j = 0; j < fftSize_ / 2 + 1; ++j) {
            double real = fftOutput_[j][0];
            double imag = fftOutput_[j][1];
            powerSpectrum[j] += (real * real + imag * imag);
        }
        
        numFrames++;
    }
    
    // Average power spectrum
    if (numFrames > 0) {
        for (double& power : powerSpectrum) {
            power /= numFrames;
        }
    }
    
    // Calculate spectral metrics
    calculateSpectralMetrics(powerSpectrum, sampleRate, metrics);
}

void AudioQualityValidator::calculateSpectralMetrics(
    const std::vector<double>& powerSpectrum,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    double freqBinSize = static_cast<double>(sampleRate) / (2.0 * (powerSpectrum.size() - 1));
    
    // Calculate spectral centroid
    double weightedSum = 0.0;
    double totalPower = 0.0;
    
    for (size_t i = 1; i < powerSpectrum.size(); ++i) {
        double frequency = i * freqBinSize;
        double power = powerSpectrum[i];
        
        weightedSum += frequency * power;
        totalPower += power;
    }
    
    metrics.spectralCentroid = totalPower > 0 ? weightedSum / totalPower : 0.0;
    
    // Calculate spectral bandwidth
    double variance = 0.0;
    for (size_t i = 1; i < powerSpectrum.size(); ++i) {
        double frequency = i * freqBinSize;
        double power = powerSpectrum[i];
        
        double diff = frequency - metrics.spectralCentroid;
        variance += diff * diff * power;
    }
    
    metrics.spectralBandwidth = totalPower > 0 ? std::sqrt(variance / totalPower) : 0.0;
    
    // Calculate spectral rolloff (95% of energy)
    double cumulativeEnergy = 0.0;
    double targetEnergy = totalPower * 0.95;
    
    for (size_t i = 1; i < powerSpectrum.size(); ++i) {
        cumulativeEnergy += powerSpectrum[i];
        if (cumulativeEnergy >= targetEnergy) {
            metrics.spectralRolloff = i * freqBinSize;
            break;
        }
    }
    
    // Calculate frequency response flatness
    calculateFrequencyResponseFlatness(powerSpectrum, freqBinSize, metrics);
    
    // Calculate harmonic distortion
    calculateHarmonicDistortion(powerSpectrum, freqBinSize, metrics);
}

void AudioQualityValidator::calculateFrequencyResponseFlatness(
    const std::vector<double>& powerSpectrum,
    double freqBinSize,
    AudioQualityMetrics& metrics
) {
    // Calculate flatness in speech frequency range (300-3400 Hz)
    size_t startBin = static_cast<size_t>(300.0 / freqBinSize);
    size_t endBin = static_cast<size_t>(3400.0 / freqBinSize);
    
    if (endBin >= powerSpectrum.size()) {
        endBin = powerSpectrum.size() - 1;
    }
    
    if (startBin >= endBin) {
        metrics.frequencyResponseFlatness = 0.0;
        return;
    }
    
    // Calculate geometric and arithmetic means
    double geometricMean = 0.0;
    double arithmeticMean = 0.0;
    int count = 0;
    
    for (size_t i = startBin; i <= endBin; ++i) {
        if (powerSpectrum[i] > 0) {
            geometricMean += std::log(powerSpectrum[i]);
            arithmeticMean += powerSpectrum[i];
            count++;
        }
    }
    
    if (count > 0) {
        geometricMean = std::exp(geometricMean / count);
        arithmeticMean /= count;
        
        // Spectral flatness measure
        metrics.frequencyResponseFlatness = geometricMean / (arithmeticMean + 1e-10);
    }
}

void AudioQualityValidator::calculateHarmonicDistortion(
    const std::vector<double>& powerSpectrum,
    double freqBinSize,
    AudioQualityMetrics& metrics
) {
    // Find fundamental frequency (simplified - assumes single tone)
    size_t maxBin = 1;
    double maxPower = powerSpectrum[1];
    
    for (size_t i = 2; i < powerSpectrum.size() / 4; ++i) {
        if (powerSpectrum[i] > maxPower) {
            maxPower = powerSpectrum[i];
            maxBin = i;
        }
    }
    
    double fundamentalFreq = maxBin * freqBinSize;
    double fundamentalPower = maxPower;
    
    // Calculate harmonic power
    double harmonicPower = 0.0;
    for (int harmonic = 2; harmonic <= 5; ++harmonic) {
        double harmonicFreq = fundamentalFreq * harmonic;
        size_t harmonicBin = static_cast<size_t>(harmonicFreq / freqBinSize);
        
        if (harmonicBin < powerSpectrum.size()) {
            harmonicPower += powerSpectrum[harmonicBin];
        }
    }
    
    // Total Harmonic Distortion (THD)
    metrics.totalHarmonicDistortion = harmonicPower / (fundamentalPower + 1e-10);
}

void AudioQualityValidator::detectAudioArtifacts(
    const std::vector<float>& audioData,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    // Detect clicks and pops
    detectClicksAndPops(audioData, sampleRate, metrics);
    
    // Detect dropouts and silence
    detectDropoutsAndSilence(audioData, sampleRate, metrics);
    
    // Detect noise and hum
    detectNoiseAndHum(audioData, sampleRate, metrics);
    
    // Detect distortion
    detectDistortion(audioData, metrics);
}

void AudioQualityValidator::detectClicksAndPops(
    const std::vector<float>& audioData,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    if (audioData.size() < 3) return;
    
    int clickCount = 0;
    double clickThreshold = 0.1; // Adjust based on requirements
    
    for (size_t i = 1; i < audioData.size() - 1; ++i) {
        // Calculate second derivative (acceleration)
        double secondDerivative = audioData[i+1] - 2*audioData[i] + audioData[i-1];
        
        if (std::abs(secondDerivative) > clickThreshold) {
            clickCount++;
        }
    }
    
    metrics.clicksAndPops = static_cast<double>(clickCount) / (audioData.size() / sampleRate);
}

void AudioQualityValidator::detectDropoutsAndSilence(
    const std::vector<float>& audioData,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    double silenceThreshold = 0.001; // -60 dB
    int consecutiveSilentSamples = 0;
    int maxConsecutiveSilent = 0;
    int totalSilentSamples = 0;
    
    for (float sample : audioData) {
        if (std::abs(sample) < silenceThreshold) {
            consecutiveSilentSamples++;
            totalSilentSamples++;
        } else {
            maxConsecutiveSilent = std::max(maxConsecutiveSilent, consecutiveSilentSamples);
            consecutiveSilentSamples = 0;
        }
    }
    
    maxConsecutiveSilent = std::max(maxConsecutiveSilent, consecutiveSilentSamples);
    
    metrics.silencePercentage = static_cast<double>(totalSilentSamples) / audioData.size() * 100.0;
    metrics.maxSilenceDuration = static_cast<double>(maxConsecutiveSilent) / sampleRate;
}

void AudioQualityValidator::detectNoiseAndHum(
    const std::vector<float>& audioData,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    // Simple noise floor estimation
    std::vector<float> absAudio;
    absAudio.reserve(audioData.size());
    
    for (float sample : audioData) {
        absAudio.push_back(std::abs(sample));
    }
    
    std::sort(absAudio.begin(), absAudio.end());
    
    // Estimate noise floor as 10th percentile
    size_t noiseIndex = static_cast<size_t>(absAudio.size() * 0.1);
    double noiseFloor = absAudio[noiseIndex];
    
    metrics.noiseFloor = 20.0 * std::log10(noiseFloor + 1e-10);
    
    // Calculate SNR
    double signalLevel = metrics.rmsLevel;
    metrics.signalToNoiseRatio = 20.0 * std::log10((signalLevel + 1e-10) / (noiseFloor + 1e-10));
}

void AudioQualityValidator::detectDistortion(
    const std::vector<float>& audioData,
    AudioQualityMetrics& metrics
) {
    // Calculate zero-crossing rate as a simple distortion indicator
    int zeroCrossings = 0;
    
    for (size_t i = 1; i < audioData.size(); ++i) {
        if ((audioData[i-1] >= 0 && audioData[i] < 0) ||
            (audioData[i-1] < 0 && audioData[i] >= 0)) {
            zeroCrossings++;
        }
    }
    
    metrics.zeroCrossingRate = static_cast<double>(zeroCrossings) / audioData.size();
    
    // High zero-crossing rate can indicate distortion or noise
    if (metrics.zeroCrossingRate > 0.1) {
        metrics.artifacts.push_back({
            AudioArtifactType::DISTORTION,
            "High zero-crossing rate detected",
            0.6
        });
    }
}

void AudioQualityValidator::calculatePerceptualMetrics(
    const std::vector<float>& audioData,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    // Calculate loudness (simplified LUFS approximation)
    calculateLoudness(audioData, sampleRate, metrics);
    
    // Calculate sharpness and roughness (simplified)
    calculateSharpnessAndRoughness(audioData, sampleRate, metrics);
}

void AudioQualityValidator::calculateLoudness(
    const std::vector<float>& audioData,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    // Simplified loudness calculation (not full ITU-R BS.1770)
    // Apply K-weighting filter approximation
    
    double sum = 0.0;
    for (float sample : audioData) {
        // Simplified pre-filter (high-pass around 38 Hz)
        // In practice, you'd implement proper K-weighting
        sum += sample * sample;
    }
    
    double meanSquare = sum / audioData.size();
    metrics.loudness = -0.691 + 10.0 * std::log10(meanSquare + 1e-10);
}

void AudioQualityValidator::calculateSharpnessAndRoughness(
    const std::vector<float>& audioData,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    // Simplified sharpness calculation based on spectral centroid
    metrics.sharpness = metrics.spectralCentroid / 1000.0; // Normalize to kHz
    
    // Simplified roughness based on spectral irregularity
    metrics.roughness = 1.0 - metrics.frequencyResponseFlatness;
}

void AudioQualityValidator::evaluateSpeechQuality(
    const std::vector<float>& audioData,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    // Calculate speech-specific metrics
    calculateSpeechIntelligibility(audioData, sampleRate, metrics);
    calculateSpeechNaturalness(audioData, sampleRate, metrics);
    
    // Detect speech artifacts
    detectSpeechArtifacts(audioData, sampleRate, metrics);
}

void AudioQualityValidator::calculateSpeechIntelligibility(
    const std::vector<float>& audioData,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    // Speech Intelligibility Index (SII) approximation
    // Based on frequency band importance for speech understanding
    
    struct FrequencyBand {
        double lowFreq, highFreq, importance;
    };
    
    std::vector<FrequencyBand> speechBands = {
        {200, 450, 0.0617},
        {450, 720, 0.0802},
        {720, 1080, 0.0928},
        {1080, 1550, 0.1016},
        {1550, 2250, 0.1031},
        {2250, 3250, 0.0985},
        {3250, 4700, 0.0868},
        {4700, 6800, 0.0688},
        {6800, 9800, 0.0454}
    };
    
    double totalImportance = 0.0;
    double weightedQuality = 0.0;
    
    // This is a simplified calculation - proper SII requires more complex analysis
    for (const auto& band : speechBands) {
        // Estimate band quality based on SNR in that frequency range
        double bandQuality = std::min(1.0, std::max(0.0, 
            (metrics.signalToNoiseRatio - 5.0) / 25.0)); // Normalize SNR to 0-1
        
        weightedQuality += bandQuality * band.importance;
        totalImportance += band.importance;
    }
    
    metrics.speechIntelligibility = totalImportance > 0 ? weightedQuality / totalImportance : 0.0;
}

void AudioQualityValidator::calculateSpeechNaturalness(
    const std::vector<float>& audioData,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    // Simplified speech naturalness based on various factors
    double naturalness = 1.0;
    
    // Penalize for artifacts
    if (metrics.clippingPercentage > 1.0) {
        naturalness -= 0.3;
    }
    
    if (metrics.totalHarmonicDistortion > 0.1) {
        naturalness -= 0.2;
    }
    
    if (metrics.signalToNoiseRatio < 20.0) {
        naturalness -= 0.2;
    }
    
    // Penalize for unnatural spectral characteristics
    if (metrics.spectralCentroid < 500 || metrics.spectralCentroid > 4000) {
        naturalness -= 0.1;
    }
    
    metrics.speechNaturalness = std::max(0.0, naturalness);
}

void AudioQualityValidator::detectSpeechArtifacts(
    const std::vector<float>& audioData,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    // Detect robotic/synthetic artifacts
    if (metrics.frequencyResponseFlatness < 0.1) {
        metrics.artifacts.push_back({
            AudioArtifactType::ROBOTIC_VOICE,
            "Unnatural frequency response detected",
            0.5
        });
    }
    
    // Detect breathing artifacts
    detectBreathingArtifacts(audioData, sampleRate, metrics);
    
    // Detect pitch artifacts
    detectPitchArtifacts(audioData, sampleRate, metrics);
}

void AudioQualityValidator::detectBreathingArtifacts(
    const std::vector<float>& audioData,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    // Simple breathing detection based on low-frequency energy
    // This is a very simplified approach
    
    if (audioData.size() < sampleRate) return; // Need at least 1 second
    
    int frameSize = sampleRate / 10; // 100ms frames
    int breathCount = 0;
    
    for (size_t i = 0; i < audioData.size() - frameSize; i += frameSize) {
        double lowFreqEnergy = 0.0;
        double totalEnergy = 0.0;
        
        for (size_t j = i; j < i + frameSize; ++j) {
            double sample = audioData[j];
            totalEnergy += sample * sample;
            
            // Simple low-pass filter approximation
            if (j > i) {
                double filtered = 0.1 * sample + 0.9 * audioData[j-1];
                lowFreqEnergy += filtered * filtered;
            }
        }
        
        if (totalEnergy > 0 && (lowFreqEnergy / totalEnergy) > 0.8) {
            breathCount++;
        }
    }
    
    if (breathCount > 2) {
        metrics.artifacts.push_back({
            AudioArtifactType::BREATHING_NOISE,
            "Breathing artifacts detected",
            0.3
        });
    }
}

void AudioQualityValidator::detectPitchArtifacts(
    const std::vector<float>& audioData,
    int sampleRate,
    AudioQualityMetrics& metrics
) {
    // Detect unnatural pitch variations
    // This would require more sophisticated pitch tracking in practice
    
    if (metrics.spectralCentroid > 0) {
        // Check for extreme pitch values
        if (metrics.spectralCentroid < 80 || metrics.spectralCentroid > 500) {
            metrics.artifacts.push_back({
                AudioArtifactType::PITCH_ARTIFACTS,
                "Unnatural pitch detected",
                0.4
            });
        }
    }
}

double AudioQualityValidator::calculateOverallQuality(const AudioQualityMetrics& metrics) {
    double quality = 1.0;
    
    // Penalize for poor SNR
    if (metrics.signalToNoiseRatio < snrThreshold_) {
        quality -= (snrThreshold_ - metrics.signalToNoiseRatio) / snrThreshold_ * 0.3;
    }
    
    // Penalize for clipping
    if (metrics.clippingPercentage > 0.1) {
        quality -= metrics.clippingPercentage / 100.0 * 0.4;
    }
    
    // Penalize for distortion
    if (metrics.totalHarmonicDistortion > distortionThreshold_) {
        quality -= (metrics.totalHarmonicDistortion - distortionThreshold_) * 0.5;
    }
    
    // Penalize for artifacts
    for (const auto& artifact : metrics.artifacts) {
        quality -= artifact.severity * 0.1;
    }
    
    // Bonus for good speech quality (if applicable)
    if (metrics.speechIntelligibility > 0) {
        quality = quality * 0.7 + metrics.speechIntelligibility * 0.3;
    }
    
    if (metrics.speechNaturalness > 0) {
        quality = quality * 0.8 + metrics.speechNaturalness * 0.2;
    }
    
    return std::max(0.0, std::min(1.0, quality));
}

AudioValidationReport AudioQualityValidator::generateValidationReport(
    const std::vector<AudioQualityMetrics>& evaluations
) {
    AudioValidationReport report;
    report.totalEvaluations = evaluations.size();
    report.timestamp = std::chrono::system_clock::now();
    
    if (evaluations.empty()) {
        return report;
    }
    
    // Calculate aggregate statistics
    double totalQuality = 0.0;
    double totalSNR = 0.0;
    double totalTHD = 0.0;
    double totalIntelligibility = 0.0;
    double totalNaturalness = 0.0;
    
    int snrCount = 0;
    int thdCount = 0;
    int intelligibilityCount = 0;
    int naturalnessCount = 0;
    
    std::map<AudioArtifactType, int> artifactCounts;
    
    for (const auto& eval : evaluations) {
        totalQuality += eval.overallQuality;
        
        if (eval.signalToNoiseRatio > -100) {
            totalSNR += eval.signalToNoiseRatio;
            snrCount++;
        }
        
        if (eval.totalHarmonicDistortion >= 0) {
            totalTHD += eval.totalHarmonicDistortion;
            thdCount++;
        }
        
        if (eval.speechIntelligibility >= 0) {
            totalIntelligibility += eval.speechIntelligibility;
            intelligibilityCount++;
        }
        
        if (eval.speechNaturalness >= 0) {
            totalNaturalness += eval.speechNaturalness;
            naturalnessCount++;
        }
        
        for (const auto& artifact : eval.artifacts) {
            artifactCounts[artifact.type]++;
        }
    }
    
    report.averageQuality = totalQuality / evaluations.size();
    report.averageSNR = snrCount > 0 ? totalSNR / snrCount : -100.0;
    report.averageTHD = thdCount > 0 ? totalTHD / thdCount : -1.0;
    report.averageIntelligibility = intelligibilityCount > 0 ? totalIntelligibility / intelligibilityCount : -1.0;
    report.averageNaturalness = naturalnessCount > 0 ? totalNaturalness / naturalnessCount : -1.0;
    
    // Quality distribution
    int excellent = 0, good = 0, fair = 0, poor = 0;
    for (const auto& eval : evaluations) {
        if (eval.overallQuality >= 0.8) excellent++;
        else if (eval.overallQuality >= 0.6) good++;
        else if (eval.overallQuality >= 0.4) fair++;
        else poor++;
    }
    
    report.qualityDistribution = {
        {"excellent", excellent},
        {"good", good},
        {"fair", fair},
        {"poor", poor}
    };
    
    // Artifact analysis
    for (const auto& artifactPair : artifactCounts) {
        report.artifactAnalysis[artifactPair.first] = {
            artifactPair.second,
            static_cast<double>(artifactPair.second) / evaluations.size()
        };
    }
    
    // Generate recommendations
    if (report.averageQuality < 0.6) {
        report.recommendations.push_back("Overall audio quality is below acceptable threshold. Review audio processing pipeline.");
    }
    
    if (report.averageSNR < 20.0) {
        report.recommendations.push_back("Signal-to-noise ratio is low. Consider noise reduction or better recording conditions.");
    }
    
    if (report.averageTHD > 0.05) {
        report.recommendations.push_back("High harmonic distortion detected. Check for overdriving or processing artifacts.");
    }
    
    if (artifactCounts[AudioArtifactType::CLIPPING] > evaluations.size() * 0.1) {
        report.recommendations.push_back("Frequent clipping detected. Reduce input levels or improve dynamic range handling.");
    }
    
    return report;
}

} // namespace validation