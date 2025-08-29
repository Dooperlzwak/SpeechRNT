# Services Documentation

This document describes the core services implemented for the SpeechRNT application.

## Services Overview

- **AudioPlaybackManager** - Handles audio playback and streaming
- **WebSocketManager** - Manages WebSocket connections and messaging
- **AudioManager** - Handles audio capture and recording
- **ErrorReportingService** - Centralized error reporting and monitoring

---

# ErrorReportingService

The ErrorReportingService provides centralized error reporting to external monitoring services with configurable backends, data sanitization, and privacy protection.

## Features

- **Configurable Backends**: Support for development and production environments
- **Error Context Collection**: Automatic collection of user agent, URL, component stack
- **Data Sanitization**: Privacy protection by sanitizing sensitive information
- **Global Error Handling**: Automatic capture of unhandled errors and promise rejections
- **Session Management**: User and session context tracking
- **Rate Limiting**: Prevents spam reporting with configurable limits

## Quick Start

```typescript
import { createErrorReportingService } from './services/ErrorReportingService';

// Initialize service
const errorService = createErrorReportingService('development');

// Report an error
await errorService.reportError(new Error('Something went wrong'), {
  component: 'MyComponent',
  operation: 'handleClick',
  additionalData: { userId: 'user123' }
});
```

## Configuration

### Development Configuration
```typescript
const devConfig = {
  environment: 'development',
  enableLocalLogging: true,
  enableRemoteReporting: false,
  sanitizeData: false,
  maxErrorsPerSession: 100
};
```

### Production Configuration
```typescript
const prodConfig = {
  environment: 'production',
  enableLocalLogging: false,
  enableRemoteReporting: true,
  sanitizeData: true,
  maxErrorsPerSession: 50,
  apiKey: 'your-api-key',
  endpoint: 'https://api.monitoring-service.com/errors'
};
```

## API Reference

### ErrorReportingService

#### Methods

- `reportError(error: Error, context?: ErrorContext)` - Report a general error
- `reportException(error: Error, errorInfo: ErrorInfo, context?: ErrorContext)` - Report React component exception
- `setUserContext(userId: string, sessionId?: string)` - Set user context
- `clearUserContext()` - Clear user context
- `getStats()` - Get service statistics
- `reset()` - Reset error count and session

#### Error Context
```typescript
interface ErrorContext {
  component?: string;
  operation?: string;
  userId?: string;
  sessionId?: string;
  additionalData?: Record<string, any>;
}
```

## Data Sanitization

The service automatically sanitizes sensitive information:

- **Email addresses** → `[EMAIL]`
- **Credit card numbers** → `[CARD]`
- **Social security numbers** → `[SSN]`
- **Passwords** → `password=[REDACTED]`
- **Tokens** → `token=[REDACTED]`
- **File paths** → `[FILE_PATH]`

## Integration Examples

### With React Error Boundary
```typescript
import { ErrorReportingService } from './services/ErrorReportingService';

class ErrorBoundary extends React.Component {
  private errorService = createErrorReportingService();

  componentDidCatch(error: Error, errorInfo: ErrorInfo) {
    this.errorService.reportException(error, errorInfo, {
      component: 'ErrorBoundary'
    });
  }
}
```

### With Custom Hook
```typescript
const useErrorReporting = () => {
  const [errorService] = useState(() => createErrorReportingService());
  
  const reportError = useCallback(async (error: Error, context?: ErrorContext) => {
    await errorService.reportError(error, context);
  }, [errorService]);

  return { reportError, errorService };
};
```

---

# Audio Playback Implementation

This section describes the audio playback system implemented for the SpeechRNT application.

## Overview

The audio playback system handles automatic playback of synthesized speech audio received from the backend. It supports:

- Real-time audio streaming and playback
- Multiple audio formats (WAV, PCM)
- Automatic queue management
- Volume control
- Browser compatibility handling
- Error recovery

## Architecture

