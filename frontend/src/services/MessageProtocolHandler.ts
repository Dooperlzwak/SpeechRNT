/**
 * MessageProtocolHandler - Enhanced message protocol handling with queuing, acknowledgment, and retry mechanisms
 * 
 * This service provides comprehensive message protocol integration including:
 * - Proper message serialization/deserialization
 * - Message queuing for offline scenarios
 * - Message acknowledgment and retry mechanisms
 * - Message ordering and deduplication
 */

import { type ClientMessage, type ServerMessage } from '../types/messageProtocol';

export interface QueuedMessage {
  id: string;
  message: ClientMessage | ArrayBuffer;
  timestamp: Date;
  attempts: number;
  maxAttempts: number;
  priority: 'low' | 'normal' | 'high';
  requiresAck: boolean;
  ackTimeout: number;
  onSuccess?: (messageId: string) => void;
  onFailure?: (messageId: string, error: Error) => void;
}

export interface MessageAcknowledgment {
  messageId: string;
  status: 'received' | 'processed' | 'error';
  timestamp: Date;
  error?: string;
}

export interface MessageProtocolConfig {
  maxQueueSize: number;
  defaultMaxAttempts: number;
  defaultAckTimeout: number;
  retryDelayBase: number;
  retryDelayMax: number;
  enableDeduplication: boolean;
  enableOrdering: boolean;
  queuePersistence: boolean;
}

export interface MessageProtocolStats {
  queuedMessages: number;
  sentMessages: number;
  failedMessages: number;
  acknowledgedMessages: number;
  duplicateMessages: number;
  averageLatency: number;
  lastActivity: Date | null;
}

export class MessageProtocolHandler {
  private config: MessageProtocolConfig;
  private messageQueue: QueuedMessage[] = [];
  private pendingAcks: Map<string, QueuedMessage> = new Map();
  private messageHistory: Set<string> = new Set();
  private stats: MessageProtocolStats;
  private isProcessing = false;
  private processingTimer: ReturnType<typeof setInterval> | null = null;
  private ackTimeoutTimers: Map<string, ReturnType<typeof setTimeout>> = new Map();
  private sequenceNumber = 0;

  constructor(config: MessageProtocolConfig) {
    this.config = config;
    this.stats = {
      queuedMessages: 0,
      sentMessages: 0,
      failedMessages: 0,
      acknowledgedMessages: 0,
      duplicateMessages: 0,
      averageLatency: 0,
      lastActivity: null
    };

    // Load persisted queue if enabled
    if (this.config.queuePersistence) {
      this.loadPersistedQueue();
    }

    // Start processing timer
    this.startProcessing();
  }

  /**
   * Queue a message for sending with optional acknowledgment
   */
  queueMessage(
    message: ClientMessage | ArrayBuffer,
    options: {
      priority?: 'low' | 'normal' | 'high';
      maxAttempts?: number;
      requiresAck?: boolean;
      ackTimeout?: number;
      onSuccess?: (messageId: string) => void;
      onFailure?: (messageId: string, error: Error) => void;
    } = {}
  ): string {
    const messageId = this.generateMessageId();
    
    // Check for duplicate messages if deduplication is enabled
    if (this.isDuplicateMessage(message)) {
      console.warn('Duplicate message detected, skipping:', messageId);
      return messageId;
    }

    const queuedMessage: QueuedMessage = {
      id: messageId,
      message,
      timestamp: new Date(),
      attempts: 0,
      maxAttempts: options.maxAttempts || this.config.defaultMaxAttempts,
      priority: options.priority || 'normal',
      requiresAck: options.requiresAck || false,
      ackTimeout: options.ackTimeout || this.config.defaultAckTimeout,
      onSuccess: options.onSuccess,
      onFailure: options.onFailure
    };

    // Add message ID to message if it's a ClientMessage
    if (this.isClientMessage(message)) {
      (message as any).messageId = messageId;
      (message as any).sequenceNumber = ++this.sequenceNumber;
      
      // Determine if message should require acknowledgment based on type
      if (!options.requiresAck && this.shouldRequireAcknowledgment(message)) {
        queuedMessage.requiresAck = true;
      }
    }

    // Insert message based on priority and ordering
    this.insertMessageInQueue(queuedMessage);
    
    // Update stats
    this.stats.lastActivity = new Date();

    // Persist queue if enabled
    if (this.config.queuePersistence) {
      this.persistQueue();
    }

    console.log(`Message queued: ${messageId} (priority: ${queuedMessage.priority}, queue size: ${this.messageQueue.length})`);
    
    return messageId;
  }

