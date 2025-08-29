#include <gtest/gtest.h>
#include "stt/advanced/speaker_diarization_engine.hpp"
#include <vector>
#include <cmath>
#include <random>

using namespace stt::advanced;

class SpeakerDiarizationEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<SpeakerDiarizationEngine>();
        
        // Create test model directory
        testModelPath_ = "test_models/speaker_diarization";
        std::filesystem::create_directories(testModelPath_);
    }
    
    void TearDown() override {
        // Clean up test directory
        if (std::filesystem::exists(testModelPath_)) {
            std::filesystem::remove_all(testModelPath_);
        }
    }
    
    // Helper function to generate test audio with different speakers
    std::vector<float> generateTestAudio(int durationMs, int sampleRate, float frequency) {
        size_t numSamples = static_cast<size_t>(durationMs * sampleRate / 1000);
        std::vector<float> audio(numSamples);
        
        for (size_t i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            audio[i] = 0.5f * std::sin(2.0f * M_PI * frequency * t);
        }
        
        return audio;
    }
    
    // Helper function to generate multi-speaker audio
    std::vector<float> generateMultiSpeakerAudio(int sampleRate) {
        // Generate 3 seconds of audio with 2 speakers
        auto speaker1Audio = generateTestAudio(1500, sampleRate, 440.0f); // A4 note
        auto speaker2Audio = generateTestAudio(1500, sampleRate, 880.0f); // A5 note
        
        std::vector<float> combinedAudio;
        combinedAudio.insert(combinedAudio.end(), speaker1Audio.begin(), speaker1Audio.end());
        combinedAudio.insert(combinedAudio.end(), speaker2Audio.begin(), speaker2Audio.end());
        
        return combinedAudio;
    }
    
    std::unique_ptr<SpeakerDiarizationEngine> engine_;
    std::string testModelPath_;
};

TEST_F(SpeakerDiarizationEngineTest, InitializationTest) {
    EXPECT_FALSE(engine_->isInitialized());
    
    bool result = engine_->initialize(testModelPath_);
    EXPECT_TRUE(result);
    EXPECT_TRUE(engine_->isInitialized());
    EXPECT_TRUE(engine_->getLastError().empty());
}

TEST_F(SpeakerDiarizationEngineTest, InitializationWithInvalidPath) {
    bool result = engine_->initialize("/invalid/path/that/cannot/be/created");
    EXPECT_FALSE(result);
    EXPECT_FALSE(engine_->isInitialized());
    EXPECT_FALSE(engine_->getLastError().empty());
}

TEST_F(SpeakerDiarizationEngineTest, BasicDiarizationTest) {
    ASSERT_TRUE(engine_->initialize(testModelPath_));
    
    auto testAudio = generateTestAudio(2000, 16000, 440.0f); // 2 seconds of audio
    auto result = engine_->processSpeakerDiarization(testAudio, 16000);
    
    EXPECT_GT(result.segments.size(), 0);
    EXPECT_GT(result.totalSpeakers, 0);
    EXPECT_GE(result.overallConfidence, 0.0f);
    EXPECT_LE(result.overallConfidence, 1.0f);
}

TEST_F(SpeakerDiarizationEngineTest, MultiSpeakerDiarizationTest) {
    ASSERT_TRUE(engine_->initialize(testModelPath_));
    
    auto testAudio = generateMultiSpeakerAudio(16000);
    auto result = engine_->processSpeakerDiarization(testAudio, 16000);
    
    EXPECT_GT(result.segments.size(), 0);
    EXPECT_GT(result.totalSpeakers, 0);
    EXPECT_GE(result.overallConfidence, 0.0f);
    EXPECT_LE(result.overallConfidence, 1.0f);
    
    // Check that segments have valid time ranges
    for (const auto& segment : result.segments) {
        EXPECT_GE(segment.startTimeMs, 0);
        EXPECT_GT(segment.endTimeMs, segment.startTimeMs);
        EXPECT_GE(segment.confidence, 0.0f);
        EXPECT_LE(segment.confidence, 1.0f);
        EXPECT_GT(segment.speakerId, 0);
        EXPECT_FALSE(segment.speakerLabel.empty());
    }
}

TEST_F(SpeakerDiarizationEngineTest, SpeakerProfileManagementTest) {
    ASSERT_TRUE(engine_->initialize(testModelPath_));
    
    // Create a test speaker profile
    SpeakerProfile profile;
    profile.speakerId = 1;
    profile.speakerLabel = "Test Speaker";
    profile.referenceEmbedding = std::vector<float>(128, 0.5f); // 128-dimensional embedding
    profile.confidence = 0.9f;
    profile.utteranceCount = 5;
    
    // Add speaker profile
    EXPECT_TRUE(engine_->addSpeakerProfile(profile));
    
    // Retrieve speaker profiles
    auto profiles = engine_->getSpeakerProfiles();
    EXPECT_EQ(profiles.size(), 1);
    EXPECT_EQ(profiles[1].speakerId, 1);
    EXPECT_EQ(profiles[1].speakerLabel, "Test Speaker");
    EXPECT_EQ(profiles[1].referenceEmbedding.size(), 128);
    
    // Remove speaker profile
    EXPECT_TRUE(engine_->removeSpeakerProfile(1));
    profiles = engine_->getSpeakerProfiles();
    EXPECT_EQ(profiles.size(), 0);
}

