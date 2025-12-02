#include "stt/emotional_context_manager.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace stt {

class EmotionalContextManager::Impl {
public:
    EmotionalContextConfig config_;
    std::map<uint32_t, ConversationEmotionalState> conversation_states_;
    std::vector<EmotionalTransitionCallback> transition_callbacks_;
    std::vector<EmotionalSegmentCallback> segment_callbacks_;
    bool initialized_ = false;
    std::string last_error_;
    uint32_t next_segment_id_ = 1;
    uint32_t next_transition_id_ = 1;

    bool initialize(const EmotionalContextConfig& config) {
        config_ = config;
        initialized_ = true;
        speechrnt::utils::Logger::info("EmotionalContextManager initialized successfully");
        return true;
    }

    void updateEmotionalContext(uint32_t conversation_id,
                               const EmotionalAnalysisResult& analysis_result,
                               const std::string& transcribed_text) {
        if (!initialized_) {
            last_error_ = "EmotionalContextManager not initialized";
            return;
        }

        try {
            // Get or create conversation state
            auto& state = getOrCreateConversationState(conversation_id);
            
            // Update current emotion and sentiment
            state.current_emotion = analysis_result.emotion.primary_emotion;
            state.current_sentiment = analysis_result.sentiment.polarity;
            state.last_update_timestamp_ms = analysis_result.emotion.timestamp_ms;

            // Process emotional segmentation
            processEmotionalSegmentation(conversation_id, analysis_result, transcribed_text);

            // Update emotion and sentiment distributions
            updateDistributions(state, analysis_result);

            // Calculate emotional stability and sentiment trend
            state.emotional_stability = calculateEmotionalStability(conversation_id);
            state.overall_sentiment_trend = calculateSentimentTrend(conversation_id);

            speechrnt::utils::Logger::debug("Updated emotional context for conversation " + 
                           std::to_string(conversation_id));

        } catch (const std::exception& e) {
            last_error_ = "Error updating emotional context: " + std::string(e.what());
            speechrnt::utils::Logger::error("EmotionalContextManager error: " + last_error_);
        }
    }

    void processEmotionalSegmentation(uint32_t conversation_id,
                                     const EmotionalAnalysisResult& analysis_result,
                                     const std::string& transcribed_text) {
        auto& state = conversation_states_[conversation_id];
        
        // Check if we need to start a new segment or continue the current one
        bool should_create_new_segment = false;
        
        if (state.segments.empty()) {
            // First segment
            should_create_new_segment = true;
        } else {
            auto& current_segment = state.segments.back();
            
            // Check for emotion/sentiment change
            if (analysis_result.is_emotional_transition ||
                current_segment.dominant_emotion != analysis_result.emotion.primary_emotion ||
                current_segment.dominant_sentiment != analysis_result.sentiment.polarity) {
                
                // Check if minimum segment duration has passed
                int64_t segment_duration = analysis_result.emotion.timestamp_ms - current_segment.start_timestamp_ms;
                if (segment_duration >= config_.segment_min_duration_ms) {
                    should_create_new_segment = true;
                }
            }
            
            // Check for maximum segment duration
            int64_t segment_duration = analysis_result.emotion.timestamp_ms - current_segment.start_timestamp_ms;
            if (segment_duration >= config_.segment_max_duration_ms) {
                should_create_new_segment = true;
            }
        }

        if (should_create_new_segment) {
            // Finalize current segment if exists
            if (!state.segments.empty()) {
                finalizeCurrentSegment(state, analysis_result.emotion.timestamp_ms);
            }
            
            // Create new segment
            createNewSegment(state, analysis_result, transcribed_text);
        } else {
            // Update current segment
            updateCurrentSegment(state, analysis_result, transcribed_text);
        }
    }

    std::vector<EmotionalTransition> detectEmotionalTransitions(uint32_t conversation_id) {
        auto it = conversation_states_.find(conversation_id);
        if (it == conversation_states_.end()) {
            return {};
        }

        return it->second.transitions;
    }

