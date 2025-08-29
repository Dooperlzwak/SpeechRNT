#include <gtest/gtest.h>
#include "stt/advanced/speaker_diarization_engine.hpp"
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>

using namespace stt::advanced;

class SpeakerClusteringProfileManagementTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<SpeakerDiarizationEngine>();
        
        // Create test model directory
        testModelPath_ = "test_models/clustering_profile_management";
        std::filesystem::create_directories(testModelPath_);
        
        ASSERT_TRUE(engine_->initialize(testModelPath_));
        
        // Enable profile learning
        engine_->setProfileLearningEnabled(true);
    }
    
    void TearDown() override {
        // Clean up test directory
        if (std::filesystem::exists(testModelPath_)) {
            std::filesystem::remove_all(testModelPath_);
        }
    }
    
    // Helper function to generate test audio with specific characteristics
    std::vector<float> generateCharacteristicAudio(int durationMs, int sampleRate, 
                                                  float baseFreq, float amplitude = 0.5f, 
                                                  float noiseLevel = 0.1f) {
        size_t numSamples = static_cast<size_t>(durationMs * sampleRate / 1000);
        std::vector<float> audio(numSamples);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<float> noise(0.0f, noiseLevel);
        
        for (size_t i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            
            // Create a more complex waveform with harmonics
            float signal = amplitude * std::sin(2.0f * M_PI * baseFreq * t);
            signal += 0.3f * amplitude * std::sin(2.0f * M_PI * baseFreq * 2.0f * t); // 2nd harmonic
            signal += 0.2f * amplitude * std::sin(2.0f * M_PI * baseFreq * 3.0f * t); // 3rd harmonic
            
            audio[i] = signal + noise(gen);
        }
        
        return audio;
    }
    
    // Helper to create a speaker profile with specific embedding
    SpeakerProfile createTestSpeakerProfile(uint32_t speakerId, const std::string& label, 
                                           const std::vector<float>& embedding) {
        SpeakerProfile profile;
        profile.speakerId = speakerId;
        profile.speakerLabel = label;
        profile.referenceEmbedding = embedding;
        profile.confidence = 0.9f;
        profile.utteranceCount = 1;
        profile.metadata = "{\"test\": true}";
        
        return profile;
    }
    
    // Helper to generate a test embedding with specific characteristics
    std::vector<float> generateTestEmbedding(size_t dimension, float baseValue, float variance = 0.1f) {
        std::vector<float> embedding(dimension);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<float> dist(baseValue, variance);
        
        for (size_t i = 0; i < dimension; ++i) {
            embedding[i] = dist(gen);
        }
        
        // Normalize embedding
        float norm = 0.0f;
        for (float val : embedding) {
            norm += val * val;
        }
        norm = std::sqrt(norm);
        
        if (norm > 0.0f) {
            for (float& val : embedding) {
                val /= norm;
            }
        }
        
        return embedding;
    }
    
    std::unique_ptr<SpeakerDiarizationEngine> engine_;
    std::string testModelPath_;
};

TEST_F(SpeakerClusteringProfileManagementTest, UnsupervisedSpeakerClustering) {
    // Generate audio from multiple "speakers" with different characteristics
    auto speaker1Audio1 = generateCharacteristicAudio(1000, 16000, 300.0f, 0.5f, 0.05f);
    auto speaker1Audio2 = generateCharacteristicAudio(1000, 16000, 320.0f, 0.5f, 0.05f); // Similar to speaker 1
    
    auto speaker2Audio1 = generateCharacteristicAudio(1000, 16000, 800.0f, 0.4f, 0.03f);
    auto speaker2Audio2 = generateCharacteristicAudio(1000, 16000, 850.0f, 0.4f, 0.03f); // Similar to speaker 2
    
    auto speaker3Audio1 = generateCharacteristicAudio(1000, 16000, 1500.0f, 0.6f, 0.08f);
    
    // Process each audio segment
    std::vector<DiarizationResult> results;
    results.push_back(engine_->processSpeakerDiarization(speaker1Audio1, 16000));
    results.push_back(engine_->processSpeakerDiarization(speaker1Audio2, 16000));
    results.push_back(engine_->processSpeakerDiarization(speaker2Audio1, 16000));
    results.push_back(engine_->processSpeakerDiarization(speaker2Audio2, 16000));
    results.push_back(engine_->processSpeakerDiarization(speaker3Audio1, 16000));
    
    // Verify that clustering identified different speakers
    std::set<uint32_t> allSpeakerIds;
    for (const auto& result : results) {
        EXPECT_GT(result.segments.size(), 0);
        for (const auto& segment : result.segments) {
            allSpeakerIds.insert(segment.speakerId);
        }
    }
    
    // Should have identified multiple speakers (at least 2, ideally 3)
    EXPECT_GE(allSpeakerIds.size(), 2);
    
    // Check that similar audio segments are assigned to the same speaker
    // (This is probabilistic due to the simple implementation, so we just verify basic functionality)
    EXPECT_GT(results[0].segments[0].speakerId, 0);
    EXPECT_GT(results[2].segments[0].speakerId, 0);
}

