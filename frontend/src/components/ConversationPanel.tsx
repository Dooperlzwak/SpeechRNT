import React from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import StatusIndicator, { type SystemState } from './StatusIndicator';
import TranscriptionDisplay from './TranscriptionDisplay';
import { Mic, MicOff, Settings } from 'lucide-react';

interface ConversationPanelProps {
  sessionActive: boolean;
  currentState: SystemState;
  originalText: string;
  translatedText: string;
  sourceLang: string;
  targetLang: string;
  connectionStatus: 'connected' | 'disconnected' | 'reconnecting';
  onToggleSession: () => void;
  onOpenSettings: () => void;
  transcriptionConfidence?: number;
}

const ConversationPanel: React.FC<ConversationPanelProps> = ({
  sessionActive,
  currentState,
  originalText,
  translatedText,
  sourceLang,
  targetLang,
  connectionStatus,
  onToggleSession,
  onOpenSettings,
  transcriptionConfidence
}) => {
  return (
    <div className="h-screen bg-background flex flex-col">
      {/* Header */}
      <div className="bg-card border-b border-border p-4">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-4">
            <h1 className="text-2xl font-bold text-primary">
              Speech<span className="text-blue-500">RNT</span>
            </h1>
            <StatusIndicator state={currentState} />
          </div>
          
          <div className="flex items-center gap-2">
            {/* Language indicators */}
            <div className="flex items-center gap-2 text-sm text-muted-foreground">
              <span className="flex items-center gap-1">
                🇺🇸 {sourceLang}
              </span>
              <span>⇄</span>
              <span className="flex items-center gap-1">
                🇪🇸 {targetLang}
              </span>
            </div>
            
            {/* Connection status */}
            <div className="flex items-center gap-2">
              <div className={`w-2 h-2 rounded-full ${
                connectionStatus === 'connected' ? 'bg-green-500' :
                connectionStatus === 'reconnecting' ? 'bg-yellow-500 animate-pulse' :
                'bg-red-500'
              }`}></div>
              <span className="text-xs text-muted-foreground capitalize">
                {connectionStatus}
              </span>
            </div>
            
            <Button
              variant="outline"
              size="icon"
              onClick={onOpenSettings}
              aria-label="Open settings"
            >
              <Settings className="w-4 h-4" />
            </Button>
          </div>
        </div>
      </div>

      {/* Main conversation area */}
      <div className="flex-1 flex">
        {/* Original (Left Panel) */}
        <div className="flex-1 p-4 border-r border-border">
          <Card className="h-full">
            <CardHeader className="pb-3">
              <CardTitle className="text-lg">Original ({sourceLang})</CardTitle>
            </CardHeader>
            <CardContent className="flex-1">
              <TranscriptionDisplay
                text={originalText}
                isLive={currentState === 'listening'}
                confidence={transcriptionConfidence}
                className="h-full border-0 shadow-none"
              />
            </CardContent>
          </Card>
        </div>

        {/* Translation (Right Panel) */}
        <div className="flex-1 p-4">
          <Card className="h-full">
            <CardHeader className="pb-3">
              <CardTitle className="text-lg">Translation ({targetLang})</CardTitle>
            </CardHeader>
            <CardContent className="flex-1">
              <TranscriptionDisplay
                text={translatedText}
                isLive={currentState === 'speaking'}
                className="h-full border-0 shadow-none"
              />
            </CardContent>
          </Card>
        </div>
      </div>

      {/* Bottom control area */}
      <div className="p-6 border-t border-border bg-card">
        <div className="flex justify-center">
          <Button
            size="lg"
            onClick={onToggleSession}
            disabled={connectionStatus === 'disconnected'}
            aria-label={sessionActive ? 'Stop conversation' : 'Start conversation'}
            className={`w-16 h-16 rounded-full transition-all duration-200 ${
              sessionActive 
                ? 'bg-red-500 hover:bg-red-600 text-white shadow-lg' 
                : connectionStatus === 'disconnected'
                ? 'bg-gray-400 text-gray-600 cursor-not-allowed'
                : 'bg-blue-500 hover:bg-blue-600 text-white shadow-lg hover:shadow-xl'
            } ${
              currentState === 'listening' ? 'animate-pulse' : ''
            }`}
          >
            {sessionActive ? (
              <MicOff className="w-6 h-6" />
            ) : (
              <Mic className="w-6 h-6" />
            )}
          </Button>
        </div>
        
        <div className="text-center mt-3">
          <p className="text-sm text-muted-foreground">
            {connectionStatus === 'disconnected' 
              ? 'Connection required to start conversation'
              : sessionActive 
              ? 'Click to stop conversation' 
              : 'Click to start conversation'
            }
          </p>
          {sessionActive && (
            <p className="text-xs text-muted-foreground mt-1">
              Status: {currentState === 'idle' ? 'Waiting for speech' : 
                      currentState === 'listening' ? 'Listening...' :
                      currentState === 'thinking' ? 'Processing...' :
                      'Playing audio'}
            </p>
          )}
        </div>
      </div>
    </div>
  );
};

export default ConversationPanel;