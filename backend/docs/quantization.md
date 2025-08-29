# Model Quantization Support

This document describes the model quantization support implemented in the WhisperSTT system, which allows for memory-efficient model loading and inference with different precision levels.

## Overview

Model quantization reduces the precision of model weights and activations to decrease memory usage and potentially improve inference speed, with a trade-off in accuracy. The system supports three quantization levels:

- **FP32**: Full precision (32-bit floating point) - baseline accuracy
- **FP16**: Half precision (16-bit floating point) - ~50% memory reduction, ~2% accuracy loss
- **INT8**: 8-bit integer quantization - ~75% memory reduction, ~5% accuracy loss

## Features

### Automatic Quantization Level Selection

The system can automatically select the optimal quantization level based on available GPU memory:

```cpp
// Automatic selection based on hardware
whisperSTT->setQuantizationLevel(QuantizationLevel::AUTO);

// Or use the manager directly
QuantizationManager manager;
auto optimalLevel = manager.selectOptimalLevel(availableMemoryMB, modelSizeMB);
```

### Model Caching

Different quantization levels can be cached simultaneously, allowing for quick switching between precision levels without reloading models:

```cpp
// Initialize with specific quantization level
whisperSTT->initializeWithQuantization(modelPath, QuantizationLevel::FP16);

// Switch to different level (will load if not cached)
whisperSTT->setQuantizationLevel(QuantizationLevel::INT8);
```

### Accuracy Validation

The system provides accuracy validation for quantized models:

```cpp
std::vector<std::string> validationAudio = {"test1.wav", "test2.wav"};
std::vector<std::string> expectedTexts = {"hello world", "test speech"};

auto result = whisperSTT->validateQuantizedModel(validationAudio, expectedTexts);
if (result.passesThreshold) {
    std::cout << "Model accuracy acceptable: " << result.confidenceScore << std::endl;
}
```

## Usage Examples

### Basic Usage

```cpp
#include "stt/whisper_stt.hpp"
#include "stt/quantization_config.hpp"

// Create STT instance
auto whisperSTT = std::make_unique<WhisperSTT>();

// Check supported quantization levels
auto supportedLevels = whisperSTT->getSupportedQuantizationLevels();

// Initialize with FP16 quantization
bool success = whisperSTT->initializeWithQuantization(
    "/models/whisper-base.bin", 
    QuantizationLevel::FP16
);

if (success) {
    // Use for transcription
    whisperSTT->transcribe(audioData, [](const TranscriptionResult& result) {
        std::cout << "Transcription: " << result.text << std::endl;
    });
}
```

### GPU-Accelerated Quantization

```cpp
// Initialize with GPU acceleration and quantization
bool success = whisperSTT->initializeWithQuantizationGPU(
    "/models/whisper-base.bin",
    QuantizationLevel::FP16,
    0,  // GPU device ID
    4   // number of threads
);
```

### Advanced Configuration

```cpp
QuantizationManager manager;

// Set custom accuracy threshold
manager.setAccuracyThreshold(0.9f);  // 90% minimum accuracy

// Get quantization configuration
auto config = manager.getConfig(QuantizationLevel::FP16);
std::cout << "Expected accuracy loss: " << config.expectedAccuracyLoss << std::endl;
std::cout << "Minimum GPU memory: " << config.minGPUMemoryMB << "MB" << std::endl;

// Generate quantized model path
std::string quantizedPath = manager.getQuantizedModelPath(
    "/models/whisper-base.bin", 
    QuantizationLevel::FP16
);
// Result: "/models/whisper-base_fp16.bin"
```

## Hardware Requirements

### FP32 (Full Precision)
- **GPU Memory**: 2GB+ recommended
- **Compute Capability**: Any (can run on CPU)
- **Accuracy**: Baseline (100%)

### FP16 (Half Precision)
- **GPU Memory**: 1GB+ recommended
- **Compute Capability**: 5.3+ (Maxwell, Pascal, Volta, Turing, Ampere)
- **Accuracy**: ~98% of FP32

### INT8 (8-bit Quantization)
- **GPU Memory**: 512MB+ recommended
- **Compute Capability**: 6.1+ (Pascal, Volta, Turing, Ampere)
- **Accuracy**: ~95% of FP32

## Model File Organization

The system expects quantized models to follow this naming convention:

```
/models/
├── whisper-base.bin          # FP32 (original)
├── whisper-base_fp16.bin     # FP16 quantized
├── whisper-base_int8.bin     # INT8 quantized
└── whisper-large.bin         # FP32 (original)
    ├── whisper-large_fp16.bin # FP16 quantized
    └── whisper-large_int8.bin # INT8 quantized
```

If quantized model files are not found, the system will:
1. Use the original model with runtime quantization (if supported)
2. Fall back to FP32 precision
3. Log appropriate warnings

## Performance Characteristics

### Memory Usage (Approximate)

| Model Size | FP32 | FP16 | INT8 |
|------------|------|------|------|
| Base (74MB) | ~220MB | ~110MB | ~55MB |
| Small (244MB) | ~730MB | ~365MB | ~180MB |
| Medium (769MB) | ~2.3GB | ~1.2GB | ~600MB |
| Large (1550MB) | ~4.7GB | ~2.4GB | ~1.2GB |

### Inference Speed

- **FP16**: 1.2-1.8x faster than FP32 on modern GPUs
- **INT8**: 1.5-2.5x faster than FP32 on supported hardware
- **CPU**: FP32 recommended (quantization may not improve speed)

## Error Handling

The quantization system includes comprehensive error handling:

```cpp
// Check if quantization level is supported
if (!whisperSTT->validateQuantizationSupport(QuantizationLevel::INT8)) {
    std::cout << "INT8 not supported, falling back to FP16" << std::endl;
    whisperSTT->setQuantizationLevel(QuantizationLevel::FP16);
}

// Handle initialization failures
if (!whisperSTT->initializeWithQuantization(modelPath, level)) {
    std::cout << "Error: " << whisperSTT->getLastError() << std::endl;
    // Try fallback level
    whisperSTT->initializeWithQuantization(modelPath, QuantizationLevel::FP32);
}
```

## Best Practices

1. **Start with AUTO**: Let the system select the optimal quantization level
2. **Validate accuracy**: Use validation datasets to ensure acceptable quality
3. **Monitor memory**: Check GPU memory usage to avoid out-of-memory errors
4. **Fallback strategy**: Always have FP32 as a fallback option
5. **Model preparation**: Pre-quantize models offline for best performance

## Troubleshooting

### Common Issues

**Issue**: "Quantization level not supported"
- **Solution**: Check GPU compute capability and available memory

**Issue**: "Quantized model file not found"
- **Solution**: Ensure quantized models are available or use runtime quantization

**Issue**: "Out of GPU memory"
- **Solution**: Use lower quantization level or increase GPU memory

**Issue**: "Poor transcription quality"
- **Solution**: Validate model accuracy and consider higher precision level

### Debug Information

Enable debug logging to get detailed quantization information:

```cpp
// The system will log:
// - Selected quantization level and reasoning
// - Model loading status
// - Memory usage statistics
// - Fallback decisions
```

## Integration with Existing Code

The quantization system is designed to be backward compatible. Existing code will continue to work without changes, using FP32 precision by default. To enable quantization, simply replace initialization calls:

```cpp
// Old way
whisperSTT->initialize(modelPath);

// New way with quantization
whisperSTT->initializeWithQuantization(modelPath, QuantizationLevel::AUTO);
```