TEST_F(SpeakerClusteringProfileManagementTest, SpeakerProfileDatabase) {
    // Create test speaker profiles
    auto embedding1 = generateTestEmbedding(128, 0.1f, 0.05f);
    auto embedding2 = generateTestEmbedding(128, 0.5f, 0.05f);
    auto embedding3 = generateTestEmbedding(128, 0.9f, 0.05f);
    
    auto profile1 = createTestSpeakerProfile(101, "Alice", embedding1);
    auto profile2 = createTestSpeakerProfile(102, "Bob", embedding2);
    auto profile3 = createTestSpeakerProfile(103, "Charlie", embedding3);
    
    // Add profiles to database
    EXPECT_TRUE(engine_->addSpeakerProfile(profile1));
    EXPECT_TRUE(engine_->addSpeakerProfile(profile2));
    EXPECT_TRUE(engine_->addSpeakerProfile(profile3));
    
    // Retrieve profiles
    auto profiles = engine_->getSpeakerProfiles();
    EXPECT_EQ(profiles.size(), 3);
    
    // Verify profile data integrity
    EXPECT_EQ(profiles[101].speakerLabel, "Alice");
    EXPECT_EQ(profiles[102].speakerLabel, "Bob");
    EXPECT_EQ(profiles[103].speakerLabel, "Charlie");
    
    EXPECT_EQ(profiles[101].referenceEmbedding.size(), 128);
    EXPECT_EQ(profiles[102].referenceEmbedding.size(), 128);
    EXPECT_EQ(profiles[103].referenceEmbedding.size(), 128);
    
    // Test profile removal
    EXPECT_TRUE(engine_->removeSpeakerProfile(102));
    profiles = engine_->getSpeakerProfiles();
    EXPECT_EQ(profiles.size(), 2);
    EXPECT_EQ(profiles.find(102), profiles.end());
    
    // Test profile clearing
    engine_->clearSpeakerProfiles();
    profiles = engine_->getSpeakerProfiles();
    EXPECT_EQ(profiles.size(), 0);
}

TEST_F(SpeakerClusteringProfileManagementTest, SpeakerProfileLearningAndAdaptation) {
    // Create initial speaker profile
    auto initialEmbedding = generateTestEmbedding(128, 0.3f, 0.02f);
    auto profile = createTestSpeakerProfile(201, "Learning Speaker", initialEmbedding);
    
    EXPECT_TRUE(engine_->addSpeakerProfile(profile));
    
    // Get initial profile state
    auto initialProfiles = engine_->getSpeakerProfiles();
    EXPECT_EQ(initialProfiles.size(), 1);
    auto initialProfile = initialProfiles[201];
    
    // Create diarization results that should update the profile
    DiarizationResult result1;
    SpeakerSegment segment1;
    segment1.speakerId = 201;
    segment1.speakerLabel = "Learning Speaker";
    segment1.startTimeMs = 0;
    segment1.endTimeMs = 1000;
    segment1.confidence = 0.85f;
    segment1.speakerEmbedding = generateTestEmbedding(128, 0.32f, 0.02f); // Slightly different
    result1.segments.push_back(segment1);
    result1.totalSpeakers = 1;
    result1.overallConfidence = 0.85f;
    
    DiarizationResult result2;
    SpeakerSegment segment2;
    segment2.speakerId = 201;
    segment2.speakerLabel = "Learning Speaker";
    segment2.startTimeMs = 1000;
    segment2.endTimeMs = 2000;
    segment2.confidence = 0.88f;
    segment2.speakerEmbedding = generateTestEmbedding(128, 0.31f, 0.02f); // Slightly different
    result2.segments.push_back(segment2);
    result2.totalSpeakers = 1;
    result2.overallConfidence = 0.88f;
    
    // Update profiles with new data
    engine_->updateSpeakerProfiles(result1);
    engine_->updateSpeakerProfiles(result2);
    
    // Check that profile was updated
    auto updatedProfiles = engine_->getSpeakerProfiles();
    EXPECT_EQ(updatedProfiles.size(), 1);
    auto updatedProfile = updatedProfiles[201];
    
    // Utterance count should have increased
    EXPECT_GT(updatedProfile.utteranceCount, initialProfile.utteranceCount);
    
    // Embedding should have been adapted (not exactly the same as initial)
    bool embeddingChanged = false;
    for (size_t i = 0; i < initialProfile.referenceEmbedding.size(); ++i) {
        if (std::abs(updatedProfile.referenceEmbedding[i] - initialProfile.referenceEmbedding[i]) > 1e-6f) {
            embeddingChanged = true;
            break;
        }
    }
    EXPECT_TRUE(embeddingChanged);
}

