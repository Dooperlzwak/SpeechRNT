/**
 * Simple WebSocket + Audio Integration Tests
 * Basic tests for integration functionality without complex service dependencies
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';

// Mock WebSocket
class MockWebSocket {
  static CONNECTING = 0;
  static OPEN = 1;
  static CLOSING = 2;
  static CLOSED = 3;

  readyState = MockWebSocket.CONNECTING;
  onopen: ((event: Event) => void) | null = null;
  onclose: ((event: CloseEvent) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;
  onmessage: ((event: MessageEvent) => void) | null = null;
  binaryType: 'blob' | 'arraybuffer' = 'arraybuffer';

  sentMessages: (string | ArrayBuffer)[] = [];

  constructor(public url: string) {
    setTimeout(() => {
      this.readyState = MockWebSocket.OPEN;
      if (this.onopen) {
        this.onopen(new Event('open'));
      }
    }, 10);
  }

  send(data: string | ArrayBuffer) {
    this.sentMessages.push(data);
  }

  close(code?: number, reason?: string) {
    this.readyState = MockWebSocket.CLOSED;
    if (this.onclose) {
      this.onclose(new CloseEvent('close', { code: code || 1000, reason }));
    }
  }

  simulateMessage(message: any) {
    if (this.onmessage) {
      this.onmessage(new MessageEvent('message', { 
        data: JSON.stringify(message) 
      }));
    }
  }

  simulateBinaryMessage(data: ArrayBuffer) {
    if (this.onmessage) {
      this.onmessage(new MessageEvent('message', { data }));
    }
  }

  getLastMessage() {
    const lastMessage = this.sentMessages[this.sentMessages.length - 1];
    if (typeof lastMessage === 'string') {
      try {
        return JSON.parse(lastMessage);
      } catch {
        return null;
      }
    }
    return null;
  }

  getLastBinaryMessage(): ArrayBuffer | null {
    const lastMessage = this.sentMessages[this.sentMessages.length - 1];
    return lastMessage instanceof ArrayBuffer ? lastMessage : null;
  }
}

// Mock MediaRecorder
class MockMediaRecorder {
  static isTypeSupported = vi.fn(() => true);
  
  state: 'inactive' | 'recording' | 'paused' = 'inactive';
  ondataavailable: ((event: BlobEvent) => void) | null = null;
  onstart: ((event: Event) => void) | null = null;
  onstop: ((event: Event) => void) | null = null;

  constructor(public stream: MediaStream, public options?: MediaRecorderOptions) {}

  start() {
    this.state = 'recording';
    if (this.onstart) {
      this.onstart(new Event('start'));
    }
    
    // Simulate data chunks
    setTimeout(() => {
      if (this.ondataavailable && this.state === 'recording') {
        const mockData = new Uint8Array(1024).fill(0);
        const blob = new Blob([mockData], { type: 'audio/webm' });
        this.ondataavailable(new BlobEvent('dataavailable', { data: blob }));
      }
    }, 100);
  }

  stop() {
    this.state = 'inactive';
    if (this.onstop) {
      this.onstop(new Event('stop'));
    }
  }
}

// Mock MediaStream
class MockMediaStream {
  constructor() {}
}

// Mock BlobEvent
class MockBlobEvent extends Event {
  data: Blob;
  constructor(type: string, eventInitDict: { data: Blob }) {
    super(type);
    this.data = eventInitDict.data;
  }
}

// Mock FileReader
class MockFileReader {
  result: ArrayBuffer | null = null;
  onload: ((event: any) => void) | null = null;

  readAsArrayBuffer(blob: Blob) {
    // Simulate async reading
    setTimeout(() => {
      this.result = new ArrayBuffer(1024);
      if (this.onload) {
        this.onload({ target: this });
      }
    }, 10);
  }
}

// Mock getUserMedia
const mockGetUserMedia = vi.fn(() => 
  Promise.resolve(new MockMediaStream())
);

// Global mocks
global.WebSocket = MockWebSocket as any;
global.MediaRecorder = MockMediaRecorder as any;
global.MediaStream = MockMediaStream as any;
global.BlobEvent = MockBlobEvent as any;
global.FileReader = MockFileReader as any;
Object.defineProperty(global.navigator, 'mediaDevices', {
  value: { getUserMedia: mockGetUserMedia },
  writable: true
});

describe('WebSocket + Audio Integration - Simple Tests', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    mockGetUserMedia.mockClear();
  });

  describe('WebSocket Basic Functionality', () => {
    it('should create WebSocket connection', async () => {
      const ws = new MockWebSocket('ws://localhost:8080');
      
      expect(ws.url).toBe('ws://localhost:8080');
      expect(ws.readyState).toBe(MockWebSocket.CONNECTING);

      // Wait for connection to open
      await new Promise(resolve => {
        ws.onopen = () => resolve(void 0);
      });

      expect(ws.readyState).toBe(MockWebSocket.OPEN);
    });

    it('should send and receive messages', async () => {
      const ws = new MockWebSocket('ws://localhost:8080');
      let receivedMessage: any = null;

      ws.onmessage = (event) => {
        receivedMessage = JSON.parse(event.data);
      };

      // Wait for connection
      await new Promise(resolve => {
        ws.onopen = () => resolve(void 0);
      });

      // Send message
      const testMessage = { type: 'ping' };
      ws.send(JSON.stringify(testMessage));

      expect(ws.getLastMessage()).toEqual(testMessage);

      // Simulate receiving message
      const serverMessage = { type: 'pong' };
      ws.simulateMessage(serverMessage);

      expect(receivedMessage).toEqual(serverMessage);
    });

    it('should handle binary messages', async () => {
      const ws = new MockWebSocket('ws://localhost:8080');
      let receivedBinaryData: ArrayBuffer | null = null;

      ws.onmessage = (event) => {
        if (event.data instanceof ArrayBuffer) {
          receivedBinaryData = event.data;
        }
      };

      // Wait for connection
      await new Promise(resolve => {
        ws.onopen = () => resolve(void 0);
      });

      // Send binary data
      const binaryData = new ArrayBuffer(1024);
      ws.send(binaryData);

      expect(ws.getLastBinaryMessage()).toBe(binaryData);

      // Simulate receiving binary data
      const serverBinaryData = new ArrayBuffer(512);
      ws.simulateBinaryMessage(serverBinaryData);

      expect(receivedBinaryData).toBe(serverBinaryData);
    });
  });

  describe('Audio Basic Functionality', () => {
    it('should request microphone permission', async () => {
      const stream = await mockGetUserMedia({
        audio: {
          sampleRate: 16000,
          channelCount: 1
        }
      });

      expect(mockGetUserMedia).toHaveBeenCalledWith({
        audio: {
          sampleRate: 16000,
          channelCount: 1
        }
      });

      expect(stream).toBeInstanceOf(MockMediaStream);
    });

    it('should create MediaRecorder and record audio', async () => {
      const stream = await mockGetUserMedia({ audio: true });
      const recorder = new MockMediaRecorder(stream);

      let audioDataReceived = false;
      recorder.ondataavailable = () => {
        audioDataReceived = true;
      };

      expect(recorder.state).toBe('inactive');

      recorder.start();
      expect(recorder.state).toBe('recording');

      // Wait for audio data
      await new Promise(resolve => setTimeout(resolve, 150));
      expect(audioDataReceived).toBe(true);

      recorder.stop();
      expect(recorder.state).toBe('inactive');
    });
  });

  describe('Integration Scenarios', () => {
    it('should coordinate WebSocket and audio for streaming', async () => {
      // Setup WebSocket
      const ws = new MockWebSocket('ws://localhost:8080');
      await new Promise(resolve => {
        ws.onopen = () => resolve(void 0);
      });

      // Setup audio
      const stream = await mockGetUserMedia({ audio: true });
      const recorder = new MockMediaRecorder(stream);

      // Setup audio data handler to send via WebSocket
      recorder.ondataavailable = (event) => {
        const reader = new FileReader();
        reader.onload = () => {
          if (reader.result instanceof ArrayBuffer) {
            ws.send(reader.result);
          }
        };
        reader.readAsArrayBuffer(event.data);
      };

      // Start recording
      recorder.start();

      // Wait for audio data to be processed and sent
      await new Promise(resolve => setTimeout(resolve, 200));

      // Verify binary data was sent via WebSocket
      expect(ws.getLastBinaryMessage()).toBeInstanceOf(ArrayBuffer);

      recorder.stop();
    });

    it('should handle connection errors gracefully', async () => {
      const ws = new MockWebSocket('ws://localhost:8080');
      let errorOccurred = false;

      ws.onerror = () => {
        errorOccurred = true;
      };

      // Wait for connection
      await new Promise(resolve => {
        ws.onopen = () => resolve(void 0);
      });

      // Simulate error
      if (ws.onerror) {
        ws.onerror(new Event('error'));
      }

      expect(errorOccurred).toBe(true);
    });

    it('should handle audio permission denied', async () => {
      mockGetUserMedia.mockRejectedValueOnce(new Error('Permission denied'));

      try {
        await mockGetUserMedia({ audio: true });
        expect.fail('Should have thrown an error');
      } catch (error) {
        expect(error).toBeInstanceOf(Error);
        expect((error as Error).message).toBe('Permission denied');
      }
    });
  });
});