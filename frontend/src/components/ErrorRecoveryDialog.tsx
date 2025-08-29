import React, { useState, useEffect } from 'react';
import { AlertTriangle, RefreshCw, Wifi, Mic } from 'lucide-react';
import { Button } from './ui/button';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from './ui/dialog';
import { Alert, AlertDescription } from './ui/alert';
import { Progress } from './ui/progress';
import { type AppError, ErrorType } from './ErrorNotification';

interface ErrorRecoveryDialogProps {
  error: AppError | null;
  isOpen: boolean;
  onClose: () => void;
  onRetry: () => void;
  onSkip?: () => void;
  onReportIssue?: () => void;
}

interface RecoveryStep {
  id: string;
  title: string;
  description: string;
  action: () => Promise<boolean>;
  icon: React.ComponentType<{ className?: string }>;
}

/**
 * Enhanced error recovery dialog that provides step-by-step recovery guidance
 */
export function ErrorRecoveryDialog({
  error,
  isOpen,
  onClose,
  onRetry,
  onSkip,
  onReportIssue
}: ErrorRecoveryDialogProps) {
  const [currentStep, setCurrentStep] = useState(0);
  const [isRecovering, setIsRecovering] = useState(false);
  const [recoveryProgress, setRecoveryProgress] = useState(0);
  const [recoverySteps, setRecoverySteps] = useState<RecoveryStep[]>([]);
  const [stepResults, setStepResults] = useState<boolean[]>([]);

  useEffect(() => {
    if (error && isOpen) {
      setRecoverySteps(generateRecoverySteps(error));
      setCurrentStep(0);
      setStepResults([]);
      setRecoveryProgress(0);
    }
  }, [error, isOpen]);

  const generateRecoverySteps = (error: AppError): RecoveryStep[] => {
    const steps: RecoveryStep[] = [];

    switch (error.type) {
      case ErrorType.WEBSOCKET_CONNECTION:
        steps.push(
          {
            id: 'check-network',
            title: 'Check Network Connection',
            description: 'Verify your internet connection is stable',
            icon: Wifi,
            action: async () => {
              // Check if we can reach the server
              try {
                const response = await fetch('/api/health', { 
                  method: 'HEAD',
                  signal: AbortSignal.timeout(5000)
                });
                return response.ok;
              } catch {
                return false;
              }
            }
          },
          {
            id: 'reconnect-websocket',
            title: 'Reconnect to Server',
            description: 'Attempt to re-establish connection with the server',
            icon: RefreshCw,
            action: async () => {
              // Trigger WebSocket reconnection
              onRetry();
              await new Promise(resolve => setTimeout(resolve, 2000));
              return true;
            }
          }
        );
        break;

      case ErrorType.AUDIO_PERMISSION:
        steps.push(
          {
            id: 'request-permission',
            title: 'Request Microphone Permission',
            description: 'Grant microphone access for speech translation',
            icon: Mic,
            action: async () => {
              try {
                const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
                stream.getTracks().forEach(track => track.stop());
                return true;
              } catch {
                return false;
              }
            }
          },
          {
            id: 'check-microphone',
            title: 'Test Microphone',
            description: 'Verify microphone is working properly',
            icon: Mic,
            action: async () => {
              try {
                const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
                const audioContext = new AudioContext();
                const source = audioContext.createMediaStreamSource(stream);
                const analyser = audioContext.createAnalyser();
                source.connect(analyser);
                
                // Test for audio input
                const dataArray = new Uint8Array(analyser.frequencyBinCount);
                analyser.getByteFrequencyData(dataArray);
                
                stream.getTracks().forEach(track => track.stop());
                audioContext.close();
                
                return dataArray.some(value => value > 0);
              } catch {
                return false;
              }
            }
          }
        );
        break;

      case ErrorType.AUDIO_CAPTURE:
        steps.push(
          {
            id: 'check-microphone-device',
            title: 'Check Microphone Device',
            description: 'Verify microphone device is available and not in use',
            icon: Mic,
            action: async () => {
              try {
                const devices = await navigator.mediaDevices.enumerateDevices();
                return devices.some(device => device.kind === 'audioinput');
              } catch {
                return false;
              }
            }
          },
          {
            id: 'restart-audio-capture',
            title: 'Restart Audio Capture',
            description: 'Reset audio capture system',
            icon: RefreshCw,
            action: async () => {
              onRetry();
              await new Promise(resolve => setTimeout(resolve, 1000));
              return true;
            }
          }
        );
        break;

      case ErrorType.TRANSCRIPTION:
      case ErrorType.TRANSLATION:
      case ErrorType.SYNTHESIS:
        steps.push(
          {
            id: 'check-server-connection',
            title: 'Check Server Connection',
            description: 'Verify connection to processing server',
            icon: Wifi,
            action: async () => {
              try {
                const response = await fetch('/api/health', { 
                  method: 'HEAD',
                  signal: AbortSignal.timeout(3000)
                });
                return response.ok;
              } catch {
                return false;
              }
            }
          },
          {
            id: 'retry-processing',
            title: 'Retry Processing',
            description: 'Attempt to process the request again',
            icon: RefreshCw,
            action: async () => {
              onRetry();
              await new Promise(resolve => setTimeout(resolve, 2000));
              return true;
            }
          }
        );
        break;

      default:
        steps.push(
          {
            id: 'general-retry',
            title: 'Retry Operation',
            description: 'Attempt to resolve the issue automatically',
            icon: RefreshCw,
            action: async () => {
              onRetry();
              await new Promise(resolve => setTimeout(resolve, 1000));
              return true;
            }
          }
        );
        break;
    }

    return steps;
  };

  const executeRecoveryStep = async (stepIndex: number) => {
    if (stepIndex >= recoverySteps.length) return;

    setIsRecovering(true);
    const step = recoverySteps[stepIndex];

    try {
      const result = await step.action();
      const newResults = [...stepResults];
      newResults[stepIndex] = result;
      setStepResults(newResults);

      if (result) {
        // Step succeeded, move to next step or complete
        if (stepIndex < recoverySteps.length - 1) {
          setCurrentStep(stepIndex + 1);
          setRecoveryProgress(((stepIndex + 1) / recoverySteps.length) * 100);
        } else {
          // All steps completed successfully
          setRecoveryProgress(100);
          setTimeout(() => {
            onClose();
          }, 1500);
        }
      } else {
        // Step failed, show options
        setRecoveryProgress(((stepIndex + 1) / recoverySteps.length) * 100);
      }
    } catch (error) {
      console.error('Recovery step failed:', error);
      const newResults = [...stepResults];
      newResults[stepIndex] = false;
      setStepResults(newResults);
    } finally {
      setIsRecovering(false);
    }
  };

  const executeAllSteps = async () => {
    setIsRecovering(true);
    setCurrentStep(0);
    setStepResults([]);
    setRecoveryProgress(0);

    for (let i = 0; i < recoverySteps.length; i++) {
      setCurrentStep(i);
      await executeRecoveryStep(i);
      
      // If step failed and it's critical, stop
      if (stepResults[i] === false && i === 0) {
        break;
      }
    }

    setIsRecovering(false);
  };

  const getErrorTitle = (error: AppError): string => {
    switch (error.type) {
      case ErrorType.WEBSOCKET_CONNECTION:
        return 'Connection Problem';
      case ErrorType.AUDIO_PERMISSION:
        return 'Microphone Access Required';
      case ErrorType.AUDIO_CAPTURE:
        return 'Audio Capture Issue';
      case ErrorType.TRANSCRIPTION:
        return 'Speech Recognition Problem';
      case ErrorType.TRANSLATION:
        return 'Translation Service Issue';
      case ErrorType.SYNTHESIS:
        return 'Speech Synthesis Problem';
      case ErrorType.PIPELINE:
        return 'Processing Pipeline Error';
      default:
        return 'Unexpected Error';
    }
  };

  const getErrorDescription = (error: AppError): string => {
    switch (error.type) {
      case ErrorType.WEBSOCKET_CONNECTION:
        return 'The connection to the server was lost. This might be due to network issues or server maintenance.';
      case ErrorType.AUDIO_PERMISSION:
        return 'Microphone access is required for speech translation. Please grant permission to continue.';
      case ErrorType.AUDIO_CAPTURE:
        return 'There was a problem capturing audio from your microphone. This could be due to device issues or conflicting applications.';
      case ErrorType.TRANSCRIPTION:
        return 'The speech recognition service encountered an error. This might be temporary.';
      case ErrorType.TRANSLATION:
        return 'The translation service is currently unavailable. This is usually temporary.';
      case ErrorType.SYNTHESIS:
        return 'There was a problem generating speech from the translated text.';
      case ErrorType.PIPELINE:
        return 'The processing pipeline encountered an error. This affects the entire translation process.';
      default:
        return 'An unexpected error occurred. We\'ll try to resolve it automatically.';
    }
  };

  if (!error || !isOpen) {
    return null;
  }

  const currentStepData = recoverySteps[currentStep];
  const allStepsCompleted = stepResults.length === recoverySteps.length && stepResults.every(result => result);
  const hasFailedSteps = stepResults.some(result => result === false);

  return (
    <Dialog open={isOpen} onOpenChange={onClose}>
      <DialogContent className="sm:max-w-md">
        <DialogHeader>
          <DialogTitle className="flex items-center gap-2">
            <AlertTriangle className="h-5 w-5 text-orange-500" />
            {getErrorTitle(error)}
          </DialogTitle>
          <DialogDescription>
            {getErrorDescription(error)}
          </DialogDescription>
        </DialogHeader>

        <div className="space-y-4">
          {/* Progress bar */}
          <div className="space-y-2">
            <div className="flex justify-between text-sm">
              <span>Recovery Progress</span>
              <span>{Math.round(recoveryProgress)}%</span>
            </div>
            <Progress value={recoveryProgress} className="w-full" />
          </div>

          {/* Current step */}
          {currentStepData && (
            <Alert>
              <currentStepData.icon className="h-4 w-4" />
              <AlertDescription>
                <div className="space-y-2">
                  <div className="font-medium">{currentStepData.title}</div>
                  <div className="text-sm text-muted-foreground">
                    {currentStepData.description}
                  </div>
                </div>
              </AlertDescription>
            </Alert>
          )}

          {/* Step results */}
          {stepResults.length > 0 && (
            <div className="space-y-2">
              <div className="text-sm font-medium">Recovery Steps:</div>
              {recoverySteps.map((step, index) => (
                <div key={step.id} className="flex items-center gap-2 text-sm">
                  <step.icon className="h-4 w-4" />
                  <span className="flex-1">{step.title}</span>
                  {index < stepResults.length && (
                    <span className={stepResults[index] ? 'text-green-600' : 'text-red-600'}>
                      {stepResults[index] ? '✓' : '✗'}
                    </span>
                  )}
                  {index === currentStep && isRecovering && (
                    <RefreshCw className="h-4 w-4 animate-spin" />
                  )}
                </div>
              ))}
            </div>
          )}

          {/* Success message */}
          {allStepsCompleted && (
            <Alert className="border-green-200 bg-green-50">
              <AlertDescription className="text-green-800">
                Recovery completed successfully! The issue should be resolved.
              </AlertDescription>
            </Alert>
          )}

          {/* Failure message */}
          {hasFailedSteps && !isRecovering && (
            <Alert className="border-orange-200 bg-orange-50">
              <AlertDescription className="text-orange-800">
                Some recovery steps failed. You can try manual solutions or report the issue.
              </AlertDescription>
            </Alert>
          )}
        </div>

        <DialogFooter className="flex-col sm:flex-row gap-2">
          {!allStepsCompleted && !isRecovering && (
            <>
              <Button
                onClick={executeAllSteps}
                disabled={isRecovering}
                className="w-full sm:w-auto"
              >
                <RefreshCw className="h-4 w-4 mr-2" />
                Start Recovery
              </Button>
              
              {currentStepData && (
                <Button
                  variant="outline"
                  onClick={() => executeRecoveryStep(currentStep)}
                  disabled={isRecovering}
                  className="w-full sm:w-auto"
                >
                  Try This Step
                </Button>
              )}
            </>
          )}

          {hasFailedSteps && !isRecovering && (
            <>
              {onSkip && (
                <Button
                  variant="outline"
                  onClick={onSkip}
                  className="w-full sm:w-auto"
                >
                  Skip & Continue
                </Button>
              )}
              
              {onReportIssue && (
                <Button
                  variant="outline"
                  onClick={onReportIssue}
                  className="w-full sm:w-auto"
                >
                  Report Issue
                </Button>
              )}
            </>
          )}

          <Button
            variant={allStepsCompleted ? "default" : "secondary"}
            onClick={onClose}
            className="w-full sm:w-auto"
          >
            {allStepsCompleted ? 'Done' : 'Cancel'}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

/**
 * Hook for managing error recovery dialog state
 */
export function useErrorRecoveryDialog() {
  const [isOpen, setIsOpen] = useState(false);
  const [currentError, setCurrentError] = useState<AppError | null>(null);

  const showRecoveryDialog = (error: AppError) => {
    setCurrentError(error);
    setIsOpen(true);
  };

  const hideRecoveryDialog = () => {
    setIsOpen(false);
    setCurrentError(null);
  };

  return {
    isOpen,
    currentError,
    showRecoveryDialog,
    hideRecoveryDialog
  };
}