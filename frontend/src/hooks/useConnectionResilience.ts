import { useEffect, useRef, useCallback, useState } from 'react';
import { useAppStore } from '../store';
import { useErrorHandler } from './useErrorHandler';
import { WebSocketManager, type ConnectionQuality, type SessionState } from '../services/WebSocketManager';

interface ConnectionResilienceConfig {
  enableSessionRecovery: boolean;
  enableOfflineMode: boolean;
  maxOfflineTime: number;
  connectionRetryDelay: number;
  maxReconnectAttempts: number;
  exponentialBackoffMultiplier: number;
  connectionQualityCheckInterval: number;
  offlineNotificationDelay: number;
}

const DEFAULT_CONFIG: ConnectionResilienceConfig = {
  enableSessionRecovery: true,
  enableOfflineMode: true,
  maxOfflineTime: 300000, // 5 minutes
  connectionRetryDelay: 2000, // 2 seconds initial delay
  maxReconnectAttempts: 10,
  exponentialBackoffMultiplier: 1.5,
  connectionQualityCheckInterval: 5000, // 5 seconds
  offlineNotificationDelay: 3000, // 3 seconds before showing offline notification
};

/**
 * Hook for managing connection resilience and session recovery
 */
export function useConnectionResilience(
  webSocketManager: WebSocketManager | null,
  config: Partial<ConnectionResilienceConfig> = {}
) {
  const finalConfig = { ...DEFAULT_CONFIG, ...config };
  const { handleWebSocketError } = useErrorHandler();
  const {
    sessionActive,
    sourceLang,
    targetLang,
    selectedVoice,
    setConnectionStatus,
    setCurrentState
  } = useAppStore();

  // State for connection resilience
  const [isOfflineMode, setIsOfflineMode] = useState(false);
  const [reconnectAttempts, setReconnectAttempts] = useState(0);
  const [lastConnectionQuality, setLastConnectionQuality] = useState<ConnectionQuality>('good');
  const [offlineNotificationShown, setOfflineNotificationShown] = useState(false);
  const [manualRetryAvailable, setManualRetryAvailable] = useState(false);

  // Refs for timers and state tracking
  const offlineStartTime = useRef<number | null>(null);
  const connectionRetryTimer = useRef<number | null>(null);
  const qualityCheckTimer = useRef<number | null>(null);
  const offlineNotificationTimer = useRef<number | null>(null);
  const lastSessionState = useRef<SessionState | null>(null);
  const reconnectDelayRef = useRef<number>(finalConfig.connectionRetryDelay);

  /**
   * Create session state from current app state
   */
  const createSessionState = useCallback((): SessionState => {
    return {
      sessionId: `session_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`,
      sourceLang,
      targetLang,
      selectedVoice,
      isActive: sessionActive,
      lastActivity: Date.now(),
      pendingUtterances: [], // This would be populated with actual utterance data
      currentUtteranceId: null
    };
  }, [sessionActive, sourceLang, targetLang, selectedVoice]);

  /**
   * Handle connection state changes with enhanced resilience
   */
  const handleConnectionChange = useCallback((connected: boolean) => {
    if (connected) {
      // Connection restored
      console.log('Connection restored');
      setConnectionStatus('connected');
      setIsOfflineMode(false);
      setOfflineNotificationShown(false);
      setManualRetryAvailable(false);
      setReconnectAttempts(0);
      
      // Reset reconnect delay to initial value
      reconnectDelayRef.current = finalConfig.connectionRetryDelay;
      
      // Clear all timers
      clearAllTimers();
      
      // Clear offline start time
      offlineStartTime.current = null;
      
      // Start connection quality monitoring
      startConnectionQualityMonitoring();
      
      // Attempt session recovery if we have session state
      if (finalConfig.enableSessionRecovery && lastSessionState.current) {
        console.log('Attempting session recovery after reconnection');
        webSocketManager?.setSessionState(lastSessionState.current);
      }
    } else {
      // Connection lost
      console.log('Connection lost, initiating resilience measures');
      setConnectionStatus('disconnected');
      offlineStartTime.current = Date.now();
      
      // Stop quality monitoring
      if (qualityCheckTimer.current) {
        clearInterval(qualityCheckTimer.current);
        qualityCheckTimer.current = null;
      }
      
      // Start offline mode detection with delay
      if (finalConfig.enableOfflineMode) {
        startOfflineNotificationTimer();
        scheduleConnectionRetry();
      }
    }
  }, [setConnectionStatus, finalConfig.enableOfflineMode, finalConfig.enableSessionRecovery, finalConfig.connectionRetryDelay, webSocketManager]);

  /**
   * Handle connection quality changes with degradation handling
   */
  const handleConnectionQualityChange = useCallback((quality: ConnectionQuality) => {
    console.log('Connection quality changed from', lastConnectionQuality, 'to', quality);
    setLastConnectionQuality(quality);
    
    // Handle quality degradation
    if (quality === 'poor' && lastConnectionQuality === 'good') {
      console.warn('Connection quality degraded to poor - monitoring closely');
      handleWebSocketError('Connection quality is poor. Audio may be affected.');
      
      // Increase quality check frequency
      if (qualityCheckTimer.current) {
        clearInterval(qualityCheckTimer.current);
        startConnectionQualityMonitoring(2000); // Check every 2 seconds
      }
    } else if (quality === 'critical') {
      console.error('Connection quality is critical - forcing reconnection');
      handleWebSocketError('Connection quality is critical. Attempting to reconnect...');
      
      // Force reconnection for critical quality
      if (webSocketManager) {
        webSocketManager.forceReconnect();
      }
    } else if (quality === 'good' && lastConnectionQuality !== 'good') {
      console.log('Connection quality improved to good');
      
      // Reset quality check frequency to normal
      if (qualityCheckTimer.current) {
        clearInterval(qualityCheckTimer.current);
        startConnectionQualityMonitoring();
      }
    }
  }, [handleWebSocketError, lastConnectionQuality, webSocketManager]);

  /**
   * Handle session recovery
   */
  const handleSessionRecovered = useCallback((sessionState: SessionState) => {
    console.log('Session recovered:', sessionState);
    
    // Restore app state from recovered session
    if (sessionState.isActive && !sessionActive) {
      // Session was active, restore it
      setCurrentState('listening');
    }
    
    // Update last session state
    lastSessionState.current = sessionState;
  }, [sessionActive, setCurrentState]);

  /**
   * Start offline notification timer
   */
  const startOfflineNotificationTimer = useCallback(() => {
    if (offlineNotificationTimer.current) {
      clearTimeout(offlineNotificationTimer.current);
    }

    offlineNotificationTimer.current = window.setTimeout(() => {
      if (!offlineNotificationShown && offlineStartTime.current) {
        setIsOfflineMode(true);
        setOfflineNotificationShown(true);
        handleWebSocketError('You are currently offline. Attempting to reconnect...');
      }
    }, finalConfig.offlineNotificationDelay);
  }, [offlineNotificationShown, handleWebSocketError, finalConfig.offlineNotificationDelay]);

  /**
   * Start connection quality monitoring
   */
  const startConnectionQualityMonitoring = useCallback((interval?: number) => {
    if (qualityCheckTimer.current) {
      clearInterval(qualityCheckTimer.current);
    }

    const checkInterval = interval || finalConfig.connectionQualityCheckInterval;
    
    qualityCheckTimer.current = window.setInterval(() => {
      if (webSocketManager && webSocketManager.isConnected()) {
        const quality = webSocketManager.getConnectionQuality();
        if (quality !== lastConnectionQuality) {
          handleConnectionQualityChange(quality);
        }
      }
    }, checkInterval);
  }, [webSocketManager, lastConnectionQuality, handleConnectionQualityChange, finalConfig.connectionQualityCheckInterval]);

  /**
   * Clear all timers
   */
  const clearAllTimers = useCallback(() => {
    if (connectionRetryTimer.current) {
      clearTimeout(connectionRetryTimer.current);
      connectionRetryTimer.current = null;
    }
    if (qualityCheckTimer.current) {
      clearInterval(qualityCheckTimer.current);
      qualityCheckTimer.current = null;
    }
    if (offlineNotificationTimer.current) {
      clearTimeout(offlineNotificationTimer.current);
      offlineNotificationTimer.current = null;
    }
  }, []);

  /**
   * Schedule connection retry with exponential backoff
   */
  const scheduleConnectionRetry = useCallback(() => {
    if (connectionRetryTimer.current) {
      return; // Already scheduled
    }

    // Check if we've exceeded max attempts
    if (reconnectAttempts >= finalConfig.maxReconnectAttempts) {
      console.warn('Max reconnection attempts reached, enabling manual retry');
      setManualRetryAvailable(true);
      setConnectionStatus('disconnected');
      handleWebSocketError(`Failed to reconnect after ${finalConfig.maxReconnectAttempts} attempts. Manual retry available.`);
      return;
    }

    connectionRetryTimer.current = window.setTimeout(() => {
      if (webSocketManager && offlineStartTime.current) {
        const offlineTime = Date.now() - offlineStartTime.current;
        
        if (offlineTime < finalConfig.maxOfflineTime) {
          const currentAttempt = reconnectAttempts + 1;
          console.log(`Attempting to reconnect... (attempt ${currentAttempt}/${finalConfig.maxReconnectAttempts})`);
          
          setConnectionStatus('reconnecting');
          setReconnectAttempts(currentAttempt);
          
          // Attempt reconnection
          webSocketManager.forceReconnect();
          
          // Calculate next delay with exponential backoff
          reconnectDelayRef.current = Math.min(
            reconnectDelayRef.current * finalConfig.exponentialBackoffMultiplier,
            30000 // Max 30 seconds
          );
          
          // Schedule next retry
          connectionRetryTimer.current = null;
          scheduleConnectionRetry();
        } else {
          console.warn('Max offline time exceeded, stopping automatic reconnection attempts');
          setManualRetryAvailable(true);
          setIsOfflineMode(true);
          handleWebSocketError('Connection lost for too long. Manual retry available.');
        }
      }
    }, reconnectDelayRef.current);
  }, [
    webSocketManager, 
    finalConfig.maxOfflineTime, 
    finalConfig.maxReconnectAttempts,
    finalConfig.exponentialBackoffMultiplier,
    setConnectionStatus, 
    handleWebSocketError,
    reconnectAttempts
  ]);

  /**
   * Update session state when app state changes
   */
  useEffect(() => {
    if (webSocketManager && finalConfig.enableSessionRecovery) {
      const sessionState = createSessionState();
      webSocketManager.setSessionState(sessionState);
      lastSessionState.current = sessionState;
    }
  }, [webSocketManager, sessionActive, sourceLang, targetLang, selectedVoice, finalConfig.enableSessionRecovery, createSessionState]);

  /**
   * Setup WebSocket event handlers with resilience features
   */
  useEffect(() => {
    if (!webSocketManager) {
      return;
    }

    // Get current message handler and extend it
    const originalHandler = (webSocketManager as any).messageHandler;
    
    // Extend the message handler with resilience features
    (webSocketManager as any).messageHandler = {
      ...originalHandler,
      onConnectionChange: (connected: boolean) => {
        originalHandler.onConnectionChange?.(connected);
        handleConnectionChange(connected);
      },
      onConnectionQualityChange: handleConnectionQualityChange,
      onSessionRecovered: handleSessionRecovered,
    };

    // Start connection quality monitoring if connected
    if (webSocketManager.isConnected()) {
      startConnectionQualityMonitoring();
    }

    return () => {
      // Cleanup all timers
      clearAllTimers();
    };
  }, [webSocketManager, handleConnectionChange, handleConnectionQualityChange, handleSessionRecovered, startConnectionQualityMonitoring, clearAllTimers]);

  /**
   * Check if we're currently offline
   */
  const isOffline = useCallback((): boolean => {
    return isOfflineMode || offlineStartTime.current !== null;
  }, [isOfflineMode]);

  /**
   * Get offline duration
   */
  const getOfflineDuration = useCallback((): number => {
    if (!offlineStartTime.current) {
      return 0;
    }
    return Date.now() - offlineStartTime.current;
  }, []);

  /**
   * Check if session can be recovered
   */
  const canRecoverSession = useCallback((): boolean => {
    return finalConfig.enableSessionRecovery && lastSessionState.current !== null;
  }, [finalConfig.enableSessionRecovery]);

  /**
   * Manually trigger connection retry
   */
  const manualRetry = useCallback(() => {
    if (!webSocketManager) {
      console.warn('Cannot retry: WebSocket manager not available');
      return;
    }

    console.log('Manual retry initiated');
    
    // Reset retry state
    setReconnectAttempts(0);
    setManualRetryAvailable(false);
    setIsOfflineMode(false);
    setOfflineNotificationShown(false);
    reconnectDelayRef.current = finalConfig.connectionRetryDelay;
    
    // Clear all timers
    clearAllTimers();
    
    // Update connection status
    setConnectionStatus('reconnecting');
    
    // Attempt connection
    webSocketManager.forceReconnect();
    
    // Start retry scheduling again
    scheduleConnectionRetry();
  }, [webSocketManager, finalConfig.connectionRetryDelay, setConnectionStatus, clearAllTimers, scheduleConnectionRetry]);

  /**
   * Manually trigger session recovery
   */
  const triggerSessionRecovery = useCallback(() => {
    if (webSocketManager && canRecoverSession()) {
      console.log('Manual session recovery initiated');
      webSocketManager.connect();
    }
  }, [webSocketManager, canRecoverSession]);

  /**
   * Force immediate reconnection (bypasses retry limits)
   */
  const forceReconnect = useCallback(() => {
    if (!webSocketManager) {
      console.warn('Cannot force reconnect: WebSocket manager not available');
      return;
    }

    console.log('Force reconnect initiated');
    
    // Reset all state
    setReconnectAttempts(0);
    setManualRetryAvailable(false);
    setIsOfflineMode(false);
    setOfflineNotificationShown(false);
    reconnectDelayRef.current = finalConfig.connectionRetryDelay;
    
    // Clear all timers
    clearAllTimers();
    
    // Force reconnection
    webSocketManager.forceReconnect();
  }, [webSocketManager, finalConfig.connectionRetryDelay, clearAllTimers]);

  /**
   * Get connection statistics with resilience information
   */
  const getConnectionStats = useCallback(() => {
    if (!webSocketManager) {
      return null;
    }
    
    return {
      ...webSocketManager.getConnectionStats(),
      isOffline: isOffline(),
      offlineDuration: getOfflineDuration(),
      canRecoverSession: canRecoverSession(),
      maxOfflineTime: finalConfig.maxOfflineTime,
      reconnectAttempts,
      maxReconnectAttempts: finalConfig.maxReconnectAttempts,
      manualRetryAvailable,
      connectionQuality: lastConnectionQuality,
      nextRetryDelay: reconnectDelayRef.current,
      offlineNotificationShown
    };
  }, [
    webSocketManager, 
    isOffline, 
    getOfflineDuration, 
    canRecoverSession, 
    finalConfig.maxOfflineTime,
    finalConfig.maxReconnectAttempts,
    reconnectAttempts,
    manualRetryAvailable,
    lastConnectionQuality,
    offlineNotificationShown
  ]);

  return {
    // Connection state
    isOffline,
    isOfflineMode,
    reconnectAttempts,
    manualRetryAvailable,
    connectionQuality: lastConnectionQuality,
    
    // Duration and timing
    getOfflineDuration,
    connectionRetryDelay: reconnectDelayRef.current,
    maxOfflineTime: finalConfig.maxOfflineTime,
    
    // Session recovery
    canRecoverSession,
    triggerSessionRecovery,
    
    // Manual controls
    manualRetry,
    forceReconnect,
    
    // Statistics and monitoring
    getConnectionStats,
    
    // Configuration
    config: finalConfig
  };
}

