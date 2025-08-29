#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace stt {

// Word-level timing and confidence information
struct WordTiming {
    std::string word;
    int64_t start_ms;
    int64_t end_ms;
    float confidence;
    
    WordTiming() : start_ms(0), end_ms(0), confidence(0.0f) {}
    WordTiming(const std::string& w, int64_t start, int64_t end, float conf)
        : word(w), start_ms(start), end_ms(end), confidence(conf) {}
};

// Quality indicators for transcription results
struct TranscriptionQuality {
    float signal_to_noise_ratio;
    float audio_clarity_score;
    bool has_background_noise;
    float processing_latency_ms;
    float average_token_probability;
    float no_speech_probability;
    
    TranscriptionQuality() 
        : signal_to_noise_ratio(0.0f)
        , audio_clarity_score(0.0f)
        , has_background_noise(false)
        , processing_latency_ms(0.0f)
        , average_token_probability(0.0f)
        , no_speech_probability(0.0f) {}
};

// Alternative transcription candidate
struct AlternativeTranscription {
    std::string text;
    float confidence;
    
    AlternativeTranscription() : confidence(0.0f) {}
    AlternativeTranscription(const std::string& t, float conf) : text(t), confidence(conf) {}
};

struct TranscriptionResult {
    std::string text;
    float confidence;
    bool is_partial;
    int64_t start_time_ms;
    int64_t end_time_ms;
    
    // Language detection fields
    std::string detected_language;
    float language_confidence;
    bool language_changed;
    
    // Enhanced confidence and quality fields
    std::vector<WordTiming> word_timings;
    std::vector<AlternativeTranscription> alternatives;
    TranscriptionQuality quality_metrics;
    
    // Confidence-based quality indicators
    bool meets_confidence_threshold;
    std::string quality_level; // "high", "medium", "low"
    
    TranscriptionResult() 
        : confidence(0.0f)
        , is_partial(false)
        , start_time_ms(0)
        , end_time_ms(0)
        , language_confidence(0.0f)
        , language_changed(false)
        , meets_confidence_threshold(false)
        , quality_level("low") {}
};

// Abstract interface for Speech-to-Text engines
class STTInterface {
public:
    using TranscriptionCallback = std::function<void(const TranscriptionResult& result)>;
    
    virtual ~STTInterface() = default;
    
    // Core functionality
    virtual bool initialize(const std::string& modelPath, int n_threads = 4) = 0;
    virtual void transcribe(const std::vector<float>& audioData, TranscriptionCallback callback) = 0;
    virtual void transcribeLive(const std::vector<float>& audioData, TranscriptionCallback callback) = 0;
    
    // Configuration
    virtual void setLanguage(const std::string& language) = 0;
    virtual void setTranslateToEnglish(bool translate) = 0;
    virtual void setTemperature(float temperature) = 0;
    virtual void setMaxTokens(int max_tokens) = 0;
    
    // Language detection configuration
    virtual void setLanguageDetectionEnabled(bool enabled) = 0;
    virtual void setLanguageDetectionThreshold(float threshold) = 0;
    virtual void setAutoLanguageSwitching(bool enabled) = 0;
    
    // Status
    virtual bool isInitialized() const = 0;
    virtual std::string getLastError() const = 0;
};

// Factory function for creating STT instances
std::unique_ptr<STTInterface> createWhisperSTT();

} // namespace stt