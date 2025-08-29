#include "audio/audio_utils.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace audio;

int main() {
    std::cout << "Audio Utils Demo - Testing Advanced Audio Processing" << std::endl;
    
    // Initialize logging
    utils::Logger::initialize();
    
    try {
        // Generate test audio data (440Hz sine wave)
        const size_t sampleCount = 1024;
        const float frequency = 440.0f; // A4 note
        const uint32_t sampleRate = 16000;
        
        std::vector<float> testSamples;
        testSamples.reserve(sampleCount);
        
        for (size_t i = 0; i < sampleCount; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sampleRate);
            testSamples.push_back(0.5f * std::sin(2.0f * M_PI * frequency * t));
        }
        
        std::cout << "Generated " << testSamples.size() << " test samples" << std::endl;
        
        // Test 1: Audio Format Validation
        std::cout << "\n=== Testing Audio Format Validation ===" << std::endl;
        
        ExtendedAudioFormat format(SampleRate::SR_16000, 1, AudioCodec::PCM_16, 1024);
        std::cout << "Format: " << format.toString() << std::endl;
        std::cout << "Format valid: " << (format.isValid() ? "Yes" : "No") << std::endl;
        std::cout << "Format supported: " << (AudioFormatValidator::isFormatSupported(format) ? "Yes" : "No") << std::endl;
        
        // Test 2: Audio Quality Assessment
        std::cout << "\n=== Testing Audio Quality Assessment ===" << std::endl;
        
        auto metrics = AudioQualityAssessor::assessQuality(testSamples, sampleRate);
        std::cout << "Signal-to-Noise Ratio: " << metrics.signalToNoiseRatio << " dB" << std::endl;
        std::cout << "Total Harmonic Distortion: " << metrics.totalHarmonicDistortion << "%" << std::endl;
        std::cout << "Dynamic Range: " << metrics.dynamicRange << " dB" << std::endl;
        std::cout << "RMS Level: " << metrics.rmsLevel << std::endl;
        std::cout << "Zero Crossing Rate: " << metrics.zeroCrossingRate << " Hz" << std::endl;
        std::cout << "Has Clipping: " << (metrics.hasClipping ? "Yes" : "No") << std::endl;
        std::cout << "Has Silence: " << (metrics.hasSilence ? "Yes" : "No") << std::endl;
        std::cout << "Quality Assessment: " << metrics.getQualityDescription() << std::endl;
        
        // Test 3: Noise Detection
        std::cout << "\n=== Testing Noise Detection ===" << std::endl;
        
        auto noiseProfile = NoiseDetector::analyzeNoise(testSamples, sampleRate);
        std::cout << "Noise Level: " << noiseProfile.noiseLevel << " dB" << std::endl;
        std::cout << "Speech Level: " << noiseProfile.speechLevel << " dB" << std::endl;
        std::cout << "SNR: " << noiseProfile.getSNR() << " dB" << std::endl;
        std::cout << "Has Background Noise: " << (noiseProfile.hasBackgroundNoise ? "Yes" : "No") << std::endl;
        std::cout << "Has Impulse Noise: " << (noiseProfile.hasImpulseNoise ? "Yes" : "No") << std::endl;
        std::cout << "Requires Denoising: " << (noiseProfile.requiresDenoising() ? "Yes" : "No") << std::endl;
        
        auto noiseType = NoiseDetector::classifyNoise(noiseProfile);
        std::cout << "Noise Type: " << NoiseDetector::noiseTypeToString(noiseType) << std::endl;
        
        // Test 4: Audio Preprocessing
        std::cout << "\n=== Testing Audio Preprocessing ===" << std::endl;
        
        auto normalized = AudioPreprocessor::normalizeAmplitude(testSamples, 0.8f);
        std::cout << "Normalized " << normalized.size() << " samples to 0.8 amplitude" << std::endl;
        
        auto enhanced = AudioPreprocessor::enhanceSpeech(testSamples, sampleRate);
        std::cout << "Enhanced " << enhanced.size() << " samples for speech" << std::endl;
        
        auto filtered = AudioPreprocessor::applyHighPassFilter(testSamples, sampleRate, 100.0f);
        std::cout << "Applied high-pass filter (100Hz) to " << filtered.size() << " samples" << std::endl;
        
        // Test 5: Format Conversion
        std::cout << "\n=== Testing Format Conversion ===" << std::endl;
        
        auto pcm16Data = AudioFormatConverter::convertToPCM16(testSamples);
        std::cout << "Converted " << testSamples.size() << " float samples to " << pcm16Data.size() << " PCM16 samples" << std::endl;
        
        auto stereoData = AudioFormatConverter::monoToStereo(testSamples);
        std::cout << "Converted " << testSamples.size() << " mono samples to " << stereoData.size() << " stereo samples" << std::endl;
        
        auto resampledData = AudioFormatConverter::resample(testSamples, 16000, 22050);
        std::cout << "Resampled " << testSamples.size() << " samples (16kHz) to " << resampledData.size() << " samples (22.05kHz)" << std::endl;
        
        // Test 6: Stream Validation
        std::cout << "\n=== Testing Stream Validation ===" << std::endl;
        
        AudioStreamValidator validator(format);
        
        // Convert to PCM data for validation
        std::string pcmString(reinterpret_cast<const char*>(pcm16Data.data()), 
                             pcm16Data.size() * sizeof(int16_t));
        
        bool chunkValid = validator.validateChunk(pcmString);
        std::cout << "Chunk validation: " << (chunkValid ? "Valid" : "Invalid") << std::endl;
        
        bool continuityValid = validator.validateContinuity(testSamples);
        std::cout << "Continuity validation: " << (continuityValid ? "Valid" : "Invalid") << std::endl;
        
        auto streamHealth = validator.getStreamHealth();
        std::cout << "Stream health: " << (streamHealth.isHealthy ? "Healthy" : "Unhealthy") << std::endl;
        std::cout << "Dropout rate: " << (streamHealth.dropoutRate * 100.0f) << "%" << std::endl;
        
        std::cout << "\n=== Audio Utils Demo Completed Successfully ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Demo failed with unknown exception" << std::endl;
        return 1;
    }
    
    return 0;
}