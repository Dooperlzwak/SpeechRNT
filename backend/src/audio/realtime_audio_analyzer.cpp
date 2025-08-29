#include "audio/realtime_audio_analyzer.hpp"
#include <algorithm>
#include <numeric>
#include <cstring>
#include <cmath>
#include <cassert>

namespace speechrnt {
namespace audio {

// CircularBuffer implementation
template<typename T>
CircularBuffer<T>::CircularBuffer(size_t capacity) 
    : buffer_(capacity), capacity_(capacity), head_(0), tail_(0), size_(0) {
}

template<typename T>
bool CircularBuffer<T>::push(const T& item) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size_ >= capacity_) {
        return false; // Buffer full
    }
    
    buffer_[head_] = item;
    head_ = (head_ + 1) % capacity_;
    size_++;
    return true;
}

template<typename T>
bool CircularBuffer<T>::push(const std::vector<T>& items) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size_ + items.size() > capacity_) {
        return false; // Not enough space
    }
    
    for (const auto& item : items) {
        buffer_[head_] = item;
        head_ = (head_ + 1) % capacity_;
        size_++;
    }
    return true;
}

template<typename T>
bool CircularBuffer<T>::pop(T& item) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size_ == 0) {
        return false; // Buffer empty
    }
    
    item = buffer_[tail_];
    tail_ = (tail_ + 1) % capacity_;
    size_--;
    return true;
}

template<typename T>
std::vector<T> CircularBuffer<T>::pop(size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    count = std::min(count, size_.load());
    std::vector<T> result;
    result.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        result.push_back(buffer_[tail_]);
        tail_ = (tail_ + 1) % capacity_;
        size_--;
    }
    
    return result;
}

template<typename T>
size_t CircularBuffer<T>::size() const {
    return size_;
}

template<typename T>
size_t CircularBuffer<T>::capacity() const {
    return capacity_;
}

template<typename T>
bool CircularBuffer<T>::empty() const {
    return size_ == 0;
}

template<typename T>
bool CircularBuffer<T>::full() const {
    return size_ >= capacity_;
}

template<typename T>
void CircularBuffer<T>::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    head_ = 0;
    tail_ = 0;
    size_ = 0;
}

template<typename T>
std::vector<T> CircularBuffer<T>::getLatest(size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    count = std::min(count, size_.load());
    std::vector<T> result;
    result.reserve(count);
    
    size_t start = (head_ + capacity_ - count) % capacity_;
    for (size_t i = 0; i < count; ++i) {
        result.push_back(buffer_[(start + i) % capacity_]);
    }
    
    return result;
}

// Explicit template instantiations
template class CircularBuffer<float>;
template class CircularBuffer<RealTimeMetrics>;

// RealTimeFFT implementation
RealTimeFFT::RealTimeFFT(size_t fftSize) 
    : fftSize_(fftSize), windowType_("hann") {
    fftBuffer_.resize(fftSize_ * 2, 0.0f);
    fftOutput_.resize(fftSize_ * 2, 0.0f);
    generateWindow();
}

void RealTimeFFT::setFFTSize(size_t size) {
    fftSize_ = size;
    fftBuffer_.resize(fftSize_ * 2, 0.0f);
    fftOutput_.resize(fftSize_ * 2, 0.0f);
    generateWindow();
}

void RealTimeFFT::setWindowFunction(const std::string& windowType) {
    windowType_ = windowType;
    generateWindow();
}

void RealTimeFFT::generateWindow() {
    window_.resize(fftSize_);
    
    if (windowType_ == "hann") {
        for (size_t i = 0; i < fftSize_; ++i) {
            window_[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (fftSize_ - 1)));
        }
    } else if (windowType_ == "hamming") {
        for (size_t i = 0; i < fftSize_; ++i) {
            window_[i] = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (fftSize_ - 1));
        }
    } else if (windowType_ == "blackman") {
        for (size_t i = 0; i < fftSize_; ++i) {
            window_[i] = 0.42f - 0.5f * std::cos(2.0f * M_PI * i / (fftSize_ - 1)) + 
                        0.08f * std::cos(4.0f * M_PI * i / (fftSize_ - 1));
        }
    } else { // rectangular
        std::fill(window_.begin(), window_.end(), 1.0f);
    }
}

