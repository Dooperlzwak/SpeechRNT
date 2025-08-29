/**
 * ErrorMetricsService - Comprehensive error metrics collection and reporting
 * 
 * This service collects detailed error metrics, user behavior analytics,
 * and system performance data related to error handling and recovery.
 */

import { type AppError, ErrorType } from '../components/ErrorNotification';
import { type ErrorMetrics, type ErrorClassification } from './ErrorClassificationService';
import { type RecoverySession } from './ErrorRecoveryService';
import { createErrorReportingService } from './ErrorReportingService';

export interface UserInteractionMetrics {
  errorId: string;
  interactionType: 'dismiss' | 'retry' | 'help_viewed' | 'permission_granted' | 'manual_action';
  timestamp: Date;
  timeFromErrorStart: number;
  context?: Record<string, any>;
}

export interface SystemPerformanceMetrics {
  timestamp: Date;
  memoryUsage?: number;
  connectionQuality: 'good' | 'poor' | 'critical';
  activeServices: string[];
  errorRate: number;
  recoveryRate: number;
}

export interface ErrorPatternMetrics {
  pattern: string;
  frequency: number;
  firstOccurrence: Date;
  lastOccurrence: Date;
  affectedUsers: number;
  averageRecoveryTime: number;
  commonContext: Record<string, any>;
}

export interface SessionErrorMetrics {
  sessionId: string;
  startTime: Date;
  endTime?: Date;
  totalErrors: number;
  errorsByType: Record<ErrorType, number>;
  recoveryAttempts: number;
  successfulRecoveries: number;
  userInterventions: number;
  sessionTerminated: boolean;
  terminationReason?: string;
}

/**
 * Comprehensive error metrics collection and analysis service
 */
export class ErrorMetricsService {
  private static instance: ErrorMetricsService | null = null;
  private errorReportingService = createErrorReportingService();
  
  // Metrics storage
  private errorMetrics: ErrorMetrics[] = [];
  private userInteractionMetrics: UserInteractionMetrics[] = [];
  private systemPerformanceMetrics: SystemPerformanceMetrics[] = [];
  private errorPatterns: Map<string, ErrorPatternMetrics> = new Map();
  private sessionMetrics: Map<string, SessionErrorMetrics> = new Map();
  
  // Current session tracking
  private currentSessionId: string = this.generateSessionId();
  private sessionStartTime: Date = new Date();
  private performanceMonitorInterval: number | null = null;

  private constructor() {
    this.initializePerformanceMonitoring();
    this.initializeSessionMetrics();
  }

  static getInstance(): ErrorMetricsService {
    if (!ErrorMetricsService.instance) {
      ErrorMetricsService.instance = new ErrorMetricsService();
    }
    return ErrorMetricsService.instance;
  }

  /**
   * Initialize performance monitoring
   */
  private initializePerformanceMonitoring(): void {
    // Monitor system performance every 30 seconds
    this.performanceMonitorInterval = window.setInterval(() => {
      this.collectSystemPerformanceMetrics();
    }, 30000);

    // Initial collection
    this.collectSystemPerformanceMetrics();
  }

  /**
   * Initialize session metrics tracking
   */
  private initializeSessionMetrics(): void {
    const sessionMetrics: SessionErrorMetrics = {
      sessionId: this.currentSessionId,
      startTime: this.sessionStartTime,
      totalErrors: 0,
      errorsByType: {} as Record<ErrorType, number>,
      recoveryAttempts: 0,
      successfulRecoveries: 0,
      userInterventions: 0,
      sessionTerminated: false
    };

    this.sessionMetrics.set(this.currentSessionId, sessionMetrics);
  }

  /**
   * Record error occurrence with comprehensive metrics
   */
  recordError(
    appError: AppError,
    classification: ErrorClassification,
    context?: Record<string, any>
  ): void {
    const errorMetrics: ErrorMetrics = {
      errorId: appError.id,
      errorType: appError.type,
      classification,
      timestamp: appError.timestamp,
      context: context || {},
      recoveryAttempts: 0,
      recoverySuccess: false
    };

    this.errorMetrics.push(errorMetrics);

    // Update session metrics
    const sessionMetrics = this.sessionMetrics.get(this.currentSessionId);
    if (sessionMetrics) {
      sessionMetrics.totalErrors++;
      sessionMetrics.errorsByType[appError.type] = 
        (sessionMetrics.errorsByType[appError.type] || 0) + 1;

      // Check if this is a session-terminating error
      if (classification.sessionTerminating) {
        sessionMetrics.sessionTerminated = true;
        sessionMetrics.terminationReason = `${appError.type}: ${appError.message}`;
        sessionMetrics.endTime = new Date();
      }
    }

    // Update error patterns
    this.updateErrorPatterns(appError, context);

    // Report to external service
    this.reportErrorMetrics(errorMetrics);

    // Cleanup old metrics
    this.cleanupOldMetrics();
  }

