#include "audio/audio_preprocessor.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <complex>

namespace audio {

// NoiseReductionFilter implementation

NoiseReductionFilter::NoiseReductionFilter(int sampleRate)
    : sampleRate_(sampleRate), frameSize_(1024) {
    noiseProfile_.resize(frameSize_ / 2, 0.0f);
    previousFrame_.resize(frameSize_, 0.0f);
}

std::vector<float> NoiseReductionFilter::processSpectralSubtraction(
    const std::vector<float>& audioData, float alpha, float beta) {
    
    if (audioData.size() < frameSize_) {
        return audioData; // Not enough data for processing
    }
    
    std::vector<float> processed;
    processed.reserve(audioData.size());
    
    // Process in overlapping frames
    size_t hopSize = frameSize_ / 2;
    for (size_t i = 0; i + frameSize_ <= audioData.size(); i += hopSize) {
        // Extract frame
        std::vector<float> frame(audioData.begin() + i, audioData.begin() + i + frameSize_);
        
        // Apply window
        std::vector<float> windowedFrame = applyWindow(frame);
        
        // Compute FFT
        std::vector<std::complex<float>> spectrum = computeFFT(windowedFrame);
        
        // Apply spectral subtraction
        for (size_t bin = 0; bin < spectrum.size() / 2; ++bin) {
            float magnitude = std::abs(spectrum[bin]);
            float noiseMagnitude = noiseProfile_[bin];
            
            // Spectral subtraction formula
            float subtractedMagnitude = magnitude - alpha * noiseMagnitude;
            subtractedMagnitude = std::max(subtractedMagnitude, beta * magnitude);
            
            // Preserve phase, modify magnitude
            if (magnitude > 0.0f) {
                float scale = subtractedMagnitude / magnitude;
                spectrum[bin] *= scale;
                spectrum[spectrum.size() - 1 - bin] *= scale; // Mirror for real signal
            }
        }
        
        // Compute IFFT
        std::vector<float> processedFrame = computeIFFT(spectrum);
        
        // Overlap-add
        if (i == 0) {
            processed.insert(processed.end(), processedFrame.begin(), processedFrame.end());
        } else {
            // Add overlapping part
            size_t overlapStart = processed.size() - hopSize;
            for (size_t j = 0; j < hopSize && j < processedFrame.size(); ++j) {
                processed[overlapStart + j] += processedFrame[j];
            }
            // Add non-overlapping part
            processed.insert(processed.end(), 
                           processedFrame.begin() + hopSize, processedFrame.end());
        }
    }
    
    return processed;
}

std::vector<float> NoiseReductionFilter::processWienerFilter(
    const std::vector<float>& audioData, float smoothingFactor) {
    
    std::vector<float> filtered = audioData;
    
    // Simple Wiener filtering implementation
    for (size_t i = 1; i < filtered.size() - 1; ++i) {
        // Estimate local signal and noise power
        float localPower = filtered[i] * filtered[i];
        float noisePower = 0.01f; // Assumed noise power
        
        // Wiener gain
        float wienerGain = localPower / (localPower + noisePower);
        
        // Apply smoothing
        wienerGain = smoothingFactor * wienerGain + (1.0f - smoothingFactor) * 1.0f;
        
        // Apply filter
        filtered[i] *= wienerGain;
    }
    
    return filtered;
}

std::vector<float> NoiseReductionFilter::processNoiseGate(
    const std::vector<float>& audioData, float threshold) {
    
    std::vector<float> gated = audioData;
    float thresholdLinear = std::pow(10.0f, threshold / 20.0f);
    
    for (float& sample : gated) {
        if (std::abs(sample) < thresholdLinear) {
            sample *= 0.1f; // Reduce by 20dB instead of complete gating
        }
    }
    
    return gated;
}

void NoiseReductionFilter::updateNoiseProfile(const std::vector<float>& noiseData) {
    if (noiseData.size() < frameSize_) return;
    
    // Compute noise spectrum
    std::vector<float> windowedNoise = applyWindow(
        std::vector<float>(noiseData.begin(), noiseData.begin() + frameSize_));
    std::vector<std::complex<float>> noiseSpectrum = computeFFT(windowedNoise);
    
    // Update noise profile with exponential averaging
    float alpha = 0.1f; // Learning rate
    for (size_t i = 0; i < noiseProfile_.size() && i < noiseSpectrum.size() / 2; ++i) {
        float noiseMagnitude = std::abs(noiseSpectrum[i]);
        noiseProfile_[i] = alpha * noiseMagnitude + (1.0f - alpha) * noiseProfile_[i];
    }
}

void NoiseReductionFilter::resetNoiseProfile() {
    std::fill(noiseProfile_.begin(), noiseProfile_.end(), 0.0f);
}

std::vector<std::complex<float>> NoiseReductionFilter::computeFFT(const std::vector<float>& signal) {
    std::vector<std::complex<float>> result(signal.size());
    
    // Simple DFT implementation (would use FFTW in production)
    for (size_t k = 0; k < signal.size(); ++k) {
        std::complex<float> sum(0.0f, 0.0f);
        for (size_t n = 0; n < signal.size(); ++n) {
            float angle = -2.0f * M_PI * k * n / signal.size();
            sum += signal[n] * std::complex<float>(std::cos(angle), std::sin(angle));
        }
        result[k] = sum;
    }
    
    return result;
}

std::vector<float> NoiseReductionFilter::computeIFFT(const std::vector<std::complex<float>>& spectrum) {
    std::vector<float> result(spectrum.size());
    
    // Simple IDFT implementation
    for (size_t n = 0; n < spectrum.size(); ++n) {
        std::complex<float> sum(0.0f, 0.0f);
        for (size_t k = 0; k < spectrum.size(); ++k) {
            float angle = 2.0f * M_PI * k * n / spectrum.size();
            sum += spectrum[k] * std::complex<float>(std::cos(angle), std::sin(angle));
        }
        result[n] = sum.real() / spectrum.size();
    }
    
    return result;
}

std::vector<float> NoiseReductionFilter::applyWindow(const std::vector<float>& signal) {
    std::vector<float> windowed(signal.size());
    
    // Hann window
    for (size_t i = 0; i < signal.size(); ++i) {
        float window = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (signal.size() - 1)));
        windowed[i] = signal[i] * window;
    }
    
    return windowed;
}

