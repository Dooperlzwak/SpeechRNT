/**
 * ErrorRecoveryService - Automated error recovery and retry mechanisms
 * 
 * This service handles automatic error recovery, retry logic with exponential backoff,
 * and coordination with other services for comprehensive error handling.
 */

import { errorClassificationService, type ErrorRecoveryStrategy } from './ErrorClassificationService';
import { type AppError, ErrorType } from '../components/ErrorNotification';
import { createErrorReportingService } from './ErrorReportingService';

export interface RecoveryAttempt {
  attemptNumber: number;
  timestamp: Date;
  strategy: ErrorRecoveryStrategy;
  success: boolean;
  error?: Error;
  timeToComplete?: number;
}

export interface RecoverySession {
  errorId: string;
  errorType: ErrorType;
  startTime: Date;
  endTime?: Date;
  attempts: RecoveryAttempt[];
  finalSuccess: boolean;
  userIntervention: boolean;
  fallbackUsed: boolean;
}

export interface RecoveryCallbacks {
  onRetryAttempt?: (attempt: RecoveryAttempt) => void;
  onRecoverySuccess?: (session: RecoverySession) => void;
  onRecoveryFailure?: (session: RecoverySession) => void;
  onFallbackActivated?: (session: RecoverySession) => void;
  onUserInterventionRequired?: (session: RecoverySession) => void;
}

/**
 * Comprehensive error recovery service with automatic retry and fallback mechanisms
 */
export class ErrorRecoveryService {
  private static instance: ErrorRecoveryService | null = null;
  private activeSessions: Map<string, RecoverySession> = new Map();
  private retryTimers: Map<string, number> = new Map();
  private callbacks: RecoveryCallbacks = {};
  private errorReportingService = createErrorReportingService();

  private constructor() {}

  static getInstance(): ErrorRecoveryService {
    if (!ErrorRecoveryService.instance) {
      ErrorRecoveryService.instance = new ErrorRecoveryService();
    }
    return ErrorRecoveryService.instance;
  }

  /**
   * Set recovery callbacks for monitoring recovery progress
   */
  setCallbacks(callbacks: RecoveryCallbacks): void {
    this.callbacks = { ...this.callbacks, ...callbacks };
  }

  /**
   * Start recovery process for an error
   */
  async startRecovery(
    appError: AppError,
    recoveryOperation: () => Promise<void>,
    context?: Record<string, any>
  ): Promise<boolean> {
    const classification = errorClassificationService.classifyError(
      new Error(appError.message),
      context
    );

    const strategy = classification.recoveryStrategy;
    
    // Don't auto-retry if strategy doesn't allow it
    if (!strategy.autoRetry) {
      return false;
    }

    // Create recovery session
    const session: RecoverySession = {
      errorId: appError.id,
      errorType: appError.type,
      startTime: new Date(),
      attempts: [],
      finalSuccess: false,
      userIntervention: false,
      fallbackUsed: false
    };

    this.activeSessions.set(appError.id, session);

    try {
      const success = await this.executeRecoveryWithRetry(
        session,
        strategy,
        recoveryOperation,
        context
      );

      session.finalSuccess = success;
      session.endTime = new Date();

      // Update error metrics
      const timeToRecover = session.endTime.getTime() - session.startTime.getTime();
      errorClassificationService.updateErrorMetrics(
        appError.id,
        session.attempts.length,
        success,
        timeToRecover,
        session.userIntervention ? 'user_intervention' : 'automatic'
      );

      // Report recovery result
      if (success) {
        this.callbacks.onRecoverySuccess?.(session);
        await this.errorReportingService.reportError(
          new Error(`Recovery successful for ${appError.type}`),
          {
            component: 'ErrorRecoveryService',
            operation: 'recovery_success',
            additionalData: {
              errorId: appError.id,
              attempts: session.attempts.length,
              timeToRecover
            }
          }
        );
      } else {
        this.callbacks.onRecoveryFailure?.(session);
        await this.errorReportingService.reportError(
          new Error(`Recovery failed for ${appError.type} after ${session.attempts.length} attempts`),
          {
            component: 'ErrorRecoveryService',
            operation: 'recovery_failure',
            additionalData: {
              errorId: appError.id,
              attempts: session.attempts.length,
              lastError: session.attempts[session.attempts.length - 1]?.error?.message
            }
          }
        );
      }

      return success;
    } catch (error) {
      session.finalSuccess = false;
      session.endTime = new Date();
      
      await this.errorReportingService.reportError(
        error as Error,
        {
          component: 'ErrorRecoveryService',
          operation: 'recovery_exception',
          additionalData: { errorId: appError.id }
        }
      );
      
      return false;
    } finally {
      this.activeSessions.delete(appError.id);
      this.clearRetryTimer(appError.id);
    }
  }

