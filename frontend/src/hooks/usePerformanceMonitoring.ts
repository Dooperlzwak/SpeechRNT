/**
 * usePerformanceMonitoring - React hook for performance monitoring integration
 * 
 * This hook provides a React interface to the PerformanceMonitoringService,
 * integrating with WebSocket and Audio services to collect comprehensive metrics.
 */

import { useEffect, useRef, useState, useCallback } from 'react';
import { 
  PerformanceMonitoringService, 
  performanceMonitor,
  type PerformanceSnapshot,
  type ConnectionMetrics,
  type AudioMetrics,
  type ServiceInitializationMetrics,
  type ErrorMetrics,
  type MetricEvent,
  type MetricEventType
} from '../services/PerformanceMonitoringService';

export interface PerformanceMonitoringConfig {
  enabled: boolean;
  updateInterval?: number;
  maxEventHistory?: number;
  autoStart?: boolean;
  enableConnectionMonitoring?: boolean;
  enableAudioMonitoring?: boolean;
  enableErrorTracking?: boolean;
}

export interface PerformanceMonitoringReturn {
  // Monitoring state
  isMonitoring: boolean;
  lastUpdate: number;
  
  // Control methods
  startMonitoring: () => void;
  stopMonitoring: () => void;
  resetMetrics: () => void;
  
  // Metric recording methods
  recordConnectionMetrics: (data: {
    latency?: number;
    quality?: 'good' | 'poor' | 'critical';
    connected?: boolean;
    messagesSent?: number;
    messagesReceived?: number;
    messageFailures?: number;
  }) => void;
  
  recordAudioMetrics: (data: {
    isStreaming?: boolean;
    bytesTransmitted?: number;
    packetsTransmitted?: number;
    packetsLost?: number;
    deviceSwitches?: number;
    permissionDenials?: number;
    captureErrors?: number;
    audioQuality?: 'good' | 'degraded' | 'poor';
  }) => void;
  
  recordServiceInitialization: (
    service: 'websocket' | 'audio' | 'errorReporting',
    duration: number,
    success: boolean
  ) => void;
  
  recordError: (
    errorType: 'connection' | 'audio' | 'configuration' | 'system',
    error?: Error
  ) => void;
  
  recordRecovery: (success: boolean, errorType?: string) => void;
  
  recordEvent: (event: MetricEvent) => void;
  
  // Data access methods
  getPerformanceSnapshot: () => PerformanceSnapshot;
  getConnectionMetrics: () => ConnectionMetrics;
  getAudioMetrics: () => AudioMetrics;
  getServiceInitializationMetrics: () => ServiceInitializationMetrics;
  getErrorMetrics: () => ErrorMetrics;
  getRecentEvents: (count?: number) => MetricEvent[];
  getEventsByType: (type: MetricEventType, limit?: number) => MetricEvent[];
  
  // Real-time metrics (updated via state)
  currentSnapshot: PerformanceSnapshot | null;
  connectionMetrics: ConnectionMetrics | null;
  audioMetrics: AudioMetrics | null;
  errorMetrics: ErrorMetrics | null;
  systemHealth: 'healthy' | 'degraded' | 'critical';
  
  // Export functionality
  exportMetrics: () => {
    snapshot: PerformanceSnapshot;
    events: MetricEvent[];
    summary: {
      monitoringDuration: number;
      totalEvents: number;
      averageLatency: number;
      errorRate: number;
      recoveryRate: number;
      systemHealth: string;
    };
  };
}

