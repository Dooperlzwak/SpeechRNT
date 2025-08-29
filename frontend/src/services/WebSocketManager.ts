/**
 * WebSocketManager - Handles WebSocket connection with automatic reconnection
 * and enhanced message protocol handling for JSON and binary messages
 */

import { MessageProtocolHandler, type MessageProtocolConfig } from './MessageProtocolHandler';
import { type ClientMessage, type ServerMessage } from '../types/messageProtocol';

export interface WebSocketConfig {
  url: string;
  reconnectInterval: number;
  maxReconnectAttempts: number;
  heartbeatInterval: number;
  sessionRecoveryTimeout: number;
  messageQueueSize: number;
  connectionQualityThreshold: number;
  // Message protocol configuration
  messageProtocol?: {
    maxQueueSize?: number;
    defaultMaxAttempts?: number;
    defaultAckTimeout?: number;
    retryDelayBase?: number;
    retryDelayMax?: number;
    enableDeduplication?: boolean;
    enableOrdering?: boolean;
    queuePersistence?: boolean;
  };
}

export interface SessionState {
  sessionId: string;
  sourceLang: string;
  targetLang: string;
  selectedVoice: string;
  isActive: boolean;
  lastActivity: number;
  pendingUtterances: any[];
  currentUtteranceId: number | null;
}

export type ConnectionQuality = 'good' | 'poor' | 'critical';

export interface MessageHandler {
  onMessage: (message: ServerMessage) => void;
  onBinaryMessage: (data: ArrayBuffer, messageType?: string) => void;
  onConnectionChange: (connected: boolean) => void;
  onError: (error: Event) => void;
  onSessionRecovered?: (sessionState: SessionState) => void;
  onConnectionQualityChange?: (quality: ConnectionQuality) => void;
  onMessageSent?: (messageId: string) => void;
  onMessageFailed?: (messageId: string, error: Error) => void;
}

export enum ConnectionState {
  DISCONNECTED = 'disconnected',
  CONNECTING = 'connecting',
  CONNECTED = 'connected',
  RECONNECTING = 'reconnecting'
}

export class WebSocketManager {
  private ws: WebSocket | null = null;
  private config: WebSocketConfig;
  private messageHandler: MessageHandler;
  private connectionState: ConnectionState = ConnectionState.DISCONNECTED;
  private reconnectAttempts = 0;
  private reconnectTimer: number | null = null;
  private heartbeatTimer: number | null = null;
  private pendingAudioMessage: any = null;
  private lastError: Error | null = null;
  
  // Enhanced message protocol handling
  private messageProtocolHandler: MessageProtocolHandler;
  private messageProcessingTimer: ReturnType<typeof setInterval> | null = null;
  
  // Session state recovery
  private sessionState: SessionState | null = null;
  private isRecovering = false;
  private lastHeartbeatResponse = Date.now();
  private connectionQuality: ConnectionQuality = 'good';

  constructor(config: WebSocketConfig, messageHandler: MessageHandler) {
    this.config = config;
    this.messageHandler = messageHandler;
    
    // Initialize message protocol handler
    const protocolConfig: MessageProtocolConfig = {
      maxQueueSize: config.messageProtocol?.maxQueueSize || config.messageQueueSize || 100,
      defaultMaxAttempts: config.messageProtocol?.defaultMaxAttempts || 3,
      defaultAckTimeout: config.messageProtocol?.defaultAckTimeout || 10000,
      retryDelayBase: config.messageProtocol?.retryDelayBase || 1000,
      retryDelayMax: config.messageProtocol?.retryDelayMax || 30000,
      enableDeduplication: config.messageProtocol?.enableDeduplication ?? true,
      enableOrdering: config.messageProtocol?.enableOrdering ?? true,
      queuePersistence: config.messageProtocol?.queuePersistence ?? true,
    };
    
    this.messageProtocolHandler = new MessageProtocolHandler(protocolConfig);
    this.startMessageProcessing();
  }

  /**
   * Connect to the WebSocket server
   */
  connect(): void {
    if (this.connectionState === ConnectionState.CONNECTED || 
        this.connectionState === ConnectionState.CONNECTING) {
      return;
    }

    this.setConnectionState(ConnectionState.CONNECTING);
    
    // Try to recover session state if not already set
    if (!this.sessionState) {
      this.sessionState = this.recoverSessionState();
    }
    
    try {
      this.ws = new WebSocket(this.config.url);
      this.setupEventHandlers();
    } catch (error) {
      console.error('Failed to create WebSocket connection:', error);
      this.handleConnectionError();
    }
  }

