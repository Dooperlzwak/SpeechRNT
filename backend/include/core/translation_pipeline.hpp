#pragma once

#include "stt/stt_interface.hpp"
#include "mt/translation_interface.hpp"
#include "mt/language_detector.hpp"
#include "tts/tts_interface.hpp"
#include "core/task_queue.hpp"
#include "utils/performance_monitor.hpp"
#include <memory>
#include <functional>
#include <vector>
#include <string>
#include <mutex>
#include <unordered_map>
#include <chrono>

namespace speechrnt {
namespace core {

/**
 * Configuration for translation pipeline behavior
 */
struct TranslationPipelineConfig {
    // Confidence thresholds
    float min_transcription_confidence = 0.7f;
    float min_translation_confidence = 0.6f;
    
    // Pipeline behavior
    bool enable_automatic_translation = true;
    bool enable_confidence_gating = true;
    bool enable_multiple_candidates = true;
    bool enable_preliminary_translation = false;
    
    // Performance settings
    size_t max_concurrent_translations = 5;
    std::chrono::milliseconds translation_timeout = std::chrono::milliseconds(5000);
    
    // Quality settings
    size_t max_transcription_candidates = 3;
    float candidate_confidence_threshold = 0.5f;
    bool enable_fallback_translation = true;
    
    // Language detection settings
    bool enable_language_detection = true;
    bool enable_automatic_language_switching = true;
    float language_detection_confidence_threshold = 0.8f;
    bool enable_language_detection_caching = true;
    std::chrono::milliseconds language_detection_cache_ttl = std::chrono::milliseconds(30000);
    bool notify_language_changes = true;
};

/**
 * Result of translation pipeline processing
 */
struct PipelineResult {
    uint32_t utterance_id;
    std::string session_id;
    
    // Original transcription data
    stt::TranscriptionResult transcription;
    std::vector<stt::TranscriptionResult> transcription_candidates;
    
    // Translation data
    mt::TranslationResult translation;
    std::vector<mt::TranslationResult> translation_candidates;
    
    // Language detection data
    mt::LanguageDetectionResult language_detection;
    bool language_changed;
    std::string previous_language;
    
    // Pipeline metadata
    bool translation_triggered;
    bool confidence_gate_passed;
    bool language_detection_passed;
    std::string pipeline_stage; // "transcription", "language_detection", "translation", "complete", "error"
    std::string error_message;
    
    // Timing information
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point transcription_complete_time;
    std::chrono::steady_clock::time_point language_detection_complete_time;
    std::chrono::steady_clock::time_point translation_complete_time;
    
    PipelineResult() 
        : utterance_id(0)
        , language_changed(false)
        , translation_triggered(false)
        , confidence_gate_passed(false)
        , language_detection_passed(false)
        , pipeline_stage("transcription")
        , start_time(std::chrono::steady_clock::now()) {}
};

/**
 * Callback function types for pipeline events
 */
using TranscriptionCompleteCallback = std::function<void(const PipelineResult&)>;
using LanguageDetectionCompleteCallback = std::function<void(const PipelineResult&)>;
using LanguageChangeCallback = std::function<void(const std::string& sessionId, const std::string& oldLang, const std::string& newLang, float confidence)>;
using TranslationCompleteCallback = std::function<void(const PipelineResult&)>;
using PipelineErrorCallback = std::function<void(const PipelineResult&, const std::string&)>;
using ConfidenceGateCallback = std::function<bool(const stt::TranscriptionResult&)>; // Return true to proceed

/**
 * Translation Pipeline Integration
 * 
 * Orchestrates the flow from STT to MT, handling confidence-based gating,
 * multiple candidates, and automatic translation triggering.
 */
class TranslationPipeline {
public:
    explicit TranslationPipeline(const TranslationPipelineConfig& config = TranslationPipelineConfig{});
    ~TranslationPipeline();
    
    // Non-copyable, non-movable
    TranslationPipeline(const TranslationPipeline&) = delete;
    TranslationPipeline& operator=(const TranslationPipeline&) = delete;
    TranslationPipeline(TranslationPipeline&&) = delete;
    TranslationPipeline& operator=(TranslationPipeline&&) = delete;
    
    /**
     * Initialize the pipeline with STT, MT engines, and language detector
     */
    bool initialize(
        std::shared_ptr<stt::STTInterface> stt_engine,
        std::shared_ptr<mt::TranslationInterface> mt_engine,
        std::shared_ptr<mt::LanguageDetector> language_detector,
        std::shared_ptr<TaskQueue> task_queue
    );
    
    /**
     * Shutdown the pipeline and cleanup resources
     */
    void shutdown();
    
    /**
     * Process transcription result and trigger translation if appropriate
     * This is the main entry point for STT->MT integration
     */
    void processTranscriptionResult(
        uint32_t utterance_id,
        const std::string& session_id,
        const stt::TranscriptionResult& result,
        const std::vector<stt::TranscriptionResult>& candidates = {}
    );
    
