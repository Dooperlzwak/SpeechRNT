# STT Performance Monitoring Integration

This document describes the enhanced Speech-to-Text (STT) performance monitoring system that provides comprehensive tracking of STT pipeline performance, quality metrics, and resource usage.

## Overview

The STT performance monitoring system consists of two main components:

1. **Enhanced PerformanceMonitor**: Extended with STT-specific metrics and methods
2. **STTPerformanceTracker**: Specialized high-level interface for STT performance tracking

## Features

### Pipeline Stage Tracking
- **VAD Latency**: Voice Activity Detection processing time
- **Preprocessing Latency**: Audio preprocessing and format conversion
- **Inference Latency**: Core STT model inference time
- **Postprocessing Latency**: Text cleanup and formatting
- **Streaming Latency**: Real-time streaming update generation

### Quality Metrics
- **Confidence Scores**: Transcription confidence tracking (partial and final)
- **Accuracy Scores**: Transcription accuracy when ground truth is available
- **Language Detection**: Language detection confidence and switching events
- **VAD Accuracy**: Voice activity detection accuracy scores

### Throughput and Concurrency
- **Transcription Throughput**: Transcriptions completed per second
- **Concurrent Transcriptions**: Number of simultaneous transcriptions
- **Queue Size**: Pending transcription requests
- **Success Rate**: Ratio of successful to total transcriptions

### Resource Usage
- **Buffer Usage**: Audio buffer memory consumption
- **Model Load Time**: Time to load STT models
- **GPU Utilization**: GPU memory and compute usage (when available)

### Streaming Metrics
- **Streaming Updates**: Number of incremental updates sent
- **Update Latency**: Time to generate streaming updates
- **Text Delta Tracking**: Changes in transcribed text over time

## Usage

### Basic Integration

```cpp
#include "stt/stt_performance_tracker.hpp"

// Create performance tracker
auto tracker = std::make_unique<STTPerformanceTracker>();
tracker->initialize(true); // Enable detailed tracking

// Start transcription tracking
uint64_t sessionId = tracker->startTranscription(utteranceId, isStreaming);

// Record pipeline stages
tracker->recordVADProcessing(sessionId, vadLatency, vadAccuracy, stateChanged);
tracker->recordPreprocessing(sessionId, prepLatency, audioLengthMs);
tracker->recordInference(sessionId, inferenceLatency, modelType, useGPU);
tracker->recordPostprocessing(sessionId, postLatency, textLength);

// Record transcription result
tracker->recordTranscriptionResult(sessionId, confidence, isPartial, 
                                 textLength, detectedLanguage, langConfidence);

// Complete transcription
tracker->completeTranscription(sessionId, success, finalConfidence, totalTextLength);
```

### RAII Session Tracking

```cpp
#include "stt/stt_performance_tracker.hpp"

// Automatic session lifecycle management
{
    STT_TRACK_TRANSCRIPTION(tracker, utteranceId, isStreaming);
    
    // Perform transcription work...
    
    // Session automatically completed when going out of scope
    // Mark as successful if needed
    sessionTracker.markSuccess(finalConfidence, textLength);
}
```

### Streaming Transcription Tracking

```cpp
// Start streaming transcription
uint64_t sessionId = tracker->startTranscription(utteranceId, true);

// Record streaming updates
for (each audio chunk) {
    tracker->recordStreamingUpdate(sessionId, updateLatency, isIncremental, textDelta);
    tracker->recordTranscriptionResult(sessionId, partialConfidence, true, currentTextLength);
}

// Finalize streaming transcription
tracker->completeTranscription(sessionId, true, finalConfidence, finalTextLength);
```

### Performance Monitoring

```cpp
// Get performance summary
auto summary = tracker->getPerformanceSummary();
std::cout << "Success Rate: " << summary["success_rate"] * 100 << "%" << std::endl;
std::cout << "Average Latency: " << summary["stt_latency_mean_ms"] << "ms" << std::endl;

// Get detailed metrics
auto detailedMetrics = tracker->getDetailedMetrics(10); // Last 10 minutes
auto latencyStats = detailedMetrics["overall_latency"];
std::cout << "P95 Latency: " << latencyStats.p95 << "ms" << std::endl;
```

## Available Metrics

### Latency Metrics
- `stt.latency_ms` - Overall STT pipeline latency
- `stt.vad_latency_ms` - VAD processing time
- `stt.preprocessing_latency_ms` - Audio preprocessing time
- `stt.inference_latency_ms` - Model inference time
- `stt.postprocessing_latency_ms` - Result postprocessing time
- `stt.streaming_latency_ms` - Streaming update generation time
- `stt.language_detection_latency_ms` - Language detection time