  /**
   * Process the message queue and send messages
   */
  async processQueue(sendFunction: (message: ClientMessage | ArrayBuffer) => Promise<boolean>): Promise<void> {
    if (this.isProcessing || this.messageQueue.length === 0) {
      return;
    }

    this.isProcessing = true;

    try {
      // Process messages in order
      const messagesToProcess = [...this.messageQueue];
      this.messageQueue = [];

      for (const queuedMessage of messagesToProcess) {
        try {
          const success = await this.sendMessage(queuedMessage, sendFunction);
          
          if (success) {
            this.stats.sentMessages++;
            
            // If message requires acknowledgment, move to pending acks
            if (queuedMessage.requiresAck) {
              this.pendingAcks.set(queuedMessage.id, queuedMessage);
              this.startAckTimeout(queuedMessage);
            } else {
              // Call success callback immediately
              queuedMessage.onSuccess?.(queuedMessage.id);
            }
          } else {
            // Re-queue message for retry
            this.handleMessageFailure(queuedMessage, new Error('Send function returned false'));
          }
        } catch (error) {
          this.handleMessageFailure(queuedMessage, error as Error);
        }
      }
    } finally {
      this.isProcessing = false;
      
      // Persist queue if enabled
      if (this.config.queuePersistence) {
        this.persistQueue();
      }
    }
  }

  /**
   * Handle incoming message acknowledgment
   */
  handleAcknowledgment(ack: MessageAcknowledgment): void {
    const pendingMessage = this.pendingAcks.get(ack.messageId);
    
    if (!pendingMessage) {
      console.warn('Received acknowledgment for unknown message:', ack.messageId);
      return;
    }

    // Clear timeout timer
    const timeoutTimer = this.ackTimeoutTimers.get(ack.messageId);
    if (timeoutTimer) {
      clearTimeout(timeoutTimer);
      this.ackTimeoutTimers.delete(ack.messageId);
    }

    // Remove from pending acks
    this.pendingAcks.delete(ack.messageId);

    // Update stats
    this.stats.acknowledgedMessages++;
    this.updateLatencyStats(pendingMessage.timestamp);

    if (ack.status === 'processed') {
      console.log(`Message acknowledged successfully: ${ack.messageId}`);
      pendingMessage.onSuccess?.(ack.messageId);
    } else if (ack.status === 'error') {
      console.error(`Message acknowledgment error: ${ack.messageId} - ${ack.error}`);
      pendingMessage.onFailure?.(ack.messageId, new Error(ack.error || 'Unknown acknowledgment error'));
    }
  }

  /**
   * Handle incoming server message with proper deserialization
   */
  deserializeMessage(data: string | ArrayBuffer): ServerMessage | ArrayBuffer | null {
    try {
      if (typeof data === 'string') {
        const parsed = JSON.parse(data);
        
        // Validate message structure
        if (!this.isValidServerMessage(parsed)) {
          console.error('Invalid server message structure:', parsed);
          return null;
        }

        // Handle acknowledgment messages
        if (parsed.type === 'message_ack') {
          this.handleAcknowledgment(parsed.data as MessageAcknowledgment);
          return null; // Don't pass acknowledgments to application layer
        }
        
        // Handle session recovery acknowledgments
        if (parsed.type === 'session_recovery_ack') {
          this.handleAcknowledgment({
            messageId: parsed.sessionId || 'session_recovery',
            status: parsed.success ? 'processed' : 'error',
            timestamp: new Date(),
            error: parsed.error
          });
          return parsed; // Pass session recovery responses to application layer
        }

        return parsed as ServerMessage;
      } else {
        // Binary data - return as-is
        return data;
      }
    } catch (error) {
      console.error('Failed to deserialize message:', error);
      return null;
    }
  }

  /**
   * Serialize client message for sending
   */
  serializeMessage(message: ClientMessage): string {
    try {
      return JSON.stringify(message);
    } catch (error) {
      console.error('Failed to serialize message:', error);
      throw new Error(`Message serialization failed: ${error}`);
    }
  }

  /**
   * Get current queue statistics
   */
  getStats(): MessageProtocolStats {
    return {
      ...this.stats,
      queuedMessages: this.messageQueue.length
    };
  }

  /**
   * Clear the message queue
   */
  clearQueue(): void {
    this.messageQueue = [];
    this.pendingAcks.clear();
    this.messageHistory.clear();
    
    // Clear all timeout timers
    this.ackTimeoutTimers.forEach(timer => clearTimeout(timer));
    this.ackTimeoutTimers.clear();

    // Reset stats
    this.stats = {
      queuedMessages: 0,
      sentMessages: 0,
      failedMessages: 0,
      acknowledgedMessages: 0,
      duplicateMessages: 0,
      averageLatency: 0,
      lastActivity: null
    };

    if (this.config.queuePersistence) {
      this.persistQueue();
    }

    console.log('Message queue cleared');
  }

