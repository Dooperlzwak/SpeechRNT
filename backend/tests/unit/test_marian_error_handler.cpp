#include <gtest/gtest.h>
#include "mt/marian_error_handler.hpp"
#include <chrono>
#include <thread>

using namespace speechrnt::mt;

class MarianErrorHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        errorHandler = std::make_unique<MarianErrorHandler>();
        errorHandler->initialize();
    }
    
    void TearDown() override {
        errorHandler.reset();
    }
    
    std::unique_ptr<MarianErrorHandler> errorHandler;
};

TEST_F(MarianErrorHandlerTest, InitializationTest) {
    EXPECT_TRUE(errorHandler != nullptr);
    EXPECT_FALSE(errorHandler->isInDegradedMode());
}

TEST_F(MarianErrorHandlerTest, ErrorCategorizationTest) {
    ErrorContext context;
    context.component = "TestComponent";
    context.operation = "TestOperation";
    
    // Test timeout error handling
    RecoveryResult timeoutResult = errorHandler->handleError("Operation timed out", context);
    EXPECT_EQ(timeoutResult.strategyUsed, RecoveryStrategy::RETRY);
    
    // Test GPU error handling
    RecoveryResult gpuResult = errorHandler->handleError("CUDA initialization failed", context);
    EXPECT_EQ(gpuResult.strategyUsed, RecoveryStrategy::FALLBACK_CPU);
    
    // Test model corruption error
    RecoveryResult corruptionResult = errorHandler->handleError("Model file is corrupted", context);
    EXPECT_EQ(corruptionResult.strategyUsed, RecoveryStrategy::DEGRADED_MODE);
}

TEST_F(MarianErrorHandlerTest, DegradedModeTest) {
    ErrorContext context;
    context.component = "TestComponent";
    
    // Enter degraded mode
    EXPECT_TRUE(errorHandler->enterDegradedMode("Test critical error", context));
    EXPECT_TRUE(errorHandler->isInDegradedMode());
    
    // Check degraded mode status
    auto status = errorHandler->getDegradedModeStatus();
    EXPECT_TRUE(status.active);
    EXPECT_EQ(status.reason, "Test critical error");
    EXPECT_FALSE(status.activeRestrictions.empty());
    
    // Exit degraded mode
    EXPECT_TRUE(errorHandler->exitDegradedMode());
    EXPECT_FALSE(errorHandler->isInDegradedMode());
}

TEST_F(MarianErrorHandlerTest, RetryConfigurationTest) {
    RetryConfig config(2, std::chrono::milliseconds(100), std::chrono::milliseconds(500), 2.0, std::chrono::milliseconds(2000));
    errorHandler->setRetryConfig(ErrorCategory::TRANSLATION_TIMEOUT, config);
    
    // Test retry mechanism with a function that fails twice then succeeds
    int attemptCount = 0;
    auto operation = [&attemptCount]() -> int {
        attemptCount++;
        if (attemptCount < 3) {
            throw MarianTimeoutException("Simulated timeout");
        }
        return 42;
    };
    
    ErrorContext context;
    context.operation = "TestRetry";
    
    int result = errorHandler->executeWithRetry<int>(operation, config, context);
    EXPECT_EQ(result, 42);
    EXPECT_EQ(attemptCount, 3);
}

TEST_F(MarianErrorHandlerTest, TimeoutExecutionTest) {
    ErrorContext context;
    context.operation = "TestTimeout";
    
    // Test operation that completes within timeout
    auto fastOperation = []() -> int {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return 123;
    };
    
    int result = errorHandler->executeWithTimeout<int>(fastOperation, std::chrono::milliseconds(200), context);
    EXPECT_EQ(result, 123);
    
    // Test operation that times out
    auto slowOperation = []() -> int {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return 456;
    };
    
    EXPECT_THROW(
        errorHandler->executeWithTimeout<int>(slowOperation, std::chrono::milliseconds(100), context),
        MarianTimeoutException
    );
}

TEST_F(MarianErrorHandlerTest, ErrorStatisticsTest) {
    ErrorContext context;
    context.component = "TestComponent";
    
    // Generate some errors
    errorHandler->handleError("Test error 1", context);
    errorHandler->handleError("GPU failure", context);
    errorHandler->handleError("Timeout occurred", context);
    
    auto stats = errorHandler->getErrorStatistics();
    EXPECT_EQ(stats.totalErrors, 3);
    EXPECT_GT(stats.errorsByCategory.size(), 0);
    
    // Reset statistics
    errorHandler->resetErrorStatistics();
    auto resetStats = errorHandler->getErrorStatistics();
    EXPECT_EQ(resetStats.totalErrors, 0);
}

TEST_F(MarianErrorHandlerTest, CustomRecoveryStrategyTest) {
    // Register a custom recovery strategy
    bool customStrategyCalled = false;
    auto customStrategy = [&customStrategyCalled](const std::string& error, const ErrorContext& context) -> RecoveryResult {
        customStrategyCalled = true;
        RecoveryResult result;
        result.successful = true;
        result.message = "Custom recovery executed";
        result.strategyUsed = RecoveryStrategy::RETRY;
        return result;
    };
    
    errorHandler->registerRecoveryStrategy(ErrorCategory::TRANSLATION_FAILURE, customStrategy);
    
    ErrorContext context;
    RecoveryResult result = errorHandler->handleError("Translation failed", context);
    
    EXPECT_TRUE(customStrategyCalled);
    EXPECT_TRUE(result.successful);
    EXPECT_EQ(result.message, "Custom recovery executed");
}

TEST_F(MarianErrorHandlerTest, ModelCorruptionHandlingTest) {
    ErrorContext context;
    context.modelPath = "/tmp/test_model";
    context.component = "TestComponent";
    
    // Test model corruption detection (will fail since path doesn't exist)
    RecoveryResult result = errorHandler->checkAndHandleModelCorruption("/tmp/nonexistent_model", context);
    EXPECT_FALSE(result.successful);
    EXPECT_EQ(result.strategyUsed, RecoveryStrategy::RELOAD_MODEL);
}

TEST_F(MarianErrorHandlerTest, GPUFallbackTest) {
    ErrorContext context;
    context.gpuDeviceId = 0;
    context.component = "TestComponent";
    
    RecoveryResult result = errorHandler->handleGPUErrorWithFallback("CUDA out of memory", context);
    EXPECT_EQ(result.strategyUsed, RecoveryStrategy::FALLBACK_CPU);
    // Note: The actual success depends on the implementation details
}