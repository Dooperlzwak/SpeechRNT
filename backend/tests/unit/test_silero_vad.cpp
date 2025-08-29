#include <gtest/gtest.h>
#include "audio/silero_vad_impl.hpp"
#include "audio/voice_activity_detector.hpp"
#include <vector>
#include <cmath>

using namespace audio;

class SileroVadTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test audio data
        generateTestAudio();
    }
    
    void generateTestAudio() {
        const size_t sampleRate = 16000;
        const float duration = 1.0f; // 1 second
        const size_t numSamples = static_cast<size_t>(sampleRate * duration);
        
        // Generate silence
        silenceAudio_.resize(numSamples, 0.0f);
        
        // Generate speech-like audio (sine wave with noise)
        speechAudio_.resize(numSamples);
        for (size_t i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            // Mix of frequencies typical for speech
            float signal = 0.3f * std::sin(2.0f * M_PI * 200.0f * t) +  // Fundamental
                          0.2f * std::sin(2.0f * M_PI * 400.0f * t) +   // Harmonic
                          0.1f * std::sin(2.0f * M_PI * 800.0f * t);    // Higher harmonic
            
            // Add some noise
            float noise = 0.05f * (static_cast<float>(rand()) / RAND_MAX - 0.5f);
            speechAudio_[i] = signal + noise;
        }
        
        // Generate noise
        noiseAudio_.resize(numSamples);
        for (size_t i = 0; i < numSamples; ++i) {
            noiseAudio_[i] = 0.02f * (static_cast<float>(rand()) / RAND_MAX - 0.5f);
        }
    }
    
    std::vector<float> silenceAudio_;
    std::vector<float> speechAudio_;
    std::vector<float> noiseAudio_;
};

TEST_F(SileroVadTest, InitializationTest) {
    SileroVadImpl vad;
    
    // Test initialization
    EXPECT_TRUE(vad.initialize(16000));
    EXPECT_TRUE(vad.isInitialized());
    
    // Test shutdown
    vad.shutdown();
    EXPECT_FALSE(vad.isInitialized());
}

TEST_F(SileroVadTest, VadModeTest) {
    SileroVadImpl vad;
    EXPECT_TRUE(vad.initialize(16000));
    
    // Test mode switching
    vad.setVadMode(SileroVadImpl::VadMode::ENERGY_BASED);
    EXPECT_EQ(vad.getCurrentMode(), SileroVadImpl::VadMode::ENERGY_BASED);
    
    vad.setVadMode(SileroVadImpl::VadMode::HYBRID);
    EXPECT_EQ(vad.getCurrentMode(), SileroVadImpl::VadMode::HYBRID);
    
    // Test silero mode (may fallback to hybrid if model not loaded)
    vad.setVadMode(SileroVadImpl::VadMode::SILERO);
    // Mode should be either SILERO or HYBRID depending on model availability
    auto mode = vad.getCurrentMode();
    EXPECT_TRUE(mode == SileroVadImpl::VadMode::SILERO || 
                mode == SileroVadImpl::VadMode::HYBRID);
}

TEST_F(SileroVadTest, EnergyBasedVadTest) {
    SileroVadImpl vad;
    EXPECT_TRUE(vad.initialize(16000));
    
    // Force energy-based mode
    vad.setVadMode(SileroVadImpl::VadMode::ENERGY_BASED);
    
    // Test with silence - should return low probability
    float silenceProb = vad.processSamples(silenceAudio_);
    EXPECT_GE(silenceProb, 0.0f);
    EXPECT_LE(silenceProb, 1.0f);
    EXPECT_LT(silenceProb, 0.3f); // Should be low for silence
    
    // Test with speech - should return higher probability
    float speechProb = vad.processSamples(speechAudio_);
    EXPECT_GE(speechProb, 0.0f);
    EXPECT_LE(speechProb, 1.0f);
    EXPECT_GT(speechProb, silenceProb); // Speech should have higher probability
    
    // Test with noise - should be between silence and speech
    float noiseProb = vad.processSamples(noiseAudio_);
    EXPECT_GE(noiseProb, 0.0f);
    EXPECT_LE(noiseProb, 1.0f);
}

