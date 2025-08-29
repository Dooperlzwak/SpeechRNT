#pragma once

#include <string>
#include <stdexcept>
#include <chrono>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace speechrnt {
namespace mt {

/**
 * Error severity levels for categorizing errors
 */
enum class ErrorSeverity {
    INFO,
    WARNING,
    ERROR,
    CRITICAL,
    FATAL
};

/**
 * Error categories for different types of failures
 */
enum class ErrorCategory {
    MODEL_LOADING,
    MODEL_CORRUPTION,
    GPU_FAILURE,
    TRANSLATION_TIMEOUT,
    TRANSLATION_FAILURE,
    MEMORY_EXHAUSTION,
    CONFIGURATION_ERROR,
    NETWORK_ERROR,
    UNKNOWN
};

/**
 * Recovery strategy types
 */
enum class RecoveryStrategy {
    RETRY,
    FALLBACK_CPU,
    FALLBACK_MODEL,
    RELOAD_MODEL,
    DEGRADED_MODE,
    FAIL_SAFE,
    NO_RECOVERY
};

/**
 * Error context information
 */
struct ErrorContext {
    std::string component;
    std::string operation;
    std::string modelPath;
    std::string languagePair;
    int gpuDeviceId;
    size_t memoryUsageMB;
    std::chrono::steady_clock::time_point timestamp;
    std::string additionalInfo;
    
    ErrorContext() : gpuDeviceId(-1), memoryUsageMB(0) {
        timestamp = std::chrono::steady_clock::now();
    }
};

/**
 * Recovery action result
 */
struct RecoveryResult {
    bool successful;
    RecoveryStrategy strategyUsed;
    std::string message;
    std::chrono::milliseconds recoveryTime;
    bool requiresUserIntervention;
    
    RecoveryResult() : successful(false), strategyUsed(RecoveryStrategy::NO_RECOVERY), 
                      recoveryTime(0), requiresUserIntervention(false) {}
};

/**
 * Exception types for Marian NMT specific errors
 */
class MarianException : public std::runtime_error {
public:
    explicit MarianException(const std::string& message, ErrorCategory category = ErrorCategory::UNKNOWN, 
                           ErrorSeverity severity = ErrorSeverity::ERROR) 
        : std::runtime_error("Marian NMT Error: " + message), category_(category), severity_(severity) {}
    
    ErrorCategory getCategory() const { return category_; }
    ErrorSeverity getSeverity() const { return severity_; }

private:
    ErrorCategory category_;
    ErrorSeverity severity_;
};

class MarianModelException : public MarianException {
public:
    explicit MarianModelException(const std::string& message) 
        : MarianException("Model Error: " + message, ErrorCategory::MODEL_LOADING, ErrorSeverity::ERROR) {}
};

class MarianGPUException : public MarianException {
public:
    explicit MarianGPUException(const std::string& message) 
        : MarianException("GPU Error: " + message, ErrorCategory::GPU_FAILURE, ErrorSeverity::WARNING) {}
};

class MarianTranslationException : public MarianException {
public:
    explicit MarianTranslationException(const std::string& message) 
        : MarianException("Translation Error: " + message, ErrorCategory::TRANSLATION_FAILURE, ErrorSeverity::ERROR) {}
};

class MarianTimeoutException : public MarianException {
public:
    explicit MarianTimeoutException(const std::string& message) 
        : MarianException("Timeout Error: " + message, ErrorCategory::TRANSLATION_TIMEOUT, ErrorSeverity::WARNING) {}
};

class MarianCorruptionException : public MarianException {
public:
    explicit MarianCorruptionException(const std::string& message) 
        : MarianException("Corruption Error: " + message, ErrorCategory::MODEL_CORRUPTION, ErrorSeverity::CRITICAL) {}
};

/**
 * Retry configuration for different operations
 */
struct RetryConfig {
    int maxRetries;
    std::chrono::milliseconds initialDelay;
    std::chrono::milliseconds maxDelay;
    double backoffMultiplier;
    std::chrono::milliseconds timeout;
    
