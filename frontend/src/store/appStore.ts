import { create } from 'zustand';
import { devtools } from 'zustand/middleware';
import type { SystemState } from '../components/StatusIndicator';
import type { AppError } from '../components/ErrorNotification';
import { loadSettingsFromStorage, saveSettingsToStorage, validateSettings } from '../utils/settingsValidation';

export interface ConversationEntry {
  utteranceId: number;
  originalText: string;
  translatedText: string;
  timestamp: Date;
  speaker: 'user' | 'system';
}

export interface AppState {
  // Session state
  sessionActive: boolean;
  currentState: SystemState;

  // Language and voice settings
  sourceLang: string;
  targetLang: string;
  selectedVoice: string;
  selectedModel: string;

  // Conversation content
  conversationHistory: ConversationEntry[];
  currentOriginalText: string;
  currentTranslatedText: string;
  transcriptionConfidence?: number;

  // Connection state
  connectionStatus: 'connected' | 'disconnected' | 'reconnecting';

  // UI state
  settingsOpen: boolean;

  // Error handling
  currentError: AppError | null;
  errorHistory: AppError[];

  // Actions
  toggleSession: () => void;
  setCurrentState: (state: SystemState) => void;
  setLanguages: (source: string, target: string) => void;
  setVoice: (voice: string) => void;
  setModel: (model: string) => void;
  setCurrentOriginalText: (text: string) => void;
  setCurrentTranslatedText: (text: string) => void;
  setTranscriptionConfidence: (confidence?: number) => void;
  addConversationEntry: (entry: Omit<ConversationEntry, 'timestamp'>) => void;
  clearConversation: () => void;
  setConnectionStatus: (status: 'connected' | 'disconnected' | 'reconnecting') => void;
  setSettingsOpen: (open: boolean) => void;
  resetSession: () => void;

  // Error handling actions
  setCurrentError: (error: AppError) => void;
  clearError: () => void;
  addToErrorHistory: (error: AppError) => void;
  clearErrorHistory: () => void;
}

export const useAppStore = create<AppState>()(
  devtools(
    (set, get) => ({
      // Initial state - load from localStorage with validation
      ...loadSettingsFromStorage(),
      sessionActive: false,
      currentState: 'idle',
      conversationHistory: [],
      currentOriginalText: '',
      currentTranslatedText: '',
      transcriptionConfidence: undefined,
      connectionStatus: 'disconnected',
      settingsOpen: false,

      // Error handling initial state
      currentError: null,
      errorHistory: [],

      // Actions
      toggleSession: () => {
        const { sessionActive, resetSession } = get();

        if (sessionActive) {
          // Stop session
          set({
            sessionActive: false,
            currentState: 'idle'
          });
          resetSession();
        } else {
          // Start session
          set({
            sessionActive: true,
            currentState: 'listening',
            connectionStatus: 'connected' // This would be set by WebSocket connection
          });
        }
      },

      setCurrentState: (state: SystemState) => {
        set({ currentState: state });
      },

      setLanguages: (source: string, target: string) => {
        const { selectedVoice } = get();

        // Validate and get corrected settings
        const validation = validateSettings({
          sourceLang: source,
          targetLang: target,
          selectedVoice: selectedVoice
        });

        // Use the corrected settings from validation
        set({
          sourceLang: validation.correctedSettings.sourceLang,
          targetLang: validation.correctedSettings.targetLang,
          selectedVoice: validation.correctedSettings.selectedVoice
        });

        // Persist to localStorage with validation
        saveSettingsToStorage({
          sourceLang: source,
          targetLang: target,
          selectedVoice: selectedVoice,
          selectedModel: get().selectedModel
        });
      },

      setVoice: (voice: string) => {
        const { sourceLang, targetLang } = get();

        // Validate and get corrected settings
        const validation = validateSettings({
          sourceLang: sourceLang,
          targetLang: targetLang,
          selectedVoice: voice
        });

        // Use the corrected voice from validation
        set({ selectedVoice: validation.correctedSettings.selectedVoice });

        // Persist to localStorage with validation
        saveSettingsToStorage({
          sourceLang: sourceLang,
          targetLang: targetLang,
          selectedVoice: voice,
          selectedModel: get().selectedModel
        });
      },

      setModel: (model: string) => {
        const { sourceLang, targetLang, selectedVoice } = get();

        // Validate and get corrected settings
        const validation = validateSettings({
          sourceLang: sourceLang,
          targetLang: targetLang,
          selectedVoice: selectedVoice,
          selectedModel: model
        });

        // Use the corrected model from validation
        set({ selectedModel: validation.correctedSettings.selectedModel });

        // Persist to localStorage with validation
        saveSettingsToStorage({
          sourceLang: sourceLang,
          targetLang: targetLang,
          selectedVoice: selectedVoice,
          selectedModel: model
        });
      },

      setCurrentOriginalText: (text: string) => {
        set({ currentOriginalText: text });
      },

      setCurrentTranslatedText: (text: string) => {
        set({ currentTranslatedText: text });
      },

      setTranscriptionConfidence: (confidence?: number) => {
        set({ transcriptionConfidence: confidence });
      },

      addConversationEntry: (entry: Omit<ConversationEntry, 'timestamp'>) => {
        const { conversationHistory } = get();
        const newEntry: ConversationEntry = {
          ...entry,
          timestamp: new Date()
        };

        set({
          conversationHistory: [...conversationHistory, newEntry]
        });
      },

      clearConversation: () => {
        set({
          conversationHistory: [],
          currentOriginalText: '',
          currentTranslatedText: '',
          transcriptionConfidence: undefined
        });
      },

      setConnectionStatus: (status: 'connected' | 'disconnected' | 'reconnecting') => {
        set({ connectionStatus: status });
      },

      setSettingsOpen: (open: boolean) => {
        set({ settingsOpen: open });
      },

      resetSession: () => {
        set({
          currentOriginalText: '',
          currentTranslatedText: '',
          transcriptionConfidence: undefined,
          currentState: 'idle'
        });
      },

      // Error handling actions
      setCurrentError: (error: AppError) => {
        const { addToErrorHistory } = get();

        // Add to history
        addToErrorHistory(error);

        // Set as current error
        set({ currentError: error });
      },

      clearError: () => {
        set({ currentError: null });
      },

      addToErrorHistory: (error: AppError) => {
        const { errorHistory } = get();
        const updatedHistory = [...errorHistory, error];

        // Keep only last 50 errors to prevent memory issues
        if (updatedHistory.length > 50) {
          updatedHistory.splice(0, updatedHistory.length - 50);
        }

        set({ errorHistory: updatedHistory });
      },

      clearErrorHistory: () => {
        set({ errorHistory: [] });
      }
    }),
    {
      name: 'vocr-store',
    }
  )
);