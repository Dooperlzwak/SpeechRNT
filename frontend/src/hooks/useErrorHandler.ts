import { useCallback, useRef } from 'react';
import { useAppStore } from '../store';
import { type AppError, ErrorFactory } from '../components/ErrorNotification';
import { errorClassificationService } from '../services/ErrorClassificationService';
import { errorRecoveryService } from '../services/ErrorRecoveryService';
import { errorMetricsService } from '../services/ErrorMetricsService';
import { createErrorReportingService } from '../services/ErrorReportingService';

/**
 * Enhanced custom hook for comprehensive error handling throughout the application
 * Integrates error classification, recovery, and reporting services
 */
export function useErrorHandler() {
  const { setCurrentError, clearError } = useAppStore();
  const errorReportingService = useRef(createErrorReportingService()).current;
  const recoveryOperationsRef = useRef<Map<string, () => Promise<void>>>(new Map());

  // Set up recovery callbacks for metrics collection
  useRef(() => {
    errorRecoveryService.setCallbacks({
      onRecoverySuccess: (session) => {
        errorMetricsService.recordRecoverySession(session);
      },
      onRecoveryFailure: (session) => {
        errorMetricsService.recordRecoverySession(session);
      },
      onUserInterventionRequired: (session) => {
        errorMetricsService.recordUserInteraction(
          session.errorId,
          'manual_action',
          { reason: 'recovery_failed', attempts: session.attempts.length }
        );
      }
    });
  }).current;

  const handleError = useCallback(async (
    error: Error | AppError | string, 
    context?: Record<string, any>,
    recoveryOperation?: () => Promise<void>
  ) => {
    let sourceError: Error;
    let appError: AppError;

    // Convert input to Error and AppError
    if (typeof error === 'string') {
      sourceError = new Error(error);
    } else if (error instanceof Error) {
      sourceError = error;
    } else {
      // Already an AppError
      appError = error;
      sourceError = new Error(error.message);
    }

    // Use error classification service if we don't already have an AppError
    if (!appError!) {
      const classification = errorClassificationService.classifyError(sourceError, context);
      appError = classification.appError;
      
      // Add enhanced context from classification
      appError.context = {
        ...appError.context,
        ...context,
        classification: classification.classification,
        recoveryStrategy: classification.recoveryStrategy
      };

      // Record error metrics
      errorMetricsService.recordError(
        appError,
        classification.classification,
        context
      );
    }

    // Log error for debugging
    console.error('Error handled:', appError, { context, classification: appError.context?.classification });

    // Report error to monitoring service
    try {
      await errorReportingService.reportError(sourceError, {
        component: context?.component || 'Unknown',
        operation: context?.operation || 'unknown_operation',
        additionalData: {
          errorId: appError.id,
          errorType: appError.type,
          classification: appError.context?.classification,
          ...context
        }
      });
    } catch (reportingError) {
      console.error('Failed to report error:', reportingError);
    }

    // Set error in store
    setCurrentError(appError);

    // Store recovery operation if provided
    if (recoveryOperation) {
      recoveryOperationsRef.current.set(appError.id, recoveryOperation);
    }

    // Attempt automatic recovery if applicable
    // const classification = appError.context?.classification;
    const recoveryStrategy = appError.context?.recoveryStrategy;
    
    if (recoveryStrategy?.autoRetry && recoveryOperation) {
      try {
        const recoverySuccess = await errorRecoveryService.startRecovery(
          appError,
          recoveryOperation,
          context
        );

        if (recoverySuccess) {
          // Clear error if recovery was successful
          clearError();
          recoveryOperationsRef.current.delete(appError.id);
        }
      } catch (recoveryError) {
        console.error('Automatic recovery failed:', recoveryError);
      }
    }
  }, [setCurrentError, clearError]);

  const handleWebSocketError = useCallback(async (
    error: Event | Error | string,
    recoveryOperation?: () => Promise<void>
  ) => {
    let sourceError: Error;
    
    if (error instanceof Error) {
      sourceError = error;
    } else if (typeof error === 'string') {
      sourceError = new Error(error);
    } else {
      sourceError = new Error('WebSocket connection failed');
    }

    await handleError(sourceError, {
      component: 'WebSocketManager',
      operation: 'connection',
      errorCategory: 'connection'
    }, recoveryOperation);
  }, [handleError]);

  const handleAudioError = useCallback(async (
    error: Error | string, 
    type: 'permission' | 'capture' | 'playback',
    recoveryOperation?: () => Promise<void>
  ) => {
    const sourceError = error instanceof Error ? error : new Error(error);
    
    await handleError(sourceError, {
      component: 'AudioManager',
      operation: type,
      errorCategory: 'audio',
      audioErrorType: type
    }, recoveryOperation);
  }, [handleError]);

  const handlePipelineError = useCallback(async (
    stage: 'transcription' | 'translation' | 'synthesis', 
    error: Error | string,
    recoveryOperation?: () => Promise<void>
  ) => {
    const sourceError = error instanceof Error ? error : new Error(error);
    
    await handleError(sourceError, {
      component: 'TranslationPipeline',
      operation: stage,
      errorCategory: 'pipeline',
      pipelineStage: stage
    }, recoveryOperation);
  }, [handleError]);

  const handleConfigurationError = useCallback(async (
    error: Error | string,
    recoveryOperation?: () => Promise<void>
  ) => {
    const sourceError = error instanceof Error ? error : new Error(error);
    
    await handleError(sourceError, {
      component: 'ConfigurationSync',
      operation: 'sync',
      errorCategory: 'configuration'
    }, recoveryOperation);
  }, [handleError]);

  const dismissError = useCallback(() => {
    const { currentError } = useAppStore.getState();
    if (currentError) {
      // Record user interaction
      errorMetricsService.recordUserInteraction(
        currentError.id,
        'dismiss',
        { dismissMethod: 'manual' }
      );

      // Cancel any ongoing recovery
      errorRecoveryService.cancelRecovery(currentError.id);
      recoveryOperationsRef.current.delete(currentError.id);
    }
    clearError();
  }, [clearError]);

  const retryLastOperation = useCallback(async () => {
    const { currentError } = useAppStore.getState();
    if (!currentError) {
      return false;
    }

    // Record user interaction
    errorMetricsService.recordUserInteraction(
      currentError.id,
      'retry',
      { retryMethod: 'manual' }
    );

    const recoveryOperation = recoveryOperationsRef.current.get(currentError.id);
    if (!recoveryOperation) {
      // No recovery operation available, just clear the error
      clearError();
      return false;
    }

    try {
      const success = await errorRecoveryService.manualRetry(
        currentError.id,
        recoveryOperation
      );

      if (success) {
        clearError();
        recoveryOperationsRef.current.delete(currentError.id);
        return true;
      }

      return false;
    } catch (error) {
      console.error('Manual retry failed:', error);
      return false;
    }
  }, [clearError]);

  const getErrorGuidance = useCallback((errorType?: string) => {
    if (!errorType) {
      const { currentError } = useAppStore.getState();
      if (!currentError) return [];
      errorType = currentError.type;

      // Record that user viewed help
      errorMetricsService.recordUserInteraction(
        currentError.id,
        'help_viewed',
        { errorType }
      );
    }

    return errorClassificationService.getRecoveryGuidance(
      errorType as any,
      useAppStore.getState().currentError?.context
    );
  }, []);

  const getErrorMetrics = useCallback(() => {
    return {
      classification: errorClassificationService.getErrorStatistics(),
      recovery: errorRecoveryService.getRecoveryStatistics(),
      comprehensive: errorMetricsService.getErrorAnalytics()
    };
  }, []);

  const isRecoveryInProgress = useCallback((errorId?: string) => {
    if (!errorId) {
      const { currentError } = useAppStore.getState();
      if (!currentError) return false;
      errorId = currentError.id;
    }
    return errorRecoveryService.isRecoveryInProgress(errorId);
  }, []);

  return {
    handleError,
    handleWebSocketError,
    handleAudioError,
    handlePipelineError,
    handleConfigurationError,
    dismissError,
    retryLastOperation,
    getErrorGuidance,
    getErrorMetrics,
    isRecoveryInProgress,
  };
}