std::vector<float> RealTimeFFT::applyWindow(const std::vector<float>& samples) {
    std::vector<float> windowed(samples.size());
    size_t minSize = std::min(samples.size(), window_.size());
    
    for (size_t i = 0; i < minSize; ++i) {
        windowed[i] = samples[i] * window_[i];
    }
    
    return windowed;
}

std::vector<float> RealTimeFFT::computeFFT(const std::vector<float>& samples) {
    // Prepare input data
    size_t inputSize = std::min(samples.size(), fftSize_);
    std::fill(fftBuffer_.begin(), fftBuffer_.end(), 0.0f);
    
    // Apply window and copy to FFT buffer
    for (size_t i = 0; i < inputSize; ++i) {
        fftBuffer_[i] = samples[i] * window_[i];
    }
    
    // Perform FFT
    performFFT(fftBuffer_.data(), fftOutput_.data());
    
    // Return complex result (real and imaginary interleaved)
    return std::vector<float>(fftOutput_.begin(), fftOutput_.begin() + fftSize_ * 2);
}

std::vector<float> RealTimeFFT::computeMagnitudeSpectrum(const std::vector<float>& samples) {
    auto fftResult = computeFFT(samples);
    std::vector<float> magnitude(fftSize_ / 2 + 1);
    
    for (size_t i = 0; i < magnitude.size(); ++i) {
        float real = fftResult[2 * i];
        float imag = fftResult[2 * i + 1];
        magnitude[i] = std::sqrt(real * real + imag * imag);
    }
    
    return magnitude;
}

std::vector<float> RealTimeFFT::computePowerSpectrum(const std::vector<float>& samples) {
    auto fftResult = computeFFT(samples);
    std::vector<float> power(fftSize_ / 2 + 1);
    
    for (size_t i = 0; i < power.size(); ++i) {
        float real = fftResult[2 * i];
        float imag = fftResult[2 * i + 1];
        power[i] = real * real + imag * imag;
    }
    
    return power;
}

float RealTimeFFT::getFrequencyBin(size_t bin, uint32_t sampleRate) const {
    return static_cast<float>(bin * sampleRate) / (2.0f * fftSize_);
}

size_t RealTimeFFT::getFrequencyBin(float frequency, uint32_t sampleRate) const {
    return static_cast<size_t>(frequency * 2.0f * fftSize_ / sampleRate);
}

std::vector<float> RealTimeFFT::getFrequencyAxis(uint32_t sampleRate) const {
    std::vector<float> frequencies(fftSize_ / 2 + 1);
    for (size_t i = 0; i < frequencies.size(); ++i) {
        frequencies[i] = getFrequencyBin(i, sampleRate);
    }
    return frequencies;
}

void RealTimeFFT::performFFT(const float* input, float* output) {
    // Copy input to output buffer for in-place FFT
    for (size_t i = 0; i < fftSize_; ++i) {
        output[2 * i] = input[i];     // Real part
        output[2 * i + 1] = 0.0f;     // Imaginary part
    }
    
    // Perform Cooley-Tukey FFT
    cooleyTukeyFFT(output, fftSize_);
}

void RealTimeFFT::cooleyTukeyFFT(float* data, size_t n) {
    if (n <= 1) return;
    
    // Bit-reversal permutation
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        
        if (i < j) {
            std::swap(data[2 * i], data[2 * j]);
            std::swap(data[2 * i + 1], data[2 * j + 1]);
        }
    }
    
    // Cooley-Tukey FFT
    for (size_t len = 2; len <= n; len <<= 1) {
        float wlen_real = std::cos(-2.0f * M_PI / len);
        float wlen_imag = std::sin(-2.0f * M_PI / len);
        
        for (size_t i = 0; i < n; i += len) {
            float w_real = 1.0f;
            float w_imag = 0.0f;
            
            for (size_t j = 0; j < len / 2; ++j) {
                size_t u = i + j;
                size_t v = i + j + len / 2;
                
                float u_real = data[2 * u];
                float u_imag = data[2 * u + 1];
                float v_real = data[2 * v];
                float v_imag = data[2 * v + 1];
                
                float temp_real = v_real * w_real - v_imag * w_imag;
                float temp_imag = v_real * w_imag + v_imag * w_real;
                
                data[2 * u] = u_real + temp_real;
                data[2 * u + 1] = u_imag + temp_imag;
                data[2 * v] = u_real - temp_real;
                data[2 * v + 1] = u_imag - temp_imag;
                
                float temp_w_real = w_real * wlen_real - w_imag * wlen_imag;
                w_imag = w_real * wlen_imag + w_imag * wlen_real;
                w_real = temp_w_real;
            }
        }
    }
}

