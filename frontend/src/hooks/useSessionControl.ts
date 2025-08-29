import { useEffect, useCallback, useRef, useState } from 'react';
import { useAppStore } from '../store/appStore';
import type { SystemState } from '../components/StatusIndicator';
import { useWebSocketIntegration, type WebSocketIntegrationConfig } from './useWebSocketIntegration';
import { useAudioIntegration, type AudioIntegrationConfig } from './useAudioIntegration';
import { useConfigurationSync } from './useConfigurationSync';
import { useConnectionResilience } from './useConnectionResilience';
import { type ServerMessage, type ClientMessage } from '../types/messageProtocol';
import { useErrorHandler } from './useErrorHandler';

// Service health monitoring interface
interface ServiceHealth {
  isHealthy: boolean;
  lastHealthCheck: Date;
  errorCount: number;
  lastError: Error | null;
}

interface ServiceHealthStatus {
  webSocket: ServiceHealth;
  audio: ServiceHealth;
  configuration: ServiceHealth;
  overall: 'healthy' | 'degraded' | 'critical' | 'offline';
}

// Service initialization state
interface ServiceInitializationState {
  webSocket: 'pending' | 'initializing' | 'ready' | 'failed';
  audio: 'pending' | 'initializing' | 'ready' | 'failed';
  configuration: 'pending' | 'initializing' | 'ready' | 'failed';
  overall: 'pending' | 'initializing' | 'ready' | 'failed';
}

/**
 * Enhanced custom hook for managing session control and state transitions
 * Integrates WebSocket, Audio, and Configuration services with proper error handling,
 * connection resilience mechanisms, and comprehensive service coordination
 */