  /**
   * Disconnect from the WebSocket server
   */
  disconnect(): void {
    this.clearTimers();
    this.reconnectAttempts = 0;
    
    if (this.ws) {
      this.ws.close(1000, 'Client disconnect');
      this.ws = null;
    }
    
    this.setConnectionState(ConnectionState.DISCONNECTED);
  }

  /**
   * Send a JSON message with enhanced protocol handling
   */
  sendMessage(message: ClientMessage, options?: {
    priority?: 'low' | 'normal' | 'high';
    requiresAck?: boolean;
    maxAttempts?: number;
  }): string {
    const messageId = this.messageProtocolHandler.queueMessage(message, {
      priority: options?.priority || 'normal',
      requiresAck: options?.requiresAck || false,
      maxAttempts: options?.maxAttempts,
      onSuccess: (id) => {
        console.log(`Message sent successfully: ${id}`);
        this.messageHandler.onMessageSent?.(id);
      },
      onFailure: (id, error) => {
        console.error(`Message failed: ${id}`, error);
        this.messageHandler.onMessageFailed?.(id, error);
      }
    });

    return messageId;
  }

  /**
   * Send binary data with enhanced protocol handling
   */
  sendBinaryMessage(data: ArrayBuffer, options?: {
    priority?: 'low' | 'normal' | 'high';
    maxAttempts?: number;
  }): string {
    const messageId = this.messageProtocolHandler.queueMessage(data, {
      priority: options?.priority || 'high', // Binary data typically has high priority
      requiresAck: false, // Binary data usually doesn't require acknowledgment
      maxAttempts: options?.maxAttempts || 1, // Binary data typically shouldn't be retried
      onSuccess: (id) => {
        console.log(`Binary message sent successfully: ${id}`);
        this.messageHandler.onMessageSent?.(id);
      },
      onFailure: (id, error) => {
        console.error(`Binary message failed: ${id}`, error);
        this.messageHandler.onMessageFailed?.(id, error);
      }
    });

    return messageId;
  }

  /**
   * Get current connection state
   */
  getConnectionState(): ConnectionState {
    return this.connectionState;
  }

  /**
   * Check if connected
   */
  isConnected(): boolean {
    return this.connectionState === ConnectionState.CONNECTED;
  }

  /**
   * Get last error
   */
  getLastError(): Error | null {
    return this.lastError;
  }

  /**
   * Force reconnection
   */
  forceReconnect(): void {
    this.disconnect();
    this.reconnectAttempts = 0;
    this.connect();
  }

  /**
   * Set session state for recovery
   */
  setSessionState(sessionState: SessionState): void {
    this.sessionState = sessionState;
    // Store in localStorage for persistence across page reloads
    try {
      localStorage.setItem('websocket_session_state', JSON.stringify(sessionState));
    } catch (error) {
      console.warn('Failed to store session state:', error);
    }
  }

  /**
   * Get current session state
   */
  getSessionState(): SessionState | null {
    return this.sessionState;
  }

  /**
   * Recover session state from storage
   */
  private recoverSessionState(): SessionState | null {
    try {
      const stored = localStorage.getItem('websocket_session_state');
      if (stored) {
        const sessionState = JSON.parse(stored);
        // Check if session is still valid (not too old)
        const now = Date.now();
        if (now - sessionState.lastActivity < this.config.sessionRecoveryTimeout) {
          return sessionState;
        } else {
          // Session expired, clean up
          localStorage.removeItem('websocket_session_state');
        }
      }
    } catch (error) {
      console.warn('Failed to recover session state:', error);
      localStorage.removeItem('websocket_session_state');
    }
    return null;
  }

  /**
   * Clear session state
   */
  clearSessionState(): void {
    this.sessionState = null;
    try {
      localStorage.removeItem('websocket_session_state');
    } catch (error) {
      console.warn('Failed to clear session state:', error);
    }
  }

  /**
   * Start message processing timer with offline scenario handling
   */
  private startMessageProcessing(): void {
    this.messageProcessingTimer = setInterval(async () => {
      // Always try to process the queue, even when disconnected
      // The message protocol handler will queue messages when offline
      await this.messageProtocolHandler.processQueue(this.sendRawMessage.bind(this));
      
      // If we have queued messages but are disconnected, log the queue status
      const queuedCount = this.messageProtocolHandler.getPendingMessagesCount();
      if (queuedCount > 0 && this.connectionState !== ConnectionState.CONNECTED) {
        console.log(`${queuedCount} messages queued while offline`);
      }
    }, 100);
  }

