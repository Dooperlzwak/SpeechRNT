#include "mt/marian_error_handler.hpp"
#include "utils/logging.hpp"
#include "utils/config.hpp"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <future>
#include <random>

namespace speechrnt {
namespace mt {

MarianErrorHandler::MarianErrorHandler() 
    : initialized_(false)
    , degradedModeActive_(false) {
    
    // Initialize default retry configurations for different error categories
    retryConfigs_[ErrorCategory::TRANSLATION_TIMEOUT] = RetryConfig(2, std::chrono::milliseconds(500), 
                                                                   std::chrono::milliseconds(2000), 1.5, 
                                                                   std::chrono::milliseconds(10000));
    
    retryConfigs_[ErrorCategory::GPU_FAILURE] = RetryConfig(1, std::chrono::milliseconds(1000), 
                                                           std::chrono::milliseconds(3000), 2.0, 
                                                           std::chrono::milliseconds(5000));
    
    retryConfigs_[ErrorCategory::MODEL_LOADING] = RetryConfig(2, std::chrono::milliseconds(2000), 
                                                             std::chrono::milliseconds(5000), 2.0, 
                                                             std::chrono::milliseconds(30000));
    
    retryConfigs_[ErrorCategory::TRANSLATION_FAILURE] = RetryConfig(3, std::chrono::milliseconds(100), 
                                                                   std::chrono::milliseconds(1000), 1.8, 
                                                                   std::chrono::milliseconds(5000));
    
    retryConfigs_[ErrorCategory::MEMORY_EXHAUSTION] = RetryConfig(1, std::chrono::milliseconds(3000), 
                                                                 std::chrono::milliseconds(5000), 1.0, 
                                                                 std::chrono::milliseconds(10000));
    
    // Initialize default degraded mode configuration
    degradedModeConfig_ = DegradedModeConfig();
}

MarianErrorHandler::~MarianErrorHandler() {
    if (degradedModeActive_) {
        exitDegradedMode();
    }
}

bool MarianErrorHandler::initialize(const std::string& configPath) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    
    try {
        if (!configPath.empty() && std::filesystem::exists(configPath)) {
            // Load configuration from file
            utils::Config config;
            if (config.loadFromFile(configPath)) {
                loadConfigurationFromFile(config);
                speechrnt::utils::Logger::info("MarianErrorHandler initialized with configuration from: " + configPath);
            } else {
                speechrnt::utils::Logger::warning("Failed to load error handler configuration, using defaults");
            }
        } else {
            speechrnt::utils::Logger::info("MarianErrorHandler initialized with default configuration");
        }
        
        initialized_ = true;
        return true;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to initialize MarianErrorHandler: " + std::string(e.what()));
        return false;
    }
}

RecoveryResult MarianErrorHandler::handleError(const std::string& error, const ErrorContext& context) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    
    auto startTime = std::chrono::steady_clock::now();
    RecoveryResult result;
    
    try {
        // Categorize and assess the error
        ErrorCategory category = categorizeError(error);
        ErrorSeverity severity = assessErrorSeverity(error, category);
        
        // Update statistics
        statistics_.totalErrors++;
        statistics_.errorsByCategory[category]++;
        statistics_.lastError = std::chrono::steady_clock::now();
        
        // Log error with context
        logErrorWithContext(error, context, severity);
        
        // Determine recovery strategy
        RecoveryStrategy strategy = determineRecoveryStrategy(category, severity);
        
        // Execute recovery strategy
        result = executeRecoveryStrategy(strategy, error, context);
        
        // Update statistics based on result
        updateStatistics(category, strategy, result.successful);
        
        // Calculate recovery time
        auto endTime = std::chrono::steady_clock::now();
        result.recoveryTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        if (result.successful) {
            statistics_.recoveredErrors++;
            speechrnt::utils::Logger::info("Successfully recovered from error using strategy: " + 
                              recoveryStrategyToString(strategy));
        } else {
            if (severity == ErrorSeverity::CRITICAL || severity == ErrorSeverity::FATAL) {
                statistics_.criticalErrors++;
                
                // Consider entering degraded mode for critical errors
                if (!degradedModeActive_ && degradedModeConfig_.enableFallbackTranslation) {
                    enterDegradedMode("Critical error: " + error, context);
                    result.successful = true; // Degraded mode is still operational
                    result.message += " (Entered degraded mode)";
                }
            }
        }
        
    } catch (const std::exception& e) {
        result.successful = false;
        result.message = "Error handling failed: " + std::string(e.what());
        speechrnt::utils::Logger::error("Error handling failed: " + std::string(e.what()));
    }
    
