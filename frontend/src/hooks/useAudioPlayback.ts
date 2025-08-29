/**
 * useAudioPlayback - React hook for managing audio playback
 */

import { useState, useCallback, useRef, useEffect } from 'react';
import { AudioPlaybackManager, type AudioPlaybackConfig, type PlaybackHandler, type AudioPlaybackState } from '../services/AudioPlaybackManager';

export interface UseAudioPlaybackReturn {
  playbackState: AudioPlaybackState;
  isSupported: boolean;
  isInitialized: boolean;
  error: Error | null;
  initialize: () => Promise<void>;
  handleAudioStart: (utteranceId: number, duration: number, format?: string, sampleRate?: number, channels?: number) => void;
  handleAudioData: (utteranceId: number, audioData: ArrayBuffer, sequenceNumber: number, isLast: boolean) => void;
  handleAudioEnd: (utteranceId: number) => void;
  playAudio: (utteranceId: number) => Promise<void>;
  stopPlayback: () => void;
  setVolume: (volume: number) => void;
  cleanup: () => void;
}

export const useAudioPlayback = (
  config: AudioPlaybackConfig,
  onPlaybackStart?: (utteranceId: number) => void,
  onPlaybackEnd?: (utteranceId: number) => void,
  onPlaybackError?: (utteranceId: number, error: Error) => void,
  onVolumeChange?: (volume: number) => void
): UseAudioPlaybackReturn => {
  const [playbackState, setPlaybackState] = useState<AudioPlaybackState>({
    isPlaying: false,
    currentUtteranceId: null,
    volume: config.volume,
    queue: [],
  });
  const [isInitialized, setIsInitialized] = useState(false);
  const [error, setError] = useState<Error | null>(null);
  
  const managerRef = useRef<AudioPlaybackManager | null>(null);
  const isSupported = AudioPlaybackManager.isSupported();

  // Create playback handler
  const playbackHandler: PlaybackHandler = {
    onPlaybackStart: useCallback((utteranceId: number) => {
      setPlaybackState(prev => ({
        ...prev,
        isPlaying: true,
        currentUtteranceId: utteranceId,
      }));
      onPlaybackStart?.(utteranceId);
    }, [onPlaybackStart]),

    onPlaybackEnd: useCallback((utteranceId: number) => {
      setPlaybackState(prev => ({
        ...prev,
        isPlaying: false,
        currentUtteranceId: null,
      }));
      onPlaybackEnd?.(utteranceId);
    }, [onPlaybackEnd]),

    onPlaybackError: useCallback((utteranceId: number, error: Error) => {
      setError(error);
      setPlaybackState(prev => ({
        ...prev,
        isPlaying: false,
        currentUtteranceId: null,
      }));
      onPlaybackError?.(utteranceId, error);
    }, [onPlaybackError]),

    onVolumeChange: useCallback((volume: number) => {
      setPlaybackState(prev => ({
        ...prev,
        volume,
      }));
      onVolumeChange?.(volume);
    }, [onVolumeChange]),
  };

  // Initialize audio playback manager
  const initialize = useCallback(async () => {
    if (!isSupported) {
      const error = new Error('Audio playback is not supported in this browser');
      setError(error);
      throw error;
    }

    if (managerRef.current) {
      return; // Already initialized
    }

    try {
      setError(null);
      managerRef.current = new AudioPlaybackManager(config, playbackHandler);
      await managerRef.current.initialize();
      setIsInitialized(true);
    } catch (error) {
      const initError = error instanceof Error ? error : new Error(String(error));
      setError(initError);
      throw initError;
    }
  }, [isSupported, config, playbackHandler]);

  // Handle audio start
  const handleAudioStart = useCallback((
    utteranceId: number, 
    duration: number, 
    format = 'wav', 
    sampleRate = 22050, 
    channels = 1
  ) => {
    if (!managerRef.current) {
      console.warn('AudioPlaybackManager not initialized');
      return;
    }

    try {
      managerRef.current.handleAudioStart(utteranceId, duration, format, sampleRate, channels);
      // Update state to reflect new pending audio
      setPlaybackState(prev => ({
        ...prev,
        queue: managerRef.current!.getPlaybackState().queue,
      }));
    } catch (error) {
      const handleError = error instanceof Error ? error : new Error(String(error));
      setError(handleError);
    }
  }, []);

  // Handle audio data
  const handleAudioData = useCallback((
    utteranceId: number, 
    audioData: ArrayBuffer, 
    sequenceNumber: number, 
    isLast: boolean
  ) => {
    if (!managerRef.current) {
      console.warn('AudioPlaybackManager not initialized');
      return;
    }

    try {
      managerRef.current.handleAudioData(utteranceId, audioData, sequenceNumber, isLast);
      // Update state to reflect audio progress
      setPlaybackState(prev => ({
        ...prev,
        queue: managerRef.current!.getPlaybackState().queue,
      }));
    } catch (error) {
      const handleError = error instanceof Error ? error : new Error(String(error));
      setError(handleError);
    }
  }, []);

  // Handle audio end
  const handleAudioEnd = useCallback((utteranceId: number) => {
    if (!managerRef.current) {
      console.warn('AudioPlaybackManager not initialized');
      return;
    }

    try {
      managerRef.current.handleAudioEnd(utteranceId);
      setPlaybackState(prev => ({
        ...prev,
        queue: managerRef.current!.getPlaybackState().queue,
      }));
    } catch (error) {
      const handleError = error instanceof Error ? error : new Error(String(error));
      setError(handleError);
    }
  }, []);

  // Play specific audio
  const playAudio = useCallback(async (utteranceId: number) => {
    if (!managerRef.current) {
      const error = new Error('AudioPlaybackManager not initialized');
      setError(error);
      throw error;
    }

    try {
      setError(null);
      await managerRef.current.playAudio(utteranceId);
    } catch (error) {
      const playError = error instanceof Error ? error : new Error(String(error));
      setError(playError);
      throw playError;
    }
  }, []);

  // Stop playback
  const stopPlayback = useCallback(() => {
    if (!managerRef.current) {
      return;
    }

    try {
      managerRef.current.stopPlayback();
      setPlaybackState(prev => ({
        ...prev,
        isPlaying: false,
        currentUtteranceId: null,
        queue: [],
      }));
    } catch (error) {
      const stopError = error instanceof Error ? error : new Error(String(error));
      setError(stopError);
    }
  }, []);

  // Set volume
  const setVolume = useCallback((volume: number) => {
    if (!managerRef.current) {
      return;
    }

    try {
      managerRef.current.setVolume(volume);
    } catch (error) {
      const volumeError = error instanceof Error ? error : new Error(String(error));
      setError(volumeError);
    }
  }, []);

  // Cleanup
  const cleanup = useCallback(() => {
    if (managerRef.current) {
      managerRef.current.cleanup();
      managerRef.current = null;
    }
    setIsInitialized(false);
    setError(null);
    setPlaybackState({
      isPlaying: false,
      currentUtteranceId: null,
      volume: config.volume,
      queue: [],
    });
  }, [config.volume]);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      cleanup();
    };
  }, [cleanup]);

  return {
    playbackState,
    isSupported,
    isInitialized,
    error,
    initialize,
    handleAudioStart,
    handleAudioData,
    handleAudioEnd,
    playAudio,
    stopPlayback,
    setVolume,
    cleanup,
  };
};