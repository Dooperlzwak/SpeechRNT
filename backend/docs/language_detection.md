# Language Detection and Auto-switching

This document describes the language detection and automatic language switching functionality implemented in the SpeechRNT backend.

## Overview

The language detection system automatically identifies the spoken language in audio input and can optionally switch the transcription language based on the detected language. This enables seamless multilingual conversations without manual language selection.

## Features

### 1. Automatic Language Detection
- Uses Whisper.cpp's built-in language detection capabilities
- Provides confidence scores for detected languages
- Supports all languages supported by the Whisper model

### 2. Auto-switching
- Automatically switches transcription language when a different language is detected
- Configurable confidence threshold to prevent false switches
- Requires consistent detection across multiple transcriptions for stability

### 3. Language Change Notifications
- Notifies clients when language changes are detected
- Includes confidence scores and utterance context
- Supports both streaming and batch transcription modes

### 4. Configuration Options
- Enable/disable language detection
- Set confidence thresholds
- Enable/disable automatic switching
- Configure consistency requirements

## Configuration

### Model Configuration (models.json)

```json
{
  "whisper": {
    "languageDetection": {
      "enabled": true,
      "threshold": 0.7,
      "autoSwitching": true,
      "consistentDetectionRequired": 2,
      "supportedLanguages": ["en", "es", "fr", "de", "it", ...]
    }
  }
}
```

### Runtime Configuration

```cpp
// Enable language detection
whisperSTT->setLanguageDetectionEnabled(true);

// Set confidence threshold (0.0 - 1.0)
whisperSTT->setLanguageDetectionThreshold(0.7f);

// Enable automatic language switching
whisperSTT->setAutoLanguageSwitching(true);

// Set language change callback
whisperSTT->setLanguageChangeCallback([](const std::string& oldLang, 
                                        const std::string& newLang, 
                                        float confidence) {
    std::cout << "Language changed: " << oldLang << " -> " << newLang 
              << " (confidence: " << confidence << ")" << std::endl;
});
```

## API Reference

### WhisperSTT Methods

#### Language Detection Configuration
- `setLanguageDetectionEnabled(bool enabled)` - Enable/disable language detection
- `setLanguageDetectionThreshold(float threshold)` - Set confidence threshold (0.0-1.0)
- `setAutoLanguageSwitching(bool enabled)` - Enable/disable automatic switching
- `setLanguageChangeCallback(LanguageChangeCallback callback)` - Set language change notification callback

#### Status Methods
- `isLanguageDetectionEnabled()` - Check if language detection is enabled
- `isAutoLanguageSwitchingEnabled()` - Check if auto-switching is enabled
- `getCurrentDetectedLanguage()` - Get the currently detected language

### TranscriptionResult Fields

```cpp
struct TranscriptionResult {
    std::string text;                // Transcribed text
    float confidence;                // Transcription confidence
    bool is_partial;                 // Whether this is a partial result
    int64_t start_time_ms;          // Start time in milliseconds
    int64_t end_time_ms;            // End time in milliseconds
    
    // Language detection fields
    std::string detected_language;   // Detected language code (e.g., "en", "es")
    float language_confidence;       // Language detection confidence (0.0-1.0)
    bool language_changed;           // Whether language changed from previous
};
```

### Message Protocol

Language changes are communicated to clients via WebSocket messages:

```json
{
  "type": "language_change",
  "data": {
    "oldLanguage": "en",
    "newLanguage": "es",
    "confidence": 0.85,
    "utteranceId": 12345
  }
}
```

## Implementation Details

### Language Detection Process

1. **Audio Processing**: Audio is processed through Whisper.cpp
2. **Language Detection**: Whisper's `whisper_lang_auto_detect()` function is used to get language probabilities
3. **Confidence Evaluation**: The confidence score is calculated based on language probabilities and speech quality
4. **Consistency Check**: Multiple consistent detections are required before switching
5. **Validation**: Detected language is validated against supported languages
6. **Notification**: Language change is communicated to the client

### Streaming Support

Language detection works with both batch and streaming transcription:

- **Batch Mode**: Language is detected for each complete transcription
- **Streaming Mode**: Language is detected incrementally as audio chunks are processed
- **Partial Results**: Language information is included in partial transcription results

### Performance Considerations

- Language detection adds minimal overhead (~5-10% increase in processing time)
- GPU acceleration is supported for language detection
- Language model caching reduces switching overhead

## Error Handling

### Common Error Scenarios

1. **Unsupported Language**: If an unsupported language is detected, the system falls back to the configured default
2. **Low Confidence**: Languages with confidence below the threshold are ignored
3. **Rapid Switching**: Consistency requirements prevent rapid language switching
4. **Model Errors**: Whisper model errors are handled gracefully with fallback behavior

### Debugging

Enable debug logging to troubleshoot language detection issues:

```cpp
// Enable debug output
std::cout << "Language detection enabled: " << whisperSTT->isLanguageDetectionEnabled() << std::endl;
std::cout << "Current language: " << whisperSTT->getCurrentDetectedLanguage() << std::endl;
```

## Testing

### Unit Tests
- `test_language_detection.cpp` - Core language detection functionality
- Tests configuration, detection accuracy, and callback behavior

### Integration Tests
- `test_language_detection_integration.cpp` - End-to-end language detection
- Tests message protocol, streaming support, and client notifications

### Running Tests

```bash
# Build and run tests
cd backend/build
make unit_tests
./unit_tests --gtest_filter="LanguageDetectionTest.*"

make integration_tests  
./integration_tests --gtest_filter="LanguageDetectionIntegrationTest.*"
```

## Limitations

1. **Model Dependency**: Language detection quality depends on the Whisper model used
2. **Short Audio**: Very short audio clips may not provide reliable language detection
3. **Mixed Languages**: Code-switching within a single utterance is not well supported
4. **Accent Sensitivity**: Strong accents may affect detection accuracy

## Future Enhancements

1. **Language History**: Track language usage patterns over time
2. **User Preferences**: Allow users to specify preferred languages
3. **Confidence Tuning**: Adaptive confidence thresholds based on user feedback
4. **Multi-speaker Support**: Per-speaker language detection in conversations
5. **Custom Models**: Support for custom language detection models

## Troubleshooting

### Common Issues

**Language detection not working**
- Verify `languageDetectionEnabled` is set to `true`
- Check that the Whisper model supports multilingual detection
- Ensure audio quality is sufficient for detection

**False language switches**
- Increase the confidence threshold
- Increase the consistency requirement
- Check for background noise or poor audio quality

**Missing language change notifications**
- Verify the language change callback is set
- Check WebSocket connection status
- Ensure message protocol is correctly implemented

### Debug Commands

```cpp
// Check current configuration
std::cout << "Detection enabled: " << whisperSTT->isLanguageDetectionEnabled() << std::endl;
std::cout << "Auto-switching enabled: " << whisperSTT->isAutoLanguageSwitchingEnabled() << std::endl;
std::cout << "Current language: " << whisperSTT->getCurrentDetectedLanguage() << std::endl;

// Monitor transcription results
whisperSTT->transcribe(audio, [](const TranscriptionResult& result) {
    std::cout << "Detected: " << result.detected_language 
              << " (confidence: " << result.language_confidence 
              << ", changed: " << result.language_changed << ")" << std::endl;
});
```