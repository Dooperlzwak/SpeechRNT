/**
 * Performance Monitoring Tests
 * 
 * Tests for the PerformanceMonitoringService and usePerformanceMonitoring hook
 */

import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { renderHook, act } from '@testing-library/react';
import { PerformanceMonitoringService } from '../services/PerformanceMonitoringService';
import { usePerformanceMonitoring } from '../hooks/usePerformanceMonitoring';

describe('PerformanceMonitoringService', () => {
  let service: PerformanceMonitoringService;

  beforeEach(() => {
    service = new PerformanceMonitoringService();
    vi.useFakeTimers();
  });

  afterEach(() => {
    service.stopMonitoring();
    vi.useRealTimers();
  });

  describe('Basic Functionality', () => {
    it('should initialize with default metrics', () => {
      const snapshot = service.getPerformanceSnapshot();
      
      expect(snapshot.connection.latency).toBe(0);
      expect(snapshot.connection.quality).toBe('good');
      expect(snapshot.audio.isStreaming).toBe(false);
      expect(snapshot.errors.totalErrors).toBe(0);
      expect(snapshot.systemHealth).toBe('healthy');
    });

    it('should start and stop monitoring', () => {
      expect(service['isMonitoring']).toBe(false);
      
      service.startMonitoring();
      expect(service['isMonitoring']).toBe(true);
      
      service.stopMonitoring();
      expect(service['isMonitoring']).toBe(false);
    });

    it('should record events', () => {
      service.recordEvent({
        type: 'connection_established',
        timestamp: Date.now()
      });

      const events = service.getRecentEvents(10);
      expect(events).toHaveLength(1);
      expect(events[0].type).toBe('connection_established');
    });
  });

  describe('Connection Metrics', () => {
    it('should record connection metrics', () => {
      service.recordConnectionMetrics({
        latency: 100,
        quality: 'good',
        connected: true,
        messagesSent: 5,
        messagesReceived: 3,
        messageFailures: 1
      });

      const metrics = service.getConnectionMetrics();
      expect(metrics.latency).toBe(100);
      expect(metrics.quality).toBe('good');
      expect(metrics.messagesSent).toBe(5);
      expect(metrics.messagesReceived).toBe(3);
      expect(metrics.messageFailures).toBe(1);
    });

    it('should calculate average latency', () => {
      service.recordConnectionMetrics({ latency: 100 });
      service.recordConnectionMetrics({ latency: 200 });
      service.recordConnectionMetrics({ latency: 150 });

      const metrics = service.getConnectionMetrics();
      expect(metrics.averageLatency).toBe(150);
    });

    it('should track connection uptime', () => {
      const startTime = Date.now();
      service.recordConnectionMetrics({ connected: true });
      
      vi.advanceTimersByTime(5000); // 5 seconds
      
      const metrics = service.getConnectionMetrics();
      expect(metrics.connectionUptime).toBeGreaterThanOrEqual(5000);
    });
  });

  describe('Audio Metrics', () => {
    it('should record audio metrics', () => {
      service.setAudioMetrics({
        isStreaming: true,
        bytesTransmitted: 1024,
        packetsTransmitted: 10,
        packetsLost: 1,
        audioQuality: 'good'
      });

      const metrics = service.getAudioMetrics();
      expect(metrics.isStreaming).toBe(true);
      expect(metrics.bytesTransmitted).toBe(1024);
      expect(metrics.packetsTransmitted).toBe(10);
      expect(metrics.packetsLost).toBe(1);
      expect(metrics.audioQuality).toBe('good');
    });

    it('should calculate average packet size', () => {
      service.setAudioMetrics({
        bytesTransmitted: 2048,
        packetsTransmitted: 4
      });

      const metrics = service.getAudioMetrics();
      expect(metrics.averagePacketSize).toBe(512); // 2048 / 4
    });

    it('should track streaming duration', () => {
      const startTime = Date.now();
      service.setAudioMetrics({ isStreaming: true });
      
      vi.advanceTimersByTime(3000); // 3 seconds
      
      const metrics = service.getAudioMetrics();
      expect(metrics.streamingDuration).toBeGreaterThanOrEqual(3000);
    });
  });

  describe('Service Initialization Metrics', () => {
    it('should record service initialization', () => {
      service.recordServiceInitialization('websocket', 500, true);
      service.recordServiceInitialization('audio', 300, true);
      service.recordServiceInitialization('errorReporting', 100, true);

      const metrics = service.getServiceInitializationMetrics();
      expect(metrics.webSocketInitTime).toBe(500);
      expect(metrics.audioInitTime).toBe(300);
      expect(metrics.errorReportingInitTime).toBe(100);
      expect(metrics.totalInitTime).toBe(900);
      expect(metrics.servicesReady).toBe(true);
    });

    it('should track initialization failures', () => {
      service.recordServiceInitialization('websocket', 500, false);

      const metrics = service.getServiceInitializationMetrics();
      expect(metrics.initializationFailures).toBe(1);
      expect(metrics.servicesReady).toBe(false);
    });
  });

  describe('Error Metrics', () => {
    it('should record errors', () => {
      const error = new Error('Test error');
      service.recordError('connection', error);

      const metrics = service.getErrorMetrics();
      expect(metrics.totalErrors).toBe(1);
      expect(metrics.connectionErrors).toBe(1);
      expect(metrics.lastErrorTime).toBeDefined();
    });

    it('should record recovery attempts', () => {
      service.recordRecovery(true, 'connection');
      service.recordRecovery(false, 'audio');

      const metrics = service.getErrorMetrics();
      expect(metrics.recoveryAttempts).toBe(2);
      expect(metrics.successfulRecoveries).toBe(1);
      expect(metrics.failedRecoveries).toBe(1);
      expect(metrics.recoverySuccessRate).toBe(50);
    });

    it('should calculate error rate', () => {
      // Record multiple errors within the time window
      service.recordError('connection');
      service.recordError('audio');
      service.recordError('system');

      const metrics = service.getErrorMetrics();
      expect(metrics.errorRate).toBe(3); // 3 errors in current minute
    });
  });

  describe('System Health', () => {
    it('should report healthy status by default', () => {
      const snapshot = service.getPerformanceSnapshot();
      expect(snapshot.systemHealth).toBe('healthy');
    });

    it('should report critical status for poor conditions', () => {
      service.recordConnectionMetrics({ quality: 'critical' });
      
      const snapshot = service.getPerformanceSnapshot();
      expect(snapshot.systemHealth).toBe('critical');
    });

    it('should report degraded status for moderate issues', () => {
      service.recordConnectionMetrics({ quality: 'poor' });
      
      const snapshot = service.getPerformanceSnapshot();
      expect(snapshot.systemHealth).toBe('degraded');
    });
  });

  describe('Data Export', () => {
    it('should export metrics data', () => {
      service.recordConnectionMetrics({ latency: 100 });
      service.recordAudioMetrics({ bytesTransmitted: 1024 });
      service.recordError('connection');

      const exportData = service.exportMetrics();
      
      expect(exportData.snapshot).toBeDefined();
      expect(exportData.events).toBeDefined();
      expect(exportData.summary).toBeDefined();
      expect(exportData.summary.totalEvents).toBeGreaterThan(0);
    });
  });
});

