import { vi } from 'vitest';
import '@testing-library/jest-dom';

// Mock Web Audio API
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
  suspend: vi.fn(() => Promise.resolve()),
  state: 'running',
  sampleRate: 44100,
  destination: {
    connect: vi.fn(),
    disconnect: vi.fn()
  },
  addEventListener: vi.fn()
};

// Mock AudioContext constructor
(global as any).AudioContext = vi.fn(() => mockAudioContext);
(global as any).webkitAudioContext = vi.fn(() => mockAudioContext);

// Mock MediaRecorder
const mockMediaRecorder = {
  start: vi.fn(),
  stop: vi.fn(),
  pause: vi.fn(),
  resume: vi.fn(),
  requestData: vi.fn(),
  state: 'inactive',
  ondataavailable: null,
  onstart: null,
  onstop: null,
  onerror: null,
  onpause: null,
  onresume: null
};

(global as any).MediaRecorder = vi.fn(() => mockMediaRecorder);
(global as any).MediaRecorder.isTypeSupported = vi.fn(() => true);

// Mock getUserMedia
const mockGetUserMedia = vi.fn(() => Promise.resolve({
  getTracks: vi.fn(() => []),
  getAudioTracks: vi.fn(() => []),
  getVideoTracks: vi.fn(() => [])
}));

Object.defineProperty(global.navigator, 'mediaDevices', {
  value: {
    getUserMedia: mockGetUserMedia,
    enumerateDevices: vi.fn(() => Promise.resolve([]))
  },
  writable: true
});

// Mock localStorage
const localStorageMock = {
  getItem: vi.fn(),
  setItem: vi.fn(),
  removeItem: vi.fn(),
  clear: vi.fn(),
  length: 0,
  key: vi.fn()
};

Object.defineProperty(global, 'localStorage', {
  value: localStorageMock,
  writable: true
});

// Mock console methods to reduce noise in tests
global.console = {
  ...console,
  log: vi.fn(),
  warn: vi.fn(),
  error: vi.fn()
};

// Export mocks for use in tests
export {
  mockAudioContext,
  mockMediaRecorder,
  mockGetUserMedia,
  localStorageMock
};