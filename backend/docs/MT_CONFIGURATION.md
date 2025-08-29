# Machine Translation Configuration System

This document describes the comprehensive configuration system for the Machine Translation (MT) backend component of SpeechRNT.

## Overview

The MT configuration system provides flexible, runtime-configurable settings for all aspects of machine translation, including:

- GPU acceleration settings
- Translation quality assessment
- Caching and performance optimization
- Batch and streaming processing
- Error handling and recovery
- Language detection
- Model management
- Environment-specific overrides

## Architecture

### Core Components

1. **MTConfig**: Main configuration class containing all MT settings
2. **MTConfigManager**: Singleton manager for global configuration access
3. **MTConfigLoader**: Utility for loading and processing configurations
4. **MTConfigTuner**: Auto-tuning utilities for system optimization

### Configuration Sections

#### GPU Configuration (`gpu`)
Controls GPU acceleration settings:
```json
{
  "gpu": {
    "enabled": true,
    "fallbackToCPU": true,
    "defaultDeviceId": 0,
    "memoryPoolSizeMB": 1024,
    "maxModelMemoryMB": 2048,
    "memoryReservationRatio": 0.8,
    "allowedDeviceIds": [0, 1]
  }
}
```

#### Quality Configuration (`quality`)
Controls translation quality assessment:
```json
{
  "quality": {
    "enabled": true,
    "highQualityThreshold": 0.8,
    "mediumQualityThreshold": 0.6,
    "lowQualityThreshold": 0.4,
    "generateAlternatives": true,
    "maxAlternatives": 3,
    "enableFallbackTranslation": true
  }
}
```

#### Caching Configuration (`caching`)
Controls translation result caching:
```json
{
  "caching": {
    "enabled": true,
    "maxCacheSize": 1000,
    "cacheExpirationTimeMinutes": 60,
    "persistToDisk": false,
    "cacheDirectory": "cache/translations"
  }
}
```

#### Batch Configuration (`batch`)
Controls batch processing settings:
```json
{
  "batch": {
    "maxBatchSize": 32,
    "batchTimeoutMs": 5000,
    "enableBatchOptimization": true,
    "optimalBatchSize": 8
  }
}
```

#### Streaming Configuration (`streaming`)
Controls streaming translation settings:
```json
{
  "streaming": {
    "enabled": true,
    "sessionTimeoutMinutes": 30,
    "maxConcurrentSessions": 100,
    "maxContextLength": 1000,
    "enableContextPreservation": true
  }
}
```

#### Error Handling Configuration (`errorHandling`)
Controls error handling and recovery:
```json
{
  "errorHandling": {
    "enableRetry": true,
    "maxRetryAttempts": 3,
    "initialRetryDelayMs": 100,
    "retryBackoffMultiplier": 2.0,
    "maxRetryDelayMs": 10000,
    "translationTimeoutMs": 5000,
    "enableDegradedMode": true,
    "enableFallbackTranslation": true
  }
}
```

#### Performance Configuration (`performance`)
Controls performance monitoring:
```json
{
  "performance": {
    "enabled": true,
    "metricsCollectionIntervalSeconds": 30,
    "enableLatencyTracking": true,
    "enableThroughputTracking": true,
    "enableResourceUsageTracking": true,
    "maxMetricsHistorySize": 1000
  }
}
```

#### Language Detection Configuration (`languageDetection`)
Controls automatic language detection:
```json
{
  "languageDetection": {
    "enabled": true,
    "confidenceThreshold": 0.7,
    "detectionMethod": "hybrid",
    "enableHybridDetection": true,
    "hybridWeightText": 0.6,
    "hybridWeightAudio": 0.4,
    "supportedLanguages": ["en", "es", "fr", "de", "it", "pt"],
    "fallbackLanguages": {
      "unknown": "en",
      "auto": "en"
    }
  }
}
```

#### Model Configuration (`models`)
Defines language pair models:
```json
{
  "models": {
    "en->es": {
      "modelPath": "data/marian/en-es/model.npz",
      "vocabPath": "data/marian/en-es/vocab.yml",
      "configPath": "data/marian/en-es/config.yml",
      "modelType": "transformer",
      "domain": "general",
      "accuracy": 0.85,
      "estimatedSizeMB": 180,
      "quantized": false,
      "quantizationType": ""
    }
  }
}
```

## Usage

### Basic Configuration Loading

```cpp
#include "mt/mt_config.hpp"
#include "mt/mt_config_loader.hpp"

// Initialize configuration manager
auto& configManager = MTConfigManager::getInstance();
if (!configManager.initialize("config/mt.json")) {
    // Handle error
}

// Get current configuration
auto config = configManager.getConfig();
```