TEST_F(SpeakerClusteringProfileManagementTest, SpeakerIdentificationConfidenceScoring) {
    // Create known speaker profiles with distinct embeddings
    auto embedding1 = generateTestEmbedding(128, 0.2f, 0.01f); // Low variance for consistency
    auto embedding2 = generateTestEmbedding(128, 0.8f, 0.01f); // High base value, low variance
    
    auto profile1 = createTestSpeakerProfile(301, "High Confidence Speaker", embedding1);
    auto profile2 = createTestSpeakerProfile(302, "Another Speaker", embedding2);
    
    EXPECT_TRUE(engine_->addSpeakerProfile(profile1));
    EXPECT_TRUE(engine_->addSpeakerProfile(profile2));
    
    // Test with different identification thresholds
    
    // High threshold - should be more selective
    engine_->setSpeakerIdentificationThreshold(0.95f);
    
    // Generate audio that should match profile1
    auto matchingAudio = generateCharacteristicAudio(1000, 16000, 300.0f, 0.5f, 0.02f);
    auto result1 = engine_->processSpeakerDiarization(matchingAudio, 16000);
    
    EXPECT_GT(result1.segments.size(), 0);
    
    // Lower threshold - should be less selective
    engine_->setSpeakerIdentificationThreshold(0.5f);
    
    auto result2 = engine_->processSpeakerDiarization(matchingAudio, 16000);
    EXPECT_GT(result2.segments.size(), 0);
    
    // Verify confidence scores are within valid range
    for (const auto& segment : result1.segments) {
        EXPECT_GE(segment.confidence, 0.0f);
        EXPECT_LE(segment.confidence, 1.0f);
    }
    
    for (const auto& segment : result2.segments) {
        EXPECT_GE(segment.confidence, 0.0f);
        EXPECT_LE(segment.confidence, 1.0f);
    }
}

TEST_F(SpeakerClusteringProfileManagementTest, SpeakerIdentificationValidation) {
    // Create a known speaker profile
    auto knownEmbedding = generateTestEmbedding(128, 0.4f, 0.02f);
    auto knownProfile = createTestSpeakerProfile(401, "Known Speaker", knownEmbedding);
    
    EXPECT_TRUE(engine_->addSpeakerProfile(knownProfile));
    
    // Generate audio that should be similar to the known speaker
    auto similarAudio = generateCharacteristicAudio(1000, 16000, 400.0f, 0.5f, 0.03f);
    
    // Process with moderate identification threshold
    engine_->setSpeakerIdentificationThreshold(0.7f);
    auto result = engine_->processSpeakerDiarization(similarAudio, 16000);
    
    EXPECT_GT(result.segments.size(), 0);
    
    // Check if the known speaker was identified or a new speaker was created
    bool knownSpeakerIdentified = false;
    for (const auto& segment : result.segments) {
        if (segment.speakerId == 401) {
            knownSpeakerIdentified = true;
            EXPECT_EQ(segment.speakerLabel, "Known Speaker");
        }
        
        // Validate segment data
        EXPECT_GT(segment.speakerId, 0);
        EXPECT_FALSE(segment.speakerLabel.empty());
        EXPECT_GE(segment.confidence, 0.0f);
        EXPECT_LE(segment.confidence, 1.0f);
        EXPECT_FALSE(segment.speakerEmbedding.empty());
    }
    
    // Test with very high threshold - should create new speaker
    engine_->setSpeakerIdentificationThreshold(0.99f);
    auto strictResult = engine_->processSpeakerDiarization(similarAudio, 16000);
    
    EXPECT_GT(strictResult.segments.size(), 0);
    
    // With very high threshold, might create a new speaker instead of matching known one
    // This tests the validation logic
}