    EmotionalTranscriptionInfluence calculateTranscriptionInfluence(
        uint32_t conversation_id,
        const std::string& base_transcription,
        float base_confidence) const {
        
        EmotionalTranscriptionInfluence influence;
        influence.confidence_adjustment = 0.0f;
        influence.formatting_style = "normal";
        influence.should_emphasize = false;

        if (!config_.enable_transcription_influence) {
            return influence;
        }

        auto it = conversation_states_.find(conversation_id);
        if (it == conversation_states_.end() || it->second.segments.empty()) {
            return influence;
        }

        const auto& current_state = it->second;
        const auto& current_emotion = current_state.current_emotion;
        const auto& current_sentiment = current_state.current_sentiment;

        // Adjust confidence based on emotional consistency
        if (current_state.emotional_stability > 0.7f) {
            influence.confidence_adjustment = config_.transcription_confidence_boost;
        }

        // Add emotional markers based on current emotion
        switch (current_emotion) {
            case EmotionType::HAPPY:
            case EmotionType::EXCITEMENT:
                influence.emotional_markers.push_back("[cheerful]");
                influence.formatting_style = "enthusiastic";
                break;
            case EmotionType::SAD:
                influence.emotional_markers.push_back("[melancholy]");
                influence.formatting_style = "subdued";
                break;
            case EmotionType::ANGRY:
                influence.emotional_markers.push_back("[agitated]");
                influence.formatting_style = "intense";
                influence.should_emphasize = true;
                break;
            case EmotionType::FEAR:
                influence.emotional_markers.push_back("[anxious]");
                influence.formatting_style = "hesitant";
                break;
            case EmotionType::SURPRISE:
                influence.emotional_markers.push_back("[surprised]");
                influence.formatting_style = "animated";
                break;
            default:
                break;
        }

        // Add sentiment markers
        if (current_sentiment == SentimentPolarity::POSITIVE) {
            influence.emotional_markers.push_back("[positive]");
        } else if (current_sentiment == SentimentPolarity::NEGATIVE) {
            influence.emotional_markers.push_back("[negative]");
        }

        return influence;
    }

    std::string applyEmotionalFormatting(const std::string& text,
                                        const EmotionalAnalysisResult& emotion_result) const {
        if (!config_.enable_emotional_formatting) {
            return text;
        }

        std::string formatted_text = text;
        
        // Apply formatting based on emotion
        switch (emotion_result.emotion.primary_emotion) {
            case EmotionType::ANGRY:
                // Add emphasis for angry speech
                formatted_text = "**" + formatted_text + "**";
                break;
            case EmotionType::HAPPY:
            case EmotionType::EXCITEMENT:
                // Add exclamation for happy/excited speech
                if (!formatted_text.empty() && formatted_text.back() == '.') {
                    formatted_text.back() = '!';
                }
                break;
            case EmotionType::SAD:
                // Add ellipsis for sad speech
                formatted_text += "...";
                break;
            case EmotionType::SURPRISE:
                // Add question mark for surprised speech
                if (!formatted_text.empty() && formatted_text.back() == '.') {
                    formatted_text.back() = '?';
                }
                break;
            default:
                break;
        }

        // Add emotional context markers
        std::string emotion_marker = "[" + emotion_utils::emotionTypeToString(emotion_result.emotion.primary_emotion) + "]";
        formatted_text = emotion_marker + " " + formatted_text;

        return formatted_text;
    }

    float calculateEmotionalStability(uint32_t conversation_id) const {
        auto it = conversation_states_.find(conversation_id);
        if (it == conversation_states_.end() || it->second.segments.empty()) {
            return 0.5f; // Neutral stability
        }

        const auto& segments = it->second.segments;
        if (segments.size() < 2) {
            return 1.0f; // Single segment is perfectly stable
        }

        // Calculate stability based on emotion transitions
        int transition_count = 0;
        int64_t total_duration = 0;
        
        for (size_t i = 1; i < segments.size(); ++i) {
            if (segments[i].dominant_emotion != segments[i-1].dominant_emotion) {
                transition_count++;
            }
            total_duration += segments[i].end_timestamp_ms - segments[i].start_timestamp_ms;
        }

        // Stability is inversely related to transition frequency
        float transition_rate = (float)transition_count / segments.size();
        return std::max(0.0f, 1.0f - transition_rate);
    }

