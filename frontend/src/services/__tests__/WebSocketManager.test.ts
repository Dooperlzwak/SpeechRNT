/**
 * WebSocketManager Tests
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { WebSocketManager, ConnectionState, WebSocketConfig, MessageHandler } from '../WebSocketManager';

// Mock WebSocket
class MockWebSocket {
  static CONNECTING = 0;
  static OPEN = 1;
  static CLOSING = 2;
  static CLOSED = 3;

  readyState = MockWebSocket.CONNECTING;
  onopen: ((event: Event) => void) | null = null;
  onmessage: ((event: MessageEvent) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;
  onclose: ((event: CloseEvent) => void) | null = null;

  constructor(public url: string) {
    // Simulate connection after a short delay
    setTimeout(() => {
      this.readyState = MockWebSocket.OPEN;
      if (this.onopen) {
        this.onopen(new Event('open'));
      }
    }, 10);
  }

  send(data: string | ArrayBuffer): void {
    if (this.readyState !== MockWebSocket.OPEN) {
      throw new Error('WebSocket is not open');
    }
    // Mock successful send
  }

  close(code?: number, reason?: string): void {
    this.readyState = MockWebSocket.CLOSED;
    if (this.onclose) {
      this.onclose(new CloseEvent('close', { code: code || 1000, reason: reason || '' }));
    }
  }
}

// Replace global WebSocket with mock
(global as any).WebSocket = MockWebSocket;

describe('WebSocketManager', () => {
  let config: WebSocketConfig;
  let messageHandler: MessageHandler;
  let wsManager: WebSocketManager;

  beforeEach(() => {
    config = {
      url: 'ws://localhost:8080',
      reconnectInterval: 100,
      maxReconnectAttempts: 3,
      heartbeatInterval: 1000,
    };

    messageHandler = {
      onMessage: vi.fn(),
      onBinaryMessage: vi.fn(),
      onConnectionChange: vi.fn(),
      onError: vi.fn(),
    };

    wsManager = new WebSocketManager(config, messageHandler);
  });

  afterEach(() => {
    wsManager.disconnect();
  });

  it('should create WebSocketManager instance', () => {
    expect(wsManager).toBeInstanceOf(WebSocketManager);
    expect(wsManager.getConnectionState()).toBe(ConnectionState.DISCONNECTED);
  });

  it('should connect to WebSocket server', async () => {
    wsManager.connect();
    
    // Wait for connection
    await new Promise(resolve => setTimeout(resolve, 50));
    
    expect(wsManager.getConnectionState()).toBe(ConnectionState.CONNECTED);
    expect(wsManager.isConnected()).toBe(true);
    expect(messageHandler.onConnectionChange).toHaveBeenCalledWith(true);
  });

  it('should disconnect from WebSocket server', async () => {
    wsManager.connect();
    await new Promise(resolve => setTimeout(resolve, 50));
    
    wsManager.disconnect();
    
    expect(wsManager.getConnectionState()).toBe(ConnectionState.DISCONNECTED);
    expect(wsManager.isConnected()).toBe(false);
  });

  it('should send JSON message when connected', async () => {
    wsManager.connect();
    await new Promise(resolve => setTimeout(resolve, 50));
    
    const message = { type: 'test', data: 'hello' };
    const result = wsManager.sendMessage(message);
    
    expect(result).toBeTruthy(); // Now returns message ID string
    expect(typeof result).toBe('string');
  });

  it('should not send message when disconnected', () => {
    const message = { type: 'test', data: 'hello' };
    const result = wsManager.sendMessage(message);
    
    expect(result).toBe(false);
  });

  it('should send binary message when connected', async () => {
    wsManager.connect();
    await new Promise(resolve => setTimeout(resolve, 50));
    
    const binaryData = new ArrayBuffer(8);
    const result = wsManager.sendBinaryMessage(binaryData);
    
    expect(result).toBe(true);
  });

  it('should handle incoming JSON messages', async () => {
    wsManager.connect();
    await new Promise(resolve => setTimeout(resolve, 50));
    
    // Simulate incoming message
    const mockWs = (wsManager as any).ws;
    const testMessage = { type: 'test', data: 'response' };
    
    if (mockWs && mockWs.onmessage) {
      mockWs.onmessage(new MessageEvent('message', { 
        data: JSON.stringify(testMessage) 
      }));
    }
    
    expect(messageHandler.onMessage).toHaveBeenCalledWith(testMessage);
  });

  it('should handle incoming binary messages', async () => {
    wsManager.connect();
    await new Promise(resolve => setTimeout(resolve, 50));
    
    // Simulate incoming binary message
    const mockWs = (wsManager as any).ws;
    const binaryData = new ArrayBuffer(8);
    
    if (mockWs && mockWs.onmessage) {
      mockWs.onmessage(new MessageEvent('message', { data: binaryData }));
    }
    
    expect(messageHandler.onBinaryMessage).toHaveBeenCalledWith(binaryData, 'unknown');
  });
});