export const usePerformanceMonitoring = (
  config: PerformanceMonitoringConfig = { enabled: true }
): PerformanceMonitoringReturn => {
  // State management
  const [isMonitoring, setIsMonitoring] = useState(false);
  const [lastUpdate, setLastUpdate] = useState(0);
  const [currentSnapshot, setCurrentSnapshot] = useState<PerformanceSnapshot | null>(null);
  const [connectionMetrics, setConnectionMetrics] = useState<ConnectionMetrics | null>(null);
  const [audioMetrics, setAudioMetrics] = useState<AudioMetrics | null>(null);
  const [errorMetrics, setErrorMetrics] = useState<ErrorMetrics | null>(null);
  const [systemHealth, setSystemHealth] = useState<'healthy' | 'degraded' | 'critical'>('healthy');

  // Refs
  const updateTimerRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const serviceRef = useRef<PerformanceMonitoringService>(performanceMonitor);

  // Configuration with defaults
  const effectiveConfig = {
    enabled: config.enabled,
    updateInterval: config.updateInterval || 1000,
    maxEventHistory: config.maxEventHistory || 1000,
    autoStart: config.autoStart !== false,
    enableConnectionMonitoring: config.enableConnectionMonitoring !== false,
    enableAudioMonitoring: config.enableAudioMonitoring !== false,
    enableErrorTracking: config.enableErrorTracking !== false,
  };

  // Start monitoring
  const startMonitoring = useCallback(() => {
    if (!effectiveConfig.enabled || isMonitoring) {
      return;
    }

    serviceRef.current.startMonitoring();
    setIsMonitoring(true);

    // Start periodic updates
    updateTimerRef.current = setInterval(() => {
      updateMetrics();
    }, effectiveConfig.updateInterval);

    console.log('Performance monitoring started');
  }, [effectiveConfig.enabled, effectiveConfig.updateInterval, isMonitoring]);

  // Stop monitoring
  const stopMonitoring = useCallback(() => {
    if (!isMonitoring) {
      return;
    }

    serviceRef.current.stopMonitoring();
    setIsMonitoring(false);

    if (updateTimerRef.current) {
      clearInterval(updateTimerRef.current);
      updateTimerRef.current = null;
    }

    console.log('Performance monitoring stopped');
  }, [isMonitoring]);

  // Reset metrics
  const resetMetrics = useCallback(() => {
    serviceRef.current.reset();
    setCurrentSnapshot(null);
    setConnectionMetrics(null);
    setAudioMetrics(null);
    setErrorMetrics(null);
    setSystemHealth('healthy');
    setLastUpdate(0);
    
    console.log('Performance metrics reset');
  }, []);

  // Update metrics from service
  const updateMetrics = useCallback(() => {
    const snapshot = serviceRef.current.getPerformanceSnapshot();
    const now = Date.now();

    setCurrentSnapshot(snapshot);
    setConnectionMetrics(snapshot.connection);
    setAudioMetrics(snapshot.audio);
    setErrorMetrics(snapshot.errors);
    setSystemHealth(snapshot.systemHealth);
    setLastUpdate(now);
  }, []);

  // Record connection metrics
  const recordConnectionMetrics = useCallback((data: {
    latency?: number;
    quality?: 'good' | 'poor' | 'critical';
    connected?: boolean;
    messagesSent?: number;
    messagesReceived?: number;
    messageFailures?: number;
  }) => {
    if (!effectiveConfig.enableConnectionMonitoring) {
      return;
    }

    serviceRef.current.recordConnectionMetrics(data);

    // Record specific events
    if (data.connected === true) {
      serviceRef.current.recordEvent({
        type: 'connection_established',
        timestamp: Date.now()
      });
    } else if (data.connected === false) {
      serviceRef.current.recordEvent({
        type: 'connection_lost',
        timestamp: Date.now()
      });
    }

    if (data.messagesSent !== undefined) {
      serviceRef.current.recordEvent({
        type: 'message_sent',
        timestamp: Date.now(),
        data: { count: data.messagesSent }
      });
    }

    if (data.messagesReceived !== undefined) {
      serviceRef.current.recordEvent({
        type: 'message_received',
        timestamp: Date.now(),
        data: { count: data.messagesReceived }
      });
    }

    if (data.messageFailures !== undefined) {
      serviceRef.current.recordEvent({
        type: 'message_failed',
        timestamp: Date.now(),
        data: { count: data.messageFailures }
      });
    }
  }, [effectiveConfig.enableConnectionMonitoring]);

  // Record audio metrics
  const recordAudioMetrics = useCallback((data: {
    isStreaming?: boolean;
    bytesTransmitted?: number;
    packetsTransmitted?: number;
    packetsLost?: number;
    deviceSwitches?: number;
    permissionDenials?: number;
    captureErrors?: number;
    audioQuality?: 'good' | 'degraded' | 'poor';
  }) => {
    if (!effectiveConfig.enableAudioMonitoring) {
      return;
    }

    serviceRef.current.recordAudioMetrics(data);

    // Record specific events
    if (data.isStreaming === true) {
      serviceRef.current.recordEvent({
        type: 'audio_stream_start',
        timestamp: Date.now()
      });
    } else if (data.isStreaming === false) {
      serviceRef.current.recordEvent({
        type: 'audio_stream_stop',
        timestamp: Date.now()
      });
    }

    if (data.bytesTransmitted !== undefined && data.bytesTransmitted > 0) {
      serviceRef.current.recordEvent({
        type: 'audio_data_sent',
        timestamp: Date.now(),
        data: { bytes: data.bytesTransmitted }
      });
    }

    if (data.packetsLost !== undefined && data.packetsLost > 0) {
      serviceRef.current.recordEvent({
        type: 'audio_packet_lost',
        timestamp: Date.now(),
        data: { count: data.packetsLost }
      });
    }

    if (data.deviceSwitches !== undefined && data.deviceSwitches > 0) {
      serviceRef.current.recordEvent({
        type: 'audio_device_switch',
        timestamp: Date.now()
      });
    }

    if (data.permissionDenials !== undefined && data.permissionDenials > 0) {
      serviceRef.current.recordEvent({
        type: 'audio_permission_denied',
        timestamp: Date.now()
      });
    }

    if (data.captureErrors !== undefined && data.captureErrors > 0) {
      serviceRef.current.recordEvent({
        type: 'audio_capture_error',
        timestamp: Date.now()
      });
    }
  }, [effectiveConfig.enableAudioMonitoring]);

  // Record service initialization
  const recordServiceInitialization = useCallback((
    service: 'websocket' | 'audio' | 'errorReporting',
    duration: number,
    success: boolean
  ) => {
    serviceRef.current.recordServiceInitialization(service, duration, success);

    serviceRef.current.recordEvent({
      type: success ? 'service_init_complete' : 'service_init_failed',
      timestamp: Date.now(),
      data: { service },
      duration
    });
  }, []);

  // Record error
  const recordError = useCallback((
    errorType: 'connection' | 'audio' | 'configuration' | 'system',
    error?: Error
  ) => {
    if (!effectiveConfig.enableErrorTracking) {
      return;
    }

    serviceRef.current.recordError(errorType, error);
  }, [effectiveConfig.enableErrorTracking]);

  // Record recovery
  const recordRecovery = useCallback((success: boolean, errorType?: string) => {
    if (!effectiveConfig.enableErrorTracking) {
      return;
    }

    serviceRef.current.recordRecovery(success, errorType);
  }, [effectiveConfig.enableErrorTracking]);

  // Record custom event
  const recordEvent = useCallback((event: MetricEvent) => {
    serviceRef.current.recordEvent(event);
  }, []);

  // Data access methods
  const getPerformanceSnapshot = useCallback(() => {
    return serviceRef.current.getPerformanceSnapshot();
  }, []);

  const getConnectionMetrics = useCallback(() => {
    return serviceRef.current.getConnectionMetrics();
  }, []);

  const getAudioMetrics = useCallback(() => {
    return serviceRef.current.getAudioMetrics();
  }, []);

  const getServiceInitializationMetrics = useCallback(() => {
    return serviceRef.current.getServiceInitializationMetrics();
  }, []);

  const getErrorMetrics = useCallback(() => {
    return serviceRef.current.getErrorMetrics();
  }, []);

  const getRecentEvents = useCallback((count: number = 50) => {
    return serviceRef.current.getRecentEvents(count);
  }, []);

  const getEventsByType = useCallback((type: MetricEventType, limit: number = 50) => {
    return serviceRef.current.getEventsByType(type, limit);
  }, []);

  // Export metrics
  const exportMetrics = useCallback(() => {
    return serviceRef.current.exportMetrics();
  }, []);

  // Auto-start monitoring if enabled
  useEffect(() => {
    if (effectiveConfig.enabled && effectiveConfig.autoStart) {
      startMonitoring();
    }

    return () => {
      stopMonitoring();
    };
  }, [effectiveConfig.enabled, effectiveConfig.autoStart, startMonitoring, stopMonitoring]);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      if (updateTimerRef.current) {
        clearInterval(updateTimerRef.current);
      }
    };
  }, []);

  return {
    // Monitoring state
    isMonitoring,
    lastUpdate,
    
    // Control methods
    startMonitoring,
    stopMonitoring,
    resetMetrics,
    
    // Metric recording methods
    recordConnectionMetrics,
    recordAudioMetrics,
    recordServiceInitialization,
    recordError,
    recordRecovery,
    recordEvent,
    
    // Data access methods
    getPerformanceSnapshot,
    getConnectionMetrics,
    getAudioMetrics,
    getServiceInitializationMetrics,
    getErrorMetrics,
    getRecentEvents,
    getEventsByType,
    
    // Real-time metrics
    currentSnapshot,
    connectionMetrics,
    audioMetrics,
    errorMetrics,
    systemHealth,
    
    // Export functionality
    exportMetrics,
  };
};