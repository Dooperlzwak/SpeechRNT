/**
 * useWebSocket - React hook for managing WebSocket connection
 */

import { useEffect, useRef, useState, useCallback } from 'react';
import { WebSocketManager, ConnectionState, type WebSocketConfig, type MessageHandler } from '../services/WebSocketManager';
import { type ServerMessage, type ClientMessage } from '../types/messageProtocol';
import { useErrorHandler } from './useErrorHandler';

interface UseWebSocketOptions {
  url: string;
  reconnectInterval?: number;
  maxReconnectAttempts?: number;
  heartbeatInterval?: number;
  autoConnect?: boolean;
}

interface UseWebSocketReturn {
  connectionState: ConnectionState;
  isConnected: boolean;
  connect: () => void;
  disconnect: () => void;
  sendMessage: (message: ClientMessage) => boolean;
  sendBinaryMessage: (data: ArrayBuffer) => boolean;
  lastMessage: ServerMessage | null;
  lastBinaryMessage: ArrayBuffer | null;
  error: Event | null;
  forceReconnect: () => void;
}

export const useWebSocket = (
  options: UseWebSocketOptions,
  onMessage?: (message: ServerMessage) => void,
  onBinaryMessage?: (data: ArrayBuffer) => void
): UseWebSocketReturn => {
  const [connectionState, setConnectionState] = useState<ConnectionState>(ConnectionState.DISCONNECTED);
  const [lastMessage, setLastMessage] = useState<ServerMessage | null>(null);
  const [lastBinaryMessage, setLastBinaryMessage] = useState<ArrayBuffer | null>(null);
  const [error, setError] = useState<Event | null>(null);
  
  const wsManagerRef = useRef<WebSocketManager | null>(null);
  const { handleWebSocketError } = useErrorHandler();

  const config: WebSocketConfig = {
    url: options.url,
    reconnectInterval: options.reconnectInterval || 1000,
    maxReconnectAttempts: options.maxReconnectAttempts || 5,
    heartbeatInterval: options.heartbeatInterval || 30000,
    sessionRecoveryTimeout: 30000,
    messageQueueSize: 100,
    connectionQualityThreshold: 0.8,
  };

  const messageHandler: MessageHandler = {
    onMessage: (message: ServerMessage) => {
      setLastMessage(message);
      onMessage?.(message);
    },
    onBinaryMessage: (data: ArrayBuffer) => {
      setLastBinaryMessage(data);
      onBinaryMessage?.(data);
    },
    onConnectionChange: (connected: boolean) => {
      setConnectionState(connected ? ConnectionState.CONNECTED : ConnectionState.DISCONNECTED);
    },
    onError: (errorEvent: Event) => {
      setError(errorEvent);
      
      // Handle different types of WebSocket errors
      if (errorEvent.type === 'max_reconnect_attempts_reached') {
        handleWebSocketError('Failed to reconnect to server after multiple attempts');
      } else {
        handleWebSocketError('WebSocket connection error occurred');
      }
    },
  };

  // Initialize WebSocket manager
  useEffect(() => {
    wsManagerRef.current = new WebSocketManager(config, messageHandler);
    
    if (options.autoConnect !== false) {
      wsManagerRef.current.connect();
    }

    return () => {
      if (wsManagerRef.current) {
        wsManagerRef.current.disconnect();
      }
    };
  }, [options.url]); // Only recreate if URL changes

  const connect = useCallback(() => {
    wsManagerRef.current?.connect();
  }, []);

  const disconnect = useCallback(() => {
    wsManagerRef.current?.disconnect();
  }, []);

  const sendMessage = useCallback((message: ClientMessage): boolean => {
    const result = wsManagerRef.current?.sendMessage(message);
    return result !== undefined;
  }, []);

  const sendBinaryMessage = useCallback((data: ArrayBuffer): boolean => {
    const result = wsManagerRef.current?.sendBinaryMessage(data);
    return result !== undefined;
  }, []);

  const forceReconnect = useCallback(() => {
    wsManagerRef.current?.forceReconnect();
  }, []);

  // Update connection state from manager
  useEffect(() => {
    const interval = setInterval(() => {
      if (wsManagerRef.current) {
        const currentState = wsManagerRef.current.getConnectionState();
        if (currentState !== connectionState) {
          setConnectionState(currentState);
        }
      }
    }, 100);

    return () => clearInterval(interval);
  }, [connectionState]);

  return {
    connectionState,
    isConnected: connectionState === ConnectionState.CONNECTED,
    connect,
    disconnect,
    sendMessage,
    sendBinaryMessage,
    lastMessage,
    lastBinaryMessage,
    error,
    forceReconnect,
  };
};