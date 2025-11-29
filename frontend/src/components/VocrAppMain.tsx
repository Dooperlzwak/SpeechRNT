import { useState } from 'react';
import { TopNav } from './TopNav';
import { TranslationConsole } from './TranslationConsole';
import { ControlBar } from './ControlBar';
import { SettingsDrawer } from './SettingsDrawer';
import { SessionHistory } from './SessionHistory';
import type { SystemState } from './StatusIndicator';
import type { ConversationEntry } from '@/store/appStore';

export type TranslationMode = 'speech' | 'text' | 'type';

interface VocrAppMainProps {
    sessionActive: boolean;
    currentState: SystemState;
    originalText: string;
    translatedText: string;
    sourceLang: string;
    targetLang: string;
    selectedVoice: string;
    conversationHistory: ConversationEntry[];
    onToggleSession: () => void;
    onOpenSettings: () => void;
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

export function VocrAppMain({
    sessionActive,
    currentState,
    originalText,
    translatedText,
    sourceLang,
    targetLang,
    selectedVoice,
    conversationHistory,
    onToggleSession,
    onOpenSettings: onOpenSettingsProp,
    onLanguageChange,
    onVoiceChange,
    isLanguageSyncing,
    isVoiceSyncing,
    configSyncError,
    onClearConfigError,
    onRetryConfigSync,
    audioDevices,
    selectedAudioDevice,
    onAudioDeviceChange,
    isAudioDeviceSyncing,
    audioDeviceError,
    isDeviceEnumerating,
    onRefreshAudioDevices
}: VocrAppMainProps) {
    const [isSettingsOpen, setIsSettingsOpen] = useState(false);
    const [isHistoryOpen, setIsHistoryOpen] = useState(false);
    const [mode, setMode] = useState<TranslationMode>('speech');

    // Determine if recording based on session state
    const isRecording = sessionActive && (currentState === 'listening' || currentState === 'thinking');

    const handleOpenSettings = () => {
        setIsSettingsOpen(true);
        onOpenSettingsProp();
    };

    return (
        <div className="flex h-screen w-full flex-col bg-background text-foreground overflow-hidden selection:bg-primary/20">
            <TopNav
                onOpenSettings={handleOpenSettings}
                onOpenHistory={() => setIsHistoryOpen(true)}
                isRecording={isRecording}
                mode={mode}
                onModeChange={setMode}
            />

            <main className="flex-1 relative flex flex-col">
                <TranslationConsole
                    isRecording={isRecording}
                    mode={mode}
                    originalText={originalText}
                    translatedText={translatedText}
                    sourceLang={sourceLang}
                    targetLang={targetLang}
                />

                <div className="absolute bottom-0 left-0 right-0 p-6 bg-gradient-to-t from-background via-background to-transparent z-10">
                    <ControlBar
                        isRecording={isRecording}
                        onToggleRecording={onToggleSession}
                        mode={mode}
                    />
                </div>
            </main>

            <SettingsDrawer
                isOpen={isSettingsOpen}
                onClose={() => setIsSettingsOpen(false)}
                sourceLang={sourceLang}
                targetLang={targetLang}
                selectedVoice={selectedVoice}
                onLanguageChange={onLanguageChange}
                onVoiceChange={onVoiceChange}
                isLanguageSyncing={isLanguageSyncing}
                isVoiceSyncing={isVoiceSyncing}
                configSyncError={configSyncError}
                onClearConfigError={onClearConfigError}
                onRetryConfigSync={onRetryConfigSync}
                audioDevices={audioDevices}
                selectedAudioDevice={selectedAudioDevice}
                onAudioDeviceChange={onAudioDeviceChange}
                isAudioDeviceSyncing={isAudioDeviceSyncing}
                audioDeviceError={audioDeviceError}
                isDeviceEnumerating={isDeviceEnumerating}
                onRefreshAudioDevices={onRefreshAudioDevices}
            />

            <SessionHistory
                isOpen={isHistoryOpen}
                onClose={() => setIsHistoryOpen(false)}
                conversationHistory={conversationHistory}
            />
        </div>
    );
}
