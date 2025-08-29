/**
 * Tests for useConfigurationSync hook
 */

import { renderHook, act, waitFor } from '@testing-library/react';
import { vi, describe, it, expect, beforeEach, afterEach } from 'vitest';
import { useConfigurationSync } from '../useConfigurationSync';
import { ClientMessage } from '../../types/messageProtocol';

// Mock the useErrorHandler hook
vi.mock('../useErrorHandler', () => ({
  useErrorHandler: () => ({
    handleConfigurationError: vi.fn(),
  }),
}));

describe('useConfigurationSync', () => {
  let mockSendMessage: vi.MockedFunction<(message: ClientMessage) => boolean>;
  let mockOnError: vi.MockedFunction<(error: Error) => void>;

  beforeEach(() => {
    mockSendMessage = vi.fn().mockReturnValue(true);
    mockOnError = vi.fn();
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.clearAllMocks();
  });

  describe('Language Settings Synchronization', () => {
    it('should sync language settings successfully', async () => {
      const { result } = renderHook(() =>
        useConfigurationSync(mockSendMessage, mockOnError)
      );

      let syncPromise: Promise<void>;
      
      act(() => {
        syncPromise = result.current.syncLanguageSettings('en', 'es');
      });

      // Fast-forward debounce delay
      act(() => {
        vi.advanceTimersByTime(500);
      });

      // Fast-forward sync execution
      act(() => {
        vi.advanceTimersByTime(100);
      });

      await act(async () => {
        await syncPromise;
      });

      expect(mockSendMessage).toHaveBeenCalledWith({
        type: 'config',
        data: {
          sourceLang: 'en',
          targetLang: 'es',
          voice: 'default',
        },
      });

      expect(result.current.lastSyncError).toBeNull();
      expect(result.current.pendingSyncs).toBe(0);
    });

    it('should debounce rapid language changes', async () => {
      const { result } = renderHook(() =>
        useConfigurationSync(mockSendMessage, mockOnError, { debounceDelay: 300 })
      );

      // Make multiple rapid changes
      act(() => {
        result.current.syncLanguageSettings('en', 'es');
        result.current.syncLanguageSettings('en', 'fr');
        result.current.syncLanguageSettings('en', 'de');
      });

      // Fast-forward less than debounce delay
      act(() => {
        vi.advanceTimersByTime(200);
      });

      expect(mockSendMessage).not.toHaveBeenCalled();

      // Fast-forward past debounce delay
      act(() => {
        vi.advanceTimersByTime(200);
      });

      // Fast-forward sync execution
      act(() => {
        vi.advanceTimersByTime(100);
      });

      // Should only send the last configuration
      expect(mockSendMessage).toHaveBeenCalledTimes(1);
      expect(mockSendMessage).toHaveBeenCalledWith({
        type: 'config',
        data: {
          sourceLang: 'en',
          targetLang: 'de',
          voice: 'default',
        },
      });
    });

    it('should validate language settings input', async () => {
      const { result } = renderHook(() =>
        useConfigurationSync(mockSendMessage, mockOnError)
      );

      // Test empty source language
      await expect(
        result.current.syncLanguageSettings('', 'es')
      ).rejects.toThrow('Source and target languages are required');

      // Test empty target language
      await expect(
        result.current.syncLanguageSettings('en', '')
      ).rejects.toThrow('Source and target languages are required');

      // Test same source and target
      await expect(
        result.current.syncLanguageSettings('en', 'en')
      ).rejects.toThrow('Source and target languages cannot be the same');

      expect(mockSendMessage).not.toHaveBeenCalled();
    });
  });

  describe('Voice Settings Synchronization', () => {
    it('should sync voice settings successfully', async () => {
      const { result } = renderHook(() =>
        useConfigurationSync(mockSendMessage, mockOnError)
      );

      let syncPromise: Promise<void>;
      
      act(() => {
        syncPromise = result.current.syncVoiceSettings('female-voice-1');
      });

      // Fast-forward debounce delay
      act(() => {
        vi.advanceTimersByTime(500);
      });

      // Fast-forward sync execution
      act(() => {
        vi.advanceTimersByTime(100);
      });

      await act(async () => {
        await syncPromise;
      });

      expect(mockSendMessage).toHaveBeenCalledWith({
        type: 'config',
        data: {
          sourceLang: 'en',
          targetLang: 'es',
          voice: 'female-voice-1',
        },
      });

      expect(result.current.lastSyncError).toBeNull();
      expect(result.current.pendingSyncs).toBe(0);
    });

    it('should validate voice settings input', async () => {
      const { result } = renderHook(() =>
        useConfigurationSync(mockSendMessage, mockOnError)
      );

      // Test empty voice
      await expect(
        result.current.syncVoiceSettings('')
      ).rejects.toThrow('Voice selection is required');

      expect(mockSendMessage).not.toHaveBeenCalled();
    });

    it('should debounce rapid voice changes', async () => {
      const { result } = renderHook(() =>
        useConfigurationSync(mockSendMessage, mockOnError, { debounceDelay: 300 })
      );

      // Make multiple rapid changes
      act(() => {
        result.current.syncVoiceSettings('voice-1');
        result.current.syncVoiceSettings('voice-2');
        result.current.syncVoiceSettings('voice-3');
      });

      // Fast-forward less than debounce delay
      act(() => {
        vi.advanceTimersByTime(200);
      });

      expect(mockSendMessage).not.toHaveBeenCalled();

      // Fast-forward past debounce delay
      act(() => {
        vi.advanceTimersByTime(200);
      });

      // Fast-forward sync execution
      act(() => {
        vi.advanceTimersByTime(100);
      });

      // Should only send the last configuration
      expect(mockSendMessage).toHaveBeenCalledTimes(1);
      expect(mockSendMessage).toHaveBeenCalledWith({
        type: 'config',
        data: {
          sourceLang: 'en',
          targetLang: 'es',
          voice: 'voice-3',
        },
      });
    });
  });

  describe('Error Handling and Retry Logic', () => {
    it('should handle WebSocket send failure', async () => {
      mockSendMessage.mockReturnValue(false);

      const { result } = renderHook(() =>
        useConfigurationSync(mockSendMessage, mockOnError, { maxRetryAttempts: 2 })
      );

      let syncPromise: Promise<void>;
      let syncError: Error | undefined;
      
      act(() => {
        syncPromise = result.current.syncLanguageSettings('en', 'es').catch(err => {
          syncError = err;
        });
      });

      // Fast-forward debounce delay
      act(() => {
        vi.advanceTimersByTime(500);
      });

      // Fast-forward sync execution and retries
      act(() => {
        vi.advanceTimersByTime(100);
      });

      // Fast-forward first retry
      act(() => {
        vi.advanceTimersByTime(1000);
      });

      act(() => {
        vi.advanceTimersByTime(100);
      });

      // Fast-forward second retry
      act(() => {
        vi.advanceTimersByTime(2000);
      });

      act(() => {
        vi.advanceTimersByTime(100);
      });

      await act(async () => {
        await syncPromise;
      });

      expect(syncError).toBeInstanceOf(Error);
      expect(syncError?.message).toContain('Failed to send configuration message');
      expect(mockOnError).toHaveBeenCalledWith(syncError);
      expect(result.current.lastSyncError).toBe(syncError);
    });

    it('should retry failed syncs with exponential backoff', async () => {
      let callCount = 0;
      mockSendMessage.mockImplementation(() => {
        callCount++;
        return callCount >= 3; // Succeed on third attempt
      });

      const { result } = renderHook(() =>
        useConfigurationSync(mockSendMessage, mockOnError, { 
          maxRetryAttempts: 3,
          retryDelay: 1000 
        })
      );

      let syncPromise: Promise<void>;
      
      act(() => {
        syncPromise = result.current.syncLanguageSettings('en', 'es');
      });

      // Fast-forward debounce delay
      act(() => {
        vi.advanceTimersByTime(500);
      });

      // First attempt (fails)
      act(() => {
        vi.advanceTimersByTime(100);
      });

      expect(callCount).toBe(1);

      // First retry after 1 second (fails)
      act(() => {
        vi.advanceTimersByTime(1000);
      });

      act(() => {
        vi.advanceTimersByTime(100);
      });

      expect(callCount).toBe(2);

      // Second retry after 2 seconds (succeeds)
      act(() => {
        vi.advanceTimersByTime(2000);
      });

      act(() => {
        vi.advanceTimersByTime(100);
      });

      expect(callCount).toBe(3);

      await act(async () => {
        await syncPromise;
      });

      expect(result.current.lastSyncError).toBeNull();
      expect(result.current.pendingSyncs).toBe(0);
    });

    it('should allow manual retry of failed sync', async () => {
      mockSendMessage.mockReturnValue(false);

      const { result } = renderHook(() =>
        useConfigurationSync(mockSendMessage, mockOnError, { maxRetryAttempts: 1 })
      );

      // Initial sync that will fail
      act(() => {
        result.current.syncLanguageSettings('en', 'es').catch(() => {});
      });

      // Fast-forward to complete failed sync
      act(() => {
        vi.advanceTimersByTime(500 + 100 + 1000 + 100);
      });

      expect(result.current.lastSyncError).not.toBeNull();

      // Now make sendMessage succeed
      mockSendMessage.mockReturnValue(true);

      // Retry the failed sync
      let retryPromise: Promise<void>;
      act(() => {
        retryPromise = result.current.retryFailedSync();
      });

      act(() => {
        vi.advanceTimersByTime(100);
      });

      await act(async () => {
        await retryPromise;
      });

      expect(result.current.lastSyncError).toBeNull();
      expect(result.current.pendingSyncs).toBe(0);
    });

    it('should clear sync error', () => {
      const { result } = renderHook(() =>
        useConfigurationSync(mockSendMessage, mockOnError)
      );

      // Manually set an error state (simulating a failed sync)
      act(() => {
        result.current.syncLanguageSettings('en', 'es').catch(() => {});
      });

      // Clear the error
      act(() => {
        result.current.clearSyncError();
      });

      expect(result.current.lastSyncError).toBeNull();
    });
  });

  describe('State Management', () => {
    it('should track syncing state correctly', async () => {
      const { result } = renderHook(() =>
        useConfigurationSync(mockSendMessage, mockOnError)
      );

      expect(result.current.isSyncing).toBe(false);
      expect(result.current.pendingSyncs).toBe(0);

      let syncPromise: Promise<void>;
      
      act(() => {
        syncPromise = result.current.syncLanguageSettings('en', 'es');
      });

      expect(result.current.pendingSyncs).toBe(1);

      // Fast-forward debounce delay
      act(() => {
        vi.advanceTimersByTime(500);
      });

      expect(result.current.isSyncing).toBe(true);

      // Fast-forward sync execution
      act(() => {
        vi.advanceTimersByTime(100);
      });

      await act(async () => {
        await syncPromise;
      });

      expect(result.current.isSyncing).toBe(false);
      expect(result.current.pendingSyncs).toBe(0);
    });

    it('should provide sync statistics', async () => {
      const { result } = renderHook(() =>
        useConfigurationSync(mockSendMessage, mockOnError)
      );

      const initialStats = result.current.getSyncStats();
      expect(initialStats.totalSyncs).toBe(0);
      expect(initialStats.successfulSyncs).toBe(0);
      expect(initialStats.failedSyncs).toBe(0);
      expect(initialStats.lastSyncTime).toBeNull();

      // Successful sync
      let syncPromise: Promise<void>;
      
      act(() => {
        syncPromise = result.current.syncLanguageSettings('en', 'es');
      });

      act(() => {
        vi.advanceTimersByTime(500 + 100);
      });

      await act(async () => {
        await syncPromise;
      });

      const successStats = result.current.getSyncStats();
      expect(successStats.totalSyncs).toBe(1);
      expect(successStats.successfulSyncs).toBe(1);
      expect(successStats.failedSyncs).toBe(0);
      expect(successStats.lastSyncTime).toBeInstanceOf(Date);

      // Failed sync
      mockSendMessage.mockReturnValue(false);
      
      act(() => {
        result.current.syncVoiceSettings('voice-1').catch(() => {});
      });

      act(() => {
        vi.advanceTimersByTime(500 + 100 + 1000 + 100 + 2000 + 100 + 4000 + 100);
      });

      const failedStats = result.current.getSyncStats();
      expect(failedStats.totalSyncs).toBe(2);
      expect(failedStats.successfulSyncs).toBe(1);
      expect(failedStats.failedSyncs).toBe(1);
    });
  });

  describe('Configuration Coordination', () => {
    it('should coordinate language and voice settings', async () => {
      const { result } = renderHook(() =>
        useConfigurationSync(mockSendMessage, mockOnError)
      );

      // Set language first
      let langPromise: Promise<void>;
      
      act(() => {
        langPromise = result.current.syncLanguageSettings('en', 'es');
      });

      act(() => {
        vi.advanceTimersByTime(500 + 100);
      });

      await act(async () => {
        await langPromise;
      });

      // Then set voice - should include the previously set language
      let voicePromise: Promise<void>;
      
      act(() => {
        voicePromise = result.current.syncVoiceSettings('female-voice');
      });

      act(() => {
        vi.advanceTimersByTime(500 + 100);
      });

      await act(async () => {
        await voicePromise;
      });

      expect(mockSendMessage).toHaveBeenCalledTimes(2);
      
      // First call should set language with default voice
      expect(mockSendMessage).toHaveBeenNthCalledWith(1, {
        type: 'config',
        data: {
          sourceLang: 'en',
          targetLang: 'es',
          voice: 'default',
        },
      });

      // Second call should set voice with previously set language
      expect(mockSendMessage).toHaveBeenNthCalledWith(2, {
        type: 'config',
        data: {
          sourceLang: 'en',
          targetLang: 'es',
          voice: 'female-voice',
        },
      });
    });

    it('should only send the latest configuration when multiple syncs are queued', async () => {
      const { result } = renderHook(() =>
        useConfigurationSync(mockSendMessage, mockOnError, { debounceDelay: 1000 })
      );

      // Start multiple language syncs rapidly - only the last one should be sent
      result.current.syncLanguageSettings('en', 'es').catch(() => {});
      result.current.syncLanguageSettings('en', 'fr').catch(() => {});
      const finalPromise = result.current.syncLanguageSettings('en', 'de');

      // Process the final sync
      act(() => {
        vi.advanceTimersByTime(1000 + 100);
      });

      await act(async () => {
        await finalPromise;
      });

      // Should only have sent the final configuration
      expect(mockSendMessage).toHaveBeenCalledTimes(1);
      expect(mockSendMessage).toHaveBeenCalledWith({
        type: 'config',
        data: {
          sourceLang: 'en',
          targetLang: 'de',
          voice: 'default',
        },
      });
    });
  });
});