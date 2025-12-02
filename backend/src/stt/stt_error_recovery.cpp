#include "stt/stt_error_recovery.hpp"
#include "utils/logging.hpp"
#include "utils/gpu_manager.hpp"
#include "utils/performance_monitor.hpp"
#include <algorithm>
#include <random>
#include <sstream>
#include <thread>

namespace stt {

STTErrorRecovery::STTErrorRecovery() 
    : initialized_(false)
    , enabled_(true) {
}

STTErrorRecovery::~STTErrorRecovery() {
    // Cancel any active recoveries
    std::lock_guard<std::mutex> lock(mutex_);
    activeRecoveries_.clear();
}

bool STTErrorRecovery::initialize(const RecoveryConfig& defaultConfig) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        lastError_ = "STTErrorRecovery already initialized";
        return false;
    }
    
    try {
        defaultConfig_ = defaultConfig;
        
        // Initialize default configurations for different error types
        RecoveryConfig modelLoadConfig = defaultConfig;
        modelLoadConfig.maxRetryAttempts = 2;
        modelLoadConfig.enableGPUFallback = true;
        modelLoadConfig.enableQuantizationFallback = true;
        errorConfigs_[STTErrorType::MODEL_LOAD_FAILURE] = modelLoadConfig;
        
        RecoveryConfig gpuMemoryConfig = defaultConfig;
        gpuMemoryConfig.maxRetryAttempts = 1;
        gpuMemoryConfig.enableGPUFallback = true;
        gpuMemoryConfig.enableQuantizationFallback = true;
        errorConfigs_[STTErrorType::GPU_MEMORY_ERROR] = gpuMemoryConfig;
        
        RecoveryConfig timeoutConfig = defaultConfig;
        timeoutConfig.maxRetryAttempts = 2;
        timeoutConfig.initialBackoffMs = std::chrono::milliseconds(50);
        timeoutConfig.enableBufferClear = true;
        errorConfigs_[STTErrorType::TRANSCRIPTION_TIMEOUT] = timeoutConfig;
        
        RecoveryConfig bufferConfig = defaultConfig;
        bufferConfig.maxRetryAttempts = 1;
        bufferConfig.enableBufferClear = true;
        errorConfigs_[STTErrorType::STREAMING_BUFFER_OVERFLOW] = bufferConfig;
        
        RecoveryConfig inferenceConfig = defaultConfig;
        inferenceConfig.maxRetryAttempts = 3;
        inferenceConfig.enableGPUFallback = true;
        inferenceConfig.enableQuantizationFallback = true;
        errorConfigs_[STTErrorType::WHISPER_INFERENCE_ERROR] = inferenceConfig;
        
        initialized_ = true;
        speechrnt::utils::Logger::info("STTErrorRecovery initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        lastError_ = "Failed to initialize STTErrorRecovery: " + std::string(e.what());
        speechrnt::utils::Logger::error(lastError_);
        return false;
    }
}

