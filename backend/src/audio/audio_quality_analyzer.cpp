#include "audio/audio_quality_analyzer.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <fftw3.h>

namespace audio {

AudioQualityAnalyzer::AudioQualityAnalyzer(const AudioQualityConfig& config)
    : config_(config), bufferPosition_(0), realTimeInitialized_(false) {
    initializeFFT();
    initializeWindowFunction();
}

AudioQualityMetrics AudioQualityAnalyzer::analyzeQuality(const std::vector<float>& audioData, int sampleRate) {
    AudioQualityMetrics metrics;
    metrics.timestamp = std::chrono::steady_clock::now();
    metrics.sampleCount = audioData.size();
    metrics.durationSeconds = static_cast<float>(audioData.size()) / sampleRate;
    
    if (audioData.empty()) {
        return metrics;
    }
    
    // Calculate basic metrics
    metrics.signalToNoiseRatio = calculateSNR(audioData);
    metrics.zeroCrossingRate = calculateZeroCrossingRate(audioData);
    metrics.mfccFeatures = calculateMFCC(audioData, sampleRate);
    
    // Perform spectral analysis
    SpectralAnalysis spectral = performSpectralAnalysis(audioData, sampleRate);
    metrics.spectralCentroid = spectral.spectralCentroid;
    metrics.spectralBandwidth = spectral.spectralBandwidth;
    metrics.spectralRolloff = spectral.spectralRolloff;
    
    // Detect artifacts
    AudioArtifacts artifacts = detectArtifacts(audioData, sampleRate);
    metrics.hasClipping = artifacts.clipping.detected;
    metrics.hasDropouts = artifacts.dropouts.detected;
    metrics.hasDistortion = artifacts.distortion.detected;
    metrics.hasEcho = artifacts.echo.detected;
    metrics.hasNoise = artifacts.noise.detected;
    metrics.noiseLevel = artifacts.noise.level / 100.0f; // Normalize to 0-1
    
    // Calculate quality scores
    metrics.overallQuality = calculateOverallQuality(metrics);
    metrics.speechQuality = calculateSpeechQuality(metrics);
    
    return metrics;
}

AudioArtifacts AudioQualityAnalyzer::detectArtifacts(const std::vector<float>& audioData, int sampleRate) {
    AudioArtifacts artifacts;
    
    artifacts.clipping = detectClipping(audioData);
    artifacts.dropouts = detectDropouts(audioData);
    artifacts.distortion = detectDistortion(audioData, sampleRate);
    artifacts.echo = detectEcho(audioData, sampleRate);
    artifacts.noise = detectNoise(audioData, sampleRate);
    
    return artifacts;
}

SpectralAnalysis AudioQualityAnalyzer::performSpectralAnalysis(const std::vector<float>& audioData, int sampleRate) {
    SpectralAnalysis analysis;
    
    if (audioData.size() < config_.fftSize) {
        return analysis;
    }
    
    // Apply window and compute FFT
    std::vector<float> windowedSignal = applyWindow(audioData);
    std::vector<std::complex<float>> fft = computeFFT(windowedSignal);
    
    // Compute spectra
    analysis.magnitudeSpectrum = computeMagnitudeSpectrum(fft);
    analysis.powerSpectrum = computePowerSpectrum(fft);
    
    // Generate frequency bins
    analysis.frequencies.resize(analysis.magnitudeSpectrum.size());
    for (size_t i = 0; i < analysis.frequencies.size(); ++i) {
        analysis.frequencies[i] = static_cast<float>(i * sampleRate) / (2.0f * analysis.frequencies.size());
    }
    
    // Compute spectral features
    analysis.spectralCentroid = computeSpectralCentroid(analysis.magnitudeSpectrum, sampleRate);
    analysis.spectralBandwidth = computeSpectralBandwidth(analysis.magnitudeSpectrum, analysis.spectralCentroid, sampleRate);
    analysis.spectralRolloff = computeSpectralRolloff(analysis.magnitudeSpectrum, sampleRate);
    analysis.spectralFlatness = computeSpectralFlatness(analysis.magnitudeSpectrum);
    
    // Find dominant frequency
    auto maxIt = std::max_element(analysis.magnitudeSpectrum.begin(), analysis.magnitudeSpectrum.end());
    if (maxIt != analysis.magnitudeSpectrum.end()) {
        size_t maxIndex = std::distance(analysis.magnitudeSpectrum.begin(), maxIt);
        analysis.dominantFrequency = analysis.frequencies[maxIndex];
    }
    
    // Calculate frequency band energies
    size_t lowBandEnd = static_cast<size_t>(500.0f * analysis.frequencies.size() * 2.0f / sampleRate);
    size_t midBandEnd = static_cast<size_t>(2000.0f * analysis.frequencies.size() * 2.0f / sampleRate);
    
    analysis.lowFreqEnergy = std::accumulate(analysis.powerSpectrum.begin(), 
                                           analysis.powerSpectrum.begin() + std::min(lowBandEnd, analysis.powerSpectrum.size()), 0.0f);
    analysis.midFreqEnergy = std::accumulate(analysis.powerSpectrum.begin() + lowBandEnd,
                                           analysis.powerSpectrum.begin() + std::min(midBandEnd, analysis.powerSpectrum.size()), 0.0f);
    analysis.highFreqEnergy = std::accumulate(analysis.powerSpectrum.begin() + midBandEnd,
                                            analysis.powerSpectrum.end(), 0.0f);
    
    return analysis;
}

float AudioQualityAnalyzer::calculateSNR(const std::vector<float>& audioData) {
    if (audioData.empty()) return 0.0f;
    
    // Estimate signal power (RMS of entire signal)
    float signalPower = 0.0f;
    for (float sample : audioData) {
        signalPower += sample * sample;
    }
    signalPower = std::sqrt(signalPower / audioData.size());
    
    // Estimate noise power (use quieter segments)
    std::vector<float> sortedSamples = audioData;
    std::sort(sortedSamples.begin(), sortedSamples.end(), [](float a, float b) {
        return std::abs(a) < std::abs(b);
    });
    
    // Use bottom 25% as noise estimate
    size_t noiseCount = sortedSamples.size() / 4;
    float noisePower = 0.0f;
    for (size_t i = 0; i < noiseCount; ++i) {
        noisePower += sortedSamples[i] * sortedSamples[i];
    }
    noisePower = std::sqrt(noisePower / noiseCount);
    
    // Calculate SNR in dB
    if (noisePower > 0.0f) {
        return 20.0f * std::log10(signalPower / noisePower);
    }
    return 60.0f; // Very high SNR if no noise detected
}

float AudioQualityAnalyzer::calculateZeroCrossingRate(const std::vector<float>& audioData) {
    if (audioData.size() < 2) return 0.0f;
    
    size_t crossings = 0;
    for (size_t i = 1; i < audioData.size(); ++i) {
        if ((audioData[i-1] >= 0.0f && audioData[i] < 0.0f) ||
            (audioData[i-1] < 0.0f && audioData[i] >= 0.0f)) {
            crossings++;
        }
    }
    
    return static_cast<float>(crossings) / (audioData.size() - 1);
}

std::vector<float> AudioQualityAnalyzer::calculateMFCC(const std::vector<float>& audioData, int sampleRate) {
    std::vector<float> mfcc(config_.numMfccCoeffs, 0.0f);
    
    if (audioData.size() < config_.fftSize) {
        return mfcc;
    }
    
    // Initialize mel filter bank if needed
    if (melFilterBank_.empty()) {
        initializeMelFilterBank(sampleRate);
    }
    
    // Compute power spectrum
    SpectralAnalysis spectral = performSpectralAnalysis(audioData, sampleRate);
    
    // Apply mel filter bank
    std::vector<float> melEnergies(config_.melFilterBankSize, 0.0f);
    // Simplified mel filter bank application
    for (size_t i = 0; i < melEnergies.size() && i < spectral.powerSpectrum.size(); ++i) {
        melEnergies[i] = spectral.powerSpectrum[i];
    }
    
    // Apply log and DCT to get MFCC coefficients
    for (size_t i = 0; i < mfcc.size(); ++i) {
        float sum = 0.0f;
        for (size_t j = 0; j < melEnergies.size(); ++j) {
            if (melEnergies[j] > 0.0f) {
                sum += std::log(melEnergies[j]) * std::cos(M_PI * i * (j + 0.5f) / melEnergies.size());
            }
        }
        mfcc[i] = sum;
    }
    
    return mfcc;
}

AudioArtifacts::ClippingInfo AudioQualityAnalyzer::detectClipping(const std::vector<float>& audioData) {
    AudioArtifacts::ClippingInfo clipping;
    clipping.detected = false;
    clipping.percentage = 0.0f;
    
    if (audioData.empty()) return clipping;
    
    size_t clippedSamples = 0;
    for (size_t i = 0; i < audioData.size(); ++i) {
        if (std::abs(audioData[i]) >= config_.clippingThreshold) {
            clippedSamples++;
            clipping.locations.push_back(i);
        }
    }
    
    clipping.percentage = static_cast<float>(clippedSamples) / audioData.size() * 100.0f;
    clipping.detected = clipping.percentage > 1.0f; // More than 1% clipped
    
    return clipping;
}

AudioArtifacts::DropoutInfo AudioQualityAnalyzer::detectDropouts(const std::vector<float>& audioData) {
    AudioArtifacts::DropoutInfo dropouts;
    dropouts.detected = false;
    dropouts.count = 0;
    
    if (audioData.empty()) return dropouts;
    
    bool inDropout = false;
    size_t dropoutStart = 0;
    
    for (size_t i = 0; i < audioData.size(); ++i) {
        bool isSilent = std::abs(audioData[i]) < config_.dropoutThreshold;
        
        if (isSilent && !inDropout) {
            inDropout = true;
            dropoutStart = i;
        } else if (!isSilent && inDropout) {
            inDropout = false;
            if (i - dropoutStart > 100) { // Minimum 100 samples for dropout
                dropouts.locations.push_back({dropoutStart, i});
                dropouts.count++;
            }
        }
    }
    
    dropouts.detected = dropouts.count > 0;
    return dropouts;
}

AudioArtifacts::DistortionInfo AudioQualityAnalyzer::detectDistortion(const std::vector<float>& audioData, int sampleRate) {
    AudioArtifacts::DistortionInfo distortion;
    distortion.detected = false;
    distortion.thd = 0.0f;
    distortion.severity = 0.0f;
    
    if (audioData.size() < config_.fftSize) return distortion;
    
    // Perform spectral analysis to detect harmonics
    SpectralAnalysis spectral = performSpectralAnalysis(audioData, sampleRate);
    
    // Find fundamental frequency and harmonics
    auto maxIt = std::max_element(spectral.magnitudeSpectrum.begin(), spectral.magnitudeSpectrum.end());
    if (maxIt == spectral.magnitudeSpectrum.end()) return distortion;
    
    size_t fundamentalBin = std::distance(spectral.magnitudeSpectrum.begin(), maxIt);
    float fundamentalMagnitude = *maxIt;
    
    // Calculate harmonic distortion
    float harmonicEnergy = 0.0f;
    for (int harmonic = 2; harmonic <= 5; ++harmonic) {
        size_t harmonicBin = fundamentalBin * harmonic;
        if (harmonicBin < spectral.magnitudeSpectrum.size()) {
            harmonicEnergy += spectral.magnitudeSpectrum[harmonicBin] * spectral.magnitudeSpectrum[harmonicBin];
        }
    }
    
    if (fundamentalMagnitude > 0.0f) {
        distortion.thd = std::sqrt(harmonicEnergy) / fundamentalMagnitude;
        distortion.severity = std::min(distortion.thd / config_.distortionThreshold, 1.0f);
        distortion.detected = distortion.thd > config_.distortionThreshold;
    }
    
    return distortion;
}

AudioArtifacts::EchoInfo AudioQualityAnalyzer::detectEcho(const std::vector<float>& audioData, int sampleRate) {
    AudioArtifacts::EchoInfo echo;
    echo.detected = false;
    echo.delay = 0.0f;
    echo.strength = 0.0f;
    
    if (audioData.size() < 1000) return echo; // Need sufficient data
    
    // Compute autocorrelation to detect echo
    std::vector<float> autocorr = computeAutocorrelation(audioData);
    
    // Look for peaks in autocorrelation (excluding the main peak at 0)
    size_t minDelay = sampleRate / 100; // 10ms minimum delay
    size_t maxDelay = std::min(autocorr.size() / 2, static_cast<size_t>(sampleRate)); // 1s maximum delay
    
    float maxCorr = 0.0f;
    size_t echoDelaySamples = 0;
    
    for (size_t i = minDelay; i < maxDelay; ++i) {
        if (autocorr[i] > maxCorr) {
            maxCorr = autocorr[i];
            echoDelaySamples = i;
        }
    }
    
    echo.strength = maxCorr;
    echo.delay = static_cast<float>(echoDelaySamples) / sampleRate;
    echo.detected = echo.strength > config_.echoThreshold;
    
    return echo;
}

AudioArtifacts::NoiseInfo AudioQualityAnalyzer::detectNoise(const std::vector<float>& audioData, int sampleRate) {
    AudioArtifacts::NoiseInfo noise;
    noise.detected = false;
    noise.level = 0.0f;
    noise.type = "unknown";
    
    if (audioData.empty()) return noise;
    
    // Estimate noise floor using spectral analysis
    SpectralAnalysis spectral = performSpectralAnalysis(audioData, sampleRate);
    float noiseFloor = estimateNoiseFloor(spectral.magnitudeSpectrum);
    
    // Convert to dB
    noise.level = 20.0f * std::log10(noiseFloor + 1e-10f);
    noise.detected = noise.level > config_.noiseThreshold;
    
    // Simple noise type classification based on spectral characteristics
    float spectralFlatness = spectral.spectralFlatness;
    if (spectralFlatness > 0.8f) {
        noise.type = "white";
    } else if (spectralFlatness > 0.5f) {
        noise.type = "pink";
    } else {
        noise.type = "colored";
    }
    
    return noise;
}

float AudioQualityAnalyzer::calculateOverallQuality(const AudioQualityMetrics& metrics) {
    std::vector<float> scores;
    std::vector<float> weights;
    
    // SNR contribution
    float snrScore = std::min(std::max(metrics.signalToNoiseRatio / 40.0f, 0.0f), 1.0f);
    scores.push_back(snrScore);
    weights.push_back(config_.snrWeight);
    
    // Spectral quality contribution
    float spectralScore = 1.0f;
    if (metrics.spectralCentroid > 0.0f) {
        // Prefer speech-like spectral centroid (around 1000-2000 Hz)
        float idealCentroid = 1500.0f;
        float centroidDiff = std::abs(metrics.spectralCentroid - idealCentroid) / idealCentroid;
        spectralScore = std::max(1.0f - centroidDiff, 0.0f);
    }
    scores.push_back(spectralScore);
    weights.push_back(config_.spectralWeight);
    
    // Artifact penalty
    float artifactScore = 1.0f;
    if (metrics.hasClipping) artifactScore -= 0.3f;
    if (metrics.hasDropouts) artifactScore -= 0.2f;
    if (metrics.hasDistortion) artifactScore -= 0.3f;
    if (metrics.hasEcho) artifactScore -= 0.1f;
    if (metrics.hasNoise) artifactScore -= metrics.noiseLevel * 0.2f;
    artifactScore = std::max(artifactScore, 0.0f);
    scores.push_back(artifactScore);
    weights.push_back(config_.artifactWeight);
    
    return combineQualityScores(scores, weights);
}

float AudioQualityAnalyzer::calculateSpeechQuality(const AudioQualityMetrics& metrics) {
    // Speech-specific quality assessment
    float speechScore = calculateOverallQuality(metrics);
    
    // Additional speech-specific factors
    if (metrics.spectralCentroid > 500.0f && metrics.spectralCentroid < 3000.0f) {
        speechScore += 0.1f; // Bonus for speech-like spectral centroid
    }
    
    if (metrics.zeroCrossingRate > 0.01f && metrics.zeroCrossingRate < 0.3f) {
        speechScore += 0.05f; // Bonus for speech-like zero crossing rate
    }
    
    return std::min(speechScore, 1.0f);
}

void AudioQualityAnalyzer::adaptParametersForQuality(const AudioQualityMetrics& metrics) {
    // Adapt analysis parameters based on detected quality issues
    if (metrics.hasNoise) {
        config_.noiseThreshold = std::min(config_.noiseThreshold + 2.0f, -10.0f);
    }
    
    if (metrics.hasDistortion) {
        config_.distortionThreshold = std::max(config_.distortionThreshold - 0.01f, 0.05f);
    }
    
    if (metrics.overallQuality < 0.5f) {
        // Increase sensitivity for low quality audio
        config_.clippingThreshold = std::max(config_.clippingThreshold - 0.05f, 0.8f);
        config_.dropoutThreshold = std::max(config_.dropoutThreshold - 0.005f, 0.005f);
    }
}

AudioQualityConfig AudioQualityAnalyzer::getOptimalConfig(const AudioQualityMetrics& metrics) {
    AudioQualityConfig optimalConfig = config_;
    
    // Adjust FFT size based on audio characteristics
    if (metrics.spectralCentroid > 2000.0f) {
        optimalConfig.fftSize = std::max(optimalConfig.fftSize * 2, static_cast<size_t>(2048));
    }
    
    // Adjust thresholds based on detected quality
    if (metrics.overallQuality > 0.8f) {
        // High quality audio - can use stricter thresholds
        optimalConfig.clippingThreshold = std::min(optimalConfig.clippingThreshold + 0.02f, 0.98f);
        optimalConfig.distortionThreshold = std::min(optimalConfig.distortionThreshold + 0.02f, 0.15f);
    }
    
    return optimalConfig;
}

void AudioQualityAnalyzer::initializeRealTimeAnalysis(int sampleRate, size_t bufferSize) {
    analysisBuffer_.resize(bufferSize);
    bufferPosition_ = 0;
    realTimeInitialized_ = true;
    initializeMelFilterBank(sampleRate);
}

AudioQualityMetrics AudioQualityAnalyzer::analyzeRealTime(const std::vector<float>& audioChunk) {
    if (!realTimeInitialized_) {
        return AudioQualityMetrics();
    }
    
    // Add chunk to circular buffer
    for (float sample : audioChunk) {
        analysisBuffer_[bufferPosition_] = sample;
        bufferPosition_ = (bufferPosition_ + 1) % analysisBuffer_.size();
    }
    
    // Analyze current buffer contents
    return analyzeQuality(analysisBuffer_);
}

void AudioQualityAnalyzer::resetRealTimeState() {
    std::fill(analysisBuffer_.begin(), analysisBuffer_.end(), 0.0f);
    bufferPosition_ = 0;
}

// Private helper methods implementation

void AudioQualityAnalyzer::initializeFFT() {
    fftBuffer_.resize(config_.fftSize);
}

void AudioQualityAnalyzer::initializeWindowFunction() {
    windowFunction_.resize(config_.fftSize);
    for (size_t i = 0; i < config_.fftSize; ++i) {
        // Hann window
        windowFunction_[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (config_.fftSize - 1)));
    }
}

