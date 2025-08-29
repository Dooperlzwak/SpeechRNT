#include <gtest/gtest.h>
#include "audio/audio_buffer_manager.hpp"
#include <vector>
#include <thread>
#include <chrono>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace audio;

class AudioBufferManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Configure for testing
        config_.maxBufferSizeMB = 1;  // 1MB for testing
        config_.maxUtterances = 5;
        config_.cleanupIntervalMs = 100;
        config_.maxIdleTimeMs = 1000;
        config_.enableCircularBuffer = true;
        
        bufferManager_ = std::make_unique<AudioBufferManager>(config_);
    }
    
    void TearDown() override {
        bufferManager_.reset();
    }
    
    std::vector<float> generateTestAudio(size_t sampleCount, float frequency = 440.0f) {
        std::vector<float> audio;
        audio.reserve(sampleCount);
        
        for (size_t i = 0; i < sampleCount; ++i) {
            float sample = std::sin(2.0f * M_PI * frequency * i / 16000.0f);
            audio.push_back(sample);
        }
        
        return audio;
    }
    
    AudioBufferManager::BufferConfig config_;
    std::unique_ptr<AudioBufferManager> bufferManager_;
};

TEST_F(AudioBufferManagerTest, BasicUtteranceCreation) {
    uint32_t utteranceId = 1;
    
    // Test utterance creation
    EXPECT_TRUE(bufferManager_->createUtterance(utteranceId));
    EXPECT_TRUE(bufferManager_->hasUtterance(utteranceId));
    EXPECT_TRUE(bufferManager_->isUtteranceActive(utteranceId));
    
    // Test duplicate creation
    EXPECT_TRUE(bufferManager_->createUtterance(utteranceId));
    EXPECT_EQ(bufferManager_->getUtteranceCount(), 1);
}

TEST_F(AudioBufferManagerTest, AudioDataAddition) {
    uint32_t utteranceId = 1;
    auto testAudio = generateTestAudio(1000);
    
    // Should auto-create utterance
    EXPECT_TRUE(bufferManager_->addAudioData(utteranceId, testAudio));
    EXPECT_TRUE(bufferManager_->hasUtterance(utteranceId));
    
    // Verify audio retrieval
    auto retrievedAudio = bufferManager_->getBufferedAudio(utteranceId);
    EXPECT_EQ(retrievedAudio.size(), testAudio.size());
    
    // Add more audio
    auto moreAudio = generateTestAudio(500);
    EXPECT_TRUE(bufferManager_->addAudioData(utteranceId, moreAudio));
    
    retrievedAudio = bufferManager_->getBufferedAudio(utteranceId);
    EXPECT_EQ(retrievedAudio.size(), testAudio.size() + moreAudio.size());
}

TEST_F(AudioBufferManagerTest, RecentAudioRetrieval) {
    uint32_t utteranceId = 1;
    auto testAudio = generateTestAudio(1000);
    
    EXPECT_TRUE(bufferManager_->addAudioData(utteranceId, testAudio));
    
    // Get recent 100 samples
    auto recentAudio = bufferManager_->getRecentAudio(utteranceId, 100);
    EXPECT_EQ(recentAudio.size(), 100);
    
    // Verify it's the last 100 samples
    auto allAudio = bufferManager_->getBufferedAudio(utteranceId);
    for (size_t i = 0; i < 100; ++i) {
        EXPECT_FLOAT_EQ(recentAudio[i], allAudio[allAudio.size() - 100 + i]);
    }
}