TEST_F(SileroVadTest, HybridModeTest) {
    SileroVadImpl vad;
    EXPECT_TRUE(vad.initialize(16000));
    
    // Test hybrid mode (should work regardless of silero model availability)
    vad.setVadMode(SileroVadImpl::VadMode::HYBRID);
    
    float silenceProb = vad.processSamples(silenceAudio_);
    float speechProb = vad.processSamples(speechAudio_);
    
    EXPECT_GE(silenceProb, 0.0f);
    EXPECT_LE(silenceProb, 1.0f);
    EXPECT_GE(speechProb, 0.0f);
    EXPECT_LE(speechProb, 1.0f);
    EXPECT_GT(speechProb, silenceProb);
}

TEST_F(SileroVadTest, StatisticsTest) {
    SileroVadImpl vad;
    EXPECT_TRUE(vad.initialize(16000));
    
    // Process some audio
    vad.processSamples(silenceAudio_);
    vad.processSamples(speechAudio_);
    vad.processSamples(noiseAudio_);
    
    auto stats = vad.getStatistics();
    EXPECT_EQ(stats.totalProcessedChunks, 3);
    EXPECT_GE(stats.averageProcessingTimeMs, 0.0);
    EXPECT_GE(stats.averageConfidence, 0.0);
    EXPECT_LE(stats.averageConfidence, 1.0);
    
    // Test statistics reset
    vad.resetStatistics();
    auto resetStats = vad.getStatistics();
    EXPECT_EQ(resetStats.totalProcessedChunks, 0);
}

TEST_F(SileroVadTest, EnergyBasedVADClassTest) {
    EnergyBasedVAD::Config config;
    config.energyThreshold = 0.01f;
    config.useAdaptiveThreshold = true;
    config.useSpectralFeatures = true;
    
    EnergyBasedVAD energyVad(config);
    
    // Test with different audio types
    float silenceProb = energyVad.detectVoiceActivity(silenceAudio_);
    float speechProb = energyVad.detectVoiceActivity(speechAudio_);
    float noiseProb = energyVad.detectVoiceActivity(noiseAudio_);
    
    EXPECT_GE(silenceProb, 0.0f);
    EXPECT_LE(silenceProb, 1.0f);
    EXPECT_GE(speechProb, 0.0f);
    EXPECT_LE(speechProb, 1.0f);
    EXPECT_GE(noiseProb, 0.0f);
    EXPECT_LE(noiseProb, 1.0f);
    
    // Speech should have higher probability than silence
    EXPECT_GT(speechProb, silenceProb);
    
    // Test reset
    energyVad.reset();
    
    // Should still work after reset
    float postResetProb = energyVad.detectVoiceActivity(speechAudio_);
    EXPECT_GE(postResetProb, 0.0f);
    EXPECT_LE(postResetProb, 1.0f);
}

TEST_F(SileroVadTest, VoiceActivityDetectorIntegrationTest) {
    VadConfig config;
    config.speechThreshold = 0.5f;
    config.silenceThreshold = 0.3f;
    config.sampleRate = 16000;
    
    VoiceActivityDetector detector(config);
    EXPECT_TRUE(detector.initialize());
    
    // Test enhanced VAD methods
    int currentMode = detector.getCurrentVadMode();
    EXPECT_GE(currentMode, 0);
    EXPECT_LE(currentMode, 2);
    
    // Test mode switching
    detector.setVadMode(1); // Energy-based
    EXPECT_EQ(detector.getCurrentVadMode(), 1);
    
    detector.setVadMode(2); // Hybrid
    EXPECT_EQ(detector.getCurrentVadMode(), 2);
    
    // Test audio processing
    detector.processAudio(speechAudio_);
    detector.processAudio(silenceAudio_);
    
    auto stats = detector.getStatistics();
    EXPECT_GT(stats.totalAudioProcessed, 0);
}

// Test with empty audio
TEST_F(SileroVadTest, EmptyAudioTest) {
    SileroVadImpl vad;
    EXPECT_TRUE(vad.initialize(16000));
    
    std::vector<float> emptyAudio;
    float prob = vad.processSamples(emptyAudio);
    EXPECT_EQ(prob, 0.0f);
}

// Test with very short audio
TEST_F(SileroVadTest, ShortAudioTest) {
    SileroVadImpl vad;
    EXPECT_TRUE(vad.initialize(16000));
    
    std::vector<float> shortAudio = {0.1f, -0.1f, 0.2f};
    float prob = vad.processSamples(shortAudio);
    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);
}