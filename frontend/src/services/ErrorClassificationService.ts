/**
 * ErrorClassificationService - Advanced error classification and recovery strategies
 * 
 * This service provides comprehensive error classification, recovery strategies,
 * and user-friendly error messages for the SpeechRNT application.
 */

import { type AppError, ErrorType, ErrorFactory } from '../components/ErrorNotification';

export interface ErrorRecoveryStrategy {
  errorType: ErrorType;
  maxRetries: number;
  retryDelay: number;
  backoffMultiplier: number;
  fallbackAction?: () => Promise<void>;
  userNotification: boolean;
  criticalError: boolean;
  autoRetry: boolean;
  recoveryGuidance: string[];
}

export interface ErrorClassification {
  category: 'connection' | 'audio' | 'pipeline' | 'configuration' | 'system' | 'user';
  severity: 'low' | 'medium' | 'high' | 'critical';
  impact: 'none' | 'minor' | 'major' | 'blocking';
  userActionRequired: boolean;
  systemActionRequired: boolean;
  sessionTerminating: boolean;
}

export interface ErrorMetrics {
  errorId: string;
  errorType: ErrorType;
  classification: ErrorClassification;
  timestamp: Date;
  context: Record<string, any>;
  recoveryAttempts: number;
  recoverySuccess: boolean;
  timeToRecover?: number;
  userAction?: string;
}

/**
 * Comprehensive error classification and recovery service
 */
export class ErrorClassificationService {
  private static instance: ErrorClassificationService | null = null;
  private errorMetrics: ErrorMetrics[] = [];
  private recoveryStrategies: Map<ErrorType, ErrorRecoveryStrategy> = new Map();
  private errorClassifications: Map<ErrorType, ErrorClassification> = new Map();

  private constructor() {
    this.initializeRecoveryStrategies();
    this.initializeErrorClassifications();
  }

  static getInstance(): ErrorClassificationService {
    if (!ErrorClassificationService.instance) {
      ErrorClassificationService.instance = new ErrorClassificationService();
    }
    return ErrorClassificationService.instance;
  }

