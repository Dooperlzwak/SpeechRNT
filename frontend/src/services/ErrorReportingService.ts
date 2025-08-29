/**
 * ErrorReportingService - Centralized error reporting to external monitoring services
 * 
 * Features:
 * - Configurable reporting backends (development vs production)
 * - Error context collection (user agent, URL, component stack)
 * - Error sanitization and privacy protection
 * - Service initialization and configuration management
 */

import { type ErrorInfo } from 'react';

export interface ErrorReportingConfig {
  apiKey?: string;
  environment: 'development' | 'production';
  enableLocalLogging: boolean;
  enableRemoteReporting: boolean;
  maxErrorsPerSession?: number;
  sanitizeData?: boolean;
  endpoint?: string;
}

export interface ErrorContext {
  component?: string;
  operation?: string;
  userId?: string;
  sessionId?: string;
  additionalData?: Record<string, any>;
}

export interface ErrorReport {
  message: string;
  stack?: string;
  componentStack?: string;
  timestamp: string;
  userAgent: string;
  url: string;
  environment: string;
  context?: ErrorContext;
  sessionId?: string;
  userId?: string;
  errorId: string;
}

/**
 * ErrorReportingService class with configurable reporting backends
 */
export class ErrorReportingService {
  private config: ErrorReportingConfig;
  private sessionId: string;
  private userId?: string;
  private errorCount: number = 0;
  private reportedErrors: Set<string> = new Set();

  constructor(config: ErrorReportingConfig) {
    this.config = {
      maxErrorsPerSession: 50,
      sanitizeData: true,
      ...config
    };
    this.sessionId = this.generateSessionId();
    
    // Initialize service
    this.initialize();
  }

  /**
   * Initialize the error reporting service
   */
  private initialize(): void {
    if (this.config.enableLocalLogging) {
      console.log('[ErrorReportingService] Initialized with config:', {
        environment: this.config.environment,
        enableRemoteReporting: this.config.enableRemoteReporting,
        sessionId: this.sessionId
      });
    }

    // Set up global error handlers
    this.setupGlobalErrorHandlers();
  }

  /**
   * Set up global error handlers for unhandled errors
   */
  private setupGlobalErrorHandlers(): void {
    // Handle unhandled JavaScript errors
    window.addEventListener('error', (event) => {
      this.reportError(new Error(event.message), {
        component: 'GlobalErrorHandler',
        operation: 'unhandledError',
        additionalData: {
          filename: event.filename,
          lineno: event.lineno,
          colno: event.colno
        }
      });
    });

    // Handle unhandled promise rejections
    window.addEventListener('unhandledrejection', (event) => {
      const error = event.reason instanceof Error ? event.reason : new Error(String(event.reason));
      this.reportError(error, {
        component: 'GlobalErrorHandler',
        operation: 'unhandledPromiseRejection'
      });
    });
  }

  /**
   * Report a general error with optional context
   */
  async reportError(error: Error, context?: ErrorContext): Promise<void> {
    try {
      const errorReport = this.createErrorReport(error, context);
      await this.sendErrorReport(errorReport);
    } catch (reportingError) {
      if (this.config.enableLocalLogging) {
        console.error('[ErrorReportingService] Failed to report error:', reportingError);
      }
    }
  }

  /**
   * Report a React component exception with error info
   */
  async reportException(error: Error, errorInfo: ErrorInfo, context?: ErrorContext): Promise<void> {
    try {
      const enhancedContext: ErrorContext = {
        ...context,
        component: context?.component || 'ReactComponent',
        operation: context?.operation || 'componentDidCatch',
        additionalData: {
          ...context?.additionalData,
          componentStack: errorInfo.componentStack
        }
      };

      const errorReport = this.createErrorReport(error, enhancedContext);
      errorReport.componentStack = errorInfo.componentStack ?? undefined;
      
      await this.sendErrorReport(errorReport);
    } catch (reportingError) {
      if (this.config.enableLocalLogging) {
        console.error('[ErrorReportingService] Failed to report exception:', reportingError);
      }
    }
  }

