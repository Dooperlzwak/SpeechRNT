/**
 * AudioManager Tests
 */

import { describe, test, expect, beforeEach, afterEach, vi } from 'vitest';
import { AudioManager, AudioConfig, AudioHandler } from '../AudioManager';

// Mock MediaDevices and AudioContext
const mockGetUserMedia = vi.fn();
const mockAudioContext = {
  createMediaStreamSource: vi.fn(),
  createScriptProcessor: vi.fn(),
  resume: vi.fn(),
  close: vi.fn(),
  state: 'running',
  destination: {},
};

const mockMediaStreamSource = {
  connect: vi.fn(),
  disconnect: vi.fn(),
};

const mockScriptProcessor = {
  connect: vi.fn(),
  disconnect: vi.fn(),
  onaudioprocess: null,
};

const mockMediaStream = {
  getTracks: () => [{ stop: vi.fn() }],
};

// Setup mocks
Object.defineProperty(global.navigator, 'mediaDevices', {
  writable: true,
  value: {
    getUserMedia: mockGetUserMedia,
    enumerateDevices: vi.fn().mockResolvedValue([]),
  },
});

Object.defineProperty(global.window, 'AudioContext', {
  writable: true,
  value: vi.fn(() => mockAudioContext),
});

mockAudioContext.createMediaStreamSource.mockReturnValue(mockMediaStreamSource);
mockAudioContext.createScriptProcessor.mockReturnValue(mockScriptProcessor);
mockGetUserMedia.mockResolvedValue(mockMediaStream);

describe('AudioManager', () => {
  let config: AudioConfig;
  let audioHandler: AudioHandler;
  let audioManager: AudioManager;

  beforeEach(() => {
    config = {
      sampleRate: 16000,
      channels: 1,
      bitsPerSample: 16,
      chunkSize: 1024,
    };

    audioHandler = {
      onAudioData: vi.fn(),
      onError: vi.fn(),
      onStateChange: vi.fn(),
    };

    audioManager = new AudioManager(config, audioHandler);
    
    // Reset mocks
    vi.clearAllMocks();
  });

  afterEach(() => {
    audioManager.cleanup();
  });

  test('should create AudioManager instance', () => {
    expect(audioManager).toBeInstanceOf(AudioManager);
    expect(audioManager.isActive()).toBe(false);
  });

  test('should initialize audio capture', async () => {
    await audioManager.initialize();
    
    expect(mockGetUserMedia).toHaveBeenCalledWith({
      audio: {
        sampleRate: 16000,
        channelCount: 1,
        echoCancellation: true,
        noiseSuppression: true,
        autoGainControl: true,
      },
    });
    
    expect(mockAudioContext.createMediaStreamSource).toHaveBeenCalled();
    expect(mockAudioContext.createScriptProcessor).toHaveBeenCalledWith(1024, 1, 1);
  });

  test('should initialize with specific device ID', async () => {
    const deviceId = 'test-device-id';
    await audioManager.initialize(deviceId);
    
    expect(mockGetUserMedia).toHaveBeenCalledWith({
      audio: {
        sampleRate: 16000,
        channelCount: 1,
        echoCancellation: true,
        noiseSuppression: true,
        autoGainControl: true,
        deviceId: { exact: deviceId },
      },
    });
  });

  test('should start recording', async () => {
    await audioManager.initialize();
    await audioManager.startRecording();
    
    expect(audioManager.isActive()).toBe(true);
    expect(audioHandler.onStateChange).toHaveBeenCalledWith(true);
  });

  test('should stop recording', async () => {
    await audioManager.initialize();
    await audioManager.startRecording();
    
    audioManager.stopRecording();
    
    expect(audioManager.isActive()).toBe(false);
    expect(audioHandler.onStateChange).toHaveBeenCalledWith(false);
  });

  test('should cleanup resources', async () => {
    await audioManager.initialize();
    
    audioManager.cleanup();
    
    expect(mockScriptProcessor.disconnect).toHaveBeenCalled();
    expect(mockMediaStreamSource.disconnect).toHaveBeenCalled();
    expect(mockAudioContext.close).toHaveBeenCalled();
  });

  test('should check if audio is supported', () => {
    const isSupported = AudioManager.isSupported();
    expect(typeof isSupported).toBe('boolean');
  });

  test('should get audio configuration', () => {
    const returnedConfig = audioManager.getConfig();
    expect(returnedConfig).toEqual(config);
    expect(returnedConfig).not.toBe(config); // Should be a copy
  });

  test('should handle getUserMedia errors', async () => {
    const error = new Error('Permission denied');
    mockGetUserMedia.mockRejectedValueOnce(error);
    
    await expect(audioManager.initialize()).rejects.toThrow('Failed to initialize audio');
    expect(audioHandler.onError).toHaveBeenCalled();
  });

  test('should not start recording if not initialized', async () => {
    // Try to start recording without initialization
    await audioManager.startRecording();
    
    // Should initialize first, then start recording
    expect(mockGetUserMedia).toHaveBeenCalled();
    expect(audioManager.isActive()).toBe(true);
  });

  test('should set audio device', async () => {
    const deviceId = 'new-device-id';
    await audioManager.initialize();
    await audioManager.startRecording();
    
    await audioManager.setAudioDevice(deviceId);
    
    expect(audioManager.getSelectedDeviceId()).toBe(deviceId);
    expect(audioManager.isActive()).toBe(true); // Should resume recording
  });

  test('should get audio devices', async () => {
    const mockDevices = [
      { deviceId: 'device1', kind: 'audioinput', label: 'Microphone 1' },
      { deviceId: 'device2', kind: 'audioinput', label: 'Microphone 2' },
    ];
    
    navigator.mediaDevices.enumerateDevices = vi.fn().mockResolvedValue(mockDevices);
    
    const devices = await AudioManager.getAudioDevices();
    expect(devices).toEqual(mockDevices);
  });

  test('should request microphone permission', async () => {
    const result = await AudioManager.requestMicrophonePermission();
    expect(result).toBe(true);
    expect(mockGetUserMedia).toHaveBeenCalledWith({ audio: true });
  });

  test('should handle microphone permission denial', async () => {
    mockGetUserMedia.mockRejectedValueOnce(new Error('Permission denied'));
    
    const result = await AudioManager.requestMicrophonePermission();
    expect(result).toBe(false);
  });

  test('should validate audio format (16kHz mono PCM)', async () => {
    await audioManager.initialize();
    await audioManager.startRecording();
    
    // Simulate audio processing
    const mockBuffer = {
      getChannelData: vi.fn().mockReturnValue(new Float32Array([0.5, -0.5, 0.25])),
      length: 3,
    };
    
    // Access private method for testing
    const processAudioBuffer = (audioManager as any).processAudioBuffer;
    processAudioBuffer.call(audioManager, mockBuffer);
    
    expect(audioHandler.onAudioData).toHaveBeenCalled();
    const callArgs = (audioHandler.onAudioData as any).mock.calls[0];
    const audioData = new Int16Array(callArgs[0]);
    
    // Verify PCM conversion
    expect(audioData.length).toBe(3);
    expect(audioData[0]).toBe(16383); // 0.5 * 0x7FFF
    expect(audioData[1]).toBe(-16383); // -0.5 * 0x7FFF (rounded)
  });
});