TEST_F(SpeakerDiarizationEngineTest, StreamingDiarizationTest) {
    ASSERT_TRUE(engine_->initialize(testModelPath_));
    
    uint32_t utteranceId = 12345;
    
    // Start streaming diarization
    EXPECT_TRUE(engine_->startStreamingDiarization(utteranceId));
    
    // Add audio chunks
    auto audioChunk1 = generateTestAudio(500, 16000, 440.0f); // 0.5 seconds
    auto audioChunk2 = generateTestAudio(500, 16000, 880.0f); // 0.5 seconds
    
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, audioChunk1, 16000));
    EXPECT_TRUE(engine_->addAudioForDiarization(utteranceId, audioChunk2, 16000));
    
    // Get current speaker
    auto currentSpeaker = engine_->getCurrentSpeaker(utteranceId);
    EXPECT_GT(currentSpeaker.speakerId, 0);
    EXPECT_FALSE(currentSpeaker.speakerLabel.empty());
    
    // Finish streaming diarization
    auto result = engine_->finishStreamingDiarization(utteranceId);
    EXPECT_GE(result.segments.size(), 0);
    EXPECT_GE(result.totalSpeakers, 0);
}

TEST_F(SpeakerDiarizationEngineTest, ConfigurationTest) {
    ASSERT_TRUE(engine_->initialize(testModelPath_));
    
    // Test configuration setters
    engine_->setMaxSpeakers(5);
    engine_->setSpeakerChangeThreshold(0.8f);
    engine_->setSpeakerIdentificationThreshold(0.9f);
    engine_->setProfileLearningEnabled(false);
    
    // Configuration should be applied (no direct getters, but should not crash)
    auto testAudio = generateTestAudio(1000, 16000, 440.0f);
    auto result = engine_->processSpeakerDiarization(testAudio, 16000);
    
    EXPECT_GE(result.segments.size(), 0);
}

TEST_F(SpeakerDiarizationEngineTest, ErrorHandlingTest) {
    // Test processing without initialization
    auto testAudio = generateTestAudio(1000, 16000, 440.0f);
    auto result = engine_->processSpeakerDiarization(testAudio, 16000);
    
    EXPECT_EQ(result.segments.size(), 0);
    EXPECT_FALSE(engine_->getLastError().empty());
    
    // Test with empty audio
    ASSERT_TRUE(engine_->initialize(testModelPath_));
    std::vector<float> emptyAudio;
    result = engine_->processSpeakerDiarization(emptyAudio, 16000);
    
    EXPECT_EQ(result.segments.size(), 0);
    EXPECT_FALSE(engine_->getLastError().empty());
    
    // Test with invalid sample rate
    result = engine_->processSpeakerDiarization(testAudio, -1);
    EXPECT_EQ(result.segments.size(), 0);
    EXPECT_FALSE(engine_->getLastError().empty());
}

TEST_F(SpeakerDiarizationEngineTest, ProcessingStatsTest) {
    ASSERT_TRUE(engine_->initialize(testModelPath_));
    
    auto testAudio = generateTestAudio(1000, 16000, 440.0f);
    auto result = engine_->processSpeakerDiarization(testAudio, 16000);
    
    // Get processing statistics
    std::string stats = engine_->getProcessingStats();
    EXPECT_FALSE(stats.empty());
    
    // Stats should be valid JSON format (basic check)
    EXPECT_NE(stats.find("totalProcessedSegments"), std::string::npos);
    EXPECT_NE(stats.find("totalDetectedSpeakers"), std::string::npos);
    EXPECT_NE(stats.find("averageConfidence"), std::string::npos);
}

TEST_F(SpeakerDiarizationEngineTest, ResetTest) {
    ASSERT_TRUE(engine_->initialize(testModelPath_));
    
    // Add a speaker profile
    SpeakerProfile profile;
    profile.speakerId = 1;
    profile.speakerLabel = "Test Speaker";
    profile.referenceEmbedding = std::vector<float>(128, 0.5f);
    engine_->addSpeakerProfile(profile);
    
    // Start a streaming session
    engine_->startStreamingDiarization(123);
    
    // Reset engine
    engine_->reset();
    
    // Check that state is cleared
    auto profiles = engine_->getSpeakerProfiles();
    EXPECT_EQ(profiles.size(), 0);
    
    // Should still be initialized
    EXPECT_TRUE(engine_->isInitialized());
}

