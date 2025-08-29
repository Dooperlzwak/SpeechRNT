# STT Error Recovery System

The STT Error Recovery System provides intelligent error handling and recovery mechanisms for Speech-to-Text operations in the SpeechRNT backend. It implements retry logic with exponential backoff, fallback strategies (GPU→CPU, FP32→FP16→INT8), error context tracking, and recovery attempt logging.

## Overview

The error recovery system is designed to handle various types of STT failures gracefully, ensuring maximum uptime and user experience quality. It provides:

- **Automatic Error Classification**: Categorizes errors into specific types for targeted recovery
- **Intelligent Recovery Strategies**: Implements multiple recovery approaches based on error type
- **Exponential Backoff**: Prevents system overload during retry attempts
- **Fallback Mechanisms**: Graceful degradation from GPU to CPU, high to low precision
- **Context Tracking**: Maintains detailed error context for debugging and analysis
- **Statistics and Monitoring**: Tracks recovery success rates and error patterns

## Architecture

### Core Components

1. **STTErrorRecovery**: Main error recovery orchestrator
2. **STTErrorContext**: Detailed error information and context
3. **RecoveryStrategy**: Enumeration of available recovery approaches
4. **RecoveryConfig**: Configuration for recovery behavior
5. **Utility Functions**: Helper functions for error classification and strategy selection

### Error Types

```cpp
enum class STTErrorType {
    MODEL_LOAD_FAILURE,           // Failed to load Whisper model
    GPU_MEMORY_ERROR,             // GPU out of memory
    TRANSCRIPTION_TIMEOUT,        // Transcription took too long
    AUDIO_FORMAT_ERROR,           // Invalid audio format
    NETWORK_ERROR,                // Network connectivity issues
    RESOURCE_EXHAUSTION,          // System resources exhausted
    QUANTIZATION_ERROR,           // Model quantization failed
    STREAMING_BUFFER_OVERFLOW,    // Audio buffer overflow
    LANGUAGE_DETECTION_FAILURE,   // Language detection failed
    WHISPER_INFERENCE_ERROR,      // Whisper inference failed
    VAD_PROCESSING_ERROR,         // Voice activity detection error
    UNKNOWN_ERROR                 // Unclassified error
};
```

### Recovery Strategies

```cpp
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
```

## Usage

### Basic Initialization

```cpp
#include "stt/stt_error_recovery.hpp"

// Create and initialize error recovery system
auto errorRecovery = std::make_unique<STTErrorRecovery>();

RecoveryConfig config;
config.maxRetryAttempts = 3;
config.initialBackoffMs = std::chrono::milliseconds(100);
config.maxBackoffMs = std::chrono::milliseconds(2000);
config.backoffMultiplier = 2.0;
config.enableGPUFallback = true;
config.enableQuantizationFallback = true;

if (!errorRecovery->initialize(config)) {
    throw std::runtime_error("Failed to initialize error recovery");
}
```

### Registering Recovery Callbacks

```cpp
// Register callback for model load failures
errorRecovery->registerRecoveryCallback(
    STTErrorType::MODEL_LOAD_FAILURE,
    [&whisperSTT](const STTErrorContext& context) -> bool {
        // Try with different quantization level
        if (context.currentQuantization == QuantizationLevel::FP32) {
            return whisperSTT->initializeWithQuantization(
                context.modelPath, QuantizationLevel::FP16);
        }
        return false;
    }
);

// Register callback for GPU memory errors
errorRecovery->registerRecoveryCallback(
    STTErrorType::GPU_MEMORY_ERROR,
    [&whisperSTT](const STTErrorContext& context) -> bool {
        if (context.wasUsingGPU) {
            // Fallback to CPU processing
            return whisperSTT->initialize(context.modelPath);
        }
        return false;
    }
);
```

### Handling Errors

```cpp
// Create error context
STTErrorContext context;
context.errorType = STTErrorType::TRANSCRIPTION_TIMEOUT;
context.errorMessage = "Transcription timed out after 5 seconds";
context.utteranceId = utteranceId;
context.sessionId = sessionId;
context.isRecoverable = true;

// Attempt recovery
RecoveryResult result = errorRecovery->handleError(context);

if (result.success) {
    std::cout << "Recovery successful using strategy: " 
              << error_utils::recoveryStrategyToString(result.strategyUsed) << std::endl;
} else {
    std::cout << "Recovery failed: " << result.resultMessage << std::endl;
    if (result.requiresClientNotification) {
        // Notify client of service degradation
        notifyClient(context, result);
    }
}
```

### Monitoring and Statistics

```cpp
// Get recovery statistics
auto stats = errorRecovery->getRecoveryStatistics();
for (const auto& pair : stats) {
    std::cout << error_utils::errorTypeToString(pair.first) 
              << ": " << pair.second << " attempts" << std::endl;
}

// Get recent error history
auto history = errorRecovery->getRecentErrors(10);
for (const auto& error : history) {
    std::cout << "Error: " << error.errorMessage 
              << " (Type: " << error_utils::errorTypeToString(error.errorType) << ")"
              << std::endl;
}

// Check if recovery is in progress
if (errorRecovery->isRecoveryInProgress(utteranceId)) {
    std::cout << "Recovery in progress for utterance " << utteranceId << std::endl;
}
```

## Configuration

### Recovery Configuration

