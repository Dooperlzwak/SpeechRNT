#include <gtest/gtest.h>
#include "audio/audio_processor.hpp"
#include <vector>
#include <string>
#include <cstring>

using namespace audio;

class AudioProcessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        format_.sampleRate = 16000;
        format_.channels = 1;
        format_.bitsPerSample = 16;
        format_.chunkSize = 1024;
        
        processor_ = std::make_unique<AudioProcessor>(format_);
    }
    
    void TearDown() override {
        processor_.reset();
    }
    
    // Helper function to create PCM data
    std::vector<uint8_t> createPCMData(const std::vector<int16_t>& samples) {
        std::vector<uint8_t> data;
        data.reserve(samples.size() * 2);
        
        for (int16_t sample : samples) {
            data.push_back(sample & 0xFF);         // Low byte
            data.push_back((sample >> 8) & 0xFF);  // High byte
        }
        
        return data;
    }
    
    AudioFormat format_;
    std::unique_ptr<AudioProcessor> processor_;
};

TEST_F(AudioProcessorTest, ValidateFormat) {
    // Valid format
    EXPECT_TRUE(processor_->validateFormat(format_));
    
    // Invalid sample rate
    AudioFormat invalidFormat = format_;
    invalidFormat.sampleRate = 44100;
    EXPECT_FALSE(processor_->validateFormat(invalidFormat));
    
    // Invalid channels
    invalidFormat = format_;
    invalidFormat.channels = 2;
    EXPECT_FALSE(processor_->validateFormat(invalidFormat));
    
    // Invalid bits per sample
    invalidFormat = format_;
    invalidFormat.bitsPerSample = 24;
    EXPECT_FALSE(processor_->validateFormat(invalidFormat));
}

TEST_F(AudioProcessorTest, ValidatePCMData) {
    // Valid PCM data (even number of bytes)
    std::string validData(1024, 0);
    EXPECT_TRUE(processor_->validatePCMData(validData));
    
    // Invalid PCM data (odd number of bytes)
    std::string invalidData(1023, 0);
    EXPECT_FALSE(processor_->validatePCMData(invalidData));
    
    // Empty data
    std::string emptyData;
    EXPECT_TRUE(processor_->validatePCMData(emptyData));
}

TEST_F(AudioProcessorTest, ConvertPCMToFloat) {
    // Test PCM to float conversion
    std::vector<int16_t> pcmSamples = {0, 16383, -16384, 32767, -32768};
    std::vector<uint8_t> pcmData = createPCMData(pcmSamples);
    
    std::string_view dataView(reinterpret_cast<const char*>(pcmData.data()), pcmData.size());
    std::vector<float> floatSamples = processor_->convertPCMToFloat(dataView);
    
    ASSERT_EQ(floatSamples.size(), pcmSamples.size());
    
    // Check conversion accuracy
    EXPECT_NEAR(floatSamples[0], 0.0f, 0.001f);           // 0 -> 0.0
    EXPECT_NEAR(floatSamples[1], 0.5f, 0.001f);           // 16383 -> ~0.5
    EXPECT_NEAR(floatSamples[2], -0.5f, 0.001f);          // -16384 -> ~-0.5
    EXPECT_NEAR(floatSamples[3], 1.0f, 0.001f);           // 32767 -> ~1.0
    EXPECT_NEAR(floatSamples[4], -1.0f, 0.001f);          // -32768 -> -1.0
}

TEST_F(AudioProcessorTest, ConvertFloatToPCM) {
    // Test float to PCM conversion
    std::vector<float> floatSamples = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f};
    std::vector<int16_t> pcmSamples = processor_->convertFloatToPCM(floatSamples);
    
    ASSERT_EQ(pcmSamples.size(), floatSamples.size());
    
    // Check conversion accuracy
    EXPECT_EQ(pcmSamples[0], 0);
    EXPECT_NEAR(pcmSamples[1], 16383, 1);     // 0.5 -> ~16383
    EXPECT_NEAR(pcmSamples[2], -16383, 1);    // -0.5 -> ~-16383
    EXPECT_EQ(pcmSamples[3], 32767);          // 1.0 -> 32767
    EXPECT_EQ(pcmSamples[4], -32767);         // -1.0 -> -32767
}

