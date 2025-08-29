import React from 'react';
import { Alert, AlertDescription } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { AlertCircle, Headphones, X, RefreshCw } from 'lucide-react';

interface AudioDeviceNotificationProps {
  error: Error | null;
  onDismiss?: () => void;
  onRefreshDevices?: () => Promise<void>;
  isRefreshing?: boolean;
}

export const AudioDeviceNotification: React.FC<AudioDeviceNotificationProps> = ({
  error,
  onDismiss,
  onRefreshDevices,
  isRefreshing = false
}) => {
  if (!error) return null;

  const isDeviceDisconnected = error.message.includes('disconnected') || error.message.includes('fell back');
  const isPermissionError = error.message.includes('permission') || error.message.includes('denied');
  const isDeviceNotFound = error.message.includes('not found') || error.message.includes('No audio');

  const getIcon = () => {
    if (isPermissionError) return <AlertCircle className="w-4 h-4" />;
    if (isDeviceNotFound || isDeviceDisconnected) return <Headphones className="w-4 h-4" />;
    return <AlertCircle className="w-4 h-4" />;
  };

  const getVariant = (): "default" | "destructive" => {
    if (isPermissionError || isDeviceNotFound) return "destructive";
    return "default"; // For device disconnection warnings
  };

  const getTitle = () => {
    if (isPermissionError) return "Microphone Permission Required";
    if (isDeviceNotFound) return "No Audio Device Found";
    if (isDeviceDisconnected) return "Audio Device Changed";
    return "Audio Device Issue";
  };

  return (
    <Alert variant={getVariant()} className="mb-4">
      <div className="flex items-start justify-between">
        <div className="flex items-start gap-2 flex-1">
          {getIcon()}
          <div className="flex-1">
            <div className="font-medium text-sm">{getTitle()}</div>
            <AlertDescription className="text-xs mt-1">
              {error.message}
            </AlertDescription>
            
            {/* Action buttons */}
            <div className="flex gap-2 mt-2">
              {onRefreshDevices && (
                <Button
                  variant="outline"
                  size="sm"
                  onClick={onRefreshDevices}
                  disabled={isRefreshing}
                  className="h-7 text-xs"
                >
                  {isRefreshing ? (
                    <RefreshCw className="w-3 h-3 mr-1 animate-spin" />
                  ) : (
                    <RefreshCw className="w-3 h-3 mr-1" />
                  )}
                  Refresh Devices
                </Button>
              )}
              
              {isPermissionError && (
                <Button
                  variant="outline"
                  size="sm"
                  onClick={() => {
                    // Open browser settings or provide guidance
                    window.open('chrome://settings/content/microphone', '_blank');
                  }}
                  className="h-7 text-xs"
                >
                  Open Settings
                </Button>
              )}
            </div>
          </div>
        </div>
        
        {onDismiss && (
          <Button
            variant="ghost"
            size="sm"
            onClick={onDismiss}
            className="h-6 w-6 p-0 ml-2"
          >
            <X className="w-3 h-3" />
          </Button>
        )}
      </div>
    </Alert>
  );
};