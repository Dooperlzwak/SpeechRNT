/**
 * Audio Playback Integration Tests
 * Tests the integration between WebSocket messages and audio playback
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { renderHook, act } from '@testing-library/react';
import { useWebSocket } from '../hooks/useWebSocket';
import { useAudioPlayback } from '../hooks/useAudioPlayback';
import { AudioStartMessage, AudioDataMessage, AudioEndMessage } from '../types/messageProtocol';

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
    setTimeout(() => {
      this.readyState = MockWebSocket.OPEN;
      this.onopen?.(new Event('open'));
    }, 0);
  }

  send = vi.fn();
  close = vi.fn(() => {
    this.readyState = MockWebSocket.CLOSED;
    this.onclose?.(new CloseEvent('close', { code: 1000, reason: 'Normal closure' }));
  });

  // Helper methods for testing
  simulateMessage(data: any) {
    if (typeof data === 'string') {
      this.onmessage?.(new MessageEvent('message', { data }));
    } else {
      this.onmessage?.(new MessageEvent('message', { data }));
    }
  }

  simulateError() {
    this.onerror?.(new Event('error'));
  }
}

// Mock Audio API
const mockAudioContext = {
  createBuffer: vi.fn(),
  createBufferSource: vi.fn(),
  createGain: vi.fn(),
  decodeAudioData: vi.fn(),
  resume: vi.fn(),
  close: vi.fn(),
  state: 'running',
  destination: {},
  addEventListener: vi.fn(),
};

const mockGainNode = {
  connect: vi.fn(),
  gain: { value: 1 },
};

const mockBufferSource = {
  buffer: null,
  connect: vi.fn(),
  start: vi.fn(),
  stop: vi.fn(),
  onended: null,
};

const mockAudioBuffer = {
  duration: 2.5,
  sampleRate: 22050,
  numberOfChannels: 1,
};

Object.defineProperty(global, 'WebSocket', {
  writable: true,
  value: MockWebSocket,
});

Object.defineProperty(global, 'AudioContext', {
  writable: true,
  value: vi.fn(() => mockAudioContext),
});

describe('Audio Playback Integration', () => {
  let mockWs: MockWebSocket;

  beforeEach(() => {
    vi.clearAllMocks();
    
    // Setup audio mocks
    mockAudioContext.createGain.mockReturnValue(mockGainNode);
    mockAudioContext.createBufferSource.mockReturnValue(mockBufferSource);
    mockAudioContext.createBuffer.mockReturnValue(mockAudioBuffer);
    mockAudioContext.decodeAudioData.mockResolvedValue(mockAudioBuffer);

    // Mock WebSocket constructor to capture instance
    const OriginalMockWebSocket = MockWebSocket;
    (global as any).WebSocket = class extends OriginalMockWebSocket {
      constructor(url: string) {
        super(url);
        mockWs = this;
      }
    };
  });

  afterEach(() => {
    vi.resetAllMocks();
  });

  describe('WebSocket to Audio Playback Flow', () => {
    it('should handle complete audio playback flow', async () => {
      const messages: any[] = [];
      const playbackEvents: string[] = [];

      // Setup WebSocket hook
      const wsHook = renderHook(() =>
        useWebSocket(
          {
            url: 'ws://localhost:8080',
            reconnectInterval: 1000,
            maxReconnectAttempts: 3,
            heartbeatInterval: 30000,
          },
          (message) => {
            messages.push(message);
          },
          (data, messageType) => {
            console.log('Binary data received:', data.byteLength, 'bytes, type:', messageType);
          }
        )
      );

      // Setup audio playback hook
      const audioHook = renderHook(() =>
        useAudioPlayback(
          {
            volume: 0.8,
            autoPlay: true,
            crossfade: false,
            bufferSize: 4096,
          },
          (utteranceId) => playbackEvents.push(`start:${utteranceId}`),
          (utteranceId) => playbackEvents.push(`end:${utteranceId}`),
          (utteranceId, error) => playbackEvents.push(`error:${utteranceId}:${error.message}`),
          (volume) => playbackEvents.push(`volume:${volume}`)
        )
      );

      // Connect WebSocket
      act(() => {
        wsHook.result.current.connect();
      });

      // Wait for connection
      await act(async () => {
        await new Promise(resolve => setTimeout(resolve, 10));
      });

      expect(wsHook.result.current.isConnected).toBe(true);

      // Initialize audio playback
      await act(async () => {
        await audioHook.result.current.initialize();
      });

      expect(audioHook.result.current.isInitialized).toBe(true);

      // Simulate audio start message
      const audioStartMsg: AudioStartMessage = {
        type: 'audio_start',
        data: {
          utteranceId: 1,
          duration: 2.5,
          format: 'wav',
          sampleRate: 22050,
          channels: 1,
        },
      };

      act(() => {
        mockWs.simulateMessage(JSON.stringify(audioStartMsg));
      });

      expect(messages).toHaveLength(1);
      expect(messages[0]).toEqual(audioStartMsg);

      // Handle audio start in playback manager
      act(() => {
        audioHook.result.current.handleAudioStart(
          audioStartMsg.data.utteranceId,
          audioStartMsg.data.duration,
          audioStartMsg.data.format,
          audioStartMsg.data.sampleRate,
          audioStartMsg.data.channels
        );
      });

      // Simulate audio data messages and binary data
      const audioDataMsg: AudioDataMessage = {
        type: 'audio_data',
        data: {
          utteranceId: 1,
          sequenceNumber: 0,
          isLast: false,
        },
      };

      act(() => {
        mockWs.simulateMessage(JSON.stringify(audioDataMsg));
      });

      // Simulate binary audio data
      const audioData = new ArrayBuffer(1024);
      act(() => {
        audioHook.result.current.handleAudioData(1, audioData, 0, false);
      });

      // Simulate final audio data chunk
      const finalAudioDataMsg: AudioDataMessage = {
        type: 'audio_data',
        data: {
          utteranceId: 1,
          sequenceNumber: 1,
          isLast: true,
        },
      };

      act(() => {
        mockWs.simulateMessage(JSON.stringify(finalAudioDataMsg));
      });

      const finalAudioData = new ArrayBuffer(512);
      act(() => {
        audioHook.result.current.handleAudioData(1, finalAudioData, 1, true);
      });

      // Simulate audio end message
      const audioEndMsg: AudioEndMessage = {
        type: 'audio_end',
        data: {
          utteranceId: 1,
        },
      };

      act(() => {
        mockWs.simulateMessage(JSON.stringify(audioEndMsg));
      });

      act(() => {
        audioHook.result.current.handleAudioEnd(1);
      });

      // Verify audio playback state
      expect(audioHook.result.current.playbackState.queue).toHaveLength(1);
      expect(audioHook.result.current.playbackState.queue[0].isComplete).toBe(true);
      expect(audioHook.result.current.playbackState.queue[0].audioData).toHaveLength(2);

      // Trigger manual playback (since auto-play might not work in test environment)
      await act(async () => {
        await audioHook.result.current.playAudio(1);
      });

      expect(playbackEvents).toContain('start:1');
      expect(mockBufferSource.start).toHaveBeenCalled();
    });

    it('should handle multiple concurrent audio streams', async () => {
      const audioHook = renderHook(() =>
        useAudioPlayback(
          {
            volume: 0.8,
            autoPlay: false, // Disable auto-play for manual control
            crossfade: false,
            bufferSize: 4096,
          }
        )
      );

      await act(async () => {
        await audioHook.result.current.initialize();
      });

      // Start multiple audio streams
      act(() => {
        audioHook.result.current.handleAudioStart(1, 2.0);
        audioHook.result.current.handleAudioStart(2, 3.0);
        audioHook.result.current.handleAudioStart(3, 1.5);
      });

      // Add data to each stream
      act(() => {
        audioHook.result.current.handleAudioData(1, new ArrayBuffer(1024), 0, true);
        audioHook.result.current.handleAudioData(2, new ArrayBuffer(2048), 0, false);
        audioHook.result.current.handleAudioData(3, new ArrayBuffer(512), 0, true);
      });

      // Complete stream 2
      act(() => {
        audioHook.result.current.handleAudioData(2, new ArrayBuffer(1024), 1, true);
      });

      const state = audioHook.result.current.playbackState;
      expect(state.queue).toHaveLength(3);
      
      // Check that streams 1 and 3 are complete, stream 2 is complete
      const completeStreams = state.queue.filter(q => q.isComplete);
      expect(completeStreams).toHaveLength(3);
    });

    it('should handle audio playback errors gracefully', async () => {
      const playbackErrors: Array<{ utteranceId: number; error: Error }> = [];

      const audioHook = renderHook(() =>
        useAudioPlayback(
          {
            volume: 0.8,
            autoPlay: true,
            crossfade: false,
            bufferSize: 4096,
          },
          undefined,
          undefined,
          (utteranceId, error) => playbackErrors.push({ utteranceId, error })
        )
      );

      await act(async () => {
        await audioHook.result.current.initialize();
      });

      // Setup audio that will fail to decode
      mockAudioContext.decodeAudioData.mockRejectedValueOnce(new Error('Decode failed'));

      act(() => {
        audioHook.result.current.handleAudioStart(1, 2.5);
        audioHook.result.current.handleAudioData(1, new ArrayBuffer(1024), 0, true);
      });

      // Try to play the audio
      await act(async () => {
        try {
          await audioHook.result.current.playAudio(1);
        } catch (error) {
          // Expected to fail
        }
      });

      expect(playbackErrors).toHaveLength(1);
      expect(playbackErrors[0].utteranceId).toBe(1);
      expect(playbackErrors[0].error.message).toContain('Failed to play audio for utterance 1');
    });

    it('should handle volume changes during playback', async () => {
      const volumeChanges: number[] = [];

      const audioHook = renderHook(() =>
        useAudioPlayback(
          {
            volume: 0.5,
            autoPlay: true,
            crossfade: false,
            bufferSize: 4096,
          },
          undefined,
          undefined,
          undefined,
          (volume) => volumeChanges.push(volume)
        )
      );

      await act(async () => {
        await audioHook.result.current.initialize();
      });

      // Change volume multiple times
      act(() => {
        audioHook.result.current.setVolume(0.8);
      });

      act(() => {
        audioHook.result.current.setVolume(0.3);
      });

      act(() => {
        audioHook.result.current.setVolume(1.0);
      });

      expect(volumeChanges).toEqual([0.8, 0.3, 1.0]);
      expect(mockGainNode.gain.value).toBe(1.0);
    });

    it('should stop playback when requested', async () => {
      const playbackEvents: string[] = [];

      const audioHook = renderHook(() =>
        useAudioPlayback(
          {
            volume: 0.8,
            autoPlay: true,
            crossfade: false,
            bufferSize: 4096,
          },
          (utteranceId) => playbackEvents.push(`start:${utteranceId}`),
          (utteranceId) => playbackEvents.push(`end:${utteranceId}`)
        )
      );

      await act(async () => {
        await audioHook.result.current.initialize();
      });

      // Setup and start audio
      act(() => {
        audioHook.result.current.handleAudioStart(1, 2.5);
        audioHook.result.current.handleAudioData(1, new ArrayBuffer(1024), 0, true);
      });

      await act(async () => {
        await audioHook.result.current.playAudio(1);
      });

      expect(playbackEvents).toContain('start:1');
      expect(audioHook.result.current.playbackState.isPlaying).toBe(true);

      // Stop playback
      act(() => {
        audioHook.result.current.stopPlayback();
      });

      expect(mockBufferSource.stop).toHaveBeenCalled();
      expect(audioHook.result.current.playbackState.isPlaying).toBe(false);
    });
  });

  describe('Browser Compatibility', () => {
    it('should handle missing AudioContext gracefully', () => {
      const originalAudioContext = global.AudioContext;
      const originalWebkitAudioContext = (global as any).webkitAudioContext;
      
      (global as any).AudioContext = undefined;
      (global as any).webkitAudioContext = undefined;

      const audioHook = renderHook(() =>
        useAudioPlayback({
          volume: 0.8,
          autoPlay: true,
          crossfade: false,
          bufferSize: 4096,
        })
      );

      expect(audioHook.result.current.isSupported).toBe(false);

      // Restore
      global.AudioContext = originalAudioContext;
      (global as any).webkitAudioContext = originalWebkitAudioContext;
    });

    it('should handle webkit AudioContext', async () => {
      const originalAudioContext = global.AudioContext;
      (global as any).AudioContext = undefined;
      
      (global as any).webkitAudioContext = vi.fn(() => mockAudioContext);

      const audioHook = renderHook(() =>
        useAudioPlayback({
          volume: 0.8,
          autoPlay: true,
          crossfade: false,
          bufferSize: 4096,
        })
      );

      expect(audioHook.result.current.isSupported).toBe(true);

      await act(async () => {
        await audioHook.result.current.initialize();
      });

      expect(audioHook.result.current.isInitialized).toBe(true);

      // Restore
      global.AudioContext = originalAudioContext;
      (global as any).webkitAudioContext = undefined;
    });
  });
});