```cpp
struct RecoveryConfig {
    int maxRetryAttempts;                    // Maximum retry attempts (default: 3)
    std::chrono::milliseconds initialBackoffMs;  // Initial backoff delay (default: 100ms)
    std::chrono::milliseconds maxBackoffMs;      // Maximum backoff delay (default: 5000ms)
    double backoffMultiplier;                // Backoff multiplier (default: 2.0)
    bool enableGPUFallback;                  // Enable GPU to CPU fallback (default: true)
    bool enableQuantizationFallback;         // Enable quantization fallback (default: true)
    bool enableBufferClear;                  // Enable buffer clearing (default: true)
    std::chrono::milliseconds recoveryTimeoutMs; // Recovery timeout (default: 10000ms)
};
```

### Per-Error-Type Configuration

```cpp
// Configure specific recovery settings for different error types
RecoveryConfig gpuErrorConfig;
gpuErrorConfig.maxRetryAttempts = 1;  // Don't retry GPU errors multiple times
gpuErrorConfig.enableGPUFallback = true;
gpuErrorConfig.enableQuantizationFallback = true;

errorRecovery->configureRecovery(STTErrorType::GPU_MEMORY_ERROR, gpuErrorConfig);

RecoveryConfig timeoutConfig;
timeoutConfig.maxRetryAttempts = 2;
timeoutConfig.initialBackoffMs = std::chrono::milliseconds(50);
timeoutConfig.enableBufferClear = true;

errorRecovery->configureRecovery(STTErrorType::TRANSCRIPTION_TIMEOUT, timeoutConfig);
```

## Recovery Strategies by Error Type

### Model Load Failure
1. **First Attempt**: Fallback to lower quantization (FP32 → FP16 → INT8)
2. **Second Attempt**: Fallback to CPU processing
3. **Final**: Notify client of service unavailability

### GPU Memory Error
1. **First Attempt**: Immediate fallback to CPU processing
2. **Second Attempt**: Fallback to lower quantization on CPU
3. **Final**: Reduce processing quality

### Transcription Timeout
1. **First Attempt**: Retry with exponential backoff
2. **Second Attempt**: Clear buffers and retry
3. **Third Attempt**: Reduce processing quality

### Streaming Buffer Overflow
1. **First Attempt**: Clear buffers and restart streaming
2. **Fallback**: Reduce buffer size and continue

### Whisper Inference Error
1. **First Attempt**: Retry with backoff
2. **Second Attempt**: Fallback to CPU processing
3. **Third Attempt**: Fallback to lower quantization

## Integration with WhisperSTT

The error recovery system integrates seamlessly with the WhisperSTT class:

```cpp
class WhisperSTTWithRecovery {
private:
    std::unique_ptr<WhisperSTT> whisperSTT_;
    std::unique_ptr<STTErrorRecovery> errorRecovery_;
    
public:
    void transcribeWithRecovery(const std::vector<float>& audioData, 
                               TranscriptionCallback callback) {
        try {
            whisperSTT_->transcribe(audioData, callback);
        } catch (const std::exception& e) {
            // Create error context
            STTErrorContext context = error_utils::createErrorContext(e);
            context.utteranceId = getCurrentUtteranceId();
            context.sessionId = getCurrentSessionId();
            
            // Attempt recovery
            RecoveryResult result = errorRecovery_->handleError(context);
            
            if (result.success) {
                // Retry transcription after successful recovery
                whisperSTT_->transcribe(audioData, callback);
            } else {
                // Recovery failed, propagate error
                throw;
            }
        }
    }
};
```

## Best Practices

### 1. Error Classification
- Use specific error types for better recovery strategy selection
- Include detailed error context information
- Set appropriate recoverability flags

### 2. Recovery Callbacks
- Keep recovery callbacks lightweight and fast
- Implement proper error handling within callbacks
- Return accurate success/failure status

### 3. Configuration
- Tune retry attempts based on error type criticality
- Set reasonable backoff delays to prevent system overload
- Enable appropriate fallback mechanisms for your deployment

### 4. Monitoring
- Regularly check recovery statistics for patterns
- Monitor recovery success rates
- Alert on high error rates or recovery failures

### 5. Testing
- Test recovery scenarios in development
- Simulate various error conditions
- Validate fallback behavior under load

## Performance Considerations

### Memory Usage
- Error history is limited to prevent memory growth
- Active recovery tracking is cleaned up automatically
- Statistics are stored efficiently in hash maps

### Latency Impact
- Recovery attempts add latency to failed operations
- Exponential backoff prevents rapid retry storms
- Fallback strategies are ordered by speed

### Thread Safety
- All operations are thread-safe with appropriate locking
- Recovery callbacks may be called from different threads
- Statistics and history updates are synchronized

## Troubleshooting

### Common Issues

1. **Recovery Callbacks Not Called**
   - Verify error type classification is correct
   - Check if recovery is enabled
   - Ensure callbacks are registered before handling errors

2. **Excessive Retry Attempts**
   - Review maxRetryAttempts configuration
   - Check if errors are properly classified as non-recoverable
   - Verify backoff delays are appropriate

3. **Memory Leaks**
   - Ensure active recoveries are cleaned up
   - Check error history size limits
   - Verify callback lifecycle management

### Debugging

Enable detailed logging to trace recovery attempts:

```cpp
// Error recovery system logs all attempts automatically
// Check logs for recovery strategy selection and execution
utils::Logger::info("Recovery attempt details logged automatically");

// Get detailed error history for analysis
auto history = errorRecovery->getRecentErrors(50);
for (const auto& error : history) {
    analyzeErrorPattern(error);
}
```

## Examples

See `backend/examples/stt_error_recovery_example.cpp` for a complete working example demonstrating all features of the error recovery system.

## Testing

Unit tests are available in `backend/tests/unit/test_stt_error_recovery.cpp` covering:
- Error type classification
- Recovery strategy selection
- Callback registration and execution
- Statistics and history tracking
- Configuration management
- Thread safety