// VolumeNormalizer implementation

VolumeNormalizer::VolumeNormalizer(int sampleRate)
    : sampleRate_(sampleRate), currentGain_(1.0f), currentRMS_(0.0f), compressorGain_(1.0f) {
    updateCoefficients(0.01f, 0.1f); // Default attack/release times
}

std::vector<float> VolumeNormalizer::processAGC(const std::vector<float>& audioData, float targetRMS) {
    std::vector<float> normalized = audioData;
    
    // Calculate current RMS
    float rms = calculateRMS(audioData);
    
    // Update current RMS with smoothing
    currentRMS_ = 0.9f * currentRMS_ + 0.1f * rms;
    
    // Calculate required gain
    if (currentRMS_ > 1e-6f) {
        float targetGain = targetRMS / currentRMS_;
        
        // Smooth gain changes
        currentGain_ = 0.95f * currentGain_ + 0.05f * targetGain;
        
        // Limit gain to prevent excessive amplification
        currentGain_ = std::min(currentGain_, 10.0f);
        currentGain_ = std::max(currentGain_, 0.1f);
        
        // Apply gain
        for (float& sample : normalized) {
            sample *= currentGain_;
        }
    }
    
    return normalized;
}

std::vector<float> VolumeNormalizer::processCompression(
    const std::vector<float>& audioData, float ratio, float threshold, 
    float attack, float release) {
    
    updateCoefficients(attack, release);
    std::vector<float> compressed = audioData;
    
    for (float& sample : compressed) {
        float absSample = std::abs(sample);
        
        // Calculate compression
        if (absSample > threshold) {
            float excess = absSample - threshold;
            float compressedExcess = excess / ratio;
            float targetGain = (threshold + compressedExcess) / absSample;
            
            // Apply envelope following
            if (targetGain < compressorGain_) {
                compressorGain_ = updateEnvelope(targetGain, compressorGain_, attackCoeff_, releaseCoeff_);
            } else {
                compressorGain_ = updateEnvelope(targetGain, compressorGain_, releaseCoeff_, attackCoeff_);
            }
        }
        
        sample *= compressorGain_;
    }
    
    return compressed;
}