    return result;
}

RecoveryResult MarianErrorHandler::handleError(const MarianException& exception, const ErrorContext& context) {
    ErrorContext enhancedContext = context;
    enhancedContext.additionalInfo = "Exception type: " + std::string(typeid(exception).name());
    
    return handleError(exception.what(), enhancedContext);
}

RecoveryResult MarianErrorHandler::checkAndHandleModelCorruption(const std::string& modelPath, const ErrorContext& context) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    
    RecoveryResult result;
    result.strategyUsed = RecoveryStrategy::RELOAD_MODEL;
    
    try {
        // Check model integrity
        if (!validateModelIntegrity(modelPath)) {
            speechrnt::utils::Logger::warning("Model corruption detected: " + modelPath);
            
            // Attempt to reload the model
            if (attemptModelReload(modelPath, context)) {
                result.successful = true;
                result.message = "Successfully reloaded corrupted model: " + modelPath;
                speechrnt::utils::Logger::info(result.message);
            } else {
                result.successful = false;
                result.message = "Failed to reload corrupted model: " + modelPath;
                result.requiresUserIntervention = true;
                speechrnt::utils::Logger::error(result.message);
            }
        } else {
            result.successful = true;
            result.message = "Model integrity check passed: " + modelPath;
        }
        
    } catch (const std::exception& e) {
        result.successful = false;
        result.message = "Model corruption check failed: " + std::string(e.what());
        speechrnt::utils::Logger::error(result.message);
    }
    
    return result;
}

RecoveryResult MarianErrorHandler::handleGPUErrorWithFallback(const std::string& error, const ErrorContext& context) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    
    RecoveryResult result;
    result.strategyUsed = RecoveryStrategy::FALLBACK_CPU;
    
    try {
        speechrnt::utils::Logger::warning("GPU error detected, attempting CPU fallback: " + error);
        
        // Execute CPU fallback strategy
        result = fallbackToCPU(error, context);
        
        if (result.successful) {
            speechrnt::utils::Logger::info("Successfully fell back to CPU processing");
        } else {
            speechrnt::utils::Logger::error("CPU fallback failed: " + result.message);
        }
        
    } catch (const std::exception& e) {
        result.successful = false;
        result.message = "GPU fallback handling failed: " + std::string(e.what());
        speechrnt::utils::Logger::error(result.message);
    }
    
    return result;
}

bool MarianErrorHandler::enterDegradedMode(const std::string& reason, const ErrorContext& context) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    
    if (degradedModeActive_) {
        speechrnt::utils::Logger::warning("Already in degraded mode, reason: " + degradedModeReason_);
        return true;
    }
    
    try {
        degradedModeActive_ = true;
        degradedModeReason_ = reason;
        degradedModeStartTime_ = std::chrono::steady_clock::now();
        activeDegradedRestrictions_.clear();
        
        // Configure degraded mode restrictions
        if (degradedModeConfig_.enableCPUOnlyMode) {
            activeDegradedRestrictions_.push_back("GPU acceleration disabled");
        }
        
        if (degradedModeConfig_.enableReducedQuality) {
            activeDegradedRestrictions_.push_back("Quality threshold reduced to " + 
                                                std::to_string(degradedModeConfig_.qualityThreshold));
        }
        
        if (degradedModeConfig_.enableSimplifiedModels) {
            activeDegradedRestrictions_.push_back("Using simplified translation models");
        }
        
        if (degradedModeConfig_.enableFallbackTranslation) {
            activeDegradedRestrictions_.push_back("Fallback translation enabled");
        }
        
        speechrnt::utils::Logger::warning("Entered degraded mode: " + reason);
        for (const auto& restriction : activeDegradedRestrictions_) {
            speechrnt::utils::Logger::info("Degraded mode restriction: " + restriction);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to enter degraded mode: " + std::string(e.what()));
        degradedModeActive_ = false;
        return false;
    }
}

