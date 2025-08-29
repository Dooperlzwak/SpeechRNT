#include <gtest/gtest.h>
#include "core/client_session.hpp"
#include "core/websocket_server.hpp"
#include "audio/audio_processor.hpp"
#include <vector>
#include <string>
#include <thread>
#include <chrono>

using namespace core;
using namespace audio;

class AudioIngestionIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        session_ = std::make_unique<ClientSession>("test-session-123");
        
        // Set up audio format for testing
        AudioFormat format;
        format.sampleRate = 16000;
        format.channels = 1;
        format.bitsPerSample = 16;
        format.chunkSize = 1024;
        
        session_->setAudioFormat(format);
    }
    
    void TearDown() override {
        session_.reset();
    }
    
    // Helper function to create PCM audio data
    std::vector<uint8_t> createTestPCMData(size_t sampleCount, int16_t baseValue = 1000) {
        std::vector<uint8_t> data;
        data.reserve(sampleCount * 2);
        
        for (size_t i = 0; i < sampleCount; ++i) {
            int16_t sample = baseValue + static_cast<int16_t>(i % 100);
            data.push_back(sample & 0xFF);         // Low byte
            data.push_back((sample >> 8) & 0xFF);  // High byte
        }
        
        return data;
    }
    
    // Helper function to simulate continuous audio streaming
    void simulateAudioStream(size_t chunkCount, size_t samplesPerChunk) {
        for (size_t i = 0; i < chunkCount; ++i) {
            auto pcmData = createTestPCMData(samplesPerChunk, 1000 + i * 100);
            std::string_view dataView(reinterpret_cast<const char*>(pcmData.data()), pcmData.size());
            
            bool success = session_->ingestAudioData(dataView);
            EXPECT_TRUE(success) << "Failed to ingest chunk " << i;
            
            // Small delay to simulate real-time streaming
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    std::unique_ptr<ClientSession> session_;
};

TEST_F(AudioIngestionIntegrationTest, BasicAudioIngestion) {
    // Create test audio data (1024 samples = 2048 bytes)
    auto pcmData = createTestPCMData(1024);
    std::string_view dataView(reinterpret_cast<const char*>(pcmData.data()), pcmData.size());
    
    // Ingest the audio data
    EXPECT_TRUE(session_->ingestAudioData(dataView));
    
    // Verify audio buffer contains the data
    auto audioBuffer = session_->getAudioBuffer();
    ASSERT_NE(audioBuffer, nullptr);
    
    EXPECT_GT(audioBuffer->getChunkCount(), 0);
    EXPECT_EQ(audioBuffer->getTotalSamples(), 1024);
    
    // Verify statistics
    auto stats = session_->getAudioStatistics();
    EXPECT_EQ(stats.totalBytesIngested, pcmData.size());
    EXPECT_GT(stats.totalChunksIngested, 0);
    EXPECT_EQ(stats.droppedChunks, 0);
}

TEST_F(AudioIngestionIntegrationTest, ContinuousAudioStreaming) {
    const size_t chunkCount = 10;
    const size_t samplesPerChunk = 512;
    
    // Simulate continuous audio streaming
    simulateAudioStream(chunkCount, samplesPerChunk);
    
    // Verify all data was ingested
    auto audioBuffer = session_->getAudioBuffer();
    ASSERT_NE(audioBuffer, nullptr);
    
    EXPECT_EQ(audioBuffer->getTotalSamples(), chunkCount * samplesPerChunk);
    
    // Verify statistics
    auto stats = session_->getAudioStatistics();
    EXPECT_EQ(stats.totalBytesIngested, chunkCount * samplesPerChunk * 2); // 2 bytes per sample
    EXPECT_EQ(stats.totalChunksIngested, chunkCount);
    EXPECT_EQ(stats.droppedChunks, 0);
}

TEST_F(AudioIngestionIntegrationTest, AudioFormatValidation) {
    // Test with correct 16kHz mono 16-bit format
    AudioFormat correctFormat;
    correctFormat.sampleRate = 16000;
    correctFormat.channels = 1;
    correctFormat.bitsPerSample = 16;
    correctFormat.chunkSize = 1024;
    
    session_->setAudioFormat(correctFormat);
    
    auto pcmData = createTestPCMData(1024);
    std::string_view dataView(reinterpret_cast<const char*>(pcmData.data()), pcmData.size());
    
    EXPECT_TRUE(session_->ingestAudioData(dataView));
    
    // Verify format is correctly applied
    const auto& sessionFormat = session_->getAudioFormat();
    EXPECT_EQ(sessionFormat.sampleRate, 16000);
    EXPECT_EQ(sessionFormat.channels, 1);
    EXPECT_EQ(sessionFormat.bitsPerSample, 16);
}

TEST_F(AudioIngestionIntegrationTest, AudioDataIntegrity) {
    // Create known test pattern
    std::vector<int16_t> testPattern = {0, 1000, -1000, 16000, -16000, 32767, -32768};
    
    std::vector<uint8_t> pcmData;
    for (int16_t sample : testPattern) {
        pcmData.push_back(sample & 0xFF);
        pcmData.push_back((sample >> 8) & 0xFF);
    }
    
    std::string_view dataView(reinterpret_cast<const char*>(pcmData.data()), pcmData.size());
    
    // Ingest the data
    EXPECT_TRUE(session_->ingestAudioData(dataView));
    
    // Retrieve and verify the data
    auto audioBuffer = session_->getAudioBuffer();
    ASSERT_NE(audioBuffer, nullptr);
    
    auto allSamples = audioBuffer->getAllSamples();
    ASSERT_EQ(allSamples.size(), testPattern.size());
    
    // Verify conversion accuracy (16-bit PCM to float)
    for (size_t i = 0; i < testPattern.size(); ++i) {
        float expected = static_cast<float>(testPattern[i]) / 32768.0f;
        EXPECT_NEAR(allSamples[i], expected, 0.001f) 
            << "Sample " << i << " conversion error";
    }
}

TEST_F(AudioIngestionIntegrationTest, BufferManagement) {
    // Test buffer behavior with large amounts of data
    const size_t largeChunkCount = 100;
    const size_t samplesPerChunk = 1024;
    
    simulateAudioStream(largeChunkCount, samplesPerChunk);
    
    auto audioBuffer = session_->getAudioBuffer();
    ASSERT_NE(audioBuffer, nullptr);
    
    // Buffer should contain all the data (or manage overflow gracefully)
    size_t totalExpectedSamples = largeChunkCount * samplesPerChunk;
    size_t actualSamples = audioBuffer->getTotalSamples();
    
    // Either all samples are stored, or buffer management kicked in
    EXPECT_GT(actualSamples, 0);
    EXPECT_LE(actualSamples, totalExpectedSamples);
    
    // Verify we can retrieve recent samples
    auto recentSamples = audioBuffer->getRecentSamples(1024);
    EXPECT_EQ(recentSamples.size(), 1024);
}

TEST_F(AudioIngestionIntegrationTest, ErrorHandling) {
    // Test with invalid data (odd number of bytes for 16-bit samples)
    std::vector<uint8_t> invalidData(1023, 0); // Odd number of bytes
    std::string_view invalidDataView(reinterpret_cast<const char*>(invalidData.data()), invalidData.size());
    
    // This should still succeed but log a warning
    EXPECT_TRUE(session_->ingestAudioData(invalidDataView));
    
    // Test with empty data
    std::string_view emptyData;
    EXPECT_TRUE(session_->ingestAudioData(emptyData));
}

TEST_F(AudioIngestionIntegrationTest, ConcurrentAccess) {
    const size_t threadCount = 4;
    const size_t chunksPerThread = 25;
    const size_t samplesPerChunk = 256;
    
    std::vector<std::thread> threads;
    
    // Launch multiple threads to simulate concurrent audio ingestion
    for (size_t t = 0; t < threadCount; ++t) {
        threads.emplace_back([this, t, chunksPerThread, samplesPerChunk]() {
            for (size_t i = 0; i < chunksPerThread; ++i) {
                auto pcmData = createTestPCMData(samplesPerChunk, 1000 + t * 1000 + i);
                std::string_view dataView(reinterpret_cast<const char*>(pcmData.data()), pcmData.size());
                
                bool success = session_->ingestAudioData(dataView);
                EXPECT_TRUE(success);
                
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify all data was processed
    auto stats = session_->getAudioStatistics();
    size_t expectedTotalBytes = threadCount * chunksPerThread * samplesPerChunk * 2;
    
    EXPECT_EQ(stats.totalBytesIngested, expectedTotalBytes);
    EXPECT_EQ(stats.totalChunksIngested, threadCount * chunksPerThread);
    
    // Buffer should contain data from all threads
    auto audioBuffer = session_->getAudioBuffer();
    ASSERT_NE(audioBuffer, nullptr);
    EXPECT_GT(audioBuffer->getTotalSamples(), 0);
}

TEST_F(AudioIngestionIntegrationTest, RealTimePerformance) {
    // Test performance with real-time audio constraints
    // 16kHz mono = 16,000 samples/sec = 32,000 bytes/sec
    // At 64ms chunks = 1024 samples = 2048 bytes per chunk
    // Should process 15.625 chunks per second
    
    const size_t chunkCount = 50; // ~3.2 seconds of audio
    const size_t samplesPerChunk = 1024;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    simulateAudioStream(chunkCount, samplesPerChunk);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // Processing should be much faster than real-time
    // 50 chunks * 64ms = 3200ms of audio should process in < 1000ms
    EXPECT_LT(duration.count(), 1000) << "Audio processing too slow for real-time";
    
    // Verify all data was processed correctly
    auto stats = session_->getAudioStatistics();
    EXPECT_EQ(stats.totalChunksIngested, chunkCount);
    EXPECT_EQ(stats.droppedChunks, 0);
}

TEST_F(AudioIngestionIntegrationTest, SessionLifecycle) {
    // Test complete session lifecycle with audio ingestion
    
    // 1. Initial state
    auto stats = session_->getAudioStatistics();
    EXPECT_EQ(stats.totalBytesIngested, 0);
    EXPECT_EQ(stats.totalChunksIngested, 0);
    
    // 2. Ingest some audio
    simulateAudioStream(5, 512);
    
    stats = session_->getAudioStatistics();
    EXPECT_GT(stats.totalBytesIngested, 0);
    EXPECT_EQ(stats.totalChunksIngested, 5);
    
    // 3. Clear buffer
    session_->clearAudioBuffer();
    
    auto audioBuffer = session_->getAudioBuffer();
    EXPECT_EQ(audioBuffer->getChunkCount(), 0);
    EXPECT_EQ(audioBuffer->getTotalSamples(), 0);
    
    // 4. Statistics should persist (not reset by buffer clear)
    stats = session_->getAudioStatistics();
    EXPECT_GT(stats.totalBytesIngested, 0); // Still shows historical data
    
    // 5. Continue ingesting after clear
    simulateAudioStream(3, 256);
    
    stats = session_->getAudioStatistics();
    EXPECT_EQ(stats.totalChunksIngested, 8); // 5 + 3
}

// Main function for running integration tests
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}