TEST_F(AudioProcessorTest, ProcessRawData) {
    // Create test PCM data
    std::vector<int16_t> pcmSamples = {1000, -1000, 2000, -2000};
    std::vector<uint8_t> pcmData = createPCMData(pcmSamples);
    
    std::string_view dataView(reinterpret_cast<const char*>(pcmData.data()), pcmData.size());
    AudioChunk chunk = processor_->processRawData(dataView);
    
    EXPECT_EQ(chunk.samples.size(), pcmSamples.size());
    EXPECT_GT(chunk.sequenceNumber, 0);
    
    // Verify statistics
    EXPECT_EQ(processor_->getTotalBytesProcessed(), pcmData.size());
    EXPECT_EQ(processor_->getTotalChunksProcessed(), 1);
}

TEST_F(AudioProcessorTest, ProcessStreamingData) {
    // Create multiple chunks of PCM data
    std::vector<int16_t> chunk1 = {1000, -1000};
    std::vector<int16_t> chunk2 = {2000, -2000};
    
    std::vector<uint8_t> data1 = createPCMData(chunk1);
    std::vector<uint8_t> data2 = createPCMData(chunk2);
    
    // Combine chunks
    std::vector<uint8_t> combinedData;
    combinedData.insert(combinedData.end(), data1.begin(), data1.end());
    combinedData.insert(combinedData.end(), data2.begin(), data2.end());
    
    std::string_view dataView(reinterpret_cast<const char*>(combinedData.data()), combinedData.size());
    
    // Set chunk size to match individual chunks
    AudioFormat smallChunkFormat = format_;
    smallChunkFormat.chunkSize = 2; // 2 samples per chunk
    processor_->setFormat(smallChunkFormat);
    
    std::vector<AudioChunk> chunks = processor_->processStreamingData(dataView);
    
    EXPECT_EQ(chunks.size(), 2);
    EXPECT_EQ(chunks[0].samples.size(), 2);
    EXPECT_EQ(chunks[1].samples.size(), 2);
}

class AudioBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_unique<AudioBuffer>(1024); // 1KB buffer
    }
    
    void TearDown() override {
        buffer_.reset();
    }
    
    std::unique_ptr<AudioBuffer> buffer_;
};

TEST_F(AudioBufferTest, AddAndRetrieveChunks) {
    // Create test chunks
    std::vector<float> samples1 = {1.0f, 2.0f, 3.0f};
    std::vector<float> samples2 = {4.0f, 5.0f, 6.0f};
    
    AudioChunk chunk1(samples1, 1);
    AudioChunk chunk2(samples2, 2);
    
    // Add chunks
    EXPECT_TRUE(buffer_->addChunk(chunk1));
    EXPECT_TRUE(buffer_->addChunk(chunk2));
    
    // Verify chunk count
    EXPECT_EQ(buffer_->getChunkCount(), 2);
    EXPECT_EQ(buffer_->getTotalSamples(), 6);
    
    // Retrieve chunks
    auto retrievedChunks = buffer_->getChunks();
    ASSERT_EQ(retrievedChunks.size(), 2);
    EXPECT_EQ(retrievedChunks[0].samples, samples1);
    EXPECT_EQ(retrievedChunks[1].samples, samples2);
}

TEST_F(AudioBufferTest, GetAllSamples) {
    // Add multiple chunks
    std::vector<float> samples1 = {1.0f, 2.0f};
    std::vector<float> samples2 = {3.0f, 4.0f};
    
    buffer_->addRawData(samples1);
    buffer_->addRawData(samples2);
    
    // Get all samples
    std::vector<float> allSamples = buffer_->getAllSamples();
    std::vector<float> expected = {1.0f, 2.0f, 3.0f, 4.0f};
    
    EXPECT_EQ(allSamples, expected);
}

TEST_F(AudioBufferTest, GetRecentSamples) {
    // Add samples
    std::vector<float> samples = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    buffer_->addRawData(samples);
    
    // Get recent samples
    std::vector<float> recent = buffer_->getRecentSamples(3);
    std::vector<float> expected = {3.0f, 4.0f, 5.0f};
    
    EXPECT_EQ(recent, expected);
    
    // Request more samples than available
    std::vector<float> all = buffer_->getRecentSamples(10);
    EXPECT_EQ(all, samples);
}

