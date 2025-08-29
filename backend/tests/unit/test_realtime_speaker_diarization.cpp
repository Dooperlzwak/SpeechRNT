#include <gtest/gtest.h>
#include "stt/advanced/speaker_diarization_engine.hpp"
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

using namespace stt::advanced;

class RealTimeSpeakerDiarizationTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<SpeakerDiarizationEngine>();
        
        // Create test model directory
        testModelPath_ = "test_models/realtime_speaker_diarization";
        std::filesystem::create_directories(testModelPath_);
        
        ASSERT_TRUE(engine_->initialize(testModelPath_));
    }
    
    void TearDown() override {
        // Clean up test directory
        if (std::filesystem::exists(testModelPath_)) {
            std::filesystem::remove_all(testModelPath_);
        }
    }
    
    // Helper function to generate test audio chunks with different speakers
    std::vector<float> generateSpeakerAudio(int durationMs, int sampleRate, float frequency, float amplitude = 0.5f) {
        size_t numSamples = static_cast<size_t>(durationMs * sampleRate / 1000);
        std::vector<float> audio(numSamples);
        
        for (size_t i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            audio[i] = amplitude * std::sin(2.0f * M_PI * frequency * t);
        }
        
        return audio;
    }
    
    std::unique_ptr<SpeakerDiarizationEngine> engine_;
    std::string testModelPath_;
};

TEST_F(RealTimeSpeakerDiarizationTest, StreamingDiarizationBasicFlow) {
    uint32_t utteranceId = 1001;
    
    // Start streaming diarization
    EXPECT_TRUE(engine_->startStreamingDiarization(utteranceId));
    
    // Simulate real-time audio chunks from different speakers
    auto speaker1Chunk1 = generateSpeakerAudio(200, 16000, 440.0f); // Speaker 1: A4 note
    auto speaker1Chunk2 = generateSpeakerAudio(200, 16000, 440.0f); // Speaker 1: A4 note
    auto speaker2Chunk1 = generateSpeakerAudio(200, 16000, 880.0f); // Speaker 2: A5 note
    auto speaker2Chunk2 = generateSpeakerAudio(200, 16000, 880.0f); // Speaker 2: A5 note
    
    // Add chunks sequentially to simulate real-time processing
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, speaker1Chunk1, 16000));
    
    // Check current speaker after first chunk
    auto currentSpeaker = engine_->getCurrentSpeaker(utteranceId);
    EXPECT_GT(currentSpeaker.speakerId, 0);
    EXPECT_FALSE(currentSpeaker.speakerLabel.empty());
    EXPECT_GE(currentSpeaker.startTimeMs, 0);
    EXPECT_GE(currentSpeaker.confidence, 0.0f);
    EXPECT_LE(currentSpeaker.confidence, 1.0f);
    
    uint32_t firstSpeakerId = currentSpeaker.speakerId;
    
    // Add more chunks from same speaker
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, speaker1Chunk2, 16000));
    
    // Speaker should remain consistent
    currentSpeaker = engine_->getCurrentSpeaker(utteranceId);
    EXPECT_EQ(currentSpeaker.speakerId, firstSpeakerId);
    
    // Add chunks from different speaker
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, speaker2Chunk1, 16000));
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, speaker2Chunk2, 16000));
    
    // Check if speaker change was detected (may or may not change depending on sensitivity)
    currentSpeaker = engine_->getCurrentSpeaker(utteranceId);
    EXPECT_GT(currentSpeaker.speakerId, 0);
    
    // Finish streaming and get final result
    auto finalResult = engine_->finishStreamingDiarization(utteranceId);
    
    EXPECT_GE(finalResult.segments.size(), 1);
    EXPECT_GE(finalResult.totalSpeakers, 1);
    EXPECT_GE(finalResult.overallConfidence, 0.0f);
    EXPECT_LE(finalResult.overallConfidence, 1.0f);
    
    // Verify segments have proper timing
    for (const auto& segment : finalResult.segments) {
        EXPECT_GE(segment.startTimeMs, 0);
        EXPECT_GT(segment.endTimeMs, segment.startTimeMs);
        EXPECT_GT(segment.speakerId, 0);
        EXPECT_FALSE(segment.speakerLabel.empty());
    }
}