// AudioEffectsProcessor implementation
AudioEffectsProcessor::AudioEffectsProcessor(const AudioEffectsConfig& config)
    : config_(config), compressorEnvelope_(0.0f), noiseGateEnvelope_(0.0f) {
    reverbBuffer_.resize(8192, 0.0f); // 8192 samples for reverb delay
    equalizerState_.resize(10, 0.0f); // 10 bands for equalizer
}

std::vector<float> AudioEffectsProcessor::processEffects(const std::vector<float>& input) {
    std::vector<float> output = input;
    
    if (config_.enableNoiseGate) {
        output = applyNoiseGate(output);
    }
    
    if (config_.enableCompressor) {
        output = applyCompressor(output);
    }
    
    if (config_.enableEqualizer) {
        output = applyEqualizer(output);
    }
    
    if (config_.enableReverb) {
        output = applyReverb(output);
    }
    
    return output;
}

std::vector<float> AudioEffectsProcessor::applyCompressor(const std::vector<float>& input) {
    std::vector<float> output(input.size());
    
    float threshold = std::pow(10.0f, config_.compressorThreshold / 20.0f);
    float ratio = config_.compressorRatio;
    float attack = std::exp(-1.0f / (config_.compressorAttack * 0.001f * 16000.0f)); // Assuming 16kHz
    float release = std::exp(-1.0f / (config_.compressorRelease * 0.001f * 16000.0f));
    
    for (size_t i = 0; i < input.size(); ++i) {
        float inputLevel = std::abs(input[i]);
        
        // Update envelope
        if (inputLevel > compressorEnvelope_) {
            compressorEnvelope_ = attack * compressorEnvelope_ + (1.0f - attack) * inputLevel;
        } else {
            compressorEnvelope_ = release * compressorEnvelope_ + (1.0f - release) * inputLevel;
        }
        
        // Apply compression
        float gain = 1.0f;
        if (compressorEnvelope_ > threshold) {
            float overThreshold = compressorEnvelope_ / threshold;
            float compressedGain = std::pow(overThreshold, (1.0f / ratio) - 1.0f);
            gain = compressedGain;
        }
        
        output[i] = input[i] * gain;
    }
    
    return output;
}

std::vector<float> AudioEffectsProcessor::applyNoiseGate(const std::vector<float>& input) {
    std::vector<float> output(input.size());
    
    float threshold = std::pow(10.0f, config_.noiseGateThreshold / 20.0f);
    float ratio = config_.noiseGateRatio;
    float attack = 0.99f;  // Fast attack
    float release = 0.9999f; // Slow release
    
    for (size_t i = 0; i < input.size(); ++i) {
        float inputLevel = std::abs(input[i]);
        
        // Update envelope
        if (inputLevel > noiseGateEnvelope_) {
            noiseGateEnvelope_ = attack * noiseGateEnvelope_ + (1.0f - attack) * inputLevel;
        } else {
            noiseGateEnvelope_ = release * noiseGateEnvelope_ + (1.0f - release) * inputLevel;
        }
        
        // Apply gating
        float gain = 1.0f;
        if (noiseGateEnvelope_ < threshold) {
            gain = 1.0f / ratio;
        }
        
        output[i] = input[i] * gain;
    }
    
    return output;
}

std::vector<float> AudioEffectsProcessor::applyEqualizer(const std::vector<float>& input) {
    // Simple 3-band equalizer implementation
    // This is a placeholder - a real implementation would use proper filter design
    return input; // Pass-through for now
}

std::vector<float> AudioEffectsProcessor::applyReverb(const std::vector<float>& input) {
    std::vector<float> output(input.size());
    
    // Simple delay-based reverb
    const float reverbMix = 0.3f;
    const size_t delayLength = 4096;
    
    for (size_t i = 0; i < input.size(); ++i) {
        // Add input to reverb buffer
        size_t writeIndex = i % reverbBuffer_.size();
        reverbBuffer_[writeIndex] = input[i] + reverbBuffer_[writeIndex] * 0.5f;
        
        // Read delayed signal
        size_t readIndex = (i + reverbBuffer_.size() - delayLength) % reverbBuffer_.size();
        float delayedSignal = reverbBuffer_[readIndex];
        
        // Mix dry and wet signals
        output[i] = input[i] + delayedSignal * reverbMix;
    }
    
    return output;
}

