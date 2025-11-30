import { VocrAppMain } from './components/VocrAppMain'
import SettingsDialog from './components/SettingsDialog'
import { ErrorBoundary } from './components/ErrorBoundary'
import { ErrorNotification } from './components/ErrorNotification'
import { AudioDeviceNotification } from './components/AudioDeviceNotification'
import { useAppStore } from './store'
import { useSessionControl } from './hooks/useSessionControl'
import { useErrorHandler } from './hooks/useErrorHandler'
import { createErrorReportingService } from './services/ErrorReportingService'
import { useMemo, useState, useCallback } from 'react'

function App() {
  // Create shared ErrorReportingService instance
  const errorReportingService = useMemo(() => {
    return createErrorReportingService(
      process.env.NODE_ENV === 'production' ? 'production' : 'development',
      {
        // Add any custom configuration here
        // For example, you could add an API endpoint for production
        // endpoint: process.env.REACT_APP_ERROR_REPORTING_ENDPOINT,
        // apiKey: process.env.REACT_APP_ERROR_REPORTING_API_KEY,
      }
    );
  }, []);

  // Get state and actions from Zustand store
  const {
    sourceLang,
    targetLang,
    selectedVoice,
    selectedModel,
    currentOriginalText,
    currentTranslatedText,
    conversationHistory,
    settingsOpen,
    currentError,
    setLanguages,
    setVoice,
    setModel,
    setSettingsOpen
  } = useAppStore()

  // Use session control hook for session management
  const {
    sessionActive,
    currentState,
    toggleSession,
    configurationSync,
    audioDevices,
    selectedAudioDevice,
    audioDeviceError,
    isDeviceEnumerating,
    setAudioDevice,
    refreshAudioDevices,

  } = useSessionControl()

  // Error handling
  const { dismissError, retryLastOperation } = useErrorHandler()

  // Configuration sync loading states
  const [isLanguageSyncing, setIsLanguageSyncing] = useState(false)
  const [isVoiceSyncing, setIsVoiceSyncing] = useState(false)
  const [isAudioDeviceSyncing, setIsAudioDeviceSyncing] = useState(false)
  const [configSyncError, setConfigSyncError] = useState<Error | null>(null)

  // Clear configuration sync error
  const clearConfigSyncError = useCallback(() => {
    setConfigSyncError(null)
  }, [])

  // Audio device error handling
  const [dismissedAudioError, setDismissedAudioError] = useState<string | null>(null)

  const clearAudioDeviceError = useCallback(() => {
    if (audioDeviceError) {
      setDismissedAudioError(audioDeviceError.message)
    }
  }, [audioDeviceError])

  // Show audio device error only if not dismissed and different from last dismissed
  const shouldShowAudioError = audioDeviceError &&
    audioDeviceError.message !== dismissedAudioError

  // Retry failed configuration sync
  const retryConfigSync = useCallback(async () => {
    if (!configurationSync) {
      console.warn('Configuration sync not available for retry')
      return
    }

    try {
      setConfigSyncError(null)

      // Retry both language and voice settings
      setIsLanguageSyncing(true)
      await configurationSync.syncLanguageSettings(sourceLang, targetLang)

      setIsLanguageSyncing(false)
      setIsVoiceSyncing(true)
      await configurationSync.syncVoiceSettings(selectedVoice)

      console.log('Configuration sync retry successful')
    } catch (error) {
      const retryError = error instanceof Error ? error : new Error('Failed to retry configuration sync')
      console.error('Configuration sync retry failed:', retryError)
      setConfigSyncError(retryError)
    } finally {
      setIsLanguageSyncing(false)
      setIsVoiceSyncing(false)
    }
  }, [configurationSync, sourceLang, targetLang, selectedVoice])

  const handleToggleSession = () => {
    toggleSession()
  }

  const handleLanguageChange = useCallback(async (source: string, target: string) => {
    try {
      // Clear any previous sync errors
      setConfigSyncError(null)
      setIsLanguageSyncing(true)

      // Update local state first
      setLanguages(source, target)

      // Sync with backend if configuration sync is available
      if (configurationSync) {
        await configurationSync.syncLanguageSettings(source, target)
        console.log('Language configuration synced successfully:', { source, target })
      } else {
        console.warn('Configuration sync not available - language settings saved locally only')
      }
    } catch (error) {
      const syncError = error instanceof Error ? error : new Error('Failed to sync language settings')
      console.error('Failed to sync language configuration:', syncError)

      // Set error state for user feedback
      setConfigSyncError(syncError)

      // Still keep the local changes but notify user of sync failure
      // The local state was already updated above
    } finally {
      setIsLanguageSyncing(false)
    }
  }, [setLanguages, configurationSync])

  const handleVoiceChange = useCallback(async (voice: string) => {
    try {
      // Clear any previous sync errors
      setConfigSyncError(null)
      setIsVoiceSyncing(true)

      // Update local state first
      setVoice(voice)

      // Sync with backend if configuration sync is available
      if (configurationSync) {
        await configurationSync.syncVoiceSettings(voice)
        console.log('Voice configuration synced successfully:', { voice })
      } else {
        console.warn('Configuration sync not available - voice settings saved locally only')
      }
    } catch (error) {
      const syncError = error instanceof Error ? error : new Error('Failed to sync voice settings')
      console.error('Failed to sync voice configuration:', syncError)

      // Set error state for user feedback
      setConfigSyncError(syncError)

      // Still keep the local changes but notify user of sync failure
      // The local state was already updated above
    } finally {
      setIsVoiceSyncing(false)
    }
  }, [setVoice, configurationSync])

  const handleModelChange = useCallback(async (model: string) => {
    try {
      // Clear any previous sync errors
      setConfigSyncError(null)
      // We can reuse isVoiceSyncing or create a new state, but for now let's reuse or just set local
      // Ideally we should have isModelSyncing, but to keep it simple and consistent with existing UI
      // we'll just update local state immediately. 
      // If we want a spinner, we'd need to add isModelSyncing state.
      // Let's assume fast local update for now.

      setModel(model)
      console.log('Model configuration updated:', { model })

      // If we had backend sync for model, we'd call it here
      // if (configurationSync) { await configurationSync.syncModelSettings(model) }

    } catch (error) {
      console.error('Failed to update model configuration:', error)
      setConfigSyncError(error instanceof Error ? error : new Error('Failed to update model settings'))
    }
  }, [setModel])

  const handleAudioDeviceChange = useCallback(async (deviceId: string) => {
    try {
      // Clear any previous sync errors
      setConfigSyncError(null)
      setIsAudioDeviceSyncing(true)

      // Set the audio device through session control
      await setAudioDevice(deviceId)
      console.log('Audio device changed successfully:', { deviceId })
    } catch (error) {
      const syncError = error instanceof Error ? error : new Error('Failed to change audio device')
      console.error('Failed to change audio device:', syncError)

      // Set error state for user feedback
      setConfigSyncError(syncError)
    } finally {
      setIsAudioDeviceSyncing(false)
    }
  }, [setAudioDevice])

  return (
    <ErrorBoundary errorReportingService={errorReportingService}>
      <div className="min-h-screen">
        <ErrorBoundary errorReportingService={errorReportingService}>
          <VocrAppMain
            sessionActive={sessionActive}
            currentState={currentState}
            originalText={currentOriginalText}
            translatedText={currentTranslatedText}
            sourceLang={sourceLang}
            targetLang={targetLang}
            selectedVoice={selectedVoice}
            conversationHistory={conversationHistory}
            onToggleSession={handleToggleSession}
            onOpenSettings={() => setSettingsOpen(true)}
            onLanguageChange={handleLanguageChange}
            onVoiceChange={handleVoiceChange}
            isLanguageSyncing={isLanguageSyncing}
            isVoiceSyncing={isVoiceSyncing}
            configSyncError={configSyncError}
            onClearConfigError={clearConfigSyncError}
            onRetryConfigSync={retryConfigSync}
            audioDevices={audioDevices}
            selectedAudioDevice={selectedAudioDevice}
            onAudioDeviceChange={handleAudioDeviceChange}
            isAudioDeviceSyncing={isAudioDeviceSyncing}
            audioDeviceError={audioDeviceError}
            isDeviceEnumerating={isDeviceEnumerating}
            onRefreshAudioDevices={refreshAudioDevices}
          />
        </ErrorBoundary>

        <ErrorBoundary errorReportingService={errorReportingService}>
          <SettingsDialog
            isOpen={settingsOpen}
            onClose={() => setSettingsOpen(false)}
            sourceLang={sourceLang}
            targetLang={targetLang}
            selectedVoice={selectedVoice}
            selectedModel={selectedModel}
            onLanguageChange={handleLanguageChange}
            onVoiceChange={handleVoiceChange}
            onModelChange={handleModelChange}
            isLanguageSyncing={isLanguageSyncing}
            isVoiceSyncing={isVoiceSyncing}
            configSyncError={configSyncError}
            onClearConfigError={clearConfigSyncError}
            onRetryConfigSync={retryConfigSync}
            audioDevices={audioDevices}
            selectedAudioDevice={selectedAudioDevice}
            onAudioDeviceChange={handleAudioDeviceChange}
            isAudioDeviceSyncing={isAudioDeviceSyncing}
            audioDeviceError={audioDeviceError}
            isDeviceEnumerating={isDeviceEnumerating}
            onRefreshAudioDevices={refreshAudioDevices}
          />
        </ErrorBoundary>

        {/* Audio device notification */}
        {shouldShowAudioError && (
          <div className="fixed top-4 left-1/2 transform -translate-x-1/2 z-50 w-full max-w-md px-4">
            <AudioDeviceNotification
              error={audioDeviceError}
              onDismiss={clearAudioDeviceError}
              onRefreshDevices={refreshAudioDevices}
              isRefreshing={isDeviceEnumerating}
            />
          </div>
        )}

        {/* Global error notification */}
        <ErrorNotification
          error={currentError}
          onDismiss={dismissError}
          onRetry={retryLastOperation}
        />
      </div>
    </ErrorBoundary>
  )
}

export default App
