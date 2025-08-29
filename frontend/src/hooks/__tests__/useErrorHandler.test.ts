import { renderHook, act } from '@testing-library/react';
import { useErrorHandler, useAsyncErrorHandler } from '../useErrorHandler';
import { useAppStore } from '../../store';
import { ErrorType } from '../../components/ErrorNotification';

// Mock the store
jest.mock('../../store', () => ({
  useAppStore: jest.fn(),
}));

const mockUseAppStore = useAppStore as jest.MockedFunction<typeof useAppStore>;

describe('useErrorHandler', () => {
  const mockSetCurrentError = jest.fn();
  const mockClearError = jest.fn();

  beforeEach(() => {
    jest.clearAllMocks();
    mockUseAppStore.mockReturnValue({
      setCurrentError: mockSetCurrentError,
      clearError: mockClearError,
    } as any);
  });

  it('handles string errors', () => {
    const { result } = renderHook(() => useErrorHandler());

    act(() => {
      result.current.handleError('Test error message');
    });

    expect(mockSetCurrentError).toHaveBeenCalledWith(
      expect.objectContaining({
        type: ErrorType.UNKNOWN,
        message: 'An unexpected error occurred.',
        details: 'Test error message',
      })
    );
  });

  it('handles Error objects', () => {
    const { result } = renderHook(() => useErrorHandler());
    const error = new Error('Test error');

    act(() => {
      result.current.handleError(error);
    });

    expect(mockSetCurrentError).toHaveBeenCalledWith(
      expect.objectContaining({
        type: ErrorType.UNKNOWN,
        message: 'An unexpected error occurred.',
        details: 'Test error',
      })
    );
  });

  it('categorizes WebSocket errors correctly', () => {
    const { result } = renderHook(() => useErrorHandler());
    const error = new Error('WebSocket connection failed');

    act(() => {
      result.current.handleError(error);
    });

    expect(mockSetCurrentError).toHaveBeenCalledWith(
      expect.objectContaining({
        type: ErrorType.WEBSOCKET_CONNECTION,
      })
    );
  });

  it('categorizes audio permission errors correctly', () => {
    const { result } = renderHook(() => useErrorHandler());
    const error = new Error('microphone permission denied');

    act(() => {
      result.current.handleError(error);
    });

    expect(mockSetCurrentError).toHaveBeenCalledWith(
      expect.objectContaining({
        type: ErrorType.AUDIO_PERMISSION,
      })
    );
  });

  it('handles WebSocket errors specifically', () => {
    const { result } = renderHook(() => useErrorHandler());

    act(() => {
      result.current.handleWebSocketError('Connection timeout');
    });

    expect(mockSetCurrentError).toHaveBeenCalledWith(
      expect.objectContaining({
        type: ErrorType.WEBSOCKET_CONNECTION,
        message: 'Connection timeout',
      })
    );
  });

  it('handles audio errors with different types', () => {
    const { result } = renderHook(() => useErrorHandler());

    act(() => {
      result.current.handleAudioError('Permission denied', 'permission');
    });

    expect(mockSetCurrentError).toHaveBeenCalledWith(
      expect.objectContaining({
        type: ErrorType.AUDIO_PERMISSION,
      })
    );

    act(() => {
      result.current.handleAudioError('Capture failed', 'capture');
    });

    expect(mockSetCurrentError).toHaveBeenCalledWith(
      expect.objectContaining({
        type: ErrorType.AUDIO_CAPTURE,
      })
    );

    act(() => {
      result.current.handleAudioError('Playback failed', 'playback');
    });

    expect(mockSetCurrentError).toHaveBeenCalledWith(
      expect.objectContaining({
        type: ErrorType.AUDIO_PLAYBACK,
      })
    );
  });

  it('handles pipeline errors for different stages', () => {
    const { result } = renderHook(() => useErrorHandler());

    act(() => {
      result.current.handlePipelineError('transcription', 'STT failed');
    });

    expect(mockSetCurrentError).toHaveBeenCalledWith(
      expect.objectContaining({
        type: ErrorType.TRANSCRIPTION,
      })
    );

    act(() => {
      result.current.handlePipelineError('translation', 'MT failed');
    });

    expect(mockSetCurrentError).toHaveBeenCalledWith(
      expect.objectContaining({
        type: ErrorType.TRANSLATION,
      })
    );

    act(() => {
      result.current.handlePipelineError('synthesis', 'TTS failed');
    });

    expect(mockSetCurrentError).toHaveBeenCalledWith(
      expect.objectContaining({
        type: ErrorType.SYNTHESIS,
      })
    );
  });

  it('dismisses errors', () => {
    const { result } = renderHook(() => useErrorHandler());

    act(() => {
      result.current.dismissError();
    });

    expect(mockClearError).toHaveBeenCalled();
  });

  it('adds context to errors', () => {
    const { result } = renderHook(() => useErrorHandler());
    const context = { component: 'TestComponent', operation: 'testOp' };

    act(() => {
      result.current.handleError('Test error', context);
    });

    expect(mockSetCurrentError).toHaveBeenCalledWith(
      expect.objectContaining({
        context: expect.objectContaining(context),
      })
    );
  });
});

