#include "stt/stt_performance_tracker.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <chrono>

using namespace speechrnt::utils;
using utils::Logger;

namespace stt {

STTPerformanceTracker::STTPerformanceTracker()
    : enabled_(true)
    , detailedTracking_(true)
    , nextSessionId_(1)
    , totalTranscriptions_(0)
    , successfulTranscriptions_(0)
    , streamingTranscriptions_(0)
    , currentConcurrentTranscriptions_(0)
    , lastThroughputUpdate_(std::chrono::steady_clock::now())
    , transcriptionsAtLastUpdate_(0)
    , performanceMonitor_(PerformanceMonitor::getInstance()) {
}

STTPerformanceTracker::~STTPerformanceTracker() {
    // Clean up any remaining sessions
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    for (auto& pair : activeSessions_) {
        if (!pair.second.completed) {
            recordSessionMetrics(pair.second);
        }
    }
}

bool STTPerformanceTracker::initialize(bool enableDetailedTracking) {
    detailedTracking_ = enableDetailedTracking;
    
    Logger::info("STT Performance Tracker initialized (detailed tracking: " + 
                std::string(enableDetailedTracking ? "enabled" : "disabled") + ")");
    
    return true;
}

uint64_t STTPerformanceTracker::startTranscription(uint32_t utteranceId, bool isStreaming) {
    if (!enabled_.load()) {
        return 0;
    }
    
    uint64_t sessionId = generateSessionId();
    
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        activeSessions_.emplace(sessionId, TranscriptionSession(utteranceId, sessionId, isStreaming));
    }
    
    totalTranscriptions_++;
    if (isStreaming) {
        streamingTranscriptions_++;
    }
    
    // Update concurrent transcription count
    int currentCount = static_cast<int>(activeSessions_.size());
    currentConcurrentTranscriptions_ = currentCount;
    performanceMonitor_.recordConcurrentTranscriptions(currentCount);
    
    return sessionId;
}

void STTPerformanceTracker::recordVADProcessing(uint64_t sessionId, double latencyMs, float accuracy, bool stateChanged) {
    if (!enabled_.load() || sessionId == 0) {
        return;
    }
    
    // Record global VAD metrics
    performanceMonitor_.recordVADMetrics(latencyMs, accuracy, stateChanged);
    
    if (detailedTracking_.load()) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = activeSessions_.find(sessionId);
        if (it != activeSessions_.end()) {
            it->second.vadLatencyMs = latencyMs;
            performanceMonitor_.recordSTTStageLatency("vad", latencyMs, it->second.utteranceId);
        }
    }
}

void STTPerformanceTracker::recordPreprocessing(uint64_t sessionId, double latencyMs, double audioLengthMs) {
    if (!enabled_.load() || sessionId == 0) {
        return;
    }
    
    std::map<std::string, std::string> tags;
    tags["audio_length_ms"] = std::to_string(audioLengthMs);
    
    performanceMonitor_.recordLatency(PerformanceMonitor::METRIC_STT_PREPROCESSING_LATENCY, latencyMs, tags);
    
    if (detailedTracking_.load()) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = activeSessions_.find(sessionId);
        if (it != activeSessions_.end()) {
            it->second.preprocessingLatencyMs = latencyMs;
            performanceMonitor_.recordSTTStageLatency("preprocessing", latencyMs, it->second.utteranceId);
        }
    }
}

void STTPerformanceTracker::recordInference(uint64_t sessionId, double latencyMs, const std::string& modelType, bool useGPU) {
    if (!enabled_.load() || sessionId == 0) {
        return;
    }
    
    std::map<std::string, std::string> tags;
    if (!modelType.empty()) {
        tags["model_type"] = modelType;
    }
    tags["use_gpu"] = useGPU ? "true" : "false";
    
    performanceMonitor_.recordLatency(PerformanceMonitor::METRIC_STT_INFERENCE_LATENCY, latencyMs, tags);
    
    if (detailedTracking_.load()) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = activeSessions_.find(sessionId);
        if (it != activeSessions_.end()) {
            it->second.inferenceLatencyMs = latencyMs;
            performanceMonitor_.recordSTTStageLatency("inference", latencyMs, it->second.utteranceId);
        }
    }
}

