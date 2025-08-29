#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "stt/stt_translation_integration.hpp"
#include "stt/whisper_stt.hpp"
#include "core/translation_pipeline.hpp"
#include "core/task_queue.hpp"
#include "mt/marian_translator.hpp"
#include <memory>
#include <vector>
#include <chrono>
#include <thread>

using namespace stt;
using namespace speechrnt;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::StrictMock;

// Mock classes for testing
class MockWhisperSTT : public WhisperSTT {
public:
    MOCK_METHOD(bool, initialize, (const std::string& modelPath, int n_threads), (override));
    MOCK_METHOD(bool, isInitialized, (), (const, override));
    MOCK_METHOD(void, transcribe, (const std::vector<float>& audioData, TranscriptionCallback callback), (override));
    MOCK_METHOD(void, setTranscriptionCompleteCallback, (TranscriptionCompleteCallback callback));
    MOCK_METHOD(void, generateTranscriptionCandidates, (const std::vector<float>& audioData, std::vector<TranscriptionResult>& candidates, int maxCandidates));
};

class MockTranslationPipeline : public core::TranslationPipeline {
public:
    MockTranslationPipeline() : core::TranslationPipeline() {}
    
    MOCK_METHOD(bool, isReady, (), (const, override));
    MOCK_METHOD(void, processTranscriptionResult, 
                (uint32_t utteranceId, const std::string& sessionId, 
                 const TranscriptionResult& result, const std::vector<TranscriptionResult>& candidates));
    MOCK_METHOD(void, triggerTranslation, 
                (uint32_t utteranceId, const std::string& sessionId, 
                 const TranscriptionResult& transcription, bool forceTranslation));
    MOCK_METHOD(void, updateConfiguration, (const core::TranslationPipelineConfig& config));
};

class STTTranslationIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockSTT_ = std::make_shared<StrictMock<MockWhisperSTT>>();
        mockPipeline_ = std::make_shared<StrictMock<MockTranslationPipeline>>();
        
        // Set up default expectations
        EXPECT_CALL(*mockSTT_, isInitialized())
            .WillRepeatedly(Return(true));
        EXPECT_CALL(*mockPipeline_, isReady())
            .WillRepeatedly(Return(true));
        
        config_.enable_automatic_translation = true;
        config_.enable_confidence_gating = true;
        config_.enable_multiple_candidates = true;
        config_.min_transcription_confidence = 0.7f;
        config_.candidate_confidence_threshold = 0.5f;
        config_.max_transcription_candidates = 3;
        
        integration_ = std::make_unique<STTTranslationIntegration>(config_);
    }
    
    void TearDown() override {
        if (integration_) {
            integration_->shutdown();
        }
    }
    
    TranscriptionResult createMockTranscriptionResult(const std::string& text, float confidence, bool meetsThreshold = true) {
        TranscriptionResult result;
        result.text = text;
        result.confidence = confidence;
        result.is_partial = false;
        result.meets_confidence_threshold = meetsThreshold;
        result.quality_level = confidence > 0.8f ? "high" : (confidence > 0.6f ? "medium" : "low");
        result.start_time_ms = 0;
        result.end_time_ms = 3000;
        return result;
    }
    
    std::vector<float> createMockAudioData(size_t samples = 48000) {
        return std::vector<float>(samples, 0.1f);
    }
    
    std::shared_ptr<StrictMock<MockWhisperSTT>> mockSTT_;
    std::shared_ptr<StrictMock<MockTranslationPipeline>> mockPipeline_;
    std::unique_ptr<STTTranslationIntegration> integration_;
    STTTranslationConfig config_;
};

TEST_F(STTTranslationIntegrationTest, InitializationSuccess) {
    EXPECT_CALL(*mockSTT_, setTranscriptionCompleteCallback(_))
        .Times(1);
    EXPECT_CALL(*mockPipeline_, updateConfiguration(_))
        .Times(1);
    
    EXPECT_TRUE(integration_->initialize(mockSTT_, mockPipeline_));
    EXPECT_TRUE(integration_->isReady());
}