void AudioQualityAnalyzer::initializeMelFilterBank(int sampleRate) {
    melFilterBank_.resize(config_.melFilterBankSize);
    // Simplified mel filter bank initialization
    for (size_t i = 0; i < config_.melFilterBankSize; ++i) {
        melFilterBank_[i] = 1.0f; // Placeholder - would implement proper mel scale
    }
}

std::vector<std::complex<float>> AudioQualityAnalyzer::computeFFT(const std::vector<float>& signal) {
    std::vector<std::complex<float>> result(config_.fftSize);
    
    // Simple DFT implementation (would use FFTW in production)
    for (size_t k = 0; k < config_.fftSize; ++k) {
        std::complex<float> sum(0.0f, 0.0f);
        for (size_t n = 0; n < std::min(signal.size(), config_.fftSize); ++n) {
            float angle = -2.0f * M_PI * k * n / config_.fftSize;
            sum += signal[n] * std::complex<float>(std::cos(angle), std::sin(angle));
        }
        result[k] = sum;
    }
    
    return result;
}

std::vector<float> AudioQualityAnalyzer::computeMagnitudeSpectrum(const std::vector<std::complex<float>>& fft) {
    std::vector<float> magnitude(fft.size() / 2);
    for (size_t i = 0; i < magnitude.size(); ++i) {
        magnitude[i] = std::abs(fft[i]);
    }
    return magnitude;
}