std::vector<float> VolumeNormalizer::processLimiter(const std::vector<float>& audioData, float threshold) {
    std::vector<float> limited = audioData;
    
    for (float& sample : limited) {
        if (sample > threshold) {
            sample = threshold;
        } else if (sample < -threshold) {
            sample = -threshold;
        }
    }
    
    return limited;
}

void VolumeNormalizer::resetState() {
    currentGain_ = 1.0f;
    currentRMS_ = 0.0f;
    compressorGain_ = 1.0f;
}

float VolumeNormalizer::calculateRMS(const std::vector<float>& audioData) {
    if (audioData.empty()) return 0.0f;
    
    float sum = 0.0f;
    for (float sample : audioData) {
        sum += sample * sample;
    }
    
    return std::sqrt(sum / audioData.size());
}

float VolumeNormalizer::updateEnvelope(float input, float current, float attack, float release) {
    if (input > current) {
        return attack * input + (1.0f - attack) * current;
    } else {
        return release * input + (1.0f - release) * current;
    }
}

void VolumeNormalizer::updateCoefficients(float attack, float release) {
    attackCoeff_ = 1.0f - std::exp(-1.0f / (attack * sampleRate_));
    releaseCoeff_ = 1.0f - std::exp(-1.0f / (release * sampleRate_));
}

// EchoCanceller implementation

EchoCanceller::EchoCanceller(int sampleRate, size_t filterLength)
    : sampleRate_(sampleRate), filterLength_(filterLength) {
    adaptiveFilter_.resize(filterLength, 0.0f);
    inputHistory_.resize(filterLength, 0.0f);
    errorHistory_.resize(filterLength, 0.0f);
}

std::vector<float> EchoCanceller::processLMS(const std::vector<float>& audioData, float convergenceRate) {
    std::vector<float> processed = audioData;
    
    for (size_t i = 0; i < processed.size(); ++i) {
        // Update input history
        updateInputHistory(processed[i]);
        
        // Calculate echo estimate
        float echoEstimate = dotProduct(adaptiveFilter_, inputHistory_);
        
        // Calculate error (desired - estimated)
        float error = processed[i] - echoEstimate;
        processed[i] = error;
        
        // Update adaptive filter using LMS algorithm
        for (size_t j = 0; j < filterLength_; ++j) {
            adaptiveFilter_[j] += convergenceRate * error * inputHistory_[j];
        }
    }
    
    return processed;
}

std::vector<float> EchoCanceller::processNLMS(const std::vector<float>& audioData, float convergenceRate) {
    std::vector<float> processed = audioData;
    
    for (size_t i = 0; i < processed.size(); ++i) {
        updateInputHistory(processed[i]);
        
        float echoEstimate = dotProduct(adaptiveFilter_, inputHistory_);
        float error = processed[i] - echoEstimate;
        processed[i] = error;
        
        // Calculate input power for normalization
        float inputPower = dotProduct(inputHistory_, inputHistory_) + 1e-6f;
        float normalizedStep = convergenceRate / inputPower;
        
        // Update adaptive filter using NLMS algorithm
        for (size_t j = 0; j < filterLength_; ++j) {
            adaptiveFilter_[j] += normalizedStep * error * inputHistory_[j];
        }
    }
    
    return processed;
}