### Quality Metrics
- `stt.confidence_score` - Transcription confidence scores
- `stt.accuracy_score` - Transcription accuracy scores
- `stt.language_confidence` - Language detection confidence
- `vad.accuracy_score` - VAD accuracy scores

### Throughput Metrics
- `stt.throughput_ops_per_sec` - Transcriptions per second
- `stt.concurrent_transcriptions` - Active transcription count
- `stt.queue_size` - Pending transcription requests

### Resource Metrics
- `stt.buffer_usage_mb` - Audio buffer memory usage
- `stt.model_load_time_ms` - Model loading time
- `stt.streaming_updates_count` - Number of streaming updates

### VAD Metrics
- `vad.response_time_ms` - VAD response time
- `vad.state_changes_count` - VAD state transitions
- `vad.speech_detection_rate` - Speech detection frequency

## Performance Targets

Based on the requirements, the system tracks against these performance targets:

- **End-to-end latency**: < 2000ms
- **VAD response time**: < 100ms
- **STT latency**: < 500ms
- **Streaming update latency**: < 100ms
- **Confidence threshold**: > 0.7 for production use

## Integration with Existing Components

### WhisperSTT Integration
The `WhisperSTT` class automatically integrates with the performance tracker:

```cpp
class WhisperSTT : public STTInterface {
private:
    std::unique_ptr<STTPerformanceTracker> performanceTracker_;
    
public:
    WhisperSTT() : performanceTracker_(std::make_unique<STTPerformanceTracker>()) {
        performanceTracker_->initialize(true);
    }
    
    void transcribe(const std::vector<float>& audioData, TranscriptionCallback callback) override {
        STT_TRACK_TRANSCRIPTION(*performanceTracker_, utteranceId, false);
        // ... transcription logic with performance tracking
    }
};
```

### StreamingTranscriber Integration
The `StreamingTranscriber` uses the performance tracker for streaming metrics:

```cpp
class StreamingTranscriber {
private:
    std::shared_ptr<STTPerformanceTracker> performanceTracker_;
    
public:
    void startTranscription(uint32_t utteranceId, const std::vector<float>& audioData, bool isLive) {
        uint64_t sessionId = performanceTracker_->startTranscription(utteranceId, isLive);
        // ... streaming logic with performance tracking
    }
};
```

## Testing

The system includes comprehensive unit tests:

```bash
# Run STT performance tracker tests
cd backend/build
make test
./tests/unit/test_stt_performance_tracker
```

## Demo Application

A complete demo application shows all features:

```bash
# Build and run the demo
cd backend/build
make STTPerformanceDemo
./bin/STTPerformanceDemo
```

The demo simulates realistic STT workloads and shows:
- Basic transcription pipeline tracking
- Streaming transcription with incremental updates
- Concurrent transcription monitoring
- VAD performance tracking
- Language detection metrics
- Resource usage monitoring

## Configuration

### Enable/Disable Tracking
```cpp
tracker->setEnabled(false); // Disable for production if needed
```

### Detailed vs. Basic Tracking
```cpp
tracker->initialize(false); // Disable detailed per-utterance tracking for performance
```

### Performance Monitor Integration
```cpp
// Access global performance monitor
auto& perfMon = PerformanceMonitor::getInstance();
auto sttMetrics = perfMon.getSTTMetrics(10); // Last 10 minutes
```

## Best Practices

1. **Use RAII Session Tracking**: Prefer `TranscriptionSessionTracker` for automatic lifecycle management
2. **Batch Metric Updates**: Update throughput metrics periodically, not per transcription
3. **Monitor Resource Usage**: Track buffer usage to prevent memory leaks
4. **Set Performance Thresholds**: Use confidence and latency thresholds for quality control
5. **Regular Cleanup**: Reset metrics periodically in long-running applications

## Troubleshooting

### High Latency
- Check `inference_latency` vs. other pipeline stages
- Monitor GPU utilization if using GPU acceleration
- Verify model quantization settings

### Low Confidence Scores
- Check VAD accuracy - poor VAD can affect transcription quality
- Monitor language detection confidence
- Verify audio preprocessing quality

### Memory Issues
- Monitor `buffer_usage_mb` metrics
- Check for streaming transcription cleanup
- Verify model loading/unloading patterns

### Throughput Issues
- Monitor `concurrent_transcriptions` count
- Check queue size buildup
- Verify thread pool utilization

This enhanced STT performance monitoring system provides comprehensive visibility into the STT pipeline performance, enabling optimization and troubleshooting of speech recognition systems.