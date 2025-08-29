#include "stt/stt_error_recovery.hpp"
#include "stt/whisper_stt.hpp"
#include "utils/logging.hpp"
#include "utils/gpu_manager.hpp"
#include <iostream>
#include <memory>
#include <vector>

using namespace stt;

/**
 * Example demonstrating STT Error Recovery System integration
 * 
 * This example shows how to:
 * 1. Initialize the error recovery system
 * 2. Register recovery callbacks for different error types
 * 3. Handle STT errors with automatic recovery
 * 4. Monitor recovery statistics and history
 */

class STTErrorRecoveryExample {
private:
    std::unique_ptr<STTErrorRecovery> errorRecovery_;
    std::unique_ptr<WhisperSTT> whisperSTT_;
    
public:
    STTErrorRecoveryExample() {
        utils::Logger::initialize();
        
        // Initialize error recovery system
        errorRecovery_ = std::make_unique<STTErrorRecovery>();
        
        RecoveryConfig config;
        config.maxRetryAttempts = 3;
        config.initialBackoffMs = std::chrono::milliseconds(100);
        config.maxBackoffMs = std::chrono::milliseconds(2000);
        config.backoffMultiplier = 2.0;
        config.enableGPUFallback = true;
        config.enableQuantizationFallback = true;
        config.enableBufferClear = true;
        
        if (!errorRecovery_->initialize(config)) {
            throw std::runtime_error("Failed to initialize error recovery system");
        }
        
        // Initialize WhisperSTT
        whisperSTT_ = std::make_unique<WhisperSTT>();
        
        setupRecoveryCallbacks();
        setupNotificationCallback();
        
        std::cout << "STT Error Recovery Example initialized successfully" << std::endl;
    }
    
    void setupRecoveryCallbacks() {
        // Model load failure recovery
        errorRecovery_->registerRecoveryCallback(
            STTErrorType::MODEL_LOAD_FAILURE,
            [this](const STTErrorContext& context) -> bool {
                std::cout << "Attempting model load recovery for utterance " << context.utteranceId << std::endl;
                
                // Try with different quantization level
                if (context.currentQuantization == QuantizationLevel::FP32) {
                    std::cout << "Retrying with FP16 quantization" << std::endl;
                    return whisperSTT_->initializeWithQuantization(context.modelPath, QuantizationLevel::FP16);
                } else if (context.currentQuantization == QuantizationLevel::FP16) {
                    std::cout << "Retrying with INT8 quantization" << std::endl;
                    return whisperSTT_->initializeWithQuantization(context.modelPath, QuantizationLevel::INT8);
                }
                
                return false;
            }
        );
        
        // GPU memory error recovery
        errorRecovery_->registerRecoveryCallback(
            STTErrorType::GPU_MEMORY_ERROR,
            [this](const STTErrorContext& context) -> bool {
                std::cout << "Attempting GPU memory error recovery for utterance " << context.utteranceId << std::endl;
                
                if (context.wasUsingGPU) {
                    std::cout << "Falling back to CPU processing" << std::endl;
                    
                    // Reset GPU and reinitialize with CPU
                    auto& gpuManager = speechrnt::utils::GPUManager::getInstance();
                    gpuManager.resetDevice();
                    
                    return whisperSTT_->initialize(context.modelPath);
                }
                
                return false;
            }
        );
        
        // Transcription timeout recovery
        errorRecovery_->registerRecoveryCallback(
            STTErrorType::TRANSCRIPTION_TIMEOUT,
            [this](const STTErrorContext& context) -> bool {
                std::cout << "Attempting transcription timeout recovery for utterance " << context.utteranceId << std::endl;
                
                // Clear any pending transcriptions and retry
                if (whisperSTT_->isStreamingActive(context.utteranceId)) {
                    whisperSTT_->finalizeStreamingTranscription(context.utteranceId);
                }
                
                return true; // Indicate retry should be attempted
            }
        );
        
        // Buffer overflow recovery
        errorRecovery_->registerRecoveryCallback(
            STTErrorType::STREAMING_BUFFER_OVERFLOW,
            [this](const STTErrorContext& context) -> bool {
                std::cout << "Attempting buffer overflow recovery for utterance " << context.utteranceId << std::endl;
                
                // Clear streaming state and restart
                if (whisperSTT_->isStreamingActive(context.utteranceId)) {
                    whisperSTT_->finalizeStreamingTranscription(context.utteranceId);
                }
                
                // Restart streaming with fresh buffers
                whisperSTT_->startStreamingTranscription(context.utteranceId);
                return true;
            }
        );
        
        // Whisper inference error recovery
        errorRecovery_->registerRecoveryCallback(
            STTErrorType::WHISPER_INFERENCE_ERROR,
            [this](const STTErrorContext& context) -> bool {
                std::cout << "Attempting Whisper inference error recovery for utterance " << context.utteranceId << std::endl;
                
                // Try reducing quality settings or switching to CPU
                if (context.wasUsingGPU) {
                    std::cout << "Switching to CPU for inference" << std::endl;
                    return whisperSTT_->initialize(context.modelPath);
                }
                
                return true; // Indicate retry should be attempted
            }
        );
    }
    
