#include "stt/emotion_detector.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <fstream>
#include <sstream>
#include <regex>

namespace stt {

// Simple emotion detection model implementation
class SimpleEmotionModel : public EmotionDetectionModel {
public:
    bool initialize(const std::string& model_path) override {
        // For now, implement a rule-based emotion detector
        // In production, this would load a trained ML model
        initialized_ = true;
        utils::Logger::info("SimpleEmotionModel initialized");
        return true;
    }

    EmotionResult detectEmotion(const std::vector<float>& audio_data, 
                               int sample_rate) override {
        if (!initialized_) {
            return {};
        }

        EmotionResult result;
        result.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Extract basic prosodic features
        ProsodicFeatureExtractor extractor;
        extractor.initialize(sample_rate);
        result.prosodic_features = extractor.extractFeatures(audio_data);

        // Simple rule-based emotion detection based on prosodic features
        result.emotion_probabilities = analyzeProsodicFeatures(result.prosodic_features);
        
        // Find primary emotion
        auto max_emotion = std::max_element(result.emotion_probabilities.begin(),
                                          result.emotion_probabilities.end(),
                                          [](const auto& a, const auto& b) {
                                              return a.second < b.second;
                                          });
        
        if (max_emotion != result.emotion_probabilities.end()) {
            result.primary_emotion = max_emotion->first;
            result.confidence = max_emotion->second;
        } else {
            result.primary_emotion = EmotionType::NEUTRAL;
            result.confidence = 0.5f;
        }

        // Calculate arousal and valence
        result.arousal = calculateArousal(result.prosodic_features);
        result.valence = calculateValence(result.prosodic_features);

        return result;
    }

    bool isInitialized() const override {
        return initialized_;
    }

private:
    bool initialized_ = false;

    std::map<EmotionType, float> analyzeProsodicFeatures(const ProsodicFeatures& features) {
        std::map<EmotionType, float> probabilities;

        // Initialize all emotions with base probability
        for (int i = 0; i <= static_cast<int>(EmotionType::CALM); ++i) {
            probabilities[static_cast<EmotionType>(i)] = 0.1f;
        }

        // Rule-based emotion detection
        // High pitch + high energy = excitement/happiness
        if (features.pitch_mean > 200.0f && features.energy_mean > 0.7f) {
            probabilities[EmotionType::HAPPY] += 0.4f;
            probabilities[EmotionType::EXCITEMENT] += 0.3f;
        }

        // Low pitch + low energy = sadness
        if (features.pitch_mean < 150.0f && features.energy_mean < 0.3f) {
            probabilities[EmotionType::SAD] += 0.5f;
        }

        // High pitch variation + high energy = anger
        if (features.pitch_std > 50.0f && features.energy_mean > 0.6f) {
            probabilities[EmotionType::ANGRY] += 0.4f;
        }

        // Fast speaking rate = excitement/anxiety
        if (features.speaking_rate > 180.0f) {
            probabilities[EmotionType::EXCITEMENT] += 0.3f;
            probabilities[EmotionType::FEAR] += 0.2f;
        }

        // Slow speaking rate + low energy = calm/sad
        if (features.speaking_rate < 120.0f && features.energy_mean < 0.4f) {
            probabilities[EmotionType::CALM] += 0.3f;
            probabilities[EmotionType::SAD] += 0.2f;
        }

        // Normalize probabilities
        float total = 0.0f;
        for (const auto& pair : probabilities) {
            total += pair.second;
        }
        
        if (total > 0.0f) {
            for (auto& pair : probabilities) {
                pair.second /= total;
            }
        }

        return probabilities;
    }

    float calculateArousal(const ProsodicFeatures& features) {
        // Arousal based on energy and pitch variation
        float arousal = (features.energy_mean + features.pitch_std / 100.0f) / 2.0f;
        return std::clamp(arousal, 0.0f, 1.0f);
    }

