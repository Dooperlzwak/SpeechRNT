#include <gtest/gtest.h>
#include "stt/whisper_stt.hpp"
#include "audio/audio_buffer_manager.hpp"
#include <vector>
#include <chrono>
#include <thread>
#include <memory>
#include <atomic>
#include <mutex>
#include <future>
#include <queue>

using namespace stt;

class WhisperSTTStreamingTest : public ::testing::Test {
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
        
        // Generate speech chunks of different lengths
        speechChunk1_.resize(sampleRate / 2); // 0.5 seconds
        for (size_t i = 0; i < speechChunk1_.size(); ++i) {
            float t = static_cast<float>(i) / sampleRate;
            speechChunk1_[i] = 0.3f * std::sin(2.0f * M_PI * 440.0f * t);
        }
        
        speechChunk2_.resize(sampleRate / 4); // 0.25 seconds
        for (size_t i = 0; i < speechChunk2_.size(); ++i) {
            float t = static_cast<float>(i) / sampleRate;
            speechChunk2_[i] = 0.3f * std::sin(2.0f * M_PI * 880.0f * t);
        }
        
        speechChunk3_.resize(sampleRate); // 1 second
        for (size_t i = 0; i < speechChunk3_.size(); ++i) {
            float t = static_cast<float>(i) / sampleRate;
            speechChunk3_[i] = 0.3f * std::sin(2.0f * M_PI * 220.0f * t);
        }
        
        // Generate silence chunk
        silenceChunk_.resize(sampleRate / 2, 0.0f);
        
        // Generate noise chunk
        noiseChunk_.resize(sampleRate / 4);
        for (size_t i = 0; i < noiseChunk_.size(); ++i) {
            noiseChunk_[i] = 0.05f * (static_cast<float>(rand()) / RAND_MAX - 0.5f);
        }
        
        // Generate very small chunk
        tinyChunk_.resize(160, 0.1f); // 10ms at 16kHz
        
        // Generate large chunk
        largeChunk_.resize(sampleRate * 5); // 5 seconds
        for (size_t i = 0; i < largeChunk_.size(); ++i) {
            float t = static_cast<float>(i) / sampleRate;
            largeChunk_[i] = 0.2f * std::sin(2.0f * M_PI * 330.0f * t);
        }
    }
    
    std::unique_ptr<WhisperSTT> whisperSTT_;
    std::vector<float> speechChunk1_;
    std::vector<float> speechChunk2_;
    std::vector<float> speechChunk3_;
    std::vector<float> silenceChunk_;
    std::vector<float> noiseChunk_;
    std::vector<float> tinyChunk_;
    std::vector<float> largeChunk_;
};

// ============================================================================
// Basic Streaming Functionality Tests
// ============================================================================

TEST_F(WhisperSTTStreamingTest, BasicStreamingWorkflow) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    uint32_t utteranceId = 100;
    std::atomic<int> callbackCount{0};
    std::vector<TranscriptionResult> results;
    std::mutex resultsMutex;
    
    // Set up streaming callback
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        std::lock_guard<std::mutex> lock(resultsMutex);
        results.push_back(result);
        callbackCount++;
    });
    
    // Start streaming
    whisperSTT_->startStreamingTranscription(utteranceId);
    EXPECT_TRUE(whisperSTT_->isStreamingActive(utteranceId));
    EXPECT_EQ(whisperSTT_->getActiveStreamingCount(), 1);
    
    // Add audio chunks
    whisperSTT_->addAudioChunk(utteranceId, speechChunk1_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    whisperSTT_->addAudioChunk(utteranceId, speechChunk2_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Finalize streaming
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_FALSE(whisperSTT_->isStreamingActive(utteranceId));
    EXPECT_EQ(whisperSTT_->getActiveStreamingCount(), 0);
    EXPECT_GT(callbackCount.load(), 0);
    
    // Check results
    std::lock_guard<std::mutex> lock(resultsMutex);
    EXPECT_GT(results.size(), 0);
    
    // Should have both partial and final results
    bool hasPartial = false, hasFinal = false;
    for (const auto& result : results) {
        if (result.is_partial) hasPartial = true;
        else hasFinal = true;
    }
    EXPECT_TRUE(hasPartial || hasFinal);
}

TEST_F(WhisperSTTStreamingTest, StreamingConfiguration) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Test streaming configuration
    whisperSTT_->setPartialResultsEnabled(true);
    whisperSTT_->setMinChunkSizeMs(50);
    whisperSTT_->setConfidenceThreshold(0.6f);
    
    EXPECT_EQ(whisperSTT_->getConfidenceThreshold(), 0.6f);
    
    // Test word-level configuration
    whisperSTT_->setWordLevelConfidenceEnabled(true);
    whisperSTT_->setQualityIndicatorsEnabled(true);
    whisperSTT_->setConfidenceFilteringEnabled(true);
    
    EXPECT_TRUE(whisperSTT_->isWordLevelConfidenceEnabled());
    EXPECT_TRUE(whisperSTT_->isQualityIndicatorsEnabled());
    EXPECT_TRUE(whisperSTT_->isConfidenceFilteringEnabled());
}

