/**
 * useConfigurationSync - React hook for synchronizing configuration with backend
 * 
 * This hook manages the synchronization of user settings (language and voice configuration)
 * with the backend server. It provides debouncing for rapid changes, retry logic for
 * failed synchronization attempts, and comprehensive error handling.
 */

import { useCallback, useRef, useState, useEffect } from 'react';
import { type ClientMessage, type ConfigMessage } from '../types/messageProtocol';
import { useErrorHandler } from './useErrorHandler';

export interface ConfigurationSyncConfig {
  debounceDelay: number;
  maxRetryAttempts: number;
  retryDelay: number;
  syncTimeout: number;
}

export interface ConfigurationSyncReturn {
  syncLanguageSettings: (source: string, target: string) => Promise<void>;
  syncVoiceSettings: (voice: string) => Promise<void>;
  isSyncing: boolean;
  lastSyncError: Error | null;
  pendingSyncs: number;
  retryFailedSync: () => Promise<void>;
  clearSyncError: () => void;
  getSyncStats: () => {
    totalSyncs: number;
    successfulSyncs: number;
    failedSyncs: number;
    lastSyncTime: Date | null;
  };
}

interface PendingSync {
  id: string;
  type: 'language' | 'voice';
  data: any;
  attempts: number;
  timestamp: Date;
  resolve: (value: void) => void;
  reject: (error: Error) => void;
}

interface SyncStats {
  totalSyncs: number;
  successfulSyncs: number;
  failedSyncs: number;
  lastSyncTime: Date | null;
}

const DEFAULT_CONFIG: ConfigurationSyncConfig = {
  debounceDelay: 500, // 500ms debounce
  maxRetryAttempts: 3,
  retryDelay: 1000, // 1 second base delay
  syncTimeout: 10000, // 10 seconds timeout
};

