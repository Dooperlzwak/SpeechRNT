# Machine Translation Configuration Guide

## Overview

This guide covers the configuration of the Machine Translation (MT) backend components, including model settings, GPU acceleration, language detection, and quality assessment.

## Configuration Files

### 1. models.json - Model Configuration

Located at `backend/config/models.json`, this file configures Marian NMT models and translation settings.

#### Key Sections:

**Model Configurations**
```json
"modelConfigurations": {
  "transformer-base": {
    "type": "transformer",
    "layers": 6,
    "heads": 8,
    "embedSize": 512,
    "beamSize": 4,
    "lengthPenalty": 0.6
  }
}
```

**Model Paths**
```json
"modelPaths": {
  "en-es": "data/marian/en-es/model.npz",
  "es-en": "data/marian/es-en/model.npz"
}
```

**Translation Settings**
```json
"translation": {
  "maxInputLength": 1000,
  "timeoutMs": 10000,
  "confidenceThreshold": 0.5,
  "enableAlternatives": true,
  "maxAlternatives": 3
}
```

### 2. gpu.json - GPU Configuration

Located at `backend/config/gpu.json`, this file configures GPU acceleration settings.

#### Marian GPU Settings:
```json
"marian": {
  "useGPU": true,
  "deviceId": 0,
  "memoryPoolSizeMB": 1024,
  "maxConcurrentModels": 3,
  "fallback": {
    "enableCPUFallback": true,
    "fallbackThresholdMs": 5000
  }
}
```

### 3. language_detection.json - Language Detection

Located at `backend/config/language_detection.json`, this file configures automatic language detection.

#### Detection Methods:
```json
"detection_methods": {
  "hybrid": {
    "enabled": true,
    "text_weight": 0.6,
    "audio_weight": 0.4
  }
}
```

#### Automatic Language Switching:
```json
"automatic_language_switching": {
  "enabled": true,
  "confidence_threshold": 0.85,
  "notify_clients": true,
  "min_switch_interval_ms": 5000
}
```

### 4. quality_assessment.json - Quality Assessment

Located at `backend/config/quality_assessment.json`, this file configures translation quality assessment.

#### Quality Thresholds:
```json
"quality_thresholds": {
  "high": 0.8,
  "medium": 0.6,
  "low": 0.4,
  "unacceptable": 0.2
}
```

#### Quality Metrics:
```json
"quality_metrics": {
  "fluency": {
    "weight": 0.3,
    "algorithms": ["perplexity", "grammar_check"]
  },
  "adequacy": {
    "weight": 0.4,
    "algorithms": ["semantic_similarity", "content_preservation"]
  }
}
```

## Configuration Best Practices

### Performance Optimization

1. **GPU Memory Management**
   - Set `memoryPoolSizeMB` based on available GPU memory
   - Enable `enableMemoryPool` for efficient allocation
   - Use `maxConcurrentModels` to limit memory usage

2. **Model Caching**
   - Enable `lruEviction` for automatic model management
   - Set `memoryThresholdMB` based on system RAM
   - Use `preloadModels` for frequently used language pairs

3. **Translation Settings**
   - Adjust `timeoutMs` based on expected text length
   - Set `confidenceThreshold` based on quality requirements
   - Enable `enableAlternatives` for better user experience

### Quality Configuration

1. **Quality Thresholds**
   - Set thresholds based on use case requirements
   - Higher thresholds for critical applications
   - Lower thresholds for casual conversation

2. **Alternative Generation**
   - Enable multiple generation methods for diversity
   - Adjust `max_alternatives` based on UI constraints
   - Use back-translation for quality validation

### Language Detection

1. **Detection Method Selection**
   - Use "hybrid" for best accuracy
   - Use "text_analysis" for text-only scenarios
   - Use "audio_analysis" for speech-heavy applications

2. **Confidence Tuning**
   - Higher thresholds for more reliable detection
   - Lower thresholds for broader language support
   - Enable fallback languages for unsupported variants

## Environment-Specific Configuration

### Development Environment
```json
{
  "translation": {
    "timeoutMs": 30000,
    "enableAlternatives": true
  },
  "performance": {
    "enableProfiling": true,
    "logTranslationTimes": true
  }
}
```

### Production Environment
```json
{
  "translation": {
    "timeoutMs": 10000,
    "enableAlternatives": false
  },
  "performance": {
    "enableProfiling": false,
    "enableMetrics": true
  }
}
```

### High-Performance Environment
```json
{
  "gpuAcceleration": {
    "enabled": true,
    "batchSize": 8,
    "precision": "fp16"
  },
  "modelCaching": {
    "preloadModels": ["en-es", "es-en", "en-fr", "fr-en"],
    "memoryThresholdMB": 8192
  }
}
```

## Troubleshooting Configuration Issues

### Common Problems

1. **GPU Initialization Failure**
   - Check CUDA installation and version
   - Verify GPU memory availability
   - Enable CPU fallback as backup

2. **Model Loading Errors**
   - Verify model file paths
   - Check file permissions
   - Validate model file integrity

3. **Performance Issues**
   - Adjust batch sizes for GPU memory
   - Enable model quantization
   - Optimize memory pool settings

### Configuration Validation

Use the following commands to validate configuration:

```bash
# Validate JSON syntax
python -m json.tool backend/config/models.json

# Test GPU configuration
./backend/build/test_gpu_config

# Validate model paths
./backend/build/test_model_loading
```

## Configuration Updates

### Runtime Configuration Updates

Some settings can be updated at runtime without restarting the service:

- Quality thresholds
- Language detection confidence
- Translation timeouts
- GPU memory settings (with restart)

### Hot-Swapping Models

Enable hot-swapping for seamless model updates:

```json
"modelCaching": {
  "hotSwapping": {
    "enabled": true,
    "gracefulShutdown": true,
    "maxSwapTimeMs": 5000
  }
}
```

## Monitoring Configuration

### Performance Metrics

Enable metrics collection for monitoring:

```json
"performance": {
  "enableMetrics": true,
  "metricsIntervalMs": 30000,
  "logTranslationTimes": true
}
```

### Logging Configuration

Configure detailed logging for debugging:

```json
"logging": {
  "log_detections": true,
  "log_quality_scores": true,
  "detailed_logging": false
}
```

## Security Considerations

### Model File Security

- Store models in secure directories with appropriate permissions
- Validate model file integrity on loading
- Use secure channels for model downloads

### Configuration Security

- Protect configuration files from unauthorized access
- Use environment variables for sensitive settings
- Implement configuration validation and sanitization

## Support

For configuration support:
1. Check the troubleshooting section
2. Validate JSON syntax
3. Review log files for errors
4. Test individual components
5. Consult the API documentation for parameter details