std::vector<float> EchoCanceller::processEchoSuppression(
    const std::vector<float>& audioData, float suppressionStrength) {
    
    std::vector<float> suppressed = audioData;
    
    // Simple echo suppression using autocorrelation
    std::vector<float> autocorr = computeAutocorrelation(audioData);
    
    // Find echo delay
    float echoDelay = estimateEchoDelay(audioData);
    size_t delaySamples = static_cast<size_t>(echoDelay * sampleRate_);
    
    if (delaySamples > 0 && delaySamples < suppressed.size()) {
        for (size_t i = delaySamples; i < suppressed.size(); ++i) {
            suppressed[i] -= suppressionStrength * suppressed[i - delaySamples];
        }
    }
    
    return suppressed;
}

void EchoCanceller::resetAdaptiveFilter() {
    std::fill(adaptiveFilter_.begin(), adaptiveFilter_.end(), 0.0f);
    std::fill(inputHistory_.begin(), inputHistory_.end(), 0.0f);
    std::fill(errorHistory_.begin(), errorHistory_.end(), 0.0f);
}

void EchoCanceller::setFilterLength(size_t length) {
    filterLength_ = length;
    adaptiveFilter_.resize(length, 0.0f);
    inputHistory_.resize(length, 0.0f);
    errorHistory_.resize(length, 0.0f);
}

bool EchoCanceller::detectEcho(const std::vector<float>& audioData, float threshold) {
    if (audioData.size() < 1000) return false;
    
    std::vector<float> autocorr = computeAutocorrelation(audioData);
    
    // Look for significant peaks in autocorrelation
    size_t minDelay = sampleRate_ / 100; // 10ms minimum
    size_t maxDelay = std::min(autocorr.size() / 2, static_cast<size_t>(sampleRate_)); // 1s maximum
    
    for (size_t i = minDelay; i < maxDelay; ++i) {
        if (autocorr[i] > threshold) {
            return true;
        }
    }
    
    return false;
}

float EchoCanceller::estimateEchoDelay(const std::vector<float>& audioData) {
    if (audioData.size() < 1000) return 0.0f;
    
    std::vector<float> autocorr = computeAutocorrelation(audioData);
    
    size_t minDelay = sampleRate_ / 100; // 10ms minimum
    size_t maxDelay = std::min(autocorr.size() / 2, static_cast<size_t>(sampleRate_));
    
    float maxCorr = 0.0f;
    size_t bestDelay = 0;
    
    for (size_t i = minDelay; i < maxDelay; ++i) {
        if (autocorr[i] > maxCorr) {
            maxCorr = autocorr[i];
            bestDelay = i;
        }
    }
    
    return static_cast<float>(bestDelay) / sampleRate_;
}

float EchoCanceller::dotProduct(const std::vector<float>& a, const std::vector<float>& b) {
    float result = 0.0f;
    size_t minSize = std::min(a.size(), b.size());
    
    for (size_t i = 0; i < minSize; ++i) {
        result += a[i] * b[i];
    }
    
    return result;
}

void EchoCanceller::updateInputHistory(float sample) {
    // Shift history and add new sample
    for (size_t i = inputHistory_.size() - 1; i > 0; --i) {
        inputHistory_[i] = inputHistory_[i - 1];
    }
    inputHistory_[0] = sample;
}

