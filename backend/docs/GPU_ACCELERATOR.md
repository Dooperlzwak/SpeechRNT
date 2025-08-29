# GPU Accelerator Component

## Overview

The GPUAccelerator component provides GPU acceleration management specifically for Machine Translation (MT) operations in SpeechRNT. It wraps the existing GPU utilities and provides MT-specific functionality including model loading, translation acceleration, performance monitoring, and error handling.

## Features

### GPU Discovery and Management
- Automatic detection of compatible GPUs
- GPU device selection and validation
- CUDA context and stream management
- GPU compatibility assessment for MT workloads

### Memory Management
- GPU memory allocation and deallocation
- Memory pool configuration with defragmentation
- Memory usage monitoring and optimization
- Automatic cleanup of unused resources

### Model Operations
- Loading Marian NMT models to GPU
- Model quantization support (FP16, INT8)
- Model lifecycle management with LRU caching
- Hot-swapping of models without service interruption

### Translation Acceleration
- Single translation acceleration
- Batch translation processing
- Streaming translation sessions
- Concurrent stream processing

### Performance Monitoring
- Real-time GPU performance statistics
- Performance threshold monitoring
- Historical performance data
- Performance alerts and warnings

### Error Handling and Recovery
- Comprehensive GPU error handling
- Automatic recovery mechanisms
- CPU fallback support
- Device reset and reinitialization

## Usage

### Basic Initialization

```cpp
#include "mt/gpu_accelerator.hpp"

using namespace speechrnt::mt;

// Create and initialize GPU accelerator
GPUAccelerator accelerator;

if (!accelerator.initialize()) {
    std::cerr << "Failed to initialize GPU accelerator: " 
              << accelerator.getLastGPUError() << std::endl;
    return false;
}

// Check if GPU is available
if (accelerator.isGPUAvailable()) {
    std::cout << "GPU acceleration available" << std::endl;
    
    // Get current GPU info
    auto gpuInfo = accelerator.getCurrentGPUInfo();
    std::cout << "Using GPU: " << gpuInfo.deviceName << std::endl;
} else {
    std::cout << "No compatible GPU found, using CPU fallback" << std::endl;
    accelerator.enableCPUFallback(true);
}
```

### Configuration

```cpp
// Configure memory pool
accelerator.configureMemoryPool(1024, true); // 1GB pool with defragmentation

// Enable quantization
accelerator.configureQuantization(true, "fp16"); // FP16 precision

// Configure batch processing
accelerator.configureBatchProcessing(32, 8); // Max 32, optimal 8

// Enable concurrent streams
accelerator.configureConcurrentStreams(true, 4); // 4 streams

// Set performance thresholds
accelerator.setPerformanceThresholds(80.0f, 85.0f, 90.0f);
```

### Model Loading

```cpp
// Load model to GPU
void* gpuModelPtr = nullptr;
std::string modelPath = "models/en-es.npz";
std::string languagePair = "en-es";

if (accelerator.loadModelToGPU(modelPath, languagePair, &gpuModelPtr)) {
    std::cout << "Model loaded successfully" << std::endl;
    
    // Check if model is loaded
    if (accelerator.isModelLoadedOnGPU(languagePair)) {
        std::cout << "Model is ready for translation" << std::endl;
    }
} else {
    std::cerr << "Failed to load model: " << accelerator.getLastGPUError() << std::endl;
}

// Get loaded models information
auto loadedModels = accelerator.getLoadedModels();
for (const auto& model : loadedModels) {
    std::cout << "Loaded: " << model.languagePair 
              << " (" << model.memorySizeMB << " MB)" << std::endl;
}
```

### Translation Acceleration

```cpp
// Single translation
std::string input = "Hello, how are you?";
std::string output;

if (accelerator.accelerateTranslation(gpuModelPtr, input, output)) {
    std::cout << "Translation: " << input << " -> " << output << std::endl;
} else {
    std::cerr << "Translation failed: " << accelerator.getLastGPUError() << std::endl;
}

// Batch translation
std::vector<std::string> inputs = {
    "Good morning",
    "How are you?",
    "Thank you"
};
std::vector<std::string> outputs;

if (accelerator.accelerateBatchTranslation(gpuModelPtr, inputs, outputs)) {
    for (size_t i = 0; i < inputs.size(); ++i) {
        std::cout << inputs[i] << " -> " << outputs[i] << std::endl;
    }
}
```

### Streaming Translation

```cpp
// Start streaming session
std::string sessionId = "session_001";

if (accelerator.startStreamingSession(gpuModelPtr, sessionId)) {
    // Process streaming chunks
    std::vector<std::string> chunks = {"Hello", " there,", " how", " are", " you?"};
    
    for (const auto& chunk : chunks) {
        std::string outputChunk;
        if (accelerator.processStreamingChunk(sessionId, chunk, outputChunk)) {
            std::cout << "Chunk: " << chunk << " -> " << outputChunk << std::endl;
        }
    }
    
    // End session
    accelerator.endStreamingSession(sessionId);
}
```

### Performance Monitoring

```cpp
// Start performance monitoring
accelerator.startPerformanceMonitoring(1000); // 1 second interval

// Get current statistics
auto stats = accelerator.getGPUStatistics();
std::cout << "GPU Utilization: " << stats.utilizationPercent << "%" << std::endl;
std::cout << "Memory Used: " << stats.memoryUsedMB << " MB" << std::endl;
std::cout << "Temperature: " << stats.temperatureCelsius << "°C" << std::endl;

// Check for performance alerts
auto alerts = accelerator.getPerformanceAlerts();
for (const auto& alert : alerts) {
    std::cout << "Alert: " << alert << std::endl;
}

// Stop monitoring
accelerator.stopPerformanceMonitoring();
```

