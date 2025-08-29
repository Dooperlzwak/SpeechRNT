# STT Configuration Management

This document describes the STT Configuration Management system implemented for the SpeechRNT backend. The system provides comprehensive configuration management for Speech-to-Text settings with runtime updates, validation, and frontend-backend synchronization.

## Overview

The STT Configuration Management system consists of several key components:

1. **STTConfigManager** - Core configuration management with file I/O and validation
2. **STTConfigHandler** - WebSocket message handler for frontend-backend communication
3. **Configuration Files** - JSON-based configuration storage
4. **Frontend Integration** - TypeScript interfaces and React components

## Architecture

### Backend Components

#### STTConfigManager (`backend/include/stt/stt_config.hpp`)

The main configuration manager that handles:
- Loading/saving configuration from/to JSON files
- Runtime configuration updates with validation
- Configuration change notifications
- Schema and metadata generation
- Model and quantization level discovery

Key features:
- Thread-safe configuration access
- Automatic validation on updates
- Change notification callbacks
- Auto-save functionality
- Configuration rollback on validation failure

#### STTConfigHandler (`backend/include/core/stt_config_handler.hpp`)

WebSocket message handler that provides:
- Real-time configuration synchronization with frontend
- Message-based configuration API
- Statistics and monitoring
- Error handling and recovery

Supported message types:
- `GET_CONFIG` - Retrieve current configuration
- `UPDATE_CONFIG` - Update entire configuration
- `UPDATE_CONFIG_VALUE` - Update specific configuration value
- `CONFIG_CHANGED` - Configuration change notification
- `GET_SCHEMA` - Get configuration JSON schema
- `GET_METADATA` - Get configuration metadata
- `VALIDATE_CONFIG` - Validate configuration
- `RESET_CONFIG` - Reset to default configuration
- `GET_AVAILABLE_MODELS` - Get available Whisper models
- `GET_SUPPORTED_QUANTIZATION_LEVELS` - Get supported quantization levels

### Configuration Structure

The STT configuration is organized into logical sections:

```cpp
struct STTConfig {
    // Model configuration
    std::string defaultModel = "base";
    std::string modelsPath = "data/whisper/";
    std::string language = "auto";
    bool translateToEnglish = false;
    
    // Language detection settings
    bool languageDetectionEnabled = true;
    float languageDetectionThreshold = 0.7f;
    bool autoLanguageSwitching = true;
    int consistentDetectionRequired = 2;
    
    // Quantization settings
    QuantizationLevel quantizationLevel = QuantizationLevel::AUTO;
    bool enableGPUAcceleration = true;
    int gpuDeviceId = 0;
    float accuracyThreshold = 0.85f;
    
    // Streaming configuration
    bool partialResultsEnabled = true;
    int minChunkSizeMs = 1000;
    int maxChunkSizeMs = 10000;
    int overlapSizeMs = 200;
    bool enableIncrementalUpdates = true;
    
    // Confidence and quality settings
    float confidenceThreshold = 0.5f;
    bool wordLevelConfidenceEnabled = true;
    bool qualityIndicatorsEnabled = true;
    bool confidenceFilteringEnabled = false;
    
    // Performance settings
    int threadCount = 4;
    float temperature = 0.0f;
    int maxTokens = 0;
    
    // Audio processing settings
    int sampleRate = 16000;
    int audioBufferSizeMB = 8;
    bool enableNoiseReduction = false;
    float vadThreshold = 0.5f;
    
    // Error recovery settings
    bool enableErrorRecovery = true;
    int maxRetryAttempts = 3;
    float retryBackoffMultiplier = 2.0f;
    int retryInitialDelayMs = 100;
    
    // Health monitoring settings
    bool enableHealthMonitoring = true;
    int healthCheckIntervalMs = 30000;
    float maxLatencyMs = 2000.0f;
    float maxMemoryUsageMB = 4096.0f;
};
```

## Usage Examples

### Basic Configuration Management

```cpp
#include "stt/stt_config.hpp"

// Create configuration manager
stt::STTConfigManager configManager;

// Load configuration from file
if (configManager.loadFromFile("config/stt.json")) {
    std::cout << "Configuration loaded successfully" << std::endl;
}

// Get current configuration
stt::STTConfig config = configManager.getConfig();
std::cout << "Current model: " << config.defaultModel << std::endl;

// Update a configuration value
auto result = configManager.updateConfigValue("model", "defaultModel", "large");
if (result.isValid) {
    std::cout << "Configuration updated successfully" << std::endl;
} else {
    for (const auto& error : result.errors) {
        std::cerr << "Error: " << error << std::endl;
    }
}

// Register for change notifications
configManager.registerChangeCallback(
    [](const stt::ConfigChangeNotification& notification) {
        std::cout << "Configuration changed: " << notification.section 
                  << "." << notification.key 
                  << " = " << notification.newValue << std::endl;
    }
);

// Save configuration
configManager.saveToFile("config/stt.json");
```

### WebSocket Handler Integration

```cpp
#include "core/stt_config_handler.hpp"

// Create configuration handler
core::STTConfigHandler configHandler;

// Set up message sender (WebSocket)
auto messageSender = [](const std::string& message) {
    // Send message to frontend via WebSocket
    webSocketConnection->send(message);
};

// Initialize handler
if (configHandler.initialize("config/stt.json", messageSender)) {
    std::cout << "Configuration handler initialized" << std::endl;
}

// Handle incoming WebSocket messages
void onWebSocketMessage(const std::string& message) {
    configHandler.handleMessage(message);
}

// Register for configuration changes
configHandler.registerConfigChangeCallback(
    [](const stt::ConfigChangeNotification& notification) {
        // Handle configuration change
        updateSTTSystem(notification.config);
    }
);
```