export const useConfigurationSync = (
  sendMessage: (message: ClientMessage, options?: {
    priority?: 'low' | 'normal' | 'high';
    requiresAck?: boolean;
    maxAttempts?: number;
  }) => string,
  onError: (error: Error) => void,
  config: Partial<ConfigurationSyncConfig> = {}
): ConfigurationSyncReturn => {
  // Configuration with defaults
  const syncConfig = { ...DEFAULT_CONFIG, ...config };
  
  // State management
  const [isSyncing, setIsSyncing] = useState(false);
  const [lastSyncError, setLastSyncError] = useState<Error | null>(null);
  const [pendingSyncs, setPendingSyncs] = useState(0);
  const [syncStats, setSyncStats] = useState<SyncStats>({
    totalSyncs: 0,
    successfulSyncs: 0,
    failedSyncs: 0,
    lastSyncTime: null,
  });

  // Refs for managing debouncing and retries
  const debounceTimerRef = useRef<number | null>(null);
  const pendingSyncQueueRef = useRef<Map<string, PendingSync>>(new Map());
  const currentConfigRef = useRef<{ sourceLang?: string; targetLang?: string; voice?: string }>({});
  const retryTimeoutRef = useRef<number | null>(null);
  const lastFailedSyncRef = useRef<PendingSync | null>(null);

  // Error handler
  const { handleConfigurationError } = useErrorHandler();

  // Generate unique sync ID
  const generateSyncId = useCallback((): string => {
    return `sync_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
  }, []);

  // Update sync statistics
  const updateSyncStats = useCallback((success: boolean) => {
    setSyncStats(prev => ({
      totalSyncs: prev.totalSyncs + 1,
      successfulSyncs: success ? prev.successfulSyncs + 1 : prev.successfulSyncs,
      failedSyncs: success ? prev.failedSyncs : prev.failedSyncs + 1,
      lastSyncTime: new Date(),
    }));
  }, []);

  // Clear debounce timer
  const clearDebounceTimer = useCallback(() => {
    if (debounceTimerRef.current) {
      clearTimeout(debounceTimerRef.current);
      debounceTimerRef.current = null;
    }
  }, []);

  // Clear retry timeout
  const clearRetryTimeout = useCallback(() => {
    if (retryTimeoutRef.current) {
      clearTimeout(retryTimeoutRef.current);
      retryTimeoutRef.current = null;
    }
  }, []);

  // Execute sync operation
  const executeSyncOperation = useCallback(async (pendingSync: PendingSync): Promise<void> => {
    const { id, type, data, attempts } = pendingSync;
    
    try {
      setIsSyncing(true);
      setLastSyncError(null);

      // Create configuration message
      let configMessage: ConfigMessage;
      
      if (type === 'language') {
        configMessage = {
          type: 'config',
          data: {
            sourceLang: data.source,
            targetLang: data.target,
            voice: currentConfigRef.current.voice || 'default', // Include current voice
          },
        };
        
        // Update current config reference
        currentConfigRef.current.sourceLang = data.source;
        currentConfigRef.current.targetLang = data.target;
      } else if (type === 'voice') {
        configMessage = {
          type: 'config',
          data: {
            sourceLang: currentConfigRef.current.sourceLang || 'en',
            targetLang: currentConfigRef.current.targetLang || 'es',
            voice: data.voice,
          },
        };
        
        // Update current config reference
        currentConfigRef.current.voice = data.voice;
      } else {
        throw new Error(`Unknown sync type: ${type}`);
      }

      // Send message with enhanced protocol options
      const messageId = sendMessage(configMessage, {
        priority: 'normal',
        requiresAck: true, // Configuration changes should be acknowledged
        maxAttempts: 3
      });
      
      if (!messageId) {
        throw new Error('Failed to send configuration message - WebSocket not connected');
      }
      
      console.log(`Configuration sync message sent: ${messageId} (${type})`);
      
      // Store message ID for potential tracking
      (pendingSync as any).messageId = messageId;

      // Wait for confirmation or timeout
      await new Promise<void>((resolve, reject) => {
        const timeout = setTimeout(() => {
          reject(new Error(`Configuration sync timeout after ${syncConfig.syncTimeout}ms`));
        }, syncConfig.syncTimeout);

        // For now, we'll assume success after a short delay since we don't have
        // explicit confirmation messages from the backend yet
        setTimeout(() => {
          clearTimeout(timeout);
          resolve();
        }, 100);
      });

      // Success - resolve the pending sync
      pendingSync.resolve();
      pendingSyncQueueRef.current.delete(id);
      setPendingSyncs(prev => prev - 1);
      updateSyncStats(true);
      
      console.log(`Configuration sync successful: ${type}`, data);

    } catch (error) {
      const syncError = error instanceof Error ? error : new Error('Unknown sync error');
      
      // Update attempts
      pendingSync.attempts = attempts + 1;
      
      if (pendingSync.attempts >= syncConfig.maxRetryAttempts) {
        // Max attempts reached - fail the sync
        pendingSync.reject(syncError);
        pendingSyncQueueRef.current.delete(id);
        setPendingSyncs(prev => prev - 1);
        updateSyncStats(false);
        
        // Store for potential manual retry
        lastFailedSyncRef.current = pendingSync;
        
        // Set error state
        setLastSyncError(syncError);
        
        // Handle error through error handler
        handleConfigurationError(`Failed to sync ${type} configuration: ${syncError.message}`);
        onError(syncError);
        
        console.error(`Configuration sync failed after ${syncConfig.maxRetryAttempts} attempts:`, syncError);
      } else {
        // Schedule retry with exponential backoff
        const retryDelay = syncConfig.retryDelay * Math.pow(2, pendingSync.attempts - 1);
        
        console.warn(`Configuration sync attempt ${pendingSync.attempts} failed, retrying in ${retryDelay}ms:`, syncError);
        
        retryTimeoutRef.current = window.setTimeout(() => {
          executeSyncOperation(pendingSync);
        }, retryDelay);
      }
    } finally {
      setIsSyncing(false);
    }
  }, [sendMessage, syncConfig, updateSyncStats, handleConfigurationError, onError]);

  // Process pending sync queue
  const processPendingSyncs = useCallback(() => {
    const pendingSyncs = Array.from(pendingSyncQueueRef.current.values());
    
    if (pendingSyncs.length === 0) {
      return;
    }

    // Process the oldest pending sync
    const oldestSync = pendingSyncs.reduce((oldest, current) => 
      current.timestamp < oldest.timestamp ? current : oldest
    );

    executeSyncOperation(oldestSync);
  }, [executeSyncOperation]);

  // Debounced sync execution
  const debouncedSync = useCallback(() => {
    clearDebounceTimer();
    
    debounceTimerRef.current = window.setTimeout(() => {
      processPendingSyncs();
    }, syncConfig.debounceDelay);
  }, [clearDebounceTimer, processPendingSyncs, syncConfig.debounceDelay]);

  // Sync language settings
  const syncLanguageSettings = useCallback(async (source: string, target: string): Promise<void> => {
    // Validate input
    if (!source || !target) {
      throw new Error('Source and target languages are required');
    }

    if (source === target) {
      throw new Error('Source and target languages cannot be the same');
    }

    return new Promise<void>((resolve, reject) => {
      const syncId = generateSyncId();
      
      // Remove any existing language sync from queue
      const existingLanguageSync = Array.from(pendingSyncQueueRef.current.entries())
        .find(([_, sync]) => sync.type === 'language');
      
      if (existingLanguageSync) {
        const [existingId, existingSync] = existingLanguageSync;
        pendingSyncQueueRef.current.delete(existingId);
        // Reject asynchronously to avoid unhandled promise rejection warnings
        setTimeout(() => {
          existingSync.reject(new Error('Superseded by newer language sync'));
        }, 0);
      }

      // Add new sync to queue
      const pendingSync: PendingSync = {
        id: syncId,
        type: 'language',
        data: { source, target },
        attempts: 0,
        timestamp: new Date(),
        resolve,
        reject,
      };

      pendingSyncQueueRef.current.set(syncId, pendingSync);
      
      // Update pending syncs count (net change is 0 if replacing, +1 if new)
      if (!existingLanguageSync) {
        setPendingSyncs(prev => prev + 1);
      }

      // Trigger debounced sync
      debouncedSync();
    });
  }, [generateSyncId, debouncedSync]);

  // Sync voice settings
  const syncVoiceSettings = useCallback(async (voice: string): Promise<void> => {
    // Validate input
    if (!voice) {
      throw new Error('Voice selection is required');
    }

    return new Promise<void>((resolve, reject) => {
      const syncId = generateSyncId();
      
      // Remove any existing voice sync from queue
      const existingVoiceSync = Array.from(pendingSyncQueueRef.current.entries())
        .find(([_, sync]) => sync.type === 'voice');
      
      if (existingVoiceSync) {
        const [existingId, existingSync] = existingVoiceSync;
        pendingSyncQueueRef.current.delete(existingId);
        // Reject asynchronously to avoid unhandled promise rejection warnings
        setTimeout(() => {
          existingSync.reject(new Error('Superseded by newer voice sync'));
        }, 0);
      }

      // Add new sync to queue
      const pendingSync: PendingSync = {
        id: syncId,
        type: 'voice',
        data: { voice },
        attempts: 0,
        timestamp: new Date(),
        resolve,
        reject,
      };

      pendingSyncQueueRef.current.set(syncId, pendingSync);
      
      // Update pending syncs count (net change is 0 if replacing, +1 if new)
      if (!existingVoiceSync) {
        setPendingSyncs(prev => prev + 1);
      }

      // Trigger debounced sync
      debouncedSync();
    });
  }, [generateSyncId, debouncedSync]);

  // Retry failed sync
  const retryFailedSync = useCallback(async (): Promise<void> => {
    if (!lastFailedSyncRef.current) {
      throw new Error('No failed sync to retry');
    }

    const failedSync = lastFailedSyncRef.current;
    
    // Reset attempts and add back to queue
    failedSync.attempts = 0;
    failedSync.timestamp = new Date();
    
    pendingSyncQueueRef.current.set(failedSync.id, failedSync);
    setPendingSyncs(prev => prev + 1);
    
    // Clear the failed sync reference
    lastFailedSyncRef.current = null;
    setLastSyncError(null);

    // Process immediately (no debounce for retries)
    await executeSyncOperation(failedSync);
  }, [executeSyncOperation]);

  // Clear sync error
  const clearSyncError = useCallback(() => {
    setLastSyncError(null);
    lastFailedSyncRef.current = null;
  }, []);

  // Get sync statistics
  const getSyncStats = useCallback(() => {
    return { ...syncStats };
  }, [syncStats]);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      clearDebounceTimer();
      clearRetryTimeout();
      
      // Reject all pending syncs asynchronously to avoid unhandled promise rejection warnings
      const pendingSyncs = Array.from(pendingSyncQueueRef.current.values());
      pendingSyncQueueRef.current.clear();
      
      setTimeout(() => {
        pendingSyncs.forEach(pendingSync => {
          pendingSync.reject(new Error('Component unmounted'));
        });
      }, 0);
    };
  }, [clearDebounceTimer, clearRetryTimeout]);

  return {
    syncLanguageSettings,
    syncVoiceSettings,
    isSyncing,
    lastSyncError,
    pendingSyncs,
    retryFailedSync,
    clearSyncError,
    getSyncStats,
  };
};