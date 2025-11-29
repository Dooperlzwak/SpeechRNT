/**
 * useWebSocketIntegration - React hook for WebSocket integration with session lifecycle management
 * 
 * This hook wraps WebSocketManager with enhanced session management, connection resilience,
 * and integration-specific features for the Vocr application.
 */

import { useEffect, useRef, useState, useCallback } from 'react';
import { WebSocketManager, ConnectionState, type WebSocketConfig, type MessageHandler, type ConnectionQuality, type SessionState } from '../services/WebSocketManager';
import { type ServerMessage, type ClientMessage } from '../types/messageProtocol';
import { useErrorHandler } from './useErrorHandler';
import { usePerformanceMonitoring } from './usePerformanceMonitoring';

export interface WebSocketIntegrationConfig {
  url: string;
  reconnectInterval: number;
  maxReconnectAttempts: number;
  heartbeatInterval: number;
  sessionRecoveryTimeout?: number;
  messageQueueSize?: number;
  connectionQualityThreshold?: number;
}

export interface WebSocketIntegrationReturn {
  connectionState: ConnectionState;
  connectionQuality: ConnectionQuality;
  isConnected: boolean;
  connect: () => Promise<void>;
  disconnect: () => void;
  sendMessage: (message: ClientMessage, options?: {
    priority?: 'low' | 'normal' | 'high';
    requiresAck?: boolean;
    maxAttempts?: number;
  }) => string;
  sendBinaryMessage: (data: ArrayBuffer, options?: {
    priority?: 'low' | 'normal' | 'high';
    maxAttempts?: number;
  }) => string;
  lastMessage: ServerMessage | null;
  lastBinaryMessage: ArrayBuffer | null;
  error: Error | null;
  retryConnection: () => Promise<void>;
  getConnectionStats: () => {
    state: ConnectionState;
    quality: ConnectionQuality;
    reconnectAttempts: number;
    queuedMessages: number;
    lastError: string | null;
    sessionRecoverable: boolean;
    messageProtocol: {
      queuedMessages: number;
      sentMessages: number;
      failedMessages: number;
      acknowledgedMessages: number;
      duplicateMessages: number;
      averageLatency: number;
      pendingMessages: number;
    };
  };
  setSessionState: (sessionState: SessionState) => void;
  getSessionState: () => SessionState | null;
  clearSessionState: () => void;
  // New message protocol methods
  clearMessageQueue: () => void;
  retryFailedMessages: () => void;
  getPendingMessagesCount: () => number;
  getMessageProtocolStats: () => {
    queuedMessages: number;
    sentMessages: number;
    failedMessages: number;
    acknowledgedMessages: number;
    duplicateMessages: number;
    averageLatency: number;
    lastActivity: Date | null;
  };
}