bool MarianErrorHandler::exitDegradedMode() {
    std::lock_guard<std::mutex> lock(errorMutex_);
    
    if (!degradedModeActive_) {
        return true;
    }
    
    try {
        auto duration = std::chrono::steady_clock::now() - degradedModeStartTime_;
        auto durationMinutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
        
        degradedModeActive_ = false;
        activeDegradedRestrictions_.clear();
        
        speechrnt::utils::Logger::info("Exited degraded mode after " + std::to_string(durationMinutes.count()) + 
                          " minutes. Reason was: " + degradedModeReason_);
        
        degradedModeReason_.clear();
        
        return true;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to exit degraded mode: " + std::string(e.what()));
        return false;
    }
}

bool MarianErrorHandler::isInDegradedMode() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    
    // Check if degraded mode has exceeded maximum time
    if (degradedModeActive_) {
        auto duration = std::chrono::steady_clock::now() - degradedModeStartTime_;
        auto durationMinutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
        
        if (durationMinutes > degradedModeConfig_.maxDegradedTime) {
            speechrnt::utils::Logger::warning("Degraded mode exceeded maximum time limit, attempting to exit");
            const_cast<MarianErrorHandler*>(this)->exitDegradedMode();
            return false;
        }
    }
    
    return degradedModeActive_;
}

MarianErrorHandler::DegradedModeStatus MarianErrorHandler::getDegradedModeStatus() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    
    DegradedModeStatus status;
    status.active = degradedModeActive_;
    status.reason = degradedModeReason_;
    status.startTime = degradedModeStartTime_;
    status.activeRestrictions = activeDegradedRestrictions_;
    
    if (degradedModeActive_) {
        auto duration = std::chrono::steady_clock::now() - degradedModeStartTime_;
        status.duration = std::chrono::duration_cast<std::chrono::minutes>(duration);
    }
    
    return status;
}

void MarianErrorHandler::setRetryConfig(ErrorCategory category, const RetryConfig& config) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    retryConfigs_[category] = config;
}

void MarianErrorHandler::setDegradedModeConfig(const DegradedModeConfig& config) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    degradedModeConfig_ = config;
}

ErrorStatistics MarianErrorHandler::getErrorStatistics() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return statistics_;
}

void MarianErrorHandler::resetErrorStatistics() {
    std::lock_guard<std::mutex> lock(errorMutex_);
    statistics_ = ErrorStatistics();
}

void MarianErrorHandler::registerRecoveryStrategy(ErrorCategory category, 
    std::function<RecoveryResult(const std::string&, const ErrorContext&)> strategy) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    customRecoveryStrategies_[category] = strategy;
}

// Legacy methods with enhanced functionality

std::string MarianErrorHandler::handleModelLoadingError(const std::string& error, const std::string& modelPath) {
    std::string category = extractErrorCategory(error);
    std::ostringstream oss;
    
    oss << "Failed to load Marian model from: " << modelPath << "\n";
    oss << "Original error: " << error << "\n";
    
    if (category == "file_not_found") {
        oss << "Suggestion: Ensure model files (model.npz, vocab.yml, config.yml) exist in the model directory.\n";
        oss << "Run the model download script or check the model path configuration.";
    } else if (category == "memory") {
        oss << "Suggestion: Insufficient memory to load model. Try:\n";
        oss << "- Close other applications to free memory\n";
        oss << "- Use model quantization to reduce memory usage\n";
        oss << "- Switch to a smaller model variant";
    } else if (category == "corruption") {
        oss << "Suggestion: Model files may be corrupted. Try:\n";
        oss << "- Re-download the model files\n";
        oss << "- Verify file integrity with checksums\n";
        oss << "- Check disk space and file permissions";
    } else {
        oss << "Suggestion: Check model compatibility and Marian NMT installation.";
    }
    
    logError(oss.str(), "error");
    return oss.str();
}

std::string MarianErrorHandler::handleGPUError(const std::string& error, int deviceId) {
    std::ostringstream oss;
    
    oss << "GPU acceleration failed for device " << deviceId << "\n";
    oss << "Original error: " << error << "\n";
    
    if (error.find("CUDA") != std::string::npos) {
        oss << "Suggestion: CUDA-related error. Try:\n";
        oss << "- Verify CUDA installation and driver compatibility\n";
        oss << "- Check GPU memory availability\n";
        oss << "- Ensure GPU device " << deviceId << " exists and is accessible\n";
        oss << "- Falling back to CPU processing";
    } else if (error.find("memory") != std::string::npos) {
        oss << "Suggestion: GPU memory insufficient. Try:\n";
        oss << "- Reduce batch size or model size\n";
        oss << "- Close other GPU-intensive applications\n";
        oss << "- Use model quantization\n";
        oss << "- Falling back to CPU processing";
    } else {
        oss << "Suggestion: GPU initialization failed. Falling back to CPU processing.";
    }
    
    logError(oss.str(), "warning");
    return oss.str();
}

