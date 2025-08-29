#include <gtest/gtest.h>
#include "audio/voice_activity_detector.hpp"
#include <thread>
#include <chrono>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace audio;

class VoiceActivityDetectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = VadConfig{};
        config_.speechThreshold = 0.5f;
        config_.silenceThreshold = 0.3f;
        config_.minSpeechDurationMs = 100;
        config_.minSilenceDurationMs = 500;
        config_.windowSizeMs = 64;
        config_.sampleRate = 16000;
        
        vad_ = std::make_unique<VoiceActivityDetector>(config_);
        
        // Setup callbacks for testing
        vadEvents_.clear();
        utterances_.clear();
        
        vad_->setVadCallback([this](const VadEvent& event) {
            vadEvents_.push_back(event);
        });
        
        vad_->setUtteranceCallback([this](uint32_t id, const std::vector<float>& audio) {
            utterances_.emplace_back(id, audio);
        });
    }
    
    void TearDown() override {
        if (vad_) {
            vad_->shutdown();
        }
    }
    
    // Helper methods
    std::vector<float> generateSilence(size_t samples) {
        return std::vector<float>(samples, 0.0f);
    }
    
    std::vector<float> generateSpeech(size_t samples, float amplitude = 0.1f) {
        std::vector<float> speech(samples);
        for (size_t i = 0; i < samples; ++i) {
            speech[i] = amplitude * std::sin(2.0f * M_PI * 440.0f * i / config_.sampleRate);
        }
        return speech;
    }
    
    std::vector<float> generateNoise(size_t samples, float amplitude = 0.02f) {
        std::vector<float> noise(samples);
        for (size_t i = 0; i < samples; ++i) {
            noise[i] = amplitude * (2.0f * (rand() / float(RAND_MAX)) - 1.0f);
        }
        return noise;
    }
    
    void waitForStateTransition(VadState expectedState, int timeoutMs = 1000) {
        auto start = std::chrono::steady_clock::now();
        while (vad_->getCurrentState() != expectedState) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            if (elapsed.count() > timeoutMs) {
                break;
            }
        }
    }
    
    VadConfig config_;
    std::unique_ptr<VoiceActivityDetector> vad_;
    std::vector<VadEvent> vadEvents_;
    std::vector<std::pair<uint32_t, std::vector<float>>> utterances_;
};

// Basic functionality tests
TEST_F(VoiceActivityDetectorTest, InitializationAndShutdown) {
    EXPECT_FALSE(vad_->isInitialized());
    EXPECT_TRUE(vad_->initialize());
    EXPECT_TRUE(vad_->isInitialized());
    EXPECT_EQ(vad_->getCurrentState(), VadState::IDLE);
    
    vad_->shutdown();
    EXPECT_FALSE(vad_->isInitialized());
}

TEST_F(VoiceActivityDetectorTest, ConfigurationValidation) {
    VadConfig invalidConfig;
    invalidConfig.speechThreshold = 1.5f; // Invalid: > 1.0
    
    EXPECT_THROW(VoiceActivityDetector invalidVad(invalidConfig), std::invalid_argument);
    
    // Test valid configuration
    VadConfig validConfig;
    validConfig.speechThreshold = 0.6f;
    validConfig.silenceThreshold = 0.4f;
    
    EXPECT_NO_THROW(VoiceActivityDetector validVad(validConfig));
}

TEST_F(VoiceActivityDetectorTest, ConfigurationUpdate) {
    EXPECT_TRUE(vad_->initialize());
    
    VadConfig newConfig = config_;
    newConfig.speechThreshold = 0.7f;
    newConfig.minSpeechDurationMs = 200;
    
    EXPECT_NO_THROW(vad_->setConfig(newConfig));
    EXPECT_EQ(vad_->getConfig().speechThreshold, 0.7f);
    EXPECT_EQ(vad_->getConfig().minSpeechDurationMs, 200);
}

// State machine tests
TEST_F(VoiceActivityDetectorTest, IdleToSpeechDetection) {
    EXPECT_TRUE(vad_->initialize());
    EXPECT_EQ(vad_->getCurrentState(), VadState::IDLE);
    
    // Process speech audio
    auto speechAudio = generateSpeech(1024);
    vad_->processAudio(speechAudio);
    
    // Should transition to SPEECH_DETECTED
    EXPECT_EQ(vad_->getCurrentState(), VadState::SPEECH_DETECTED);
    EXPECT_FALSE(vadEvents_.empty());
    EXPECT_EQ(vadEvents_.back().currentState, VadState::SPEECH_DETECTED);
}