  /**
   * Send raw message through WebSocket (used by message protocol handler)
   */
  private async sendRawMessage(message: ClientMessage | ArrayBuffer): Promise<boolean> {
    // Check if we're connected
    if (this.connectionState !== ConnectionState.CONNECTED || !this.ws) {
      console.warn('Cannot send message - WebSocket not connected. Message will remain queued.');
      return false;
    }

    // Check WebSocket ready state
    if (this.ws.readyState !== WebSocket.OPEN) {
      console.warn('Cannot send message - WebSocket not in OPEN state. Current state:', this.ws.readyState);
      return false;
    }

    try {
      if (message instanceof ArrayBuffer) {
        this.ws.send(message);
        console.log('Sent binary message:', message.byteLength, 'bytes');
      } else {
        const serialized = this.messageProtocolHandler.serializeMessage(message);
        this.ws.send(serialized);
        console.log('Sent JSON message:', message.type);
      }
      return true;
    } catch (error) {
      console.error('Failed to send raw message:', error);
      this.lastError = error as Error;
      
      // If send fails due to connection issues, trigger reconnection
      if (error instanceof Error && (
          error.message.includes('WebSocket') || 
          error.message.includes('INVALID_STATE') ||
          error.message.includes('NETWORK_ERR')
      )) {
        console.warn('Send failed due to connection issue, triggering reconnection');
        this.handleConnectionError();
      }
      
      return false;
    }
  }

  /**
   * Process queued messages (legacy method for compatibility)
   */
  private processMessageQueue(): void {
    // This method is now handled by the message protocol handler
    // Keep for compatibility but delegate to the new system
    if (this.connectionState === ConnectionState.CONNECTED) {
      this.messageProtocolHandler.processQueue(this.sendRawMessage.bind(this));
    }
  }

  /**
   * Monitor connection quality
   */
  private updateConnectionQuality(): void {
    const now = Date.now();
    const timeSinceLastHeartbeat = now - this.lastHeartbeatResponse;
    
    let newQuality: ConnectionQuality;
    if (timeSinceLastHeartbeat < this.config.connectionQualityThreshold) {
      newQuality = 'good';
    } else if (timeSinceLastHeartbeat < this.config.connectionQualityThreshold * 2) {
      newQuality = 'poor';
    } else {
      newQuality = 'critical';
    }

    if (newQuality !== this.connectionQuality) {
      this.connectionQuality = newQuality;
      this.messageHandler.onConnectionQualityChange?.(newQuality);
      
      // Take action based on quality
      if (newQuality === 'critical' && this.connectionState === ConnectionState.CONNECTED) {
        console.warn('Connection quality critical, attempting reconnection');
        this.forceReconnect();
      }
    }
  }

  /**
   * Get connection quality
   */
  getConnectionQuality(): ConnectionQuality {
    return this.connectionQuality;
  }

  /**
   * Get connection statistics including message protocol stats
   */
  getConnectionStats(): {
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
  } {
    const protocolStats = this.messageProtocolHandler.getStats();
    
    return {
      state: this.connectionState,
      quality: this.connectionQuality,
      reconnectAttempts: this.reconnectAttempts,
      queuedMessages: protocolStats.queuedMessages,
      lastError: this.lastError?.message || null,
      sessionRecoverable: this.sessionState !== null,
      messageProtocol: {
        queuedMessages: protocolStats.queuedMessages,
        sentMessages: protocolStats.sentMessages,
        failedMessages: protocolStats.failedMessages,
        acknowledgedMessages: protocolStats.acknowledgedMessages,
        duplicateMessages: protocolStats.duplicateMessages,
        averageLatency: protocolStats.averageLatency,
        pendingMessages: this.messageProtocolHandler.getPendingMessagesCount()
      }
    };
  }

