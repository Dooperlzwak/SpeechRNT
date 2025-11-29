/**
 * PerformanceMonitoringService - Comprehensive performance monitoring for Vocr
 * 
 * Monitors connection quality, audio streaming performance, service initialization timing,
 * and error rates with recovery success tracking.
 */

export interface ConnectionMetrics {
  latency: number;
  quality: 'good' | 'poor' | 'critical';
  reconnectAttempts: number;
  lastReconnectTime: number | null;
  totalDisconnections: number;
  averageLatency: number;
  connectionUptime: number;
  connectionStartTime: number;
  heartbeatMissed: number;
  messagesSent: number;
  messagesReceived: number;
  messageFailures: number;
}

export interface AudioMetrics {
  isStreaming: boolean;
  bytesTransmitted: number;
  packetsTransmitted: number;
  packetsLost: number;
  averagePacketSize: number;
  streamingDuration: number;
  streamStartTime: number | null;
  deviceSwitches: number;
  permissionDenials: number;
  captureErrors: number;
  lastCaptureError: number | null;
  audioQuality: 'good' | 'degraded' | 'poor';
}

export interface ServiceInitializationMetrics {
  webSocketInitTime: number | null;
  audioInitTime: number | null;
  errorReportingInitTime: number | null;
  totalInitTime: number | null;
  initializationAttempts: number;
  initializationFailures: number;
  lastInitializationTime: number | null;
  servicesReady: boolean;
}

export interface ErrorMetrics {
  totalErrors: number;
  connectionErrors: number;
  audioErrors: number;
  configurationErrors: number;
  systemErrors: number;
  recoveryAttempts: number;
  successfulRecoveries: number;
  failedRecoveries: number;
  errorRate: number; // errors per minute
  recoverySuccessRate: number; // percentage
  lastErrorTime: number | null;
  lastRecoveryTime: number | null;
}

export interface PerformanceSnapshot {
  timestamp: number;
  connection: ConnectionMetrics;
  audio: AudioMetrics;
  serviceInit: ServiceInitializationMetrics;
  errors: ErrorMetrics;
  systemHealth: 'healthy' | 'degraded' | 'critical';
}

export type MetricEventType =
  | 'connection_established'
  | 'connection_lost'
  | 'reconnection_attempt'
  | 'reconnection_success'
  | 'reconnection_failure'
  | 'message_sent'
  | 'message_received'
  | 'message_failed'
  | 'heartbeat_sent'
  | 'heartbeat_received'
  | 'heartbeat_missed'
  | 'audio_stream_start'
  | 'audio_stream_stop'
  | 'audio_data_sent'
  | 'audio_packet_lost'
  | 'audio_device_switch'
  | 'audio_permission_denied'
  | 'audio_capture_error'
  | 'service_init_start'
  | 'service_init_complete'
  | 'service_init_failed'
  | 'error_occurred'
  | 'recovery_attempt'
  | 'recovery_success'
  | 'recovery_failure';

export interface MetricEvent {
  type: MetricEventType;
  timestamp: number;
  data?: any;
  duration?: number;
  error?: Error;
}

export class PerformanceMonitoringService {
  private connectionMetrics: ConnectionMetrics = {
    latency: 0,
    quality: 'good',
    reconnectAttempts: 0,
    lastReconnectTime: null,
    totalDisconnections: 0,
    averageLatency: 0,
    connectionUptime: 0,
    connectionStartTime: 0,
    heartbeatMissed: 0,
    messagesSent: 0,
    messagesReceived: 0,
    messageFailures: 0
  };
  private audioMetrics: AudioMetrics = {
    isStreaming: false,
    bytesTransmitted: 0,
    packetsTransmitted: 0,
    packetsLost: 0,
    averagePacketSize: 0,
    streamingDuration: 0,
    streamStartTime: null,
    deviceSwitches: 0,
    permissionDenials: 0,
    captureErrors: 0,
    lastCaptureError: null,
    audioQuality: 'good'
  };
  private serviceInitMetrics: ServiceInitializationMetrics = {
    webSocketInitTime: null,
    audioInitTime: null,
    errorReportingInitTime: null,
    totalInitTime: null,
    initializationAttempts: 0,
    initializationFailures: 0,
    lastInitializationTime: null,
    servicesReady: false
  };
  private errorMetrics: ErrorMetrics = {
    totalErrors: 0,
    connectionErrors: 0,
    audioErrors: 0,
    configurationErrors: 0,
    systemErrors: 0,
    recoveryAttempts: 0,
    successfulRecoveries: 0,
    failedRecoveries: 0,
    errorRate: 0,
    recoverySuccessRate: 0,
    lastErrorTime: null,
    lastRecoveryTime: null
  };