export const useSessionControl = () => {
  const {
    sessionActive,
    currentState,
    connectionStatus,
    sourceLang,
    targetLang,
    selectedVoice,
    toggleSession,
    setCurrentState,
    setConnectionStatus,
    resetSession,
    setCurrentOriginalText,
    setCurrentTranslatedText,
    setTranscriptionConfidence,
    addConversationEntry
  } = useAppStore();

  // Local state for integration management
  const [isAudioReady, setIsAudioReady] = useState(false);
  const [isWebSocketReady, setIsWebSocketReady] = useState(false);
  const [audioDevices, setAudioDevices] = useState<MediaDeviceInfo[]>([]);
  const [selectedAudioDevice, setSelectedAudioDevice] = useState<string | null>(null);
  const [audioDeviceError, setAudioDeviceError] = useState<Error | null>(null);
  const [isDeviceEnumerating, setIsDeviceEnumerating] = useState(false);
  const [sessionError, setSessionError] = useState<Error | null>(null);
  
  // Service coordination state
  const [serviceInitializationState, setServiceInitializationState] = useState<ServiceInitializationState>({
    webSocket: 'pending',
    audio: 'pending',
    configuration: 'pending',
    overall: 'pending'
  });
  
  const [serviceHealthStatus, setServiceHealthStatus] = useState<ServiceHealthStatus>({
    webSocket: { isHealthy: false, lastHealthCheck: new Date(), errorCount: 0, lastError: null },
    audio: { isHealthy: false, lastHealthCheck: new Date(), errorCount: 0, lastError: null },
    configuration: { isHealthy: false, lastHealthCheck: new Date(), errorCount: 0, lastError: null },
    overall: 'offline'
  });
  
  // Refs for managing session lifecycle
  const sessionInitializationRef = useRef<Promise<void> | null>(null);
  const currentUtteranceIdRef = useRef<number | null>(null);
  const serviceHealthCheckIntervalRef = useRef<number | null>(null);
  const serviceCleanupCallbacksRef = useRef<Array<() => void>>([]);
  const { handleError, handleWebSocketError, handleAudioError } = useErrorHandler();

  // WebSocket configuration
  const webSocketConfig: WebSocketIntegrationConfig = {
    url: 'ws://localhost:8080',
    reconnectInterval: 2000,
    maxReconnectAttempts: 5,
    heartbeatInterval: 30000,
    sessionRecoveryTimeout: 300000,
    messageQueueSize: 100,
    connectionQualityThreshold: 5000,
  };

  // Audio configuration
  const audioConfig: AudioIntegrationConfig = {
    sampleRate: 16000,
    channels: 1,
    bitsPerSample: 16,
    chunkSize: 1024,
    autoInitialize: false,
    deviceSelectionEnabled: true,
    permissionRetryAttempts: 3,
    deviceSwitchDelay: 500,
  };

  // Forward declaration for transitionToState (defined later)
  const transitionToStateRef = useRef<((newState: SystemState) => void) | null>(null);

  // WebSocket message handlers with complete message protocol integration
  const handleWebSocketMessage = useCallback((message: ServerMessage) => {
    try {
      switch (message.type) {
        case 'transcription_update':
          setCurrentOriginalText(message.data.text);
          setTranscriptionConfidence(message.data.confidence);
          currentUtteranceIdRef.current = message.data.utteranceId;
          
          // Update session activity
          const sessionState = webSocketIntegration?.getSessionState();
          if (sessionState) {
            sessionState.lastActivity = Date.now();
            sessionState.currentUtteranceId = message.data.utteranceId;
            webSocketIntegration?.setSessionState(sessionState);
          }
          break;
          
        case 'translation_result':
          setCurrentTranslatedText(message.data.translatedText);
          
          // Add to conversation history
          addConversationEntry({
            utteranceId: message.data.utteranceId,
            originalText: message.data.originalText,
            translatedText: message.data.translatedText,
            speaker: 'user'
          });
          
          // Transition to speaking state
          if (transitionToStateRef.current) {
            transitionToStateRef.current('speaking');
          }
          break;
          
        case 'audio_start':
          console.log(`Audio playback starting for utterance ${message.data.utteranceId}, duration: ${message.data.duration}ms`);
          currentUtteranceIdRef.current = message.data.utteranceId;
          
          // Prepare for incoming audio data
          if (transitionToStateRef.current) {
            transitionToStateRef.current('speaking');
          }
          break;
          
        case 'audio_data':
          console.log(`Audio data chunk received for utterance ${message.data.utteranceId}, sequence: ${message.data.sequenceNumber}, isLast: ${message.data.isLast}`);
          
          // The actual audio data will be handled in handleWebSocketBinaryMessage
          // This message just provides metadata about the incoming binary data
          break;
          
        case 'audio_end':
          console.log(`Audio playback ended for utterance ${message.data.utteranceId}`);
          
          // Transition back to listening after audio playback ends
          if (transitionToStateRef.current && sessionActive) {
            transitionToStateRef.current('listening');
          }
          break;
          
        case 'status_update':
          if (transitionToStateRef.current) {
            transitionToStateRef.current(message.data.state);
          }
          if (message.data.utteranceId) {
            currentUtteranceIdRef.current = message.data.utteranceId;
          }
          
          // Update session state with server status
          const currentSessionState = webSocketIntegration?.getSessionState();
          if (currentSessionState) {
            currentSessionState.lastActivity = Date.now();
            if (message.data.utteranceId) {
              currentSessionState.currentUtteranceId = message.data.utteranceId;
            }
            webSocketIntegration?.setSessionState(currentSessionState);
          }
          break;
          
        case 'error':
          const errorMessage = `Server error: ${message.data.message}`;
          const serverError = new Error(errorMessage);
          
          // Handle different types of server errors
          if (message.data.code) {
            switch (message.data.code) {
              case 'AUDIO_PROCESSING_ERROR':
                handleAudioError(serverError, 'capture');
                break;
              case 'TRANSLATION_ERROR':
                handleError(serverError, { context: 'Translation processing', utteranceId: message.data.utteranceId });
                break;
              case 'SESSION_ERROR':
                propagateServiceError(serverError, 'webSocket', ['audio', 'configuration']);
                break;
              default:
                handleError(serverError, { context: 'Server error', code: message.data.code });
            }
          } else {
            handleError(serverError);
          }
          break;
          
        case 'pong':
          // Heartbeat response - handled by WebSocketManager
          console.log('Received heartbeat response from server');
          break;
          
        case 'session_recovered':
          // Session recovery response - handled by WebSocketManager but we can add application-level handling
          console.log('Session recovery response received:', message.success);
          if (message.success) {
            console.log('Session successfully recovered, resuming normal operation');
            // Update session state if provided
            if (message.sessionState && webSocketIntegration) {
              webSocketIntegration.setSessionState(message.sessionState);
            }
            // Clear any session errors since recovery was successful
            setSessionError(null);
          } else {
            console.warn('Session recovery failed:', message.reason);
            const recoveryError = new Error(`Session recovery failed: ${message.reason || 'Unknown reason'}`);
            setSessionError(recoveryError);
            propagateServiceError(recoveryError, 'webSocket', ['audio', 'configuration']);
          }
          break;
          
        case 'message_ack':
          // Message acknowledgment - handled by MessageProtocolHandler but we can add application-level logging
          console.log('Message acknowledgment received:', message.data.messageId, message.data.status);
          if (message.data.status === 'error') {
            console.error('Message processing failed on server:', message.data.messageId, message.data.error);
            // Could add user notification for critical message failures
            if (message.data.error?.includes('CRITICAL') || message.data.error?.includes('FATAL')) {
              const ackError = new Error(`Critical message processing error: ${message.data.error}`);
              handleError(ackError, { context: 'Message acknowledgment error', messageId: message.data.messageId });
            }
          }
          break;
          
        default:
          console.log('Received unhandled message type:', (message as any).type, message);
      }
    } catch (error) {
      handleError(error as Error, { context: 'WebSocket message handling', message });
    }
  }, [setCurrentOriginalText, setCurrentTranslatedText, setTranscriptionConfidence, 
      addConversationEntry, handleError, handleAudioError, sessionActive]);

  const handleWebSocketBinaryMessage = useCallback((data: ArrayBuffer, messageType?: string) => {
    // Handle binary audio data from server (TTS output) with proper message protocol integration
    try {
      console.log(`Received binary ${messageType || 'unknown'} data:`, data.byteLength, 'bytes');
      
      // Handle different types of binary messages based on the preceding JSON message
      switch (messageType) {
        case 'audio_start':
        case 'audio_data':
          // This is TTS audio data that should be played back
          // For now, we'll log it - in a full implementation, this would be sent to AudioPlaybackManager
          console.log('Processing TTS audio data for playback');
          
          // Update session activity for binary audio data
          const sessionState = webSocketIntegration?.getSessionState();
          if (sessionState) {
            sessionState.lastActivity = Date.now();
            webSocketIntegration?.setSessionState(sessionState);
          }
          
          // Note: Audio playback would be handled by AudioPlaybackManager when implemented
          // For now, we log the received audio data for debugging purposes
          break;
          
        default:
          console.log('Received binary data with unknown message type:', messageType);
      }
    } catch (error) {
      handleError(error as Error, { context: 'WebSocket binary message handling', messageType });
    }
  }, [handleError]);

  const handleWebSocketConnectionChange = useCallback((connected: boolean) => {
    setIsWebSocketReady(connected);
    setConnectionStatus(connected ? 'connected' : 'disconnected');
    
    if (connected) {
      console.log('WebSocket connected successfully');
      setSessionError(null);
    } else {
      console.log('WebSocket disconnected');
      if (sessionActive) {
        setCurrentState('idle');
      }
    }
  }, [sessionActive, setConnectionStatus, setCurrentState]);

  // Service health monitoring functions
  const updateServiceHealth = useCallback((service: keyof Omit<ServiceHealthStatus, 'overall'>, isHealthy: boolean, error?: Error) => {
    setServiceHealthStatus(prev => {
      const updatedHealth = {
        ...prev[service],
        isHealthy,
        lastHealthCheck: new Date(),
        errorCount: error ? prev[service].errorCount + 1 : prev[service].errorCount,
        lastError: error || prev[service].lastError
      };

      const newStatus = {
        ...prev,
        [service]: updatedHealth
      };

      // Calculate overall health
      const services = [newStatus.webSocket, newStatus.audio, newStatus.configuration];
      const healthyServices = services.filter(s => s.isHealthy).length;
      
      let overall: ServiceHealthStatus['overall'];
      if (healthyServices === 0) {
        overall = 'offline';
      } else if (healthyServices === services.length) {
        overall = 'healthy';
      } else if (healthyServices >= services.length / 2) {
        overall = 'degraded';
      } else {
        overall = 'critical';
      }

      newStatus.overall = overall;
      return newStatus;
    });
  }, []);

  // Cross-service error propagation (defined early for use in handlers)
  const propagateServiceError = useCallback((error: Error, sourceService: string, affectedServices?: string[]) => {
    console.error(`Service error in ${sourceService}:`, error);
    
    // Update health for source service
    if (sourceService === 'webSocket') {
      updateServiceHealth('webSocket', false, error);
    } else if (sourceService === 'audio') {
      updateServiceHealth('audio', false, error);
    } else if (sourceService === 'configuration') {
      updateServiceHealth('configuration', false, error);
    }

    // Determine if error should cause session termination
    const criticalErrors = [
      'WebSocket connection failed',
      'Maximum reconnection attempts reached',
      'Microphone permission denied',
      'Audio initialization failed'
    ];

    const isCritical = criticalErrors.some(criticalError => 
      error.message.includes(criticalError)
    );

    if (isCritical && sessionActive) {
      console.warn('Critical service error detected, terminating session');
      setSessionError(error);
      setCurrentState('idle');
    }

    // Propagate error to error handler
    handleError(error, { 
      context: 'Cross-service error propagation', 
      sourceService, 
      affectedServices,
      sessionActive 
    });
  }, [updateServiceHealth, sessionActive, setCurrentState, handleError]);

  const handleWebSocketErrorInternal = useCallback((error: Error) => {
    console.error('WebSocket error:', error);
    setSessionError(error);
    setIsWebSocketReady(false);
    
    // Use cross-service error propagation
    propagateServiceError(error, 'webSocket', ['audio', 'configuration']);
    handleWebSocketError(error);
  }, [propagateServiceError, handleWebSocketError]);

  // Initialize WebSocket integration first
  const webSocketIntegration = useWebSocketIntegration(
    webSocketConfig,
    handleWebSocketMessage,
    handleWebSocketBinaryMessage,
    handleWebSocketConnectionChange,
    handleWebSocketErrorInternal
  );

  // Audio handlers (now that webSocketIntegration is available)
  const handleAudioData = useCallback((data: ArrayBuffer) => {
    // Send audio data through WebSocket with high priority
    if (webSocketIntegration?.isConnected) {
      const messageId = webSocketIntegration.sendBinaryMessage(data, {
        priority: 'high', // Audio data has high priority
        maxAttempts: 1 // Don't retry audio data as it's time-sensitive
      });
      
      if (!messageId) {
        console.warn('Failed to queue audio data - WebSocket not ready');
      }
    }
  }, [webSocketIntegration]);

  const handleAudioErrorInternal = useCallback((error: Error) => {
    console.error('Audio error:', error);
    setSessionError(error);
    setIsAudioReady(false);
    
    // Use cross-service error propagation
    propagateServiceError(error, 'audio', ['webSocket']);
    handleAudioError(error, 'capture');
  }, [propagateServiceError, handleAudioError]);

  const handleAudioStateChange = useCallback((recording: boolean) => {
    setIsAudioReady(recording);
    
    if (recording && sessionActive) {
      if (transitionToStateRef.current) {
        transitionToStateRef.current('listening');
      }
    } else if (!recording && sessionActive) {
      if (transitionToStateRef.current) {
        transitionToStateRef.current('thinking');
      }
    }
  }, [sessionActive]);

  // Initialize Audio integration
  const audioIntegration = useAudioIntegration(
    audioConfig,
    handleAudioData,
    handleAudioErrorInternal,
    handleAudioStateChange
  );

  // Initialize Configuration sync
  const configurationSync = useConfigurationSync(
    webSocketIntegration?.sendMessage || (() => false),
    handleError
  );

  // Initialize Connection resilience - this will be initialized after WebSocket manager is available
  const connectionResilience = useConnectionResilience(null);

  const performServiceHealthCheck = useCallback(async () => {
    try {
      // Check WebSocket health including message protocol health
      if (webSocketIntegration) {
        const wsStats = webSocketIntegration.getConnectionStats();
        const protocolStats = webSocketIntegration.getMessageProtocolStats();
        
        // Consider WebSocket healthy if connected and message protocol is functioning well
        const connectionHealthy = wsStats?.state === 'connected' && !wsStats?.lastError;
        const protocolHealthy = protocolStats.failedMessages === 0 || 
          (protocolStats.sentMessages > 0 && protocolStats.failedMessages / protocolStats.sentMessages < 0.1);
        const queueHealthy = webSocketIntegration.getPendingMessagesCount() < 20;
        
        const wsHealthy = connectionHealthy && protocolHealthy && queueHealthy;
        
        // Create comprehensive error if unhealthy
        let healthError: Error | undefined;
        if (!wsHealthy) {
          const issues = [];
          if (!connectionHealthy) issues.push('connection issues');
          if (!protocolHealthy) issues.push(`high message failure rate (${protocolStats.failedMessages}/${protocolStats.sentMessages})`);
          if (!queueHealthy) issues.push(`large message queue (${webSocketIntegration.getPendingMessagesCount()} pending)`);
          
          healthError = new Error(`WebSocket health issues: ${issues.join(', ')}`);
        }
        
        updateServiceHealth('webSocket', wsHealthy, healthError);
      }

      // Check Audio health
      if (audioIntegration) {
        const audioStats = audioIntegration.getAudioStats();
        const audioHealthy = audioStats?.isInitialized && audioStats?.permissionGranted && !audioStats?.lastError;
        updateServiceHealth('audio', audioHealthy, audioStats?.lastError ? new Error(audioStats.lastError) : undefined);
      }

      // Check Configuration health
      if (configurationSync) {
        const configStats = configurationSync.getSyncStats();
        const configHealthy = configStats && configStats.failedSyncs === 0;
        updateServiceHealth('configuration', configHealthy);
      }

    } catch (error) {
      console.error('Service health check failed:', error);
    }
  }, [webSocketIntegration, audioIntegration, configurationSync, updateServiceHealth]);

  // Service initialization coordination
  const initializeServiceSequence = useCallback(async (): Promise<void> => {
    console.log('Starting coordinated service initialization sequence...');
    
    setServiceInitializationState(prev => ({ ...prev, overall: 'initializing' }));
    
    try {
      // Phase 1: Initialize WebSocket connection
      console.log('Phase 1: Initializing WebSocket connection...');
      setServiceInitializationState(prev => ({ ...prev, webSocket: 'initializing' }));
      
      if (!webSocketIntegration) {
        throw new Error('WebSocket integration not available');
      }
      
      await webSocketIntegration.connect();
      
      // Verify message protocol is ready
      const protocolStats = webSocketIntegration.getMessageProtocolStats();
      console.log('Message protocol initialized:', {
        queuedMessages: protocolStats.queuedMessages,
        lastActivity: protocolStats.lastActivity
      });
      
      setServiceInitializationState(prev => ({ ...prev, webSocket: 'ready' }));
      updateServiceHealth('webSocket', true);
      console.log('✓ WebSocket connection and message protocol established');

      // Phase 2: Sync configuration with backend
      console.log('Phase 2: Synchronizing configuration...');
      setServiceInitializationState(prev => ({ ...prev, configuration: 'initializing' }));
      
      if (!configurationSync) {
        throw new Error('Configuration sync not available');
      }
      
      // Use enhanced message sending for configuration sync
      const configMessageId = webSocketIntegration.sendMessage({
        type: 'config',
        data: {
          sourceLang,
          targetLang,
          voice: selectedVoice
        }
      }, {
        priority: 'normal',
        requiresAck: true,
        maxAttempts: 3
      });
      
      console.log('Configuration message sent:', configMessageId);
      
      // Also use the configuration sync service for additional coordination
      await Promise.all([
        configurationSync.syncLanguageSettings(sourceLang, targetLang),
        configurationSync.syncVoiceSettings(selectedVoice)
      ]);
      
      setServiceInitializationState(prev => ({ ...prev, configuration: 'ready' }));
      updateServiceHealth('configuration', true);
      console.log('✓ Configuration synchronized');

      // Phase 3: Initialize audio capture
      console.log('Phase 3: Initializing audio capture...');
      setServiceInitializationState(prev => ({ ...prev, audio: 'initializing' }));
      
      if (!audioIntegration) {
        throw new Error('Audio integration not available');
      }
      
      await audioIntegration.initialize();
      const devices = await audioIntegration.getAudioDevices();
      setAudioDevices(devices);
      
      await audioIntegration.startRecording();
      
      setServiceInitializationState(prev => ({ ...prev, audio: 'ready' }));
      updateServiceHealth('audio', true);
      console.log('✓ Audio capture initialized');

      // Phase 4: Final coordination
      setServiceInitializationState(prev => ({ ...prev, overall: 'ready' }));
      console.log('✓ All services initialized successfully');

    } catch (error) {
      const initError = error as Error;
      console.error('Service initialization failed:', initError);
      
      // Update failed service state
      if (initError.message.includes('WebSocket') || initError.message.includes('connection')) {
        setServiceInitializationState(prev => ({ ...prev, webSocket: 'failed' }));
        updateServiceHealth('webSocket', false, initError);
      } else if (initError.message.includes('audio') || initError.message.includes('microphone')) {
        setServiceInitializationState(prev => ({ ...prev, audio: 'failed' }));
        updateServiceHealth('audio', false, initError);
      } else if (initError.message.includes('config') || initError.message.includes('sync')) {
        setServiceInitializationState(prev => ({ ...prev, configuration: 'failed' }));
        updateServiceHealth('configuration', false, initError);
      }
      
      setServiceInitializationState(prev => ({ ...prev, overall: 'failed' }));
      throw initError;
    }
  }, [webSocketIntegration, configurationSync, audioIntegration, sourceLang, targetLang, selectedVoice, updateServiceHealth]);

  // Service cleanup coordination
  const cleanupServiceSequence = useCallback(async (): Promise<void> => {
    console.log('Starting coordinated service cleanup sequence...');
    
    try {
      // Execute all registered cleanup callbacks
      serviceCleanupCallbacksRef.current.forEach(cleanup => {
        try {
          cleanup();
        } catch (error) {
          console.error('Service cleanup callback failed:', error);
        }
      });
      serviceCleanupCallbacksRef.current = [];

      // Phase 1: Stop audio recording and cleanup
      console.log('Phase 1: Cleaning up audio services...');
      if (audioIntegration) {
        audioIntegration.stopRecording();
        audioIntegration.cleanup();
      }
      updateServiceHealth('audio', false);

      // Phase 2: Send session end message and cleanup WebSocket connection
      console.log('Phase 2: Cleaning up WebSocket connection...');
      if (webSocketIntegration) {
        // Send session end message before disconnecting
        try {
          const endMessageId = webSocketIntegration.sendMessage({
            type: 'end_session'
          }, {
            priority: 'high',
            requiresAck: true,
            maxAttempts: 1 // Don't retry too much during cleanup
          });
          console.log('Session end message sent:', endMessageId);
          
          // Give a brief moment for the message to be sent
          await new Promise(resolve => setTimeout(resolve, 100));
        } catch (error) {
          console.warn('Failed to send session end message:', error);
        }
        
        // Clear message queue and session state
        webSocketIntegration.clearMessageQueue();
        webSocketIntegration.clearSessionState();
        webSocketIntegration.disconnect();
      }
      updateServiceHealth('webSocket', false);

      // Phase 3: Reset configuration sync state
      console.log('Phase 3: Cleaning up configuration sync...');
      if (configurationSync) {
        configurationSync.clearSyncError();
      }
      updateServiceHealth('configuration', false);

      // Phase 4: Reset local state
      setIsWebSocketReady(false);
      setIsAudioReady(false);
      setSessionError(null);
      currentUtteranceIdRef.current = null;
      
      setServiceInitializationState({
        webSocket: 'pending',
        audio: 'pending',
        configuration: 'pending',
        overall: 'pending'
      });

      console.log('✓ All services cleaned up successfully');

    } catch (error) {
      console.error('Service cleanup failed:', error);
      handleError(error as Error, { context: 'Service cleanup' });
    }
  }, [audioIntegration, webSocketIntegration, configurationSync, updateServiceHealth, handleError]);



  // Register cleanup callback
  const registerCleanupCallback = useCallback((callback: () => void) => {
    serviceCleanupCallbacksRef.current.push(callback);
  }, []);



  /**
   * Start a conversation session with coordinated service integration
   */
  const startSession = useCallback(async () => {
    if (sessionInitializationRef.current) {
      return sessionInitializationRef.current;
    }

    sessionInitializationRef.current = new Promise<void>(async (resolve, reject) => {
      try {
        setConnectionStatus('reconnecting');
        setSessionError(null);
        
        console.log('Starting session with coordinated service integration...');

        // Use coordinated service initialization sequence
        await initializeServiceSequence();
        
        // Set up session state for recovery
        const sessionState = {
          sessionId: `session_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`,
          sourceLang,
          targetLang,
          selectedVoice,
          isActive: true,
          lastActivity: Date.now(),
          pendingUtterances: [],
          currentUtteranceId: null
        };
        
        webSocketIntegration.setSessionState(sessionState);
        
        // Update final state
        setConnectionStatus('connected');
        setCurrentState('listening');
        setIsWebSocketReady(true);
        setIsAudioReady(true);
        
        // Start service health monitoring
        if (serviceHealthCheckIntervalRef.current) {
          clearInterval(serviceHealthCheckIntervalRef.current);
        }
        serviceHealthCheckIntervalRef.current = window.setInterval(performServiceHealthCheck, 5000);
        
        console.log('Session started successfully with all services coordinated');
        resolve();
        
      } catch (error) {
        console.error('Failed to start session:', error);
        const sessionError = error as Error;
        setSessionError(sessionError);
        setConnectionStatus('disconnected');
        setCurrentState('idle');
        setIsWebSocketReady(false);
        setIsAudioReady(false);
        
        // Use coordinated cleanup on failure
        await cleanupServiceSequence();
        
        // Propagate error across services
        propagateServiceError(sessionError, 'session', ['webSocket', 'audio', 'configuration']);
        
        reject(sessionError);
      } finally {
        sessionInitializationRef.current = null;
      }
    });

    return sessionInitializationRef.current;
  }, [initializeServiceSequence, webSocketIntegration, sourceLang, targetLang, selectedVoice, 
      setConnectionStatus, setCurrentState, performServiceHealthCheck, cleanupServiceSequence, propagateServiceError]);

  /**
   * Stop a conversation session with coordinated cleanup
   */
  const stopSession = useCallback(async () => {
    try {
      console.log('Stopping session with coordinated cleanup...');
      
      // Stop service health monitoring
      if (serviceHealthCheckIntervalRef.current) {
        clearInterval(serviceHealthCheckIntervalRef.current);
        serviceHealthCheckIntervalRef.current = null;
      }
      
      // Use coordinated service cleanup sequence
      await cleanupServiceSequence();
      
      // Reset final state
      setConnectionStatus('disconnected');
      resetSession();
      
      console.log('Session stopped successfully with all services cleaned up');
      
    } catch (error) {
      console.error('Failed to stop session:', error);
      propagateServiceError(error as Error, 'session', ['webSocket', 'audio', 'configuration']);
    }
  }, [cleanupServiceSequence, setConnectionStatus, resetSession, propagateServiceError]);

  /**
   * Handle state transitions based on system events with service coordination
   */
  const transitionToState = useCallback((newState: SystemState) => {
    if (!sessionActive && newState !== 'idle') {
      console.warn('Cannot transition to non-idle state when session is inactive');
      return;
    }
    
    console.log(`State transition: ${currentState} -> ${newState}`);
    setCurrentState(newState);
    
    // Coordinate services based on state changes
    try {
      switch (newState) {
        case 'listening':
          // Ensure audio is recording when in listening state
          if (sessionActive && !audioIntegration.isRecording) {
            audioIntegration.startRecording().catch(error => {
              console.error('Failed to start recording during state transition:', error);
              handleAudioError(error, 'capture');
            });
          }
          break;
          
        case 'thinking':
          // Audio should continue recording during thinking
          break;
          
        case 'speaking':
          // Audio recording should continue to capture any interruptions
          break;
          
        case 'idle':
          // Stop audio recording when idle
          if (audioIntegration.isRecording) {
            audioIntegration.stopRecording();
          }
          break;
      }
    } catch (error) {
      console.error('Error during state transition coordination:', error);
      handleError(error as Error, { context: 'State transition', newState, currentState });
    }
  }, [sessionActive, currentState, setCurrentState, audioIntegration, handleError, handleAudioError]);

  // Set the ref for use in callbacks
  transitionToStateRef.current = transitionToState;

  /**
   * Set audio device with coordinated session management and fallback handling
   */
  const setAudioDevice = useCallback(async (deviceId: string): Promise<void> => {
    try {
      setAudioDeviceError(null);
      
      await audioIntegration.setAudioDevice(deviceId);
      
      // Update selected device state
      setSelectedAudioDevice(deviceId);
      
      // Refresh device list to update selection
      const devices = await audioIntegration.getAudioDevices();
      setAudioDevices(devices);
      
      // Update audio service health after successful device change
      updateServiceHealth('audio', true);
      
      console.log('Audio device changed successfully:', deviceId);
    } catch (error) {
      const deviceError = error as Error;
      console.error('Failed to change audio device:', deviceError);
      
      setAudioDeviceError(deviceError);
      
      // Try to fallback to default device if the selected device failed
      if (deviceId !== 'default') {
        console.log('Attempting fallback to default audio device...');
        try {
          await audioIntegration.setAudioDevice('default');
          setSelectedAudioDevice('default');
          
          // Refresh device list
          const devices = await audioIntegration.getAudioDevices();
          setAudioDevices(devices);
          
          console.log('Successfully fell back to default audio device');
          
          // Still throw the original error to notify the user
          throw new Error(`Failed to use selected device "${deviceId}", fell back to default device. Original error: ${deviceError.message}`);
        } catch (fallbackError) {
          console.error('Fallback to default device also failed:', fallbackError);
          
          // Use cross-service error propagation for critical failure
          propagateServiceError(new Error(`Audio device selection failed and fallback unsuccessful: ${deviceError.message}`), 'audio', ['webSocket']);
          
          throw deviceError;
        }
      } else {
        // Use cross-service error propagation
        propagateServiceError(deviceError, 'audio', ['webSocket']);
        throw deviceError;
      }
    }
  }, [audioIntegration, updateServiceHealth, propagateServiceError]);

  /**
   * Enumerate available audio devices with error handling
   */
  const enumerateAudioDevices = useCallback(async (): Promise<MediaDeviceInfo[]> => {
    if (isDeviceEnumerating) {
      return audioDevices; // Return cached devices if enumeration is in progress
    }

    try {
      setIsDeviceEnumerating(true);
      setAudioDeviceError(null);
      
      const devices = await audioIntegration.getAudioDevices();
      setAudioDevices(devices);
      
      // Update selected device if not set or if current device is no longer available
      const currentDeviceId = audioIntegration.getSelectedDeviceId();
      if (currentDeviceId) {
        const deviceExists = devices.some(device => device.deviceId === currentDeviceId);
        if (!deviceExists && devices.length > 0) {
          console.warn(`Selected device ${currentDeviceId} no longer available, switching to default`);
          await setAudioDevice(devices[0].deviceId || 'default');
        } else {
          setSelectedAudioDevice(currentDeviceId);
        }
      } else if (devices.length > 0) {
        // Set first available device as default
        setSelectedAudioDevice(devices[0].deviceId || 'default');
      }
      
      console.log(`Enumerated ${devices.length} audio devices`);
      return devices;
    } catch (error) {
      const enumerationError = error as Error;
      console.error('Failed to enumerate audio devices:', enumerationError);
      setAudioDeviceError(enumerationError);
      
      // Don't propagate as critical error, just log and return empty array
      return [];
    } finally {
      setIsDeviceEnumerating(false);
    }
  }, [audioIntegration, audioDevices, isDeviceEnumerating, setAudioDevice]);

  /**
   * Refresh audio device list and handle device changes
   */
  const refreshAudioDevices = useCallback(async (): Promise<void> => {
    try {
      await enumerateAudioDevices();
      console.log('Audio device list refreshed successfully');
    } catch (error) {
      console.error('Failed to refresh audio devices:', error);
      setAudioDeviceError(error as Error);
    }
  }, [enumerateAudioDevices]);

  /**
   * Get current audio device information
   */
  const getCurrentAudioDevice = useCallback((): MediaDeviceInfo | null => {
    const currentDeviceId = selectedAudioDevice || audioIntegration.getSelectedDeviceId();
    if (!currentDeviceId) return null;
    
    return audioDevices.find(device => device.deviceId === currentDeviceId) || null;
  }, [selectedAudioDevice, audioIntegration, audioDevices]);

  /**
   * Check if audio device is available and working
   */
  const checkAudioDeviceAvailability = useCallback(async (deviceId: string): Promise<boolean> => {
    try {
      const devices = await audioIntegration.getAudioDevices();
      return devices.some(device => device.deviceId === deviceId);
    } catch (error) {
      console.error('Failed to check audio device availability:', error);
      return false;
    }
  }, [audioIntegration]);

  /**
   * Handle audio device disconnection with automatic fallback
   */
  const handleAudioDeviceDisconnection = useCallback(async (disconnectedDeviceId: string): Promise<void> => {
    console.warn(`Audio device disconnected: ${disconnectedDeviceId}`);
    
    try {
      // Refresh device list to get current available devices
      const availableDevices = await enumerateAudioDevices();
      
      if (availableDevices.length === 0) {
        const noDeviceError = new Error('No audio input devices available. Please connect a microphone.');
        setAudioDeviceError(noDeviceError);
        
        // If session is active, this is critical
        if (sessionActive) {
          propagateServiceError(noDeviceError, 'audio', ['webSocket']);
        }
        return;
      }
      
      // Try to fallback to the first available device
      const fallbackDevice = availableDevices[0];
      console.log(`Falling back to device: ${fallbackDevice.label || fallbackDevice.deviceId}`);
      
      await setAudioDevice(fallbackDevice.deviceId);
      
      // Notify user about the device change
      const deviceChangeNotification = new Error(`Audio device "${disconnectedDeviceId}" was disconnected. Switched to "${fallbackDevice.label || fallbackDevice.deviceId}".`);
      setAudioDeviceError(deviceChangeNotification);
      
    } catch (error) {
      console.error('Failed to handle audio device disconnection:', error);
      const fallbackError = new Error(`Audio device disconnected and fallback failed: ${(error as Error).message}`);
      setAudioDeviceError(fallbackError);
      
      if (sessionActive) {
        propagateServiceError(fallbackError, 'audio', ['webSocket']);
      }
    }
  }, [enumerateAudioDevices, setAudioDevice, sessionActive, propagateServiceError]);

  /**
   * Retry connection with coordinated service recovery and message queue processing
   */
  const retryConnection = useCallback(async (): Promise<void> => {
    try {
      console.log('Retrying connection with coordinated service recovery...');
      setSessionError(null);
      setAudioDeviceError(null);
      
      // First cleanup any existing connections
      await cleanupServiceSequence();
      
      // Wait a moment before retrying
      await new Promise(resolve => setTimeout(resolve, 1000));
      
      // Re-initialize all services in coordinated sequence
      await initializeServiceSequence();
      
      // Process any queued messages after reconnection with enhanced protocol handling
      if (webSocketIntegration) {
        console.log('Processing queued messages after reconnection...');
        const queuedCount = webSocketIntegration.getPendingMessagesCount();
        const protocolStats = webSocketIntegration.getMessageProtocolStats();
        
        if (queuedCount > 0) {
          console.log(`Found ${queuedCount} queued messages, processing...`);
          webSocketIntegration.retryFailedMessages();
          
          // Log protocol statistics for debugging
          console.log('Message protocol stats after reconnection:', {
            queuedMessages: protocolStats.queuedMessages,
            failedMessages: protocolStats.failedMessages,
            averageLatency: protocolStats.averageLatency
          });
        }
        
        // Clear any stale message queue if it's too large (potential memory leak prevention)
        if (protocolStats.queuedMessages > 100) {
          console.warn('Message queue is very large, clearing to prevent memory issues');
          webSocketIntegration.clearMessageQueue();
        }
      }
      
      // Update connection state
      setConnectionStatus('connected');
      setIsWebSocketReady(true);
      setIsAudioReady(true);
      
      // Restart health monitoring
      if (serviceHealthCheckIntervalRef.current) {
        clearInterval(serviceHealthCheckIntervalRef.current);
      }
      serviceHealthCheckIntervalRef.current = window.setInterval(performServiceHealthCheck, 5000);
      
      console.log('Connection retry successful with all services coordinated and queued messages processed');
    } catch (error) {
      console.error('Connection retry failed:', error);
      propagateServiceError(error as Error, 'retry', ['webSocket', 'audio', 'configuration']);
      throw error;
    }
  }, [cleanupServiceSequence, initializeServiceSequence, setConnectionStatus, performServiceHealthCheck, propagateServiceError, webSocketIntegration]);

  // Effect to handle session start/stop when sessionActive changes
  useEffect(() => {
    if (sessionActive) {
      startSession().catch(error => {
        console.error('Failed to start session from effect:', error);
      });
    } else {
      stopSession().catch(error => {
        console.error('Failed to stop session from effect:', error);
      });
    }
  }, [sessionActive, startSession, stopSession]);

  // Effect to handle automatic state transitions with service coordination
  useEffect(() => {
    if (!sessionActive) {
      return;
    }

    // Auto-transition from thinking back to listening after processing
    if (currentState === 'thinking') {
      const timer = setTimeout(() => {
        transitionToState('listening');
      }, 3000); // Allow time for backend processing

      return () => clearTimeout(timer);
    }

    // Auto-transition from speaking back to listening after audio playback
    if (currentState === 'speaking') {
      const timer = setTimeout(() => {
        transitionToState('listening');
      }, 2000); // Allow time for TTS audio playback

      return () => clearTimeout(timer);
    }
  }, [currentState, sessionActive, transitionToState]);

  // Effect to sync configuration changes with backend
  useEffect(() => {
    if (sessionActive && webSocketIntegration?.isConnected && configurationSync) {
      configurationSync.syncLanguageSettings(sourceLang, targetLang).catch(error => {
        console.error('Failed to sync language settings:', error);
        propagateServiceError(error as Error, 'configuration');
      });
    }
  }, [sessionActive, sourceLang, targetLang, webSocketIntegration?.isConnected]);

  useEffect(() => {
    if (sessionActive && webSocketIntegration?.isConnected && configurationSync) {
      configurationSync.syncVoiceSettings(selectedVoice).catch(error => {
        console.error('Failed to sync voice settings:', error);
        propagateServiceError(error as Error, 'configuration');
      });
    }
  }, [sessionActive, selectedVoice, webSocketIntegration?.isConnected]);

  // Effect to refresh audio devices periodically and monitor device changes
  useEffect(() => {
    const refreshDevices = async () => {
      try {
        if (audioIntegration) {
          await enumerateAudioDevices();
          updateServiceHealth('audio', true);
        }
      } catch (error) {
        console.error('Failed to refresh audio devices:', error);
        propagateServiceError(error as Error, 'audio');
      }
    };

    // Initial refresh
    refreshDevices();

    // Set up device change listener
    const handleDeviceChange = async () => {
      console.log('Audio device change detected');
      
      try {
        await enumerateAudioDevices();
        
        // Check if the currently selected device is still available
        const currentDeviceId = selectedAudioDevice || audioIntegration.getSelectedDeviceId();
        if (currentDeviceId) {
          const isCurrentDeviceAvailable = await checkAudioDeviceAvailability(currentDeviceId);
          if (!isCurrentDeviceAvailable) {
            await handleAudioDeviceDisconnection(currentDeviceId);
          }
        }
      } catch (error) {
        console.error('Failed to handle device change:', error);
        setAudioDeviceError(error as Error);
      }
    };

    // Add device change listener
    if (navigator.mediaDevices && navigator.mediaDevices.addEventListener) {
      navigator.mediaDevices.addEventListener('devicechange', handleDeviceChange);
    }

    // Refresh every 30 seconds when session is active
    let interval: number | null = null;
    if (sessionActive && audioIntegration) {
      interval = window.setInterval(refreshDevices, 30000);
    }

    return () => {
      if (navigator.mediaDevices && navigator.mediaDevices.removeEventListener) {
        navigator.mediaDevices.removeEventListener('devicechange', handleDeviceChange);
      }
      if (interval) {
        clearInterval(interval);
      }
    };
  }, [sessionActive, audioDevices, selectedAudioDevice, enumerateAudioDevices, checkAudioDeviceAvailability, handleAudioDeviceDisconnection, updateServiceHealth, propagateServiceError]);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      // Stop health monitoring
      if (serviceHealthCheckIntervalRef.current) {
        clearInterval(serviceHealthCheckIntervalRef.current);
      }
      
      // Cleanup session if active
      if (sessionActive) {
        stopSession().catch(error => {
          console.error('Failed to cleanup session on unmount:', error);
        });
      }
    };
  }, []);

  return {
    // Core session state
    sessionActive,
    currentState,
    connectionStatus,
    
    // Integration state
    isAudioReady,
    isWebSocketReady,
    audioDevices,
    selectedAudioDevice,
    audioDeviceError,
    isDeviceEnumerating,
    sessionError,
    
    // Service coordination state
    serviceInitializationState,
    serviceHealthStatus,
    
    // Core actions
    toggleSession,
    transitionToState,
    startSession,
    stopSession,
    
    // Enhanced actions
    setAudioDevice,
    retryConnection,
    
    // Audio device management actions
    enumerateAudioDevices,
    refreshAudioDevices,
    getCurrentAudioDevice,
    checkAudioDeviceAvailability,
    handleAudioDeviceDisconnection,
    
    // Service coordination actions
    performServiceHealthCheck,
    registerCleanupCallback,
    
    // Service access for advanced usage
    webSocketIntegration,
    audioIntegration,
    configurationSync,
    connectionResilience,
    
    // Statistics and monitoring
    getConnectionStats: webSocketIntegration?.getConnectionStats || (() => ({})),
    getAudioStats: audioIntegration?.getAudioStats || (() => ({})),
    getSyncStats: configurationSync?.getSyncStats || (() => ({})),
    getServiceHealthStatus: () => serviceHealthStatus,
    getServiceInitializationState: () => serviceInitializationState,
    
    // Enhanced message protocol integration methods
    getMessageProtocolStats: webSocketIntegration?.getMessageProtocolStats || (() => ({
      queuedMessages: 0,
      sentMessages: 0,
      failedMessages: 0,
      acknowledgedMessages: 0,
      duplicateMessages: 0,
      averageLatency: 0,
      lastActivity: null
    })),
    clearMessageQueue: webSocketIntegration?.clearMessageQueue || (() => {}),
    retryFailedMessages: webSocketIntegration?.retryFailedMessages || (() => {}),
    getPendingMessagesCount: webSocketIntegration?.getPendingMessagesCount || (() => 0),
    
    // Enhanced message protocol methods for better integration
    getMessageQueueHealth: () => {
      const stats = webSocketIntegration?.getMessageProtocolStats() || {
        queuedMessages: 0,
        sentMessages: 0,
        failedMessages: 0,
        acknowledgedMessages: 0,
        duplicateMessages: 0,
        averageLatency: 0,
        lastActivity: null
      };
      
      const pendingCount = webSocketIntegration?.getPendingMessagesCount() || 0;
      const failureRate = stats.sentMessages > 0 ? stats.failedMessages / stats.sentMessages : 0;
      const ackRate = stats.sentMessages > 0 ? stats.acknowledgedMessages / stats.sentMessages : 0;
      
      return {
        ...stats,
        pendingMessages: pendingCount,
        failureRate,
        acknowledgmentRate: ackRate,
        health: failureRate < 0.1 ? 'good' : failureRate < 0.3 ? 'warning' : 'critical',
        averageLatencyMs: Math.round(stats.averageLatency),
        isHealthy: failureRate < 0.1 && pendingCount < 10 && stats.averageLatency < 5000
      };
    },
    
    // Message protocol diagnostics
    getMessageProtocolDiagnostics: () => {
      const connectionStats = webSocketIntegration?.getConnectionStats();
      const protocolStats = webSocketIntegration?.getMessageProtocolStats();
      
      return {
        connection: {
          state: connectionStats?.state || 'unknown',
          quality: connectionStats?.quality || 'unknown',
          reconnectAttempts: connectionStats?.reconnectAttempts || 0,
          lastError: connectionStats?.lastError || null
        },
        protocol: protocolStats || {
          queuedMessages: 0,
          sentMessages: 0,
          failedMessages: 0,
          acknowledgedMessages: 0,
          duplicateMessages: 0,
          averageLatency: 0,
          lastActivity: null
        },
        session: {
          sessionId: webSocketIntegration?.getSessionState()?.sessionId || null,
          isActive: sessionActive,
          lastActivity: webSocketIntegration?.getSessionState()?.lastActivity || null,
          currentUtteranceId: currentUtteranceIdRef.current
        }
      };
    },
    
    // Enhanced message sending with protocol options and session context
    sendMessage: (message: ClientMessage, options?: {
      priority?: 'low' | 'normal' | 'high';
      requiresAck?: boolean;
      maxAttempts?: number;
    }) => {
      if (!webSocketIntegration) {
        console.warn('Cannot send message - WebSocket integration not available');
        return '';
      }
      
      // Add session context to message if available
      const sessionState = webSocketIntegration.getSessionState();
      if (sessionState && message.type !== 'ping') {
        // Update session activity when sending messages
        sessionState.lastActivity = Date.now();
        webSocketIntegration.setSessionState(sessionState);
      }
      
      // Determine appropriate priority based on message type
      const priority = options?.priority || (() => {
        switch (message.type) {
          case 'end_session':
          case 'recover_session':
            return 'high';
          case 'config':
            return 'normal';
          case 'ping':
            return 'low';
          default:
            return 'normal';
        }
      })();
      
      // Determine if acknowledgment is needed based on message type and session state
      const requiresAck = options?.requiresAck ?? (() => {
        switch (message.type) {
          case 'config':
          case 'end_session':
          case 'recover_session':
            return true;
          case 'ping':
            return false;
          default:
            return false;
        }
      })();
      
      return webSocketIntegration.sendMessage(message, {
        ...options,
        priority,
        requiresAck
      });
    },
    
    sendBinaryMessage: (data: ArrayBuffer, options?: {
      priority?: 'low' | 'normal' | 'high';
      maxAttempts?: number;
    }) => {
      if (!webSocketIntegration) {
        console.warn('Cannot send binary message - WebSocket integration not available');
        return '';
      }
      
      // Update session activity when sending binary data
      const sessionState = webSocketIntegration.getSessionState();
      if (sessionState) {
        sessionState.lastActivity = Date.now();
        webSocketIntegration.setSessionState(sessionState);
      }
      
      // Binary messages (audio data) typically have high priority and shouldn't be retried
      return webSocketIntegration.sendBinaryMessage(data, {
        priority: options?.priority || 'high',
        maxAttempts: options?.maxAttempts || 1
      });
    },
    
    // Convenience methods for common message types
    sendConfigurationMessage: (sourceLang: string, targetLang: string, voice: string) => {
      return webSocketIntegration?.sendMessage({
        type: 'config',
        data: {
          sourceLang,
          targetLang,
          voice
        }
      }, {
        priority: 'normal',
        requiresAck: true,
        maxAttempts: 3
      }) || '';
    },
    
    sendSessionEndMessage: () => {
      return webSocketIntegration?.sendMessage({
        type: 'end_session'
      }, {
        priority: 'high',
        requiresAck: true,
        maxAttempts: 2
      }) || '';
    },
    
    sendSessionRecoveryMessage: (sessionId: string, lastActivity: number, sessionState: any) => {
      return webSocketIntegration?.sendMessage({
        type: 'recover_session',
        sessionId,
        lastActivity,
        sessionState
      }, {
        priority: 'high',
        requiresAck: true,
        maxAttempts: 3
      }) || '';
    },
  };
};