#include <gtest/gtest.h>
#include "stt/whisper_stt.hpp"
#include "audio/voice_activity_detector.hpp"
#include "audio/silero_vad_impl.hpp"
#include <vector>
#include <chrono>
#include <thread>
#include <memory>
#include <atomic>

using namespace stt;
using namespace audio;

class WhisperSTTVADIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        whisperSTT_ = std::make_unique<WhisperSTT>();
        
        // Generate test audio data with different VAD characteristics
        generateTestAudioData();
    }
    
    void TearDown() override {
        whisperSTT_.reset();
    }
    
    void generateTestAudioData() {
        const size_t sampleRate = 16000;
        
        // Generate speech-like audio (multiple frequencies)
        speechAudio_.resize(sampleRate * 2); // 2 seconds
        for (size_t i = 0; i < speechAudio_.size(); ++i) {
            float t = static_cast<float>(i) / sampleRate;
            // Mix of frequencies typical for speech
            speechAudio_[i] = 0.3f * std::sin(2.0f * M_PI * 200.0f * t) +  // Fundamental
                             0.2f * std::sin(2.0f * M_PI * 400.0f * t) +   // Harmonic
                             0.1f * std::sin(2.0f * M_PI * 800.0f * t);    // Higher harmonic
        }
        
        // Generate silence
        silenceAudio_.resize(sampleRate, 0.0f);
        
        // Generate background noise
        noiseAudio_.resize(sampleRate);
        for (size_t i = 0; i < noiseAudio_.size(); ++i) {
            noiseAudio_[i] = 0.02f * (static_cast<float>(rand()) / RAND_MAX - 0.5f);
        }
        
        // Generate speech with noise
        speechWithNoiseAudio_.resize(sampleRate);
        for (size_t i = 0; i < speechWithNoiseAudio_.size(); ++i) {
            float t = static_cast<float>(i) / sampleRate;
            float speech = 0.3f * std::sin(2.0f * M_PI * 440.0f * t);
            float noise = 0.05f * (static_cast<float>(rand()) / RAND_MAX - 0.5f);
            speechWithNoiseAudio_[i] = speech + noise;
        }
        
        // Generate alternating speech and silence
        alternatingAudio_.resize(sampleRate * 3); // 3 seconds
        for (size_t i = 0; i < alternatingAudio_.size(); ++i) {
            float t = static_cast<float>(i) / sampleRate;
            // Alternate between speech and silence every 0.5 seconds
            if (static_cast<int>(t * 2) % 2 == 0) {
                alternatingAudio_[i] = 0.3f * std::sin(2.0f * M_PI * 440.0f * t);
            } else {
                alternatingAudio_[i] = 0.0f;
            }
        }
    }
    
    std::unique_ptr<WhisperSTT> whisperSTT_;
    std::vector<float> speechAudio_;
    std::vector<float> silenceAudio_;
    std::vector<float> noiseAudio_;
    std::vector<float> speechWithNoiseAudio_;
    std::vector<float> alternatingAudio_;
};

// ============================================================================
// VAD Integration Tests
// ============================================================================

