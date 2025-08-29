/**
 * Audio Integration Tests
 * Tests the complete audio capture and streaming flow
 */

import { describe, test, expect, beforeEach, afterEach, vi } from 'vitest';
import { AudioManager, AudioConfig, AudioHandler } from '../AudioManager';

describe('Audio Integration Tests', () => {
  let audioManager: AudioManager;
  let config: AudioConfig;
  let audioHandler: AudioHandler;
  let capturedAudioData: ArrayBuffer[] = [];

  beforeEach(() => {
    capturedAudioData = [];
    
    config = {
      sampleRate: 16000,
      channels: 1,
      bitsPerSample: 16,
      chunkSize: 1024,
    };

    audioHandler = {
      onAudioData: (data: ArrayBuffer) => {
        capturedAudioData.push(data);
      },
      onError: vi.fn(),
      onStateChange: vi.fn(),
    };

    // Mock MediaDevices API
    const mockStream = {
      getTracks: () => [{ stop: vi.fn() }],
    };

    const mockAudioContext = {
      createMediaStreamSource: vi.fn().mockReturnValue({
        connect: vi.fn(),
        disconnect: vi.fn(),
      }),
      createScriptProcessor: vi.fn().mockReturnValue({
        connect: vi.fn(),
        disconnect: vi.fn(),
        onaudioprocess: null,
      }),
      resume: vi.fn().mockResolvedValue(undefined),
      close: vi.fn().mockResolvedValue(undefined),
      state: 'running',
      destination: {},
    };

    Object.defineProperty(global.navigator, 'mediaDevices', {
      writable: true,
      value: {
        getUserMedia: vi.fn().mockResolvedValue(mockStream),
        enumerateDevices: vi.fn().mockResolvedValue([
          { deviceId: 'default', kind: 'audioinput', label: 'Default Microphone' },
          { deviceId: 'device1', kind: 'audioinput', label: 'USB Microphone' },
        ]),
      },
    });

    Object.defineProperty(global.window, 'AudioContext', {
      writable: true,
      value: vi.fn(() => mockAudioContext),
    });

    audioManager = new AudioManager(config, audioHandler);
  });

  afterEach(() => {
    audioManager.cleanup();
  });

  test('should complete full audio capture workflow', async () => {
    // 1. Initialize audio capture
    await audioManager.initialize();
    expect(audioHandler.onError).not.toHaveBeenCalled();

    // 2. Start recording
    await audioManager.startRecording();
    expect(audioManager.isActive()).toBe(true);
    expect(audioHandler.onStateChange).toHaveBeenCalledWith(true);

    // 3. Simulate audio processing
    const mockProcessor = (global.window.AudioContext as any).mock.results[0].value
      .createScriptProcessor.mock.results[0].value;
    
    // Simulate audio buffer processing
    const mockAudioBuffer = {
      getChannelData: vi.fn().mockReturnValue(new Float32Array([0.1, 0.2, -0.1, -0.2])),
      length: 4,
    };

    // Trigger audio processing
    if (mockProcessor.onaudioprocess) {
      mockProcessor.onaudioprocess({ inputBuffer: mockAudioBuffer });
    }

    // 4. Verify audio data was captured and processed
    expect(capturedAudioData.length).toBeGreaterThan(0);
    
    // Verify PCM format (16-bit signed integers)
    const audioData = new Int16Array(capturedAudioData[0]);
    expect(audioData.length).toBe(4);
    expect(audioData[0]).toBe(Math.floor(0.1 * 0x7FFF)); // ~3276
    expect(audioData[1]).toBe(Math.floor(0.2 * 0x7FFF)); // ~6553

    // 5. Stop recording
    audioManager.stopRecording();
    expect(audioManager.isActive()).toBe(false);
    expect(audioHandler.onStateChange).toHaveBeenCalledWith(false);
  });

  test('should handle device switching during recording', async () => {
    // Initialize with default device
    await audioManager.initialize();
    await audioManager.startRecording();
    
    expect(audioManager.isActive()).toBe(true);

    // Switch to a different device
    await audioManager.setAudioDevice('device1');
    
    // Should maintain recording state
    expect(audioManager.isActive()).toBe(true);
    expect(audioManager.getSelectedDeviceId()).toBe('device1');
    
    // Should have reinitialized with new device
    expect(navigator.mediaDevices.getUserMedia).toHaveBeenCalledWith({
      audio: expect.objectContaining({
        deviceId: { exact: 'device1' },
      }),
    });
  });

  test('should validate audio format requirements', async () => {
    await audioManager.initialize();
    await audioManager.startRecording();

    // Verify configuration matches requirements
    const actualConfig = audioManager.getConfig();
    expect(actualConfig.sampleRate).toBe(16000); // 16kHz
    expect(actualConfig.channels).toBe(1); // Mono
    expect(actualConfig.bitsPerSample).toBe(16); // 16-bit
    expect(actualConfig.chunkSize).toBe(1024); // 1024 samples per chunk

    // Verify getUserMedia was called with correct constraints
    expect(navigator.mediaDevices.getUserMedia).toHaveBeenCalledWith({
      audio: {
        sampleRate: 16000,
        channelCount: 1,
        echoCancellation: true,
        noiseSuppression: true,
        autoGainControl: true,
      },
    });
  });

  test('should handle continuous streaming', async () => {
    await audioManager.initialize();
    await audioManager.startRecording();

    const mockProcessor = (global.window.AudioContext as any).mock.results[0].value
      .createScriptProcessor.mock.results[0].value;

    // Simulate multiple audio chunks
    const chunks = [
      new Float32Array([0.1, 0.2, 0.3]),
      new Float32Array([0.4, 0.5, 0.6]),
      new Float32Array([0.7, 0.8, 0.9]),
    ];

    chunks.forEach((chunk, index) => {
      const mockBuffer = {
        getChannelData: vi.fn().mockReturnValue(chunk),
        length: chunk.length,
      };

      if (mockProcessor.onaudioprocess) {
        mockProcessor.onaudioprocess({ inputBuffer: mockBuffer });
      }
    });

    // Should have captured all chunks
    expect(capturedAudioData.length).toBe(3);
    
    // Verify each chunk was processed correctly
    chunks.forEach((originalChunk, index) => {
      const processedChunk = new Int16Array(capturedAudioData[index]);
      expect(processedChunk.length).toBe(originalChunk.length);
      
      // Verify PCM conversion for first sample of each chunk
      const expectedValue = Math.floor(originalChunk[0] * 0x7FFF);
      expect(processedChunk[0]).toBe(expectedValue);
    });
  });

  test('should handle microphone permission and device enumeration', async () => {
    // Test permission request
    const hasPermission = await AudioManager.requestMicrophonePermission();
    expect(hasPermission).toBe(true);
    expect(navigator.mediaDevices.getUserMedia).toHaveBeenCalledWith({ audio: true });

    // Test device enumeration
    const devices = await AudioManager.getAudioDevices();
    expect(devices).toHaveLength(2);
    expect(devices[0]).toEqual({
      deviceId: 'default',
      kind: 'audioinput',
      label: 'Default Microphone',
    });
    expect(devices[1]).toEqual({
      deviceId: 'device1',
      kind: 'audioinput',
      label: 'USB Microphone',
    });
  });

  test('should handle errors gracefully', async () => {
    // Mock getUserMedia to fail
    navigator.mediaDevices.getUserMedia = vi.fn().mockRejectedValue(
      new Error('Permission denied')
    );

    await expect(audioManager.initialize()).rejects.toThrow('Failed to initialize audio');
    expect(audioHandler.onError).toHaveBeenCalledWith(
      expect.objectContaining({
        message: expect.stringContaining('Failed to initialize audio'),
      })
    );
  });
});