    float calculateSentimentTrend(uint32_t conversation_id) const {
        auto it = conversation_states_.find(conversation_id);
        if (it == conversation_states_.end() || it->second.segments.empty()) {
            return 0.0f; // Neutral trend
        }

        const auto& segments = it->second.segments;
        if (segments.size() < 2) {
            return 0.0f;
        }

        // Calculate sentiment trend using linear regression
        std::vector<float> sentiment_values;
        for (const auto& segment : segments) {
            float sentiment_value = 0.0f;
            switch (segment.dominant_sentiment) {
                case SentimentPolarity::POSITIVE: sentiment_value = 1.0f; break;
                case SentimentPolarity::NEGATIVE: sentiment_value = -1.0f; break;
                case SentimentPolarity::NEUTRAL: sentiment_value = 0.0f; break;
            }
            sentiment_values.push_back(sentiment_value);
        }

        return emotional_context_utils::calculateSentimentSlope(
            std::vector<SentimentResult>() // Simplified for now
        );
    }

private:
    ConversationEmotionalState& getOrCreateConversationState(uint32_t conversation_id) {
        auto it = conversation_states_.find(conversation_id);
        if (it == conversation_states_.end()) {
            ConversationEmotionalState new_state;
            new_state.conversation_id = conversation_id;
            new_state.current_emotion = EmotionType::NEUTRAL;
            new_state.current_sentiment = SentimentPolarity::NEUTRAL;
            new_state.emotional_stability = 0.5f;
            new_state.overall_sentiment_trend = 0.0f;
            new_state.last_update_timestamp_ms = 0;
            
            conversation_states_[conversation_id] = new_state;
            return conversation_states_[conversation_id];
        }
        return it->second;
    }

    void createNewSegment(ConversationEmotionalState& state,
                         const EmotionalAnalysisResult& analysis_result,
                         const std::string& transcribed_text) {
        EmotionalSegment new_segment;
        new_segment.segment_id = next_segment_id_++;
        new_segment.dominant_emotion = analysis_result.emotion.primary_emotion;
        new_segment.dominant_sentiment = analysis_result.sentiment.polarity;
        new_segment.start_timestamp_ms = analysis_result.emotion.timestamp_ms;
        new_segment.end_timestamp_ms = analysis_result.emotion.timestamp_ms;
        new_segment.emotional_intensity = analysis_result.emotion.arousal;
        new_segment.confidence = analysis_result.overall_confidence;
        new_segment.transcribed_text = transcribed_text;
        new_segment.emotion_samples.push_back(analysis_result.emotion);
        new_segment.sentiment_samples.push_back(analysis_result.sentiment);

        // Check for transition from previous segment
        if (!state.segments.empty()) {
            createTransition(state, state.segments.back(), new_segment);
        }

        state.segments.push_back(new_segment);

        // Maintain history size limit
        if (state.segments.size() > config_.max_segments_history) {
            state.segments.erase(state.segments.begin());
        }

        // Notify callbacks
        for (const auto& callback : segment_callbacks_) {
            callback(state.conversation_id, new_segment);
        }

        speechrnt::utils::Logger::debug("Created new emotional segment " + std::to_string(new_segment.segment_id));
    }

    void updateCurrentSegment(ConversationEmotionalState& state,
                             const EmotionalAnalysisResult& analysis_result,
                             const std::string& transcribed_text) {
        if (state.segments.empty()) {
            return;
        }

        auto& current_segment = state.segments.back();
        current_segment.end_timestamp_ms = analysis_result.emotion.timestamp_ms;
        current_segment.transcribed_text += " " + transcribed_text;
        current_segment.emotion_samples.push_back(analysis_result.emotion);
        current_segment.sentiment_samples.push_back(analysis_result.sentiment);

        // Recalculate dominant emotion and sentiment
        current_segment.dominant_emotion = emotional_context_utils::calculateDominantEmotion(
            current_segment.emotion_samples);
        current_segment.dominant_sentiment = emotional_context_utils::calculateDominantSentiment(
            current_segment.sentiment_samples);
        current_segment.emotional_intensity = emotional_context_utils::calculateEmotionalIntensity(
            current_segment.emotion_samples);
    }

    void finalizeCurrentSegment(ConversationEmotionalState& state, int64_t end_timestamp) {
        if (state.segments.empty()) {
            return;
        }

        auto& current_segment = state.segments.back();
        current_segment.end_timestamp_ms = end_timestamp;
    }

