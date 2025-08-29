/**
 * STT Configuration Types for Frontend
 * These types mirror the backend STT configuration structure
 */

export interface STTConfig {
  // Model configuration
  model: {
    defaultModel: 'tiny' | 'base' | 'small' | 'medium' | 'large';
    modelsPath: string;
    language: string;
    translateToEnglish: boolean;
  };

  // Language detection settings
  languageDetection: {
    enabled: boolean;
    threshold: number;
    autoSwitching: boolean;
    consistentDetectionRequired: number;
  };

  // Quantization settings
  quantization: {
    level: 'FP32' | 'FP16' | 'INT8' | 'AUTO';
    enableGPUAcceleration: boolean;
    gpuDeviceId: number;
    accuracyThreshold: number;
  };

  // Streaming configuration
  streaming: {
    partialResultsEnabled: boolean;
    minChunkSizeMs: number;
    maxChunkSizeMs: number;
    overlapSizeMs: number;
    enableIncrementalUpdates: boolean;
  };

  // Confidence and quality settings
  confidence: {
    threshold: number;
    wordLevelEnabled: boolean;
    qualityIndicatorsEnabled: boolean;
    filteringEnabled: boolean;
  };

  // Performance settings
  performance: {
    threadCount: number;
    temperature: number;
    maxTokens: number;
    suppressBlank: boolean;
    suppressNonSpeechTokens: boolean;
  };

  // Audio processing settings
  audio: {
    sampleRate: number;
    audioBufferSizeMB: number;
    enableNoiseReduction: boolean;
    vadThreshold: number;
  };

  // Error recovery settings
  errorRecovery: {
    enabled: boolean;
    maxRetryAttempts: number;
    retryBackoffMultiplier: number;
    retryInitialDelayMs: number;
  };

  // Health monitoring settings
  healthMonitoring: {
    enabled: boolean;
    healthCheckIntervalMs: number;
    maxLatencyMs: number;
    maxMemoryUsageMB: number;
  };
}

export interface ConfigValidationResult {
  isValid: boolean;
  errors: string[];
  warnings: string[];
}

export interface ConfigChangeNotification {
  section: string;
  key: string;
  oldValue: string;
  newValue: string;
  timestamp: number;
  config: STTConfig;
}

export type STTConfigMessageType = 
  | 'GET_CONFIG'
  | 'UPDATE_CONFIG'
  | 'UPDATE_CONFIG_VALUE'
  | 'CONFIG_CHANGED'
  | 'GET_SCHEMA'
  | 'GET_METADATA'
  | 'VALIDATE_CONFIG'
  | 'RESET_CONFIG'
  | 'GET_AVAILABLE_MODELS'
  | 'GET_SUPPORTED_QUANTIZATION_LEVELS';

export interface STTConfigMessage {
  type: STTConfigMessageType;
  requestId: string;
  data?: any;
  success?: boolean;
  error?: string;
}

export interface ConfigSchema {
  type: 'object';
  properties: Record<string, any>;
}

export interface ConfigMetadata {
  [section: string]: {
    [key: string]: {
      description: string;
      default?: any;
      options?: any[];
      range?: [number, number];
      minimum?: number;
      maximum?: number;
    };
  };
}

export interface STTConfigManagerOptions {
  autoSave?: boolean;
  validateOnUpdate?: boolean;
  enableChangeNotifications?: boolean;
}