describe('usePerformanceMonitoring Hook', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('should initialize with default configuration', () => {
    const { result } = renderHook(() => 
      usePerformanceMonitoring({ enabled: true })
    );

    expect(result.current.isMonitoring).toBe(true);
    expect(result.current.systemHealth).toBe('healthy');
  });

  it('should start and stop monitoring', () => {
    const { result } = renderHook(() => 
      usePerformanceMonitoring({ enabled: true, autoStart: false })
    );

    expect(result.current.isMonitoring).toBe(false);

    act(() => {
      result.current.startMonitoring();
    });

    expect(result.current.isMonitoring).toBe(true);

    act(() => {
      result.current.stopMonitoring();
    });

    expect(result.current.isMonitoring).toBe(false);
  });

  it('should record connection metrics', () => {
    const { result } = renderHook(() => 
      usePerformanceMonitoring({ enabled: true })
    );

    act(() => {
      result.current.recordConnectionMetrics({
        latency: 150,
        quality: 'good',
        connected: true
      });
    });

    const metrics = result.current.getConnectionMetrics();
    expect(metrics.latency).toBe(150);
    expect(metrics.quality).toBe('good');
  });

  it('should record audio metrics', () => {
    const { result } = renderHook(() => 
      usePerformanceMonitoring({ enabled: true })
    );

    act(() => {
      result.current.recordAudioMetrics({
        isStreaming: true,
        bytesTransmitted: 2048,
        audioQuality: 'good'
      });
    });

    const metrics = result.current.getAudioMetrics();
    expect(metrics.isStreaming).toBe(true);
    expect(metrics.bytesTransmitted).toBeGreaterThanOrEqual(2048); // May be accumulated
    expect(metrics.audioQuality).toBe('good');
  });

  it('should record service initialization', () => {
    const { result } = renderHook(() => 
      usePerformanceMonitoring({ enabled: true })
    );

    act(() => {
      result.current.recordServiceInitialization('websocket', 400, true);
    });

    const metrics = result.current.getServiceInitializationMetrics();
    expect(metrics.webSocketInitTime).toBe(400);
  });

  it('should record errors and recovery', () => {
    const { result } = renderHook(() => 
      usePerformanceMonitoring({ enabled: true })
    );

    const error = new Error('Test error');

    act(() => {
      result.current.recordError('connection', error);
      result.current.recordRecovery(true, 'connection');
    });

    const metrics = result.current.getErrorMetrics();
    expect(metrics.totalErrors).toBe(1);
    expect(metrics.connectionErrors).toBe(1);
    expect(metrics.recoveryAttempts).toBe(1);
    expect(metrics.successfulRecoveries).toBe(1);
  });

  it('should export metrics', () => {
    const { result } = renderHook(() => 
      usePerformanceMonitoring({ enabled: true })
    );

    act(() => {
      result.current.recordConnectionMetrics({ latency: 100 });
    });

    const exportData = result.current.exportMetrics();
    expect(exportData.snapshot).toBeDefined();
    expect(exportData.events).toBeDefined();
    expect(exportData.summary).toBeDefined();
  });

  it('should reset metrics', () => {
    const { result } = renderHook(() => 
      usePerformanceMonitoring({ enabled: true, autoStart: false })
    );

    act(() => {
      result.current.startMonitoring();
      result.current.recordConnectionMetrics({ latency: 100 });
      result.current.recordError('connection');
    });

    let metrics = result.current.getErrorMetrics();
    expect(metrics.totalErrors).toBeGreaterThanOrEqual(1);

    act(() => {
      result.current.resetMetrics();
    });

    metrics = result.current.getErrorMetrics();
    expect(metrics.totalErrors).toBe(0);
  });

  it('should handle disabled monitoring', () => {
    const { result } = renderHook(() => 
      usePerformanceMonitoring({ enabled: false })
    );

    expect(result.current.isMonitoring).toBe(false);

    act(() => {
      result.current.recordConnectionMetrics({ latency: 100 });
    });

    // Should still work even when disabled for testing purposes
    const metrics = result.current.getConnectionMetrics();
    expect(metrics).toBeDefined();
  });
});