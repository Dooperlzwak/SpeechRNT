#include "audio/audio_processor.hpp"
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
    std::cout << "Audio Integration Test - Testing AudioProcessor with AudioUtils" << std::endl;
    
    // Initialize logging
    utils::Logger::initialize();
    
    try {
        // Create audio format
        AudioFormat basicFormat;
        basicFormat.sampleRate = 16000;
        basicFormat.channels = 1;
        basicFormat.bitsPerSample = 16;
        basicFormat.chunkSize = 1024;
        
        // Create extended format for advanced processing
        ExtendedAudioFormat extendedFormat(SampleRate::SR_16000, 1, AudioCodec::PCM_16, 1024);
        
        std::cout << "Basic format: " << basicFormat.sampleRate << "Hz, " 
                  << basicFormat.channels << " channels, " 
                  << basicFormat.bitsPerSample << " bits" << std::endl;
        std::cout << "Extended format: " << extendedFormat.toString() << std::endl;
        
        // Create audio processor
        AudioProcessor processor(basicFormat);
        std::cout << "AudioProcessor created successfully" << std::endl;
        
        // Generate test PCM data
        const size_t sampleCount = 1024;
        std::vector<int16_t> pcmData;
        pcmData.reserve(sampleCount);
        
        for (size_t i = 0; i < sampleCount; ++i) {
            float t = static_cast<float>(i) / 16000.0f;
            float sample = 0.5f * std::sin(2.0f * M_PI * 440.0f * t);
            pcmData.push_back(static_cast<int16_t>(sample * 32767.0f));
        }
        
        std::string pcmString(reinterpret_cast<const char*>(pcmData.data()), 
                             pcmData.size() * sizeof(int16_t));
        
        std::cout << "Generated " << pcmData.size() << " PCM samples (" << pcmString.size() << " bytes)" << std::endl;
        
        // Test basic audio processing
        auto audioChunk = processor.processRawData(pcmString);
        std::cout << "Processed audio chunk with " << audioChunk.samples.size() << " float samples" << std::endl;
        
        // Test advanced audio processing
        std::cout << "\n=== Testing Advanced Audio Processing ===" << std::endl;
        
        // Validate format
        bool formatSupported = AudioFormatValidator::isFormatSupported(extendedFormat);
        std::cout << "Extended format supported: " << (formatSupported ? "Yes" : "No") << std::endl;
        
        // Validate PCM data
        bool dataValid = AudioFormatValidator::validateAudioData(pcmString, extendedFormat);
        std::cout << "PCM data valid: " << (dataValid ? "Yes" : "No") << std::endl;
        
        // Assess audio quality
        auto qualityMetrics = AudioQualityAssessor::assessQuality(audioChunk.samples, 16000);
        std::cout << "Audio quality - SNR: " << qualityMetrics.signalToNoiseRatio << " dB, "
                  << "RMS: " << qualityMetrics.rmsLevel << ", "
                  << "Good quality: " << (qualityMetrics.isGoodQuality() ? "Yes" : "No") << std::endl;
        
        // Analyze noise
        auto noiseProfile = NoiseDetector::analyzeNoise(audioChunk.samples, 16000);
        std::cout << "Noise analysis - SNR: " << noiseProfile.getSNR() << " dB, "
                  << "Requires denoising: " << (noiseProfile.requiresDenoising() ? "Yes" : "No") << std::endl;
        
        // Test preprocessing
        auto normalizedSamples = AudioPreprocessor::normalizeAmplitude(audioChunk.samples, 0.8f);
        std::cout << "Normalized " << normalizedSamples.size() << " samples to 0.8 amplitude" << std::endl;
        
        // Test stream validation
        AudioStreamValidator validator(extendedFormat);
        bool chunkValid = validator.validateChunk(pcmString);
        std::cout << "Stream validation: " << (chunkValid ? "Valid" : "Invalid") << std::endl;
        
        // Test with AudioBuffer
        std::cout << "\n=== Testing AudioBuffer Integration ===" << std::endl;
        
        AudioBuffer buffer;
        bool added = buffer.addChunk(audioChunk);
        std::cout << "Added chunk to buffer: " << (added ? "Success" : "Failed") << std::endl;
        std::cout << "Buffer chunk count: " << buffer.getChunkCount() << std::endl;
        std::cout << "Buffer total samples: " << buffer.getTotalSamples() << std::endl;
        
        // Get samples back and test quality
        auto bufferSamples = buffer.getAllSamples();
        std::cout << "Retrieved " << bufferSamples.size() << " samples from buffer" << std::endl;
        
        if (bufferSamples.size() == audioChunk.samples.size()) {
            bool samplesMatch = true;
            for (size_t i = 0; i < bufferSamples.size(); ++i) {
                if (std::abs(bufferSamples[i] - audioChunk.samples[i]) > 1e-6f) {
                    samplesMatch = false;
                    break;
                }
            }
            std::cout << "Buffer samples match original: " << (samplesMatch ? "Yes" : "No") << std::endl;
        }
        
        // Test AudioIngestionManager
        std::cout << "\n=== Testing AudioIngestionManager Integration ===" << std::endl;
        
        AudioIngestionManager manager("test-session");
        manager.setActive(true);
        manager.setAudioFormat(basicFormat);
        
        bool ingested = manager.ingestAudioData(pcmString);
        std::cout << "Ingested audio data: " << (ingested ? "Success" : "Failed") << std::endl;
        
        auto stats = manager.getStatistics();
        std::cout << "Ingestion stats - Total bytes: " << stats.totalBytesIngested 
                  << ", Total chunks: " << stats.totalChunksIngested
                  << ", Dropped chunks: " << stats.droppedChunks << std::endl;
        
        auto latestAudio = manager.getLatestAudio(512);
        std::cout << "Retrieved " << latestAudio.size() << " latest samples" << std::endl;
        
        std::cout << "\n=== Audio Integration Test Completed Successfully ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Integration test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Integration test failed with unknown exception" << std::endl;
        return 1;
    }
    
    return 0;
}