void AudioEffectsProcessor::updateConfig(const AudioEffectsConfig& config) {
    config_ = config;
}

void AudioEffectsProcessor::resetEffectStates() {
    compressorEnvelope_ = 0.0f;
    noiseGateEnvelope_ = 0.0f;
    std::fill(reverbBuffer_.begin(), reverbBuffer_.end(), 0.0f);
    std::fill(equalizerState_.begin(), equalizerState_.end(), 0.0f);
}

// RealTimeAudioAnalyzer implementation
RealTimeAudioAnalyzer::RealTimeAudioAnalyzer(uint32_t sampleRate, size_t bufferSize)
    : sampleRate_(sampleRate), bufferSize_(bufferSize), 
      updateInterval_(std::chrono::milliseconds(50)), // 50ms update interval
      initialized_(false), running_(false), effectsEnabled_(false),
      noiseFloorThreshold_(-60.0f), silenceThreshold_(0.01f), 
      clippingThreshold_(0.95f), speechDetectionSensitivity_(0.5f),
      runningAverage_(0.0f), peakHold_(0.0f), spectralFluxAccumulator_(0.0f),
      expectingAudio_(false) {
    
    // Initialize buffers
    audioBuffer_ = std::make_unique<CircularBuffer<float>>(bufferSize * 10); // 10x buffer size
    metricsBuffer_ = std::make_unique<CircularBuffer<RealTimeMetrics>>(1000); // Store 1000 metrics
    
    // Initialize processing components
    fftProcessor_ = std::make_unique<RealTimeFFT>(bufferSize);
    
    AudioEffectsConfig defaultEffectsConfig;
    effectsProcessor_ = std::make_unique<AudioEffectsProcessor>(defaultEffectsConfig);
    
    // Initialize level history
    levelHistory_.resize(100, 0.0f); // Store 100 level samples
    
    // Initialize performance metrics
    performanceMetrics_ = {};
    lastPerformanceUpdate_ = std::chrono::steady_clock::now();
}

RealTimeAudioAnalyzer::~RealTimeAudioAnalyzer() {
    shutdown();
}

bool RealTimeAudioAnalyzer::initialize() {
    if (initialized_) {
        return true;
    }
    
    try {
        // Start processing thread
        running_ = true;
        processingThread_ = std::make_unique<std::thread>(&RealTimeAudioAnalyzer::processingLoop, this);
        
        initialized_ = true;
        return true;
    } catch (const std::exception& e) {
        running_ = false;
        return false;
    }
}

void RealTimeAudioAnalyzer::shutdown() {
    if (!initialized_) {
        return;
    }
    
    running_ = false;
    
    if (processingThread_ && processingThread_->joinable()) {
        processingThread_->join();
    }
    
    initialized_ = false;
}

void RealTimeAudioAnalyzer::processAudioSample(float sample) {
    if (!initialized_) {
        return;
    }
    
    audioBuffer_->push(sample);
    lastAudioTime_ = std::chrono::steady_clock::now();
    expectingAudio_ = true;
}

void RealTimeAudioAnalyzer::processAudioChunk(const std::vector<float>& chunk) {
    if (!initialized_ || chunk.empty()) {
        return;
    }
    
    audioBuffer_->push(chunk);
    lastAudioTime_ = std::chrono::steady_clock::now();
    expectingAudio_ = true;
}

void RealTimeAudioAnalyzer::processAudioChunk(const float* samples, size_t count) {
    if (!initialized_ || !samples || count == 0) {
        return;
    }
    
    std::vector<float> chunk(samples, samples + count);
    processAudioChunk(chunk);
}

RealTimeMetrics RealTimeAudioAnalyzer::getCurrentMetrics() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return currentMetrics_;
}

std::vector<RealTimeMetrics> RealTimeAudioAnalyzer::getMetricsHistory(size_t samples) const {
    return metricsBuffer_->getLatest(samples);
}

AudioLevelMetrics RealTimeAudioAnalyzer::getCurrentLevels() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return currentLevels_;
}

