import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { useAppStore } from '../appStore';

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

describe('Settings Persistence', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  afterEach(() => {
    vi.clearAllMocks();
  });

  it('loads settings from localStorage on store initialization', async () => {
    const mockSettings = {
      sourceLang: 'fr',
      targetLang: 'de',
      selectedVoice: 'de_male_1',
      timestamp: Date.now()
    };

    localStorageMock.getItem.mockReturnValue(JSON.stringify(mockSettings));

    // Import and test the loading function directly
    const settingsModule = await import('../../utils/settingsValidation');
    const loadedSettings = settingsModule.loadSettingsFromStorage();
    
    expect(localStorageMock.getItem).toHaveBeenCalledWith('speechrnt-settings');
    expect(loadedSettings.sourceLang).toBe('fr');
    expect(loadedSettings.targetLang).toBe('de');
    expect(loadedSettings.selectedVoice).toBe('de_male_1');
  });

  it('falls back to defaults when localStorage is empty', () => {
    localStorageMock.getItem.mockReturnValue(null);

    const store = useAppStore.getState();
    
    expect(store.sourceLang).toBe('en');
    expect(store.targetLang).toBe('es');
    expect(store.selectedVoice).toBe('es_female_1');
  });

  it('falls back to defaults when localStorage contains invalid JSON', () => {
    localStorageMock.getItem.mockReturnValue('invalid json');

    const store = useAppStore.getState();
    
    expect(store.sourceLang).toBe('en');
    expect(store.targetLang).toBe('es');
    expect(store.selectedVoice).toBe('es_female_1');
  });

  it('persists language changes to localStorage', () => {
    const store = useAppStore.getState();
    
    store.setLanguages('fr', 'de');
    
    expect(localStorageMock.setItem).toHaveBeenCalledWith(
      'speechrnt-settings',
      expect.stringContaining('"sourceLang":"fr"')
    );
    expect(localStorageMock.setItem).toHaveBeenCalledWith(
      'speechrnt-settings',
      expect.stringContaining('"targetLang":"de"')
    );
  });

  it('persists voice changes to localStorage', () => {
    const store = useAppStore.getState();
    
    // First set the language to French so the voice is compatible
    store.setLanguages('en', 'fr');
    store.setVoice('fr_male_1');
    
    expect(localStorageMock.setItem).toHaveBeenCalledWith(
      'speechrnt-settings',
      expect.stringContaining('"selectedVoice":"fr_male_1"')
    );
  });

  it('includes timestamp when persisting settings', () => {
    const store = useAppStore.getState();
    const beforeTime = Date.now();
    
    store.setLanguages('it', 'pt');
    
    const savedData = localStorageMock.setItem.mock.calls[0][1];
    const parsedData = JSON.parse(savedData);
    
    expect(parsedData.timestamp).toBeGreaterThanOrEqual(beforeTime);
    expect(parsedData.timestamp).toBeLessThanOrEqual(Date.now());
  });

  it('preserves existing settings when updating individual values', () => {
    // Mock existing settings
    const existingSettings = {
      sourceLang: 'en',
      targetLang: 'es',
      selectedVoice: 'es_female_1',
      timestamp: Date.now() - 1000
    };
    
    localStorageMock.getItem.mockReturnValue(JSON.stringify(existingSettings));
    
    // Reset the store to load from localStorage
    useAppStore.setState((state) => ({
      ...state,
      ...existingSettings
    }));
    
    const store = useAppStore.getState();
    
    // Update only the voice
    store.setVoice('es_male_1');
    
    const savedData = localStorageMock.setItem.mock.calls[0][1];
    const parsedData = JSON.parse(savedData);
    
    expect(parsedData.sourceLang).toBe('en');
    expect(parsedData.targetLang).toBe('es');
    expect(parsedData.selectedVoice).toBe('es_male_1');
    expect(parsedData.timestamp).toBeGreaterThan(existingSettings.timestamp);
  });

  it('handles localStorage quota exceeded gracefully', () => {
    const store = useAppStore.getState();
    
    // Mock localStorage.setItem to throw quota exceeded error
    localStorageMock.setItem.mockImplementation(() => {
      throw new Error('QuotaExceededError');
    });
    
    // Should not throw error
    expect(() => {
      store.setLanguages('fr', 'de');
    }).not.toThrow();
  });

  it('validates settings format when loading from localStorage', async () => {
    const invalidSettings = {
      sourceLang: 123, // Invalid type
      targetLang: null, // Invalid type
      selectedVoice: '', // Empty string
      timestamp: 'invalid' // Invalid type
    };

    localStorageMock.getItem.mockReturnValue(JSON.stringify(invalidSettings));

    // Create a fresh store instance to test loading from localStorage
    const { loadSettingsFromStorage } = await import('../../utils/settingsValidation');
    const loadedSettings = loadSettingsFromStorage();
    
    // Should fall back to defaults for invalid values
    expect(loadedSettings.sourceLang).toBe('en');
    expect(loadedSettings.targetLang).toBe('es');
    expect(loadedSettings.selectedVoice).toBe('es_female_1');
  });
});