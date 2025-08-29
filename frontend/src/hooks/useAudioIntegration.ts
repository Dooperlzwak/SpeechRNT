/**
 * useAudioIntegration - React hook for audio integration with session lifecycle management
 * 
 * This hook wraps AudioManager with enhanced session management, device enumeration,
 * WebSocket binary message integration, and comprehensive error handling for the SpeechRNT application.
 */

import { useEffect, useRef, useState, useCallback } from 'react';
import { AudioManager, type AudioConfig, type AudioHandler } from '../services/AudioManager';
import { useErrorHandler } from './useErrorHandler';
import { usePerformanceMonitoring } from './usePerformanceMonitoring';

export interface AudioIntegrationConfig {
  sampleRate: number;
  channels: number;
  bitsPerSample: number;
  chunkSize: number;
  autoInitialize?: boolean;
  deviceSelectionEnabled?: boolean;
  permissionRetryAttempts?: number;
  deviceSwitchDelay?: number;
}

export interface AudioIntegrationReturn {
  // Audio state
  isInitialized: boolean;
  isRecording: boolean;
  error: Error | null;
  
  // Audio control
  initialize: (deviceId?: string) => Promise<void>;
  startRecording: () => Promise<void>;
  stopRecording: () => void;
  cleanup: () => void;
  
  // Device management
  getAudioDevices: () => Promise<MediaDeviceInfo[]>;
  setAudioDevice: (deviceId: string) => Promise<void>;
  getSelectedDeviceId: () => string | null;
  refreshDeviceList: () => Promise<MediaDeviceInfo[]>;
  
  // Permission handling
  requestMicrophonePermission: () => Promise<boolean>;
  checkMicrophonePermission: () => Promise<PermissionState>;
  
  // Integration features
  isSupported: boolean;
  getAudioConfig: () => AudioConfig;
  getAudioStats: () => {
    isInitialized: boolean;
    isRecording: boolean;
    selectedDevice: string | null;
    availableDevices: number;
    permissionGranted: boolean;
    lastError: string | null;
  };
}