/**
 * Hook for displaying enhanced connection status to users
 */
export function useConnectionStatus(resilienceStats?: ReturnType<typeof useConnectionResilience>['getConnectionStats']) {
  const { connectionStatus } = useAppStore();
  const { handleWebSocketError } = useErrorHandler();

  const getStatusMessage = useCallback((): string => {
    const stats = resilienceStats?.();
    
    switch (connectionStatus) {
      case 'connected':
        if (stats?.connectionQuality === 'poor') {
          return 'Connected (Poor Quality)';
        } else if (stats?.connectionQuality === 'critical') {
          return 'Connected (Critical Quality)';
        }
        return 'Connected';
      case 'disconnected':
        if (stats?.manualRetryAvailable) {
          return 'Disconnected (Retry Available)';
        } else if (stats?.isOffline) {
          return 'Offline';
        }
        return 'Disconnected';
      case 'reconnecting':
        if (stats?.reconnectAttempts) {
          return `Reconnecting... (${stats.reconnectAttempts}/${stats.maxReconnectAttempts})`;
        }
        return 'Reconnecting...';
      default:
        return 'Unknown';
    }
  }, [connectionStatus, resilienceStats]);

  const getStatusColor = useCallback((): 'green' | 'yellow' | 'red' => {
    const stats = resilienceStats?.();
    
    switch (connectionStatus) {
      case 'connected':
        if (stats?.connectionQuality === 'poor') {
          return 'yellow';
        } else if (stats?.connectionQuality === 'critical') {
          return 'red';
        }
        return 'green';
      case 'reconnecting':
        return 'yellow';
      case 'disconnected':
        return 'red';
      default:
        return 'red';
    }
  }, [connectionStatus, resilienceStats]);

  const getDetailedStatus = useCallback(() => {
    const stats = resilienceStats?.();
    if (!stats) return null;

    return {
      isOffline: stats.isOffline,
      offlineDuration: stats.offlineDuration,
      reconnectAttempts: stats.reconnectAttempts,
      maxReconnectAttempts: stats.maxReconnectAttempts,
      manualRetryAvailable: stats.manualRetryAvailable,
      connectionQuality: stats.connectionQuality,
      canRecoverSession: stats.canRecoverSession,
      nextRetryDelay: stats.nextRetryDelay
    };
  }, [resilienceStats]);

  const showConnectionError = useCallback(() => {
    if (connectionStatus === 'disconnected') {
      handleWebSocketError('Connection to server lost. Attempting to reconnect...');
    }
  }, [connectionStatus, handleWebSocketError]);

  const formatOfflineDuration = useCallback((duration: number): string => {
    const seconds = Math.floor(duration / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);

    if (hours > 0) {
      return `${hours}h ${minutes % 60}m`;
    } else if (minutes > 0) {
      return `${minutes}m ${seconds % 60}s`;
    } else {
      return `${seconds}s`;
    }
  }, []);

  return {
    status: connectionStatus,
    message: getStatusMessage(),
    color: getStatusColor(),
    detailedStatus: getDetailedStatus(),
    showConnectionError,
    formatOfflineDuration
  };
}