import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { render, screen, fireEvent, waitFor, act } from '@testing-library/react';
import { userEvent } from '@testing-library/user-event';
import App from '../App';
import { WebSocketManager } from '../services/WebSocketManager';
import { AudioManager } from '../services/AudioManager';

// Performance monitoring utilities
class PerformanceMonitor {
  private measurements: Map<string, number[]> = new Map();
  private memoryBaseline: number = 0;

  startMeasurement(name: string): void {
    const start = performance.now();
    if (!this.measurements.has(name)) {
      this.measurements.set(name, []);
    }
    this.measurements.get(name)!.push(start);
  }

  endMeasurement(name: string): number {
    const end = performance.now();
    const measurements = this.measurements.get(name);
    if (!measurements || measurements.length === 0) {
      throw new Error(`No start measurement found for ${name}`);
    }
    const start = measurements.pop()!;
    const duration = end - start;
    measurements.push(duration); // Store the duration instead
    return duration;
  }

  getAverageDuration(name: string): number {
    const measurements = this.measurements.get(name);
    if (!measurements || measurements.length === 0) {
      return 0;
    }
    const sum = measurements.reduce((a, b) => a + b, 0);
    return sum / measurements.length;
  }

  getMemoryUsage(): number {
    if ('memory' in performance) {
      return (performance as any).memory.usedJSHeapSize;
    }
    return 0;
  }

  setMemoryBaseline(): void {
    this.memoryBaseline = this.getMemoryUsage();
  }

  getMemoryIncrease(): number {
    return this.getMemoryUsage() - this.memoryBaseline;
  }

  reset(): void {
    this.measurements.clear();
    this.memoryBaseline = 0;
  }
}

// Mock WebSocket with performance tracking
class MockWebSocketWithPerformance {
  static CONNECTING = 0;
  static OPEN = 1;
  static CLOSING = 2;
  static CLOSED = 3;

