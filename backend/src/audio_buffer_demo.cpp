#include "audio/audio_buffer_manager.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cmath>
#include <random>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace audio;

/**
 * AudioBufferManager Demo
 * 
 * This demo shows how to use the AudioBufferManager for streaming audio processing.
 * It simulates multiple concurrent audio streams with different characteristics.
 */

class AudioBufferDemo {
public:
    AudioBufferDemo() {
        // Configure AudioBufferManager for demo
        AudioBufferManager::BufferConfig config;
        config.maxBufferSizeMB = 4;           // 4MB per utterance
        config.maxUtterances = 5;             // Support 5 concurrent streams
        config.cleanupIntervalMs = 2000;      // Cleanup every 2 seconds
        config.maxIdleTimeMs = 10000;         // Remove idle buffers after 10 seconds
        config.enableCircularBuffer = true;   // Use circular buffers
        
        bufferManager_ = std::make_unique<AudioBufferManager>(config);
        
        std::cout << "AudioBufferManager Demo initialized\n";
        std::cout << "Configuration:\n";
        std::cout << "  Max Buffer Size: " << config.maxBufferSizeMB << " MB per utterance\n";
        std::cout << "  Max Utterances: " << config.maxUtterances << "\n";
        std::cout << "  Cleanup Interval: " << config.cleanupIntervalMs << " ms\n";
        std::cout << "  Max Idle Time: " << config.maxIdleTimeMs << " ms\n";
        std::cout << "  Circular Buffer: " << (config.enableCircularBuffer ? "enabled" : "disabled") << "\n\n";
    }
    
    void runDemo() {
        std::cout << "=== AudioBufferManager Demo ===\n\n";
        
        // Demo 1: Basic buffer operations
        demonstrateBasicOperations();
        
        // Demo 2: Concurrent audio streams
        demonstrateConcurrentStreams();
        
        // Demo 3: Memory management and cleanup
        demonstrateMemoryManagement();
        
        // Demo 4: Performance monitoring
        demonstratePerformanceMonitoring();
        
        std::cout << "\n=== Demo Complete ===\n";
    }

private:
    std::unique_ptr<AudioBufferManager> bufferManager_;
    
    // Generate test audio with specified characteristics
    std::vector<float> generateAudio(size_t sampleCount, float frequency = 440.0f, float amplitude = 0.5f) {
        std::vector<float> audio;
        audio.reserve(sampleCount);
        
        for (size_t i = 0; i < sampleCount; ++i) {
            float sample = amplitude * std::sin(2.0f * M_PI * frequency * i / 16000.0f);
            audio.push_back(sample);
        }
        
        return audio;
    }
    
    // Generate audio with noise
    std::vector<float> generateNoisyAudio(size_t sampleCount, float signalFreq = 440.0f, float noiseLevel = 0.1f) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<float> noise(0.0f, noiseLevel);
        
        auto audio = generateAudio(sampleCount, signalFreq, 0.5f);
        
        for (auto& sample : audio) {
            sample += noise(gen);
            sample = std::max(-1.0f, std::min(1.0f, sample)); // Clamp to valid range
        }
        
