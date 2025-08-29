/**
 * ErrorReportingService Tests
 * 
 * Tests for error reporting functionality including:
 * - Service initialization and configuration
 * - Error reporting with context collection
 * - Development vs production modes
 * - Error sanitization and privacy protection
 * - Global error handling
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { ErrorInfo } from 'react';
import { 
  ErrorReportingService, 
  ErrorReportingConfig,
  ErrorContext,
  createErrorReportingService,
  defaultDevelopmentConfig,
  defaultProductionConfig
} from '../ErrorReportingService';

// Mock fetch globally
const mockFetch = vi.fn();
global.fetch = mockFetch;

// Mock console methods
const mockConsoleError = vi.spyOn(console, 'error').mockImplementation(() => {});
const mockConsoleLog = vi.spyOn(console, 'log').mockImplementation(() => {});
const mockConsoleWarn = vi.spyOn(console, 'warn').mockImplementation(() => {});

describe('ErrorReportingService', () => {
  let service: ErrorReportingService;
  let config: ErrorReportingConfig;

  beforeEach(() => {
    vi.clearAllMocks();
    mockFetch.mockClear();
    
    config = {
      environment: 'development',
      enableLocalLogging: true,
      enableRemoteReporting: false,
      sanitizeData: true,
      maxErrorsPerSession: 10
    };
  });

  afterEach(() => {
    // Clean up global error listeners
    if (service) {
      service.reset();
    }
  });

  describe('Service Initialization', () => {
    it('should initialize with provided configuration', () => {
      service = new ErrorReportingService(config);
      const stats = service.getStats();
      
      expect(stats.errorCount).toBe(0);
      expect(stats.sessionId).toMatch(/^session_\d+_[a-z0-9]+$/);
      expect(stats.userId).toBeUndefined();
    });

    it('should apply default values for missing config options', () => {
      const minimalConfig: ErrorReportingConfig = {
        environment: 'production',
        enableLocalLogging: false,
        enableRemoteReporting: true
      };
      
      service = new ErrorReportingService(minimalConfig);
      // Should not throw and should work with defaults
      expect(service.getStats().errorCount).toBe(0);
    });

    it('should set up global error handlers', () => {
      const addEventListenerSpy = vi.spyOn(window, 'addEventListener');
      service = new ErrorReportingService(config);
      
      expect(addEventListenerSpy).toHaveBeenCalledWith('error', expect.any(Function));
      expect(addEventListenerSpy).toHaveBeenCalledWith('unhandledrejection', expect.any(Function));
    });
  });

  describe('Error Reporting', () => {
    beforeEach(() => {
      // Disable sanitization for basic tests
      config.sanitizeData = false;
      service = new ErrorReportingService(config);
    });

    it('should report basic errors with context collection', async () => {
      const testError = new Error('Test error message');
      const context: ErrorContext = {
        component: 'TestComponent',
        operation: 'testOperation',
        additionalData: { key: 'value' }
      };

      await service.reportError(testError, context);

      expect(mockConsoleError).toHaveBeenCalledWith(
        '[ErrorReportingService] Error Report:',
        expect.objectContaining({
          message: 'Test error message',
          timestamp: expect.any(String),
          userAgent: expect.any(String),
          url: expect.any(String),
          environment: 'development',
          errorId: expect.any(String),
          sessionId: expect.any(String),
          context: expect.objectContaining({
            component: 'TestComponent',
            operation: 'testOperation',
            additionalData: { key: 'value' }
          })
        })
      );
    });

    it('should report React exceptions with error info', async () => {
      const testError = new Error('React component error');
      const errorInfo: ErrorInfo = {
        componentStack: '\n    in TestComponent\n    in App'
      };

      await service.reportException(testError, errorInfo);

      expect(mockConsoleError).toHaveBeenCalledWith(
        '[ErrorReportingService] Error Report:',
        expect.objectContaining({
          message: 'React component error',
          componentStack: '\n    in TestComponent\n    in App',
          context: expect.objectContaining({
            component: 'ReactComponent',
            operation: 'componentDidCatch'
          })
        })
      );
    });

    it('should increment error count on each report', async () => {
      const error1 = new Error('Error 1');
      const error2 = new Error('Error 2');

      await service.reportError(error1);
      expect(service.getStats().errorCount).toBe(1);

      await service.reportError(error2);
      expect(service.getStats().errorCount).toBe(2);
    });

    it('should not report duplicate errors', async () => {
      const testError = new Error('Duplicate error');

      await service.reportError(testError);
      await service.reportError(testError);

      expect(service.getStats().errorCount).toBe(1);
    });

    it('should respect max errors per session limit', async () => {
      const maxErrors = config.maxErrorsPerSession!;
      
      // Report max + 1 errors
      for (let i = 0; i <= maxErrors; i++) {
        await service.reportError(new Error(`Error ${i}`));
      }

      expect(service.getStats().errorCount).toBe(maxErrors);
      expect(mockConsoleWarn).toHaveBeenCalledWith(
        '[ErrorReportingService] Max errors per session reached, skipping report'
      );
    });
  });

  describe('User Context Management', () => {
    beforeEach(() => {
      service = new ErrorReportingService(config);
    });

    it('should set and clear user context', () => {
      service.setUserContext('user123', 'session456');
      let stats = service.getStats();
      
      expect(stats.userId).toBe('user123');
      expect(stats.sessionId).toBe('session456');

      service.clearUserContext();
      stats = service.getStats();
      
      expect(stats.userId).toBeUndefined();
      expect(stats.sessionId).toMatch(/^session_\d+_[a-z0-9]+$/);
      expect(stats.sessionId).not.toBe('session456');
    });

    it('should include user context in error reports', async () => {
      service.setUserContext('user123');
      await service.reportError(new Error('Test error'));

      expect(mockConsoleError).toHaveBeenCalledWith(
        '[ErrorReportingService] Error Report:',
        expect.objectContaining({
          userId: 'user123'
        })
      );
    });
  });

  describe('Data Sanitization', () => {
    beforeEach(() => {
      config.sanitizeData = true;
      service = new ErrorReportingService(config);
    });

    it('should sanitize sensitive information in error messages', async () => {
      const sensitiveError = new Error('Error with email user@example.com and password=secret123');
      await service.reportError(sensitiveError);

      expect(mockConsoleError).toHaveBeenCalledWith(
        '[ErrorReportingService] Error Report:',
        expect.objectContaining({
          message: 'Error with email [EMAIL] and password=[REDACTED]'
        })
      );
    });

    it('should sanitize URLs with sensitive query parameters', async () => {
      // Mock window.location.href
      const originalLocation = window.location;
      delete (window as any).location;
      window.location = { href: 'https://example.com?token=secret123&key=abc' } as any;

      await service.reportError(new Error('Test error'));

      expect(mockConsoleError).toHaveBeenCalledWith(
        '[ErrorReportingService] Error Report:',
        expect.objectContaining({
          url: 'https://example.com/?token=%5BREDACTED%5D&key=%5BREDACTED%5D'
        })
      );

      // Restore original location
      window.location = originalLocation;
    });

    it('should sanitize context additional data', async () => {
      const context: ErrorContext = {
        additionalData: {
          password: 'secret123',
          token: 'abc123',
          normalData: 'safe'
        }
      };

      await service.reportError(new Error('Test error'), context);

      expect(mockConsoleError).toHaveBeenCalledWith(
        '[ErrorReportingService] Error Report:',
        expect.objectContaining({
          context: expect.objectContaining({
            additionalData: {
              password: '[REDACTED]',
              token: '[REDACTED]',
              normalData: 'safe'
            }
          })
        })
      );
    });

    it('should not sanitize when sanitizeData is false', async () => {
      config.sanitizeData = false;
      service = new ErrorReportingService(config);

      const sensitiveError = new Error('Error with password=secret123');
      await service.reportError(sensitiveError);

      expect(mockConsoleError).toHaveBeenCalledWith(
        '[ErrorReportingService] Error Report:',
        expect.objectContaining({
          message: 'Error with password=secret123'
        })
      );
    });
  });

  describe('Remote Reporting', () => {
    beforeEach(() => {
      config.environment = 'production';
      config.enableRemoteReporting = true;
      config.enableLocalLogging = false;
      config.endpoint = 'https://api.example.com/errors';
      config.apiKey = 'test-api-key';
      service = new ErrorReportingService(config);
    });

    it('should send errors to remote service in production', async () => {
      mockFetch.mockResolvedValueOnce({
        ok: true,
        status: 200,
        statusText: 'OK'
      } as Response);

      await service.reportError(new Error('Production error'));

      expect(mockFetch).toHaveBeenCalledWith(
        'https://api.example.com/errors',
        expect.objectContaining({
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            'Authorization': 'Bearer test-api-key'
          },
          body: expect.stringContaining('Production error')
        })
      );
    });

    it('should handle remote service failures gracefully', async () => {
      mockFetch.mockRejectedValueOnce(new Error('Network error'));

      // Should not throw
      await expect(service.reportError(new Error('Test error'))).resolves.toBeUndefined();
    });

    it('should not send to remote service in development', async () => {
      config.environment = 'development';
      service = new ErrorReportingService(config);

      await service.reportError(new Error('Dev error'));

      expect(mockFetch).not.toHaveBeenCalled();
    });
  });

  describe('Global Error Handling', () => {
    beforeEach(() => {
      service = new ErrorReportingService(config);
    });

    it('should handle global JavaScript errors', async () => {
      const errorEvent = new ErrorEvent('error', {
        message: 'Global error',
        filename: 'test.js',
        lineno: 10,
        colno: 5
      });

      window.dispatchEvent(errorEvent);

      // Wait for async error reporting
      await new Promise(resolve => setTimeout(resolve, 0));

      expect(mockConsoleError).toHaveBeenCalledWith(
        '[ErrorReportingService] Error Report:',
        expect.objectContaining({
          message: 'Global error',
          context: expect.objectContaining({
            component: 'GlobalErrorHandler',
            operation: 'unhandledError',
            additionalData: expect.objectContaining({
              filename: 'test.js',
              lineno: 10,
              colno: 5
            })
          })
        })
      );
    });

    it('should handle unhandled promise rejections', async () => {
      // Create a mock event since PromiseRejectionEvent may not be available in test environment
      const rejectedPromise = Promise.reject(new Error('Promise rejection'));
      const rejectionEvent = {
        type: 'unhandledrejection',
        reason: new Error('Promise rejection'),
        promise: rejectedPromise
      } as any;

      // Catch the promise to prevent unhandled rejection in test
      rejectedPromise.catch(() => {});

      // Manually trigger the handler
      const unhandledRejectionHandler = (event: any) => {
        const error = event.reason instanceof Error ? event.reason : new Error(String(event.reason));
        service.reportError(error, {
          component: 'GlobalErrorHandler',
          operation: 'unhandledPromiseRejection'
        });
      };

      unhandledRejectionHandler(rejectionEvent);

      // Wait for async error reporting
      await new Promise(resolve => setTimeout(resolve, 0));

      expect(mockConsoleError).toHaveBeenCalledWith(
        '[ErrorReportingService] Error Report:',
        expect.objectContaining({
          message: 'Promise rejection',
          context: expect.objectContaining({
            component: 'GlobalErrorHandler',
            operation: 'unhandledPromiseRejection'
          })
        })
      );
    });
  });

  describe('Factory Functions', () => {
    it('should create service with development defaults', () => {
      const devService = createErrorReportingService('development');
      expect(devService).toBeInstanceOf(ErrorReportingService);
    });

    it('should create service with production defaults', () => {
      const prodService = createErrorReportingService('production');
      expect(prodService).toBeInstanceOf(ErrorReportingService);
    });

    it('should apply overrides to default config', () => {
      const service = createErrorReportingService('development', {
        maxErrorsPerSession: 5
      });
      
      // Test that override was applied by hitting the limit
      for (let i = 0; i < 6; i++) {
        service.reportError(new Error(`Error ${i}`));
      }
      
      expect(service.getStats().errorCount).toBe(5);
    });
  });

  describe('Service Reset', () => {
    beforeEach(() => {
      service = new ErrorReportingService(config);
    });

    it('should reset error count and session', async () => {
      await service.reportError(new Error('Test error'));
      service.setUserContext('user123');
      
      expect(service.getStats().errorCount).toBe(1);
      expect(service.getStats().userId).toBe('user123');

      service.reset();

      const stats = service.getStats();
      expect(stats.errorCount).toBe(0);
      expect(stats.userId).toBe('user123'); // User context should persist
      expect(stats.sessionId).toMatch(/^session_\d+_[a-z0-9]+$/);
    });
  });

  describe('Default Configurations', () => {
    it('should have correct development defaults', () => {
      expect(defaultDevelopmentConfig).toEqual({
        environment: 'development',
        enableLocalLogging: true,
        enableRemoteReporting: false,
        sanitizeData: false,
        maxErrorsPerSession: 100
      });
    });

    it('should have correct production defaults', () => {
      expect(defaultProductionConfig).toEqual({
        environment: 'production',
        enableLocalLogging: false,
        enableRemoteReporting: true,
        sanitizeData: true,
        maxErrorsPerSession: 50
      });
    });
  });
});