/**
 * Services Index
 * 
 * Centralized exports for all service modules
 */

// Audio Services
export { AudioManager } from './AudioManager';
export { AudioPlaybackManager } from './AudioPlaybackManager';

// WebSocket Services
export { WebSocketManager } from './WebSocketManager';

// Error Reporting Services
export { 
  ErrorReportingService,
  createErrorReportingService,
  defaultDevelopmentConfig,
  defaultProductionConfig
} from './ErrorReportingService';

// Performance Monitoring Services
export { 
  PerformanceMonitoringService,
  performanceMonitor
} from './PerformanceMonitoringService';

// Types
export type { 
  ErrorReportingConfig,
  ErrorContext,
  ErrorReport
} from './ErrorReportingService';

export type {
  ConnectionMetrics,
  AudioMetrics,
  ServiceInitializationMetrics,
  ErrorMetrics,
  PerformanceSnapshot,
  MetricEvent,
  MetricEventType
} from './PerformanceMonitoringService';