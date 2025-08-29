#include "audio/audio_utils.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <complex>
#include <unordered_set>
#include <sstream>
#include <chrono>
#include <mutex>
#include <atomic>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

// ExtendedAudioFormat implementation
bool ExtendedAudioFormat::isValid() const {
    return static_cast<uint32_t>(sampleRate) > 0 && 
           channels > 0 && 
           codec != AudioCodec::UNKNOWN && 
           chunkSize > 0;
}

size_t ExtendedAudioFormat::getBytesPerSample() const {
    switch (codec) {
        case AudioCodec::PCM_16: return 2;
        case AudioCodec::PCM_24: return 3;
        case AudioCodec::PCM_32: return 4;
        case AudioCodec::FLOAT_32: return 4;
        default: return 0;
    }
}

size_t ExtendedAudioFormat::getChunkSizeBytes() const {
    return chunkSize * channels * getBytesPerSample();
}

std::string ExtendedAudioFormat::toString() const {
    std::stringstream ss;
    ss << static_cast<uint32_t>(sampleRate) << "Hz, " 
       << channels << " channel(s), ";
    
    switch (codec) {
        case AudioCodec::PCM_16: ss << "16-bit PCM"; break;
        case AudioCodec::PCM_24: ss << "24-bit PCM"; break;
        case AudioCodec::PCM_32: ss << "32-bit PCM"; break;
        case AudioCodec::FLOAT_32: ss << "32-bit Float"; break;
        default: ss << "Unknown"; break;
    }
    
    ss << ", " << chunkSize << " samples/chunk";
    return ss.str();
}

// AudioQualityMetrics implementation
AudioQualityMetrics::AudioQualityMetrics() 
    : signalToNoiseRatio(0.0f), totalHarmonicDistortion(0.0f), dynamicRange(0.0f),
      peakAmplitude(0.0f), rmsLevel(0.0f), zeroCrossingRate(0.0f), spectralCentroid(0.0f),
      hasClipping(false), hasSilence(false), noiseFloor(-60.0f) {
}

bool AudioQualityMetrics::isGoodQuality() const {
    return signalToNoiseRatio > 20.0f &&
           totalHarmonicDistortion < 5.0f &&
           dynamicRange > 30.0f &&
           !hasClipping &&
           rmsLevel > 0.01f;
}

std::string AudioQualityMetrics::getQualityDescription() const {
    if (isGoodQuality()) {
        return "Good quality audio";
    }
    
    std::vector<std::string> issues;
    if (signalToNoiseRatio < 20.0f) issues.push_back("Low SNR");
    if (totalHarmonicDistortion > 5.0f) issues.push_back("High distortion");
    if (dynamicRange < 30.0f) issues.push_back("Limited dynamic range");
    if (hasClipping) issues.push_back("Clipping detected");
    if (hasSilence) issues.push_back("Silence detected");
    if (rmsLevel < 0.01f) issues.push_back("Very low level");
    
    std::stringstream ss;
    ss << "Quality issues: ";
    for (size_t i = 0; i < issues.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << issues[i];
    }
    return ss.str();
}

// NoiseProfile implementation
NoiseProfile::NoiseProfile() 
    : noiseLevel(-60.0f), speechLevel(-20.0f), noiseFloor(-60.0f),
      hasBackgroundNoise(false), hasImpulseNoise(false), hasPeriodicNoise(false) {
}

float NoiseProfile::getSNR() const {
    return speechLevel - noiseLevel;
}

bool NoiseProfile::requiresDenoising() const {
    return getSNR() < 15.0f || hasBackgroundNoise || hasImpulseNoise;
}

// AudioFormatValidator implementation
const std::vector<SampleRate> AudioFormatValidator::supportedSampleRates_ = {
    SampleRate::SR_8000, SampleRate::SR_16000, SampleRate::SR_22050,
    SampleRate::SR_44100, SampleRate::SR_48000
};

const std::vector<AudioCodec> AudioFormatValidator::supportedCodecs_ = {
    AudioCodec::PCM_16, AudioCodec::PCM_24, AudioCodec::PCM_32, AudioCodec::FLOAT_32
};

const std::unordered_map<AudioCodec, size_t> AudioFormatValidator::codecBytesPerSample_ = {
    {AudioCodec::PCM_16, 2},
    {AudioCodec::PCM_24, 3},
    {AudioCodec::PCM_32, 4},
    {AudioCodec::FLOAT_32, 4}
};

bool AudioFormatValidator::isFormatSupported(const ExtendedAudioFormat& format) {
    // Check sample rate
    auto srIt = std::find(supportedSampleRates_.begin(), supportedSampleRates_.end(), format.sampleRate);
    if (srIt == supportedSampleRates_.end()) {
        return false;
    }
    
    // Check codec
    auto codecIt = std::find(supportedCodecs_.begin(), supportedCodecs_.end(), format.codec);
    if (codecIt == supportedCodecs_.end()) {
        return false;
    }
    
    // Check channels (support mono and stereo)
    if (format.channels == 0 || format.channels > 2) {
        return false;
    }
    
    // Check chunk size
    if (format.chunkSize == 0 || format.chunkSize > 8192) {
        return false;
    }
    
    return true;
}

bool AudioFormatValidator::canConvertFormat(const ExtendedAudioFormat& from, const ExtendedAudioFormat& to) {
    return isFormatSupported(from) && isFormatSupported(to);
}

std::vector<ExtendedAudioFormat> AudioFormatValidator::getSupportedFormats() {
    std::vector<ExtendedAudioFormat> formats;
    
    for (auto sr : supportedSampleRates_) {
        for (auto codec : supportedCodecs_) {
            for (uint16_t channels = 1; channels <= 2; ++channels) {
                formats.emplace_back(sr, channels, codec, 1024);
            }
        }
    }
    
    return formats;
}