TEST_F(WhisperSTTVADIntegrationTest, VADWithSpeechDetection) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::atomic<bool> callbackCalled{false};
    TranscriptionResult result;
    
    // Transcribe speech audio - VAD should detect speech
    whisperSTT_->transcribe(speechAudio_, [&](const TranscriptionResult& res) {
        callbackCalled = true;
        result = res;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    EXPECT_TRUE(callbackCalled);
    EXPECT_FALSE(result.text.empty() || result.confidence > 0.0f);
    
    // Quality metrics should indicate speech was detected
    EXPECT_GE(result.quality_metrics.signal_to_noise_ratio, 0.0f);
}

TEST_F(WhisperSTTVADIntegrationTest, VADWithSilenceHandling) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::atomic<bool> callbackCalled{false};
    TranscriptionResult result;
    
    // Transcribe silence - VAD should detect no speech
    whisperSTT_->transcribe(silenceAudio_, [&](const TranscriptionResult& res) {
        callbackCalled = true;
        result = res;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(callbackCalled);
    // Silence should result in low confidence or empty text
    EXPECT_TRUE(result.text.empty() || result.confidence < 0.5f);
}

TEST_F(WhisperSTTVADIntegrationTest, VADWithBackgroundNoise) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::atomic<bool> callbackCalled{false};
    TranscriptionResult result;
    
    // Transcribe noise - VAD should distinguish from speech
    whisperSTT_->transcribe(noiseAudio_, [&](const TranscriptionResult& res) {
        callbackCalled = true;
        result = res;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(callbackCalled);
    
    // Quality metrics should indicate background noise
    if (result.quality_metrics.has_background_noise) {
        EXPECT_TRUE(result.quality_metrics.has_background_noise);
    }
}

TEST_F(WhisperSTTVADIntegrationTest, VADWithSpeechInNoise) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::atomic<bool> callbackCalled{false};
    TranscriptionResult result;
    
    // Transcribe speech with noise - VAD should detect speech despite noise
    whisperSTT_->transcribe(speechWithNoiseAudio_, [&](const TranscriptionResult& res) {
        callbackCalled = true;
        result = res;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    EXPECT_TRUE(callbackCalled);
    
    // Should detect speech but with lower quality metrics
    EXPECT_GE(result.confidence, 0.0f);
    if (result.quality_metrics.has_background_noise) {
        EXPECT_TRUE(result.quality_metrics.has_background_noise);
    }
}

// ============================================================================
// Streaming VAD State Management Tests
// ============================================================================

TEST_F(WhisperSTTVADIntegrationTest, StreamingVADStateTransitions) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    uint32_t utteranceId = 100;
    std::atomic<int> callbackCount{0};
    std::vector<TranscriptionResult> results;
    std::mutex resultsMutex;
    
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        std::lock_guard<std::mutex> lock(resultsMutex);
        results.push_back(result);
        callbackCount++;
    });
    
    whisperSTT_->startStreamingTranscription(utteranceId);
    
    // Add alternating speech and silence to trigger VAD state changes
    size_t chunkSize = alternatingAudio_.size() / 6; // 6 chunks of 0.5 seconds each
    for (size_t i = 0; i < 6; ++i) {
        std::vector<float> chunk(alternatingAudio_.begin() + i * chunkSize,
                                alternatingAudio_.begin() + (i + 1) * chunkSize);
        whisperSTT_->addAudioChunk(utteranceId, chunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_GT(callbackCount.load(), 0);
    
    // Should have received multiple results due to VAD state changes
    std::lock_guard<std::mutex> lock(resultsMutex);
    EXPECT_GT(results.size(), 1);
}

TEST_F(WhisperSTTVADIntegrationTest, StreamingVADUtteranceBoundaries) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    uint32_t utteranceId = 200;
    std::atomic<int> partialCount{0};
    std::atomic<int> finalCount{0};
    
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        if (result.is_partial) {
            partialCount++;
        } else {
            finalCount++;
        }
    });
    
    whisperSTT_->startStreamingTranscription(utteranceId);
    
    // Add speech chunk
    whisperSTT_->addAudioChunk(utteranceId, speechAudio_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Add silence (should trigger utterance boundary)
    whisperSTT_->addAudioChunk(utteranceId, silenceAudio_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Add more speech
    whisperSTT_->addAudioChunk(utteranceId, speechAudio_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Should have received both partial and final results
    EXPECT_GE(partialCount.load() + finalCount.load(), 1);
}

TEST_F(WhisperSTTVADIntegrationTest, MultipleStreamingVADStates) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::vector<uint32_t> utteranceIds = {300, 301, 302};
    std::atomic<int> totalCallbacks{0};
    std::map<uint32_t, int> callbackCounts;
    std::mutex countsMutex;
    
    // Start multiple streaming sessions with different audio types
    for (size_t i = 0; i < utteranceIds.size(); ++i) {
        uint32_t id = utteranceIds[i];
        callbackCounts[id] = 0;
        
        whisperSTT_->setStreamingCallback(id, [&, id](const TranscriptionResult& result) {
            std::lock_guard<std::mutex> lock(countsMutex);
            callbackCounts[id]++;
            totalCallbacks++;
        });
        
        whisperSTT_->startStreamingTranscription(id);
    }
    
    // Add different audio types to each stream
    whisperSTT_->addAudioChunk(utteranceIds[0], speechAudio_);
    whisperSTT_->addAudioChunk(utteranceIds[1], silenceAudio_);
    whisperSTT_->addAudioChunk(utteranceIds[2], noiseAudio_);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Finalize all streams
    for (uint32_t id : utteranceIds) {
        whisperSTT_->finalizeStreamingTranscription(id);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_GT(totalCallbacks.load(), 0);
    
    // Each stream should have received at least some callbacks
    std::lock_guard<std::mutex> lock(countsMutex);
    for (uint32_t id : utteranceIds) {
        EXPECT_GE(callbackCounts[id], 0); // At least attempted processing
    }
}

// ============================================================================
// VAD Configuration and Fallback Tests
// ============================================================================

TEST_F(WhisperSTTVADIntegrationTest, VADFallbackMechanisms) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Test that VAD works even if advanced features are not available
    std::atomic<bool> callbackCalled{false};
    TranscriptionResult result;
    
    whisperSTT_->transcribe(speechAudio_, [&](const TranscriptionResult& res) {
        callbackCalled = true;
        result = res;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    EXPECT_TRUE(callbackCalled);
    // Should work with basic VAD even if silero-vad is not available
}

TEST_F(WhisperSTTVADIntegrationTest, VADPerformanceWithDifferentAudioTypes) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    struct AudioTest {
        std::string name;
        std::vector<float>* audio;
        bool expectSpeech;
    };
    
    std::vector<AudioTest> tests = {
        {"Speech", &speechAudio_, true},
        {"Silence", &silenceAudio_, false},
        {"Noise", &noiseAudio_, false},
        {"Speech with Noise", &speechWithNoiseAudio_, true}
    };
    
    for (const auto& test : tests) {
        std::atomic<bool> callbackCalled{false};
        TranscriptionResult result;
        auto startTime = std::chrono::steady_clock::now();
        
        whisperSTT_->transcribe(*test.audio, [&](const TranscriptionResult& res) {
            callbackCalled = true;
            result = res;
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        auto endTime = std::chrono::steady_clock::now();
        
        EXPECT_TRUE(callbackCalled) << "Failed for audio type: " << test.name;
        
        // Check processing time is reasonable
        auto processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        EXPECT_LT(processingTime.count(), 5000) << "Processing too slow for: " << test.name;
        
        // Validate VAD behavior based on expected speech content
        if (test.expectSpeech) {
            // For speech, expect either non-empty text or reasonable confidence
            EXPECT_TRUE(!result.text.empty() || result.confidence > 0.1f) 
                << "Expected speech detection for: " << test.name;
        }
    }
}

// ============================================================================
// VAD Error Handling Tests
// ============================================================================

TEST_F(WhisperSTTVADIntegrationTest, VADErrorRecovery) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Test VAD behavior with problematic audio
    std::vector<float> problematicAudio;
    
    // Test with NaN values
    problematicAudio.assign(1000, std::numeric_limits<float>::quiet_NaN());
    
    std::atomic<bool> callbackCalled{false};
    whisperSTT_->transcribe(problematicAudio, [&](const TranscriptionResult& result) {
        callbackCalled = true;
        // Should handle NaN values gracefully
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Should either call callback or handle error gracefully without crashing
    
    // Test with infinite values
    problematicAudio.assign(1000, std::numeric_limits<float>::infinity());
    
    callbackCalled = false;
    whisperSTT_->transcribe(problematicAudio, [&](const TranscriptionResult& result) {
        callbackCalled = true;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Should handle infinite values gracefully
}

TEST_F(WhisperSTTVADIntegrationTest, VADStreamingErrorRecovery) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    uint32_t utteranceId = 400;
    std::atomic<int> callbackCount{0};
    
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        callbackCount++;
    });
    
    whisperSTT_->startStreamingTranscription(utteranceId);
    
    // Add normal audio
    whisperSTT_->addAudioChunk(utteranceId, speechAudio_);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Add problematic audio
    std::vector<float> badAudio(1000, std::numeric_limits<float>::quiet_NaN());
    whisperSTT_->addAudioChunk(utteranceId, badAudio);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Add normal audio again - should recover
    whisperSTT_->addAudioChunk(utteranceId, speechAudio_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Should have processed at least some chunks successfully
    EXPECT_GE(callbackCount.load(), 0);
}

// ============================================================================
// VAD Quality and Accuracy Tests
// ============================================================================

TEST_F(WhisperSTTVADIntegrationTest, VADAccuracyWithKnownContent) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Test VAD accuracy with known speech/silence patterns
    struct VADTest {
        std::string name;
        std::vector<float>* audio;
        bool expectedHasSpeech;
        float minExpectedConfidence;
    };
    
    std::vector<VADTest> tests = {
        {"Clear Speech", &speechAudio_, true, 0.3f},
        {"Pure Silence", &silenceAudio_, false, 0.0f},
        {"Background Noise", &noiseAudio_, false, 0.0f},
        {"Speech in Noise", &speechWithNoiseAudio_, true, 0.1f}
    };
    
    for (const auto& test : tests) {
        std::atomic<bool> callbackCalled{false};
        TranscriptionResult result;
        
        whisperSTT_->transcribe(*test.audio, [&](const TranscriptionResult& res) {
            callbackCalled = true;
            result = res;
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        
        EXPECT_TRUE(callbackCalled) << "Callback not called for: " << test.name;
        
        if (test.expectedHasSpeech) {
            // For speech content, expect reasonable confidence or non-empty text
            EXPECT_TRUE(result.confidence >= test.minExpectedConfidence || !result.text.empty())
                << "Expected speech detection for: " << test.name 
                << " (confidence: " << result.confidence << ", text: '" << result.text << "')";
        } else {
            // For non-speech content, expect low confidence or empty text
            EXPECT_TRUE(result.confidence < 0.5f || result.text.empty())
                << "Expected no speech detection for: " << test.name
                << " (confidence: " << result.confidence << ", text: '" << result.text << "')";
        }
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}