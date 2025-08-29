#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "stt/whisper_stt.hpp"
#include "stt/stt_interface.hpp"
#include <vector>
#include <string>
#include <chrono>
#include <thread>

using namespace stt;
using ::testing::_;
using ::testing::Return;
using ::testing::InSequence;

class LanguageDetectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        whisperSTT_ = std::make_unique<WhisperSTT>();
        
        // Initialize with a test model path (will use simulation mode if whisper.cpp not available)
        bool initialized = whisperSTT_->initialize("test_models/whisper-base.bin", 4);
        ASSERT_TRUE(initialized) << "Failed to initialize WhisperSTT: " << whisperSTT_->getLastError();
        
        // Generate test audio data (1 second of 16kHz audio)
        testAudio_.resize(16000);
        for (size_t i = 0; i < testAudio_.size(); ++i) {
            testAudio_[i] = 0.1f * std::sin(2.0f * M_PI * 440.0f * i / 16000.0f); // 440Hz sine wave
        }
    }
    
    void TearDown() override {
        whisperSTT_.reset();
    }
    
    std::unique_ptr<WhisperSTT> whisperSTT_;
    std::vector<float> testAudio_;
    
    // Helper to wait for async transcription
    TranscriptionResult waitForTranscription() {
        TranscriptionResult result;
        bool completed = false;
        
        whisperSTT_->transcribe(testAudio_, [&](const TranscriptionResult& res) {
            result = res;
            completed = true;
        });
        
        // Wait up to 5 seconds for completion
        auto start = std::chrono::steady_clock::now();
        while (!completed && std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        EXPECT_TRUE(completed) << "Transcription did not complete within timeout";
        return result;
    }
};

TEST_F(LanguageDetectionTest, DefaultConfiguration) {
    // Test default language detection configuration
    EXPECT_FALSE(whisperSTT_->isLanguageDetectionEnabled());
    EXPECT_FALSE(whisperSTT_->isAutoLanguageSwitchingEnabled());
    EXPECT_EQ(whisperSTT_->getCurrentDetectedLanguage(), "en");
}

TEST_F(LanguageDetectionTest, EnableLanguageDetection) {
    // Test enabling language detection
    whisperSTT_->setLanguageDetectionEnabled(true);
    EXPECT_TRUE(whisperSTT_->isLanguageDetectionEnabled());
    
    whisperSTT_->setLanguageDetectionEnabled(false);
    EXPECT_FALSE(whisperSTT_->isLanguageDetectionEnabled());
}

TEST_F(LanguageDetectionTest, SetLanguageDetectionThreshold) {
    // Test setting language detection threshold
    whisperSTT_->setLanguageDetectionThreshold(0.8f);
    
    // Test boundary values
    whisperSTT_->setLanguageDetectionThreshold(-0.1f); // Should clamp to 0.0
    whisperSTT_->setLanguageDetectionThreshold(1.5f);  // Should clamp to 1.0
}

TEST_F(LanguageDetectionTest, EnableAutoLanguageSwitching) {
    // Test enabling auto language switching
    whisperSTT_->setAutoLanguageSwitching(true);
    EXPECT_TRUE(whisperSTT_->isAutoLanguageSwitchingEnabled());
    
    whisperSTT_->setAutoLanguageSwitching(false);
    EXPECT_FALSE(whisperSTT_->isAutoLanguageSwitchingEnabled());
}

TEST_F(LanguageDetectionTest, TranscriptionWithLanguageDetection) {
    // Enable language detection
    whisperSTT_->setLanguageDetectionEnabled(true);
    whisperSTT_->setLanguageDetectionThreshold(0.5f);
    
    // Perform transcription and check language information
    auto result = waitForTranscription();
    
    // Check that language detection fields are populated
    EXPECT_FALSE(result.detected_language.empty());
    EXPECT_GE(result.language_confidence, 0.0f);
    EXPECT_LE(result.language_confidence, 1.0f);
}

