import { renderHook, act } from '@testing-library/react';
import { vi, describe, it, expect, beforeEach, afterEach } from 'vitest';
import { useConnectionResilience } from '../hooks/useConnectionResilience';
import { WebSocketManager, ConnectionState, ConnectionQuality } from '../services/WebSocketManager';
import { useAppStore } from '../store';

// Mock the store
vi.mock('../store', () => ({
  useAppStore: vi.fn()
}));

// Mock the error handler
vi.mock('../hooks/useErrorHandler', () => ({
  useErrorHandler: () => ({
    handleWebSocketError: vi.fn()
  })
}));

describe('useConnectionResilience Core Functionality', () => {
  let mockWebSocketManager: Partial<WebSocketManager>;
  let mockStore: any;

  beforeEach(() => {
    // Reset all timers
    vi.useFakeTimers();

    // Mock store
    mockStore = {
      sessionActive: false,
      sourceLang: 'en',
      targetLang: 'es',
      selectedVoice: 'default',
      setConnectionStatus: vi.fn(),
      currentState: 'idle',
      setCurrentState: vi.fn()
    };
    (useAppStore as any).mockReturnValue(mockStore);

    // Mock WebSocket manager
    mockWebSocketManager = {
      isConnected: vi.fn().mockReturnValue(false),
      getConnectionQuality: vi.fn().mockReturnValue('good' as ConnectionQuality),
      getConnectionStats: vi.fn().mockReturnValue({
        state: ConnectionState.DISCONNECTED,
        quality: 'good' as ConnectionQuality,
        reconnectAttempts: 0,
        queuedMessages: 0,
        lastError: null,
        sessionRecoverable: false
      }),
      forceReconnect: vi.fn(),
      setSessionState: vi.fn(),
      connect: vi.fn()
    };
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.clearAllMocks();
  });

  it('should initialize with default state', () => {
    const { result } = renderHook(() => 
      useConnectionResilience(mockWebSocketManager as WebSocketManager)
    );

    expect(result.current.isOffline()).toBe(false);
    expect(result.current.isOfflineMode).toBe(false);
    expect(result.current.reconnectAttempts).toBe(0);
    expect(result.current.manualRetryAvailable).toBe(false);
    expect(result.current.connectionQuality).toBe('good');
  });

  it('should provide manual retry functionality', () => {
    const { result } = renderHook(() => 
      useConnectionResilience(mockWebSocketManager as WebSocketManager)
    );

    act(() => {
      result.current.manualRetry();
    });

    expect(mockStore.setConnectionStatus).toHaveBeenCalledWith('reconnecting');
    expect(mockWebSocketManager.forceReconnect).toHaveBeenCalled();
  });

  it('should provide force reconnect functionality', () => {
    const { result } = renderHook(() => 
      useConnectionResilience(mockWebSocketManager as WebSocketManager)
    );

    act(() => {
      result.current.forceReconnect();
    });

    expect(mockWebSocketManager.forceReconnect).toHaveBeenCalled();
  });

  it('should provide comprehensive connection statistics', () => {
    const { result } = renderHook(() => 
      useConnectionResilience(mockWebSocketManager as WebSocketManager)
    );

    const stats = result.current.getConnectionStats();

    expect(stats).toMatchObject({
      isOffline: expect.any(Boolean),
      offlineDuration: expect.any(Number),
      canRecoverSession: expect.any(Boolean),
      reconnectAttempts: expect.any(Number),
      manualRetryAvailable: expect.any(Boolean),
      connectionQuality: expect.any(String),
      maxReconnectAttempts: expect.any(Number),
      nextRetryDelay: expect.any(Number),
      offlineNotificationShown: expect.any(Boolean)
    });
  });

  it('should handle session recovery when enabled', () => {
    const { result } = renderHook(() => 
      useConnectionResilience(mockWebSocketManager as WebSocketManager, {
        enableSessionRecovery: true
      })
    );

    act(() => {
      result.current.triggerSessionRecovery();
    });

    expect(mockWebSocketManager.connect).toHaveBeenCalled();
  });

  it('should respect configuration options', () => {
    const customConfig = {
      maxReconnectAttempts: 5,
      connectionRetryDelay: 1000,
      enableOfflineMode: false,
      enableSessionRecovery: false
    };

    const { result } = renderHook(() => 
      useConnectionResilience(mockWebSocketManager as WebSocketManager, customConfig)
    );

    expect(result.current.config).toMatchObject({
      maxReconnectAttempts: 5,
      connectionRetryDelay: 1000,
      enableOfflineMode: false,
      enableSessionRecovery: false
    });
  });

  it('should track offline duration', () => {
    const { result } = renderHook(() => 
      useConnectionResilience(mockWebSocketManager as WebSocketManager)
    );

    // Initially should be 0
    expect(result.current.getOfflineDuration()).toBe(0);

    // The offline duration would be set when connection is lost
    // For now, just verify the function exists and returns a number
    expect(typeof result.current.getOfflineDuration()).toBe('number');
  });

  it('should provide connection quality information', () => {
    const { result } = renderHook(() => 
      useConnectionResilience(mockWebSocketManager as WebSocketManager)
    );

    expect(result.current.connectionQuality).toBe('good');
    expect(['good', 'poor', 'critical']).toContain(result.current.connectionQuality);
  });

  it('should handle WebSocket manager being null', () => {
    const { result } = renderHook(() => 
      useConnectionResilience(null)
    );

    // Should not crash and should provide default values
    expect(result.current.isOffline()).toBe(false);
    expect(result.current.getConnectionStats()).toBe(null);
    
    // Manual operations should handle null gracefully
    act(() => {
      result.current.manualRetry();
      result.current.forceReconnect();
      result.current.triggerSessionRecovery();
    });

    // Should not have called any WebSocket methods
    expect(mockWebSocketManager.forceReconnect).not.toHaveBeenCalled();
  });
});