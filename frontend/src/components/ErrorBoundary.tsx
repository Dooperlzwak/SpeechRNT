import React, { Component, type ErrorInfo, type ReactNode } from 'react';
import { AlertTriangle, RefreshCw, ChevronDown, ChevronRight } from 'lucide-react';
import { Button } from './ui/button';
import { Alert, AlertDescription, AlertTitle } from './ui/alert';
import { ErrorReportingService, type ErrorContext, createErrorReportingService } from '../services/ErrorReportingService';
import { errorClassificationService } from '../services/ErrorClassificationService';
import { errorMetricsService } from '../services/ErrorMetricsService';

interface Props {
  children: ReactNode;
  fallback?: ReactNode;
  onError?: (error: Error, errorInfo: ErrorInfo) => void;
  errorReportingService?: ErrorReportingService;
}

interface State {
  hasError: boolean;
  error: Error | null;
  errorInfo: ErrorInfo | null;
  errorReportingFailed: boolean;
  showGuidance: boolean;
  errorClassification: any;
  recoveryGuidance: string[];
}

/**
 * ErrorBoundary - Catches JavaScript errors anywhere in the child component tree
 * and displays a fallback UI instead of crashing the entire application
 */
export class ErrorBoundary extends Component<Props, State> {
  private errorReportingService: ErrorReportingService;

  constructor(props: Props) {
    super(props);
    this.state = {
      hasError: false,
      error: null,
      errorInfo: null,
      errorReportingFailed: false,
      showGuidance: false,
      errorClassification: null,
      recoveryGuidance: [],
    };

    // Initialize error reporting service
    this.errorReportingService = props.errorReportingService || 
      createErrorReportingService(
        process.env.NODE_ENV === 'production' ? 'production' : 'development'
      );
  }

  static getDerivedStateFromError(error: Error): Partial<State> {
    // Classify the error for enhanced handling
    const classification = errorClassificationService.classifyError(error, {
      component: 'ErrorBoundary',
      operation: 'componentDidCatch'
    });

    // Update state so the next render will show the fallback UI
    return {
      hasError: true,
      error,
      errorInfo: null,
      errorReportingFailed: false,
      showGuidance: false,
      errorClassification: classification,
      recoveryGuidance: classification.recoveryStrategy.recoveryGuidance,
    };
  }

  componentDidCatch(error: Error, errorInfo: ErrorInfo) {
    // Log error details
    console.error('ErrorBoundary caught an error:', error, errorInfo);
    
    // Get enhanced error classification
    const classification = errorClassificationService.classifyError(error, {
      component: 'ErrorBoundary',
      operation: 'componentDidCatch',
      componentStack: errorInfo.componentStack
    });

    // Record error metrics
    errorMetricsService.recordError(
      classification.appError,
      classification.classification,
      {
        component: 'ErrorBoundary',
        operation: 'componentDidCatch',
        componentStack: errorInfo.componentStack,
        userAgent: navigator.userAgent,
        url: window.location.href
      }
    );
    
    // Update state with error info and classification
    this.setState({
      error,
      errorInfo,
      errorClassification: classification,
      recoveryGuidance: classification.recoveryStrategy.recoveryGuidance,
    });

    // Call optional error handler
    if (this.props.onError) {
      this.props.onError(error, errorInfo);
    }

    // Report error to monitoring service
    this.reportError(error, errorInfo);
  }

  private reportError = async (error: Error, errorInfo: ErrorInfo) => {
    try {
      // Collect error context
      const context: ErrorContext = {
        component: 'ErrorBoundary',
        operation: 'componentDidCatch',
        additionalData: {
          componentStack: errorInfo.componentStack,
          userAgent: navigator.userAgent,
          url: window.location.href,
          timestamp: new Date().toISOString(),
          errorBoundaryProps: {
            hasCustomFallback: !!this.props.fallback,
            hasCustomErrorHandler: !!this.props.onError,
          }
        }
      };

      // Report error using ErrorReportingService
      await this.errorReportingService.reportException(error, errorInfo, context);
      
      // Log success in development
      if (process.env.NODE_ENV === 'development') {
        console.log('[ErrorBoundary] Error reported successfully to ErrorReportingService');
      }
    } catch (reportingError) {
      // Graceful fallback when error reporting fails
      console.error('[ErrorBoundary] Failed to report error to ErrorReportingService:', reportingError);
      
      // Update state to indicate reporting failure
      this.setState({ errorReportingFailed: true });
      
      // Fallback to basic console logging
      const fallbackReport = {
        message: error.message,
        stack: error.stack,
        componentStack: errorInfo.componentStack,
        timestamp: new Date().toISOString(),
        userAgent: navigator.userAgent,
        url: window.location.href,
        reportingError: reportingError instanceof Error ? reportingError.message : String(reportingError),
      };
      
      console.error('[ErrorBoundary] Fallback Error Report:', fallbackReport);
    }
  };

  private handleRetry = () => {
    // Record user interaction
    if (this.state.errorClassification?.appError) {
      errorMetricsService.recordUserInteraction(
        this.state.errorClassification.appError.id,
        'retry',
        { retryMethod: 'error_boundary' }
      );
    }

    // Reset error state to retry rendering
    this.setState({
      hasError: false,
      error: null,
      errorInfo: null,
      errorReportingFailed: false,
      showGuidance: false,
      errorClassification: null,
      recoveryGuidance: [],
    });
  };

  private handleShowGuidance = () => {
    // Record user interaction
    if (this.state.errorClassification?.appError) {
      errorMetricsService.recordUserInteraction(
        this.state.errorClassification.appError.id,
        'help_viewed',
        { helpLocation: 'error_boundary' }
      );
    }

    this.setState({ showGuidance: !this.state.showGuidance });
  };

