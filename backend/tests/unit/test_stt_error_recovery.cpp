#include <gtest/gtest.h>
#include "stt/stt_error_recovery.hpp"
#include "utils/logging.hpp"
#include <chrono>
#include <thread>

using namespace stt;

class STTErrorRecoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        utils::Logger::initialize();
        recovery_ = std::make_unique<STTErrorRecovery>();
        
        RecoveryConfig config;
        config.maxRetryAttempts = 3;
        config.initialBackoffMs = std::chrono::milliseconds(10);
        config.maxBackoffMs = std::chrono::milliseconds(100);
        config.backoffMultiplier = 2.0;
        
        ASSERT_TRUE(recovery_->initialize(config));
    }
    
    void TearDown() override {
        recovery_.reset();
    }
    
    STTErrorContext createTestErrorContext(STTErrorType errorType, uint32_t utteranceId = 1) {
        STTErrorContext context;
        context.errorType = errorType;
        context.errorMessage = "Test error message";
        context.utteranceId = utteranceId;
        context.sessionId = "test_session";
        context.isRecoverable = true;
        context.wasUsingGPU = true;
        context.currentQuantization = QuantizationLevel::FP32;
        return context;
    }
    
    std::unique_ptr<STTErrorRecovery> recovery_;
};

TEST_F(STTErrorRecoveryTest, InitializationTest) {
    EXPECT_TRUE(recovery_->isEnabled());
    EXPECT_EQ(recovery_->getLastError(), "");
}

TEST_F(STTErrorRecoveryTest, ErrorTypeToStringConversion) {
    EXPECT_EQ(error_utils::errorTypeToString(STTErrorType::MODEL_LOAD_FAILURE), "MODEL_LOAD_FAILURE");
    EXPECT_EQ(error_utils::errorTypeToString(STTErrorType::GPU_MEMORY_ERROR), "GPU_MEMORY_ERROR");
    EXPECT_EQ(error_utils::errorTypeToString(STTErrorType::TRANSCRIPTION_TIMEOUT), "TRANSCRIPTION_TIMEOUT");
}

TEST_F(STTErrorRecoveryTest, RecoveryStrategyToStringConversion) {
    EXPECT_EQ(error_utils::recoveryStrategyToString(RecoveryStrategy::RETRY_WITH_BACKOFF), "RETRY_WITH_BACKOFF");
    EXPECT_EQ(error_utils::recoveryStrategyToString(RecoveryStrategy::FALLBACK_GPU_TO_CPU), "FALLBACK_GPU_TO_CPU");
    EXPECT_EQ(error_utils::recoveryStrategyToString(RecoveryStrategy::FALLBACK_QUANTIZATION), "FALLBACK_QUANTIZATION");
}

TEST_F(STTErrorRecoveryTest, TransientErrorClassification) {
    EXPECT_TRUE(error_utils::isTransientError(STTErrorType::TRANSCRIPTION_TIMEOUT));
    EXPECT_TRUE(error_utils::isTransientError(STTErrorType::NETWORK_ERROR));
    EXPECT_TRUE(error_utils::isTransientError(STTErrorType::STREAMING_BUFFER_OVERFLOW));
    
    EXPECT_FALSE(error_utils::isTransientError(STTErrorType::AUDIO_FORMAT_ERROR));
    EXPECT_FALSE(error_utils::isTransientError(STTErrorType::MODEL_LOAD_FAILURE));
}

TEST_F(STTErrorRecoveryTest, RecommendedStrategySelection) {
    // GPU memory error should recommend GPU fallback first
    EXPECT_EQ(error_utils::getRecommendedStrategy(STTErrorType::GPU_MEMORY_ERROR, 1), 
              RecoveryStrategy::FALLBACK_GPU_TO_CPU);
    
    // Model load failure should recommend quantization fallback first
    EXPECT_EQ(error_utils::getRecommendedStrategy(STTErrorType::MODEL_LOAD_FAILURE, 1), 
              RecoveryStrategy::FALLBACK_QUANTIZATION);
    
    // Timeout should recommend retry with backoff
    EXPECT_EQ(error_utils::getRecommendedStrategy(STTErrorType::TRANSCRIPTION_TIMEOUT, 1), 
              RecoveryStrategy::RETRY_WITH_BACKOFF);
    
    // Buffer overflow should recommend clearing buffers
    EXPECT_EQ(error_utils::getRecommendedStrategy(STTErrorType::STREAMING_BUFFER_OVERFLOW, 1), 
              RecoveryStrategy::CLEAR_BUFFERS);
}

