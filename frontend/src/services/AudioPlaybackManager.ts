/**
 * AudioPlaybackManager - Handles audio output and playback
 * Manages automatic audio playback from synthesized speech data
 */

export interface AudioPlaybackConfig {
  volume: number;
  autoPlay: boolean;
  crossfade: boolean;
  bufferSize: number;
}

export interface PlaybackHandler {
  onPlaybackStart: (utteranceId: number) => void;
  onPlaybackEnd: (utteranceId: number) => void;
  onPlaybackError: (utteranceId: number, error: Error) => void;
  onVolumeChange: (volume: number) => void;
}

export interface AudioPlaybackState {
  isPlaying: boolean;
  currentUtteranceId: number | null;
  volume: number;
  queue: PendingAudio[];
}

interface PendingAudio {
  utteranceId: number;
  audioData: ArrayBuffer[];
  format: string;
  sampleRate: number;
  channels: number;
  duration: number;
  isComplete: boolean;
}

export class AudioPlaybackManager {
  private config: AudioPlaybackConfig;
  private handler: PlaybackHandler;
  private audioContext: AudioContext | null = null;
  private currentSource: AudioBufferSourceNode | null = null;
  private gainNode: GainNode | null = null;
  private pendingAudio: Map<number, PendingAudio> = new Map();
  private playbackQueue: number[] = [];
  private isPlaying = false;
  private currentUtteranceId: number | null = null;

  constructor(config: AudioPlaybackConfig, handler: PlaybackHandler) {
    this.config = config;
    this.handler = handler;
  }

  /**
   * Initialize audio playback system
   */
  async initialize(): Promise<void> {
    try {
      // Create audio context
      this.audioContext = new (window.AudioContext || (window as any).webkitAudioContext)();
      
      // Create gain node for volume control
      this.gainNode = this.audioContext.createGain();
      this.gainNode.connect(this.audioContext.destination);
      this.gainNode.gain.value = this.config.volume;

      // Handle audio context state changes
      this.audioContext.addEventListener('statechange', () => {
        console.log('Audio context state:', this.audioContext?.state);
      });

    } catch (error) {
      throw new Error(`Failed to initialize audio playback: ${error}`);
    }
  }

  /**
   * Handle audio start message
   */
  handleAudioStart(utteranceId: number, duration: number, format = 'wav', sampleRate = 22050, channels = 1): void {
    console.log(`Audio start for utterance ${utteranceId}: ${duration}s, ${format}, ${sampleRate}Hz, ${channels}ch`);
    
    // Create pending audio entry
    const pendingAudio: PendingAudio = {
      utteranceId,
      audioData: [],
      format,
      sampleRate,
      channels,
      duration,
      isComplete: false,
    };

    this.pendingAudio.set(utteranceId, pendingAudio);
    
    // Add to playback queue if auto-play is enabled
    if (this.config.autoPlay) {
      this.playbackQueue.push(utteranceId);
      this.processPlaybackQueue();
    }
  }

  /**
   * Handle audio data chunk
   */
  handleAudioData(utteranceId: number, audioData: ArrayBuffer, _sequenceNumber: number, isLast: boolean): void {
    const pending = this.pendingAudio.get(utteranceId);
    if (!pending) {
      console.warn(`Received audio data for unknown utterance ${utteranceId}`);
      return;
    }

    // Add audio data to the pending audio
    pending.audioData.push(audioData);

    if (isLast) {
      pending.isComplete = true;
      console.log(`Audio complete for utterance ${utteranceId}: ${pending.audioData.length} chunks`);
      
      // Process playback queue if this audio is ready
      this.processPlaybackQueue();
    }
  }

  /**
   * Handle audio end message
   */
  handleAudioEnd(utteranceId: number): void {
    const pending = this.pendingAudio.get(utteranceId);
    if (pending) {
      pending.isComplete = true;
      this.processPlaybackQueue();
    }
  }

  /**
   * Play audio for a specific utterance
   */
  async playAudio(utteranceId: number): Promise<void> {
    const pending = this.pendingAudio.get(utteranceId);
    if (!pending || !pending.isComplete) {
      throw new Error(`Audio not ready for utterance ${utteranceId}`);
    }

    if (!this.audioContext || !this.gainNode) {
      await this.initialize();
    }

    try {
      // Resume audio context if suspended
      if (this.audioContext!.state === 'suspended') {
        await this.audioContext!.resume();
      }

      // Combine all audio data chunks
      const totalLength = pending.audioData.reduce((sum, chunk) => sum + chunk.byteLength, 0);
      const combinedData = new Uint8Array(totalLength);
      let offset = 0;
      
      for (const chunk of pending.audioData) {
        combinedData.set(new Uint8Array(chunk), offset);
        offset += chunk.byteLength;
      }

      // Decode audio data
      const audioBuffer = await this.decodeAudioData(combinedData.buffer, pending.format, pending.sampleRate, pending.channels);
      
      // Stop current playback if any
      this.stopCurrentPlayback();

      // Create and configure audio source
      this.currentSource = this.audioContext!.createBufferSource();
      this.currentSource.buffer = audioBuffer;
      this.currentSource.connect(this.gainNode!);

      // Set up playback event handlers
      this.currentSource.onended = () => {
        this.handlePlaybackEnd(utteranceId);
      };

      // Start playback
      this.isPlaying = true;
      this.currentUtteranceId = utteranceId;
      this.currentSource.start(0);
      
      this.handler.onPlaybackStart(utteranceId);
      console.log(`Started playback for utterance ${utteranceId}`);

    } catch (error) {
      const playbackError = new Error(`Failed to play audio for utterance ${utteranceId}: ${error}`);
      this.handler.onPlaybackError(utteranceId, playbackError);
      throw playbackError;
    }
  }

