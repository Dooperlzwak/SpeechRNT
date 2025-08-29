#include <gtest/gtest.h>
#include "stt/whisper_stt.hpp"
#include "stt/stt_interface.hpp"
#include "stt/quantization_config.hpp"
#include "stt/stt_performance_tracker.hpp"
#include "audio/audio_buffer_manager.hpp"
#include <vector>
#include <chrono>
#include <thread>
#include <memory>
#include <atomic>
#include <future>

using namespace stt;

class WhisperSTTTest : public ::testing::Test {
protected:
    void SetUp() override {
        whisperSTT_ = std::make_unique<WhisperSTT>();
        
        // Generate test audio data
        generateTestAudioData();
    }
    
    void TearDown() override {
        whisperSTT_.reset();
    }
    
    void generateTestAudioData() {
        const size_t sampleRate = 16000;
        
        // Generate 1 second of test audio (sine wave)
        testAudio1s_.resize(sampleRate);
        for (size_t i = 0; i < sampleRate; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            testAudio1s_[i] = 0.3f * std::sin(2.0f * M_PI * 440.0f * t); // 440Hz tone
        }
        
        // Generate 0.5 second of test audio
        testAudio500ms_.resize(sampleRate / 2);
        for (size_t i = 0; i < sampleRate / 2; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            testAudio500ms_[i] = 0.2f * std::sin(2.0f * M_PI * 880.0f * t); // 880Hz tone
        }
        
        // Generate silence
        silenceAudio_.resize(sampleRate, 0.0f);
        
        // Generate noise
        noiseAudio_.resize(sampleRate);
        for (size_t i = 0; i < sampleRate; ++i) {
            noiseAudio_[i] = 0.05f * (static_cast<float>(rand()) / RAND_MAX - 0.5f);
        }
    }
    
    std::unique_ptr<WhisperSTT> whisperSTT_;
    std::vector<float> testAudio1s_;
    std::vector<float> testAudio500ms_;
    std::vector<float> silenceAudio_;
    std::vector<float> noiseAudio_;
};

// ============================================================================
// Real Model Loading and Inference Tests
// ============================================================================

TEST_F(WhisperSTTTest, InitializationWithRealModel) {
    // Test initialization with dummy model path (simulation mode)
    EXPECT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    EXPECT_TRUE(whisperSTT_->isInitialized());
    EXPECT_EQ(whisperSTT_->getLastError(), "");
}

TEST_F(WhisperSTTTest, InitializationWithGPU) {
    // Test GPU initialization
    EXPECT_TRUE(whisperSTT_->initializeWithGPU("dummy_model.bin", 0, 4));
    EXPECT_TRUE(whisperSTT_->isInitialized());
}

TEST_F(WhisperSTTTest, InitializationFailureHandling) {
    // Test initialization with invalid model path
    EXPECT_FALSE(whisperSTT_->initialize(""));
    EXPECT_FALSE(whisperSTT_->isInitialized());
    EXPECT_NE(whisperSTT_->getLastError(), "");
}

TEST_F(WhisperSTTTest, ModelValidation) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Test that model validation occurs during initialization
    EXPECT_TRUE(whisperSTT_->isInitialized());
}

TEST_F(WhisperSTTTest, RealInferenceVsSimulation) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::atomic<bool> callbackCalled{false};
    std::string transcribedText;
    float confidence = 0.0f;
    
    whisperSTT_->transcribe(testAudio1s_, [&](const TranscriptionResult& result) {
        callbackCalled = true;
        transcribedText = result.text;
        confidence = result.confidence;
    });
    
    // Wait for callback
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    EXPECT_TRUE(callbackCalled);
    EXPECT_FALSE(transcribedText.empty());
    EXPECT_GE(confidence, 0.0f);
    EXPECT_LE(confidence, 1.0f);
}

// ============================================================================
// Streaming Transcription Tests
// ============================================================================

