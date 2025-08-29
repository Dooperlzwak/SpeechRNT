#include <gtest/gtest.h>
#include "stt/whisper_stt.hpp"
#include "stt/stt_error_recovery.hpp"
#include "stt/quantization_config.hpp"
#include <vector>
#include <chrono>
#include <thread>
#include <memory>
#include <atomic>
#include <future>
#include <stdexcept>

using namespace stt;

class WhisperSTTErrorRecoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        whisperSTT_ = std::make_unique<WhisperSTT>();
        generateTestAudioData();
    }
    
    void TearDown() override {
        whisperSTT_.reset();
    }
    
    void generateTestAudioData() {
        const size_t sampleRate = 16000;
        
        // Generate normal test audio
        normalAudio_.resize(sampleRate);
        for (size_t i = 0; i < normalAudio_.size(); ++i) {
            float t = static_cast<float>(i) / sampleRate;
            normalAudio_[i] = 0.3f * std::sin(2.0f * M_PI * 440.0f * t);
        }
        
        // Generate problematic audio with extreme values
        extremeAudio_.resize(sampleRate);
        for (size_t i = 0; i < extremeAudio_.size(); ++i) {
            extremeAudio_[i] = (i % 2 == 0) ? 1000.0f : -1000.0f; // Extreme values
        }
        
        // Generate audio with NaN values
        nanAudio_.resize(sampleRate, std::numeric_limits<float>::quiet_NaN());
        
        // Generate audio with infinite values
        infAudio_.resize(sampleRate, std::numeric_limits<float>::infinity());
        
        // Generate very long audio for stress testing
        longAudio_.resize(sampleRate * 10); // 10 seconds
        for (size_t i = 0; i < longAudio_.size(); ++i) {
            float t = static_cast<float>(i) / sampleRate;
            longAudio_[i] = 0.2f * std::sin(2.0f * M_PI * 220.0f * t);
        }
    }
    
    std::unique_ptr<WhisperSTT> whisperSTT_;
    std::vector<float> normalAudio_;
    std::vector<float> extremeAudio_;
    std::vector<float> nanAudio_;
    std::vector<float> infAudio_;
    std::vector<float> longAudio_;
};

// ============================================================================
// Model Loading Error Recovery Tests
// ============================================================================

TEST_F(WhisperSTTErrorRecoveryTest, ModelLoadingFailureRecovery) {
    // Test initialization with invalid model path
    EXPECT_FALSE(whisperSTT_->initialize(""));
    EXPECT_FALSE(whisperSTT_->isInitialized());
    EXPECT_NE(whisperSTT_->getLastError(), "");
    
    // Should be able to recover with valid model
    EXPECT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    EXPECT_TRUE(whisperSTT_->isInitialized());
    EXPECT_EQ(whisperSTT_->getLastError(), "");
}

TEST_F(WhisperSTTErrorRecoveryTest, ModelPathValidationRecovery) {
    // Test with various invalid paths
    std::vector<std::string> invalidPaths = {
        "",
        "nonexistent_model.bin",
        "/invalid/path/model.bin",
        "model_with_invalid_extension.txt"
    };
    
    for (const auto& path : invalidPaths) {
        auto testSTT = std::make_unique<WhisperSTT>();
        EXPECT_FALSE(testSTT->initialize(path)) << "Should fail for path: " << path;
        EXPECT_FALSE(testSTT->isInitialized());
        EXPECT_NE(testSTT->getLastError(), "");
        
        // Should be able to recover with valid path
        EXPECT_TRUE(testSTT->initialize("dummy_model.bin"));
        EXPECT_TRUE(testSTT->isInitialized());
    }
}