    void setupNotificationCallback() {
        errorRecovery_->setNotificationCallback(
            [](const STTErrorContext& context, const RecoveryResult& result) {
                std::cout << "\n=== Recovery Notification ===" << std::endl;
                std::cout << "Utterance ID: " << context.utteranceId << std::endl;
                std::cout << "Error Type: " << error_utils::errorTypeToString(context.errorType) << std::endl;
                std::cout << "Error Message: " << context.errorMessage << std::endl;
                std::cout << "Recovery Strategy: " << error_utils::recoveryStrategyToString(result.strategyUsed) << std::endl;
                std::cout << "Recovery Success: " << (result.success ? "YES" : "NO") << std::endl;
                std::cout << "Recovery Time: " << result.recoveryTime.count() << "ms" << std::endl;
                std::cout << "Result Message: " << result.resultMessage << std::endl;
                
                if (result.requiresClientNotification) {
                    std::cout << "*** CLIENT NOTIFICATION REQUIRED ***" << std::endl;
                }
                std::cout << "============================\n" << std::endl;
            }
        );
    }
    
    void simulateErrors() {
        std::cout << "\n=== Simulating STT Errors ===" << std::endl;
        
        // Simulate model load failure
        {
            STTErrorContext context;
            context.errorType = STTErrorType::MODEL_LOAD_FAILURE;
            context.errorMessage = "Failed to load Whisper model: insufficient GPU memory";
            context.utteranceId = 1;
            context.sessionId = "example_session";
            context.modelPath = "models/whisper-base.bin";
            context.currentQuantization = QuantizationLevel::FP32;
            context.wasUsingGPU = true;
            context.isRecoverable = true;
            
            std::cout << "Simulating model load failure..." << std::endl;
            RecoveryResult result = errorRecovery_->handleError(context);
            std::cout << "Recovery result: " << (result.success ? "SUCCESS" : "FAILED") << std::endl;
        }
        
        // Simulate GPU memory error
        {
            STTErrorContext context;
            context.errorType = STTErrorType::GPU_MEMORY_ERROR;
            context.errorMessage = "CUDA out of memory during inference";
            context.utteranceId = 2;
            context.sessionId = "example_session";
            context.modelPath = "models/whisper-base.bin";
            context.currentQuantization = QuantizationLevel::FP32;
            context.wasUsingGPU = true;
            context.gpuDeviceId = 0;
            context.isRecoverable = true;
            
            std::cout << "Simulating GPU memory error..." << std::endl;
            RecoveryResult result = errorRecovery_->handleError(context);
            std::cout << "Recovery result: " << (result.success ? "SUCCESS" : "FAILED") << std::endl;
        }
        
        // Simulate transcription timeout
        {
            STTErrorContext context;
            context.errorType = STTErrorType::TRANSCRIPTION_TIMEOUT;
            context.errorMessage = "Transcription timed out after 5 seconds";
            context.utteranceId = 3;
            context.sessionId = "example_session";
            context.audioBufferSize = 80000; // 5 seconds at 16kHz
            context.isRecoverable = true;
            
            std::cout << "Simulating transcription timeout..." << std::endl;
            RecoveryResult result = errorRecovery_->handleError(context);
            std::cout << "Recovery result: " << (result.success ? "SUCCESS" : "FAILED") << std::endl;
        }
        
        // Simulate buffer overflow
        {
            STTErrorContext context;
            context.errorType = STTErrorType::STREAMING_BUFFER_OVERFLOW;
            context.errorMessage = "Audio buffer overflow: buffer size exceeded 8MB limit";
            context.utteranceId = 4;
            context.sessionId = "example_session";
            context.audioBufferSize = 8 * 1024 * 1024; // 8MB
            context.isRecoverable = true;
            
            std::cout << "Simulating buffer overflow..." << std::endl;
            RecoveryResult result = errorRecovery_->handleError(context);
            std::cout << "Recovery result: " << (result.success ? "SUCCESS" : "FAILED") << std::endl;
        }
    }
    
