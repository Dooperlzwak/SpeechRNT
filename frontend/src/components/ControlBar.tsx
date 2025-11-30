import { Mic } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { cn } from '@/lib/utils';

export type TranslationMode = 'speech' | 'text' | 'type';

interface ControlBarProps {
    isRecording: boolean;
    onToggleRecording: () => void;
    mode: TranslationMode;
}

export function ControlBar({ isRecording, onToggleRecording, mode }: ControlBarProps) {
    return (
        <div className="flex flex-col items-center gap-6">
            {mode === "speech" && (
                <>
                    <Button
                        size="lg"
                        onClick={onToggleRecording}
                        className={cn(
                            "h-20 w-20 rounded-full border-4 transition-all duration-300 shadow-2xl flex items-center justify-center",
                            isRecording
                                ? "bg-red-500 hover:bg-red-600 border-red-950/30 shadow-red-500/20 animate-pulse"
                                : "bg-primary hover:bg-primary/90 border-background shadow-primary/10"
                        )}
                    >
                        {isRecording ? (
                            <div className="h-30 w-30 rounded-sm bg-white" />
                        ) : (
                            <Mic className="h-30 w-30 text-primary-foreground" />
                        )}
                    </Button>

                    <p className="text-sm text-muted-foreground font-medium">
                        {isRecording ? "Listening..." : "Tap to speak"}
                    </p>
                </>
            )}
        </div>
    );
}