SpectralAnalysis RealTimeAudioAnalyzer::getCurrentSpectralAnalysis() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return currentSpectral_;
}

void RealTimeAudioAnalyzer::registerMetricsCallback(MetricsCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    metricsCallbacks_.push_back(callback);
}

void RealTimeAudioAnalyzer::registerLevelsCallback(LevelsCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    levelsCallbacks_.push_back(callback);
}

void RealTimeAudioAnalyzer::registerSpectralCallback(SpectralCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    spectralCallbacks_.push_back(callback);
}

void RealTimeAudioAnalyzer::clearCallbacks() {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    metricsCallbacks_.clear();
    levelsCallbacks_.clear();
    spectralCallbacks_.clear();
}

void RealTimeAudioAnalyzer::enableRealTimeEffects(bool enabled) {
    effectsEnabled_ = enabled;
}

std::vector<float> RealTimeAudioAnalyzer::applyRealTimeEffects(const std::vector<float>& audio) {
    if (!effectsEnabled_ || !effectsProcessor_) {
        return audio;
    }
    
    return effectsProcessor_->processEffects(audio);
}

void RealTimeAudioAnalyzer::updateEffectsConfig(const AudioEffectsConfig& config) {
    if (effectsProcessor_) {
        effectsProcessor_->updateConfig(config);
    }
}

void RealTimeAudioAnalyzer::setSampleRate(uint32_t sampleRate) {
    sampleRate_ = sampleRate;
}

void RealTimeAudioAnalyzer::setBufferSize(size_t bufferSize) {
    bufferSize_ = bufferSize;
    if (fftProcessor_) {
        fftProcessor_->setFFTSize(bufferSize);
    }
}

void RealTimeAudioAnalyzer::setUpdateInterval(std::chrono::milliseconds interval) {
    updateInterval_ = interval;
}

void RealTimeAudioAnalyzer::processingLoop() {
    while (running_) {
        auto startTime = std::chrono::steady_clock::now();
        
        // Get audio samples for analysis
        auto samples = audioBuffer_->getLatest(bufferSize_);
        
        if (!samples.empty()) {
            // Update level metrics
            updateLevelMetrics(samples);
            
            // Update spectral analysis
            updateSpectralAnalysis(samples);
            
            // Update noise estimation
            updateNoiseEstimation(samples);
            
            // Update speech detection
            updateSpeechDetection(currentLevels_, currentSpectral_);
            
            // Detect dropouts
            detectDropouts(samples);
            
            // Update current metrics
            {
                std::lock_guard<std::mutex> lock(metricsMutex_);
                currentMetrics_.levels = currentLevels_;
                currentMetrics_.spectral = currentSpectral_;
                currentMetrics_.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                currentMetrics_.sequenceNumber++;
            }
            
            // Store metrics in history
            metricsBuffer_->push(currentMetrics_);
            
            // Notify callbacks
            notifyMetricsCallbacks(currentMetrics_);
            notifyLevelsCallbacks(currentLevels_);
            notifySpectralCallbacks(currentSpectral_);
        }
        
        // Update performance metrics
        auto endTime = std::chrono::steady_clock::now();
        auto processingTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        updatePerformanceMetrics(processingTime);
        
        // Sleep until next update
        std::this_thread::sleep_for(updateInterval_);
    }
}

void RealTimeAudioAnalyzer::updateLevelMetrics(const std::vector<float>& samples) {
    if (samples.empty()) {
        return;
    }
    
    // Calculate RMS level
    float rms = calculateRMS(samples);
    currentLevels_.currentLevel = rms;
    
    // Calculate peak level
    float peak = calculatePeak(samples);
    currentLevels_.peakLevel = peak;
    
    // Update running average
    runningAverage_ = 0.95f * runningAverage_ + 0.05f * rms;
    currentLevels_.averageLevel = runningAverage_;
    
    // Update peak hold
    if (peak > peakHold_) {
        peakHold_ = peak;
        lastPeakTime_ = std::chrono::steady_clock::now();
    } else {
        // Decay peak hold over time
        auto now = std::chrono::steady_clock::now();
        auto timeSincePeak = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPeakTime_);
        if (timeSincePeak.count() > 1000) { // 1 second decay
            peakHold_ *= 0.99f;
        }
    }
    currentLevels_.peakHoldLevel = peakHold_;
    
    // Detect clipping and silence
    currentLevels_.clipping = detectClipping(samples, clippingThreshold_);
    currentLevels_.silence = detectSilence(samples, silenceThreshold_);
    
    // Update level history
    levelHistory_.erase(levelHistory_.begin());
    levelHistory_.push_back(rms);
}

