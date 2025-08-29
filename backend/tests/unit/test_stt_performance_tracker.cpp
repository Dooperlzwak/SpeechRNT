#include <gtest/gtest.h>
#include "stt/stt_performance_tracker.hpp"
#include "utils/performance_monitor.hpp"
#include <thread>
#include <chrono>

using namespace stt;
using namespace speechrnt::utils;

class STTPerformanceTrackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize performance monitor
        PerformanceMonitor::getInstance().initialize(false, 100); // Disable system metrics for testing
        
        // Create tracker
        tracker_ = std::make_unique<STTPerformanceTracker>();
        tracker_->initialize(true);
    }
    
    void TearDown() override {
        tracker_.reset();
        PerformanceMonitor::getInstance().clearMetrics();
    }
    
    std::unique_ptr<STTPerformanceTracker> tracker_;
};

TEST_F(STTPerformanceTrackerTest, BasicTranscriptionTracking) {
    uint32_t utteranceId = 1;
    bool isStreaming = false;
    
    // Start transcription
    uint64_t sessionId = tracker_->startTranscription(utteranceId, isStreaming);
    EXPECT_GT(sessionId, 0);
    
    // Record various stages
    tracker_->recordVADProcessing(sessionId, 50.0, 0.9f, true);
    tracker_->recordPreprocessing(sessionId, 25.0, 1000.0);
    tracker_->recordInference(sessionId, 200.0, "whisper-base", true);
    tracker_->recordPostprocessing(sessionId, 15.0, 100);
    
    // Record transcription result
    tracker_->recordTranscriptionResult(sessionId, 0.85f, false, 100, "en", 0.95f);
    
    // Complete transcription
    tracker_->completeTranscription(sessionId, true, 0.85f, 100);
    
    // Verify metrics were recorded
    auto summary = tracker_->getPerformanceSummary();
    EXPECT_EQ(summary["total_transcriptions"], 1.0);
    EXPECT_EQ(summary["successful_transcriptions"], 1.0);
    EXPECT_EQ(summary["success_rate"], 1.0);
}