### Error Handling

```cpp
// Enable CPU fallback
accelerator.enableCPUFallback(true);

// Handle GPU errors
if (!accelerator.isGPUOperational()) {
    std::cout << "GPU not operational, attempting recovery..." << std::endl;
    
    if (accelerator.recoverFromGPUError()) {
        std::cout << "GPU recovery successful" << std::endl;
    } else if (accelerator.isCPUFallbackEnabled()) {
        std::cout << "Falling back to CPU processing" << std::endl;
        accelerator.fallbackToCPU("GPU recovery failed");
    }
}

// Reset GPU device if needed
if (accelerator.resetGPUDevice()) {
    std::cout << "GPU device reset successful" << std::endl;
}
```

## Configuration Files

The GPUAccelerator uses the existing `gpu.json` configuration file:

```json
{
  "global": {
    "enabled": true,
    "deviceId": 0,
    "memoryLimitMB": 4096,
    "enableMemoryPool": true,
    "memoryPoolSizeMB": 1024,
    "enableProfiling": false
  },
  "models": {
    "marian": {
      "useGPU": true,
      "deviceId": 0,
      "batchSize": 8,
      "enableQuantization": true,
      "precision": "fp16"
    }
  }
}
```

## Performance Considerations

### Memory Management
- Use memory pools for frequent allocations
- Enable defragmentation for long-running processes
- Monitor memory usage to prevent OOM errors
- Implement LRU caching for model management

### Batch Processing
- Use optimal batch sizes for your GPU
- Balance latency vs throughput requirements
- Consider memory constraints when setting batch sizes

### Concurrent Processing
- Use multiple CUDA streams for parallel processing
- Balance stream count with available GPU resources
- Monitor stream utilization for optimization

### Model Quantization
- Use FP16 for better performance on modern GPUs
- Consider INT8 for memory-constrained environments
- Validate accuracy after quantization

## Error Handling

### Common Error Scenarios
1. **GPU Not Available**: No compatible GPU detected
2. **Memory Allocation Failed**: Insufficient GPU memory
3. **Model Loading Failed**: Invalid model file or format
4. **CUDA Context Error**: CUDA driver or runtime issues
5. **Translation Timeout**: Model inference taking too long

### Recovery Strategies
1. **Automatic Recovery**: Reset device and reinitialize
2. **CPU Fallback**: Switch to CPU processing
3. **Memory Optimization**: Free unused models and defragment
4. **Device Reset**: Full GPU device reset
5. **Graceful Degradation**: Reduce batch sizes or disable features

## Integration with MarianTranslator

The GPUAccelerator is designed to integrate seamlessly with the MarianTranslator:

```cpp
// In MarianTranslator
class MarianTranslator {
private:
    std::unique_ptr<GPUAccelerator> gpuAccelerator_;
    
public:
    bool initializeWithGPU(const std::string& sourceLang, 
                          const std::string& targetLang, 
                          int gpuDeviceId = 0) override {
        if (!gpuAccelerator_) {
            gpuAccelerator_ = std::make_unique<GPUAccelerator>();
        }
        
        if (!gpuAccelerator_->initialize()) {
            return false;
        }
        
        if (gpuDeviceId >= 0) {
            return gpuAccelerator_->selectGPU(gpuDeviceId);
        }
        
        return gpuAccelerator_->isGPUAvailable();
    }
    
    TranslationResult translate(const std::string& text) override {
        if (gpuAccelerator_ && gpuAccelerator_->isGPUAvailable()) {
            // Use GPU acceleration
            void* gpuModel = gpuAccelerator_->getGPUModelPointer(getLanguagePairKey());
            if (gpuModel) {
                std::string output;
                if (gpuAccelerator_->accelerateTranslation(gpuModel, text, output)) {
                    return createSuccessResult(output);
                }
            }
        }
        
        // Fallback to CPU translation
        return performCPUTranslation(text);
    }
};
```

## Testing

Run the unit tests to verify GPUAccelerator functionality:

```bash
cd backend/build
make test_gpu_accelerator
./tests/unit/test_gpu_accelerator
```

Run the example to see GPUAccelerator in action:

```bash
cd backend/build
make GPUAcceleratorExample
./bin/GPUAcceleratorExample
```

## Dependencies

- CUDA Toolkit 11.0+
- cuBLAS (for matrix operations)
- cuDNN (optional, for optimized operations)
- Existing GPU utilities (GPUManager, GPUMemoryPool)
- Marian NMT library

## Limitations

1. **CUDA Only**: Currently supports only NVIDIA GPUs with CUDA
2. **Model Format**: Supports only Marian NMT model format (.npz)
3. **Memory Constraints**: Limited by available GPU memory
4. **Driver Dependencies**: Requires compatible CUDA drivers

## Future Enhancements

1. **Multi-GPU Support**: Distribute models across multiple GPUs
2. **Dynamic Load Balancing**: Automatically balance load across devices
3. **Model Compression**: Advanced quantization and pruning techniques
4. **ROCm Support**: Support for AMD GPUs
5. **Tensor Parallelism**: Split large models across multiple GPUs
6. **Persistent Kernels**: Reduce kernel launch overhead
7. **Graph Optimization**: Use CUDA graphs for better performance