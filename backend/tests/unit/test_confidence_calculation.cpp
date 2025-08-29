#include <gtest/gtest.h>
#include "stt/whisper_stt.hpp"
#include "stt/stt_interface.hpp"
#include <vector>
#include <memory>
#include <chrono>
#include <thread>

class ConfidenceCalculationTest : public ::testing::Test {
protected:
    void SetUp() override {
        whisperSTT = std::make_unique<stt::WhisperSTT>();
        
        // Initialize with a test model path (will use simulation mode if whisper.cpp not available)
        std::string testModelPath = "test_models/whisper-base.bin";
        whisperSTT->initialize(testModelPath, 4);
    }
    
    void TearDown() override {
        whisperSTT.reset();
    }
    
    std::unique_ptr<stt::WhisperSTT> whisperSTT;
};

TEST_F(ConfidenceCalculationTest, ConfidenceThresholdConfiguration) {
    // Test confidence threshold setting
    whisperSTT->setConfidenceThreshold(0.8f);
    EXPECT_EQ(whisperSTT->getConfidenceThreshold(), 0.8f);
    
    // Test word-level confidence configuration
    whisperSTT->setWordLevelConfidenceEnabled(true);
    EXPECT_TRUE(whisperSTT->isWordLevelConfidenceEnabled());
    
    whisperSTT->setWordLevelConfidenceEnabled(false);
    EXPECT_FALSE(whisperSTT->isWordLevelConfidenceEnabled());
    
    // Test quality indicators configuration
    whisperSTT->setQualityIndicatorsEnabled(true);
    EXPECT_TRUE(whisperSTT->isQualityIndicatorsEnabled());
    
    // Test confidence filtering configuration
    whisperSTT->setConfidenceFilteringEnabled(true);
    EXPECT_TRUE(whisperSTT->isConfidenceFilteringEnabled());
}

TEST_F(ConfidenceCalculationTest, TranscriptionResultStructure) {
    // Test that TranscriptionResult has all required confidence fields
    stt::TranscriptionResult result;
    
    // Basic confidence fields
    EXPECT_EQ(result.confidence, 0.0f);
    EXPECT_FALSE(result.meets_confidence_threshold);
    EXPECT_EQ(result.quality_level, "low");
    
    // Enhanced fields
    EXPECT_TRUE(result.word_timings.empty());
    EXPECT_TRUE(result.alternatives.empty());
    
    // Quality metrics
    EXPECT_EQ(result.quality_metrics.signal_to_noise_ratio, 0.0f);
    EXPECT_EQ(result.quality_metrics.audio_clarity_score, 0.0f);
    EXPECT_FALSE(result.quality_metrics.has_background_noise);
    EXPECT_EQ(result.quality_metrics.processing_latency_ms, 0.0f);
}

TEST_F(ConfidenceCalculationTest, WordTimingStructure) {
    // Test WordTiming structure
    stt::WordTiming wordTiming("hello", 100, 500, 0.95f);
    
    EXPECT_EQ(wordTiming.word, "hello");
    EXPECT_EQ(wordTiming.start_ms, 100);
    EXPECT_EQ(wordTiming.end_ms, 500);
    EXPECT_EQ(wordTiming.confidence, 0.95f);
}

TEST_F(ConfidenceCalculationTest, TranscriptionQualityStructure) {
    // Test TranscriptionQuality structure
    stt::TranscriptionQuality quality;
    
    EXPECT_EQ(quality.signal_to_noise_ratio, 0.0f);
    EXPECT_EQ(quality.audio_clarity_score, 0.0f);
    EXPECT_FALSE(quality.has_background_noise);
    EXPECT_EQ(quality.processing_latency_ms, 0.0f);
    EXPECT_EQ(quality.average_token_probability, 0.0f);
    EXPECT_EQ(quality.no_speech_probability, 0.0f);
}

