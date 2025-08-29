import { useEffect, useState } from 'react';
import { AlertCircle, X, RefreshCw, Wifi, WifiOff, Loader2, ChevronDown, ChevronRight } from 'lucide-react';
import { Alert, AlertDescription, AlertTitle } from './ui/alert';
import { Button } from './ui/button';
import { cn } from '../lib/utils';
import { useErrorHandler } from '../hooks/useErrorHandler';
import { errorRecoveryService } from '../services/ErrorRecoveryService';

export interface ErrorNotificationProps {
  error: AppError | null;
  onDismiss: () => void;
  onRetry?: () => void;
  className?: string;
}

export interface AppError {
  id: string;
  type: ErrorType;
  message: string;
  details?: string;
  timestamp: Date;
  recoverable: boolean;
  retryable: boolean;
  context?: Record<string, any>;
}

export enum ErrorType {
  WEBSOCKET_CONNECTION = 'websocket_connection',
  WEBSOCKET_RECONNECTION = 'websocket_reconnection',
  AUDIO_PERMISSION = 'audio_permission',
  AUDIO_CAPTURE = 'audio_capture',
  AUDIO_PLAYBACK = 'audio_playback',
  TRANSCRIPTION = 'transcription',
  TRANSLATION = 'translation',
  SYNTHESIS = 'synthesis',
  PIPELINE = 'pipeline',
  NETWORK = 'network',
  UNKNOWN = 'unknown',
}

const ERROR_ICONS = {
  [ErrorType.WEBSOCKET_CONNECTION]: WifiOff,
  [ErrorType.WEBSOCKET_RECONNECTION]: Wifi,
  [ErrorType.AUDIO_PERMISSION]: AlertCircle,
  [ErrorType.AUDIO_CAPTURE]: AlertCircle,
  [ErrorType.AUDIO_PLAYBACK]: AlertCircle,
  [ErrorType.TRANSCRIPTION]: AlertCircle,
  [ErrorType.TRANSLATION]: AlertCircle,
  [ErrorType.SYNTHESIS]: AlertCircle,
  [ErrorType.PIPELINE]: AlertCircle,
  [ErrorType.NETWORK]: WifiOff,
  [ErrorType.UNKNOWN]: AlertCircle,
};

const ERROR_TITLES = {
  [ErrorType.WEBSOCKET_CONNECTION]: 'Connection Failed',
  [ErrorType.WEBSOCKET_RECONNECTION]: 'Reconnecting...',
  [ErrorType.AUDIO_PERMISSION]: 'Microphone Access Required',
  [ErrorType.AUDIO_CAPTURE]: 'Audio Capture Error',
  [ErrorType.AUDIO_PLAYBACK]: 'Audio Playback Error',
  [ErrorType.TRANSCRIPTION]: 'Transcription Error',
  [ErrorType.TRANSLATION]: 'Translation Error',
  [ErrorType.SYNTHESIS]: 'Speech Synthesis Error',
  [ErrorType.PIPELINE]: 'Processing Error',
  [ErrorType.NETWORK]: 'Network Error',
  [ErrorType.UNKNOWN]: 'Unexpected Error',
};

const ERROR_VARIANTS = {
  [ErrorType.WEBSOCKET_CONNECTION]: 'destructive' as const,
  [ErrorType.WEBSOCKET_RECONNECTION]: 'default' as const,
  [ErrorType.AUDIO_PERMISSION]: 'destructive' as const,
  [ErrorType.AUDIO_CAPTURE]: 'destructive' as const,
  [ErrorType.AUDIO_PLAYBACK]: 'default' as const,
  [ErrorType.TRANSCRIPTION]: 'default' as const,
  [ErrorType.TRANSLATION]: 'default' as const,
  [ErrorType.SYNTHESIS]: 'default' as const,
  [ErrorType.PIPELINE]: 'default' as const,
  [ErrorType.NETWORK]: 'destructive' as const,
  [ErrorType.UNKNOWN]: 'destructive' as const,
};

/**
 * ErrorNotification - Displays user-friendly error messages with appropriate actions
 */
