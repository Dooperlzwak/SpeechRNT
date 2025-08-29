/**
 * useAudio - React hook for managing audio capture
 */

import { useEffect, useRef, useState, useCallback } from 'react';
import { AudioManager, type AudioConfig, type AudioHandler } from '../services/AudioManager';
import { useErrorHandler } from './useErrorHandler';

interface UseAudioOptions {
  sampleRate?: number;
  channels?: number;
  bitsPerSample?: number;
  chunkSize?: number;
  autoInitialize?: boolean;
  deviceId?: string;
}

interface UseAudioReturn {
  isRecording: boolean;
  isInitialized: boolean;
  error: Error | null;
  initialize: (deviceId?: string) => Promise<void>;
  startRecording: () => Promise<void>;
  stopRecording: () => void;
  cleanup: () => void;
  setAudioDevice: (deviceId: string) => Promise<void>;
  getSelectedDeviceId: () => string | null;
  isSupported: boolean;
  getAudioDevices: () => Promise<MediaDeviceInfo[]>;
  requestMicrophonePermission: () => Promise<boolean>;
}

export const useAudio = (
  options: UseAudioOptions = {},
  onAudioData?: (data: ArrayBuffer) => void
): UseAudioReturn => {
  const [isRecording, setIsRecording] = useState(false);
  const [isInitialized, setIsInitialized] = useState(false);
  const [error, setError] = useState<Error | null>(null);
  
  const audioManagerRef = useRef<AudioManager | null>(null);
  const { handleAudioError } = useErrorHandler();

  const config: AudioConfig = {
    sampleRate: options.sampleRate || 16000,
    channels: options.channels || 1,
    bitsPerSample: options.bitsPerSample || 16,
    chunkSize: options.chunkSize || 1024,
  };

  const audioHandler: AudioHandler = {
    onAudioData: (data: ArrayBuffer) => {
      onAudioData?.(data);
    },
    onError: (audioError: Error) => {
      setError(audioError);
      setIsRecording(false);
      
      // Determine error type and handle appropriately
      if (audioError.message.includes('permission') || audioError.message.includes('denied')) {
        handleAudioError(audioError, 'permission');
      } else if (audioError.message.includes('recording') || audioError.message.includes('start')) {
        handleAudioError(audioError, 'capture');
      } else {
        handleAudioError(audioError, 'capture');
      }
    },
    onStateChange: (recording: boolean) => {
      setIsRecording(recording);
    },
  };

  // Initialize audio manager
  useEffect(() => {
    audioManagerRef.current = new AudioManager(config, audioHandler);
    
    if (options.autoInitialize !== false) {
      initialize();
    }

    return () => {
      if (audioManagerRef.current) {
        audioManagerRef.current.cleanup();
      }
    };
  }, []); // Only run once

  const initialize = useCallback(async (deviceId?: string): Promise<void> => {
    if (!audioManagerRef.current) {
      return;
    }

    try {
      setError(null);
      await audioManagerRef.current.initialize(deviceId || options.deviceId);
      setIsInitialized(true);
    } catch (err) {
      const error = err as Error;
      setError(error);
      setIsInitialized(false);
      
      // Error is already handled by the AudioHandler, no need to handle again here
    }
  }, [options.deviceId]);

  const startRecording = useCallback(async (): Promise<void> => {
    if (!audioManagerRef.current) {
      return;
    }

    try {
      setError(null);
      await audioManagerRef.current.startRecording();
    } catch (err) {
      const error = err as Error;
      setError(error);
      
      // Error is already handled by the AudioHandler, no need to handle again here
    }
  }, []);

  const stopRecording = useCallback((): void => {
    audioManagerRef.current?.stopRecording();
  }, []);

  const cleanup = useCallback((): void => {
    audioManagerRef.current?.cleanup();
    setIsInitialized(false);
    setIsRecording(false);
  }, []);

  const setAudioDevice = useCallback(async (deviceId: string): Promise<void> => {
    if (!audioManagerRef.current) {
      return;
    }

    try {
      setError(null);
      await audioManagerRef.current.setAudioDevice(deviceId);
    } catch (err) {
      const error = err as Error;
      setError(error);
      
      // Error is already handled by the AudioHandler, no need to handle again here
    }
  }, []);

  const getSelectedDeviceId = useCallback((): string | null => {
    return audioManagerRef.current?.getSelectedDeviceId() || null;
  }, []);

  const getAudioDevices = useCallback(async (): Promise<MediaDeviceInfo[]> => {
    return AudioManager.getAudioDevices();
  }, []);

  const requestMicrophonePermission = useCallback(async (): Promise<boolean> => {
    return AudioManager.requestMicrophonePermission();
  }, []);

  return {
    isRecording,
    isInitialized,
    error,
    initialize,
    startRecording,
    stopRecording,
    cleanup,
    setAudioDevice,
    getSelectedDeviceId,
    isSupported: AudioManager.isSupported(),
    getAudioDevices,
    requestMicrophonePermission,
  };
};