RecoveryResult STTErrorRecovery::handleError(const STTErrorContext& errorContext) {
    if (!enabled_.load()) {
        RecoveryResult result;
        result.success = false;
        result.strategyUsed = RecoveryStrategy::NONE;
        result.resultMessage = "Error recovery is disabled";
        return result;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if recovery is already in progress for this utterance
    if (activeRecoveries_.find(errorContext.utteranceId) != activeRecoveries_.end()) {
        RecoveryResult result;
        result.success = false;
        result.strategyUsed = RecoveryStrategy::NONE;
        result.resultMessage = "Recovery already in progress for utterance " + std::to_string(errorContext.utteranceId);
        return result;
    }
    
    // Get recovery configuration for this error type
    RecoveryConfig config = defaultConfig_;
    auto configIt = errorConfigs_.find(errorContext.errorType);
    if (configIt != errorConfigs_.end()) {
        config = configIt->second;
    }
    
    // Check if error is recoverable
    if (!isErrorRecoverable(errorContext)) {
        RecoveryResult result;
        result.success = false;
        result.strategyUsed = RecoveryStrategy::NONE;
        result.resultMessage = "Error is not recoverable: " + errorContext.errorMessage;
        logRecoveryAttempt(errorContext, RecoveryStrategy::NONE, false);
        return result;
    }
    
    // Add to error history
    {
        std::lock_guard<std::mutex> historyLock(historyMutex_);
        errorHistory_.push(errorContext);
        if (errorHistory_.size() > MAX_ERROR_HISTORY) {
            errorHistory_.pop();
        }
    }
    
    // Attempt recovery
    auto startTime = std::chrono::steady_clock::now();
    RecoveryResult result = attemptRecovery(errorContext, config);
    result.recoveryTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime);
    
    // Update statistics
    updateStatistics(errorContext.errorType, result.success);
    
    // Log recovery result
    std::string logMessage = "Recovery " + (result.success ? "succeeded" : "failed") + 
                            " for error type " + error_utils::errorTypeToString(errorContext.errorType) +
                            " using strategy " + error_utils::recoveryStrategyToString(result.strategyUsed) +
                            " (took " + std::to_string(result.recoveryTime.count()) + "ms)";
    
    if (result.success) {
        speechrnt::utils::Logger::info(logMessage);
    } else {
        speechrnt::utils::Logger::warn(logMessage + " - " + result.resultMessage);
    }
    
    // Notify callback if set
    if (notificationCallback_) {
        try {
            notificationCallback_(errorContext, result);
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Exception in notification callback: " + std::string(e.what()));
        }
    }
    
    return result;
}

void STTErrorRecovery::registerRecoveryCallback(STTErrorType errorType, RecoveryCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    recoveryCallbacks_[errorType] = callback;
}

void STTErrorRecovery::setNotificationCallback(NotificationCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    notificationCallback_ = callback;
}

void STTErrorRecovery::configureRecovery(STTErrorType errorType, const RecoveryConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    errorConfigs_[errorType] = config;
}

std::unordered_map<STTErrorType, size_t> STTErrorRecovery::getRecoveryStatistics() const {
    std::lock_guard<std::mutex> lock(statisticsMutex_);
    
    std::unordered_map<STTErrorType, size_t> stats;
    for (const auto& pair : recoveryAttempts_) {
        stats[pair.first] = pair.second;
    }
    return stats;
}

std::vector<STTErrorContext> STTErrorRecovery::getRecentErrors(size_t maxCount) const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    
    std::vector<STTErrorContext> errors;
    std::queue<STTErrorContext> tempQueue = errorHistory_;
    
    while (!tempQueue.empty() && errors.size() < maxCount) {
        errors.push_back(tempQueue.front());
        tempQueue.pop();
    }
    
    // Reverse to get most recent first
    std::reverse(errors.begin(), errors.end());
    return errors;
}

void STTErrorRecovery::clearHistory() {
    {
        std::lock_guard<std::mutex> historyLock(historyMutex_);
        while (!errorHistory_.empty()) {
            errorHistory_.pop();
        }
    }
    
    {
        std::lock_guard<std::mutex> statsLock(statisticsMutex_);
        recoveryAttempts_.clear();
        recoverySuccesses_.clear();
    }
    
    speechrnt::utils::Logger::info("STTErrorRecovery history and statistics cleared");
}

bool STTErrorRecovery::isRecoveryInProgress(uint32_t utteranceId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeRecoveries_.find(utteranceId) != activeRecoveries_.end();
}

void STTErrorRecovery::cancelRecovery(uint32_t utteranceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = activeRecoveries_.find(utteranceId);
    if (it != activeRecoveries_.end()) {
        speechrnt::utils::Logger::info("Cancelling recovery for utterance " + std::to_string(utteranceId));
        activeRecoveries_.erase(it);
    }
}

