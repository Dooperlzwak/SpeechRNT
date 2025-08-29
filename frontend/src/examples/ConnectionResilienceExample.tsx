import React, { useEffect, useState } from 'react';
import { WebSocketManager } from '../services/WebSocketManager';
import { useConnectionResilience, useConnectionStatus } from '../hooks/useConnectionResilience';
import ConnectionStatus from '../components/ConnectionStatus';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';

/**
 * Example component demonstrating the enhanced connection resilience features
 */
const ConnectionResilienceExample: React.FC = () => {
  const [webSocketManager, setWebSocketManager] = useState<WebSocketManager | null>(null);

  // Initialize WebSocket manager
  useEffect(() => {
    const wsManager = new WebSocketManager(
      {
        url: 'ws://localhost:8080',
        reconnectInterval: 2000,
        maxReconnectAttempts: 10,
        heartbeatInterval: 30000,
        sessionRecoveryTimeout: 300000,
        messageQueueSize: 100,
        connectionQualityThreshold: 5000
      },
      {
        onMessage: (message) => console.log('Received message:', message),
        onBinaryMessage: (data) => console.log('Received binary data:', data.byteLength, 'bytes'),
        onConnectionChange: (connected) => console.log('Connection changed:', connected),
        onError: (error) => console.error('WebSocket error:', error)
      }
    );

    setWebSocketManager(wsManager);

    return () => {
      wsManager.disconnect();
    };
  }, []);

  // Use the enhanced connection resilience hook
  const resilience = useConnectionResilience(webSocketManager, {
    enableSessionRecovery: true,
    enableOfflineMode: true,
    maxOfflineTime: 300000, // 5 minutes
    connectionRetryDelay: 2000,
    maxReconnectAttempts: 10,
    exponentialBackoffMultiplier: 1.5,
    connectionQualityCheckInterval: 5000,
    offlineNotificationDelay: 3000
  });

  // Use the enhanced connection status hook
  const connectionStatus = useConnectionStatus(resilience.getConnectionStats);

  const handleConnect = () => {
    webSocketManager?.connect();
  };

  const handleDisconnect = () => {
    webSocketManager?.disconnect();
  };

  const handleManualRetry = () => {
    resilience.manualRetry();
  };

  const handleForceReconnect = () => {
    resilience.forceReconnect();
  };

  const handleSessionRecovery = () => {
    resilience.triggerSessionRecovery();
  };

  const stats = resilience.getConnectionStats();

  return (
    <div className="p-6 space-y-6">
      <Card>
        <CardHeader>
          <CardTitle>Connection Resilience Demo</CardTitle>
        </CardHeader>
        <CardContent className="space-y-4">
          {/* Connection Status Display */}
          <ConnectionStatus
            resilienceStats={resilience.getConnectionStats}
            onManualRetry={handleManualRetry}
            onForceReconnect={handleForceReconnect}
            showDetails={true}
          />

          {/* Connection Controls */}
          <div className="flex gap-2 flex-wrap">
            <Button onClick={handleConnect} variant="default">
              Connect
            </Button>
            <Button onClick={handleDisconnect} variant="outline">
              Disconnect
            </Button>
            <Button 
              onClick={handleManualRetry} 
              variant="secondary"
              disabled={!resilience.manualRetryAvailable}
            >
              Manual Retry
            </Button>
            <Button onClick={handleForceReconnect} variant="destructive">
              Force Reconnect
            </Button>
            <Button 
              onClick={handleSessionRecovery} 
              variant="outline"
              disabled={!resilience.canRecoverSession()}
            >
              Recover Session
            </Button>
          </div>

          {/* Connection Statistics */}
          {stats && (
            <Card>
              <CardHeader>
                <CardTitle className="text-lg">Connection Statistics</CardTitle>
              </CardHeader>
              <CardContent>
                <div className="grid grid-cols-2 gap-4 text-sm">
                  <div>
                    <strong>Status:</strong> {connectionStatus.status}
                  </div>
                  <div>
                    <strong>Quality:</strong> {stats.connectionQuality}
                  </div>
                  <div>
                    <strong>Offline:</strong> {resilience.isOffline() ? 'Yes' : 'No'}
                  </div>
                  <div>
                    <strong>Offline Mode:</strong> {resilience.isOfflineMode ? 'Yes' : 'No'}
                  </div>
                  <div>
                    <strong>Reconnect Attempts:</strong> {resilience.reconnectAttempts}/{stats.maxReconnectAttempts}
                  </div>
                  <div>
                    <strong>Manual Retry Available:</strong> {resilience.manualRetryAvailable ? 'Yes' : 'No'}
                  </div>
                  <div>
                    <strong>Session Recoverable:</strong> {resilience.canRecoverSession() ? 'Yes' : 'No'}
                  </div>
                  <div>
                    <strong>Offline Duration:</strong> {connectionStatus.formatOfflineDuration(resilience.getOfflineDuration())}
                  </div>
                  <div>
                    <strong>Next Retry Delay:</strong> {stats.nextRetryDelay}ms
                  </div>
                  <div>
                    <strong>Queued Messages:</strong> {stats.queuedMessages}
                  </div>
                </div>
              </CardContent>
            </Card>
          )}

          {/* Configuration Display */}
          <Card>
            <CardHeader>
              <CardTitle className="text-lg">Configuration</CardTitle>
            </CardHeader>
            <CardContent>
              <div className="grid grid-cols-2 gap-4 text-sm">
                <div>
                  <strong>Max Offline Time:</strong> {resilience.maxOfflineTime / 1000}s
                </div>
                <div>
                  <strong>Retry Delay:</strong> {resilience.connectionRetryDelay}ms
                </div>
                <div>
                  <strong>Max Attempts:</strong> {resilience.config.maxReconnectAttempts}
                </div>
                <div>
                  <strong>Backoff Multiplier:</strong> {resilience.config.exponentialBackoffMultiplier}
                </div>
                <div>
                  <strong>Session Recovery:</strong> {resilience.config.enableSessionRecovery ? 'Enabled' : 'Disabled'}
                </div>
                <div>
                  <strong>Offline Mode:</strong> {resilience.config.enableOfflineMode ? 'Enabled' : 'Disabled'}
                </div>
              </div>
            </CardContent>
          </Card>
        </CardContent>
      </Card>
    </div>
  );
};

export default ConnectionResilienceExample;