std::vector<float> EchoCanceller::computeAutocorrelation(const std::vector<float>& signal) {
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

// AudioPreprocessor implementation

AudioPreprocessor::AudioPreprocessor(const AudioPreprocessingConfig& config, int sampleRate)
    : config_(config), sampleRate_(sampleRate), bufferPosition_(0), realTimeInitialized_(false) {
    
    initializeComponents();
    initializePresets();
    
    // Initialize statistics
    stats_.totalSamplesProcessed = 0;
    stats_.totalChunksProcessed = 0;
    stats_.averageProcessingTime = 0.0;
    stats_.averageQualityImprovement = 0.0;
    stats_.lastProcessingTime = std::chrono::steady_clock::now();
}

PreprocessingResult AudioPreprocessor::preprocessAudio(const std::vector<float>& audioData) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    PreprocessingResult result;
    result.inputSampleCount = audioData.size();
    
    if (audioData.empty()) {
        return result;
    }
    
    // Analyze quality before processing
    if (config_.enableQualityAnalysis) {
        result.qualityBefore = qualityAnalyzer_->analyzeQuality(audioData, sampleRate_);
        result.audioCharacteristics = adaptiveProcessor_->analyzeAudioCharacteristics(audioData);
    }
    
    // Apply processing pipeline
    result.processedAudio = applyProcessingPipeline(audioData, result.appliedFilters, 
                                                   result.processingParameters);
    result.outputSampleCount = result.processedAudio.size();
    
    // Analyze quality after processing
    if (config_.enableQualityAnalysis) {
        result.qualityAfter = qualityAnalyzer_->analyzeQuality(result.processedAudio, sampleRate_);
        result.qualityImprovement = calculateQualityImprovement(result.qualityBefore, result.qualityAfter);
    }
    
    // Calculate processing time
    auto endTime = std::chrono::high_resolution_clock::now();
    result.processingLatencyMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    // Update statistics
    updateStatistics(result.outputSampleCount, result.processingLatencyMs,
                    result.appliedFilters, result.qualityImprovement);
    
    return result;
}

std::vector<float> AudioPreprocessor::preprocessAudioSimple(const std::vector<float>& audioData) {
    if (audioData.empty()) {
        return audioData;
    }
    
    std::vector<std::string> appliedFilters;
    std::map<std::string, float> parameters;
    
    return applyProcessingPipeline(audioData, appliedFilters, parameters);
}

std::vector<PreprocessingResult> AudioPreprocessor::preprocessMultiChannelAudio(
    const std::vector<std::vector<float>>& audioData) {
    
    std::vector<PreprocessingResult> results;
    
    for (const auto& channel : audioData) {
        results.push_back(preprocessAudio(channel));
    }
    
    return results;
}

void AudioPreprocessor::initializeRealTimeProcessing(size_t bufferSize) {
    processingBuffer_.resize(bufferSize);
    bufferPosition_ = 0;
    realTimeInitialized_ = true;
    
    if (adaptiveProcessor_) {
        adaptiveProcessor_->initializeRealTimeProcessing(bufferSize);
    }
}

std::vector<float> AudioPreprocessor::preprocessRealTimeChunk(const std::vector<float>& audioChunk) {
    if (!realTimeInitialized_) {
        initializeRealTimeProcessing(audioChunk.size() * 4);
    }
    
    // Add chunk to circular buffer
    for (float sample : audioChunk) {
        processingBuffer_[bufferPosition_] = sample;
        bufferPosition_ = (bufferPosition_ + 1) % processingBuffer_.size();
    }
    
    // Process the chunk
    return preprocessAudioSimple(audioChunk);
}

void AudioPreprocessor::resetRealTimeState() {
    if (realTimeInitialized_) {
        std::fill(processingBuffer_.begin(), processingBuffer_.end(), 0.0f);
        bufferPosition_ = 0;
    }
    
    // Reset component states
    if (volumeNormalizer_) {
        volumeNormalizer_->resetState();
    }
    if (echoCanceller_) {
        echoCanceller_->resetAdaptiveFilter();
    }
    if (noiseFilter_) {
        noiseFilter_->resetNoiseProfile();
    }
    if (adaptiveProcessor_) {
        adaptiveProcessor_->resetRealTimeState();
    }
}