    /**
     * Manually trigger translation for a specific transcription
     * Bypasses confidence gating if force_translation is true
     */
    void triggerTranslation(
        uint32_t utterance_id,
        const std::string& session_id,
        const stt::TranscriptionResult& transcription,
        bool force_translation = false
    );
    
    /**
     * Set language configuration for the pipeline
     */
    void setLanguageConfiguration(
        const std::string& source_language,
        const std::string& target_language
    );
    
    /**
     * Get current language configuration
     */
    std::pair<std::string, std::string> getLanguageConfiguration() const;
    
    /**
     * Update pipeline configuration
     */
    void updateConfiguration(const TranslationPipelineConfig& config);
    
    /**
     * Get current configuration
     */
    const TranslationPipelineConfig& getConfiguration() const { return config_; }
    
    /**
     * Register callbacks for pipeline events
     */
    void setTranscriptionCompleteCallback(TranscriptionCompleteCallback callback);
    void setLanguageDetectionCompleteCallback(LanguageDetectionCompleteCallback callback);
    void setLanguageChangeCallback(LanguageChangeCallback callback);
    void setTranslationCompleteCallback(TranslationCompleteCallback callback);
    void setPipelineErrorCallback(PipelineErrorCallback callback);
    void setConfidenceGateCallback(ConfidenceGateCallback callback);
    
    /**
     * Get pipeline statistics
     */
    struct Statistics {
        size_t total_transcriptions_processed;
        size_t language_detections_performed;
        size_t language_changes_detected;
        size_t language_detection_cache_hits;
        size_t translations_triggered;
        size_t confidence_gate_rejections;
        size_t language_detection_rejections;
        size_t successful_translations;
        size_t failed_translations;
        std::chrono::milliseconds average_translation_latency;
        std::chrono::milliseconds average_language_detection_latency;
        size_t active_pipeline_operations;
    };
    Statistics getStatistics() const;
    
    /**
     * Get active pipeline operations
     */
    std::vector<uint32_t> getActivePipelineOperations() const;
    
    /**
     * Cancel pipeline operation for specific utterance
     */
    bool cancelPipelineOperation(uint32_t utterance_id);
    
    /**
     * Check if pipeline is ready to process requests
     */
    bool isReady() const;
    
    /**
     * Enable/disable automatic translation
     */
    void setAutomaticTranslationEnabled(bool enabled);
    
    /**
     * Enable/disable confidence-based gating
     */
    void setConfidenceGatingEnabled(bool enabled);
    
    /**
     * Set confidence thresholds
     */
    void setConfidenceThresholds(float transcription_threshold, float translation_threshold);
    
    /**
     * Enable/disable preliminary translation for partial results
     */
    void setPreliminaryTranslationEnabled(bool enabled);
    
    /**
     * Language detection configuration
     */
    void setLanguageDetectionEnabled(bool enabled);
    void setAutomaticLanguageSwitchingEnabled(bool enabled);
    void setLanguageDetectionConfidenceThreshold(float threshold);
    void setLanguageDetectionCachingEnabled(bool enabled);
    void setLanguageChangeNotificationsEnabled(bool enabled);
    
    /**
     * Manual language detection trigger
     */
    void triggerLanguageDetection(
        uint32_t utterance_id,
        const std::string& session_id,
        const std::string& text,
        const std::vector<float>& audio_data = {}
    );
    
    /**
     * Get current detected language for session
     */
    std::string getCurrentDetectedLanguage(const std::string& session_id) const;
    
    /**
     * Clear language detection cache
     */
    void clearLanguageDetectionCache();
    void clearLanguageDetectionCache(const std::string& session_id);

private:
    /**
     * Internal pipeline state for tracking operations
     */
    struct PipelineOperation {
        uint32_t utterance_id;
        std::string session_id;
        PipelineResult result;
        std::chrono::steady_clock::time_point start_time;
        bool is_active;
        
        PipelineOperation(uint32_t id, const std::string& session)
            : utterance_id(id)
            , session_id(session)
            , start_time(std::chrono::steady_clock::now())
            , is_active(true) {
            result.utterance_id = id;
            result.session_id = session;
        }
    };
    
    // Core processing methods
    void processTranscriptionInternal(
        std::shared_ptr<PipelineOperation> operation,
        const stt::TranscriptionResult& transcription,
        const std::vector<stt::TranscriptionResult>& candidates
    );
    
    void executeLanguageDetection(std::shared_ptr<PipelineOperation> operation);
    
    void processLanguageDetectionResult(
        std::shared_ptr<PipelineOperation> operation,
        const mt::LanguageDetectionResult& detection_result
    );
    
    void executeTranslation(std::shared_ptr<PipelineOperation> operation);
    
    void processTranslationResult(
        std::shared_ptr<PipelineOperation> operation,
        const mt::TranslationResult& translation_result
    );
    
