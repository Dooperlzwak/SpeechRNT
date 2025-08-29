import { describe, it, expect, beforeEach, vi, afterEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import App from '../App';
import { useAppStore } from '../store';

// Mock timers for session control
vi.useFakeTimers();

describe('Session State Integration', () => {
  beforeEach(() => {
    // Reset store to initial state before each test
    useAppStore.setState({
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
    }, true);
  });

  afterEach(() => {
    vi.clearAllTimers();
  });

  describe('Complete Session Flow', () => {
    it('should handle complete session start and stop flow', async () => {
      render(<App />);
      
      // Initial state - session should be inactive
      expect(screen.getByText('Waiting')).toBeInTheDocument();
      expect(screen.getByText('disconnected')).toBeInTheDocument();
      expect(screen.getByText('Connection required to start conversation')).toBeInTheDocument();
      
      // Button should be disabled initially
      const sessionButton = screen.getByRole('button', { name: /mic/i });
      expect(sessionButton).toBeDisabled();
      expect(sessionButton).toHaveClass('bg-gray-400');
      
      // Simulate connection establishment (this would normally happen via WebSocket)
      const store = useAppStore.getState();
      store.setConnectionStatus('connected');
      
      await waitFor(() => {
        expect(screen.getByText('connected')).toBeInTheDocument();
      });
      
      // Button should now be enabled
      expect(sessionButton).not.toBeDisabled();
      expect(sessionButton).toHaveClass('bg-blue-500');
      expect(screen.getByText('Click to start conversation')).toBeInTheDocument();
      
      // Start session
      fireEvent.click(sessionButton);
      
      // Should show reconnecting state
      expect(screen.getByText('reconnecting')).toBeInTheDocument();
      
      // Fast-forward connection timer
      vi.advanceTimersByTime(1000);
      
      await waitFor(() => {
        expect(screen.getByText('connected')).toBeInTheDocument();
        expect(screen.getByText('Listening')).toBeInTheDocument();
        expect(screen.getByText('Status: Listening...')).toBeInTheDocument();
      });
      
      // Button should now show stop state
      const stopButton = screen.getByRole('button', { name: /micoff/i });
      expect(stopButton).toHaveClass('bg-red-500', 'animate-pulse');
      expect(screen.getByText('Click to stop conversation')).toBeInTheDocument();
      
      // Stop session
      fireEvent.click(stopButton);
      
      await waitFor(() => {
        expect(screen.getByText('Waiting')).toBeInTheDocument();
        expect(screen.getByText('disconnected')).toBeInTheDocument();
      });
      
      // Should be back to initial state
      const newSessionButton = screen.getByRole('button', { name: /mic/i });
      expect(newSessionButton).toBeDisabled();
      expect(newSessionButton).toHaveClass('bg-gray-400');
    });

    it('should handle state transitions during active session', async () => {
      render(<App />);
      
      // Set up connected state and start session
      const store = useAppStore.getState();
      store.setConnectionStatus('connected');
      
      const sessionButton = screen.getByRole('button', { name: /mic/i });
      fireEvent.click(sessionButton);
      
      // Fast-forward connection timer
      vi.advanceTimersByTime(1000);
      
      await waitFor(() => {
        expect(screen.getByText('Listening')).toBeInTheDocument();
      });
      
      // Simulate state transition to thinking
      store.setCurrentState('thinking');
      
      await waitFor(() => {
        expect(screen.getByText('Thinking')).toBeInTheDocument();
        expect(screen.getByText('Status: Processing...')).toBeInTheDocument();
      });
      
      // Should auto-transition back to listening after 3 seconds
      vi.advanceTimersByTime(3000);
      
      await waitFor(() => {
        expect(screen.getByText('Listening')).toBeInTheDocument();
        expect(screen.getByText('Status: Listening...')).toBeInTheDocument();
      });
      
      // Simulate state transition to speaking
      store.setCurrentState('speaking');
      
      await waitFor(() => {
        expect(screen.getByText('Speaking')).toBeInTheDocument();
        expect(screen.getByText('Status: Playing audio')).toBeInTheDocument();
      });
      
      // Should auto-transition back to listening after 2 seconds
      vi.advanceTimersByTime(2000);
      
      await waitFor(() => {
        expect(screen.getByText('Listening')).toBeInTheDocument();
        expect(screen.getByText('Status: Listening...')).toBeInTheDocument();
      });
    });

    it('should handle conversation text updates', async () => {
      render(<App />);
      
      const store = useAppStore.getState();
      
      // Set some conversation text
      store.setCurrentOriginalText('Hello, how are you?');
      store.setCurrentTranslatedText('Hola, ¿cómo estás?');
      store.setTranscriptionConfidence(0.95);
      
      await waitFor(() => {
        expect(screen.getByText('Hello, how are you?')).toBeInTheDocument();
        expect(screen.getByText('Hola, ¿cómo estás?')).toBeInTheDocument();
      });
      
      // Clear conversation
      store.clearConversation();
      
      await waitFor(() => {
        expect(screen.queryByText('Hello, how are you?')).not.toBeInTheDocument();
        expect(screen.queryByText('Hola, ¿cómo estás?')).not.toBeInTheDocument();
      });
    });

    it('should handle language changes', async () => {
      render(<App />);
      
      // Initial languages
      expect(screen.getByText('🇺🇸 English')).toBeInTheDocument();
      expect(screen.getByText('🇪🇸 Spanish')).toBeInTheDocument();
      
      const store = useAppStore.getState();
      
      // Change languages
      store.setLanguages('French', 'German');
      
      await waitFor(() => {
        expect(screen.getByText('🇺🇸 French')).toBeInTheDocument();
        expect(screen.getByText('🇪🇸 German')).toBeInTheDocument();
        expect(screen.getByText('Original (French)')).toBeInTheDocument();
        expect(screen.getByText('Translation (German)')).toBeInTheDocument();
      });
    });
  });

  describe('Error Handling', () => {
    it('should handle connection failures gracefully', async () => {
      render(<App />);
      
      const store = useAppStore.getState();
      
      // Start with connected state
      store.setConnectionStatus('connected');
      
      const sessionButton = screen.getByRole('button', { name: /mic/i });
      fireEvent.click(sessionButton);
      
      // Simulate connection failure during session
      store.setConnectionStatus('disconnected');
      
      await waitFor(() => {
        expect(screen.getByText('disconnected')).toBeInTheDocument();
      });
      
      // Session should still be active but button should be disabled
      const stopButton = screen.getByRole('button', { name: /micoff/i });
      expect(stopButton).toBeInTheDocument();
    });

    it('should prevent session start when disconnected', () => {
      render(<App />);
      
      // Button should be disabled when disconnected
      const sessionButton = screen.getByRole('button', { name: /mic/i });
      expect(sessionButton).toBeDisabled();
      
      // Click should not start session
      fireEvent.click(sessionButton);
      
      expect(screen.getByText('Waiting')).toBeInTheDocument();
      expect(screen.getByText('Connection required to start conversation')).toBeInTheDocument();
    });
  });

  describe('Settings Integration', () => {
    it('should open and close settings dialog', async () => {
      render(<App />);
      
      // Settings should not be visible initially
      expect(screen.queryByText('Language Settings')).not.toBeInTheDocument();
      
      // Open settings
      const settingsButton = screen.getByRole('button', { name: /settings/i });
      fireEvent.click(settingsButton);
      
      await waitFor(() => {
        expect(screen.getByText('Language Settings')).toBeInTheDocument();
      });
      
      // Close settings (assuming there's a close button)
      const closeButton = screen.getByRole('button', { name: /close/i });
      fireEvent.click(closeButton);
      
      await waitFor(() => {
        expect(screen.queryByText('Language Settings')).not.toBeInTheDocument();
      });
    });
  });
});