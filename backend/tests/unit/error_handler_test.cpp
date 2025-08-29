#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "utils/error_handler.hpp"
#include <thread>
#include <chrono>

using namespace utils;
using ::testing::_;
using ::testing::Return;
using ::testing::InSequence;

class ErrorHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear error history before each test
        ErrorHandler::getInstance().clearErrorHistory();
    }

    void TearDown() override {
        // Clean up after each test
        ErrorHandler::getInstance().clearErrorHistory();
        ErrorHandler::getInstance().setErrorCallback(nullptr);
    }
};

TEST_F(ErrorHandlerTest, ErrorInfoCreation) {
    ErrorInfo error(ErrorCategory::WEBSOCKET, ErrorSeverity::ERROR, 
                   "Test message", "Test details", "Test context", "session123");
    
    EXPECT_EQ(error.category, ErrorCategory::WEBSOCKET);
    EXPECT_EQ(error.severity, ErrorSeverity::ERROR);
    EXPECT_EQ(error.message, "Test message");
    EXPECT_EQ(error.details, "Test details");
    EXPECT_EQ(error.context, "Test context");
    EXPECT_EQ(error.session_id, "session123");
    EXPECT_FALSE(error.id.empty());
    EXPECT_GT(error.timestamp.time_since_epoch().count(), 0);
}

TEST_F(ErrorHandlerTest, ErrorInfoUniqueIds) {
    ErrorInfo error1(ErrorCategory::STT, ErrorSeverity::WARNING, "Message 1");
    ErrorInfo error2(ErrorCategory::STT, ErrorSeverity::WARNING, "Message 2");
    
    EXPECT_NE(error1.id, error2.id);
}

TEST_F(ErrorHandlerTest, SpeechRNTExceptionBasic) {
    ErrorInfo error(ErrorCategory::TRANSLATION, ErrorSeverity::ERROR, 
                   "Translation failed", "Model not loaded");
    
    SpeechRNTException exception(error);
    
    EXPECT_STREQ(exception.what(), "Translation failed: Model not loaded");
    EXPECT_EQ(exception.getErrorInfo().category, ErrorCategory::TRANSLATION);
}

TEST_F(ErrorHandlerTest, SpecificExceptions) {
    // Test WebSocketException
    WebSocketException ws_ex("Connection lost", "session123");
    EXPECT_EQ(ws_ex.getErrorInfo().category, ErrorCategory::WEBSOCKET);
    EXPECT_EQ(ws_ex.getErrorInfo().session_id, "session123");
    
    // Test AudioProcessingException
    AudioProcessingException audio_ex("VAD failed", "voice_detection");
    EXPECT_EQ(audio_ex.getErrorInfo().category, ErrorCategory::AUDIO_PROCESSING);
    EXPECT_EQ(audio_ex.getErrorInfo().context, "voice_detection");
    
    // Test STTException
    STTException stt_ex("Whisper model error");
    EXPECT_EQ(stt_ex.getErrorInfo().category, ErrorCategory::STT);
    
    // Test TranslationException
    TranslationException mt_ex("Marian translation failed");
    EXPECT_EQ(mt_ex.getErrorInfo().category, ErrorCategory::TRANSLATION);
    
    // Test TTSException
    TTSException tts_ex("Coqui synthesis error");
    EXPECT_EQ(tts_ex.getErrorInfo().category, ErrorCategory::TTS);
    
    // Test ModelLoadingException
    ModelLoadingException model_ex("Failed to load model", "/path/to/model");
    EXPECT_EQ(model_ex.getErrorInfo().category, ErrorCategory::MODEL_LOADING);
    EXPECT_EQ(model_ex.getErrorInfo().severity, ErrorSeverity::CRITICAL);
    EXPECT_EQ(model_ex.getErrorInfo().details, "/path/to/model");
    
    // Test PipelineException
    PipelineException pipeline_ex("Pipeline stage failed", "STT_stage");
    EXPECT_EQ(pipeline_ex.getErrorInfo().category, ErrorCategory::PIPELINE);
    EXPECT_EQ(pipeline_ex.getErrorInfo().context, "STT_stage");
}