  private handleReload = () => {
    // Reload the entire page as a last resort
    window.location.reload();
  };

  render() {
    if (this.state.hasError) {
      // Custom fallback UI
      if (this.props.fallback) {
        return this.props.fallback;
      }

      // Enhanced error UI with classification and guidance
      const classification = this.state.errorClassification?.classification;
      const isCritical = classification?.severity === 'critical';
      
      return (
        <div className="min-h-screen flex items-center justify-center p-4">
          <div className="max-w-md w-full">
            <Alert variant={isCritical ? "destructive" : "default"} className="mb-4">
              <AlertTriangle className="h-4 w-4" />
              <AlertTitle className="flex items-center justify-between">
                <div className="flex items-center gap-2">
                  Something went wrong
                  {isCritical && (
                    <span className="text-xs bg-red-100 text-red-800 px-1.5 py-0.5 rounded">
                      Critical
                    </span>
                  )}
                </div>
              </AlertTitle>
              <AlertDescription>
                <div className="space-y-3">
                  <p>
                    An unexpected error occurred. 
                    {classification?.userActionRequired 
                      ? ' Please follow the guidance below to resolve the issue.'
                      : ' We\'re working to fix this automatically.'
                    }
                  </p>
                  
                  {this.state.errorReportingFailed && (
                    <div className="text-sm text-orange-600">
                      Note: Error reporting failed, but the error has been logged locally.
                    </div>
                  )}
                  
                  {!this.state.errorReportingFailed && process.env.NODE_ENV === 'production' && (
                    <div className="text-sm text-gray-600">
                      This error has been automatically reported to help us improve the application.
                    </div>
                  )}

                  {/* Recovery Guidance */}
                  {this.state.recoveryGuidance.length > 0 && this.state.showGuidance && (
                    <div className="border-t pt-3 mt-3 animate-in slide-in-from-top-2 duration-200">
                      <h4 className="text-sm font-medium mb-2 flex items-center gap-1">
                        <ChevronDown className="h-3 w-3" />
                        How to fix this:
                      </h4>
                      <ol className="text-sm space-y-1 list-decimal list-inside opacity-90">
                        {this.state.recoveryGuidance.map((step, index) => (
                          <li key={index}>{step}</li>
                        ))}
                      </ol>
                    </div>
                  )}
                </div>
              </AlertDescription>
            </Alert>

            <div className="space-y-2">
              <div className="flex gap-2">
                <Button 
                  onClick={this.handleRetry} 
                  className="flex-1"
                  variant="outline"
                >
                  <RefreshCw className="h-4 w-4 mr-2" />
                  Try Again
                </Button>
                
                {this.state.recoveryGuidance.length > 0 && (
                  <Button
                    onClick={this.handleShowGuidance}
                    variant="ghost"
                    className="px-3"
                  >
                    {this.state.showGuidance ? (
                      <ChevronDown className="h-4 w-4" />
                    ) : (
                      <ChevronRight className="h-4 w-4" />
                    )}
                  </Button>
                )}
              </div>
              
              <Button 
                onClick={this.handleReload} 
                className="w-full"
                variant="secondary"
              >
                Reload Page
              </Button>
            </div>

            {/* Show error details in development */}
            {process.env.NODE_ENV === 'development' && this.state.error && (
              <details className="mt-4 p-4 bg-gray-100 rounded-md text-sm">
                <summary className="cursor-pointer font-medium">
                  Error Details (Development Only)
                </summary>
                <div className="mt-2 space-y-3">
                  <div>
                    <strong>Error Type:</strong> {this.state.error.name}
                  </div>
                  <div>
                    <strong>Error Message:</strong> {this.state.error.message}
                  </div>
                  {this.state.error.stack && (
                    <div>
                      <strong>Stack Trace:</strong>
                      <pre className="whitespace-pre-wrap text-xs mt-1 p-2 bg-gray-200 rounded overflow-x-auto">
                        {this.state.error.stack}
                      </pre>
                    </div>
                  )}
                  {this.state.errorInfo?.componentStack && (
                    <div>
                      <strong>Component Stack:</strong>
                      <pre className="whitespace-pre-wrap text-xs mt-1 p-2 bg-gray-200 rounded overflow-x-auto">
                        {this.state.errorInfo.componentStack}
                      </pre>
                    </div>
                  )}
                  <div>
                    <strong>Error Reporting Status:</strong>{' '}
                    <span className={this.state.errorReportingFailed ? 'text-red-600' : 'text-green-600'}>
                      {this.state.errorReportingFailed ? 'Failed (logged locally)' : 'Success'}
                    </span>
                  </div>
                  <div>
                    <strong>Timestamp:</strong> {new Date().toISOString()}
                  </div>
                  <div>
                    <strong>User Agent:</strong> {navigator.userAgent}
                  </div>
                  <div>
                    <strong>URL:</strong> {window.location.href}
                  </div>
                </div>
              </details>
            )}
          </div>
        </div>
      );
    }

    return this.props.children;
  }
}

/**
 * Higher-order component to wrap components with error boundary
 */
export function withErrorBoundary<P extends object>(
  Component: React.ComponentType<P>,
  fallback?: ReactNode,
  onError?: (error: Error, errorInfo: ErrorInfo) => void,
  errorReportingService?: ErrorReportingService
) {
  const WrappedComponent = (props: P) => (
    <ErrorBoundary 
      fallback={fallback} 
      onError={onError}
      errorReportingService={errorReportingService}
    >
      <Component {...props} />
    </ErrorBoundary>
  );

  WrappedComponent.displayName = `withErrorBoundary(${Component.displayName || Component.name})`;
  
  return WrappedComponent;
}