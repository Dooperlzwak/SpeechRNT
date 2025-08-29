import React from 'react';
import { Badge } from '@/components/ui/badge';
import { Mic, Brain, Volume2, Pause } from 'lucide-react';

export type SystemState = 'idle' | 'listening' | 'thinking' | 'speaking';

interface StatusIndicatorProps {
  state: SystemState;
  className?: string;
}

const StatusIndicator: React.FC<StatusIndicatorProps> = ({ state, className }) => {
  const getStatusConfig = (state: SystemState) => {
    switch (state) {
      case 'idle':
        return {
          icon: <Pause className="w-4 h-4" />,
          text: 'Waiting',
          variant: 'secondary' as const,
          className: 'bg-gray-100 text-gray-700'
        };
      case 'listening':
        return {
          icon: <Mic className="w-4 h-4" />,
          text: 'Listening',
          variant: 'default' as const,
          className: 'bg-blue-500 text-white animate-pulse'
        };
      case 'thinking':
        return {
          icon: <Brain className="w-4 h-4" />,
          text: 'Thinking',
          variant: 'secondary' as const,
          className: 'bg-yellow-500 text-white'
        };
      case 'speaking':
        return {
          icon: <Volume2 className="w-4 h-4" />,
          text: 'Speaking',
          variant: 'default' as const,
          className: 'bg-green-500 text-white animate-pulse'
        };
      default:
        return {
          icon: <Pause className="w-4 h-4" />,
          text: 'Unknown',
          variant: 'outline' as const,
          className: 'bg-gray-100 text-gray-700'
        };
    }
  };

  const config = getStatusConfig(state);

  return (
    <Badge 
      variant={config.variant}
      className={`flex items-center gap-2 px-3 py-1 ${config.className} ${className || ''}`}
    >
      {config.icon}
      <span className="text-sm font-medium">{config.text}</span>
    </Badge>
  );
};

export default StatusIndicator;