  private setupEventHandlers(): void {
    if (!this.ws) return;

    this.ws.onopen = () => {
      console.log('WebSocket connected');
      this.reconnectAttempts = 0;
      this.lastHeartbeatResponse = Date.now();
      this.setConnectionState(ConnectionState.CONNECTED);
      this.startHeartbeat();
      
      // Attempt session recovery if we have session state
      if (this.sessionState && !this.isRecovering) {
        this.attemptSessionRecovery();
      }
      
      // Process any queued messages
      this.processMessageQueue();
    };

    this.ws.onmessage = (event) => {
      if (typeof event.data === 'string') {
        // Use message protocol handler for deserialization
        const deserializedMessage = this.messageProtocolHandler.deserializeMessage(event.data);
        
        if (deserializedMessage) {
          const message = deserializedMessage as ServerMessage;
          
          // Handle heartbeat responses
          if (message.type === 'pong') {
            this.lastHeartbeatResponse = Date.now();
            this.updateConnectionQuality();
            return;
          }
          
          // Handle session recovery responses
          if (message.type === 'session_recovered') {
            this.handleSessionRecoveryResponse(message);
            return;
          }
          
          // Handle message acknowledgments
          if (message.type === 'message_ack') {
            // This is handled by the MessageProtocolHandler
            return;
          }
          
          // Check if this is an audio-related message that precedes binary data
          if (message.type === 'audio_start' || message.type === 'audio_data') {
            this.pendingAudioMessage = message;
          }
          
          // Handle all other message types and pass them to the application layer
          this.validateAndProcessMessage(message);
          
          this.messageHandler.onMessage(message);
        }
      } else if (event.data instanceof ArrayBuffer) {
        // Use message protocol handler for binary data
        const deserializedData = this.messageProtocolHandler.deserializeMessage(event.data);
        
        if (deserializedData instanceof ArrayBuffer) {
          // Determine message type based on pending audio message
          const messageType = this.pendingAudioMessage?.type || 'unknown';
          this.messageHandler.onBinaryMessage(deserializedData, messageType);
          this.pendingAudioMessage = null;
        }
      } else if (event.data instanceof Blob) {
        // Convert Blob to ArrayBuffer and process
        event.data.arrayBuffer().then((buffer) => {
          const deserializedData = this.messageProtocolHandler.deserializeMessage(buffer);
          
          if (deserializedData instanceof ArrayBuffer) {
            const messageType = this.pendingAudioMessage?.type || 'unknown';
            this.messageHandler.onBinaryMessage(deserializedData, messageType);
            this.pendingAudioMessage = null;
          }
        });
      }
    };

    this.ws.onerror = (error) => {
      console.error('WebSocket error:', error);
      this.lastError = new Error('WebSocket connection error');
      this.messageHandler.onError(error);
    };

    this.ws.onclose = (event) => {
      console.log('WebSocket closed:', event.code, event.reason);
      this.clearTimers();
      
      if (event.code !== 1000) { // Not a normal closure
        this.handleConnectionError();
      } else {
        this.setConnectionState(ConnectionState.DISCONNECTED);
      }
    };
  }

  private handleConnectionError(): void {
    this.clearTimers();
    
    if (this.reconnectAttempts < this.config.maxReconnectAttempts) {
      this.setConnectionState(ConnectionState.RECONNECTING);
      
      // Exponential backoff
      const delay = Math.min(
        this.config.reconnectInterval * Math.pow(2, this.reconnectAttempts),
        30000 // Max 30 seconds
      );
      
      console.log(`Reconnecting in ${delay}ms (attempt ${this.reconnectAttempts + 1}/${this.config.maxReconnectAttempts})`);
      
      this.reconnectTimer = window.setTimeout(() => {
        this.reconnectAttempts++;
        this.connect();
      }, delay);
    } else {
      console.error('Max reconnection attempts reached');
      this.lastError = new Error(`Failed to reconnect after ${this.config.maxReconnectAttempts} attempts`);
      this.setConnectionState(ConnectionState.DISCONNECTED);
      
      // Notify about connection failure
      this.messageHandler.onError(new Event('max_reconnect_attempts_reached'));
    }
  }

  private setConnectionState(state: ConnectionState): void {
    if (this.connectionState !== state) {
      this.connectionState = state;
      this.messageHandler.onConnectionChange(state === ConnectionState.CONNECTED);
    }
  }

  private startHeartbeat(): void {
    this.heartbeatTimer = window.setInterval(() => {
      if (this.ws && this.ws.readyState === WebSocket.OPEN) {
        // Send ping message
        this.sendMessage({ type: 'ping' });
        
        // Update connection quality based on heartbeat response time
        this.updateConnectionQuality();
      }
    }, this.config.heartbeatInterval);
  }