  private eventHistory: MetricEvent[] = [];
  private maxEventHistory = 1000;

  private latencyMeasurements: number[] = [];
  private maxLatencyMeasurements = 100;

  private errorTimestamps: number[] = [];
  private errorRateWindow = 60000; // 1 minute

  private monitoringStartTime: number;
  private isMonitoring = false;

  private performanceTimer: ReturnType<typeof setInterval> | null = null;
  private metricsUpdateInterval = 1000; // 1 second

  constructor() {
    this.monitoringStartTime = Date.now();
    this.initializeMetrics();
  }

  /**
   * Start performance monitoring
   */
  startMonitoring(): void {
    if (this.isMonitoring) {
      return;
    }

    this.isMonitoring = true;
    this.monitoringStartTime = Date.now();

    // Start periodic metrics updates
    this.performanceTimer = setInterval(() => {
      this.updateDerivedMetrics();
    }, this.metricsUpdateInterval);

    this.recordEvent({
      type: 'service_init_start',
      timestamp: Date.now()
    });
  }

  /**
   * Stop performance monitoring
   */
  stopMonitoring(): void {
    if (!this.isMonitoring) {
      return;
    }

    this.isMonitoring = false;

    if (this.performanceTimer) {
      clearInterval(this.performanceTimer);
      this.performanceTimer = null;
    }
  }

  /**
   * Record a metric event
   */
  recordEvent(event: MetricEvent): void {
    // Add to event history
    this.eventHistory.push(event);
    if (this.eventHistory.length > this.maxEventHistory) {
      this.eventHistory.shift();
    }

    // Update specific metrics based on event type
    this.processEvent(event);
  }

  /**
   * Record connection quality metrics
   */
  recordConnectionMetrics(data: {
    latency?: number;
    quality?: 'good' | 'poor' | 'critical';
    connected?: boolean;
    messagesSent?: number;
    messagesReceived?: number;
    messageFailures?: number;
  }): void {
    const now = Date.now();

    if (data.latency !== undefined) {
      this.latencyMeasurements.push(data.latency);
      if (this.latencyMeasurements.length > this.maxLatencyMeasurements) {
        this.latencyMeasurements.shift();
      }
      this.connectionMetrics.latency = data.latency;
    }

    if (data.quality !== undefined) {
      this.connectionMetrics.quality = data.quality;
    }

    if (data.connected === true && this.connectionMetrics.connectionStartTime === 0) {
      this.connectionMetrics.connectionStartTime = now;
    }

    if (data.messagesSent !== undefined) {
      this.connectionMetrics.messagesSent = data.messagesSent;
    }

    if (data.messagesReceived !== undefined) {
      this.connectionMetrics.messagesReceived = data.messagesReceived;
    }

    if (data.messageFailures !== undefined) {
      this.connectionMetrics.messageFailures = data.messageFailures;
    }
  }

  /**
   * Record audio streaming metrics
   */
  recordAudioMetrics(data: {
    isStreaming?: boolean;
    bytesTransmitted?: number;
    packetsTransmitted?: number;
    packetsLost?: number;
    deviceSwitches?: number;
    permissionDenials?: number;
    captureErrors?: number;
    audioQuality?: 'good' | 'degraded' | 'poor';
  }): void {
    const now = Date.now();

    if (data.isStreaming === true && this.audioMetrics.streamStartTime === null) {
      this.audioMetrics.streamStartTime = now;
    } else if (data.isStreaming === false) {
      this.audioMetrics.streamStartTime = null;
    }

    if (data.isStreaming !== undefined) {
      this.audioMetrics.isStreaming = data.isStreaming;
    }

    if (data.bytesTransmitted !== undefined) {
      this.audioMetrics.bytesTransmitted += data.bytesTransmitted;
    }

    if (data.packetsTransmitted !== undefined) {
      this.audioMetrics.packetsTransmitted += data.packetsTransmitted;
    }

    if (data.packetsLost !== undefined) {
      this.audioMetrics.packetsLost += data.packetsLost;
    }

    if (data.deviceSwitches !== undefined) {
      this.audioMetrics.deviceSwitches += data.deviceSwitches;
    }

    if (data.permissionDenials !== undefined) {
      this.audioMetrics.permissionDenials += data.permissionDenials;
    }

    if (data.captureErrors !== undefined) {
      this.audioMetrics.captureErrors += data.captureErrors;
      this.audioMetrics.lastCaptureError = now;
    }

    if (data.audioQuality !== undefined) {
      this.audioMetrics.audioQuality = data.audioQuality;
    }
  }

