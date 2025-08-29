# Machine Translation API Documentation

## Overview

The Machine Translation (MT) backend provides production-ready neural machine translation capabilities using Marian NMT, with support for GPU acceleration, language detection, quality assessment, and advanced translation features.

## Core Components

### MarianTranslator

The main translation engine that provides actual Marian NMT integration.

#### Interface Methods

##### Basic Translation
```cpp
TranslationResult translate(const std::string& text, const std::string& sourceLang, const std::string& targetLang) override;
```
- **Purpose**: Translate text from source language to target language
- **Parameters**:
  - `text`: Input text to translate (max 1000 characters)
  - `sourceLang`: Source language code (ISO 639-1)
  - `targetLang`: Target language code (ISO 639-1)
- **Returns**: `TranslationResult` with translated text, confidence score, and metadata
- **Throws**: `MTError` on translation failure

##### GPU Acceleration
```cpp
bool initializeWithGPU(const std::string& sourceLang, const std::string& targetLang, int gpuDeviceId = 0) override;
void setGPUAcceleration(bool enabled, int deviceId = 0) override;
bool isGPUAccelerationEnabled() const;
```
- **Purpose**: Enable and manage GPU acceleration for translation
- **GPU Requirements**: NVIDIA GPU with CUDA 11.0+, 8GB+ VRAM recommended
- **Fallback**: Automatic CPU fallback on GPU initialization failure

##### Batch Processing
```cpp
std::vector<TranslationResult> translateBatch(const std::vector<std::string>& texts);
std::future<std::vector<TranslationResult>> translateBatchAsync(const std::vector<std::string>& texts);
```
- **Purpose**: Process multiple translations efficiently
- **Batch Size**: Configurable, default 32 texts per batch
- **Performance**: 3-5x faster than individual translations for large batches

##### Streaming Translation
```cpp
void startStreamingTranslation(const std::string& sessionId);
TranslationResult addStreamingText(const std::string& sessionId, const std::string& text);
TranslationResult finalizeStreamingTranslation(const std::string& sessionId);
```
- **Purpose**: Handle incremental translation for real-time scenarios
- **Use Case**: Live speech translation with partial results
- **Context Preservation**: Maintains translation context across chunks

##### Quality and Confidence
```cpp
float calculateTranslationConfidence(const std::string& sourceText, const std::string& translatedText);
std::vector<TranslationResult> getTranslationCandidates(const std::string& text, int maxCandidates = 3);
```
- **Purpose**: Assess translation quality and provide alternatives
- **Confidence Range**: 0.0 (lowest) to 1.0 (highest)
- **Quality Levels**: "high" (≥0.8), "medium" (≥0.6), "low" (≥0.4), "unacceptable" (<0.4)

##### Model Management
```cpp
bool preloadModel(const std::string& sourceLang, const std::string& targetLang);
void setModelQuantization(bool enabled, const std::string& quantizationType = "int8");
bool isModelQuantizationSupported() const;
```
- **Purpose**: Optimize model loading and memory usage
- **Quantization**: Reduces memory usage by 50-75% with minimal quality loss
- **Preloading**: Reduces first-translation latency for frequently used language pairs

### LanguageDetector

Automatic source language detection component.

#### Interface Methods

##### Detection Methods
```cpp
LanguageDetectionResult detectLanguage(const std::string& text);
LanguageDetectionResult detectLanguageFromAudio(const std::vector<float>& audioData);
LanguageDetectionResult detectLanguageHybrid(const std::string& text, const std::vector<float>& audioData);
```
- **Text Detection**: Character frequency, n-gram analysis, dictionary lookup
- **Audio Detection**: Integration with Whisper language detection
- **Hybrid Detection**: Combines text and audio analysis for improved accuracy

##### Configuration
```cpp
void setConfidenceThreshold(float threshold);
void setDetectionMethod(const std::string& method);
void setSupportedLanguages(const std::vector<std::string>& languages);
```
- **Confidence Threshold**: Default 0.8, range 0.0-1.0
- **Detection Methods**: "text_analysis", "audio_analysis", "hybrid"
- **Supported Languages**: 26 languages with fallback mapping

##### Integration
```cpp
void setSTTLanguageDetectionCallback(std::function<LanguageDetectionResult(const std::vector<float>&)> callback);
bool isLanguageSupported(const std::string& languageCode) const;
std::string getFallbackLanguage(const std::string& unsupportedLanguage) const;
```