void RealTimeAudioAnalyzer::updateSpectralAnalysis(const std::vector<float>& samples) {
    if (samples.empty() || !fftProcessor_) {
        return;
    }
    
    // Compute power spectrum
    auto powerSpectrum = fftProcessor_->computePowerSpectrum(samples);
    currentSpectral_.powerSpectrum = powerSpectrum;
    
    // Compute magnitude spectrum
    auto magnitudeSpectrum = fftProcessor_->computeMagnitudeSpectrum(samples);
    currentSpectral_.frequencySpectrum = magnitudeSpectrum;
    
    // Calculate spectral features
    currentSpectral_.spectralCentroid = calculateSpectralCentroid(magnitudeSpectrum, sampleRate_);
    currentSpectral_.spectralBandwidth = calculateSpectralBandwidth(magnitudeSpectrum, 
                                                                   currentSpectral_.spectralCentroid, sampleRate_);
    currentSpectral_.spectralRolloff = calculateSpectralRolloff(magnitudeSpectrum, sampleRate_);
    currentSpectral_.spectralFlatness = calculateSpectralFlatness(magnitudeSpectrum);
    
    // Calculate spectral flux
    if (!previousSpectrum_.empty()) {
        currentSpectral_.spectralFlux = calculateSpectralFlux(magnitudeSpectrum, previousSpectrum_);
    }
    previousSpectrum_ = magnitudeSpectrum;
    
    // Find dominant frequency
    auto maxIt = std::max_element(magnitudeSpectrum.begin(), magnitudeSpectrum.end());
    if (maxIt != magnitudeSpectrum.end()) {
        size_t maxBin = std::distance(magnitudeSpectrum.begin(), maxIt);
        currentSpectral_.dominantFrequency = fftProcessor_->getFrequencyBin(maxBin, sampleRate_);
    }
    
    // Calculate MFCC coefficients (simplified)
    currentSpectral_.mfccCoefficients = calculateMFCC(magnitudeSpectrum, sampleRate_);
}

void RealTimeAudioAnalyzer::updateNoiseEstimation(const std::vector<float>& samples) {
    if (samples.empty()) {
        return;
    }
    
    float noiseLevel = estimateNoiseLevel(samples);
    currentMetrics_.noiseLevel = noiseLevel;
}

void RealTimeAudioAnalyzer::updateSpeechDetection(const AudioLevelMetrics& levels, const SpectralAnalysis& spectral) {
    // Simple speech detection based on level and spectral characteristics
    float speechProb = calculateSpeechProbability(levels, spectral);
    currentMetrics_.speechProbability = speechProb;
    currentMetrics_.voiceActivityScore = speechProb;
}

void RealTimeAudioAnalyzer::detectDropouts(const std::vector<float>& samples) {
    // Simple dropout detection based on sudden level drops
    if (samples.empty()) {
        return;
    }
    
    float currentLevel = calculateRMS(samples);
    static float previousLevel = currentLevel;
    
    // Detect sudden level drop
    if (previousLevel > 0.1f && currentLevel < 0.01f) {
        AudioDropout dropout;
        dropout.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        dropout.durationMs = static_cast<float>(samples.size()) / sampleRate_ * 1000.0f;
        dropout.severityScore = (previousLevel - currentLevel) / previousLevel;
        dropout.description = "Audio level dropout detected";
        
        std::lock_guard<std::mutex> lock(dropoutMutex_);
        detectedDropouts_.push_back(dropout);
        
        // Keep only recent dropouts (last 10 seconds)
        auto cutoffTime = dropout.timestampMs - 10000;
        detectedDropouts_.erase(
            std::remove_if(detectedDropouts_.begin(), detectedDropouts_.end(),
                          [cutoffTime](const AudioDropout& d) { return d.timestampMs < cutoffTime; }),
            detectedDropouts_.end());
    }
    
    previousLevel = currentLevel;
}