  /**
   * Record user interaction with error notifications
   */
  recordUserInteraction(
    errorId: string,
    interactionType: UserInteractionMetrics['interactionType'],
    context?: Record<string, any>
  ): void {
    const errorMetric = this.errorMetrics.find(m => m.errorId === errorId);
    if (!errorMetric) return;

    const timeFromErrorStart = Date.now() - errorMetric.timestamp.getTime();

    const interaction: UserInteractionMetrics = {
      errorId,
      interactionType,
      timestamp: new Date(),
      timeFromErrorStart,
      context
    };

    this.userInteractionMetrics.push(interaction);

    // Update session metrics
    const sessionMetrics = this.sessionMetrics.get(this.currentSessionId);
    if (sessionMetrics && interactionType === 'manual_action') {
      sessionMetrics.userInterventions++;
    }

    // Report interaction
    this.reportUserInteraction(interaction);
  }

  /**
   * Record recovery session completion
   */
  recordRecoverySession(recoverySession: RecoverySession): void {
    const errorMetric = this.errorMetrics.find(m => m.errorId === recoverySession.errorId);
    if (errorMetric) {
      errorMetric.recoveryAttempts = recoverySession.attempts.length;
      errorMetric.recoverySuccess = recoverySession.finalSuccess;
      
      if (recoverySession.endTime) {
        errorMetric.timeToRecover = 
          recoverySession.endTime.getTime() - recoverySession.startTime.getTime();
      }
    }

    // Update session metrics
    const sessionMetrics = this.sessionMetrics.get(this.currentSessionId);
    if (sessionMetrics) {
      sessionMetrics.recoveryAttempts += recoverySession.attempts.length;
      if (recoverySession.finalSuccess) {
        sessionMetrics.successfulRecoveries++;
      }
      if (recoverySession.userIntervention) {
        sessionMetrics.userInterventions++;
      }
    }

    // Report recovery session
    this.reportRecoverySession(recoverySession);
  }

  /**
   * Collect system performance metrics
   */
  private collectSystemPerformanceMetrics(): void {
    const metrics: SystemPerformanceMetrics = {
      timestamp: new Date(),
      connectionQuality: this.assessConnectionQuality(),
      activeServices: this.getActiveServices(),
      errorRate: this.calculateErrorRate(),
      recoveryRate: this.calculateRecoveryRate()
    };

    // Add memory usage if available
    if ('memory' in performance) {
      const memoryInfo = (performance as any).memory;
      metrics.memoryUsage = memoryInfo.usedJSHeapSize / memoryInfo.totalJSHeapSize;
    }

    this.systemPerformanceMetrics.push(metrics);

    // Keep only last 100 performance metrics
    if (this.systemPerformanceMetrics.length > 100) {
      this.systemPerformanceMetrics.splice(0, this.systemPerformanceMetrics.length - 100);
    }
  }

  /**
   * Update error patterns for trend analysis
   */
  private updateErrorPatterns(appError: AppError, context?: Record<string, any>): void {
    // Create pattern key based on error type and key context elements
    const patternKey = this.createPatternKey(appError, context);
    
    const existingPattern = this.errorPatterns.get(patternKey);
    
    if (existingPattern) {
      existingPattern.frequency++;
      existingPattern.lastOccurrence = appError.timestamp;
      existingPattern.affectedUsers++; // Simplified - in real app would track unique users
      
      // Update common context
      if (context) {
        Object.keys(context).forEach(key => {
          if (existingPattern.commonContext[key] === context[key]) {
            // Keep common values
          } else if (existingPattern.commonContext[key] !== undefined) {
            // Mark as variable if different values seen
            existingPattern.commonContext[key] = '[VARIABLE]';
          } else {
            // First time seeing this context key
            existingPattern.commonContext[key] = context[key];
          }
        });
      }
    } else {
      const newPattern: ErrorPatternMetrics = {
        pattern: patternKey,
        frequency: 1,
        firstOccurrence: appError.timestamp,
        lastOccurrence: appError.timestamp,
        affectedUsers: 1,
        averageRecoveryTime: 0,
        commonContext: { ...context }
      };
      
      this.errorPatterns.set(patternKey, newPattern);
    }
  }

  /**
   * Create pattern key for error grouping
   */
  private createPatternKey(appError: AppError, context?: Record<string, any>): string {
    const contextKeys = context ? Object.keys(context).sort() : [];
    const relevantContext = contextKeys
      .filter(key => ['component', 'operation', 'errorCategory'].includes(key))
      .map(key => `${key}:${context![key]}`)
      .join('|');
    
    return `${appError.type}${relevantContext ? `|${relevantContext}` : ''}`;
  }