### Runtime Configuration Updates

```cpp
// Update configuration at runtime
std::string configUpdate = R"({
    "gpu": {
        "memoryPoolSizeMB": 2048
    },
    "batch": {
        "maxBatchSize": 64
    }
})";

if (configManager.updateConfig(configUpdate)) {
    // Configuration updated successfully
}
```

### Environment-Specific Configuration

```cpp
// Load configuration for specific environment
auto config = MTConfigLoader::loadConfiguration("config/mt.json", "production");

// Set environment and apply overrides
configManager.setEnvironment("production");
```

### Custom Model Paths

```cpp
auto config = std::make_shared<MTConfig>();

// Set custom model paths
config->setCustomModelPath("en", "es", "/custom/models/en-es-medical");
config->setCustomModelPath("es", "en", "/custom/models/es-en-medical");

// Check if custom path exists
if (config->hasCustomModelPath("en", "es")) {
    std::string modelPath = config->getModelPath("en", "es");
}
```

### Configuration Tuning

```cpp
auto config = MTConfigLoader::createDefaultConfiguration("development");

// Apply tuning parameters
std::unordered_map<std::string, std::string> tuningParams = {
    {"gpu.memoryPoolSizeMB", "4096"},
    {"batch.maxBatchSize", "128"},
    {"quality.highQualityThreshold", "0.9"}
};

MTConfigLoader::applyTuningParameters(*config, tuningParams);
```

### Auto-Tuning for System Resources

```cpp
auto config = MTConfigLoader::createDefaultConfiguration("development");

// Auto-tune based on system resources
size_t availableGPUMemoryMB = 8192;
size_t availableRAMMB = 32768;
int cpuCores = 16;

MTConfigTuner::autoTuneForSystem(*config, availableGPUMemoryMB, availableRAMMB, cpuCores);
```

### Use Case-Specific Tuning

```cpp
auto config = MTConfigLoader::createDefaultConfiguration("development");

// Tune for specific use cases
MTConfigTuner::tuneForUseCase(*config, "realtime");  // Low latency
MTConfigTuner::tuneForUseCase(*config, "batch");     // High throughput
MTConfigTuner::tuneForUseCase(*config, "quality");   // High quality
```

### Translator Integration

```cpp
#include "mt/marian_translator.hpp"

// Create translator with configuration
auto config = MTConfigLoader::loadConfiguration("config/mt.json", "production");
MarianTranslator translator(config);

// Update translator configuration at runtime
auto newConfig = std::make_shared<MTConfig>(*config);
// Modify newConfig...
translator.updateConfiguration(newConfig);
```

## Environment-Specific Overrides

The system supports environment-specific configuration overrides:

### File Structure
```
config/
├── mt.json                 # Base configuration
├── mt_development.json     # Development overrides
├── mt_production.json      # Production overrides
├── mt_testing.json         # Testing overrides
└── mt_staging.json         # Staging overrides
```

### Override Example
Base configuration (`mt.json`):
```json
{
  "gpu": {
    "enabled": true,
    "memoryPoolSizeMB": 1024
  }
}
```

Production override (`mt_production.json`):
```json
{
  "gpu": {
    "memoryPoolSizeMB": 4096,
    "fallbackToCPU": false
  }
}
```

## Configuration Templates

Pre-defined templates for common scenarios:

### Development Template
- GPU enabled with CPU fallback
- All features enabled for testing
- Moderate resource usage
- Comprehensive error handling

### Production Template
- Optimized for performance
- High resource allocation
- Strict error handling
- Monitoring enabled

### Testing Template
- CPU-only processing
- Minimal features enabled
- Fast execution
- Simple error handling

### High Performance Template
- Maximum GPU utilization
- Large batch sizes
- Aggressive caching
- Minimal error recovery

### Low Resource Template
- CPU-only processing
- Small batch sizes
- Minimal caching
- Conservative settings

## Configuration Validation

The system includes comprehensive validation:

```cpp
auto config = MTConfigLoader::loadConfiguration("config/mt.json");

// Validate configuration
if (!config->validate()) {
    auto errors = config->getValidationErrors();
    for (const auto& error : errors) {
        std::cout << "Validation error: " << error << std::endl;
    }
}
```

### Common Validation Rules
- GPU memory settings must be positive and within reasonable bounds
- Quality thresholds must be in ascending order (low < medium < high)
- Batch sizes must be positive
- Timeout values must be positive
- Language codes must be valid ISO 639-1 codes
- File paths must be accessible