void STTErrorRecovery::setEnabled(bool enabled) {
    enabled_.store(enabled);
    speechrnt::utils::Logger::info("STTErrorRecovery " + std::string(enabled ? "enabled" : "disabled"));
}

bool STTErrorRecovery::isEnabled() const {
    return enabled_.load();
}

std::string STTErrorRecovery::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

// Private methods implementation

RecoveryResult STTErrorRecovery::attemptRecovery(const STTErrorContext& context, const RecoveryConfig& config) {
    RecoveryResult result;
    
    // Create active recovery tracking
    auto activeRecovery = std::make_unique<ActiveRecovery>();
    activeRecovery->context = context;
    activeRecovery->config = config;
    activeRecovery->startTime = std::chrono::steady_clock::now();
    activeRecovery->currentAttempt = 0;
    
    activeRecoveries_[context.utteranceId] = std::move(activeRecovery);
    
    // Attempt recovery with configured strategies
    for (int attempt = 1; attempt <= config.maxRetryAttempts; ++attempt) {
        activeRecoveries_[context.utteranceId]->currentAttempt = attempt;
        
        RecoveryStrategy strategy = selectRecoveryStrategy(context, attempt);
        activeRecoveries_[context.utteranceId]->currentStrategy = strategy;
        
        speechrnt::utils::Logger::info("Attempting recovery for utterance " + std::to_string(context.utteranceId) + 
                           " (attempt " + std::to_string(attempt) + "/" + std::to_string(config.maxRetryAttempts) + 
                           ") using strategy: " + error_utils::recoveryStrategyToString(strategy));
        
        bool success = executeRecoveryStrategy(context, strategy);
        logRecoveryAttempt(context, strategy, success);
        
        if (success) {
            result.success = true;
            result.strategyUsed = strategy;
            result.resultMessage = "Recovery successful using " + error_utils::recoveryStrategyToString(strategy);
            break;
        }
        
        // If not the last attempt, wait before retrying
        if (attempt < config.maxRetryAttempts) {
            auto delay = calculateBackoffDelay(attempt, config);
            activeRecoveries_[context.utteranceId]->nextRetryDelay = delay;
            
            speechrnt::utils::Logger::info("Recovery attempt " + std::to_string(attempt) + " failed, waiting " + 
                               std::to_string(delay.count()) + "ms before next attempt");
            
            // Release lock during sleep
            mutex_.unlock();
            std::this_thread::sleep_for(delay);
            mutex_.lock();
            
            // Check if recovery was cancelled during sleep
            if (activeRecoveries_.find(context.utteranceId) == activeRecoveries_.end()) {
                result.success = false;
                result.strategyUsed = strategy;
                result.resultMessage = "Recovery cancelled";
                return result;
            }
        }
    }
    
    // Clean up active recovery
    activeRecoveries_.erase(context.utteranceId);
    
    if (!result.success) {
        result.strategyUsed = RecoveryStrategy::NONE;
        result.resultMessage = "All recovery attempts failed";
        result.requiresClientNotification = true;
    }
    
    return result;
}

