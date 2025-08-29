import { describe, it, expect, beforeEach, vi, afterEach } from 'vitest';
import { renderHook, act, waitFor } from '@testing-library/react';
import { useSessionControl } from '../useSessionControl';
import { useAppStore } from '../../store/appStore';

// Mock timers
vi.useFakeTimers();

describe('useSessionControl', () => {
  beforeEach(() => {
    // Reset store to initial state before each test
    useAppStore.setState((state) => ({
      ...state,
      sessionActive: false,
      currentState: 'idle',
      sourceLang: 'English',
      targetLang: 'Spanish',
      selectedVoice: 'female_voice_1',
      conversationHistory: [],
      currentOriginalText: '',
      currentTranslatedText: '',
      transcriptionConfidence: undefined,
      connectionStatus: 'disconnected',
      settingsOpen: false,
    }));
  });

  afterEach(() => {
    vi.clearAllTimers();
  });

  describe('Session Control', () => {
    it('should provide initial state', () => {
      const { result } = renderHook(() => useSessionControl());
      
      expect(result.current.sessionActive).toBe(false);
      expect(result.current.currentState).toBe('idle');
      expect(result.current.connectionStatus).toBe('disconnected');
    });

    it('should start session when toggled', async () => {
      const { result } = renderHook(() => useSessionControl());
      
      act(() => {
        result.current.toggleSession();
      });
      
      expect(result.current.sessionActive).toBe(true);
      expect(result.current.connectionStatus).toBe('reconnecting');
      
      // Fast-forward timers to simulate connection establishment
      act(() => {
        vi.advanceTimersByTime(1000);
      });
      
      await waitFor(() => {
        expect(result.current.connectionStatus).toBe('connected');
        expect(result.current.currentState).toBe('listening');
      });
    });

    it('should stop session when toggled off', async () => {
      const { result } = renderHook(() => useSessionControl());
      
      // Start session first
      act(() => {
        result.current.toggleSession();
      });
      
      // Fast-forward to establish connection
      act(() => {
        vi.advanceTimersByTime(1000);
      });
      
      // Stop session
      act(() => {
        result.current.toggleSession();
      });
      
      expect(result.current.sessionActive).toBe(false);
      expect(result.current.connectionStatus).toBe('disconnected');
      expect(result.current.currentState).toBe('idle');
    });
  });

  describe('State Transitions', () => {
    it('should transition to specified state when session is active', async () => {
      const { result } = renderHook(() => useSessionControl());
      
      // Start session first
      act(() => {
        result.current.toggleSession();
      });
      
      // Fast-forward to establish connection
      act(() => {
        vi.advanceTimersByTime(1000);
      });
      
      // Transition to thinking state
      act(() => {
        result.current.transitionToState('thinking');
      });
      
      expect(result.current.currentState).toBe('thinking');
    });

    it('should not transition to non-idle state when session is inactive', () => {
      const { result } = renderHook(() => useSessionControl());
      const consoleSpy = vi.spyOn(console, 'warn').mockImplementation(() => {});
      
      act(() => {
        result.current.transitionToState('listening');
      });
      
      expect(result.current.currentState).toBe('idle');
      expect(consoleSpy).toHaveBeenCalledWith('Cannot transition to non-idle state when session is inactive');
      
      consoleSpy.mockRestore();
    });

    it('should auto-transition from thinking to listening', async () => {
      const { result } = renderHook(() => useSessionControl());
      
      // Start session and establish connection
      act(() => {
        result.current.toggleSession();
      });
      
      act(() => {
        vi.advanceTimersByTime(1000);
      });
      
      // Transition to thinking
      act(() => {
        result.current.transitionToState('thinking');
      });
      
      expect(result.current.currentState).toBe('thinking');
      
      // Fast-forward thinking timer
      act(() => {
        vi.advanceTimersByTime(3000);
      });
      
      await waitFor(() => {
        expect(result.current.currentState).toBe('listening');
      });
    });

    it('should auto-transition from speaking to listening', async () => {
      const { result } = renderHook(() => useSessionControl());
      
      // Start session and establish connection
      act(() => {
        result.current.toggleSession();
      });
      
      act(() => {
        vi.advanceTimersByTime(1000);
      });
      
      // Transition to speaking
      act(() => {
        result.current.transitionToState('speaking');
      });
      
      expect(result.current.currentState).toBe('speaking');
      
      // Fast-forward speaking timer
      act(() => {
        vi.advanceTimersByTime(2000);
      });
      
      await waitFor(() => {
        expect(result.current.currentState).toBe('listening');
      });
    });
  });

  describe('Session Management Functions', () => {
    it('should provide startSession function', () => {
      const { result } = renderHook(() => useSessionControl());
      
      expect(typeof result.current.startSession).toBe('function');
    });

    it('should provide stopSession function', () => {
      const { result } = renderHook(() => useSessionControl());
      
      expect(typeof result.current.stopSession).toBe('function');
    });

    it('should handle startSession directly', async () => {
      const { result } = renderHook(() => useSessionControl());
      
      await act(async () => {
        await result.current.startSession();
      });
      
      // Fast-forward connection timer
      act(() => {
        vi.advanceTimersByTime(1000);
      });
      
      await waitFor(() => {
        expect(result.current.connectionStatus).toBe('connected');
        expect(result.current.currentState).toBe('listening');
      });
    });

    it('should handle stopSession directly', async () => {
      const { result } = renderHook(() => useSessionControl());
      
      // Start session first
      await act(async () => {
        await result.current.startSession();
      });
      
      // Stop session
      await act(async () => {
        await result.current.stopSession();
      });
      
      expect(result.current.connectionStatus).toBe('disconnected');
      expect(result.current.currentState).toBe('idle');
    });
  });
});