std::vector<float> AudioPreprocessor::applyNoiseReduction(const std::vector<float>& audioData) {
    if (!config_.enableNoiseReduction || !noiseFilter_) {
        return audioData;
    }
    
    if (config_.noiseReduction.enableSpectralSubtraction) {
        return noiseFilter_->processSpectralSubtraction(audioData, 
            config_.noiseReduction.spectralSubtractionAlpha);
    } else if (config_.noiseReduction.enableWienerFiltering) {
        return noiseFilter_->processWienerFilter(audioData, 
            config_.noiseReduction.wienerFilterBeta);
    } else {
        return noiseFilter_->processNoiseGate(audioData, 
            config_.noiseReduction.noiseGateThreshold);
    }
}

std::vector<float> AudioPreprocessor::applyVolumeNormalization(const std::vector<float>& audioData) {
    if (!config_.enableVolumeNormalization || !volumeNormalizer_) {
        return audioData;
    }
    
    std::vector<float> normalized = audioData;
    
    if (config_.volumeNormalization.enableAGC) {
        normalized = volumeNormalizer_->processAGC(normalized, 
            config_.volumeNormalization.targetRMS);
    }
    
    if (config_.volumeNormalization.enableCompression) {
        normalized = volumeNormalizer_->processCompression(normalized,
            config_.volumeNormalization.compressionRatio, 0.7f,
            config_.volumeNormalization.attackTime,
            config_.volumeNormalization.releaseTime);
    }
    
    return normalized;
}

std::vector<float> AudioPreprocessor::applyEchoCancellation(const std::vector<float>& audioData) {
    if (!config_.enableEchoCancellation || !echoCanceller_) {
        return audioData;
    }
    
    if (config_.echoCancellation.enableLMS) {
        return echoCanceller_->processLMS(audioData, 
            config_.echoCancellation.convergenceRate);
    } else if (config_.echoCancellation.enableNLMS) {
        return echoCanceller_->processNLMS(audioData, 
            config_.echoCancellation.convergenceRate);
    } else {
        return echoCanceller_->processEchoSuppression(audioData,
            config_.echoCancellation.echoSuppressionStrength);
    }
}

AudioQualityMetrics AudioPreprocessor::analyzeAudioQuality(const std::vector<float>& audioData) {
    if (qualityAnalyzer_) {
        return qualityAnalyzer_->analyzeQuality(audioData, sampleRate_);
    }
    return AudioQualityMetrics();
}

float AudioPreprocessor::calculateQualityImprovement(const AudioQualityMetrics& before,
                                                    const AudioQualityMetrics& after) {
    // Simple quality improvement calculation
    float improvement = after.overallQuality - before.overallQuality;
    
    // Additional factors
    if (before.hasNoise && !after.hasNoise) improvement += 0.1f;
    if (before.hasEcho && !after.hasEcho) improvement += 0.1f;
    if (before.hasClipping && !after.hasClipping) improvement += 0.1f;
    
    // SNR improvement
    float snrImprovement = (after.signalToNoiseRatio - before.signalToNoiseRatio) / 40.0f;
    improvement += snrImprovement * 0.2f;
    
    return std::max(0.0f, improvement);
}

void AudioPreprocessor::setConfig(const AudioPreprocessingConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_ = config;
    
    // Update component configurations
    if (qualityAnalyzer_) {
        qualityAnalyzer_->setConfig(config.qualityConfig);
    }
    if (adaptiveProcessor_) {
        adaptiveProcessor_->setProcessingParams(config.adaptiveParams);
    }
}

AudioPreprocessingConfig AudioPreprocessor::getConfig() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return config_;
}

void AudioPreprocessor::setSampleRate(int sampleRate) {
    sampleRate_ = sampleRate;
    
    // Update component sample rates
    if (noiseFilter_) {
        noiseFilter_->setSampleRate(sampleRate);
    }
    if (adaptiveProcessor_) {
        adaptiveProcessor_->setSampleRate(sampleRate);
    }
}

