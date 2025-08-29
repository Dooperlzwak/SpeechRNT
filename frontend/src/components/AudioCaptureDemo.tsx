/**
 * AudioCaptureDemo - Demonstrates the audio capture functionality
 */

import React, { useState, useEffect } from 'react';
import { useAudio } from '../hooks/useAudio';

export const AudioCaptureDemo: React.FC = () => {
  const [audioDevices, setAudioDevices] = useState<MediaDeviceInfo[]>([]);
  const [selectedDevice, setSelectedDevice] = useState<string>('');
  const [audioDataCount, setAudioDataCount] = useState(0);
  const [permissionGranted, setPermissionGranted] = useState(false);

  const {
    isRecording,
    isInitialized,
    error,
    initialize,
    startRecording,
    stopRecording,
    setAudioDevice,
    getSelectedDeviceId,
    isSupported,
    getAudioDevices,
    requestMicrophonePermission,
  } = useAudio(
    {
      sampleRate: 16000,
      channels: 1,
      bitsPerSample: 16,
      chunkSize: 1024,
      autoInitialize: false,
    },
    (audioData: ArrayBuffer) => {
      // Count audio data chunks received
      setAudioDataCount(prev => prev + 1);
      
      // Log audio data info for debugging
      const pcmData = new Int16Array(audioData);
      console.log(`Received audio chunk: ${pcmData.length} samples, first sample: ${pcmData[0]}`);
    }
  );

  useEffect(() => {
    loadAudioDevices();
  }, []);

  const loadAudioDevices = async () => {
    try {
      // Request permission first to get device labels
      const hasPermission = await requestMicrophonePermission();
      setPermissionGranted(hasPermission);
      
      if (hasPermission) {
        const devices = await getAudioDevices();
        setAudioDevices(devices);
        
        // Select first device by default
        if (devices.length > 0 && !selectedDevice) {
          setSelectedDevice(devices[0].deviceId);
        }
      }
    } catch (err) {
      console.error('Failed to load audio devices:', err);
    }
  };

  const handleInitialize = async () => {
    try {
      await initialize(selectedDevice || undefined);
    } catch (err) {
      console.error('Failed to initialize audio:', err);
    }
  };

  const handleStartRecording = async () => {
    try {
      if (!isInitialized) {
        await handleInitialize();
      }
      await startRecording();
      setAudioDataCount(0); // Reset counter
    } catch (err) {
      console.error('Failed to start recording:', err);
    }
  };

  const handleStopRecording = () => {
    stopRecording();
  };

  const handleDeviceChange = async (deviceId: string) => {
    setSelectedDevice(deviceId);
    
    if (isInitialized) {
      try {
        await setAudioDevice(deviceId);
      } catch (err) {
        console.error('Failed to change audio device:', err);
      }
    }
  };

  if (!isSupported) {
    return (
      <div className="p-4 bg-red-100 border border-red-400 rounded">
        <h2 className="text-lg font-semibold text-red-800">Audio Not Supported</h2>
        <p className="text-red-700">
          Your browser does not support the required audio APIs (MediaRecorder and AudioContext).
        </p>
      </div>
    );
  }

  return (
    <div className="p-6 max-w-2xl mx-auto bg-white rounded-lg shadow-lg">
      <h2 className="text-2xl font-bold mb-6">Audio Capture Demo</h2>
      
      {/* Permission Status */}
      <div className="mb-4">
        <div className={`inline-flex items-center px-3 py-1 rounded-full text-sm ${
          permissionGranted 
            ? 'bg-green-100 text-green-800' 
            : 'bg-yellow-100 text-yellow-800'
        }`}>
          {permissionGranted ? '✓ Microphone Permission Granted' : '⚠ Microphone Permission Required'}
        </div>
      </div>

      {/* Device Selection */}
      {permissionGranted && audioDevices.length > 0 && (
        <div className="mb-4">
          <label className="block text-sm font-medium text-gray-700 mb-2">
            Select Audio Device:
          </label>
          <select
            value={selectedDevice}
            onChange={(e) => handleDeviceChange(e.target.value)}
            className="w-full p-2 border border-gray-300 rounded-md focus:ring-2 focus:ring-blue-500 focus:border-blue-500"
            disabled={isRecording}
          >
            {audioDevices.map((device) => (
              <option key={device.deviceId} value={device.deviceId}>
                {device.label || `Device ${device.deviceId}`}
              </option>
            ))}
          </select>
        </div>
      )}

      {/* Status Display */}
      <div className="mb-4 space-y-2">
        <div className="flex items-center space-x-4">
          <span className="text-sm font-medium">Status:</span>
          <div className={`inline-flex items-center px-2 py-1 rounded text-sm ${
            isRecording 
              ? 'bg-red-100 text-red-800' 
              : isInitialized 
                ? 'bg-green-100 text-green-800'
                : 'bg-gray-100 text-gray-800'
          }`}>
            {isRecording ? '🔴 Recording' : isInitialized ? '✓ Ready' : '⚪ Not Initialized'}
          </div>
        </div>
        
        <div className="flex items-center space-x-4">
          <span className="text-sm font-medium">Audio Chunks Received:</span>
          <span className="text-lg font-mono">{audioDataCount}</span>
        </div>

        {getSelectedDeviceId() && (
          <div className="flex items-center space-x-4">
            <span className="text-sm font-medium">Selected Device:</span>
            <span className="text-sm text-gray-600">{getSelectedDeviceId()}</span>
          </div>
        )}
      </div>

      {/* Controls */}
      <div className="flex space-x-4 mb-4">
        {!permissionGranted && (
          <button
            onClick={loadAudioDevices}
            className="px-4 py-2 bg-blue-500 text-white rounded hover:bg-blue-600 focus:ring-2 focus:ring-blue-500"
          >
            Request Permission
          </button>
        )}
        
        {permissionGranted && !isInitialized && (
          <button
            onClick={handleInitialize}
            className="px-4 py-2 bg-green-500 text-white rounded hover:bg-green-600 focus:ring-2 focus:ring-green-500"
          >
            Initialize Audio
          </button>
        )}
        
        {isInitialized && !isRecording && (
          <button
            onClick={handleStartRecording}
            className="px-4 py-2 bg-red-500 text-white rounded hover:bg-red-600 focus:ring-2 focus:ring-red-500"
          >
            Start Recording
          </button>
        )}
        
        {isRecording && (
          <button
            onClick={handleStopRecording}
            className="px-4 py-2 bg-gray-500 text-white rounded hover:bg-gray-600 focus:ring-2 focus:ring-gray-500"
          >
            Stop Recording
          </button>
        )}
      </div>

      {/* Error Display */}
      {error && (
        <div className="p-4 bg-red-100 border border-red-400 rounded">
          <h3 className="text-lg font-semibold text-red-800">Error</h3>
          <p className="text-red-700">{error.message}</p>
        </div>
      )}

      {/* Technical Info */}
      <div className="mt-6 p-4 bg-gray-50 rounded">
        <h3 className="text-lg font-semibold mb-2">Audio Configuration</h3>
        <ul className="text-sm text-gray-600 space-y-1">
          <li>Sample Rate: 16,000 Hz (16kHz)</li>
          <li>Channels: 1 (Mono)</li>
          <li>Bit Depth: 16-bit PCM</li>
          <li>Chunk Size: 1,024 samples</li>
          <li>Echo Cancellation: Enabled</li>
          <li>Noise Suppression: Enabled</li>
          <li>Auto Gain Control: Enabled</li>
        </ul>
      </div>
    </div>
  );
};