    void demonstrateRecoveryInProgress() {
        std::cout << "\n=== Demonstrating Recovery In Progress ===" << std::endl;
        
        // Register a slow recovery callback
        errorRecovery_->registerRecoveryCallback(
            STTErrorType::WHISPER_INFERENCE_ERROR,
            [](const STTErrorContext& context) -> bool {
                std::cout << "Performing slow recovery operation..." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                return true;
            }
        );
        
        STTErrorContext context;
        context.errorType = STTErrorType::WHISPER_INFERENCE_ERROR;
        context.errorMessage = "Whisper inference failed";
        context.utteranceId = 5;
        context.sessionId = "example_session";
        context.isRecoverable = true;
        
        // Start recovery in a separate thread
        std::thread recoveryThread([this, &context]() {
            errorRecovery_->handleError(context);
        });
        
        // Check recovery status
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "Recovery in progress for utterance 5: " 
                  << (errorRecovery_->isRecoveryInProgress(5) ? "YES" : "NO") << std::endl;
        
        // Wait for completion
        recoveryThread.join();
        
        std::cout << "Recovery completed for utterance 5: " 
                  << (!errorRecovery_->isRecoveryInProgress(5) ? "YES" : "NO") << std::endl;
    }
    
    void showStatistics() {
        std::cout << "\n=== Recovery Statistics ===" << std::endl;
        
        auto stats = errorRecovery_->getRecoveryStatistics();
        for (const auto& pair : stats) {
            std::cout << error_utils::errorTypeToString(pair.first) 
                      << ": " << pair.second << " attempts" << std::endl;
        }
        
        std::cout << "\n=== Recent Error History ===" << std::endl;
        auto history = errorRecovery_->getRecentErrors(5);
        for (size_t i = 0; i < history.size(); ++i) {
            const auto& error = history[i];
            std::cout << (i + 1) << ". " << error_utils::errorTypeToString(error.errorType)
                      << " (Utterance " << error.utteranceId << "): " << error.errorMessage << std::endl;
        }
    }
    
    void demonstrateCustomConfiguration() {
        std::cout << "\n=== Custom Recovery Configuration ===" << std::endl;
        
        // Configure specific recovery settings for model load failures
        RecoveryConfig modelLoadConfig;
        modelLoadConfig.maxRetryAttempts = 2;
        modelLoadConfig.initialBackoffMs = std::chrono::milliseconds(50);
        modelLoadConfig.enableQuantizationFallback = true;
        modelLoadConfig.enableGPUFallback = false; // Don't fallback to CPU for model loads
        
        errorRecovery_->configureRecovery(STTErrorType::MODEL_LOAD_FAILURE, modelLoadConfig);
        
        std::cout << "Configured custom recovery settings for model load failures" << std::endl;
        std::cout << "- Max retry attempts: 2" << std::endl;
        std::cout << "- Initial backoff: 50ms" << std::endl;
        std::cout << "- Quantization fallback: enabled" << std::endl;
        std::cout << "- GPU fallback: disabled" << std::endl;
    }
    
    void run() {
        std::cout << "STT Error Recovery System Example" << std::endl;
        std::cout << "=================================" << std::endl;
        
        demonstrateCustomConfiguration();
        simulateErrors();
        demonstrateRecoveryInProgress();
        showStatistics();
        
        std::cout << "\nExample completed successfully!" << std::endl;
    }
};

int main() {
    try {
        STTErrorRecoveryExample example;
        example.run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}