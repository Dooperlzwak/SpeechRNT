# useConfigurationSync Hook

The `useConfigurationSync` hook provides backend configuration synchronization functionality for the Vocr application. It handles language and voice settings synchronization with debouncing, retry logic, and comprehensive error handling.

## Features

- **Debounced Synchronization**: Prevents excessive backend calls during rapid configuration changes
- **Retry Logic**: Automatically retries failed synchronization attempts with exponential backoff
- **Error Handling**: Comprehensive error handling with user-friendly error reporting
- **Configuration Coordination**: Ensures language and voice settings are properly coordinated
- **Statistics Tracking**: Provides sync statistics for monitoring and debugging
- **Promise-based API**: Returns promises for async/await usage

## Usage

```typescript
import { useConfigurationSync } from './hooks/useConfigurationSync';
import { useWebSocketIntegration } from './hooks/useWebSocketIntegration';

const MyComponent = () => {
  // WebSocket integration for sending messages
  const { sendMessage } = useWebSocketIntegration(/* ... */);

  // Configuration synchronization
  const {
    syncLanguageSettings,
    syncVoiceSettings,
    isSyncing,
    lastSyncError,
    pendingSyncs,
    retryFailedSync,
    clearSyncError,
    getSyncStats
  } = useConfigurationSync(
    sendMessage,
    (error) => console.error('Sync error:', error),
    {
      debounceDelay: 500,
      maxRetryAttempts: 3,
      retryDelay: 1000,
      syncTimeout: 10000,
    }
  );

  // Sync language settings
  const handleLanguageChange = async (source: string, target: string) => {
    try {
      await syncLanguageSettings(source, target);
      console.log('Language settings synchronized');
    } catch (error) {
      console.error('Failed to sync language settings:', error);
    }
  };

  // Sync voice settings
  const handleVoiceChange = async (voice: string) => {
    try {
      await syncVoiceSettings(voice);
      console.log('Voice settings synchronized');
    } catch (error) {
      console.error('Failed to sync voice settings:', error);
    }
  };

  return (
    <div>
      {/* Your UI components */}
      {isSyncing && <div>Syncing...</div>}
      {lastSyncError && (
        <div>
          Error: {lastSyncError.message}
          <button onClick={retryFailedSync}>Retry</button>
          <button onClick={clearSyncError}>Clear</button>
        </div>
      )}
    </div>
  );
};
```

## API Reference

### Parameters

#### `sendMessage: (message: ClientMessage) => boolean`
Function to send messages through WebSocket. Should return `true` if message was sent successfully.

#### `onError: (error: Error) => void`
Callback function called when synchronization errors occur.

#### `config?: Partial<ConfigurationSyncConfig>`
Optional configuration object with the following properties:

- `debounceDelay: number` - Delay in milliseconds for debouncing rapid changes (default: 500)
- `maxRetryAttempts: number` - Maximum number of retry attempts for failed syncs (default: 3)
- `retryDelay: number` - Base delay in milliseconds between retry attempts (default: 1000)
- `syncTimeout: number` - Timeout in milliseconds for sync operations (default: 10000)

### Return Value

The hook returns an object with the following properties and methods:

#### `syncLanguageSettings(source: string, target: string): Promise<void>`
Synchronizes language settings with the backend. Validates input and handles debouncing.

**Parameters:**
- `source: string` - Source language code (e.g., 'en', 'es')
- `target: string` - Target language code (e.g., 'es', 'fr')

**Throws:**
- Error if source or target languages are empty
- Error if source and target languages are the same

#### `syncVoiceSettings(voice: string): Promise<void>`
Synchronizes voice settings with the backend. Handles debouncing and coordination with language settings.

**Parameters:**
- `voice: string` - Voice identifier (e.g., 'female-voice-1', 'male-voice-1')

**Throws:**
- Error if voice selection is empty

#### `isSyncing: boolean`
Indicates whether a synchronization operation is currently in progress.

#### `lastSyncError: Error | null`
The last synchronization error that occurred, or `null` if no error.

#### `pendingSyncs: number`
Number of synchronization operations currently pending.

#### `retryFailedSync(): Promise<void>`
Manually retry the last failed synchronization operation.

**Throws:**
- Error if no failed sync is available to retry

#### `clearSyncError(): void`
Clear the current synchronization error state.

#### `getSyncStats(): SyncStats`
Get synchronization statistics.

**Returns:**
```typescript
{
  totalSyncs: number;        // Total number of sync attempts
  successfulSyncs: number;   // Number of successful syncs
  failedSyncs: number;       // Number of failed syncs
  lastSyncTime: Date | null; // Timestamp of last sync attempt
}
```

## Message Protocol

The hook sends configuration messages in the following format:

```typescript
{
  type: 'config',
  data: {
    sourceLang: string,  // Source language code
    targetLang: string,  // Target language code
    voice: string        // Voice identifier
  }
}
```

## Error Handling

The hook implements comprehensive error handling:

1. **Input Validation**: Validates all input parameters before processing
2. **Connection Errors**: Handles WebSocket connection failures
3. **Timeout Errors**: Handles synchronization timeouts
4. **Retry Logic**: Automatically retries failed operations with exponential backoff
5. **Error Reporting**: Reports errors through the provided `onError` callback

## Debouncing Behavior

The hook implements debouncing to prevent excessive backend calls:

1. **Rapid Changes**: Multiple rapid configuration changes are debounced
2. **Superseding**: Newer sync requests supersede older pending requests of the same type
3. **Coordination**: Language and voice settings are properly coordinated

## Testing

The hook includes comprehensive tests covering:

- Language and voice synchronization
- Debouncing behavior
- Error handling and retry logic
- State management
- Configuration coordination

Run tests with:
```bash
npm test -- useConfigurationSync.test.ts
```

## Integration with Other Hooks

The hook is designed to work seamlessly with other Vocr hooks:

- **useWebSocketIntegration**: Provides the `sendMessage` function
- **useSessionControl**: Can be integrated for session-aware configuration sync
- **useErrorHandler**: Provides error handling utilities

## Requirements Satisfied

This hook satisfies the following requirements from the specification:

- **3.1**: Language configuration sync with success/error handling
- **3.2**: Voice configuration sync with validation
- **3.3**: Retry logic for failed synchronization attempts
- **3.4**: Debouncing for rapid configuration changes
- **3.5**: Backend settings synchronization coordination