std::vector<float> AudioQualityAnalyzer::computePowerSpectrum(const std::vector<std::complex<float>>& fft) {
    std::vector<float> power(fft.size() / 2);
    for (size_t i = 0; i < power.size(); ++i) {
        power[i] = std::norm(fft[i]);
    }
    return power;
}

float AudioQualityAnalyzer::computeSpectralCentroid(const std::vector<float>& spectrum, int sampleRate) {
    float weightedSum = 0.0f;
    float magnitudeSum = 0.0f;
    
    for (size_t i = 0; i < spectrum.size(); ++i) {
        float frequency = static_cast<float>(i * sampleRate) / (2.0f * spectrum.size());
        weightedSum += frequency * spectrum[i];
        magnitudeSum += spectrum[i];
    }
    
    return magnitudeSum > 0.0f ? weightedSum / magnitudeSum : 0.0f;
}

float AudioQualityAnalyzer::computeSpectralBandwidth(const std::vector<float>& spectrum, float centroid, int sampleRate) {
    float weightedSum = 0.0f;
    float magnitudeSum = 0.0f;
    
    for (size_t i = 0; i < spectrum.size(); ++i) {
        float frequency = static_cast<float>(i * sampleRate) / (2.0f * spectrum.size());
        float diff = frequency - centroid;
        weightedSum += diff * diff * spectrum[i];
        magnitudeSum += spectrum[i];
    }
    
    return magnitudeSum > 0.0f ? std::sqrt(weightedSum / magnitudeSum) : 0.0f;
}

