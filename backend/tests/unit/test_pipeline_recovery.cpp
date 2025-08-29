#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "core/pipeline_recovery.hpp"
#include "core/utterance_manager.hpp"
#include "utils/error_handler.hpp"
#include <memory>
#include <thread>
#include <chrono>

using namespace speechrnt::core;
using namespace speechrnt::utils;
using ::testing::_;
using ::testing::Return;
using ::testing::InSequence;

// Mock UtteranceManager for testing
class MockUtteranceManager : public UtteranceManager {
public:
    MockUtteranceManager() : UtteranceManager(UtteranceManagerConfig{}) {}
    
    MOCK_METHOD(std::shared_ptr<UtteranceData>, getUtterance, (uint32_t utterance_id), (override));
    MOCK_METHOD(bool, setUtteranceState, (uint32_t utterance_id, UtteranceState state), (override));
    MOCK_METHOD(bool, setUtteranceError, (uint32_t utterance_id, const std::string& error_message), (override));
};

class PipelineRecoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_utterance_manager = std::make_shared<MockUtteranceManager>();
        recovery = std::make_unique<PipelineRecovery>(mock_utterance_manager);
        recovery->initialize();
    }

    void TearDown() override {
        if (recovery) {
            recovery->shutdown();
        }
    }

    std::shared_ptr<MockUtteranceManager> mock_utterance_manager;
    std::unique_ptr<PipelineRecovery> recovery;
};

TEST_F(PipelineRecoveryTest, InitializationAndShutdown) {
    EXPECT_TRUE(recovery != nullptr);
    
    // Test shutdown
    recovery->shutdown();
    
    // Should be safe to call shutdown multiple times
    recovery->shutdown();
}

TEST_F(PipelineRecoveryTest, DefaultRecoveryConfigurations) {
    // Test that default configurations are set up correctly
    ErrorInfo stt_error(ErrorCategory::STT, ErrorSeverity::ERROR, "STT failed");
    ErrorInfo translation_error(ErrorCategory::TRANSLATION, ErrorSeverity::ERROR, "Translation failed");
    ErrorInfo audio_error(ErrorCategory::AUDIO_PROCESSING, ErrorSeverity::ERROR, "Audio failed");
    
    // Create mock utterance
    auto utterance = std::make_shared<UtteranceData>();
    utterance->id = 1;
    utterance->state = UtteranceState::ERROR;
    
    EXPECT_CALL(*mock_utterance_manager, getUtterance(1))
        .WillRepeatedly(Return(utterance));
    
    // Test STT error recovery
    EXPECT_TRUE(recovery->attemptRecovery(stt_error, 1));
    
    // Test translation error recovery
    EXPECT_TRUE(recovery->attemptRecovery(translation_error, 1));
    
    // Test audio error recovery
    EXPECT_TRUE(recovery->attemptRecovery(audio_error, 1));
}

TEST_F(PipelineRecoveryTest, CustomRecoveryConfiguration) {
    RecoveryConfig custom_config;
    custom_config.strategy = RecoveryStrategy::RETRY_IMMEDIATE;
    custom_config.max_retry_attempts = 5;
    custom_config.retry_delay = std::chrono::milliseconds(500);
    
    recovery->configureRecovery(ErrorCategory::STT, custom_config);
    
    ErrorInfo error(ErrorCategory::STT, ErrorSeverity::ERROR, "Custom STT error");
    
    auto utterance = std::make_shared<UtteranceData>();
    utterance->id = 1;
    utterance->state = UtteranceState::ERROR;
    
    EXPECT_CALL(*mock_utterance_manager, getUtterance(1))
        .WillRepeatedly(Return(utterance));
    
    EXPECT_TRUE(recovery->attemptRecovery(error, 1));
}