TEST_F(STTPerformanceTrackerTest, StreamingTranscriptionTracking) {
    uint32_t utteranceId = 2;
    bool isStreaming = true;
    
    // Start streaming transcription
    uint64_t sessionId = tracker_->startTranscription(utteranceId, isStreaming);
    EXPECT_GT(sessionId, 0);
    
    // Record multiple streaming updates
    for (int i = 0; i < 5; ++i) {
        tracker_->recordStreamingUpdate(sessionId, 30.0 + i * 5, true, 10 + i * 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Complete streaming transcription
    tracker_->completeTranscription(sessionId, true, 0.90f, 150);
    
    // Verify streaming metrics
    auto summary = tracker_->getPerformanceSummary();
    EXPECT_EQ(summary["streaming_transcriptions"], 1.0);
    EXPECT_GT(summary["streaming_ratio"], 0.0);
}

TEST_F(STTPerformanceTrackerTest, ConcurrentTranscriptionTracking) {
    std::vector<uint64_t> sessionIds;
    
    // Start multiple concurrent transcriptions
    for (int i = 0; i < 3; ++i) {
        uint64_t sessionId = tracker_->startTranscription(i + 10, false);
        sessionIds.push_back(sessionId);
    }
    
    // Verify concurrent count
    auto summary = tracker_->getPerformanceSummary();
    EXPECT_EQ(summary["current_concurrent_transcriptions"], 3.0);
    
    // Complete transcriptions one by one
    for (size_t i = 0; i < sessionIds.size(); ++i) {
        tracker_->completeTranscription(sessionIds[i], true);
        
        // Check concurrent count decreases
        auto currentSummary = tracker_->getPerformanceSummary();
        EXPECT_EQ(currentSummary["current_concurrent_transcriptions"], 3.0 - (i + 1));
    }
}

TEST_F(STTPerformanceTrackerTest, VADMetricsTracking) {
    // Record VAD metrics
    tracker_->recordVADMetrics(45.0, 0.88f, true);
    tracker_->recordVADMetrics(52.0, 0.92f, false);
    tracker_->recordVADMetrics(38.0, 0.85f, true);
    
    // Get detailed metrics
    auto detailedMetrics = tracker_->getDetailedMetrics(1);
    
    // Verify VAD metrics are recorded
    EXPECT_GT(detailedMetrics["vad_response_time"].count, 0);
    EXPECT_GT(detailedMetrics["vad_accuracy"].count, 0);
    EXPECT_GT(detailedMetrics["vad_state_changes"].count, 0);
    
    // Check average values are reasonable
    EXPECT_GT(detailedMetrics["vad_response_time"].mean, 0);
    EXPECT_GT(detailedMetrics["vad_accuracy"].mean, 0.8);
}

TEST_F(STTPerformanceTrackerTest, LanguageDetectionTracking) {
    // Record language detection events
    tracker_->recordLanguageDetection(75.0, 0.95f, "en", "");
    tracker_->recordLanguageDetection(82.0, 0.88f, "es", "en");
    tracker_->recordLanguageDetection(69.0, 0.92f, "fr", "es");
    
    // Get detailed metrics
    auto detailedMetrics = tracker_->getDetailedMetrics(1);
    
    // Verify language detection metrics
    EXPECT_GT(detailedMetrics["language_detection_latency"].count, 0);
    EXPECT_GT(detailedMetrics["language_confidence"].count, 0);
    
    // Check latency is reasonable
    EXPECT_GT(detailedMetrics["language_detection_latency"].mean, 60.0);
    EXPECT_LT(detailedMetrics["language_detection_latency"].mean, 90.0);
}

TEST_F(STTPerformanceTrackerTest, BufferUsageTracking) {
    // Record buffer usage over time
    tracker_->recordBufferUsage(15.5, 65.0f, 3);
    tracker_->recordBufferUsage(18.2, 72.5f, 4);
    tracker_->recordBufferUsage(12.8, 58.0f, 2);
    
    // Get detailed metrics
    auto detailedMetrics = tracker_->getDetailedMetrics(1);
    
    // Verify buffer usage metrics
    EXPECT_GT(detailedMetrics["buffer_usage"].count, 0);
    EXPECT_GT(detailedMetrics["buffer_usage"].mean, 10.0);
    EXPECT_LT(detailedMetrics["buffer_usage"].mean, 20.0);
}

TEST_F(STTPerformanceTrackerTest, ModelLoadingTracking) {
    // Record model loading events
    tracker_->recordModelLoading("whisper-base", 1250.0, 142.5, true);
    tracker_->recordModelLoading("whisper-small", 850.0, 244.8, false);
    
    // Get detailed metrics
    auto detailedMetrics = tracker_->getDetailedMetrics(1);
    
    // Verify model loading metrics
    EXPECT_GT(detailedMetrics["model_load_time"].count, 0);
    EXPECT_GT(detailedMetrics["model_load_time"].mean, 800.0);
    EXPECT_LT(detailedMetrics["model_load_time"].mean, 1500.0);
}

TEST_F(STTPerformanceTrackerTest, ThroughputCalculation) {
    // Start and complete multiple transcriptions quickly
    for (int i = 0; i < 10; ++i) {
        uint64_t sessionId = tracker_->startTranscription(i + 100, false);
        tracker_->completeTranscription(sessionId, true);
    }
    
    // Update throughput metrics
    tracker_->updateThroughputMetrics();
    
    // Verify throughput is calculated
    auto summary = tracker_->getPerformanceSummary();
    EXPECT_EQ(summary["total_transcriptions"], 10.0);
    EXPECT_EQ(summary["successful_transcriptions"], 10.0);
    EXPECT_EQ(summary["success_rate"], 1.0);
}

TEST_F(STTPerformanceTrackerTest, SessionTrackerRAII) {
    uint32_t utteranceId = 999;
    
    {
        // Create RAII session tracker
        TranscriptionSessionTracker sessionTracker(*tracker_, utteranceId, false);
        
        // Verify session was started
        auto summary = tracker_->getPerformanceSummary();
        EXPECT_EQ(summary["current_concurrent_transcriptions"], 1.0);
        
        // Mark as successful
        sessionTracker.markSuccess(0.88f, 125);
    } // Session tracker destructor should complete the transcription
    
    // Verify transcription was completed
    auto finalSummary = tracker_->getPerformanceSummary();
    EXPECT_EQ(finalSummary["total_transcriptions"], 1.0);
    EXPECT_EQ(finalSummary["successful_transcriptions"], 1.0);
    EXPECT_EQ(finalSummary["current_concurrent_transcriptions"], 0.0);
}

TEST_F(STTPerformanceTrackerTest, EnableDisableTracking) {
    // Disable tracking
    tracker_->setEnabled(false);
    EXPECT_FALSE(tracker_->isEnabled());
    
    // Try to start transcription (should return 0)
    uint64_t sessionId = tracker_->startTranscription(1, false);
    EXPECT_EQ(sessionId, 0);
    
    // Re-enable tracking
    tracker_->setEnabled(true);
    EXPECT_TRUE(tracker_->isEnabled());
    
    // Now transcription should work
    sessionId = tracker_->startTranscription(1, false);
    EXPECT_GT(sessionId, 0);
    
    tracker_->completeTranscription(sessionId, true);
}

TEST_F(STTPerformanceTrackerTest, ResetFunctionality) {
    // Create some transcriptions
    for (int i = 0; i < 5; ++i) {
        uint64_t sessionId = tracker_->startTranscription(i, false);
        tracker_->completeTranscription(sessionId, true);
    }
    
    // Verify metrics exist
    auto summary = tracker_->getPerformanceSummary();
    EXPECT_EQ(summary["total_transcriptions"], 5.0);
    
    // Reset tracker
    tracker_->reset();
    
    // Verify metrics are cleared
    auto resetSummary = tracker_->getPerformanceSummary();
    EXPECT_EQ(resetSummary["total_transcriptions"], 0.0);
    EXPECT_EQ(resetSummary["successful_transcriptions"], 0.0);
    EXPECT_EQ(resetSummary["current_concurrent_transcriptions"], 0.0);
}