  /**
   * Initialize recovery strategies for different error types
   */
  private initializeRecoveryStrategies(): void {
    // WebSocket connection errors
    this.recoveryStrategies.set(ErrorType.WEBSOCKET_CONNECTION, {
      errorType: ErrorType.WEBSOCKET_CONNECTION,
      maxRetries: 5,
      retryDelay: 2000,
      backoffMultiplier: 1.5,
      userNotification: true,
      criticalError: true,
      autoRetry: true,
      recoveryGuidance: [
        'Check your internet connection',
        'Verify the server is running',
        'Try refreshing the page',
        'Contact support if the problem persists'
      ]
    });

    this.recoveryStrategies.set(ErrorType.WEBSOCKET_RECONNECTION, {
      errorType: ErrorType.WEBSOCKET_RECONNECTION,
      maxRetries: 10,
      retryDelay: 1000,
      backoffMultiplier: 1.2,
      userNotification: false,
      criticalError: false,
      autoRetry: true,
      recoveryGuidance: [
        'Attempting to reconnect automatically',
        'Please wait while we restore your connection'
      ]
    });

    // Audio permission errors
    this.recoveryStrategies.set(ErrorType.AUDIO_PERMISSION, {
      errorType: ErrorType.AUDIO_PERMISSION,
      maxRetries: 3,
      retryDelay: 0,
      backoffMultiplier: 1,
      userNotification: true,
      criticalError: true,
      autoRetry: false,
      recoveryGuidance: [
        'Click the microphone icon in your browser\'s address bar',
        'Select "Allow" to grant microphone access',
        'Refresh the page if needed',
        'Check your browser settings if permission is blocked'
      ]
    });

    // Audio capture errors
    this.recoveryStrategies.set(ErrorType.AUDIO_CAPTURE, {
      errorType: ErrorType.AUDIO_CAPTURE,
      maxRetries: 3,
      retryDelay: 1000,
      backoffMultiplier: 1.5,
      userNotification: true,
      criticalError: false,
      autoRetry: true,
      recoveryGuidance: [
        'Check that your microphone is connected and working',
        'Try selecting a different microphone in settings',
        'Ensure no other applications are using your microphone',
        'Restart your browser if the problem persists'
      ]
    });

    // Audio playback errors
    this.recoveryStrategies.set(ErrorType.AUDIO_PLAYBACK, {
      errorType: ErrorType.AUDIO_PLAYBACK,
      maxRetries: 2,
      retryDelay: 500,
      backoffMultiplier: 2,
      userNotification: true,
      criticalError: false,
      autoRetry: true,
      recoveryGuidance: [
        'Check your speaker or headphone connections',
        'Adjust your system volume settings',
        'Try refreshing the page',
        'The translation text is still available above'
      ]
    });

    // Pipeline processing errors
    this.recoveryStrategies.set(ErrorType.TRANSCRIPTION, {
      errorType: ErrorType.TRANSCRIPTION,
      maxRetries: 2,
      retryDelay: 1000,
      backoffMultiplier: 1,
      userNotification: true,
      criticalError: false,
      autoRetry: false,
      recoveryGuidance: [
        'Try speaking more clearly',
        'Ensure you\'re speaking in the selected source language',
        'Check that your microphone is working properly',
        'Try speaking closer to your microphone'
      ]
    });

    this.recoveryStrategies.set(ErrorType.TRANSLATION, {
      errorType: ErrorType.TRANSLATION,
      maxRetries: 3,
      retryDelay: 1500,
      backoffMultiplier: 1.5,
      userNotification: true,
      criticalError: false,
      autoRetry: true,
      recoveryGuidance: [
        'The translation service is temporarily unavailable',
        'We\'ll retry automatically',
        'Your original speech was: "{originalText}"',
        'Try speaking again if the retry fails'
      ]
    });

    this.recoveryStrategies.set(ErrorType.SYNTHESIS, {
      errorType: ErrorType.SYNTHESIS,
      maxRetries: 2,
      retryDelay: 1000,
      backoffMultiplier: 2,
      userNotification: true,
      criticalError: false,
      autoRetry: true,
      recoveryGuidance: [
        'Speech synthesis failed, but translation is complete',
        'You can read the translated text above',
        'We\'ll try to generate audio again',
        'Try a different voice in settings if this continues'
      ]
    });

    // Network and system errors
    this.recoveryStrategies.set(ErrorType.NETWORK, {
      errorType: ErrorType.NETWORK,
      maxRetries: 3,
      retryDelay: 2000,
      backoffMultiplier: 2,
      userNotification: true,
      criticalError: true,
      autoRetry: true,
      recoveryGuidance: [
        'Check your internet connection',
        'Try refreshing the page',
        'Wait a moment and try again',
        'Contact support if the problem persists'
      ]
    });

    this.recoveryStrategies.set(ErrorType.UNKNOWN, {
      errorType: ErrorType.UNKNOWN,
      maxRetries: 1,
      retryDelay: 1000,
      backoffMultiplier: 1,
      userNotification: true,
      criticalError: false,
      autoRetry: false,
      recoveryGuidance: [
        'An unexpected error occurred',
        'Try refreshing the page',
        'Contact support with details of what you were doing',
        'Your session data has been preserved'
      ]
    });
  }