void STTPerformanceTracker::recordPostprocessing(uint64_t sessionId, double latencyMs, size_t textLength) {
    if (!enabled_.load() || sessionId == 0) {
        return;
    }
    
    std::map<std::string, std::string> tags;
    tags["text_length"] = std::to_string(textLength);
    
    performanceMonitor_.recordLatency(PerformanceMonitor::METRIC_STT_POSTPROCESSING_LATENCY, latencyMs, tags);
    
    if (detailedTracking_.load()) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = activeSessions_.find(sessionId);
        if (it != activeSessions_.end()) {
            it->second.postprocessingLatencyMs = latencyMs;
            it->second.totalTextLength = std::max(it->second.totalTextLength, textLength);
            performanceMonitor_.recordSTTStageLatency("postprocessing", latencyMs, it->second.utteranceId);
        }
    }
}

void STTPerformanceTracker::recordTranscriptionResult(uint64_t sessionId, float confidence, bool isPartial, 
                                                    size_t textLength, const std::string& detectedLanguage, 
                                                    float languageConfidence) {
    if (!enabled_.load() || sessionId == 0) {
        return;
    }
    
    // Record global confidence metrics
    performanceMonitor_.recordSTTConfidence(confidence, isPartial, 0);
    
    // Record language detection if applicable
    if (!detectedLanguage.empty() && languageConfidence >= 0.0f) {
        std::map<std::string, std::string> tags;
        tags["detected_language"] = detectedLanguage;
        performanceMonitor_.recordMetric(PerformanceMonitor::METRIC_STT_LANGUAGE_CONFIDENCE, 
                                       static_cast<double>(languageConfidence), "score", tags);
    }
    
    if (detailedTracking_.load()) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = activeSessions_.find(sessionId);
        if (it != activeSessions_.end()) {
            it->second.bestConfidence = std::max(it->second.bestConfidence, confidence);
            it->second.totalTextLength = std::max(it->second.totalTextLength, textLength);
            it->second.lastUpdateTime = std::chrono::steady_clock::now();
            
            performanceMonitor_.recordSTTConfidence(confidence, isPartial, it->second.utteranceId);
        }
    }
}

void STTPerformanceTracker::recordStreamingUpdate(uint64_t sessionId, double updateLatencyMs, bool isIncremental, int textDelta) {
    if (!enabled_.load() || sessionId == 0) {
        return;
    }
    
    // Record global streaming metrics
    performanceMonitor_.recordStreamingUpdate(updateLatencyMs, std::abs(textDelta), isIncremental);
    
    if (detailedTracking_.load()) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = activeSessions_.find(sessionId);
        if (it != activeSessions_.end()) {
            it->second.streamingUpdates++;
            it->second.lastUpdateTime = std::chrono::steady_clock::now();
            
            performanceMonitor_.recordSTTStageLatency("streaming", updateLatencyMs, it->second.utteranceId);
        }
    }
}

void STTPerformanceTracker::completeTranscription(uint64_t sessionId, bool success, float finalConfidence, size_t totalTextLength) {
    if (!enabled_.load() || sessionId == 0) {
        return;
    }
    
    TranscriptionSession session(0, 0, false); // Default session for cleanup
    bool sessionFound = false;
    
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = activeSessions_.find(sessionId);
        if (it != activeSessions_.end()) {
            session = it->second;
            session.completed = true;
            session.successful = success;
            if (finalConfidence >= 0.0f) {
                session.finalConfidence = finalConfidence;
            }
            if (totalTextLength > 0) {
                session.totalTextLength = totalTextLength;
            }
            
            sessionFound = true;
            activeSessions_.erase(it);
        }
    }
    
    if (sessionFound) {
        if (success) {
            successfulTranscriptions_++;
        }
        
        // Record session metrics
        recordSessionMetrics(session);
        
        // Update concurrent transcription count
        int currentCount = 0;
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            currentCount = static_cast<int>(activeSessions_.size());
        }
        currentConcurrentTranscriptions_ = currentCount;
        performanceMonitor_.recordConcurrentTranscriptions(currentCount);
    }
}