  /**
   * Attempt to recover session state
   */
  private attemptSessionRecovery(): void {
    if (!this.sessionState || this.isRecovering) {
      return;
    }

    this.isRecovering = true;
    console.log('Attempting session recovery for session:', this.sessionState.sessionId);

    // Send session recovery request
    this.sendMessage({
      type: 'recover_session',
      sessionId: this.sessionState.sessionId,
      lastActivity: this.sessionState.lastActivity,
      sessionState: this.sessionState
    });

    // Set timeout for recovery attempt
    setTimeout(() => {
      if (this.isRecovering) {
        console.warn('Session recovery timed out');
        this.isRecovering = false;
        // Clear invalid session state
        this.clearSessionState();
      }
    }, 10000); // 10 second timeout
  }

  /**
   * Handle session recovery response
   */
  private handleSessionRecoveryResponse(message: any): void {
    this.isRecovering = false;

    if (message.success) {
      console.log('Session recovery successful');
      
      // Update session state with any server-side changes
      if (message.sessionState) {
        this.sessionState = message.sessionState;
        if (this.sessionState) {
          this.setSessionState(this.sessionState);
        }
      }

      // Notify handler about successful recovery
      if (this.messageHandler.onSessionRecovered && this.sessionState) {
        this.messageHandler.onSessionRecovered(this.sessionState);
      }
    } else {
      console.warn('Session recovery failed:', message.reason);
      // Clear invalid session state
      this.clearSessionState();
    }
  }

  /**
   * Validate and process incoming server messages
   */
  private validateAndProcessMessage(message: ServerMessage): void {
    // Validate message structure based on type
    const isValid = this.validateMessageStructure(message);
    
    if (!isValid) {
      console.error('Invalid message structure received:', message);
      return;
    }
    
    // Update connection quality based on message reception
    this.lastHeartbeatResponse = Date.now();
    this.updateConnectionQuality();
    
    // Pass validated message to handler
    this.messageHandler.onMessage(message);
  }

  /**
   * Validate message structure based on message type
   */
  private validateMessageStructure(message: ServerMessage): boolean {
    if (!message || typeof message.type !== 'string') {
      return false;
    }

    // Validate specific message types
    switch (message.type) {
      case 'transcription_update':
        return !!(message.data?.text !== undefined && 
                 typeof message.data.utteranceId === 'number' &&
                 typeof message.data.confidence === 'number');
                 
      case 'translation_result':
        return !!(message.data?.originalText && 
                 message.data?.translatedText &&
                 typeof message.data.utteranceId === 'number');
                 
      case 'audio_start':
        return !!(typeof message.data?.utteranceId === 'number' &&
                 typeof message.data?.duration === 'number');
                 
      case 'audio_data':
        return !!(typeof message.data?.utteranceId === 'number' &&
                 typeof message.data?.sequenceNumber === 'number' &&
                 typeof message.data?.isLast === 'boolean');
                 
      case 'audio_end':
        return !!(typeof message.data?.utteranceId === 'number');
        
      case 'status_update':
        return !!(message.data?.state && 
                 ['idle', 'listening', 'thinking', 'speaking'].includes(message.data.state));
                 
      case 'error':
        return !!(message.data?.message);
        
      case 'pong':
        return true; // Pong messages don't require data
        
      case 'session_recovered':
        return !!(typeof message.success === 'boolean');
        
      case 'message_ack':
        return !!(message.data?.messageId && 
                 message.data?.status && 
                 ['received', 'processed', 'error'].includes(message.data.status));
        
      default:
        console.warn('Unknown message type received:', (message as any).type);
        return true; // Allow unknown message types to pass through
    }
  }

  /**
   * Get message protocol statistics
   */
  getMessageProtocolStats() {
    return this.messageProtocolHandler.getStats();
  }

  /**
   * Clear message queue
   */
  clearMessageQueue(): void {
    this.messageProtocolHandler.clearQueue();
  }

  /**
   * Retry failed messages
   */
  retryFailedMessages(): void {
    this.messageProtocolHandler.retryFailedMessages();
  }

  /**
   * Get pending messages count
   */
  getPendingMessagesCount(): number {
    return this.messageProtocolHandler.getPendingMessagesCount();
  }

  private clearTimers(): void {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }

    if (this.messageProcessingTimer) {
      clearInterval(this.messageProcessingTimer);
      this.messageProcessingTimer = null;
    }

    // Cleanup message protocol handler
    this.messageProtocolHandler.cleanup();
  }
}