bool AudioFormatValidator::validateAudioData(std::string_view data, const ExtendedAudioFormat& format) {
    if (!format.isValid()) {
        return false;
    }
    
    size_t expectedBytes = format.getBytesPerSample();
    if (data.size() % expectedBytes != 0) {
        utils::Logger::warn("Audio data size not aligned to sample size");
        return false;
    }
    
    size_t sampleCount = data.size() / expectedBytes;
    if (sampleCount % format.channels != 0) {
        utils::Logger::warn("Audio data not aligned to channel count");
        return false;
    }
    
    return true;
}

bool AudioFormatValidator::validateSampleRate(uint32_t sampleRate) {
    SampleRate sr = static_cast<SampleRate>(sampleRate);
    return std::find(supportedSampleRates_.begin(), supportedSampleRates_.end(), sr) != supportedSampleRates_.end();
}

bool AudioFormatValidator::validateChannelCount(uint16_t channels) {
    return channels >= 1 && channels <= 2;
}

bool AudioFormatValidator::validateBitDepth(const AudioCodec& codec) {
    return std::find(supportedCodecs_.begin(), supportedCodecs_.end(), codec) != supportedCodecs_.end();
}

ExtendedAudioFormat AudioFormatValidator::detectFormat(std::string_view data) {
    ExtendedAudioFormat format;
    
    // Simple heuristic-based format detection
    if (data.size() % 2 == 0) {
        format.codec = AudioCodec::PCM_16;
    } else if (data.size() % 4 == 0) {
        format.codec = AudioCodec::PCM_32;
    } else {
        format.codec = AudioCodec::UNKNOWN;
    }
    
    // Default to common format
    format.sampleRate = SampleRate::SR_16000;
    format.channels = 1;
    format.chunkSize = data.size() / format.getBytesPerSample();
    
    return format;
}

AudioCodec AudioFormatValidator::detectCodec(std::string_view data) {
    if (data.size() % 4 == 0) {
        // Could be 32-bit PCM or float
        return AudioCodec::PCM_32;
    } else if (data.size() % 2 == 0) {
        return AudioCodec::PCM_16;
    } else if (data.size() % 3 == 0) {
        return AudioCodec::PCM_24;
    }
    
    return AudioCodec::UNKNOWN;
}

// AudioFormatConverter implementation
std::vector<float> AudioFormatConverter::resample(const std::vector<float>& input, 
                                                 uint32_t inputRate, uint32_t outputRate) {
    if (inputRate == outputRate) {
        return input;
    }
    
    float ratio = static_cast<float>(outputRate) / static_cast<float>(inputRate);
    size_t outputSize = static_cast<size_t>(input.size() * ratio);
    std::vector<float> output;
    output.reserve(outputSize);
    
    for (size_t i = 0; i < outputSize; ++i) {
        float srcIndex = static_cast<float>(i) / ratio;
        output.push_back(interpolate(input, srcIndex));
    }
    
    return output;
}

std::vector<float> AudioFormatConverter::upsample(const std::vector<float>& input, uint32_t factor) {
    std::vector<float> output;
    output.reserve(input.size() * factor);
    
    for (float sample : input) {
        output.push_back(sample);
        for (uint32_t i = 1; i < factor; ++i) {
            output.push_back(0.0f); // Zero-padding
        }
    }
    
    return applyAntiAliasingFilter(output);
}

std::vector<float> AudioFormatConverter::downsample(const std::vector<float>& input, uint32_t factor) {
    // Apply anti-aliasing filter first
    auto filtered = applyAntiAliasingFilter(input);
    
    std::vector<float> output;
    output.reserve(filtered.size() / factor);
    
    for (size_t i = 0; i < filtered.size(); i += factor) {
        output.push_back(filtered[i]);
    }
    
    return output;
}

std::vector<float> AudioFormatConverter::stereoToMono(const std::vector<float>& stereoData) {
    if (stereoData.size() % 2 != 0) {
        utils::Logger::warn("Stereo data size is not even");
        return {};
    }
    
    std::vector<float> mono;
    mono.reserve(stereoData.size() / 2);
    
    for (size_t i = 0; i < stereoData.size(); i += 2) {
        float monoSample = (stereoData[i] + stereoData[i + 1]) * 0.5f;
        mono.push_back(monoSample);
    }
    
    return mono;
}

std::vector<float> AudioFormatConverter::monoToStereo(const std::vector<float>& monoData) {
    std::vector<float> stereo;
    stereo.reserve(monoData.size() * 2);
    
    for (float sample : monoData) {
        stereo.push_back(sample); // Left channel
        stereo.push_back(sample); // Right channel
    }
    
    return stereo;
}

std::vector<float> AudioFormatConverter::convertChannels(const std::vector<float>& input, 
                                                        uint16_t inputChannels, uint16_t outputChannels) {
    if (inputChannels == outputChannels) {
        return input;
    }
    
    if (inputChannels == 2 && outputChannels == 1) {
        return stereoToMono(input);
    } else if (inputChannels == 1 && outputChannels == 2) {
        return monoToStereo(input);
    }
    
    utils::Logger::warn("Unsupported channel conversion: " + std::to_string(inputChannels) + 
                       " to " + std::to_string(outputChannels));
    return input;
}

std::vector<float> AudioFormatConverter::convertToFloat(std::string_view data, AudioCodec codec) {
    std::vector<float> samples;
    
    switch (codec) {
        case AudioCodec::PCM_16: {
            const int16_t* pcmData = reinterpret_cast<const int16_t*>(data.data());
            size_t sampleCount = data.size() / 2;
            samples.reserve(sampleCount);
            
            for (size_t i = 0; i < sampleCount; ++i) {
                samples.push_back(static_cast<float>(pcmData[i]) / 32768.0f);
            }
            break;
        }
        case AudioCodec::PCM_24: {
            const uint8_t* byteData = reinterpret_cast<const uint8_t*>(data.data());
            size_t sampleCount = data.size() / 3;
            samples.reserve(sampleCount);
            
            for (size_t i = 0; i < sampleCount; ++i) {
                int32_t sample = (byteData[i*3] << 8) | (byteData[i*3+1] << 16) | (byteData[i*3+2] << 24);
                sample >>= 8; // Sign extend
                samples.push_back(static_cast<float>(sample) / 8388608.0f);
            }
            break;
        }
        case AudioCodec::PCM_32: {
            const int32_t* pcmData = reinterpret_cast<const int32_t*>(data.data());
            size_t sampleCount = data.size() / 4;
            samples.reserve(sampleCount);
            
            for (size_t i = 0; i < sampleCount; ++i) {
                samples.push_back(static_cast<float>(pcmData[i]) / 2147483648.0f);
            }
            break;
        }
        case AudioCodec::FLOAT_32: {
            const float* floatData = reinterpret_cast<const float*>(data.data());
            size_t sampleCount = data.size() / 4;
            samples.assign(floatData, floatData + sampleCount);
            break;
        }
        default:
            utils::Logger::error("Unsupported audio codec for conversion");
            break;
    }
    
    return samples;
}