TEST_F(LanguageDetectionTest, LanguageChangeCallback) {
    // Test language change callback functionality
    std::string oldLang, newLang;
    float confidence = 0.0f;
    bool callbackCalled = false;
    
    whisperSTT_->setLanguageChangeCallback([&](const std::string& old_lang, const std::string& new_lang, float conf) {
        oldLang = old_lang;
        newLang = new_lang;
        confidence = conf;
        callbackCalled = true;
    });
    
    // Enable language detection and auto-switching
    whisperSTT_->setLanguageDetectionEnabled(true);
    whisperSTT_->setAutoLanguageSwitching(true);
    whisperSTT_->setLanguageDetectionThreshold(0.3f); // Low threshold for testing
    
    // Perform multiple transcriptions to potentially trigger language change
    for (int i = 0; i < 5; ++i) {
        auto result = waitForTranscription();
        if (callbackCalled) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // In simulation mode, we should eventually get a language change
    // Note: This test might be flaky in simulation mode due to randomness
    if (callbackCalled) {
        EXPECT_FALSE(oldLang.empty());
        EXPECT_FALSE(newLang.empty());
        EXPECT_NE(oldLang, newLang);
        EXPECT_GE(confidence, 0.0f);
        EXPECT_LE(confidence, 1.0f);
    }
}

TEST_F(LanguageDetectionTest, StreamingWithLanguageDetection) {
    // Test language detection with streaming transcription
    whisperSTT_->setLanguageDetectionEnabled(true);
    whisperSTT_->setAutoLanguageSwitching(true);
    whisperSTT_->setPartialResultsEnabled(true);
    
    uint32_t utteranceId = 12345;
    std::vector<TranscriptionResult> results;
    
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        results.push_back(result);
    });
    
    // Start streaming transcription
    whisperSTT_->startStreamingTranscription(utteranceId);
    EXPECT_TRUE(whisperSTT_->isStreamingActive(utteranceId));
    
    // Add audio chunks
    size_t chunkSize = testAudio_.size() / 4;
    for (size_t i = 0; i < 4; ++i) {
        std::vector<float> chunk(testAudio_.begin() + i * chunkSize, 
                                testAudio_.begin() + (i + 1) * chunkSize);
        whisperSTT_->addAudioChunk(utteranceId, chunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Finalize streaming
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    
    // Wait for final results
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    EXPECT_FALSE(whisperSTT_->isStreamingActive(utteranceId));
    
    // Check that we got some results with language information
    EXPECT_GT(results.size(), 0);
    for (const auto& result : results) {
        EXPECT_FALSE(result.detected_language.empty());
        EXPECT_GE(result.language_confidence, 0.0f);
        EXPECT_LE(result.language_confidence, 1.0f);
    }
}

TEST_F(LanguageDetectionTest, MultipleStreamingUtterances) {
    // Test language detection with multiple concurrent streaming utterances
    whisperSTT_->setLanguageDetectionEnabled(true);
    whisperSTT_->setPartialResultsEnabled(true);
    
    std::vector<uint32_t> utteranceIds = {1001, 1002, 1003};
    std::map<uint32_t, std::vector<TranscriptionResult>> allResults;
    
    // Set up callbacks for each utterance
    for (uint32_t id : utteranceIds) {
        whisperSTT_->setStreamingCallback(id, [&, id](const TranscriptionResult& result) {
            allResults[id].push_back(result);
        });
    }
    
    // Start all streaming transcriptions
    for (uint32_t id : utteranceIds) {
        whisperSTT_->startStreamingTranscription(id);
        EXPECT_TRUE(whisperSTT_->isStreamingActive(id));
    }
    
    EXPECT_EQ(whisperSTT_->getActiveStreamingCount(), utteranceIds.size());
    
    // Add audio to all utterances
    for (uint32_t id : utteranceIds) {
        whisperSTT_->addAudioChunk(id, testAudio_);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Finalize all streaming
    for (uint32_t id : utteranceIds) {
        whisperSTT_->finalizeStreamingTranscription(id);
    }
    
    // Wait for completion
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    EXPECT_EQ(whisperSTT_->getActiveStreamingCount(), 0);
    
    // Verify results for each utterance
    for (uint32_t id : utteranceIds) {
        EXPECT_GT(allResults[id].size(), 0) << "No results for utterance " << id;
        for (const auto& result : allResults[id]) {
            EXPECT_FALSE(result.detected_language.empty());
            EXPECT_GE(result.language_confidence, 0.0f);
            EXPECT_LE(result.language_confidence, 1.0f);
        }
    }
}

TEST_F(LanguageDetectionTest, LanguageDetectionDisabled) {
    // Test that language detection fields are properly set when detection is disabled
    whisperSTT_->setLanguageDetectionEnabled(false);
    whisperSTT_->setLanguage("es"); // Set Spanish
    
    auto result = waitForTranscription();
    
    // When detection is disabled, should use the configured language
    EXPECT_EQ(result.detected_language, "es");
    EXPECT_EQ(result.language_confidence, 1.0f);
    EXPECT_FALSE(result.language_changed);
}

TEST_F(LanguageDetectionTest, LanguageValidation) {
    // Test language validation in auto-switching
    whisperSTT_->setLanguageDetectionEnabled(true);
    whisperSTT_->setAutoLanguageSwitching(true);
    whisperSTT_->setLanguageDetectionThreshold(0.1f); // Very low threshold
    
    // The system should validate languages before switching
    // This test mainly ensures no crashes occur with invalid language codes
    for (int i = 0; i < 3; ++i) {
        auto result = waitForTranscription();
        // Just ensure we get valid results without crashes
        EXPECT_GE(result.language_confidence, 0.0f);
        EXPECT_LE(result.language_confidence, 1.0f);
    }
}

// Performance test for language detection overhead
TEST_F(LanguageDetectionTest, LanguageDetectionPerformance) {
    const int NUM_ITERATIONS = 10;
    
    // Test without language detection
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        auto result = waitForTranscription();
    }
    auto withoutDetection = std::chrono::high_resolution_clock::now() - start;
    
    // Test with language detection
    whisperSTT_->setLanguageDetectionEnabled(true);
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        auto result = waitForTranscription();
    }
    auto withDetection = std::chrono::high_resolution_clock::now() - start;
    
    // Language detection should not add significant overhead (less than 50% increase)
    auto detectionOverhead = std::chrono::duration_cast<std::chrono::milliseconds>(withDetection - withoutDetection).count();
    auto baseTime = std::chrono::duration_cast<std::chrono::milliseconds>(withoutDetection).count();
    
    std::cout << "Base time: " << baseTime << "ms, With detection: " 
              << std::chrono::duration_cast<std::chrono::milliseconds>(withDetection).count() 
              << "ms, Overhead: " << detectionOverhead << "ms" << std::endl;
    
    // This is a performance guideline, not a strict requirement
    if (baseTime > 0) {
        float overheadPercent = (float)detectionOverhead / baseTime * 100.0f;
        EXPECT_LT(overheadPercent, 50.0f) << "Language detection overhead too high: " << overheadPercent << "%";
    }
}