    float calculateValence(const ProsodicFeatures& features) {
        // Valence based on pitch and spectral features
        float valence = (features.pitch_mean / 300.0f + features.spectral_centroid / 4000.0f) - 0.5f;
        return std::clamp(valence, -1.0f, 1.0f);
    }
};

// Simple sentiment analysis model implementation
class SimpleSentimentModel : public SentimentAnalysisModel {
public:
    bool initialize(const std::string& model_path) override {
        // Load sentiment keywords
        loadSentimentKeywords();
        initialized_ = true;
        utils::Logger::info("SimpleSentimentModel initialized");
        return true;
    }

    SentimentResult analyzeSentiment(const std::string& text) override {
        if (!initialized_) {
            return {};
        }

        SentimentResult result;
        result.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Convert to lowercase for analysis
        std::string lower_text = text;
        std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);

        // Count positive and negative words
        int positive_count = 0;
        int negative_count = 0;
        std::vector<std::string> found_keywords;

        for (const auto& word : positive_words_) {
            if (lower_text.find(word) != std::string::npos) {
                positive_count++;
                found_keywords.push_back(word);
            }
        }

        for (const auto& word : negative_words_) {
            if (lower_text.find(word) != std::string::npos) {
                negative_count++;
                found_keywords.push_back(word);
            }
        }

        // Calculate sentiment score
        int total_sentiment_words = positive_count + negative_count;
        if (total_sentiment_words == 0) {
            result.polarity = SentimentPolarity::NEUTRAL;
            result.sentiment_score = 0.0f;
            result.confidence = 0.5f;
        } else {
            float score = (float)(positive_count - negative_count) / total_sentiment_words;
            result.sentiment_score = score;
            
            if (score > 0.2f) {
                result.polarity = SentimentPolarity::POSITIVE;
                result.confidence = std::min(0.9f, 0.5f + std::abs(score));
            } else if (score < -0.2f) {
                result.polarity = SentimentPolarity::NEGATIVE;
                result.confidence = std::min(0.9f, 0.5f + std::abs(score));
            } else {
                result.polarity = SentimentPolarity::NEUTRAL;
                result.confidence = 0.6f;
            }
        }

        result.sentiment_keywords = found_keywords;
        return result;
    }

    bool isInitialized() const override {
        return initialized_;
    }

private:
    bool initialized_ = false;
    std::vector<std::string> positive_words_;
    std::vector<std::string> negative_words_;

    void loadSentimentKeywords() {
        // Basic positive sentiment words
        positive_words_ = {
            "good", "great", "excellent", "amazing", "wonderful", "fantastic",
            "happy", "joy", "love", "like", "enjoy", "pleased", "satisfied",
            "perfect", "awesome", "brilliant", "outstanding", "superb",
            "delighted", "thrilled", "excited", "cheerful", "optimistic"
        };

        // Basic negative sentiment words
        negative_words_ = {
            "bad", "terrible", "awful", "horrible", "disgusting", "hate",
            "sad", "angry", "frustrated", "disappointed", "upset", "annoyed",
            "worried", "concerned", "stressed", "anxious", "depressed",
            "furious", "outraged", "disgusted", "miserable", "unhappy"
        };
    }
};

// ProsodicFeatureExtractor implementation
class ProsodicFeatureExtractor::Impl {
public:
    int sample_rate_ = 16000;
    
    bool initialize(int sample_rate) {
        sample_rate_ = sample_rate;
        return true;
    }

    ProsodicFeatures extractFeatures(const std::vector<float>& audio_data) {
        ProsodicFeatures features = {};
        
        if (audio_data.empty()) {
            return features;
        }

        // Calculate basic statistics
        features.energy_mean = calculateRMSEnergy(audio_data);
        features.energy_std = calculateEnergyVariation(audio_data);
        
        // Estimate pitch using autocorrelation
        auto pitch_values = estimatePitch(audio_data);
        if (!pitch_values.empty()) {
            features.pitch_mean = calculateMean(pitch_values);
            features.pitch_std = calculateStdDev(pitch_values);
            features.pitch_range = *std::max_element(pitch_values.begin(), pitch_values.end()) -
                                  *std::min_element(pitch_values.begin(), pitch_values.end());
        }

        // Estimate speaking rate (simplified)
        features.speaking_rate = estimateSpeakingRate(audio_data);
        
        // Calculate spectral features
        features.spectral_centroid = calculateSpectralCentroid(audio_data);
        features.spectral_rolloff = calculateSpectralRolloff(audio_data);
        
        // Extract MFCC features (simplified)
        features.mfcc = extractMFCC(audio_data);

        return features;
    }

private:
    float calculateRMSEnergy(const std::vector<float>& audio_data) {
        float sum_squares = 0.0f;
        for (float sample : audio_data) {
            sum_squares += sample * sample;
        }
        return std::sqrt(sum_squares / audio_data.size());
    }