export function ErrorNotification({ 
  error, 
  onDismiss, 
  onRetry, 
  className 
}: ErrorNotificationProps) {
  const [isVisible, setIsVisible] = useState(false);
  const [autoHideTimer, setAutoHideTimer] = useState<number | null>(null);
  const [showGuidance, setShowGuidance] = useState(false);
  const [isRetrying, setIsRetrying] = useState(false);
  const [recoveryProgress, setRecoveryProgress] = useState<string>('');
  
  const { 
    retryLastOperation, 
    getErrorGuidance, 
    isRecoveryInProgress 
  } = useErrorHandler();

  useEffect(() => {
    if (error) {
      setIsVisible(true);
      setIsRetrying(isRecoveryInProgress(error.id));
      
      // Auto-hide non-critical errors after 8 seconds (increased for guidance)
      if (error.recoverable && error.type !== ErrorType.WEBSOCKET_CONNECTION && error.type !== ErrorType.AUDIO_PERMISSION) {
        const timer = window.setTimeout(() => {
          handleDismiss();
        }, 8000);
        setAutoHideTimer(timer);
      }
    } else {
      setIsVisible(false);
      setIsRetrying(false);
      setRecoveryProgress('');
    }

    return () => {
      if (autoHideTimer) {
        clearTimeout(autoHideTimer);
      }
    };
  }, [error]);

  // Monitor recovery progress
  useEffect(() => {
    if (!error) return;

    const checkRecoveryProgress = () => {
      const inProgress = isRecoveryInProgress(error.id);
      setIsRetrying(inProgress);
      
      if (inProgress) {
        const session = errorRecoveryService.getRecoverySession(error.id);
        if (session) {
          const attemptCount = session.attempts.length;
          const maxRetries = session.attempts[0]?.strategy?.maxRetries || 1;
          setRecoveryProgress(`Attempting recovery (${attemptCount}/${maxRetries})...`);
        }
      } else {
        setRecoveryProgress('');
      }
    };

    checkRecoveryProgress();
    const interval = setInterval(checkRecoveryProgress, 500);

    return () => clearInterval(interval);
  }, [error, isRecoveryInProgress]);

  const handleDismiss = () => {
    if (autoHideTimer) {
      clearTimeout(autoHideTimer);
      setAutoHideTimer(null);
    }
    setIsVisible(false);
    setTimeout(onDismiss, 300); // Allow fade out animation
  };

  const handleRetry = async () => {
    if (autoHideTimer) {
      clearTimeout(autoHideTimer);
      setAutoHideTimer(null);
    }

    setIsRetrying(true);
    setRecoveryProgress('Retrying...');

    try {
      // Try the enhanced retry mechanism first
      const success = await retryLastOperation();
      
      if (success) {
        handleDismiss();
        return;
      }

      // Fall back to the provided onRetry callback
      if (onRetry) {
        onRetry();
        handleDismiss();
      }
    } catch (error) {
      console.error('Retry failed:', error);
    } finally {
      setIsRetrying(false);
      setRecoveryProgress('');
    }
  };

  if (!error || !isVisible) {
    return null;
  }

  const Icon = ERROR_ICONS[error.type];
  const title = ERROR_TITLES[error.type];
  const variant = ERROR_VARIANTS[error.type];
  const guidance = getErrorGuidance(error.type);
  const classification = error.context?.classification;

  return (
    <div className={cn(
      "fixed top-4 right-4 z-50 w-96 max-w-[calc(100vw-2rem)]",
      "animate-in slide-in-from-right-full duration-300",
      className
    )}>
      <Alert variant={variant} className="shadow-lg border">
        <Icon className="h-4 w-4" />
        <AlertTitle className="flex items-center justify-between">
          <div className="flex items-center gap-2">
            {title}
            {classification?.severity === 'critical' && (
              <span className="text-xs bg-red-100 text-red-800 px-1.5 py-0.5 rounded">
                Critical
              </span>
            )}
          </div>
          <Button
            variant="ghost"
            size="sm"
            className="h-6 w-6 p-0 hover:bg-transparent"
            onClick={handleDismiss}
          >
            <X className="h-4 w-4" />
          </Button>
        </AlertTitle>
        <AlertDescription className="mt-2">
          <div className="space-y-3">
            <p>{error.message}</p>
            
            {error.details && (
              <p className="text-sm opacity-80">{error.details}</p>
            )}

            {/* Recovery Progress */}
            {isRetrying && recoveryProgress && (
              <div className="flex items-center gap-2 text-sm text-blue-600">
                <Loader2 className="h-3 w-3 animate-spin" />
                {recoveryProgress}
              </div>
            )}
            
            {/* Action Buttons */}
            <div className="flex gap-2 mt-3">
              {error.retryable && (
                <Button
                  size="sm"
                  variant="outline"
                  onClick={handleRetry}
                  disabled={isRetrying}
                  className="h-8"
                >
                  {isRetrying ? (
                    <Loader2 className="h-3 w-3 mr-1 animate-spin" />
                  ) : (
                    <RefreshCw className="h-3 w-3 mr-1" />
                  )}
                  {isRetrying ? 'Retrying...' : 'Retry'}
                </Button>
              )}
              
              {error.type === ErrorType.AUDIO_PERMISSION && (
                <Button
                  size="sm"
                  variant="outline"
                  onClick={() => {
                    // Trigger permission request
                    navigator.mediaDevices.getUserMedia({ audio: true })
                      .then(() => handleDismiss())
                      .catch(() => {
                        // Permission still denied, keep error visible
                      });
                  }}
                  className="h-8"
                >
                  Grant Permission
                </Button>
              )}

              {/* Help/Guidance Toggle */}
              {guidance.length > 0 && (
                <Button
                  size="sm"
                  variant="ghost"
                  onClick={() => setShowGuidance(!showGuidance)}
                  className="h-8"
                >
                  {showGuidance ? (
                    <ChevronDown className="h-3 w-3 mr-1" />
                  ) : (
                    <ChevronRight className="h-3 w-3 mr-1" />
                  )}
                  Help
                </Button>
              )}
            </div>

            {/* Recovery Guidance */}
            {guidance.length > 0 && showGuidance && (
              <div className="border-t pt-3 mt-3 animate-in slide-in-from-top-2 duration-200">
                <h4 className="text-sm font-medium mb-2 flex items-center gap-1">
                  <ChevronDown className="h-3 w-3" />
                  How to fix this:
                </h4>
                <ol className="text-sm space-y-1 list-decimal list-inside opacity-90">
                  {guidance.map((step, index) => (
                    <li key={index}>{step}</li>
                  ))}
                </ol>
              </div>
            )}

            {/* Error Classification Info (Development Only) */}
            {process.env.NODE_ENV === 'development' && classification && (
              <details className="text-xs opacity-60">
                <summary className="cursor-pointer">Debug Info</summary>
                <div className="mt-1 space-y-1">
                  <div>Category: {classification.category}</div>
                  <div>Severity: {classification.severity}</div>
                  <div>Impact: {classification.impact}</div>
                  <div>Session Terminating: {classification.sessionTerminating ? 'Yes' : 'No'}</div>
                </div>
              </details>
            )}
          </div>
        </AlertDescription>
      </Alert>
    </div>
  );
}