TEST_F(STTErrorRecoveryTest, ErrorContextCreation) {
    std::runtime_error testException("Test exception message");
    
    STTErrorContext context = error_utils::createErrorContext(testException, 123, "test_session");
    
    EXPECT_EQ(context.errorMessage, "Test exception message");
    EXPECT_EQ(context.utteranceId, 123);
    EXPECT_EQ(context.sessionId, "test_session");
    EXPECT_NE(context.errorType, STTErrorType::UNKNOWN_ERROR); // Should classify the error
}

TEST_F(STTErrorRecoveryTest, RecoveryCallbackRegistration) {
    bool callbackCalled = false;
    RecoveryCallback callback = [&callbackCalled](const STTErrorContext& context) -> bool {
        callbackCalled = true;
        return true;
    };
    
    recovery_->registerRecoveryCallback(STTErrorType::MODEL_LOAD_FAILURE, callback);
    
    STTErrorContext context = createTestErrorContext(STTErrorType::MODEL_LOAD_FAILURE);
    RecoveryResult result = recovery_->handleError(context);
    
    EXPECT_TRUE(callbackCalled);
    EXPECT_TRUE(result.success);
}

TEST_F(STTErrorRecoveryTest, NotificationCallback) {
    bool notificationCalled = false;
    NotificationCallback callback = [&notificationCalled](const STTErrorContext& context, const RecoveryResult& result) {
        notificationCalled = true;
    };
    
    recovery_->setNotificationCallback(callback);
    
    STTErrorContext context = createTestErrorContext(STTErrorType::TRANSCRIPTION_TIMEOUT);
    recovery_->handleError(context);
    
    EXPECT_TRUE(notificationCalled);
}

TEST_F(STTErrorRecoveryTest, RecoveryInProgressTracking) {
    uint32_t utteranceId = 123;
    
    EXPECT_FALSE(recovery_->isRecoveryInProgress(utteranceId));
    
    // Create a slow recovery callback to test in-progress tracking
    RecoveryCallback slowCallback = [](const STTErrorContext& context) -> bool {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return true;
    };
    
    recovery_->registerRecoveryCallback(STTErrorType::MODEL_LOAD_FAILURE, slowCallback);
    
    STTErrorContext context = createTestErrorContext(STTErrorType::MODEL_LOAD_FAILURE, utteranceId);
    
    // Start recovery in a separate thread
    std::thread recoveryThread([this, &context]() {
        recovery_->handleError(context);
    });
    
    // Give it a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Should be in progress now
    EXPECT_TRUE(recovery_->isRecoveryInProgress(utteranceId));
    
    // Wait for completion
    recoveryThread.join();
    
    // Should no longer be in progress
    EXPECT_FALSE(recovery_->isRecoveryInProgress(utteranceId));
}

TEST_F(STTErrorRecoveryTest, RecoveryCancellation) {
    uint32_t utteranceId = 456;
    
    // Create a slow recovery callback
    RecoveryCallback slowCallback = [](const STTErrorContext& context) -> bool {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return true;
    };
    
    recovery_->registerRecoveryCallback(STTErrorType::TRANSCRIPTION_TIMEOUT, slowCallback);
    
    STTErrorContext context = createTestErrorContext(STTErrorType::TRANSCRIPTION_TIMEOUT, utteranceId);
    
    // Start recovery in a separate thread
    std::thread recoveryThread([this, &context]() {
        recovery_->handleError(context);
    });
    
    // Give it a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_TRUE(recovery_->isRecoveryInProgress(utteranceId));
    
    // Cancel the recovery
    recovery_->cancelRecovery(utteranceId);
    
    // Should no longer be in progress
    EXPECT_FALSE(recovery_->isRecoveryInProgress(utteranceId));
    
    // Wait for thread to complete
    recoveryThread.join();
}

