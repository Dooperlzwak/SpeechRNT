import React from 'react';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { act } from '@testing-library/react';
import App from '../App';
import { useAppStore } from '../store';
import { ErrorFactory, ErrorType } from '../components/ErrorNotification';
import { useErrorHandler } from '../hooks/useErrorHandler';

// Mock WebSocket
class MockWebSocket {
  static CONNECTING = 0;
  static OPEN = 1;
  static CLOSING = 2;
  static CLOSED = 3;

  readyState = MockWebSocket.CONNECTING;
  onopen: ((event: Event) => void) | null = null;
  onclose: ((event: CloseEvent) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;
  onmessage: ((event: MessageEvent) => void) | null = null;

  constructor(public url: string) {
    setTimeout(() => {
      this.readyState = MockWebSocket.OPEN;
      if (this.onopen) {
        this.onopen(new Event('open'));
      }
    }, 10);
  }

  send(data: string | ArrayBuffer) {
    // Mock send implementation
  }

  close(code?: number, reason?: string) {
    this.readyState = MockWebSocket.CLOSED;
    if (this.onclose) {
      this.onclose(new CloseEvent('close', { code: code || 1000, reason }));
    }
  }

  // Helper method to simulate connection error
  simulateError() {
    if (this.onerror) {
      this.onerror(new Event('error'));
    }
  }

  // Helper method to simulate connection close
  simulateClose(code: number = 1006) {
    this.readyState = MockWebSocket.CLOSED;
    if (this.onclose) {
      this.onclose(new CloseEvent('close', { code, reason: 'Connection lost' }));
    }
  }
}

// Mock MediaDevices
const mockGetUserMedia = jest.fn();
const mockEnumerateDevices = jest.fn();

Object.defineProperty(navigator, 'mediaDevices', {
  writable: true,
  value: {
    getUserMedia: mockGetUserMedia,
    enumerateDevices: mockEnumerateDevices,
  },
});

// Mock fetch
global.fetch = jest.fn();

// Mock WebSocket globally
(global as any).WebSocket = MockWebSocket;

describe('Error Handling Integration Tests', () => {
  let mockWebSocket: MockWebSocket;

  beforeEach(() => {
    jest.clearAllMocks();
    
    // Reset store state
    useAppStore.getState().clearError();
    useAppStore.getState().clearErrorHistory();
    
    // Setup default mocks
    mockGetUserMedia.mockResolvedValue({
      getTracks: () => [{ stop: jest.fn() }],
    });
    
    mockEnumerateDevices.mockResolvedValue([
      { kind: 'audioinput', deviceId: 'default', label: 'Default Microphone' },
    ]);
    
    (fetch as jest.Mock).mockResolvedValue({ ok: true });
  });

  afterEach(() => {
    if (mockWebSocket) {
      mockWebSocket.close();
    }
  });

  it('handles WebSocket connection errors with recovery dialog', async () => {
    render(<App />);

    // Simulate WebSocket connection error
    act(() => {
      const error = ErrorFactory.createWebSocketError('Connection failed');
      useAppStore.getState().setCurrentError(error);
    });

    // Should show error notification
    expect(screen.getByText('Connection Failed')).toBeInTheDocument();
    expect(screen.getByText('Connection failed')).toBeInTheDocument();

    // Should show retry button
    const retryButton = screen.getByText('Retry');
    expect(retryButton).toBeInTheDocument();

    // Click retry button
    fireEvent.click(retryButton);

    // Error should be cleared after retry
    await waitFor(() => {
      expect(screen.queryByText('Connection Failed')).not.toBeInTheDocument();
    });
  });

  it('handles audio permission errors with step-by-step recovery', async () => {
    render(<App />);

    // Simulate audio permission error
    act(() => {
      const error = ErrorFactory.createAudioPermissionError();
      useAppStore.getState().setCurrentError(error);
    });

    // Should show error notification
    expect(screen.getByText('Microphone Access Required')).toBeInTheDocument();

    // Should show grant permission button
    const grantButton = screen.getByText('Grant Permission');
    expect(grantButton).toBeInTheDocument();

    // Click grant permission button
    fireEvent.click(grantButton);

    // Should call getUserMedia
    await waitFor(() => {
      expect(mockGetUserMedia).toHaveBeenCalledWith({ audio: true });
    });
  });

  it('handles pipeline errors with graceful degradation', async () => {
    render(<App />);

    // Simulate transcription error
    act(() => {
      const error = ErrorFactory.createTranscriptionError('STT service unavailable');
      useAppStore.getState().setCurrentError(error);
    });

    // Should show error notification
    expect(screen.getByText('Transcription Error')).toBeInTheDocument();
    expect(screen.getByText('Failed to transcribe speech.')).toBeInTheDocument();

    // Should auto-hide after timeout (non-critical error)
    await waitFor(() => {
      expect(screen.queryByText('Transcription Error')).not.toBeInTheDocument();
    }, { timeout: 6000 });
  });

  it('handles multiple concurrent errors correctly', async () => {
    render(<App />);

    // Simulate multiple errors
    act(() => {
      const wsError = ErrorFactory.createWebSocketError('Connection lost');
      const audioError = ErrorFactory.createAudioCaptureError('Microphone busy');
      
      useAppStore.getState().setCurrentError(wsError);
      useAppStore.getState().addToErrorHistory(audioError);
    });

    // Should show the most recent error
    expect(screen.getByText('Connection Failed')).toBeInTheDocument();

    // Should have both errors in history
    const errorHistory = useAppStore.getState().errorHistory;
    expect(errorHistory).toHaveLength(2);
    expect(errorHistory[0].type).toBe(ErrorType.WEBSOCKET_CONNECTION);
    expect(errorHistory[1].type).toBe(ErrorType.AUDIO_CAPTURE);
  });

  it('handles error boundary crashes gracefully', async () => {
    // Component that throws an error
    const BuggyComponent = ({ shouldThrow }: { shouldThrow: boolean }) => {
      if (shouldThrow) {
        throw new Error('Component crashed');
      }
      return <div>Working component</div>;
    };

    const TestApp = () => {
      const [shouldThrow, setShouldThrow] = React.useState(false);
      
      return (
        <div>
          <button onClick={() => setShouldThrow(true)}>Trigger Error</button>
          <BuggyComponent shouldThrow={shouldThrow} />
        </div>
      );
    };

    // Suppress console.error for this test
    const originalError = console.error;
    console.error = jest.fn();

    render(<TestApp />);

    expect(screen.getByText('Working component')).toBeInTheDocument();

    // Trigger component error
    fireEvent.click(screen.getByText('Trigger Error'));

    // Should show error boundary UI
    expect(screen.getByText('Something went wrong')).toBeInTheDocument();
    expect(screen.getByText('Try Again')).toBeInTheDocument();

    // Restore console.error
    console.error = originalError;
  });

  it('handles network connectivity issues', async () => {
    render(<App />);

    // Mock network failure
    (fetch as jest.Mock).mockRejectedValue(new Error('Network error'));

    // Simulate network error
    act(() => {
      const error = ErrorFactory.createNetworkError('Network connection lost');
      useAppStore.getState().setCurrentError(error);
    });

    // Should show network error
    expect(screen.getByText('Network Error')).toBeInTheDocument();
    expect(screen.getByText('Network connection lost')).toBeInTheDocument();

    // Should show retry button
    const retryButton = screen.getByText('Retry');
    fireEvent.click(retryButton);

    // Should attempt to retry
    await waitFor(() => {
      expect(screen.queryByText('Network Error')).not.toBeInTheDocument();
    });
  });

  it('handles session recovery after connection loss', async () => {
    render(<App />);

    // Start a session
    const toggleButton = screen.getByRole('button', { name: /start conversation|stop conversation/i });
    fireEvent.click(toggleButton);

    // Verify session is active
    await waitFor(() => {
      expect(useAppStore.getState().sessionActive).toBe(true);
    });

    // Simulate connection loss
    act(() => {
      const error = ErrorFactory.createWebSocketError('Connection lost during session');
      useAppStore.getState().setCurrentError(error);
      useAppStore.getState().setConnectionStatus('disconnected');
    });

    // Should show connection error
    expect(screen.getByText('Connection Failed')).toBeInTheDocument();

    // Should maintain session state
    expect(useAppStore.getState().sessionActive).toBe(true);

    // Simulate reconnection
    act(() => {
      useAppStore.getState().clearError();
      useAppStore.getState().setConnectionStatus('connected');
    });

    // Session should still be active after reconnection
    expect(useAppStore.getState().sessionActive).toBe(true);
  });

  it('handles error recovery with user guidance', async () => {
    const TestComponent = () => {
      const { handleError, dismissError } = useErrorHandler();
      
      return (
        <div>
          <button onClick={() => handleError('Test error for recovery')}>
            Trigger Error
          </button>
          <button onClick={dismissError}>
            Dismiss Error
          </button>
        </div>
      );
    };

    render(<TestComponent />);

    // Trigger an error
    fireEvent.click(screen.getByText('Trigger Error'));

    // Should show error in store
    await waitFor(() => {
      expect(useAppStore.getState().currentError).not.toBeNull();
    });

    // Dismiss the error
    fireEvent.click(screen.getByText('Dismiss Error'));

    // Error should be cleared
    await waitFor(() => {
      expect(useAppStore.getState().currentError).toBeNull();
    });
  });

  it('handles error escalation for critical failures', async () => {
    render(<App />);

    // Simulate critical model loading error
    act(() => {
      const error = ErrorFactory.createUnknownError(new Error('Critical system failure'));
      error.recoverable = false;
      useAppStore.getState().setCurrentError(error);
    });

    // Should show critical error
    expect(screen.getByText('Unexpected Error')).toBeInTheDocument();

    // Should not auto-hide critical errors
    await new Promise(resolve => setTimeout(resolve, 6000));
    expect(screen.getByText('Unexpected Error')).toBeInTheDocument();
  });

  it('handles error context and debugging information', async () => {
    render(<App />);

    // Create error with context
    const error = ErrorFactory.createPipelineError('Pipeline stage failed', {
      stage: 'transcription',
      utteranceId: 123,
      timestamp: Date.now()
    });

    act(() => {
      useAppStore.getState().setCurrentError(error);
    });

    // Should show error with context
    expect(screen.getByText('Processing Error')).toBeInTheDocument();
    expect(screen.getByText('Pipeline stage failed')).toBeInTheDocument();

    // In development mode, should show additional details
    if (process.env.NODE_ENV === 'development') {
      // Context information should be available for debugging
      expect(error.context).toEqual({
        stage: 'transcription',
        utteranceId: 123,
        timestamp: expect.any(Number)
      });
    }
  });
});