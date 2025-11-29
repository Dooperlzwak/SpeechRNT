import { Settings, History, Activity, MessageSquare, Type, Keyboard } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { cn } from '@/lib/utils';
import type { TranslationMode } from './ControlBar';

interface TopNavProps {
    onOpenSettings: () => void;
    onOpenHistory: () => void;
    isRecording: boolean;
    mode: TranslationMode;
    onModeChange: (mode: TranslationMode) => void;
}

export function TopNav({
    onOpenSettings,
    onOpenHistory,
    isRecording,
    mode,
    onModeChange
}: TopNavProps) {
    return (
        <header className="flex h-14 items-center justify-between border-b border-border px-6 bg-background/50 backdrop-blur-sm z-50">
            <div className="flex items-center gap-2">
                <div className="flex h-8 w-8 items-center justify-center rounded-lg bg-primary text-primary-foreground">
                    <Activity className="h-5 w-5" />
                </div>
                <span className="font-bold text-lg tracking-tight">Vocr</span>
                <span className="ml-2 rounded-full bg-muted px-2 py-0.5 text-xs font-medium text-muted-foreground border border-border">
                    Beta
                </span>
            </div>

            <div className="absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2">
                <div className="flex items-center p-1 bg-muted/50 backdrop-blur-md border border-border rounded-full">
                    <TabButton
                        active={mode === "speech"}
                        icon={MessageSquare}
                        label="Speech"
                        onClick={() => onModeChange("speech")}
                    />
                    <TabButton
                        active={mode === "text"}
                        icon={Type}
                        label="Text"
                        onClick={() => onModeChange("text")}
                    />
                    <TabButton
                        active={mode === "type"}
                        icon={Keyboard}
                        label="Type"
                        onClick={() => onModeChange("type")}
                    />
                </div>
            </div>

            <div className="flex items-center gap-2">
                {isRecording && (
                    <div className="flex items-center gap-2 mr-4 animate-pulse">
                        <div className="h-2 w-2 rounded-full bg-green-500" />
                        <span className="text-xs font-medium text-green-500">Live</span>
                    </div>
                )}

                <Button
                    variant="ghost"
                    size="icon"
                    onClick={onOpenHistory}
                    className="text-muted-foreground hover:text-foreground"
                >
                    <History className="h-5 w-5" />
                </Button>
                <Button
                    variant="ghost"
                    size="icon"
                    onClick={onOpenSettings}
                    className="text-muted-foreground hover:text-foreground"
                >
                    <Settings className="h-5 w-5" />
                </Button>
            </div>
        </header>
    );
}

function TabButton({
    active,
    icon: Icon,
    label,
    onClick
}: {
    active?: boolean;
    icon: any;
    label: string;
    onClick?: () => void;
}) {
    return (
        <button
            onClick={onClick}
            className={cn(
                "flex items-center gap-2 px-4 py-2 rounded-full text-sm font-medium transition-all",
                active
                    ? "bg-background text-foreground shadow-sm"
                    : "text-muted-foreground hover:text-foreground hover:bg-background/50"
            )}
        >
            <Icon className="h-4 w-4" />
            <span className="hidden sm:inline">{label}</span>
        </button>
    );
}