    float calculateEnergyVariation(const std::vector<float>& audio_data) {
        const size_t frame_size = 512;
        std::vector<float> frame_energies;
        
        for (size_t i = 0; i < audio_data.size(); i += frame_size) {
            size_t end = std::min(i + frame_size, audio_data.size());
            std::vector<float> frame(audio_data.begin() + i, audio_data.begin() + end);
            frame_energies.push_back(calculateRMSEnergy(frame));
        }
        
        return calculateStdDev(frame_energies);
    }

    std::vector<float> estimatePitch(const std::vector<float>& audio_data) {
        std::vector<float> pitch_values;
        const size_t frame_size = 1024;
        const size_t hop_size = 512;
        
        for (size_t i = 0; i < audio_data.size() - frame_size; i += hop_size) {
            std::vector<float> frame(audio_data.begin() + i, audio_data.begin() + i + frame_size);
            float pitch = estimateFramePitch(frame);
            if (pitch > 0) {
                pitch_values.push_back(pitch);
            }
        }
        
        return pitch_values;
    }

    float estimateFramePitch(const std::vector<float>& frame) {
        // Simple autocorrelation-based pitch estimation
        const int min_period = sample_rate_ / 500;  // 500 Hz max
        const int max_period = sample_rate_ / 80;   // 80 Hz min
        
        float max_correlation = 0.0f;
        int best_period = 0;
        
        for (int period = min_period; period < max_period && period < (int)frame.size() / 2; ++period) {
            float correlation = 0.0f;
            for (size_t i = 0; i < frame.size() - period; ++i) {
                correlation += frame[i] * frame[i + period];
            }
            
            if (correlation > max_correlation) {
                max_correlation = correlation;
                best_period = period;
            }
        }
        
        return best_period > 0 ? (float)sample_rate_ / best_period : 0.0f;
    }

    float estimateSpeakingRate(const std::vector<float>& audio_data) {
        // Simplified speaking rate estimation based on energy peaks
        const size_t frame_size = 512;
        std::vector<float> frame_energies;
        
        for (size_t i = 0; i < audio_data.size(); i += frame_size) {
            size_t end = std::min(i + frame_size, audio_data.size());
            std::vector<float> frame(audio_data.begin() + i, audio_data.begin() + end);
            frame_energies.push_back(calculateRMSEnergy(frame));
        }
        
        // Count energy peaks as syllable approximation
        int peak_count = 0;
        float threshold = calculateMean(frame_energies) * 0.7f;
        
        for (size_t i = 1; i < frame_energies.size() - 1; ++i) {
            if (frame_energies[i] > threshold &&
                frame_energies[i] > frame_energies[i-1] &&
                frame_energies[i] > frame_energies[i+1]) {
                peak_count++;
            }
        }
        
        float duration_seconds = (float)audio_data.size() / sample_rate_;
        float syllables_per_second = peak_count / duration_seconds;
        
        // Rough conversion: syllables per second to words per minute
        return syllables_per_second * 60.0f / 2.5f;  // Assume ~2.5 syllables per word
    }

