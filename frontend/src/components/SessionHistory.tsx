import { X, Play, Clock, ArrowRight } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { cn } from '@/lib/utils';
import type { ConversationEntry } from '@/store/appStore';

interface SessionHistoryProps {
    isOpen: boolean;
    onClose: () => void;
    conversationHistory: ConversationEntry[];
}

export function SessionHistory({ isOpen, onClose, conversationHistory }: SessionHistoryProps) {
    const formatTimestamp = (timestamp: Date) => {
        const now = new Date();
        const diff = now.getTime() - timestamp.getTime();
        const minutes = Math.floor(diff / 60000);
        const hours = Math.floor(diff / 3600000);

        if (minutes < 1) return 'Just now';
        if (minutes < 60) return `${minutes} min${minutes > 1 ? 's' : ''} ago`;
        if (hours < 24) return `${hours} hour${hours > 1 ? 's' : ''} ago`;
        return timestamp.toLocaleDateString();
    };

    return (
        <>
            <div
                className={cn(
                    "fixed inset-0 bg-black/50 backdrop-blur-sm z-40 transition-opacity duration-300",
                    isOpen ? "opacity-100" : "opacity-0 pointer-events-none"
                )}
                onClick={onClose}
            />

            <div
                className={cn(
                    "fixed inset-y-0 left-0 w-full max-w-md bg-background border-r border-border z-50 shadow-2xl transform transition-transform duration-300 ease-in-out flex flex-col",
                    isOpen ? "translate-x-0" : "-translate-x-full"
                )}
            >
                <div className="flex items-center justify-between p-6 border-b border-border">
                    <h2 className="text-xl font-semibold">History</h2>
                    <Button variant="ghost" size="icon" onClick={onClose}>
                        <X className="h-5 w-5" />
                    </Button>
                </div>

                <div className="flex-1 overflow-y-auto">
                    {conversationHistory.length > 0 ? (
                        conversationHistory.map((entry) => (
                            <div key={entry.utteranceId} className="p-6 border-b border-border hover:bg-muted/30 transition-colors group">
                                <div className="flex items-center justify-between mb-3 text-xs text-muted-foreground">
                                    <div className="flex items-center gap-1">
                                        <Clock className="h-3 w-3" />
                                        <span>{formatTimestamp(entry.timestamp)}</span>
                                    </div>
                                    <span className="px-1.5 py-0.5 rounded bg-muted border border-border">
                                        #{entry.utteranceId}
                                    </span>
                                </div>

                                <div className="space-y-3">
                                    <div className="flex gap-3">
                                        <Button size="icon" variant="ghost" className="h-6 w-6 mt-0.5 shrink-0 opacity-50 group-hover:opacity-100">
                                            <Play className="h-3 w-3" />
                                        </Button>
                                        <p className="text-sm leading-relaxed">
                                            {entry.originalText}
                                        </p>
                                    </div>

                                    <div className="flex gap-3 pl-9">
                                        <ArrowRight className="h-3 w-3 mt-1.5 text-muted-foreground shrink-0" />
                                        <p className="text-sm leading-relaxed text-blue-400">
                                            {entry.translatedText}
                                        </p>
                                    </div>
                                </div>
                            </div>
                        ))
                    ) : (
                        <div className="flex flex-col items-center justify-center h-full text-muted-foreground p-6">
                            <p className="text-sm">No conversation history yet</p>
                            <p className="text-xs mt-2">Start a session to see your translations here</p>
                        </div>
                    )}
                </div>
            </div>
        </>
    );
}