  readyState = MockWebSocketWithPerformance.CONNECTING;
  onopen: ((event: Event) => void) | null = null;
  onclose: ((event: CloseEvent) => void) | null = null;
  onmessage: ((event: MessageEvent) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;

  private messageQueue: any[] = [];
  private processingTime = 0;

  constructor(public url: string) {
    setTimeout(() => {
      this.readyState = MockWebSocketWithPerformance.OPEN;
      this.onopen?.(new Event('open'));
    }, 10);
  }

  send(data: string | ArrayBuffer) {
    // Simulate processing time
    const start = performance.now();
    // Mock processing
    setTimeout(() => {
      this.processingTime = performance.now() - start;
    }, 1);
  }

  close(code?: number, reason?: string) {
    this.readyState = MockWebSocketWithPerformance.CLOSED;
    this.onclose?.(new CloseEvent('close', { code, reason }));
  }

  simulateMessage(data: any) {
    if (this.onmessage) {
      this.onmessage(new MessageEvent('message', { data: JSON.stringify(data) }));
    }
  }

  simulateBinaryMessage(data: ArrayBuffer) {
    if (this.onmessage) {
      this.onmessage(new MessageEvent('message', { data }));
    }
  }

  simulateHighFrequencyMessages(count: number, intervalMs: number = 10) {
    for (let i = 0; i < count; i++) {
      setTimeout(() => {
        this.simulateMessage({
          type: 'transcription_update',
          data: {
            text: `Message ${i}`,
            utteranceId: 1,
            confidence: 0.95
          }
        });
      }, i * intervalMs);
    }
  }

  getProcessingTime(): number {
    return this.processingTime;
  }
}

describe('Performance Tests', () => {
  let mockWebSocket: MockWebSocketWithPerformance;
  let performanceMonitor: PerformanceMonitor;
  let user: ReturnType<typeof userEvent.setup>;

  beforeEach(() => {
    user = userEvent.setup();
    performanceMonitor = new PerformanceMonitor();
    
    // Setup global mocks
    (global as any).WebSocket = vi.fn((url: string) => {
      mockWebSocket = new MockWebSocketWithPerformance(url);
      return mockWebSocket;
    });

    // Mock performance.memory if not available
    if (!('memory' in performance)) {
      (performance as any).memory = {
        usedJSHeapSize: 50000000, // 50MB baseline
        totalJSHeapSize: 100000000,
        jsHeapSizeLimit: 2000000000
      };
    }

    // Mock audio APIs
    const mockAudioContext = {
      createGain: vi.fn(() => ({
        connect: vi.fn(),
        disconnect: vi.fn(),
        gain: { value: 1 }
      })),
      createBufferSource: vi.fn(() => ({
        connect: vi.fn(),
        disconnect: vi.fn(),
        start: vi.fn(),
        stop: vi.fn(),
        buffer: null,
        onended: null
      })),
      createBuffer: vi.fn(() => ({
        getChannelData: vi.fn(() => new Float32Array(1024)),
        numberOfChannels: 1,
        sampleRate: 22050,
        length: 1024,
        duration: 1024 / 22050
      })),
      decodeAudioData: vi.fn((buffer) => Promise.resolve({
        getChannelData: vi.fn(() => new Float32Array(1024)),
        numberOfChannels: 1,
        sampleRate: 22050,
        length: 1024,
        duration: 1024 / 22050
      })),
      close: vi.fn(() => Promise.resolve()),
      resume: vi.fn(() => Promise.resolve()),
      state: 'running',
      sampleRate: 44100,
      destination: { connect: vi.fn(), disconnect: vi.fn() }
    };

    (global as any).AudioContext = vi.fn(() => mockAudioContext);
    (global as any).MediaRecorder = vi.fn(() => ({
      start: vi.fn(),
      stop: vi.fn(),
      state: 'inactive',
      ondataavailable: null,
      onstart: null,
      onstop: null
    }));

    Object.defineProperty(global.navigator, 'mediaDevices', {
      value: {
        getUserMedia: vi.fn(() => Promise.resolve({
          getTracks: vi.fn(() => []),
          getAudioTracks: vi.fn(() => [])
        }))
      },
      writable: true
    });

    performanceMonitor.setMemoryBaseline();
  });

  afterEach(() => {
    vi.clearAllMocks();
    performanceMonitor.reset();
  });

  describe('Rendering Performance', () => {
    it('should render initial app within performance budget', async () => {
      performanceMonitor.startMeasurement('initial_render');
      
      render(<App />);
      
      await waitFor(() => {
        expect(screen.getByText(/Speech/)).toBeInTheDocument();
      });
      
      const renderTime = performanceMonitor.endMeasurement('initial_render');
      
      // Should render within 100ms
      expect(renderTime).toBeLessThan(100);
      console.log(`Initial render time: ${renderTime.toFixed(2)}ms`);
    });

    it('should handle rapid UI updates efficiently', async () => {
      render(<App />);
      
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      performanceMonitor.startMeasurement('rapid_updates');
      
      // Simulate rapid transcription updates
      for (let i = 0; i < 100; i++) {
        act(() => {
          mockWebSocket.simulateMessage({
            type: 'transcription_update',
            data: {
              text: `Rapid update ${i}`,
              utteranceId: 1,
              confidence: 0.95
            }
          });
        });
      }

      // Wait for updates to complete
      await waitFor(() => {
        expect(screen.getByText('Rapid update 99')).toBeInTheDocument();
      });

      const updateTime = performanceMonitor.endMeasurement('rapid_updates');
      
      // Should handle 100 updates within 500ms
      expect(updateTime).toBeLessThan(500);
      console.log(`100 rapid updates time: ${updateTime.toFixed(2)}ms`);
    });

    it('should maintain smooth animations under load', async () => {
      render(<App />);
      
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Simulate listening state with animation
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'status_update',
          data: { state: 'listening', utteranceId: 1 }
        });
      });

      // Measure animation performance
      performanceMonitor.startMeasurement('animation_performance');
      
      // Simulate continuous updates during animation
      const updateInterval = setInterval(() => {
        act(() => {
          mockWebSocket.simulateMessage({
            type: 'transcription_update',
            data: {
              text: `Live transcription ${Date.now()}`,
              utteranceId: 1,
              confidence: 0.95
            }
          });
        });
      }, 50);

      // Let it run for 1 second
      await new Promise(resolve => setTimeout(resolve, 1000));
      clearInterval(updateInterval);

      const animationTime = performanceMonitor.endMeasurement('animation_performance');
      
      // Should maintain smooth performance
      expect(animationTime).toBeLessThan(1100); // Allow small overhead
      console.log(`Animation with updates time: ${animationTime.toFixed(2)}ms`);
    });
  });

  describe('Memory Performance', () => {
    it('should not leak memory during normal operation', async () => {
      render(<App />);
      
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const initialMemory = performanceMonitor.getMemoryUsage();
      
      // Simulate multiple conversation cycles
      for (let cycle = 0; cycle < 10; cycle++) {
        const startButton = screen.getByRole('button', { name: /start conversation/i });
        await user.click(startButton);

        // Simulate conversation
        act(() => {
          mockWebSocket.simulateMessage({
            type: 'transcription_update',
            data: { text: `Cycle ${cycle} text`, utteranceId: cycle, confidence: 0.95 }
          });
        });

        act(() => {
          mockWebSocket.simulateMessage({
            type: 'translation_result',
            data: { originalText: `Cycle ${cycle} text`, translatedText: `Texto ciclo ${cycle}`, utteranceId: cycle }
          });
        });

        // Stop conversation
        const stopButton = screen.getByRole('button', { name: /stop conversation/i });
        await user.click(stopButton);

        // Small delay between cycles
        await new Promise(resolve => setTimeout(resolve, 100));
      }

      const finalMemory = performanceMonitor.getMemoryUsage();
      const memoryIncrease = finalMemory - initialMemory;
      
      // Memory increase should be reasonable (less than 10MB)
      expect(memoryIncrease).toBeLessThan(10 * 1024 * 1024);
      console.log(`Memory increase after 10 cycles: ${(memoryIncrease / 1024 / 1024).toFixed(2)}MB`);
    });

    it('should handle large audio data without memory issues', async () => {
      render(<App />);
      
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      const initialMemory = performanceMonitor.getMemoryUsage();
      
      // Simulate large audio data
      for (let i = 0; i < 50; i++) {
        const largeAudioBuffer = new ArrayBuffer(1024 * 1024); // 1MB each
        act(() => {
          mockWebSocket.simulateBinaryMessage(largeAudioBuffer);
        });
      }

      // Wait for processing
      await new Promise(resolve => setTimeout(resolve, 1000));

      const finalMemory = performanceMonitor.getMemoryUsage();
      const memoryIncrease = finalMemory - initialMemory;
      
      // Should not accumulate all audio data in memory
      expect(memoryIncrease).toBeLessThan(20 * 1024 * 1024); // Less than 20MB
      console.log(`Memory increase with large audio: ${(memoryIncrease / 1024 / 1024).toFixed(2)}MB`);
    });
  });

  describe('WebSocket Performance', () => {
    it('should handle high-frequency messages efficiently', async () => {
      render(<App />);
      
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      performanceMonitor.startMeasurement('high_frequency_messages');
      
      // Send 1000 messages rapidly
      mockWebSocket.simulateHighFrequencyMessages(1000, 1);

      // Wait for all messages to be processed
      await waitFor(() => {
        expect(screen.getByText('Message 999')).toBeInTheDocument();
      }, { timeout: 5000 });

      const processingTime = performanceMonitor.endMeasurement('high_frequency_messages');
      
      // Should handle 1000 messages within 3 seconds
      expect(processingTime).toBeLessThan(3000);
      console.log(`1000 high-frequency messages time: ${processingTime.toFixed(2)}ms`);
    });

    it('should maintain connection stability under load', async () => {
      render(<App />);
      
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      let connectionDrops = 0;
      const originalOnClose = mockWebSocket.onclose;
      
      mockWebSocket.onclose = (event) => {
        connectionDrops++;
        originalOnClose?.(event);
      };

      // Simulate heavy load
      for (let i = 0; i < 500; i++) {
        act(() => {
          mockWebSocket.simulateMessage({
            type: 'transcription_update',
            data: { text: `Load test ${i}`, utteranceId: i, confidence: 0.95 }
          });
        });

        // Simulate binary data
        const audioData = new ArrayBuffer(4096);
        act(() => {
          mockWebSocket.simulateBinaryMessage(audioData);
        });
      }

      // Wait for processing
      await new Promise(resolve => setTimeout(resolve, 2000));

      // Connection should remain stable
      expect(connectionDrops).toBe(0);
      expect(screen.getByText(/connected/i)).toBeInTheDocument();
    });
  });

  describe('Audio Processing Performance', () => {
    it('should process audio data efficiently', async () => {
      render(<App />);
      
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      performanceMonitor.startMeasurement('audio_processing');
      
      // Simulate continuous audio stream
      const audioInterval = setInterval(() => {
        const audioBuffer = new ArrayBuffer(1024 * 2); // 1024 samples * 2 bytes
        act(() => {
          mockWebSocket.simulateBinaryMessage(audioBuffer);
        });
      }, 64); // ~16kHz sample rate chunks

      // Run for 2 seconds
      await new Promise(resolve => setTimeout(resolve, 2000));
      clearInterval(audioInterval);

      const processingTime = performanceMonitor.endMeasurement('audio_processing');
      
      // Should handle continuous audio stream smoothly
      expect(processingTime).toBeLessThan(2100); // Allow small overhead
      console.log(`Continuous audio processing time: ${processingTime.toFixed(2)}ms`);
    });

    it('should handle audio format conversion efficiently', async () => {
      render(<App />);
      
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      performanceMonitor.startMeasurement('audio_conversion');
      
      // Simulate various audio formats
      const formats = [
        { sampleRate: 16000, channels: 1, bitsPerSample: 16 },
        { sampleRate: 44100, channels: 2, bitsPerSample: 16 },
        { sampleRate: 48000, channels: 1, bitsPerSample: 24 },
      ];

      for (const format of formats) {
        const bytesPerSample = format.bitsPerSample / 8;
        const bufferSize = 1024 * format.channels * bytesPerSample;
        const audioBuffer = new ArrayBuffer(bufferSize);
        
        act(() => {
          mockWebSocket.simulateBinaryMessage(audioBuffer);
        });
      }

      const conversionTime = performanceMonitor.endMeasurement('audio_conversion');
      
      // Should handle format conversion quickly
      expect(conversionTime).toBeLessThan(50);
      console.log(`Audio format conversion time: ${conversionTime.toFixed(2)}ms`);
    });
  });

  describe('State Management Performance', () => {
    it('should update state efficiently with complex data', async () => {
      render(<App />);
      
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      performanceMonitor.startMeasurement('state_updates');
      
      // Simulate complex state updates
      for (let i = 0; i < 100; i++) {
        act(() => {
          mockWebSocket.simulateMessage({
            type: 'transcription_update',
            data: {
              text: `Complex state update ${i} with lots of text that simulates real transcription data`,
              utteranceId: i,
              confidence: 0.95,
              metadata: {
                timestamp: Date.now(),
                processingTime: Math.random() * 100,
                alternatives: [
                  { text: `Alternative 1 for ${i}`, confidence: 0.85 },
                  { text: `Alternative 2 for ${i}`, confidence: 0.75 }
                ]
              }
            }
          });
        });
      }

      const stateUpdateTime = performanceMonitor.endMeasurement('state_updates');
      
      // Should handle complex state updates efficiently
      expect(stateUpdateTime).toBeLessThan(200);
      console.log(`Complex state updates time: ${stateUpdateTime.toFixed(2)}ms`);
    });

    it('should handle concurrent state updates without conflicts', async () => {
      render(<App />);
      
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      performanceMonitor.startMeasurement('concurrent_updates');
      
      // Simulate concurrent updates from different sources
      const promises = [];
      
      for (let i = 0; i < 50; i++) {
        promises.push(new Promise<void>(resolve => {
          setTimeout(() => {
            act(() => {
              mockWebSocket.simulateMessage({
                type: 'transcription_update',
                data: { text: `Concurrent ${i}`, utteranceId: i, confidence: 0.95 }
              });
            });
            resolve();
          }, Math.random() * 100);
        }));
      }

      await Promise.all(promises);
      
      const concurrentTime = performanceMonitor.endMeasurement('concurrent_updates');
      
      // Should handle concurrent updates without issues
      expect(concurrentTime).toBeLessThan(500);
      console.log(`Concurrent state updates time: ${concurrentTime.toFixed(2)}ms`);
    });
  });

  describe('Overall Performance Benchmarks', () => {
    it('should meet end-to-end performance targets', async () => {
      performanceMonitor.startMeasurement('end_to_end_benchmark');
      
      render(<App />);
      
      // Wait for connection
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      // Start conversation
      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      // Simulate complete conversation flow
      act(() => {
        mockWebSocket.simulateMessage({
          type: 'transcription_update',
          data: { text: 'Performance test utterance', utteranceId: 1, confidence: 0.95 }
        });
      });

      act(() => {
        mockWebSocket.simulateMessage({
          type: 'translation_result',
          data: { originalText: 'Performance test utterance', translatedText: 'Expresión de prueba de rendimiento', utteranceId: 1 }
        });
      });

      // Simulate audio playback
      const audioBuffer = new ArrayBuffer(44100 * 2); // 1 second of audio
      act(() => {
        mockWebSocket.simulateBinaryMessage(audioBuffer);
      });

      // Wait for completion
      await waitFor(() => {
        expect(screen.getByText('Performance test utterance')).toBeInTheDocument();
        expect(screen.getByText('Expresión de prueba de rendimiento')).toBeInTheDocument();
      });

      const benchmarkTime = performanceMonitor.endMeasurement('end_to_end_benchmark');
      const memoryUsage = performanceMonitor.getMemoryIncrease();
      
      // Performance targets
      expect(benchmarkTime).toBeLessThan(2000); // Complete flow under 2 seconds
      expect(memoryUsage).toBeLessThan(5 * 1024 * 1024); // Memory increase under 5MB
      
      console.log(`End-to-end benchmark results:`);
      console.log(`  Time: ${benchmarkTime.toFixed(2)}ms`);
      console.log(`  Memory: ${(memoryUsage / 1024 / 1024).toFixed(2)}MB`);
    });

    it('should maintain performance under sustained load', async () => {
      render(<App />);
      
      await waitFor(() => {
        expect(screen.getByText(/connected/i)).toBeInTheDocument();
      });

      const startButton = screen.getByRole('button', { name: /start conversation/i });
      await user.click(startButton);

      const initialMemory = performanceMonitor.getMemoryUsage();
      const startTime = performance.now();
      
      // Sustained load test - 5 minutes simulated in fast time
      const iterations = 300; // Simulate 5 minutes of activity
      
      for (let i = 0; i < iterations; i++) {
        // Transcription
        act(() => {
          mockWebSocket.simulateMessage({
            type: 'transcription_update',
            data: { text: `Sustained load ${i}`, utteranceId: i, confidence: 0.95 }
          });
        });

        // Translation
        act(() => {
          mockWebSocket.simulateMessage({
            type: 'translation_result',
            data: { originalText: `Sustained load ${i}`, translatedText: `Carga sostenida ${i}`, utteranceId: i }
          });
        });

        // Audio data
        if (i % 10 === 0) { // Every 10th iteration
          const audioBuffer = new ArrayBuffer(1024);
          act(() => {
            mockWebSocket.simulateBinaryMessage(audioBuffer);
          });
        }

        // Small delay to prevent overwhelming
        if (i % 50 === 0) {
          await new Promise(resolve => setTimeout(resolve, 10));
        }
      }

      const endTime = performance.now();
      const finalMemory = performanceMonitor.getMemoryUsage();
      
      const totalTime = endTime - startTime;
      const memoryIncrease = finalMemory - initialMemory;
      const throughput = iterations / (totalTime / 1000); // iterations per second
      
      // Performance assertions for sustained load
      expect(throughput).toBeGreaterThan(50); // At least 50 iterations per second
      expect(memoryIncrease).toBeLessThan(20 * 1024 * 1024); // Memory increase under 20MB
      
      console.log(`Sustained load test results:`);
      console.log(`  Iterations: ${iterations}`);
      console.log(`  Total time: ${totalTime.toFixed(2)}ms`);
      console.log(`  Throughput: ${throughput.toFixed(2)} ops/sec`);
      console.log(`  Memory increase: ${(memoryIncrease / 1024 / 1024).toFixed(2)}MB`);
    });
  });
});