void AudioPreprocessor::enableAdaptiveMode(bool enabled) {
    if (adaptiveProcessor_) {
        adaptiveProcessor_->enableAdaptiveMode(enabled);
    }
}

bool AudioPreprocessor::isAdaptiveModeEnabled() const {
    if (adaptiveProcessor_) {
        return adaptiveProcessor_->isAdaptiveModeEnabled();
    }
    return false;
}

void AudioPreprocessor::adaptParametersForQuality(const AudioQualityMetrics& quality) {
    if (!adaptiveProcessor_) return;
    
    AudioCharacteristics characteristics;
    characteristics.type = AudioType::UNKNOWN; // Would be determined from quality
    
    // Update adaptive processor parameters
    adaptiveProcessor_->adaptParametersForQuality(quality);
    
    // Update our own configuration based on quality
    AudioPreprocessingConfig newConfig = config_;
    
    if (quality.signalToNoiseRatio < 10.0f) {
        newConfig.noiseReduction.spectralSubtractionAlpha = 
            std::min(newConfig.noiseReduction.spectralSubtractionAlpha + 0.2f, 3.0f);
    }
    
    if (quality.hasEcho) {
        newConfig.echoCancellation.echoSuppressionStrength = 
            std::min(newConfig.echoCancellation.echoSuppressionStrength + 0.1f, 1.0f);
    }
    
    if (quality.overallQuality < 0.5f) {
        newConfig.volumeNormalization.compressionRatio = 
            std::min(newConfig.volumeNormalization.compressionRatio + 0.5f, 5.0f);
    }
    
    setConfig(newConfig);
}

AudioPreprocessor::PreprocessingStatistics AudioPreprocessor::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void AudioPreprocessor::resetStatistics() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = PreprocessingStatistics();
    stats_.lastProcessingTime = std::chrono::steady_clock::now();
}

void AudioPreprocessor::loadPreset(const std::string& presetName) {
    auto it = presets_.find(presetName);
    if (it != presets_.end()) {
        setConfig(it->second);
    }
}

void AudioPreprocessor::savePreset(const std::string& presetName, const AudioPreprocessingConfig& config) {
    presets_[presetName] = config;
}

std::vector<std::string> AudioPreprocessor::getAvailablePresets() const {
    std::vector<std::string> presetNames;
    for (const auto& preset : presets_) {
        presetNames.push_back(preset.first);
    }
    return presetNames;
}

// Private method implementations

void AudioPreprocessor::initializeComponents() {
    noiseFilter_ = std::make_shared<NoiseReductionFilter>(sampleRate_);
    volumeNormalizer_ = std::make_shared<VolumeNormalizer>(sampleRate_);
    echoCanceller_ = std::make_shared<EchoCanceller>(sampleRate_);
    qualityAnalyzer_ = std::make_shared<AudioQualityAnalyzer>(config_.qualityConfig);
    adaptiveProcessor_ = std::make_shared<AdaptiveAudioProcessor>(sampleRate_);
}

void AudioPreprocessor::initializePresets() {
    // Speech preset
    AudioPreprocessingConfig speechPreset;
    speechPreset.enableNoiseReduction = true;
    speechPreset.enableVolumeNormalization = true;
    speechPreset.enableEchoCancellation = true;
    speechPreset.noiseReduction.spectralSubtractionAlpha = 2.5f;
    speechPreset.volumeNormalization.targetRMS = 0.15f;
    speechPreset.volumeNormalization.compressionRatio = 3.0f;
    presets_["speech"] = speechPreset;
    
    // Music preset
    AudioPreprocessingConfig musicPreset;
    musicPreset.enableNoiseReduction = false;
    musicPreset.enableVolumeNormalization = true;
    musicPreset.enableEchoCancellation = false;
    musicPreset.volumeNormalization.targetRMS = 0.2f;
    musicPreset.volumeNormalization.compressionRatio = 1.5f;
    presets_["music"] = musicPreset;
    
    // Low quality preset
    AudioPreprocessingConfig lowQualityPreset;
    lowQualityPreset.enableNoiseReduction = true;
    lowQualityPreset.enableVolumeNormalization = true;
    lowQualityPreset.enableEchoCancellation = true;
    lowQualityPreset.noiseReduction.spectralSubtractionAlpha = 3.0f;
    lowQualityPreset.volumeNormalization.compressionRatio = 4.0f;
    lowQualityPreset.echoCancellation.echoSuppressionStrength = 0.9f;
    presets_["low_quality"] = lowQualityPreset;
}