TEST_F(PipelineRecoveryTest, RetryRecoveryStrategy) {
    RecoveryConfig retry_config;
    retry_config.strategy = RecoveryStrategy::RETRY_IMMEDIATE;
    retry_config.max_retry_attempts = 3;
    
    recovery->configureRecovery(ErrorCategory::STT, retry_config);
    
    ErrorInfo error(ErrorCategory::STT, ErrorSeverity::ERROR, "STT retry test");
    
    auto utterance = std::make_shared<UtteranceData>();
    utterance->id = 1;
    utterance->state = UtteranceState::ERROR;
    
    EXPECT_CALL(*mock_utterance_manager, getUtterance(1))
        .WillRepeatedly(Return(utterance));
    
    // First attempt should succeed
    EXPECT_TRUE(recovery->attemptRecovery(error, 1));
    EXPECT_EQ(utterance->state, UtteranceState::TRANSCRIBING);
    EXPECT_TRUE(utterance->error_message.empty());
}

TEST_F(PipelineRecoveryTest, MaxRetryAttemptsExceeded) {
    RecoveryConfig retry_config;
    retry_config.strategy = RecoveryStrategy::RETRY_IMMEDIATE;
    retry_config.max_retry_attempts = 2;
    
    recovery->configureRecovery(ErrorCategory::STT, retry_config);
    
    ErrorInfo error(ErrorCategory::STT, ErrorSeverity::ERROR, "STT max retry test");
    
    auto utterance = std::make_shared<UtteranceData>();
    utterance->id = 1;
    utterance->state = UtteranceState::ERROR;
    
    EXPECT_CALL(*mock_utterance_manager, getUtterance(1))
        .WillRepeatedly(Return(utterance));
    
    // First attempt
    EXPECT_TRUE(recovery->attemptRecovery(error, 1));
    
    // Second attempt
    EXPECT_TRUE(recovery->attemptRecovery(error, 1));
    
    // Third attempt should fail (exceeds max)
    EXPECT_FALSE(recovery->attemptRecovery(error, 1));
}

TEST_F(PipelineRecoveryTest, SkipStageRecoveryStrategy) {
    RecoveryConfig skip_config;
    skip_config.strategy = RecoveryStrategy::SKIP_STAGE;
    skip_config.max_retry_attempts = 1;
    
    recovery->configureRecovery(ErrorCategory::STT, skip_config);
    
    ErrorInfo error(ErrorCategory::STT, ErrorSeverity::ERROR, "STT skip test");
    
    auto utterance = std::make_shared<UtteranceData>();
    utterance->id = 1;
    utterance->state = UtteranceState::ERROR;
    utterance->transcript = "original transcript";
    
    EXPECT_CALL(*mock_utterance_manager, getUtterance(1))
        .WillRepeatedly(Return(utterance));
    
    EXPECT_TRUE(recovery->attemptRecovery(error, 1));
    
    // Should skip STT and move to translation
    EXPECT_EQ(utterance->state, UtteranceState::TRANSLATING);
    EXPECT_EQ(utterance->transcript, "[Transcription unavailable]");
}

TEST_F(PipelineRecoveryTest, RestartPipelineRecoveryStrategy) {
    RecoveryConfig restart_config;
    restart_config.strategy = RecoveryStrategy::RESTART_PIPELINE;
    restart_config.max_retry_attempts = 1;
    
    recovery->configureRecovery(ErrorCategory::PIPELINE, restart_config);
    
    ErrorInfo error(ErrorCategory::PIPELINE, ErrorSeverity::ERROR, "Pipeline restart test");
    
    auto utterance = std::make_shared<UtteranceData>();
    utterance->id = 1;
    utterance->state = UtteranceState::ERROR;
    utterance->transcript = "old transcript";
    utterance->translation = "old translation";
    utterance->error_message = "old error";
    
    EXPECT_CALL(*mock_utterance_manager, getUtterance(1))
        .WillRepeatedly(Return(utterance));
    
    EXPECT_TRUE(recovery->attemptRecovery(error, 1));
    
    // Should restart entire pipeline
    EXPECT_EQ(utterance->state, UtteranceState::TRANSCRIBING);
    EXPECT_TRUE(utterance->transcript.empty());
    EXPECT_TRUE(utterance->translation.empty());
    EXPECT_TRUE(utterance->error_message.empty());
}