std::vector<int16_t> AudioFormatConverter::convertToPCM16(const std::vector<float>& samples) {
    std::vector<int16_t> pcm;
    pcm.reserve(samples.size());
    
    for (float sample : samples) {
        float clamped = std::max(-1.0f, std::min(1.0f, sample));
        pcm.push_back(static_cast<int16_t>(clamped * 32767.0f));
    }
    
    return pcm;
}

std::vector<int32_t> AudioFormatConverter::convertToPCM24(const std::vector<float>& samples) {
    std::vector<int32_t> pcm;
    pcm.reserve(samples.size());
    
    for (float sample : samples) {
        float clamped = std::max(-1.0f, std::min(1.0f, sample));
        pcm.push_back(static_cast<int32_t>(clamped * 8388607.0f));
    }
    
    return pcm;
}

std::vector<int32_t> AudioFormatConverter::convertToPCM32(const std::vector<float>& samples) {
    std::vector<int32_t> pcm;
    pcm.reserve(samples.size());
    
    for (float sample : samples) {
        float clamped = std::max(-1.0f, std::min(1.0f, sample));
        pcm.push_back(static_cast<int32_t>(clamped * 2147483647.0f));
    }
    
    return pcm;
}

std::vector<float> AudioFormatConverter::convertFormat(std::string_view input, 
                                                      const ExtendedAudioFormat& inputFormat,
                                                      const ExtendedAudioFormat& outputFormat) {
    // Convert to float first
    auto floatSamples = convertToFloat(input, inputFormat.codec);
    
    // Convert channels if needed
    if (inputFormat.channels != outputFormat.channels) {
        floatSamples = convertChannels(floatSamples, inputFormat.channels, outputFormat.channels);
    }
    
    // Resample if needed
    if (inputFormat.sampleRate != outputFormat.sampleRate) {
        floatSamples = resample(floatSamples, 
                               static_cast<uint32_t>(inputFormat.sampleRate),
                               static_cast<uint32_t>(outputFormat.sampleRate));
    }
    
    return floatSamples;
}

float AudioFormatConverter::interpolate(const std::vector<float>& data, float index) {
    if (data.empty()) return 0.0f;
    
    size_t i0 = static_cast<size_t>(std::floor(index));
    size_t i1 = i0 + 1;
    
    if (i0 >= data.size()) return data.back();
    if (i1 >= data.size()) return data[i0];
    
    float frac = index - static_cast<float>(i0);
    return data[i0] * (1.0f - frac) + data[i1] * frac;
}

std::vector<float> AudioFormatConverter::applyAntiAliasingFilter(const std::vector<float>& input) {
    // Simple low-pass filter to prevent aliasing
    std::vector<float> output = input;
    
    // Apply a simple moving average filter
    const size_t filterSize = 5;
    for (size_t i = filterSize; i < output.size() - filterSize; ++i) {
        float sum = 0.0f;
        for (size_t j = i - filterSize/2; j <= i + filterSize/2; ++j) {
            sum += input[j];
        }
        output[i] = sum / static_cast<float>(filterSize);
    }
    
    return output;
}

} // namespace audio/
/ AudioQualityAssessor implementation
AudioQualityMetrics AudioQualityAssessor::assessQuality(const std::vector<float>& samples, uint32_t sampleRate) {
    AudioQualityMetrics metrics;
    
    if (samples.empty()) {
        return metrics;
    }
    
    metrics.signalToNoiseRatio = calculateSNR(samples);
    metrics.totalHarmonicDistortion = calculateTHD(samples, sampleRate);
    metrics.dynamicRange = calculateDynamicRange(samples);
    metrics.peakAmplitude = findPeakAmplitude(samples);
    metrics.rmsLevel = calculateRMSLevel(samples);
    metrics.zeroCrossingRate = calculateZeroCrossingRate(samples, sampleRate);
    metrics.spectralCentroid = calculateSpectralCentroid(samples, sampleRate);
    metrics.hasClipping = hasClipping(samples);
    metrics.hasSilence = hasSilence(samples);
    metrics.noiseFloor = estimateNoiseFloor(samples);
    
    return metrics;
}

float AudioQualityAssessor::calculateSNR(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0f;
    
    // Calculate RMS of signal
    float signalRMS = calculateRMSLevel(samples);
    
    // Estimate noise floor (bottom 10% of samples by amplitude)
    std::vector<float> sortedSamples = samples;
    std::sort(sortedSamples.begin(), sortedSamples.end(), 
              [](float a, float b) { return std::abs(a) < std::abs(b); });
    
    size_t noiseCount = sortedSamples.size() / 10;
    float noiseSum = 0.0f;
    for (size_t i = 0; i < noiseCount; ++i) {
        noiseSum += sortedSamples[i] * sortedSamples[i];
    }
    float noiseRMS = std::sqrt(noiseSum / static_cast<float>(noiseCount));
    
    if (noiseRMS < 1e-10f) noiseRMS = 1e-10f; // Avoid division by zero
    
    return 20.0f * std::log10(signalRMS / noiseRMS);
}

