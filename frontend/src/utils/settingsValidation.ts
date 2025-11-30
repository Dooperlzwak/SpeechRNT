/**
 * Settings validation utilities
 * Ensures settings data integrity and provides validation functions
 */

export interface LanguageOption {
  code: string;
  name: string;
  flag: string;
  supported: boolean;
}

export interface VoiceOption {
  id: string;
  name: string;
  language: string;
  gender: 'male' | 'female' | 'neutral';
  supported: boolean;
}

export interface SettingsData {
  sourceLang: string;
  targetLang: string;
  selectedVoice: string;
  selectedModel?: string;
  timestamp?: number;
}

// Available languages configuration
export const AVAILABLE_LANGUAGES: LanguageOption[] = [
  { code: 'en', name: 'English', flag: '🇺🇸', supported: true },
  { code: 'es', name: 'Spanish', flag: '🇪🇸', supported: true },
  { code: 'fr', name: 'French', flag: '🇫🇷', supported: true },
  { code: 'de', name: 'German', flag: '🇩🇪', supported: true },
  { code: 'it', name: 'Italian', flag: '🇮🇹', supported: true },
  { code: 'pt', name: 'Portuguese', flag: '🇵🇹', supported: true },
  { code: 'ja', name: 'Japanese', flag: '🇯🇵', supported: false },
  { code: 'ko', name: 'Korean', flag: '🇰🇷', supported: false },
  { code: 'zh', name: 'Chinese', flag: '🇨🇳', supported: false },
];

// Available voices configuration
export const AVAILABLE_VOICES: VoiceOption[] = [
  { id: 'en_female_1', name: 'Emma (English)', language: 'en', gender: 'female', supported: true },
  { id: 'en_male_1', name: 'James (English)', language: 'en', gender: 'male', supported: true },
  { id: 'es_female_1', name: 'Sofia (Spanish)', language: 'es', gender: 'female', supported: true },
  { id: 'es_male_1', name: 'Carlos (Spanish)', language: 'es', gender: 'male', supported: true },
  { id: 'fr_female_1', name: 'Marie (French)', language: 'fr', gender: 'female', supported: true },
  { id: 'fr_male_1', name: 'Pierre (French)', language: 'fr', gender: 'male', supported: true },
  { id: 'de_female_1', name: 'Anna (German)', language: 'de', gender: 'female', supported: true },
  { id: 'de_male_1', name: 'Hans (German)', language: 'de', gender: 'male', supported: true },
  { id: 'it_female_1', name: 'Giulia (Italian)', language: 'it', gender: 'female', supported: true },
  { id: 'it_male_1', name: 'Marco (Italian)', language: 'it', gender: 'male', supported: true },
  { id: 'pt_female_1', name: 'Ana (Portuguese)', language: 'pt', gender: 'female', supported: true },
  { id: 'pt_male_1', name: 'João (Portuguese)', language: 'pt', gender: 'male', supported: true },
];

// Available translation models
export const AVAILABLE_MODELS = [
  { id: 'marian', name: 'Marian NMT (Default)', description: 'Fast, efficient transformer models', supported: true },
  { id: 'opus-mt', name: 'Opus-MT', description: 'High-quality open models from Helsinki-NLP', supported: true },
  { id: 'm2m-100', name: 'M2M-100', description: 'Massively multilingual model (Facebook)', supported: true },
];

// Default settings
export const DEFAULT_SETTINGS: SettingsData = {
  sourceLang: 'en',
  targetLang: 'es',
  selectedVoice: 'es_female_1',
  selectedModel: 'marian',
};

/**
 * Validates if a language code is supported
 */
export function isLanguageSupported(langCode: string): boolean {
  const language = AVAILABLE_LANGUAGES.find(lang => lang.code === langCode);
  return language?.supported ?? false;
}

/**
 * Validates if a voice ID is supported
 */
export function isVoiceSupported(voiceId: string): boolean {
  const voice = AVAILABLE_VOICES.find(voice => voice.id === voiceId);
  return voice?.supported ?? false;
}

/**
 * Gets available voices for a specific language
 */
export function getVoicesForLanguage(langCode: string): VoiceOption[] {
  return AVAILABLE_VOICES.filter(voice =>
    voice.language === langCode && voice.supported
  );
}

/**
 * Validates if a voice is compatible with a language
 */
export function isVoiceCompatibleWithLanguage(voiceId: string, langCode: string): boolean {
  const voice = AVAILABLE_VOICES.find(voice => voice.id === voiceId);
  return voice?.language === langCode && voice?.supported === true;
}

/**
 * Validates a complete settings object
 */