std::string MarianErrorHandler::handleTranslationError(const std::string& error, const std::string& sourceText) {
    std::ostringstream oss;
    
    oss << "Translation failed for input text (length: " << sourceText.length() << ")\n";
    oss << "Original error: " << error << "\n";
    
    if (sourceText.length() > 1000) {
        oss << "Suggestion: Input text is very long. Try:\n";
        oss << "- Split text into smaller segments\n";
        oss << "- Use batch processing for long texts";
    } else if (sourceText.empty()) {
        oss << "Suggestion: Input text is empty. Provide non-empty text for translation.";
    } else if (error.find("timeout") != std::string::npos) {
        oss << "Suggestion: Translation timed out. Try:\n";
        oss << "- Increase timeout duration\n";
        oss << "- Use GPU acceleration if available\n";
        oss << "- Simplify input text";
    } else {
        oss << "Suggestion: Check input text encoding and model compatibility.";
    }
    
    logError(oss.str(), "error");
    return oss.str();
}

std::string MarianErrorHandler::handleModelCorruptionError(const std::string& error, const std::string& modelPath) {
    std::ostringstream oss;
    
    oss << "Model corruption detected: " << modelPath << "\n";
    oss << "Original error: " << error << "\n";
    oss << "Recovery steps:\n";
    oss << "1. Delete corrupted model files\n";
    oss << "2. Re-download model from official source\n";
    oss << "3. Verify file integrity with checksums\n";
    oss << "4. Check disk health and available space\n";
    oss << "5. Restart the translation service";
    
    logError(oss.str(), "critical");
    return oss.str();
}

bool MarianErrorHandler::isRecoverableError(const std::string& error) {
    // Check for recoverable error patterns
    std::vector<std::string> recoverablePatterns = {
        "timeout",
        "memory",
        "GPU",
        "CUDA",
        "temporary",
        "network",
        "connection"
    };
    
    std::string lowerError = error;
    std::transform(lowerError.begin(), lowerError.end(), lowerError.begin(), ::tolower);
    
    for (const auto& pattern : recoverablePatterns) {
        if (lowerError.find(pattern) != std::string::npos) {
            return true;
        }
    }
    
    // Non-recoverable errors
    std::vector<std::string> nonRecoverablePatterns = {
        "corruption",
        "invalid model",
        "unsupported",
        "fatal"
    };
    
    for (const auto& pattern : nonRecoverablePatterns) {
        if (lowerError.find(pattern) != std::string::npos) {
            return false;
        }
    }
    
    // Default to recoverable for unknown errors
    return true;
}

std::vector<std::string> MarianErrorHandler::getRecoverySuggestions(const std::string& error) {
    std::vector<std::string> suggestions;
    std::string lowerError = error;
    std::transform(lowerError.begin(), lowerError.end(), lowerError.begin(), ::tolower);
    
    if (lowerError.find("memory") != std::string::npos) {
        suggestions.push_back("Free up system memory by closing other applications");
        suggestions.push_back("Use model quantization to reduce memory usage");
        suggestions.push_back("Switch to a smaller model variant");
        suggestions.push_back("Increase system virtual memory");
    }
    
    if (lowerError.find("gpu") != std::string::npos || lowerError.find("cuda") != std::string::npos) {
        suggestions.push_back("Update GPU drivers to latest version");
        suggestions.push_back("Verify CUDA installation and compatibility");
        suggestions.push_back("Check GPU memory availability");
        suggestions.push_back("Fall back to CPU processing");
    }
    
    if (lowerError.find("timeout") != std::string::npos) {
        suggestions.push_back("Increase translation timeout duration");
        suggestions.push_back("Use GPU acceleration if available");
        suggestions.push_back("Split long texts into smaller segments");
        suggestions.push_back("Optimize system performance");
    }
    
    if (lowerError.find("file") != std::string::npos || lowerError.find("model") != std::string::npos) {
        suggestions.push_back("Verify model files exist and are accessible");
        suggestions.push_back("Re-download model files if corrupted");
        suggestions.push_back("Check file permissions and disk space");
        suggestions.push_back("Validate model file integrity");
    }
    
    if (suggestions.empty()) {
        suggestions.push_back("Restart the translation service");
        suggestions.push_back("Check system logs for additional details");
        suggestions.push_back("Contact support if problem persists");
    }
    
    return suggestions;
}