TEST_F(ConfidenceCalculationTest, BasicTranscriptionWithConfidence) {
    // Test that transcription produces enhanced confidence information
    std::vector<float> testAudio(16000, 0.1f); // 1 second of test audio
    bool callbackCalled = false;
    stt::TranscriptionResult receivedResult;
    
    // Enable all confidence features
    whisperSTT->setWordLevelConfidenceEnabled(true);
    whisperSTT->setQualityIndicatorsEnabled(true);
    whisperSTT->setConfidenceThreshold(0.7f);
    
    whisperSTT->transcribe(testAudio, [&](const stt::TranscriptionResult& result) {
        receivedResult = result;
        callbackCalled = true;
    });
    
    // Wait for callback
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    ASSERT_TRUE(callbackCalled);
    
    // Verify confidence information is present
    EXPECT_GE(receivedResult.confidence, 0.0f);
    EXPECT_LE(receivedResult.confidence, 1.0f);
    
    // Verify quality level is set
    EXPECT_FALSE(receivedResult.quality_level.empty());
    EXPECT_TRUE(receivedResult.quality_level == "high" || 
                receivedResult.quality_level == "medium" || 
                receivedResult.quality_level == "low");
    
    // Verify confidence threshold check is performed
    bool expectedThresholdMet = receivedResult.confidence >= 0.7f;
    EXPECT_EQ(receivedResult.meets_confidence_threshold, expectedThresholdMet);
    
    // Verify quality metrics are calculated
    EXPECT_GE(receivedResult.quality_metrics.processing_latency_ms, 0.0f);
}

TEST_F(ConfidenceCalculationTest, ConfidenceFilteringBehavior) {
    // Test confidence filtering functionality
    std::vector<float> testAudio(8000, 0.05f); // Short, low-quality audio
    bool callbackCalled = false;
    stt::TranscriptionResult receivedResult;
    
    // Enable confidence filtering with high threshold
    whisperSTT->setConfidenceFilteringEnabled(true);
    whisperSTT->setConfidenceThreshold(0.9f); // Very high threshold
    
    whisperSTT->transcribe(testAudio, [&](const stt::TranscriptionResult& result) {
        receivedResult = result;
        callbackCalled = true;
    });
    
    // Wait for callback
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    ASSERT_TRUE(callbackCalled);
    
    // With high threshold and low-quality audio, result should not meet threshold
    EXPECT_FALSE(receivedResult.meets_confidence_threshold);
    
    // Quality level should reflect the low confidence
    EXPECT_TRUE(receivedResult.quality_level == "low" || receivedResult.quality_level == "rejected");
}

TEST_F(ConfidenceCalculationTest, LiveTranscriptionConfidence) {
    // Test that live transcription also includes confidence information
    std::vector<float> testAudio(16000, 0.1f);
    bool callbackCalled = false;
    stt::TranscriptionResult receivedResult;
    
    whisperSTT->setQualityIndicatorsEnabled(true);
    
    whisperSTT->transcribeLive(testAudio, [&](const stt::TranscriptionResult& result) {
        receivedResult = result;
        callbackCalled = true;
    });
    
    // Wait for callback
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    ASSERT_TRUE(callbackCalled);
    
    // Verify confidence information is present for live transcription
    EXPECT_GE(receivedResult.confidence, 0.0f);
    EXPECT_LE(receivedResult.confidence, 1.0f);
    EXPECT_FALSE(receivedResult.quality_level.empty());
    EXPECT_TRUE(receivedResult.is_partial); // Live transcription should be marked as partial
}

TEST_F(ConfidenceCalculationTest, ErrorResultsHaveProperConfidence) {
    // Test that error results have proper confidence information
    std::vector<float> emptyAudio; // Empty audio should cause an error
    bool callbackCalled = false;
    stt::TranscriptionResult receivedResult;
    
    whisperSTT->transcribe(emptyAudio, [&](const stt::TranscriptionResult& result) {
        receivedResult = result;
        callbackCalled = true;
    });
    
    // Wait for callback
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Empty audio might not trigger callback in some implementations
    // This test verifies that if a callback is made, it has proper confidence info
    if (callbackCalled) {
        EXPECT_EQ(receivedResult.confidence, 0.0f);
        EXPECT_FALSE(receivedResult.meets_confidence_threshold);
        EXPECT_FALSE(receivedResult.quality_level.empty());
    }
}