  /**
   * Set user context for error reporting
   */
  setUserContext(userId: string, sessionId?: string): void {
    this.userId = userId;
    if (sessionId) {
      this.sessionId = sessionId;
    }

    if (this.config.enableLocalLogging) {
      console.log('[ErrorReportingService] User context updated:', { userId, sessionId: this.sessionId });
    }
  }

  /**
   * Clear user context
   */
  clearUserContext(): void {
    this.userId = undefined;
    this.sessionId = this.generateSessionId();

    if (this.config.enableLocalLogging) {
      console.log('[ErrorReportingService] User context cleared');
    }
  }

  /**
   * Create a standardized error report
   */
  private createErrorReport(error: Error, context?: ErrorContext): ErrorReport {
    const errorId = this.generateErrorId(error);
    
    const report: ErrorReport = {
      message: this.sanitizeErrorMessage(error.message),
      stack: this.config.sanitizeData ? this.sanitizeStack(error.stack) : error.stack,
      timestamp: new Date().toISOString(),
      userAgent: navigator.userAgent,
      url: this.sanitizeUrl(window.location.href),
      environment: this.config.environment,
      sessionId: this.sessionId,
      userId: this.userId,
      errorId,
      context: context ? this.sanitizeContext(context) : undefined
    };

    return report;
  }

  /**
   * Send error report to configured backends
   */
  private async sendErrorReport(report: ErrorReport): Promise<void> {
    // Check if we've already reported this error
    if (this.reportedErrors.has(report.errorId)) {
      return;
    }

    // Check error count limit
    if (this.errorCount >= (this.config.maxErrorsPerSession || 50)) {
      if (this.config.enableLocalLogging) {
        console.warn('[ErrorReportingService] Max errors per session reached, skipping report');
      }
      return;
    }

    this.errorCount++;
    this.reportedErrors.add(report.errorId);

    // Local logging
    if (this.config.enableLocalLogging) {
      console.error('[ErrorReportingService] Error Report:', report);
    }

    // Remote reporting (only in production or when explicitly enabled)
    if (this.config.enableRemoteReporting && this.config.environment === 'production') {
      await this.sendToRemoteService(report);
    }
  }

