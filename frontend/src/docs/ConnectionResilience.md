# Connection Resilience Features

This document describes the enhanced connection resilience features implemented in the `useConnectionResilience` hook and related components.

## Overview

The connection resilience system provides robust handling of network connectivity issues, automatic reconnection with exponential backoff, connection quality monitoring, offline mode detection, and manual retry mechanisms.

## Key Features

### 1. Automatic Reconnection with Exponential Backoff

- **Initial Delay**: Configurable starting delay (default: 2 seconds)
- **Exponential Backoff**: Delay increases by a multiplier after each failed attempt (default: 1.5x)
- **Maximum Attempts**: Configurable limit on automatic retry attempts (default: 10)
- **Maximum Delay**: Capped at 30 seconds to prevent excessive delays

```typescript
const resilience = useConnectionResilience(webSocketManager, {
  connectionRetryDelay: 2000,
  maxReconnectAttempts: 10,
  exponentialBackoffMultiplier: 1.5
});
```

### 2. Connection Quality Monitoring

- **Quality Levels**: `good`, `poor`, `critical`
- **Automatic Monitoring**: Periodic checks based on heartbeat response times
- **Quality-Based Actions**: Automatic reconnection on critical quality
- **Adaptive Monitoring**: Increased frequency during poor quality periods

```typescript
// Quality changes trigger automatic actions
// Poor quality: Warning notification + increased monitoring
// Critical quality: Automatic reconnection attempt
```

### 3. Offline Mode Detection

- **Delayed Notification**: Configurable delay before showing offline status (default: 3 seconds)
- **Duration Tracking**: Tracks how long the connection has been offline
- **Maximum Offline Time**: Configurable limit for automatic retry attempts (default: 5 minutes)
- **User Notifications**: Automatic error notifications for offline state

```typescript
const resilience = useConnectionResilience(webSocketManager, {
  offlineNotificationDelay: 3000,
  maxOfflineTime: 300000 // 5 minutes
});
```

### 4. Manual Retry Mechanisms

- **Manual Retry**: Available after automatic attempts are exhausted
- **Force Reconnect**: Immediate reconnection bypassing retry limits
- **Session Recovery**: Restore previous session state after reconnection

```typescript
// Manual retry after automatic attempts fail
resilience.manualRetry();

// Force immediate reconnection
resilience.forceReconnect();

// Recover previous session
resilience.triggerSessionRecovery();
```

### 5. Session State Recovery

- **Persistent Storage**: Session state stored in localStorage
- **Automatic Recovery**: Attempts to restore session on reconnection
- **Timeout Handling**: Sessions expire after configurable timeout
- **State Validation**: Ensures session data integrity

```typescript
const resilience = useConnectionResilience(webSocketManager, {
  enableSessionRecovery: true
});
```

## Configuration Options

```typescript
interface ConnectionResilienceConfig {
  enableSessionRecovery: boolean;           // Enable session state recovery
  enableOfflineMode: boolean;               // Enable offline mode detection
  maxOfflineTime: number;                   // Max time before stopping retries (ms)
  connectionRetryDelay: number;             // Initial retry delay (ms)
  maxReconnectAttempts: number;             // Max automatic retry attempts
  exponentialBackoffMultiplier: number;     // Backoff multiplier
  connectionQualityCheckInterval: number;   // Quality check frequency (ms)
  offlineNotificationDelay: number;         // Delay before offline notification (ms)
}
```

## Usage Examples

### Basic Usage

```typescript
import { useConnectionResilience } from '../hooks/useConnectionResilience';

const MyComponent = () => {
  const resilience = useConnectionResilience(webSocketManager);
  
  return (
    <div>
      <p>Offline: {resilience.isOffline() ? 'Yes' : 'No'}</p>
      <p>Attempts: {resilience.reconnectAttempts}</p>
      {resilience.manualRetryAvailable && (
        <button onClick={resilience.manualRetry}>
          Retry Connection
        </button>
      )}
    </div>
  );
};
```

### With Connection Status Display

```typescript
import { useConnectionStatus } from '../hooks/useConnectionResilience';
import ConnectionStatus from '../components/ConnectionStatus';

const MyComponent = () => {
  const resilience = useConnectionResilience(webSocketManager);
  
  return (
    <ConnectionStatus
      resilienceStats={resilience.getConnectionStats}
      onManualRetry={resilience.manualRetry}
      onForceReconnect={resilience.forceReconnect}
      showDetails={true}
    />
  );
};
```

### Custom Configuration

```typescript
const resilience = useConnectionResilience(webSocketManager, {
  maxReconnectAttempts: 5,
  connectionRetryDelay: 1000,
  exponentialBackoffMultiplier: 2.0,
  enableOfflineMode: true,
  enableSessionRecovery: true,
  maxOfflineTime: 600000, // 10 minutes
  connectionQualityCheckInterval: 3000,
  offlineNotificationDelay: 5000
});
```

## Connection Statistics

The hook provides comprehensive statistics about the connection state:

```typescript
const stats = resilience.getConnectionStats();
// Returns:
{
  // WebSocket manager stats
  state: ConnectionState,
  quality: ConnectionQuality,
  reconnectAttempts: number,
  queuedMessages: number,
  lastError: string | null,
  sessionRecoverable: boolean,
  
  // Resilience-specific stats
  isOffline: boolean,
  offlineDuration: number,
  canRecoverSession: boolean,
  maxOfflineTime: number,
  manualRetryAvailable: boolean,
  connectionQuality: ConnectionQuality,
  nextRetryDelay: number,
  offlineNotificationShown: boolean
}
```

## Error Handling

The system integrates with the application's error handling system:

- **Automatic Error Reporting**: Connection issues are automatically reported
- **User-Friendly Messages**: Clear error messages for different scenarios
- **Recovery Guidance**: Provides users with recovery options
- **Error Context**: Includes relevant context for debugging

## Testing

The connection resilience features are thoroughly tested:

```bash
npm test -- connectionResilienceCore.test.tsx --run
```

Tests cover:
- Initialization and default state
- Manual retry functionality
- Force reconnect capability
- Connection statistics
- Session recovery
- Configuration handling
- Null WebSocket manager handling

## Integration with Existing Components

The enhanced connection resilience integrates seamlessly with existing components:

1. **App Component**: Uses resilience for main WebSocket connection
2. **StatusIndicator**: Shows connection quality and retry status
3. **ErrorBoundary**: Handles connection-related errors
4. **Settings**: Allows configuration of resilience parameters

## Performance Considerations

- **Timer Management**: All timers are properly cleaned up to prevent memory leaks
- **Efficient Monitoring**: Connection quality checks are optimized
- **Storage Usage**: Session state storage is minimal and cleaned up when expired
- **Event Handler Optimization**: Handlers are memoized to prevent unnecessary re-renders

## Browser Compatibility

The connection resilience features work across all modern browsers:
- Chrome 80+
- Firefox 75+
- Safari 13+
- Edge 80+

Graceful degradation is provided for older browsers with limited WebSocket support.