#pragma once

#include "utils/error_handler.hpp"
#include "stt/quantization_config.hpp"
#include <string>
#include <memory>
#include <mutex>
#include <chrono>
#include <queue>
#include <unordered_map>
#include <functional>
#include <atomic>

namespace stt {

/**
 * STT-specific error types for detailed error classification
 */
enum class STTErrorType {
    MODEL_LOAD_FAILURE,
    GPU_MEMORY_ERROR,
    TRANSCRIPTION_TIMEOUT,
    AUDIO_FORMAT_ERROR,
    NETWORK_ERROR,
    RESOURCE_EXHAUSTION,
    QUANTIZATION_ERROR,
    STREAMING_BUFFER_OVERFLOW,
    LANGUAGE_DETECTION_FAILURE,
    WHISPER_INFERENCE_ERROR,
    VAD_PROCESSING_ERROR,
    UNKNOWN_ERROR
};

/**
 * Recovery strategy types for different error scenarios
 */
enum class RecoveryStrategy {
    NONE,                    // No recovery attempted
    RETRY_SAME,             // Retry with same configuration
    RETRY_WITH_BACKOFF,     // Retry with exponential backoff
    FALLBACK_GPU_TO_CPU,    // Switch from GPU to CPU processing
    FALLBACK_QUANTIZATION,  // Switch to lower precision model
    RESTART_COMPONENT,      // Restart STT component
    CLEAR_BUFFERS,         // Clear audio buffers and restart
    REDUCE_QUALITY,        // Reduce processing quality/complexity
    NOTIFY_CLIENT          // Notify client of degraded service
};

/**
 * Error context information for STT operations
 */
struct STTErrorContext {
    STTErrorType errorType;
    std::string errorMessage;
    std::string detailedDescription;
    uint32_t utteranceId;
    std::string sessionId;
    std::chrono::steady_clock::time_point timestamp;
    int retryCount;
    bool isRecoverable;
    
    // STT-specific context
    std::string modelPath;
    QuantizationLevel currentQuantization;
    bool wasUsingGPU;
    int gpuDeviceId;
    size_t audioBufferSize;
    std::string language;
    
    STTErrorContext() 
        : errorType(STTErrorType::UNKNOWN_ERROR)
        , utteranceId(0)
        , retryCount(0)
        , isRecoverable(true)
        , currentQuantization(QuantizationLevel::FP32)
        , wasUsingGPU(false)
        , gpuDeviceId(-1)
        , audioBufferSize(0) {
        timestamp = std::chrono::steady_clock::now();
    }
};

/**
 * Recovery attempt result information
 */
struct RecoveryResult {
    bool success;
    RecoveryStrategy strategyUsed;
    std::string resultMessage;
    std::chrono::milliseconds recoveryTime;
    bool requiresClientNotification;
    
    RecoveryResult() 
        : success(false)
        , strategyUsed(RecoveryStrategy::NONE)
        , recoveryTime(0)
        , requiresClientNotification(false) {}
};

/**
 * Recovery configuration for different error types
 */
struct RecoveryConfig {
    int maxRetryAttempts;
    std::chrono::milliseconds initialBackoffMs;
    std::chrono::milliseconds maxBackoffMs;
    double backoffMultiplier;
    bool enableGPUFallback;
    bool enableQuantizationFallback;
    bool enableBufferClear;
    std::chrono::milliseconds recoveryTimeoutMs;
    
    RecoveryConfig() 
        : maxRetryAttempts(3)
        , initialBackoffMs(100)
        , maxBackoffMs(5000)
        , backoffMultiplier(2.0)
        , enableGPUFallback(true)
        , enableQuantizationFallback(true)
        , enableBufferClear(true)
        , recoveryTimeoutMs(10000) {}
};

/**
 * Callback types for recovery operations
 */
using RecoveryCallback = std::function<bool(const STTErrorContext&)>;
using NotificationCallback = std::function<void(const STTErrorContext&, const RecoveryResult&)>;

/**
 * STT Error Recovery System
 * 
 * Handles transcription failures with intelligent recovery strategies including:
 * - Retry logic with exponential backoff
 * - GPU to CPU fallback
 * - Model quantization fallback (FP32 → FP16 → INT8)
 * - Error context tracking and logging
 * - Recovery attempt monitoring
 */
class STTErrorRecovery {
public:
    STTErrorRecovery();
    ~STTErrorRecovery();
    
    /**
     * Initialize the error recovery system
     * @param defaultConfig Default recovery configuration
     * @return true if initialization successful
     */
    bool initialize(const RecoveryConfig& defaultConfig = RecoveryConfig());
    
    /**
     * Handle an STT error and attempt recovery
     * @param errorContext Error context information
     * @return Recovery result
     */
    RecoveryResult handleError(const STTErrorContext& errorContext);
    
    /**
     * Register a recovery callback for specific error types
     * @param errorType Error type to handle
     * @param callback Recovery callback function
     */
    void registerRecoveryCallback(STTErrorType errorType, RecoveryCallback callback);
    
    /**
     * Register a notification callback for recovery events
     * @param callback Notification callback function
     */
    void setNotificationCallback(NotificationCallback callback);
    
    /**
     * Configure recovery settings for specific error types
     * @param errorType Error type to configure
     * @param config Recovery configuration
     */
    void configureRecovery(STTErrorType errorType, const RecoveryConfig& config);
    