TEST_F(ErrorHandlerTest, ErrorReporting) {
    auto& handler = ErrorHandler::getInstance();
    
    ErrorInfo error(ErrorCategory::AUDIO_PROCESSING, ErrorSeverity::WARNING, 
                   "Audio buffer overflow");
    
    handler.reportError(error);
    
    EXPECT_EQ(handler.getErrorCount(), 1);
    EXPECT_EQ(handler.getErrorCount(ErrorCategory::AUDIO_PROCESSING), 1);
    EXPECT_EQ(handler.getErrorCount(ErrorCategory::WEBSOCKET), 0);
}

TEST_F(ErrorHandlerTest, ErrorCallback) {
    auto& handler = ErrorHandler::getInstance();
    
    bool callback_called = false;
    ErrorInfo received_error(ErrorCategory::UNKNOWN, ErrorSeverity::INFO, "");
    
    handler.setErrorCallback([&](const ErrorInfo& error) {
        callback_called = true;
        received_error = error;
    });
    
    ErrorInfo test_error(ErrorCategory::TTS, ErrorSeverity::ERROR, "TTS failed");
    handler.reportError(test_error);
    
    EXPECT_TRUE(callback_called);
    EXPECT_EQ(received_error.category, ErrorCategory::TTS);
    EXPECT_EQ(received_error.message, "TTS failed");
}

TEST_F(ErrorHandlerTest, ErrorHistory) {
    auto& handler = ErrorHandler::getInstance();
    
    // Add multiple errors
    for (int i = 0; i < 5; ++i) {
        ErrorInfo error(ErrorCategory::PIPELINE, ErrorSeverity::INFO, 
                       "Error " + std::to_string(i));
        handler.reportError(error);
    }
    
    EXPECT_EQ(handler.getErrorCount(), 5);
    
    auto recent_errors = handler.getRecentErrors(3);
    EXPECT_EQ(recent_errors.size(), 3);
    EXPECT_EQ(recent_errors[2].message, "Error 4"); // Most recent
    
    handler.clearErrorHistory();
    EXPECT_EQ(handler.getErrorCount(), 0);
}

TEST_F(ErrorHandlerTest, RecoveryActions) {
    auto& handler = ErrorHandler::getInstance();
    
    bool recovery_called = false;
    handler.addRecoveryAction(ErrorCategory::WEBSOCKET, [&]() -> bool {
        recovery_called = true;
        return true; // Recovery successful
    });
    
    ErrorInfo error(ErrorCategory::WEBSOCKET, ErrorSeverity::ERROR, "Connection lost");
    bool recovery_result = handler.attemptRecovery(error);
    
    EXPECT_TRUE(recovery_called);
    EXPECT_TRUE(recovery_result);
}

TEST_F(ErrorHandlerTest, RecoveryFailure) {
    auto& handler = ErrorHandler::getInstance();
    
    handler.addRecoveryAction(ErrorCategory::STT, [&]() -> bool {
        return false; // Recovery failed
    });
    
    ErrorInfo error(ErrorCategory::STT, ErrorSeverity::ERROR, "STT model crashed");
    bool recovery_result = handler.attemptRecovery(error);
    
    EXPECT_FALSE(recovery_result);
}

TEST_F(ErrorHandlerTest, NoRecoveryAction) {
    auto& handler = ErrorHandler::getInstance();
    
    ErrorInfo error(ErrorCategory::UNKNOWN, ErrorSeverity::ERROR, "Unknown error");
    bool recovery_result = handler.attemptRecovery(error);
    
    EXPECT_FALSE(recovery_result); // No recovery action registered
}

TEST_F(ErrorHandlerTest, GracefulDegradation) {
    auto& handler = ErrorHandler::getInstance();
    
    EXPECT_TRUE(handler.isGracefulDegradationEnabled()); // Default enabled
    
    handler.enableGracefulDegradation(false);
    EXPECT_FALSE(handler.isGracefulDegradationEnabled());
    
    handler.enableGracefulDegradation(true);
    EXPECT_TRUE(handler.isGracefulDegradationEnabled());
}

