import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { render, screen, fireEvent, waitFor, act } from '@testing-library/react';
import { userEvent } from '@testing-library/user-event';
import App from '../App';
import { WebSocketManager } from '../services/WebSocketManager';
import { AudioManager } from '../services/AudioManager';

// Mock WebSocket
class MockWebSocket {
  static CONNECTING = 0;
  static OPEN = 1;
  static CLOSING = 2;
  static CLOSED = 3;

  readyState = MockWebSocket.CONNECTING;
  onopen: ((event: Event) => void) | null = null;
  onclose: ((event: CloseEvent) => void) | null = null;
  onmessage: ((event: MessageEvent) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;

  constructor(public url: string) {
    // Simulate connection opening
    setTimeout(() => {
      this.readyState = MockWebSocket.OPEN;
      this.onopen?.(new Event('open'));
    }, 10);
  }

  send(data: string | ArrayBuffer) {
    // Mock sending data
    console.log('Mock WebSocket send:', data);
  }

  close(code?: number, reason?: string) {
    this.readyState = MockWebSocket.CLOSED;
    this.onclose?.(new CloseEvent('close', { code, reason }));
  }

  // Helper methods for testing
  simulateMessage(data: any) {
    if (this.onmessage) {
      this.onmessage(new MessageEvent('message', { data: JSON.stringify(data) }));
    }
  }

  simulateBinaryMessage(data: ArrayBuffer) {
    if (this.onmessage) {
      this.onmessage(new MessageEvent('message', { data }));
    }
  }

  simulateError() {
    this.onerror?.(new Event('error'));
  }

  simulateClose(code = 1000, reason = 'Normal closure') {
    this.readyState = MockWebSocket.CLOSED;
    this.onclose?.(new CloseEvent('close', { code, reason }));
  }
}

// Mock AudioContext and MediaRecorder
const mockAudioContext = {
  createGain: vi.fn(() => ({
    connect: vi.fn(),
    disconnect: vi.fn(),
    gain: { value: 1 }
  })),
  createBufferSource: vi.fn(() => ({
    connect: vi.fn(),
    disconnect: vi.fn(),
    start: vi.fn(),
    stop: vi.fn(),
    buffer: null,
    onended: null
  })),
  createBuffer: vi.fn(() => ({
    getChannelData: vi.fn(() => new Float32Array(1024)),
    numberOfChannels: 1,
    sampleRate: 22050,
    length: 1024,
    duration: 1024 / 22050
  })),
  decodeAudioData: vi.fn((buffer) => Promise.resolve({
    getChannelData: vi.fn(() => new Float32Array(1024)),
    numberOfChannels: 1,
    sampleRate: 22050,
    length: 1024,
    duration: 1024 / 22050
  })),
  close: vi.fn(() => Promise.resolve()),
  resume: vi.fn(() => Promise.resolve()),
  suspend: vi.fn(() => Promise.resolve()),
  state: 'running',
  sampleRate: 44100,
  destination: {
    connect: vi.fn(),
    disconnect: vi.fn()
  }
};

const mockMediaRecorder = {
  start: vi.fn(),
  stop: vi.fn(),
  pause: vi.fn(),
  resume: vi.fn(),
  requestData: vi.fn(),
  state: 'inactive',
  ondataavailable: null,
  onstart: null,
  onstop: null,
  onerror: null,
  onpause: null,
  onresume: null
};

describe('Conversation Flow Integration Tests', () => {
  let mockWebSocket: MockWebSocket;
  let user: ReturnType<typeof userEvent.setup>;

  beforeEach(() => {
    user = userEvent.setup();
    
    // Setup global mocks
    (global as any).WebSocket = vi.fn((url: string) => {
      mockWebSocket = new MockWebSocket(url);
      return mockWebSocket;
    });
    
    (global as any).AudioContext = vi.fn(() => mockAudioContext);
    (global as any).webkitAudioContext = vi.fn(() => mockAudioContext);
    (global as any).MediaRecorder = vi.fn(() => mockMediaRecorder);
    (global as any).MediaRecorder.isTypeSupported = vi.fn(() => true);

    // Mock getUserMedia
    Object.defineProperty(global.navigator, 'mediaDevices', {
      value: {
        getUserMedia: vi.fn(() => Promise.resolve({
          getTracks: vi.fn(() => []),
          getAudioTracks: vi.fn(() => []),
          getVideoTracks: vi.fn(() => [])
        })),
        enumerateDevices: vi.fn(() => Promise.resolve([]))
      },
      writable: true
    });

    // Mock localStorage
    const localStorageMock = {
      getItem: vi.fn(),
      setItem: vi.fn(),
      removeItem: vi.fn(),
      clear: vi.fn()
    };
    Object.defineProperty(global, 'localStorage', {
      value: localStorageMock,
      writable: true
    });
  });

  afterEach(() => {
    vi.clearAllMocks();
  });

  describe('Complete Conversation Flow', () => {
    it('should handle complete conversation from start to finish', async () => {
      render(<App />);

      // Wait for initial connection
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      // Start conversation
      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Should show listening state
      await waitFor(() => {
        expect(screen.getByText(/listening/i)).toBeInTheDocument();
      });

      // Simulate receiving live transcription
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'transcription_update',
          data: {
            text: 'Hello',
            utteranceId: 1,
            confidence: 0.95
          }
        });
      });

      // Should display live transcription
      await waitFor(() => {
        expect(screen.getByText('Hello')).toBeInTheDocument();
      });

      // Simulate more transcription updates
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'transcription_update',
          data: {
            text: 'Hello world',
            utteranceId: 1,
            confidence: 0.97
          }
        });
      });

      await waitFor(() => {
        expect(screen.getByText('Hello world')).toBeInTheDocument();
      });

      // Simulate status change to thinking
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'status_update',
          data: {
            state: 'thinking',
            utteranceId: 1
          }
        });
      });

      await waitFor(() => {
        expect(screen.getByText(/processing/i)).toBeInTheDocument();
      });

      // Simulate translation result
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'translation_result',
          data: {
            originalText: 'Hello world',
            translatedText: 'Hola mundo',
            utteranceId: 1
          }
        });
      });

      await waitFor(() => {
        expect(screen.getByText('Hola mundo')).toBeInTheDocument();
      });

      // Simulate audio start
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'audio_start',
          data: {
            utteranceId: 1,
            duration: 2.5
          }
        });
      });

      // Simulate status change to speaking
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'status_update',
          data: {
            state: 'speaking',
            utteranceId: 1
          }
        });
      });

      await waitFor(() => {
        expect(screen.getByText(/playing audio/i)).toBeInTheDocument();
      });

      // Simulate binary audio data
      const audioData = new ArrayBuffer(1024);
      act(() => {
        mockWebSocket.simulateBinaryMessage(audioData);
      });

      // Should return to listening state after audio
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'status_update',
          data: {
            state: 'idle',
            utteranceId: 1
          }
        });
      });

      await waitFor(() => {
        expect(screen.getByText(/waiting for speech/i)).toBeInTheDocument();
      });

      // Stop conversation
      const stopButton = screen.getByRole('button', { name: /stop conversation/i });
      await user.click(stopButton);

      await waitFor(() => {
        expect(screen.getByText(/waiting/i)).toBeInTheDocument();
      });
    });

    it('should handle multiple utterances in sequence', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // First utterance
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'transcription_update',
          data: { text: 'First sentence', utteranceId: 1, confidence: 0.95 }
        });
      });

      act(() => {
        mockWebSocket.simulateMessage({
          type: 'translation_result',
          data: { originalText: 'First sentence', translatedText: 'Primera oración', utteranceId: 1 }
        });
      });

      await waitFor(() => {
        expect(screen.getByText('First sentence')).toBeInTheDocument();
        expect(screen.getByText('Primera oración')).toBeInTheDocument();
      });

      // Second utterance
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'transcription_update',
          data: { text: 'Second sentence', utteranceId: 2, confidence: 0.92 }
        });
      });

      act(() => {
        mockWebSocket.simulateMessage({
          type: 'translation_result',
          data: { originalText: 'Second sentence', translatedText: 'Segunda oración', utteranceId: 2 }
        });
      });

      await waitFor(() => {
        expect(screen.getByText('Second sentence')).toBeInTheDocument();
        expect(screen.getByText('Segunda oración')).toBeInTheDocument();
      });

      // Should show both utterances (implementation dependent)
      expect(screen.getByText('First sentence')).toBeInTheDocument();
      expect(screen.getByText('Segunda oración')).toBeInTheDocument();
    });

    it('should handle connection loss and recovery', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      // Start conversation
      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Simulate connection loss
      act(() => {
        mockWebSocket.simulateClose(1006, 'Connection lost');
      });

      await waitFor(() => {
        expect(screen.getByText(/reconnecting/i)).toBeInTheDocument();
      });

      // Session should be stopped due to connection loss
      await waitFor(() => {
        expect(screen.getByRole('button', { name: /start conversation/i })).toBeInTheDocument();
      });

      // Simulate reconnection
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      }, { timeout: 5000 });

      // Should be able to start conversation again
      const newStartButton = screen.getByRole('button', { name: /start conversation/i });
      expect(newStartButton).not.toBeDisabled();
    });

    it('should handle errors gracefully', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Simulate transcription error
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'error',
          data: {
            message: 'Transcription failed',
            code: 'STT_ERROR',
            utteranceId: 1
          }
        });
      });

      // Should display error but continue session
      await waitFor(() => {
        expect(screen.getByText(/error/i)).toBeInTheDocument();
      });

      // Should still be in active session
      expect(screen.getByRole('button', { name: /stop conversation/i })).toBeInTheDocument();

      // Should recover and continue processing new utterances
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'transcription_update',
          data: { text: 'Recovery test', utteranceId: 2, confidence: 0.90 }
        });
      });

      await waitFor(() => {
        expect(screen.getByText('Recovery test')).toBeInTheDocument();
      });
    });

    it('should handle language switching during conversation', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      // Open settings and change language
      const settingsButton = screen.getByRole('button', { name: /open settings/i });
      await user.click(settingsButton);

      // Change target language (assuming French is available)
      const targetLanguageSelect = screen.getByLabelText(/target language/i);
      await user.selectOptions(targetLanguageSelect, 'French');

      // Close settings
      const closeButton = screen.getByRole('button', { name: /close/i });
      await user.click(closeButton);

      // Start conversation with new language setting
      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Should show updated language in UI
      await waitFor(() => {
        expect(screen.getByText(/french/i)).toBeInTheDocument();
      });

      // Simulate translation in new language
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'transcription_update',
          data: { text: 'Hello', utteranceId: 1, confidence: 0.95 }
        });
      });

      act(() => {
        mockWebSocket.simulateMessage({
          type: 'translation_result',
          data: { originalText: 'Hello', translatedText: 'Bonjour', utteranceId: 1 }
        });
      });

      await waitFor(() => {
        expect(screen.getByText('Hello')).toBeInTheDocument();
        expect(screen.getByText('Bonjour')).toBeInTheDocument();
      });
    });

    it('should handle audio playback and controls', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Simulate complete translation flow
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'transcription_update',
          data: { text: 'Test audio', utteranceId: 1, confidence: 0.95 }
        });
      });

      act(() => {
        mockWebSocket.simulateMessage({
          type: 'translation_result',
          data: { originalText: 'Test audio', translatedText: 'Audio de prueba', utteranceId: 1 }
        });
      });

      // Simulate audio start
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'audio_start',
          data: { utteranceId: 1, duration: 2.0 }
        });
      });

      // Should show speaking state
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'status_update',
          data: { state: 'speaking', utteranceId: 1 }
        });
      });

      await waitFor(() => {
        expect(screen.getByText(/playing audio/i)).toBeInTheDocument();
      });

      // Simulate audio data
      const audioBuffer = new ArrayBuffer(4096);
      act(() => {
        mockWebSocket.simulateBinaryMessage(audioBuffer);
      });

      // Audio should be processed (implementation dependent)
      expect(mockAudioContext.decodeAudioData).toHaveBeenCalled();
    });

    it('should measure and display latency information', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      const startTime = Date.now();

      // Simulate transcription with delay
      setTimeout(() => {
        act(() => {
          mockWebSocket.simulateMessage({
            type: 'transcription_update',
            data: { text: 'Latency test', utteranceId: 1, confidence: 0.95 }
          });
        });
      }, 200);

      // Simulate translation with additional delay
      setTimeout(() => {
        act(() => {
          mockWebSocket.simulateMessage({
            type: 'translation_result',
            data: { originalText: 'Latency test', translatedText: 'Prueba de latencia', utteranceId: 1 }
          });
        });
      }, 400);

      // Simulate audio with final delay
      setTimeout(() => {
        act(() => {
          mockWebSocket.simulateMessage({
            type: 'audio_start',
            data: { utteranceId: 1, duration: 1.5 }
          });
        });
        
        const audioBuffer = new ArrayBuffer(2048);
        act(() => {
          mockWebSocket.simulateBinaryMessage(audioBuffer);
        });
      }, 600);

      // Wait for all processing to complete
      await waitFor(() => {
        expect(screen.getByText('Latency test')).toBeInTheDocument();
        expect(screen.getByText('Prueba de latencia')).toBeInTheDocument();
      }, { timeout: 1000 });

      const endTime = Date.now();
      const totalLatency = endTime - startTime;

      // Should complete within reasonable time
      expect(totalLatency).toBeLessThan(1000);
    });
  });

  describe('Error Scenarios', () => {
    it('should handle microphone permission denied', async () => {
      // Mock permission denied
      Object.defineProperty(global.navigator, 'mediaDevices', {
        value: {
          getUserMedia: vi.fn(() => Promise.reject(new Error('Permission denied'))),
          enumerateDevices: vi.fn(() => Promise.resolve([]))
        },
        writable: true
      });

      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Should show permission error
      await waitFor(() => {
        expect(screen.getByText(/permission/i)).toBeInTheDocument();
      });

      // Session should not be active
      expect(screen.getByRole('button', { name: /start conversation/i })).toBeInTheDocument();
    });

    it('should handle WebSocket connection failure', async () => {
      // Mock WebSocket that fails to connect
      (global as any).WebSocket = vi.fn(() => {
        const ws = new MockWebSocket('ws://localhost:8080');
        setTimeout(() => ws.simulateError(), 10);
        return ws;
      });

      render(<App />);

      // Should show disconnected state
      await waitFor(() => {
        expect(screen.getByText(/disconnected/i)).toBeInTheDocument();
      });

      // Start button should be disabled
      const startButton = screen.getByRole('button', { name: /start conversation/i });
      expect(startButton).toBeDisabled();
    });

    it('should handle audio processing errors', async () => {
      // Mock MediaRecorder that throws error
      const mockMediaRecorderWithError = {
        ...mockMediaRecorder,
        start: vi.fn(() => {
          setTimeout(() => {
            if (mockMediaRecorderWithError.onerror) {
              mockMediaRecorderWithError.onerror(new Event('error'));
            }
          }, 100);
        })
      };

      (global as any).MediaRecorder = vi.fn(() => mockMediaRecorderWithError);

      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Should handle audio error gracefully
      await waitFor(() => {
        expect(screen.getByText(/error/i)).toBeInTheDocument();
      });
    });
  });

  describe('Performance Tests', () => {
    it('should handle rapid message updates without performance degradation', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      const startTime = performance.now();

      // Send rapid transcription updates
      for (let i = 0; i < 100; i++) {
        act(() => {
          mockWebSocket.simulateMessage({
            type: 'transcription_update',
            data: {
              text: `Rapid update ${i}`,
              utteranceId: 1,
              confidence: 0.95
            }
          });
        });
      }

      const endTime = performance.now();
      const processingTime = endTime - startTime;

      // Should process updates quickly
      expect(processingTime).toBeLessThan(1000);

      // Should show final update
      await waitFor(() => {
        expect(screen.getByText('Rapid update 99')).toBeInTheDocument();
      });
    });

    it('should handle large audio data efficiently', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Simulate large audio buffer
      const largeAudioBuffer = new ArrayBuffer(1024 * 1024); // 1MB
      const startTime = performance.now();

      act(() => {
        mockWebSocket.simulateBinaryMessage(largeAudioBuffer);
      });

      const endTime = performance.now();
      const processingTime = endTime - startTime;

      // Should handle large audio data efficiently
      expect(processingTime).toBeLessThan(500);
    });
  });
});