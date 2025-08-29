/**
 * Tests for useAudioIntegration hook
 */

import { renderHook, act } from '@testing-library/react';
import { vi, describe, it, expect, beforeEach, afterEach } from 'vitest';
import { useAudioIntegration } from '../useAudioIntegration';
import { AudioManager } from '../../services/AudioManager';

// Mock AudioManager
vi.mock('../../services/AudioManager');
const MockedAudioManager = vi.mocked(AudioManager);

// Mock useErrorHandler
vi.mock('../useErrorHandler', () => ({
  useErrorHandler: () => ({
    handleAudioError: vi.fn(),
  }),
}));

// Mock navigator.mediaDevices
const mockGetUserMedia = vi.fn();
const mockEnumerateDevices = vi.fn();
const mockAddEventListener = vi.fn();
const mockRemoveEventListener = vi.fn();

Object.defineProperty(navigator, 'mediaDevices', {
  value: {
    getUserMedia: mockGetUserMedia,
    enumerateDevices: mockEnumerateDevices,
    addEventListener: mockAddEventListener,
    removeEventListener: mockRemoveEventListener,
  },
  writable: true,
});

// Mock navigator.permissions
const mockPermissionQuery = vi.fn();
Object.defineProperty(navigator, 'permissions', {
  value: {
    query: mockPermissionQuery,
  },
  writable: true,
});

