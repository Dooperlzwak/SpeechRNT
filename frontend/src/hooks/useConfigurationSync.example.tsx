/**
 * Example usage of useConfigurationSync hook
 * 
 * This example demonstrates how to integrate the useConfigurationSync hook
 * with WebSocket communication for backend configuration synchronization.
 */

import React, { useState } from 'react';
import { useConfigurationSync } from './useConfigurationSync';
import { useWebSocketIntegration } from './useWebSocketIntegration';

const ConfigurationSyncExample: React.FC = () => {
  const [sourceLang, setSourceLang] = useState('en');
  const [targetLang, setTargetLang] = useState('es');
  const [selectedVoice, setSelectedVoice] = useState('default');

  // WebSocket integration for sending messages
  const {
    sendMessage,
    isConnected,
    connect,
    disconnect
  } = useWebSocketIntegration(
    {
      url: 'ws://localhost:8080',
      reconnectInterval: 2000,
      maxReconnectAttempts: 5,
      heartbeatInterval: 30000,
    },
    (message) => {
      console.log('Received message:', message);
    },
    (data) => {
      console.log('Received binary data:', data);
    },
    (connected) => {
      console.log('Connection status changed:', connected);
    },
    (error) => {
      console.error('WebSocket error:', error);
    }
  );

  // Configuration synchronization hook
  const {
    syncLanguageSettings,
    syncVoiceSettings,
    isSyncing,
    lastSyncError,
    pendingSyncs,
    retryFailedSync,
    clearSyncError,
    getSyncStats
  } = useConfigurationSync(
    sendMessage,
    (error) => {
      console.error('Configuration sync error:', error);
    },
    {
      debounceDelay: 500,
      maxRetryAttempts: 3,
      retryDelay: 1000,
      syncTimeout: 10000,
    }
  );

  // Handle language change
  const handleLanguageChange = async (source: string, target: string) => {
    setSourceLang(source);
    setTargetLang(target);
    
    try {
      await syncLanguageSettings(source, target);
      console.log('Language settings synchronized successfully');
    } catch (error) {
      console.error('Failed to sync language settings:', error);
    }
  };

  // Handle voice change
  const handleVoiceChange = async (voice: string) => {
    setSelectedVoice(voice);
    
    try {
      await syncVoiceSettings(voice);
      console.log('Voice settings synchronized successfully');
    } catch (error) {
      console.error('Failed to sync voice settings:', error);
    }
  };

  // Get sync statistics
  const syncStats = getSyncStats();

  return (
    <div className="p-6 max-w-md mx-auto bg-white rounded-lg shadow-md">
      <h2 className="text-2xl font-bold mb-4">Configuration Sync Example</h2>
      
      {/* Connection Status */}
      <div className="mb-4">
        <h3 className="text-lg font-semibold mb-2">Connection Status</h3>
        <div className="flex items-center gap-2">
          <div className={`w-3 h-3 rounded-full ${isConnected ? 'bg-green-500' : 'bg-red-500'}`} />
          <span>{isConnected ? 'Connected' : 'Disconnected'}</span>
          {!isConnected && (
            <button
              onClick={connect}
              className="ml-2 px-3 py-1 bg-blue-500 text-white rounded text-sm"
            >
              Connect
            </button>
          )}
          {isConnected && (
            <button
              onClick={disconnect}
              className="ml-2 px-3 py-1 bg-red-500 text-white rounded text-sm"
            >
              Disconnect
            </button>
          )}
        </div>
      </div>

      {/* Language Settings */}
      <div className="mb-4">
        <h3 className="text-lg font-semibold mb-2">Language Settings</h3>
        <div className="grid grid-cols-2 gap-2">
          <div>
            <label className="block text-sm font-medium mb-1">Source Language</label>
            <select
              value={sourceLang}
              onChange={(e) => handleLanguageChange(e.target.value, targetLang)}
              className="w-full p-2 border rounded"
              disabled={!isConnected}
            >
              <option value="en">English</option>
              <option value="es">Spanish</option>
              <option value="fr">French</option>
              <option value="de">German</option>
            </select>
          </div>
          <div>
            <label className="block text-sm font-medium mb-1">Target Language</label>
            <select
              value={targetLang}
              onChange={(e) => handleLanguageChange(sourceLang, e.target.value)}
              className="w-full p-2 border rounded"
              disabled={!isConnected}
            >
              <option value="en">English</option>
              <option value="es">Spanish</option>
              <option value="fr">French</option>
              <option value="de">German</option>
            </select>
          </div>
        </div>
      </div>

      {/* Voice Settings */}
      <div className="mb-4">
        <h3 className="text-lg font-semibold mb-2">Voice Settings</h3>
        <select
          value={selectedVoice}
          onChange={(e) => handleVoiceChange(e.target.value)}
          className="w-full p-2 border rounded"
          disabled={!isConnected}
        >
          <option value="default">Default Voice</option>
          <option value="female-voice-1">Female Voice 1</option>
          <option value="male-voice-1">Male Voice 1</option>
          <option value="female-voice-2">Female Voice 2</option>
        </select>
      </div>

      {/* Sync Status */}
      <div className="mb-4">
        <h3 className="text-lg font-semibold mb-2">Sync Status</h3>
        <div className="space-y-2">
          <div className="flex items-center gap-2">
            <span className="text-sm">Syncing:</span>
            <div className={`w-2 h-2 rounded-full ${isSyncing ? 'bg-yellow-500' : 'bg-gray-300'}`} />
            <span className="text-sm">{isSyncing ? 'Yes' : 'No'}</span>
          </div>
          <div className="flex items-center gap-2">
            <span className="text-sm">Pending Syncs:</span>
            <span className="text-sm font-mono">{pendingSyncs}</span>
          </div>
          {lastSyncError && (
            <div className="p-2 bg-red-100 border border-red-300 rounded">
              <div className="flex items-center justify-between">
                <span className="text-sm text-red-700">Error: {lastSyncError.message}</span>
                <div className="flex gap-1">
                  <button
                    onClick={retryFailedSync}
                    className="px-2 py-1 bg-red-500 text-white rounded text-xs"
                  >
                    Retry
                  </button>
                  <button
                    onClick={clearSyncError}
                    className="px-2 py-1 bg-gray-500 text-white rounded text-xs"
                  >
                    Clear
                  </button>
                </div>
              </div>
            </div>
          )}
        </div>
      </div>

      {/* Sync Statistics */}
      <div className="mb-4">
        <h3 className="text-lg font-semibold mb-2">Sync Statistics</h3>
        <div className="grid grid-cols-2 gap-2 text-sm">
          <div>Total Syncs: {syncStats.totalSyncs}</div>
          <div>Successful: {syncStats.successfulSyncs}</div>
          <div>Failed: {syncStats.failedSyncs}</div>
          <div>
            Last Sync: {syncStats.lastSyncTime ? 
              syncStats.lastSyncTime.toLocaleTimeString() : 
              'Never'
            }
          </div>
        </div>
      </div>

      {/* Test Buttons */}
      <div className="space-y-2">
        <h3 className="text-lg font-semibold">Test Actions</h3>
        <div className="flex gap-2">
          <button
            onClick={() => handleLanguageChange('en', 'fr')}
            className="px-3 py-1 bg-blue-500 text-white rounded text-sm"
            disabled={!isConnected}
          >
            EN → FR
          </button>
          <button
            onClick={() => handleLanguageChange('es', 'de')}
            className="px-3 py-1 bg-blue-500 text-white rounded text-sm"
            disabled={!isConnected}
          >
            ES → DE
          </button>
          <button
            onClick={() => handleVoiceChange('female-voice-1')}
            className="px-3 py-1 bg-green-500 text-white rounded text-sm"
            disabled={!isConnected}
          >
            Female Voice
          </button>
        </div>
      </div>
    </div>
  );
};

export default ConfigurationSyncExample;