float AudioQualityAssessor::calculateTHD(const std::vector<float>& samples, uint32_t sampleRate) {
    // Simplified THD calculation using harmonic analysis
    auto spectrum = computePowerSpectrum(samples);
    
    if (spectrum.size() < 4) return 0.0f;
    
    // Find fundamental frequency (peak in spectrum)
    size_t fundamentalBin = 0;
    float maxPower = 0.0f;
    for (size_t i = 1; i < spectrum.size() / 4; ++i) {
        if (spectrum[i] > maxPower) {
            maxPower = spectrum[i];
            fundamentalBin = i;
        }
    }
    
    if (fundamentalBin == 0) return 0.0f;
    
    // Calculate harmonic power
    float harmonicPower = 0.0f;
    for (size_t harmonic = 2; harmonic <= 5; ++harmonic) {
        size_t harmonicBin = fundamentalBin * harmonic;
        if (harmonicBin < spectrum.size()) {
            harmonicPower += spectrum[harmonicBin];
        }
    }
    
    return (harmonicPower / maxPower) * 100.0f;
}

float AudioQualityAssessor::calculateDynamicRange(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0f;
    
    float maxAmplitude = findPeakAmplitude(samples);
    float noiseFloor = estimateNoiseFloor(samples);
    
    if (noiseFloor < 1e-10f) noiseFloor = 1e-10f;
    
    return 20.0f * std::log10(maxAmplitude / noiseFloor);
}

float AudioQualityAssessor::calculateRMSLevel(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0f;
    
    float sum = 0.0f;
    for (float sample : samples) {
        sum += sample * sample;
    }
    
    return std::sqrt(sum / static_cast<float>(samples.size()));
}

float AudioQualityAssessor::calculateZeroCrossingRate(const std::vector<float>& samples, uint32_t sampleRate) {
    if (samples.size() < 2) return 0.0f;
    
    size_t crossings = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if ((samples[i-1] >= 0.0f && samples[i] < 0.0f) ||
            (samples[i-1] < 0.0f && samples[i] >= 0.0f)) {
            crossings++;
        }
    }
    
    float duration = static_cast<float>(samples.size()) / static_cast<float>(sampleRate);
    return static_cast<float>(crossings) / duration;
}

float AudioQualityAssessor::calculateSpectralCentroid(const std::vector<float>& samples, uint32_t sampleRate) {
    auto spectrum = computePowerSpectrum(samples);
    
    if (spectrum.empty()) return 0.0f;
    
    float weightedSum = 0.0f;
    float totalPower = 0.0f;
    
    for (size_t i = 0; i < spectrum.size(); ++i) {
        float frequency = static_cast<float>(i * sampleRate) / (2.0f * static_cast<float>(spectrum.size()));
        weightedSum += frequency * spectrum[i];
        totalPower += spectrum[i];
    }
    
    return totalPower > 0.0f ? weightedSum / totalPower : 0.0f;
}

bool AudioQualityAssessor::hasClipping(const std::vector<float>& samples, float threshold) {
    for (float sample : samples) {
        if (std::abs(sample) >= threshold) {
            return true;
        }
    }
    return false;
}

bool AudioQualityAssessor::hasSilence(const std::vector<float>& samples, float threshold) {
    size_t silentSamples = 0;
    for (float sample : samples) {
        if (std::abs(sample) < threshold) {
            silentSamples++;
        }
    }
    
    // Consider it silence if more than 90% of samples are below threshold
    return static_cast<float>(silentSamples) / static_cast<float>(samples.size()) > 0.9f;
}

float AudioQualityAssessor::estimateNoiseFloor(const std::vector<float>& samples) {
    if (samples.empty()) return -60.0f;
    
    std::vector<float> amplitudes;
    amplitudes.reserve(samples.size());
    
    for (float sample : samples) {
        amplitudes.push_back(std::abs(sample));
    }
    
    std::sort(amplitudes.begin(), amplitudes.end());
    
    // Use 10th percentile as noise floor estimate
    size_t index = amplitudes.size() / 10;
    float noiseLevel = amplitudes[index];
    
    if (noiseLevel < 1e-10f) noiseLevel = 1e-10f;
    
    return 20.0f * std::log10(noiseLevel);
}

std::vector<std::string> AudioQualityAssessor::getQualityIssues(const AudioQualityMetrics& metrics) {
    std::vector<std::string> issues;
    
    if (metrics.signalToNoiseRatio < 20.0f) {
        issues.push_back("Low signal-to-noise ratio (" + std::to_string(metrics.signalToNoiseRatio) + " dB)");
    }
    
    if (metrics.totalHarmonicDistortion > 5.0f) {
        issues.push_back("High total harmonic distortion (" + std::to_string(metrics.totalHarmonicDistortion) + "%)");
    }
    
    if (metrics.dynamicRange < 30.0f) {
        issues.push_back("Limited dynamic range (" + std::to_string(metrics.dynamicRange) + " dB)");
    }
    
    if (metrics.hasClipping) {
        issues.push_back("Audio clipping detected");
    }
    
    if (metrics.hasSilence) {
        issues.push_back("Excessive silence detected");
    }
    
    if (metrics.rmsLevel < 0.01f) {
        issues.push_back("Very low audio level");
    }
    
    return issues;
}

bool AudioQualityAssessor::requiresPreprocessing(const AudioQualityMetrics& metrics) {
    return !metrics.isGoodQuality();
}

std::vector<float> AudioQualityAssessor::computeFFT(const std::vector<float>& samples) {
    // Simplified FFT implementation for power spectrum calculation
    // In a real implementation, you would use a proper FFT library like FFTW
    size_t N = samples.size();
    std::vector<float> magnitude(N/2);
    
    for (size_t k = 0; k < N/2; ++k) {
        float real = 0.0f, imag = 0.0f;
        
        for (size_t n = 0; n < N; ++n) {
            float angle = -2.0f * M_PI * static_cast<float>(k * n) / static_cast<float>(N);
            real += samples[n] * std::cos(angle);
            imag += samples[n] * std::sin(angle);
        }
        
        magnitude[k] = std::sqrt(real * real + imag * imag);
    }
    
    return magnitude;
}