  /**
   * Get pending messages count
   */
  getPendingMessagesCount(): number {
    return this.messageQueue.length + this.pendingAcks.size;
  }

  /**
   * Retry failed messages
   */
  retryFailedMessages(): void {
    console.log('Manually retrying failed messages...');
    
    // Reset attempts for all queued messages to give them another chance
    this.messageQueue.forEach(message => {
      if (message.attempts > 0) {
        console.log(`Resetting attempts for message ${message.id} (was ${message.attempts})`);
        message.attempts = 0;
        message.timestamp = new Date(); // Update timestamp for immediate processing
      }
    });
    
    // Also retry any pending acknowledgment messages
    this.pendingAcks.forEach(message => {
      if (message.attempts > 0) {
        console.log(`Re-queuing pending ack message ${message.id} for retry`);
        
        // Clear the timeout timer
        const timeoutTimer = this.ackTimeoutTimers.get(message.id);
        if (timeoutTimer) {
          clearTimeout(timeoutTimer);
          this.ackTimeoutTimers.delete(message.id);
        }
        
        // Move back to queue for retry
        this.pendingAcks.delete(message.id);
        message.attempts = 0;
        message.timestamp = new Date();
        this.insertMessageInQueue(message);
      }
    });
    
    console.log(`Retry initiated for ${this.messageQueue.length} queued messages`);
  }

  /**
   * Cleanup resources
   */
  cleanup(): void {
    if (this.processingTimer) {
      clearInterval(this.processingTimer);
      this.processingTimer = null;
    }

    this.ackTimeoutTimers.forEach(timer => clearTimeout(timer));
    this.ackTimeoutTimers.clear();

    if (this.config.queuePersistence) {
      this.persistQueue();
    }
  }

  // Private methods

