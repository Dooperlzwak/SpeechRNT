/**
 * Example component demonstrating useAudioIntegration hook usage
 * This shows how to integrate audio capture with WebSocket binary message sending
 */

import React, { useState, useCallback } from 'react';
import { useAudioIntegration } from '../hooks/useAudioIntegration';

interface AudioIntegrationExampleProps {
  onAudioData?: (data: ArrayBuffer) => void;
  webSocketSendBinary?: (data: ArrayBuffer) => boolean;
}

export const AudioIntegrationExample: React.FC<AudioIntegrationExampleProps> = ({
  onAudioData,
  webSocketSendBinary,
}) => {
  const [selectedDeviceId, setSelectedDeviceId] = useState<string>('');
  const [devices, setDevices] = useState<MediaDeviceInfo[]>([]);

  // Audio integration configuration
  const audioConfig = {
    sampleRate: 16000,
    channels: 1,
    bitsPerSample: 16,
    chunkSize: 1024,
    autoInitialize: true,
    deviceSelectionEnabled: true,
    permissionRetryAttempts: 3,
    deviceSwitchDelay: 500,
  };

  // Handle audio data - integrate with WebSocket binary sending
  const handleAudioData = useCallback((data: ArrayBuffer) => {
    // Send to WebSocket if available
    if (webSocketSendBinary) {
      const sent = webSocketSendBinary(data);
      if (!sent) {
        console.warn('Failed to send audio data via WebSocket');
      }
    }
    
    // Call external handler if provided
    onAudioData?.(data);
  }, [onAudioData, webSocketSendBinary]);

  // Handle audio errors
  const handleAudioError = useCallback((error: Error) => {
    console.error('Audio integration error:', error);
  }, []);

  // Handle recording state changes
  const handleStateChange = useCallback((recording: boolean) => {
    console.log('Recording state changed:', recording);
  }, []);

  // Use the audio integration hook
  const {
    isInitialized,
    isRecording,
    error,
    initialize,
    startRecording,
    stopRecording,
    cleanup,
    getAudioDevices,
    setAudioDevice,
    refreshDeviceList,
    requestMicrophonePermission,
    isSupported,
    getAudioConfig,
    getAudioStats,
  } = useAudioIntegration(audioConfig, handleAudioData, handleAudioError, handleStateChange);

  // Load available devices
  const loadDevices = useCallback(async () => {
    try {
      const deviceList = await getAudioDevices();
      setDevices(deviceList);
      
      // Set default device if none selected
      if (!selectedDeviceId && deviceList.length > 0) {
        setSelectedDeviceId(deviceList[0].deviceId);
      }
    } catch (error) {
      console.error('Failed to load audio devices:', error);
    }
  }, [getAudioDevices, selectedDeviceId]);

  // Handle device selection
  const handleDeviceChange = useCallback(async (deviceId: string) => {
    try {
      setSelectedDeviceId(deviceId);
      await setAudioDevice(deviceId);
    } catch (error) {
      console.error('Failed to set audio device:', error);
    }
  }, [setAudioDevice]);

  // Handle recording toggle
  const handleRecordingToggle = useCallback(async () => {
    try {
      if (isRecording) {
        stopRecording();
      } else {
        if (!isInitialized) {
          await initialize(selectedDeviceId || undefined);
        }
        await startRecording();
      }
    } catch (error) {
      console.error('Failed to toggle recording:', error);
    }
  }, [isRecording, isInitialized, initialize, startRecording, stopRecording, selectedDeviceId]);

  // Handle permission request
  const handlePermissionRequest = useCallback(async () => {
    try {
      const granted = await requestMicrophonePermission();
      if (granted) {
        await loadDevices();
      }
    } catch (error) {
      console.error('Failed to request microphone permission:', error);
    }
  }, [requestMicrophonePermission, loadDevices]);

  // Get audio statistics for display
  const audioStats = getAudioStats();
  const audioConfig_display = getAudioConfig();

  if (!isSupported) {
    return (
      <div className="p-4 bg-red-100 border border-red-400 rounded">
        <h3 className="text-lg font-semibold text-red-800">Audio Not Supported</h3>
        <p className="text-red-700">
          Your browser does not support the required audio APIs for this feature.
        </p>
      </div>
    );
  }

  return (
    <div className="p-6 bg-white border border-gray-200 rounded-lg shadow-sm">
      <h2 className="text-xl font-semibold mb-4">Audio Integration Example</h2>
      
      {/* Error Display */}
      {error && (
        <div className="mb-4 p-3 bg-red-100 border border-red-400 rounded">
          <p className="text-red-700">Error: {error.message}</p>
        </div>
      )}

      {/* Status Display */}
      <div className="mb-4 p-3 bg-gray-100 rounded">
        <h3 className="font-semibold mb-2">Status</h3>
        <div className="grid grid-cols-2 gap-2 text-sm">
          <div>Initialized: <span className={isInitialized ? 'text-green-600' : 'text-red-600'}>
            {isInitialized ? 'Yes' : 'No'}
          </span></div>
          <div>Recording: <span className={isRecording ? 'text-green-600' : 'text-red-600'}>
            {isRecording ? 'Yes' : 'No'}
          </span></div>
          <div>Permission: <span className={audioStats.permissionGranted ? 'text-green-600' : 'text-red-600'}>
            {audioStats.permissionGranted ? 'Granted' : 'Not Granted'}
          </span></div>
          <div>Devices: {audioStats.availableDevices}</div>
        </div>
      </div>

      {/* Audio Configuration */}
      <div className="mb-4 p-3 bg-gray-100 rounded">
        <h3 className="font-semibold mb-2">Configuration</h3>
        <div className="grid grid-cols-2 gap-2 text-sm">
          <div>Sample Rate: {audioConfig_display.sampleRate}Hz</div>
          <div>Channels: {audioConfig_display.channels}</div>
          <div>Bits per Sample: {audioConfig_display.bitsPerSample}</div>
          <div>Chunk Size: {audioConfig_display.chunkSize}</div>
        </div>
      </div>

      {/* Device Selection */}
      <div className="mb-4">
        <h3 className="font-semibold mb-2">Audio Device</h3>
        <div className="flex gap-2 mb-2">
          <button
            onClick={loadDevices}
            className="px-3 py-1 bg-blue-500 text-white rounded hover:bg-blue-600"
          >
            Load Devices
          </button>
          <button
            onClick={refreshDeviceList}
            className="px-3 py-1 bg-gray-500 text-white rounded hover:bg-gray-600"
          >
            Refresh
          </button>
        </div>
        
        {devices.length > 0 && (
          <select
            value={selectedDeviceId}
            onChange={(e) => handleDeviceChange(e.target.value)}
            className="w-full p-2 border border-gray-300 rounded"
          >
            <option value="">Select a device...</option>
            {devices.map((device) => (
              <option key={device.deviceId} value={device.deviceId}>
                {device.label || `Device ${device.deviceId.slice(0, 8)}...`}
              </option>
            ))}
          </select>
        )}
        
        {audioStats.selectedDevice && (
          <p className="text-sm text-gray-600 mt-1">
            Current: {audioStats.selectedDevice.slice(0, 20)}...
          </p>
        )}
      </div>

      {/* Controls */}
      <div className="flex gap-2 mb-4">
        <button
          onClick={handlePermissionRequest}
          className="px-4 py-2 bg-yellow-500 text-white rounded hover:bg-yellow-600"
        >
          Request Permission
        </button>
        
        <button
          onClick={() => initialize(selectedDeviceId || undefined)}
          disabled={isInitialized}
          className="px-4 py-2 bg-green-500 text-white rounded hover:bg-green-600 disabled:bg-gray-400"
        >
          Initialize
        </button>
        
        <button
          onClick={handleRecordingToggle}
          disabled={!isInitialized}
          className={`px-4 py-2 text-white rounded ${
            isRecording 
              ? 'bg-red-500 hover:bg-red-600' 
              : 'bg-blue-500 hover:bg-blue-600'
          } disabled:bg-gray-400`}
        >
          {isRecording ? 'Stop Recording' : 'Start Recording'}
        </button>
        
        <button
          onClick={cleanup}
          className="px-4 py-2 bg-gray-500 text-white rounded hover:bg-gray-600"
        >
          Cleanup
        </button>
      </div>

      {/* Integration Info */}
      <div className="p-3 bg-blue-50 border border-blue-200 rounded">
        <h3 className="font-semibold mb-2 text-blue-800">Integration Features</h3>
        <ul className="text-sm text-blue-700 space-y-1">
          <li>✓ Session lifecycle management</li>
          <li>✓ Audio device enumeration and selection</li>
          <li>✓ WebSocket binary message integration</li>
          <li>✓ Microphone permission handling</li>
          <li>✓ Error recovery and retry logic</li>
          <li>✓ Device change event handling</li>
          <li>✓ Connection quality monitoring</li>
        </ul>
      </div>
    </div>
  );
};