float AudioQualityAssessor::findPeakAmplitude(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0f;
    
    float peak = 0.0f;
    for (float sample : samples) {
        peak = std::max(peak, std::abs(sample));
    }
    
    return peak;
}

std::vector<float> AudioQualityAssessor::computePowerSpectrum(const std::vector<float>& samples) {
    auto fft = computeFFT(samples);
    std::vector<float> power;
    power.reserve(fft.size());
    
    for (float magnitude : fft) {
        power.push_back(magnitude * magnitude);
    }
    
    return power;
}

// NoiseDetector implementation
NoiseProfile NoiseDetector::analyzeNoise(const std::vector<float>& samples, uint32_t sampleRate) {
    NoiseProfile profile;
    
    if (samples.empty()) {
        return profile;
    }
    
    profile.noiseLevel = detectNoiseLevel(samples);
    profile.speechLevel = detectSpeechLevel(samples);
    profile.noiseFloor = estimateNoiseFloor(samples);
    profile.hasBackgroundNoise = hasBackgroundNoise(samples);
    profile.hasImpulseNoise = hasImpulseNoise(samples);
    profile.hasPeriodicNoise = hasPeriodicNoise(samples, sampleRate);
    profile.frequencySpectrum = computeNoiseSpectrum(samples);
    
    return profile;
}

float NoiseDetector::detectNoiseLevel(const std::vector<float>& samples) {
    if (samples.empty()) return -60.0f;
    
    // Use bottom 25% of samples by amplitude as noise estimate
    std::vector<float> amplitudes;
    amplitudes.reserve(samples.size());
    
    for (float sample : samples) {
        amplitudes.push_back(std::abs(sample));
    }
    
    std::sort(amplitudes.begin(), amplitudes.end());
    
    size_t noiseCount = amplitudes.size() / 4;
    float noiseSum = 0.0f;
    for (size_t i = 0; i < noiseCount; ++i) {
        noiseSum += amplitudes[i] * amplitudes[i];
    }
    
    float noiseRMS = std::sqrt(noiseSum / static_cast<float>(noiseCount));
    if (noiseRMS < 1e-10f) noiseRMS = 1e-10f;
    
    return 20.0f * std::log10(noiseRMS);
}

float NoiseDetector::detectSpeechLevel(const std::vector<float>& samples) {
    if (samples.empty()) return -60.0f;
    
    // Use top 25% of samples by amplitude as speech estimate
    std::vector<float> amplitudes;
    amplitudes.reserve(samples.size());
    
    for (float sample : samples) {
        amplitudes.push_back(std::abs(sample));
    }
    
    std::sort(amplitudes.begin(), amplitudes.end(), std::greater<float>());
    
    size_t speechCount = amplitudes.size() / 4;
    float speechSum = 0.0f;
    for (size_t i = 0; i < speechCount; ++i) {
        speechSum += amplitudes[i] * amplitudes[i];
    }
    
    float speechRMS = std::sqrt(speechSum / static_cast<float>(speechCount));
    if (speechRMS < 1e-10f) speechRMS = 1e-10f;
    
    return 20.0f * std::log10(speechRMS);
}

bool NoiseDetector::hasBackgroundNoise(const std::vector<float>& samples, float threshold) {
    float noiseLevel = detectNoiseLevel(samples);
    return noiseLevel > threshold;
}

bool NoiseDetector::hasImpulseNoise(const std::vector<float>& samples) {
    if (samples.size() < 10) return false;
    
    // Detect sudden amplitude spikes
    float avgAmplitude = 0.0f;
    for (float sample : samples) {
        avgAmplitude += std::abs(sample);
    }
    avgAmplitude /= static_cast<float>(samples.size());
    
    float threshold = avgAmplitude * 5.0f; // 5x average is considered impulse
    
    size_t impulseCount = 0;
    for (float sample : samples) {
        if (std::abs(sample) > threshold) {
            impulseCount++;
        }
    }
    
    // If more than 1% of samples are impulses, consider it impulse noise
    return static_cast<float>(impulseCount) / static_cast<float>(samples.size()) > 0.01f;
}

bool NoiseDetector::hasPeriodicNoise(const std::vector<float>& samples, uint32_t sampleRate) {
    return isPeriodicPattern(samples, sampleRate);
}

std::vector<float> NoiseDetector::computeNoiseSpectrum(const std::vector<float>& samples) {
    return AudioQualityAssessor::computePowerSpectrum(samples);
}

float NoiseDetector::estimateNoiseFloor(const std::vector<float>& samples) {
    return AudioQualityAssessor::estimateNoiseFloor(samples);
}

std::vector<float> NoiseDetector::identifyNoiseFrequencies(const std::vector<float>& samples, uint32_t sampleRate) {
    auto spectrum = computeNoiseSpectrum(samples);
    return detectSpectralPeaks(spectrum);
}

NoiseDetector::NoiseType NoiseDetector::classifyNoise(const NoiseProfile& profile) {
    if (profile.getSNR() > 30.0f) {
        return NoiseType::NONE;
    }
    
    if (profile.hasImpulseNoise) {
        return NoiseType::IMPULSE_NOISE;
    }
    
    if (profile.hasPeriodicNoise) {
        return NoiseType::PERIODIC_NOISE;
    }
    
    if (profile.hasBackgroundNoise) {
        // Analyze spectrum to classify background noise type
        if (!profile.frequencySpectrum.empty()) {
            float spectralFlatness = calculateSpectralFlatness(profile.frequencySpectrum);
            
            if (spectralFlatness > 0.8f) {
                return NoiseType::WHITE_NOISE;
            } else if (spectralFlatness > 0.5f) {
                return NoiseType::PINK_NOISE;
            } else {
                return NoiseType::ENVIRONMENTAL_NOISE;
            }
        }
    }
    
    return NoiseType::UNKNOWN;
}

