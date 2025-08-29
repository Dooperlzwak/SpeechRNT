#pragma once

#include "utils/performance_monitor.hpp"
#include <memory>
#include <chrono>
#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace stt {

/**
 * STT Performance Tracker - Specialized performance monitoring for STT pipeline
 * 
 * This class provides a high-level interface for tracking STT-specific performance
 * metrics including pipeline stage latencies, confidence scores, throughput, and
 * quality metrics. It integrates with the global PerformanceMonitor system.
 */
class STTPerformanceTracker {
public:
    STTPerformanceTracker();
    ~STTPerformanceTracker();
    
    /**
     * Initialize the performance tracker
     * @param enableDetailedTracking Enable detailed per-utterance tracking
     * @return true if initialization successful
     */
    bool initialize(bool enableDetailedTracking = true);
    
    /**
     * Start tracking a new transcription request
     * @param utteranceId Unique utterance identifier
     * @param isStreaming Whether this is a streaming transcription
     * @return tracking session ID for this transcription
     */
    uint64_t startTranscription(uint32_t utteranceId, bool isStreaming = false);
    
    /**
     * Record VAD processing time
     * @param sessionId Tracking session ID
     * @param latencyMs VAD processing latency in milliseconds
     * @param accuracy VAD accuracy score (0.0-1.0, -1 if unknown)
     * @param stateChanged Whether VAD state changed
     */
    void recordVADProcessing(uint64_t sessionId, double latencyMs, float accuracy = -1.0f, bool stateChanged = false);
    
    /**
     * Record audio preprocessing time
     * @param sessionId Tracking session ID
     * @param latencyMs Preprocessing latency in milliseconds
     * @param audioLengthMs Length of processed audio in milliseconds
     */
    void recordPreprocessing(uint64_t sessionId, double latencyMs, double audioLengthMs);
    
    /**
     * Record STT inference time
     * @param sessionId Tracking session ID
     * @param latencyMs Inference latency in milliseconds
     * @param modelType Model type used (e.g., "whisper-base", "whisper-large")
     * @param useGPU Whether GPU was used for inference
     */
    void recordInference(uint64_t sessionId, double latencyMs, const std::string& modelType = "", bool useGPU = false);
    
    /**
     * Record post-processing time
     * @param sessionId Tracking session ID
     * @param latencyMs Post-processing latency in milliseconds
     * @param textLength Length of transcribed text
     */
    void recordPostprocessing(uint64_t sessionId, double latencyMs, size_t textLength);
    
    /**
     * Record transcription result
     * @param sessionId Tracking session ID
     * @param confidence Transcription confidence score (0.0-1.0)
     * @param isPartial Whether this is a partial result
     * @param textLength Length of transcribed text
     * @param detectedLanguage Detected language code (empty if not detected)
     * @param languageConfidence Language detection confidence (0.0-1.0, -1 if not applicable)
     */
    void recordTranscriptionResult(uint64_t sessionId, float confidence, bool isPartial, 
                                 size_t textLength, const std::string& detectedLanguage = "", 
                                 float languageConfidence = -1.0f);
    
    /**
     * Record streaming update
     * @param sessionId Tracking session ID
     * @param updateLatencyMs Time to generate update in milliseconds
     * @param isIncremental Whether this is an incremental update
     * @param textDelta Change in text length since last update
     */
    void recordStreamingUpdate(uint64_t sessionId, double updateLatencyMs, bool isIncremental, int textDelta);
    
    /**
     * Complete transcription tracking
     * @param sessionId Tracking session ID
     * @param success Whether transcription completed successfully
     * @param finalConfidence Final confidence score
     * @param totalTextLength Total length of transcribed text
     */
    void completeTranscription(uint64_t sessionId, bool success, float finalConfidence = -1.0f, size_t totalTextLength = 0);
    
    /**
     * Record language detection metrics
     * @param detectionLatencyMs Time taken for language detection
     * @param confidence Detection confidence (0.0-1.0)
     * @param detectedLanguage Detected language code
     * @param previousLanguage Previous language (for language switching tracking)
     */
    void recordLanguageDetection(double detectionLatencyMs, float confidence, 
                               const std::string& detectedLanguage, const std::string& previousLanguage = "");
    
    /**
     * Record buffer usage metrics
     * @param bufferSizeMB Current buffer size in megabytes
     * @param utilizationPercent Buffer utilization percentage (0.0-100.0)
     * @param utteranceCount Number of active utterances in buffer
     */
    void recordBufferUsage(double bufferSizeMB, float utilizationPercent, int utteranceCount);
    