/**
 * Utility functions for creating standardized error objects
 */
export class ErrorFactory {
  private static generateId(): string {
    return `error_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
  }

  static createWebSocketError(message: string, details?: string): AppError {
    return {
      id: this.generateId(),
      type: ErrorType.WEBSOCKET_CONNECTION,
      message,
      details,
      timestamp: new Date(),
      recoverable: true,
      retryable: true,
    };
  }

  static createAudioPermissionError(): AppError {
    return {
      id: this.generateId(),
      type: ErrorType.AUDIO_PERMISSION,
      message: 'Microphone access is required for speech translation.',
      details: 'Please grant microphone permission and try again.',
      timestamp: new Date(),
      recoverable: true,
      retryable: true,
    };
  }

  static createAudioCaptureError(message: string): AppError {
    return {
      id: this.generateId(),
      type: ErrorType.AUDIO_CAPTURE,
      message: 'Failed to capture audio from microphone.',
      details: message,
      timestamp: new Date(),
      recoverable: true,
      retryable: true,
    };
  }

  static createAudioPlaybackError(message: string): AppError {
    return {
      id: this.generateId(),
      type: ErrorType.AUDIO_PLAYBACK,
      message: 'Failed to play translated audio.',
      details: message,
      timestamp: new Date(),
      recoverable: true,
      retryable: false,
    };
  }

  static createTranscriptionError(message?: string): AppError {
    return {
      id: this.generateId(),
      type: ErrorType.TRANSCRIPTION,
      message: 'Failed to transcribe speech.',
      details: message || 'The speech could not be processed. Please try speaking again.',
      timestamp: new Date(),
      recoverable: true,
      retryable: false,
    };
  }

  static createTranslationError(message?: string): AppError {
    return {
      id: this.generateId(),
      type: ErrorType.TRANSLATION,
      message: 'Failed to translate text.',
      details: message || 'The translation service is temporarily unavailable.',
      timestamp: new Date(),
      recoverable: true,
      retryable: true,
    };
  }

  static createSynthesisError(message?: string): AppError {
    return {
      id: this.generateId(),
      type: ErrorType.SYNTHESIS,
      message: 'Failed to generate speech.',
      details: message || 'The text-to-speech service encountered an error.',
      timestamp: new Date(),
      recoverable: true,
      retryable: true,
    };
  }

  static createPipelineError(message: string, context?: Record<string, any>): AppError {
    return {
      id: this.generateId(),
      type: ErrorType.PIPELINE,
      message: 'Processing pipeline error.',
      details: message,
      timestamp: new Date(),
      recoverable: true,
      retryable: true,
      context,
    };
  }

  static createNetworkError(message: string): AppError {
    return {
      id: this.generateId(),
      type: ErrorType.NETWORK,
      message: 'Network connection error.',
      details: message,
      timestamp: new Date(),
      recoverable: true,
      retryable: true,
    };
  }

  static createUnknownError(error: Error): AppError {
    return {
      id: this.generateId(),
      type: ErrorType.UNKNOWN,
      message: 'An unexpected error occurred.',
      details: error.message,
      timestamp: new Date(),
      recoverable: true,
      retryable: true,
      context: {
        stack: error.stack,
        name: error.name,
      },
    };
  }
}