TEST_F(AudioBufferTest, BufferOverflow) {
    // Create a small buffer
    AudioBuffer smallBuffer(32); // 32 bytes = 8 float samples
    
    // Add data that exceeds buffer capacity
    std::vector<float> largeSamples(20, 1.0f); // 80 bytes
    
    // First addition should succeed
    EXPECT_TRUE(smallBuffer.addRawData(std::vector<float>(5, 1.0f))); // 20 bytes
    
    // Second addition should trigger cleanup and succeed
    EXPECT_TRUE(smallBuffer.addRawData(std::vector<float>(5, 2.0f))); // 20 bytes
    
    // Very large addition should fail
    EXPECT_FALSE(smallBuffer.addRawData(largeSamples));
}

class AudioIngestionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = std::make_unique<AudioIngestionManager>("test-session");
        manager_->setActive(true);
    }
    
    void TearDown() override {
        manager_.reset();
    }
    
    std::unique_ptr<AudioIngestionManager> manager_;
};

TEST_F(AudioIngestionManagerTest, IngestAudioData) {
    // Create test PCM data
    std::vector<int16_t> pcmSamples = {1000, -1000, 2000, -2000};
    std::vector<uint8_t> pcmData;
    
    for (int16_t sample : pcmSamples) {
        pcmData.push_back(sample & 0xFF);
        pcmData.push_back((sample >> 8) & 0xFF);
    }
    
    std::string_view dataView(reinterpret_cast<const char*>(pcmData.data()), pcmData.size());
    
    // Ingest data
    EXPECT_TRUE(manager_->ingestAudioData(dataView));
    
    // Verify statistics
    auto stats = manager_->getStatistics();
    EXPECT_EQ(stats.totalBytesIngested, pcmData.size());
    EXPECT_GT(stats.totalChunksIngested, 0);
    EXPECT_EQ(stats.droppedChunks, 0);
}

TEST_F(AudioIngestionManagerTest, InactiveSession) {
    // Deactivate session
    manager_->setActive(false);
    
    // Try to ingest data
    std::string testData(100, 0);
    EXPECT_FALSE(manager_->ingestAudioData(testData));
    EXPECT_EQ(manager_->getLastError(), AudioIngestionManager::ErrorCode::INACTIVE_SESSION);
}

TEST_F(AudioIngestionManagerTest, AudioFormatConfiguration) {
    // Test default format
    const auto& defaultFormat = manager_->getAudioFormat();
    EXPECT_EQ(defaultFormat.sampleRate, 16000);
    EXPECT_EQ(defaultFormat.channels, 1);
    EXPECT_EQ(defaultFormat.bitsPerSample, 16);
    
    // Set custom format
    AudioFormat customFormat;
    customFormat.sampleRate = 16000;
    customFormat.channels = 1;
    customFormat.bitsPerSample = 16;
    customFormat.chunkSize = 2048;
    
    manager_->setAudioFormat(customFormat);
    
    const auto& updatedFormat = manager_->getAudioFormat();
    EXPECT_EQ(updatedFormat.chunkSize, 2048);
}

TEST_F(AudioIngestionManagerTest, GetLatestAudio) {
    // Add some audio data
    std::vector<float> samples = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    
    // Convert to PCM and ingest
    std::vector<int16_t> pcmSamples;
    for (float sample : samples) {
        pcmSamples.push_back(static_cast<int16_t>(sample * 32767.0f));
    }
    
    std::vector<uint8_t> pcmData;
    for (int16_t sample : pcmSamples) {
        pcmData.push_back(sample & 0xFF);
        pcmData.push_back((sample >> 8) & 0xFF);
    }
    
    std::string_view dataView(reinterpret_cast<const char*>(pcmData.data()), pcmData.size());
    EXPECT_TRUE(manager_->ingestAudioData(dataView));
    
    // Get latest audio
    std::vector<float> latest = manager_->getLatestAudio(3);
    EXPECT_EQ(latest.size(), 3);
    
    // Values should be approximately the last 3 samples
    EXPECT_NEAR(latest[0], 3.0f, 0.1f);
    EXPECT_NEAR(latest[1], 4.0f, 0.1f);
    EXPECT_NEAR(latest[2], 5.0f, 0.1f);
}

// Main function for running tests
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}