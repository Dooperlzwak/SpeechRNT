/**
 * ErrorReportingService Usage Examples
 * 
 * This file demonstrates how to use the ErrorReportingService in different scenarios
 */

import React, { useEffect, useState } from 'react';
import { 
  ErrorReportingService, 
  createErrorReportingService,
  type ErrorContext 
} from './ErrorReportingService';

// Example 1: Basic service initialization
const initializeErrorReporting = () => {
  // Development environment
  const devService = createErrorReportingService('development', {
    enableLocalLogging: true,
    enableRemoteReporting: false
  });

  // Production environment
  const prodService = createErrorReportingService('production', {
    apiKey: 'your-api-key-here',
    endpoint: 'https://api.your-monitoring-service.com/errors',
    enableRemoteReporting: true,
    sanitizeData: true
  });

  return process.env.NODE_ENV === 'production' ? prodService : devService;
};

// Example 2: Using the service in a React component
export const ErrorReportingExample: React.FC = () => {
  const [errorService] = useState(() => initializeErrorReporting());
  const [errorCount, setErrorCount] = useState(0);

  useEffect(() => {
    // Set user context when component mounts
    errorService.setUserContext('user123', 'session456');

    return () => {
      // Clean up when component unmounts
      errorService.clearUserContext();
    };
  }, [errorService]);

  // Example 3: Reporting different types of errors
  const handleBasicError = async () => {
    try {
      // Simulate an error
      throw new Error('Something went wrong in the UI');
    } catch (error) {
      await errorService.reportError(error as Error, {
        component: 'ErrorReportingExample',
        operation: 'handleBasicError',
        additionalData: {
          buttonClicked: 'basic-error',
          timestamp: new Date().toISOString()
        }
      });
      setErrorCount(prev => prev + 1);
    }
  };

  const handleAsyncError = async () => {
    try {
      // Simulate an async operation that fails
      const response = await fetch('/api/nonexistent');
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }
    } catch (error) {
      await errorService.reportError(error as Error, {
        component: 'ErrorReportingExample',
        operation: 'handleAsyncError',
        additionalData: {
          url: '/api/nonexistent',
          method: 'GET'
        }
      });
      setErrorCount(prev => prev + 1);
    }
  };

  const handleValidationError = async () => {
    const userData = {
      email: 'invalid-email',
      password: 'secret123' // This will be sanitized in reports
    };

    try {
      // Simulate validation error
      if (!userData.email.includes('@')) {
        throw new Error('Invalid email format');
      }
    } catch (error) {
      await errorService.reportError(error as Error, {
        component: 'ErrorReportingExample',
        operation: 'handleValidationError',
        additionalData: {
          formData: userData, // Password will be sanitized
          validationField: 'email'
        }
      });
      setErrorCount(prev => prev + 1);
    }
  };

  const getServiceStats = () => {
    const stats = errorService.getStats();
    console.log('Error Service Stats:', stats);
    return stats;
  };

  return (
    <div className="p-6 max-w-2xl mx-auto">
      <h2 className="text-2xl font-bold mb-4">Error Reporting Service Examples</h2>
      
      <div className="space-y-4">
        <div className="bg-gray-100 p-4 rounded">
          <h3 className="font-semibold mb-2">Service Information</h3>
          <p>Environment: {process.env.NODE_ENV}</p>
          <p>Errors Reported: {errorCount}</p>
          <button 
            onClick={getServiceStats}
            className="mt-2 px-4 py-2 bg-blue-500 text-white rounded hover:bg-blue-600"
          >
            Log Service Stats
          </button>
        </div>

        <div className="space-y-2">
          <h3 className="font-semibold">Error Examples</h3>
          
          <button 
            onClick={handleBasicError}
            className="block w-full px-4 py-2 bg-red-500 text-white rounded hover:bg-red-600"
          >
            Trigger Basic Error
          </button>
          
          <button 
            onClick={handleAsyncError}
            className="block w-full px-4 py-2 bg-orange-500 text-white rounded hover:bg-orange-600"
          >
            Trigger Async Error
          </button>
          
          <button 
            onClick={handleValidationError}
            className="block w-full px-4 py-2 bg-yellow-500 text-white rounded hover:bg-yellow-600"
          >
            Trigger Validation Error (with sensitive data)
          </button>
        </div>

        <div className="bg-blue-50 p-4 rounded">
          <h3 className="font-semibold mb-2">What happens when you click these buttons:</h3>
          <ul className="list-disc list-inside space-y-1 text-sm">
            <li>Errors are logged to console (development mode)</li>
            <li>Error context is collected (component, operation, additional data)</li>
            <li>Sensitive data is sanitized (passwords, tokens, etc.)</li>
            <li>User and session context is included</li>
            <li>In production, errors would be sent to remote monitoring service</li>
          </ul>
        </div>
      </div>
    </div>
  );
};

// Example 4: Custom Error Boundary with ErrorReportingService
interface ErrorBoundaryWithReportingProps {
  children: React.ReactNode;
  errorService: ErrorReportingService;
}

export class ErrorBoundaryWithReporting extends React.Component<
  ErrorBoundaryWithReportingProps,
  { hasError: boolean }
> {
  constructor(props: ErrorBoundaryWithReportingProps) {
    super(props);
    this.state = { hasError: false };
  }

  static getDerivedStateFromError(): { hasError: boolean } {
    return { hasError: true };
  }

  async componentDidCatch(error: Error, errorInfo: React.ErrorInfo) {
    // Report the error using the service
    await this.props.errorService.reportException(error, errorInfo, {
      component: 'ErrorBoundaryWithReporting',
      operation: 'componentDidCatch'
    });
  }

  render() {
    if (this.state.hasError) {
      return (
        <div className="p-4 bg-red-50 border border-red-200 rounded">
          <h3 className="text-red-800 font-semibold">Something went wrong</h3>
          <p className="text-red-600">The error has been reported automatically.</p>
          <button 
            onClick={() => this.setState({ hasError: false })}
            className="mt-2 px-4 py-2 bg-red-500 text-white rounded hover:bg-red-600"
          >
            Try Again
          </button>
        </div>
      );
    }

    return this.props.children;
  }
}

// Example 5: Hook for using ErrorReportingService in functional components
export const useErrorReporting = () => {
  const [errorService] = useState(() => initializeErrorReporting());

  const reportError = async (error: Error, context?: ErrorContext) => {
    await errorService.reportError(error, context);
  };

  const reportException = async (error: Error, errorInfo: React.ErrorInfo, context?: ErrorContext) => {
    await errorService.reportException(error, errorInfo, context);
  };

  const setUserContext = (userId: string, sessionId?: string) => {
    errorService.setUserContext(userId, sessionId);
  };

  const clearUserContext = () => {
    errorService.clearUserContext();
  };

  const getStats = () => {
    return errorService.getStats();
  };

  return {
    reportError,
    reportException,
    setUserContext,
    clearUserContext,
    getStats,
    errorService
  };
};

// Example 6: Global error service instance (singleton pattern)
let globalErrorService: ErrorReportingService | null = null;

export const getGlobalErrorService = (): ErrorReportingService => {
  if (!globalErrorService) {
    globalErrorService = initializeErrorReporting();
  }
  return globalErrorService;
};

export const initializeGlobalErrorReporting = (userId?: string, sessionId?: string) => {
  const service = getGlobalErrorService();
  if (userId) {
    service.setUserContext(userId, sessionId);
  }
  return service;
};