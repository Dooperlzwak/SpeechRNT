/**
 * useAudioPlayback Hook Tests
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { renderHook, act } from '@testing-library/react';
import { useAudioPlayback } from '../useAudioPlayback';
import { AudioPlaybackConfig } from '../../services/AudioPlaybackManager';

// Create mock manager instance
const mockManager = {
  initialize: vi.fn(),
  handleAudioStart: vi.fn(),
  handleAudioData: vi.fn(),
  handleAudioEnd: vi.fn(),
  playAudio: vi.fn(),
  stopPlayback: vi.fn(),
  setVolume: vi.fn(),
  getPlaybackState: vi.fn(),
  cleanup: vi.fn(),
};

// Mock AudioPlaybackManager
vi.mock('../../services/AudioPlaybackManager', () => {
  const MockAudioPlaybackManager = vi.fn(() => mockManager);
  MockAudioPlaybackManager.isSupported = vi.fn().mockReturnValue(true);

  return {
    AudioPlaybackManager: MockAudioPlaybackManager,
  };
});

// Import the mocked module to access it in tests
const { AudioPlaybackManager } = await import('../../services/AudioPlaybackManager');

describe('useAudioPlayback', () => {
  let config: AudioPlaybackConfig;
  let onPlaybackStart: ReturnType<typeof vi.fn>;
  let onPlaybackEnd: ReturnType<typeof vi.fn>;
  let onPlaybackError: ReturnType<typeof vi.fn>;
  let onVolumeChange: ReturnType<typeof vi.fn>;

  beforeEach(async () => {
    vi.clearAllMocks();
    
    config = {
      volume: 0.8,
      autoPlay: true,
      crossfade: false,
      bufferSize: 4096,
    };

    onPlaybackStart = vi.fn();
    onPlaybackEnd = vi.fn();
    onPlaybackError = vi.fn();
    onVolumeChange = vi.fn();

    mockManager.getPlaybackState.mockReturnValue({
      isPlaying: false,
      currentUtteranceId: null,
      volume: 0.8,
      queue: [],
    });

    // Reset mocks
    vi.clearAllMocks();
  });

  afterEach(() => {
    vi.resetAllMocks();
  });

  describe('initialization', () => {
    it('should initialize with correct default state', () => {
      const { result } = renderHook(() =>
        useAudioPlayback(config, onPlaybackStart, onPlaybackEnd, onPlaybackError, onVolumeChange)
      );

      expect(result.current.isSupported).toBe(true);
      expect(result.current.isInitialized).toBe(false);
      expect(result.current.error).toBe(null);
      expect(result.current.playbackState.isPlaying).toBe(false);
      expect(result.current.playbackState.volume).toBe(0.8);
    });

    it('should initialize manager successfully', async () => {
      const { result } = renderHook(() =>
        useAudioPlayback(config, onPlaybackStart, onPlaybackEnd, onPlaybackError, onVolumeChange)
      );

      await act(async () => {
        await result.current.initialize();
      });

      expect(mockManager.initialize).toHaveBeenCalled();
      expect(result.current.isInitialized).toBe(true);
      expect(result.current.error).toBe(null);
    });

    it('should handle initialization errors', async () => {
      const error = new Error('Initialization failed');
      mockManager.initialize.mockRejectedValueOnce(error);

      const { result } = renderHook(() =>
        useAudioPlayback(config, onPlaybackStart, onPlaybackEnd, onPlaybackError, onVolumeChange)
      );

      await act(async () => {
        await expect(result.current.initialize()).rejects.toThrow('Initialization failed');
      });

      expect(result.current.error).toEqual(error);
      expect(result.current.isInitialized).toBe(false);
    });

    it('should handle unsupported browsers', async () => {
      AudioPlaybackManager.isSupported.mockReturnValue(false);

      const { result } = renderHook(() =>
        useAudioPlayback(config, onPlaybackStart, onPlaybackEnd, onPlaybackError, onVolumeChange)
      );

      await act(async () => {
        await expect(result.current.initialize()).rejects.toThrow('Audio playback is not supported');
      });

      expect(result.current.error?.message).toBe('Audio playback is not supported in this browser');
    });
  });

  describe('audio handling', () => {
    let hook: ReturnType<typeof renderHook>;

    beforeEach(async () => {
      hook = renderHook(() =>
        useAudioPlayback(config, onPlaybackStart, onPlaybackEnd, onPlaybackError, onVolumeChange)
      );

      await act(async () => {
        await hook.result.current.initialize();
      });
    });

    it('should handle audio start', () => {
      act(() => {
        hook.result.current.handleAudioStart(1, 2.5, 'wav', 22050, 1);
      });

      expect(mockManager.handleAudioStart).toHaveBeenCalledWith(1, 2.5, 'wav', 22050, 1);
    });

    it('should handle audio data', () => {
      const audioData = new ArrayBuffer(1024);
      
      act(() => {
        hook.result.current.handleAudioData(1, audioData, 0, false);
      });

      expect(mockManager.handleAudioData).toHaveBeenCalledWith(1, audioData, 0, false);
    });

    it('should handle audio end', () => {
      act(() => {
        hook.result.current.handleAudioEnd(1);
      });

      expect(mockManager.handleAudioEnd).toHaveBeenCalledWith(1);
    });

    it('should handle methods when manager not initialized', () => {
      const consoleSpy = vi.spyOn(console, 'warn').mockImplementation(() => {});
      
      const uninitializedHook = renderHook(() =>
        useAudioPlayback(config, onPlaybackStart, onPlaybackEnd, onPlaybackError, onVolumeChange)
      );

      act(() => {
        uninitializedHook.result.current.handleAudioStart(1, 2.5);
      });

      expect(consoleSpy).toHaveBeenCalledWith('AudioPlaybackManager not initialized');
      consoleSpy.mockRestore();
    });
  });

  describe('playback control', () => {
    let hook: ReturnType<typeof renderHook>;

    beforeEach(async () => {
      hook = renderHook(() =>
        useAudioPlayback(config, onPlaybackStart, onPlaybackEnd, onPlaybackError, onVolumeChange)
      );

      await act(async () => {
        await hook.result.current.initialize();
      });
    });

    it('should play audio', async () => {
      await act(async () => {
        await hook.result.current.playAudio(1);
      });

      expect(mockManager.playAudio).toHaveBeenCalledWith(1);
    });

    it('should handle play audio errors', async () => {
      const error = new Error('Playback failed');
      mockManager.playAudio.mockRejectedValueOnce(error);

      await act(async () => {
        await expect(hook.result.current.playAudio(1)).rejects.toThrow('Playback failed');
      });

      expect(hook.result.current.error).toEqual(error);
    });

    it('should stop playback', () => {
      act(() => {
        hook.result.current.stopPlayback();
      });

      expect(mockManager.stopPlayback).toHaveBeenCalled();
    });

    it('should set volume', () => {
      act(() => {
        hook.result.current.setVolume(0.5);
      });

      expect(mockManager.setVolume).toHaveBeenCalledWith(0.5);
    });
  });

  describe('event handlers', () => {
    let hook: ReturnType<typeof renderHook>;
    let playbackHandler: any;

    beforeEach(async () => {
      hook = renderHook(() =>
        useAudioPlayback(config, onPlaybackStart, onPlaybackEnd, onPlaybackError, onVolumeChange)
      );

      await act(async () => {
        await hook.result.current.initialize();
      });

      // Get the handler passed to AudioPlaybackManager
      const { AudioPlaybackManager } = require('../../services/AudioPlaybackManager');
      playbackHandler = AudioPlaybackManager.mock.calls[0][1];
    });

    it('should handle playback start', () => {
      act(() => {
        playbackHandler.onPlaybackStart(1);
      });

      expect(onPlaybackStart).toHaveBeenCalledWith(1);
      expect(hook.result.current.playbackState.isPlaying).toBe(true);
      expect(hook.result.current.playbackState.currentUtteranceId).toBe(1);
    });

    it('should handle playback end', () => {
      act(() => {
        playbackHandler.onPlaybackEnd(1);
      });

      expect(onPlaybackEnd).toHaveBeenCalledWith(1);
      expect(hook.result.current.playbackState.isPlaying).toBe(false);
      expect(hook.result.current.playbackState.currentUtteranceId).toBe(null);
    });

    it('should handle playback error', () => {
      const error = new Error('Playback error');
      
      act(() => {
        playbackHandler.onPlaybackError(1, error);
      });

      expect(onPlaybackError).toHaveBeenCalledWith(1, error);
      expect(hook.result.current.error).toEqual(error);
      expect(hook.result.current.playbackState.isPlaying).toBe(false);
    });

    it('should handle volume change', () => {
      act(() => {
        playbackHandler.onVolumeChange(0.6);
      });

      expect(onVolumeChange).toHaveBeenCalledWith(0.6);
      expect(hook.result.current.playbackState.volume).toBe(0.6);
    });
  });

  describe('cleanup', () => {
    it('should cleanup on unmount', () => {
      const hook = renderHook(() =>
        useAudioPlayback(config, onPlaybackStart, onPlaybackEnd, onPlaybackError, onVolumeChange)
      );

      hook.unmount();

      expect(mockManager.cleanup).toHaveBeenCalled();
    });

    it('should cleanup manually', async () => {
      const hook = renderHook(() =>
        useAudioPlayback(config, onPlaybackStart, onPlaybackEnd, onPlaybackError, onVolumeChange)
      );

      await act(async () => {
        await hook.result.current.initialize();
      });

      act(() => {
        hook.result.current.cleanup();
      });

      expect(mockManager.cleanup).toHaveBeenCalled();
      expect(hook.result.current.isInitialized).toBe(false);
      expect(hook.result.current.error).toBe(null);
    });
  });

  describe('state updates', () => {
    let hook: ReturnType<typeof renderHook>;

    beforeEach(async () => {
      hook = renderHook(() =>
        useAudioPlayback(config, onPlaybackStart, onPlaybackEnd, onPlaybackError, onVolumeChange)
      );

      await act(async () => {
        await hook.result.current.initialize();
      });
    });

    it('should update queue state when handling audio', () => {
      const mockQueue = [
        {
          utteranceId: 1,
          audioData: [new ArrayBuffer(1024)],
          format: 'wav',
          sampleRate: 22050,
          channels: 1,
          duration: 2.5,
          isComplete: false,
        },
      ];

      mockManager.getPlaybackState.mockReturnValue({
        isPlaying: false,
        currentUtteranceId: null,
        volume: 0.8,
        queue: mockQueue,
      });

      act(() => {
        hook.result.current.handleAudioStart(1, 2.5);
      });

      expect(hook.result.current.playbackState.queue).toEqual(mockQueue);
    });
  });
});