  /**
   * Execute recovery with retry logic and exponential backoff
   */
  private async executeRecoveryWithRetry(
    session: RecoverySession,
    strategy: ErrorRecoveryStrategy,
    recoveryOperation: () => Promise<void>,
    _context?: Record<string, any>
  ): Promise<boolean> {
    for (let attemptNumber = 1; attemptNumber <= strategy.maxRetries; attemptNumber++) {
      const attempt: RecoveryAttempt = {
        attemptNumber,
        timestamp: new Date(),
        strategy,
        success: false
      };

      const startTime = Date.now();

      try {
        // Calculate delay with exponential backoff
        if (attemptNumber > 1) {
          const delay = errorClassificationService.calculateRetryDelay(strategy, attemptNumber);
          await this.delay(delay);
        }

        // Execute recovery operation
        await recoveryOperation();

        // Success!
        attempt.success = true;
        attempt.timeToComplete = Date.now() - startTime;
        session.attempts.push(attempt);

        this.callbacks.onRetryAttempt?.(attempt);
        return true;

      } catch (error) {
        attempt.success = false;
        attempt.error = error as Error;
        attempt.timeToComplete = Date.now() - startTime;
        session.attempts.push(attempt);

        this.callbacks.onRetryAttempt?.(attempt);

        // If this was the last attempt, try fallback
        if (attemptNumber === strategy.maxRetries) {
          if (strategy.fallbackAction) {
            try {
              await strategy.fallbackAction();
              session.fallbackUsed = true;
              this.callbacks.onFallbackActivated?.(session);
              return true;
            } catch (fallbackError) {
              console.error('Fallback action failed:', fallbackError);
            }
          }

          // If user intervention is required
          if (strategy.userNotification && strategy.criticalError) {
            session.userIntervention = true;
            this.callbacks.onUserInterventionRequired?.(session);
          }
        }
      }
    }

    return false;
  }

  /**
   * Cancel recovery for a specific error
   */
  cancelRecovery(errorId: string): void {
    const session = this.activeSessions.get(errorId);
    if (session) {
      session.endTime = new Date();
      session.finalSuccess = false;
      this.activeSessions.delete(errorId);
    }

    this.clearRetryTimer(errorId);
  }

  /**
   * Cancel all active recovery sessions
   */
  cancelAllRecoveries(): void {
    const activeErrorIds = Array.from(this.activeSessions.keys());
    activeErrorIds.forEach(errorId => this.cancelRecovery(errorId));
  }

  /**
   * Get active recovery sessions
   */
  getActiveSessions(): RecoverySession[] {
    return Array.from(this.activeSessions.values());
  }

  /**
   * Get recovery session by error ID
   */
  getRecoverySession(errorId: string): RecoverySession | null {
    return this.activeSessions.get(errorId) || null;
  }

  /**
   * Check if recovery is in progress for an error
   */
  isRecoveryInProgress(errorId: string): boolean {
    return this.activeSessions.has(errorId);
  }

