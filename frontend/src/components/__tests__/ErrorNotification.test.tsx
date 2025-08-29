import React from 'react';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { ErrorNotification, ErrorFactory, ErrorType } from '../ErrorNotification';

describe('ErrorNotification', () => {
  const mockOnDismiss = jest.fn();
  const mockOnRetry = jest.fn();

  beforeEach(() => {
    jest.clearAllMocks();
    jest.useFakeTimers();
  });

  afterEach(() => {
    jest.useRealTimers();
  });

  it('renders nothing when no error is provided', () => {
    const { container } = render(
      <ErrorNotification
        error={null}
        onDismiss={mockOnDismiss}
        onRetry={mockOnRetry}
      />
    );

    expect(container.firstChild).toBeNull();
  });

  it('renders error notification with correct title and message', () => {
    const error = ErrorFactory.createWebSocketError('Connection failed');

    render(
      <ErrorNotification
        error={error}
        onDismiss={mockOnDismiss}
        onRetry={mockOnRetry}
      />
    );

    expect(screen.getByText('Connection Failed')).toBeInTheDocument();
    expect(screen.getByText('Connection failed')).toBeInTheDocument();
  });

  it('shows retry button for retryable errors', () => {
    const error = ErrorFactory.createTranslationError('Translation failed');

    render(
      <ErrorNotification
        error={error}
        onDismiss={mockOnDismiss}
        onRetry={mockOnRetry}
      />
    );

    expect(screen.getByText('Retry')).toBeInTheDocument();
  });

  it('does not show retry button for non-retryable errors', () => {
    const error = ErrorFactory.createAudioPlaybackError('Playback failed');

    render(
      <ErrorNotification
        error={error}
        onDismiss={mockOnDismiss}
        onRetry={mockOnRetry}
      />
    );

    expect(screen.queryByText('Retry')).not.toBeInTheDocument();
  });

  it('calls onDismiss when close button is clicked', () => {
    const error = ErrorFactory.createWebSocketError('Connection failed');

    render(
      <ErrorNotification
        error={error}
        onDismiss={mockOnDismiss}
        onRetry={mockOnRetry}
      />
    );

    const closeButton = screen.getByRole('button', { name: /close/i });
    fireEvent.click(closeButton);

    expect(mockOnDismiss).toHaveBeenCalled();
  });

  it('calls onRetry when retry button is clicked', () => {
    const error = ErrorFactory.createTranslationError('Translation failed');

    render(
      <ErrorNotification
        error={error}
        onDismiss={mockOnDismiss}
        onRetry={mockOnRetry}
      />
    );

    const retryButton = screen.getByText('Retry');
    fireEvent.click(retryButton);

    expect(mockOnRetry).toHaveBeenCalled();
  });

  it('auto-hides recoverable errors after timeout', async () => {
    const error = ErrorFactory.createTranscriptionError('Transcription failed');

    render(
      <ErrorNotification
        error={error}
        onDismiss={mockOnDismiss}
        onRetry={mockOnRetry}
      />
    );

    expect(screen.getByText('Transcription Error')).toBeInTheDocument();

    // Fast-forward time
    jest.advanceTimersByTime(5000);

    await waitFor(() => {
      expect(mockOnDismiss).toHaveBeenCalled();
    });
  });

  it('does not auto-hide critical connection errors', () => {
    const error = ErrorFactory.createWebSocketError('Connection failed');

    render(
      <ErrorNotification
        error={error}
        onDismiss={mockOnDismiss}
        onRetry={mockOnRetry}
      />
    );

    expect(screen.getByText('Connection Failed')).toBeInTheDocument();

    // Fast-forward time
    jest.advanceTimersByTime(10000);

    expect(mockOnDismiss).not.toHaveBeenCalled();
  });

  it('shows grant permission button for audio permission errors', () => {
    const error = ErrorFactory.createAudioPermissionError();

    render(
      <ErrorNotification
        error={error}
        onDismiss={mockOnDismiss}
        onRetry={mockOnRetry}
      />
    );

    expect(screen.getByText('Grant Permission')).toBeInTheDocument();
  });

  it('displays error details when provided', () => {
    const error = ErrorFactory.createNetworkError('Network timeout');

    render(
      <ErrorNotification
        error={error}
        onDismiss={mockOnDismiss}
        onRetry={mockOnRetry}
      />
    );

    expect(screen.getByText('Network timeout')).toBeInTheDocument();
  });
});

describe('ErrorFactory', () => {
  it('creates WebSocket error with correct properties', () => {
    const error = ErrorFactory.createWebSocketError('Connection failed', 'Additional details');

    expect(error.type).toBe(ErrorType.WEBSOCKET_CONNECTION);
    expect(error.message).toBe('Connection failed');
    expect(error.details).toBe('Additional details');
    expect(error.recoverable).toBe(true);
    expect(error.retryable).toBe(true);
    expect(error.id).toBeDefined();
    expect(error.timestamp).toBeInstanceOf(Date);
  });

  it('creates audio permission error with correct properties', () => {
    const error = ErrorFactory.createAudioPermissionError();

    expect(error.type).toBe(ErrorType.AUDIO_PERMISSION);
    expect(error.message).toBe('Microphone access is required for speech translation.');
    expect(error.recoverable).toBe(true);
    expect(error.retryable).toBe(true);
  });

  it('creates transcription error with correct properties', () => {
    const error = ErrorFactory.createTranscriptionError('Custom message');

    expect(error.type).toBe(ErrorType.TRANSCRIPTION);
    expect(error.message).toBe('Failed to transcribe speech.');
    expect(error.details).toBe('Custom message');
    expect(error.recoverable).toBe(true);
    expect(error.retryable).toBe(false);
  });

  it('creates unknown error from Error object', () => {
    const originalError = new Error('Something went wrong');
    const error = ErrorFactory.createUnknownError(originalError);

    expect(error.type).toBe(ErrorType.UNKNOWN);
    expect(error.message).toBe('An unexpected error occurred.');
    expect(error.details).toBe('Something went wrong');
    expect(error.context?.stack).toBeDefined();
    expect(error.context?.name).toBe('Error');
  });

  it('generates unique error IDs', () => {
    const error1 = ErrorFactory.createWebSocketError('Test 1');
    const error2 = ErrorFactory.createWebSocketError('Test 2');

    expect(error1.id).not.toBe(error2.id);
    expect(error1.id).toMatch(/^error_\d+_[a-z0-9]+$/);
  });
});