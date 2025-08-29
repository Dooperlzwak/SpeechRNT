import React from 'react';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Alert, AlertDescription } from '@/components/ui/alert';
import { 
  Wifi, 
  WifiOff, 
  RefreshCw, 
  AlertTriangle, 
  CheckCircle,
  Clock
} from 'lucide-react';
import { useConnectionStatus } from '../hooks/useConnectionResilience';

interface ConnectionStatusProps {
  resilienceStats?: () => any;
  onManualRetry?: () => void;
  onForceReconnect?: () => void;
  className?: string;
  showDetails?: boolean;
}

const ConnectionStatus: React.FC<ConnectionStatusProps> = ({
  resilienceStats,
  onManualRetry,
  onForceReconnect,
  className = '',
  showDetails = false
}) => {
  const { 
    status, 
    message, 
    color, 
    detailedStatus, 
    formatOfflineDuration 
  } = useConnectionStatus(resilienceStats);

  const details = detailedStatus;

  const getStatusIcon = () => {
    switch (status) {
      case 'connected':
        if (details?.connectionQuality === 'poor' || details?.connectionQuality === 'critical') {
          return <AlertTriangle className="w-4 h-4" />;
        }
        return <CheckCircle className="w-4 h-4" />;
      case 'reconnecting':
        return <RefreshCw className="w-4 h-4 animate-spin" />;
      case 'disconnected':
        return <WifiOff className="w-4 h-4" />;
      default:
        return <Wifi className="w-4 h-4" />;
    }
  };

  const getStatusVariant = () => {
    switch (color) {
      case 'green':
        return 'default';
      case 'yellow':
        return 'secondary';
      case 'red':
        return 'destructive';
      default:
        return 'outline';
    }
  };

  const getBadgeClassName = () => {
    switch (color) {
      case 'green':
        return 'bg-green-500 text-white';
      case 'yellow':
        return 'bg-yellow-500 text-white';
      case 'red':
        return 'bg-red-500 text-white';
      default:
        return 'bg-gray-500 text-white';
    }
  };

  return (
    <div className={`space-y-2 ${className}`}>
      {/* Main Status Badge */}
      <Badge 
        variant={getStatusVariant()}
        className={`flex items-center gap-2 px-3 py-1 ${getBadgeClassName()}`}
      >
        {getStatusIcon()}
        <span className="text-sm font-medium">{message}</span>
      </Badge>

      {/* Detailed Status Information */}
      {showDetails && details && (
        <div className="space-y-2">
          {/* Offline Duration */}
          {details.isOffline && details.offlineDuration > 0 && (
            <div className="flex items-center gap-2 text-sm text-gray-600">
              <Clock className="w-3 h-3" />
              <span>Offline for {formatOfflineDuration(details.offlineDuration)}</span>
            </div>
          )}

          {/* Reconnection Attempts */}
          {status === 'reconnecting' && details.reconnectAttempts > 0 && (
            <div className="text-sm text-gray-600">
              Attempt {details.reconnectAttempts} of {details.maxReconnectAttempts}
            </div>
          )}

          {/* Connection Quality Warning */}
          {status === 'connected' && details.connectionQuality !== 'good' && (
            <Alert className="py-2">
              <AlertTriangle className="h-4 w-4" />
              <AlertDescription className="text-sm">
                Connection quality is {details.connectionQuality}. 
                Audio may be affected.
              </AlertDescription>
            </Alert>
          )}

          {/* Manual Retry Options */}
          {details.manualRetryAvailable && (
            <Alert className="py-2">
              <WifiOff className="h-4 w-4" />
              <AlertDescription className="text-sm">
                <div className="flex items-center justify-between">
                  <span>Connection failed. Manual retry available.</span>
                  <div className="flex gap-2 ml-2">
                    {onManualRetry && (
                      <Button 
                        size="sm" 
                        variant="outline"
                        onClick={onManualRetry}
                        className="h-6 px-2 text-xs"
                      >
                        <RefreshCw className="w-3 h-3 mr-1" />
                        Retry
                      </Button>
                    )}
                    {onForceReconnect && (
                      <Button 
                        size="sm" 
                        variant="default"
                        onClick={onForceReconnect}
                        className="h-6 px-2 text-xs"
                      >
                        Force Reconnect
                      </Button>
                    )}
                  </div>
                </div>
              </AlertDescription>
            </Alert>
          )}

          {/* Session Recovery Available */}
          {details.canRecoverSession && status === 'connected' && (
            <div className="text-sm text-green-600">
              Session recovery available
            </div>
          )}
        </div>
      )}
    </div>
  );
};

export default ConnectionStatus;