TEST_F(VoiceActivityDetectorTest, SpeechDetectionToSpeaking) {
    EXPECT_TRUE(vad_->initialize());
    
    // Generate speech audio for longer than minSpeechDurationMs
    size_t samplesPerMs = config_.sampleRate / 1000;
    size_t totalSamples = samplesPerMs * (config_.minSpeechDurationMs + 50);
    
    auto speechAudio = generateSpeech(totalSamples);
    vad_->processAudio(speechAudio);
    
    // Wait for state transition
    waitForStateTransition(VadState::SPEAKING);
    
    EXPECT_EQ(vad_->getCurrentState(), VadState::SPEAKING);
    EXPECT_GT(vad_->getCurrentUtteranceId(), 0);
}

TEST_F(VoiceActivityDetectorTest, SpeakingToPauseDetection) {
    EXPECT_TRUE(vad_->initialize());
    
    // First, get to SPEAKING state
    size_t samplesPerMs = config_.sampleRate / 1000;
    size_t speechSamples = samplesPerMs * (config_.minSpeechDurationMs + 50);
    auto speechAudio = generateSpeech(speechSamples);
    vad_->processAudio(speechAudio);
    waitForStateTransition(VadState::SPEAKING);
    
    // Now process silence
    auto silenceAudio = generateSilence(samplesPerMs * 100);
    vad_->processAudio(silenceAudio);
    
    EXPECT_EQ(vad_->getCurrentState(), VadState::PAUSE_DETECTED);
}

TEST_F(VoiceActivityDetectorTest, PauseDetectionToIdle) {
    EXPECT_TRUE(vad_->initialize());
    
    // Get to PAUSE_DETECTED state
    size_t samplesPerMs = config_.sampleRate / 1000;
    size_t speechSamples = samplesPerMs * (config_.minSpeechDurationMs + 50);
    auto speechAudio = generateSpeech(speechSamples);
    vad_->processAudio(speechAudio);
    waitForStateTransition(VadState::SPEAKING);
    
    auto silenceAudio = generateSilence(samplesPerMs * 100);
    vad_->processAudio(silenceAudio);
    EXPECT_EQ(vad_->getCurrentState(), VadState::PAUSE_DETECTED);
    
    // Process silence for longer than minSilenceDurationMs
    size_t longSilenceSamples = samplesPerMs * (config_.minSilenceDurationMs + 100);
    auto longSilence = generateSilence(longSilenceSamples);
    vad_->processAudio(longSilence);
    
    waitForStateTransition(VadState::IDLE);
    EXPECT_EQ(vad_->getCurrentState(), VadState::IDLE);
}

// Utterance management tests
TEST_F(VoiceActivityDetectorTest, UtteranceCreationAndFinalization) {
    EXPECT_TRUE(vad_->initialize());
    
    // Generate a complete utterance: speech -> pause -> silence
    size_t samplesPerMs = config_.sampleRate / 1000;
    
    // Speech phase
    size_t speechSamples = samplesPerMs * (config_.minSpeechDurationMs + 100);
    auto speechAudio = generateSpeech(speechSamples);
    vad_->processAudio(speechAudio);
    waitForStateTransition(VadState::SPEAKING);
    
    uint32_t utteranceId = vad_->getCurrentUtteranceId();
    EXPECT_GT(utteranceId, 0);
    
    // Silence phase to end utterance
    size_t silenceSamples = samplesPerMs * (config_.minSilenceDurationMs + 100);
    auto silenceAudio = generateSilence(silenceSamples);
    vad_->processAudio(silenceAudio);
    
    waitForStateTransition(VadState::IDLE);
    
    // Check that utterance was finalized
    EXPECT_FALSE(utterances_.empty());
    EXPECT_EQ(utterances_.back().first, utteranceId);
    EXPECT_FALSE(utterances_.back().second.empty());
}