### Components

1. **AudioPlaybackManager** - Core service class that manages audio playback
2. **useAudioPlayback** - React hook that provides a convenient interface
3. **WebSocket Integration** - Handles audio messages from the backend
4. **Message Protocol** - Extended protocol for audio data transmission

### Message Flow

```
Backend → WebSocket → Frontend
1. audio_start (metadata)
2. audio_data (binary chunks) 
3. audio_end (completion)
```

## AudioPlaybackManager

The core service class that handles all audio playback functionality.

### Key Features

- **Automatic Playback**: Queues and plays audio automatically when `autoPlay` is enabled
- **Format Support**: Handles WAV files and raw PCM data
- **Volume Control**: Real-time volume adjustment with gain nodes
- **Error Handling**: Graceful error recovery and user notification
- **Resource Management**: Proper cleanup of audio resources

### Configuration

```typescript
interface AudioPlaybackConfig {
  volume: number;        // 0.0 to 1.0
  autoPlay: boolean;     // Automatic playback when audio is ready
  crossfade: boolean;    // Crossfade between audio (future feature)
  bufferSize: number;    // Audio buffer size
}
```

### Usage

```typescript
const manager = new AudioPlaybackManager(config, handler);
await manager.initialize();

// Handle audio messages
manager.handleAudioStart(utteranceId, duration, format, sampleRate, channels);
manager.handleAudioData(utteranceId, audioData, sequenceNumber, isLast);
manager.handleAudioEnd(utteranceId);

// Manual playback control
await manager.playAudio(utteranceId);
manager.stopPlayback();
manager.setVolume(0.8);
```

## useAudioPlayback Hook

React hook that provides a convenient interface to the AudioPlaybackManager.

### Features

- **State Management**: Tracks playback state, initialization, and errors
- **Event Handling**: Provides callbacks for playback events
- **Automatic Cleanup**: Handles resource cleanup on unmount
- **Error Boundaries**: Isolates errors and provides recovery

### Usage

```typescript
const {
  playbackState,
  isSupported,
  isInitialized,
  error,
  initialize,
  handleAudioStart,
  handleAudioData,
  handleAudioEnd,
  playAudio,
  stopPlayback,
  setVolume,
} = useAudioPlayback(
  config,
  onPlaybackStart,
  onPlaybackEnd,
  onPlaybackError,
  onVolumeChange
);
```

## WebSocket Integration

The audio playback system integrates with the WebSocket message protocol.

### Message Types

#### audio_start
```json
{
  "type": "audio_start",
  "data": {
    "utteranceId": 1,
    "duration": 2.5,
    "format": "wav",
    "sampleRate": 22050,
    "channels": 1
  }
}
```

#### audio_data
```json
{
  "type": "audio_data", 
  "data": {
    "utteranceId": 1,
    "sequenceNumber": 0,
    "isLast": false
  }
}
```
*Followed by binary audio data*

#### audio_end
```json
{
  "type": "audio_end",
  "data": {
    "utteranceId": 1
  }
}
```

### WebSocket Handler Updates

The WebSocketManager was updated to:
- Track pending audio messages
- Associate binary data with message types
- Provide message type context to handlers

## Browser Compatibility

### Supported Browsers

- Chrome 66+
- Firefox 60+
- Safari 14.1+
- Edge 79+

### Feature Detection

```typescript
const isSupported = AudioPlaybackManager.isSupported();
```

### Fallbacks

- Automatic detection of AudioContext vs webkitAudioContext
- Graceful degradation when audio is not supported
- Error handling for permission issues

## Audio Format Support

### WAV Files
- Standard WAV format with headers
- Automatic decoding via Web Audio API
- Fallback to manual PCM parsing

### PCM Data
- 16-bit signed integer samples
- Configurable sample rate and channels
- Manual audio buffer creation

### Format Detection
```typescript
// Automatic format handling
manager.handleAudioStart(id, duration, 'wav', 22050, 1);
manager.handleAudioStart(id, duration, 'pcm', 16000, 1);
```