    float calculateSpectralCentroid(const std::vector<float>& audio_data) {
        // Simplified spectral centroid calculation
        const size_t fft_size = 512;
        if (audio_data.size() < fft_size) return 0.0f;
        
        // Use first frame for simplicity
        std::vector<float> frame(audio_data.begin(), audio_data.begin() + fft_size);
        
        // Simple magnitude spectrum approximation
        float weighted_sum = 0.0f;
        float magnitude_sum = 0.0f;
        
        for (size_t i = 0; i < fft_size / 2; ++i) {
            float magnitude = std::abs(frame[i]);
            float frequency = (float)i * sample_rate_ / fft_size;
            weighted_sum += magnitude * frequency;
            magnitude_sum += magnitude;
        }
        
        return magnitude_sum > 0 ? weighted_sum / magnitude_sum : 0.0f;
    }

    float calculateSpectralRolloff(const std::vector<float>& audio_data) {
        // Simplified spectral rolloff (85% of spectral energy)
        return calculateSpectralCentroid(audio_data) * 1.5f;  // Rough approximation
    }

    std::vector<float> extractMFCC(const std::vector<float>& audio_data) {
        // Simplified MFCC extraction - return basic spectral features
        std::vector<float> mfcc(13, 0.0f);
        
        if (audio_data.size() < 512) {
            return mfcc;
        }
        
        // Use energy and spectral features as simplified MFCC
        mfcc[0] = calculateRMSEnergy(audio_data);
        mfcc[1] = calculateSpectralCentroid(audio_data) / 1000.0f;  // Normalize
        
        // Fill remaining with derived features
        for (size_t i = 2; i < mfcc.size(); ++i) {
            mfcc[i] = mfcc[1] * std::sin((float)i * 0.5f);
        }
        
        return mfcc;
    }

    float calculateMean(const std::vector<float>& values) {
        if (values.empty()) return 0.0f;
        return std::accumulate(values.begin(), values.end(), 0.0f) / values.size();
    }

    float calculateStdDev(const std::vector<float>& values) {
        if (values.size() < 2) return 0.0f;
        
        float mean = calculateMean(values);
        float sum_squares = 0.0f;
        
        for (float value : values) {
            float diff = value - mean;
            sum_squares += diff * diff;
        }
        
        return std::sqrt(sum_squares / (values.size() - 1));
    }
};

ProsodicFeatureExtractor::ProsodicFeatureExtractor() : impl_(std::make_unique<Impl>()) {}
ProsodicFeatureExtractor::~ProsodicFeatureExtractor() = default;

bool ProsodicFeatureExtractor::initialize(int sample_rate) {
    return impl_->initialize(sample_rate);
}

ProsodicFeatures ProsodicFeatureExtractor::extractFeatures(const std::vector<float>& audio_data) {
    return impl_->extractFeatures(audio_data);
}

// EmotionDetector implementation
class EmotionDetector::Impl {
public:
    EmotionDetectionConfig config_;
    std::unique_ptr<EmotionDetectionModel> emotion_model_;
    std::unique_ptr<SentimentAnalysisModel> sentiment_model_;
    std::vector<EmotionResult> emotion_history_;
    bool initialized_ = false;
    std::string last_error_;

    bool initialize(const EmotionDetectionConfig& config) {
        config_ = config;
        
        // Initialize emotion detection model
        emotion_model_ = std::make_unique<SimpleEmotionModel>();
        if (!emotion_model_->initialize(config.emotion_model_path)) {
            last_error_ = "Failed to initialize emotion detection model";
            return false;
        }

        // Initialize sentiment analysis model
        sentiment_model_ = std::make_unique<SimpleSentimentModel>();
        if (!sentiment_model_->initialize(config.sentiment_model_path)) {
            last_error_ = "Failed to initialize sentiment analysis model";
            return false;
        }

        initialized_ = true;
        utils::Logger::info("EmotionDetector initialized successfully");
        return true;
    }