  /**
   * Initialize error classifications
   */
  private initializeErrorClassifications(): void {
    // Connection errors
    this.errorClassifications.set(ErrorType.WEBSOCKET_CONNECTION, {
      category: 'connection',
      severity: 'critical',
      impact: 'blocking',
      userActionRequired: true,
      systemActionRequired: true,
      sessionTerminating: true
    });

    this.errorClassifications.set(ErrorType.WEBSOCKET_RECONNECTION, {
      category: 'connection',
      severity: 'medium',
      impact: 'major',
      userActionRequired: false,
      systemActionRequired: true,
      sessionTerminating: false
    });

    // Audio errors
    this.errorClassifications.set(ErrorType.AUDIO_PERMISSION, {
      category: 'audio',
      severity: 'critical',
      impact: 'blocking',
      userActionRequired: true,
      systemActionRequired: false,
      sessionTerminating: true
    });

    this.errorClassifications.set(ErrorType.AUDIO_CAPTURE, {
      category: 'audio',
      severity: 'high',
      impact: 'major',
      userActionRequired: true,
      systemActionRequired: true,
      sessionTerminating: false
    });

    this.errorClassifications.set(ErrorType.AUDIO_PLAYBACK, {
      category: 'audio',
      severity: 'medium',
      impact: 'minor',
      userActionRequired: true,
      systemActionRequired: true,
      sessionTerminating: false
    });

    // Pipeline errors
    this.errorClassifications.set(ErrorType.TRANSCRIPTION, {
      category: 'pipeline',
      severity: 'medium',
      impact: 'minor',
      userActionRequired: true,
      systemActionRequired: false,
      sessionTerminating: false
    });

    this.errorClassifications.set(ErrorType.TRANSLATION, {
      category: 'pipeline',
      severity: 'medium',
      impact: 'major',
      userActionRequired: false,
      systemActionRequired: true,
      sessionTerminating: false
    });

    this.errorClassifications.set(ErrorType.SYNTHESIS, {
      category: 'pipeline',
      severity: 'low',
      impact: 'minor',
      userActionRequired: false,
      systemActionRequired: true,
      sessionTerminating: false
    });

    // System errors
    this.errorClassifications.set(ErrorType.NETWORK, {
      category: 'system',
      severity: 'high',
      impact: 'major',
      userActionRequired: true,
      systemActionRequired: true,
      sessionTerminating: false
    });

    this.errorClassifications.set(ErrorType.UNKNOWN, {
      category: 'system',
      severity: 'medium',
      impact: 'minor',
      userActionRequired: true,
      systemActionRequired: false,
      sessionTerminating: false
    });
  }

  /**
   * Classify an error and determine appropriate response
   */
  classifyError(error: Error, context?: Record<string, any>): {
    appError: AppError;
    classification: ErrorClassification;
    recoveryStrategy: ErrorRecoveryStrategy;
  } {
    // Determine error type based on error message and context
    const errorType = this.determineErrorType(error, context);
    
    // Create standardized AppError
    const appError = this.createAppError(error, errorType, context);
    
    // Get classification and recovery strategy
    const classification = this.errorClassifications.get(errorType) || this.getDefaultClassification();
    const recoveryStrategy = this.recoveryStrategies.get(errorType) || this.getDefaultRecoveryStrategy();

    // Record metrics
    this.recordErrorMetrics(appError, classification, context);

    return {
      appError,
      classification,
      recoveryStrategy
    };
  }