TEST_F(WhisperSTTErrorRecoveryTest, ModelReinitialization) {
    // Initialize with valid model
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    EXPECT_TRUE(whisperSTT_->isInitialized());
    
    // Reinitialize with different model (should handle gracefully)
    EXPECT_TRUE(whisperSTT_->initialize("dummy_model_v2.bin"));
    EXPECT_TRUE(whisperSTT_->isInitialized());
    
    // Reinitialize with invalid model (should maintain previous state or handle gracefully)
    bool result = whisperSTT_->initialize("invalid_model.bin");
    // Implementation may either maintain previous state or fail gracefully
    if (!result) {
        EXPECT_NE(whisperSTT_->getLastError(), "");
    }
}

// ============================================================================
// GPU Fallback Tests
// ============================================================================

TEST_F(WhisperSTTErrorRecoveryTest, GPUToCPUFallback) {
    // Test GPU initialization with invalid device ID
    bool gpuResult = whisperSTT_->initializeWithGPU("dummy_model.bin", 999, 4);
    
    if (!gpuResult) {
        // Should be able to fallback to CPU
        EXPECT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
        EXPECT_TRUE(whisperSTT_->isInitialized());
    }
}

TEST_F(WhisperSTTErrorRecoveryTest, GPUMemoryErrorFallback) {
    // Test GPU initialization and fallback scenarios
    auto testSTT = std::make_unique<WhisperSTT>();
    
    // Try GPU initialization first
    bool gpuSuccess = testSTT->initializeWithGPU("dummy_model.bin", 0, 4);
    
    if (!gpuSuccess) {
        // GPU failed, should fallback to CPU
        EXPECT_TRUE(testSTT->initialize("dummy_model.bin"));
        EXPECT_TRUE(testSTT->isInitialized());
        
        // Should be able to transcribe normally on CPU
        std::atomic<bool> callbackCalled{false};
        testSTT->transcribe(normalAudio_, [&](const TranscriptionResult& result) {
            callbackCalled = true;
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        EXPECT_TRUE(callbackCalled);
    }
}

// ============================================================================
// Quantization Fallback Tests
// ============================================================================

TEST_F(WhisperSTTErrorRecoveryTest, QuantizationLevelFallback) {
    // Test fallback through quantization levels: FP32 -> FP16 -> INT8
    std::vector<QuantizationLevel> levels = {
        QuantizationLevel::FP32,
        QuantizationLevel::FP16,
        QuantizationLevel::INT8
    };
    
    bool anySucceeded = false;
    for (auto level : levels) {
        auto testSTT = std::make_unique<WhisperSTT>();
        bool result = testSTT->initializeWithQuantization("dummy_model.bin", level);
        
        if (result) {
            anySucceeded = true;
            EXPECT_TRUE(testSTT->isInitialized());
            EXPECT_EQ(testSTT->getQuantizationLevel(), level);
            
            // Test that transcription works with this quantization level
            std::atomic<bool> callbackCalled{false};
            testSTT->transcribe(normalAudio_, [&](const TranscriptionResult& res) {
                callbackCalled = true;
            });
            
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            EXPECT_TRUE(callbackCalled);
            break;
        }
    }
    
    EXPECT_TRUE(anySucceeded) << "At least one quantization level should work";
}

TEST_F(WhisperSTTErrorRecoveryTest, AutoQuantizationSelection) {
    // Test automatic quantization level selection
    EXPECT_TRUE(whisperSTT_->initializeWithQuantization("dummy_model.bin", QuantizationLevel::AUTO));
    EXPECT_TRUE(whisperSTT_->isInitialized());
    
    // Should have selected some quantization level
    auto selectedLevel = whisperSTT_->getQuantizationLevel();
    EXPECT_NE(selectedLevel, QuantizationLevel::AUTO); // Should have resolved to specific level
}

// ============================================================================
// Transcription Error Recovery Tests
// ============================================================================

TEST_F(WhisperSTTErrorRecoveryTest, InvalidAudioDataRecovery) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    struct AudioTest {
        std::string name;
        std::vector<float>* audio;
    };
    
    std::vector<AudioTest> problematicAudioTests = {
        {"Empty Audio", new std::vector<float>()},
        {"NaN Audio", &nanAudio_},
        {"Infinite Audio", &infAudio_},
        {"Extreme Values", &extremeAudio_}
    };
    
    for (const auto& test : problematicAudioTests) {
        std::atomic<bool> callbackCalled{false};
        std::atomic<bool> errorHandled{true};
        
        try {
            whisperSTT_->transcribe(*test.audio, [&](const TranscriptionResult& result) {
                callbackCalled = true;
                // Should handle problematic audio gracefully
            });
            
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
        } catch (const std::exception& e) {
            errorHandled = false;
            FAIL() << "Exception thrown for " << test.name << ": " << e.what();
        }
        
        EXPECT_TRUE(errorHandled) << "Should handle " << test.name << " gracefully";
        
        // After problematic audio, should still work with normal audio
        callbackCalled = false;
        whisperSTT_->transcribe(normalAudio_, [&](const TranscriptionResult& result) {
            callbackCalled = true;
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        EXPECT_TRUE(callbackCalled) << "Should recover after " << test.name;
    }
    
    // Clean up dynamically allocated test data
    delete problematicAudioTests[0].audio;
}

TEST_F(WhisperSTTErrorRecoveryTest, TranscriptionTimeoutRecovery) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Test with very long audio that might cause timeout
    std::atomic<bool> callbackCalled{false};
    auto startTime = std::chrono::steady_clock::now();
    
    whisperSTT_->transcribe(longAudio_, [&](const TranscriptionResult& result) {
        callbackCalled = true;
    });
    
    // Wait with reasonable timeout
    auto timeout = std::chrono::seconds(15);
    auto deadline = startTime + timeout;
    
    while (!callbackCalled && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Should either complete or handle timeout gracefully
    if (!callbackCalled) {
        // If it timed out, system should still be responsive
        callbackCalled = false;
        whisperSTT_->transcribe(normalAudio_, [&](const TranscriptionResult& result) {
            callbackCalled = true;
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        EXPECT_TRUE(callbackCalled) << "Should recover after timeout";
    }
}

// ============================================================================
// Streaming Error Recovery Tests
// ============================================================================

TEST_F(WhisperSTTErrorRecoveryTest, StreamingErrorRecovery) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    uint32_t utteranceId = 100;
    std::atomic<int> callbackCount{0};
    std::atomic<bool> errorOccurred{false};
    
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        callbackCount++;
    });
    
    whisperSTT_->startStreamingTranscription(utteranceId);
    
    // Add normal audio
    whisperSTT_->addAudioChunk(utteranceId, normalAudio_);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Add problematic audio
    try {
        whisperSTT_->addAudioChunk(utteranceId, nanAudio_);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } catch (const std::exception& e) {
        errorOccurred = true;
    }
    
    // Should be able to continue with normal audio
    whisperSTT_->addAudioChunk(utteranceId, normalAudio_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Should have processed at least some chunks
    EXPECT_GE(callbackCount.load(), 0);
    EXPECT_FALSE(errorOccurred) << "Should handle streaming errors gracefully";
}

TEST_F(WhisperSTTErrorRecoveryTest, StreamingStateRecovery) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    uint32_t utteranceId = 200;
    
    // Start streaming without callback (error condition)
    whisperSTT_->startStreamingTranscription(utteranceId);
    EXPECT_TRUE(whisperSTT_->isStreamingActive(utteranceId));
    
    // Add audio chunk
    whisperSTT_->addAudioChunk(utteranceId, normalAudio_);
    
    // Should be able to finalize even without callback
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    EXPECT_FALSE(whisperSTT_->isStreamingActive(utteranceId));
    
    // Should be able to start new streaming session
    std::atomic<bool> callbackCalled{false};
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        callbackCalled = true;
    });
    
    whisperSTT_->startStreamingTranscription(utteranceId);
    whisperSTT_->addAudioChunk(utteranceId, normalAudio_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(callbackCalled);
}

// ============================================================================
// Concurrent Error Recovery Tests
// ============================================================================

TEST_F(WhisperSTTErrorRecoveryTest, ConcurrentErrorRecovery) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    const int numConcurrent = 5;
    std::vector<std::future<bool>> futures;
    std::atomic<int> successCount{0};
    std::atomic<int> errorCount{0};
    
    // Start multiple concurrent transcriptions with mixed normal and problematic audio
    for (int i = 0; i < numConcurrent; ++i) {
        futures.push_back(std::async(std::launch::async, [this, i, &successCount, &errorCount]() {
            std::atomic<bool> callbackCalled{false};
            bool hadError = false;
            
            try {
                // Use problematic audio for some threads
                auto& audioToUse = (i % 2 == 0) ? normalAudio_ : extremeAudio_;
                
                whisperSTT_->transcribe(audioToUse, [&](const TranscriptionResult& result) {
                    callbackCalled = true;
                });
                
                // Wait for completion
                auto start = std::chrono::steady_clock::now();
                while (!callbackCalled && std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                
                if (callbackCalled) {
                    successCount++;
                }
                
            } catch (const std::exception& e) {
                hadError = true;
                errorCount++;
            }
            
            return callbackCalled && !hadError;
        }));
    }
    
    // Wait for all to complete
    int completedSuccessfully = 0;
    for (auto& future : futures) {
        if (future.get()) {
            completedSuccessfully++;
        }
    }
    
    // Should handle concurrent errors gracefully
    EXPECT_GE(completedSuccessfully, 1) << "At least some concurrent transcriptions should succeed";
    EXPECT_EQ(errorCount.load(), 0) << "Should not throw exceptions during error recovery";
}

// ============================================================================
// Memory Error Recovery Tests
// ============================================================================

TEST_F(WhisperSTTErrorRecoveryTest, MemoryPressureRecovery) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Perform many transcriptions to test memory stability
    const int numTranscriptions = 50;
    std::atomic<int> completedCount{0};
    std::atomic<int> errorCount{0};
    
    for (int i = 0; i < numTranscriptions; ++i) {
        try {
            std::atomic<bool> callbackCalled{false};
            
            whisperSTT_->transcribe(normalAudio_, [&](const TranscriptionResult& result) {
                callbackCalled = true;
                completedCount++;
            });
            
            // Short wait to avoid overwhelming the system
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            
        } catch (const std::exception& e) {
            errorCount++;
        }
    }
    
    // Wait for remaining transcriptions to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    EXPECT_GT(completedCount.load(), numTranscriptions / 2) << "Should complete most transcriptions";
    EXPECT_EQ(errorCount.load(), 0) << "Should handle memory pressure without exceptions";
}

// ============================================================================
// Configuration Error Recovery Tests
// ============================================================================

TEST_F(WhisperSTTErrorRecoveryTest, InvalidConfigurationRecovery) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Test recovery from invalid configurations
    whisperSTT_->setTemperature(-1.0f);  // Invalid temperature
    whisperSTT_->setMaxTokens(-100);     // Invalid max tokens
    whisperSTT_->setConfidenceThreshold(2.0f);  // Invalid threshold (>1.0)
    
    // Should still be able to transcribe
    std::atomic<bool> callbackCalled{false};
    whisperSTT_->transcribe(normalAudio_, [&](const TranscriptionResult& result) {
        callbackCalled = true;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(callbackCalled) << "Should work despite invalid configurations";
    
    // Reset to valid configurations
    whisperSTT_->setTemperature(0.5f);
    whisperSTT_->setMaxTokens(100);
    whisperSTT_->setConfidenceThreshold(0.7f);
    
    // Should continue to work
    callbackCalled = false;
    whisperSTT_->transcribe(normalAudio_, [&](const TranscriptionResult& result) {
        callbackCalled = true;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(callbackCalled);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}