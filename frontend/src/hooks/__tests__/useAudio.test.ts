/**
 * useAudio Hook Tests
 */

import { describe, test, expect, beforeEach, vi } from 'vitest';
import { renderHook, act } from '@testing-library/react';
import { useAudio } from '../useAudio';

// Mock AudioManager
vi.mock('../../services/AudioManager', () => {
  const mockAudioManager = {
    initialize: vi.fn(),
    startRecording: vi.fn(),
    stopRecording: vi.fn(),
    cleanup: vi.fn(),
    isActive: vi.fn().mockReturnValue(false),
    getConfig: vi.fn().mockReturnValue({
      sampleRate: 16000,
      channels: 1,
      bitsPerSample: 16,
      chunkSize: 1024,
    }),
    setAudioDevice: vi.fn(),
    getSelectedDeviceId: vi.fn().mockReturnValue(null),
  };

  const MockAudioManager = vi.fn(() => mockAudioManager);
  MockAudioManager.isSupported = vi.fn().mockReturnValue(true);
  MockAudioManager.getAudioDevices = vi.fn().mockResolvedValue([]);
  MockAudioManager.requestMicrophonePermission = vi.fn().mockResolvedValue(true);

  return {
    AudioManager: MockAudioManager,
  };
});

describe('useAudio', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  test('should initialize with default configuration', () => {
    const { result } = renderHook(() => useAudio());

    expect(result.current.isRecording).toBe(false);
    expect(result.current.isInitialized).toBe(false);
    expect(result.current.error).toBe(null);
    expect(result.current.isSupported).toBeDefined();
  });

  test('should initialize with custom configuration', () => {
    const options = {
      sampleRate: 44100,
      channels: 2,
      chunkSize: 2048,
    };

    const { result } = renderHook(() => useAudio(options));

    expect(result.current.isRecording).toBe(false);
    expect(result.current.isInitialized).toBe(false);
  });

  test('should handle audio data callback', () => {
    const onAudioData = vi.fn();
    const { result } = renderHook(() => useAudio({}, onAudioData));

    // The hook should be created successfully
    expect(result.current).toBeDefined();
  });

  test('should provide device management functions', () => {
    const { result } = renderHook(() => useAudio());

    expect(typeof result.current.setAudioDevice).toBe('function');
    expect(typeof result.current.getSelectedDeviceId).toBe('function');
    expect(typeof result.current.getAudioDevices).toBe('function');
    expect(typeof result.current.requestMicrophonePermission).toBe('function');
  });

  test('should handle initialization', async () => {
    const { result } = renderHook(() => useAudio({ autoInitialize: false }));

    await act(async () => {
      await result.current.initialize();
    });

    // The initialize function should be callable
    expect(result.current.initialize).toBeDefined();
  });

  test('should handle recording start/stop', async () => {
    const { result } = renderHook(() => useAudio({ autoInitialize: false }));

    await act(async () => {
      await result.current.startRecording();
    });

    act(() => {
      result.current.stopRecording();
    });

    // Functions should be callable
    expect(result.current.startRecording).toBeDefined();
    expect(result.current.stopRecording).toBeDefined();
  });

  test('should cleanup on unmount', () => {
    const { result, unmount } = renderHook(() => useAudio());

    unmount();

    // Should not throw errors on unmount
    expect(result.current.cleanup).toBeDefined();
  });
});