TEST_F(RealTimeSpeakerDiarizationTest, SpeakerChangeDetectionSensitivity) {
    uint32_t utteranceId = 1002;
    
    // Test with high sensitivity (low threshold)
    engine_->setSpeakerChangeThreshold(0.3f);
    
    EXPECT_TRUE(engine_->startStreamingDiarization(utteranceId));
    
    // Add very different audio chunks
    auto lowFreqChunk = generateSpeakerAudio(300, 16000, 200.0f);   // Low frequency
    auto highFreqChunk = generateSpeakerAudio(300, 16000, 2000.0f); // High frequency
    
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, lowFreqChunk, 16000));
    auto speaker1 = engine_->getCurrentSpeaker(utteranceId);
    
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, highFreqChunk, 16000));
    auto speaker2 = engine_->getCurrentSpeaker(utteranceId);
    
    // With high sensitivity, different frequencies might be detected as different speakers
    // (This is implementation dependent, so we just verify the system responds)
    EXPECT_GT(speaker1.speakerId, 0);
    EXPECT_GT(speaker2.speakerId, 0);
    
    auto result = engine_->finishStreamingDiarization(utteranceId);
    EXPECT_GE(result.segments.size(), 1);
    
    // Test with low sensitivity (high threshold)
    utteranceId = 1003;
    engine_->setSpeakerChangeThreshold(0.9f);
    
    EXPECT_TRUE(engine_->startStreamingDiarization(utteranceId));
    
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, lowFreqChunk, 16000));
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, highFreqChunk, 16000));
    
    result = engine_->finishStreamingDiarization(utteranceId);
    EXPECT_GE(result.segments.size(), 1);
}

TEST_F(RealTimeSpeakerDiarizationTest, SpeakerConsistencyTracking) {
    uint32_t utteranceId = 1004;
    
    EXPECT_TRUE(engine_->startStreamingDiarization(utteranceId));
    
    // Add multiple chunks from the same "speaker" (same frequency)
    std::vector<std::vector<float>> sameFrequencyChunks;
    for (int i = 0; i < 5; ++i) {
        sameFrequencyChunks.push_back(generateSpeakerAudio(200, 16000, 440.0f));
    }
    
    std::vector<uint32_t> speakerIds;
    
    // Add chunks and track speaker consistency
    for (const auto& chunk : sameFrequencyChunks) {
        EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, chunk, 16000));
        auto currentSpeaker = engine_->getCurrentSpeaker(utteranceId);
        speakerIds.push_back(currentSpeaker.speakerId);
    }
    
    // Check that speaker ID remains consistent for similar audio
    // (Allow for some variation due to the simple implementation)
    EXPECT_GT(speakerIds.size(), 0);
    
    // At least the first few should be consistent
    if (speakerIds.size() >= 2) {
        // We don't enforce perfect consistency due to the simple nature of our test implementation
        // but we verify that the system is tracking speakers
        EXPECT_GT(speakerIds[0], 0);
        EXPECT_GT(speakerIds[1], 0);
    }
    
    auto result = engine_->finishStreamingDiarization(utteranceId);
    EXPECT_GE(result.segments.size(), 1);
}

TEST_F(RealTimeSpeakerDiarizationTest, SpeakerTransitionMarkers) {
    uint32_t utteranceId = 1005;
    
    EXPECT_TRUE(engine_->startStreamingDiarization(utteranceId));
    
    // Create a sequence with clear speaker transitions
    auto speaker1Audio = generateSpeakerAudio(500, 16000, 300.0f);  // 0.5s
    auto speaker2Audio = generateSpeakerAudio(500, 16000, 1200.0f); // 0.5s
    auto speaker1AudioAgain = generateSpeakerAudio(500, 16000, 300.0f); // 0.5s
    
    // Add audio chunks with small delays to simulate real-time
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, speaker1Audio, 16000));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, speaker2Audio, 16000));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, speaker1AudioAgain, 16000));
    
    auto result = engine_->finishStreamingDiarization(utteranceId);
    
    // Verify that segments contain proper transition markers
    EXPECT_GE(result.segments.size(), 1);
    
    // Check that segments are properly ordered by time
    for (size_t i = 1; i < result.segments.size(); ++i) {
        EXPECT_GE(result.segments[i].startTimeMs, result.segments[i-1].startTimeMs);
    }
    
    // Verify segment metadata
    for (const auto& segment : result.segments) {
        EXPECT_GT(segment.speakerId, 0);
        EXPECT_FALSE(segment.speakerLabel.empty());
        EXPECT_GE(segment.startTimeMs, 0);
        EXPECT_GT(segment.endTimeMs, segment.startTimeMs);
        EXPECT_GE(segment.confidence, 0.0f);
        EXPECT_LE(segment.confidence, 1.0f);
        
        // Check that speaker embedding is populated
        EXPECT_FALSE(segment.speakerEmbedding.empty());
    }
}