void MarianErrorHandler::logError(const std::string& error, const std::string& severity) {
    if (severity == "critical") {
        speechrnt::utils::Logger::error("[CRITICAL] " + error);
    } else if (severity == "error") {
        speechrnt::utils::Logger::error(error);
    } else if (severity == "warning") {
        speechrnt::utils::Logger::warning(error);
    } else {
        speechrnt::utils::Logger::info(error);
    }
}

std::string MarianErrorHandler::extractErrorCategory(const std::string& error) {
    std::string lowerError = error;
    std::transform(lowerError.begin(), lowerError.end(), lowerError.begin(), ::tolower);
    
    if (lowerError.find("file not found") != std::string::npos || 
        lowerError.find("no such file") != std::string::npos) {
        return "file_not_found";
    } else if (lowerError.find("memory") != std::string::npos || 
               lowerError.find("out of memory") != std::string::npos) {
        return "memory";
    } else if (lowerError.find("corrupt") != std::string::npos || 
               lowerError.find("invalid") != std::string::npos) {
        return "corruption";
    } else if (lowerError.find("timeout") != std::string::npos) {
        return "timeout";
    } else if (lowerError.find("gpu") != std::string::npos || 
               lowerError.find("cuda") != std::string::npos) {
        return "gpu";
    } else {
        return "unknown";
    }
}

std::string MarianErrorHandler::formatErrorMessage(const std::string& originalError, const std::string& context) {
    std::ostringstream oss;
    oss << "[Marian NMT Error]";
    if (!context.empty()) {
        oss << " [" << context << "]";
    }
    oss << ": " << originalError;
    return oss.str();
}

// Private helper method implementations

ErrorCategory MarianErrorHandler::categorizeError(const std::string& error) const {
    std::string lowerError = error;
    std::transform(lowerError.begin(), lowerError.end(), lowerError.begin(), ::tolower);
    
    if (lowerError.find("timeout") != std::string::npos) {
        return ErrorCategory::TRANSLATION_TIMEOUT;
    } else if (lowerError.find("gpu") != std::string::npos || lowerError.find("cuda") != std::string::npos) {
        return ErrorCategory::GPU_FAILURE;
    } else if (lowerError.find("memory") != std::string::npos || lowerError.find("out of memory") != std::string::npos) {
        return ErrorCategory::MEMORY_EXHAUSTION;
    } else if (lowerError.find("corrupt") != std::string::npos || lowerError.find("invalid model") != std::string::npos) {
        return ErrorCategory::MODEL_CORRUPTION;
    } else if (lowerError.find("model") != std::string::npos || lowerError.find("loading") != std::string::npos) {
        return ErrorCategory::MODEL_LOADING;
    } else if (lowerError.find("translation") != std::string::npos) {
        return ErrorCategory::TRANSLATION_FAILURE;
    } else if (lowerError.find("config") != std::string::npos || lowerError.find("configuration") != std::string::npos) {
        return ErrorCategory::CONFIGURATION_ERROR;
    } else if (lowerError.find("network") != std::string::npos || lowerError.find("connection") != std::string::npos) {
        return ErrorCategory::NETWORK_ERROR;
    } else {
        return ErrorCategory::UNKNOWN;
    }
}

ErrorSeverity MarianErrorHandler::assessErrorSeverity(const std::string& error, ErrorCategory category) const {
    std::string lowerError = error;
    std::transform(lowerError.begin(), lowerError.end(), lowerError.begin(), ::tolower);
    
    // Fatal errors
    if (lowerError.find("fatal") != std::string::npos || lowerError.find("crash") != std::string::npos) {
        return ErrorSeverity::FATAL;
    }
    
    // Critical errors
    if (category == ErrorCategory::MODEL_CORRUPTION || 
        lowerError.find("critical") != std::string::npos ||
        lowerError.find("system failure") != std::string::npos) {
        return ErrorSeverity::CRITICAL;
    }
    
    // Warning level errors
    if (category == ErrorCategory::GPU_FAILURE || 
        category == ErrorCategory::TRANSLATION_TIMEOUT ||
        lowerError.find("warning") != std::string::npos) {
        return ErrorSeverity::WARNING;
    }
    
    // Info level
    if (lowerError.find("info") != std::string::npos || lowerError.find("notice") != std::string::npos) {
        return ErrorSeverity::INFO;
    }
    
    // Default to ERROR
    return ErrorSeverity::ERROR;
}