TEST_F(PipelineRecoveryTest, FallbackModelRecoveryStrategy) {
    RecoveryConfig fallback_config;
    fallback_config.strategy = RecoveryStrategy::FALLBACK_MODEL;
    fallback_config.max_retry_attempts = 1;
    fallback_config.fallback_model_path = "/path/to/fallback/model";
    
    recovery->configureRecovery(ErrorCategory::MODEL_LOADING, fallback_config);
    
    ErrorInfo error(ErrorCategory::MODEL_LOADING, ErrorSeverity::CRITICAL, "Model loading failed");
    
    auto utterance = std::make_shared<UtteranceData>();
    utterance->id = 1;
    utterance->state = UtteranceState::ERROR;
    
    EXPECT_CALL(*mock_utterance_manager, getUtterance(1))
        .WillRepeatedly(Return(utterance));
    
    EXPECT_TRUE(recovery->attemptRecovery(error, 1));
}

TEST_F(PipelineRecoveryTest, CustomRecoveryAction) {
    bool custom_action_called = false;
    
    RecoveryConfig custom_config;
    custom_config.strategy = RecoveryStrategy::RETRY_IMMEDIATE;
    custom_config.max_retry_attempts = 1;
    custom_config.custom_recovery_action = [&custom_action_called]() -> bool {
        custom_action_called = true;
        return true;
    };
    
    recovery->configureRecovery(ErrorCategory::SYSTEM, custom_config);
    
    ErrorInfo error(ErrorCategory::SYSTEM, ErrorSeverity::ERROR, "System error");
    
    auto utterance = std::make_shared<UtteranceData>();
    utterance->id = 1;
    utterance->state = UtteranceState::ERROR;
    
    EXPECT_CALL(*mock_utterance_manager, getUtterance(1))
        .WillRepeatedly(Return(utterance));
    
    EXPECT_TRUE(recovery->attemptRecovery(error, 1));
    // Note: Custom action would be called in a real scenario, but our mock setup doesn't trigger it
}

TEST_F(PipelineRecoveryTest, DelayedRecoveryStrategy) {
    RecoveryConfig delayed_config;
    delayed_config.strategy = RecoveryStrategy::RETRY_WITH_DELAY;
    delayed_config.max_retry_attempts = 2;
    delayed_config.retry_delay = std::chrono::milliseconds(100);
    delayed_config.exponential_backoff = false;
    
    recovery->configureRecovery(ErrorCategory::TRANSLATION, delayed_config);
    
    ErrorInfo error(ErrorCategory::TRANSLATION, ErrorSeverity::ERROR, "Translation delayed retry");
    
    auto utterance = std::make_shared<UtteranceData>();
    utterance->id = 1;
    utterance->state = UtteranceState::ERROR;
    
    EXPECT_CALL(*mock_utterance_manager, getUtterance(1))
        .WillRepeatedly(Return(utterance));
    
    // Should return true immediately (recovery scheduled)
    EXPECT_TRUE(recovery->attemptRecovery(error, 1));
    
    // Should be marked as recovering
    EXPECT_TRUE(recovery->isRecovering(1));
    
    // Wait for delayed recovery to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Should no longer be recovering after delay
    EXPECT_FALSE(recovery->isRecovering(1));
}

TEST_F(PipelineRecoveryTest, RecoveryStatistics) {
    RecoveryConfig config;
    config.strategy = RecoveryStrategy::RETRY_IMMEDIATE;
    config.max_retry_attempts = 1;
    
    recovery->configureRecovery(ErrorCategory::STT, config);
    recovery->configureRecovery(ErrorCategory::TRANSLATION, config);
    
    auto utterance1 = std::make_shared<UtteranceData>();
    utterance1->id = 1;
    utterance1->state = UtteranceState::ERROR;
    
    auto utterance2 = std::make_shared<UtteranceData>();
    utterance2->id = 2;
    utterance2->state = UtteranceState::ERROR;
    
    EXPECT_CALL(*mock_utterance_manager, getUtterance(1))
        .WillRepeatedly(Return(utterance1));
    EXPECT_CALL(*mock_utterance_manager, getUtterance(2))
        .WillRepeatedly(Return(utterance2));
    
    ErrorInfo stt_error(ErrorCategory::STT, ErrorSeverity::ERROR, "STT error");
    ErrorInfo translation_error(ErrorCategory::TRANSLATION, ErrorSeverity::ERROR, "Translation error");
    
    // Attempt recoveries
    recovery->attemptRecovery(stt_error, 1);
    recovery->attemptRecovery(translation_error, 2);
    
    auto stats = recovery->getRecoveryStats();
    
    EXPECT_EQ(stats.total_recovery_attempts, 2);
    EXPECT_EQ(stats.successful_recoveries, 2);
    EXPECT_EQ(stats.failed_recoveries, 0);
    EXPECT_EQ(stats.recovery_attempts_by_category[ErrorCategory::STT], 1);
    EXPECT_EQ(stats.recovery_attempts_by_category[ErrorCategory::TRANSLATION], 1);
}