    EmotionalAnalysisResult analyzeEmotion(const std::vector<float>& audio_data,
                                          const std::string& transcribed_text,
                                          int sample_rate) {
        EmotionalAnalysisResult result;
        
        if (!initialized_) {
            last_error_ = "EmotionDetector not initialized";
            return result;
        }

        try {
            // Detect emotion from audio
            if (config_.enable_prosodic_analysis && !audio_data.empty()) {
                result.emotion = emotion_model_->detectEmotion(audio_data, sample_rate);
            }

            // Analyze sentiment from text
            if (config_.enable_text_sentiment && !transcribed_text.empty()) {
                result.sentiment = sentiment_model_->analyzeSentiment(transcribed_text);
            }

            // Check for emotion transition
            if (config_.enable_emotion_tracking && !emotion_history_.empty()) {
                result.is_emotional_transition = detectEmotionTransition(result.emotion);
                result.previous_emotion = emotion_history_.back().primary_emotion;
            }

            // Calculate overall confidence
            result.overall_confidence = (result.emotion.confidence + result.sentiment.confidence) / 2.0f;

            // Update emotion history
            if (config_.enable_emotion_tracking) {
                updateEmotionHistory(result.emotion);
            }

            // Add metadata
            std::ostringstream metadata;
            metadata << "emotion_model:simple,sentiment_model:simple";
            result.analysis_metadata = metadata.str();

        } catch (const std::exception& e) {
            last_error_ = "Error during emotion analysis: " + std::string(e.what());
            utils::Logger::error("EmotionDetector error: " + last_error_);
        }

        return result;
    }

    void updateEmotionHistory(const EmotionResult& result) {
        emotion_history_.push_back(result);
        
        // Maintain history size limit
        if (emotion_history_.size() > config_.emotion_history_size) {
            emotion_history_.erase(emotion_history_.begin());
        }
    }

    bool detectEmotionTransition(const EmotionResult& current_emotion) {
        if (emotion_history_.empty()) {
            return false;
        }

        const auto& previous = emotion_history_.back();
        
        // Check if primary emotion changed
        if (current_emotion.primary_emotion != previous.primary_emotion) {
            return true;
        }

        // Check if confidence changed significantly
        float confidence_diff = std::abs(current_emotion.confidence - previous.confidence);
        if (confidence_diff > config_.transition_threshold) {
            return true;
        }

        return false;
    }
};

EmotionDetector::EmotionDetector() : impl_(std::make_unique<Impl>()) {}
EmotionDetector::~EmotionDetector() = default;

bool EmotionDetector::initialize(const EmotionDetectionConfig& config) {
    return impl_->initialize(config);
}

void EmotionDetector::shutdown() {
    impl_->initialized_ = false;
    impl_->emotion_history_.clear();
}

EmotionalAnalysisResult EmotionDetector::analyzeEmotion(const std::vector<float>& audio_data,
                                                       const std::string& transcribed_text,
                                                       int sample_rate) {
    return impl_->analyzeEmotion(audio_data, transcribed_text, sample_rate);
}

EmotionResult EmotionDetector::detectEmotionFromAudio(const std::vector<float>& audio_data,
                                                     int sample_rate) {
    if (!impl_->initialized_ || !impl_->emotion_model_) {
        return {};
    }
    return impl_->emotion_model_->detectEmotion(audio_data, sample_rate);
}

SentimentResult EmotionDetector::analyzeSentimentFromText(const std::string& text) {
    if (!impl_->initialized_ || !impl_->sentiment_model_) {
        return {};
    }
    return impl_->sentiment_model_->analyzeSentiment(text);
}

void EmotionDetector::updateEmotionHistory(const EmotionResult& result) {
    impl_->updateEmotionHistory(result);
}

bool EmotionDetector::detectEmotionTransition(const EmotionResult& current_emotion) {
    return impl_->detectEmotionTransition(current_emotion);
}

EmotionType EmotionDetector::getPreviousEmotion() const {
    if (impl_->emotion_history_.empty()) {
        return EmotionType::NEUTRAL;
    }
    return impl_->emotion_history_.back().primary_emotion;
}

std::vector<EmotionResult> EmotionDetector::getEmotionHistory(size_t count) const {
    if (impl_->emotion_history_.size() <= count) {
        return impl_->emotion_history_;
    }
    
    return std::vector<EmotionResult>(
        impl_->emotion_history_.end() - count,
        impl_->emotion_history_.end()
    );
}

