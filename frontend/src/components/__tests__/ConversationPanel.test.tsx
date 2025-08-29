import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import ConversationPanel from '../ConversationPanel';
import type { SystemState } from '../StatusIndicator';

const defaultProps = {
  sessionActive: false,
  currentState: 'idle' as SystemState,
  originalText: '',
  translatedText: '',
  sourceLang: 'English',
  targetLang: 'Spanish',
  connectionStatus: 'disconnected' as const,
  onToggleSession: vi.fn(),
  onOpenSettings: vi.fn(),
  transcriptionConfidence: undefined,
};

describe('ConversationPanel', () => {
  describe('Header Display', () => {
    it('should display application title', () => {
      render(<ConversationPanel {...defaultProps} />);
      
      expect(screen.getByText('Speech')).toBeInTheDocument();
      expect(screen.getByText('RNT')).toBeInTheDocument();
    });

    it('should display language indicators', () => {
      render(<ConversationPanel {...defaultProps} />);
      
      expect(screen.getByText('🇺🇸 English')).toBeInTheDocument();
      expect(screen.getByText('🇪🇸 Spanish')).toBeInTheDocument();
      expect(screen.getByText('⇄')).toBeInTheDocument();
    });

    it('should display status indicator', () => {
      render(<ConversationPanel {...defaultProps} />);
      
      expect(screen.getByText('Waiting')).toBeInTheDocument();
    });
  });

  describe('Connection Status', () => {
    it('should show disconnected status with red indicator', () => {
      render(<ConversationPanel {...defaultProps} connectionStatus="disconnected" />);
      
      expect(screen.getByText('disconnected')).toBeInTheDocument();
      const indicator = screen.getByText('disconnected').previousElementSibling;
      expect(indicator).toHaveClass('bg-red-500');
    });

    it('should show connected status with green indicator', () => {
      render(<ConversationPanel {...defaultProps} connectionStatus="connected" />);
      
      expect(screen.getByText('connected')).toBeInTheDocument();
      const indicator = screen.getByText('connected').previousElementSibling;
      expect(indicator).toHaveClass('bg-green-500');
    });

    it('should show reconnecting status with yellow pulsing indicator', () => {
      render(<ConversationPanel {...defaultProps} connectionStatus="reconnecting" />);
      
      expect(screen.getByText('reconnecting')).toBeInTheDocument();
      const indicator = screen.getByText('reconnecting').previousElementSibling;
      expect(indicator).toHaveClass('bg-yellow-500', 'animate-pulse');
    });
  });

  describe('Session Control Button', () => {
    it('should show start button when session is inactive', () => {
      render(<ConversationPanel {...defaultProps} />);
      
      const button = screen.getByRole('button', { name: /start conversation/i });
      expect(button).toHaveClass('bg-gray-400'); // Disabled due to disconnected status
      expect(screen.getByText('Connection required to start conversation')).toBeInTheDocument();
    });

    it('should show stop button when session is active', () => {
      render(
        <ConversationPanel 
          {...defaultProps} 
          sessionActive={true}
          connectionStatus="connected"
        />
      );
      
      const button = screen.getByRole('button', { name: /stop conversation/i });
      expect(button).toHaveClass('bg-red-500');
      expect(screen.getByText('Click to stop conversation')).toBeInTheDocument();
    });

    it('should be disabled when disconnected', () => {
      render(<ConversationPanel {...defaultProps} connectionStatus="disconnected" />);
      
      const button = screen.getByRole('button', { name: /start conversation/i });
      expect(button).toBeDisabled();
      expect(button).toHaveClass('cursor-not-allowed');
    });

    it('should be enabled when connected', () => {
      render(<ConversationPanel {...defaultProps} connectionStatus="connected" />);
      
      const button = screen.getByRole('button', { name: /start conversation/i });
      expect(button).not.toBeDisabled();
      expect(button).toHaveClass('bg-blue-500');
    });

    it('should call onToggleSession when clicked', () => {
      const onToggleSession = vi.fn();
      render(
        <ConversationPanel 
          {...defaultProps} 
          connectionStatus="connected"
          onToggleSession={onToggleSession}
        />
      );
      
      const button = screen.getByRole('button', { name: /start conversation/i });
      fireEvent.click(button);
      
      expect(onToggleSession).toHaveBeenCalledOnce();
    });
  });

  describe('Visual State Feedback', () => {
    it('should show listening state with pulsing animation', () => {
      render(
        <ConversationPanel 
          {...defaultProps} 
          sessionActive={true}
          currentState="listening"
          connectionStatus="connected"
        />
      );
      
      const button = screen.getByRole('button', { name: /stop conversation/i });
      expect(button).toHaveClass('animate-pulse');
      expect(screen.getByText('Status: Listening...')).toBeInTheDocument();
    });

    it('should show thinking state', () => {
      render(
        <ConversationPanel 
          {...defaultProps} 
          sessionActive={true}
          currentState="thinking"
          connectionStatus="connected"
        />
      );
      
      expect(screen.getByText('Status: Processing...')).toBeInTheDocument();
    });

    it('should show speaking state', () => {
      render(
        <ConversationPanel 
          {...defaultProps} 
          sessionActive={true}
          currentState="speaking"
          connectionStatus="connected"
        />
      );
      
      expect(screen.getByText('Status: Playing audio')).toBeInTheDocument();
    });

    it('should show idle state when session is active', () => {
      render(
        <ConversationPanel 
          {...defaultProps} 
          sessionActive={true}
          currentState="idle"
          connectionStatus="connected"
        />
      );
      
      expect(screen.getByText('Status: Waiting for speech')).toBeInTheDocument();
    });
  });

  describe('Settings Button', () => {
    it('should call onOpenSettings when settings button is clicked', () => {
      const onOpenSettings = vi.fn();
      render(
        <ConversationPanel 
          {...defaultProps} 
          onOpenSettings={onOpenSettings}
        />
      );
      
      const settingsButton = screen.getByRole('button', { name: /open settings/i });
      fireEvent.click(settingsButton);
      
      expect(onOpenSettings).toHaveBeenCalledOnce();
    });
  });

  describe('Conversation Panels', () => {
    it('should display original text panel', () => {
      render(
        <ConversationPanel 
          {...defaultProps} 
          originalText="Hello world"
        />
      );
      
      expect(screen.getByText('Original (English)')).toBeInTheDocument();
      expect(screen.getByText('Hello world')).toBeInTheDocument();
    });

    it('should display translated text panel', () => {
      render(
        <ConversationPanel 
          {...defaultProps} 
          translatedText="Hola mundo"
        />
      );
      
      expect(screen.getByText('Translation (Spanish)')).toBeInTheDocument();
      expect(screen.getByText('Hola mundo')).toBeInTheDocument();
    });

    it('should show live transcription indicator when listening', () => {
      render(
        <ConversationPanel 
          {...defaultProps} 
          currentState="listening"
          originalText="Hello"
        />
      );
      
      // The TranscriptionDisplay component should receive isLive=true
      // This would be tested more thoroughly in TranscriptionDisplay tests
      expect(screen.getByText('Hello')).toBeInTheDocument();
    });

    it('should show live translation indicator when speaking', () => {
      render(
        <ConversationPanel 
          {...defaultProps} 
          currentState="speaking"
          translatedText="Hola"
        />
      );
      
      // The TranscriptionDisplay component should receive isLive=true for translation
      expect(screen.getByText('Hola')).toBeInTheDocument();
    });
  });
});