  /**
   * Set absolute audio metrics (for testing)
   */
  setAudioMetrics(data: {
    isStreaming?: boolean;
    bytesTransmitted?: number;
    packetsTransmitted?: number;
    packetsLost?: number;
    deviceSwitches?: number;
    permissionDenials?: number;
    captureErrors?: number;
    audioQuality?: 'good' | 'degraded' | 'poor';
  }): void {
    const now = Date.now();

    if (data.isStreaming === true && this.audioMetrics.streamStartTime === null) {
      this.audioMetrics.streamStartTime = now;
    } else if (data.isStreaming === false) {
      this.audioMetrics.streamStartTime = null;
    }

    if (data.isStreaming !== undefined) {
      this.audioMetrics.isStreaming = data.isStreaming;
    }

    if (data.bytesTransmitted !== undefined) {
      this.audioMetrics.bytesTransmitted = data.bytesTransmitted;
    }

    if (data.packetsTransmitted !== undefined) {
      this.audioMetrics.packetsTransmitted = data.packetsTransmitted;
    }

    if (data.packetsLost !== undefined) {
      this.audioMetrics.packetsLost = data.packetsLost;
    }

    if (data.deviceSwitches !== undefined) {
      this.audioMetrics.deviceSwitches = data.deviceSwitches;
    }

    if (data.permissionDenials !== undefined) {
      this.audioMetrics.permissionDenials = data.permissionDenials;
    }

    if (data.captureErrors !== undefined) {
      this.audioMetrics.captureErrors = data.captureErrors;
      this.audioMetrics.lastCaptureError = now;
    }

    if (data.audioQuality !== undefined) {
      this.audioMetrics.audioQuality = data.audioQuality;
    }
  }

  /**
   * Record service initialization metrics
   */
  recordServiceInitialization(service: 'websocket' | 'audio' | 'errorReporting', duration: number, success: boolean): void {
    const now = Date.now();

    if (success) {
      switch (service) {
        case 'websocket':
          this.serviceInitMetrics.webSocketInitTime = duration;
          break;
        case 'audio':
          this.serviceInitMetrics.audioInitTime = duration;
          break;
        case 'errorReporting':
          this.serviceInitMetrics.errorReportingInitTime = duration;
          break;
      }
    } else {
      this.serviceInitMetrics.initializationFailures++;
    }

    this.serviceInitMetrics.initializationAttempts++;
    this.serviceInitMetrics.lastInitializationTime = now;

    // Calculate total initialization time
    const times = [
      this.serviceInitMetrics.webSocketInitTime,
      this.serviceInitMetrics.audioInitTime,
      this.serviceInitMetrics.errorReportingInitTime
    ].filter(t => t !== null) as number[];

    if (times.length > 0) {
      this.serviceInitMetrics.totalInitTime = times.reduce((sum, time) => sum + time, 0);
    }

    // Check if all services are ready
    this.serviceInitMetrics.servicesReady =
      this.serviceInitMetrics.webSocketInitTime !== null &&
      this.serviceInitMetrics.audioInitTime !== null &&
      this.serviceInitMetrics.errorReportingInitTime !== null;
  }

  /**
   * Record error and recovery metrics
   */
  recordError(errorType: 'connection' | 'audio' | 'configuration' | 'system', error?: Error): void {
    const now = Date.now();

    this.errorMetrics.totalErrors++;
    this.errorMetrics.lastErrorTime = now;

    // Add to error timestamps for rate calculation
    this.errorTimestamps.push(now);
    this.cleanupOldErrorTimestamps();

    switch (errorType) {
      case 'connection':
        this.errorMetrics.connectionErrors++;
        break;
      case 'audio':
        this.errorMetrics.audioErrors++;
        break;
      case 'configuration':
        this.errorMetrics.configurationErrors++;
        break;
      case 'system':
        this.errorMetrics.systemErrors++;
        break;
    }

    this.recordEvent({
      type: 'error_occurred',
      timestamp: now,
      data: { errorType },
      error
    });
  }

  /**
   * Record recovery attempt and result
   */
  recordRecovery(success: boolean, errorType?: string): void {
    const now = Date.now();

    this.errorMetrics.recoveryAttempts++;
    this.errorMetrics.lastRecoveryTime = now;

    if (success) {
      this.errorMetrics.successfulRecoveries++;
      this.recordEvent({
        type: 'recovery_success',
        timestamp: now,
        data: { errorType }
      });
    } else {
      this.errorMetrics.failedRecoveries++;
      this.recordEvent({
        type: 'recovery_failure',
        timestamp: now,
        data: { errorType }
      });
    }
  }

