#include "utils/error_handler.hpp"
#include "utils/logging.hpp"
#include <sstream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <mutex>

namespace utils {

// Thread-local storage for error context
thread_local std::string ErrorContext::current_context_;
thread_local std::string ErrorContext::current_session_id_;

// ErrorInfo implementation
ErrorInfo::ErrorInfo(ErrorCategory cat, ErrorSeverity sev, const std::string& msg, 
                     const std::string& det, const std::string& ctx, const std::string& sid)
    : category(cat), severity(sev), message(msg), details(det), context(ctx), 
      timestamp(std::chrono::steady_clock::now()), session_id(sid) {
    
    // Generate unique error ID
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << "err_";
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << dis(gen);
    }
    id = ss.str();
}

// SpeechRNTException implementation
SpeechRNTException::SpeechRNTException(const ErrorInfo& error_info) 
    : error_info_(error_info) {
}

const char* SpeechRNTException::what() const noexcept {
    if (what_message_.empty()) {
        what_message_ = error_info_.message;
        if (!error_info_.details.empty()) {
            what_message_ += ": " + error_info_.details;
        }
    }
    return what_message_.c_str();
}

// Specific exception implementations
WebSocketException::WebSocketException(const std::string& message, const std::string& session_id)
    : SpeechRNTException(ErrorInfo(ErrorCategory::WEBSOCKET, ErrorSeverity::ERROR, 
                                  message, "", "WebSocket", session_id)) {
}

AudioProcessingException::AudioProcessingException(const std::string& message, const std::string& context)
    : SpeechRNTException(ErrorInfo(ErrorCategory::AUDIO_PROCESSING, ErrorSeverity::ERROR, 
                                  message, "", context.empty() ? "AudioProcessing" : context)) {
}

STTException::STTException(const std::string& message, const std::string& context)
    : SpeechRNTException(ErrorInfo(ErrorCategory::STT, ErrorSeverity::ERROR, 
                                  message, "", context.empty() ? "STT" : context)) {
}

TranslationException::TranslationException(const std::string& message, const std::string& context)
    : SpeechRNTException(ErrorInfo(ErrorCategory::TRANSLATION, ErrorSeverity::ERROR, 
                                  message, "", context.empty() ? "Translation" : context)) {
}

TTSException::TTSException(const std::string& message, const std::string& context)
    : SpeechRNTException(ErrorInfo(ErrorCategory::TTS, ErrorSeverity::ERROR, 
                                  message, "", context.empty() ? "TTS" : context)) {
}

ModelLoadingException::ModelLoadingException(const std::string& message, const std::string& model_path)
    : SpeechRNTException(ErrorInfo(ErrorCategory::MODEL_LOADING, ErrorSeverity::CRITICAL, 
                                  message, model_path, "ModelLoading")) {
}

PipelineException::PipelineException(const std::string& message, const std::string& stage)
    : SpeechRNTException(ErrorInfo(ErrorCategory::PIPELINE, ErrorSeverity::ERROR, 
                                  message, "", stage.empty() ? "Pipeline" : stage)) {
}

// ErrorHandler implementation
ErrorHandler& ErrorHandler::getInstance() {
    static ErrorHandler instance;
    return instance;
}

void ErrorHandler::reportError(const ErrorInfo& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Log the error
    logError(error);
    
    // Add to history
    error_history_.push_back(error);
    if (error_history_.size() > max_history_size_) {
        error_history_.erase(error_history_.begin());
    }
    
    // Call error callback if set
    if (error_callback_) {
        try {
            error_callback_(error);
        } catch (const std::exception& e) {
            Logger::error("Error in error callback: " + std::string(e.what()));
        }
    }
    
    // Attempt recovery for non-critical errors
    if (error.severity != ErrorSeverity::CRITICAL && graceful_degradation_enabled_) {
        attemptRecovery(error);
    }
}

void ErrorHandler::reportError(const std::exception& e, const std::string& context, 
                              const std::string& session_id) {
    ErrorCategory category = ErrorCategory::UNKNOWN;
    ErrorSeverity severity = ErrorSeverity::ERROR;
    
    // Try to determine category from exception type
    if (dynamic_cast<const WebSocketException*>(&e)) {
        category = ErrorCategory::WEBSOCKET;
    } else if (dynamic_cast<const AudioProcessingException*>(&e)) {
        category = ErrorCategory::AUDIO_PROCESSING;
    } else if (dynamic_cast<const STTException*>(&e)) {
        category = ErrorCategory::STT;
    } else if (dynamic_cast<const TranslationException*>(&e)) {
        category = ErrorCategory::TRANSLATION;
    } else if (dynamic_cast<const TTSException*>(&e)) {
        category = ErrorCategory::TTS;
    } else if (dynamic_cast<const ModelLoadingException*>(&e)) {
        category = ErrorCategory::MODEL_LOADING;
        severity = ErrorSeverity::CRITICAL;
    } else if (dynamic_cast<const PipelineException*>(&e)) {
        category = ErrorCategory::PIPELINE;
    }
    
    ErrorInfo error(category, severity, e.what(), "", context, session_id);
    reportError(error);
}