export const useAudioIntegration = (
  config: AudioIntegrationConfig,
  onAudioData: (data: ArrayBuffer) => void,
  onError: (error: Error) => void,
  onStateChange: (recording: boolean) => void
): AudioIntegrationReturn => {
  // State management
  const [isInitialized, setIsInitialized] = useState(false);
  const [isRecording, setIsRecording] = useState(false);
  const [error, setError] = useState<Error | null>(null);
  const [availableDevices, setAvailableDevices] = useState<MediaDeviceInfo[]>([]);
  const [permissionGranted, setPermissionGranted] = useState(false);
  const [isDeviceSwitching, setIsDeviceSwitching] = useState(false);

  // Refs
  const audioManagerRef = useRef<AudioManager | null>(null);
  const initializationPromiseRef = useRef<Promise<void> | null>(null);
  const deviceEnumerationPromiseRef = useRef<Promise<MediaDeviceInfo[]> | null>(null);
  const { handleAudioError } = useErrorHandler();
  
  // Performance monitoring
  const performanceMonitoring = usePerformanceMonitoring({
    enabled: true,
    enableAudioMonitoring: true,
    enableErrorTracking: true
  });

  // Create audio configuration with defaults
  const audioConfig: AudioConfig = {
    sampleRate: config.sampleRate,
    channels: config.channels,
    bitsPerSample: config.bitsPerSample,
    chunkSize: config.chunkSize,
  };

  // Enhanced audio handler with session lifecycle integration
  const audioHandler: AudioHandler = {
    onAudioData: (data: ArrayBuffer) => {
      // Clear error on successful audio data
      if (error) {
        setError(null);
      }
      
      // Record performance metrics
      performanceMonitoring.recordAudioMetrics({
        bytesTransmitted: data.byteLength,
        packetsTransmitted: 1
      });
      
      onAudioData(data);
    },

    onError: (audioError: Error) => {
      setError(audioError);
      setIsRecording(false);
      
      // Record performance metrics
      if (audioError.message.includes('permission') || audioError.message.includes('denied')) {
        performanceMonitoring.recordAudioMetrics({
          permissionDenials: 1
        });
        performanceMonitoring.recordError('audio', audioError);
        setPermissionGranted(false);
        handleAudioError(audioError, 'permission');
      } else if (audioError.message.includes('device') || audioError.message.includes('not found')) {
        performanceMonitoring.recordAudioMetrics({
          captureErrors: 1
        });
        performanceMonitoring.recordError('audio', audioError);
        handleAudioError(audioError, 'capture');
        // Refresh device list on device errors
        refreshDeviceList().catch(console.error);
      } else if (audioError.message.includes('recording') || audioError.message.includes('start')) {
        performanceMonitoring.recordAudioMetrics({
          captureErrors: 1
        });
        performanceMonitoring.recordError('audio', audioError);
        handleAudioError(audioError, 'capture');
      } else {
        performanceMonitoring.recordAudioMetrics({
          captureErrors: 1
        });
        performanceMonitoring.recordError('audio', audioError);
        handleAudioError(audioError, 'capture');
      }
      
      onError(audioError);
    },

    onStateChange: (recording: boolean) => {
      setIsRecording(recording);
      
      // Record performance metrics
      performanceMonitoring.recordAudioMetrics({
        isStreaming: recording
      });
      
      // Clear error on successful state change
      if (recording && error) {
        setError(null);
      }
      
      onStateChange(recording);
    },
  };

  // Initialize audio manager
  useEffect(() => {
    const initStartTime = Date.now();
    
    try {
      audioManagerRef.current = new AudioManager(audioConfig, audioHandler);
      
      // Record successful initialization
      const initDuration = Date.now() - initStartTime;
      performanceMonitoring.recordServiceInitialization('audio', initDuration, true);
      
      // Auto-initialize if enabled
      if (config.autoInitialize !== false) {
        initialize().catch(console.error);
      }
    } catch (error) {
      // Record failed initialization
      const initDuration = Date.now() - initStartTime;
      performanceMonitoring.recordServiceInitialization('audio', initDuration, false);
      performanceMonitoring.recordError('system', error as Error);
    }

    // Set up device change listener
    if (navigator.mediaDevices && navigator.mediaDevices.addEventListener) {
      const handleDeviceChange = () => {
        refreshDeviceList().catch(console.error);
      };
      
      navigator.mediaDevices.addEventListener('devicechange', handleDeviceChange);
      
      return () => {
        if (audioManagerRef.current) {
          audioManagerRef.current.cleanup();
        }
        navigator.mediaDevices.removeEventListener('devicechange', handleDeviceChange);
      };
    }

    return () => {
      if (audioManagerRef.current) {
        audioManagerRef.current.cleanup();
      }
    };
  }, [performanceMonitoring]); // Only run once

  // Initialize with device selection and permission handling
  const initialize = useCallback(async (deviceId?: string): Promise<void> => {
    if (initializationPromiseRef.current) {
      return initializationPromiseRef.current;
    }

    initializationPromiseRef.current = new Promise<void>(async (resolve, reject) => {
      if (!audioManagerRef.current) {
        reject(new Error('Audio manager not available'));
        return;
      }

      try {
        setError(null);
        
        // Check microphone permission first
        const permissionState = await checkMicrophonePermission();
        if (permissionState === 'denied') {
          throw new Error('Microphone permission denied. Please grant access and try again.');
        }
        
        // Request permission if not granted
        if (permissionState !== 'granted') {
          const granted = await requestMicrophonePermission();
          if (!granted) {
            throw new Error('Microphone permission is required for audio capture.');
          }
        }
        
        setPermissionGranted(true);
        
        // Refresh device list before initialization
        await refreshDeviceList();
        
        // Initialize audio manager with selected device
        await audioManagerRef.current.initialize(deviceId);
        setIsInitialized(true);
        
        resolve();
      } catch (err) {
        const error = err as Error;
        setError(error);
        setIsInitialized(false);
        setPermissionGranted(false);
        reject(error);
      } finally {
        initializationPromiseRef.current = null;
      }
    });

    return initializationPromiseRef.current;
  }, []);

  // Start recording with error recovery
  const startRecording = useCallback(async (): Promise<void> => {
    if (!audioManagerRef.current) {
      throw new Error('Audio manager not initialized');
    }

    if (isRecording) {
      return;
    }

    try {
      setError(null);
      
      // Ensure audio is initialized
      if (!isInitialized) {
        await initialize();
      }
      
      await audioManagerRef.current.startRecording();
    } catch (err) {
      const error = err as Error;
      setError(error);
      
      // Try to recover from common errors
      if (error.message.includes('permission')) {
        // Try to re-request permission
        const granted = await requestMicrophonePermission();
        if (granted) {
          // Retry initialization and recording
          await initialize();
          await audioManagerRef.current.startRecording();
        } else {
          throw error;
        }
      } else {
        throw error;
      }
    }
  }, [isInitialized, isRecording, initialize]);

  // Stop recording
  const stopRecording = useCallback((): void => {
    audioManagerRef.current?.stopRecording();
  }, []);

  // Cleanup resources
  const cleanup = useCallback((): void => {
    audioManagerRef.current?.cleanup();
    setIsInitialized(false);
    setIsRecording(false);
    setError(null);
    initializationPromiseRef.current = null;
    deviceEnumerationPromiseRef.current = null;
  }, []);

  // Get available audio devices with caching
  const getAudioDevices = useCallback(async (): Promise<MediaDeviceInfo[]> => {
    if (deviceEnumerationPromiseRef.current) {
      return deviceEnumerationPromiseRef.current;
    }

    deviceEnumerationPromiseRef.current = new Promise<MediaDeviceInfo[]>(async (resolve, reject) => {
      try {
        // Ensure permission is granted for device labels
        if (!permissionGranted) {
          const granted = await requestMicrophonePermission();
          if (!granted) {
            resolve([]); // Return empty array if permission denied
            return;
          }
          setPermissionGranted(true);
        }
        
        const devices = await AudioManager.getAudioDevices();
        setAvailableDevices(devices);
        resolve(devices);
      } catch (error) {
        console.error('Failed to enumerate audio devices:', error);
        reject(error);
      } finally {
        deviceEnumerationPromiseRef.current = null;
      }
    });

    return deviceEnumerationPromiseRef.current;
  }, [permissionGranted]);

  // Set audio device with proper state management
  const setAudioDevice = useCallback(async (deviceId: string): Promise<void> => {
    if (!audioManagerRef.current) {
      throw new Error('Audio manager not initialized');
    }

    if (isDeviceSwitching) {
      throw new Error('Device switch already in progress');
    }

    try {
      setIsDeviceSwitching(true);
      setError(null);
      
      // Record device switch attempt
      performanceMonitoring.recordAudioMetrics({
        deviceSwitches: 1
      });
      
      // Add delay to prevent rapid device switching
      if (config.deviceSwitchDelay && config.deviceSwitchDelay > 0) {
        await new Promise(resolve => setTimeout(resolve, config.deviceSwitchDelay));
      }
      
      await audioManagerRef.current.setAudioDevice(deviceId);
      
      // Refresh device list to update selection
      await refreshDeviceList();
      
    } catch (err) {
      const error = err as Error;
      setError(error);
      performanceMonitoring.recordError('audio', error);
      throw error;
    } finally {
      setIsDeviceSwitching(false);
    }
  }, [isDeviceSwitching, config.deviceSwitchDelay]);

  // Get selected device ID
  const getSelectedDeviceId = useCallback((): string | null => {
    return audioManagerRef.current?.getSelectedDeviceId() || null;
  }, []);

  // Refresh device list
  const refreshDeviceList = useCallback(async (): Promise<MediaDeviceInfo[]> => {
    // Clear cached promise to force refresh
    deviceEnumerationPromiseRef.current = null;
    return getAudioDevices();
  }, [getAudioDevices]);

  // Request microphone permission with retry logic
  const requestMicrophonePermission = useCallback(async (): Promise<boolean> => {
    const maxAttempts = config.permissionRetryAttempts || 1;
    
    for (let attempt = 1; attempt <= maxAttempts; attempt++) {
      try {
        const granted = await AudioManager.requestMicrophonePermission();
        if (granted) {
          setPermissionGranted(true);
          return true;
        }
        
        // If not the last attempt, wait before retrying
        if (attempt < maxAttempts) {
          await new Promise(resolve => setTimeout(resolve, 1000));
        }
      } catch (error) {
        console.error(`Permission request attempt ${attempt} failed:`, error);
        
        if (attempt === maxAttempts) {
          setPermissionGranted(false);
          return false;
        }
      }
    }
    
    setPermissionGranted(false);
    return false;
  }, [config.permissionRetryAttempts]);

  // Check microphone permission status
  const checkMicrophonePermission = useCallback(async (): Promise<PermissionState> => {
    try {
      if (navigator.permissions && navigator.permissions.query) {
        const result = await navigator.permissions.query({ name: 'microphone' as PermissionName });
        return result.state;
      }
      
      // Fallback: try to access microphone to check permission
      try {
        const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
        stream.getTracks().forEach(track => track.stop());
        return 'granted';
      } catch (error) {
        return 'denied';
      }
    } catch (error) {
      console.error('Failed to check microphone permission:', error);
      return 'prompt';
    }
  }, []);

  // Get audio configuration
  const getAudioConfig = useCallback((): AudioConfig => {
    return audioManagerRef.current?.getConfig() || audioConfig;
  }, [audioConfig]);

  // Get audio statistics
  const getAudioStats = useCallback(() => {
    return {
      isInitialized,
      isRecording,
      selectedDevice: getSelectedDeviceId(),
      availableDevices: availableDevices.length,
      permissionGranted,
      lastError: error?.message || null,
    };
  }, [isInitialized, isRecording, availableDevices.length, permissionGranted, error, getSelectedDeviceId]);

  return {
    // Audio state
    isInitialized,
    isRecording,
    error,
    
    // Audio control
    initialize,
    startRecording,
    stopRecording,
    cleanup,
    
    // Device management
    getAudioDevices,
    setAudioDevice,
    getSelectedDeviceId,
    refreshDeviceList,
    
    // Permission handling
    requestMicrophonePermission,
    checkMicrophonePermission,
    
    // Integration features
    isSupported: AudioManager.isSupported(),
    getAudioConfig,
    getAudioStats,
  };
};