    /**
     * Record model loading metrics
     * @param modelType Type of model loaded
     * @param loadTimeMs Time taken to load model in milliseconds
     * @param modelSizeMB Model size in megabytes
     * @param useGPU Whether model was loaded on GPU
     */
    void recordModelLoading(const std::string& modelType, double loadTimeMs, double modelSizeMB, bool useGPU);
    
    /**
     * Update concurrent transcription count
     * @param count Current number of concurrent transcriptions
     */
    void updateConcurrentTranscriptions(int count);
    
    /**
     * Calculate and record throughput metrics
     * This should be called periodically to update throughput statistics
     */
    void updateThroughputMetrics();
    
    /**
     * Get current performance summary
     * @return map of key performance indicators
     */
    std::map<std::string, double> getPerformanceSummary() const;
    
    /**
     * Get detailed STT metrics
     * @param windowMinutes Time window for metrics calculation
     * @return detailed STT performance metrics
     */
    std::map<std::string, speechrnt::utils::MetricStats> getDetailedMetrics(int windowMinutes = 10) const;
    
    /**
     * Reset all performance counters
     */
    void reset();
    
    /**
     * Enable or disable performance tracking
     * @param enabled true to enable tracking
     */
    void setEnabled(bool enabled);
    
    /**
     * Check if performance tracking is enabled
     * @return true if enabled
     */
    bool isEnabled() const;

private:
    struct TranscriptionSession {
        uint32_t utteranceId;
        uint64_t sessionId;
        bool isStreaming;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point lastUpdateTime;
        
        // Stage timings
        double vadLatencyMs;
        double preprocessingLatencyMs;
        double inferenceLatencyMs;
        double postprocessingLatencyMs;
        
        // Quality metrics
        float bestConfidence;
        float finalConfidence;
        size_t totalTextLength;
        int streamingUpdates;
        
        // Status
        bool completed;
        bool successful;
        
        TranscriptionSession(uint32_t uttId, uint64_t sessId, bool streaming)
            : utteranceId(uttId), sessionId(sessId), isStreaming(streaming)
            , startTime(std::chrono::steady_clock::now())
            , lastUpdateTime(startTime)
            , vadLatencyMs(0), preprocessingLatencyMs(0)
            , inferenceLatencyMs(0), postprocessingLatencyMs(0)
            , bestConfidence(0), finalConfidence(0)
            , totalTextLength(0), streamingUpdates(0)
            , completed(false), successful(false) {}
    };
    
    // Configuration
    std::atomic<bool> enabled_;
    std::atomic<bool> detailedTracking_;
    
    // Session management
    std::mutex sessionsMutex_;
    std::unordered_map<uint64_t, TranscriptionSession> activeSessions_;
    std::atomic<uint64_t> nextSessionId_;
    
    // Performance counters
    std::atomic<uint64_t> totalTranscriptions_;
    std::atomic<uint64_t> successfulTranscriptions_;
    std::atomic<uint64_t> streamingTranscriptions_;
    std::atomic<int> currentConcurrentTranscriptions_;
    
    // Throughput tracking
    std::mutex throughputMutex_;
    std::chrono::steady_clock::time_point lastThroughputUpdate_;
    uint64_t transcriptionsAtLastUpdate_;
    
    // Reference to global performance monitor
    speechrnt::utils::PerformanceMonitor& performanceMonitor_;
    
    // Internal methods
    uint64_t generateSessionId();
    void cleanupCompletedSessions();
    double calculateElapsedMs(const std::chrono::steady_clock::time_point& startTime) const;
    void recordSessionMetrics(const TranscriptionSession& session);
};

/**
 * RAII helper for automatic transcription session tracking
 */
class TranscriptionSessionTracker {
public:
    TranscriptionSessionTracker(STTPerformanceTracker& tracker, uint32_t utteranceId, bool isStreaming = false);
    ~TranscriptionSessionTracker();
    
    // Get the session ID for manual metric recording
    uint64_t getSessionId() const { return sessionId_; }
    
    // Mark transcription as successful
    void markSuccess(float finalConfidence = -1.0f, size_t totalTextLength = 0);
    
    // Mark transcription as failed
    void markFailure();

private:
    STTPerformanceTracker& tracker_;
    uint64_t sessionId_;
    bool completed_;
};

// Convenience macros for STT performance tracking
#define STT_TRACK_TRANSCRIPTION(tracker, utteranceId, isStreaming) \
    TranscriptionSessionTracker sessionTracker(tracker, utteranceId, isStreaming)

#define STT_RECORD_VAD(tracker, sessionId, latencyMs, accuracy, stateChanged) \
    tracker.recordVADProcessing(sessionId, latencyMs, accuracy, stateChanged)

#define STT_RECORD_INFERENCE(tracker, sessionId, latencyMs, modelType, useGPU) \
    tracker.recordInference(sessionId, latencyMs, modelType, useGPU)

} // namespace stt