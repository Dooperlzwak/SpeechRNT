import React, { useState, useEffect } from 'react';
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@/components/ui/dialog';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select';
import { Button } from '@/components/ui/button';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { Badge } from '@/components/ui/badge';
import { Volume2, Mic, AlertCircle, Loader2, Headphones, RefreshCw } from 'lucide-react';
import {
  AVAILABLE_LANGUAGES,
  AVAILABLE_MODELS,

  getVoicesForLanguage,
  validateSettings,
  saveSettingsToStorage,

} from '../utils/settingsValidation';

interface SettingsDialogProps {
  isOpen: boolean;
  onClose: () => void;
  sourceLang: string;
  targetLang: string;
  selectedVoice: string;
  selectedModel: string;
  onLanguageChange: (source: string, target: string) => Promise<void>;
  onVoiceChange: (voice: string) => Promise<void>;
  onModelChange: (model: string) => Promise<void>;
  isLanguageSyncing?: boolean;
  isVoiceSyncing?: boolean;
  configSyncError?: Error | null;
  onClearConfigError?: () => void;
  onRetryConfigSync?: () => Promise<void>;
  // Audio device management props
  audioDevices?: MediaDeviceInfo[];
  selectedAudioDevice?: string | null;
  onAudioDeviceChange?: (deviceId: string) => Promise<void>;
  isAudioDeviceSyncing?: boolean;
  audioDeviceError?: Error | null;
  isDeviceEnumerating?: boolean;
  onRefreshAudioDevices?: () => Promise<void>;
}