/**
 * Hook for handling async operations with automatic error handling
 */
export function useAsyncErrorHandler() {
  const { handleError } = useErrorHandler();

  const executeWithErrorHandling = useCallback(async <T>(
    operation: () => Promise<T>,
    context?: Record<string, any>
  ): Promise<T | null> => {
    try {
      return await operation();
    } catch (error) {
      handleError(error as Error, context);
      return null;
    }
  }, [handleError]);

  const executeWithRetry = useCallback(async <T>(
    operation: () => Promise<T>,
    maxRetries: number = 3,
    delay: number = 1000,
    context?: Record<string, any>
  ): Promise<T | null> => {
    let lastError: Error | null = null;

    for (let attempt = 1; attempt <= maxRetries; attempt++) {
      try {
        return await operation();
      } catch (error) {
        lastError = error as Error;
        
        if (attempt === maxRetries) {
          handleError(lastError, { ...context, attempts: attempt });
          return null;
        }

        // Wait before retrying
        await new Promise(resolve => setTimeout(resolve, delay * attempt));
      }
    }

    return null;
  }, [handleError]);

  return {
    executeWithErrorHandling,
    executeWithRetry,
  };
}

/**
 * Hook for handling component-level errors with recovery
 */
export function useComponentErrorHandler(componentName: string) {
  const { handleError } = useErrorHandler();

  const handleComponentError = useCallback((error: Error, errorInfo?: any) => {
    const appError = ErrorFactory.createUnknownError(error);
    appError.context = {
      ...appError.context,
      component: componentName,
      errorInfo,
    };
    
    handleError(appError);
  }, [handleError, componentName]);

  const wrapAsyncOperation = useCallback(async <T>(
    operation: () => Promise<T>,
    operationName: string
  ): Promise<T | null> => {
    try {
      return await operation();
    } catch (error) {
      handleComponentError(error as Error, { operation: operationName });
      return null;
    }
  }, [handleComponentError]);

  return {
    handleComponentError,
    wrapAsyncOperation,
  };
}