    void createTransition(ConversationEmotionalState& state,
                         const EmotionalSegment& from_segment,
                         const EmotionalSegment& to_segment) {
        if (!config_.enable_transition_detection) {
            return;
        }

        EmotionalTransition transition;
        transition.transition_id = next_transition_id_++;
        transition.from_segment_id = from_segment.segment_id;
        transition.to_segment_id = to_segment.segment_id;
        transition.from_emotion = from_segment.dominant_emotion;
        transition.to_emotion = to_segment.dominant_emotion;
        transition.from_sentiment = from_segment.dominant_sentiment;
        transition.to_sentiment = to_segment.dominant_sentiment;
        transition.transition_timestamp_ms = to_segment.start_timestamp_ms;
        
        // Calculate transition strength
        if (!from_segment.emotion_samples.empty() && !to_segment.emotion_samples.empty()) {
            transition.transition_strength = emotional_context_utils::calculateTransitionStrength(
                from_segment.emotion_samples.back(),
                to_segment.emotion_samples.front()
            );
        } else {
            transition.transition_strength = 0.5f;
        }

        transition.transition_type = emotional_context_utils::classifyTransitionType(transition);

        state.transitions.push_back(transition);

        // Notify callbacks
        for (const auto& callback : transition_callbacks_) {
            callback(state.conversation_id, transition);
        }

        speechrnt::utils::Logger::debug("Created emotional transition " + std::to_string(transition.transition_id));
    }

    void updateDistributions(ConversationEmotionalState& state,
                            const EmotionalAnalysisResult& analysis_result) {
        // Update emotion distribution
        state.emotion_distribution[analysis_result.emotion.primary_emotion] += 1.0f;
        
        // Normalize emotion distribution
        float total_emotions = 0.0f;
        for (const auto& pair : state.emotion_distribution) {
            total_emotions += pair.second;
        }
        if (total_emotions > 0.0f) {
            for (auto& pair : state.emotion_distribution) {
                pair.second /= total_emotions;
            }
        }

        // Update sentiment distribution
        state.sentiment_distribution[analysis_result.sentiment.polarity] += 1.0f;
        
        // Normalize sentiment distribution
        float total_sentiments = 0.0f;
        for (const auto& pair : state.sentiment_distribution) {
            total_sentiments += pair.second;
        }
        if (total_sentiments > 0.0f) {
            for (auto& pair : state.sentiment_distribution) {
                pair.second /= total_sentiments;
            }
        }
    }
};

EmotionalContextManager::EmotionalContextManager() : impl_(std::make_unique<Impl>()) {}
EmotionalContextManager::~EmotionalContextManager() = default;

bool EmotionalContextManager::initialize(const EmotionalContextConfig& config) {
    return impl_->initialize(config);
}

void EmotionalContextManager::shutdown() {
    impl_->initialized_ = false;
    impl_->conversation_states_.clear();
}

void EmotionalContextManager::updateEmotionalContext(uint32_t conversation_id,
                                                    const EmotionalAnalysisResult& analysis_result,
                                                    const std::string& transcribed_text) {
    impl_->updateEmotionalContext(conversation_id, analysis_result, transcribed_text);
}

ConversationEmotionalState EmotionalContextManager::getConversationState(uint32_t conversation_id) const {
    auto it = impl_->conversation_states_.find(conversation_id);
    if (it != impl_->conversation_states_.end()) {
        return it->second;
    }
    return ConversationEmotionalState{};
}

void EmotionalContextManager::processEmotionalSegmentation(uint32_t conversation_id,
                                                          const EmotionalAnalysisResult& analysis_result,
                                                          const std::string& transcribed_text) {
    impl_->processEmotionalSegmentation(conversation_id, analysis_result, transcribed_text);
}

std::vector<EmotionalTransition> EmotionalContextManager::detectEmotionalTransitions(uint32_t conversation_id) {
    return impl_->detectEmotionalTransitions(conversation_id);
}

EmotionalTransition EmotionalContextManager::getLastTransition(uint32_t conversation_id) const {
    auto it = impl_->conversation_states_.find(conversation_id);
    if (it != impl_->conversation_states_.end() && !it->second.transitions.empty()) {
        return it->second.transitions.back();
    }
    return EmotionalTransition{};
}

EmotionalTranscriptionInfluence EmotionalContextManager::calculateTranscriptionInfluence(
    uint32_t conversation_id,
    const std::string& base_transcription,
    float base_confidence) const {
    return impl_->calculateTranscriptionInfluence(conversation_id, base_transcription, base_confidence);
}

std::string EmotionalContextManager::applyEmotionalFormatting(const std::string& text,
                                                             const EmotionalAnalysisResult& emotion_result) const {
    return impl_->applyEmotionalFormatting(text, emotion_result);
}