RecoveryStrategy STTErrorRecovery::selectRecoveryStrategy(const STTErrorContext& context, int attemptNumber) {
    // Use utility function for recommended strategy
    RecoveryStrategy recommended = error_utils::getRecommendedStrategy(context.errorType, attemptNumber);
    
    // Override with configuration-specific logic
    const RecoveryConfig& config = errorConfigs_.count(context.errorType) ? 
                                  errorConfigs_[context.errorType] : defaultConfig_;
    
    switch (context.errorType) {
        case STTErrorType::GPU_MEMORY_ERROR:
            if (attemptNumber == 1 && config.enableGPUFallback && context.wasUsingGPU) {
                return RecoveryStrategy::FALLBACK_GPU_TO_CPU;
            } else if (attemptNumber == 2 && config.enableQuantizationFallback) {
                return RecoveryStrategy::FALLBACK_QUANTIZATION;
            }
            break;
            
        case STTErrorType::MODEL_LOAD_FAILURE:
            if (attemptNumber == 1 && config.enableQuantizationFallback) {
                return RecoveryStrategy::FALLBACK_QUANTIZATION;
            } else if (attemptNumber == 2 && config.enableGPUFallback && context.wasUsingGPU) {
                return RecoveryStrategy::FALLBACK_GPU_TO_CPU;
            }
            break;
            
        case STTErrorType::STREAMING_BUFFER_OVERFLOW:
            if (config.enableBufferClear) {
                return RecoveryStrategy::CLEAR_BUFFERS;
            }
            break;
            
        case STTErrorType::TRANSCRIPTION_TIMEOUT:
        case STTErrorType::WHISPER_INFERENCE_ERROR:
            if (attemptNumber == 1) {
                return RecoveryStrategy::RETRY_WITH_BACKOFF;
            } else if (attemptNumber == 2 && config.enableGPUFallback && context.wasUsingGPU) {
                return RecoveryStrategy::FALLBACK_GPU_TO_CPU;
            } else if (attemptNumber == 3 && config.enableQuantizationFallback) {
                return RecoveryStrategy::FALLBACK_QUANTIZATION;
            }
            break;
            
        default:
            break;
    }
    
    return recommended;
}

bool STTErrorRecovery::executeRecoveryStrategy(const STTErrorContext& context, RecoveryStrategy strategy) {
    try {
        switch (strategy) {
            case RecoveryStrategy::RETRY_SAME:
                return retryWithBackoff(context, std::chrono::milliseconds(0));
                
            case RecoveryStrategy::RETRY_WITH_BACKOFF: {
                auto delay = calculateBackoffDelay(context.retryCount + 1, 
                    errorConfigs_.count(context.errorType) ? errorConfigs_[context.errorType] : defaultConfig_);
                return retryWithBackoff(context, delay);
            }
            
            case RecoveryStrategy::FALLBACK_GPU_TO_CPU:
                return fallbackGPUToCPU(context);
                
            case RecoveryStrategy::FALLBACK_QUANTIZATION:
                return fallbackQuantization(context);
                
            case RecoveryStrategy::RESTART_COMPONENT:
                return restartComponent(context);
                
            case RecoveryStrategy::CLEAR_BUFFERS:
                return clearBuffers(context);
                
            case RecoveryStrategy::REDUCE_QUALITY:
                return reduceQuality(context);
                
            case RecoveryStrategy::NOTIFY_CLIENT:
                // This is handled at a higher level
                return false;
                
            case RecoveryStrategy::NONE:
            default:
                return false;
        }
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Exception during recovery strategy execution: " + std::string(e.what()));
        return false;
    }
}

bool STTErrorRecovery::retryWithBackoff(const STTErrorContext& context, std::chrono::milliseconds delay) {
    if (delay.count() > 0) {
        std::this_thread::sleep_for(delay);
    }
    
    // Check if we have a registered callback for this error type
    auto callbackIt = recoveryCallbacks_.find(context.errorType);
    if (callbackIt != recoveryCallbacks_.end()) {
        return callbackIt->second(context);
    }
    
    // Default retry behavior - just indicate that retry should be attempted
    speechrnt::utils::Logger::info("Retry with backoff requested for error: " + context.errorMessage);
    return true; // Assume retry will be handled by caller
}

bool STTErrorRecovery::fallbackGPUToCPU(const STTErrorContext& context) {
    if (!context.wasUsingGPU) {
        speechrnt::utils::Logger::warn("GPU fallback requested but GPU was not being used");
        return false;
    }
    
    speechrnt::utils::Logger::info("Attempting GPU to CPU fallback for utterance " + std::to_string(context.utteranceId));
    
    // Check if we have a registered callback for this error type
    auto callbackIt = recoveryCallbacks_.find(context.errorType);
    if (callbackIt != recoveryCallbacks_.end()) {
        return callbackIt->second(context);
    }
    
    // Default fallback behavior
    auto& gpuManager = speechrnt::utils::GPUManager::getInstance();
    if (gpuManager.isCudaAvailable()) {
        // Reset GPU device to free memory
        gpuManager.resetDevice();
        speechrnt::utils::Logger::info("GPU device reset for fallback to CPU");
    }
    
    return true; // Assume fallback will be handled by caller
}