  /**
   * Manual retry for user-initiated recovery
   */
  async manualRetry(
    errorId: string,
    recoveryOperation: () => Promise<void>
  ): Promise<boolean> {
    const session = this.activeSessions.get(errorId);
    if (!session) {
      return false;
    }

    session.userIntervention = true;

    const attempt: RecoveryAttempt = {
      attemptNumber: session.attempts.length + 1,
      timestamp: new Date(),
      strategy: session.attempts[0]?.strategy || errorClassificationService.getRecoveryStrategy(session.errorType),
      success: false
    };

    const startTime = Date.now();

    try {
      await recoveryOperation();
      
      attempt.success = true;
      attempt.timeToComplete = Date.now() - startTime;
      session.attempts.push(attempt);
      session.finalSuccess = true;
      session.endTime = new Date();

      this.callbacks.onRetryAttempt?.(attempt);
      this.callbacks.onRecoverySuccess?.(session);

      // Update metrics
      const timeToRecover = session.endTime.getTime() - session.startTime.getTime();
      errorClassificationService.updateErrorMetrics(
        errorId,
        session.attempts.length,
        true,
        timeToRecover,
        'manual_retry'
      );

      this.activeSessions.delete(errorId);
      return true;

    } catch (error) {
      attempt.success = false;
      attempt.error = error as Error;
      attempt.timeToComplete = Date.now() - startTime;
      session.attempts.push(attempt);

      this.callbacks.onRetryAttempt?.(attempt);
      return false;
    }
  }

  /**
   * Get recovery statistics
   */
  getRecoveryStatistics(): {
    totalRecoveryAttempts: number;
    successfulRecoveries: number;
    failedRecoveries: number;
    averageRecoveryTime: number;
    recoverySuccessRate: number;
    fallbackUsageRate: number;
    userInterventionRate: number;
  } {
    const completedSessions = Array.from(this.activeSessions.values())
      .filter(session => session.endTime);

    const totalRecoveryAttempts = completedSessions.length;
    const successfulRecoveries = completedSessions.filter(s => s.finalSuccess).length;
    const failedRecoveries = totalRecoveryAttempts - successfulRecoveries;
    
    const totalRecoveryTime = completedSessions.reduce((total, session) => {
      if (session.endTime) {
        return total + (session.endTime.getTime() - session.startTime.getTime());
      }
      return total;
    }, 0);

    const averageRecoveryTime = totalRecoveryAttempts > 0 ? totalRecoveryTime / totalRecoveryAttempts : 0;
    const recoverySuccessRate = totalRecoveryAttempts > 0 ? successfulRecoveries / totalRecoveryAttempts : 0;
    const fallbackUsageRate = totalRecoveryAttempts > 0 ? 
      completedSessions.filter(s => s.fallbackUsed).length / totalRecoveryAttempts : 0;
    const userInterventionRate = totalRecoveryAttempts > 0 ? 
      completedSessions.filter(s => s.userIntervention).length / totalRecoveryAttempts : 0;

    return {
      totalRecoveryAttempts,
      successfulRecoveries,
      failedRecoveries,
      averageRecoveryTime,
      recoverySuccessRate,
      fallbackUsageRate,
      userInterventionRate
    };
  }

  /**
   * Clear retry timer for an error
   */
  private clearRetryTimer(errorId: string): void {
    const timerId = this.retryTimers.get(errorId);
    if (timerId) {
      clearTimeout(timerId);
      this.retryTimers.delete(errorId);
    }
  }

  /**
   * Utility method for delays
   */
  private delay(ms: number): Promise<void> {
    return new Promise(resolve => setTimeout(resolve, ms));
  }

  /**
   * Reset service state (useful for testing)
   */
  reset(): void {
    this.cancelAllRecoveries();
    this.activeSessions.clear();
    this.retryTimers.clear();
    this.callbacks = {};
  }
}

/**
 * Singleton instance for global access
 */
export const errorRecoveryService = ErrorRecoveryService.getInstance();