  /**
   * Stop current audio playback
   */
  stopPlayback(): void {
    this.stopCurrentPlayback();
    this.playbackQueue.length = 0; // Clear queue
  }

  /**
   * Set playback volume (0.0 to 1.0)
   */
  setVolume(volume: number): void {
    this.config.volume = Math.max(0, Math.min(1, volume));
    
    if (this.gainNode) {
      this.gainNode.gain.value = this.config.volume;
    }
    
    this.handler.onVolumeChange(this.config.volume);
  }

  /**
   * Get current volume
   */
  getVolume(): number {
    return this.config.volume;
  }

  /**
   * Get current playback state
   */
  getPlaybackState(): AudioPlaybackState {
    return {
      isPlaying: this.isPlaying,
      currentUtteranceId: this.currentUtteranceId,
      volume: this.config.volume,
      queue: Array.from(this.pendingAudio.values()),
    };
  }

  /**
   * Check if audio playback is supported
   */
  static isSupported(): boolean {
    return !!(window.AudioContext || (window as any).webkitAudioContext);
  }

  /**
   * Clean up resources
   */
  cleanup(): void {
    this.stopCurrentPlayback();
    
    if (this.audioContext) {
      this.audioContext.close();
      this.audioContext = null;
    }
    
    this.gainNode = null;
    this.pendingAudio.clear();
    this.playbackQueue.length = 0;
  }

  private async processPlaybackQueue(): Promise<void> {
    if (this.isPlaying || this.playbackQueue.length === 0) {
      return;
    }

    const nextUtteranceId = this.playbackQueue[0];
    const pending = this.pendingAudio.get(nextUtteranceId);
    
    if (pending && pending.isComplete) {
      this.playbackQueue.shift(); // Remove from queue
      
      try {
        await this.playAudio(nextUtteranceId);
      } catch (error) {
        console.error(`Failed to play queued audio ${nextUtteranceId}:`, error);
        // Continue with next item in queue
        this.processPlaybackQueue();
      }
    }
  }

  private stopCurrentPlayback(): void {
    if (this.currentSource) {
      try {
        this.currentSource.stop();
      } catch (error) {
        // Ignore errors when stopping already stopped sources
      }
      this.currentSource = null;
    }
    
    if (this.isPlaying && this.currentUtteranceId !== null) {
      this.handlePlaybackEnd(this.currentUtteranceId);
    }
  }

  private handlePlaybackEnd(utteranceId: number): void {
    this.isPlaying = false;
    this.currentUtteranceId = null;
    this.currentSource = null;
    
    // Clean up completed audio data
    this.pendingAudio.delete(utteranceId);
    
    this.handler.onPlaybackEnd(utteranceId);
    console.log(`Playback ended for utterance ${utteranceId}`);
    
    // Process next item in queue
    this.processPlaybackQueue();
  }

  private async decodeAudioData(
    audioData: ArrayBuffer, 
    format: string, 
    sampleRate: number, 
    channels: number
  ): Promise<AudioBuffer> {
    if (!this.audioContext) {
      throw new Error('Audio context not initialized');
    }

    try {
      // For WAV format, try direct decoding first
      if (format === 'wav') {
        try {
          return await this.audioContext.decodeAudioData(audioData.slice(0));
        } catch (error) {
          console.warn('Direct WAV decoding failed, trying manual decode:', error);
        }
      }

      // Manual PCM decoding for raw audio data
      return this.decodePCMAudio(audioData, sampleRate, channels);
      
    } catch (error) {
      throw new Error(`Failed to decode ${format} audio: ${error}`);
    }
  }

  private decodePCMAudio(audioData: ArrayBuffer, sampleRate: number, channels: number): AudioBuffer {
    if (!this.audioContext) {
      throw new Error('Audio context not initialized');
    }

    // Assume 16-bit PCM data
    const samples = new Int16Array(audioData);
    const frameCount = samples.length / channels;
    
    // Create audio buffer
    const audioBuffer = this.audioContext.createBuffer(channels, frameCount, sampleRate);
    
    // Convert and copy data for each channel
    for (let channel = 0; channel < channels; channel++) {
      const channelData = audioBuffer.getChannelData(channel);
      
      for (let i = 0; i < frameCount; i++) {
        // Convert from 16-bit PCM to float32 [-1, 1]
        const sampleIndex = i * channels + channel;
        channelData[i] = samples[sampleIndex] / 32768.0;
      }
    }
    
    return audioBuffer;
  }

}