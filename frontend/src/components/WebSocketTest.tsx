/**
 * WebSocketTest - Test component for WebSocket functionality
 */

import React, { useState, useCallback } from 'react';
import { useWebSocket } from '../hooks/useWebSocket';
import { useAudio } from '../hooks/useAudio';
import { useAudioPlayback } from '../hooks/useAudioPlayback';
import { type ServerMessage, type ClientMessage, type AudioStartMessage, type AudioDataMessage, type AudioEndMessage } from '../types/messageProtocol';

const WebSocketTest: React.FC = () => {
  const [messages, setMessages] = useState<ServerMessage[]>([]);
  const [sourceLang, setSourceLang] = useState('en');
  const [targetLang, setTargetLang] = useState('es');
  const [voice, setVoice] = useState('female_voice_1');
  const [playbackVolume, setPlaybackVolume] = useState(0.8);

  const handleMessage = useCallback((message: ServerMessage) => {
    console.log('Received message:', message);
    setMessages(prev => [...prev, message]);

    // Handle audio-related messages
    if (message.type === 'audio_start') {
      const audioMsg = message as AudioStartMessage;
      handleAudioStart(
        audioMsg.data.utteranceId,
        audioMsg.data.duration,
        audioMsg.data.format,
        audioMsg.data.sampleRate,
        audioMsg.data.channels
      );
    } else if (message.type === 'audio_end') {
      const audioMsg = message as AudioEndMessage;
      handleAudioEnd(audioMsg.data.utteranceId);
    }
  }, []);

  const handleBinaryMessage = useCallback((data: ArrayBuffer, messageType?: string) => {
    console.log('Received binary data:', data.byteLength, 'bytes', 'type:', messageType);
    
    // Handle audio data for playback
    if (messageType === 'audio_data') {
      // Find the most recent audio_data message to get utterance info
      const lastAudioDataMsg = [...messages].reverse().find(msg => msg.type === 'audio_data') as AudioDataMessage;
      if (lastAudioDataMsg) {
        handleAudioPlayback(data);
      }
    }
  }, [messages]);

  // This will be defined after the useWebSocket hook

  const handleAudioPlayback = useCallback((data: ArrayBuffer) => {
    // Handle audio data for playback
    console.log('Received audio data for playback:', data.byteLength, 'bytes');
  }, []);

  // Audio playback handlers
  const handlePlaybackStart = useCallback((utteranceId: number) => {
    console.log(`Audio playback started for utterance ${utteranceId}`);
  }, []);

  const handlePlaybackEnd = useCallback((utteranceId: number) => {
    console.log(`Audio playback ended for utterance ${utteranceId}`);
  }, []);

  const handlePlaybackError = useCallback((utteranceId: number, error: Error) => {
    console.error(`Audio playback error for utterance ${utteranceId}:`, error);
  }, []);

  const handleVolumeChange = useCallback((volume: number) => {
    console.log(`Volume changed to ${volume}`);
  }, []);

  const {
    connectionState,
    isConnected,
    connect,
    disconnect,
    sendMessage,
    sendBinaryMessage,
    error: wsError,
  } = useWebSocket(
    {
      url: 'ws://localhost:8080',
      reconnectInterval: 1000,
      maxReconnectAttempts: 5,
      heartbeatInterval: 30000,
    },
    handleMessage,
    handleBinaryMessage
  );

  // Audio playback hook
  const {
    playbackState,
    isSupported: playbackSupported,
    isInitialized: playbackInitialized,
    error: playbackError,
    initialize: initializePlayback,
    handleAudioStart,
    handleAudioEnd,

    stopPlayback,
    setVolume,
  } = useAudioPlayback(
    {
      volume: playbackVolume,
      autoPlay: true,
      crossfade: false,
      bufferSize: 4096,
    },
    handlePlaybackStart,
    handlePlaybackEnd,
    handlePlaybackError,
    handleVolumeChange
  );

  const {
    isRecording,
    isInitialized: audioInitialized,
    error: audioError,
    initialize: initializeAudio,
    startRecording,
    stopRecording,
    isSupported: audioSupported,
  } = useAudio(
    {
      sampleRate: 16000,
      channels: 1,
      bitsPerSample: 16,
      chunkSize: 1024,
    },
    useCallback((data: ArrayBuffer) => {
      if (isConnected) {
        sendBinaryMessage(data);
      }
    }, [isConnected, sendBinaryMessage])
  );

  const handleConnect = () => {
    connect();
  };

  const handleDisconnect = () => {
    disconnect();
  };

  const handleSendConfig = () => {
    const configMessage: ClientMessage = {
      type: 'config',
      data: {
        sourceLang,
        targetLang,
        voice,
      },
    };
    sendMessage(configMessage);
  };

  const handleEndSession = () => {
    const endMessage: ClientMessage = {
      type: 'end_session',
    };
    sendMessage(endMessage);
  };

  const handleStartRecording = async () => {
    if (!audioInitialized) {
      await initializeAudio();
    }
    await startRecording();
  };

  const handleInitializePlayback = async () => {
    if (!playbackInitialized) {
      await initializePlayback();
    }
  };

  const handleVolumeSliderChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    const volume = parseFloat(event.target.value);
    setPlaybackVolume(volume);
    setVolume(volume);
  };

  const handleStopPlayback = () => {
    stopPlayback();
  };

  const handleStopRecording = () => {
    stopRecording();
  };

  return (
    <div style={{ padding: '20px', fontFamily: 'Arial, sans-serif' }}>
      <h1>WebSocket & Audio Test</h1>
      
      {/* WebSocket Status */}
      <div style={{ marginBottom: '20px' }}>
        <h2>WebSocket Connection</h2>
        <p>Status: <strong>{connectionState}</strong></p>
        <p>Connected: <strong>{isConnected ? 'Yes' : 'No'}</strong></p>
        {wsError && <p style={{ color: 'red' }}>Error: {wsError.toString()}</p>}
        
        <button onClick={handleConnect} disabled={isConnected}>
          Connect
        </button>
        <button onClick={handleDisconnect} disabled={!isConnected} style={{ marginLeft: '10px' }}>
          Disconnect
        </button>
      </div>

      {/* Configuration */}
      <div style={{ marginBottom: '20px' }}>
        <h2>Configuration</h2>
        <div style={{ marginBottom: '10px' }}>
          <label>
            Source Language:
            <select value={sourceLang} onChange={(e) => setSourceLang(e.target.value)} style={{ marginLeft: '10px' }}>
              <option value="en">English</option>
              <option value="es">Spanish</option>
              <option value="fr">French</option>
              <option value="de">German</option>
            </select>
          </label>
        </div>
        <div style={{ marginBottom: '10px' }}>
          <label>
            Target Language:
            <select value={targetLang} onChange={(e) => setTargetLang(e.target.value)} style={{ marginLeft: '10px' }}>
              <option value="en">English</option>
              <option value="es">Spanish</option>
              <option value="fr">French</option>
              <option value="de">German</option>
            </select>
          </label>
        </div>
        <div style={{ marginBottom: '10px' }}>
          <label>
            Voice:
            <select value={voice} onChange={(e) => setVoice(e.target.value)} style={{ marginLeft: '10px' }}>
              <option value="female_voice_1">Female Voice 1</option>
              <option value="male_voice_1">Male Voice 1</option>
              <option value="female_voice_2">Female Voice 2</option>
            </select>
          </label>
        </div>
        
        <button onClick={handleSendConfig} disabled={!isConnected}>
          Send Configuration
        </button>
        <button onClick={handleEndSession} disabled={!isConnected} style={{ marginLeft: '10px' }}>
          End Session
        </button>
      </div>

      {/* Audio Controls */}
      <div style={{ marginBottom: '20px' }}>
        <h2>Audio Recording</h2>
        <p>Supported: <strong>{audioSupported ? 'Yes' : 'No'}</strong></p>
        <p>Initialized: <strong>{audioInitialized ? 'Yes' : 'No'}</strong></p>
        <p>Recording: <strong>{isRecording ? 'Yes' : 'No'}</strong></p>
        {audioError && <p style={{ color: 'red' }}>Audio Error: {audioError.message}</p>}
        
        <button onClick={handleStartRecording} disabled={!audioSupported || isRecording}>
          Start Recording
        </button>
        <button onClick={handleStopRecording} disabled={!isRecording} style={{ marginLeft: '10px' }}>
          Stop Recording
        </button>
      </div>

      {/* Audio Playback Controls */}
      <div style={{ marginBottom: '20px' }}>
        <h2>Audio Playback</h2>
        <p>Supported: <strong>{playbackSupported ? 'Yes' : 'No'}</strong></p>
        <p>Initialized: <strong>{playbackInitialized ? 'Yes' : 'No'}</strong></p>
        <p>Playing: <strong>{playbackState.isPlaying ? 'Yes' : 'No'}</strong></p>
        <p>Current Utterance: <strong>{playbackState.currentUtteranceId || 'None'}</strong></p>
        <p>Queue Length: <strong>{playbackState.queue.length}</strong></p>
        {playbackError && <p style={{ color: 'red' }}>Playback Error: {playbackError.message}</p>}
        
        <div style={{ marginBottom: '10px' }}>
          <label>
            Volume: {Math.round(playbackVolume * 100)}%
            <input
              type="range"
              min="0"
              max="1"
              step="0.1"
              value={playbackVolume}
              onChange={handleVolumeSliderChange}
              style={{ marginLeft: '10px', width: '200px' }}
            />
          </label>
        </div>
        
        <button onClick={handleInitializePlayback} disabled={!playbackSupported || playbackInitialized}>
          Initialize Playback
        </button>
        <button onClick={handleStopPlayback} disabled={!playbackState.isPlaying} style={{ marginLeft: '10px' }}>
          Stop Playback
        </button>
      </div>

      {/* Messages */}
      <div>
        <h2>Received Messages</h2>
        <div style={{ 
          height: '300px', 
          overflowY: 'scroll', 
          border: '1px solid #ccc', 
          padding: '10px',
          backgroundColor: '#f9f9f9'
        }}>
          {messages.length === 0 ? (
            <p>No messages received yet...</p>
          ) : (
            messages.map((message, index) => (
              <div key={index} style={{ marginBottom: '10px', padding: '5px', backgroundColor: 'white', borderRadius: '3px' }}>
                <strong>{message.type}:</strong>
                <pre style={{ margin: '5px 0', fontSize: '12px' }}>
                  {JSON.stringify(message, null, 2)}
                </pre>
              </div>
            ))
          )}
        </div>
      </div>
    </div>
  );
};

export default WebSocketTest;