  /**
   * Assess current connection quality
   */
  private assessConnectionQuality(): 'good' | 'poor' | 'critical' {
    // Simplified assessment - in real app would check actual connection metrics
    const recentErrors = this.errorMetrics.filter(
      m => Date.now() - m.timestamp.getTime() < 60000 // Last minute
    );
    
    const connectionErrors = recentErrors.filter(
      m => m.errorType === ErrorType.WEBSOCKET_CONNECTION || 
           m.errorType === ErrorType.NETWORK
    );

    if (connectionErrors.length >= 3) return 'critical';
    if (connectionErrors.length >= 1) return 'poor';
    return 'good';
  }

  /**
   * Get list of active services
   */
  private getActiveServices(): string[] {
    // Simplified - in real app would check actual service status
    return ['WebSocket', 'Audio', 'ErrorReporting'];
  }

  /**
   * Calculate current error rate (errors per minute)
   */
  private calculateErrorRate(): number {
    const oneMinuteAgo = Date.now() - 60000;
    const recentErrors = this.errorMetrics.filter(
      m => m.timestamp.getTime() > oneMinuteAgo
    );
    return recentErrors.length;
  }

  /**
   * Calculate current recovery rate
   */
  private calculateRecoveryRate(): number {
    const oneMinuteAgo = Date.now() - 60000;
    const recentErrors = this.errorMetrics.filter(
      m => m.timestamp.getTime() > oneMinuteAgo && m.recoveryAttempts > 0
    );
    
    if (recentErrors.length === 0) return 1;
    
    const successfulRecoveries = recentErrors.filter(m => m.recoverySuccess).length;
    return successfulRecoveries / recentErrors.length;
  }

  /**
   * Report error metrics to external service
   */
  private async reportErrorMetrics(metrics: ErrorMetrics): Promise<void> {
    try {
      await this.errorReportingService.reportError(
        new Error(`Error metrics: ${metrics.errorType}`),
        {
          component: 'ErrorMetricsService',
          operation: 'error_metrics',
          additionalData: {
            errorId: metrics.errorId,
            errorType: metrics.errorType,
            classification: metrics.classification,
            context: metrics.context
          }
        }
      );
    } catch (error) {
      console.error('Failed to report error metrics:', error);
    }
  }

  /**
   * Report user interaction to external service
   */
  private async reportUserInteraction(interaction: UserInteractionMetrics): Promise<void> {
    try {
      await this.errorReportingService.reportError(
        new Error(`User interaction: ${interaction.interactionType}`),
        {
          component: 'ErrorMetricsService',
          operation: 'user_interaction',
          additionalData: {
            errorId: interaction.errorId,
            interactionType: interaction.interactionType,
            timeFromErrorStart: interaction.timeFromErrorStart,
            context: interaction.context
          }
        }
      );
    } catch (error) {
      console.error('Failed to report user interaction:', error);
    }
  }

  /**
   * Report recovery session to external service
   */
  private async reportRecoverySession(session: RecoverySession): Promise<void> {
    try {
      await this.errorReportingService.reportError(
        new Error(`Recovery session: ${session.finalSuccess ? 'success' : 'failure'}`),
        {
          component: 'ErrorMetricsService',
          operation: 'recovery_session',
          additionalData: {
            errorId: session.errorId,
            errorType: session.errorType,
            attempts: session.attempts.length,
            finalSuccess: session.finalSuccess,
            userIntervention: session.userIntervention,
            fallbackUsed: session.fallbackUsed,
            duration: session.endTime ? 
              session.endTime.getTime() - session.startTime.getTime() : undefined
          }
        }
      );
    } catch (error) {
      console.error('Failed to report recovery session:', error);
    }
  }