export function validateSettings(settings: Partial<SettingsData>): {
  isValid: boolean;
  errors: string[];
  correctedSettings: SettingsData;
} {
  const errors: string[] = [];
  let correctedSettings: SettingsData = { ...DEFAULT_SETTINGS };

  // Validate source language
  if (settings.sourceLang && typeof settings.sourceLang === 'string') {
    if (isLanguageSupported(settings.sourceLang)) {
      correctedSettings.sourceLang = settings.sourceLang;
    } else {
      errors.push(`Source language '${settings.sourceLang}' is not supported`);
    }
  } else if (settings.sourceLang !== undefined) {
    errors.push('Source language must be a valid string');
  }

  // Validate target language
  if (settings.targetLang && typeof settings.targetLang === 'string') {
    if (isLanguageSupported(settings.targetLang)) {
      correctedSettings.targetLang = settings.targetLang;
    } else {
      errors.push(`Target language '${settings.targetLang}' is not supported`);
    }
  } else if (settings.targetLang !== undefined) {
    errors.push('Target language must be a valid string');
  }

  // Validate that source and target are different
  if (correctedSettings.sourceLang === correctedSettings.targetLang) {
    errors.push('Source and target languages must be different');
    correctedSettings.targetLang = DEFAULT_SETTINGS.targetLang;
  }

  // Validate voice
  if (settings.selectedVoice && typeof settings.selectedVoice === 'string') {
    if (isVoiceSupported(settings.selectedVoice)) {
      // Check if voice is compatible with target language
      if (isVoiceCompatibleWithLanguage(settings.selectedVoice, correctedSettings.targetLang)) {
        correctedSettings.selectedVoice = settings.selectedVoice;
      } else {
        errors.push(`Voice '${settings.selectedVoice}' is not compatible with target language '${correctedSettings.targetLang}'`);
        // Auto-select first available voice for target language
        const availableVoices = getVoicesForLanguage(correctedSettings.targetLang);
        if (availableVoices.length > 0) {
          correctedSettings.selectedVoice = availableVoices[0].id;
        }
      }
    } else {
      errors.push(`Voice '${settings.selectedVoice}' is not supported`);
      // Auto-select first available voice for target language
      const availableVoices = getVoicesForLanguage(correctedSettings.targetLang);
      if (availableVoices.length > 0) {
        correctedSettings.selectedVoice = availableVoices[0].id;
      }
    }
  } else if (settings.selectedVoice !== undefined) {
    errors.push('Selected voice must be a valid string');
  }

  // Validate model
  if (settings.selectedModel && typeof settings.selectedModel === 'string') {
    const model = AVAILABLE_MODELS.find(m => m.id === settings.selectedModel);
    if (model) {
      if (model.supported) {
        correctedSettings.selectedModel = settings.selectedModel;
      } else {
        errors.push(`Model '${settings.selectedModel}' is not supported`);
        correctedSettings.selectedModel = DEFAULT_SETTINGS.selectedModel;
      }
    } else {
      errors.push(`Model '${settings.selectedModel}' is not valid`);
      correctedSettings.selectedModel = DEFAULT_SETTINGS.selectedModel;
    }
  } else if (settings.selectedModel !== undefined) {
    errors.push('Selected model must be a valid string');
  } else {
    // If not present, use default
    correctedSettings.selectedModel = DEFAULT_SETTINGS.selectedModel;
  }

  // Validate timestamp
  if (settings.timestamp !== undefined) {
    if (typeof settings.timestamp === 'number' && settings.timestamp > 0) {
      correctedSettings.timestamp = settings.timestamp;
    } else {
      errors.push('Timestamp must be a positive number');
    }
  }

  return {
    isValid: errors.length === 0,
    errors,
    correctedSettings
  };
}

/**
 * Safely loads settings from localStorage with validation
 */
export function loadSettingsFromStorage(): SettingsData {
  try {
    const stored = localStorage.getItem('vocr-settings');
    if (!stored) {
      return DEFAULT_SETTINGS;
    }

    const parsed = JSON.parse(stored);
    const validation = validateSettings(parsed);

    if (validation.isValid) {
      return validation.correctedSettings;
    } else {
      console.warn('Invalid settings found in localStorage, using corrected values:', validation.errors);
      return validation.correctedSettings;
    }
  } catch (error) {
    console.warn('Failed to load settings from localStorage:', error);
    return DEFAULT_SETTINGS;
  }
}

/**
 * Safely saves settings to localStorage with validation
 */
export function saveSettingsToStorage(settings: SettingsData): boolean {
  try {
    const validation = validateSettings(settings);

    if (!validation.isValid) {
      console.warn('Invalid settings provided, saving corrected values:', validation.errors);
    }

    const toSave = {
      ...validation.correctedSettings,
      timestamp: Date.now()
    };

    localStorage.setItem('vocr-settings', JSON.stringify(toSave));
    return true;
  } catch (error) {
    console.error('Failed to save settings to localStorage:', error);
    return false;
  }
}