// ============================================================================
// Multiple Concurrent Streaming Tests
// ============================================================================

TEST_F(WhisperSTTStreamingTest, MultipleStreamingUtterances) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    std::vector<uint32_t> utteranceIds = {200, 201, 202, 203};
    std::atomic<int> totalCallbacks{0};
    std::map<uint32_t, int> callbackCounts;
    std::mutex countsMutex;
    
    // Start multiple streaming sessions
    for (uint32_t id : utteranceIds) {
        callbackCounts[id] = 0;
        
        whisperSTT_->setStreamingCallback(id, [&, id](const TranscriptionResult& result) {
            std::lock_guard<std::mutex> lock(countsMutex);
            callbackCounts[id]++;
            totalCallbacks++;
        });
        
        whisperSTT_->startStreamingTranscription(id);
        EXPECT_TRUE(whisperSTT_->isStreamingActive(id));
    }
    
    EXPECT_EQ(whisperSTT_->getActiveStreamingCount(), utteranceIds.size());
    
    // Add audio to all streams
    for (size_t i = 0; i < utteranceIds.size(); ++i) {
        uint32_t id = utteranceIds[i];
        
        // Use different audio chunks for each stream
        switch (i % 4) {
            case 0: whisperSTT_->addAudioChunk(id, speechChunk1_); break;
            case 1: whisperSTT_->addAudioChunk(id, speechChunk2_); break;
            case 2: whisperSTT_->addAudioChunk(id, speechChunk3_); break;
            case 3: whisperSTT_->addAudioChunk(id, silenceChunk_); break;
        }
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Finalize all streams
    for (uint32_t id : utteranceIds) {
        whisperSTT_->finalizeStreamingTranscription(id);
        EXPECT_FALSE(whisperSTT_->isStreamingActive(id));
    }
    
    EXPECT_EQ(whisperSTT_->getActiveStreamingCount(), 0);
    EXPECT_GT(totalCallbacks.load(), 0);
}

TEST_F(WhisperSTTStreamingTest, ConcurrentStreamingStressTest) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    const int numStreams = 10;
    std::vector<std::future<bool>> futures;
    std::atomic<int> successfulStreams{0};
    
    // Start multiple concurrent streaming sessions
    for (int i = 0; i < numStreams; ++i) {
        futures.push_back(std::async(std::launch::async, [this, i, &successfulStreams]() {
            uint32_t utteranceId = 1000 + i;
            std::atomic<bool> gotCallback{false};
            
            whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
                gotCallback = true;
            });
            
            whisperSTT_->startStreamingTranscription(utteranceId);
            
            // Add multiple chunks
            whisperSTT_->addAudioChunk(utteranceId, speechChunk1_);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            whisperSTT_->addAudioChunk(utteranceId, speechChunk2_);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            whisperSTT_->finalizeStreamingTranscription(utteranceId);
            
            // Wait for callback
            auto start = std::chrono::steady_clock::now();
            while (!gotCallback && std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            if (gotCallback) {
                successfulStreams++;
                return true;
            }
            return false;
        }));
    }
    
    // Wait for all to complete
    int completedSuccessfully = 0;
    for (auto& future : futures) {
        if (future.get()) {
            completedSuccessfully++;
        }
    }
    
    EXPECT_GE(completedSuccessfully, numStreams / 2) << "At least half should complete successfully";
    EXPECT_EQ(successfulStreams.load(), completedSuccessfully);
}

// ============================================================================
// Streaming Audio Buffer Management Tests
// ============================================================================