describe('useAsyncErrorHandler', () => {
  const mockSetCurrentError = jest.fn();
  const mockClearError = jest.fn();

  beforeEach(() => {
    jest.clearAllMocks();
    mockUseAppStore.mockReturnValue({
      setCurrentError: mockSetCurrentError,
      clearError: mockClearError,
    } as any);
  });

  it('executes successful operations without error handling', async () => {
    const { result } = renderHook(() => useAsyncErrorHandler());
    const mockOperation = jest.fn().mockResolvedValue('success');

    const response = await act(async () => {
      return result.current.executeWithErrorHandling(mockOperation);
    });

    expect(response).toBe('success');
    expect(mockOperation).toHaveBeenCalled();
    expect(mockSetCurrentError).not.toHaveBeenCalled();
  });

  it('handles errors in async operations', async () => {
    const { result } = renderHook(() => useAsyncErrorHandler());
    const error = new Error('Async operation failed');
    const mockOperation = jest.fn().mockRejectedValue(error);

    const response = await act(async () => {
      return result.current.executeWithErrorHandling(mockOperation);
    });

    expect(response).toBeNull();
    expect(mockOperation).toHaveBeenCalled();
    expect(mockSetCurrentError).toHaveBeenCalledWith(
      expect.objectContaining({
        details: 'Async operation failed',
      })
    );
  });

  it('retries operations with exponential backoff', async () => {
    const { result } = renderHook(() => useAsyncErrorHandler());
    const mockOperation = jest.fn()
      .mockRejectedValueOnce(new Error('First failure'))
      .mockRejectedValueOnce(new Error('Second failure'))
      .mockResolvedValue('success');

    // Mock setTimeout to avoid actual delays in tests
    jest.useFakeTimers();

    const responsePromise = act(async () => {
      return result.current.executeWithRetry(mockOperation, 3, 100);
    });

    // Fast-forward through the retry delays
    jest.advanceTimersByTime(100);
    jest.advanceTimersByTime(200);

    const response = await responsePromise;

    expect(response).toBe('success');
    expect(mockOperation).toHaveBeenCalledTimes(3);
    expect(mockSetCurrentError).not.toHaveBeenCalled();

    jest.useRealTimers();
  });

  it('handles max retry attempts exceeded', async () => {
    const { result } = renderHook(() => useAsyncErrorHandler());
    const error = new Error('Persistent failure');
    const mockOperation = jest.fn().mockRejectedValue(error);

    jest.useFakeTimers();

    const responsePromise = act(async () => {
      return result.current.executeWithRetry(mockOperation, 2, 100);
    });

    // Fast-forward through the retry delays
    jest.advanceTimersByTime(100);
    jest.advanceTimersByTime(200);

    const response = await responsePromise;

    expect(response).toBeNull();
    expect(mockOperation).toHaveBeenCalledTimes(2);
    expect(mockSetCurrentError).toHaveBeenCalledWith(
      expect.objectContaining({
        details: 'Persistent failure',
        context: expect.objectContaining({
          attempts: 2,
        }),
      })
    );

    jest.useRealTimers();
  });
});