TEST_F(ErrorHandlerTest, ExceptionReporting) {
    auto& handler = ErrorHandler::getInstance();
    
    STTException stt_exception("Whisper model failed");
    handler.reportError(stt_exception, "transcription_context", "session456");
    
    EXPECT_EQ(handler.getErrorCount(ErrorCategory::STT), 1);
    
    auto recent_errors = handler.getRecentErrors(1);
    EXPECT_EQ(recent_errors[0].context, "transcription_context");
    EXPECT_EQ(recent_errors[0].session_id, "session456");
}

class ErrorContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure clean state
        EXPECT_TRUE(ErrorContext::getCurrentContext().empty());
        EXPECT_TRUE(ErrorContext::getCurrentSessionId().empty());
    }
};

TEST_F(ErrorContextTest, BasicContextManagement) {
    EXPECT_TRUE(ErrorContext::getCurrentContext().empty());
    
    {
        ErrorContext ctx("test_context");
        EXPECT_EQ(ErrorContext::getCurrentContext(), "test_context");
    }
    
    EXPECT_TRUE(ErrorContext::getCurrentContext().empty());
}

TEST_F(ErrorContextTest, NestedContexts) {
    {
        ErrorContext ctx1("outer_context");
        EXPECT_EQ(ErrorContext::getCurrentContext(), "outer_context");
        
        {
            ErrorContext ctx2("inner_context");
            EXPECT_EQ(ErrorContext::getCurrentContext(), "inner_context");
        }
        
        EXPECT_EQ(ErrorContext::getCurrentContext(), "outer_context");
    }
    
    EXPECT_TRUE(ErrorContext::getCurrentContext().empty());
}

TEST_F(ErrorContextTest, SessionIdManagement) {
    EXPECT_TRUE(ErrorContext::getCurrentSessionId().empty());
    
    {
        ErrorContext ctx("test_context", "session123");
        EXPECT_EQ(ErrorContext::getCurrentContext(), "test_context");
        EXPECT_EQ(ErrorContext::getCurrentSessionId(), "session123");
    }
    
    EXPECT_TRUE(ErrorContext::getCurrentContext().empty());
    EXPECT_TRUE(ErrorContext::getCurrentSessionId().empty());
}

TEST_F(ErrorContextTest, ThreadLocalStorage) {
    std::string main_context;
    std::string thread_context;
    
    {
        ErrorContext ctx("main_context");
        main_context = ErrorContext::getCurrentContext();
        
        std::thread t([&]() {
            // Thread should start with empty context
            EXPECT_TRUE(ErrorContext::getCurrentContext().empty());
            
            ErrorContext thread_ctx("thread_context");
            thread_context = ErrorContext::getCurrentContext();
        });
        
        t.join();
    }
    
    EXPECT_EQ(main_context, "main_context");
    EXPECT_EQ(thread_context, "thread_context");
}

// Test error handling macros
TEST_F(ErrorHandlerTest, ErrorHandlingMacros) {
    auto& handler = ErrorHandler::getInstance();
    
    bool callback_called = false;
    handler.setErrorCallback([&](const ErrorInfo& error) {
        callback_called = true;
        EXPECT_EQ(error.category, ErrorCategory::PIPELINE);
        EXPECT_EQ(error.severity, ErrorSeverity::WARNING);
        EXPECT_EQ(error.message, "Test macro error");
        EXPECT_EQ(error.details, "Additional details");
    });
    
    {
        ErrorContext ctx("macro_test");
        HANDLE_ERROR(ErrorCategory::PIPELINE, ErrorSeverity::WARNING, 
                    "Test macro error", "Additional details");
    }
    
    EXPECT_TRUE(callback_called);
}

// Performance test for error handling
TEST_F(ErrorHandlerTest, PerformanceTest) {
    auto& handler = ErrorHandler::getInstance();
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Report many errors quickly
    for (int i = 0; i < 1000; ++i) {
        ErrorInfo error(ErrorCategory::AUDIO_PROCESSING, ErrorSeverity::INFO, 
                       "Performance test error " + std::to_string(i));
        handler.reportError(error);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_EQ(handler.getErrorCount(), 1000);
    EXPECT_LT(duration.count(), 1000); // Should complete in less than 1 second
}