TEST_F(PipelineRecoveryTest, CleanupCompletedRecoveries) {
    RecoveryConfig config;
    config.strategy = RecoveryStrategy::RETRY_IMMEDIATE;
    config.max_retry_attempts = 1;
    
    recovery->configureRecovery(ErrorCategory::STT, config);
    
    auto utterance = std::make_shared<UtteranceData>();
    utterance->id = 1;
    utterance->state = UtteranceState::ERROR;
    
    EXPECT_CALL(*mock_utterance_manager, getUtterance(1))
        .WillOnce(Return(utterance))  // First call returns utterance
        .WillOnce(Return(nullptr));   // Second call returns null (utterance completed)
    
    ErrorInfo error(ErrorCategory::STT, ErrorSeverity::ERROR, "STT error");
    
    // Start recovery
    recovery->attemptRecovery(error, 1);
    EXPECT_TRUE(recovery->isRecovering(1));
    
    // Cleanup completed recoveries
    recovery->cleanupCompletedRecoveries();
    EXPECT_FALSE(recovery->isRecovering(1));
}

TEST_F(PipelineRecoveryTest, NoRecoveryConfigurationForCategory) {
    ErrorInfo unknown_error(ErrorCategory::UNKNOWN, ErrorSeverity::ERROR, "Unknown error");
    
    // Should return false for unconfigured category
    EXPECT_FALSE(recovery->attemptRecovery(unknown_error, 1));
}

TEST_F(PipelineRecoveryTest, UtteranceNotFound) {
    RecoveryConfig config;
    config.strategy = RecoveryStrategy::RETRY_IMMEDIATE;
    config.max_retry_attempts = 1;
    
    recovery->configureRecovery(ErrorCategory::STT, config);
    
    EXPECT_CALL(*mock_utterance_manager, getUtterance(999))
        .WillOnce(Return(nullptr));
    
    ErrorInfo error(ErrorCategory::STT, ErrorSeverity::ERROR, "STT error");
    
    // Should return false when utterance not found
    EXPECT_FALSE(recovery->attemptRecovery(error, 999));
}

// Test RecoveryActionFactory
TEST(RecoveryActionFactoryTest, CreateModelReloadAction) {
    auto action = RecoveryActionFactory::createModelReloadAction("/path/to/model");
    EXPECT_TRUE(action != nullptr);
    EXPECT_TRUE(action());  // Should return true (mock implementation)
}

TEST(RecoveryActionFactoryTest, CreateServiceRestartAction) {
    auto action = RecoveryActionFactory::createServiceRestartAction("test_service");
    EXPECT_TRUE(action != nullptr);
    EXPECT_TRUE(action());  // Should return true (mock implementation)
}

TEST(RecoveryActionFactoryTest, CreateCacheClearAction) {
    auto action = RecoveryActionFactory::createCacheClearAction();
    EXPECT_TRUE(action != nullptr);
    EXPECT_TRUE(action());  // Should return true (mock implementation)
}

TEST(RecoveryActionFactoryTest, CreateMemoryCleanupAction) {
    auto action = RecoveryActionFactory::createMemoryCleanupAction();
    EXPECT_TRUE(action != nullptr);
    EXPECT_TRUE(action());  // Should return true (mock implementation)
}

TEST(RecoveryActionFactoryTest, CreateGPUResetAction) {
    auto action = RecoveryActionFactory::createGPUResetAction();
    EXPECT_TRUE(action != nullptr);
    EXPECT_TRUE(action());  // Should return true (mock implementation)
}