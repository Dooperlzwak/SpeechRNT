# Session State Management

This directory contains the Zustand-based state management implementation for the Vocr application.

## Overview

The state management system provides centralized control for:
- Session lifecycle (start/stop conversation)
- System state transitions (idle → listening → thinking → speaking)
- Connection status management
- Language and voice settings
- Conversation history and current text
- UI state (settings dialog, etc.)

## Architecture

### Store Structure (`appStore.ts`)

The main store uses Zustand with the following key features:
- **Devtools integration** for debugging
- **Immutable state updates** following React patterns
- **Action-based mutations** for predictable state changes
- **Computed state derivation** where needed

### Key State Sections

#### Session State
```typescript
sessionActive: boolean;        // Whether conversation is active
currentState: SystemState;     // Current processing state
connectionStatus: string;      // WebSocket connection status
```

#### Content State
```typescript
conversationHistory: ConversationEntry[];  // Complete conversation log
currentOriginalText: string;               // Live transcription
currentTranslatedText: string;             // Live translation
transcriptionConfidence?: number;          // STT confidence score
```

#### Settings State
```typescript
sourceLang: string;      // Source language
targetLang: string;      // Target language  
selectedVoice: string;   // TTS voice selection
settingsOpen: boolean;   // Settings dialog state
```

## State Transitions

### Session Lifecycle
1. **Idle** → **Starting** (user clicks start)
2. **Starting** → **Listening** (connection established)
3. **Listening** → **Thinking** (speech detected, processing)
4. **Thinking** → **Speaking** (translation complete, TTS playing)
5. **Speaking** → **Listening** (audio complete, ready for next)
6. **Any State** → **Idle** (user stops session)

### Connection States
- **disconnected**: No WebSocket connection
- **reconnecting**: Attempting to establish connection
- **connected**: Active WebSocket connection

## Usage Patterns

### Basic Store Access
```typescript
import { useAppStore } from '../store';

function MyComponent() {
  const { sessionActive, toggleSession } = useAppStore();
  
  return (
    <button onClick={toggleSession}>
      {sessionActive ? 'Stop' : 'Start'}
    </button>
  );
}
```

### Selective State Subscription
```typescript
// Only re-render when currentState changes
const currentState = useAppStore(state => state.currentState);

// Multiple values with shallow comparison
const { sessionActive, connectionStatus } = useAppStore(
  state => ({ 
    sessionActive: state.sessionActive,
    connectionStatus: state.connectionStatus 
  }),
  shallow
);
```

### Action Dispatching
```typescript
const { 
  setCurrentState, 
  setLanguages, 
  addConversationEntry 
} = useAppStore();

// Update system state
setCurrentState('thinking');

// Update settings
setLanguages('French', 'German');

// Add conversation entry
addConversationEntry({
  utteranceId: 1,
  originalText: 'Hello',
  translatedText: 'Bonjour',
  speaker: 'user'
});
```

## Session Control Hook (`useSessionControl.ts`)

The `useSessionControl` hook provides higher-level session management:

### Features
- **Automatic state transitions** based on session lifecycle
- **Connection management** with retry logic
- **Side effect handling** for WebSocket and audio
- **Timer-based state transitions** for demo purposes

### Usage
```typescript
import { useSessionControl } from '../hooks/useSessionControl';

function ConversationPanel() {
  const {
    sessionActive,
    currentState,
    connectionStatus,
    toggleSession,
    transitionToState
  } = useSessionControl();

  return (
    <div>
      <StatusIndicator state={currentState} />
      <button onClick={toggleSession}>
        {sessionActive ? 'Stop' : 'Start'}
      </button>
    </div>
  );
}
```

## Testing Strategy

### Unit Tests
- **Store actions**: Test each action in isolation
- **State transitions**: Verify correct state changes
- **Side effects**: Mock external dependencies

### Integration Tests
- **Component integration**: Test store + components
- **Session flows**: Test complete user workflows
- **Error scenarios**: Test error handling and recovery

### Test Files
- `__tests__/appStore.test.ts`: Core store functionality
- `__tests__/useSessionControl.test.ts`: Session control hook
- `../test/sessionStateIntegration.test.tsx`: Full integration tests

## Performance Considerations

### Optimization Strategies
1. **Selective subscriptions**: Only subscribe to needed state slices
2. **Shallow comparison**: Use shallow equality for object selections
3. **Action batching**: Batch related state updates
4. **Memoization**: Memoize expensive computations

### Memory Management
- **Conversation history**: Implement size limits for long conversations
- **Model caching**: LRU cache for language models
- **Event cleanup**: Proper cleanup of timers and listeners

## Future Enhancements

### Planned Features
1. **Persistence**: Save conversation history to localStorage
2. **Offline support**: Queue actions when disconnected
3. **Real-time sync**: Multi-device conversation sync
4. **Analytics**: Usage metrics and performance tracking

### Integration Points
- **WebSocket service**: Real-time communication
- **Audio service**: Microphone and speaker management
- **AI pipeline**: STT, MT, and TTS integration
- **Settings service**: User preferences persistence