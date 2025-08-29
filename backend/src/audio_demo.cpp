#include "audio/audio_processor.hpp"
#include "core/client_session.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace audio;
using namespace core;

// Demo function to create synthetic PCM audio data
std::vector<uint8_t> createSyntheticPCM(size_t sampleCount, double frequency = 440.0) {
    std::vector<uint8_t> pcmData;
    pcmData.reserve(sampleCount * 2);
    
    const double sampleRate = 16000.0;
    const double amplitude = 16000.0; // Moderate amplitude
    
    for (size_t i = 0; i < sampleCount; ++i) {
        // Generate sine wave
        double time = static_cast<double>(i) / sampleRate;
        double sample = amplitude * std::sin(2.0 * M_PI * frequency * time);
        
        // Convert to 16-bit PCM
        int16_t pcmSample = static_cast<int16_t>(std::clamp(sample, -32768.0, 32767.0));
        
        // Store as little-endian bytes
        pcmData.push_back(pcmSample & 0xFF);
        pcmData.push_back((pcmSample >> 8) & 0xFF);
    }
    
    return pcmData;
}

void demonstrateAudioProcessor() {
    std::cout << "\n=== Audio Processor Demo ===\n";
    
    // Create audio processor with standard format
    AudioFormat format;
    format.sampleRate = 16000;
    format.channels = 1;
    format.bitsPerSample = 16;
    format.chunkSize = 1024;
    
    AudioProcessor processor(format);
    
    std::cout << "Audio Format:\n";
    std::cout << "  Sample Rate: " << format.sampleRate << " Hz\n";
    std::cout << "  Channels: " << format.channels << " (mono)\n";
    std::cout << "  Bits per Sample: " << format.bitsPerSample << "\n";
    std::cout << "  Chunk Size: " << format.chunkSize << " samples\n";
    
    // Create test PCM data (1 second of 440Hz tone)
    auto pcmData = createSyntheticPCM(16000, 440.0);
    std::string_view dataView(reinterpret_cast<const char*>(pcmData.data()), pcmData.size());
    
    std::cout << "\nProcessing " << pcmData.size() << " bytes of PCM data...\n";
    
    // Process the data
    auto chunks = processor.processStreamingData(dataView);
    
    std::cout << "Generated " << chunks.size() << " audio chunks\n";
    std::cout << "Total bytes processed: " << processor.getTotalBytesProcessed() << "\n";
    std::cout << "Total chunks processed: " << processor.getTotalChunksProcessed() << "\n";
    
    // Analyze first chunk
    if (!chunks.empty()) {
        const auto& firstChunk = chunks[0];
        std::cout << "\nFirst chunk analysis:\n";
        std::cout << "  Sample count: " << firstChunk.samples.size() << "\n";
        std::cout << "  Sequence number: " << firstChunk.sequenceNumber << "\n";
        
        // Show sample statistics
        float minSample = *std::min_element(firstChunk.samples.begin(), firstChunk.samples.end());
        float maxSample = *std::max_element(firstChunk.samples.begin(), firstChunk.samples.end());
        
        std::cout << "  Sample range: [" << minSample << ", " << maxSample << "]\n";
    }
}

void demonstrateAudioBuffer() {
    std::cout << "\n=== Audio Buffer Demo ===\n";
    
    AudioBuffer buffer(8192); // 8KB buffer
    
    // Add multiple chunks
    for (int i = 0; i < 5; ++i) {
        std::vector<float> samples;
        for (int j = 0; j < 100; ++j) {
            samples.push_back(static_cast<float>(i * 100 + j) / 1000.0f);
        }
        
        AudioChunk chunk(samples, i);
        buffer.addChunk(chunk);
        
        std::cout << "Added chunk " << i << " (" << samples.size() << " samples)\n";
    }
    
    std::cout << "\nBuffer statistics:\n";
    std::cout << "  Chunk count: " << buffer.getChunkCount() << "\n";
    std::cout << "  Total samples: " << buffer.getTotalSamples() << "\n";
    std::cout << "  Buffer size: " << buffer.getBufferSizeBytes() << " bytes\n";
    std::cout << "  Duration: " << buffer.getDurationSeconds() << " seconds\n";
    
    // Get recent samples
    auto recentSamples = buffer.getRecentSamples(50);
    std::cout << "\nRecent 50 samples (first 10): ";
    for (size_t i = 0; i < std::min(size_t(10), recentSamples.size()); ++i) {
        std::cout << recentSamples[i] << " ";
    }
    std::cout << "...\n";
}