void ErrorHandler::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_callback_ = callback;
}

void ErrorHandler::addRecoveryAction(ErrorCategory category, RecoveryAction action) {
    std::lock_guard<std::mutex> lock(mutex_);
    recovery_actions_[category] = action;
}

size_t ErrorHandler::getErrorCount(ErrorCategory category) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (category == ErrorCategory::UNKNOWN) {
        return error_history_.size();
    }
    
    return std::count_if(error_history_.begin(), error_history_.end(),
                        [category](const ErrorInfo& error) {
                            return error.category == category;
                        });
}

std::vector<ErrorInfo> ErrorHandler::getRecentErrors(size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (error_history_.size() <= count) {
        return error_history_;
    }
    
    return std::vector<ErrorInfo>(error_history_.end() - count, error_history_.end());
}

void ErrorHandler::clearErrorHistory() {
    std::lock_guard<std::mutex> lock(mutex_);
    error_history_.clear();
}

bool ErrorHandler::attemptRecovery(const ErrorInfo& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = recovery_actions_.find(error.category);
    if (it != recovery_actions_.end()) {
        try {
            Logger::info("Attempting recovery for error: " + error.message);
            bool success = it->second();
            if (success) {
                Logger::info("Recovery successful for error: " + error.id);
            } else {
                Logger::warn("Recovery failed for error: " + error.id);
            }
            return success;
        } catch (const std::exception& e) {
            Logger::error("Exception during recovery: " + std::string(e.what()));
            return false;
        }
    }
    
    return false;
}

void ErrorHandler::setMaxRetryAttempts(size_t max_attempts) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_retry_attempts_ = max_attempts;
}

void ErrorHandler::enableGracefulDegradation(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    graceful_degradation_enabled_ = enable;
}

bool ErrorHandler::isGracefulDegradationEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return graceful_degradation_enabled_;
}

void ErrorHandler::logError(const ErrorInfo& error) {
    std::string severity_str;
    switch (error.severity) {
        case ErrorSeverity::INFO: severity_str = "INFO"; break;
        case ErrorSeverity::WARNING: severity_str = "WARN"; break;
        case ErrorSeverity::ERROR: severity_str = "ERROR"; break;
        case ErrorSeverity::CRITICAL: severity_str = "CRITICAL"; break;
    }
    
    std::string category_str;
    switch (error.category) {
        case ErrorCategory::WEBSOCKET: category_str = "WebSocket"; break;
        case ErrorCategory::AUDIO_PROCESSING: category_str = "Audio"; break;
        case ErrorCategory::STT: category_str = "STT"; break;
        case ErrorCategory::TRANSLATION: category_str = "Translation"; break;
        case ErrorCategory::TTS: category_str = "TTS"; break;
        case ErrorCategory::MODEL_LOADING: category_str = "ModelLoading"; break;
        case ErrorCategory::PIPELINE: category_str = "Pipeline"; break;
        case ErrorCategory::SYSTEM: category_str = "System"; break;
        case ErrorCategory::UNKNOWN: category_str = "Unknown"; break;
    }
    
    std::stringstream log_message;
    log_message << "[" << error.id << "] " << category_str << " - " << error.message;
    
    if (!error.details.empty()) {
        log_message << " | Details: " << error.details;
    }
    
    if (!error.context.empty()) {
        log_message << " | Context: " << error.context;
    }
    
    if (!error.session_id.empty()) {
        log_message << " | Session: " << error.session_id;
    }
    
    switch (error.severity) {
        case ErrorSeverity::INFO:
            Logger::info(log_message.str());
            break;
        case ErrorSeverity::WARNING:
            Logger::warn(log_message.str());
            break;
        case ErrorSeverity::ERROR:
        case ErrorSeverity::CRITICAL:
            Logger::error(log_message.str());
            break;
    }
}

// ErrorContext implementation
ErrorContext::ErrorContext(const std::string& context, const std::string& session_id) 
    : previous_context_(current_context_), previous_session_id_(current_session_id_) {
    current_context_ = context;
    if (!session_id.empty()) {
        current_session_id_ = session_id;
    }
}

ErrorContext::~ErrorContext() {
    current_context_ = previous_context_;
    current_session_id_ = previous_session_id_;
}

void ErrorContext::setContext(const std::string& context) {
    current_context_ = context;
}

void ErrorContext::setSessionId(const std::string& session_id) {
    current_session_id_ = session_id;
}

std::string ErrorContext::getCurrentContext() {
    return current_context_;
}

std::string ErrorContext::getCurrentSessionId() {
    return current_session_id_;
}

} // namespace utils