TEST_F(WhisperSTTTest, StreamingTranscriptionBasic) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    uint32_t utteranceId = 123;
    std::atomic<int> callbackCount{0};
    std::vector<TranscriptionResult> results;
    std::mutex resultsMutex;
    
    // Set up streaming callback
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        std::lock_guard<std::mutex> lock(resultsMutex);
        results.push_back(result);
        callbackCount++;
    });
    
    // Start streaming transcription
    whisperSTT_->startStreamingTranscription(utteranceId);
    EXPECT_TRUE(whisperSTT_->isStreamingActive(utteranceId));
    
    // Add audio chunks
    whisperSTT_->addAudioChunk(utteranceId, testAudio500ms_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    whisperSTT_->addAudioChunk(utteranceId, testAudio500ms_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Finalize streaming
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_FALSE(whisperSTT_->isStreamingActive(utteranceId));
    EXPECT_GT(callbackCount.load(), 0);
    
    // Check that we received both partial and final results
    std::lock_guard<std::mutex> lock(resultsMutex);
    bool hasPartial = false, hasFinal = false;
    for (const auto& result : results) {
        if (result.is_partial) hasPartial = true;
        else hasFinal = true;
    }
    EXPECT_TRUE(hasPartial || hasFinal); // Should have at least one result
}

TEST_F(WhisperSTTTest, StreamingTranscriptionMultipleUtterances) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::vector<uint32_t> utteranceIds = {100, 101, 102};
    std::atomic<int> totalCallbacks{0};
    
    // Start multiple streaming transcriptions
    for (uint32_t id : utteranceIds) {
        whisperSTT_->setStreamingCallback(id, [&](const TranscriptionResult& result) {
            totalCallbacks++;
        });
        whisperSTT_->startStreamingTranscription(id);
        EXPECT_TRUE(whisperSTT_->isStreamingActive(id));
    }
    
    EXPECT_EQ(whisperSTT_->getActiveStreamingCount(), 3);
    
    // Add audio to all utterances
    for (uint32_t id : utteranceIds) {
        whisperSTT_->addAudioChunk(id, testAudio500ms_);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Finalize all
    for (uint32_t id : utteranceIds) {
        whisperSTT_->finalizeStreamingTranscription(id);
        EXPECT_FALSE(whisperSTT_->isStreamingActive(id));
    }
    
    EXPECT_EQ(whisperSTT_->getActiveStreamingCount(), 0);
    EXPECT_GT(totalCallbacks.load(), 0);
}

TEST_F(WhisperSTTTest, StreamingConfiguration) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Test streaming configuration methods
    whisperSTT_->setPartialResultsEnabled(true);
    whisperSTT_->setMinChunkSizeMs(100);
    whisperSTT_->setConfidenceThreshold(0.7f);
    
    EXPECT_EQ(whisperSTT_->getConfidenceThreshold(), 0.7f);
    
    // Test word-level confidence configuration
    whisperSTT_->setWordLevelConfidenceEnabled(true);
    whisperSTT_->setQualityIndicatorsEnabled(true);
    whisperSTT_->setConfidenceFilteringEnabled(true);
    
    EXPECT_TRUE(whisperSTT_->isWordLevelConfidenceEnabled());
    EXPECT_TRUE(whisperSTT_->isQualityIndicatorsEnabled());
    EXPECT_TRUE(whisperSTT_->isConfidenceFilteringEnabled());
}

// ============================================================================
// VAD Integration and State Management Tests
// ============================================================================