    RetryConfig() : maxRetries(3), initialDelay(100), maxDelay(5000), 
                   backoffMultiplier(2.0), timeout(30000) {}
    
    RetryConfig(int retries, std::chrono::milliseconds delay, std::chrono::milliseconds maxDel, 
               double multiplier, std::chrono::milliseconds timeoutMs)
        : maxRetries(retries), initialDelay(delay), maxDelay(maxDel), 
          backoffMultiplier(multiplier), timeout(timeoutMs) {}
};

/**
 * Degraded mode configuration
 */
struct DegradedModeConfig {
    bool enableFallbackTranslation;
    bool enableSimplifiedModels;
    bool enableCPUOnlyMode;
    bool enableReducedQuality;
    float qualityThreshold;
    std::chrono::minutes maxDegradedTime;
    
    DegradedModeConfig() : enableFallbackTranslation(true), enableSimplifiedModels(true),
                          enableCPUOnlyMode(true), enableReducedQuality(true),
                          qualityThreshold(0.3f), maxDegradedTime(std::chrono::minutes(30)) {}
};

/**
 * Error statistics for monitoring
 */
struct ErrorStatistics {
    size_t totalErrors;
    size_t recoveredErrors;
    size_t criticalErrors;
    std::unordered_map<ErrorCategory, size_t> errorsByCategory;
    std::unordered_map<RecoveryStrategy, size_t> recoveryStrategiesUsed;
    std::chrono::steady_clock::time_point lastError;
    std::chrono::milliseconds totalRecoveryTime;
    
    ErrorStatistics() : totalErrors(0), recoveredErrors(0), criticalErrors(0), totalRecoveryTime(0) {}
};

/**
 * Enhanced error handling and recovery system for Marian NMT operations
 */
class MarianErrorHandler {
public:
    MarianErrorHandler();
    ~MarianErrorHandler();
    
    /**
     * Initialize error handler with configuration
     * @param configPath Path to error handling configuration file
     * @return true if initialization successful
     */
    bool initialize(const std::string& configPath = "");
    
    /**
     * Handle error with automatic recovery attempt
     * @param error Error message or exception
     * @param context Error context information
     * @return Recovery result
     */
    RecoveryResult handleError(const std::string& error, const ErrorContext& context);
    RecoveryResult handleError(const MarianException& exception, const ErrorContext& context);
    
    /**
     * Execute operation with timeout and retry logic
     * @param operation Function to execute
     * @param config Retry configuration
     * @param context Error context for logging
     * @return Result of the operation
     */
    template<typename T>
    T executeWithRetry(std::function<T()> operation, const RetryConfig& config, const ErrorContext& context);
    
    /**
     * Execute operation with timeout
     * @param operation Function to execute
     * @param timeout Maximum execution time
     * @param context Error context for logging
     * @return Result of the operation
     */
    template<typename T>
    T executeWithTimeout(std::function<T()> operation, std::chrono::milliseconds timeout, const ErrorContext& context);
    
    /**
     * Check and handle model corruption
     * @param modelPath Path to model to check
     * @param context Error context
     * @return Recovery result
     */
    RecoveryResult checkAndHandleModelCorruption(const std::string& modelPath, const ErrorContext& context);
    
    /**
     * Handle GPU errors with automatic CPU fallback
     * @param error GPU error message
     * @param context Error context including GPU device info
     * @return Recovery result
     */
    RecoveryResult handleGPUErrorWithFallback(const std::string& error, const ErrorContext& context);
    
    /**
     * Enter degraded mode operation
     * @param reason Reason for entering degraded mode
     * @param context Error context
     * @return true if degraded mode activated successfully
     */
    bool enterDegradedMode(const std::string& reason, const ErrorContext& context);
    
    /**
     * Exit degraded mode and attempt normal operation
     * @return true if successfully exited degraded mode
     */
    bool exitDegradedMode();
    