## Performance Considerations

### Memory Usage
- GPU memory pool size affects model loading capacity
- Cache size impacts RAM usage
- Batch size affects memory per operation

### Latency Optimization
- Reduce batch size for lower latency
- Disable quality assessment for faster processing
- Use GPU acceleration when available
- Minimize retry attempts

### Throughput Optimization
- Increase batch size for higher throughput
- Enable batch optimization
- Use larger GPU memory pools
- Enable aggressive caching

## Monitoring and Observability

### Configuration Change Notifications
```cpp
config->registerConfigChangeCallback("my_callback", 
    [](const std::string& section, const std::string& key) {
        std::cout << "Configuration changed: " << section << "." << key << std::endl;
    });
```

### Configuration File Watching
```cpp
auto& configManager = MTConfigManager::getInstance();
configManager.enableConfigFileWatching(true);
```

### Performance Metrics
When performance monitoring is enabled, the system tracks:
- Translation latency and throughput
- GPU utilization and memory usage
- Cache hit rates
- Error rates and recovery times

## Best Practices

### Configuration Management
1. Use environment-specific overrides for deployment differences
2. Validate configurations before deployment
3. Monitor configuration changes in production
4. Use auto-tuning for initial system setup
5. Document custom configuration parameters

### Performance Tuning
1. Start with appropriate template for your use case
2. Use auto-tuning based on actual system resources
3. Monitor performance metrics to identify bottlenecks
4. Adjust batch sizes based on workload patterns
5. Balance quality vs. performance based on requirements

### Error Handling
1. Enable degraded mode for production systems
2. Configure appropriate retry policies
3. Set reasonable timeouts for your use case
4. Enable fallback translation for critical systems
5. Monitor error rates and adjust thresholds

### Security Considerations
1. Validate all configuration inputs
2. Use secure file permissions for configuration files
3. Avoid storing sensitive information in configuration files
4. Audit configuration changes in production
5. Use environment variables for sensitive settings

## Troubleshooting

### Common Issues

#### Configuration Not Loading
- Check file permissions and paths
- Validate JSON syntax
- Verify environment-specific override files exist
- Check for circular dependencies in configuration

#### GPU Initialization Failures
- Verify CUDA installation and drivers
- Check GPU memory availability
- Validate GPU device IDs
- Enable CPU fallback for testing

#### Performance Issues
- Monitor GPU memory usage
- Check batch size settings
- Verify model loading times
- Review cache hit rates

#### Translation Quality Issues
- Adjust quality thresholds
- Enable alternative generation
- Check model accuracy settings
- Verify language pair configurations

### Debug Mode
Enable debug logging to troubleshoot configuration issues:
```cpp
// Enable debug logging for configuration
utils::Logger::setLevel(utils::LogLevel::DEBUG);
```

### Configuration Dump
Export current configuration for debugging:
```cpp
auto config = configManager.getConfig();
std::string configJson = config->toJson();
std::cout << "Current configuration:\n" << configJson << std::endl;
```

## API Reference

### MTConfig Class
- `loadFromFile(path)`: Load configuration from JSON file
- `saveToFile(path)`: Save configuration to JSON file
- `updateConfiguration(json)`: Update configuration at runtime
- `validate()`: Validate configuration settings
- `getValidationErrors()`: Get validation error messages

### MTConfigManager Class
- `getInstance()`: Get singleton instance
- `initialize(path)`: Initialize with configuration file
- `getConfig()`: Get current configuration
- `updateConfig(json)`: Update configuration at runtime
- `setEnvironment(env)`: Set current environment

### MTConfigLoader Class
- `loadConfiguration(path, env)`: Load configuration with environment overrides
- `createDefaultConfiguration(env)`: Create default configuration
- `getConfigurationTemplates()`: Get available templates
- `applyTuningParameters(config, params)`: Apply tuning parameters

### MTConfigTuner Class
- `autoTuneForSystem(config, gpu, ram, cores)`: Auto-tune for system resources
- `tuneForPerformance(config, latency, throughput, memory)`: Tune for performance targets
- `tuneForUseCase(config, useCase)`: Tune for specific use case

## Examples

See `backend/examples/mt_config_example.cpp` for comprehensive usage examples demonstrating all features of the configuration system.

## Testing

Unit tests are available in `backend/tests/unit/test_mt_config.cpp` covering:
- Configuration loading and saving
- Runtime updates
- Validation
- Environment overrides
- Auto-tuning
- Template usage