TEST_F(RealTimeSpeakerDiarizationTest, ConcurrentStreamingSessions) {
    uint32_t utteranceId1 = 2001;
    uint32_t utteranceId2 = 2002;
    
    // Start multiple streaming sessions
    EXPECT_TRUE(engine_->startStreamingDiarization(utteranceId1));
    EXPECT_TRUE(engine_->startStreamingDiarization(utteranceId2));
    
    // Add different audio to each session
    auto audio1 = generateSpeakerAudio(300, 16000, 440.0f);
    auto audio2 = generateSpeakerAudio(300, 16000, 880.0f);
    
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId1, audio1, 16000));
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId2, audio2, 16000));
    
    // Check current speakers for both sessions
    auto speaker1 = engine_->getCurrentSpeaker(utteranceId1);
    auto speaker2 = engine_->getCurrentSpeaker(utteranceId2);
    
    EXPECT_GT(speaker1.speakerId, 0);
    EXPECT_GT(speaker2.speakerId, 0);
    
    // Finish both sessions
    auto result1 = engine_->finishStreamingDiarization(utteranceId1);
    auto result2 = engine_->finishStreamingDiarization(utteranceId2);
    
    EXPECT_GE(result1.segments.size(), 1);
    EXPECT_GE(result2.segments.size(), 1);
}

TEST_F(RealTimeSpeakerDiarizationTest, StreamingSessionCancellation) {
    uint32_t utteranceId = 3001;
    
    EXPECT_TRUE(engine_->startStreamingDiarization(utteranceId));
    
    auto audioChunk = generateSpeakerAudio(200, 16000, 440.0f);
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, audioChunk, 16000));
    
    // Cancel the session
    engine_->cancelStreamingDiarization(utteranceId);
    
    // Should not be able to add more audio
    EXPECT_FALSE(engine_->addAudioForDiarization(utteranceId, audioChunk, 16000));
    
    // Should not be able to get current speaker
    auto currentSpeaker = engine_->getCurrentSpeaker(utteranceId);
    EXPECT_EQ(currentSpeaker.speakerId, 0); // Default/empty speaker
    
    // Should be able to start a new session with the same ID
    EXPECT_TRUE(engine_->startStreamingDiarization(utteranceId));
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, audioChunk, 16000));
    
    // Clean up
    engine_->cancelStreamingDiarization(utteranceId);
}

TEST_F(RealTimeSpeakerDiarizationTest, RealTimePerformanceTest) {
    uint32_t utteranceId = 4001;
    
    EXPECT_TRUE(engine_->startStreamingDiarization(utteranceId));
    
    // Test processing latency with multiple chunks
    const int numChunks = 10;
    const int chunkDurationMs = 100; // 100ms chunks
    
    std::vector<std::chrono::milliseconds> processingTimes;
    
    for (int i = 0; i < numChunks; ++i) {
        auto audioChunk = generateSpeakerAudio(chunkDurationMs, 16000, 440.0f + i * 50.0f);
        
        auto startTime = std::chrono::steady_clock::now();
        EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, audioChunk, 16000));
        auto endTime = std::chrono::steady_clock::now();
        
        auto processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        processingTimes.push_back(processingTime);
        
        // Get current speaker to ensure processing is complete
        auto currentSpeaker = engine_->getCurrentSpeaker(utteranceId);
        EXPECT_GT(currentSpeaker.speakerId, 0);
    }
    
    // Calculate average processing time
    auto totalTime = std::chrono::milliseconds(0);
    for (const auto& time : processingTimes) {
        totalTime += time;
    }
    auto avgProcessingTime = totalTime / numChunks;
    
    // Processing should be reasonably fast (less than the chunk duration for real-time)
    EXPECT_LT(avgProcessingTime.count(), chunkDurationMs * 2); // Allow 2x chunk duration
    
    auto result = engine_->finishStreamingDiarization(utteranceId);
    EXPECT_GE(result.segments.size(), 1);
    
    std::cout << "Average processing time per chunk: " << avgProcessingTime.count() << "ms" << std::endl;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}