    /**
     * Check if system is in degraded mode
     * @return true if in degraded mode
     */
    bool isInDegradedMode() const;
    
    /**
     * Get degraded mode status and information
     * @return Degraded mode status information
     */
    struct DegradedModeStatus {
        bool active;
        std::string reason;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::minutes duration;
        std::vector<std::string> activeRestrictions;
    };
    DegradedModeStatus getDegradedModeStatus() const;
    
    /**
     * Set retry configuration for specific error categories
     * @param category Error category
     * @param config Retry configuration
     */
    void setRetryConfig(ErrorCategory category, const RetryConfig& config);
    
    /**
     * Set degraded mode configuration
     * @param config Degraded mode configuration
     */
    void setDegradedModeConfig(const DegradedModeConfig& config);
    
    /**
     * Get error statistics
     * @return Current error statistics
     */
    ErrorStatistics getErrorStatistics() const;
    
    /**
     * Reset error statistics
     */
    void resetErrorStatistics();
    
    /**
     * Register custom recovery strategy
     * @param category Error category
     * @param strategy Recovery function
     */
    void registerRecoveryStrategy(ErrorCategory category, std::function<RecoveryResult(const std::string&, const ErrorContext&)> strategy);
    
    // Legacy methods (enhanced with new functionality)
    
    /**
     * Handle model loading errors with recovery
     * @param error Original error message
     * @param modelPath Path to the model that failed to load
     * @return Formatted error message with recovery suggestions
     */
    static std::string handleModelLoadingError(const std::string& error, const std::string& modelPath);
    
    /**
     * Handle GPU initialization errors with fallback
     * @param error Original error message
     * @param deviceId GPU device ID that failed
     * @return Formatted error message with fallback suggestions
     */
    static std::string handleGPUError(const std::string& error, int deviceId);
    
    /**
     * Handle translation inference errors with retry
     * @param error Original error message
     * @param sourceText Input text that caused the error
     * @return Formatted error message with debugging info
     */
    static std::string handleTranslationError(const std::string& error, const std::string& sourceText);
    
    /**
     * Handle model corruption or validation errors with reloading
     * @param error Original error message
     * @param modelPath Path to the corrupted model
     * @return Formatted error message with recovery steps
     */
    static std::string handleModelCorruptionError(const std::string& error, const std::string& modelPath);
    
    /**
     * Check if an error is recoverable
     * @param error Error message to analyze
     * @return true if the error can be recovered from
     */
    static bool isRecoverableError(const std::string& error);
    
    /**
     * Get recovery suggestions for an error
     * @param error Error message to analyze
     * @return Vector of suggested recovery actions
     */
    static std::vector<std::string> getRecoverySuggestions(const std::string& error);
    
    /**
     * Log error with appropriate severity level
     * @param error Error message
     * @param severity Severity level (info, warning, error, critical)
     */
    static void logError(const std::string& error, const std::string& severity = "error");

private:
    // Instance members for enhanced error handling
    mutable std::mutex errorMutex_;
    bool initialized_;
    
    // Configuration
    std::unordered_map<ErrorCategory, RetryConfig> retryConfigs_;
    DegradedModeConfig degradedModeConfig_;
    
    // State tracking
    bool degradedModeActive_;
    std::string degradedModeReason_;
    std::chrono::steady_clock::time_point degradedModeStartTime_;
    std::vector<std::string> activeDegradedRestrictions_;
    
    // Statistics
    mutable ErrorStatistics statistics_;
    
    // Custom recovery strategies
    std::unordered_map<ErrorCategory, std::function<RecoveryResult(const std::string&, const ErrorContext&)>> customRecoveryStrategies_;
    
    // Private helper methods
    ErrorCategory categorizeError(const std::string& error) const;
    ErrorSeverity assessErrorSeverity(const std::string& error, ErrorCategory category) const;
    RecoveryStrategy determineRecoveryStrategy(ErrorCategory category, ErrorSeverity severity) const;
    
