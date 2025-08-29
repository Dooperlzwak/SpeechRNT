import React from 'react';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { vi } from 'vitest';
import { ErrorBoundary } from '../ErrorBoundary';
import { ErrorReportingService, ErrorReportingConfig } from '../../services/ErrorReportingService';

// Mock component that throws an error
const ThrowError = ({ shouldThrow }: { shouldThrow: boolean }) => {
  if (shouldThrow) {
    throw new Error('Test error');
  }
  return <div>No error</div>;
};

// Mock ErrorReportingService
const mockErrorReportingService = {
  reportException: vi.fn(),
  reportError: vi.fn(),
  setUserContext: vi.fn(),
  clearUserContext: vi.fn(),
  getStats: vi.fn(),
  reset: vi.fn(),
} as any;

describe('ErrorBoundary', () => {
  // Suppress console.error for these tests
  const originalError = console.error;
  beforeAll(() => {
    console.error = vi.fn();
  });

  afterAll(() => {
    console.error = originalError;
  });

  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('renders children when there is no error', () => {
    render(
      <ErrorBoundary>
        <ThrowError shouldThrow={false} />
      </ErrorBoundary>
    );

    expect(screen.getByText('No error')).toBeInTheDocument();
  });

  it('renders error UI when there is an error', () => {
    render(
      <ErrorBoundary>
        <ThrowError shouldThrow={true} />
      </ErrorBoundary>
    );

    expect(screen.getByText('Something went wrong')).toBeInTheDocument();
    expect(screen.getByText(/An unexpected error occurred/)).toBeInTheDocument();
  });

  it('renders custom fallback when provided', () => {
    const customFallback = <div>Custom error message</div>;

    render(
      <ErrorBoundary fallback={customFallback}>
        <ThrowError shouldThrow={true} />
      </ErrorBoundary>
    );

    expect(screen.getByText('Custom error message')).toBeInTheDocument();
  });

  it('calls onError callback when error occurs', () => {
    const onError = vi.fn();

    render(
      <ErrorBoundary onError={onError}>
        <ThrowError shouldThrow={true} />
      </ErrorBoundary>
    );

    expect(onError).toHaveBeenCalledWith(
      expect.any(Error),
      expect.objectContaining({
        componentStack: expect.any(String),
      })
    );
  });

  it('reports error to ErrorReportingService when error occurs', async () => {
    mockErrorReportingService.reportException.mockResolvedValue();

    render(
      <ErrorBoundary errorReportingService={mockErrorReportingService}>
        <ThrowError shouldThrow={true} />
      </ErrorBoundary>
    );

    await waitFor(() => {
      expect(mockErrorReportingService.reportException).toHaveBeenCalledWith(
        expect.any(Error),
        expect.objectContaining({
          componentStack: expect.any(String),
        }),
        expect.objectContaining({
          component: 'ErrorBoundary',
          operation: 'componentDidCatch',
          additionalData: expect.objectContaining({
            componentStack: expect.any(String),
            userAgent: expect.any(String),
            url: expect.any(String),
            timestamp: expect.any(String),
          })
        })
      );
    });
  });

  it('handles error reporting failure gracefully', async () => {
    const reportingError = new Error('Reporting failed');
    mockErrorReportingService.reportException.mockRejectedValue(reportingError);

    render(
      <ErrorBoundary errorReportingService={mockErrorReportingService}>
        <ThrowError shouldThrow={true} />
      </ErrorBoundary>
    );

    await waitFor(() => {
      expect(mockErrorReportingService.reportException).toHaveBeenCalled();
    });

    // Should still show error UI even if reporting fails
    expect(screen.getByText('Something went wrong')).toBeInTheDocument();
    expect(screen.getByText(/Error reporting failed/)).toBeInTheDocument();
  });

  it('shows error reporting success message in production', async () => {
    const originalEnv = process.env.NODE_ENV;
    process.env.NODE_ENV = 'production';

    mockErrorReportingService.reportException.mockResolvedValue();

    render(
      <ErrorBoundary errorReportingService={mockErrorReportingService}>
        <ThrowError shouldThrow={true} />
      </ErrorBoundary>
    );

    await waitFor(() => {
      expect(screen.getByText(/automatically reported/)).toBeInTheDocument();
    });

    process.env.NODE_ENV = originalEnv;
  });

  it('has retry button that can be clicked', () => {
    render(
      <ErrorBoundary>
        <ThrowError shouldThrow={true} />
      </ErrorBoundary>
    );

    expect(screen.getByText('Something went wrong')).toBeInTheDocument();

    const retryButton = screen.getByText('Try Again');
    expect(retryButton).toBeInTheDocument();
    
    // Should be able to click the retry button without errors
    fireEvent.click(retryButton);
    
    // The error UI should still be there since we haven't re-rendered with different props
    expect(screen.getByText('Something went wrong')).toBeInTheDocument();
  });

  it('shows error details in development mode', async () => {
    const originalEnv = process.env.NODE_ENV;
    process.env.NODE_ENV = 'development';

    mockErrorReportingService.reportException.mockResolvedValue();

    render(
      <ErrorBoundary errorReportingService={mockErrorReportingService}>
        <ThrowError shouldThrow={true} />
      </ErrorBoundary>
    );

    expect(screen.getByText('Error Details (Development Only)')).toBeInTheDocument();
    
    // Click to expand details
    fireEvent.click(screen.getByText('Error Details (Development Only)'));
    
    await waitFor(() => {
      expect(screen.getByText('Error Type:')).toBeInTheDocument();
      expect(screen.getByText('Error Message:')).toBeInTheDocument();
      expect(screen.getByText('Stack Trace:')).toBeInTheDocument();
      expect(screen.getByText('Component Stack:')).toBeInTheDocument();
      expect(screen.getByText('Error Reporting Status:')).toBeInTheDocument();
      expect(screen.getByText('Success')).toBeInTheDocument();
    });

    process.env.NODE_ENV = originalEnv;
  });

  it('hides error details in production mode', () => {
    const originalEnv = process.env.NODE_ENV;
    process.env.NODE_ENV = 'production';

    render(
      <ErrorBoundary>
        <ThrowError shouldThrow={true} />
      </ErrorBoundary>
    );

    expect(screen.queryByText('Error Details (Development Only)')).not.toBeInTheDocument();

    process.env.NODE_ENV = originalEnv;
  });

  it('reloads page when reload button is clicked', () => {
    // Mock window.location.reload
    const mockReload = vi.fn();
    Object.defineProperty(window, 'location', {
      value: { reload: mockReload },
      writable: true,
    });

    render(
      <ErrorBoundary>
        <ThrowError shouldThrow={true} />
      </ErrorBoundary>
    );

    const reloadButton = screen.getByText('Reload Page');
    fireEvent.click(reloadButton);

    expect(mockReload).toHaveBeenCalled();
  });
});