### GPUAccelerator

GPU acceleration management component.

#### Interface Methods

##### Device Management
```cpp
bool initialize();
std::vector<GPUInfo> getAvailableGPUs() const;
bool selectGPU(int deviceId);
```
- **Auto-Detection**: Automatically discovers compatible NVIDIA GPUs
- **Multi-GPU**: Supports selection from multiple available devices
- **Compatibility**: Validates CUDA version and memory requirements

##### Memory Management
```cpp
bool allocateGPUMemory(size_t sizeMB);
void freeGPUMemory();
size_t getAvailableGPUMemory() const;
```
- **Memory Pool**: Efficient memory allocation with pooling
- **Monitoring**: Real-time memory usage tracking
- **Cleanup**: Automatic memory cleanup on errors

##### Performance Monitoring
```cpp
GPUStats getGPUStatistics() const;
bool isGPUOperational() const;
```
- **Metrics**: Utilization, memory usage, temperature, processing times
- **Health Monitoring**: Continuous operational status checking

### QualityManager

Translation quality assessment and improvement component.

#### Interface Methods

##### Quality Assessment
```cpp
QualityMetrics assessTranslationQuality(
    const std::string& sourceText,
    const std::string& translatedText,
    const std::string& sourceLang,
    const std::string& targetLang
);
```
- **Fluency**: Grammar, readability, perplexity analysis
- **Adequacy**: Semantic similarity, content preservation
- **Consistency**: Style, tone, format consistency

##### Confidence Calculation
```cpp
float calculateConfidenceScore(
    const std::string& sourceText,
    const std::string& translatedText,
    const std::vector<float>& modelScores
);
```
- **Multi-factor**: Model scores, length ratio, diversity, alignment
- **Normalization**: Min-max normalization with configurable bounds

##### Alternative Generation
```cpp
std::vector<std::string> generateAlternatives(
    const std::string& sourceText,
    const std::string& currentTranslation,
    int maxAlternatives = 3
);
```
- **Methods**: Paraphrase, simplified, formal, back-translation
- **Ranking**: Quality-based ranking of alternatives

## Data Models

### TranslationResult

Enhanced translation result structure with comprehensive metadata.

```cpp
struct TranslationResult {
    // Core translation data
    std::string translatedText;
    float confidence;
    std::string sourceLang;
    std::string targetLang;
    bool success;
    std::string errorMessage;
    
    // Quality assessment
    QualityMetrics qualityMetrics;
    std::vector<std::string> alternativeTranslations;
    
    // Performance metadata
    std::chrono::milliseconds processingTime;
    bool usedGPUAcceleration;
    std::string modelVersion;
    
    // Advanced features
    std::vector<float> wordLevelConfidences;
    LanguageDetectionResult sourceLanguageDetection;
    
    // Session management
    int batchIndex;
    std::string sessionId;
    bool isPartialResult;
    bool isStreamingComplete;
};
```

### LanguageDetectionResult

Language detection result with confidence and method information.

```cpp
struct LanguageDetectionResult {
    std::string detectedLanguage;
    float confidence;
    std::vector<std::pair<std::string, float>> languageCandidates;
    bool isReliable;
    std::string detectionMethod;
};
```

### QualityMetrics

Comprehensive quality assessment metrics.

```cpp
struct QualityMetrics {
    float overallConfidence;
    float fluencyScore;
    float adequacyScore;
    float consistencyScore;
    std::vector<float> wordLevelConfidences;
    std::string qualityLevel;
    std::vector<std::string> qualityIssues;
};
```

### GPUInfo

GPU device information and capabilities.

```cpp
struct GPUInfo {
    int deviceId;
    std::string deviceName;
    size_t totalMemoryMB;
    size_t availableMemoryMB;
    bool isCompatible;
    std::string cudaVersion;
};
```

## Configuration

### Model Configuration (models.json)

```json
{
  "marian": {
    "modelsPath": "data/marian/",
    "defaultModel": "transformer-base",
    "modelConfigurations": {
      "transformer-base": {
        "type": "transformer",
        "layers": 6,
        "heads": 8,
        "embedSize": 512,
        "beamSize": 4,
        "lengthPenalty": 0.6
      }
    },
    "translation": {
      "maxInputLength": 1000,
      "timeoutMs": 10000,
      "confidenceThreshold": 0.5,
      "enableAlternatives": true,
      "maxAlternatives": 3
    }
  }
}
```

