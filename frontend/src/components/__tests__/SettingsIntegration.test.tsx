import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { useAppStore } from '../../store/appStore';

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

// Simple test component that uses the store
function SettingsTestComponent() {
  const { sourceLang, targetLang, selectedVoice, setLanguages, setVoice } = useAppStore();

  return (
    <div>
      <div data-testid="source-lang">{sourceLang}</div>
      <div data-testid="target-lang">{targetLang}</div>
      <div data-testid="selected-voice">{selectedVoice}</div>
      <button 
        data-testid="change-languages"
        onClick={() => setLanguages('fr', 'de')}
      >
        Change Languages
      </button>
      <button 
        data-testid="change-voice"
        onClick={() => setVoice('de_male_1')}
      >
        Change Voice
      </button>
    </div>
  );
}

describe('Settings Integration', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    // Reset store state
    useAppStore.setState({
      sourceLang: 'en',
      targetLang: 'es',
      selectedVoice: 'es_female_1'
    });
  });

  it('should display current settings', () => {
    render(<SettingsTestComponent />);
    
    expect(screen.getByTestId('source-lang')).toHaveTextContent('en');
    expect(screen.getByTestId('target-lang')).toHaveTextContent('es');
    expect(screen.getByTestId('selected-voice')).toHaveTextContent('es_female_1');
  });

  it('should update languages and persist to localStorage', () => {
    render(<SettingsTestComponent />);
    
    const changeLanguagesButton = screen.getByTestId('change-languages');
    fireEvent.click(changeLanguagesButton);
    
    expect(screen.getByTestId('source-lang')).toHaveTextContent('fr');
    expect(screen.getByTestId('target-lang')).toHaveTextContent('de');
    
    // Should persist to localStorage
    expect(localStorageMock.setItem).toHaveBeenCalledWith(
      'speechrnt-settings',
      expect.stringContaining('"sourceLang":"fr"')
    );
    expect(localStorageMock.setItem).toHaveBeenCalledWith(
      'speechrnt-settings',
      expect.stringContaining('"targetLang":"de"')
    );
  });

  it('should validate voice compatibility and auto-correct when needed', () => {
    render(<SettingsTestComponent />);
    
    // Try to set a German voice for Spanish target language
    const changeVoiceButton = screen.getByTestId('change-voice');
    fireEvent.click(changeVoiceButton);
    
    // Voice should be auto-corrected to a compatible Spanish voice
    expect(screen.getByTestId('selected-voice')).toHaveTextContent('es_female_1');
    
    // Should persist the corrected voice to localStorage
    expect(localStorageMock.setItem).toHaveBeenCalledWith(
      'speechrnt-settings',
      expect.stringContaining('"selectedVoice":"es_female_1"')
    );
  });

  it('should include timestamp when persisting settings', () => {
    render(<SettingsTestComponent />);
    
    const beforeTime = Date.now();
    const changeLanguagesButton = screen.getByTestId('change-languages');
    fireEvent.click(changeLanguagesButton);
    
    const savedData = localStorageMock.setItem.mock.calls[0][1];
    const parsedData = JSON.parse(savedData);
    
    expect(parsedData.timestamp).toBeGreaterThanOrEqual(beforeTime);
    expect(parsedData.timestamp).toBeLessThanOrEqual(Date.now());
  });

  it('should validate settings when changing languages', () => {
    render(<SettingsTestComponent />);
    
    // Change to French -> German, voice should be auto-corrected to German voice
    const changeLanguagesButton = screen.getByTestId('change-languages');
    fireEvent.click(changeLanguagesButton);
    
    const savedData = localStorageMock.setItem.mock.calls[0][1];
    const parsedData = JSON.parse(savedData);
    
    // Voice should be corrected to a German voice since target language is German
    expect(parsedData.selectedVoice).toBe('de_female_1'); // Auto-corrected
  });
});