  /**
   * Get current performance snapshot
   */
  getPerformanceSnapshot(): PerformanceSnapshot {
    this.updateDerivedMetrics();

    return {
      timestamp: Date.now(),
      connection: { ...this.connectionMetrics },
      audio: { ...this.audioMetrics },
      serviceInit: { ...this.serviceInitMetrics },
      errors: { ...this.errorMetrics },
      systemHealth: this.calculateSystemHealth()
    };
  }

  /**
   * Get connection quality metrics
   */
  getConnectionMetrics(): ConnectionMetrics {
    this.updateConnectionMetrics();
    return { ...this.connectionMetrics };
  }

  /**
   * Get audio streaming metrics
   */
  getAudioMetrics(): AudioMetrics {
    this.updateAudioMetrics();
    return { ...this.audioMetrics };
  }

  /**
   * Get service initialization metrics
   */
  getServiceInitializationMetrics(): ServiceInitializationMetrics {
    return { ...this.serviceInitMetrics };
  }

  /**
   * Get error and recovery metrics
   */
  getErrorMetrics(): ErrorMetrics {
    this.updateErrorMetrics();
    return { ...this.errorMetrics };
  }

  /**
   * Get recent events
   */
  getRecentEvents(count: number = 50): MetricEvent[] {
    return this.eventHistory.slice(-count);
  }

  /**
   * Get events by type
   */
  getEventsByType(type: MetricEventType, limit: number = 50): MetricEvent[] {
    return this.eventHistory
      .filter(event => event.type === type)
      .slice(-limit);
  }

  /**
   * Clear all metrics and reset
   */
  reset(): void {
    this.initializeMetrics();
    this.eventHistory = [];
    this.latencyMeasurements = [];
    this.errorTimestamps = [];
    this.monitoringStartTime = Date.now();
  }

  /**
   * Export metrics for external reporting
   */
  exportMetrics(): {
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
  } {
    const snapshot = this.getPerformanceSnapshot();
    const now = Date.now();

    return {
      snapshot,
      events: this.eventHistory,
      summary: {
        monitoringDuration: now - this.monitoringStartTime,
        totalEvents: this.eventHistory.length,
        averageLatency: this.connectionMetrics.averageLatency,
        errorRate: this.errorMetrics.errorRate,
        recoveryRate: this.errorMetrics.recoverySuccessRate,
        systemHealth: snapshot.systemHealth
      }
    };
  }

  private initializeMetrics(): void {
    this.connectionMetrics = {
      latency: 0,
      quality: 'good',
      reconnectAttempts: 0,
      lastReconnectTime: null,
      totalDisconnections: 0,
      averageLatency: 0,
      connectionUptime: 0,
      connectionStartTime: 0,
      heartbeatMissed: 0,
      messagesSent: 0,
      messagesReceived: 0,
      messageFailures: 0
    };

    this.audioMetrics = {
      isStreaming: false,
      bytesTransmitted: 0,
      packetsTransmitted: 0,
      packetsLost: 0,
      averagePacketSize: 0,
      streamingDuration: 0,
      streamStartTime: null,
      deviceSwitches: 0,
      permissionDenials: 0,
      captureErrors: 0,
      lastCaptureError: null,
      audioQuality: 'good'
    };

    this.serviceInitMetrics = {
      webSocketInitTime: null,
      audioInitTime: null,
      errorReportingInitTime: null,
      totalInitTime: null,
      initializationAttempts: 0,
      initializationFailures: 0,
      lastInitializationTime: null,
      servicesReady: false
    };

    this.errorMetrics = {
      totalErrors: 0,
      connectionErrors: 0,
      audioErrors: 0,
      configurationErrors: 0,
      systemErrors: 0,
      recoveryAttempts: 0,
      successfulRecoveries: 0,
      failedRecoveries: 0,
      errorRate: 0,
      recoverySuccessRate: 0,
      lastErrorTime: null,
      lastRecoveryTime: null
    };
  }