void EmotionDetector::setConfig(const EmotionDetectionConfig& config) {
    impl_->config_ = config;
}

EmotionDetectionConfig EmotionDetector::getConfig() const {
    return impl_->config_;
}

bool EmotionDetector::isInitialized() const {
    return impl_->initialized_;
}

std::string EmotionDetector::getLastError() const {
    return impl_->last_error_;
}

// Utility functions
namespace emotion_utils {

std::string emotionTypeToString(EmotionType emotion) {
    switch (emotion) {
        case EmotionType::NEUTRAL: return "neutral";
        case EmotionType::HAPPY: return "happy";
        case EmotionType::SAD: return "sad";
        case EmotionType::ANGRY: return "angry";
        case EmotionType::FEAR: return "fear";
        case EmotionType::SURPRISE: return "surprise";
        case EmotionType::DISGUST: return "disgust";
        case EmotionType::CONTEMPT: return "contempt";
        case EmotionType::EXCITEMENT: return "excitement";
        case EmotionType::CALM: return "calm";
        default: return "unknown";
    }
}

EmotionType stringToEmotionType(const std::string& emotion_str) {
    if (emotion_str == "happy") return EmotionType::HAPPY;
    if (emotion_str == "sad") return EmotionType::SAD;
    if (emotion_str == "angry") return EmotionType::ANGRY;
    if (emotion_str == "fear") return EmotionType::FEAR;
    if (emotion_str == "surprise") return EmotionType::SURPRISE;
    if (emotion_str == "disgust") return EmotionType::DISGUST;
    if (emotion_str == "contempt") return EmotionType::CONTEMPT;
    if (emotion_str == "excitement") return EmotionType::EXCITEMENT;
    if (emotion_str == "calm") return EmotionType::CALM;
    return EmotionType::NEUTRAL;
}

std::string sentimentPolarityToString(SentimentPolarity polarity) {
    switch (polarity) {
        case SentimentPolarity::POSITIVE: return "positive";
        case SentimentPolarity::NEGATIVE: return "negative";
        case SentimentPolarity::NEUTRAL: return "neutral";
        default: return "unknown";
    }
}

SentimentPolarity stringToSentimentPolarity(const std::string& polarity_str) {
    if (polarity_str == "positive") return SentimentPolarity::POSITIVE;
    if (polarity_str == "negative") return SentimentPolarity::NEGATIVE;
    return SentimentPolarity::NEUTRAL;
}

float calculateEmotionalDistance(const EmotionResult& emotion1, const EmotionResult& emotion2) {
    // Calculate distance in arousal-valence space
    float arousal_diff = emotion1.arousal - emotion2.arousal;
    float valence_diff = emotion1.valence - emotion2.valence;
    return std::sqrt(arousal_diff * arousal_diff + valence_diff * valence_diff);
}

bool isEmotionalTransition(const EmotionResult& previous, const EmotionResult& current, float threshold) {
    if (previous.primary_emotion != current.primary_emotion) {
        return true;
    }
    
    float distance = calculateEmotionalDistance(previous, current);
    return distance > threshold;
}

std::map<std::string, float> emotionResultToJson(const EmotionResult& result) {
    std::map<std::string, float> json_data;
    json_data["confidence"] = result.confidence;
    json_data["arousal"] = result.arousal;
    json_data["valence"] = result.valence;
    json_data["timestamp_ms"] = static_cast<float>(result.timestamp_ms);
    
    // Add emotion probabilities
    for (const auto& pair : result.emotion_probabilities) {
        std::string key = "prob_" + emotionTypeToString(pair.first);
        json_data[key] = pair.second;
    }
    
    return json_data;
}

std::map<std::string, float> sentimentResultToJson(const SentimentResult& result) {
    std::map<std::string, float> json_data;
    json_data["confidence"] = result.confidence;
    json_data["sentiment_score"] = result.sentiment_score;
    json_data["timestamp_ms"] = static_cast<float>(result.timestamp_ms);
    return json_data;
}

} // namespace emotion_utils

} // namespace stt