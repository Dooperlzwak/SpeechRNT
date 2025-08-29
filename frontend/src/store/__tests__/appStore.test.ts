import { describe, it, expect, beforeEach } from 'vitest';
import { useAppStore } from '../appStore';
import type { ConversationEntry } from '../appStore';

describe('AppStore', () => {
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

  describe('Session Management', () => {
    it('should toggle session from inactive to active', () => {
      const initialState = useAppStore.getState();
      
      expect(initialState.sessionActive).toBe(false);
      expect(initialState.currentState).toBe('idle');
      
      useAppStore.getState().toggleSession();
      
      const newState = useAppStore.getState();
      expect(newState.sessionActive).toBe(true);
      expect(newState.currentState).toBe('listening');
      expect(newState.connectionStatus).toBe('connected');
    });

    it('should toggle session from active to inactive and reset', () => {
      const store = useAppStore.getState();
      
      // First activate session
      store.toggleSession();
      store.setCurrentOriginalText('Hello world');
      store.setCurrentTranslatedText('Hola mundo');
      
      // Then deactivate
      store.toggleSession();
      
      const newState = useAppStore.getState();
      expect(newState.sessionActive).toBe(false);
      expect(newState.currentState).toBe('idle');
      expect(newState.currentOriginalText).toBe('');
      expect(newState.currentTranslatedText).toBe('');
    });

    it('should set current state', () => {
      useAppStore.getState().setCurrentState('thinking');

      const newState = useAppStore.getState();
      expect(newState.currentState).toBe('thinking');
    });
  });

  describe('Language and Voice Settings', () => {
    it('should set languages', () => {
      useAppStore.getState().setLanguages('fr', 'de');

      const newState = useAppStore.getState();
      expect(newState.sourceLang).toBe('fr');
      expect(newState.targetLang).toBe('de');
    });

    it('should set voice', () => {
      // Set a Spanish voice that's compatible with the default target language (es)
      useAppStore.getState().setVoice('es_male_1');

      const newState = useAppStore.getState();
      expect(newState.selectedVoice).toBe('es_male_1');
    });
  });

  describe('Conversation Management', () => {
    it('should set current original text', () => {
      useAppStore.getState().setCurrentOriginalText('Hello world');

      const newState = useAppStore.getState();
      expect(newState.currentOriginalText).toBe('Hello world');
    });

    it('should set current translated text', () => {
      useAppStore.getState().setCurrentTranslatedText('Hola mundo');

      const newState = useAppStore.getState();
      expect(newState.currentTranslatedText).toBe('Hola mundo');
    });

    it('should set transcription confidence', () => {
      useAppStore.getState().setTranscriptionConfidence(0.85);

      const newState = useAppStore.getState();
      expect(newState.transcriptionConfidence).toBe(0.85);
    });

    it('should add conversation entry', () => {
      const entry: Omit<ConversationEntry, 'timestamp'> = {
        utteranceId: 1,
        originalText: 'Hello',
        translatedText: 'Hola',
        speaker: 'user'
      };

      useAppStore.getState().addConversationEntry(entry);

      const newState = useAppStore.getState();
      expect(newState.conversationHistory).toHaveLength(1);
      expect(newState.conversationHistory[0].originalText).toBe('Hello');
      expect(newState.conversationHistory[0].translatedText).toBe('Hola');
    });

    it('should clear conversation', () => {
      const store = useAppStore.getState();

      // Add some conversation data
      store.addConversationEntry({
        utteranceId: 1,
        originalText: 'Hello',
        translatedText: 'Hola',
        speaker: 'user'
      });
      
      store.setCurrentOriginalText('Current text');
      store.setCurrentTranslatedText('Texto actual');
      store.setTranscriptionConfidence(0.9);

      // Clear conversation
      store.clearConversation();

      const newState = useAppStore.getState();
      expect(newState.conversationHistory).toHaveLength(0);
      expect(newState.currentOriginalText).toBe('');
      expect(newState.currentTranslatedText).toBe('');
      expect(newState.transcriptionConfidence).toBeUndefined();
    });
  });

  describe('Connection Status', () => {
    it('should set connection status', () => {
      useAppStore.getState().setConnectionStatus('reconnecting');

      const newState = useAppStore.getState();
      expect(newState.connectionStatus).toBe('reconnecting');
    });
  });

  describe('UI State', () => {
    it('should set settings open state', () => {
      useAppStore.getState().setSettingsOpen(true);

      const newState = useAppStore.getState();
      expect(newState.settingsOpen).toBe(true);
    });
  });

  describe('Session Reset', () => {
    it('should reset session data', () => {
      const store = useAppStore.getState();

      // Set some session data
      store.setCurrentOriginalText('Some text');
      store.setCurrentTranslatedText('Algún texto');
      store.setTranscriptionConfidence(0.8);
      store.setCurrentState('thinking');

      // Reset session
      store.resetSession();

      const newState = useAppStore.getState();
      expect(newState.currentOriginalText).toBe('');
      expect(newState.currentTranslatedText).toBe('');
      expect(newState.transcriptionConfidence).toBeUndefined();
      expect(newState.currentState).toBe('idle');
    });
  });
});