TEST_F(STTTranslationIntegrationTest, InitializationFailsWithNullParameters) {
    EXPECT_FALSE(integration_->initialize(nullptr, mockPipeline_));
    EXPECT_FALSE(integration_->initialize(mockSTT_, nullptr));
    EXPECT_FALSE(integration_->initialize(nullptr, nullptr));
}

TEST_F(STTTranslationIntegrationTest, InitializationFailsWithUnreadyComponents) {
    EXPECT_CALL(*mockSTT_, isInitialized())
        .WillOnce(Return(false));
    
    EXPECT_FALSE(integration_->initialize(mockSTT_, mockPipeline_));
}

TEST_F(STTTranslationIntegrationTest, ProcessTranscriptionWithTranslationSuccess) {
    // Initialize integration
    EXPECT_CALL(*mockSTT_, setTranscriptionCompleteCallback(_))
        .Times(1);
    EXPECT_CALL(*mockPipeline_, updateConfiguration(_))
        .Times(1);
    
    EXPECT_TRUE(integration_->initialize(mockSTT_, mockPipeline_));
    
    // Set up transcription expectation
    auto audioData = createMockAudioData();
    uint32_t utteranceId = 123;
    std::string sessionId = "test_session";
    
    EXPECT_CALL(*mockSTT_, transcribe(audioData, _))
        .WillOnce(Invoke([this](const std::vector<float>&, TranscriptionCallback callback) {
            auto result = createMockTranscriptionResult("Hello world", 0.85f);
            callback(result);
        }));
    
    EXPECT_CALL(*mockSTT_, generateTranscriptionCandidates(audioData, _, config_.max_transcription_candidates))
        .Times(1);
    
    integration_->processTranscriptionWithTranslation(utteranceId, sessionId, audioData, true);
    
    // Allow some time for async processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_F(STTTranslationIntegrationTest, AutomaticTranslationTriggering) {
    // Initialize integration
    EXPECT_CALL(*mockSTT_, setTranscriptionCompleteCallback(_))
        .WillOnce(Invoke([this](TranscriptionCompleteCallback callback) {
            transcriptionCallback_ = callback;
        }));
    EXPECT_CALL(*mockPipeline_, updateConfiguration(_))
        .Times(1);
    
    EXPECT_TRUE(integration_->initialize(mockSTT_, mockPipeline_));
    
    // Set up translation pipeline expectation
    uint32_t utteranceId = 456;
    auto result = createMockTranscriptionResult("Test transcription", 0.9f);
    std::vector<TranscriptionResult> candidates;
    
    EXPECT_CALL(*mockPipeline_, processTranscriptionResult(utteranceId, _, result, candidates))
        .Times(1);
    
    // Trigger transcription complete callback
    transcriptionCallback_(utteranceId, result, candidates);
    
    // Verify statistics
    auto stats = integration_->getStatistics();
    EXPECT_EQ(stats.automatic_translations_triggered, 1);
}

TEST_F(STTTranslationIntegrationTest, ConfidenceGatingPreventsTranslation) {
    // Initialize integration
    EXPECT_CALL(*mockSTT_, setTranscriptionCompleteCallback(_))
        .WillOnce(Invoke([this](TranscriptionCompleteCallback callback) {
            transcriptionCallback_ = callback;
        }));
    EXPECT_CALL(*mockPipeline_, updateConfiguration(_))
        .Times(1);
    
    EXPECT_TRUE(integration_->initialize(mockSTT_, mockPipeline_));
    
    // Create low-confidence result that should be rejected
    uint32_t utteranceId = 789;
    auto result = createMockTranscriptionResult("Low confidence text", 0.5f, false); // Below threshold
    std::vector<TranscriptionResult> candidates;
    
    // Translation should NOT be triggered
    EXPECT_CALL(*mockPipeline_, processTranscriptionResult(_, _, _, _))
        .Times(0);
    
    // Trigger transcription complete callback
    transcriptionCallback_(utteranceId, result, candidates);
    
    // Verify statistics
    auto stats = integration_->getStatistics();
    EXPECT_EQ(stats.confidence_gate_rejections, 1);
    EXPECT_EQ(stats.automatic_translations_triggered, 0);
}