std::vector<float> AudioPreprocessor::applyProcessingPipeline(
    const std::vector<float>& audioData, std::vector<std::string>& appliedFilters,
    std::map<std::string, float>& parameters) {
    
    std::vector<float> processed = audioData;
    
    // Analyze quality to determine which filters to apply
    AudioQualityMetrics quality;
    if (config_.enableQualityAnalysis) {
        quality = qualityAnalyzer_->analyzeQuality(audioData, sampleRate_);
    }
    
    // Apply noise reduction
    if (shouldApplyNoiseReduction(quality)) {
        processed = applyNoiseReduction(processed);
        appliedFilters.push_back("noise_reduction");
        parameters["noise_reduction_strength"] = config_.noiseReduction.spectralSubtractionAlpha;
    }
    
    // Apply volume normalization
    if (shouldApplyVolumeNormalization(quality)) {
        processed = applyVolumeNormalization(processed);
        appliedFilters.push_back("volume_normalization");
        parameters["target_rms"] = config_.volumeNormalization.targetRMS;
        parameters["compression_ratio"] = config_.volumeNormalization.compressionRatio;
    }
    
    // Apply echo cancellation
    if (shouldApplyEchoCancellation(quality)) {
        processed = applyEchoCancellation(processed);
        appliedFilters.push_back("echo_cancellation");
        parameters["echo_suppression_strength"] = config_.echoCancellation.echoSuppressionStrength;
    }
    
    // Apply adaptive processing if enabled
    if (config_.enableAdaptiveProcessing && adaptiveProcessor_) {
        processed = adaptiveProcessor_->processAudio(processed);
        appliedFilters.push_back("adaptive_processing");
    }
    
    return processed;
}

bool AudioPreprocessor::shouldApplyNoiseReduction(const AudioQualityMetrics& quality) {
    if (!config_.enableNoiseReduction) return false;
    
    // Apply if noise is detected or SNR is low
    return quality.hasNoise || quality.signalToNoiseRatio < 15.0f;
}

bool AudioPreprocessor::shouldApplyVolumeNormalization(const AudioQualityMetrics& quality) {
    if (!config_.enableVolumeNormalization) return false;
    
    // Always apply volume normalization for consistency
    return true;
}

bool AudioPreprocessor::shouldApplyEchoCancellation(const AudioQualityMetrics& quality) {
    if (!config_.enableEchoCancellation) return false;
    
    // Apply if echo is detected
    return quality.hasEcho;
}

void AudioPreprocessor::updateStatistics(size_t samplesProcessed, double processingTime,
                                        const std::vector<std::string>& appliedFilters,
                                        float qualityImprovement) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    stats_.totalSamplesProcessed += samplesProcessed;
    stats_.totalChunksProcessed++;
    
    // Update average processing time
    double alpha = 0.1;
    stats_.averageProcessingTime = alpha * processingTime + (1.0 - alpha) * stats_.averageProcessingTime;
    
    // Update average quality improvement
    stats_.averageQualityImprovement = alpha * qualityImprovement + (1.0 - alpha) * stats_.averageQualityImprovement;
    
    // Update filter usage counts
    for (const std::string& filter : appliedFilters) {
        stats_.filterUsageCount[filter]++;
    }
    
    stats_.lastProcessingTime = std::chrono::steady_clock::now();
}

} // namespace audio