bool STTErrorRecovery::fallbackQuantization(const STTErrorContext& context) {
    speechrnt::utils::Logger::info("Attempting quantization fallback for utterance " + std::to_string(context.utteranceId));
    
    // Determine next quantization level
    QuantizationLevel nextLevel = context.currentQuantization;
    switch (context.currentQuantization) {
        case QuantizationLevel::FP32:
            nextLevel = QuantizationLevel::FP16;
            break;
        case QuantizationLevel::FP16:
            nextLevel = QuantizationLevel::INT8;
            break;
        case QuantizationLevel::INT8:
            speechrnt::utils::Logger::warn("Already at lowest quantization level (INT8), cannot fallback further");
            return false;
        case QuantizationLevel::AUTO:
            nextLevel = QuantizationLevel::FP16; // Conservative fallback
            break;
    }
    
    QuantizationManager quantManager;
    speechrnt::utils::Logger::info("Falling back from " + quantManager.levelToString(context.currentQuantization) + 
                       " to " + quantManager.levelToString(nextLevel));
    
    // Check if we have a registered callback for this error type
    auto callbackIt = recoveryCallbacks_.find(context.errorType);
    if (callbackIt != recoveryCallbacks_.end()) {
        return callbackIt->second(context);
    }
    
    return true; // Assume fallback will be handled by caller
}

bool STTErrorRecovery::restartComponent(const STTErrorContext& context) {
    speechrnt::utils::Logger::info("Attempting component restart for utterance " + std::to_string(context.utteranceId));
    
    // Check if we have a registered callback for this error type
    auto callbackIt = recoveryCallbacks_.find(context.errorType);
    if (callbackIt != recoveryCallbacks_.end()) {
        return callbackIt->second(context);
    }
    
    // Default restart behavior - indicate restart is needed
    return true; // Assume restart will be handled by caller
}

bool STTErrorRecovery::clearBuffers(const STTErrorContext& context) {
    speechrnt::utils::Logger::info("Attempting buffer clear for utterance " + std::to_string(context.utteranceId));
    
    // Check if we have a registered callback for this error type
    auto callbackIt = recoveryCallbacks_.find(context.errorType);
    if (callbackIt != recoveryCallbacks_.end()) {
        return callbackIt->second(context);
    }
    
    // Default buffer clear behavior
    return true; // Assume buffer clear will be handled by caller
}

bool STTErrorRecovery::reduceQuality(const STTErrorContext& context) {
    speechrnt::utils::Logger::info("Attempting quality reduction for utterance " + std::to_string(context.utteranceId));
    
    // Check if we have a registered callback for this error type
    auto callbackIt = recoveryCallbacks_.find(context.errorType);
    if (callbackIt != recoveryCallbacks_.end()) {
        return callbackIt->second(context);
    }
    
    // Default quality reduction behavior
    return true; // Assume quality reduction will be handled by caller
}

std::chrono::milliseconds STTErrorRecovery::calculateBackoffDelay(int attemptNumber, const RecoveryConfig& config) {
    if (attemptNumber <= 1) {
        return config.initialBackoffMs;
    }
    
    // Exponential backoff with jitter
    double delay = config.initialBackoffMs.count() * std::pow(config.backoffMultiplier, attemptNumber - 1);
    
    // Add random jitter (±25%)
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> jitter(0.75, 1.25);
    delay *= jitter(gen);
    
    // Clamp to maximum
    delay = std::min(delay, static_cast<double>(config.maxBackoffMs.count()));
    
    return std::chrono::milliseconds(static_cast<long long>(delay));
}

