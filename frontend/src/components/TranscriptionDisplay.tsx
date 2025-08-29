import React from 'react';
import { Card, CardContent } from '@/components/ui/card';

interface TranscriptionDisplayProps {
  text: string;
  isLive?: boolean;
  confidence?: number;
  className?: string;
}

const TranscriptionDisplay: React.FC<TranscriptionDisplayProps> = ({ 
  text, 
  isLive = false, 
  confidence,
  className 
}) => {
  const getConfidenceColor = (confidence?: number) => {
    if (!confidence) return '';
    if (confidence >= 0.8) return 'text-green-600';
    if (confidence >= 0.6) return 'text-yellow-600';
    return 'text-red-600';
  };

  return (
    <Card className={`${className || ''}`}>
      <CardContent className="p-4">
        <div className="min-h-[100px] flex flex-col">
          <div className="flex-1">
            {text ? (
              <p className={`text-base leading-relaxed ${isLive ? 'animate-pulse' : ''}`}>
                {text}
              </p>
            ) : (
              <p className="text-muted-foreground italic">
                {isLive ? 'Listening...' : 'No transcription yet'}
              </p>
            )}
          </div>
          
          {confidence !== undefined && (
            <div className="mt-2 pt-2 border-t border-border">
              <span className={`text-xs ${getConfidenceColor(confidence)}`}>
                Confidence: {Math.round(confidence * 100)}%
              </span>
            </div>
          )}
        </div>
      </CardContent>
    </Card>
  );
};

export default TranscriptionDisplay;