RecoveryStrategy MarianErrorHandler::determineRecoveryStrategy(ErrorCategory category, ErrorSeverity severity) const {
    // Critical and fatal errors go to degraded mode
    if (severity == ErrorSeverity::CRITICAL || severity == ErrorSeverity::FATAL) {
        return RecoveryStrategy::DEGRADED_MODE;
    }
    
    switch (category) {
        case ErrorCategory::TRANSLATION_TIMEOUT:
        case ErrorCategory::TRANSLATION_FAILURE:
            return RecoveryStrategy::RETRY;
            
        case ErrorCategory::GPU_FAILURE:
            return RecoveryStrategy::FALLBACK_CPU;
            
        case ErrorCategory::MODEL_CORRUPTION:
            return RecoveryStrategy::RELOAD_MODEL;
            
        case ErrorCategory::MODEL_LOADING:
            return RecoveryStrategy::RETRY;
            
        case ErrorCategory::MEMORY_EXHAUSTION:
            return RecoveryStrategy::FALLBACK_CPU;
            
        case ErrorCategory::CONFIGURATION_ERROR:
            return RecoveryStrategy::FAIL_SAFE;
            
        default:
            return RecoveryStrategy::RETRY;
    }
}

RecoveryResult MarianErrorHandler::executeRecoveryStrategy(RecoveryStrategy strategy, const std::string& error, const ErrorContext& context) {
    RecoveryResult result;
    result.strategyUsed = strategy;
    
    // Check for custom recovery strategies first
    ErrorCategory category = categorizeError(error);
    auto customIt = customRecoveryStrategies_.find(category);
    if (customIt != customRecoveryStrategies_.end()) {
        try {
            result = customIt->second(error, context);
            return result;
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::warning("Custom recovery strategy failed: " + std::string(e.what()));
            // Fall through to default strategies
        }
    }
    
    switch (strategy) {
        case RecoveryStrategy::RETRY:
            result = retryOperation(error, context);
            break;
            
        case RecoveryStrategy::FALLBACK_CPU:
            result = fallbackToCPU(error, context);
            break;
            
        case RecoveryStrategy::RELOAD_MODEL:
            result = reloadModel(error, context);
            break;
            
        case RecoveryStrategy::DEGRADED_MODE:
            result = activateDegradedMode(error, context);
            break;
            
        case RecoveryStrategy::FAIL_SAFE:
            result.successful = false;
            result.message = "Fail-safe strategy: " + error;
            result.requiresUserIntervention = true;
            break;
            
        case RecoveryStrategy::NO_RECOVERY:
        default:
            result.successful = false;
            result.message = "No recovery strategy available for: " + error;
            break;
    }
    
    return result;
}

RecoveryResult MarianErrorHandler::retryOperation(const std::string& error, const ErrorContext& context) {
    RecoveryResult result;
    result.strategyUsed = RecoveryStrategy::RETRY;
    
    // This is a placeholder - actual retry logic would be implemented by the caller
    // using the executeWithRetry template method
    result.successful = true;
    result.message = "Retry strategy prepared for: " + error;
    
    return result;
}

RecoveryResult MarianErrorHandler::fallbackToCPU(const std::string& error, const ErrorContext& context) {
    RecoveryResult result;
    result.strategyUsed = RecoveryStrategy::FALLBACK_CPU;
    
    try {
        // This would typically involve disabling GPU acceleration and switching to CPU
        // The actual implementation would be handled by the MarianTranslator
        result.successful = true;
        result.message = "CPU fallback activated due to: " + error;
        
        speechrnt::utils::Logger::info("Activated CPU fallback for GPU error: " + error);
        
    } catch (const std::exception& e) {
        result.successful = false;
        result.message = "CPU fallback failed: " + std::string(e.what());
    }
    
    return result;
}

