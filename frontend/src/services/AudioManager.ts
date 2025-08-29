/**
 * AudioManager - Handles audio capture using MediaRecorder API
 * Captures 16kHz mono PCM audio and streams it continuously
 */

export interface AudioConfig {
  sampleRate: number;
  channels: number;
  bitsPerSample: number;
  chunkSize: number;
}

export interface AudioHandler {
  onAudioData: (data: ArrayBuffer) => void;
  onError: (error: Error) => void;
  onStateChange: (recording: boolean) => void;
}

export class AudioManager {
  private audioStream: MediaStream | null = null;
  private audioHandler: AudioHandler;
  private config: AudioConfig;
  private isRecording = false;
  private audioContext: AudioContext | null = null;
  private processor: ScriptProcessorNode | null = null;
  private source: MediaStreamAudioSourceNode | null = null;
  private selectedDeviceId: string | null = null;

  constructor(config: AudioConfig, audioHandler: AudioHandler) {
    this.config = config;
    this.audioHandler = audioHandler;
  }

  /**
   * Request microphone permission and initialize audio capture
   */
  async initialize(deviceId?: string): Promise<void> {
    try {
      // Store selected device ID
      if (deviceId) {
        this.selectedDeviceId = deviceId;
      }

      // Request microphone access with device selection
      const audioConstraints: MediaTrackConstraints = {
        sampleRate: this.config.sampleRate,
        channelCount: this.config.channels,
        echoCancellation: true,
        noiseSuppression: true,
        autoGainControl: true,
      };

      // Add device ID constraint if specified
      if (this.selectedDeviceId) {
        audioConstraints.deviceId = { exact: this.selectedDeviceId };
      }

      this.audioStream = await navigator.mediaDevices.getUserMedia({
        audio: audioConstraints,
      });

      // Create audio context for processing
      this.audioContext = new (window.AudioContext || (window as any).webkitAudioContext)({
        sampleRate: this.config.sampleRate,
      });

      this.source = this.audioContext.createMediaStreamSource(this.audioStream);
      
      // Create script processor for real-time audio processing
      this.processor = this.audioContext.createScriptProcessor(
        this.config.chunkSize,
        this.config.channels,
        this.config.channels
      );

      this.processor.onaudioprocess = (event) => {
        if (this.isRecording) {
          this.processAudioBuffer(event.inputBuffer);
        }
      };

      // Connect audio nodes
      this.source.connect(this.processor);
      this.processor.connect(this.audioContext.destination);

    } catch (error) {
      let audioError: Error;
      
      if (error instanceof Error) {
        if (error.name === 'NotAllowedError' || error.name === 'PermissionDeniedError') {
          audioError = new Error('Microphone permission denied. Please grant access and try again.');
        } else if (error.name === 'NotFoundError' || error.name === 'DevicesNotFoundError') {
          audioError = new Error('No microphone found. Please connect a microphone and try again.');
        } else if (error.name === 'NotReadableError' || error.name === 'TrackStartError') {
          audioError = new Error('Microphone is already in use by another application.');
        } else if (error.name === 'OverconstrainedError' || error.name === 'ConstraintNotSatisfiedError') {
          audioError = new Error('Microphone does not support the required audio format.');
        } else {
          audioError = new Error(`Failed to initialize audio: ${error.message}`);
        }
      } else {
        audioError = new Error(`Failed to initialize audio: ${error}`);
      }
      
      this.audioHandler.onError(audioError);
      throw audioError;
    }
  }