## Error Handling

### Error Types

1. **Initialization Errors**: Audio context creation failures
2. **Decoding Errors**: Invalid or corrupted audio data
3. **Playback Errors**: Audio source or connection issues
4. **Permission Errors**: Browser audio permission denied

### Error Recovery

- Automatic fallback to PCM decoding for WAV failures
- Graceful handling of missing audio data
- User notification with actionable error messages
- Continued operation after individual utterance failures

### Error Callbacks

```typescript
const onPlaybackError = (utteranceId: number, error: Error) => {
  console.error(`Playback failed for utterance ${utteranceId}:`, error);
  // Handle error (show notification, retry, etc.)
};
```

## Performance Considerations

### Memory Management
- Automatic cleanup of completed audio data
- LRU-style queue management
- Resource disposal on component unmount

### Latency Optimization
- Streaming audio data processing
- Minimal buffering delays
- Concurrent audio preparation

### CPU Usage
- Efficient audio buffer creation
- Optimized format conversion
- Minimal main thread blocking

## Testing

### Unit Tests
- AudioPlaybackManager functionality
- useAudioPlayback hook behavior
- Error handling scenarios
- Browser compatibility

### Integration Tests
- WebSocket to audio playback flow
- Multiple concurrent audio streams
- Volume control during playback
- Connection failure recovery

### Test Coverage
- Core functionality: 95%+
- Error scenarios: 90%+
- Browser compatibility: 85%+

## Usage Examples

### Basic Setup

```typescript
// In a React component
const {
  playbackState,
  initialize,
  handleAudioStart,
  handleAudioData,
  handleAudioEnd,
} = useAudioPlayback({
  volume: 0.8,
  autoPlay: true,
  crossfade: false,
  bufferSize: 4096,
});

// Initialize on mount
useEffect(() => {
  initialize();
}, []);

// Handle WebSocket messages
const handleMessage = (message) => {
  if (message.type === 'audio_start') {
    handleAudioStart(
      message.data.utteranceId,
      message.data.duration,
      message.data.format,
      message.data.sampleRate,
      message.data.channels
    );
  }
  // ... handle other message types
};
```

### Manual Playback Control

```typescript
// Disable auto-play for manual control
const { playAudio, stopPlayback } = useAudioPlayback({
  autoPlay: false,
  // ... other config
});

// Play specific utterance
const handlePlayButton = async (utteranceId) => {
  try {
    await playAudio(utteranceId);
  } catch (error) {
    console.error('Playback failed:', error);
  }
};

// Stop current playback
const handleStopButton = () => {
  stopPlayback();
};
```

### Volume Control

```typescript
const [volume, setVolume] = useState(0.8);

const { setVolume: setPlaybackVolume } = useAudioPlayback({
  volume,
  // ... other config
});

const handleVolumeChange = (newVolume) => {
  setVolume(newVolume);
  setPlaybackVolume(newVolume);
};
```

## Future Enhancements

### Planned Features
- Crossfade between audio utterances
- Audio effects (reverb, EQ)
- Spatial audio support
- Advanced queue management
- Audio visualization

### Performance Improvements
- Web Workers for audio processing
- AudioWorklet for low-latency processing
- Streaming audio compression
- Predictive audio loading

## Troubleshooting

### Common Issues

1. **No Audio Output**
   - Check browser audio permissions
   - Verify audio context initialization
   - Check volume settings

2. **Choppy Playback**
   - Increase buffer size
   - Check network connection
   - Verify audio format compatibility

3. **High Latency**
   - Reduce buffer size
   - Check WebSocket connection
   - Optimize audio processing

### Debug Tools

```typescript
// Enable debug logging
const manager = new AudioPlaybackManager(config, handler);
manager.getPlaybackState(); // Check current state
console.log('Audio queue:', manager.getPlaybackState().queue);
```