const SettingsDialog: React.FC<SettingsDialogProps> = ({
  isOpen,
  onClose,
  sourceLang,
  targetLang,
  selectedVoice,
  selectedModel,
  onLanguageChange,
  onVoiceChange,
  onModelChange,
  isLanguageSyncing = false,
  isVoiceSyncing = false,
  configSyncError = null,
  onClearConfigError,
  onRetryConfigSync,
  audioDevices = [],
  selectedAudioDevice = null,
  onAudioDeviceChange,
  isAudioDeviceSyncing = false,
  audioDeviceError = null,
  isDeviceEnumerating = false,
  onRefreshAudioDevices
}) => {
  const [tempSourceLang, setTempSourceLang] = useState(sourceLang);
  const [tempTargetLang, setTempTargetLang] = useState(targetLang);
  const [tempSelectedVoice, setTempSelectedVoice] = useState(selectedVoice);
  const [tempSelectedModel, setTempSelectedModel] = useState(selectedModel);
  const [tempSelectedAudioDevice, setTempSelectedAudioDevice] = useState(selectedAudioDevice);
  const [validationError, setValidationError] = useState<string | null>(null);
  const [isSaving, setIsSaving] = useState(false);



  // Reset temp values when dialog opens
  useEffect(() => {
    if (isOpen) {
      setTempSourceLang(sourceLang);
      setTempTargetLang(targetLang);
      setTempSelectedVoice(selectedVoice);
      setTempSelectedModel(selectedModel);
      setTempSelectedAudioDevice(selectedAudioDevice);
      setValidationError(null);
    }
  }, [isOpen, sourceLang, targetLang, selectedVoice, selectedModel, selectedAudioDevice]);

  // Validate settings using the validation utility
  const validateCurrentSettings = (): boolean => {
    const validation = validateSettings({
      sourceLang: tempSourceLang,
      targetLang: tempTargetLang,
      selectedVoice: tempSelectedVoice,
      selectedModel: tempSelectedModel
    });

    if (!validation.isValid) {
      setValidationError(validation.errors[0]); // Show first error
      return false;
    }

    setValidationError(null);
    return true;
  };

  // Handle language change and auto-update voice if needed
  const handleTargetLanguageChange = (newTargetLang: string) => {
    setTempTargetLang(newTargetLang);

    // Auto-select first available voice for new target language
    const availableVoices = getVoicesForLanguage(newTargetLang);
    if (availableVoices.length > 0) {
      setTempSelectedVoice(availableVoices[0].id);
    }
  };

  // Handle save
  const handleSave = async () => {
    if (!validateCurrentSettings()) {
      return;
    }

    try {
      setIsSaving(true);

      // Check if settings changed
      const languageChanged = tempSourceLang !== sourceLang || tempTargetLang !== targetLang;
      const voiceChanged = tempSelectedVoice !== selectedVoice;
      const modelChanged = tempSelectedModel !== selectedModel;
      const audioDeviceChanged = tempSelectedAudioDevice !== selectedAudioDevice;

      // Sync language settings if changed
      if (languageChanged) {
        await onLanguageChange(tempSourceLang, tempTargetLang);
      }

      // Sync voice settings if changed
      if (voiceChanged) {
        await onVoiceChange(tempSelectedVoice);
      }

      // Sync model settings if changed
      if (modelChanged) {
        await onModelChange(tempSelectedModel);
      }

      // Sync audio device if changed
      if (audioDeviceChanged && tempSelectedAudioDevice && onAudioDeviceChange) {
        await onAudioDeviceChange(tempSelectedAudioDevice);
      }

      // Persist settings using the validation utility
      saveSettingsToStorage({
        sourceLang: tempSourceLang,
        targetLang: tempTargetLang,
        selectedVoice: tempSelectedVoice,
        selectedModel: tempSelectedModel
      });

      onClose();
    } catch (error) {
      console.error('Failed to save settings:', error);
      // Error is handled by the parent component and shown via configSyncError
    } finally {
      setIsSaving(false);
    }
  };

  // Handle cancel
  const handleCancel = () => {
    setTempSourceLang(sourceLang);
    setTempTargetLang(targetLang);
    setTempSelectedVoice(selectedVoice);
    setTempSelectedModel(selectedModel);
    setTempSelectedAudioDevice(selectedAudioDevice);
    setValidationError(null);
    onClose();
  };

  // Handle audio device refresh
  const handleRefreshAudioDevices = async () => {
    if (onRefreshAudioDevices) {
      try {
        await onRefreshAudioDevices();
      } catch (error) {
        console.error('Failed to refresh audio devices:', error);
      }
    }
  };

  const availableVoicesForTarget = getVoicesForLanguage(tempTargetLang);

  return (
    <Dialog open={isOpen} onOpenChange={handleCancel}>
      <DialogContent className="sm:max-w-[500px]">
        <DialogHeader>
          <DialogTitle className="flex items-center gap-2">
            <Volume2 className="w-5 h-5" />
            Translation Settings
          </DialogTitle>
          <DialogDescription>
            Configure your language preferences and voice settings for real-time translation.
          </DialogDescription>
        </DialogHeader>

        <div className="space-y-6 py-4">
          {/* Source Language Selection */}
          <div className="space-y-2">
            <Label htmlFor="source-lang" className="flex items-center gap-2">
              <Mic className="w-4 h-4" />
              Source Language (What you speak)
            </Label>
            <Select value={tempSourceLang} onValueChange={setTempSourceLang}>
              <SelectTrigger id="source-lang">
                <SelectValue placeholder="Select source language" />
              </SelectTrigger>
              <SelectContent>
                {AVAILABLE_LANGUAGES.map((lang) => (
                  <SelectItem
                    key={lang.code}
                    value={lang.code}
                    disabled={!lang.supported}
                  >
                    <div className="flex items-center gap-2">
                      <span>{lang.flag}</span>
                      <span>{lang.name}</span>
                      {!lang.supported && (
                        <Badge variant="secondary" className="text-xs">
                          Coming Soon
                        </Badge>
                      )}
                    </div>
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>

          <Separator />

          {/* Target Language Selection */}
          <div className="space-y-2">
            <Label htmlFor="target-lang" className="flex items-center gap-2">
              <Volume2 className="w-4 h-4" />
              Target Language (Translation output)
            </Label>
            <Select value={tempTargetLang} onValueChange={handleTargetLanguageChange}>
              <SelectTrigger id="target-lang">
                <SelectValue placeholder="Select target language" />
              </SelectTrigger>
              <SelectContent>
                {AVAILABLE_LANGUAGES.map((lang) => (
                  <SelectItem
                    key={lang.code}
                    value={lang.code}
                    disabled={!lang.supported || lang.code === tempSourceLang}
                  >
                    <div className="flex items-center gap-2">
                      <span>{lang.flag}</span>
                      <span>{lang.name}</span>
                      {!lang.supported && (
                        <Badge variant="secondary" className="text-xs">
                          Coming Soon
                        </Badge>
                      )}
                      {lang.code === tempSourceLang && (
                        <Badge variant="outline" className="text-xs">
                          Same as source
                        </Badge>
                      )}
                    </div>
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>

          <Separator />

          {/* Voice Selection */}
          <div className="space-y-2">
            <Label htmlFor="voice-select">
              Voice Selection for {AVAILABLE_LANGUAGES.find(l => l.code === tempTargetLang)?.name}
            </Label>
            {availableVoicesForTarget.length > 0 ? (
              <Select value={tempSelectedVoice} onValueChange={setTempSelectedVoice}>
                <SelectTrigger id="voice-select">
                  <SelectValue placeholder="Select voice" />
                </SelectTrigger>
                <SelectContent>
                  {availableVoicesForTarget.map((voice) => (
                    <SelectItem key={voice.id} value={voice.id}>
                      <div className="flex items-center gap-2">
                        <span>{voice.name}</span>
                        <Badge variant="outline" className="text-xs capitalize">
                          {voice.gender}
                        </Badge>
                      </div>
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            ) : (
              <div className="text-sm text-muted-foreground p-3 bg-muted rounded-md">
                No voices available for selected target language
              </div>
            )}
          </div>

          <Separator />

          {/* Model Selection */}
          <div className="space-y-2">
            <Label htmlFor="model-select" className="flex items-center gap-2">
              <RefreshCw className="w-4 h-4" />
              Translation Model
            </Label>
            <Select value={tempSelectedModel} onValueChange={setTempSelectedModel}>
              <SelectTrigger id="model-select">
                <SelectValue placeholder="Select translation model" />
              </SelectTrigger>
              <SelectContent>
                {AVAILABLE_MODELS.map((model) => (
                  <SelectItem
                    key={model.id}
                    value={model.id}
                    disabled={!model.supported}
                  >
                    <div className="flex flex-col">
                      <div className="flex items-center gap-2">
                        <span>{model.name}</span>
                        {!model.supported && (
                          <Badge variant="secondary" className="text-xs">
                            Coming Soon
                          </Badge>
                        )}
                      </div>
                      <span className="text-xs text-muted-foreground">{model.description}</span>
                    </div>
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>

          <Separator />

          {/* Audio Device Selection */}
          <div className="space-y-2">
            <div className="flex items-center justify-between">
              <Label htmlFor="audio-device-select" className="flex items-center gap-2">
                <Headphones className="w-4 h-4" />
                Audio Input Device
              </Label>
              <Button
                variant="ghost"
                size="sm"
                onClick={handleRefreshAudioDevices}
                disabled={isDeviceEnumerating}
                className="h-7 px-2"
              >
                <RefreshCw className={`w-3 h-3 ${isDeviceEnumerating ? 'animate-spin' : ''}`} />
              </Button>
            </div>

            {audioDevices.length > 0 ? (
              <Select
                value={tempSelectedAudioDevice || ''}
                onValueChange={setTempSelectedAudioDevice}
                disabled={isDeviceEnumerating}
              >
                <SelectTrigger id="audio-device-select">
                  <SelectValue placeholder="Select audio input device" />
                </SelectTrigger>
                <SelectContent>
                  {audioDevices.map((device) => (
                    <SelectItem key={device.deviceId} value={device.deviceId}>
                      <div className="flex items-center gap-2">
                        <Mic className="w-3 h-3" />
                        <span>{device.label || `Device ${device.deviceId.slice(0, 8)}...`}</span>
                        {device.deviceId === 'default' && (
                          <Badge variant="outline" className="text-xs">
                            Default
                          </Badge>
                        )}
                      </div>
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            ) : (
              <div className="text-sm text-muted-foreground p-3 bg-muted rounded-md">
                {isDeviceEnumerating ? (
                  <div className="flex items-center gap-2">
                    <Loader2 className="w-4 h-4 animate-spin" />
                    Detecting audio devices...
                  </div>
                ) : (
                  'No audio input devices found. Please connect a microphone and refresh.'
                )}
              </div>
            )}

            {/* Audio Device Status */}
            {tempSelectedAudioDevice && audioDevices.length > 0 && (
              <div className="text-xs text-muted-foreground">
                Selected: {audioDevices.find(d => d.deviceId === tempSelectedAudioDevice)?.label || 'Unknown device'}
              </div>
            )}
          </div>

          {/* Validation Error */}
          {validationError && (
            <div className="flex items-center gap-2 p-3 bg-destructive/10 text-destructive rounded-md">
              <AlertCircle className="w-4 h-4" />
              <span className="text-sm">{validationError}</span>
            </div>
          )}

          {/* Configuration Sync Error */}
          {configSyncError && (
            <div className="p-3 bg-destructive/10 text-destructive rounded-md">
              <div className="flex items-start gap-2">
                <AlertCircle className="w-4 h-4 mt-0.5 flex-shrink-0" />
                <div className="flex-1">
                  <div className="text-sm font-medium">Configuration Sync Failed</div>
                  <div className="text-xs text-destructive/80 mt-1">
                    {configSyncError.message}
                  </div>
                  <div className="text-xs text-destructive/60 mt-1">
                    Settings saved locally but may not be synchronized with the backend.
                  </div>
                </div>
              </div>
              {(onRetryConfigSync || onClearConfigError) && (
                <div className="flex gap-2 mt-3">
                  {onRetryConfigSync && (
                    <Button
                      variant="outline"
                      size="sm"
                      onClick={onRetryConfigSync}
                      disabled={isLanguageSyncing || isVoiceSyncing || isAudioDeviceSyncing}
                      className="h-7 text-xs border-destructive/20 hover:bg-destructive/5"
                    >
                      {(isLanguageSyncing || isVoiceSyncing || isAudioDeviceSyncing) && (
                        <Loader2 className="w-3 h-3 mr-1 animate-spin" />
                      )}
                      Retry Sync
                    </Button>
                  )}
                  {onClearConfigError && (
                    <Button
                      variant="ghost"
                      size="sm"
                      onClick={onClearConfigError}
                      className="h-7 text-xs text-destructive/70 hover:text-destructive hover:bg-destructive/5"
                    >
                      Dismiss
                    </Button>
                  )}
                </div>
              )}
            </div>
          )}

          {/* Audio Device Error */}
          {audioDeviceError && (
            <div className="p-3 bg-yellow-50 text-yellow-700 rounded-md">
              <div className="flex items-start gap-2">
                <AlertCircle className="w-4 h-4 mt-0.5 flex-shrink-0" />
                <div className="flex-1">
                  <div className="text-sm font-medium">Audio Device Issue</div>
                  <div className="text-xs text-yellow-700/80 mt-1">
                    {audioDeviceError.message}
                  </div>
                  {audioDeviceError.message.includes('fell back') && (
                    <div className="text-xs text-yellow-700/60 mt-1">
                      The system automatically switched to an available device.
                    </div>
                  )}
                </div>
              </div>
              <div className="flex gap-2 mt-3">
                <Button
                  variant="outline"
                  size="sm"
                  onClick={handleRefreshAudioDevices}
                  disabled={isDeviceEnumerating}
                  className="h-7 text-xs border-yellow-200 hover:bg-yellow-50"
                >
                  {isDeviceEnumerating && (
                    <Loader2 className="w-3 h-3 mr-1 animate-spin" />
                  )}
                  Refresh Devices
                </Button>
                <Button
                  variant="ghost"
                  size="sm"
                  onClick={() => {/* Clear audio device error - could be implemented */ }}
                  className="h-7 text-xs text-yellow-700/70 hover:text-yellow-700 hover:bg-yellow-50"
                >
                  Dismiss
                </Button>
              </div>
            </div>
          )}

          {/* Sync Status Indicators */}
          {(isLanguageSyncing || isVoiceSyncing || isAudioDeviceSyncing || isSaving) && (
            <div className="flex items-center gap-2 p-3 bg-blue-50 text-blue-700 rounded-md">
              <Loader2 className="w-4 h-4 animate-spin" />
              <span className="text-sm">
                {isSaving
                  ? 'Saving configuration...'
                  : isLanguageSyncing
                    ? 'Syncing language settings...'
                    : isVoiceSyncing
                      ? 'Syncing voice settings...'
                      : 'Changing audio device...'
                }
              </span>
            </div>
          )}
        </div>

        <DialogFooter>
          <Button
            variant="outline"
            onClick={handleCancel}
            disabled={isSaving || isLanguageSyncing || isVoiceSyncing || isAudioDeviceSyncing}
          >
            Cancel
          </Button>
          <Button
            onClick={handleSave}
            disabled={isSaving || isLanguageSyncing || isVoiceSyncing || isAudioDeviceSyncing}
          >
            {(isSaving || isLanguageSyncing || isVoiceSyncing || isAudioDeviceSyncing) && (
              <Loader2 className="w-4 h-4 mr-2 animate-spin" />
            )}
            Save Settings
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
};

export default SettingsDialog;