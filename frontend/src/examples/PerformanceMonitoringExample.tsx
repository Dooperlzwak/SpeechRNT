/**
 * Performance Monitoring Integration Example
 * 
 * Demonstrates how to integrate performance monitoring with WebSocket and Audio services
 */

import React, { useEffect, useState } from 'react';
import { usePerformanceMonitoring } from '../hooks/usePerformanceMonitoring';
import { useWebSocketIntegration } from '../hooks/useWebSocketIntegration';
import { useAudioIntegration } from '../hooks/useAudioIntegration';
import { PerformanceMonitoringDashboard } from '../components/PerformanceMonitoringDashboard';

export const PerformanceMonitoringExample: React.FC = () => {
  const [showDashboard, setShowDashboard] = useState(false);

  // Initialize performance monitoring
  const performanceMonitoring = usePerformanceMonitoring({
    enabled: true,
    autoStart: true,
    enableConnectionMonitoring: true,
    enableAudioMonitoring: true,
    enableErrorTracking: true
  });

  // WebSocket integration with performance monitoring
  const webSocket = useWebSocketIntegration(
    {
      url: 'ws://localhost:8080',
      reconnectInterval: 2000,
      maxReconnectAttempts: 5,
      heartbeatInterval: 30000
    },
    (message) => {
      console.log('Received message:', message);
    },
    (data) => {
      console.log('Received binary data:', data.byteLength, 'bytes');
    },
    (connected) => {
      console.log('Connection status:', connected);
    },
    (error) => {
      console.error('WebSocket error:', error);
    }
  );

  // Audio integration with performance monitoring
  const audio = useAudioIntegration(
    {
      sampleRate: 16000,
      channels: 1,
      bitsPerSample: 16,
      chunkSize: 4096,
      autoInitialize: false
    },
    (audioData) => {
      // Send audio data through WebSocket
      webSocket.sendBinaryMessage(audioData);
    },
    (error) => {
      console.error('Audio error:', error);
    },
    (recording) => {
      console.log('Recording status:', recording);
    }
  );

  // Simulate some activity for demonstration
  useEffect(() => {
    const interval = setInterval(() => {
      // Simulate connection metrics
      performanceMonitoring.recordConnectionMetrics({
        latency: Math.random() * 200 + 50, // 50-250ms
        quality: Math.random() > 0.8 ? 'poor' : 'good',
        connected: webSocket.isConnected
      });

      // Simulate audio metrics if recording
      if (audio.isRecording) {
        performanceMonitoring.recordAudioMetrics({
          bytesTransmitted: Math.floor(Math.random() * 1024) + 512,
          packetsTransmitted: 1,
          audioQuality: Math.random() > 0.9 ? 'degraded' : 'good'
        });
      }

      // Occasionally simulate errors
      if (Math.random() > 0.95) {
        const errorTypes = ['connection', 'audio', 'configuration', 'system'] as const;
        const errorType = errorTypes[Math.floor(Math.random() * errorTypes.length)];
        performanceMonitoring.recordError(errorType, new Error(`Simulated ${errorType} error`));
        
        // Simulate recovery attempt
        setTimeout(() => {
          performanceMonitoring.recordRecovery(Math.random() > 0.2, errorType);
        }, 1000);
      }
    }, 1000);

    return () => clearInterval(interval);
  }, [performanceMonitoring, webSocket.isConnected, audio.isRecording]);

  const handleConnect = async () => {
    try {
      await webSocket.connect();
    } catch (error) {
      console.error('Failed to connect:', error);
    }
  };

  const handleStartAudio = async () => {
    try {
      await audio.initialize();
      await audio.startRecording();
    } catch (error) {
      console.error('Failed to start audio:', error);
    }
  };

  const handleStopAudio = () => {
    audio.stopRecording();
  };

  const handleExportMetrics = () => {
    const metrics = performanceMonitoring.exportMetrics();
    console.log('Exported metrics:', metrics);
    
    // In a real application, you might send this to an analytics service
    // or download it as a file
    const blob = new Blob([JSON.stringify(metrics, null, 2)], {
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

  return (
    <div className="p-6 space-y-6">
      <div>
        <h1 className="text-2xl font-bold mb-4">Performance Monitoring Example</h1>
        <p className="text-gray-600 mb-6">
          This example demonstrates real-time performance monitoring for WebSocket connections,
          audio streaming, service initialization, and error tracking.
        </p>
      </div>

      {/* Control Panel */}
      <div className="bg-gray-50 p-4 rounded-lg">
        <h2 className="text-lg font-semibold mb-4">Controls</h2>
        <div className="flex flex-wrap gap-4">
          <button
            onClick={handleConnect}
            disabled={webSocket.isConnected}
            className="px-4 py-2 bg-blue-500 text-white rounded disabled:bg-gray-300"
          >
            {webSocket.isConnected ? 'Connected' : 'Connect WebSocket'}
          </button>
          
          <button
            onClick={handleStartAudio}
            disabled={audio.isRecording}
            className="px-4 py-2 bg-green-500 text-white rounded disabled:bg-gray-300"
          >
            {audio.isRecording ? 'Recording' : 'Start Audio'}
          </button>
          
          <button
            onClick={handleStopAudio}
            disabled={!audio.isRecording}
            className="px-4 py-2 bg-red-500 text-white rounded disabled:bg-gray-300"
          >
            Stop Audio
          </button>
          
          <button
            onClick={() => setShowDashboard(!showDashboard)}
            className="px-4 py-2 bg-purple-500 text-white rounded"
          >
            {showDashboard ? 'Hide' : 'Show'} Dashboard
          </button>
          
          <button
            onClick={handleExportMetrics}
            className="px-4 py-2 bg-gray-500 text-white rounded"
          >
            Export Metrics
          </button>
        </div>
      </div>

      {/* Status Display */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
        <div className="bg-white p-4 rounded-lg border">
          <h3 className="font-semibold mb-2">System Health</h3>
          <div className={`inline-block px-2 py-1 rounded text-sm ${
            performanceMonitoring.systemHealth === 'healthy' 
              ? 'bg-green-100 text-green-800'
              : performanceMonitoring.systemHealth === 'degraded'
              ? 'bg-yellow-100 text-yellow-800'
              : 'bg-red-100 text-red-800'
          }`}>
            {performanceMonitoring.systemHealth.toUpperCase()}
          </div>
        </div>

        <div className="bg-white p-4 rounded-lg border">
          <h3 className="font-semibold mb-2">Connection</h3>
          <div className="text-sm">
            <div>State: {webSocket.connectionState}</div>
            <div>Quality: {webSocket.connectionQuality}</div>
            <div>Latency: {performanceMonitoring.connectionMetrics?.averageLatency.toFixed(0) || 0}ms</div>
          </div>
        </div>

        <div className="bg-white p-4 rounded-lg border">
          <h3 className="font-semibold mb-2">Audio</h3>
          <div className="text-sm">
            <div>Recording: {audio.isRecording ? 'Yes' : 'No'}</div>
            <div>Quality: {performanceMonitoring.audioMetrics?.audioQuality || 'unknown'}</div>
            <div>Bytes Sent: {performanceMonitoring.audioMetrics?.bytesTransmitted || 0}</div>
          </div>
        </div>
      </div>

      {/* Performance Dashboard */}
      {showDashboard && (
        <PerformanceMonitoringDashboard
          className="mt-6"
          showExportButton={true}
          autoRefresh={true}
          refreshInterval={1000}
        />
      )}

      {/* Recent Events */}
      <div className="bg-white p-4 rounded-lg border">
        <h3 className="font-semibold mb-4">Recent Events</h3>
        <div className="space-y-2 max-h-64 overflow-y-auto">
          {performanceMonitoring.getRecentEvents(10).map((event, index) => (
            <div key={index} className="text-sm p-2 bg-gray-50 rounded">
              <div className="flex justify-between">
                <span className="font-medium">{event.type}</span>
                <span className="text-gray-500">
                  {new Date(event.timestamp).toLocaleTimeString()}
                </span>
              </div>
              {event.data && (
                <div className="text-gray-600 mt-1">
                  {JSON.stringify(event.data)}
                </div>
              )}
            </div>
          ))}
        </div>
      </div>
    </div>
  );
};