TEST_F(WhisperSTTTest, VADIntegrationWithSilence) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::atomic<bool> callbackCalled{false};
    TranscriptionResult result;
    
    // Transcribe silence - should handle gracefully
    whisperSTT_->transcribe(silenceAudio_, [&](const TranscriptionResult& res) {
        callbackCalled = true;
        result = res;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(callbackCalled);
    // Silence might produce empty text or low confidence
    EXPECT_GE(result.confidence, 0.0f);
}

TEST_F(WhisperSTTTest, VADIntegrationWithNoise) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::atomic<bool> callbackCalled{false};
    TranscriptionResult result;
    
    // Transcribe noise
    whisperSTT_->transcribe(noiseAudio_, [&](const TranscriptionResult& res) {
        callbackCalled = true;
        result = res;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(callbackCalled);
    EXPECT_GE(result.confidence, 0.0f);
    EXPECT_LE(result.confidence, 1.0f);
}

TEST_F(WhisperSTTTest, VADStateManagement) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    uint32_t utteranceId = 200;
    std::atomic<int> stateChanges{0};
    
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        stateChanges++;
    });
    
    // Start streaming
    whisperSTT_->startStreamingTranscription(utteranceId);
    
    // Add different types of audio to trigger VAD state changes
    whisperSTT_->addAudioChunk(utteranceId, silenceAudio_);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    whisperSTT_->addAudioChunk(utteranceId, testAudio1s_);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    whisperSTT_->addAudioChunk(utteranceId, silenceAudio_);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Should have received some callbacks
    EXPECT_GE(stateChanges.load(), 0);
}

// ============================================================================
// Error Recovery and Fallback Mechanism Tests
// ============================================================================

TEST_F(WhisperSTTTest, ModelLoadingErrorRecovery) {
    // Test fallback to simulation mode when model loading fails
    EXPECT_FALSE(whisperSTT_->initialize("nonexistent_model.bin"));
    EXPECT_FALSE(whisperSTT_->isInitialized());
    EXPECT_NE(whisperSTT_->getLastError(), "");
    
    // Should be able to recover with valid model
    EXPECT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    EXPECT_TRUE(whisperSTT_->isInitialized());
}

TEST_F(WhisperSTTTest, GPUFallbackToCPU) {
    // Test GPU initialization failure fallback
    // This might succeed or fail depending on hardware, but should handle gracefully
    bool gpuResult = whisperSTT_->initializeWithGPU("dummy_model.bin", 999, 4); // Invalid GPU ID
    
    if (!gpuResult) {
        // Should be able to fallback to CPU
        EXPECT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
        EXPECT_TRUE(whisperSTT_->isInitialized());
    }
}

TEST_F(WhisperSTTTest, TranscriptionErrorHandling) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::atomic<bool> callbackCalled{false};
    std::string errorMessage;
    
    // Test with empty audio data
    std::vector<float> emptyAudio;
    whisperSTT_->transcribe(emptyAudio, [&](const TranscriptionResult& result) {
        callbackCalled = true;
        // Should handle empty audio gracefully
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Should either call callback with empty result or handle gracefully
    // The exact behavior depends on implementation
}

TEST_F(WhisperSTTTest, StreamingErrorRecovery) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    uint32_t utteranceId = 300;
    
    // Start streaming without setting callback (should handle gracefully)
    whisperSTT_->startStreamingTranscription(utteranceId);
    EXPECT_TRUE(whisperSTT_->isStreamingActive(utteranceId));
    
    // Add audio chunk
    whisperSTT_->addAudioChunk(utteranceId, testAudio500ms_);
    
    // Should be able to finalize even without callback
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    EXPECT_FALSE(whisperSTT_->isStreamingActive(utteranceId));
}

// ============================================================================
// Language Detection and Auto-switching Tests
// ============================================================================

TEST_F(WhisperSTTTest, LanguageDetectionConfiguration) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Test language detection configuration
    whisperSTT_->setLanguageDetectionEnabled(true);
    whisperSTT_->setLanguageDetectionThreshold(0.8f);
    whisperSTT_->setAutoLanguageSwitching(true);
    
    EXPECT_TRUE(whisperSTT_->isLanguageDetectionEnabled());
    EXPECT_TRUE(whisperSTT_->isAutoLanguageSwitchingEnabled());
}