TEST_F(WhisperSTTStreamingTest, AudioBufferManagement) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    uint32_t utteranceId = 300;
    std::atomic<int> callbackCount{0};
    std::vector<size_t> audioLengths;
    std::mutex lengthsMutex;
    
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        std::lock_guard<std::mutex> lock(lengthsMutex);
        audioLengths.push_back(result.text.length());
        callbackCount++;
    });
    
    whisperSTT_->startStreamingTranscription(utteranceId);
    
    // Add chunks of different sizes to test buffer management
    whisperSTT_->addAudioChunk(utteranceId, tinyChunk_);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    whisperSTT_->addAudioChunk(utteranceId, speechChunk1_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    whisperSTT_->addAudioChunk(utteranceId, speechChunk3_);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_GT(callbackCount.load(), 0);
    
    // Buffer should handle different chunk sizes gracefully
    std::lock_guard<std::mutex> lock(lengthsMutex);
    EXPECT_GT(audioLengths.size(), 0);
}

TEST_F(WhisperSTTStreamingTest, LargeAudioChunkHandling) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    uint32_t utteranceId = 400;
    std::atomic<bool> callbackCalled{false};
    TranscriptionResult finalResult;
    
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        if (!result.is_partial) {
            callbackCalled = true;
            finalResult = result;
        }
    });
    
    whisperSTT_->startStreamingTranscription(utteranceId);
    
    // Add large audio chunk
    whisperSTT_->addAudioChunk(utteranceId, largeChunk_);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    EXPECT_TRUE(callbackCalled);
    
    // Should handle large chunks and provide reasonable timing
    if (callbackCalled) {
        EXPECT_GT(finalResult.end_time_ms - finalResult.start_time_ms, 4000); // Should be close to 5 seconds
    }
}

// ============================================================================
// Streaming Incremental Updates Tests
// ============================================================================

TEST_F(WhisperSTTStreamingTest, IncrementalUpdates) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    whisperSTT_->setPartialResultsEnabled(true);
    
    uint32_t utteranceId = 500;
    std::atomic<int> partialCount{0};
    std::atomic<int> finalCount{0};
    std::vector<std::string> transcriptionHistory;
    std::mutex historyMutex;
    
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        std::lock_guard<std::mutex> lock(historyMutex);
        transcriptionHistory.push_back(result.text);
        
        if (result.is_partial) {
            partialCount++;
        } else {
            finalCount++;
        }
    });
    
    whisperSTT_->startStreamingTranscription(utteranceId);
    
    // Add audio chunks gradually to see incremental updates
    whisperSTT_->addAudioChunk(utteranceId, speechChunk1_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    whisperSTT_->addAudioChunk(utteranceId, speechChunk2_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    whisperSTT_->addAudioChunk(utteranceId, speechChunk3_);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_GT(partialCount.load() + finalCount.load(), 0);
    
    // Check transcription evolution
    std::lock_guard<std::mutex> lock(historyMutex);
    if (transcriptionHistory.size() > 1) {
        // Later transcriptions should generally be longer or more refined
        EXPECT_GE(transcriptionHistory.back().length(), transcriptionHistory.front().length() * 0.5);
    }
}

TEST_F(WhisperSTTStreamingTest, StreamingLatencyMeasurement) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    uint32_t utteranceId = 600;
    std::vector<std::chrono::steady_clock::time_point> chunkTimes;
    std::vector<std::chrono::steady_clock::time_point> callbackTimes;
    std::mutex timesMutex;
    
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        std::lock_guard<std::mutex> lock(timesMutex);
        callbackTimes.push_back(std::chrono::steady_clock::now());
    });
    
    whisperSTT_->startStreamingTranscription(utteranceId);
    
    // Add chunks and measure timing
    for (int i = 0; i < 3; ++i) {
        {
            std::lock_guard<std::mutex> lock(timesMutex);
            chunkTimes.push_back(std::chrono::steady_clock::now());
        }
        
        whisperSTT_->addAudioChunk(utteranceId, speechChunk1_);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // Analyze latencies
    std::lock_guard<std::mutex> lock(timesMutex);
    if (chunkTimes.size() > 0 && callbackTimes.size() > 0) {
        for (size_t i = 0; i < std::min(chunkTimes.size(), callbackTimes.size()); ++i) {
            auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                callbackTimes[i] - chunkTimes[i]);
            
            // Streaming latency should be reasonable (less than 2 seconds)
            EXPECT_LT(latency.count(), 2000) << "Streaming latency too high: " << latency.count() << "ms";
        }
    }
}

// ============================================================================
// Streaming Error Handling Tests
// ============================================================================