TEST_F(STTTranslationIntegrationTest, ManualTranslationTriggering) {
    // Initialize integration
    EXPECT_CALL(*mockSTT_, setTranscriptionCompleteCallback(_))
        .Times(1);
    EXPECT_CALL(*mockPipeline_, updateConfiguration(_))
        .Times(1);
    
    EXPECT_TRUE(integration_->initialize(mockSTT_, mockPipeline_));
    
    // Set up manual translation expectation
    uint32_t utteranceId = 999;
    std::string sessionId = "manual_session";
    auto result = createMockTranscriptionResult("Manual translation test", 0.8f);
    
    EXPECT_CALL(*mockPipeline_, triggerTranslation(utteranceId, sessionId, result, false))
        .Times(1);
    
    integration_->triggerManualTranslation(utteranceId, sessionId, result, false);
    
    // Verify statistics
    auto stats = integration_->getStatistics();
    EXPECT_EQ(stats.manual_translations_triggered, 1);
}

TEST_F(STTTranslationIntegrationTest, ConfigurationUpdate) {
    // Initialize integration
    EXPECT_CALL(*mockSTT_, setTranscriptionCompleteCallback(_))
        .Times(1);
    EXPECT_CALL(*mockPipeline_, updateConfiguration(_))
        .Times(2); // Once during init, once during update
    
    EXPECT_TRUE(integration_->initialize(mockSTT_, mockPipeline_));
    
    // Update configuration
    STTTranslationConfig newConfig = config_;
    newConfig.min_transcription_confidence = 0.8f;
    newConfig.enable_automatic_translation = false;
    
    integration_->updateConfiguration(newConfig);
    
    EXPECT_EQ(integration_->getConfiguration().min_transcription_confidence, 0.8f);
    EXPECT_FALSE(integration_->getConfiguration().enable_automatic_translation);
}

TEST_F(STTTranslationIntegrationTest, CallbackNotifications) {
    // Initialize integration
    EXPECT_CALL(*mockSTT_, setTranscriptionCompleteCallback(_))
        .WillOnce(Invoke([this](TranscriptionCompleteCallback callback) {
            transcriptionCallback_ = callback;
        }));
    EXPECT_CALL(*mockPipeline_, updateConfiguration(_))
        .Times(1);
    
    EXPECT_TRUE(integration_->initialize(mockSTT_, mockPipeline_));
    
    // Set up callbacks
    bool transcriptionReadyCalled = false;
    bool translationTriggeredCalled = false;
    
    integration_->setTranscriptionReadyCallback(
        [&transcriptionReadyCalled](uint32_t, const TranscriptionResult&, const std::vector<TranscriptionResult>&) {
            transcriptionReadyCalled = true;
        }
    );
    
    integration_->setTranslationTriggeredCallback(
        [&translationTriggeredCalled](uint32_t, const std::string&, bool) {
            translationTriggeredCalled = true;
        }
    );
    
    // Set up translation pipeline expectation
    uint32_t utteranceId = 111;
    auto result = createMockTranscriptionResult("Callback test", 0.9f);
    std::vector<TranscriptionResult> candidates;
    
    EXPECT_CALL(*mockPipeline_, processTranscriptionResult(_, _, _, _))
        .Times(1);
    
    // Trigger transcription complete callback
    transcriptionCallback_(utteranceId, result, candidates);
    
    EXPECT_TRUE(transcriptionReadyCalled);
    EXPECT_TRUE(translationTriggeredCalled);
}