TEST_F(AudioBufferManagerTest, UtteranceLifecycle) {
    uint32_t utteranceId = 1;
    auto testAudio = generateTestAudio(1000);
    
    // Create and add audio
    EXPECT_TRUE(bufferManager_->createUtterance(utteranceId));
    EXPECT_TRUE(bufferManager_->addAudioData(utteranceId, testAudio));
    EXPECT_TRUE(bufferManager_->isUtteranceActive(utteranceId));
    
    // Finalize utterance
    bufferManager_->finalizeBuffer(utteranceId);
    EXPECT_FALSE(bufferManager_->isUtteranceActive(utteranceId));
    EXPECT_TRUE(bufferManager_->hasUtterance(utteranceId));
    
    // Should reject new audio for finalized utterance
    EXPECT_FALSE(bufferManager_->addAudioData(utteranceId, testAudio));
    
    // Remove utterance
    bufferManager_->removeUtterance(utteranceId);
    EXPECT_FALSE(bufferManager_->hasUtterance(utteranceId));
    EXPECT_EQ(bufferManager_->getUtteranceCount(), 0);
}

TEST_F(AudioBufferManagerTest, MaxUtteranceLimit) {
    // Create utterances up to the limit
    for (uint32_t i = 1; i <= config_.maxUtterances; ++i) {
        EXPECT_TRUE(bufferManager_->createUtterance(i));
    }
    EXPECT_EQ(bufferManager_->getUtteranceCount(), config_.maxUtterances);
    
    // Creating one more should trigger cleanup
    uint32_t newId = config_.maxUtterances + 1;
    EXPECT_TRUE(bufferManager_->createUtterance(newId));
    EXPECT_TRUE(bufferManager_->hasUtterance(newId));
    
    // Should still be at or below the limit
    EXPECT_LE(bufferManager_->getUtteranceCount(), config_.maxUtterances);
}

TEST_F(AudioBufferManagerTest, CircularBufferBehavior) {
    uint32_t utteranceId = 1;
    
    // Create utterance with small buffer (1KB = ~256 samples)
    EXPECT_TRUE(bufferManager_->createUtterance(utteranceId, 1.0 / 1024)); // 1KB
    
    // Add audio that exceeds buffer size
    auto largeAudio = generateTestAudio(1000); // Should exceed 1KB
    EXPECT_TRUE(bufferManager_->addAudioData(utteranceId, largeAudio));
    
    // Buffer should contain data (circular behavior)
    auto retrievedAudio = bufferManager_->getBufferedAudio(utteranceId);
    EXPECT_GT(retrievedAudio.size(), 0);
}