describe('useAudioIntegration', () => {
  const mockConfig = {
    sampleRate: 16000,
    channels: 1,
    bitsPerSample: 16,
    chunkSize: 1024,
  };

  const mockOnAudioData = vi.fn();
  const mockOnError = vi.fn();
  const mockOnStateChange = vi.fn();

  let mockAudioManagerInstance: any;

  beforeEach(() => {
    vi.clearAllMocks();
    
    // Mock AudioManager instance
    mockAudioManagerInstance = {
      initialize: vi.fn().mockResolvedValue(undefined),
      startRecording: vi.fn().mockResolvedValue(undefined),
      stopRecording: vi.fn(),
      cleanup: vi.fn(),
      setAudioDevice: vi.fn().mockResolvedValue(undefined),
      getSelectedDeviceId: vi.fn().mockReturnValue(null),
      getConfig: vi.fn().mockReturnValue(mockConfig),
    };

    MockedAudioManager.mockImplementation(() => mockAudioManagerInstance);
    MockedAudioManager.isSupported = vi.fn().mockReturnValue(true);
    MockedAudioManager.getAudioDevices = vi.fn().mockResolvedValue([]);
    MockedAudioManager.requestMicrophonePermission = vi.fn().mockResolvedValue(true);

    // Mock permission query
    mockPermissionQuery.mockResolvedValue({ state: 'granted' });
    
    // Mock getUserMedia
    mockGetUserMedia.mockResolvedValue({
      getTracks: () => [{ stop: vi.fn() }],
    });
    
    // Mock enumerateDevices
    mockEnumerateDevices.mockResolvedValue([
      { deviceId: 'device1', kind: 'audioinput', label: 'Microphone 1' },
      { deviceId: 'device2', kind: 'audioinput', label: 'Microphone 2' },
    ]);
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  describe('initialization', () => {
    it('should initialize with default state', () => {
      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      expect(result.current.isInitialized).toBe(false);
      expect(result.current.isRecording).toBe(false);
      expect(result.current.error).toBe(null);
      expect(result.current.isSupported).toBe(true);
    });

    it('should auto-initialize when autoInitialize is not disabled', async () => {
      const configWithAutoInit = { ...mockConfig, autoInitialize: true };
      
      renderHook(() =>
        useAudioIntegration(configWithAutoInit, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      // Wait for auto-initialization
      await act(async () => {
        await new Promise(resolve => setTimeout(resolve, 0));
      });

      expect(mockAudioManagerInstance.initialize).toHaveBeenCalled();
    });

    it('should not auto-initialize when autoInitialize is false', () => {
      const configWithoutAutoInit = { ...mockConfig, autoInitialize: false };
      
      renderHook(() =>
        useAudioIntegration(configWithoutAutoInit, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      expect(mockAudioManagerInstance.initialize).not.toHaveBeenCalled();
    });
  });

  describe('audio control', () => {
    it('should initialize audio manager', async () => {
      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      await act(async () => {
        await result.current.initialize();
      });

      expect(mockAudioManagerInstance.initialize).toHaveBeenCalled();
      expect(result.current.isInitialized).toBe(true);
    });

    it('should start recording', async () => {
      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      await act(async () => {
        await result.current.initialize();
        await result.current.startRecording();
      });

      expect(mockAudioManagerInstance.startRecording).toHaveBeenCalled();
    });

    it('should stop recording', async () => {
      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      await act(async () => {
        await result.current.initialize();
        result.current.stopRecording();
      });

      expect(mockAudioManagerInstance.stopRecording).toHaveBeenCalled();
    });

    it('should cleanup resources', async () => {
      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      // First initialize to have something to cleanup
      await act(async () => {
        await result.current.initialize();
      });

      expect(result.current.isInitialized).toBe(true);

      await act(async () => {
        result.current.cleanup();
      });

      expect(mockAudioManagerInstance.cleanup).toHaveBeenCalled();
      expect(result.current.isInitialized).toBe(false);
      expect(result.current.isRecording).toBe(false);
    });
  });

  describe('device management', () => {
    it('should get audio devices', async () => {
      const mockDevices = [
        { deviceId: 'device1', kind: 'audioinput', label: 'Microphone 1' },
        { deviceId: 'device2', kind: 'audioinput', label: 'Microphone 2' },
      ];
      MockedAudioManager.getAudioDevices.mockResolvedValue(mockDevices);

      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      let devices: MediaDeviceInfo[] = [];
      await act(async () => {
        devices = await result.current.getAudioDevices();
      });

      expect(devices).toEqual(mockDevices);
      expect(MockedAudioManager.getAudioDevices).toHaveBeenCalled();
    });

    it('should set audio device', async () => {
      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      await act(async () => {
        await result.current.initialize();
        await result.current.setAudioDevice('device1');
      });

      expect(mockAudioManagerInstance.setAudioDevice).toHaveBeenCalledWith('device1');
    });

    it('should get selected device ID', () => {
      mockAudioManagerInstance.getSelectedDeviceId.mockReturnValue('device1');

      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      const deviceId = result.current.getSelectedDeviceId();
      expect(deviceId).toBe('device1');
    });

    it('should refresh device list', async () => {
      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      await act(async () => {
        await result.current.refreshDeviceList();
      });

      expect(MockedAudioManager.getAudioDevices).toHaveBeenCalled();
    });
  });

  describe('permission handling', () => {
    it('should request microphone permission', async () => {
      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      let granted = false;
      await act(async () => {
        granted = await result.current.requestMicrophonePermission();
      });

      expect(granted).toBe(true);
      expect(MockedAudioManager.requestMicrophonePermission).toHaveBeenCalled();
    });

    it('should check microphone permission', async () => {
      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      let permissionState: PermissionState = 'prompt';
      await act(async () => {
        permissionState = await result.current.checkMicrophonePermission();
      });

      expect(permissionState).toBe('granted');
      expect(mockPermissionQuery).toHaveBeenCalledWith({ name: 'microphone' });
    });

    it('should handle permission denied during initialization', async () => {
      mockPermissionQuery.mockResolvedValue({ state: 'denied' });

      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      await act(async () => {
        try {
          await result.current.initialize();
        } catch (error) {
          expect(error).toBeInstanceOf(Error);
          expect((error as Error).message).toContain('permission denied');
        }
      });

      expect(result.current.isInitialized).toBe(false);
    });
  });

  describe('error handling', () => {
    it('should handle initialization errors', async () => {
      const initError = new Error('Initialization failed');
      mockAudioManagerInstance.initialize.mockRejectedValue(initError);

      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      await act(async () => {
        try {
          await result.current.initialize();
        } catch (error) {
          expect(error).toBe(initError);
        }
      });

      expect(result.current.error).toBe(initError);
      expect(result.current.isInitialized).toBe(false);
    });

    it('should handle recording errors', async () => {
      const recordError = new Error('Recording failed');
      mockAudioManagerInstance.startRecording.mockRejectedValue(recordError);

      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      await act(async () => {
        await result.current.initialize();
        try {
          await result.current.startRecording();
        } catch (error) {
          expect(error).toBe(recordError);
        }
      });

      expect(result.current.error).toBe(recordError);
    });

    it('should handle device switching errors', async () => {
      const deviceError = new Error('Device switch failed');
      mockAudioManagerInstance.setAudioDevice.mockRejectedValue(deviceError);

      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      await act(async () => {
        await result.current.initialize();
        try {
          await result.current.setAudioDevice('device1');
        } catch (error) {
          expect(error).toBe(deviceError);
        }
      });

      expect(result.current.error).toBe(deviceError);
    });
  });

  describe('integration features', () => {
    it('should return audio configuration', () => {
      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      const config = result.current.getAudioConfig();
      expect(config).toEqual(mockConfig);
    });

    it('should return audio statistics', async () => {
      mockAudioManagerInstance.getSelectedDeviceId.mockReturnValue('device1');

      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      await act(async () => {
        await result.current.initialize();
      });

      const stats = result.current.getAudioStats();
      expect(stats).toEqual({
        isInitialized: true,
        isRecording: false,
        selectedDevice: 'device1',
        availableDevices: 0, // No devices loaded yet
        permissionGranted: true,
        lastError: null,
      });
    });

    it('should handle device change events', () => {
      renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      expect(mockAddEventListener).toHaveBeenCalledWith('devicechange', expect.any(Function));
    });
  });

  describe('callback handling', () => {
    it('should call onAudioData when audio data is received', () => {
      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      // Simulate audio data callback
      const mockAudioData = new ArrayBuffer(1024);
      const audioHandler = MockedAudioManager.mock.calls[0][1];
      
      act(() => {
        audioHandler.onAudioData(mockAudioData);
      });

      expect(mockOnAudioData).toHaveBeenCalledWith(mockAudioData);
    });

    it('should call onError when audio error occurs', () => {
      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      // Simulate audio error callback
      const mockError = new Error('Audio error');
      const audioHandler = MockedAudioManager.mock.calls[0][1];
      
      act(() => {
        audioHandler.onError(mockError);
      });

      expect(mockOnError).toHaveBeenCalledWith(mockError);
      expect(result.current.error).toBe(mockError);
    });

    it('should call onStateChange when recording state changes', () => {
      const { result } = renderHook(() =>
        useAudioIntegration(mockConfig, mockOnAudioData, mockOnError, mockOnStateChange)
      );

      // Simulate state change callback
      const audioHandler = MockedAudioManager.mock.calls[0][1];
      
      act(() => {
        audioHandler.onStateChange(true);
      });

      expect(mockOnStateChange).toHaveBeenCalledWith(true);
      expect(result.current.isRecording).toBe(true);
    });
  });
});