    // Confidence evaluation methods
    bool evaluateTranscriptionConfidence(const stt::TranscriptionResult& result) const;
    bool shouldTriggerLanguageDetection(const stt::TranscriptionResult& result) const;
    bool shouldTriggerTranslation(const stt::TranscriptionResult& result) const;
    bool evaluateLanguageDetectionConfidence(const mt::LanguageDetectionResult& result) const;
    stt::TranscriptionResult selectBestTranscriptionCandidate(
        const std::vector<stt::TranscriptionResult>& candidates
    ) const;
    
    // Multiple candidate handling
    void processMultipleCandidates(
        std::shared_ptr<PipelineOperation> operation,
        const std::vector<stt::TranscriptionResult>& candidates
    );
    
    std::vector<mt::TranslationResult> translateMultipleCandidates(
        const std::vector<stt::TranscriptionResult>& transcription_candidates
    );
    
    // Error handling and recovery
    void handlePipelineError(
        std::shared_ptr<PipelineOperation> operation,
        const std::string& error_message,
        const std::string& stage
    );
    
    void handleTranslationTimeout(std::shared_ptr<PipelineOperation> operation);
    
    // Callback notification methods
    void notifyTranscriptionComplete(const PipelineResult& result);
    void notifyLanguageDetectionComplete(const PipelineResult& result);
    void notifyLanguageChange(const std::string& session_id, const std::string& old_lang, const std::string& new_lang, float confidence);
    void notifyTranslationComplete(const PipelineResult& result);
    void notifyPipelineError(const PipelineResult& result, const std::string& error);
    
    // Operation management
    std::shared_ptr<PipelineOperation> createPipelineOperation(
        uint32_t utterance_id,
        const std::string& session_id
    );
    
    void completePipelineOperation(uint32_t utterance_id);
    std::shared_ptr<PipelineOperation> getPipelineOperation(uint32_t utterance_id) const;
    
    // Performance monitoring
    void updateStatistics(const PipelineOperation& operation);
    void recordTranslationLatency(std::chrono::milliseconds latency);
    void recordLanguageDetectionLatency(std::chrono::milliseconds latency);
    
    // Language detection caching
    struct LanguageDetectionCacheEntry {
        mt::LanguageDetectionResult result;
        std::chrono::steady_clock::time_point timestamp;
        std::string text_hash;
        
        LanguageDetectionCacheEntry() : timestamp(std::chrono::steady_clock::now()) {}
    };
    
    bool isLanguageDetectionCacheValid(const LanguageDetectionCacheEntry& entry) const;
    std::string calculateTextHash(const std::string& text) const;
    void cacheLanguageDetectionResult(const std::string& session_id, const std::string& text, const mt::LanguageDetectionResult& result);
    mt::LanguageDetectionResult getCachedLanguageDetectionResult(const std::string& session_id, const std::string& text) const;
    bool hasCachedLanguageDetectionResult(const std::string& session_id, const std::string& text) const;
    
    // Language change detection
    bool hasLanguageChanged(const std::string& session_id, const std::string& detected_language) const;
    void updateSessionLanguage(const std::string& session_id, const std::string& language);
    
    // Configuration and state
    TranslationPipelineConfig config_;
    std::string source_language_;
    std::string target_language_;
    bool initialized_;
    bool shutdown_requested_;
    
    // Engine references
    std::shared_ptr<stt::STTInterface> stt_engine_;
    std::shared_ptr<mt::TranslationInterface> mt_engine_;
    std::shared_ptr<mt::LanguageDetector> language_detector_;
    std::shared_ptr<TaskQueue> task_queue_;
    
    // Operation tracking
    mutable std::mutex operations_mutex_;
    std::unordered_map<uint32_t, std::shared_ptr<PipelineOperation>> active_operations_;
    
    // Callbacks
    std::mutex callbacks_mutex_;
    TranscriptionCompleteCallback transcription_complete_callback_;
    LanguageDetectionCompleteCallback language_detection_complete_callback_;
    LanguageChangeCallback language_change_callback_;
    TranslationCompleteCallback translation_complete_callback_;
    PipelineErrorCallback pipeline_error_callback_;
    ConfidenceGateCallback confidence_gate_callback_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    Statistics statistics_;
    std::vector<std::chrono::milliseconds> recent_translation_latencies_;
    std::vector<std::chrono::milliseconds> recent_language_detection_latencies_;
    static constexpr size_t MAX_LATENCY_SAMPLES = 100;
    
    // Language detection caching
    mutable std::mutex language_cache_mutex_;
    std::unordered_map<std::string, std::unordered_map<std::string, LanguageDetectionCacheEntry>> language_detection_cache_;
    
    // Session language tracking
    mutable std::mutex session_language_mutex_;
    std::unordered_map<std::string, std::string> session_languages_;
    
    // Performance monitoring
    std::shared_ptr<utils::PerformanceMonitor> performance_monitor_;
};

} // namespace core
} // namespace speechrnt