bool STTErrorRecovery::isErrorRecoverable(const STTErrorContext& context) {
    if (!context.isRecoverable) {
        return false;
    }
    
    // Check if error type is generally recoverable
    switch (context.errorType) {
        case STTErrorType::MODEL_LOAD_FAILURE:
        case STTErrorType::GPU_MEMORY_ERROR:
        case STTErrorType::TRANSCRIPTION_TIMEOUT:
        case STTErrorType::STREAMING_BUFFER_OVERFLOW:
        case STTErrorType::WHISPER_INFERENCE_ERROR:
        case STTErrorType::VAD_PROCESSING_ERROR:
        case STTErrorType::QUANTIZATION_ERROR:
            return true;
            
        case STTErrorType::AUDIO_FORMAT_ERROR:
        case STTErrorType::NETWORK_ERROR:
        case STTErrorType::RESOURCE_EXHAUSTION:
        case STTErrorType::LANGUAGE_DETECTION_FAILURE:
            return error_utils::isTransientError(context.errorType);
            
        case STTErrorType::UNKNOWN_ERROR:
        default:
            return false; // Conservative approach for unknown errors
    }
}

void STTErrorRecovery::logRecoveryAttempt(const STTErrorContext& context, RecoveryStrategy strategy, bool success) {
    std::stringstream logMsg;
    logMsg << "Recovery attempt for utterance " << context.utteranceId 
           << " (error: " << error_utils::errorTypeToString(context.errorType)
           << ", strategy: " << error_utils::recoveryStrategyToString(strategy)
           << ", result: " << (success ? "SUCCESS" : "FAILED") << ")";
    
    if (success) {
        speechrnt::utils::Logger::info(logMsg.str());
    } else {
        speechrnt::utils::Logger::warn(logMsg.str());
    }
    
    // Record performance metrics
    utils::PerformanceMonitor::getInstance().recordCounter(
        success ? "stt.recovery.success" : "stt.recovery.failure");
}

void STTErrorRecovery::updateStatistics(STTErrorType errorType, bool recoverySuccess) {
    std::lock_guard<std::mutex> lock(statisticsMutex_);
    
    recoveryAttempts_[errorType]++;
    if (recoverySuccess) {
        recoverySuccesses_[errorType]++;
    }
}

STTErrorType STTErrorRecovery::classifyError(const std::string& errorMessage) {
    std::string lowerMsg = errorMessage;
    std::transform(lowerMsg.begin(), lowerMsg.end(), lowerMsg.begin(), ::tolower);
    
    if (lowerMsg.find("model") != std::string::npos && lowerMsg.find("load") != std::string::npos) {
        return STTErrorType::MODEL_LOAD_FAILURE;
    } else if (lowerMsg.find("gpu") != std::string::npos || lowerMsg.find("cuda") != std::string::npos) {
        return STTErrorType::GPU_MEMORY_ERROR;
    } else if (lowerMsg.find("timeout") != std::string::npos) {
        return STTErrorType::TRANSCRIPTION_TIMEOUT;
    } else if (lowerMsg.find("buffer") != std::string::npos && lowerMsg.find("overflow") != std::string::npos) {
        return STTErrorType::STREAMING_BUFFER_OVERFLOW;
    } else if (lowerMsg.find("whisper") != std::string::npos && lowerMsg.find("inference") != std::string::npos) {
        return STTErrorType::WHISPER_INFERENCE_ERROR;
    } else if (lowerMsg.find("quantization") != std::string::npos) {
        return STTErrorType::QUANTIZATION_ERROR;
    } else if (lowerMsg.find("audio") != std::string::npos && lowerMsg.find("format") != std::string::npos) {
        return STTErrorType::AUDIO_FORMAT_ERROR;
    } else if (lowerMsg.find("vad") != std::string::npos) {
        return STTErrorType::VAD_PROCESSING_ERROR;
    } else if (lowerMsg.find("language") != std::string::npos && lowerMsg.find("detection") != std::string::npos) {
        return STTErrorType::LANGUAGE_DETECTION_FAILURE;
    } else if (lowerMsg.find("network") != std::string::npos || lowerMsg.find("connection") != std::string::npos) {
        return STTErrorType::NETWORK_ERROR;
    } else if (lowerMsg.find("memory") != std::string::npos || lowerMsg.find("resource") != std::string::npos) {
        return STTErrorType::RESOURCE_EXHAUSTION;
    }
    
    return STTErrorType::UNKNOWN_ERROR;
}