void RealTimeAudioAnalyzer::updatePerformanceMetrics(std::chrono::microseconds processingTime) {
    std::lock_guard<std::mutex> lock(performanceMutex_);
    
    float processingTimeMs = processingTime.count() / 1000.0f;
    
    // Update average processing time
    performanceMetrics_.averageProcessingTimeMs = 
        0.9f * performanceMetrics_.averageProcessingTimeMs + 0.1f * processingTimeMs;
    
    // Update max processing time
    if (processingTimeMs > performanceMetrics_.maxProcessingTimeMs) {
        performanceMetrics_.maxProcessingTimeMs = processingTimeMs;
    }
    
    performanceMetrics_.totalSamplesProcessed += bufferSize_;
}

// Helper function implementations
float RealTimeAudioAnalyzer::calculateRMS(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0f;
    
    float sum = 0.0f;
    for (float sample : samples) {
        sum += sample * sample;
    }
    return std::sqrt(sum / samples.size());
}

float RealTimeAudioAnalyzer::calculatePeak(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0f;
    
    float peak = 0.0f;
    for (float sample : samples) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

float RealTimeAudioAnalyzer::calculateSpectralCentroid(const std::vector<float>& spectrum, uint32_t sampleRate) {
    if (spectrum.empty()) return 0.0f;
    
    float weightedSum = 0.0f;
    float magnitudeSum = 0.0f;
    
    for (size_t i = 0; i < spectrum.size(); ++i) {
        float frequency = fftProcessor_->getFrequencyBin(i, sampleRate);
        weightedSum += frequency * spectrum[i];
        magnitudeSum += spectrum[i];
    }
    
    return magnitudeSum > 0.0f ? weightedSum / magnitudeSum : 0.0f;
}

float RealTimeAudioAnalyzer::calculateSpectralBandwidth(const std::vector<float>& spectrum, float centroid, uint32_t sampleRate) {
    if (spectrum.empty()) return 0.0f;
    
    float weightedSum = 0.0f;
    float magnitudeSum = 0.0f;
    
    for (size_t i = 0; i < spectrum.size(); ++i) {
        float frequency = fftProcessor_->getFrequencyBin(i, sampleRate);
        float deviation = frequency - centroid;
        weightedSum += deviation * deviation * spectrum[i];
        magnitudeSum += spectrum[i];
    }
    
    return magnitudeSum > 0.0f ? std::sqrt(weightedSum / magnitudeSum) : 0.0f;
}

float RealTimeAudioAnalyzer::calculateSpectralRolloff(const std::vector<float>& spectrum, uint32_t sampleRate, float rolloffPercent) {
    if (spectrum.empty()) return 0.0f;
    
    float totalEnergy = std::accumulate(spectrum.begin(), spectrum.end(), 0.0f);
    float targetEnergy = totalEnergy * rolloffPercent;
    
    float cumulativeEnergy = 0.0f;
    for (size_t i = 0; i < spectrum.size(); ++i) {
        cumulativeEnergy += spectrum[i];
        if (cumulativeEnergy >= targetEnergy) {
            return fftProcessor_->getFrequencyBin(i, sampleRate);
        }
    }
    
    return fftProcessor_->getFrequencyBin(spectrum.size() - 1, sampleRate);
}

float RealTimeAudioAnalyzer::calculateSpectralFlatness(const std::vector<float>& spectrum) {
    if (spectrum.empty()) return 0.0f;
    
    float geometricMean = 1.0f;
    float arithmeticMean = 0.0f;
    size_t validBins = 0;
    
    for (float magnitude : spectrum) {
        if (magnitude > 0.0f) {
            geometricMean *= std::pow(magnitude, 1.0f / spectrum.size());
            arithmeticMean += magnitude;
            validBins++;
        }
    }
    
    if (validBins == 0) return 0.0f;
    
    arithmeticMean /= validBins;
    return arithmeticMean > 0.0f ? geometricMean / arithmeticMean : 0.0f;
}

float RealTimeAudioAnalyzer::calculateSpectralFlux(const std::vector<float>& currentSpectrum, const std::vector<float>& previousSpectrum) {
    if (currentSpectrum.size() != previousSpectrum.size()) return 0.0f;
    
    float flux = 0.0f;
    for (size_t i = 0; i < currentSpectrum.size(); ++i) {
        float diff = currentSpectrum[i] - previousSpectrum[i];
        flux += diff * diff;
    }
    
    return std::sqrt(flux);
}

std::vector<float> RealTimeAudioAnalyzer::calculateMFCC(const std::vector<float>& spectrum, uint32_t sampleRate) {
    // Simplified MFCC calculation - in a real implementation, this would use mel-scale filtering
    std::vector<float> mfcc(13, 0.0f);
    
    if (spectrum.empty()) return mfcc;
    
    // Simple approximation: use log of spectrum bins as MFCC coefficients
    size_t step = spectrum.size() / 13;
    for (size_t i = 0; i < 13 && i * step < spectrum.size(); ++i) {
        float value = spectrum[i * step];
        mfcc[i] = value > 0.0f ? std::log(value + 1e-10f) : -10.0f;
    }
    
    return mfcc;
}

float RealTimeAudioAnalyzer::estimateNoiseLevel(const std::vector<float>& samples) {
    if (samples.empty()) return noiseFloorThreshold_;
    
    // Simple noise estimation: use minimum RMS over a sliding window
    float rms = calculateRMS(samples);
    float noiseEstimate = std::min(rms, 0.1f); // Cap at reasonable level
    
    // Convert to dB
    return 20.0f * std::log10(noiseEstimate + 1e-10f);
}

float RealTimeAudioAnalyzer::calculateSpeechProbability(const AudioLevelMetrics& levels, const SpectralAnalysis& spectral) {
    // Simple speech detection heuristic
    float levelScore = levels.currentLevel > silenceThreshold_ ? 1.0f : 0.0f;
    float spectralScore = spectral.spectralCentroid > 200.0f && spectral.spectralCentroid < 4000.0f ? 1.0f : 0.0f;
    float bandwidthScore = spectral.spectralBandwidth > 100.0f ? 1.0f : 0.0f;
    
    return (levelScore + spectralScore + bandwidthScore) / 3.0f;
}

bool RealTimeAudioAnalyzer::detectClipping(const std::vector<float>& samples, float threshold) {
    for (float sample : samples) {
        if (std::abs(sample) >= threshold) {
            return true;
        }
    }
    return false;
}

bool RealTimeAudioAnalyzer::detectSilence(const std::vector<float>& samples, float threshold) {
    float rms = calculateRMS(samples);
    return rms < threshold;
}

void RealTimeAudioAnalyzer::notifyMetricsCallbacks(const RealTimeMetrics& metrics) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    for (const auto& callback : metricsCallbacks_) {
        try {
            callback(metrics);
        } catch (...) {
            // Ignore callback exceptions
        }
    }
}