  /**
   * Start audio recording
   */
  async startRecording(): Promise<void> {
    if (this.isRecording) {
      return;
    }

    if (!this.audioStream || !this.audioContext) {
      await this.initialize();
    }

    try {
      // Resume audio context if suspended
      if (this.audioContext?.state === 'suspended') {
        await this.audioContext.resume();
      }

      this.isRecording = true;
      this.audioHandler.onStateChange(true);
      
      console.log('Audio recording started');
    } catch (error) {
      let audioError: Error;
      
      if (error instanceof Error) {
        if (error.name === 'InvalidStateError') {
          audioError = new Error('Audio context is in an invalid state. Please refresh the page.');
        } else {
          audioError = new Error(`Failed to start recording: ${error.message}`);
        }
      } else {
        audioError = new Error(`Failed to start recording: ${error}`);
      }
      
      this.audioHandler.onError(audioError);
      throw audioError;
    }
  }

  /**
   * Stop audio recording
   */
  stopRecording(): void {
    if (!this.isRecording) {
      return;
    }

    this.isRecording = false;
    this.audioHandler.onStateChange(false);
    
    console.log('Audio recording stopped');
  }

  /**
   * Clean up resources
   */
  cleanup(): void {
    this.stopRecording();

    if (this.processor) {
      this.processor.disconnect();
      this.processor = null;
    }

    if (this.source) {
      this.source.disconnect();
      this.source = null;
    }

    if (this.audioContext) {
      this.audioContext.close();
      this.audioContext = null;
    }

    if (this.audioStream) {
      this.audioStream.getTracks().forEach(track => track.stop());
      this.audioStream = null;
    }
  }

  /**
   * Check if recording is active
   */
  isActive(): boolean {
    return this.isRecording;
  }

  /**
   * Get current audio configuration
   */
  getConfig(): AudioConfig {
    return { ...this.config };
  }

  private processAudioBuffer(buffer: AudioBuffer): void {
    // Get audio data from the first channel
    const audioData = buffer.getChannelData(0);
    
    // Convert float32 samples to int16 PCM
    const pcmData = new Int16Array(audioData.length);
    for (let i = 0; i < audioData.length; i++) {
      // Clamp to [-1, 1] and convert to 16-bit PCM
      const sample = Math.max(-1, Math.min(1, audioData[i]));
      pcmData[i] = sample * 0x7FFF;
    }

    // Convert to ArrayBuffer and send
    const arrayBuffer = pcmData.buffer.slice(
      pcmData.byteOffset,
      pcmData.byteOffset + pcmData.byteLength
    );
    
    this.audioHandler.onAudioData(arrayBuffer);
  }

  /**
   * Check if audio capture is supported
   */
  static isSupported(): boolean {
    return !!(
      navigator.mediaDevices &&
      !!navigator.mediaDevices?.getUserMedia &&
      (window.AudioContext || (window as any).webkitAudioContext)
    );
  }

  /**
   * Set the audio input device
   */
  async setAudioDevice(deviceId: string): Promise<void> {
    const wasRecording = this.isRecording;
    
    // Stop current recording if active
    if (wasRecording) {
      this.stopRecording();
    }
    
    // Clean up current audio stream
    if (this.audioStream) {
      this.audioStream.getTracks().forEach(track => track.stop());
    }
    
    // Reinitialize with new device
    await this.initialize(deviceId);
    
    // Resume recording if it was active
    if (wasRecording) {
      await this.startRecording();
    }
  }

  /**
   * Get currently selected device ID
   */
  getSelectedDeviceId(): string | null {
    return this.selectedDeviceId;
  }

  /**
   * Get available audio input devices
   */
  static async getAudioDevices(): Promise<MediaDeviceInfo[]> {
    try {
      const devices = await navigator.mediaDevices.enumerateDevices();
      return devices.filter(device => device.kind === 'audioinput');
    } catch (error) {
      console.error('Failed to enumerate audio devices:', error);
      return [];
    }
  }

  /**
   * Request microphone permission (required before enumerating devices with labels)
   */
  static async requestMicrophonePermission(): Promise<boolean> {
    try {
      const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
      stream.getTracks().forEach(track => track.stop());
      return true;
    } catch (error) {
      console.error('Microphone permission denied:', error);
      return false;
    }
  }
}