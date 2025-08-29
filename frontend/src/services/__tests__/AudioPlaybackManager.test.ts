/**
 * AudioPlaybackManager Tests
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { AudioPlaybackManager, AudioPlaybackConfig, PlaybackHandler } from '../AudioPlaybackManager';

// Mock Web Audio API components
const mockGainNode = {
  connect: vi.fn(),
  disconnect: vi.fn(),
  gain: { value: 1 },
};

const mockBufferSource = {
  buffer: null,
  connect: vi.fn(),
  disconnect: vi.fn(),
  start: vi.fn(),
  stop: vi.fn(),
  onended: null,
};

const mockAudioBuffer = {
  duration: 2.5,
  sampleRate: 22050,
  numberOfChannels: 1,
  getChannelData: vi.fn(() => new Float32Array(1024)),
};

const mockAudioContext = {
  createBuffer: vi.fn(() => mockAudioBuffer),
  createBufferSource: vi.fn(() => mockBufferSource),
  createGain: vi.fn(() => mockGainNode),
  decodeAudioData: vi.fn((buffer) => Promise.resolve(mockAudioBuffer)),
  resume: vi.fn(() => Promise.resolve()),
  close: vi.fn(() => Promise.resolve()),
  state: 'running',
  sampleRate: 44100,
  destination: {
    connect: vi.fn(),
    disconnect: vi.fn(),
  },
  addEventListener: vi.fn(),
};

// Mock global AudioContext
(global as any).AudioContext = vi.fn(() => mockAudioContext);
(global as any).webkitAudioContext = vi.fn(() => mockAudioContext);

describe('AudioPlaybackManager', () => {
  let manager: AudioPlaybackManager;
  let config: AudioPlaybackConfig;
  let handler: PlaybackHandler;

  beforeEach(() => {
    // Reset mocks
    vi.clearAllMocks();
    
    // Setup mock returns
    mockAudioContext.createGain.mockReturnValue(mockGainNode);
    mockAudioContext.createBufferSource.mockReturnValue(mockBufferSource);
    mockAudioContext.createBuffer.mockReturnValue(mockAudioBuffer);
    mockAudioContext.decodeAudioData.mockResolvedValue(mockAudioBuffer);

    config = {
      volume: 0.8,
      autoPlay: true,
      crossfade: false,
      bufferSize: 4096,
    };

    handler = {
      onPlaybackStart: vi.fn(),
      onPlaybackEnd: vi.fn(),
      onPlaybackError: vi.fn(),
      onVolumeChange: vi.fn(),
    };

    manager = new AudioPlaybackManager(config, handler);
  });

  afterEach(() => {
    manager.cleanup();
  });

  describe('initialization', () => {
    it('should initialize successfully', async () => {
      await manager.initialize();
      
      expect(mockAudioContext.createGain).toHaveBeenCalled();
      expect(mockGainNode.connect).toHaveBeenCalledWith(mockAudioContext.destination);
      expect(mockGainNode.gain.value).toBe(0.8);
    });

    it('should handle initialization errors', async () => {
      mockAudioContext.createGain.mockImplementation(() => {
        throw new Error('Audio context error');
      });

      await expect(manager.initialize()).rejects.toThrow('Failed to initialize audio playback');
    });
  });

  describe('audio start handling', () => {
    beforeEach(async () => {
      await manager.initialize();
    });

    it('should handle audio start message', () => {
      const utteranceId = 1;
      const duration = 2.5;
      const format = 'wav';
      const sampleRate = 22050;
      const channels = 1;

      manager.handleAudioStart(utteranceId, duration, format, sampleRate, channels);

      const state = manager.getPlaybackState();
      expect(state.queue).toHaveLength(1);
      expect(state.queue[0].utteranceId).toBe(utteranceId);
      expect(state.queue[0].duration).toBe(duration);
      expect(state.queue[0].format).toBe(format);
      expect(state.queue[0].sampleRate).toBe(sampleRate);
      expect(state.queue[0].channels).toBe(channels);
    });

    it('should add to playback queue when autoPlay is enabled', () => {
      manager.handleAudioStart(1, 2.5);
      
      const state = manager.getPlaybackState();
      expect(state.queue).toHaveLength(1);
    });
  });

  describe('audio data handling', () => {
    beforeEach(async () => {
      await manager.initialize();
      manager.handleAudioStart(1, 2.5);
    });

    it('should handle audio data chunks', () => {
      const audioData = new ArrayBuffer(1024);
      
      manager.handleAudioData(1, audioData, 0, false);
      
      const state = manager.getPlaybackState();
      expect(state.queue[0].audioData).toHaveLength(1);
      expect(state.queue[0].isComplete).toBe(false);
    });

    it('should mark audio as complete when isLast is true', () => {
      const audioData = new ArrayBuffer(1024);
      
      manager.handleAudioData(1, audioData, 0, true);
      
      const state = manager.getPlaybackState();
      expect(state.queue[0].isComplete).toBe(true);
    });

    it('should handle unknown utterance ID', () => {
      const consoleSpy = vi.spyOn(console, 'warn').mockImplementation(() => {});
      const audioData = new ArrayBuffer(1024);
      
      manager.handleAudioData(999, audioData, 0, false);
      
      expect(consoleSpy).toHaveBeenCalledWith('Received audio data for unknown utterance 999');
      consoleSpy.mockRestore();
    });
  });

  describe('audio playback', () => {
    beforeEach(async () => {
      await manager.initialize();
      manager.handleAudioStart(1, 2.5);
      manager.handleAudioData(1, new ArrayBuffer(1024), 0, true);
    });

    it('should play audio successfully', async () => {
      await manager.playAudio(1);

      expect(mockAudioContext.createBufferSource).toHaveBeenCalled();
      expect(mockBufferSource.connect).toHaveBeenCalledWith(mockGainNode);
      expect(mockBufferSource.start).toHaveBeenCalledWith(0);
      expect(handler.onPlaybackStart).toHaveBeenCalledWith(1);
      
      const state = manager.getPlaybackState();
      expect(state.isPlaying).toBe(true);
      expect(state.currentUtteranceId).toBe(1);
    });

    it('should handle playback errors', async () => {
      mockBufferSource.start.mockImplementation(() => {
        throw new Error('Playback error');
      });

      await expect(manager.playAudio(1)).rejects.toThrow('Failed to play audio for utterance 1');
      expect(handler.onPlaybackError).toHaveBeenCalled();
    });

    it('should not play incomplete audio', async () => {
      manager.handleAudioStart(2, 2.5);
      // Don't mark as complete
      
      await expect(manager.playAudio(2)).rejects.toThrow('Audio not ready for utterance 2');
    });

    it('should resume suspended audio context', async () => {
      mockAudioContext.state = 'suspended';
      
      await manager.playAudio(1);
      
      expect(mockAudioContext.resume).toHaveBeenCalled();
    });
  });

  describe('volume control', () => {
    beforeEach(async () => {
      await manager.initialize();
    });

    it('should set volume correctly', () => {
      manager.setVolume(0.5);
      
      expect(mockGainNode.gain.value).toBe(0.5);
      expect(handler.onVolumeChange).toHaveBeenCalledWith(0.5);
    });

    it('should clamp volume to valid range', () => {
      manager.setVolume(1.5);
      expect(mockGainNode.gain.value).toBe(1);
      
      manager.setVolume(-0.5);
      expect(mockGainNode.gain.value).toBe(0);
    });

    it('should return current volume', () => {
      manager.setVolume(0.7);
      expect(manager.getVolume()).toBe(0.7);
    });
  });

  describe('playback control', () => {
    beforeEach(async () => {
      await manager.initialize();
      manager.handleAudioStart(1, 2.5);
      manager.handleAudioData(1, new ArrayBuffer(1024), 0, true);
      await manager.playAudio(1);
    });

    it('should stop playback', () => {
      manager.stopPlayback();
      
      expect(mockBufferSource.stop).toHaveBeenCalled();
      
      const state = manager.getPlaybackState();
      expect(state.isPlaying).toBe(false);
      expect(state.currentUtteranceId).toBe(null);
    });

    it('should handle playback end', () => {
      // Simulate audio ended
      if (mockBufferSource.onended) {
        mockBufferSource.onended();
      }
      
      expect(handler.onPlaybackEnd).toHaveBeenCalledWith(1);
      
      const state = manager.getPlaybackState();
      expect(state.isPlaying).toBe(false);
      expect(state.currentUtteranceId).toBe(null);
    });
  });

  describe('cleanup', () => {
    it('should cleanup resources', async () => {
      await manager.initialize();
      manager.handleAudioStart(1, 2.5);
      
      manager.cleanup();
      
      expect(mockAudioContext.close).toHaveBeenCalled();
      
      const state = manager.getPlaybackState();
      expect(state.queue).toHaveLength(0);
    });
  });

  describe('static methods', () => {
    it('should detect audio support', () => {
      expect(AudioPlaybackManager.isSupported()).toBe(true);
    });

    it('should detect no audio support', () => {
      const originalAudioContext = global.AudioContext;
      const originalWebkitAudioContext = (global as any).webkitAudioContext;
      
      (global as any).AudioContext = undefined;
      (global as any).webkitAudioContext = undefined;
      
      expect(AudioPlaybackManager.isSupported()).toBe(false);
      
      global.AudioContext = originalAudioContext;
      (global as any).webkitAudioContext = originalWebkitAudioContext;
    });
  });

  describe('audio format handling', () => {
    beforeEach(async () => {
      await manager.initialize();
    });

    it('should handle WAV format', async () => {
      manager.handleAudioStart(1, 2.5, 'wav', 22050, 1);
      manager.handleAudioData(1, new ArrayBuffer(1024), 0, true);
      
      await manager.playAudio(1);
      
      expect(mockAudioContext.decodeAudioData).toHaveBeenCalled();
    });

    it('should handle PCM fallback when WAV decoding fails', async () => {
      mockAudioContext.decodeAudioData.mockRejectedValueOnce(new Error('Decode failed'));
      
      manager.handleAudioStart(1, 2.5, 'wav', 22050, 1);
      manager.handleAudioData(1, new ArrayBuffer(1024), 0, true);
      
      await manager.playAudio(1);
      
      expect(mockAudioContext.createBuffer).toHaveBeenCalledWith(1, 512, 22050);
    });
  });
});