/**
 * AudioPlaybackDemo - Demo component for testing audio playback functionality
 */

import React, { useState, useCallback } from 'react';
import { useAudioPlayback } from '../hooks/useAudioPlayback';

const AudioPlaybackDemo: React.FC = () => {
  const [volume, setVolume] = useState(0.8);
  const [testUtteranceId, setTestUtteranceId] = useState(1);

  const handlePlaybackStart = useCallback((utteranceId: number) => {
    console.log(`Playback started for utterance ${utteranceId}`);
  }, []);

  const handlePlaybackEnd = useCallback((utteranceId: number) => {
    console.log(`Playback ended for utterance ${utteranceId}`);
  }, []);

  const handlePlaybackError = useCallback((utteranceId: number, error: Error) => {
    console.error(`Playback error for utterance ${utteranceId}:`, error);
  }, []);

  const handleVolumeChange = useCallback((newVolume: number) => {
    console.log(`Volume changed to ${newVolume}`);
  }, []);

  const {
    playbackState,
    isSupported,
    isInitialized,
    error,
    initialize,
    handleAudioStart,
    handleAudioData,
    handleAudioEnd,
    playAudio,
    stopPlayback,
    setVolume: setPlaybackVolume,
  } = useAudioPlayback(
    {
      volume,
      autoPlay: false, // Manual control for demo
      crossfade: false,
      bufferSize: 4096,
    },
    handlePlaybackStart,
    handlePlaybackEnd,
    handlePlaybackError,
    handleVolumeChange
  );

  const handleInitialize = async () => {
    try {
      await initialize();
    } catch (error) {
      console.error('Failed to initialize audio playback:', error);
    }
  };

  const handleVolumeSliderChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    const newVolume = parseFloat(event.target.value);
    setVolume(newVolume);
    setPlaybackVolume(newVolume);
  };

  const handleCreateTestAudio = () => {
    // Simulate audio start
    handleAudioStart(testUtteranceId, 2.5, 'wav', 22050, 1);

    // Create some test audio data (sine wave)
    const sampleRate = 22050;
    const duration = 2.5;
    const samples = Math.floor(sampleRate * duration);
    const audioBuffer = new ArrayBuffer(samples * 2); // 16-bit samples
    const audioData = new Int16Array(audioBuffer);

    // Generate a 440Hz sine wave (A note)
    const frequency = 440;
    for (let i = 0; i < samples; i++) {
      const time = i / sampleRate;
      const amplitude = Math.sin(2 * Math.PI * frequency * time) * 0.3; // 30% volume
      audioData[i] = Math.floor(amplitude * 32767); // Convert to 16-bit PCM
    }

    // Split into chunks and send
    const chunkSize = 1024;
    let sequenceNumber = 0;
    
    for (let offset = 0; offset < audioBuffer.byteLength; offset += chunkSize * 2) {
      const chunkEnd = Math.min(offset + chunkSize * 2, audioBuffer.byteLength);
      const chunk = audioBuffer.slice(offset, chunkEnd);
      const isLast = chunkEnd >= audioBuffer.byteLength;
      
      handleAudioData(testUtteranceId, chunk, sequenceNumber++, isLast);
    }

    // End audio
    handleAudioEnd(testUtteranceId);
  };

  const handlePlayTestAudio = async () => {
    try {
      await playAudio(testUtteranceId);
    } catch (error) {
      console.error('Failed to play test audio:', error);
    }
  };

  const handleStopPlayback = () => {
    stopPlayback();
  };

  const handleNextUtterance = () => {
    setTestUtteranceId(prev => prev + 1);
  };

  return (
    <div style={{ padding: '20px', fontFamily: 'Arial, sans-serif' }}>
      <h1>Audio Playback Demo</h1>
      
      {/* System Status */}
      <div style={{ marginBottom: '20px', padding: '15px', border: '1px solid #ccc', borderRadius: '5px' }}>
        <h2>System Status</h2>
        <p><strong>Supported:</strong> {isSupported ? 'Yes' : 'No'}</p>
        <p><strong>Initialized:</strong> {isInitialized ? 'Yes' : 'No'}</p>
        <p><strong>Playing:</strong> {playbackState.isPlaying ? 'Yes' : 'No'}</p>
        <p><strong>Current Utterance:</strong> {playbackState.currentUtteranceId || 'None'}</p>
        <p><strong>Queue Length:</strong> {playbackState.queue.length}</p>
        <p><strong>Volume:</strong> {Math.round(playbackState.volume * 100)}%</p>
        {error && <p style={{ color: 'red' }}><strong>Error:</strong> {error.message}</p>}
      </div>

      {/* Controls */}
      <div style={{ marginBottom: '20px', padding: '15px', border: '1px solid #ccc', borderRadius: '5px' }}>
        <h2>Controls</h2>
        
        <div style={{ marginBottom: '15px' }}>
          <button 
            onClick={handleInitialize} 
            disabled={!isSupported || isInitialized}
            style={{ marginRight: '10px', padding: '8px 16px' }}
          >
            Initialize Audio System
          </button>
          
          <button 
            onClick={handleStopPlayback} 
            disabled={!playbackState.isPlaying}
            style={{ padding: '8px 16px' }}
          >
            Stop Playback
          </button>
        </div>

        <div style={{ marginBottom: '15px' }}>
          <label style={{ display: 'block', marginBottom: '5px' }}>
            Volume: {Math.round(volume * 100)}%
          </label>
          <input
            type="range"
            min="0"
            max="1"
            step="0.1"
            value={volume}
            onChange={handleVolumeSliderChange}
            style={{ width: '200px' }}
            disabled={!isInitialized}
          />
        </div>
      </div>

      {/* Test Audio */}
      <div style={{ marginBottom: '20px', padding: '15px', border: '1px solid #ccc', borderRadius: '5px' }}>
        <h2>Test Audio</h2>
        <p>Current Test Utterance ID: <strong>{testUtteranceId}</strong></p>
        
        <div style={{ marginBottom: '15px' }}>
          <button 
            onClick={handleCreateTestAudio}
            disabled={!isInitialized}
            style={{ marginRight: '10px', padding: '8px 16px' }}
          >
            Create Test Audio (440Hz Sine Wave)
          </button>
          
          <button 
            onClick={handlePlayTestAudio}
            disabled={!isInitialized || playbackState.queue.length === 0}
            style={{ marginRight: '10px', padding: '8px 16px' }}
          >
            Play Test Audio
          </button>
          
          <button 
            onClick={handleNextUtterance}
            style={{ padding: '8px 16px' }}
          >
            Next Utterance ID
          </button>
        </div>
      </div>

      {/* Queue Status */}
      <div style={{ padding: '15px', border: '1px solid #ccc', borderRadius: '5px' }}>
        <h2>Audio Queue</h2>
        {playbackState.queue.length === 0 ? (
          <p>No audio in queue</p>
        ) : (
          <div>
            {playbackState.queue.map((audio) => (
              <div 
                key={audio.utteranceId} 
                style={{ 
                  marginBottom: '10px', 
                  padding: '10px', 
                  backgroundColor: '#f9f9f9', 
                  borderRadius: '3px',
                  border: playbackState.currentUtteranceId === audio.utteranceId ? '2px solid #007bff' : '1px solid #ddd'
                }}
              >
                <p><strong>Utterance {audio.utteranceId}</strong></p>
                <p>Duration: {audio.duration}s</p>
                <p>Format: {audio.format}</p>
                <p>Sample Rate: {audio.sampleRate}Hz</p>
                <p>Channels: {audio.channels}</p>
                <p>Audio Chunks: {audio.audioData.length}</p>
                <p>Complete: {audio.isComplete ? 'Yes' : 'No'}</p>
                {playbackState.currentUtteranceId === audio.utteranceId && (
                  <p style={{ color: '#007bff', fontWeight: 'bold' }}>Currently Playing</p>
                )}
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
};

export default AudioPlaybackDemo;