void STTPerformanceTracker::recordLanguageDetection(double detectionLatencyMs, float confidence, 
                                                  const std::string& detectedLanguage, const std::string& previousLanguage) {
    if (!enabled_.load()) {
        return;
    }
    
    performanceMonitor_.recordLanguageDetection(detectionLatencyMs, confidence, detectedLanguage);
    
    // Track language switching
    if (!previousLanguage.empty() && previousLanguage != detectedLanguage) {
        std::map<std::string, std::string> tags;
        tags["from_language"] = previousLanguage;
        tags["to_language"] = detectedLanguage;
        performanceMonitor_.recordCounter("stt.language_switches", 1, tags);
    }
}

void STTPerformanceTracker::recordBufferUsage(double bufferSizeMB, float utilizationPercent, int utteranceCount) {
    if (!enabled_.load()) {
        return;
    }
    
    performanceMonitor_.recordBufferUsage(bufferSizeMB, utilizationPercent);
    
    std::map<std::string, std::string> tags;
    tags["utterance_count"] = std::to_string(utteranceCount);
    performanceMonitor_.recordMetric("stt.buffer_utterances", static_cast<double>(utteranceCount), "count", tags);
}

void STTPerformanceTracker::recordModelLoading(const std::string& modelType, double loadTimeMs, double modelSizeMB, bool useGPU) {
    if (!enabled_.load()) {
        return;
    }
    
    std::map<std::string, std::string> tags;
    tags["model_type"] = modelType;
    tags["use_gpu"] = useGPU ? "true" : "false";
    tags["model_size_mb"] = std::to_string(modelSizeMB);
    
    performanceMonitor_.recordLatency(PerformanceMonitor::METRIC_STT_MODEL_LOAD_TIME, loadTimeMs, tags);
}

void STTPerformanceTracker::updateConcurrentTranscriptions(int count) {
    if (!enabled_.load()) {
        return;
    }
    
    currentConcurrentTranscriptions_ = count;
    performanceMonitor_.recordConcurrentTranscriptions(count);
}