std::string NoiseDetector::noiseTypeToString(NoiseType type) {
    switch (type) {
        case NoiseType::NONE: return "None";
        case NoiseType::WHITE_NOISE: return "White Noise";
        case NoiseType::PINK_NOISE: return "Pink Noise";
        case NoiseType::BROWN_NOISE: return "Brown Noise";
        case NoiseType::IMPULSE_NOISE: return "Impulse Noise";
        case NoiseType::PERIODIC_NOISE: return "Periodic Noise";
        case NoiseType::ENVIRONMENTAL_NOISE: return "Environmental Noise";
        default: return "Unknown";
    }
}

float NoiseDetector::calculateSpectralFlatness(const std::vector<float>& spectrum) {
    if (spectrum.empty()) return 0.0f;
    
    float geometricMean = 1.0f;
    float arithmeticMean = 0.0f;
    
    for (float value : spectrum) {
        if (value > 0.0f) {
            geometricMean *= std::pow(value, 1.0f / static_cast<float>(spectrum.size()));
            arithmeticMean += value;
        }
    }
    
    arithmeticMean /= static_cast<float>(spectrum.size());
    
    return arithmeticMean > 0.0f ? geometricMean / arithmeticMean : 0.0f;
}

std::vector<float> NoiseDetector::detectSpectralPeaks(const std::vector<float>& spectrum) {
    std::vector<float> peaks;
    
    if (spectrum.size() < 3) return peaks;
    
    for (size_t i = 1; i < spectrum.size() - 1; ++i) {
        if (spectrum[i] > spectrum[i-1] && spectrum[i] > spectrum[i+1]) {
            // Local maximum found
            if (spectrum[i] > 0.1f) { // Threshold for significant peaks
                peaks.push_back(static_cast<float>(i));
            }
        }
    }
    
    return peaks;
}

bool NoiseDetector::isPeriodicPattern(const std::vector<float>& samples, uint32_t sampleRate) {
    if (samples.size() < sampleRate / 10) return false; // Need at least 100ms of data
    
    // Simple autocorrelation-based periodicity detection
    size_t maxLag = std::min(samples.size() / 4, static_cast<size_t>(sampleRate / 50)); // Max 20ms lag
    
    float maxCorrelation = 0.0f;
    for (size_t lag = 1; lag < maxLag; ++lag) {
        float correlation = 0.0f;
        size_t count = samples.size() - lag;
        
        for (size_t i = 0; i < count; ++i) {
            correlation += samples[i] * samples[i + lag];
        }
        
        correlation /= static_cast<float>(count);
        maxCorrelation = std::max(maxCorrelation, std::abs(correlation));
    }
    
    return maxCorrelation > 0.3f; // Threshold for periodic pattern
}

} // namespace audio// Audio
Preprocessor implementation
std::vector<float> AudioPreprocessor::normalizeAmplitude(const std::vector<float>& samples, float targetLevel) {
    if (samples.empty()) return samples;
    
    float peak = 0.0f;
    for (float sample : samples) {
        peak = std::max(peak, std::abs(sample));
    }
    
    if (peak < 1e-10f) return samples; // Avoid division by zero
    
    float gain = targetLevel / peak;
    std::vector<float> normalized;
    normalized.reserve(samples.size());
    
    for (float sample : samples) {
        normalized.push_back(sample * gain);
    }
    
    return normalized;
}

std::vector<float> AudioPreprocessor::removeClipping(const std::vector<float>& samples) {
    std::vector<float> processed = samples;
    
    // Simple clipping removal using soft limiting
    for (float& sample : processed) {
        if (std::abs(sample) > 0.95f) {
            float sign = sample >= 0.0f ? 1.0f : -1.0f;
            sample = sign * (0.95f - 0.05f * std::exp(-(std::abs(sample) - 0.95f) * 10.0f));
        }
    }
    
    return processed;
}

std::vector<float> AudioPreprocessor::removeSilence(const std::vector<float>& samples, float threshold) {
    std::vector<float> processed;
    processed.reserve(samples.size());
    
    for (float sample : samples) {
        if (std::abs(sample) >= threshold) {
            processed.push_back(sample);
        }
    }
    
    return processed;
}

std::vector<float> AudioPreprocessor::applyGainControl(const std::vector<float>& samples, float gain) {
    std::vector<float> processed;
    processed.reserve(samples.size());
    
    for (float sample : samples) {
        float amplified = sample * gain;
        // Soft clipping to prevent distortion
        if (amplified > 1.0f) {
            amplified = 1.0f - std::exp(-(amplified - 1.0f));
        } else if (amplified < -1.0f) {
            amplified = -1.0f + std::exp(amplified + 1.0f);
        }
        processed.push_back(amplified);
    }
    
    return processed;
}

std::vector<float> AudioPreprocessor::applyHighPassFilter(const std::vector<float>& samples, 
                                                         uint32_t sampleRate, float cutoffHz) {
    auto coefficients = computeFilterCoefficients(cutoffHz / static_cast<float>(sampleRate), sampleRate, true);
    return applyFIRFilter(samples, coefficients);
}

std::vector<float> AudioPreprocessor::applyLowPassFilter(const std::vector<float>& samples, 
                                                        uint32_t sampleRate, float cutoffHz) {
    auto coefficients = computeFilterCoefficients(cutoffHz / static_cast<float>(sampleRate), sampleRate, false);
    return applyFIRFilter(samples, coefficients);
}

std::vector<float> AudioPreprocessor::applyBandPassFilter(const std::vector<float>& samples, 
                                                         uint32_t sampleRate, float lowHz, float highHz) {
    // Apply high-pass then low-pass
    auto highPassed = applyHighPassFilter(samples, sampleRate, lowHz);
    return applyLowPassFilter(highPassed, sampleRate, highHz);
}

std::vector<float> AudioPreprocessor::reduceNoise(const std::vector<float>& samples, 
                                                 const NoiseProfile& noiseProfile) {
    if (noiseProfile.frequencySpectrum.empty()) {
        return samples; // No noise profile available
    }
    
    return spectralSubtraction(samples, noiseProfile.frequencySpectrum);
}