void RealTimeAudioAnalyzer::notifyLevelsCallbacks(const AudioLevelMetrics& levels) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    for (const auto& callback : levelsCallbacks_) {
        try {
            callback(levels);
        } catch (...) {
            // Ignore callback exceptions
        }
    }
}

void RealTimeAudioAnalyzer::notifySpectralCallbacks(const SpectralAnalysis& spectral) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    for (const auto& callback : spectralCallbacks_) {
        try {
            callback(spectral);
        } catch (...) {
            // Ignore callback exceptions
        }
    }
}

std::vector<RealTimeAudioAnalyzer::AudioDropout> RealTimeAudioAnalyzer::getDetectedDropouts() const {
    std::lock_guard<std::mutex> lock(dropoutMutex_);
    return detectedDropouts_;
}

void RealTimeAudioAnalyzer::clearDropoutHistory() {
    std::lock_guard<std::mutex> lock(dropoutMutex_);
    detectedDropouts_.clear();
}

bool RealTimeAudioAnalyzer::hasRecentDropouts(std::chrono::milliseconds timeWindow) const {
    std::lock_guard<std::mutex> lock(dropoutMutex_);
    
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto cutoffTime = now - timeWindow.count();
    
    return std::any_of(detectedDropouts_.begin(), detectedDropouts_.end(),
                      [cutoffTime](const AudioDropout& d) { return d.timestampMs >= cutoffTime; });
}

RealTimeAudioAnalyzer::PerformanceMetrics RealTimeAudioAnalyzer::getPerformanceMetrics() const {
    std::lock_guard<std::mutex> lock(performanceMutex_);
    return performanceMetrics_;
}

void RealTimeAudioAnalyzer::resetPerformanceMetrics() {
    std::lock_guard<std::mutex> lock(performanceMutex_);
    performanceMetrics_ = {};
    lastPerformanceUpdate_ = std::chrono::steady_clock::now();
}

} // namespace audio
} // namespace speechrnt