// Utility functions implementation
namespace error_utils {

std::string errorTypeToString(STTErrorType errorType) {
    switch (errorType) {
        case STTErrorType::MODEL_LOAD_FAILURE: return "MODEL_LOAD_FAILURE";
        case STTErrorType::GPU_MEMORY_ERROR: return "GPU_MEMORY_ERROR";
        case STTErrorType::TRANSCRIPTION_TIMEOUT: return "TRANSCRIPTION_TIMEOUT";
        case STTErrorType::AUDIO_FORMAT_ERROR: return "AUDIO_FORMAT_ERROR";
        case STTErrorType::NETWORK_ERROR: return "NETWORK_ERROR";
        case STTErrorType::RESOURCE_EXHAUSTION: return "RESOURCE_EXHAUSTION";
        case STTErrorType::QUANTIZATION_ERROR: return "QUANTIZATION_ERROR";
        case STTErrorType::STREAMING_BUFFER_OVERFLOW: return "STREAMING_BUFFER_OVERFLOW";
        case STTErrorType::LANGUAGE_DETECTION_FAILURE: return "LANGUAGE_DETECTION_FAILURE";
        case STTErrorType::WHISPER_INFERENCE_ERROR: return "WHISPER_INFERENCE_ERROR";
        case STTErrorType::VAD_PROCESSING_ERROR: return "VAD_PROCESSING_ERROR";
        case STTErrorType::UNKNOWN_ERROR: return "UNKNOWN_ERROR";
        default: return "INVALID_ERROR_TYPE";
    }
}

std::string recoveryStrategyToString(RecoveryStrategy strategy) {
    switch (strategy) {
        case RecoveryStrategy::NONE: return "NONE";
        case RecoveryStrategy::RETRY_SAME: return "RETRY_SAME";
        case RecoveryStrategy::RETRY_WITH_BACKOFF: return "RETRY_WITH_BACKOFF";
        case RecoveryStrategy::FALLBACK_GPU_TO_CPU: return "FALLBACK_GPU_TO_CPU";
        case RecoveryStrategy::FALLBACK_QUANTIZATION: return "FALLBACK_QUANTIZATION";
        case RecoveryStrategy::RESTART_COMPONENT: return "RESTART_COMPONENT";
        case RecoveryStrategy::CLEAR_BUFFERS: return "CLEAR_BUFFERS";
        case RecoveryStrategy::REDUCE_QUALITY: return "REDUCE_QUALITY";
        case RecoveryStrategy::NOTIFY_CLIENT: return "NOTIFY_CLIENT";
        default: return "INVALID_STRATEGY";
    }
}

STTErrorContext createErrorContext(const std::exception& e, uint32_t utteranceId, const std::string& sessionId) {
    STTErrorContext context;
    context.errorMessage = e.what();
    context.utteranceId = utteranceId;
    context.sessionId = sessionId;
    context.timestamp = std::chrono::steady_clock::now();
    
    // Try to extract more information from specific exception types
    if (const auto* sttException = dynamic_cast<const utils::STTException*>(&e)) {
        context.errorType = STTErrorType::WHISPER_INFERENCE_ERROR; // Default for STT exceptions
        context.detailedDescription = sttException->getErrorInfo().details;
    } else {
        // Classify error based on message content
        STTErrorRecovery recovery;
        context.errorType = recovery.classifyError(context.errorMessage);
    }
    
    return context;
}

bool isTransientError(STTErrorType errorType) {
    switch (errorType) {
        case STTErrorType::TRANSCRIPTION_TIMEOUT:
        case STTErrorType::NETWORK_ERROR:
        case STTErrorType::RESOURCE_EXHAUSTION:
        case STTErrorType::STREAMING_BUFFER_OVERFLOW:
        case STTErrorType::VAD_PROCESSING_ERROR:
            return true;
            
        case STTErrorType::MODEL_LOAD_FAILURE:
        case STTErrorType::AUDIO_FORMAT_ERROR:
        case STTErrorType::QUANTIZATION_ERROR:
        case STTErrorType::LANGUAGE_DETECTION_FAILURE:
            return false;
            
        case STTErrorType::GPU_MEMORY_ERROR:
        case STTErrorType::WHISPER_INFERENCE_ERROR:
        case STTErrorType::UNKNOWN_ERROR:
        default:
            return true; // Conservative approach - assume transient
    }
}

RecoveryStrategy getRecommendedStrategy(STTErrorType errorType, int attemptNumber) {
    switch (errorType) {
        case STTErrorType::MODEL_LOAD_FAILURE:
            if (attemptNumber == 1) return RecoveryStrategy::FALLBACK_QUANTIZATION;
            if (attemptNumber == 2) return RecoveryStrategy::FALLBACK_GPU_TO_CPU;
            return RecoveryStrategy::NONE;
            
        case STTErrorType::GPU_MEMORY_ERROR:
            if (attemptNumber == 1) return RecoveryStrategy::FALLBACK_GPU_TO_CPU;
            if (attemptNumber == 2) return RecoveryStrategy::FALLBACK_QUANTIZATION;
            return RecoveryStrategy::NONE;
            
        case STTErrorType::TRANSCRIPTION_TIMEOUT:
            if (attemptNumber <= 2) return RecoveryStrategy::RETRY_WITH_BACKOFF;
            if (attemptNumber == 3) return RecoveryStrategy::REDUCE_QUALITY;
            return RecoveryStrategy::NONE;
            
        case STTErrorType::STREAMING_BUFFER_OVERFLOW:
            return RecoveryStrategy::CLEAR_BUFFERS;
            
        case STTErrorType::WHISPER_INFERENCE_ERROR:
            if (attemptNumber == 1) return RecoveryStrategy::RETRY_WITH_BACKOFF;
            if (attemptNumber == 2) return RecoveryStrategy::FALLBACK_GPU_TO_CPU;
            if (attemptNumber == 3) return RecoveryStrategy::FALLBACK_QUANTIZATION;
            return RecoveryStrategy::NONE;
            
        case STTErrorType::QUANTIZATION_ERROR:
            return RecoveryStrategy::FALLBACK_GPU_TO_CPU;
            
        case STTErrorType::VAD_PROCESSING_ERROR:
            if (attemptNumber <= 2) return RecoveryStrategy::RETRY_WITH_BACKOFF;
            return RecoveryStrategy::RESTART_COMPONENT;
            
        case STTErrorType::NETWORK_ERROR:
        case STTErrorType::RESOURCE_EXHAUSTION:
            if (attemptNumber <= 3) return RecoveryStrategy::RETRY_WITH_BACKOFF;
            return RecoveryStrategy::NONE;
            
        case STTErrorType::AUDIO_FORMAT_ERROR:
        case STTErrorType::LANGUAGE_DETECTION_FAILURE:
        case STTErrorType::UNKNOWN_ERROR:
        default:
            if (attemptNumber == 1) return RecoveryStrategy::RETRY_SAME;
            return RecoveryStrategy::NONE;
    }
}

} // namespace error_utils

} // namespace stt