  /**
   * Get comprehensive error analytics
   */
  getErrorAnalytics(): {
    totalErrors: number;
    errorsByType: Record<ErrorType, number>;
    errorsByCategory: Record<string, number>;
    averageRecoveryTime: number;
    recoverySuccessRate: number;
    userInteractionRate: number;
    topErrorPatterns: ErrorPatternMetrics[];
    sessionMetrics: SessionErrorMetrics;
    systemHealth: {
      currentErrorRate: number;
      currentRecoveryRate: number;
      connectionQuality: 'good' | 'poor' | 'critical';
      memoryUsage?: number;
    };
  } {
    const totalErrors = this.errorMetrics.length;
    const errorsByType: Record<ErrorType, number> = {} as Record<ErrorType, number>;
    const errorsByCategory: Record<string, number> = {};
    
    let totalRecoveryTime = 0;
    let recoveredErrors = 0;
    let successfulRecoveries = 0;
    let errorsWithInteraction = 0;

    this.errorMetrics.forEach(metrics => {
      // Count by type
      errorsByType[metrics.errorType] = (errorsByType[metrics.errorType] || 0) + 1;
      
      // Count by category
      const category = metrics.classification.category;
      errorsByCategory[category] = (errorsByCategory[category] || 0) + 1;
      
      // Recovery statistics
      if (metrics.recoveryAttempts > 0) {
        recoveredErrors++;
        if (metrics.recoverySuccess) {
          successfulRecoveries++;
        }
        if (metrics.timeToRecover) {
          totalRecoveryTime += metrics.timeToRecover;
        }
      }

      // User interaction statistics
      const hasInteraction = this.userInteractionMetrics.some(
        i => i.errorId === metrics.errorId
      );
      if (hasInteraction) {
        errorsWithInteraction++;
      }
    });

    // Get top error patterns
    const topErrorPatterns = Array.from(this.errorPatterns.values())
      .sort((a, b) => b.frequency - a.frequency)
      .slice(0, 10);

    // Get current session metrics
    const currentSession = this.sessionMetrics.get(this.currentSessionId)!;

    // Get latest system performance
    const latestPerformance = this.systemPerformanceMetrics[this.systemPerformanceMetrics.length - 1];

    return {
      totalErrors,
      errorsByType,
      errorsByCategory,
      averageRecoveryTime: recoveredErrors > 0 ? totalRecoveryTime / recoveredErrors : 0,
      recoverySuccessRate: recoveredErrors > 0 ? successfulRecoveries / recoveredErrors : 0,
      userInteractionRate: totalErrors > 0 ? errorsWithInteraction / totalErrors : 0,
      topErrorPatterns,
      sessionMetrics: currentSession,
      systemHealth: {
        currentErrorRate: latestPerformance?.errorRate || 0,
        currentRecoveryRate: latestPerformance?.recoveryRate || 1,
        connectionQuality: latestPerformance?.connectionQuality || 'good',
        memoryUsage: latestPerformance?.memoryUsage
      }
    };
  }

  /**
   * Start new session (useful for testing or session resets)
   */
  startNewSession(): string {
    // End current session
    const currentSession = this.sessionMetrics.get(this.currentSessionId);
    if (currentSession && !currentSession.endTime) {
      currentSession.endTime = new Date();
    }

    // Start new session
    this.currentSessionId = this.generateSessionId();
    this.sessionStartTime = new Date();
    this.initializeSessionMetrics();

    return this.currentSessionId;
  }

  /**
   * Clean up old metrics to prevent memory issues
   */
  private cleanupOldMetrics(): void {
    const oneWeekAgo = Date.now() - (7 * 24 * 60 * 60 * 1000);

    // Keep only last 1000 error metrics or last week, whichever is more recent
    if (this.errorMetrics.length > 1000) {
      this.errorMetrics = this.errorMetrics
        .filter(m => m.timestamp.getTime() > oneWeekAgo)
        .slice(-1000);
    }

    // Keep only last 500 user interactions
    if (this.userInteractionMetrics.length > 500) {
      this.userInteractionMetrics = this.userInteractionMetrics.slice(-500);
    }

    // Clean up old error patterns (keep only patterns seen in last week)
    for (const [key, pattern] of this.errorPatterns.entries()) {
      if (pattern.lastOccurrence.getTime() < oneWeekAgo) {
        this.errorPatterns.delete(key);
      }
    }

    // Clean up old session metrics (keep only last 10 sessions)
    if (this.sessionMetrics.size > 10) {
      const sessions = Array.from(this.sessionMetrics.entries())
        .sort(([, a], [, b]) => b.startTime.getTime() - a.startTime.getTime())
        .slice(0, 10);
      
      this.sessionMetrics.clear();
      sessions.forEach(([id, metrics]) => {
        this.sessionMetrics.set(id, metrics);
      });
    }
  }

  /**
   * Generate unique session ID
   */
  private generateSessionId(): string {
    return `session_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
  }

  /**
   * Cleanup resources
   */
  cleanup(): void {
    if (this.performanceMonitorInterval) {
      clearInterval(this.performanceMonitorInterval);
      this.performanceMonitorInterval = null;
    }

    // End current session
    const currentSession = this.sessionMetrics.get(this.currentSessionId);
    if (currentSession && !currentSession.endTime) {
      currentSession.endTime = new Date();
    }
  }

  /**
   * Reset all metrics (useful for testing)
   */
  reset(): void {
    this.errorMetrics = [];
    this.userInteractionMetrics = [];
    this.systemPerformanceMetrics = [];
    this.errorPatterns.clear();
    this.sessionMetrics.clear();
    this.startNewSession();
  }
}

/**
 * Singleton instance for global access
 */
export const errorMetricsService = ErrorMetricsService.getInstance();