std::vector<float> AudioPreprocessor::spectralSubtraction(const std::vector<float>& samples, 
                                                         const std::vector<float>& noiseSpectrum) {
    // Simplified spectral subtraction
    // In a real implementation, you would use proper FFT/IFFT
    
    std::vector<float> processed = samples;
    
    // Apply simple noise gate based on noise spectrum energy
    float noiseEnergy = 0.0f;
    for (float value : noiseSpectrum) {
        noiseEnergy += value;
    }
    noiseEnergy /= static_cast<float>(noiseSpectrum.size());
    
    float threshold = std::sqrt(noiseEnergy) * 2.0f;
    
    for (float& sample : processed) {
        if (std::abs(sample) < threshold) {
            sample *= 0.1f; // Attenuate noise-like samples
        }
    }
    
    return processed;
}

std::vector<float> AudioPreprocessor::adaptiveNoiseReduction(const std::vector<float>& samples, uint32_t sampleRate) {
    // Adaptive noise reduction using spectral gating
    const size_t windowSize = sampleRate / 20; // 50ms windows
    std::vector<float> processed = samples;
    
    for (size_t i = 0; i < samples.size(); i += windowSize) {
        size_t endIdx = std::min(i + windowSize, samples.size());
        
        // Calculate window energy
        float energy = 0.0f;
        for (size_t j = i; j < endIdx; ++j) {
            energy += samples[j] * samples[j];
        }
        energy /= static_cast<float>(endIdx - i);
        
        // Apply adaptive gain based on energy
        float gain = 1.0f;
        if (energy < 0.001f) { // Low energy, likely noise
            gain = 0.2f;
        } else if (energy < 0.01f) { // Medium energy
            gain = 0.6f;
        }
        
        for (size_t j = i; j < endIdx; ++j) {
            processed[j] *= gain;
        }
    }
    
    return processed;
}

std::vector<float> AudioPreprocessor::enhanceSpeech(const std::vector<float>& samples, uint32_t sampleRate) {
    // Multi-stage speech enhancement
    auto processed = samples;
    
    // 1. High-pass filter to remove low-frequency noise
    processed = applyHighPassFilter(processed, sampleRate, 80.0f);
    
    // 2. Adaptive noise reduction
    processed = adaptiveNoiseReduction(processed, sampleRate);
    
    // 3. Mild compression to even out levels
    processed = applyCompression(processed, 3.0f, -25.0f);
    
    // 4. Normalize to consistent level
    processed = normalizeAmplitude(processed, 0.7f);
    
    return processed;
}

std::vector<float> AudioPreprocessor::applyCompression(const std::vector<float>& samples, 
                                                      float ratio, float threshold) {
    std::vector<float> processed;
    processed.reserve(samples.size());
    
    float thresholdLinear = std::pow(10.0f, threshold / 20.0f);
    
    for (float sample : samples) {
        float amplitude = std::abs(sample);
        float sign = sample >= 0.0f ? 1.0f : -1.0f;
        
        if (amplitude > thresholdLinear) {
            // Apply compression above threshold
            float excess = amplitude - thresholdLinear;
            float compressedExcess = excess / ratio;
            amplitude = thresholdLinear + compressedExcess;
        }
        
        processed.push_back(sign * amplitude);
    }
    
    return processed;
}

std::vector<float> AudioPreprocessor::applyDeEsser(const std::vector<float>& samples, uint32_t sampleRate) {
    // Simple de-esser targeting sibilant frequencies (5-8 kHz)
    const float lowFreq = 5000.0f;
    const float highFreq = 8000.0f;
    
    // Extract sibilant band
    auto sibilantBand = applyBandPassFilter(samples, sampleRate, lowFreq, highFreq);
    
    // Apply compression to sibilant band
    auto compressedSibilants = applyCompression(sibilantBand, 6.0f, -30.0f);
    
    // Subtract over-compressed sibilants from original
    std::vector<float> processed;
    processed.reserve(samples.size());
    
    for (size_t i = 0; i < samples.size(); ++i) {
        float reduction = sibilantBand[i] - compressedSibilants[i];
        processed.push_back(samples[i] - reduction * 0.5f);
    }
    
    return processed;
}

std::vector<float> AudioPreprocessor::applyIIRFilter(const std::vector<float>& samples, 
                                                    const std::vector<float>& b, const std::vector<float>& a) {
    std::vector<float> output(samples.size(), 0.0f);
    std::vector<float> x(b.size(), 0.0f); // Input history
    std::vector<float> y(a.size(), 0.0f); // Output history
    
    for (size_t n = 0; n < samples.size(); ++n) {
        // Shift input history
        for (size_t i = x.size() - 1; i > 0; --i) {
            x[i] = x[i-1];
        }
        x[0] = samples[n];
        
        // Calculate output
        float sum = 0.0f;
        for (size_t i = 0; i < b.size(); ++i) {
            sum += b[i] * x[i];
        }
        for (size_t i = 1; i < a.size(); ++i) {
            sum -= a[i] * y[i];
        }
        
        output[n] = sum / a[0];
        
        // Shift output history
        for (size_t i = y.size() - 1; i > 0; --i) {
            y[i] = y[i-1];
        }
        y[0] = output[n];
    }
    
    return output;
}

std::vector<float> AudioPreprocessor::applyFIRFilter(const std::vector<float>& samples, 
                                                    const std::vector<float>& coefficients) {
    if (coefficients.empty()) return samples;
    
    std::vector<float> output;
    output.reserve(samples.size());
    
    for (size_t n = 0; n < samples.size(); ++n) {
        float sum = 0.0f;
        
        for (size_t k = 0; k < coefficients.size(); ++k) {
            if (n >= k) {
                sum += coefficients[k] * samples[n - k];
            }
        }
        
        output.push_back(sum);
    }
    
    return output;
}

