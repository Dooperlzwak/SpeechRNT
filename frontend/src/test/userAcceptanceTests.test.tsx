import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { render, screen, fireEvent, waitFor, act } from '@testing-library/react';
import { userEvent } from '@testing-library/user-event';
import App from '../App';

/**
 * User Acceptance Tests for SpeechRNT
 * These tests simulate real user scenarios and validate the complete user experience
 */

// Mock WebSocket for UAT
class UATMockWebSocket {
  static CONNECTING = 0;
  static OPEN = 1;
  static CLOSING = 2;
  static CLOSED = 3;

  readyState = UATMockWebSocket.CONNECTING;
  onopen: ((event: Event) => void) | null = null;
  onclose: ((event: CloseEvent) => void) | null = null;
  onmessage: ((event: MessageEvent) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;

  private messageHistory: any[] = [];

  constructor(public url: string) {
    setTimeout(() => {
      this.readyState = UATMockWebSocket.OPEN;
      this.onopen?.(new Event('open'));
    }, 50); // Simulate realistic connection time
  }

  send(data: string | ArrayBuffer) {
    this.messageHistory.push(data);
  }

  close(code?: number, reason?: string) {
    this.readyState = UATMockWebSocket.CLOSED;
    this.onclose?.(new CloseEvent('close', { code, reason }));
  }

  // Simulate realistic conversation flow
  simulateConversationFlow(scenario: ConversationScenario) {
    scenario.steps.forEach((step, index) => {
      setTimeout(() => {
        if (step.type === 'transcription') {
          this.simulateMessage({
            type: 'transcription_update',
            data: {
              text: step.content,
              utteranceId: index + 1,
              confidence: step.confidence || 0.95
            }
          });
        } else if (step.type === 'translation') {
          this.simulateMessage({
            type: 'translation_result',
            data: {
              originalText: step.originalText,
              translatedText: step.content,
              utteranceId: index + 1
            }
          });
        } else if (step.type === 'status') {
          this.simulateMessage({
            type: 'status_update',
            data: {
              state: step.content,
              utteranceId: index + 1
            }
          });
        } else if (step.type === 'audio') {
          this.simulateMessage({
            type: 'audio_start',
            data: {
              utteranceId: index + 1,
              duration: step.duration || 2.0
            }
          });
          
          // Simulate audio data
          const audioBuffer = new ArrayBuffer(1024);
          setTimeout(() => {
            this.simulateBinaryMessage(audioBuffer);
          }, 100);
        }
      }, step.delay || index * 500);
    });
  }

  simulateMessage(data: any) {
    if (this.onmessage) {
      this.onmessage(new MessageEvent('message', { data: JSON.stringify(data) }));
    }
  }

  simulateBinaryMessage(data: ArrayBuffer) {
    if (this.onmessage) {
      this.onmessage(new MessageEvent('message', { data }));
    }
  }

  getMessageHistory() {
    return this.messageHistory;
  }
}

interface ConversationStep {
  type: 'transcription' | 'translation' | 'status' | 'audio';
  content: string;
  originalText?: string;
  confidence?: number;
  duration?: number;
  delay?: number;
}

interface ConversationScenario {
  name: string;
  description: string;
  steps: ConversationStep[];
  expectedOutcome: string;
}

describe('User Acceptance Tests', () => {
  let mockWebSocket: UATMockWebSocket;
  let user: ReturnType<typeof userEvent.setup>;

  beforeEach(() => {
    user = userEvent.setup();
    
    // Setup comprehensive mocks
    (global as any).WebSocket = vi.fn((url: string) => {
      mockWebSocket = new UATMockWebSocket(url);
      return mockWebSocket;
    });

    // Mock audio APIs
    const mockAudioContext = {
      createGain: vi.fn(() => ({
        connect: vi.fn(),
        disconnect: vi.fn(),
        gain: { value: 1 }
      })),
      createBufferSource: vi.fn(() => ({
        connect: vi.fn(),
        disconnect: vi.fn(),
        start: vi.fn(),
        stop: vi.fn(),
        buffer: null,
        onended: null
      })),
      createBuffer: vi.fn(() => ({
        getChannelData: vi.fn(() => new Float32Array(1024)),
        numberOfChannels: 1,
        sampleRate: 22050,
        length: 1024,
        duration: 1024 / 22050
      })),
      decodeAudioData: vi.fn((buffer) => Promise.resolve({
        getChannelData: vi.fn(() => new Float32Array(1024)),
        numberOfChannels: 1,
        sampleRate: 22050,
        length: 1024,
        duration: 1024 / 22050
      })),
      close: vi.fn(() => Promise.resolve()),
      resume: vi.fn(() => Promise.resolve()),
      state: 'running',
      sampleRate: 44100,
      destination: { connect: vi.fn(), disconnect: vi.fn() }
    };

    (global as any).AudioContext = vi.fn(() => mockAudioContext);
    (global as any).MediaRecorder = vi.fn(() => ({
      start: vi.fn(),
      stop: vi.fn(),
      state: 'inactive',
      ondataavailable: null,
      onstart: null,
      onstop: null
    }));

    Object.defineProperty(global.navigator, 'mediaDevices', {
      value: {
        getUserMedia: vi.fn(() => Promise.resolve({
          getTracks: vi.fn(() => []),
          getAudioTracks: vi.fn(() => [])
        }))
      },
      writable: true
    });

    Object.defineProperty(global, 'localStorage', {
      value: {
        getItem: vi.fn(),
        setItem: vi.fn(),
        removeItem: vi.fn(),
        clear: vi.fn()
      },
      writable: true
    });
  });

  afterEach(() => {
    vi.clearAllMocks();
  });

  describe('UAT-001: First Time User Experience', () => {
    it('should guide new users through the application successfully', async () => {
      render(<App />);

      // User sees the application load
      expect(screen.getByText(/Speech/)).toBeInTheDocument();
      expect(screen.getByText(/RNT/)).toBeInTheDocument();

      // User waits for connection
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      }, { timeout: 3000 });

      // User sees language indicators
      expect(screen.getByText(/English/)).toBeInTheDocument();
      expect(screen.getByText(/Spanish/)).toBeInTheDocument();

      // User sees start button is available
      const startButton = screen.getByRole('button', { name: /start conversation/i });
      expect(startButton).not.toBeDisabled();

      // User can access settings
      const settingsButton = screen.getByRole('button', { name: /open settings/i });
      expect(settingsButton).toBeInTheDocument();
      
      await user.click(settingsButton);
      
      // Settings dialog opens
      await waitFor(() => {
        expect(screen.getByText(/settings/i)).toBeInTheDocument();
      });

      // User can see language options
      expect(screen.getByLabelText(/source language/i)).toBeInTheDocument();
      expect(screen.getByLabelText(/target language/i)).toBeInTheDocument();

      // User closes settings
      const closeButton = screen.getByRole('button', { name: /close/i });
      await user.click(closeButton);

      // Settings dialog closes
      await waitFor(() => {
        expect(screen.queryByText(/settings/i)).not.toBeInTheDocument();
      });

      // User is ready to start conversation
      expect(startButton).toBeVisible();
    });

    it('should handle microphone permission gracefully', async () => {
      // Mock permission denied
      Object.defineProperty(global.navigator, 'mediaDevices', {
        value: {
          getUserMedia: vi.fn(() => Promise.reject(new Error('Permission denied')))
        },
        writable: true
      });

      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // User should see permission error
      await waitFor(() => {
        expect(screen.getByText(/permission/i)).toBeInTheDocument();
      });

      // Application should remain functional
      expect(screen.getByText(/Speech/)).toBeInTheDocument();
    });
  });

