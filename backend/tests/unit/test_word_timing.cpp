#include <gtest/gtest.h>
#include "stt/whisper_stt.hpp"
#include "stt/stt_interface.hpp"
#include <vector>
#include <string>
#include <chrono>
#include <thread>

class WordTimingTest : public ::testing::Test {
protected:
    void SetUp() override {
        whisperSTT = std::make_unique<stt::WhisperSTT>();
        
        // Enable word-level confidence for testing
        whisperSTT->setWordLevelConfidenceEnabled(true);
        whisperSTT->setQualityIndicatorsEnabled(true);
        whisperSTT->setPartialResultsEnabled(true);
        
        // Initialize in simulation mode (no model file needed)
        whisperSTT->initialize("test_model.bin", 4);
    }
    
    void TearDown() override {
        whisperSTT.reset();
    }
    
    std::unique_ptr<stt::WhisperSTT> whisperSTT;
    stt::TranscriptionResult lastResult;
    bool callbackCalled = false;
    
    void transcriptionCallback(const stt::TranscriptionResult& result) {
        lastResult = result;
        callbackCalled = true;
    }
};

TEST_F(WordTimingTest, BasicWordTimingExtraction) {
    // Test basic transcription with word timing
    std::vector<float> testAudio(16000 * 2, 0.1f); // 2 seconds of test audio
    callbackCalled = false;
    
    whisperSTT->transcribe(testAudio, [this](const stt::TranscriptionResult& result) {
        transcriptionCallback(result);
    });
    
    // Wait for callback
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(callbackCalled);
    EXPECT_FALSE(lastResult.text.empty());
    EXPECT_FALSE(lastResult.word_timings.empty());
    
    // Verify word timings are properly structured
    for (const auto& wordTiming : lastResult.word_timings) {
        EXPECT_FALSE(wordTiming.word.empty());
        EXPECT_GE(wordTiming.start_ms, 0);
        EXPECT_GT(wordTiming.end_ms, wordTiming.start_ms);
        EXPECT_GE(wordTiming.confidence, 0.0f);
        EXPECT_LE(wordTiming.confidence, 1.0f);
    }
    
    // Verify word timings are in chronological order
    for (size_t i = 1; i < lastResult.word_timings.size(); ++i) {
        EXPECT_GE(lastResult.word_timings[i].start_ms, lastResult.word_timings[i-1].start_ms);
    }
}

TEST_F(WordTimingTest, StreamingWordTimingIntegration) {
    // Test streaming transcription with word timing
    uint32_t utteranceId = 1;
    std::vector<stt::TranscriptionResult> streamingResults;
    
    whisperSTT->setStreamingCallback(utteranceId, [&streamingResults](const stt::TranscriptionResult& result) {
        streamingResults.push_back(result);
    });
    
    // Start streaming transcription
    whisperSTT->startStreamingTranscription(utteranceId);
    
    // Add audio chunks
    std::vector<float> chunk1(8000, 0.1f); // 0.5 seconds
    std::vector<float> chunk2(8000, 0.2f); // 0.5 seconds
    std::vector<float> chunk3(8000, 0.3f); // 0.5 seconds
    
    whisperSTT->addAudioChunk(utteranceId, chunk1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    whisperSTT->addAudioChunk(utteranceId, chunk2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    whisperSTT->addAudioChunk(utteranceId, chunk3);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Finalize streaming
    whisperSTT->finalizeStreamingTranscription(utteranceId);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Verify we got streaming results
    EXPECT_FALSE(streamingResults.empty());
    
    // Check that at least one result has word timings
    bool hasWordTimings = false;
    for (const auto& result : streamingResults) {
        if (!result.word_timings.empty()) {
            hasWordTimings = true;
            
            // Verify word timing consistency
            for (const auto& wordTiming : result.word_timings) {
                EXPECT_FALSE(wordTiming.word.empty());
                EXPECT_GE(wordTiming.start_ms, 0);
                EXPECT_GT(wordTiming.end_ms, wordTiming.start_ms);
                EXPECT_GE(wordTiming.confidence, 0.0f);
                EXPECT_LE(wordTiming.confidence, 1.0f);
            }
        }
    }
    
    EXPECT_TRUE(hasWordTimings);
}

TEST_F(WordTimingTest, WordTimingConsistencyValidation) {
    // Test that word timings are validated for consistency
    std::vector<float> testAudio(16000, 0.1f); // 1 second of test audio
    callbackCalled = false;
    
    whisperSTT->transcribe(testAudio, [this](const stt::TranscriptionResult& result) {
        transcriptionCallback(result);
    });
    
    // Wait for callback
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(callbackCalled);
    
    if (!lastResult.word_timings.empty()) {
        // Verify no overlapping word timings
        for (size_t i = 1; i < lastResult.word_timings.size(); ++i) {
            EXPECT_GE(lastResult.word_timings[i].start_ms, lastResult.word_timings[i-1].end_ms)
                << "Word timings should not overlap";
        }
        
        // Verify all word timings are within transcription bounds
        for (const auto& wordTiming : lastResult.word_timings) {
            EXPECT_GE(wordTiming.start_ms, lastResult.start_time_ms);
            if (lastResult.end_time_ms > 0) {
                EXPECT_LE(wordTiming.end_ms, lastResult.end_time_ms);
            }
        }
        
        // Verify minimum word duration
        for (const auto& wordTiming : lastResult.word_timings) {
            EXPECT_GE(wordTiming.end_ms - wordTiming.start_ms, 100)
                << "Words should have minimum 100ms duration";
        }
    }
}

TEST_F(WordTimingTest, ConfidenceAdjustmentLogic) {
    // Test confidence adjustment for different word types
    std::vector<float> testAudio(16000 * 3, 0.1f); // 3 seconds of test audio
    callbackCalled = false;
    
    whisperSTT->transcribe(testAudio, [this](const stt::TranscriptionResult& result) {
        transcriptionCallback(result);
    });
    
    // Wait for callback
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    EXPECT_TRUE(callbackCalled);
    
    if (!lastResult.word_timings.empty()) {
        // Verify confidence scores are reasonable
        for (const auto& wordTiming : lastResult.word_timings) {
            EXPECT_GE(wordTiming.confidence, 0.0f);
            EXPECT_LE(wordTiming.confidence, 1.0f);
            
            // In simulation mode, confidence should be reasonably high
            EXPECT_GE(wordTiming.confidence, 0.5f);
        }
        
        // Verify confidence is consistent with overall transcription confidence
        float avgWordConfidence = 0.0f;
        for (const auto& wordTiming : lastResult.word_timings) {
            avgWordConfidence += wordTiming.confidence;
        }
        avgWordConfidence /= lastResult.word_timings.size();
        
        // Word confidence should be reasonably close to overall confidence
        float confidenceDiff = std::abs(avgWordConfidence - lastResult.confidence);
        EXPECT_LT(confidenceDiff, 0.3f) << "Word confidence should be consistent with overall confidence";
    }
}

TEST_F(WordTimingTest, DisabledWordTimingBehavior) {
    // Test behavior when word timing is disabled
    whisperSTT->setWordLevelConfidenceEnabled(false);
    
    std::vector<float> testAudio(16000, 0.1f); // 1 second of test audio
    callbackCalled = false;
    
    whisperSTT->transcribe(testAudio, [this](const stt::TranscriptionResult& result) {
        transcriptionCallback(result);
    });
    
    // Wait for callback
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(callbackCalled);
    EXPECT_FALSE(lastResult.text.empty());
    
    // Word timings should be empty when disabled
    EXPECT_TRUE(lastResult.word_timings.empty());
}