  /**
   * Determine error type from error message and context
   */
  private determineErrorType(error: Error, context?: Record<string, any>): ErrorType {
    const message = error.message.toLowerCase();
    const contextStr = JSON.stringify(context || {}).toLowerCase();

    // WebSocket errors
    if (message.includes('websocket') || message.includes('connection') || 
        contextStr.includes('websocket') || contextStr.includes('connection')) {
      if (message.includes('reconnect') || contextStr.includes('reconnect')) {
        return ErrorType.WEBSOCKET_RECONNECTION;
      }
      return ErrorType.WEBSOCKET_CONNECTION;
    }

    // Audio errors
    if (message.includes('permission') || message.includes('denied') ||
        message.includes('microphone access')) {
      return ErrorType.AUDIO_PERMISSION;
    }

    if (message.includes('audio capture') || message.includes('mediarecorder') ||
        message.includes('microphone') || contextStr.includes('audio capture')) {
      return ErrorType.AUDIO_CAPTURE;
    }

    if (message.includes('audio playback') || message.includes('audiocontext') ||
        contextStr.includes('audio playback')) {
      return ErrorType.AUDIO_PLAYBACK;
    }

    // Pipeline errors
    if (message.includes('transcription') || message.includes('stt') ||
        contextStr.includes('transcription')) {
      return ErrorType.TRANSCRIPTION;
    }

    if (message.includes('translation') || message.includes('mt') ||
        contextStr.includes('translation')) {
      return ErrorType.TRANSLATION;
    }

    if (message.includes('synthesis') || message.includes('tts') ||
        contextStr.includes('synthesis')) {
      return ErrorType.SYNTHESIS;
    }

    // Network errors
    if (message.includes('network') || message.includes('fetch') ||
        message.includes('timeout') || message.includes('offline')) {
      return ErrorType.NETWORK;
    }

    return ErrorType.UNKNOWN;
  }

  /**
   * Create standardized AppError from Error
   */
  private createAppError(error: Error, errorType: ErrorType, _context?: Record<string, any>): AppError {
    switch (errorType) {
      case ErrorType.WEBSOCKET_CONNECTION:
        return ErrorFactory.createWebSocketError(
          'Unable to connect to the translation server',
          'Please check your internet connection and try again'
        );
      
      case ErrorType.WEBSOCKET_RECONNECTION:
        return ErrorFactory.createWebSocketError(
          'Connection lost - attempting to reconnect',
          'Please wait while we restore your connection'
        );
      
      case ErrorType.AUDIO_PERMISSION:
        return ErrorFactory.createAudioPermissionError();
      
      case ErrorType.AUDIO_CAPTURE:
        return ErrorFactory.createAudioCaptureError(
          'Unable to access your microphone. Please check your microphone settings and try again.'
        );
      
      case ErrorType.AUDIO_PLAYBACK:
        return ErrorFactory.createAudioPlaybackError(
          'Unable to play translated audio. The translation text is still available.'
        );
      
      case ErrorType.TRANSCRIPTION:
        return ErrorFactory.createTranscriptionError(
          'Unable to understand your speech. Please try speaking more clearly.'
        );
      
      case ErrorType.TRANSLATION:
        return ErrorFactory.createTranslationError(
          'Translation service is temporarily unavailable. We\'ll retry automatically.'
        );
      
      case ErrorType.SYNTHESIS:
        return ErrorFactory.createSynthesisError(
          'Unable to generate speech audio. The translation text is available above.'
        );
      
      case ErrorType.NETWORK:
        return ErrorFactory.createNetworkError(
          'Network connection error. Please check your internet connection.'
        );
      
      default:
        return ErrorFactory.createUnknownError(error);
    }
  }

  /**
   * Get recovery strategy for error type
   */
  getRecoveryStrategy(errorType: ErrorType): ErrorRecoveryStrategy {
    return this.recoveryStrategies.get(errorType) || this.getDefaultRecoveryStrategy();
  }

  /**
   * Get error classification
   */
  getErrorClassification(errorType: ErrorType): ErrorClassification {
    return this.errorClassifications.get(errorType) || this.getDefaultClassification();
  }

  /**
   * Calculate retry delay with exponential backoff
   */
  calculateRetryDelay(strategy: ErrorRecoveryStrategy, attemptNumber: number): number {
    return strategy.retryDelay * Math.pow(strategy.backoffMultiplier, attemptNumber - 1);
  }

  /**
   * Get user-friendly recovery guidance
   */
  getRecoveryGuidance(errorType: ErrorType, context?: Record<string, any>): string[] {
    const strategy = this.getRecoveryStrategy(errorType);
    let guidance = [...strategy.recoveryGuidance];

    // Customize guidance based on context
    if (context?.originalText && errorType === ErrorType.TRANSLATION) {
      guidance = guidance.map(g => g.replace('{originalText}', context.originalText));
    }

    return guidance;
  }