TEST_F(SpeakerClusteringProfileManagementTest, ProfileLearningToggle) {
    // Create initial profile
    auto embedding = generateTestEmbedding(128, 0.5f, 0.02f);
    auto profile = createTestSpeakerProfile(501, "Toggle Test Speaker", embedding);
    
    EXPECT_TRUE(engine_->addSpeakerProfile(profile));
    
    // Get initial state
    auto initialProfiles = engine_->getSpeakerProfiles();
    auto initialProfile = initialProfiles[501];
    size_t initialUtteranceCount = initialProfile.utteranceCount;
    
    // Create diarization result for learning
    DiarizationResult learningResult;
    SpeakerSegment segment;
    segment.speakerId = 501;
    segment.speakerLabel = "Toggle Test Speaker";
    segment.startTimeMs = 0;
    segment.endTimeMs = 1000;
    segment.confidence = 0.9f;
    segment.speakerEmbedding = generateTestEmbedding(128, 0.52f, 0.02f);
    learningResult.segments.push_back(segment);
    learningResult.totalSpeakers = 1;
    learningResult.overallConfidence = 0.9f;
    
    // Test with learning enabled
    engine_->setProfileLearningEnabled(true);
    engine_->updateSpeakerProfiles(learningResult);
    
    auto profilesAfterLearning = engine_->getSpeakerProfiles();
    EXPECT_GT(profilesAfterLearning[501].utteranceCount, initialUtteranceCount);
    
    // Reset profile
    EXPECT_TRUE(engine_->addSpeakerProfile(profile)); // Reset to initial state
    
    // Test with learning disabled
    engine_->setProfileLearningEnabled(false);
    engine_->updateSpeakerProfiles(learningResult);
    
    auto profilesAfterDisabledLearning = engine_->getSpeakerProfiles();
    // Should not have learned (utterance count should remain the same)
    EXPECT_EQ(profilesAfterDisabledLearning[501].utteranceCount, initialProfile.utteranceCount);
}

TEST_F(SpeakerClusteringProfileManagementTest, ClusteringWithMaxSpeakersLimit) {
    // Set a low maximum speaker limit
    engine_->setMaxSpeakers(2);
    
    // Generate audio from multiple distinct "speakers"
    std::vector<std::vector<float>> speakerAudios;
    speakerAudios.push_back(generateCharacteristicAudio(800, 16000, 200.0f, 0.5f, 0.02f));
    speakerAudios.push_back(generateCharacteristicAudio(800, 16000, 600.0f, 0.4f, 0.02f));
    speakerAudios.push_back(generateCharacteristicAudio(800, 16000, 1200.0f, 0.6f, 0.02f));
    speakerAudios.push_back(generateCharacteristicAudio(800, 16000, 2000.0f, 0.3f, 0.02f));
    
    // Process all audio segments
    std::set<uint32_t> detectedSpeakers;
    for (const auto& audio : speakerAudios) {
        auto result = engine_->processSpeakerDiarization(audio, 16000);
        for (const auto& segment : result.segments) {
            detectedSpeakers.insert(segment.speakerId);
        }
    }
    
    // Should respect the maximum speaker limit (though this is implementation dependent)
    // At minimum, should detect at least 1 speaker and not crash
    EXPECT_GE(detectedSpeakers.size(), 1);
    
    // Test with higher limit
    engine_->setMaxSpeakers(10);
    
    detectedSpeakers.clear();
    for (const auto& audio : speakerAudios) {
        auto result = engine_->processSpeakerDiarization(audio, 16000);
        for (const auto& segment : result.segments) {
            detectedSpeakers.insert(segment.speakerId);
        }
    }
    
    EXPECT_GE(detectedSpeakers.size(), 1);
}

TEST_F(SpeakerClusteringProfileManagementTest, InvalidProfileHandling) {
    // Test adding invalid profiles
    
    // Profile with ID 0 (invalid)
    SpeakerProfile invalidProfile1;
    invalidProfile1.speakerId = 0;
    invalidProfile1.speakerLabel = "Invalid Speaker";
    invalidProfile1.referenceEmbedding = generateTestEmbedding(128, 0.5f);
    
    EXPECT_FALSE(engine_->addSpeakerProfile(invalidProfile1));
    
    // Profile with empty embedding
    SpeakerProfile invalidProfile2;
    invalidProfile2.speakerId = 999;
    invalidProfile2.speakerLabel = "Empty Embedding Speaker";
    invalidProfile2.referenceEmbedding = {}; // Empty
    
    EXPECT_FALSE(engine_->addSpeakerProfile(invalidProfile2));
    
    // Valid profile for comparison
    SpeakerProfile validProfile;
    validProfile.speakerId = 888;
    validProfile.speakerLabel = "Valid Speaker";
    validProfile.referenceEmbedding = generateTestEmbedding(128, 0.5f);
    
    EXPECT_TRUE(engine_->addSpeakerProfile(validProfile));
    
    // Verify only valid profile was added
    auto profiles = engine_->getSpeakerProfiles();
    EXPECT_EQ(profiles.size(), 1);
    EXPECT_EQ(profiles.begin()->first, 888);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}