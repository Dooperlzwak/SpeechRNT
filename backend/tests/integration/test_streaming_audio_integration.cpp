#include <gtest/gtest.h>
#include "stt/streaming_audio_manager.hpp"
#include "stt/whisper_stt.hpp"
#include "audio/audio_buffer_manager.hpp"
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace stt;
using namespace audio;

class StreamingAudioIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create WhisperSTT instance (will use simulation mode if whisper.cpp not available)
        whisperSTT_ = std::make_shared<WhisperSTT>();
        
        // Initialize with a dummy model path (simulation mode will handle this gracefully)
        std::string modelPath = "test_model.bin";
        whisperSTT_->initialize(modelPath, 2);
        
        // Create StreamingAudioManager
        streamingManager_ = std::make_unique<StreamingAudioManager>(whisperSTT_);
        ASSERT_TRUE(streamingManager_->initialize());
    }
    
    void TearDown() override {
        if (streamingManager_) {
            streamingManager_->stopAllTranscriptions();
        }
        streamingManager_.reset();
        whisperSTT_.reset();
    }
    
    std::vector<float> generateTestAudio(size_t sampleCount, float frequency = 440.0f, float amplitude = 0.5f) {
        std::vector<float> audio;
        audio.reserve(sampleCount);
        
        for (size_t i = 0; i < sampleCount; ++i) {
            float sample = amplitude * std::sin(2.0f * M_PI * frequency * i / 16000.0f);
            audio.push_back(sample);
        }
        
        return audio;
    }
    
    std::vector<std::vector<float>> generateAudioChunks(size_t totalSamples, size_t chunkSize) {
        auto fullAudio = generateTestAudio(totalSamples);
        std::vector<std::vector<float>> chunks;
        
        for (size_t i = 0; i < fullAudio.size(); i += chunkSize) {
            size_t endIdx = std::min(i + chunkSize, fullAudio.size());
            chunks.emplace_back(fullAudio.begin() + i, fullAudio.begin() + endIdx);
        }
        
        return chunks;
    }
    
    std::shared_ptr<WhisperSTT> whisperSTT_;
    std::unique_ptr<StreamingAudioManager> streamingManager_;
};

TEST_F(StreamingAudioIntegrationTest, BasicStreamingTranscription) {
    uint32_t utteranceId = 1;
    std::atomic<int> callbackCount{0};
    std::atomic<bool> receivedPartial{false};
    std::atomic<bool> receivedFinal{false};
    
    // Set up callback to track results
    auto callback = [&](const TranscriptionResult& result) {
        callbackCount++;
        if (result.is_partial) {
            receivedPartial = true;
        } else {
            receivedFinal = true;
        }
        
        // Verify result structure
        EXPECT_GE(result.confidence, 0.0f);
        EXPECT_LE(result.confidence, 1.0f);
        EXPECT_GE(result.start_time_ms, 0);
        EXPECT_GE(result.end_time_ms, result.start_time_ms);
    };
    
    // Configure for quick transcription
    StreamingAudioManager::StreamingConfig config;
    config.transcriptionIntervalMs = 500;  // Transcribe every 500ms
    config.minAudioSamples = 8000;         // 0.5 seconds minimum
    config.enablePartialResults = true;
    
    // Start streaming transcription
    EXPECT_TRUE(streamingManager_->startStreamingTranscription(utteranceId, callback, config));
    EXPECT_TRUE(streamingManager_->isTranscribing(utteranceId));
    
    // Generate and add audio chunks (simulate 3 seconds of audio)
    auto audioChunks = generateAudioChunks(48000, 4000); // 3 seconds in 0.25s chunks
    
    for (const auto& chunk : audioChunks) {
        EXPECT_TRUE(streamingManager_->addAudioChunk(utteranceId, chunk));
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate real-time
    }
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Finalize transcription
    streamingManager_->finalizeStreamingTranscription(utteranceId);
    
    // Wait for final processing
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Verify we received callbacks
    EXPECT_GT(callbackCount.load(), 0);
    
    // Verify streaming is no longer active
    EXPECT_FALSE(streamingManager_->isTranscribing(utteranceId));
}

