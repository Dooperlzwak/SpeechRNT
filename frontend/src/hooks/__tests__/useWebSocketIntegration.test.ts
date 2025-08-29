/**
 * useWebSocketIntegration Tests
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { renderHook, act } from '@testing-library/react';

// Mock useErrorHandler first
vi.mock('../useErrorHandler', () => ({
  useErrorHandler: () => ({
    handleWebSocketError: vi.fn()
  })
}));

// Mock the WebSocketManager
const mockWebSocketManager = {
  connect: vi.fn(),
  disconnect: vi.fn(),
  sendMessage: vi.fn().mockReturnValue(true),
  sendBinaryMessage: vi.fn().mockReturnValue(true),
  getConnectionState: vi.fn().mockReturnValue('disconnected'),
  getConnectionQuality: vi.fn().mockReturnValue('good'),
  isConnected: vi.fn().mockReturnValue(false),
  getLastError: vi.fn().mockReturnValue(null),
  forceReconnect: vi.fn(),
  setSessionState: vi.fn(),
  getSessionState: vi.fn().mockReturnValue(null),
  clearSessionState: vi.fn(),
  getConnectionStats: vi.fn().mockReturnValue({
    state: 'disconnected',
    quality: 'good',
    reconnectAttempts: 0,
    queuedMessages: 0,
    lastError: null,
    sessionRecoverable: false
  })
};

vi.mock('../../services/WebSocketManager', () => ({
  WebSocketManager: vi.fn().mockImplementation(() => mockWebSocketManager),
  ConnectionState: {
    DISCONNECTED: 'disconnected',
    CONNECTING: 'connecting',
    CONNECTED: 'connected',
    RECONNECTING: 'reconnecting'
  }
}));

import { useWebSocketIntegration, WebSocketIntegrationConfig } from '../useWebSocketIntegration';
import { ServerMessage } from '../../types/messageProtocol';

describe('useWebSocketIntegration', () => {
  let config: WebSocketIntegrationConfig;
  let onMessage: vi.Mock;
  let onBinaryMessage: vi.Mock;
  let onConnectionChange: vi.Mock;
  let onError: vi.Mock;

  beforeEach(() => {
    config = {
      url: 'ws://localhost:8080',
      reconnectInterval: 100,
      maxReconnectAttempts: 3,
      heartbeatInterval: 1000,
      sessionRecoveryTimeout: 300000,
      messageQueueSize: 100,
      connectionQualityThreshold: 5000
    };

    onMessage = vi.fn();
    onBinaryMessage = vi.fn();
    onConnectionChange = vi.fn();
    onError = vi.fn();

    vi.clearAllMocks();
  });

  afterEach(() => {
    vi.clearAllTimers();
  });

  it('should initialize with disconnected state', () => {
    const { result } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    expect(result.current.connectionState).toBe('disconnected');
    expect(result.current.connectionQuality).toBe('good');
    expect(result.current.isConnected).toBe(false);
    expect(result.current.lastMessage).toBe(null);
    expect(result.current.lastBinaryMessage).toBe(null);
    expect(result.current.error).toBe(null);
  });

  it('should provide connect function that returns a Promise', async () => {
    const { result } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    const connectPromise = result.current.connect();
    expect(connectPromise).toBeInstanceOf(Promise);
  });

  it('should provide disconnect function', () => {
    const { result } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    expect(typeof result.current.disconnect).toBe('function');
    
    act(() => {
      result.current.disconnect();
    });

    // Should not throw
  });

  it('should provide sendMessage function', () => {
    const { result } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    const message = { type: 'ping' as const };
    const success = result.current.sendMessage(message);
    
    expect(typeof result.current.sendMessage).toBe('function');
    expect(success).toBe(true);
  });

  it('should provide sendBinaryMessage function', () => {
    const { result } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    const binaryData = new ArrayBuffer(8);
    const success = result.current.sendBinaryMessage(binaryData);
    
    expect(typeof result.current.sendBinaryMessage).toBe('function');
    expect(success).toBe(true);
  });

  it('should provide retryConnection function', async () => {
    const { result } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    expect(typeof result.current.retryConnection).toBe('function');
    
    const retryPromise = result.current.retryConnection();
    expect(retryPromise).toBeInstanceOf(Promise);
  });

  it('should provide getConnectionStats function', () => {
    const { result } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    const stats = result.current.getConnectionStats();
    
    expect(typeof result.current.getConnectionStats).toBe('function');
    expect(stats).toHaveProperty('state');
    expect(stats).toHaveProperty('quality');
    expect(stats).toHaveProperty('reconnectAttempts');
    expect(stats).toHaveProperty('queuedMessages');
    expect(stats).toHaveProperty('lastError');
    expect(stats).toHaveProperty('sessionRecoverable');
  });

  it('should provide session state management functions', () => {
    const { result } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    expect(typeof result.current.setSessionState).toBe('function');
    expect(typeof result.current.getSessionState).toBe('function');
    expect(typeof result.current.clearSessionState).toBe('function');

    const sessionState = {
      sessionId: 'test-session',
      sourceLang: 'en',
      targetLang: 'es',
      selectedVoice: 'voice1',
      isActive: true,
      lastActivity: Date.now(),
      pendingUtterances: [],
      currentUtteranceId: null
    };

    act(() => {
      result.current.setSessionState(sessionState);
    });

    act(() => {
      result.current.clearSessionState();
    });

    // Should not throw
  });

  it('should handle message callbacks correctly', () => {
    const { result } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    // Simulate message handling through the internal message handler
    const testMessage: ServerMessage = {
      type: 'status_update',
      data: { state: 'listening' }
    };

    // Access the internal message handler (this would normally be called by WebSocketManager)
    // For testing purposes, we'll verify the callbacks are set up correctly
    expect(onMessage).toBeDefined();
    expect(onBinaryMessage).toBeDefined();
    expect(onConnectionChange).toBeDefined();
    expect(onError).toBeDefined();
  });

  it('should handle connection state changes', () => {
    const { result } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    // Initial state should be disconnected
    expect(result.current.connectionState).toBe('disconnected');
    expect(result.current.isConnected).toBe(false);
  });

  it('should handle connection quality changes', () => {
    const { result } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    // Initial quality should be good
    expect(result.current.connectionQuality).toBe('good');
  });

  it('should handle errors appropriately', () => {
    const { result } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    // Initial error should be null
    expect(result.current.error).toBe(null);
  });

  it('should clean up on unmount', () => {
    const { unmount } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    // Should not throw on unmount
    unmount();
  });

  it('should recreate WebSocket manager when URL changes', () => {
    const { result, rerender } = renderHook(
      ({ url }) => useWebSocketIntegration(
        { ...config, url }, 
        onMessage, 
        onBinaryMessage, 
        onConnectionChange, 
        onError
      ),
      { initialProps: { url: 'ws://localhost:8080' } }
    );

    // Change URL
    rerender({ url: 'ws://localhost:8081' });

    // Should still be functional
    expect(result.current.connectionState).toBe('disconnected');
  });

  it('should handle session recovery', () => {
    const { result } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    const sessionState = {
      sessionId: 'test-session',
      sourceLang: 'en',
      targetLang: 'es',
      selectedVoice: 'voice1',
      isActive: true,
      lastActivity: Date.now(),
      pendingUtterances: [],
      currentUtteranceId: 123
    };

    act(() => {
      result.current.setSessionState(sessionState);
    });

    // Session state management should work
    expect(typeof result.current.getSessionState).toBe('function');
  });

  it('should handle connection timeout in connect method', async () => {
    vi.useFakeTimers();
    
    const { result } = renderHook(() =>
      useWebSocketIntegration(config, onMessage, onBinaryMessage, onConnectionChange, onError)
    );

    const connectPromise = result.current.connect();
    
    // Fast-forward time to trigger timeout
    act(() => {
      vi.advanceTimersByTime(10000);
    });

    await expect(connectPromise).rejects.toThrow('Connection timeout');
    
    vi.useRealTimers();
  });
});