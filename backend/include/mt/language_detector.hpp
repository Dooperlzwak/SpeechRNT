#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>

namespace speechrnt {
namespace mt {

/**
 * Language detection result containing detected language and confidence information
 */
struct LanguageDetectionResult {
    std::string detectedLanguage;
    float confidence;
    std::vector<std::pair<std::string, float>> languageCandidates;
    bool isReliable;
    std::string detectionMethod; // "whisper", "text_analysis", "hybrid"
    
    LanguageDetectionResult() 
        : confidence(0.0f)
        , isReliable(false)
        , detectionMethod("text_analysis") {}
};

/**
 * Language detection component for automatic source language identification
 * Integrates with STT system's Whisper language detection and provides text-based analysis
 */
class LanguageDetector {
public:
    LanguageDetector();
    ~LanguageDetector();
    
    // Initialization
    bool initialize(const std::string& configPath = "config/language_detection.json");
    void cleanup();
    
    // Detection methods
    LanguageDetectionResult detectLanguage(const std::string& text);
    LanguageDetectionResult detectLanguageFromAudio(const std::vector<float>& audioData);
    LanguageDetectionResult detectLanguageHybrid(const std::string& text, const std::vector<float>& audioData);
    
    // Configuration
    void setConfidenceThreshold(float threshold);
    void setDetectionMethod(const std::string& method);
    void setSupportedLanguages(const std::vector<std::string>& languages);
    
    // Integration with STT
    void setSTTLanguageDetectionCallback(std::function<LanguageDetectionResult(const std::vector<float>&)> callback);
    
    // Validation and fallback
    bool isLanguageSupported(const std::string& languageCode) const;
    std::string getFallbackLanguage(const std::string& unsupportedLanguage) const;
    
    // Status methods
    bool isInitialized() const { return initialized_; }
    float getConfidenceThreshold() const { return confidenceThreshold_; }
    std::string getDetectionMethod() const { return detectionMethod_; }
    std::vector<std::string> getSupportedLanguages() const { return supportedLanguages_; }
    
private:
    struct LanguageModel {
        std::string languageCode;
        void* modelData;
        float accuracy;
        bool loaded;
        
        LanguageModel() : modelData(nullptr), accuracy(0.0f), loaded(false) {}
    };
    
    std::unordered_map<std::string, LanguageModel> languageModels_;
    std::vector<std::string> supportedLanguages_;
    std::unordered_map<std::string, std::string> fallbackLanguages_;
    float confidenceThreshold_;
    std::string detectionMethod_;
    bool initialized_;
    mutable std::mutex mutex_;
    
    // STT integration
    std::function<LanguageDetectionResult(const std::vector<float>&)> sttDetectionCallback_;
    
    // Text-based detection methods
    LanguageDetectionResult detectFromTextFeatures(const std::string& text);
    LanguageDetectionResult detectFromCharacteristics(const std::string& text);
    
    // Character frequency analysis
    std::unordered_map<std::string, std::unordered_map<char, float>> characterFrequencies_;
    void loadCharacterFrequencies();
    float calculateCharacterFrequencyScore(const std::string& text, const std::string& language);
    
    // Common word analysis
    std::unordered_map<std::string, std::vector<std::string>> commonWords_;
    void loadCommonWords();
    float calculateCommonWordScore(const std::string& text, const std::string& language);
    
    // N-gram analysis
    std::unordered_map<std::string, std::unordered_map<std::string, float>> ngramFrequencies_;
    void loadNgramFrequencies();
    float calculateNgramScore(const std::string& text, const std::string& language);
    
    // Integration helpers
    LanguageDetectionResult combineDetectionResults(
        const LanguageDetectionResult& textResult,
        const LanguageDetectionResult& audioResult
    );
    
    // Utility methods
    std::string normalizeText(const std::string& text);
    std::vector<std::string> extractWords(const std::string& text);
    std::vector<std::string> extractNgrams(const std::string& text, int n);
    bool loadConfiguration(const std::string& configPath);
    void initializeLanguageModels();
};

} // namespace mt
} // namespace speechrnt