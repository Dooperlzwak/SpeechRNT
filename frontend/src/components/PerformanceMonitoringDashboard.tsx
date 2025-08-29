/**
 * PerformanceMonitoringDashboard - Component for displaying performance metrics
 * 
 * Shows real-time connection quality, audio streaming performance, service initialization timing,
 * and error rates with recovery success tracking.
 */

import React, { useState, useEffect } from 'react';
import { usePerformanceMonitoring } from '../hooks/usePerformanceMonitoring';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from './ui/card';
import { Badge } from './ui/badge';
import { Button } from './ui/button';
import { Tabs, TabsContent, TabsList, TabsTrigger } from './ui/tabs';
import { Progress } from './ui/progress';

interface PerformanceMonitoringDashboardProps {
  className?: string;
  showExportButton?: boolean;
  autoRefresh?: boolean;
  refreshInterval?: number;
}

export const PerformanceMonitoringDashboard: React.FC<PerformanceMonitoringDashboardProps> = ({
  className = '',
  showExportButton = true,
  autoRefresh = true,
  refreshInterval = 1000
}) => {
  const performanceMonitoring = usePerformanceMonitoring({
    enabled: true,
    autoStart: true
  });

  const [isVisible, setIsVisible] = useState(false);

  // Auto-refresh metrics
  useEffect(() => {
    if (!autoRefresh) return;

    const interval = setInterval(() => {
      // Trigger re-render by accessing current metrics
      performanceMonitoring.getPerformanceSnapshot();
    }, refreshInterval);

    return () => clearInterval(interval);
  }, [autoRefresh, refreshInterval, performanceMonitoring]);

  const handleExportMetrics = () => {
    const exportData = performanceMonitoring.exportMetrics();
    const blob = new Blob([JSON.stringify(exportData, null, 2)], {
      type: 'application/json'
    });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `performance-metrics-${new Date().toISOString()}.json`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  };

  const handleResetMetrics = () => {
    performanceMonitoring.resetMetrics();
  };

  const getHealthBadgeVariant = (health: string) => {
    switch (health) {
      case 'healthy':
        return 'default';
      case 'degraded':
        return 'secondary';
      case 'critical':
        return 'destructive';
      default:
        return 'outline';
    }
  };

  const getQualityBadgeVariant = (quality: string) => {
    switch (quality) {
      case 'good':
        return 'default';
      case 'poor':
        return 'secondary';
      case 'critical':
        return 'destructive';
      default:
        return 'outline';
    }
  };

  const formatBytes = (bytes: number): string => {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  const formatDuration = (ms: number): string => {
    if (ms < 1000) return `${ms}ms`;
    if (ms < 60000) return `${(ms / 1000).toFixed(1)}s`;
    return `${(ms / 60000).toFixed(1)}m`;
  };

  if (!isVisible) {
    return (
      <div className={className}>
        <Button
          variant="outline"
          size="sm"
          onClick={() => setIsVisible(true)}
        >
          Show Performance Metrics
        </Button>
      </div>
    );
  }

  const { 
    currentSnapshot, 
    connectionMetrics, 
    audioMetrics, 
    errorMetrics, 
    systemHealth 
  } = performanceMonitoring;

  if (!currentSnapshot) {
    return (
      <div className={className}>
        <Card>
          <CardContent className="p-6">
            <p className="text-muted-foreground">Loading performance metrics...</p>
          </CardContent>
        </Card>
      </div>
    );
  }

  return (
    <div className={className}>
      <Card>
        <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
          <div>
            <CardTitle className="text-lg">Performance Monitoring</CardTitle>
            <CardDescription>
              Real-time system performance metrics
            </CardDescription>
          </div>
          <div className="flex items-center space-x-2">
            <Badge variant={getHealthBadgeVariant(systemHealth)}>
              {systemHealth.toUpperCase()}
            </Badge>
            <Button
              variant="outline"
              size="sm"
              onClick={() => setIsVisible(false)}
            >
              Hide
            </Button>
          </div>
        </CardHeader>
        <CardContent>
          <Tabs defaultValue="overview" className="w-full">
            <TabsList className="grid w-full grid-cols-4">
              <TabsTrigger value="overview">Overview</TabsTrigger>
              <TabsTrigger value="connection">Connection</TabsTrigger>
              <TabsTrigger value="audio">Audio</TabsTrigger>
              <TabsTrigger value="errors">Errors</TabsTrigger>
            </TabsList>

            <TabsContent value="overview" className="space-y-4">
              <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
                <Card>
                  <CardHeader className="pb-2">
                    <CardTitle className="text-sm">System Health</CardTitle>
                  </CardHeader>
                  <CardContent>
                    <Badge variant={getHealthBadgeVariant(systemHealth)}>
                      {systemHealth}
                    </Badge>
                  </CardContent>
                </Card>

                <Card>
                  <CardHeader className="pb-2">
                    <CardTitle className="text-sm">Connection</CardTitle>
                  </CardHeader>
                  <CardContent>
                    <Badge variant={getQualityBadgeVariant(connectionMetrics?.quality || 'good')}>
                      {connectionMetrics?.quality || 'unknown'}
                    </Badge>
                    <p className="text-xs text-muted-foreground mt-1">
                      {connectionMetrics?.averageLatency.toFixed(0)}ms avg
                    </p>
                  </CardContent>
                </Card>

                <Card>
                  <CardHeader className="pb-2">
                    <CardTitle className="text-sm">Audio Quality</CardTitle>
                  </CardHeader>
                  <CardContent>
                    <Badge variant={getQualityBadgeVariant(audioMetrics?.audioQuality || 'good')}>
                      {audioMetrics?.audioQuality || 'unknown'}
                    </Badge>
                    <p className="text-xs text-muted-foreground mt-1">
                      {formatBytes(audioMetrics?.bytesTransmitted || 0)} sent
                    </p>
                  </CardContent>
                </Card>

                <Card>
                  <CardHeader className="pb-2">
                    <CardTitle className="text-sm">Error Rate</CardTitle>
                  </CardHeader>
                  <CardContent>
                    <div className="text-lg font-semibold">
                      {errorMetrics?.errorRate || 0}/min
                    </div>
                    <p className="text-xs text-muted-foreground">
                      {errorMetrics?.recoverySuccessRate.toFixed(0) || 0}% recovery
                    </p>
                  </CardContent>
                </Card>
              </div>

              <Card>
                <CardHeader className="pb-2">
                  <CardTitle className="text-sm">Service Initialization</CardTitle>
                </CardHeader>
                <CardContent>
                  <div className="space-y-2">
                    <div className="flex justify-between items-center">
                      <span className="text-sm">WebSocket</span>
                      <span className="text-sm">
                        {currentSnapshot.serviceInit.webSocketInitTime 
                          ? formatDuration(currentSnapshot.serviceInit.webSocketInitTime)
                          : 'Not initialized'
                        }
                      </span>
                    </div>
                    <div className="flex justify-between items-center">
                      <span className="text-sm">Audio</span>
                      <span className="text-sm">
                        {currentSnapshot.serviceInit.audioInitTime 
                          ? formatDuration(currentSnapshot.serviceInit.audioInitTime)
                          : 'Not initialized'
                        }
                      </span>
                    </div>
                    <div className="flex justify-between items-center">
                      <span className="text-sm">Error Reporting</span>
                      <span className="text-sm">
                        {currentSnapshot.serviceInit.errorReportingInitTime 
                          ? formatDuration(currentSnapshot.serviceInit.errorReportingInitTime)
                          : 'Not initialized'
                        }
                      </span>
                    </div>
                    <div className="flex justify-between items-center font-semibold">
                      <span className="text-sm">Total</span>
                      <span className="text-sm">
                        {currentSnapshot.serviceInit.totalInitTime 
                          ? formatDuration(currentSnapshot.serviceInit.totalInitTime)
                          : 'Incomplete'
                        }
                      </span>
                    </div>
                  </div>
                </CardContent>
              </Card>
            </TabsContent>

            <TabsContent value="connection" className="space-y-4">
              <div className="grid grid-cols-2 gap-4">
                <Card>
                  <CardHeader className="pb-2">
                    <CardTitle className="text-sm">Connection Quality</CardTitle>
                  </CardHeader>
                  <CardContent>
                    <Badge variant={getQualityBadgeVariant(connectionMetrics?.quality || 'good')}>
                      {connectionMetrics?.quality || 'unknown'}
                    </Badge>
                    <div className="mt-2 space-y-1">
                      <div className="flex justify-between text-xs">
                        <span>Current Latency</span>
                        <span>{connectionMetrics?.latency || 0}ms</span>
                      </div>
                      <div className="flex justify-between text-xs">
                        <span>Average Latency</span>
                        <span>{connectionMetrics?.averageLatency.toFixed(0) || 0}ms</span>
                      </div>
                      <div className="flex justify-between text-xs">
                        <span>Uptime</span>
                        <span>{formatDuration(connectionMetrics?.connectionUptime || 0)}</span>
                      </div>
                    </div>
                  </CardContent>
                </Card>

                <Card>
                  <CardHeader className="pb-2">
                    <CardTitle className="text-sm">Message Statistics</CardTitle>
                  </CardHeader>
                  <CardContent>
                    <div className="space-y-1">
                      <div className="flex justify-between text-xs">
                        <span>Messages Sent</span>
                        <span>{connectionMetrics?.messagesSent || 0}</span>
                      </div>
                      <div className="flex justify-between text-xs">
                        <span>Messages Received</span>
                        <span>{connectionMetrics?.messagesReceived || 0}</span>
                      </div>
                      <div className="flex justify-between text-xs">
                        <span>Message Failures</span>
                        <span>{connectionMetrics?.messageFailures || 0}</span>
                      </div>
                      <div className="flex justify-between text-xs">
                        <span>Heartbeat Missed</span>
                        <span>{connectionMetrics?.heartbeatMissed || 0}</span>
                      </div>
                    </div>
                  </CardContent>
                </Card>
              </div>

              <Card>
                <CardHeader className="pb-2">
                  <CardTitle className="text-sm">Connection History</CardTitle>
                </CardHeader>
                <CardContent>
                  <div className="space-y-1">
                    <div className="flex justify-between text-xs">
                      <span>Total Disconnections</span>
                      <span>{connectionMetrics?.totalDisconnections || 0}</span>
                    </div>
                    <div className="flex justify-between text-xs">
                      <span>Reconnect Attempts</span>
                      <span>{connectionMetrics?.reconnectAttempts || 0}</span>
                    </div>
                    <div className="flex justify-between text-xs">
                      <span>Last Reconnect</span>
                      <span>
                        {connectionMetrics?.lastReconnectTime 
                          ? new Date(connectionMetrics.lastReconnectTime).toLocaleTimeString()
                          : 'Never'
                        }
                      </span>
                    </div>
                  </div>
                </CardContent>
              </Card>
            </TabsContent>

            <TabsContent value="audio" className="space-y-4">
              <div className="grid grid-cols-2 gap-4">
                <Card>
                  <CardHeader className="pb-2">
                    <CardTitle className="text-sm">Streaming Status</CardTitle>
                  </CardHeader>
                  <CardContent>
                    <Badge variant={audioMetrics?.isStreaming ? 'default' : 'secondary'}>
                      {audioMetrics?.isStreaming ? 'Streaming' : 'Stopped'}
                    </Badge>
                    <div className="mt-2 space-y-1">
                      <div className="flex justify-between text-xs">
                        <span>Quality</span>
                        <Badge variant={getQualityBadgeVariant(audioMetrics?.audioQuality || 'good')} className="text-xs">
                          {audioMetrics?.audioQuality || 'unknown'}
                        </Badge>
                      </div>
                      <div className="flex justify-between text-xs">
                        <span>Duration</span>
                        <span>{formatDuration(audioMetrics?.streamingDuration || 0)}</span>
                      </div>
                    </div>
                  </CardContent>
                </Card>

                <Card>
                  <CardHeader className="pb-2">
                    <CardTitle className="text-sm">Data Transfer</CardTitle>
                  </CardHeader>
                  <CardContent>
                    <div className="space-y-1">
                      <div className="flex justify-between text-xs">
                        <span>Bytes Transmitted</span>
                        <span>{formatBytes(audioMetrics?.bytesTransmitted || 0)}</span>
                      </div>
                      <div className="flex justify-between text-xs">
                        <span>Packets Sent</span>
                        <span>{audioMetrics?.packetsTransmitted || 0}</span>
                      </div>
                      <div className="flex justify-between text-xs">
                        <span>Packets Lost</span>
                        <span>{audioMetrics?.packetsLost || 0}</span>
                      </div>
                      <div className="flex justify-between text-xs">
                        <span>Avg Packet Size</span>
                        <span>{formatBytes(audioMetrics?.averagePacketSize || 0)}</span>
                      </div>
                    </div>
                  </CardContent>
                </Card>
              </div>

              <Card>
                <CardHeader className="pb-2">
                  <CardTitle className="text-sm">Device Management</CardTitle>
                </CardHeader>
                <CardContent>
                  <div className="space-y-1">
                    <div className="flex justify-between text-xs">
                      <span>Device Switches</span>
                      <span>{audioMetrics?.deviceSwitches || 0}</span>
                    </div>
                    <div className="flex justify-between text-xs">
                      <span>Permission Denials</span>
                      <span>{audioMetrics?.permissionDenials || 0}</span>
                    </div>
                    <div className="flex justify-between text-xs">
                      <span>Capture Errors</span>
                      <span>{audioMetrics?.captureErrors || 0}</span>
                    </div>
                    <div className="flex justify-between text-xs">
                      <span>Last Error</span>
                      <span>
                        {audioMetrics?.lastCaptureError 
                          ? new Date(audioMetrics.lastCaptureError).toLocaleTimeString()
                          : 'None'
                        }
                      </span>
                    </div>
                  </div>
                </CardContent>
              </Card>
            </TabsContent>

            <TabsContent value="errors" className="space-y-4">
              <div className="grid grid-cols-2 gap-4">
                <Card>
                  <CardHeader className="pb-2">
                    <CardTitle className="text-sm">Error Summary</CardTitle>
                  </CardHeader>
                  <CardContent>
                    <div className="space-y-1">
                      <div className="flex justify-between text-xs">
                        <span>Total Errors</span>
                        <span>{errorMetrics?.totalErrors || 0}</span>
                      </div>
                      <div className="flex justify-between text-xs">
                        <span>Error Rate</span>
                        <span>{errorMetrics?.errorRate || 0}/min</span>
                      </div>
                      <div className="flex justify-between text-xs">
                        <span>Last Error</span>
                        <span>
                          {errorMetrics?.lastErrorTime 
                            ? new Date(errorMetrics.lastErrorTime).toLocaleTimeString()
                            : 'None'
                          }
                        </span>
                      </div>
                    </div>
                  </CardContent>
                </Card>

                <Card>
                  <CardHeader className="pb-2">
                    <CardTitle className="text-sm">Recovery Statistics</CardTitle>
                  </CardHeader>
                  <CardContent>
                    <div className="space-y-1">
                      <div className="flex justify-between text-xs">
                        <span>Recovery Attempts</span>
                        <span>{errorMetrics?.recoveryAttempts || 0}</span>
                      </div>
                      <div className="flex justify-between text-xs">
                        <span>Successful</span>
                        <span>{errorMetrics?.successfulRecoveries || 0}</span>
                      </div>
                      <div className="flex justify-between text-xs">
                        <span>Failed</span>
                        <span>{errorMetrics?.failedRecoveries || 0}</span>
                      </div>
                      <div className="flex justify-between text-xs">
                        <span>Success Rate</span>
                        <span>{errorMetrics?.recoverySuccessRate.toFixed(0) || 0}%</span>
                      </div>
                    </div>
                  </CardContent>
                </Card>
              </div>

              <Card>
                <CardHeader className="pb-2">
                  <CardTitle className="text-sm">Error Breakdown</CardTitle>
                </CardHeader>
                <CardContent>
                  <div className="space-y-2">
                    <div className="flex justify-between items-center">
                      <span className="text-xs">Connection Errors</span>
                      <div className="flex items-center space-x-2">
                        <Progress 
                          value={errorMetrics?.totalErrors ? (errorMetrics.connectionErrors / errorMetrics.totalErrors) * 100 : 0} 
                          className="w-16 h-2" 
                        />
                        <span className="text-xs w-8">{errorMetrics?.connectionErrors || 0}</span>
                      </div>
                    </div>
                    <div className="flex justify-between items-center">
                      <span className="text-xs">Audio Errors</span>
                      <div className="flex items-center space-x-2">
                        <Progress 
                          value={errorMetrics?.totalErrors ? (errorMetrics.audioErrors / errorMetrics.totalErrors) * 100 : 0} 
                          className="w-16 h-2" 
                        />
                        <span className="text-xs w-8">{errorMetrics?.audioErrors || 0}</span>
                      </div>
                    </div>
                    <div className="flex justify-between items-center">
                      <span className="text-xs">Configuration Errors</span>
                      <div className="flex items-center space-x-2">
                        <Progress 
                          value={errorMetrics?.totalErrors ? (errorMetrics.configurationErrors / errorMetrics.totalErrors) * 100 : 0} 
                          className="w-16 h-2" 
                        />
                        <span className="text-xs w-8">{errorMetrics?.configurationErrors || 0}</span>
                      </div>
                    </div>
                    <div className="flex justify-between items-center">
                      <span className="text-xs">System Errors</span>
                      <div className="flex items-center space-x-2">
                        <Progress 
                          value={errorMetrics?.totalErrors ? (errorMetrics.systemErrors / errorMetrics.totalErrors) * 100 : 0} 
                          className="w-16 h-2" 
                        />
                        <span className="text-xs w-8">{errorMetrics?.systemErrors || 0}</span>
                      </div>
                    </div>
                  </div>
                </CardContent>
              </Card>
            </TabsContent>
          </Tabs>

          {showExportButton && (
            <div className="flex justify-between items-center mt-4 pt-4 border-t">
              <div className="text-xs text-muted-foreground">
                Last updated: {new Date(performanceMonitoring.lastUpdate).toLocaleTimeString()}
              </div>
              <div className="space-x-2">
                <Button variant="outline" size="sm" onClick={handleResetMetrics}>
                  Reset Metrics
                </Button>
                <Button variant="outline" size="sm" onClick={handleExportMetrics}>
                  Export Data
                </Button>
              </div>
            </div>
          )}
        </CardContent>
      </Card>
    </div>
  );
};