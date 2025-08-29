#pragma once

#include "stt/emotion_detector.hpp"
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <functional>

namespace stt {

/**
 * Represents an emotional segment in a conversation
 */
struct EmotionalSegment {
    uint32_t segment_id;
    EmotionType dominant_emotion;
    SentimentPolarity dominant_sentiment;
    int64_t start_timestamp_ms;
    int64_t end_timestamp_ms;
    float emotional_intensity;
    float confidence;
    std::string transcribed_text;
    std::vector<EmotionResult> emotion_samples;
    std::vector<SentimentResult> sentiment_samples;
};

/**
 * Represents an emotional transition between segments
 */
struct EmotionalTransition {
    uint32_t transition_id;
    uint32_t from_segment_id;
    uint32_t to_segment_id;
    EmotionType from_emotion;
    EmotionType to_emotion;
    SentimentPolarity from_sentiment;
    SentimentPolarity to_sentiment;
    int64_t transition_timestamp_ms;
    float transition_strength;  // 0.0 to 1.0
    std::string transition_type; // "gradual", "sudden", "oscillating"
};

/**
 * Emotional state tracking across conversation segments
 */
struct ConversationEmotionalState {
    uint32_t conversation_id;
    std::vector<EmotionalSegment> segments;
    std::vector<EmotionalTransition> transitions;
    EmotionType current_emotion;
    SentimentPolarity current_sentiment;
    float emotional_stability;  // 0.0 (very unstable) to 1.0 (very stable)
    float overall_sentiment_trend; // -1.0 (negative trend) to 1.0 (positive trend)
    int64_t last_update_timestamp_ms;
    std::map<EmotionType, float> emotion_distribution;
    std::map<SentimentPolarity, float> sentiment_distribution;
};

/**
 * Configuration for emotional context integration
 */
struct EmotionalContextConfig {
    float segment_min_duration_ms = 2000.0f;  // Minimum segment duration
    float segment_max_duration_ms = 30000.0f; // Maximum segment duration
    float transition_threshold = 0.4f;        // Threshold for detecting transitions
    float stability_window_ms = 10000.0f;     // Window for calculating stability
    size_t max_segments_history = 50;         // Maximum segments to keep in history
    bool enable_transcription_influence = true; // Use emotions to influence transcription
    float transcription_confidence_boost = 0.1f; // Confidence boost for emotional consistency
    bool enable_transition_detection = true;   // Enable transition detection
    bool enable_emotional_formatting = true;  // Enable emotion-aware formatting
};

/**
 * Emotional influence on transcription results
 */
struct EmotionalTranscriptionInfluence {
    float confidence_adjustment;    // Adjustment to transcription confidence
    std::vector<std::string> emotional_markers; // Emotional markers to add
    std::string formatting_style;   // Formatting style based on emotion
    bool should_emphasize;         // Whether to emphasize certain words
    std::vector<std::pair<size_t, size_t>> emphasis_ranges; // Character ranges to emphasize
};

/**
 * Manages emotional context across conversation segments
 */
class EmotionalContextManager {
public:
    EmotionalContextManager();
    ~EmotionalContextManager();

    bool initialize(const EmotionalContextConfig& config);
    void shutdown();

    // Main context management functions
    void updateEmotionalContext(uint32_t conversation_id,
                               const EmotionalAnalysisResult& analysis_result,
                               const std::string& transcribed_text);

    ConversationEmotionalState getConversationState(uint32_t conversation_id) const;
    
    // Emotional segmentation
    void processEmotionalSegmentation(uint32_t conversation_id,
                                     const EmotionalAnalysisResult& analysis_result,
                                     const std::string& transcribed_text);

    // Transition detection and reporting
    std::vector<EmotionalTransition> detectEmotionalTransitions(uint32_t conversation_id);
    EmotionalTransition getLastTransition(uint32_t conversation_id) const;

    // Transcription influence
    EmotionalTranscriptionInfluence calculateTranscriptionInfluence(
        uint32_t conversation_id,
        const std::string& base_transcription,
        float base_confidence) const;

    // Emotion-aware formatting
    std::string applyEmotionalFormatting(const std::string& text,
                                        const EmotionalAnalysisResult& emotion_result) const;

    // Analytics and insights
    float calculateEmotionalStability(uint32_t conversation_id) const;
    float calculateSentimentTrend(uint32_t conversation_id) const;
    std::map<EmotionType, float> getEmotionDistribution(uint32_t conversation_id) const;
    std::map<SentimentPolarity, float> getSentimentDistribution(uint32_t conversation_id) const;

    // History management
    void clearConversationHistory(uint32_t conversation_id);
    void setMaxHistorySize(size_t max_size);
    
    // Configuration
    void setConfig(const EmotionalContextConfig& config);
    EmotionalContextConfig getConfig() const;

    // Status and diagnostics
    bool isInitialized() const;
    std::string getLastError() const;
    size_t getActiveConversationCount() const;

    // Callback registration for real-time updates
    using EmotionalTransitionCallback = std::function<void(uint32_t, const EmotionalTransition&)>;
    using EmotionalSegmentCallback = std::function<void(uint32_t, const EmotionalSegment&)>;
    
    void registerTransitionCallback(EmotionalTransitionCallback callback);
    void registerSegmentCallback(EmotionalSegmentCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Utility functions for emotional context management
 */
namespace emotional_context_utils {
    
    // Segment analysis
    EmotionType calculateDominantEmotion(const std::vector<EmotionResult>& emotions);
    SentimentPolarity calculateDominantSentiment(const std::vector<SentimentResult>& sentiments);
    float calculateEmotionalIntensity(const std::vector<EmotionResult>& emotions);
    
    // Transition analysis
    std::string classifyTransitionType(const EmotionalTransition& transition);
    float calculateTransitionStrength(const EmotionResult& from, const EmotionResult& to);
    
    // Formatting utilities
    std::string addEmotionalMarkers(const std::string& text, EmotionType emotion);
    std::string adjustTextFormatting(const std::string& text, 
                                   EmotionType emotion, 
                                   SentimentPolarity sentiment);
    
    // Statistical utilities
    float calculateEmotionalVariance(const std::vector<EmotionResult>& emotions);
    float calculateSentimentSlope(const std::vector<SentimentResult>& sentiments);
    
    // JSON serialization
    std::map<std::string, std::string> emotionalSegmentToJson(const EmotionalSegment& segment);
    std::map<std::string, std::string> emotionalTransitionToJson(const EmotionalTransition& transition);
    std::map<std::string, float> conversationStateToJson(const ConversationEmotionalState& state);
}

} // namespace stt