### GPU Configuration (gpu.json)

```json
{
  "models": {
    "marian": {
      "useGPU": true,
      "deviceId": 0,
      "batchSize": 1,
      "memoryPoolSizeMB": 1024,
      "enableTensorRT": false,
      "fallback": {
        "enableCPUFallback": true,
        "fallbackThresholdMs": 5000
      }
    }
  }
}
```

### Language Detection Configuration (language_detection.json)

```json
{
  "enabled": true,
  "confidence_threshold": 0.8,
  "detection_method": "hybrid",
  "detection_methods": {
    "hybrid": {
      "text_weight": 0.6,
      "audio_weight": 0.4
    }
  },
  "automatic_language_switching": {
    "enabled": true,
    "confidence_threshold": 0.85,
    "notify_clients": true
  }
}
```

### Quality Assessment Configuration (quality_assessment.json)

```json
{
  "quality_thresholds": {
    "high": 0.8,
    "medium": 0.6,
    "low": 0.4
  },
  "quality_metrics": {
    "fluency": {
      "weight": 0.3,
      "algorithms": ["perplexity", "grammar_check"]
    },
    "adequacy": {
      "weight": 0.4,
      "algorithms": ["semantic_similarity", "content_preservation"]
    },
    "consistency": {
      "weight": 0.3,
      "algorithms": ["style_consistency", "tone_consistency"]
    }
  }
}
```

## Error Handling

### Error Codes

```cpp
enum class MTErrorCode {
    SUCCESS = 0,
    MODEL_LOADING_FAILED,
    GPU_INITIALIZATION_FAILED,
    TRANSLATION_TIMEOUT,
    UNSUPPORTED_LANGUAGE_PAIR,
    INSUFFICIENT_MEMORY,
    LANGUAGE_DETECTION_FAILED,
    QUALITY_THRESHOLD_NOT_MET,
    CONFIGURATION_ERROR,
    UNKNOWN_ERROR
};
```

### Error Recovery

- **Model Loading Errors**: Automatic retry with exponential backoff
- **GPU Errors**: Automatic CPU fallback
- **Translation Timeouts**: Configurable retry attempts
- **Memory Errors**: Automatic model unloading and garbage collection

## Performance Characteristics

### Latency Targets

- **Translation Latency**: < 300ms (GPU), < 800ms (CPU)
- **Language Detection**: < 100ms
- **Quality Assessment**: < 200ms
- **Model Loading**: < 2s (cached), < 10s (cold start)

### Throughput

- **Single Translation**: 10-50 translations/second (GPU-dependent)
- **Batch Translation**: 100-500 translations/second
- **Concurrent Sessions**: Up to 100 simultaneous translation sessions

### Memory Usage

- **Base Memory**: 2-4GB per language pair model
- **GPU Memory**: 1-2GB per loaded model
- **Quantized Models**: 50-75% memory reduction
- **Model Cache**: LRU eviction with configurable thresholds

## Integration Examples

### Basic Translation

```cpp
// Initialize translator
MarianTranslator translator;
translator.initialize("en", "es");

// Perform translation
TranslationResult result = translator.translate("Hello world", "en", "es");
if (result.success) {
    std::cout << "Translation: " << result.translatedText << std::endl;
    std::cout << "Confidence: " << result.confidence << std::endl;
}
```

### GPU-Accelerated Translation

```cpp
// Initialize with GPU
MarianTranslator translator;
if (translator.initializeWithGPU("en", "es", 0)) {
    std::cout << "GPU acceleration enabled" << std::endl;
} else {
    std::cout << "Falling back to CPU" << std::endl;
}

// Translation will use GPU if available
TranslationResult result = translator.translate("Hello world", "en", "es");
std::cout << "Used GPU: " << result.usedGPUAcceleration << std::endl;
```

### Language Detection Integration

```cpp
// Initialize language detector
LanguageDetector detector;
detector.initialize();

// Detect language
LanguageDetectionResult detection = detector.detectLanguage("Hola mundo");
if (detection.isReliable) {
    std::cout << "Detected: " << detection.detectedLanguage << std::endl;
    std::cout << "Confidence: " << detection.confidence << std::endl;
    
    // Use detected language for translation
    TranslationResult result = translator.translate("Hola mundo", detection.detectedLanguage, "en");
}
```

