import React, { useState, useEffect } from 'react';
import { X, Globe, Mic, Volume2, Headphones, RefreshCw, AlertCircle, Loader2 } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { cn } from '@/lib/utils';
import {
    Select,
    SelectContent,
    SelectItem,
    SelectTrigger,
    SelectValue,
} from '@/components/ui/select';
import { Label } from '@/components/ui/label';
import { Badge } from '@/components/ui/badge';
import {
    AVAILABLE_LANGUAGES,
    getVoicesForLanguage,
    validateSettings,
    saveSettingsToStorage,
} from '../utils/settingsValidation';

interface SettingsDrawerProps {
    isOpen: boolean;
    onClose: () => void;
    sourceLang: string;
    targetLang: string;
    selectedVoice: string;
    onLanguageChange: (source: string, target: string) => Promise<void>;
    onVoiceChange: (voice: string) => Promise<void>;
    isLanguageSyncing?: boolean;
    isVoiceSyncing?: boolean;
    configSyncError?: Error | null;
    onClearConfigError?: () => void;
    onRetryConfigSync?: () => Promise<void>;
    audioDevices?: MediaDeviceInfo[];
    selectedAudioDevice?: string | null;
    onAudioDeviceChange?: (deviceId: string) => Promise<void>;
    isAudioDeviceSyncing?: boolean;
    audioDeviceError?: Error | null;
    isDeviceEnumerating?: boolean;
    onRefreshAudioDevices?: () => Promise<void>;
}

