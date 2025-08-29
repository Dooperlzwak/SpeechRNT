#include <gtest/gtest.h>
#include "audio/audio_buffer.hpp"
#include <vector>
#include <thread>
#include <chrono>
#include <random>

namespace audio {

class AudioBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_unique<AudioBuffer>(16000, 1024);
    }
    
    void TearDown() override {
        buffer_.reset();
    }
    
    std::unique_ptr<AudioBuffer> buffer_;
};

// Test basic audio buffer operations
TEST_F(AudioBufferTest, BasicOperations) {
    EXPECT_EQ(buffer_->getSampleRate(), 16000);
    EXPECT_EQ(buffer_->getChannels(), 1);
    EXPECT_EQ(buffer_->size(), 0);
    EXPECT_TRUE(buffer_->empty());
}

// Test adding PCM data
TEST_F(AudioBufferTest, AddPCMData) {
    std::vector<int16_t> pcmData = {1000, -1000, 2000, -2000, 0};
    buffer_->addPCMData(pcmData.data(), pcmData.size());
    
    EXPECT_EQ(buffer_->size(), 5);
    EXPECT_FALSE(buffer_->empty());
    
    auto samples = buffer_->getAllSamples();
    EXPECT_EQ(samples.size(), 5);
    
    // Check conversion from int16 to float
    EXPECT_NEAR(samples[0], 1000.0f / 32768.0f, 0.001f);
    EXPECT_NEAR(samples[1], -1000.0f / 32768.0f, 0.001f);
    EXPECT_NEAR(samples[4], 0.0f, 0.001f);
}

// Test adding float data
TEST_F(AudioBufferTest, AddFloatData) {
    std::vector<float> floatData = {0.5f, -0.5f, 0.25f, -0.25f, 0.0f};
    buffer_->addFloatData(floatData.data(), floatData.size());
    
    EXPECT_EQ(buffer_->size(), 5);
    
    auto samples = buffer_->getAllSamples();
    EXPECT_EQ(samples.size(), 5);
    
    for (size_t i = 0; i < floatData.size(); ++i) {
        EXPECT_NEAR(samples[i], floatData[i], 0.001f);
    }
}

// Test buffer capacity and overflow handling
TEST_F(AudioBufferTest, BufferCapacity) {
    const size_t maxCapacity = buffer_->getMaxCapacity();
    
    // Fill buffer to capacity
    std::vector<float> data(maxCapacity, 0.5f);
    buffer_->addFloatData(data.data(), data.size());
    
    EXPECT_EQ(buffer_->size(), maxCapacity);
    
    // Try to add more data (should handle overflow)
    std::vector<float> extraData(100, 0.25f);
    buffer_->addFloatData(extraData.data(), extraData.size());
    
    // Buffer should still be at max capacity or handle overflow gracefully
    EXPECT_LE(buffer_->size(), maxCapacity + extraData.size());
}

// Test getting samples in chunks
TEST_F(AudioBufferTest, GetSamplesInChunks) {
    std::vector<float> testData(2048, 0.5f);
    buffer_->addFloatData(testData.data(), testData.size());
    
    const size_t chunkSize = 512;
    size_t totalRetrieved = 0;
    
    while (totalRetrieved < testData.size()) {
        auto chunk = buffer_->getSamples(totalRetrieved, chunkSize);
        EXPECT_LE(chunk.size(), chunkSize);
        
        for (float sample : chunk) {
            EXPECT_NEAR(sample, 0.5f, 0.001f);
        }
        
        totalRetrieved += chunk.size();
    }
    
    EXPECT_EQ(totalRetrieved, testData.size());
}

// Test clearing buffer
TEST_F(AudioBufferTest, ClearBuffer) {
    std::vector<float> data(1000, 0.5f);
    buffer_->addFloatData(data.data(), data.size());
    
    EXPECT_EQ(buffer_->size(), 1000);
    EXPECT_FALSE(buffer_->empty());
    
    buffer_->clear();
    
    EXPECT_EQ(buffer_->size(), 0);
    EXPECT_TRUE(buffer_->empty());
}