void demonstrateClientSessionAudio() {
    std::cout << "\n=== Client Session Audio Demo ===\n";
    
    ClientSession session("demo-session-001");
    
    // Configure audio format
    AudioFormat format;
    format.sampleRate = 16000;
    format.channels = 1;
    format.bitsPerSample = 16;
    format.chunkSize = 512;
    
    session.setAudioFormat(format);
    
    std::cout << "Created session: " << session.getSessionId() << "\n";
    std::cout << "Audio format configured\n";
    
    // Simulate receiving audio data from WebSocket
    const size_t chunkCount = 10;
    const size_t samplesPerChunk = 512;
    
    std::cout << "\nSimulating audio stream (" << chunkCount << " chunks)...\n";
    
    for (size_t i = 0; i < chunkCount; ++i) {
        // Create PCM data for this chunk
        auto pcmData = createSyntheticPCM(samplesPerChunk, 440.0 + i * 50.0);
        std::string_view dataView(reinterpret_cast<const char*>(pcmData.data()), pcmData.size());
        
        // Ingest the audio data
        bool success = session.ingestAudioData(dataView);
        if (success) {
            std::cout << "  Chunk " << i << ": " << pcmData.size() << " bytes ingested\n";
        } else {
            std::cout << "  Chunk " << i << ": FAILED to ingest\n";
        }
        
        // Small delay to simulate real-time
        std::this_thread::sleep_for(std::chrono::milliseconds(32)); // ~31.25 FPS
    }
    
    // Show final statistics
    auto stats = session.getAudioStatistics();
    std::cout << "\nFinal statistics:\n";
    std::cout << "  Total bytes ingested: " << stats.totalBytesIngested << "\n";
    std::cout << "  Total chunks ingested: " << stats.totalChunksIngested << "\n";
    std::cout << "  Dropped chunks: " << stats.droppedChunks << "\n";
    std::cout << "  Average chunk size: " << stats.averageChunkSize << " bytes\n";
    std::cout << "  Buffer utilization: " << (stats.bufferUtilization * 100) << "%\n";
    
    // Get audio buffer info
    auto audioBuffer = session.getAudioBuffer();
    if (audioBuffer) {
        std::cout << "\nAudio buffer:\n";
        std::cout << "  Chunks in buffer: " << audioBuffer->getChunkCount() << "\n";
        std::cout << "  Total samples: " << audioBuffer->getTotalSamples() << "\n";
        std::cout << "  Buffer duration: " << audioBuffer->getDurationSeconds() << " seconds\n";
    }
}

void demonstrateRealTimePerformance() {
    std::cout << "\n=== Real-Time Performance Demo ===\n";
    
    ClientSession session("perf-test-session");
    
    // Real-time audio parameters
    const size_t sampleRate = 16000;
    const size_t chunkSizeMs = 64; // 64ms chunks
    const size_t samplesPerChunk = (sampleRate * chunkSizeMs) / 1000; // 1024 samples
    const size_t testDurationMs = 5000; // 5 seconds
    const size_t totalChunks = testDurationMs / chunkSizeMs;
    
    std::cout << "Real-time simulation parameters:\n";
    std::cout << "  Sample rate: " << sampleRate << " Hz\n";
    std::cout << "  Chunk duration: " << chunkSizeMs << " ms\n";
    std::cout << "  Samples per chunk: " << samplesPerChunk << "\n";
    std::cout << "  Test duration: " << testDurationMs << " ms\n";
    std::cout << "  Total chunks: " << totalChunks << "\n";
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Process chunks at real-time intervals
    for (size_t i = 0; i < totalChunks; ++i) {
        auto chunkStartTime = std::chrono::high_resolution_clock::now();
        
        // Create and process audio chunk
        auto pcmData = createSyntheticPCM(samplesPerChunk, 440.0);
        std::string_view dataView(reinterpret_cast<const char*>(pcmData.data()), pcmData.size());
        
        bool success = session.ingestAudioData(dataView);
        
        auto chunkEndTime = std::chrono::high_resolution_clock::now();
        auto processingTime = std::chrono::duration_cast<std::chrono::microseconds>(
            chunkEndTime - chunkStartTime);
        
        if (i % 10 == 0) { // Log every 10th chunk
            std::cout << "Chunk " << i << ": " << processingTime.count() << " μs";
            if (!success) {
                std::cout << " (FAILED)";
            }
            std::cout << "\n";
        }
        
        // Sleep to maintain real-time pace
        std::this_thread::sleep_for(std::chrono::milliseconds(chunkSizeMs));
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "\nPerformance results:\n";
    std::cout << "  Total processing time: " << totalTime.count() << " ms\n";
    std::cout << "  Expected time: " << testDurationMs << " ms\n";
    std::cout << "  Real-time factor: " << (static_cast<double>(testDurationMs) / totalTime.count()) << "x\n";
    
    auto stats = session.getAudioStatistics();
    std::cout << "  Chunks processed: " << stats.totalChunksIngested << "/" << totalChunks << "\n";
    std::cout << "  Success rate: " << (100.0 * stats.totalChunksIngested / totalChunks) << "%\n";
}

int main() {
    std::cout << "SpeechRNT Audio Ingestion System Demo\n";
    std::cout << "=====================================\n";
    
    try {
        demonstrateAudioProcessor();
        demonstrateAudioBuffer();
        demonstrateClientSessionAudio();
        demonstrateRealTimePerformance();
        
        std::cout << "\n=== Demo Complete ===\n";
        std::cout << "All audio ingestion components working correctly!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Demo failed with error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}