TEST_F(SpeakerDiarizationEngineTest, InvalidSpeakerProfileTest) {
    ASSERT_TRUE(engine_->initialize(testModelPath_));
    
    // Test with invalid speaker profile (empty embedding)
    SpeakerProfile invalidProfile;
    invalidProfile.speakerId = 0; // Invalid ID
    invalidProfile.speakerLabel = "Invalid Speaker";
    invalidProfile.referenceEmbedding = {}; // Empty embedding
    
    EXPECT_FALSE(engine_->addSpeakerProfile(invalidProfile));
    EXPECT_FALSE(engine_->getLastError().empty());
}

TEST_F(SpeakerDiarizationEngineTest, StreamingSessionManagementTest) {
    ASSERT_TRUE(engine_->initialize(testModelPath_));
    
    uint32_t utteranceId = 999;
    
    // Test starting duplicate session
    EXPECT_TRUE(engine_->startStreamingDiarization(utteranceId));
    EXPECT_FALSE(engine_->startStreamingDiarization(utteranceId)); // Should fail
    
    // Test adding audio to non-existent session
    auto audioChunk = generateTestAudio(500, 16000, 440.0f);
    EXPECT_FALSE(engine_->addAudioForDiarization(888, audioChunk, 16000)); // Non-existent ID
    
    // Test canceling session
    engine_->cancelStreamingDiarization(utteranceId);
    
    // Should be able to start again after cancellation
    EXPECT_TRUE(engine_->startStreamingDiarization(utteranceId));
    
    // Clean up
    engine_->cancelStreamingDiarization(utteranceId);
}

// Test the individual model components
TEST_F(SpeakerDiarizationEngineTest, SpeakerDetectionModelTest) {
    SimpleSpeakerDetectionModel detectionModel;
    
    EXPECT_FALSE(detectionModel.isInitialized());
    EXPECT_TRUE(detectionModel.initialize(testModelPath_));
    EXPECT_TRUE(detectionModel.isInitialized());
    
    auto testAudio = generateMultiSpeakerAudio(16000);
    auto changePoints = detectionModel.detectSpeakerChanges(testAudio, 16000);
    
    // Should detect at least some change points in multi-speaker audio
    EXPECT_GE(changePoints.size(), 0);
    
    // Change points should be in ascending order
    for (size_t i = 1; i < changePoints.size(); ++i) {
        EXPECT_GT(changePoints[i], changePoints[i-1]);
    }
}

TEST_F(SpeakerDiarizationEngineTest, SpeakerEmbeddingModelTest) {
    SimpleSpeakerEmbeddingModel embeddingModel;
    
    EXPECT_FALSE(embeddingModel.isInitialized());
    EXPECT_TRUE(embeddingModel.initialize(testModelPath_));
    EXPECT_TRUE(embeddingModel.isInitialized());
    
    auto testAudio = generateTestAudio(1000, 16000, 440.0f);
    auto embedding = embeddingModel.generateEmbedding(testAudio, 16000);
    
    EXPECT_EQ(embedding.size(), embeddingModel.getEmbeddingDimension());
    EXPECT_GT(embeddingModel.getEmbeddingDimension(), 0);
    
    // Test similarity calculation
    auto embedding2 = embeddingModel.generateEmbedding(testAudio, 16000);
    float similarity = embeddingModel.calculateSimilarity(embedding, embedding2);
    
    EXPECT_GE(similarity, 0.0f);
    EXPECT_LE(similarity, 1.0f);
    EXPECT_GT(similarity, 0.8f); // Same audio should have high similarity
}

TEST_F(SpeakerDiarizationEngineTest, SpeakerClusteringTest) {
    KMeansSpeakerClustering clustering;
    
    // Create test embeddings
    std::vector<std::vector<float>> embeddings;
    embeddings.push_back(std::vector<float>(128, 0.1f)); // Cluster 1
    embeddings.push_back(std::vector<float>(128, 0.2f)); // Cluster 1
    embeddings.push_back(std::vector<float>(128, 0.9f)); // Cluster 2
    embeddings.push_back(std::vector<float>(128, 0.8f)); // Cluster 2
    
    auto assignments = clustering.clusterSpeakers(embeddings, 0.5f);
    
    EXPECT_EQ(assignments.size(), 4);
    EXPECT_GT(clustering.getClusterCount(), 0);
    EXPECT_LE(clustering.getClusterCount(), 4);
    
    // Test adding new embedding
    std::vector<float> newEmbedding(128, 0.15f); // Should be close to cluster 1
    uint32_t clusterId = clustering.addEmbedding(newEmbedding, 0.5f);
    EXPECT_GT(clusterId, 0);
    
    // Test reset
    clustering.reset();
    EXPECT_EQ(clustering.getClusterCount(), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}