float EmotionalContextManager::calculateEmotionalStability(uint32_t conversation_id) const {
    return impl_->calculateEmotionalStability(conversation_id);
}

float EmotionalContextManager::calculateSentimentTrend(uint32_t conversation_id) const {
    return impl_->calculateSentimentTrend(conversation_id);
}

std::map<EmotionType, float> EmotionalContextManager::getEmotionDistribution(uint32_t conversation_id) const {
    auto it = impl_->conversation_states_.find(conversation_id);
    if (it != impl_->conversation_states_.end()) {
        return it->second.emotion_distribution;
    }
    return {};
}

std::map<SentimentPolarity, float> EmotionalContextManager::getSentimentDistribution(uint32_t conversation_id) const {
    auto it = impl_->conversation_states_.find(conversation_id);
    if (it != impl_->conversation_states_.end()) {
        return it->second.sentiment_distribution;
    }
    return {};
}

void EmotionalContextManager::clearConversationHistory(uint32_t conversation_id) {
    impl_->conversation_states_.erase(conversation_id);
}

void EmotionalContextManager::setMaxHistorySize(size_t max_size) {
    impl_->config_.max_segments_history = max_size;
}

void EmotionalContextManager::setConfig(const EmotionalContextConfig& config) {
    impl_->config_ = config;
}

EmotionalContextConfig EmotionalContextManager::getConfig() const {
    return impl_->config_;
}

bool EmotionalContextManager::isInitialized() const {
    return impl_->initialized_;
}

std::string EmotionalContextManager::getLastError() const {
    return impl_->last_error_;
}

size_t EmotionalContextManager::getActiveConversationCount() const {
    return impl_->conversation_states_.size();
}

void EmotionalContextManager::registerTransitionCallback(EmotionalTransitionCallback callback) {
    impl_->transition_callbacks_.push_back(callback);
}

void EmotionalContextManager::registerSegmentCallback(EmotionalSegmentCallback callback) {
    impl_->segment_callbacks_.push_back(callback);
}