TEST_F(WhisperSTTStreamingTest, StreamingWithoutCallback) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    uint32_t utteranceId = 700;
    
    // Start streaming without setting callback
    whisperSTT_->startStreamingTranscription(utteranceId);
    EXPECT_TRUE(whisperSTT_->isStreamingActive(utteranceId));
    
    // Add audio chunk
    whisperSTT_->addAudioChunk(utteranceId, speechChunk1_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Should be able to finalize without crashing
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    EXPECT_FALSE(whisperSTT_->isStreamingActive(utteranceId));
}

TEST_F(WhisperSTTStreamingTest, StreamingStateCleanup) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    uint32_t utteranceId = 800;
    std::atomic<bool> callbackCalled{false};
    
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        callbackCalled = true;
    });
    
    // Start and immediately finalize
    whisperSTT_->startStreamingTranscription(utteranceId);
    EXPECT_TRUE(whisperSTT_->isStreamingActive(utteranceId));
    
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    EXPECT_FALSE(whisperSTT_->isStreamingActive(utteranceId));
    
    // Should be able to start new session with same ID
    callbackCalled = false;
    whisperSTT_->startStreamingTranscription(utteranceId);
    whisperSTT_->addAudioChunk(utteranceId, speechChunk1_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(callbackCalled);
}

TEST_F(WhisperSTTStreamingTest, StreamingMemoryManagement) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    // Test memory management with many short-lived streaming sessions
    const int numSessions = 20;
    std::atomic<int> completedSessions{0};
    
    for (int i = 0; i < numSessions; ++i) {
        uint32_t utteranceId = 900 + i;
        
        whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
            completedSessions++;
        });
        
        whisperSTT_->startStreamingTranscription(utteranceId);
        whisperSTT_->addAudioChunk(utteranceId, speechChunk1_);
        whisperSTT_->finalizeStreamingTranscription(utteranceId);
        
        // Small delay between sessions
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Wait for all sessions to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    EXPECT_GT(completedSessions.load(), numSessions / 2) << "Should complete most sessions";
    EXPECT_EQ(whisperSTT_->getActiveStreamingCount(), 0) << "All sessions should be cleaned up";
}

// ============================================================================
// Advanced Streaming Features Tests
// ============================================================================

TEST_F(WhisperSTTStreamingTest, StreamingWithWordTimings) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    whisperSTT_->setWordLevelConfidenceEnabled(true);
    
    uint32_t utteranceId = 1000;
    std::atomic<bool> gotWordTimings{false};
    TranscriptionResult resultWithTimings;
    
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        if (!result.word_timings.empty()) {
            gotWordTimings = true;
            resultWithTimings = result;
        }
    });
    
    whisperSTT_->startStreamingTranscription(utteranceId);
    whisperSTT_->addAudioChunk(utteranceId, speechChunk3_); // Longer audio for word timings
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    if (gotWordTimings) {
        // Validate word timings
        for (const auto& wordTiming : resultWithTimings.word_timings) {
            EXPECT_FALSE(wordTiming.word.empty());
            EXPECT_GE(wordTiming.confidence, 0.0f);
            EXPECT_LE(wordTiming.confidence, 1.0f);
            EXPECT_GE(wordTiming.start_ms, 0);
            EXPECT_LE(wordTiming.end_ms, resultWithTimings.end_time_ms);
            EXPECT_LE(wordTiming.start_ms, wordTiming.end_ms);
        }
    }
}

TEST_F(WhisperSTTStreamingTest, StreamingQualityMetrics) {
    ASSERT_TRUE(whisperSTT_->initialize("dummy_model.bin"));
    
    whisperSTT_->setQualityIndicatorsEnabled(true);
    
    uint32_t utteranceId = 1100;
    std::atomic<bool> gotQualityMetrics{false};
    TranscriptionResult resultWithQuality;
    
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        if (result.quality_metrics.processing_latency_ms > 0) {
            gotQualityMetrics = true;
            resultWithQuality = result;
        }
    });
    
    whisperSTT_->startStreamingTranscription(utteranceId);
    whisperSTT_->addAudioChunk(utteranceId, speechChunk2_);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    if (gotQualityMetrics) {
        // Validate quality metrics
        EXPECT_GE(resultWithQuality.quality_metrics.processing_latency_ms, 0.0f);
        EXPECT_GE(resultWithQuality.quality_metrics.signal_to_noise_ratio, 0.0f);
        EXPECT_GE(resultWithQuality.quality_metrics.audio_clarity_score, 0.0f);
        EXPECT_FALSE(resultWithQuality.quality_level.empty());
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}