float AudioQualityAnalyzer::computeSpectralRolloff(const std::vector<float>& spectrum, int sampleRate, float rolloffPercent) {
    float totalEnergy = std::accumulate(spectrum.begin(), spectrum.end(), 0.0f);
    float targetEnergy = totalEnergy * rolloffPercent;
    
    float cumulativeEnergy = 0.0f;
    for (size_t i = 0; i < spectrum.size(); ++i) {
        cumulativeEnergy += spectrum[i];
        if (cumulativeEnergy >= targetEnergy) {
            return static_cast<float>(i * sampleRate) / (2.0f * spectrum.size());
        }
    }
    
    return static_cast<float>(sampleRate) / 2.0f;
}

float AudioQualityAnalyzer::computeSpectralFlatness(const std::vector<float>& spectrum) {
    if (spectrum.empty()) return 0.0f;
    
    float geometricMean = 1.0f;
    float arithmeticMean = 0.0f;
    
    for (float value : spectrum) {
        if (value > 0.0f) {
            geometricMean *= std::pow(value, 1.0f / spectrum.size());
            arithmeticMean += value;
        }
    }
    
    arithmeticMean /= spectrum.size();
    
    return arithmeticMean > 0.0f ? geometricMean / arithmeticMean : 0.0f;
}

std::vector<float> AudioQualityAnalyzer::applyWindow(const std::vector<float>& signal) {
    std::vector<float> windowed(std::min(signal.size(), config_.fftSize));
    
    for (size_t i = 0; i < windowed.size(); ++i) {
        windowed[i] = signal[i] * windowFunction_[i];
    }
    
    return windowed;
}