  /**
   * Record error metrics for analysis
   */
  private recordErrorMetrics(appError: AppError, classification: ErrorClassification, context?: Record<string, any>): void {
    const metrics: ErrorMetrics = {
      errorId: appError.id,
      errorType: appError.type,
      classification,
      timestamp: appError.timestamp,
      context: context || {},
      recoveryAttempts: 0,
      recoverySuccess: false
    };

    this.errorMetrics.push(metrics);

    // Keep only last 1000 metrics to prevent memory issues
    if (this.errorMetrics.length > 1000) {
      this.errorMetrics.splice(0, this.errorMetrics.length - 1000);
    }
  }

  /**
   * Update error metrics with recovery information
   */
  updateErrorMetrics(errorId: string, recoveryAttempts: number, recoverySuccess: boolean, timeToRecover?: number, userAction?: string): void {
    const metrics = this.errorMetrics.find(m => m.errorId === errorId);
    if (metrics) {
      metrics.recoveryAttempts = recoveryAttempts;
      metrics.recoverySuccess = recoverySuccess;
      metrics.timeToRecover = timeToRecover;
      metrics.userAction = userAction;
    }
  }

  /**
   * Get error metrics for analysis
   */
  getErrorMetrics(): ErrorMetrics[] {
    return [...this.errorMetrics];
  }

  /**
   * Get error statistics
   */
  getErrorStatistics(): {
    totalErrors: number;
    errorsByType: Record<ErrorType, number>;
    errorsByCategory: Record<string, number>;
    averageRecoveryTime: number;
    recoverySuccessRate: number;
  } {
    const totalErrors = this.errorMetrics.length;
    const errorsByType: Record<ErrorType, number> = {} as Record<ErrorType, number>;
    const errorsByCategory: Record<string, number> = {};
    let totalRecoveryTime = 0;
    let recoveredErrors = 0;
    let successfulRecoveries = 0;

    this.errorMetrics.forEach(metrics => {
      // Count by type
      errorsByType[metrics.errorType] = (errorsByType[metrics.errorType] || 0) + 1;
      
      // Count by category
      const category = metrics.classification.category;
      errorsByCategory[category] = (errorsByCategory[category] || 0) + 1;
      
      // Recovery statistics
      if (metrics.timeToRecover !== undefined) {
        totalRecoveryTime += metrics.timeToRecover;
        recoveredErrors++;
        
        if (metrics.recoverySuccess) {
          successfulRecoveries++;
        }
      }
    });

    return {
      totalErrors,
      errorsByType,
      errorsByCategory,
      averageRecoveryTime: recoveredErrors > 0 ? totalRecoveryTime / recoveredErrors : 0,
      recoverySuccessRate: recoveredErrors > 0 ? successfulRecoveries / recoveredErrors : 0
    };
  }

  /**
   * Clear error metrics (useful for testing)
   */
  clearMetrics(): void {
    this.errorMetrics = [];
  }

  /**
   * Default classification for unknown error types
   */
  private getDefaultClassification(): ErrorClassification {
    return {
      category: 'system',
      severity: 'medium',
      impact: 'minor',
      userActionRequired: true,
      systemActionRequired: false,
      sessionTerminating: false
    };
  }

  /**
   * Default recovery strategy for unknown error types
   */
  private getDefaultRecoveryStrategy(): ErrorRecoveryStrategy {
    return {
      errorType: ErrorType.UNKNOWN,
      maxRetries: 1,
      retryDelay: 1000,
      backoffMultiplier: 1,
      userNotification: true,
      criticalError: false,
      autoRetry: false,
      recoveryGuidance: [
        'An unexpected error occurred',
        'Try refreshing the page',
        'Contact support if the problem persists'
      ]
    };
  }
}

/**
 * Singleton instance for global access
 */
export const errorClassificationService = ErrorClassificationService.getInstance();