import React from 'react';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { vi } from 'vitest';
import { ErrorRecoveryDialog, useErrorRecoveryDialog } from '../ErrorRecoveryDialog';
import { ErrorFactory, ErrorType } from '../ErrorNotification';

// Mock fetch for network tests
global.fetch = vi.fn();

// Mock MediaDevices API
Object.defineProperty(navigator, 'mediaDevices', {
  writable: true,
  value: {
    getUserMedia: vi.fn(),
    enumerateDevices: vi.fn(),
  },
});

// Mock AudioContext
global.AudioContext = vi.fn().mockImplementation(() => ({
  createMediaStreamSource: vi.fn().mockReturnValue({
    connect: vi.fn(),
  }),
  createAnalyser: vi.fn().mockReturnValue({
    frequencyBinCount: 1024,
    getByteFrequencyData: vi.fn().mockImplementation((array) => {
      // Simulate some audio data
      array[0] = 50;
    }),
  }),
  close: vi.fn(),
}));

describe('ErrorRecoveryDialog', () => {
  const mockOnClose = vi.fn();
  const mockOnRetry = vi.fn();
  const mockOnSkip = vi.fn();
  const mockOnReportIssue = vi.fn();

  beforeEach(() => {
    vi.clearAllMocks();
    (fetch as any).mockResolvedValue({ ok: true });
    (navigator.mediaDevices.getUserMedia as any).mockResolvedValue({
      getTracks: () => [{ stop: vi.fn() }],
    });
    (navigator.mediaDevices.enumerateDevices as any).mockResolvedValue([
      { kind: 'audioinput', deviceId: 'default' },
    ]);
  });

  it('renders nothing when not open', () => {
    const error = ErrorFactory.createWebSocketError('Connection failed');
    
    const { container } = render(
      <ErrorRecoveryDialog
        error={error}
        isOpen={false}
        onClose={mockOnClose}
        onRetry={mockOnRetry}
      />
    );

    expect(container.firstChild).toBeNull();
  });

  it('renders error recovery dialog for WebSocket errors', () => {
    const error = ErrorFactory.createWebSocketError('Connection failed');
    
    render(
      <ErrorRecoveryDialog
        error={error}
        isOpen={true}
        onClose={mockOnClose}
        onRetry={mockOnRetry}
      />
    );

    expect(screen.getByText('Connection Problem')).toBeInTheDocument();
    expect(screen.getByText(/connection to the server was lost/i)).toBeInTheDocument();
    expect(screen.getByText('Check Network Connection')).toBeInTheDocument();
    expect(screen.getByText('Reconnect to Server')).toBeInTheDocument();
  });

  it('renders error recovery dialog for audio permission errors', () => {
    const error = ErrorFactory.createAudioPermissionError();
    
    render(
      <ErrorRecoveryDialog
        error={error}
        isOpen={true}
        onClose={mockOnClose}
        onRetry={mockOnRetry}
      />
    );

    expect(screen.getByText('Microphone Access Required')).toBeInTheDocument();
    expect(screen.getByText('Request Microphone Permission')).toBeInTheDocument();
    expect(screen.getByText('Test Microphone')).toBeInTheDocument();
  });

  it('executes recovery steps when Start Recovery is clicked', async () => {
    const error = ErrorFactory.createWebSocketError('Connection failed');
    
    render(
      <ErrorRecoveryDialog
        error={error}
        isOpen={true}
        onClose={mockOnClose}
        onRetry={mockOnRetry}
      />
    );

    const startButton = screen.getByText('Start Recovery');
    fireEvent.click(startButton);

    // Should show progress
    expect(screen.getByText('Recovery Progress')).toBeInTheDocument();
    
    // Wait for recovery steps to complete
    await waitFor(() => {
      expect(fetch).toHaveBeenCalledWith('/api/health', expect.any(Object));
    });

    await waitFor(() => {
      expect(mockOnRetry).toHaveBeenCalled();
    });
  });

  it('shows success message when all steps complete', async () => {
    const error = ErrorFactory.createWebSocketError('Connection failed');
    
    render(
      <ErrorRecoveryDialog
        error={error}
        isOpen={true}
        onClose={mockOnClose}
        onRetry={mockOnRetry}
      />
    );

    const startButton = screen.getByText('Start Recovery');
    fireEvent.click(startButton);

    await waitFor(() => {
      expect(screen.getByText(/recovery completed successfully/i)).toBeInTheDocument();
    });
  });

  it('shows failure message when steps fail', async () => {
    (fetch as any).mockRejectedValue(new Error('Network error'));
    
    const error = ErrorFactory.createWebSocketError('Connection failed');
    
    render(
      <ErrorRecoveryDialog
        error={error}
        isOpen={true}
        onClose={mockOnClose}
        onRetry={mockOnRetry}
      />
    );

    const startButton = screen.getByText('Start Recovery');
    fireEvent.click(startButton);

    await waitFor(() => {
      expect(screen.getByText(/some recovery steps failed/i)).toBeInTheDocument();
    });
  });

  it('handles audio permission recovery steps', async () => {
    const error = ErrorFactory.createAudioPermissionError();
    
    render(
      <ErrorRecoveryDialog
        error={error}
        isOpen={true}
        onClose={mockOnClose}
        onRetry={mockOnRetry}
      />
    );

    const startButton = screen.getByText('Start Recovery');
    fireEvent.click(startButton);

    await waitFor(() => {
      expect(navigator.mediaDevices.getUserMedia).toHaveBeenCalledWith({ audio: true });
    });
  });

  it('handles microphone permission denied', async () => {
    (navigator.mediaDevices.getUserMedia as any).mockRejectedValue(
      new Error('Permission denied')
    );
    
    const error = ErrorFactory.createAudioPermissionError();
    
    render(
      <ErrorRecoveryDialog
        error={error}
        isOpen={true}
        onClose={mockOnClose}
        onRetry={mockOnRetry}
      />
    );

    const startButton = screen.getByText('Start Recovery');
    fireEvent.click(startButton);

    await waitFor(() => {
      expect(screen.getByText(/some recovery steps failed/i)).toBeInTheDocument();
    });
  });

  it('calls onSkip when skip button is clicked', async () => {
    (fetch as any).mockRejectedValue(new Error('Network error'));
    
    const error = ErrorFactory.createWebSocketError('Connection failed');
    
    render(
      <ErrorRecoveryDialog
        error={error}
        isOpen={true}
        onClose={mockOnClose}
        onRetry={mockOnRetry}
        onSkip={mockOnSkip}
      />
    );

    const startButton = screen.getByText('Start Recovery');
    fireEvent.click(startButton);

    await waitFor(() => {
      expect(screen.getByText('Skip & Continue')).toBeInTheDocument();
    });

    const skipButton = screen.getByText('Skip & Continue');
    fireEvent.click(skipButton);

    expect(mockOnSkip).toHaveBeenCalled();
  });

  it('calls onReportIssue when report button is clicked', async () => {
    (fetch as any).mockRejectedValue(new Error('Network error'));
    
    const error = ErrorFactory.createWebSocketError('Connection failed');
    
    render(
      <ErrorRecoveryDialog
        error={error}
        isOpen={true}
        onClose={mockOnClose}
        onRetry={mockOnRetry}
        onReportIssue={mockOnReportIssue}
      />
    );

    const startButton = screen.getByText('Start Recovery');
    fireEvent.click(startButton);

    await waitFor(() => {
      expect(screen.getByText('Report Issue')).toBeInTheDocument();
    });

    const reportButton = screen.getByText('Report Issue');
    fireEvent.click(reportButton);

    expect(mockOnReportIssue).toHaveBeenCalled();
  });

  it('executes individual recovery steps', async () => {
    const error = ErrorFactory.createWebSocketError('Connection failed');
    
    render(
      <ErrorRecoveryDialog
        error={error}
        isOpen={true}
        onClose={mockOnClose}
        onRetry={mockOnRetry}
      />
    );

    const tryStepButton = screen.getByText('Try This Step');
    fireEvent.click(tryStepButton);

    await waitFor(() => {
      expect(fetch).toHaveBeenCalledWith('/api/health', expect.any(Object));
    });
  });

  it('closes dialog when Done button is clicked after successful recovery', async () => {
    const error = ErrorFactory.createWebSocketError('Connection failed');
    
    render(
      <ErrorRecoveryDialog
        error={error}
        isOpen={true}
        onClose={mockOnClose}
        onRetry={mockOnRetry}
      />
    );

    const startButton = screen.getByText('Start Recovery');
    fireEvent.click(startButton);

    await waitFor(() => {
      expect(screen.getByText('Done')).toBeInTheDocument();
    });

    const doneButton = screen.getByText('Done');
    fireEvent.click(doneButton);

    expect(mockOnClose).toHaveBeenCalled();
  });
});

describe('useErrorRecoveryDialog', () => {
  it('manages dialog state correctly', () => {
    const TestComponent = () => {
      const { isOpen, currentError, showRecoveryDialog, hideRecoveryDialog } = useErrorRecoveryDialog();
      
      return (
        <div>
          <div data-testid="is-open">{isOpen.toString()}</div>
          <div data-testid="has-error">{(currentError !== null).toString()}</div>
          <button onClick={() => showRecoveryDialog(ErrorFactory.createWebSocketError('test'))}>
            Show Dialog
          </button>
          <button onClick={hideRecoveryDialog}>Hide Dialog</button>
        </div>
      );
    };

    render(<TestComponent />);

    expect(screen.getByTestId('is-open')).toHaveTextContent('false');
    expect(screen.getByTestId('has-error')).toHaveTextContent('false');

    fireEvent.click(screen.getByText('Show Dialog'));

    expect(screen.getByTestId('is-open')).toHaveTextContent('true');
    expect(screen.getByTestId('has-error')).toHaveTextContent('true');

    fireEvent.click(screen.getByText('Hide Dialog'));

    expect(screen.getByTestId('is-open')).toHaveTextContent('false');
    expect(screen.getByTestId('has-error')).toHaveTextContent('false');
  });
});