TEST_F(STTErrorRecoveryTest, StatisticsTracking) {
    // Initially no statistics
    auto stats = recovery_->getRecoveryStatistics();
    EXPECT_EQ(stats.size(), 0);
    
    // Handle some errors
    STTErrorContext context1 = createTestErrorContext(STTErrorType::MODEL_LOAD_FAILURE, 1);
    STTErrorContext context2 = createTestErrorContext(STTErrorType::GPU_MEMORY_ERROR, 2);
    
    recovery_->handleError(context1);
    recovery_->handleError(context2);
    recovery_->handleError(context1); // Same type again
    
    stats = recovery_->getRecoveryStatistics();
    EXPECT_GT(stats[STTErrorType::MODEL_LOAD_FAILURE], 0);
    EXPECT_GT(stats[STTErrorType::GPU_MEMORY_ERROR], 0);
}

TEST_F(STTErrorRecoveryTest, ErrorHistoryTracking) {
    // Initially no history
    auto history = recovery_->getRecentErrors(10);
    EXPECT_EQ(history.size(), 0);
    
    // Handle some errors
    STTErrorContext context1 = createTestErrorContext(STTErrorType::MODEL_LOAD_FAILURE, 1);
    STTErrorContext context2 = createTestErrorContext(STTErrorType::GPU_MEMORY_ERROR, 2);
    
    recovery_->handleError(context1);
    recovery_->handleError(context2);
    
    history = recovery_->getRecentErrors(10);
    EXPECT_EQ(history.size(), 2);
    
    // Most recent should be first
    EXPECT_EQ(history[0].errorType, STTErrorType::GPU_MEMORY_ERROR);
    EXPECT_EQ(history[1].errorType, STTErrorType::MODEL_LOAD_FAILURE);
}

TEST_F(STTErrorRecoveryTest, EnableDisableRecovery) {
    EXPECT_TRUE(recovery_->isEnabled());
    
    recovery_->setEnabled(false);
    EXPECT_FALSE(recovery_->isEnabled());
    
    // Disabled recovery should not attempt recovery
    STTErrorContext context = createTestErrorContext(STTErrorType::TRANSCRIPTION_TIMEOUT);
    RecoveryResult result = recovery_->handleError(context);
    
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.strategyUsed, RecoveryStrategy::NONE);
    EXPECT_EQ(result.resultMessage, "Error recovery is disabled");
    
    // Re-enable
    recovery_->setEnabled(true);
    EXPECT_TRUE(recovery_->isEnabled());
}

TEST_F(STTErrorRecoveryTest, CustomRecoveryConfiguration) {
    RecoveryConfig customConfig;
    customConfig.maxRetryAttempts = 5;
    customConfig.initialBackoffMs = std::chrono::milliseconds(50);
    customConfig.enableGPUFallback = false;
    
    recovery_->configureRecovery(STTErrorType::WHISPER_INFERENCE_ERROR, customConfig);
    
    // The configuration should be used for this error type
    STTErrorContext context = createTestErrorContext(STTErrorType::WHISPER_INFERENCE_ERROR);
    RecoveryResult result = recovery_->handleError(context);
    
    // Should attempt recovery (even if it fails due to no callback)
    EXPECT_NE(result.strategyUsed, RecoveryStrategy::NONE);
}

TEST_F(STTErrorRecoveryTest, ClearHistoryAndStatistics) {
    // Add some errors
    STTErrorContext context = createTestErrorContext(STTErrorType::MODEL_LOAD_FAILURE);
    recovery_->handleError(context);
    
    // Verify they exist
    EXPECT_GT(recovery_->getRecentErrors(10).size(), 0);
    EXPECT_GT(recovery_->getRecoveryStatistics().size(), 0);
    
    // Clear everything
    recovery_->clearHistory();
    
    // Should be empty now
    EXPECT_EQ(recovery_->getRecentErrors(10).size(), 0);
    EXPECT_EQ(recovery_->getRecoveryStatistics().size(), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}