  describe('UAT-002: Basic Conversation Flow', () => {
    const basicConversationScenario: ConversationScenario = {
      name: 'Basic Greeting',
      description: 'User says hello and receives translation',
      steps: [
        { type: 'status', content: 'listening', delay: 100 },
        { type: 'transcription', content: 'Hello', delay: 500 },
        { type: 'transcription', content: 'Hello, how are you?', delay: 800 },
        { type: 'status', content: 'thinking', delay: 1200 },
        { type: 'translation', content: 'Hola, ¿cómo estás?', originalText: 'Hello, how are you?', delay: 1500 },
        { type: 'audio', content: '', duration: 2.0, delay: 1800 },
        { type: 'status', content: 'speaking', delay: 1900 },
        { type: 'status', content: 'idle', delay: 3900 }
      ],
      expectedOutcome: 'User sees transcription, translation, and hears audio'
    };

    it('should handle a complete conversation flow successfully', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      // Start conversation
      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Simulate conversation flow
      act(() => {
        mockWebSocket.simulateConversationFlow(basicConversationScenario);
      });

      // User should see listening state
      await waitFor(() => {
        expect(screen.getByText(/listening/i)).toBeInTheDocument();
      });

      // User should see live transcription
      await waitFor(() => {
        expect(screen.getByText('Hello')).toBeInTheDocument();
      });

      await waitFor(() => {
        expect(screen.getByText('Hello, how are you?')).toBeInTheDocument();
      });

      // User should see processing state
      await waitFor(() => {
        expect(screen.getByText(/processing/i)).toBeInTheDocument();
      });

      // User should see translation
      await waitFor(() => {
        expect(screen.getByText('Hola, ¿cómo estás?')).toBeInTheDocument();
      });

      // User should see speaking state
      await waitFor(() => {
        expect(screen.getByText(/playing audio/i)).toBeInTheDocument();
      });

      // User should return to idle state
      await waitFor(() => {
        expect(screen.getByText(/waiting for speech/i)).toBeInTheDocument();
      }, { timeout: 5000 });

      // Both texts should remain visible
      expect(screen.getByText('Hello, how are you?')).toBeInTheDocument();
      expect(screen.getByText('Hola, ¿cómo estás?')).toBeInTheDocument();
    });

    it('should allow user to stop conversation at any time', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Start conversation flow
      act(() => {
        mockWebSocket.simulateConversationFlow(basicConversationScenario);
      });

      await waitFor(() => {
        expect(screen.getByText(/listening/i)).toBeInTheDocument();
      });

      // User stops conversation
      const stopButton = screen.getByRole('button', { name: /stop conversation/i });
      await user.click(stopButton);

      // Should return to waiting state
      await waitFor(() => {
        expect(screen.getByText(/waiting/i)).toBeInTheDocument();
      });

      // Start button should be available again
      expect(screen.getByRole('button', { name: /start conversation/i })).toBeInTheDocument();
    });
  });

  describe('UAT-003: Multi-turn Conversation', () => {
    const multiTurnScenario: ConversationScenario = {
      name: 'Restaurant Conversation',
      description: 'Multi-turn conversation about restaurant recommendation',
      steps: [
        // First turn
        { type: 'status', content: 'listening', delay: 100 },
        { type: 'transcription', content: 'Can you recommend a good restaurant?', delay: 500 },
        { type: 'status', content: 'thinking', delay: 800 },
        { type: 'translation', content: '¿Puedes recomendar un buen restaurante?', originalText: 'Can you recommend a good restaurant?', delay: 1100 },
        { type: 'audio', content: '', duration: 2.5, delay: 1400 },
        { type: 'status', content: 'speaking', delay: 1500 },
        { type: 'status', content: 'idle', delay: 4000 },
        
        // Second turn
        { type: 'status', content: 'listening', delay: 4500 },
        { type: 'transcription', content: 'What type of cuisine do you prefer?', delay: 5000 },
        { type: 'status', content: 'thinking', delay: 5300 },
        { type: 'translation', content: '¿Qué tipo de cocina prefieres?', originalText: 'What type of cuisine do you prefer?', delay: 5600 },
        { type: 'audio', content: '', duration: 2.0, delay: 5900 },
        { type: 'status', content: 'speaking', delay: 6000 },
        { type: 'status', content: 'idle', delay: 8000 }
      ],
      expectedOutcome: 'User experiences natural multi-turn conversation'
    };

    it('should handle multi-turn conversations naturally', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      act(() => {
        mockWebSocket.simulateConversationFlow(multiTurnScenario);
      });

      // First turn
      await waitFor(() => {
        expect(screen.getByText('Can you recommend a good restaurant?')).toBeInTheDocument();
      });

      await waitFor(() => {
        expect(screen.getByText('¿Puedes recomendar un buen restaurante?')).toBeInTheDocument();
      });

      // Second turn
      await waitFor(() => {
        expect(screen.getByText('What type of cuisine do you prefer?')).toBeInTheDocument();
      }, { timeout: 6000 });

      await waitFor(() => {
        expect(screen.getByText('¿Qué tipo de cocina prefieres?')).toBeInTheDocument();
      });

      // Both conversations should be visible (depending on UI design)
      expect(screen.getByText('Can you recommend a good restaurant?')).toBeInTheDocument();
      expect(screen.getByText('What type of cuisine do you prefer?')).toBeInTheDocument();
    });
  });

  describe('UAT-004: Language Switching', () => {
    it('should allow users to change languages mid-conversation', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      // Start with English to Spanish
      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Simulate first utterance
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'transcription_update',
          data: { text: 'Hello', utteranceId: 1, confidence: 0.95 }
        });
      });

      await waitFor(() => {
        expect(screen.getByText('Hello')).toBeInTheDocument();
      });

      // Stop conversation to change settings
      const stopButton = screen.getByRole('button', { name: /stop conversation/i });
      await user.click(stopButton);

      // Open settings
      const settingsButton = screen.getByRole('button', { name: /open settings/i });
      await user.click(settingsButton);

      // Change target language to French
      const targetLanguageSelect = screen.getByLabelText(/target language/i);
      await user.selectOptions(targetLanguageSelect, 'French');

      // Close settings
      const closeButton = screen.getByRole('button', { name: /close/i });
      await user.click(closeButton);

      // Verify language change is reflected
      await waitFor(() => {
        expect(screen.getByText(/French/)).toBeInTheDocument();
      });

      // Start conversation again
      const newStartButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(newStartButton);

      // Simulate utterance with new language
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'transcription_update',
          data: { text: 'Good morning', utteranceId: 2, confidence: 0.95 }
        });
      });

      act(() => {
        mockWebSocket.simulateMessage({
          type: 'translation_result',
          data: { originalText: 'Good morning', translatedText: 'Bonjour', utteranceId: 2 }
        });
      });

      await waitFor(() => {
        expect(screen.getByText('Good morning')).toBeInTheDocument();
        expect(screen.getByText('Bonjour')).toBeInTheDocument();
      });
    });
  });

  describe('UAT-005: Error Recovery', () => {
    it('should handle connection errors gracefully', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Simulate connection loss
      act(() => {
        mockWebSocket.close(1006, 'Connection lost');
      });

      // User should see reconnecting status
      await waitFor(() => {
        expect(screen.getByText(/reconnecting/i)).toBeInTheDocument();
      });

      // Session should be stopped
      await waitFor(() => {
        expect(screen.getByRole('button', { name: /start conversation/i })).toBeInTheDocument();
      });

      // User should see error notification
      await waitFor(() => {
        expect(screen.getByText(/connection/i)).toBeInTheDocument();
      });
    });

    it('should handle translation errors gracefully', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Simulate transcription success followed by translation error
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'transcription_update',
          data: { text: 'Test message', utteranceId: 1, confidence: 0.95 }
        });
      });

      act(() => {
        mockWebSocket.simulateMessage({
          type: 'error',
          data: {
            message: 'Translation service unavailable',
            code: 'TRANSLATION_ERROR',
            utteranceId: 1
          }
        });
      });

      // User should see the transcription
      await waitFor(() => {
        expect(screen.getByText('Test message')).toBeInTheDocument();
      });

      // User should see error notification
      await waitFor(() => {
        expect(screen.getByText(/error/i)).toBeInTheDocument();
      });

      // Session should continue (not stop due to single error)
      expect(screen.getByRole('button', { name: /stop conversation/i })).toBeInTheDocument();
    });
  });

  describe('UAT-006: Performance and Responsiveness', () => {
    it('should maintain responsive UI during heavy processing', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      const startTime = performance.now();

      // Simulate rapid updates
      for (let i = 0; i < 50; i++) {
        act(() => {
          mockWebSocket.simulateMessage({
            type: 'transcription_update',
            data: { text: `Rapid update ${i}`, utteranceId: 1, confidence: 0.95 }
          });
        });
      }

      // UI should remain responsive
      const settingsButton = screen.getByRole('button', { name: /open settings/i });
      await user.click(settingsButton);

      const responseTime = performance.now() - startTime;
      
      // Settings should open within reasonable time
      await waitFor(() => {
        expect(screen.getByText(/settings/i)).toBeInTheDocument();
      });

      // Response time should be reasonable (less than 1 second)
      expect(responseTime).toBeLessThan(1000);
    });

    it('should handle long conversations without performance degradation', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Simulate long conversation (20 utterances)
      for (let i = 1; i <= 20; i++) {
        act(() => {
          mockWebSocket.simulateMessage({
            type: 'transcription_update',
            data: { text: `Utterance ${i}`, utteranceId: i, confidence: 0.95 }
          });
        });

        act(() => {
          mockWebSocket.simulateMessage({
            type: 'translation_result',
            data: { originalText: `Utterance ${i}`, translatedText: `Expresión ${i}`, utteranceId: i }
          });
        });
      }

      // UI should still be responsive
      const stopButton = screen.getByRole('button', { name: /stop conversation/i });
      const clickTime = performance.now();
      
      await user.click(stopButton);
      
      const stopTime = performance.now();

      // Should stop quickly
      await waitFor(() => {
        expect(screen.getByRole('button', { name: /start conversation/i })).toBeInTheDocument();
      });

      // Response time should be reasonable
      expect(stopTime - clickTime).toBeLessThan(500);
    });
  });

  describe('UAT-007: Accessibility', () => {
    it('should be accessible via keyboard navigation', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      // Tab to start button
      await user.tab();
      expect(screen.getByRole('button', { name: /start conversation/i })).toHaveFocus();

      // Press Enter to start
      await user.keyboard('{Enter}');

      await waitFor(() => {
        expect(screen.getByText(/listening/i)).toBeInTheDocument();
      });

      // Tab to stop button
      await user.tab();
      expect(screen.getByRole('button', { name: /stop conversation/i })).toHaveFocus();

      // Press Enter to stop
      await user.keyboard('{Enter}');

      await waitFor(() => {
        expect(screen.getByRole('button', { name: /start conversation/i })).toBeInTheDocument();
      });
    });

    it('should have proper ARIA labels and roles', async () => {
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      // Check for proper button roles and labels
      const startButton = screen.getByRole('button', { name: /start conversation/i });
      expect(startButton).toHaveAttribute('aria-label');

      const settingsButton = screen.getByRole('button', { name: /open settings/i });
      expect(settingsButton).toHaveAttribute('aria-label');

      // Check for status indicators
      const statusElement = screen.getByText(/waiting/i);
      expect(statusElement.closest('[role="status"]')).toBeInTheDocument();
    });
  });

  describe('UAT-008: Mobile Responsiveness', () => {
    it('should work on mobile viewport', async () => {
      // Simulate mobile viewport
      Object.defineProperty(window, 'innerWidth', { value: 375 });
      Object.defineProperty(window, 'innerHeight', { value: 667 });
      
      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      // All essential elements should be visible
      expect(screen.getByText(/Speech/)).toBeInTheDocument();
      expect(screen.getByRole('button', { name: /start conversation/i })).toBeInTheDocument();
      expect(screen.getByRole('button', { name: /open settings/i })).toBeInTheDocument();

      // Should be able to start conversation
      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      await waitFor(() => {
        expect(screen.getByText(/listening/i)).toBeInTheDocument();
      });

      // Should be able to access settings
      const settingsButton = screen.getByRole('button', { name: /open settings/i });
      await user.click(settingsButton);

      await waitFor(() => {
        expect(screen.getByText(/settings/i)).toBeInTheDocument();
      });
    });
  });

  describe('UAT-009: Data Persistence', () => {
    it('should remember user settings across sessions', async () => {
      const mockLocalStorage = {
        getItem: vi.fn(() => JSON.stringify({
          sourceLang: 'English',
          targetLang: 'French',
          selectedVoice: 'female_voice_2'
        })),
        setItem: vi.fn(),
        removeItem: vi.fn(),
        clear: vi.fn()
      };

      Object.defineProperty(global, 'localStorage', {
        value: mockLocalStorage,
        writable: true
      });

      render(<App />);

      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      // Should load saved settings
      expect(screen.getByText(/French/)).toBeInTheDocument();

      // Open settings to verify
      const settingsButton = screen.getByRole('button', { name: /open settings/i });
      await user.click(settingsButton);

      await waitFor(() => {
        expect(screen.getByDisplayValue(/French/)).toBeInTheDocument();
      });
    });
  });

  describe('UAT-010: Overall User Satisfaction', () => {
    it('should provide a smooth end-to-end user experience', async () => {
      const fullUserJourney: ConversationScenario = {
        name: 'Complete User Journey',
        description: 'Full user experience from start to finish',
        steps: [
          { type: 'status', content: 'listening', delay: 200 },
          { type: 'transcription', content: 'I need directions to the airport', delay: 1000 },
          { type: 'status', content: 'thinking', delay: 1500 },
          { type: 'translation', content: 'Necesito direcciones al aeropuerto', originalText: 'I need directions to the airport', delay: 2000 },
          { type: 'audio', content: '', duration: 3.0, delay: 2500 },
          { type: 'status', content: 'speaking', delay: 2600 },
          { type: 'status', content: 'idle', delay: 5600 },
          
          { type: 'status', content: 'listening', delay: 6000 },
          { type: 'transcription', content: 'Take the highway for 20 minutes', delay: 6800 },
          { type: 'status', content: 'thinking', delay: 7200 },
          { type: 'translation', content: 'Toma la autopista por 20 minutos', originalText: 'Take the highway for 20 minutes', delay: 7600 },
          { type: 'audio', content: '', duration: 2.5, delay: 8000 },
          { type: 'status', content: 'speaking', delay: 8100 },
          { type: 'status', content: 'idle', delay: 10600 }
        ],
        expectedOutcome: 'User completes a natural, helpful conversation'
      };

      render(<App />);

      // User loads application
      await waitFor(() => {
        expect(screen.getByText(/Speech/)).toBeInTheDocument();
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      // User starts conversation
      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Simulate complete conversation
      act(() => {
        mockWebSocket.simulateConversationFlow(fullUserJourney);
      });

      // Verify complete flow
      await waitFor(() => {
        expect(screen.getByText('I need directions to the airport')).toBeInTheDocument();
      });

      await waitFor(() => {
        expect(screen.getByText('Necesito direcciones al aeropuerto')).toBeInTheDocument();
      });

      await waitFor(() => {
        expect(screen.getByText('Take the highway for 20 minutes')).toBeInTheDocument();
      }, { timeout: 8000 });

      await waitFor(() => {
        expect(screen.getByText('Toma la autopista por 20 minutos')).toBeInTheDocument();
      });

      // User ends conversation satisfied
      const stopButton = screen.getByRole('button', { name: /stop conversation/i });
      await user.click(stopButton);

      await waitFor(() => {
        expect(screen.getByText(/waiting/i)).toBeInTheDocument();
      });

      // All conversation content should be preserved
      expect(screen.getByText('I need directions to the airport')).toBeInTheDocument();
      expect(screen.getByText('Necesito direcciones al aeropuerto')).toBeInTheDocument();
      expect(screen.getByText('Take the highway for 20 minutes')).toBeInTheDocument();
      expect(screen.getByText('Toma la autopista por 20 minutos')).toBeInTheDocument();
    });
  });
});