### Quality Assessment

```cpp
// Initialize quality manager
QualityManager qualityMgr;
qualityMgr.initialize();

// Assess translation quality
QualityMetrics metrics = qualityMgr.assessTranslationQuality(
    "Hello world", "Hola mundo", "en", "es"
);

std::cout << "Quality Level: " << metrics.qualityLevel << std::endl;
std::cout << "Fluency: " << metrics.fluencyScore << std::endl;
std::cout << "Adequacy: " << metrics.adequacyScore << std::endl;

// Generate alternatives if quality is low
if (metrics.overallConfidence < 0.6) {
    auto alternatives = qualityMgr.generateAlternatives("Hello world", "Hola mundo", 3);
    for (const auto& alt : alternatives) {
        std::cout << "Alternative: " << alt << std::endl;
    }
}
```

### Batch Processing

```cpp
// Prepare batch of texts
std::vector<std::string> texts = {
    "Hello world",
    "How are you?",
    "Good morning",
    "Thank you"
};

// Process batch
std::vector<TranslationResult> results = translator.translateBatch(texts);

// Process results
for (size_t i = 0; i < results.size(); ++i) {
    if (results[i].success) {
        std::cout << texts[i] << " -> " << results[i].translatedText << std::endl;
    }
}
```

### Streaming Translation

```cpp
// Start streaming session
std::string sessionId = "session_123";
translator.startStreamingTranslation(sessionId);

// Add text incrementally
TranslationResult partial1 = translator.addStreamingText(sessionId, "Hello");
TranslationResult partial2 = translator.addStreamingText(sessionId, " world");

// Finalize translation
TranslationResult final = translator.finalizeStreamingTranslation(sessionId);
std::cout << "Final translation: " << final.translatedText << std::endl;
```

## Best Practices

### Performance Optimization

1. **Model Preloading**: Preload frequently used language pairs
2. **Batch Processing**: Use batch translation for multiple texts
3. **GPU Utilization**: Enable GPU acceleration for better performance
4. **Model Quantization**: Use quantized models to reduce memory usage
5. **Caching**: Enable translation result caching for repeated phrases

### Error Handling

1. **Graceful Degradation**: Always provide CPU fallback for GPU errors
2. **Timeout Handling**: Set appropriate timeouts for translation operations
3. **Quality Thresholds**: Define minimum quality thresholds for production use
4. **Retry Logic**: Implement exponential backoff for transient errors

### Memory Management

1. **Model Lifecycle**: Unload unused models to free memory
2. **Memory Monitoring**: Monitor GPU and system memory usage
3. **Garbage Collection**: Enable automatic cleanup of unused resources
4. **Memory Pools**: Use memory pools for efficient allocation

### Quality Assurance

1. **Confidence Scoring**: Always check translation confidence scores
2. **Alternative Generation**: Provide alternatives for low-quality translations
3. **Human Review**: Flag low-confidence translations for human review
4. **Continuous Monitoring**: Track quality metrics over time

## Troubleshooting

### Common Issues

1. **GPU Initialization Failure**
   - Check CUDA installation and version compatibility
   - Verify GPU memory availability
   - Enable CPU fallback as backup

2. **Model Loading Errors**
   - Verify model file paths and permissions
   - Check available disk space and memory
   - Validate model file integrity

3. **Translation Timeouts**
   - Increase timeout values for complex texts
   - Check system resource availability
   - Consider text length limitations

4. **Low Translation Quality**
   - Verify language pair support
   - Check input text quality and encoding
   - Consider using alternative generation

### Debugging

1. **Enable Detailed Logging**: Set log level to DEBUG for detailed information
2. **Performance Profiling**: Enable profiling to identify bottlenecks
3. **Memory Monitoring**: Track memory usage patterns
4. **GPU Monitoring**: Use nvidia-smi to monitor GPU utilization

## Version History

### Version 1.0.0
- Initial release with Marian NMT integration
- GPU acceleration support
- Language detection capabilities
- Quality assessment framework
- Batch and streaming translation support
- Comprehensive configuration system

## Support

For technical support and questions:
- Check the troubleshooting section above
- Review configuration examples
- Consult the error handling documentation
- Enable debug logging for detailed diagnostics