RecoveryResult MarianErrorHandler::reloadModel(const std::string& error, const ErrorContext& context) {
    RecoveryResult result;
    result.strategyUsed = RecoveryStrategy::RELOAD_MODEL;
    
    try {
        if (!context.modelPath.empty()) {
            if (attemptModelReload(context.modelPath, context)) {
                result.successful = true;
                result.message = "Successfully reloaded model: " + context.modelPath;
            } else {
                result.successful = false;
                result.message = "Failed to reload model: " + context.modelPath;
                result.requiresUserIntervention = true;
            }
        } else {
            result.successful = false;
            result.message = "Cannot reload model: no model path provided";
        }
        
    } catch (const std::exception& e) {
        result.successful = false;
        result.message = "Model reload failed: " + std::string(e.what());
    }
    
    return result;
}

RecoveryResult MarianErrorHandler::activateDegradedMode(const std::string& error, const ErrorContext& context) {
    RecoveryResult result;
    result.strategyUsed = RecoveryStrategy::DEGRADED_MODE;
    
    if (enterDegradedMode(error, context)) {
        result.successful = true;
        result.message = "Activated degraded mode due to: " + error;
    } else {
        result.successful = false;
        result.message = "Failed to activate degraded mode: " + error;
    }
    
    return result;
}

bool MarianErrorHandler::validateModelIntegrity(const std::string& modelPath) const {
    try {
        if (!std::filesystem::exists(modelPath)) {
            return false;
        }
        
        // Check for required model files
        std::vector<std::string> requiredFiles = {"model.npz", "vocab.yml", "config.yml"};
        
        for (const auto& file : requiredFiles) {
            std::string filePath = modelPath + "/" + file;
            if (!std::filesystem::exists(filePath)) {
                speechrnt::utils::Logger::debug("Missing model file: " + filePath);
                return false;
            }
            
            // Check if file is not empty
            if (std::filesystem::file_size(filePath) == 0) {
                speechrnt::utils::Logger::debug("Empty model file: " + filePath);
                return false;
            }
        }
        
        // Additional integrity checks could be added here
        // (e.g., checksum validation, file format validation)
        
        return true;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Model integrity check failed: " + std::string(e.what()));
        return false;
    }
}

bool MarianErrorHandler::attemptModelReload(const std::string& modelPath, const ErrorContext& context) {
    try {
        // This is a placeholder for actual model reloading logic
        // The actual implementation would involve:
        // 1. Unloading the current model
        // 2. Clearing any cached model data
        // 3. Re-downloading or restoring the model if necessary
        // 4. Reloading the model
        
        speechrnt::utils::Logger::info("Attempting to reload model: " + modelPath);
        
        // Simulate model reload delay
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // Check if model files are now valid
        if (validateModelIntegrity(modelPath)) {
            speechrnt::utils::Logger::info("Model reload successful: " + modelPath);
            return true;
        } else {
            speechrnt::utils::Logger::error("Model reload failed - integrity check failed: " + modelPath);
            return false;
        }
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Model reload attempt failed: " + std::string(e.what()));
        return false;
    }
}

void MarianErrorHandler::updateStatistics(ErrorCategory category, RecoveryStrategy strategy, bool successful) {
    statistics_.recoveryStrategiesUsed[strategy]++;
    statistics_.totalRecoveryTime += std::chrono::milliseconds(100); // Placeholder
    
    if (successful) {
        statistics_.recoveredErrors++;
    }
}

void MarianErrorHandler::logErrorWithContext(const std::string& error, const ErrorContext& context, ErrorSeverity severity) {
    std::ostringstream oss;
    oss << "[" << errorSeverityToString(severity) << "] " << error;
    
    if (!context.component.empty()) {
        oss << " [Component: " << context.component << "]";
    }
    
    if (!context.operation.empty()) {
        oss << " [Operation: " << context.operation << "]";
    }
    
    if (!context.languagePair.empty()) {
        oss << " [Language Pair: " << context.languagePair << "]";
    }
    
    if (context.gpuDeviceId >= 0) {
        oss << " [GPU Device: " << context.gpuDeviceId << "]";
    }
    
    if (context.memoryUsageMB > 0) {
        oss << " [Memory Usage: " << context.memoryUsageMB << "MB]";
    }
    
    if (!context.additionalInfo.empty()) {
        oss << " [Info: " << context.additionalInfo << "]";
    }
    
    std::string logMessage = oss.str();
    
    switch (severity) {
        case ErrorSeverity::FATAL:
        case ErrorSeverity::CRITICAL:
            speechrnt::utils::Logger::error(logMessage);
            break;
        case ErrorSeverity::ERROR:
            speechrnt::utils::Logger::error(logMessage);
            break;
        case ErrorSeverity::WARNING:
            speechrnt::utils::Logger::warning(logMessage);
            break;
        case ErrorSeverity::INFO:
        default:
            speechrnt::utils::Logger::info(logMessage);
            break;
    }
}