    RecoveryResult executeRecoveryStrategy(RecoveryStrategy strategy, const std::string& error, const ErrorContext& context);
    RecoveryResult retryOperation(const std::string& error, const ErrorContext& context);
    RecoveryResult fallbackToCPU(const std::string& error, const ErrorContext& context);
    RecoveryResult reloadModel(const std::string& error, const ErrorContext& context);
    RecoveryResult activateDegradedMode(const std::string& error, const ErrorContext& context);
    
    bool validateModelIntegrity(const std::string& modelPath) const;
    bool attemptModelReload(const std::string& modelPath, const ErrorContext& context);
    
    void updateStatistics(ErrorCategory category, RecoveryStrategy strategy, bool successful);
    void logErrorWithContext(const std::string& error, const ErrorContext& context, ErrorSeverity severity);
    void loadConfigurationFromFile(const utils::Config& config);
    
    // Static helper methods (legacy support)
    static std::string extractErrorCategory(const std::string& error);
    static std::string formatErrorMessage(const std::string& originalError, const std::string& context);
    static ErrorCategory stringToErrorCategory(const std::string& categoryStr);
    static std::string errorCategoryToString(ErrorCategory category);
    static std::string errorSeverityToString(ErrorSeverity severity);
    static std::string recoveryStrategyToString(RecoveryStrategy strategy);
};

// Template implementations

template<typename T>
T MarianErrorHandler::executeWithRetry(std::function<T()> operation, const RetryConfig& config, const ErrorContext& context) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    
    int attempt = 0;
    std::chrono::milliseconds delay = config.initialDelay;
    auto startTime = std::chrono::steady_clock::now();
    
    while (attempt <= config.maxRetries) {
        try {
            // Check timeout
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (elapsed > config.timeout) {
                throw MarianTimeoutException("Operation timed out after " + 
                    std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) + "ms");
            }
            
            return operation();
            
        } catch (const MarianException& e) {
            attempt++;
            
            if (attempt > config.maxRetries) {
                // Update statistics
                updateStatistics(e.getCategory(), RecoveryStrategy::RETRY, false);
                throw;
            }
            
            // Log retry attempt
            logErrorWithContext("Retry attempt " + std::to_string(attempt) + " for: " + e.what(), 
                              context, ErrorSeverity::WARNING);
            
            // Wait before retry with exponential backoff
            std::this_thread::sleep_for(delay);
            delay = std::min(static_cast<std::chrono::milliseconds>(delay.count() * config.backoffMultiplier), config.maxDelay);
            
        } catch (const std::exception& e) {
            attempt++;
            
            if (attempt > config.maxRetries) {
                updateStatistics(ErrorCategory::UNKNOWN, RecoveryStrategy::RETRY, false);
                throw;
            }
            
            logErrorWithContext("Retry attempt " + std::to_string(attempt) + " for: " + e.what(), 
                              context, ErrorSeverity::WARNING);
            
            std::this_thread::sleep_for(delay);
            delay = std::min(static_cast<std::chrono::milliseconds>(delay.count() * config.backoffMultiplier), config.maxDelay);
        }
    }
    
    // This should never be reached, but just in case
    throw MarianException("Maximum retry attempts exceeded");
}

template<typename T>
T MarianErrorHandler::executeWithTimeout(std::function<T()> operation, std::chrono::milliseconds timeout, const ErrorContext& context) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    
    auto startTime = std::chrono::steady_clock::now();
    
    // Create a future for the operation
    auto future = std::async(std::launch::async, operation);
    
    // Wait for the operation to complete or timeout
    if (future.wait_for(timeout) == std::future_status::timeout) {
        logErrorWithContext("Operation timed out after " + std::to_string(timeout.count()) + "ms", 
                          context, ErrorSeverity::WARNING);
        throw MarianTimeoutException("Operation timed out after " + std::to_string(timeout.count()) + "ms");
    }
    
    return future.get();
}

} // namespace mt
} // namespace speechrnt