TEST_F(STTTranslationIntegrationTest, MultipleCandidatesFiltering) {
    // Initialize integration
    EXPECT_CALL(*mockSTT_, setTranscriptionCompleteCallback(_))
        .WillOnce(Invoke([this](TranscriptionCompleteCallback callback) {
            transcriptionCallback_ = callback;
        }));
    EXPECT_CALL(*mockPipeline_, updateConfiguration(_))
        .Times(1);
    
    EXPECT_TRUE(integration_->initialize(mockSTT_, mockPipeline_));
    
    // Create candidates with varying confidence levels
    std::vector<TranscriptionResult> candidates;
    candidates.push_back(createMockTranscriptionResult("High confidence", 0.9f));
    candidates.push_back(createMockTranscriptionResult("Medium confidence", 0.7f));
    candidates.push_back(createMockTranscriptionResult("Low confidence", 0.3f)); // Should be filtered out
    candidates.push_back(createMockTranscriptionResult("Another high", 0.8f));
    
    uint32_t utteranceId = 222;
    auto result = createMockTranscriptionResult("Primary result", 0.95f);
    
    // Expect only candidates above threshold to be passed to pipeline
    EXPECT_CALL(*mockPipeline_, processTranscriptionResult(utteranceId, _, result, _))
        .WillOnce(Invoke([this](uint32_t, const std::string&, const TranscriptionResult&, 
                               const std::vector<TranscriptionResult>& filteredCandidates) {
            // Should have 3 candidates (excluding the 0.3f confidence one)
            EXPECT_EQ(filteredCandidates.size(), 3);
            // Should be sorted by confidence (highest first)
            EXPECT_GE(filteredCandidates[0].confidence, filteredCandidates[1].confidence);
            EXPECT_GE(filteredCandidates[1].confidence, filteredCandidates[2].confidence);
        }));
    
    // Trigger transcription complete callback
    transcriptionCallback_(utteranceId, result, candidates);
}

TEST_F(STTTranslationIntegrationTest, StatisticsTracking) {
    // Initialize integration
    EXPECT_CALL(*mockSTT_, setTranscriptionCompleteCallback(_))
        .WillOnce(Invoke([this](TranscriptionCompleteCallback callback) {
            transcriptionCallback_ = callback;
        }));
    EXPECT_CALL(*mockPipeline_, updateConfiguration(_))
        .Times(1);
    
    EXPECT_TRUE(integration_->initialize(mockSTT_, mockPipeline_));
    
    // Process multiple transcriptions
    EXPECT_CALL(*mockPipeline_, processTranscriptionResult(_, _, _, _))
        .Times(2);
    
    // First transcription - should trigger translation
    uint32_t utteranceId1 = 333;
    auto result1 = createMockTranscriptionResult("First transcription", 0.9f);
    std::vector<TranscriptionResult> candidates1 = {
        createMockTranscriptionResult("Candidate 1", 0.8f),
        createMockTranscriptionResult("Candidate 2", 0.7f)
    };
    transcriptionCallback_(utteranceId1, result1, candidates1);
    
    // Second transcription - should also trigger translation
    uint32_t utteranceId2 = 444;
    auto result2 = createMockTranscriptionResult("Second transcription", 0.85f);
    std::vector<TranscriptionResult> candidates2 = {
        createMockTranscriptionResult("Candidate A", 0.75f)
    };
    transcriptionCallback_(utteranceId2, result2, candidates2);
    
    // Verify statistics
    auto stats = integration_->getStatistics();
    EXPECT_EQ(stats.total_transcriptions_processed, 2);
    EXPECT_EQ(stats.automatic_translations_triggered, 2);
    EXPECT_EQ(stats.candidates_generated, 3); // 2 + 1 candidates
    EXPECT_GT(stats.average_transcription_confidence, 0.8f);
}

private:
    TranscriptionCompleteCallback transcriptionCallback_;
};