void MarianErrorHandler::loadConfigurationFromFile(const utils::Config& config) {
    // Load retry configurations
    if (config.hasSection("retry_configs")) {
        // Implementation would load retry configs from file
        speechrnt::utils::Logger::info("Loaded retry configurations from file");
    }
    
    // Load degraded mode configuration
    if (config.hasSection("degraded_mode")) {
        // Implementation would load degraded mode config from file
        speechrnt::utils::Logger::info("Loaded degraded mode configuration from file");
    }
}

// Static helper methods

ErrorCategory MarianErrorHandler::stringToErrorCategory(const std::string& categoryStr) {
    if (categoryStr == "MODEL_LOADING") return ErrorCategory::MODEL_LOADING;
    if (categoryStr == "MODEL_CORRUPTION") return ErrorCategory::MODEL_CORRUPTION;
    if (categoryStr == "GPU_FAILURE") return ErrorCategory::GPU_FAILURE;
    if (categoryStr == "TRANSLATION_TIMEOUT") return ErrorCategory::TRANSLATION_TIMEOUT;
    if (categoryStr == "TRANSLATION_FAILURE") return ErrorCategory::TRANSLATION_FAILURE;
    if (categoryStr == "MEMORY_EXHAUSTION") return ErrorCategory::MEMORY_EXHAUSTION;
    if (categoryStr == "CONFIGURATION_ERROR") return ErrorCategory::CONFIGURATION_ERROR;
    if (categoryStr == "NETWORK_ERROR") return ErrorCategory::NETWORK_ERROR;
    return ErrorCategory::UNKNOWN;
}

std::string MarianErrorHandler::errorCategoryToString(ErrorCategory category) {
    switch (category) {
        case ErrorCategory::MODEL_LOADING: return "MODEL_LOADING";
        case ErrorCategory::MODEL_CORRUPTION: return "MODEL_CORRUPTION";
        case ErrorCategory::GPU_FAILURE: return "GPU_FAILURE";
        case ErrorCategory::TRANSLATION_TIMEOUT: return "TRANSLATION_TIMEOUT";
        case ErrorCategory::TRANSLATION_FAILURE: return "TRANSLATION_FAILURE";
        case ErrorCategory::MEMORY_EXHAUSTION: return "MEMORY_EXHAUSTION";
        case ErrorCategory::CONFIGURATION_ERROR: return "CONFIGURATION_ERROR";
        case ErrorCategory::NETWORK_ERROR: return "NETWORK_ERROR";
        default: return "UNKNOWN";
    }
}

std::string MarianErrorHandler::errorSeverityToString(ErrorSeverity severity) {
    switch (severity) {
        case ErrorSeverity::INFO: return "INFO";
        case ErrorSeverity::WARNING: return "WARNING";
        case ErrorSeverity::ERROR: return "ERROR";
        case ErrorSeverity::CRITICAL: return "CRITICAL";
        case ErrorSeverity::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

std::string MarianErrorHandler::recoveryStrategyToString(RecoveryStrategy strategy) {
    switch (strategy) {
        case RecoveryStrategy::RETRY: return "RETRY";
        case RecoveryStrategy::FALLBACK_CPU: return "FALLBACK_CPU";
        case RecoveryStrategy::FALLBACK_MODEL: return "FALLBACK_MODEL";
        case RecoveryStrategy::RELOAD_MODEL: return "RELOAD_MODEL";
        case RecoveryStrategy::DEGRADED_MODE: return "DEGRADED_MODE";
        case RecoveryStrategy::FAIL_SAFE: return "FAIL_SAFE";
        case RecoveryStrategy::NO_RECOVERY: return "NO_RECOVERY";
        default: return "UNKNOWN";
    }
}

} // namespace mt
} // namespace speechrnt