### Frontend Integration

```typescript
import { sttConfigService } from '../services/sttConfigService';

// Initialize service
await sttConfigService.initialize('ws://localhost:8080');

// Get current configuration
const config = await sttConfigService.getConfig();
console.log('Current model:', config.model.defaultModel);

// Update configuration value
const result = await sttConfigService.updateConfigValue(
    'model', 
    'defaultModel', 
    'large'
);

if (result.isValid) {
    console.log('Configuration updated successfully');
} else {
    console.error('Validation errors:', result.errors);
}

// Listen for configuration changes
const unsubscribe = sttConfigService.onConfigChange((notification) => {
    console.log('Configuration changed:', notification);
    // Update UI with new configuration
});

// Get available models
const models = await sttConfigService.getAvailableModels();
console.log('Available models:', models);
```

## Configuration Validation

The system provides comprehensive validation for all configuration values:

### Model Configuration Validation
- Validates model names against available models
- Checks model file existence
- Validates language codes
- Ensures models path exists

### Streaming Configuration Validation
- Validates chunk size ranges (min: 100ms, max: 30000ms)
- Ensures max chunk size > min chunk size
- Validates overlap size < min chunk size
- Checks buffer size limits

### Performance Configuration Validation
- Validates thread count (min: 1, max: 2x CPU cores)
- Ensures temperature range (0.0 - 1.0)
- Validates token limits
- Checks memory usage limits

### Quantization Configuration Validation
- Validates quantization levels against hardware support
- Checks GPU device availability
- Validates accuracy thresholds
- Ensures GPU memory requirements

## Configuration File Format

The configuration is stored in JSON format:

```json
{
  "model": {
    "defaultModel": "base",
    "modelsPath": "data/whisper/",
    "language": "auto",
    "translateToEnglish": false
  },
  "languageDetection": {
    "enabled": true,
    "threshold": 0.7,
    "autoSwitching": true,
    "consistentDetectionRequired": 2
  },
  "quantization": {
    "level": "AUTO",
    "enableGPUAcceleration": true,
    "gpuDeviceId": 0,
    "accuracyThreshold": 0.85
  },
  "streaming": {
    "partialResultsEnabled": true,
    "minChunkSizeMs": 1000,
    "maxChunkSizeMs": 10000,
    "overlapSizeMs": 200,
    "enableIncrementalUpdates": true
  },
  "confidence": {
    "threshold": 0.5,
    "wordLevelEnabled": true,
    "qualityIndicatorsEnabled": true,
    "filteringEnabled": false
  },
  "performance": {
    "threadCount": 4,
    "temperature": 0.0,
    "maxTokens": 0
  }
}
```

## Error Handling

The system provides robust error handling:

### Configuration Loading Errors
- Missing configuration file → Use defaults
- Invalid JSON format → Return error with details
- Validation failures → Return specific error messages

### Runtime Update Errors
- Invalid values → Rollback to previous configuration
- Validation failures → Return detailed error information
- Network errors → Queue updates for retry

### WebSocket Communication Errors
- Connection failures → Attempt reconnection
- Message parsing errors → Log and ignore
- Timeout errors → Return timeout error to frontend

## Performance Considerations

### Memory Usage
- Configuration objects are lightweight (< 1KB)
- JSON parsing is optimized for small configurations
- Change notifications use minimal memory

### Thread Safety
- All configuration access is thread-safe
- Mutex protection for concurrent updates
- Lock-free reads where possible

### Network Efficiency
- Only changed values are transmitted
- JSON compression for large configurations
- Batched updates to reduce message frequency

## Testing

The system includes comprehensive tests:

### Unit Tests (`backend/tests/unit/stt_config_test.cpp`)
- Configuration loading/saving
- Validation logic
- Change notifications
- Error handling
- Thread safety

### Integration Tests (`backend/tests/integration/stt_config_handler_test.cpp`)
- WebSocket message handling
- Frontend-backend synchronization
- Performance testing
- Concurrent access testing

### Running Tests

```bash
cd backend/build
make test
./tests/unit/stt_config_test
./tests/integration/stt_config_handler_test
```

## Troubleshooting

### Common Issues

1. **Configuration file not found**
   - Check file path and permissions
   - System will use defaults if file missing

2. **Validation errors**
   - Check value ranges and types
   - Refer to configuration schema

3. **WebSocket connection issues**
   - Verify server is running on correct port
   - Check firewall settings

4. **Model loading failures**
   - Verify model files exist in specified path
   - Check file permissions and format

### Debug Logging

Enable debug logging to troubleshoot issues:

```cpp
// Enable detailed logging
configManager.setLogLevel(LogLevel::DEBUG);

// Check configuration status
std::cout << "Configuration modified: " << configManager.isModified() << std::endl;
std::cout << "Last modified: " << configManager.getLastModified() << std::endl;
```

## Future Enhancements

Planned improvements include:

1. **Configuration Profiles** - Multiple named configuration profiles
2. **Remote Configuration** - Load configuration from remote servers
3. **Configuration History** - Track and rollback configuration changes
4. **Advanced Validation** - Custom validation rules and constraints
5. **Configuration Templates** - Predefined configuration templates
6. **Hot Reloading** - Automatic configuration reloading on file changes

## API Reference

For detailed API documentation, see:
- `backend/include/stt/stt_config.hpp` - Core configuration management
- `backend/include/core/stt_config_handler.hpp` - WebSocket handler
- `frontend/src/types/sttConfig.ts` - TypeScript interfaces
- `frontend/src/services/sttConfigService.ts` - Frontend service