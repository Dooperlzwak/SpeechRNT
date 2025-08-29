/**
 * WebSocket Message Protocol Types
 * Defines all message types exchanged between frontend and backend
 */

// Base message interface
export interface BaseMessage {
  type: string;
}

// Client to Server Messages
export interface ConfigMessage extends BaseMessage {
  type: 'config';
  data: {
    sourceLang: string;
    targetLang: string;
    voice: string;
  };
}

export interface EndSessionMessage extends BaseMessage {
  type: 'end_session';
}

export interface PingMessage extends BaseMessage {
  type: 'ping';
}

export interface SessionRecoveryMessage extends BaseMessage {
  type: 'recover_session';
  sessionId: string;
  lastActivity: number;
  sessionState: any;
}

export type ClientMessage = ConfigMessage | EndSessionMessage | PingMessage | SessionRecoveryMessage;

// Server to Client Messages
export interface TranscriptionUpdateMessage extends BaseMessage {
  type: 'transcription_update';
  data: {
    text: string;
    utteranceId: number;
    confidence: number;
  };
}

export interface TranslationResultMessage extends BaseMessage {
  type: 'translation_result';
  data: {
    originalText: string;
    translatedText: string;
    utteranceId: number;
  };
}

export interface AudioStartMessage extends BaseMessage {
  type: 'audio_start';
  data: {
    utteranceId: number;
    duration: number;
    format?: 'wav' | 'mp3' | 'ogg';
    sampleRate?: number;
    channels?: number;
  };
}

export interface AudioDataMessage extends BaseMessage {
  type: 'audio_data';
  data: {
    utteranceId: number;
    sequenceNumber: number;
    isLast: boolean;
  };
}

export interface AudioEndMessage extends BaseMessage {
  type: 'audio_end';
  data: {
    utteranceId: number;
  };
}

export interface StatusUpdateMessage extends BaseMessage {
  type: 'status_update';
  data: {
    state: 'idle' | 'listening' | 'thinking' | 'speaking';
    utteranceId?: number;
  };
}

export interface ErrorMessage extends BaseMessage {
  type: 'error';
  data: {
    message: string;
    code?: string;
    utteranceId?: number;
  };
}

export interface PongMessage extends BaseMessage {
  type: 'pong';
}

export interface SessionRecoveredMessage extends BaseMessage {
  type: 'session_recovered';
  success: boolean;
  sessionState?: any;
  reason?: string;
}

export interface MessageAckMessage extends BaseMessage {
  type: 'message_ack';
  data: {
    messageId: string;
    status: 'received' | 'processed' | 'error';
    timestamp: string;
    error?: string;
  };
}

export type ServerMessage = 
  | TranscriptionUpdateMessage 
  | TranslationResultMessage 
  | AudioStartMessage 
  | AudioDataMessage
  | AudioEndMessage
  | StatusUpdateMessage 
  | ErrorMessage 
  | PongMessage
  | SessionRecoveredMessage
  | MessageAckMessage;

// Audio configuration
export interface AudioConfig {
  sampleRate: 16000;
  channels: 1;
  bitsPerSample: 16;
  chunkSize: 1024;
}

// Language and voice options
export interface LanguageOption {
  code: string;
  name: string;
  supported: boolean;
}

export interface VoiceOption {
  id: string;
  name: string;
  language: string;
  gender: 'male' | 'female' | 'neutral';
}

// Conversation state
export interface ConversationEntry {
  utteranceId: number;
  originalText: string;
  translatedText: string;
  timestamp: Date;
  speaker: 'user' | 'system';
}

// Application state
export interface AppState {
  sessionActive: boolean;
  currentState: 'idle' | 'listening' | 'thinking' | 'speaking';
  sourceLang: string;
  targetLang: string;
  selectedVoice: string;
  conversationHistory: ConversationEntry[];
  connectionStatus: 'connected' | 'disconnected' | 'reconnecting';
}