        return audio;
    }
    
    void demonstrateBasicOperations() {
        std::cout << "1. Basic Buffer Operations\n";
        std::cout << "   Creating utterance and adding audio data...\n";
        
        uint32_t utteranceId = 1;
        
        // Create utterance buffer
        if (bufferManager_->createUtterance(utteranceId, 2)) { // 2MB buffer
            std::cout << "   ✓ Created utterance buffer for ID: " << utteranceId << "\n";
        }
        
        // Add audio data in chunks
        for (int i = 0; i < 5; ++i) {
            auto audioChunk = generateAudio(8000, 440.0f + i * 100.0f); // 0.5 seconds each
            if (bufferManager_->addAudioData(utteranceId, audioChunk)) {
                std::cout << "   ✓ Added audio chunk " << (i + 1) << " (" << audioChunk.size() << " samples)\n";
            }
        }
        
        // Retrieve buffered audio
        auto bufferedAudio = bufferManager_->getBufferedAudio(utteranceId);
        std::cout << "   ✓ Retrieved " << bufferedAudio.size() << " samples from buffer\n";
        
        // Get recent audio
        auto recentAudio = bufferManager_->getRecentAudio(utteranceId, 16000);
        std::cout << "   ✓ Retrieved " << recentAudio.size() << " recent samples\n";
        
        // Finalize utterance
        bufferManager_->finalizeBuffer(utteranceId);
        std::cout << "   ✓ Finalized utterance buffer\n";
        
        std::cout << "   Memory usage: " << bufferManager_->getCurrentMemoryUsageMB() << " MB\n\n";
    }
    
    void demonstrateConcurrentStreams() {
        std::cout << "2. Concurrent Audio Streams\n";
        std::cout << "   Simulating multiple concurrent audio streams...\n";
        
        const int numStreams = 3;
        std::vector<std::thread> streamThreads;
        
        // Start concurrent audio streams
        for (int streamId = 1; streamId <= numStreams; ++streamId) {
            streamThreads.emplace_back([this, streamId]() {
                uint32_t utteranceId = 100 + streamId;
                
                // Create utterance
                bufferManager_->createUtterance(utteranceId);
                
                // Simulate streaming audio for 3 seconds
                for (int chunk = 0; chunk < 12; ++chunk) {
                    // Generate audio with different characteristics per stream
                    float frequency = 440.0f + streamId * 200.0f;
                    auto audioChunk = generateNoisyAudio(4000, frequency, 0.05f); // 0.25 seconds
                    
                    bufferManager_->addAudioData(utteranceId, audioChunk);
                    
                    // Simulate real-time streaming
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
                
                // Finalize stream
                bufferManager_->finalizeBuffer(utteranceId);
                
                std::cout << "   ✓ Stream " << streamId << " completed\n";
            });
        }
        
        // Monitor progress
        std::thread monitorThread([this, numStreams]() {
            for (int i = 0; i < 15; ++i) {
                auto stats = bufferManager_->getStatistics();
                std::cout << "   Active utterances: " << stats.activeUtterances 
                         << ", Memory: " << stats.totalMemoryUsageMB << " MB\r" << std::flush;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            std::cout << "\n";
        });
        
        // Wait for all streams to complete
        for (auto& thread : streamThreads) {
            thread.join();
        }
        monitorThread.join();
        
        auto finalStats = bufferManager_->getStatistics();
        std::cout << "   Final statistics:\n";
        std::cout << "     Total utterances: " << finalStats.totalUtterances << "\n";
        std::cout << "     Total audio samples: " << finalStats.totalAudioSamples << "\n";
        std::cout << "     Memory usage: " << finalStats.totalMemoryUsageMB << " MB\n\n";
    }
    
    void demonstrateMemoryManagement() {
        std::cout << "3. Memory Management and Cleanup\n";
        std::cout << "   Testing buffer limits and cleanup mechanisms...\n";
        
        // Create many utterances to test limits
        std::vector<uint32_t> utteranceIds;
        for (int i = 0; i < 8; ++i) { // More than maxUtterances (5)
            uint32_t utteranceId = 200 + i;
            utteranceIds.push_back(utteranceId);
            
            if (bufferManager_->createUtterance(utteranceId)) {
                // Add significant audio data
                auto largeAudio = generateAudio(32000); // 2 seconds
                bufferManager_->addAudioData(utteranceId, largeAudio);
                std::cout << "   ✓ Created utterance " << utteranceId << "\n";
            }
        }
        
        std::cout << "   Current utterance count: " << bufferManager_->getUtteranceCount() << "\n";
        std::cout << "   Memory usage: " << bufferManager_->getCurrentMemoryUsageMB() << " MB\n";
        
        // Finalize some utterances
        for (size_t i = 0; i < utteranceIds.size() / 2; ++i) {
            bufferManager_->finalizeBuffer(utteranceIds[i]);
        }
        
        std::cout << "   Finalized " << (utteranceIds.size() / 2) << " utterances\n";
        
        // Trigger cleanup
        bufferManager_->cleanupInactiveBuffers();
        std::cout << "   ✓ Cleaned up inactive buffers\n";
        std::cout << "   Utterance count after cleanup: " << bufferManager_->getUtteranceCount() << "\n";
        std::cout << "   Memory usage after cleanup: " << bufferManager_->getCurrentMemoryUsageMB() << " MB\n";
        
        // Force cleanup
        bufferManager_->forceCleanup();
        std::cout << "   ✓ Force cleanup completed\n";
        std::cout << "   Final utterance count: " << bufferManager_->getUtteranceCount() << "\n\n";
    }
    
    void demonstratePerformanceMonitoring() {
        std::cout << "4. Performance Monitoring\n";
        std::cout << "   Demonstrating health monitoring and statistics...\n";
        
        // Create some test utterances
        for (int i = 1; i <= 3; ++i) {
            uint32_t utteranceId = 300 + i;
            bufferManager_->createUtterance(utteranceId);
            
            // Add varying amounts of audio
            auto audio = generateAudio(16000 * i); // 1, 2, 3 seconds respectively
            bufferManager_->addAudioData(utteranceId, audio);
        }
        
        // Get detailed statistics
        auto stats = bufferManager_->getStatistics();
        std::cout << "   Statistics:\n";
        std::cout << "     Active utterances: " << stats.activeUtterances << "\n";
        std::cout << "     Total utterances: " << stats.totalUtterances << "\n";
        std::cout << "     Total audio samples: " << stats.totalAudioSamples << "\n";
        std::cout << "     Memory usage: " << stats.totalMemoryUsageMB << " MB\n";
        std::cout << "     Peak memory: " << stats.peakMemoryUsageMB << " MB\n";
        std::cout << "     Buffer utilization: " << (stats.averageBufferUtilization * 100.0) << "%\n";
        std::cout << "     Dropped samples: " << stats.droppedSamples << "\n";
        
        // Health status
        std::cout << "\n   Health Status:\n";
        std::cout << "     Is healthy: " << (bufferManager_->isHealthy() ? "Yes" : "No") << "\n";
        
        std::string healthReport = bufferManager_->getHealthStatus();
        std::cout << "\n   Detailed Health Report:\n";
        std::cout << healthReport << "\n";
        
        // Clean up
        bufferManager_->forceCleanup();
    }
};

int main() {
    // Initialize logging
    utils::Logger::initialize();
    
    try {
        AudioBufferDemo demo;
        demo.runDemo();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        return 1;
    }
}