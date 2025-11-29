import { useState } from 'react';
import { ChevronDown, Play, Copy, MoreHorizontal, Sparkles, Check, Volume2 } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { WaveformVisualizer } from './WaveformVisualizer';
import { Textarea } from '@/components/ui/textarea';
import type { TranslationMode } from './ControlBar';

interface TranslationConsoleProps {
    isRecording: boolean;
    mode: TranslationMode;
    originalText: string;
    translatedText: string;
    sourceLang: string;
    targetLang: string;
    onSourceTextChange?: (text: string) => void;
    onTranslate?: () => void;
}

export function TranslationConsole({
    isRecording,
    mode,
    originalText,
    translatedText,
    sourceLang,
    targetLang,
    onSourceTextChange,
    onTranslate
}: TranslationConsoleProps) {
    const [copied, setCopied] = useState(false);
    const [localSourceText, setLocalSourceText] = useState("");

    const handleCopy = () => {
        if (translatedText) {
            navigator.clipboard.writeText(translatedText);
            setCopied(true);
            setTimeout(() => setCopied(false), 2000);
        }
    };

    const handleSourceTextChange = (text: string) => {
        setLocalSourceText(text);
        onSourceTextChange?.(text);
    };

    const handleTranslate = () => {
        if (localSourceText.trim() && onTranslate) {
            onTranslate();
        }
    };

    // Get language flag emoji (simple mapping)
    const getLanguageFlag = (lang: string) => {
        const flags: Record<string, string> = {
            'en': '🇺🇸',
            'es': '🇪🇸',
            'fr': '🇫🇷',
            'de': '🇩🇪',
            'it': '🇮🇹',
            'pt': '🇵🇹',
            'ja': '🇯🇵',
            'ko': '🇰🇷',
            'zh': '🇨🇳',
        };
        return flags[lang.toLowerCase()] || '🌐';
    };

    return (
        <div className="flex-1 grid grid-cols-1 md:grid-cols-2 gap-0 md:gap-px bg-border overflow-hidden">
            {/* Source Panel */}
            <div className="bg-background flex flex-col p-6 md:p-8 relative group">
                <div className="flex items-center justify-between mb-8">
                    <Button variant="outline" className="h-10 gap-2 min-w-[180px] justify-between border-border bg-card/50 hover:bg-card">
                        <span className="flex items-center gap-2">
                            <Sparkles className="h-4 w-4 text-blue-500" />
                            Detect Language
                        </span>
                        <ChevronDown className="h-4 w-4 opacity-50" />
                    </Button>

                    <div className="flex items-center gap-2">
                        <span className="text-[10px] font-mono uppercase tracking-wider text-muted-foreground border border-border px-2 py-1 rounded-md">
                            {mode === "speech" ? "Whisper Tiny" : "Marian NMT"}
                        </span>
                    </div>
                </div>

                <div className="flex-1 flex flex-col justify-center min-h-[200px]">
                    {mode === "speech" ? (
                        // Speech mode - existing waveform interface
                        isRecording ? (
                            <div className="space-y-6">
                                <WaveformVisualizer active={true} color="bg-foreground" />
                                <p className="text-2xl md:text-3xl font-medium leading-relaxed animate-in fade-in slide-in-from-bottom-4 duration-500">
                                    {originalText || <span className="text-muted-foreground">Listening...</span>}
                                </p>
                            </div>
                        ) : (
                            <div className="flex flex-col items-center justify-center text-muted-foreground h-full opacity-50">
                                <div className="h-16 w-16 rounded-full bg-muted flex items-center justify-center mb-4">
                                    <Volume2 className="h-8 w-8" />
                                </div>
                                <p>Waiting for audio...</p>
                            </div>
                        )
                    ) : mode === "text" ? (
                        // Text mode - paste text interface
                        <div className="flex flex-col h-full">
                            <Textarea
                                placeholder="Paste or type text to translate..."
                                value={localSourceText}
                                onChange={(e) => handleSourceTextChange(e.target.value)}
                                className="flex-1 resize-none text-lg border-0 bg-transparent focus-visible:ring-0 focus-visible:ring-offset-0 p-0"
                            />
                            {localSourceText && (
                                <Button
                                    onClick={handleTranslate}
                                    className="mt-4 self-start"
                                >
                                    Translate
                                </Button>
                            )}
                        </div>
                    ) : (
                        // Type mode - live typing interface
                        <div className="flex flex-col h-full">
                            <Textarea
                                placeholder="Start typing to translate in real-time..."
                                value={localSourceText}
                                onChange={(e) => handleSourceTextChange(e.target.value)}
                                className="flex-1 resize-none text-lg border-0 bg-transparent focus-visible:ring-0 focus-visible:ring-offset-0 p-0"
                            />
                            {localSourceText && (
                                <div className="mt-4 flex items-center gap-2 text-xs text-muted-foreground">
                                    <div className="h-2 w-2 rounded-full bg-green-500 animate-pulse" />
                                    <span>Live translation active</span>
                                </div>
                            )}
                        </div>
                    )}
                </div>
            </div>

            {/* Target Panel */}
            <div className="bg-background flex flex-col p-6 md:p-8 relative group">
                <div className="flex items-center justify-between mb-8">
                    <Button variant="outline" className="h-10 gap-2 min-w-[180px] justify-between border-border bg-card/50 hover:bg-card">
                        <span className="flex items-center gap-2">
                            <span className="text-lg leading-none">{getLanguageFlag(targetLang)}</span>
                            {targetLang}
                        </span>
                        <ChevronDown className="h-4 w-4 opacity-50" />
                    </Button>

                    <div className="flex items-center gap-1">
                        <Button
                            variant="ghost"
                            size="icon"
                            className="h-8 w-8 text-muted-foreground hover:text-foreground"
                            onClick={handleCopy}
                            disabled={!translatedText}
                        >
                            {copied ? <Check className="h-4 w-4 text-green-500" /> : <Copy className="h-4 w-4" />}
                        </Button>
                        <Button variant="ghost" size="icon" className="h-8 w-8 text-muted-foreground">
                            <MoreHorizontal className="h-4 w-4" />
                        </Button>
                    </div>
                </div>

                <div className="flex-1 flex flex-col justify-center min-h-[200px]">
                    {mode === "speech" ? (
                        // Speech mode output
                        isRecording && translatedText ? (
                            <div className="space-y-6">
                                <p className="text-2xl md:text-3xl font-medium leading-relaxed text-blue-400 animate-in fade-in slide-in-from-bottom-4 duration-700 delay-150">
                                    {translatedText}
                                </p>

                                <div className="flex items-center gap-3 pt-4">
                                    <Button size="icon" className="rounded-full h-12 w-12 bg-blue-500 hover:bg-blue-600 text-white shadow-lg shadow-blue-500/20">
                                        <Play className="h-5 w-5 ml-0.5" />
                                    </Button>
                                    <div className="h-1 flex-1 bg-muted rounded-full overflow-hidden">
                                        <div className="h-full w-1/3 bg-blue-500 rounded-full" />
                                    </div>
                                    <span className="text-xs font-mono text-muted-foreground">0:04</span>
                                </div>
                            </div>
                        ) : (
                            <div className="flex flex-col items-center justify-center text-muted-foreground h-full opacity-50">
                                <p>Translation will appear here</p>
                            </div>
                        )
                    ) : translatedText ? (
                        // Text/Type mode output
                        <div className="space-y-6">
                            <p className="text-2xl md:text-3xl font-medium leading-relaxed text-blue-400">
                                {translatedText}
                            </p>
                            {mode === "text" && (
                                <div className="flex items-center gap-3 pt-4">
                                    <Button size="icon" className="rounded-full h-12 w-12 bg-blue-500 hover:bg-blue-600 text-white shadow-lg shadow-blue-500/20">
                                        <Play className="h-5 w-5 ml-0.5" />
                                    </Button>
                                    <span className="text-xs text-muted-foreground">Play translation</span>
                                </div>
                            )}
                        </div>
                    ) : (
                        <div className="flex flex-col items-center justify-center text-muted-foreground h-full opacity-50">
                            <p>Translation will appear here</p>
                        </div>
                    )}
                </div>
            </div>
        </div>
    );
}