std::vector<float> AudioQualityAnalyzer::computeEnvelope(const std::vector<float>& signal) {
    std::vector<float> envelope(signal.size());
    
    // Simple envelope detection using absolute value and smoothing
    for (size_t i = 0; i < signal.size(); ++i) {
        envelope[i] = std::abs(signal[i]);
    }
    
    // Apply smoothing filter
    for (size_t i = 1; i < envelope.size() - 1; ++i) {
        envelope[i] = (envelope[i-1] + envelope[i] + envelope[i+1]) / 3.0f;
    }
    
    return envelope;
}

std::vector<float> AudioQualityAnalyzer::computeAutocorrelation(const std::vector<float>& signal) {
    std::vector<float> autocorr(signal.size());
    
    for (size_t lag = 0; lag < signal.size(); ++lag) {
        float sum = 0.0f;
        for (size_t i = 0; i < signal.size() - lag; ++i) {
            sum += signal[i] * signal[i + lag];
        }
        autocorr[lag] = sum / (signal.size() - lag);
    }
    
    // Normalize by zero-lag value
    if (autocorr[0] > 0.0f) {
        for (float& value : autocorr) {
            value /= autocorr[0];
        }
    }
    
    return autocorr;
}

float AudioQualityAnalyzer::estimateNoiseFloor(const std::vector<float>& spectrum) {
    if (spectrum.empty()) return 0.0f;
    
    std::vector<float> sortedSpectrum = spectrum;
    std::sort(sortedSpectrum.begin(), sortedSpectrum.end());
    
    // Use median of lower 50% as noise floor estimate
    size_t medianIndex = sortedSpectrum.size() / 4;
    return sortedSpectrum[medianIndex];
}