  /**
   * Send error report to remote monitoring service
   */
  private async sendToRemoteService(report: ErrorReport): Promise<void> {
    if (!this.config.endpoint) {
      if (this.config.enableLocalLogging) {
        console.warn('[ErrorReportingService] No endpoint configured for remote reporting');
      }
      return;
    }

    try {
      const response = await fetch(this.config.endpoint, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          ...(this.config.apiKey && { 'Authorization': `Bearer ${this.config.apiKey}` })
        },
        body: JSON.stringify(report)
      });

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }

      if (this.config.enableLocalLogging) {
        console.log('[ErrorReportingService] Error reported successfully to remote service');
      }
    } catch (error) {
      if (this.config.enableLocalLogging) {
        console.error('[ErrorReportingService] Failed to send to remote service:', error);
      }
      // Don't throw here to avoid recursive error reporting
    }
  }

  /**
   * Sanitize error message to remove sensitive information
   */
  private sanitizeErrorMessage(message: string): string {
    if (!this.config.sanitizeData) {
      return message;
    }

    // Remove potential sensitive patterns
    return message
      .replace(/\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Z|a-z]{2,}\b/g, '[EMAIL]')
      .replace(/\b\d{4}[-\s]?\d{4}[-\s]?\d{4}[-\s]?\d{4}\b/g, '[CARD]')
      .replace(/\b\d{3}-\d{2}-\d{4}\b/g, '[SSN]')
      .replace(/password[=:]\s*\S+/gi, 'password=[REDACTED]')
      .replace(/token[=:]\s*\S+/gi, 'token=[REDACTED]');
  }

  /**
   * Sanitize stack trace to remove sensitive information
   */
  private sanitizeStack(stack?: string): string | undefined {
    if (!stack || !this.config.sanitizeData) {
      return stack;
    }

    // Remove file paths that might contain sensitive information
    return stack.replace(/file:\/\/\/[^\s)]+/g, '[FILE_PATH]');
  }

  /**
   * Sanitize URL to remove sensitive query parameters
   */
  private sanitizeUrl(url: string): string {
    if (!this.config.sanitizeData) {
      return url;
    }

    try {
      const urlObj = new URL(url);
      // Remove potentially sensitive query parameters
      const sensitiveParams = ['token', 'key', 'password', 'secret', 'auth'];
      sensitiveParams.forEach(param => {
        if (urlObj.searchParams.has(param)) {
          urlObj.searchParams.set(param, '[REDACTED]');
        }
      });
      return urlObj.toString();
    } catch {
      return url;
    }
  }

  /**
   * Sanitize error context to remove sensitive information
   */
  private sanitizeContext(context: ErrorContext): ErrorContext {
    if (!this.config.sanitizeData) {
      return context;
    }

    const sanitized = { ...context };
    
    if (sanitized.additionalData) {
      sanitized.additionalData = { ...sanitized.additionalData };
      // Remove potentially sensitive keys
      const sensitiveKeys = ['password', 'token', 'key', 'secret', 'auth', 'credential'];
      sensitiveKeys.forEach(key => {
        if (sanitized.additionalData![key]) {
          sanitized.additionalData![key] = '[REDACTED]';
        }
      });
    }

    return sanitized;
  }

  /**
   * Generate a unique session ID
   */
  private generateSessionId(): string {
    return `session_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
  }

  /**
   * Generate a unique error ID based on error characteristics
   */
  private generateErrorId(error: Error): string {
    const errorSignature = `${error.name}_${error.message}_${error.stack?.split('\n')[0] || ''}`;
    return `error_${this.hashString(errorSignature)}`;
  }

  /**
   * Simple hash function for generating error IDs
   */
  private hashString(str: string): string {
    let hash = 0;
    for (let i = 0; i < str.length; i++) {
      const char = str.charCodeAt(i);
      hash = ((hash << 5) - hash) + char;
      hash = hash & hash; // Convert to 32-bit integer
    }
    return Math.abs(hash).toString(36);
  }

  /**
   * Get current service statistics
   */
  getStats(): { errorCount: number; sessionId: string; userId?: string } {
    return {
      errorCount: this.errorCount,
      sessionId: this.sessionId,
      userId: this.userId
    };
  }

  /**
   * Reset error count and reported errors (useful for testing)
   */
  reset(): void {
    this.errorCount = 0;
    this.reportedErrors.clear();
    this.sessionId = this.generateSessionId();
    
    if (this.config.enableLocalLogging) {
      console.log('[ErrorReportingService] Service reset');
    }
  }
}

/**
 * Default configuration for development environment
 */
export const defaultDevelopmentConfig: ErrorReportingConfig = {
  environment: 'development',
  enableLocalLogging: true,
  enableRemoteReporting: false,
  sanitizeData: false,
  maxErrorsPerSession: 100
};

/**
 * Default configuration for production environment
 */
export const defaultProductionConfig: ErrorReportingConfig = {
  environment: 'production',
  enableLocalLogging: false,
  enableRemoteReporting: true,
  sanitizeData: true,
  maxErrorsPerSession: 50
};

/**
 * Factory function to create ErrorReportingService with environment-specific defaults
 */
export function createErrorReportingService(
  environment: 'development' | 'production' = 'development',
  overrides: Partial<ErrorReportingConfig> = {}
): ErrorReportingService {
  const baseConfig = environment === 'production' 
    ? defaultProductionConfig 
    : defaultDevelopmentConfig;
    
  const config = { ...baseConfig, ...overrides };
  return new ErrorReportingService(config);
}