TEST_F(AudioBufferManagerTest, ThreadSafety) {
    const int numThreads = 4;
    const int operationsPerThread = 100;
    std::vector<std::thread> threads;
    
    // Launch threads that perform concurrent operations
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([this, t, operationsPerThread]() {
            for (int i = 0; i < operationsPerThread; ++i) {
                uint32_t utteranceId = t * 1000 + i;
                auto testAudio = generateTestAudio(100);
                
                // Concurrent operations
                bufferManager_->addAudioData(utteranceId, testAudio);
                bufferManager_->getBufferedAudio(utteranceId);
                bufferManager_->finalizeBuffer(utteranceId);
                
                if (i % 10 == 0) {
                    bufferManager_->cleanupInactiveBuffers();
                }
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify system is still functional
    EXPECT_TRUE(bufferManager_->isHealthy());
}

TEST_F(AudioBufferManagerTest, MemoryManagement) {
    uint32_t utteranceId = 1;
    
    // Get initial memory usage
    size_t initialMemory = bufferManager_->getCurrentMemoryUsageMB();
    
    // Add significant amount of audio
    for (int i = 0; i < 10; ++i) {
        auto testAudio = generateTestAudio(10000); // ~40KB per iteration
        bufferManager_->addAudioData(utteranceId, testAudio);
    }
    
    size_t afterAddingMemory = bufferManager_->getCurrentMemoryUsageMB();
    EXPECT_GT(afterAddingMemory, initialMemory);
    
    // Remove utterance
    bufferManager_->removeUtterance(utteranceId);
    
    size_t afterRemovalMemory = bufferManager_->getCurrentMemoryUsageMB();
    EXPECT_LE(afterRemovalMemory, afterAddingMemory);
}

TEST_F(AudioBufferManagerTest, Statistics) {
    uint32_t utteranceId1 = 1;
    uint32_t utteranceId2 = 2;
    
    auto testAudio = generateTestAudio(1000);
    
    // Add audio to multiple utterances
    bufferManager_->addAudioData(utteranceId1, testAudio);
    bufferManager_->addAudioData(utteranceId2, testAudio);
    
    auto stats = bufferManager_->getStatistics();
    EXPECT_EQ(stats.activeUtterances, 2);
    EXPECT_EQ(stats.totalAudioSamples, 2000);
    EXPECT_GT(stats.totalMemoryUsageMB, 0);
    
    // Finalize one utterance
    bufferManager_->finalizeBuffer(utteranceId1);
    
    stats = bufferManager_->getStatistics();
    EXPECT_EQ(stats.activeUtterances, 1);
    EXPECT_EQ(stats.totalAudioSamples, 2000); // Still contains data
}

TEST_F(AudioBufferManagerTest, HealthChecking) {
    // Initially should be healthy
    EXPECT_TRUE(bufferManager_->isHealthy());
    
    std::string healthStatus = bufferManager_->getHealthStatus();
    EXPECT_FALSE(healthStatus.empty());
    EXPECT_NE(healthStatus.find("HEALTHY"), std::string::npos);
}

TEST_F(AudioBufferManagerTest, CleanupOperations) {
    uint32_t utteranceId1 = 1;
    uint32_t utteranceId2 = 2;
    
    auto testAudio = generateTestAudio(1000);
    
    // Add audio and finalize one utterance
    bufferManager_->addAudioData(utteranceId1, testAudio);
    bufferManager_->addAudioData(utteranceId2, testAudio);
    bufferManager_->finalizeBuffer(utteranceId1);
    
    EXPECT_EQ(bufferManager_->getUtteranceCount(), 2);
    
    // Cleanup inactive buffers
    bufferManager_->cleanupInactiveBuffers();
    EXPECT_EQ(bufferManager_->getUtteranceCount(), 1);
    EXPECT_TRUE(bufferManager_->hasUtterance(utteranceId2));
    EXPECT_FALSE(bufferManager_->hasUtterance(utteranceId1));
    
    // Force cleanup
    bufferManager_->forceCleanup();
    EXPECT_EQ(bufferManager_->getUtteranceCount(), 0);
}

TEST_F(AudioBufferManagerTest, EmptyAudioHandling) {
    uint32_t utteranceId = 1;
    std::vector<float> emptyAudio;
    
    // Should handle empty audio gracefully
    EXPECT_TRUE(bufferManager_->addAudioData(utteranceId, emptyAudio));
    
    auto retrievedAudio = bufferManager_->getBufferedAudio(utteranceId);
    EXPECT_TRUE(retrievedAudio.empty());
    
    auto recentAudio = bufferManager_->getRecentAudio(utteranceId, 100);
    EXPECT_TRUE(recentAudio.empty());
}

TEST_F(AudioBufferManagerTest, NonExistentUtteranceHandling) {
    uint32_t nonExistentId = 999;
    
    // Should handle non-existent utterances gracefully
    EXPECT_FALSE(bufferManager_->hasUtterance(nonExistentId));
    EXPECT_FALSE(bufferManager_->isUtteranceActive(nonExistentId));
    
    auto audio = bufferManager_->getBufferedAudio(nonExistentId);
    EXPECT_TRUE(audio.empty());
    
    auto recentAudio = bufferManager_->getRecentAudio(nonExistentId, 100);
    EXPECT_TRUE(recentAudio.empty());
    
    // These operations should not crash
    bufferManager_->finalizeBuffer(nonExistentId);
    bufferManager_->removeUtterance(nonExistentId);
    bufferManager_->setUtteranceActive(nonExistentId, false);
}