// Utility functions implementation
namespace emotional_context_utils {

EmotionType calculateDominantEmotion(const std::vector<EmotionResult>& emotions) {
    if (emotions.empty()) {
        return EmotionType::NEUTRAL;
    }

    std::map<EmotionType, float> emotion_counts;
    for (const auto& emotion : emotions) {
        emotion_counts[emotion.primary_emotion] += emotion.confidence;
    }

    auto max_emotion = std::max_element(emotion_counts.begin(), emotion_counts.end(),
                                       [](const auto& a, const auto& b) {
                                           return a.second < b.second;
                                       });

    return max_emotion != emotion_counts.end() ? max_emotion->first : EmotionType::NEUTRAL;
}

SentimentPolarity calculateDominantSentiment(const std::vector<SentimentResult>& sentiments) {
    if (sentiments.empty()) {
        return SentimentPolarity::NEUTRAL;
    }

    std::map<SentimentPolarity, float> sentiment_counts;
    for (const auto& sentiment : sentiments) {
        sentiment_counts[sentiment.polarity] += sentiment.confidence;
    }

    auto max_sentiment = std::max_element(sentiment_counts.begin(), sentiment_counts.end(),
                                         [](const auto& a, const auto& b) {
                                             return a.second < b.second;
                                         });

    return max_sentiment != sentiment_counts.end() ? max_sentiment->first : SentimentPolarity::NEUTRAL;
}

float calculateEmotionalIntensity(const std::vector<EmotionResult>& emotions) {
    if (emotions.empty()) {
        return 0.0f;
    }

    float total_arousal = 0.0f;
    for (const auto& emotion : emotions) {
        total_arousal += emotion.arousal;
    }

    return total_arousal / emotions.size();
}

std::string classifyTransitionType(const EmotionalTransition& transition) {
    if (transition.transition_strength > 0.7f) {
        return "sudden";
    } else if (transition.transition_strength > 0.3f) {
        return "gradual";
    } else {
        return "subtle";
    }
}

float calculateTransitionStrength(const EmotionResult& from, const EmotionResult& to) {
    return emotion_utils::calculateEmotionalDistance(from, to);
}

std::string addEmotionalMarkers(const std::string& text, EmotionType emotion) {
    std::string marker = "[" + emotion_utils::emotionTypeToString(emotion) + "]";
    return marker + " " + text;
}

std::string adjustTextFormatting(const std::string& text, 
                               EmotionType emotion, 
                               SentimentPolarity sentiment) {
    std::string formatted = text;
    
    // Apply emotion-based formatting
    switch (emotion) {
        case EmotionType::ANGRY:
            formatted = "**" + formatted + "**";
            break;
        case EmotionType::HAPPY:
        case EmotionType::EXCITEMENT:
            if (!formatted.empty() && formatted.back() == '.') {
                formatted.back() = '!';
            }
            break;
        case EmotionType::SAD:
            formatted += "...";
            break;
        default:
            break;
    }

    return formatted;
}

float calculateEmotionalVariance(const std::vector<EmotionResult>& emotions) {
    if (emotions.size() < 2) {
        return 0.0f;
    }

    // Calculate variance in arousal values
    float mean_arousal = 0.0f;
    for (const auto& emotion : emotions) {
        mean_arousal += emotion.arousal;
    }
    mean_arousal /= emotions.size();

    float variance = 0.0f;
    for (const auto& emotion : emotions) {
        float diff = emotion.arousal - mean_arousal;
        variance += diff * diff;
    }

    return variance / (emotions.size() - 1);
}

float calculateSentimentSlope(const std::vector<SentimentResult>& sentiments) {
    if (sentiments.size() < 2) {
        return 0.0f;
    }

    // Simple linear regression slope calculation
    float n = static_cast<float>(sentiments.size());
    float sum_x = 0.0f, sum_y = 0.0f, sum_xy = 0.0f, sum_x2 = 0.0f;

    for (size_t i = 0; i < sentiments.size(); ++i) {
        float x = static_cast<float>(i);
        float y = sentiments[i].sentiment_score;
        
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_x2 += x * x;
    }

    float denominator = n * sum_x2 - sum_x * sum_x;
    if (std::abs(denominator) < 1e-6f) {
        return 0.0f;
    }

    return (n * sum_xy - sum_x * sum_y) / denominator;
}

std::map<std::string, std::string> emotionalSegmentToJson(const EmotionalSegment& segment) {
    std::map<std::string, std::string> json_data;
    json_data["segment_id"] = std::to_string(segment.segment_id);
    json_data["dominant_emotion"] = emotion_utils::emotionTypeToString(segment.dominant_emotion);
    json_data["dominant_sentiment"] = emotion_utils::sentimentPolarityToString(segment.dominant_sentiment);
    json_data["start_timestamp_ms"] = std::to_string(segment.start_timestamp_ms);
    json_data["end_timestamp_ms"] = std::to_string(segment.end_timestamp_ms);
    json_data["emotional_intensity"] = std::to_string(segment.emotional_intensity);
    json_data["confidence"] = std::to_string(segment.confidence);
    json_data["transcribed_text"] = segment.transcribed_text;
    return json_data;
}

std::map<std::string, std::string> emotionalTransitionToJson(const EmotionalTransition& transition) {
    std::map<std::string, std::string> json_data;
    json_data["transition_id"] = std::to_string(transition.transition_id);
    json_data["from_segment_id"] = std::to_string(transition.from_segment_id);
    json_data["to_segment_id"] = std::to_string(transition.to_segment_id);
    json_data["from_emotion"] = emotion_utils::emotionTypeToString(transition.from_emotion);
    json_data["to_emotion"] = emotion_utils::emotionTypeToString(transition.to_emotion);
    json_data["from_sentiment"] = emotion_utils::sentimentPolarityToString(transition.from_sentiment);
    json_data["to_sentiment"] = emotion_utils::sentimentPolarityToString(transition.to_sentiment);
    json_data["transition_timestamp_ms"] = std::to_string(transition.transition_timestamp_ms);
    json_data["transition_strength"] = std::to_string(transition.transition_strength);
    json_data["transition_type"] = transition.transition_type;
    return json_data;
}

std::map<std::string, float> conversationStateToJson(const ConversationEmotionalState& state) {
    std::map<std::string, float> json_data;
    json_data["conversation_id"] = static_cast<float>(state.conversation_id);
    json_data["emotional_stability"] = state.emotional_stability;
    json_data["overall_sentiment_trend"] = state.overall_sentiment_trend;
    json_data["last_update_timestamp_ms"] = static_cast<float>(state.last_update_timestamp_ms);
    json_data["segments_count"] = static_cast<float>(state.segments.size());
    json_data["transitions_count"] = static_cast<float>(state.transitions.size());
    return json_data;
}

} // namespace emotional_context_utils

} // namespace stt