export function SettingsDrawer({
    isOpen,
    onClose,
    sourceLang,
    targetLang,
    selectedVoice,
    onLanguageChange,
    onVoiceChange,
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
}: SettingsDrawerProps) {
    const [tempSourceLang, setTempSourceLang] = useState(sourceLang);
    const [tempTargetLang, setTempTargetLang] = useState(targetLang);
    const [tempSelectedVoice, setTempSelectedVoice] = useState(selectedVoice);
    const [tempSelectedAudioDevice, setTempSelectedAudioDevice] = useState(selectedAudioDevice);
    const [validationError, setValidationError] = useState<string | null>(null);
    const [isSaving, setIsSaving] = useState(false);

    useEffect(() => {
        if (isOpen) {
            setTempSourceLang(sourceLang);
            setTempTargetLang(targetLang);
            setTempSelectedVoice(selectedVoice);
            setTempSelectedAudioDevice(selectedAudioDevice);
            setValidationError(null);
        }
    }, [isOpen, sourceLang, targetLang, selectedVoice, selectedAudioDevice]);

    const validateCurrentSettings = (): boolean => {
        const validation = validateSettings({
            sourceLang: tempSourceLang,
            targetLang: tempTargetLang,
            selectedVoice: tempSelectedVoice
        });

        if (!validation.isValid) {
            setValidationError(validation.errors[0]);
            return false;
        }

        setValidationError(null);
        return true;
    };

    const handleTargetLanguageChange = (newTargetLang: string) => {
        setTempTargetLang(newTargetLang);
        const availableVoices = getVoicesForLanguage(newTargetLang);
        if (availableVoices.length > 0) {
            setTempSelectedVoice(availableVoices[0].id);
        }
    };

    const handleSave = async () => {
        if (!validateCurrentSettings()) {
            return;
        }

        try {
            setIsSaving(true);

            const languageChanged = tempSourceLang !== sourceLang || tempTargetLang !== targetLang;
            const voiceChanged = tempSelectedVoice !== selectedVoice;
            const audioDeviceChanged = tempSelectedAudioDevice !== selectedAudioDevice;

            if (languageChanged) {
                await onLanguageChange(tempSourceLang, tempTargetLang);
            }

            if (voiceChanged) {
                await onVoiceChange(tempSelectedVoice);
            }

            if (audioDeviceChanged && tempSelectedAudioDevice && onAudioDeviceChange) {
                await onAudioDeviceChange(tempSelectedAudioDevice);
            }

            saveSettingsToStorage({
                sourceLang: tempSourceLang,
                targetLang: tempTargetLang,
                selectedVoice: tempSelectedVoice
            });

            onClose();
        } catch (error) {
            console.error('Failed to save settings:', error);
        } finally {
            setIsSaving(false);
        }
    };

    const handleCancel = () => {
        setTempSourceLang(sourceLang);
        setTempTargetLang(targetLang);
        setTempSelectedVoice(selectedVoice);
        setTempSelectedAudioDevice(selectedAudioDevice);
        setValidationError(null);
        onClose();
    };

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
        <>
            {/* Backdrop */}
            <div
                className={cn(
                    "fixed inset-0 bg-black/50 backdrop-blur-sm z-40 transition-opacity duration-300",
                    isOpen ? "opacity-100" : "opacity-0 pointer-events-none"
                )}
                onClick={onClose}
            />

            {/* Drawer */}
            <div
                className={cn(
                    "fixed inset-y-0 right-0 w-full max-w-md bg-background border-l border-border z-50 shadow-2xl transform transition-transform duration-300 ease-in-out flex flex-col",
                    isOpen ? "translate-x-0" : "translate-x-full"
                )}
            >
                <div className="flex items-center justify-between p-6 border-b border-border">
                    <h2 className="text-xl font-semibold">Settings</h2>
                    <Button variant="ghost" size="icon" onClick={onClose}>
                        <X className="h-5 w-5" />
                    </Button>
                </div>

                <div className="flex-1 overflow-y-auto p-6 space-y-6">
                    {/* Language Settings */}
                    <SettingsSection title="Languages" icon={Globe}>
                        <div className="space-y-4">
                            <div className="space-y-2">
                                <Label htmlFor="source-lang">Source Language</Label>
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
                                                            Soon
                                                        </Badge>
                                                    )}
                                                </div>
                                            </SelectItem>
                                        ))}
                                    </SelectContent>
                                </Select>
                            </div>

                            <div className="space-y-2">
                                <Label htmlFor="target-lang">Target Language</Label>
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
                                                </div>
                                            </SelectItem>
                                        ))}
                                    </SelectContent>
                                </Select>
                            </div>
                        </div>
                    </SettingsSection>

                    {/* Voice Settings */}
                    <SettingsSection title="Text-to-Speech" icon={Volume2}>
                        <div className="space-y-2">
                            <Label htmlFor="voice-select">Voice Profile</Label>
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
                    </SettingsSection>

                    {/* Audio Device Settings */}
                    <SettingsSection title="Audio Input" icon={Headphones}>
                        <div className="space-y-2">
                            <div className="flex items-center justify-between">
                                <Label htmlFor="audio-device-select">Microphone</Label>
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
                                        'No audio input devices found'
                                    )}
                                </div>
                            )}
                        </div>
                    </SettingsSection>

                    {/* Error Messages */}
                    {validationError && (
                        <div className="flex items-center gap-2 p-3 bg-destructive/10 text-destructive rounded-md">
                            <AlertCircle className="w-4 h-4" />
                            <span className="text-sm">{validationError}</span>
                        </div>
                    )}

                    {configSyncError && (
                        <div className="p-3 bg-destructive/10 text-destructive rounded-md">
                            <div className="flex items-start gap-2">
                                <AlertCircle className="w-4 h-4 mt-0.5 flex-shrink-0" />
                                <div className="flex-1">
                                    <div className="text-sm font-medium">Configuration Sync Failed</div>
                                    <div className="text-xs text-destructive/80 mt-1">
                                        {configSyncError.message}
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
                                            className="h-7 text-xs"
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
                                            className="h-7 text-xs"
                                        >
                                            Dismiss
                                        </Button>
                                    )}
                                </div>
                            )}
                        </div>
                    )}

                    {(isLanguageSyncing || isVoiceSyncing || isAudioDeviceSyncing || isSaving) && (
                        <div className="flex items-center gap-2 p-3 bg-blue-50 text-blue-700 dark:bg-blue-950 dark:text-blue-300 rounded-md">
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

                <div className="p-6 border-t border-border flex gap-2">
                    <Button
                        variant="outline"
                        onClick={handleCancel}
                        disabled={isSaving || isLanguageSyncing || isVoiceSyncing || isAudioDeviceSyncing}
                        className="flex-1"
                    >
                        Cancel
                    </Button>
                    <Button
                        onClick={handleSave}
                        disabled={isSaving || isLanguageSyncing || isVoiceSyncing || isAudioDeviceSyncing}
                        className="flex-1"
                    >
                        {(isSaving || isLanguageSyncing || isVoiceSyncing || isAudioDeviceSyncing) && (
                            <Loader2 className="w-4 h-4 mr-2 animate-spin" />
                        )}
                        Save Settings
                    </Button>
                </div>
            </div>
        </>
    );
}

function SettingsSection({
    title,
    icon: Icon,
    children
}: {
    title: string;
    icon: any;
    children: React.ReactNode;
}) {
    return (
        <div className="space-y-4">
            <div className="flex items-center gap-2 text-sm font-medium text-muted-foreground uppercase tracking-wider">
                <Icon className="h-4 w-4" />
                {title}
            </div>
            <div className="space-y-4 pl-1">
                {children}
            </div>
        </div>
    );
}