// Test thread safety
TEST_F(AudioBufferTest, ThreadSafety) {
    const int numWriterThreads = 4;
    const int numReaderThreads = 2;
    const int samplesPerThread = 1000;
    
    std::vector<std::thread> writers;
    std::vector<std::thread> readers;
    std::atomic<bool> stopReading{false};
    std::atomic<int> totalSamplesRead{0};
    
    // Start writer threads
    for (int i = 0; i < numWriterThreads; ++i) {
        writers.emplace_back([this, i, samplesPerThread]() {
            std::vector<float> data(samplesPerThread, static_cast<float>(i) * 0.1f);
            buffer_->addFloatData(data.data(), data.size());
        });
    }
    
    // Start reader threads
    for (int i = 0; i < numReaderThreads; ++i) {
        readers.emplace_back([this, &stopReading, &totalSamplesRead]() {
            while (!stopReading.load()) {
                auto samples = buffer_->getAllSamples();
                totalSamplesRead.fetch_add(samples.size());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    
    // Wait for writers to complete
    for (auto& writer : writers) {
        writer.join();
    }
    
    // Stop readers
    stopReading = true;
    for (auto& reader : readers) {
        reader.join();
    }
    
    // Verify final state
    EXPECT_EQ(buffer_->size(), numWriterThreads * samplesPerThread);
    EXPECT_GT(totalSamplesRead.load(), 0);
}

// Test audio format conversion
TEST_F(AudioBufferTest, FormatConversion) {
    // Test various PCM formats
    std::vector<int16_t> pcm16Data = {32767, -32768, 16384, -16384, 0};
    buffer_->addPCMData(pcm16Data.data(), pcm16Data.size());
    
    auto samples = buffer_->getAllSamples();
    EXPECT_EQ(samples.size(), 5);
    
    // Check proper normalization
    EXPECT_NEAR(samples[0], 1.0f, 0.001f);          // Max positive
    EXPECT_NEAR(samples[1], -1.0f, 0.001f);         // Max negative
    EXPECT_NEAR(samples[2], 0.5f, 0.001f);          // Half positive
    EXPECT_NEAR(samples[3], -0.5f, 0.001f);         // Half negative
    EXPECT_NEAR(samples[4], 0.0f, 0.001f);          // Zero
}

// Test buffer statistics
TEST_F(AudioBufferTest, BufferStatistics) {
    std::vector<float> data = {0.5f, -0.5f, 0.25f, -0.25f, 0.0f, 1.0f, -1.0f};
    buffer_->addFloatData(data.data(), data.size());
    
    auto stats = buffer_->getStatistics();
    
    EXPECT_EQ(stats.sampleCount, 7);
    EXPECT_NEAR(stats.rmsLevel, 0.5f, 0.1f);  // Approximate RMS
    EXPECT_NEAR(stats.peakLevel, 1.0f, 0.001f);
    EXPECT_GT(stats.duration, 0.0);
}

// Test buffer resampling (if implemented)
TEST_F(AudioBufferTest, Resampling) {
    // Create buffer with different sample rate
    auto buffer44k = std::make_unique<AudioBuffer>(44100, 1024);
    
    std::vector<float> data(1000, 0.5f);
    buffer44k->addFloatData(data.data(), data.size());
    
    // Test resampling to 16kHz (if implemented)
    auto resampled = buffer44k->resample(16000);
    if (resampled) {
        EXPECT_LT(resampled->size(), data.size()); // Should be smaller
        EXPECT_EQ(resampled->getSampleRate(), 16000);
    }
}

// Test buffer serialization/deserialization
TEST_F(AudioBufferTest, Serialization) {
    std::vector<float> originalData = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
    buffer_->addFloatData(originalData.data(), originalData.size());
    
    // Serialize to binary format
    auto serialized = buffer_->serialize();
    EXPECT_GT(serialized.size(), 0);
    
    // Deserialize to new buffer
    auto newBuffer = AudioBuffer::deserialize(serialized);
    ASSERT_NE(newBuffer, nullptr);
    
    EXPECT_EQ(newBuffer->size(), buffer_->size());
    EXPECT_EQ(newBuffer->getSampleRate(), buffer_->getSampleRate());
    
    auto originalSamples = buffer_->getAllSamples();
    auto newSamples = newBuffer->getAllSamples();
    
    EXPECT_EQ(originalSamples.size(), newSamples.size());
    for (size_t i = 0; i < originalSamples.size(); ++i) {
        EXPECT_NEAR(originalSamples[i], newSamples[i], 0.001f);
    }
}

// Test memory usage optimization
TEST_F(AudioBufferTest, MemoryOptimization) {
    const size_t largeSize = 100000;
    std::vector<float> largeData(largeSize, 0.5f);
    
    // Measure memory usage before
    size_t memoryBefore = buffer_->getMemoryUsage();
    
    buffer_->addFloatData(largeData.data(), largeData.size());
    
    // Measure memory usage after
    size_t memoryAfter = buffer_->getMemoryUsage();
    
    EXPECT_GT(memoryAfter, memoryBefore);
    EXPECT_GE(memoryAfter, largeSize * sizeof(float));
    
    // Test memory optimization
    buffer_->optimizeMemory();
    size_t memoryOptimized = buffer_->getMemoryUsage();
    
    // Memory usage should be reasonable
    EXPECT_LE(memoryOptimized, memoryAfter * 1.1); // Allow 10% overhead
}

} // namespace audio