#pragma once

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <chrono>

namespace stt {

/**
 * Emotion types that can be detected from speech
 */
enum class EmotionType {
    NEUTRAL,
    HAPPY,
    SAD,
    ANGRY,
    FEAR,
    SURPRISE,
    DISGUST,
    CONTEMPT,
    EXCITEMENT,
    CALM
};

/**
 * Sentiment polarity classification
 */
enum class SentimentPolarity {
    POSITIVE,
    NEGATIVE,
    NEUTRAL
};

/**
 * Prosodic features extracted from audio for emotion detection
 */
struct ProsodicFeatures {
    float pitch_mean;           // Average fundamental frequency
    float pitch_std;            // Pitch variation
    float pitch_range;          // Pitch range (max - min)
    float energy_mean;          // Average energy level
    float energy_std;           // Energy variation
    float speaking_rate;        // Words per minute
    float pause_duration;       // Average pause length
    float jitter;              // Pitch perturbation
    float shimmer;             // Amplitude perturbation
    float spectral_centroid;   // Spectral brightness
    float spectral_rolloff;    // Spectral rolloff frequency
    std::vector<float> mfcc;   // Mel-frequency cepstral coefficients
};

/**
 * Emotion detection result with confidence scores
 */
struct EmotionResult {
    EmotionType primary_emotion;
    float confidence;
    std::map<EmotionType, float> emotion_probabilities;
    ProsodicFeatures prosodic_features;
    int64_t timestamp_ms;
    float arousal;             // Emotional intensity (0.0 to 1.0)
    float valence;             // Emotional positivity (-1.0 to 1.0)
};

/**
 * Sentiment analysis result
 */
struct SentimentResult {
    SentimentPolarity polarity;
    float confidence;
    float sentiment_score;     // -1.0 (negative) to 1.0 (positive)
    std::vector<std::string> sentiment_keywords;
    int64_t timestamp_ms;
};

/**
 * Combined emotion and sentiment analysis result
 */
struct EmotionalAnalysisResult {
    EmotionResult emotion;
    SentimentResult sentiment;
    float overall_confidence;
    bool is_emotional_transition;
    EmotionType previous_emotion;
    std::string analysis_metadata;
};

/**
 * Configuration for emotion detection
 */
struct EmotionDetectionConfig {
    bool enable_prosodic_analysis = true;
    bool enable_text_sentiment = true;
    bool enable_emotion_tracking = true;
    float emotion_confidence_threshold = 0.6f;
    float sentiment_confidence_threshold = 0.7f;
    size_t emotion_history_size = 10;
    float transition_threshold = 0.3f;
    std::string emotion_model_path;
    std::string sentiment_model_path;
};

/**
 * Interface for emotion detection models
 */
class EmotionDetectionModel {
public:
    virtual ~EmotionDetectionModel() = default;
    virtual bool initialize(const std::string& model_path) = 0;
    virtual EmotionResult detectEmotion(const std::vector<float>& audio_data, 
                                       int sample_rate) = 0;
    virtual bool isInitialized() const = 0;
};

/**
 * Interface for sentiment analysis models
 */
class SentimentAnalysisModel {
public:
    virtual ~SentimentAnalysisModel() = default;
    virtual bool initialize(const std::string& model_path) = 0;
    virtual SentimentResult analyzeSentiment(const std::string& text) = 0;
    virtual bool isInitialized() const = 0;
};

/**
 * Prosodic feature extractor for emotion detection
 */
class ProsodicFeatureExtractor {
public:
    ProsodicFeatureExtractor();
    ~ProsodicFeatureExtractor();

    bool initialize(int sample_rate);
    ProsodicFeatures extractFeatures(const std::vector<float>& audio_data);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Main emotion and sentiment detector class
 */
class EmotionDetector {
public:
    EmotionDetector();
    ~EmotionDetector();

    bool initialize(const EmotionDetectionConfig& config);
    void shutdown();

    // Main analysis functions
    EmotionalAnalysisResult analyzeEmotion(const std::vector<float>& audio_data,
                                          const std::string& transcribed_text,
                                          int sample_rate);
    
    EmotionResult detectEmotionFromAudio(const std::vector<float>& audio_data,
                                        int sample_rate);
    
    SentimentResult analyzeSentimentFromText(const std::string& text);

    // Emotion tracking and context
    void updateEmotionHistory(const EmotionResult& result);
    bool detectEmotionTransition(const EmotionResult& current_emotion);
    EmotionType getPreviousEmotion() const;
    std::vector<EmotionResult> getEmotionHistory(size_t count = 5) const;

    // Configuration
    void setConfig(const EmotionDetectionConfig& config);
    EmotionDetectionConfig getConfig() const;
    
    // Status
    bool isInitialized() const;
    std::string getLastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Utility functions for emotion detection
 */
namespace emotion_utils {
    std::string emotionTypeToString(EmotionType emotion);
    EmotionType stringToEmotionType(const std::string& emotion_str);
    std::string sentimentPolarityToString(SentimentPolarity polarity);
    SentimentPolarity stringToSentimentPolarity(const std::string& polarity_str);
    
    float calculateEmotionalDistance(const EmotionResult& emotion1, 
                                   const EmotionResult& emotion2);
    bool isEmotionalTransition(const EmotionResult& previous, 
                              const EmotionResult& current,
                              float threshold = 0.3f);
    
    std::map<std::string, float> emotionResultToJson(const EmotionResult& result);
    std::map<std::string, float> sentimentResultToJson(const SentimentResult& result);
}

} // namespace stt