    /**
     * Get recovery statistics
     * @return Map of error types to recovery statistics
     */
    std::unordered_map<STTErrorType, size_t> getRecoveryStatistics() const;
    
    /**
     * Get recent error history
     * @param maxCount Maximum number of recent errors to return
     * @return Vector of recent error contexts
     */
    std::vector<STTErrorContext> getRecentErrors(size_t maxCount = 10) const;
    
    /**
     * Clear error history and statistics
     */
    void clearHistory();
    
    /**
     * Check if recovery is currently in progress for an utterance
     * @param utteranceId Utterance ID to check
     * @return true if recovery is in progress
     */
    bool isRecoveryInProgress(uint32_t utteranceId) const;
    
    /**
     * Cancel ongoing recovery for an utterance
     * @param utteranceId Utterance ID to cancel recovery for
     */
    void cancelRecovery(uint32_t utteranceId);
    
    /**
     * Enable or disable the recovery system
     * @param enabled true to enable recovery
     */
    void setEnabled(bool enabled);
    
    /**
     * Check if recovery system is enabled
     * @return true if enabled
     */
    bool isEnabled() const;
    
    /**
     * Get the last error message
     * @return Last error message
     */
    std::string getLastError() const;

private:
    /**
     * Active recovery attempt tracking
     */
    struct ActiveRecovery {
        STTErrorContext context;
        RecoveryConfig config;
        std::chrono::steady_clock::time_point startTime;
        int currentAttempt;
        RecoveryStrategy currentStrategy;
        std::chrono::milliseconds nextRetryDelay;
        
        ActiveRecovery() : currentAttempt(0), currentStrategy(RecoveryStrategy::NONE), nextRetryDelay(0) {}
    };
    
    // Core recovery methods
    RecoveryResult attemptRecovery(const STTErrorContext& context, const RecoveryConfig& config);
    RecoveryStrategy selectRecoveryStrategy(const STTErrorContext& context, int attemptNumber);
    bool executeRecoveryStrategy(const STTErrorContext& context, RecoveryStrategy strategy);
    
    // Specific recovery strategies
    bool retryWithBackoff(const STTErrorContext& context, std::chrono::milliseconds delay);
    bool fallbackGPUToCPU(const STTErrorContext& context);
    bool fallbackQuantization(const STTErrorContext& context);
    bool restartComponent(const STTErrorContext& context);
    bool clearBuffers(const STTErrorContext& context);
    bool reduceQuality(const STTErrorContext& context);
    
    // Utility methods
    std::chrono::milliseconds calculateBackoffDelay(int attemptNumber, const RecoveryConfig& config);
    bool isErrorRecoverable(const STTErrorContext& context);
    void logRecoveryAttempt(const STTErrorContext& context, RecoveryStrategy strategy, bool success);
    void updateStatistics(STTErrorType errorType, bool recoverySuccess);
    STTErrorType classifyError(const std::string& errorMessage);
    
    // Member variables
    bool initialized_;
    std::atomic<bool> enabled_;
    std::string lastError_;
    
    // Configuration
    RecoveryConfig defaultConfig_;
    std::unordered_map<STTErrorType, RecoveryConfig> errorConfigs_;
    
    // Callbacks
    std::unordered_map<STTErrorType, RecoveryCallback> recoveryCallbacks_;
    NotificationCallback notificationCallback_;
    
    // Active recovery tracking
    std::unordered_map<uint32_t, std::unique_ptr<ActiveRecovery>> activeRecoveries_;
    
    // Statistics and history
    std::unordered_map<STTErrorType, size_t> recoveryAttempts_;
    std::unordered_map<STTErrorType, size_t> recoverySuccesses_;
    std::queue<STTErrorContext> errorHistory_;
    static constexpr size_t MAX_ERROR_HISTORY = 100;
    
    // Thread safety
    mutable std::mutex mutex_;
    mutable std::mutex statisticsMutex_;
    mutable std::mutex historyMutex_;
};

/**
 * Utility functions for STT error handling
 */
namespace error_utils {
    
    /**
     * Convert STTErrorType to string
     * @param errorType Error type to convert
     * @return String representation
     */
    std::string errorTypeToString(STTErrorType errorType);
    
    /**
     * Convert RecoveryStrategy to string
     * @param strategy Recovery strategy to convert
     * @return String representation
     */
    std::string recoveryStrategyToString(RecoveryStrategy strategy);
    
    /**
     * Create STTErrorContext from exception
     * @param e Exception to convert
     * @param utteranceId Associated utterance ID
     * @param sessionId Associated session ID
     * @return STTErrorContext
     */
    STTErrorContext createErrorContext(const std::exception& e, uint32_t utteranceId = 0, const std::string& sessionId = "");
    
    /**
     * Check if error is transient (likely to succeed on retry)
     * @param errorType Error type to check
     * @return true if error is likely transient
     */
    bool isTransientError(STTErrorType errorType);
    
    /**
     * Get recommended recovery strategy for error type
     * @param errorType Error type
     * @param attemptNumber Current attempt number
     * @return Recommended recovery strategy
     */
    RecoveryStrategy getRecommendedStrategy(STTErrorType errorType, int attemptNumber);
}

} // namespace stt