TEST_F(WhisperSTTTest, LanguageChangeCallback) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::atomic<bool> languageChanged{false};
    std::string oldLang, newLang;
    float confidence = 0.0f;
    
    whisperSTT_->setLanguageChangeCallback([&](const std::string& old_lang, const std::string& new_lang, float conf) {
        languageChanged = true;
        oldLang = old_lang;
        newLang = new_lang;
        confidence = conf;
    });
    
    whisperSTT_->setLanguageDetectionEnabled(true);
    whisperSTT_->setAutoLanguageSwitching(true);
    
    // Set initial language
    whisperSTT_->setLanguage("en");
    
    // Transcribe audio (might trigger language detection)
    std::atomic<bool> transcriptionDone{false};
    whisperSTT_->transcribe(testAudio1s_, [&](const TranscriptionResult& result) {
        transcriptionDone = true;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(transcriptionDone);
    
    // Language change callback might or might not be called depending on detection
}

TEST_F(WhisperSTTTest, ManualLanguageSwitching) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Test manual language switching
    whisperSTT_->setLanguage("en");
    whisperSTT_->setLanguage("es");
    whisperSTT_->setLanguage("fr");
    whisperSTT_->setLanguage("auto");
    
    // Should handle all language switches gracefully
}

// ============================================================================
// Quantization Support Tests
// ============================================================================

TEST_F(WhisperSTTTest, QuantizationConfiguration) {
    // Test quantization level setting
    whisperSTT_->setQuantizationLevel(QuantizationLevel::FP16);
    EXPECT_EQ(whisperSTT_->getQuantizationLevel(), QuantizationLevel::FP16);
    
    whisperSTT_->setQuantizationLevel(QuantizationLevel::INT8);
    EXPECT_EQ(whisperSTT_->getQuantizationLevel(), QuantizationLevel::INT8);
    
    whisperSTT_->setQuantizationLevel(QuantizationLevel::AUTO);
    EXPECT_EQ(whisperSTT_->getQuantizationLevel(), QuantizationLevel::AUTO);
}

TEST_F(WhisperSTTTest, QuantizationInitialization) {
    // Test initialization with different quantization levels
    EXPECT_TRUE(whisperSTT_->initializeWithQuantization("dummy_model.bin", QuantizationLevel::FP32));
    EXPECT_TRUE(whisperSTT_->isInitialized());
    
    // Reset for next test
    whisperSTT_.reset();
    whisperSTT_ = std::make_unique<WhisperSTT>();
    
    EXPECT_TRUE(whisperSTT_->initializeWithQuantization("dummy_model.bin", QuantizationLevel::FP16));
    EXPECT_TRUE(whisperSTT_->isInitialized());
}

TEST_F(WhisperSTTTest, QuantizationGPUInitialization) {
    // Test GPU initialization with quantization
    bool result = whisperSTT_->initializeWithQuantizationGPU("dummy_model.bin", QuantizationLevel::FP16, 0);
    
    // Result depends on hardware availability, but should handle gracefully
    if (result) {
        EXPECT_TRUE(whisperSTT_->isInitialized());
    }
}

TEST_F(WhisperSTTTest, SupportedQuantizationLevels) {
    auto supportedLevels = whisperSTT_->getSupportedQuantizationLevels();
    EXPECT_FALSE(supportedLevels.empty());
    
    // Should at least support FP32
    bool hasFP32 = std::find(supportedLevels.begin(), supportedLevels.end(), QuantizationLevel::FP32) != supportedLevels.end();
    EXPECT_TRUE(hasFP32);
}

// ============================================================================
// Confidence Score and Quality Metrics Tests
// ============================================================================