void STTPerformanceTracker::updateThroughputMetrics() {
    if (!enabled_.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(throughputMutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastThroughputUpdate_);
    
    if (timeSinceLastUpdate.count() >= 1000) { // Update every second
        uint64_t currentTranscriptions = totalTranscriptions_.load();
        uint64_t transcriptionsDelta = currentTranscriptions - transcriptionsAtLastUpdate_;
        
        double elapsedSeconds = timeSinceLastUpdate.count() / 1000.0;
        double throughput = transcriptionsDelta / elapsedSeconds;
        
        performanceMonitor_.recordSTTThroughput(throughput);
        
        lastThroughputUpdate_ = now;
        transcriptionsAtLastUpdate_ = currentTranscriptions;
    }
}

std::map<std::string, double> STTPerformanceTracker::getPerformanceSummary() const {
    auto summary = performanceMonitor_.getSTTPerformanceSummary();
    
    // Add tracker-specific metrics
    summary["total_transcriptions"] = static_cast<double>(totalTranscriptions_.load());
    summary["successful_transcriptions"] = static_cast<double>(successfulTranscriptions_.load());
    summary["streaming_transcriptions"] = static_cast<double>(streamingTranscriptions_.load());
    summary["current_concurrent_transcriptions"] = static_cast<double>(currentConcurrentTranscriptions_.load());
    
    // Calculate success rate
    uint64_t total = totalTranscriptions_.load();
    if (total > 0) {
        summary["success_rate"] = static_cast<double>(successfulTranscriptions_.load()) / total;
    } else {
        summary["success_rate"] = 0.0;
    }
    
    // Calculate streaming ratio
    if (total > 0) {
        summary["streaming_ratio"] = static_cast<double>(streamingTranscriptions_.load()) / total;
    } else {
        summary["streaming_ratio"] = 0.0;
    }
    
    return summary;
}

std::map<std::string, MetricStats> STTPerformanceTracker::getDetailedMetrics(int windowMinutes) const {
    return performanceMonitor_.getSTTMetrics(windowMinutes);
}

void STTPerformanceTracker::reset() {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    activeSessions_.clear();
    
    totalTranscriptions_ = 0;
    successfulTranscriptions_ = 0;
    streamingTranscriptions_ = 0;
    currentConcurrentTranscriptions_ = 0;
    
    {
        std::lock_guard<std::mutex> throughputLock(throughputMutex_);
        lastThroughputUpdate_ = std::chrono::steady_clock::now();
        transcriptionsAtLastUpdate_ = 0;
    }
    
    Logger::info("STT Performance Tracker reset");
}

void STTPerformanceTracker::setEnabled(bool enabled) {
    enabled_ = enabled;
    Logger::info("STT Performance Tracker " + std::string(enabled ? "enabled" : "disabled"));
}

bool STTPerformanceTracker::isEnabled() const {
    return enabled_.load();
}

uint64_t STTPerformanceTracker::generateSessionId() {
    return nextSessionId_++;
}

void STTPerformanceTracker::cleanupCompletedSessions() {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    
    auto it = activeSessions_.begin();
    while (it != activeSessions_.end()) {
        if (it->second.completed) {
            it = activeSessions_.erase(it);
        } else {
            ++it;
        }
    }
}

double STTPerformanceTracker::calculateElapsedMs(const std::chrono::steady_clock::time_point& startTime) const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - startTime);
    return duration.count() / 1000.0; // Convert to milliseconds
}

void STTPerformanceTracker::recordSessionMetrics(const TranscriptionSession& session) {
    if (!enabled_.load()) {
        return;
    }
    
    // Calculate total latency
    double totalLatency = session.vadLatencyMs + session.preprocessingLatencyMs + 
                         session.inferenceLatencyMs + session.postprocessingLatencyMs;
    
    if (totalLatency > 0) {
        performanceMonitor_.recordLatency(PerformanceMonitor::METRIC_STT_LATENCY, totalLatency);
    }
    
    // Record final confidence if available
    if (session.finalConfidence > 0) {
        performanceMonitor_.recordSTTConfidence(session.finalConfidence, false, session.utteranceId);
    }
    
    // Record streaming metrics if applicable
    if (session.isStreaming && session.streamingUpdates > 0) {
        std::map<std::string, std::string> tags;
        tags["updates_count"] = std::to_string(session.streamingUpdates);
        tags["utterance_id"] = std::to_string(session.utteranceId);
        
        performanceMonitor_.recordMetric("stt.streaming_session_updates", 
                                       static_cast<double>(session.streamingUpdates), "count", tags);
    }
}

// TranscriptionSessionTracker implementation
TranscriptionSessionTracker::TranscriptionSessionTracker(STTPerformanceTracker& tracker, uint32_t utteranceId, bool isStreaming)
    : tracker_(tracker)
    , sessionId_(tracker.startTranscription(utteranceId, isStreaming))
    , completed_(false) {
}

TranscriptionSessionTracker::~TranscriptionSessionTracker() {
    if (!completed_) {
        tracker_.completeTranscription(sessionId_, false); // Mark as failed if not explicitly completed
    }
}

void TranscriptionSessionTracker::markSuccess(float finalConfidence, size_t totalTextLength) {
    if (!completed_) {
        tracker_.completeTranscription(sessionId_, true, finalConfidence, totalTextLength);
        completed_ = true;
    }
}

void TranscriptionSessionTracker::markFailure() {
    if (!completed_) {
        tracker_.completeTranscription(sessionId_, false);
        completed_ = true;
    }
}

} // namespace stt