  private generateMessageId(): string {
    return `msg_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
  }

  private isClientMessage(message: ClientMessage | ArrayBuffer): message is ClientMessage {
    return typeof message === 'object' && message !== null && !(message instanceof ArrayBuffer);
  }

  private isValidServerMessage(obj: any): boolean {
    return obj && typeof obj === 'object' && typeof obj.type === 'string';
  }

  private shouldRequireAcknowledgment(message: ClientMessage): boolean {
    // Determine which message types should require acknowledgment
    switch (message.type) {
      case 'config':
        // Configuration changes should be acknowledged
        return true;
      case 'end_session':
        // Session end should be acknowledged
        return true;
      case 'recover_session':
        // Session recovery should be acknowledged
        return true;
      case 'ping':
        // Ping messages don't need acknowledgment (they get pong responses)
        return false;
      default:
        // Unknown message types default to no acknowledgment
        return false;
    }
  }

  private isDuplicateMessage(message: ClientMessage | ArrayBuffer): boolean {
    if (!this.config.enableDeduplication) {
      return false;
    }

    const messageHash = this.hashMessage(message);
    if (this.messageHistory.has(messageHash)) {
      this.stats.duplicateMessages++;
      return true;
    }

    this.messageHistory.add(messageHash);
    
    // Limit history size to prevent memory leaks
    if (this.messageHistory.size > 1000) {
      const firstHash = this.messageHistory.values().next().value;
      if (firstHash) {
        this.messageHistory.delete(firstHash);
      }
    }

    return false;
  }

  private hashMessage(message: ClientMessage | ArrayBuffer): string {
    if (message instanceof ArrayBuffer) {
      // Simple hash for binary data based on size and first few bytes
      const view = new Uint8Array(message);
      const sample = Array.from(view.slice(0, 16)).join(',');
      return `binary_${message.byteLength}_${sample}`;
    } else {
      // Hash JSON message content (excluding messageId and timestamp)
      const { messageId, sequenceNumber, ...content } = message as any;
      return JSON.stringify(content);
    }
  }

  private insertMessageInQueue(message: QueuedMessage): void {
    if (this.messageQueue.length >= this.config.maxQueueSize) {
      // Remove oldest low-priority message to make room
      const lowPriorityIndex = this.messageQueue.findIndex(msg => msg.priority === 'low');
      if (lowPriorityIndex !== -1) {
        const removed = this.messageQueue.splice(lowPriorityIndex, 1)[0];
        console.warn('Queue full, removed low-priority message:', removed.id);
        removed.onFailure?.(removed.id, new Error('Queue overflow'));
      } else {
        // Remove oldest message if no low-priority messages
        const removed = this.messageQueue.shift();
        if (removed) {
          console.warn('Queue full, removed oldest message:', removed.id);
          removed.onFailure?.(removed.id, new Error('Queue overflow'));
        }
      }
    }

    if (this.config.enableOrdering) {
      // Insert based on priority and timestamp
      let insertIndex = this.messageQueue.length;
      
      for (let i = 0; i < this.messageQueue.length; i++) {
        const existing = this.messageQueue[i];
        
        // Higher priority messages go first
        if (this.getPriorityValue(message.priority) > this.getPriorityValue(existing.priority)) {
          insertIndex = i;
          break;
        }
        
        // Same priority, maintain timestamp order
        if (message.priority === existing.priority && message.timestamp < existing.timestamp) {
          insertIndex = i;
          break;
        }
      }
      
      this.messageQueue.splice(insertIndex, 0, message);
    } else {
      this.messageQueue.push(message);
    }
  }

  private getPriorityValue(priority: 'low' | 'normal' | 'high'): number {
    switch (priority) {
      case 'high': return 3;
      case 'normal': return 2;
      case 'low': return 1;
      default: return 2;
    }
  }

  private async sendMessage(
    queuedMessage: QueuedMessage, 
    sendFunction: (message: ClientMessage | ArrayBuffer) => Promise<boolean>
  ): Promise<boolean> {
    queuedMessage.attempts++;

    try {
      const success = await sendFunction(queuedMessage.message);
      
      if (success) {
        console.log(`Message sent successfully: ${queuedMessage.id} (attempt ${queuedMessage.attempts})`);
        return true;
      } else {
        throw new Error('Send function returned false');
      }
    } catch (error) {
      console.error(`Failed to send message ${queuedMessage.id} (attempt ${queuedMessage.attempts}):`, error);
      throw error;
    }
  }

  private handleMessageFailure(queuedMessage: QueuedMessage, error: Error): void {
    this.stats.failedMessages++;

    if (queuedMessage.attempts >= queuedMessage.maxAttempts) {
      console.error(`Message failed permanently after ${queuedMessage.attempts} attempts: ${queuedMessage.id}`);
      queuedMessage.onFailure?.(queuedMessage.id, error);
    } else {
      // Calculate retry delay with exponential backoff
      const delay = Math.min(
        this.config.retryDelayBase * Math.pow(2, queuedMessage.attempts - 1),
        this.config.retryDelayMax
      );

      console.log(`Retrying message ${queuedMessage.id} in ${delay}ms (attempt ${queuedMessage.attempts + 1}/${queuedMessage.maxAttempts})`);

      // Re-queue message with delay
      setTimeout(() => {
        queuedMessage.timestamp = new Date();
        this.insertMessageInQueue(queuedMessage);
      }, delay);
    }
  }

  private startAckTimeout(queuedMessage: QueuedMessage): void {
    const timer = setTimeout(() => {
      console.warn(`Acknowledgment timeout for message: ${queuedMessage.id}`);
      
      this.pendingAcks.delete(queuedMessage.id);
      this.ackTimeoutTimers.delete(queuedMessage.id);
      
      queuedMessage.onFailure?.(queuedMessage.id, new Error('Acknowledgment timeout'));
    }, queuedMessage.ackTimeout);

    this.ackTimeoutTimers.set(queuedMessage.id, timer);
  }

  private updateLatencyStats(messageTimestamp: Date): void {
    const latency = Date.now() - messageTimestamp.getTime();
    
    // Simple moving average
    if (this.stats.averageLatency === 0) {
      this.stats.averageLatency = latency;
    } else {
      this.stats.averageLatency = (this.stats.averageLatency * 0.9) + (latency * 0.1);
    }
  }

  private startProcessing(): void {
    this.processingTimer = setInterval(() => {
      // This will be called by the WebSocket integration when ready to process
    }, 100);
  }

  private loadPersistedQueue(): void {
    try {
      const stored = localStorage.getItem('message_protocol_queue');
      if (stored) {
        const data = JSON.parse(stored);
        this.messageQueue = data.queue || [];
        this.stats = { ...this.stats, ...data.stats };
        console.log(`Loaded ${this.messageQueue.length} persisted messages`);
      }
    } catch (error) {
      console.warn('Failed to load persisted queue:', error);
      localStorage.removeItem('message_protocol_queue');
    }
  }

  private persistQueue(): void {
    try {
      const data = {
        queue: this.messageQueue,
        stats: this.stats,
        timestamp: Date.now()
      };
      localStorage.setItem('message_protocol_queue', JSON.stringify(data));
    } catch (error) {
      console.warn('Failed to persist queue:', error);
    }
  }
}