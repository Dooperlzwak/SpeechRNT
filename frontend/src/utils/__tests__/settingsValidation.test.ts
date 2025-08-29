import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import {
  validateSettings,
  isLanguageSupported,
  isVoiceSupported,
  getVoicesForLanguage,
  isVoiceCompatibleWithLanguage,
  loadSettingsFromStorage,
  saveSettingsToStorage,
  DEFAULT_SETTINGS,
  AVAILABLE_LANGUAGES,
  AVAILABLE_VOICES
} from '../settingsValidation';

// Mock localStorage
const localStorageMock = {
  getItem: vi.fn(),
  setItem: vi.fn(),
  removeItem: vi.fn(),
  clear: vi.fn(),
};

Object.defineProperty(window, 'localStorage', {
  value: localStorageMock,
});

describe('Settings Validation', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  afterEach(() => {
    vi.clearAllMocks();
  });

  describe('isLanguageSupported', () => {
    it('returns true for supported languages', () => {
      expect(isLanguageSupported('en')).toBe(true);
      expect(isLanguageSupported('es')).toBe(true);
      expect(isLanguageSupported('fr')).toBe(true);
    });

    it('returns false for unsupported languages', () => {
      expect(isLanguageSupported('ja')).toBe(false);
      expect(isLanguageSupported('ko')).toBe(false);
      expect(isLanguageSupported('zh')).toBe(false);
    });

    it('returns false for non-existent languages', () => {
      expect(isLanguageSupported('xx')).toBe(false);
      expect(isLanguageSupported('')).toBe(false);
    });
  });

  describe('isVoiceSupported', () => {
    it('returns true for supported voices', () => {
      expect(isVoiceSupported('en_female_1')).toBe(true);
      expect(isVoiceSupported('es_male_1')).toBe(true);
    });

    it('returns false for non-existent voices', () => {
      expect(isVoiceSupported('nonexistent_voice')).toBe(false);
      expect(isVoiceSupported('')).toBe(false);
    });
  });

  describe('getVoicesForLanguage', () => {
    it('returns voices for supported languages', () => {
      const englishVoices = getVoicesForLanguage('en');
      expect(englishVoices).toHaveLength(2);
      expect(englishVoices.every(voice => voice.language === 'en')).toBe(true);
      expect(englishVoices.every(voice => voice.supported)).toBe(true);
    });

    it('returns empty array for unsupported languages', () => {
      const voices = getVoicesForLanguage('ja');
      expect(voices).toHaveLength(0);
    });

    it('returns empty array for non-existent languages', () => {
      const voices = getVoicesForLanguage('xx');
      expect(voices).toHaveLength(0);
    });
  });

  describe('isVoiceCompatibleWithLanguage', () => {
    it('returns true for compatible voice-language pairs', () => {
      expect(isVoiceCompatibleWithLanguage('en_female_1', 'en')).toBe(true);
      expect(isVoiceCompatibleWithLanguage('es_male_1', 'es')).toBe(true);
    });

    it('returns false for incompatible voice-language pairs', () => {
      expect(isVoiceCompatibleWithLanguage('en_female_1', 'es')).toBe(false);
      expect(isVoiceCompatibleWithLanguage('es_male_1', 'fr')).toBe(false);
    });

    it('returns false for non-existent voices', () => {
      expect(isVoiceCompatibleWithLanguage('nonexistent_voice', 'en')).toBe(false);
    });
  });

  describe('validateSettings', () => {
    it('validates correct settings', () => {
      const settings = {
        sourceLang: 'en',
        targetLang: 'es',
        selectedVoice: 'es_female_1'
      };

      const result = validateSettings(settings);
      
      expect(result.isValid).toBe(true);
      expect(result.errors).toHaveLength(0);
      expect(result.correctedSettings).toEqual(settings);
    });

    it('rejects same source and target languages', () => {
      const settings = {
        sourceLang: 'en',
        targetLang: 'en',
        selectedVoice: 'en_female_1'
      };

      const result = validateSettings(settings);
      
      expect(result.isValid).toBe(false);
      expect(result.errors).toContain('Source and target languages must be different');
      expect(result.correctedSettings.targetLang).toBe(DEFAULT_SETTINGS.targetLang);
    });

    it('rejects unsupported languages', () => {
      const settings = {
        sourceLang: 'ja',
        targetLang: 'ko',
        selectedVoice: 'en_female_1'
      };

      const result = validateSettings(settings);
      
      expect(result.isValid).toBe(false);
      expect(result.errors).toContain("Source language 'ja' is not supported");
      expect(result.errors).toContain("Target language 'ko' is not supported");
    });

    it('rejects incompatible voice-language pairs', () => {
      const settings = {
        sourceLang: 'en',
        targetLang: 'es',
        selectedVoice: 'fr_female_1'
      };

      const result = validateSettings(settings);
      
      expect(result.isValid).toBe(false);
      expect(result.errors).toContain("Voice 'fr_female_1' is not compatible with target language 'es'");
      expect(result.correctedSettings.selectedVoice).toBe('es_female_1'); // Auto-corrected
    });

    it('rejects invalid data types', () => {
      const settings = {
        sourceLang: 123,
        targetLang: null,
        selectedVoice: true,
        timestamp: 'invalid'
      };

      const result = validateSettings(settings);
      
      expect(result.isValid).toBe(false);
      expect(result.errors).toContain('Source language must be a valid string');
      expect(result.errors).toContain('Target language must be a valid string');
      expect(result.errors).toContain('Selected voice must be a valid string');
      expect(result.errors).toContain('Timestamp must be a positive number');
    });

    it('uses defaults for missing values', () => {
      const result = validateSettings({});
      
      expect(result.isValid).toBe(true);
      expect(result.correctedSettings).toEqual(DEFAULT_SETTINGS);
    });

    it('auto-corrects voice when target language changes', () => {
      const settings = {
        sourceLang: 'en',
        targetLang: 'fr',
        selectedVoice: 'es_female_1' // Spanish voice for French target
      };

      const result = validateSettings(settings);
      
      expect(result.isValid).toBe(false);
      expect(result.correctedSettings.selectedVoice).toBe('fr_female_1'); // Auto-corrected to French voice
    });
  });

  describe('loadSettingsFromStorage', () => {
    it('loads valid settings from localStorage', () => {
      const validSettings = {
        sourceLang: 'fr',
        targetLang: 'de',
        selectedVoice: 'de_female_1',
        timestamp: Date.now()
      };

      localStorageMock.getItem.mockReturnValue(JSON.stringify(validSettings));

      const result = loadSettingsFromStorage();
      
      expect(result.sourceLang).toBe('fr');
      expect(result.targetLang).toBe('de');
      expect(result.selectedVoice).toBe('de_female_1');
    });

    it('returns defaults when localStorage is empty', () => {
      localStorageMock.getItem.mockReturnValue(null);

      const result = loadSettingsFromStorage();
      
      expect(result).toEqual(DEFAULT_SETTINGS);
    });

    it('returns corrected settings for invalid data', () => {
      const invalidSettings = {
        sourceLang: 'ja', // Unsupported
        targetLang: 'ko', // Unsupported
        selectedVoice: 'invalid_voice'
      };

      localStorageMock.getItem.mockReturnValue(JSON.stringify(invalidSettings));

      const result = loadSettingsFromStorage();
      
      expect(result).toEqual(DEFAULT_SETTINGS);
    });

    it('returns defaults when JSON parsing fails', () => {
      localStorageMock.getItem.mockReturnValue('invalid json');

      const result = loadSettingsFromStorage();
      
      expect(result).toEqual(DEFAULT_SETTINGS);
    });
  });

  describe('saveSettingsToStorage', () => {
    it('saves valid settings to localStorage', () => {
      const settings = {
        sourceLang: 'fr',
        targetLang: 'de',
        selectedVoice: 'de_female_1'
      };

      const result = saveSettingsToStorage(settings);
      
      expect(result).toBe(true);
      expect(localStorageMock.setItem).toHaveBeenCalledWith(
        'speechrnt-settings',
        expect.stringContaining('"sourceLang":"fr"')
      );
    });

    it('saves corrected settings for invalid data', () => {
      const invalidSettings = {
        sourceLang: 'ja', // Unsupported
        targetLang: 'ko', // Unsupported
        selectedVoice: 'invalid_voice'
      };

      const result = saveSettingsToStorage(invalidSettings);
      
      expect(result).toBe(true);
      expect(localStorageMock.setItem).toHaveBeenCalledWith(
        'speechrnt-settings',
        expect.stringContaining('"sourceLang":"en"') // Corrected to default
      );
    });

    it('includes timestamp when saving', () => {
      const settings = {
        sourceLang: 'en',
        targetLang: 'es',
        selectedVoice: 'es_female_1'
      };

      const beforeTime = Date.now();
      saveSettingsToStorage(settings);
      
      const savedData = localStorageMock.setItem.mock.calls[0][1];
      const parsedData = JSON.parse(savedData);
      
      expect(parsedData.timestamp).toBeGreaterThanOrEqual(beforeTime);
      expect(parsedData.timestamp).toBeLessThanOrEqual(Date.now());
    });

    it('returns false when localStorage throws error', () => {
      localStorageMock.setItem.mockImplementation(() => {
        throw new Error('QuotaExceededError');
      });

      const settings = {
        sourceLang: 'en',
        targetLang: 'es',
        selectedVoice: 'es_female_1'
      };

      const result = saveSettingsToStorage(settings);
      
      expect(result).toBe(false);
    });
  });

  describe('Configuration constants', () => {
    it('has valid language configuration', () => {
      expect(AVAILABLE_LANGUAGES).toBeDefined();
      expect(AVAILABLE_LANGUAGES.length).toBeGreaterThan(0);
      
      AVAILABLE_LANGUAGES.forEach(lang => {
        expect(lang.code).toBeTruthy();
        expect(lang.name).toBeTruthy();
        expect(lang.flag).toBeTruthy();
        expect(typeof lang.supported).toBe('boolean');
      });
    });

    it('has valid voice configuration', () => {
      expect(AVAILABLE_VOICES).toBeDefined();
      expect(AVAILABLE_VOICES.length).toBeGreaterThan(0);
      
      AVAILABLE_VOICES.forEach(voice => {
        expect(voice.id).toBeTruthy();
        expect(voice.name).toBeTruthy();
        expect(voice.language).toBeTruthy();
        expect(['male', 'female', 'neutral']).toContain(voice.gender);
        expect(typeof voice.supported).toBe('boolean');
      });
    });

    it('has valid default settings', () => {
      expect(DEFAULT_SETTINGS).toBeDefined();
      expect(isLanguageSupported(DEFAULT_SETTINGS.sourceLang)).toBe(true);
      expect(isLanguageSupported(DEFAULT_SETTINGS.targetLang)).toBe(true);
      expect(isVoiceSupported(DEFAULT_SETTINGS.selectedVoice)).toBe(true);
      expect(DEFAULT_SETTINGS.sourceLang).not.toBe(DEFAULT_SETTINGS.targetLang);
    });
  });
});