TEST_F(StreamingAudioIntegrationTest, ConcurrentStreamingTranscriptions) {
    const int numUtterances = 3;
    std::vector<std::atomic<int>> callbackCounts(numUtterances);
    std::vector<uint32_t> utteranceIds;
    
    // Initialize callback counters
    for (int i = 0; i < numUtterances; ++i) {
        callbackCounts[i] = 0;
        utteranceIds.push_back(i + 1);
    }
    
    // Start multiple streaming transcriptions
    for (int i = 0; i < numUtterances; ++i) {
        auto callback = [&callbackCounts, i](const TranscriptionResult& result) {
            callbackCounts[i]++;
        };
        
        StreamingAudioManager::StreamingConfig config;
        config.transcriptionIntervalMs = 800;
        config.minAudioSamples = 8000;
        
        EXPECT_TRUE(streamingManager_->startStreamingTranscription(
            utteranceIds[i], callback, config));
    }
    
    EXPECT_EQ(streamingManager_->getActiveTranscriptionCount(), numUtterances);
    
    // Add audio to all utterances concurrently
    std::vector<std::thread> audioThreads;
    
    for (int i = 0; i < numUtterances; ++i) {
        audioThreads.emplace_back([this, utteranceId = utteranceIds[i]]() {
            auto audioChunks = generateAudioChunks(32000, 4000); // 2 seconds
            
            for (const auto& chunk : audioChunks) {
                streamingManager_->addAudioChunk(utteranceId, chunk);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    }
    
    // Wait for all audio threads to complete
    for (auto& thread : audioThreads) {
        thread.join();
    }
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    
    // Finalize all transcriptions
    for (uint32_t utteranceId : utteranceIds) {
        streamingManager_->finalizeStreamingTranscription(utteranceId);
    }
    
    // Wait for final processing
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Verify all utterances received callbacks
    for (int i = 0; i < numUtterances; ++i) {
        EXPECT_GT(callbackCounts[i].load(), 0) << "Utterance " << i << " received no callbacks";
    }
    
    EXPECT_EQ(streamingManager_->getActiveTranscriptionCount(), 0);
}

TEST_F(StreamingAudioIntegrationTest, BufferMemoryManagement) {
    uint32_t utteranceId = 1;
    std::atomic<int> callbackCount{0};
    
    auto callback = [&](const TranscriptionResult& result) {
        callbackCount++;
    };
    
    // Configure with small buffer to test memory management
    StreamingAudioManager::StreamingConfig config;
    config.maxBufferSizeMB = 1;  // 1MB buffer
    config.transcriptionIntervalMs = 200;
    config.minAudioSamples = 4000;
    
    EXPECT_TRUE(streamingManager_->startStreamingTranscription(utteranceId, callback, config));
    
    // Get initial statistics
    auto initialStats = streamingManager_->getStatistics();
    
    // Add large amount of audio to test buffer management
    for (int i = 0; i < 20; ++i) {
        auto largeChunk = generateTestAudio(8000); // 0.5 seconds each
        EXPECT_TRUE(streamingManager_->addAudioChunk(utteranceId, largeChunk));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Get final statistics
    auto finalStats = streamingManager_->getStatistics();
    
    // Verify memory usage is reasonable (should not grow unbounded)
    EXPECT_LT(finalStats.bufferMemoryUsageMB, 10); // Should stay under 10MB
    EXPECT_GT(finalStats.totalAudioProcessed, initialStats.totalAudioProcessed);
    
    // Verify we received transcription callbacks
    EXPECT_GT(callbackCount.load(), 0);
    
    streamingManager_->finalizeStreamingTranscription(utteranceId);
}

TEST_F(StreamingAudioIntegrationTest, AudioBufferManagerIntegration) {
    uint32_t utteranceId = 1;
    
    auto callback = [](const TranscriptionResult& result) {
        // Simple callback for testing
    };
    
    EXPECT_TRUE(streamingManager_->startStreamingTranscription(utteranceId, callback));
    
    // Get direct access to AudioBufferManager for testing
    auto bufferManager = streamingManager_->getAudioBufferManager();
    ASSERT_NE(bufferManager, nullptr);
    
    // Verify utterance was created in buffer manager
    EXPECT_TRUE(bufferManager->hasUtterance(utteranceId));
    EXPECT_TRUE(bufferManager->isUtteranceActive(utteranceId));
    
    // Add audio through StreamingAudioManager
    auto testAudio = generateTestAudio(16000); // 1 second
    EXPECT_TRUE(streamingManager_->addAudioChunk(utteranceId, testAudio));
    
    // Verify audio is in buffer
    auto bufferedAudio = bufferManager->getBufferedAudio(utteranceId);
    EXPECT_EQ(bufferedAudio.size(), testAudio.size());
    
    // Test recent audio retrieval
    auto recentAudio = bufferManager->getRecentAudio(utteranceId, 8000);
    EXPECT_EQ(recentAudio.size(), 8000);
    
    // Finalize and verify cleanup
    streamingManager_->finalizeStreamingTranscription(utteranceId);
    EXPECT_FALSE(bufferManager->isUtteranceActive(utteranceId));
    
    // Stop transcription and verify removal
    streamingManager_->stopStreamingTranscription(utteranceId);
    EXPECT_FALSE(bufferManager->hasUtterance(utteranceId));
}

TEST_F(StreamingAudioIntegrationTest, HealthMonitoring) {
    // Test health status reporting
    std::string healthStatus = streamingManager_->getHealthStatus();
    EXPECT_FALSE(healthStatus.empty());
    EXPECT_NE(healthStatus.find("StreamingAudioManager"), std::string::npos);
    EXPECT_NE(healthStatus.find("Buffer Manager"), std::string::npos);
    
    // Start a transcription and check statistics
    uint32_t utteranceId = 1;
    auto callback = [](const TranscriptionResult& result) {};
    
    EXPECT_TRUE(streamingManager_->startStreamingTranscription(utteranceId, callback));
    
    auto stats = streamingManager_->getStatistics();
    EXPECT_EQ(stats.activeTranscriptions, 1);
    EXPECT_EQ(stats.totalTranscriptions, 1);
    
    // Add some audio and check updated statistics
    auto testAudio = generateTestAudio(16000);
    streamingManager_->addAudioChunk(utteranceId, testAudio);
    
    stats = streamingManager_->getStatistics();
    EXPECT_GT(stats.totalAudioProcessed, 0);
    
    streamingManager_->stopStreamingTranscription(utteranceId);
    
    stats = streamingManager_->getStatistics();
    EXPECT_EQ(stats.activeTranscriptions, 0);
}

TEST_F(StreamingAudioIntegrationTest, ErrorHandling) {
    uint32_t utteranceId = 1;
    auto callback = [](const TranscriptionResult& result) {};
    
    // Test duplicate transcription start
    EXPECT_TRUE(streamingManager_->startStreamingTranscription(utteranceId, callback));
    EXPECT_FALSE(streamingManager_->startStreamingTranscription(utteranceId, callback));
    
    // Test adding audio to non-existent utterance
    uint32_t nonExistentId = 999;
    auto testAudio = generateTestAudio(1000);
    EXPECT_FALSE(streamingManager_->addAudioChunk(nonExistentId, testAudio));
    
    // Test empty audio handling
    std::vector<float> emptyAudio;
    EXPECT_FALSE(streamingManager_->addAudioChunk(utteranceId, emptyAudio));
    
    // Test operations on stopped transcription
    streamingManager_->stopStreamingTranscription(utteranceId);
    EXPECT_FALSE(streamingManager_->isTranscribing(utteranceId));
    EXPECT_FALSE(streamingManager_->addAudioChunk(utteranceId, testAudio));
}

TEST_F(StreamingAudioIntegrationTest, ConfigurationVariations) {
    uint32_t utteranceId = 1;
    std::atomic<int> callbackCount{0};
    
    auto callback = [&](const TranscriptionResult& result) {
        callbackCount++;
    };
    
    // Test with different configurations
    StreamingAudioManager::StreamingConfig config;
    config.transcriptionIntervalMs = 100;  // Very frequent transcriptions
    config.minAudioSamples = 1600;         // 0.1 seconds minimum
    config.maxBufferSizeMB = 2;            // Small buffer
    config.enablePartialResults = true;
    config.confidenceThreshold = 0.3f;     // Lower confidence threshold
    
    EXPECT_TRUE(streamingManager_->startStreamingTranscription(utteranceId, callback, config));
    
    // Add audio in small chunks to trigger frequent transcriptions
    for (int i = 0; i < 10; ++i) {
        auto smallChunk = generateTestAudio(1600); // 0.1 seconds each
        EXPECT_TRUE(streamingManager_->addAudioChunk(utteranceId, smallChunk));
        std::this_thread::sleep_for(std::chrono::milliseconds(150)); // Allow transcription
    }
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    streamingManager_->finalizeStreamingTranscription(utteranceId);
    
    // Should have received multiple callbacks due to frequent transcription
    EXPECT_GT(callbackCount.load(), 1);
}