float AudioQualityAnalyzer::estimateSignalPower(const std::vector<float>& audioData) {
    if (audioData.empty()) return 0.0f;
    
    float power = 0.0f;
    for (float sample : audioData) {
        power += sample * sample;
    }
    
    return std::sqrt(power / audioData.size());
}

float AudioQualityAnalyzer::scoreSpectralFeatures(const SpectralAnalysis& spectral) {
    float score = 1.0f;
    
    // Prefer speech-like spectral characteristics
    if (spectral.spectralCentroid > 500.0f && spectral.spectralCentroid < 3000.0f) {
        score += 0.2f;
    }
    
    if (spectral.spectralFlatness < 0.5f) {
        score += 0.1f; // Prefer structured (non-flat) spectra
    }
    
    return std::min(score, 1.0f);
}

float AudioQualityAnalyzer::scoreArtifacts(const AudioArtifacts& artifacts) {
    float score = 1.0f;
    
    if (artifacts.clipping.detected) {
        score -= artifacts.clipping.percentage / 100.0f * 0.5f;
    }
    
    if (artifacts.dropouts.detected) {
        score -= std::min(static_cast<float>(artifacts.dropouts.count) / 10.0f, 0.3f);
    }
    
    if (artifacts.distortion.detected) {
        score -= artifacts.distortion.severity * 0.4f;
    }
    
    if (artifacts.echo.detected) {
        score -= artifacts.echo.strength * 0.2f;
    }
    
    if (artifacts.noise.detected) {
        score -= std::min(std::abs(artifacts.noise.level) / 40.0f, 0.3f);
    }
    
    return std::max(score, 0.0f);
}

float AudioQualityAnalyzer::combineQualityScores(const std::vector<float>& scores, const std::vector<float>& weights) {
    if (scores.size() != weights.size() || scores.empty()) {
        return 0.0f;
    }
    
    float weightedSum = 0.0f;
    float totalWeight = 0.0f;
    
    for (size_t i = 0; i < scores.size(); ++i) {
        weightedSum += scores[i] * weights[i];
        totalWeight += weights[i];
    }
    
    return totalWeight > 0.0f ? weightedSum / totalWeight : 0.0f;
}

} // namespace audio