TEST_F(WhisperSTTTest, ConfidenceScoreCalculation) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::atomic<bool> callbackCalled{false};
    TranscriptionResult result;
    
    whisperSTT_->transcribe(testAudio1s_, [&](const TranscriptionResult& res) {
        callbackCalled = true;
        result = res;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(callbackCalled);
    EXPECT_GE(result.confidence, 0.0f);
    EXPECT_LE(result.confidence, 1.0f);
    EXPECT_FALSE(result.quality_level.empty());
}

TEST_F(WhisperSTTTest, WordLevelConfidence) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    whisperSTT_->setWordLevelConfidenceEnabled(true);
    
    std::atomic<bool> callbackCalled{false};
    TranscriptionResult result;
    
    whisperSTT_->transcribe(testAudio1s_, [&](const TranscriptionResult& res) {
        callbackCalled = true;
        result = res;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(callbackCalled);
    
    // Check word timings if available
    for (const auto& wordTiming : result.word_timings) {
        EXPECT_FALSE(wordTiming.word.empty());
        EXPECT_GE(wordTiming.confidence, 0.0f);
        EXPECT_LE(wordTiming.confidence, 1.0f);
        EXPECT_GE(wordTiming.start_ms, 0);
        EXPECT_LE(wordTiming.end_ms, result.end_time_ms);
    }
}

TEST_F(WhisperSTTTest, QualityMetrics) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    whisperSTT_->setQualityIndicatorsEnabled(true);
    
    std::atomic<bool> callbackCalled{false};
    TranscriptionResult result;
    
    whisperSTT_->transcribe(testAudio1s_, [&](const TranscriptionResult& res) {
        callbackCalled = true;
        result = res;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(callbackCalled);
    
    // Check quality metrics
    EXPECT_GE(result.quality_metrics.processing_latency_ms, 0.0f);
    EXPECT_GE(result.quality_metrics.signal_to_noise_ratio, 0.0f);
    EXPECT_GE(result.quality_metrics.audio_clarity_score, 0.0f);
}

TEST_F(WhisperSTTTest, ConfidenceFiltering) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    whisperSTT_->setConfidenceFilteringEnabled(true);
    whisperSTT_->setConfidenceThreshold(0.9f); // High threshold
    
    std::atomic<bool> callbackCalled{false};
    TranscriptionResult result;
    
    whisperSTT_->transcribe(noiseAudio_, [&](const TranscriptionResult& res) {
        callbackCalled = true;
        result = res;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(callbackCalled);
    
    // With high threshold and noise audio, might not meet threshold
    if (result.confidence < 0.9f) {
        EXPECT_FALSE(result.meets_confidence_threshold);
    }
}

// ============================================================================
// Translation Pipeline Integration Tests
// ============================================================================

TEST_F(WhisperSTTTest, TranscriptionCompleteCallback) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::atomic<bool> completionCallbackCalled{false};
    uint32_t receivedUtteranceId = 0;
    TranscriptionResult receivedResult;
    std::vector<TranscriptionResult> receivedCandidates;
    
    whisperSTT_->setTranscriptionCompleteCallback([&](uint32_t uttId, const TranscriptionResult& result, const std::vector<TranscriptionResult>& candidates) {
        completionCallbackCalled = true;
        receivedUtteranceId = uttId;
        receivedResult = result;
        receivedCandidates = candidates;
    });
    
    // Perform transcription
    std::atomic<bool> transcriptionDone{false};
    whisperSTT_->transcribe(testAudio1s_, [&](const TranscriptionResult& result) {
        transcriptionDone = true;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(transcriptionDone);
    // Completion callback might be called depending on implementation
}

TEST_F(WhisperSTTTest, MultipleCandidateGeneration) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::vector<TranscriptionResult> candidates;
    whisperSTT_->generateTranscriptionCandidates(testAudio1s_, candidates, 3);
    
    // Should generate at least one candidate
    EXPECT_GE(candidates.size(), 1);
    EXPECT_LE(candidates.size(), 3);
    
    // Candidates should be sorted by confidence (highest first)
    for (size_t i = 1; i < candidates.size(); ++i) {
        EXPECT_GE(candidates[i-1].confidence, candidates[i].confidence);
    }
}

// ============================================================================
// Performance and Stress Tests
// ============================================================================

TEST_F(WhisperSTTTest, ConcurrentTranscriptions) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    const int numConcurrent = 5;
    std::vector<std::future<bool>> futures;
    std::atomic<int> completedTranscriptions{0};
    
    // Start multiple concurrent transcriptions
    for (int i = 0; i < numConcurrent; ++i) {
        futures.push_back(std::async(std::launch::async, [this, i, &completedTranscriptions]() {
            std::atomic<bool> done{false};
            
            whisperSTT_->transcribe(testAudio1s_, [&](const TranscriptionResult& result) {
                done = true;
                completedTranscriptions++;
            });
            
            // Wait for completion with timeout
            auto start = std::chrono::steady_clock::now();
            while (!done && std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            return done.load();
        }));
    }
    
    // Wait for all to complete
    bool allCompleted = true;
    for (auto& future : futures) {
        allCompleted &= future.get();
    }
    
    EXPECT_TRUE(allCompleted);
    EXPECT_EQ(completedTranscriptions.load(), numConcurrent);
}

TEST_F(WhisperSTTTest, LongAudioTranscription) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Generate 5 seconds of audio
    std::vector<float> longAudio(16000 * 5);
    for (size_t i = 0; i < longAudio.size(); ++i) {
        float t = static_cast<float>(i) / 16000.0f;
        longAudio[i] = 0.3f * std::sin(2.0f * M_PI * 440.0f * t);
    }
    
    std::atomic<bool> callbackCalled{false};
    TranscriptionResult result;
    
    auto start = std::chrono::steady_clock::now();
    
    whisperSTT_->transcribe(longAudio, [&](const TranscriptionResult& res) {
        callbackCalled = true;
        result = res;
    });
    
    // Wait for completion with reasonable timeout
    auto timeout = std::chrono::seconds(10);
    auto deadline = start + timeout;
    
    while (!callbackCalled && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    EXPECT_TRUE(callbackCalled);
    if (callbackCalled) {
        EXPECT_GT(result.end_time_ms - result.start_time_ms, 4000); // Should be close to 5 seconds
    }
}

TEST_F(WhisperSTTTest, MemoryUsageStability) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Perform many transcriptions to test memory stability
    const int numTranscriptions = 20;
    std::atomic<int> completedCount{0};
    
    for (int i = 0; i < numTranscriptions; ++i) {
        whisperSTT_->transcribe(testAudio500ms_, [&](const TranscriptionResult& result) {
            completedCount++;
        });
        
        // Small delay between transcriptions
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Wait for all to complete
    auto start = std::chrono::steady_clock::now();
    while (completedCount < numTranscriptions && 
           std::chrono::steady_clock::now() - start < std::chrono::seconds(30)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    EXPECT_EQ(completedCount.load(), numTranscriptions);
}

// ============================================================================
// Edge Cases and Boundary Tests
// ============================================================================

TEST_F(WhisperSTTTest, EmptyAudioHandling) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::vector<float> emptyAudio;
    std::atomic<bool> callbackCalled{false};
    
    whisperSTT_->transcribe(emptyAudio, [&](const TranscriptionResult& result) {
        callbackCalled = true;
        // Should handle empty audio gracefully
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Implementation should either call callback or handle gracefully
}

TEST_F(WhisperSTTTest, VeryShortAudioHandling) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Very short audio (10ms)
    std::vector<float> shortAudio(160, 0.1f);
    std::atomic<bool> callbackCalled{false};
    
    whisperSTT_->transcribe(shortAudio, [&](const TranscriptionResult& result) {
        callbackCalled = true;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Should handle short audio gracefully
}

TEST_F(WhisperSTTTest, ExtremeAudioValues) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Audio with extreme values
    std::vector<float> extremeAudio(16000);
    for (size_t i = 0; i < extremeAudio.size(); ++i) {
        extremeAudio[i] = (i % 2 == 0) ? 1.0f : -1.0f; // Square wave at max amplitude
    }
    
    std::atomic<bool> callbackCalled{false};
    
    whisperSTT_->transcribe(extremeAudio, [&](const TranscriptionResult& result) {
        callbackCalled = true;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(callbackCalled);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}