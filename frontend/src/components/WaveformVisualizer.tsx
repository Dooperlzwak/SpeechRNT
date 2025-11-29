import { useEffect, useState } from 'react';

export function WaveformVisualizer({
    active,
    color = "bg-foreground"
}: {
    active: boolean;
    color?: string;
}) {
    const [bars, setBars] = useState<number[]>(Array(40).fill(10));

    useEffect(() => {
        if (!active) return;

        const interval = setInterval(() => {
            setBars(prev => prev.map(() => Math.max(10, Math.random() * 100)));
        }, 100);

        return () => clearInterval(interval);
    }, [active]);

    return (
        <div className="flex items-center justify-center gap-[2px] h-12 w-full max-w-md opacity-80">
            {bars.map((height, i) => (
                <div
                    key={i}
                    className={`w-1 rounded-full transition-all duration-100 ${color}`}
                    style={{
                        height: `${active ? height : 4}%`,
                        opacity: active ? 1 : 0.2
                    }}
                />
            ))}
        </div>
    );
}
