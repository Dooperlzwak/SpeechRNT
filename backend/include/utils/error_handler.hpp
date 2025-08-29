#pragma once

#include <string>
#include <exception>
#include <memory>
#include <functional>
#include <chrono>
#include <map>
#include <mutex>
#include <vector>

namespace utils {

/**
 * Error severity levels
 */
enum class ErrorSeverity {
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

/**
 * Error categories for better classification
 */
enum class ErrorCategory {
    WEBSOCKET,
    AUDIO_PROCESSING,
    STT,
    TRANSLATION,
    TTS,
    MODEL_LOADING,
    PIPELINE,
    SYSTEM,
    UNKNOWN
};

/**
 * Structured error information
 */
struct ErrorInfo {
    std::string id;
    ErrorCategory category;
    ErrorSeverity severity;
    std::string message;
    std::string details;
    std::string context;
    std::chrono::steady_clock::time_point timestamp;
    std::string session_id;
    
    ErrorInfo(ErrorCategory cat, ErrorSeverity sev, const std::string& msg, 
              const std::string& det = "", const std::string& ctx = "", 
              const std::string& sid = "");
};

/**
 * Custom exception classes for different error types
 */
class SpeechRNTException : public std::exception {
public:
    explicit SpeechRNTException(const ErrorInfo& error_info);
    const char* what() const noexcept override;
    const ErrorInfo& getErrorInfo() const { return error_info_; }

private:
    ErrorInfo error_info_;
    mutable std::string what_message_;
};

class WebSocketException : public SpeechRNTException {
public:
    WebSocketException(const std::string& message, const std::string& session_id = "");
};

class AudioProcessingException : public SpeechRNTException {
public:
    AudioProcessingException(const std::string& message, const std::string& context = "");
};

class STTException : public SpeechRNTException {
public:
    STTException(const std::string& message, const std::string& context = "");
};

class TranslationException : public SpeechRNTException {
public:
    TranslationException(const std::string& message, const std::string& context = "");
};

class TTSException : public SpeechRNTException {
public:
    TTSException(const std::string& message, const std::string& context = "");
};

class ModelLoadingException : public SpeechRNTException {
public:
    ModelLoadingException(const std::string& message, const std::string& model_path = "");
};

class PipelineException : public SpeechRNTException {
public:
    PipelineException(const std::string& message, const std::string& stage = "");
};

/**
 * Error recovery strategies
 */
enum class RecoveryStrategy {
    NONE,
    RETRY,
    FALLBACK,
    RESTART_COMPONENT,
    NOTIFY_CLIENT
};

/**
 * Error handler callback type
 */
using ErrorCallback = std::function<void(const ErrorInfo&)>;

/**
 * Recovery action callback type
 */
using RecoveryAction = std::function<bool()>;

/**
 * Central error handler for the application
 */
class ErrorHandler {
public:
    static ErrorHandler& getInstance();
    
    // Error reporting
    void reportError(const ErrorInfo& error);
    void reportError(const std::exception& e, const std::string& context = "", 
                    const std::string& session_id = "");
    
    // Error callbacks
    void setErrorCallback(ErrorCallback callback);
    void addRecoveryAction(ErrorCategory category, RecoveryAction action);
    
    // Error statistics
    size_t getErrorCount(ErrorCategory category = ErrorCategory::UNKNOWN) const;
    std::vector<ErrorInfo> getRecentErrors(size_t count = 10) const;
    void clearErrorHistory();
    
    // Recovery mechanisms
    bool attemptRecovery(const ErrorInfo& error);
    void setMaxRetryAttempts(size_t max_attempts);
    
    // Graceful degradation
    void enableGracefulDegradation(bool enable);
    bool isGracefulDegradationEnabled() const;

private:
    ErrorHandler() = default;
    ~ErrorHandler() = default;
    ErrorHandler(const ErrorHandler&) = delete;
    ErrorHandler& operator=(const ErrorHandler&) = delete;
    
    void logError(const ErrorInfo& error);
    std::string generateErrorId();
    
    ErrorCallback error_callback_;
    std::map<ErrorCategory, RecoveryAction> recovery_actions_;
    std::vector<ErrorInfo> error_history_;
    size_t max_history_size_ = 1000;
    size_t max_retry_attempts_ = 3;
    bool graceful_degradation_enabled_ = true;
    
    mutable std::mutex mutex_;
};

/**
 * RAII error context manager
 */
class ErrorContext {
public:
    ErrorContext(const std::string& context, const std::string& session_id = "");
    ~ErrorContext();
    
    void setContext(const std::string& context);
    void setSessionId(const std::string& session_id);
    
    static std::string getCurrentContext();
    static std::string getCurrentSessionId();

private:
    std::string previous_context_;
    std::string previous_session_id_;
    
    static thread_local std::string current_context_;
    static thread_local std::string current_session_id_;
};

/**
 * Utility macros for error handling
 */
#define HANDLE_ERROR(category, severity, message, details) \
    do { \
        ErrorInfo error(category, severity, message, details, \
                       ErrorContext::getCurrentContext(), \
                       ErrorContext::getCurrentSessionId()); \
        ErrorHandler::getInstance().reportError(error); \
    } while(0)

#define HANDLE_EXCEPTION(e, context) \
    ErrorHandler::getInstance().reportError(e, context, ErrorContext::getCurrentSessionId())

#define TRY_WITH_ERROR_HANDLING(operation, category, error_message) \
    try { \
        operation; \
    } catch (const std::exception& e) { \
        HANDLE_ERROR(category, ErrorSeverity::ERROR, error_message, e.what()); \
        throw; \
    }

#define TRY_WITH_RECOVERY(operation, category, error_message, recovery_action) \
    try { \
        operation; \
    } catch (const std::exception& e) { \
        ErrorInfo error(category, ErrorSeverity::ERROR, error_message, e.what(), \
                       ErrorContext::getCurrentContext(), \
                       ErrorContext::getCurrentSessionId()); \
        ErrorHandler::getInstance().reportError(error); \
        if (!ErrorHandler::getInstance().attemptRecovery(error)) { \
            recovery_action; \
        } \
    }

} // namespace utils