std::vector<float> AudioPreprocessor::computeFilterCoefficients(float cutoff, uint32_t sampleRate, bool highPass) {
    // Simple windowed sinc filter design
    const size_t filterLength = 64;
    std::vector<float> coefficients(filterLength);
    
    float fc = cutoff / static_cast<float>(sampleRate);
    int center = static_cast<int>(filterLength) / 2;
    
    for (size_t i = 0; i < filterLength; ++i) {
        int n = static_cast<int>(i) - center;
        
        if (n == 0) {
            coefficients[i] = 2.0f * fc;
        } else {
            coefficients[i] = std::sin(2.0f * M_PI * fc * static_cast<float>(n)) / 
                             (M_PI * static_cast<float>(n));
        }
        
        // Apply Hamming window
        coefficients[i] *= 0.54f - 0.46f * std::cos(2.0f * M_PI * static_cast<float>(i) / 
                                                    static_cast<float>(filterLength - 1));
    }
    
    // Convert to high-pass if needed
    if (highPass) {
        for (size_t i = 0; i < filterLength; ++i) {
            coefficients[i] = -coefficients[i];
        }
        coefficients[center] += 1.0f;
    }
    
    return coefficients;
}

// AudioStreamValidator implementation
AudioStreamValidator::AudioStreamValidator(const ExtendedAudioFormat& expectedFormat)
    : expectedFormat_(expectedFormat), minSNR_(15.0f), maxTHD_(10.0f), 
      maxLatency_(std::chrono::milliseconds(100)), totalChunks_(0), droppedChunks_(0),
      lastChunkTime_(std::chrono::steady_clock::now()) {
}

bool AudioStreamValidator::validateChunk(std::string_view data) {
    totalChunks_++;
    
    // Validate data size
    if (!AudioFormatValidator::validateAudioData(data, expectedFormat_)) {
        droppedChunks_++;
        return false;
    }
    
    // Convert to float for quality analysis
    auto samples = AudioFormatConverter::convertToFloat(data, expectedFormat_.codec);
    
    // Validate quality
    auto metrics = AudioQualityAssessor::assessQuality(samples, static_cast<uint32_t>(expectedFormat_.sampleRate));
    
    bool isValid = metrics.signalToNoiseRatio >= minSNR_ && 
                   metrics.totalHarmonicDistortion <= maxTHD_;
    
    if (!isValid) {
        droppedChunks_++;
    }
    
    // Update health metrics
    auto now = std::chrono::steady_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastChunkTime_);
    lastChunkTime_ = now;
    
    updateHealthMetrics(static_cast<float>(latency.count()), metrics.signalToNoiseRatio);
    
    return isValid;
}

bool AudioStreamValidator::validateContinuity(const std::vector<float>& samples) {
    if (samples.empty()) return false;
    
    // Check for discontinuities (sudden amplitude changes)
    const float maxJump = 0.5f;
    
    for (size_t i = 1; i < samples.size(); ++i) {
        if (std::abs(samples[i] - samples[i-1]) > maxJump) {
            return false;
        }
    }
    
    return true;
}

bool AudioStreamValidator::validateLatency(std::chrono::milliseconds maxLatency) {
    return maxLatency <= maxLatency_;
}

AudioStreamValidator::StreamHealth AudioStreamValidator::getStreamHealth() const {
    StreamHealth health;
    
    if (latencyHistory_.empty() || qualityHistory_.empty()) {
        health.isHealthy = false;
        health.averageLatency = 0.0f;
        health.dropoutRate = 1.0f;
        health.qualityScore = 0.0f;
        health.issues.push_back("No data available");
        return health;
    }
    
    // Calculate average latency
    float totalLatency = std::accumulate(latencyHistory_.begin(), latencyHistory_.end(), 0.0f);
    health.averageLatency = totalLatency / static_cast<float>(latencyHistory_.size());
    
    // Calculate dropout rate
    health.dropoutRate = totalChunks_ > 0 ? 
        static_cast<float>(droppedChunks_) / static_cast<float>(totalChunks_) : 1.0f;
    
    // Calculate quality score
    float totalQuality = std::accumulate(qualityHistory_.begin(), qualityHistory_.end(), 0.0f);
    health.qualityScore = totalQuality / static_cast<float>(qualityHistory_.size());
    
    // Determine overall health
    health.isHealthy = health.averageLatency < static_cast<float>(maxLatency_.count()) &&
                      health.dropoutRate < 0.05f &&
                      health.qualityScore >= minSNR_;
    
    // Identify issues
    if (health.averageLatency >= static_cast<float>(maxLatency_.count())) {
        health.issues.push_back("High latency (" + std::to_string(health.averageLatency) + "ms)");
    }
    
    if (health.dropoutRate >= 0.05f) {
        health.issues.push_back("High dropout rate (" + std::to_string(health.dropoutRate * 100.0f) + "%)");
    }
    
    if (health.qualityScore < minSNR_) {
        health.issues.push_back("Poor audio quality (SNR: " + std::to_string(health.qualityScore) + "dB)");
    }
    
    return health;
}

void AudioStreamValidator::resetHealth() {
    latencyHistory_.clear();
    qualityHistory_.clear();
    totalChunks_ = 0;
    droppedChunks_ = 0;
    lastChunkTime_ = std::chrono::steady_clock::now();
}

void AudioStreamValidator::setExpectedFormat(const ExtendedAudioFormat& format) {
    expectedFormat_ = format;
}

void AudioStreamValidator::setQualityThresholds(float minSNR, float maxTHD) {
    minSNR_ = minSNR;
    maxTHD_ = maxTHD;
}

void AudioStreamValidator::setLatencyThreshold(std::chrono::milliseconds maxLatency) {
    maxLatency_ = maxLatency;
}

void AudioStreamValidator::updateHealthMetrics(float latency, float quality) {
    const size_t maxHistorySize = 100;
    
    latencyHistory_.push_back(latency);
    if (latencyHistory_.size() > maxHistorySize) {
        latencyHistory_.erase(latencyHistory_.begin());
    }
    
    qualityHistory_.push_back(quality);
    if (qualityHistory_.size() > maxHistorySize) {
        qualityHistory_.erase(qualityHistory_.begin());
    }
}

bool AudioStreamValidator::isWithinLatencyBounds(std::chrono::milliseconds latency) const {
    return latency <= maxLatency_;
}

} // namespace audio