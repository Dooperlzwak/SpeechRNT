import { renderHook, act } from '@testing-library/react';
import { vi } from 'vitest';
import { useConnectionResilience, useConnectionStatus } from '../useConnectionResilience';
import { useAppStore } from '../../store';
import { WebSocketManager, ConnectionState, ConnectionQuality } from '../../services/WebSocketManager';

// Mock the store
vi.mock('../../store', () => ({
  useAppStore: vi.fn(),
}));

// Mock the error handler
vi.mock('../useErrorHandler', () => ({
  useErrorHandler: () => ({
    handleWebSocketError: vi.fn(),
  }),
}));

const mockUseAppStore = useAppStore as any;

describe('useConnectionResilience', () => {
  let mockWebSocketManager: Partial<WebSocketManager>;
  let mockSetConnectionStatus: any;
  let mockSetCurrentState: any;

  beforeEach(() => {
    vi.clearAllMocks();
    vi.useFakeTimers();

    mockSetConnectionStatus = vi.fn();
    mockSetCurrentState = vi.fn();

    mockUseAppStore.mockReturnValue({
      sessionActive: false,
      sourceLang: 'en',
      targetLang: 'es',
      selectedVoice: 'female_voice_1',
      setConnectionStatus: mockSetConnectionStatus,
      currentState: 'idle',
      setCurrentState: mockSetCurrentState,
    });

    mockWebSocketManager = {
      setSessionState: vi.fn(),
      forceReconnect: vi.fn(),
      connect: vi.fn(),
      getConnectionStats: vi.fn().mockReturnValue({
        state: ConnectionState.CONNECTED,
        quality: 'good' as ConnectionQuality,
        reconnectAttempts: 0,
        queuedMessages: 0,
        lastError: null,
        sessionRecoverable: false,
      }),
    };
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('initializes with default configuration', () => {
    const { result } = renderHook(() =>
      useConnectionResilience(mockWebSocketManager as WebSocketManager)
    );

    expect(result.current.isOffline()).toBe(false);
    expect(result.current.getOfflineDuration()).toBe(0);
    expect(result.current.connectionRetryDelay).toBe(5000);
    expect(result.current.maxOfflineTime).toBe(300000);
  });

  it('creates and sets session state when app state changes', () => {
    mockUseAppStore.mockReturnValue({
      sessionActive: true,
      sourceLang: 'en',
      targetLang: 'es',
      selectedVoice: 'female_voice_1',
      setConnectionStatus: mockSetConnectionStatus,
      currentState: 'listening',
      setCurrentState: mockSetCurrentState,
    });

    renderHook(() =>
      useConnectionResilience(mockWebSocketManager as WebSocketManager)
    );

    expect(mockWebSocketManager.setSessionState).toHaveBeenCalledWith(
      expect.objectContaining({
        sourceLang: 'en',
        targetLang: 'es',
        selectedVoice: 'female_voice_1',
        isActive: true,
      })
    );
  });

  it('handles connection loss and enters offline mode', () => {
    const { result } = renderHook(() =>
      useConnectionResilience(mockWebSocketManager as WebSocketManager)
    );

    // Simulate connection loss by calling the handler directly
    act(() => {
      // Get the extended message handler
      const messageHandler = (mockWebSocketManager as any).messageHandler;
      if (messageHandler?.onConnectionChange) {
        messageHandler.onConnectionChange(false);
      }
    });

    expect(mockSetConnectionStatus).toHaveBeenCalledWith('disconnected');
    expect(result.current.isOffline()).toBe(true);
    expect(result.current.getOfflineDuration()).toBeGreaterThan(0);
  });

  it('schedules connection retry when offline', () => {
    const { result } = renderHook(() =>
      useConnectionResilience(mockWebSocketManager as WebSocketManager)
    );

    // Simulate connection loss
    act(() => {
      const messageHandler = (mockWebSocketManager as any).messageHandler;
      if (messageHandler?.onConnectionChange) {
        messageHandler.onConnectionChange(false);
      }
    });

    expect(result.current.isOffline()).toBe(true);

    // Fast-forward time to trigger retry
    act(() => {
      vi.advanceTimersByTime(5000);
    });

    expect(mockSetConnectionStatus).toHaveBeenCalledWith('reconnecting');
    expect(mockWebSocketManager.forceReconnect).toHaveBeenCalled();
  });

  it('stops retrying after max offline time', () => {
    const { result } = renderHook(() =>
      useConnectionResilience(mockWebSocketManager as WebSocketManager, {
        maxOfflineTime: 10000, // 10 seconds
      })
    );

    // Simulate connection loss
    act(() => {
      const messageHandler = (mockWebSocketManager as any).messageHandler;
      if (messageHandler?.onConnectionChange) {
        messageHandler.onConnectionChange(false);
      }
    });

    expect(result.current.isOffline()).toBe(true);

    // Fast-forward past max offline time
    act(() => {
      vi.advanceTimersByTime(15000);
    });

    expect(result.current.isOffline()).toBe(false);
  });

  it('handles session recovery', () => {
    const { result } = renderHook(() =>
      useConnectionResilience(mockWebSocketManager as WebSocketManager)
    );

    const sessionState = {
      sessionId: 'test_session',
      sourceLang: 'en',
      targetLang: 'es',
      selectedVoice: 'female_voice_1',
      isActive: true,
      lastActivity: Date.now(),
      pendingUtterances: [],
    };

    // Simulate session recovery
    act(() => {
      const messageHandler = (mockWebSocketManager as any).messageHandler;
      if (messageHandler?.onSessionRecovered) {
        messageHandler.onSessionRecovered(sessionState);
      }
    });

    expect(mockSetCurrentState).toHaveBeenCalledWith('listening');
  });

  it('handles connection quality changes', () => {
    const mockHandleWebSocketError = vi.fn();
    vi.mocked(require('../useErrorHandler').useErrorHandler).mockReturnValue({
      handleWebSocketError: mockHandleWebSocketError,
    });

    renderHook(() =>
      useConnectionResilience(mockWebSocketManager as WebSocketManager)
    );

    // Simulate critical connection quality
    act(() => {
      const messageHandler = (mockWebSocketManager as any).messageHandler;
      if (messageHandler?.onConnectionQualityChange) {
        messageHandler.onConnectionQualityChange('critical');
      }
    });

    expect(mockHandleWebSocketError).toHaveBeenCalledWith(
      'Connection quality is critical, attempting to reconnect'
    );
  });

  it('can trigger manual session recovery', () => {
    const { result } = renderHook(() =>
      useConnectionResilience(mockWebSocketManager as WebSocketManager)
    );

    act(() => {
      result.current.triggerSessionRecovery();
    });

    expect(mockWebSocketManager.connect).toHaveBeenCalled();
  });

  it('provides connection statistics', () => {
    const { result } = renderHook(() =>
      useConnectionResilience(mockWebSocketManager as WebSocketManager)
    );

    const stats = result.current.getConnectionStats();

    expect(stats).toEqual(
      expect.objectContaining({
        state: ConnectionState.CONNECTED,
        quality: 'good',
        reconnectAttempts: 0,
        queuedMessages: 0,
        lastError: null,
        sessionRecoverable: false,
        isOffline: false,
        offlineDuration: 0,
        canRecoverSession: false,
        maxOfflineTime: 300000,
      })
    );
  });

  it('disables session recovery when configured', () => {
    const { result } = renderHook(() =>
      useConnectionResilience(mockWebSocketManager as WebSocketManager, {
        enableSessionRecovery: false,
      })
    );

    expect(result.current.canRecoverSession()).toBe(false);
    expect(mockWebSocketManager.setSessionState).not.toHaveBeenCalled();
  });
});

describe('useConnectionStatus', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('returns correct status for connected state', () => {
    mockUseAppStore.mockReturnValue({
      connectionStatus: 'connected',
    });

    const { result } = renderHook(() => useConnectionStatus());

    expect(result.current.status).toBe('connected');
    expect(result.current.message).toBe('Connected');
    expect(result.current.color).toBe('green');
  });

  it('returns correct status for disconnected state', () => {
    mockUseAppStore.mockReturnValue({
      connectionStatus: 'disconnected',
    });

    const { result } = renderHook(() => useConnectionStatus());

    expect(result.current.status).toBe('disconnected');
    expect(result.current.message).toBe('Disconnected');
    expect(result.current.color).toBe('red');
  });

  it('returns correct status for reconnecting state', () => {
    mockUseAppStore.mockReturnValue({
      connectionStatus: 'reconnecting',
    });

    const { result } = renderHook(() => useConnectionStatus());

    expect(result.current.status).toBe('reconnecting');
    expect(result.current.message).toBe('Reconnecting...');
    expect(result.current.color).toBe('yellow');
  });

  it('shows connection error when disconnected', () => {
    const mockHandleWebSocketError = vi.fn();
    vi.mocked(require('../useErrorHandler').useErrorHandler).mockReturnValue({
      handleWebSocketError: mockHandleWebSocketError,
    });

    mockUseAppStore.mockReturnValue({
      connectionStatus: 'disconnected',
    });

    const { result } = renderHook(() => useConnectionStatus());

    act(() => {
      result.current.showConnectionError();
    });

    expect(mockHandleWebSocketError).toHaveBeenCalledWith(
      'Connection to server lost. Attempting to reconnect...'
    );
  });
});