TEST_F(VoiceActivityDetectorTest, MultipleUtterances) {
    EXPECT_TRUE(vad_->initialize());
    
    size_t samplesPerMs = config_.sampleRate / 1000;
    
    // First utterance
    auto speechAudio1 = generateSpeech(samplesPerMs * (config_.minSpeechDurationMs + 50));
    vad_->processAudio(speechAudio1);
    waitForStateTransition(VadState::SPEAKING);
    
    uint32_t firstUtteranceId = vad_->getCurrentUtteranceId();
    
    // End first utterance
    auto silenceAudio = generateSilence(samplesPerMs * (config_.minSilenceDurationMs + 50));
    vad_->processAudio(silenceAudio);
    waitForStateTransition(VadState::IDLE);
    
    // Second utterance
    auto speechAudio2 = generateSpeech(samplesPerMs * (config_.minSpeechDurationMs + 50));
    vad_->processAudio(speechAudio2);
    waitForStateTransition(VadState::SPEAKING);
    
    uint32_t secondUtteranceId = vad_->getCurrentUtteranceId();
    EXPECT_GT(secondUtteranceId, firstUtteranceId);
    
    // End second utterance
    vad_->processAudio(silenceAudio);
    waitForStateTransition(VadState::IDLE);
    
    // Should have two utterances
    EXPECT_EQ(utterances_.size(), 2);
    EXPECT_EQ(utterances_[0].first, firstUtteranceId);
    EXPECT_EQ(utterances_[1].first, secondUtteranceId);
}

TEST_F(VoiceActivityDetectorTest, ForceUtteranceEnd) {
    EXPECT_TRUE(vad_->initialize());
    
    // Start speaking
    size_t samplesPerMs = config_.sampleRate / 1000;
    auto speechAudio = generateSpeech(samplesPerMs * (config_.minSpeechDurationMs + 50));
    vad_->processAudio(speechAudio);
    waitForStateTransition(VadState::SPEAKING);
    
    uint32_t utteranceId = vad_->getCurrentUtteranceId();
    EXPECT_GT(utteranceId, 0);
    
    // Force end utterance
    vad_->forceUtteranceEnd();
    EXPECT_EQ(vad_->getCurrentState(), VadState::IDLE);
    
    // Check that utterance was finalized
    EXPECT_FALSE(utterances_.empty());
    EXPECT_EQ(utterances_.back().first, utteranceId);
}

// Audio processing tests
TEST_F(VoiceActivityDetectorTest, AudioBuffering) {
    EXPECT_TRUE(vad_->initialize());
    
    // Start speaking
    size_t samplesPerMs = config_.sampleRate / 1000;
    auto speechAudio = generateSpeech(samplesPerMs * (config_.minSpeechDurationMs + 50));
    vad_->processAudio(speechAudio);
    waitForStateTransition(VadState::SPEAKING);
    
    // Check that audio is being buffered
    auto currentAudio = vad_->getCurrentUtteranceAudio();
    EXPECT_FALSE(currentAudio.empty());
    
    // Add more audio
    auto moreAudio = generateSpeech(samplesPerMs * 50);
    vad_->processAudio(moreAudio);
    
    auto updatedAudio = vad_->getCurrentUtteranceAudio();
    EXPECT_GT(updatedAudio.size(), currentAudio.size());
}

TEST_F(VoiceActivityDetectorTest, NoiseRejection) {
    EXPECT_TRUE(vad_->initialize());
    
    // Process noise (should not trigger speech detection)
    auto noiseAudio = generateNoise(1024, 0.01f); // Low amplitude noise
    vad_->processAudio(noiseAudio);
    
    EXPECT_EQ(vad_->getCurrentState(), VadState::IDLE);
    EXPECT_TRUE(vadEvents_.empty());
}

// Statistics tests
TEST_F(VoiceActivityDetectorTest, StatisticsTracking) {
    EXPECT_TRUE(vad_->initialize());
    
    // Process some audio
    auto speechAudio = generateSpeech(1024);
    vad_->processAudio(speechAudio);
    
    auto stats = vad_->getStatistics();
    EXPECT_GT(stats.totalAudioProcessed, 0);
    EXPECT_GT(stats.averageConfidence, 0.0);
    
    // Reset statistics
    vad_->resetStatistics();
    stats = vad_->getStatistics();
    EXPECT_EQ(stats.totalAudioProcessed, 0);
}

// Error handling tests
TEST_F(VoiceActivityDetectorTest, ProcessingWithoutInitialization) {
    // Try to process audio without initialization
    auto speechAudio = generateSpeech(1024);
    vad_->processAudio(speechAudio);
    
    EXPECT_EQ(vad_->getLastError(), VoiceActivityDetector::ErrorCode::NOT_INITIALIZED);
}

TEST_F(VoiceActivityDetectorTest, EmptyAudioProcessing) {
    EXPECT_TRUE(vad_->initialize());
    
    // Process empty audio (should not cause errors)
    std::vector<float> emptyAudio;
    vad_->processAudio(emptyAudio);
    
    EXPECT_EQ(vad_->getLastError(), VoiceActivityDetector::ErrorCode::NONE);
    EXPECT_EQ(vad_->getCurrentState(), VadState::IDLE);
}