export const useWebSocketIntegration = (
  config: WebSocketIntegrationConfig,
  onMessage: (message: ServerMessage) => void,
  onBinaryMessage: (data: ArrayBuffer) => void,
  onConnectionChange: (connected: boolean) => void,
  onError: (error: Error) => void
): WebSocketIntegrationReturn => {
  // State management
  const [connectionState, setConnectionState] = useState<ConnectionState>(ConnectionState.DISCONNECTED);
  const [connectionQuality, setConnectionQuality] = useState<ConnectionQuality>('good');
  const [lastMessage, setLastMessage] = useState<ServerMessage | null>(null);
  const [lastBinaryMessage, setLastBinaryMessage] = useState<ArrayBuffer | null>(null);
  const [error, setError] = useState<Error | null>(null);


  // Refs
  const wsManagerRef = useRef<WebSocketManager | null>(null);
  const connectionPromiseRef = useRef<Promise<void> | null>(null);
  const { handleWebSocketError } = useErrorHandler();

  // Performance monitoring
  const performanceMonitoring = usePerformanceMonitoring({
    enabled: true,
    enableConnectionMonitoring: true,
    enableErrorTracking: true
  });

  // Create WebSocket configuration with defaults
  const wsConfig: WebSocketConfig = {
    url: config.url,
    reconnectInterval: config.reconnectInterval,
    maxReconnectAttempts: config.maxReconnectAttempts,
    heartbeatInterval: config.heartbeatInterval,
    sessionRecoveryTimeout: config.sessionRecoveryTimeout || 300000, // 5 minutes
    messageQueueSize: config.messageQueueSize || 100,
    connectionQualityThreshold: config.connectionQualityThreshold || 5000, // 5 seconds
  };

  // Enhanced message handler with session lifecycle integration
  const messageHandler: MessageHandler = {
    onMessage: (message: ServerMessage) => {
      setLastMessage(message);
      setError(null); // Clear error on successful message

      // Record performance metrics
      performanceMonitoring.recordConnectionMetrics({
        messagesReceived: 1
      });

      // Handle session-specific messages
      if (message.type === 'status_update') {
        // Update session state based on server status
        const sessionState = wsManagerRef.current?.getSessionState();
        if (sessionState) {
          sessionState.lastActivity = Date.now();
          wsManagerRef.current?.setSessionState(sessionState);
        }
      }

      onMessage(message);
    },

    onBinaryMessage: (data: ArrayBuffer) => {
      setLastBinaryMessage(data);
      setError(null); // Clear error on successful message

      // Record performance metrics
      performanceMonitoring.recordConnectionMetrics({
        messagesReceived: 1
      });

      // Update session activity
      const sessionState = wsManagerRef.current?.getSessionState();
      if (sessionState) {
        sessionState.lastActivity = Date.now();
        wsManagerRef.current?.setSessionState(sessionState);
      }

      onBinaryMessage(data);
    },

    onConnectionChange: (connected: boolean) => {
      const newState = connected ? ConnectionState.CONNECTED : ConnectionState.DISCONNECTED;
      setConnectionState(newState);

      // Record performance metrics
      performanceMonitoring.recordConnectionMetrics({
        connected
      });

      if (connected) {
        setError(null); // Clear error on successful connection
      }

      onConnectionChange(connected);
    },

    onError: (errorEvent: Event) => {
      const errorMessage = errorEvent.type === 'max_reconnect_attempts_reached'
        ? 'Failed to reconnect to server after multiple attempts'
        : 'WebSocket connection error occurred';

      const wsError = new Error(errorMessage);
      setError(wsError);

      // Record performance metrics
      performanceMonitoring.recordError('connection', wsError);

      // Handle different types of WebSocket errors with session context
      if (errorEvent.type === 'max_reconnect_attempts_reached') {
        handleWebSocketError('Connection failed: Maximum reconnection attempts reached. Please check your network connection.');
      } else {
        handleWebSocketError('Connection error: Unable to communicate with the server.');
      }

      onError(wsError);
    },

    onSessionRecovered: (sessionState: SessionState) => {
      console.log('Session recovered successfully:', sessionState.sessionId);
      // Session recovered successfully
      setError(null);

      // Notify about successful session recovery
      onMessage({
        type: 'status_update',
        data: {
          state: 'idle',
          utteranceId: sessionState.currentUtteranceId || undefined
        }
      });
    },

    onConnectionQualityChange: (quality: ConnectionQuality) => {
      setConnectionQuality(quality);

      // Record performance metrics
      performanceMonitoring.recordConnectionMetrics({
        quality
      });

      // Handle connection quality degradation
      if (quality === 'critical') {
        const qualityError = new Error('Connection quality is critical - experiencing significant delays');
        setError(qualityError);
        performanceMonitoring.recordError('connection', qualityError);
        onError(qualityError);
      } else if (quality === 'poor') {
        console.warn('Connection quality is poor - may experience delays');
      }
    }
  };

  // Initialize WebSocket manager
  useEffect(() => {
    const initStartTime = Date.now();

    try {
      wsManagerRef.current = new WebSocketManager(wsConfig, messageHandler);

      // Record successful initialization
      const initDuration = Date.now() - initStartTime;
      performanceMonitoring.recordServiceInitialization('websocket', initDuration, true);
    } catch (error) {
      // Record failed initialization
      const initDuration = Date.now() - initStartTime;
      performanceMonitoring.recordServiceInitialization('websocket', initDuration, false);
      performanceMonitoring.recordError('system', error as Error);
    }

    return () => {
      if (wsManagerRef.current) {
        wsManagerRef.current.disconnect();
      }
    };
  }, [config.url, performanceMonitoring]); // Only recreate if URL changes

  // Update connection state from manager and collect performance metrics
  useEffect(() => {
    const interval = setInterval(() => {
      if (wsManagerRef.current) {
        const currentState = wsManagerRef.current.getConnectionState();
        const currentQuality = wsManagerRef.current.getConnectionQuality();
        const stats = wsManagerRef.current.getConnectionStats();

        if (currentState !== connectionState) {
          setConnectionState(currentState);
        }

        if (currentQuality !== connectionQuality) {
          setConnectionQuality(currentQuality);
        }

        // Update performance metrics with connection statistics
        performanceMonitoring.recordConnectionMetrics({
          quality: currentQuality,
          connected: currentState === ConnectionState.CONNECTED,
          messagesSent: stats.messageProtocol.sentMessages,
          messagesReceived: stats.messageProtocol.acknowledgedMessages,
          messageFailures: stats.messageProtocol.failedMessages
        });
      }
    }, 100);

    return () => clearInterval(interval);
  }, [connectionState, connectionQuality, performanceMonitoring]);

  // Connect with Promise support for session lifecycle
  const connect = useCallback(async (): Promise<void> => {
    if (connectionPromiseRef.current) {
      return connectionPromiseRef.current;
    }

    connectionPromiseRef.current = new Promise<void>((resolve, reject) => {
      if (!wsManagerRef.current) {
        reject(new Error('WebSocket manager not initialized'));
        return;
      }

      // Set up one-time listeners for connection result
      const originalOnConnectionChange = messageHandler.onConnectionChange;
      const originalOnError = messageHandler.onError;

      let resolved = false;
      const timeout = setTimeout(() => {
        if (!resolved) {
          resolved = true;
          connectionPromiseRef.current = null;
          reject(new Error('Connection timeout'));
        }
      }, 10000); // 10 second timeout

      messageHandler.onConnectionChange = (connected: boolean) => {
        originalOnConnectionChange(connected);

        if (connected && !resolved) {
          resolved = true;
          clearTimeout(timeout);
          connectionPromiseRef.current = null;
          resolve();
        }
      };

      messageHandler.onError = (errorEvent: Event) => {
        originalOnError(errorEvent);

        if (!resolved && errorEvent.type === 'max_reconnect_attempts_reached') {
          resolved = true;
          clearTimeout(timeout);
          connectionPromiseRef.current = null;
          reject(new Error('Failed to connect after maximum attempts'));
        }
      };

      // Start connection
      wsManagerRef.current.connect();
    });

    return connectionPromiseRef.current;
  }, []);

  // Disconnect
  const disconnect = useCallback(() => {
    wsManagerRef.current?.disconnect();
    connectionPromiseRef.current = null;
    // Session recovery reset
  }, []);

  // Send message with session context and enhanced options
  const sendMessage = useCallback((message: ClientMessage, options?: {
    priority?: 'low' | 'normal' | 'high';
    requiresAck?: boolean;
    maxAttempts?: number;
  }): string => {
    const startTime = Date.now();
    const messageId = wsManagerRef.current?.sendMessage(message, options) || '';

    // Record performance metrics
    performanceMonitoring.recordConnectionMetrics({
      messagesSent: 1
    });

    // Record latency if this is a heartbeat or time-sensitive message
    if (message.type === 'ping') {
      performanceMonitoring.recordEvent({
        type: 'heartbeat_sent',
        timestamp: startTime,
        data: { messageId }
      });
    }

    // Update session activity
    const sessionState = wsManagerRef.current?.getSessionState();
    if (sessionState) {
      sessionState.lastActivity = Date.now();
      wsManagerRef.current?.setSessionState(sessionState);
    }

    return messageId;
  }, [performanceMonitoring]);

  // Send binary message with session context and enhanced options
  const sendBinaryMessage = useCallback((data: ArrayBuffer, options?: {
    priority?: 'low' | 'normal' | 'high';
    maxAttempts?: number;
  }): string => {
    const messageId = wsManagerRef.current?.sendBinaryMessage(data, options) || '';

    // Record performance metrics
    performanceMonitoring.recordConnectionMetrics({
      messagesSent: 1
    });

    // Update session activity
    const sessionState = wsManagerRef.current?.getSessionState();
    if (sessionState) {
      sessionState.lastActivity = Date.now();
      wsManagerRef.current?.setSessionState(sessionState);
    }

    return messageId;
  }, [performanceMonitoring]);

  // Retry connection
  const retryConnection = useCallback(async (): Promise<void> => {
    disconnect();
    await new Promise(resolve => setTimeout(resolve, 1000)); // Wait 1 second
    return connect();
  }, [connect, disconnect]);

  // Get connection statistics
  const getConnectionStats = useCallback(() => {
    return wsManagerRef.current?.getConnectionStats() || {
      state: ConnectionState.DISCONNECTED,
      quality: 'good' as ConnectionQuality,
      reconnectAttempts: 0,
      queuedMessages: 0,
      lastError: null,
      sessionRecoverable: false,
      messageProtocol: {
        queuedMessages: 0,
        sentMessages: 0,
        failedMessages: 0,
        acknowledgedMessages: 0,
        duplicateMessages: 0,
        averageLatency: 0,
        pendingMessages: 0
      }
    };
  }, []);

  // Session state management
  const setSessionState = useCallback((sessionState: SessionState) => {
    wsManagerRef.current?.setSessionState(sessionState);
  }, []);

  const getSessionState = useCallback((): SessionState | null => {
    return wsManagerRef.current?.getSessionState() || null;
  }, []);

  const clearSessionState = useCallback(() => {
    wsManagerRef.current?.clearSessionState();
    // Session recovery reset
  }, []);

  // New message protocol methods
  const clearMessageQueue = useCallback(() => {
    wsManagerRef.current?.clearMessageQueue();
  }, []);

  const retryFailedMessages = useCallback(() => {
    wsManagerRef.current?.retryFailedMessages();
  }, []);

  const getPendingMessagesCount = useCallback((): number => {
    return wsManagerRef.current?.getPendingMessagesCount() || 0;
  }, []);

  const getMessageProtocolStats = useCallback(() => {
    return wsManagerRef.current?.getMessageProtocolStats() || {
      queuedMessages: 0,
      sentMessages: 0,
      failedMessages: 0,
      acknowledgedMessages: 0,
      duplicateMessages: 0,
      averageLatency: 0,
      lastActivity: null
    };
  }, []);

  return {
    connectionState,
    connectionQuality,
    isConnected: connectionState === ConnectionState.CONNECTED,
    connect,
    disconnect,
    sendMessage,
    sendBinaryMessage,
    lastMessage,
    lastBinaryMessage,
    error,
    retryConnection,
    getConnectionStats,
    setSessionState,
    getSessionState,
    clearSessionState,
    clearMessageQueue,
    retryFailedMessages,
    getPendingMessagesCount,
    getMessageProtocolStats,
  };
};