  private processEvent(event: MetricEvent): void {
    const now = Date.now();

    switch (event.type) {
      case 'connection_established':
        this.connectionMetrics.connectionStartTime = now;
        break;

      case 'connection_lost':
        this.connectionMetrics.totalDisconnections++;
        break;

      case 'reconnection_attempt':
        this.connectionMetrics.reconnectAttempts++;
        this.connectionMetrics.lastReconnectTime = now;
        break;

      case 'message_sent':
        this.connectionMetrics.messagesSent++;
        break;

      case 'message_received':
        this.connectionMetrics.messagesReceived++;
        break;

      case 'message_failed':
        this.connectionMetrics.messageFailures++;
        break;

      case 'heartbeat_missed':
        this.connectionMetrics.heartbeatMissed++;
        break;

      case 'audio_stream_start':
        this.audioMetrics.streamStartTime = now;
        this.audioMetrics.isStreaming = true;
        break;

      case 'audio_stream_stop':
        this.audioMetrics.isStreaming = false;
        if (this.audioMetrics.streamStartTime) {
          this.audioMetrics.streamingDuration += now - this.audioMetrics.streamStartTime;
        }
        this.audioMetrics.streamStartTime = null;
        break;

      case 'audio_data_sent':
        this.audioMetrics.packetsTransmitted++;
        if (event.data?.bytes) {
          this.audioMetrics.bytesTransmitted += event.data.bytes;
        }
        break;

      case 'audio_packet_lost':
        this.audioMetrics.packetsLost++;
        break;

      case 'audio_device_switch':
        this.audioMetrics.deviceSwitches++;
        break;

      case 'audio_permission_denied':
        this.audioMetrics.permissionDenials++;
        break;

      case 'audio_capture_error':
        this.audioMetrics.captureErrors++;
        this.audioMetrics.lastCaptureError = now;
        break;
    }
  }

  private updateDerivedMetrics(): void {
    this.updateConnectionMetrics();
    this.updateAudioMetrics();
    this.updateErrorMetrics();
  }

  private updateConnectionMetrics(): void {
    const now = Date.now();

    // Calculate average latency
    if (this.latencyMeasurements.length > 0) {
      this.connectionMetrics.averageLatency =
        this.latencyMeasurements.reduce((sum, latency) => sum + latency, 0) /
        this.latencyMeasurements.length;
    }

    // Calculate connection uptime
    if (this.connectionMetrics.connectionStartTime > 0) {
      this.connectionMetrics.connectionUptime = now - this.connectionMetrics.connectionStartTime;
    }
  }

  private updateAudioMetrics(): void {
    const now = Date.now();

    // Calculate average packet size
    if (this.audioMetrics.packetsTransmitted > 0) {
      this.audioMetrics.averagePacketSize =
        this.audioMetrics.bytesTransmitted / this.audioMetrics.packetsTransmitted;
    }

    // Update streaming duration if currently streaming
    if (this.audioMetrics.isStreaming && this.audioMetrics.streamStartTime) {
      this.audioMetrics.streamingDuration = now - this.audioMetrics.streamStartTime;
    }
  }

  private updateErrorMetrics(): void {
    this.cleanupOldErrorTimestamps();

    // Calculate error rate (errors per minute)
    this.errorMetrics.errorRate = this.errorTimestamps.length;

    // Calculate recovery success rate
    if (this.errorMetrics.recoveryAttempts > 0) {
      this.errorMetrics.recoverySuccessRate =
        (this.errorMetrics.successfulRecoveries / this.errorMetrics.recoveryAttempts) * 100;
    }
  }

  private cleanupOldErrorTimestamps(): void {
    const now = Date.now();
    const cutoff = now - this.errorRateWindow;
    this.errorTimestamps = this.errorTimestamps.filter(timestamp => timestamp > cutoff);
  }

  private calculateSystemHealth(): 'healthy' | 'degraded' | 'critical' {
    const connection = this.connectionMetrics;
    const audio = this.audioMetrics;
    const errors = this.errorMetrics;

    // Critical conditions
    if (
      connection.quality === 'critical' ||
      audio.audioQuality === 'poor' ||
      errors.errorRate > 10 || // More than 10 errors per minute
      (errors.recoveryAttempts > 0 && errors.recoverySuccessRate < 50) // Less than 50% recovery success (only if attempts exist)
    ) {
      return 'critical';
    }

    // Degraded conditions
    if (
      connection.quality === 'poor' ||
      audio.audioQuality === 'degraded' ||
      errors.errorRate > 5 || // More than 5 errors per minute
      (errors.recoveryAttempts > 0 && errors.recoverySuccessRate < 80) || // Less than 80% recovery success (only if attempts exist)
      connection.averageLatency > 2000 // More than 2 seconds latency
    ) {
      return 'degraded';
    }

    return 'healthy';
  }
}

// Singleton instance for global access
export const performanceMonitor = new PerformanceMonitoringService();