// Reset functionality tests
TEST_F(VoiceActivityDetectorTest, ResetFunctionality) {
    EXPECT_TRUE(vad_->initialize());
    
    // Get to SPEAKING state
    size_t samplesPerMs = config_.sampleRate / 1000;
    auto speechAudio = generateSpeech(samplesPerMs * (config_.minSpeechDurationMs + 50));
    vad_->processAudio(speechAudio);
    waitForStateTransition(VadState::SPEAKING);
    
    EXPECT_EQ(vad_->getCurrentState(), VadState::SPEAKING);
    EXPECT_GT(vad_->getCurrentUtteranceId(), 0);
    
    // Reset
    vad_->reset();
    
    EXPECT_EQ(vad_->getCurrentState(), VadState::IDLE);
    EXPECT_TRUE(vad_->getCurrentUtteranceAudio().empty());
}

// Timing tests
TEST_F(VoiceActivityDetectorTest, MinimumSpeechDuration) {
    EXPECT_TRUE(vad_->initialize());
    
    // Process speech for less than minimum duration
    size_t samplesPerMs = config_.sampleRate / 1000;
    size_t shortSpeechSamples = samplesPerMs * (config_.minSpeechDurationMs / 2);
    auto shortSpeech = generateSpeech(shortSpeechSamples);
    vad_->processAudio(shortSpeech);
    
    // Should be in SPEECH_DETECTED, not SPEAKING
    EXPECT_EQ(vad_->getCurrentState(), VadState::SPEECH_DETECTED);
    
    // Now add silence - should go back to IDLE without creating utterance
    auto silenceAudio = generateSilence(samplesPerMs * 100);
    vad_->processAudio(silenceAudio);
    
    EXPECT_EQ(vad_->getCurrentState(), VadState::IDLE);
    EXPECT_TRUE(utterances_.empty()); // No utterance should be created
}

TEST_F(VoiceActivityDetectorTest, MinimumSilenceDuration) {
    EXPECT_TRUE(vad_->initialize());
    
    // Get to SPEAKING state
    size_t samplesPerMs = config_.sampleRate / 1000;
    auto speechAudio = generateSpeech(samplesPerMs * (config_.minSpeechDurationMs + 50));
    vad_->processAudio(speechAudio);
    waitForStateTransition(VadState::SPEAKING);
    
    // Process short silence (less than minimum)
    size_t shortSilenceSamples = samplesPerMs * (config_.minSilenceDurationMs / 2);
    auto shortSilence = generateSilence(shortSilenceSamples);
    vad_->processAudio(shortSilence);
    
    // Should be in PAUSE_DETECTED, not IDLE
    EXPECT_EQ(vad_->getCurrentState(), VadState::PAUSE_DETECTED);
    
    // Continue with speech - should go back to SPEAKING
    vad_->processAudio(speechAudio);
    EXPECT_EQ(vad_->getCurrentState(), VadState::SPEAKING);
}

// Performance and edge case tests
TEST_F(VoiceActivityDetectorTest, LargeAudioChunks) {
    EXPECT_TRUE(vad_->initialize());
    
    // Process very large audio chunk
    size_t largeSamples = config_.sampleRate * 5; // 5 seconds
    auto largeAudio = generateSpeech(largeSamples);
    
    EXPECT_NO_THROW(vad_->processAudio(largeAudio));
    EXPECT_EQ(vad_->getLastError(), VoiceActivityDetector::ErrorCode::NONE);
}

TEST_F(VoiceActivityDetectorTest, MaxUtteranceDuration) {
    // Set short max utterance duration for testing
    config_.maxUtteranceDurationMs = 1000; // 1 second
    vad_ = std::make_unique<VoiceActivityDetector>(config_);
    
    vad_->setUtteranceCallback([this](uint32_t id, const std::vector<float>& audio) {
        utterances_.emplace_back(id, audio);
    });
    
    EXPECT_TRUE(vad_->initialize());
    
    // Generate very long speech (longer than max duration)
    size_t samplesPerMs = config_.sampleRate / 1000;
    size_t longSpeechSamples = samplesPerMs * (config_.maxUtteranceDurationMs + 500);
    auto longSpeech = generateSpeech(longSpeechSamples);
    
    vad_->processAudio(longSpeech);
    
    // Should force utterance end due to max duration
    waitForStateTransition(VadState::IDLE, 2000);
    EXPECT_FALSE(utterances_.empty());
}