#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>

namespace speechrnt {
namespace mt {

/**
 * Quality metrics for translation assessment
 */
struct QualityMetrics {
    float overallConfidence;        // Overall translation confidence (0.0-1.0)
    float fluencyScore;            // How natural/fluent the translation sounds (0.0-1.0)
    float adequacyScore;           // How well meaning is preserved (0.0-1.0)
    float consistencyScore;        // Internal consistency of translation (0.0-1.0)
    std::vector<float> wordLevelConfidences; // Per-word confidence scores
    std::string qualityLevel;      // "high", "medium", "low"
    std::vector<std::string> qualityIssues; // Detected quality issues
    
    QualityMetrics() 
        : overallConfidence(0.0f)
        , fluencyScore(0.0f)
        , adequacyScore(0.0f)
        , consistencyScore(0.0f)
        , qualityLevel("low") {}
};

/**
 * Translation candidate with quality assessment
 */
struct TranslationCandidate {
    std::string translatedText;
    QualityMetrics qualityMetrics;
    float modelScore;              // Raw model confidence score
    int rank;                      // Ranking among candidates (1 = best)
    
    TranslationCandidate() : modelScore(0.0f), rank(0) {}
};

/**
 * Quality assessment configuration
 */
struct QualityConfig {
    float highQualityThreshold;    // Threshold for high quality (default: 0.8)
    float mediumQualityThreshold;  // Threshold for medium quality (default: 0.6)
    float lowQualityThreshold;     // Threshold for low quality (default: 0.4)
    bool enableWordLevelScoring;   // Enable per-word confidence scoring
    bool enableAlternatives;       // Enable alternative generation
    int maxAlternatives;           // Maximum number of alternatives to generate
    bool enableQualityIssueDetection; // Enable quality issue detection
    
    QualityConfig()
        : highQualityThreshold(0.8f)
        , mediumQualityThreshold(0.6f)
        , lowQualityThreshold(0.4f)
        , enableWordLevelScoring(true)
        , enableAlternatives(true)
        , maxAlternatives(3)
        , enableQualityIssueDetection(true) {}
};

/**
 * Translation quality and confidence assessment manager
 * Provides comprehensive quality metrics and confidence scoring for translations
 */
class QualityManager {
public:
    QualityManager();
    ~QualityManager();
    
    /**
     * Initialize the quality manager with configuration
     * @param configPath Path to quality assessment configuration file
     * @return true if initialization successful
     */
    bool initialize(const std::string& configPath = "config/quality_assessment.json");
    
    /**
     * Clean up resources
     */
    void cleanup();
    
    /**
     * Assess translation quality comprehensively
     * @param sourceText Original text
     * @param translatedText Translated text
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @param modelScores Optional model confidence scores
     * @return Quality metrics
     */
    QualityMetrics assessTranslationQuality(
        const std::string& sourceText,
        const std::string& translatedText,
        const std::string& sourceLang,
        const std::string& targetLang,
        const std::vector<float>& modelScores = {}
    );
    
    /**
     * Calculate confidence score for translation
     * @param sourceText Original text
     * @param translatedText Translated text
     * @param modelScores Model confidence scores
     * @return Confidence score (0.0-1.0)
     */
    float calculateConfidenceScore(
        const std::string& sourceText,
        const std::string& translatedText,
        const std::vector<float>& modelScores
    );
    
    /**
     * Generate multiple translation candidates with quality assessment
     * @param sourceText Text to translate
     * @param currentTranslation Current best translation
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @param maxCandidates Maximum number of candidates to generate
     * @return Vector of translation candidates ranked by quality
     */
    std::vector<TranslationCandidate> generateTranslationCandidates(
        const std::string& sourceText,
        const std::string& currentTranslation,
        const std::string& sourceLang,
        const std::string& targetLang,
        int maxCandidates = 3
    );
    
    /**
     * Check if translation meets quality threshold
     * @param metrics Quality metrics to check
     * @param requiredLevel Required quality level ("high", "medium", "low")
     * @return true if meets threshold
     */
    bool meetsQualityThreshold(const QualityMetrics& metrics, const std::string& requiredLevel) const;
    
    /**
     * Get fallback translation options for low-quality results
     * @param sourceText Original text
     * @param lowQualityTranslation Low-quality translation to improve
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return Vector of fallback translation options
     */
    std::vector<std::string> getFallbackTranslations(
        const std::string& sourceText,
        const std::string& lowQualityTranslation,
        const std::string& sourceLang,
        const std::string& targetLang
    );
    
    /**
     * Generate quality improvement suggestions
     * @param metrics Quality metrics with detected issues
     * @return Vector of improvement suggestions
     */
    std::vector<std::string> suggestImprovements(const QualityMetrics& metrics);
    
    /**
     * Set quality thresholds
     * @param high High quality threshold (0.0-1.0)
     * @param medium Medium quality threshold (0.0-1.0)
     * @param low Low quality threshold (0.0-1.0)
     */
    void setQualityThresholds(float high, float medium, float low);
    
    /**
     * Get current quality configuration
     * @return Quality configuration
     */
    const QualityConfig& getConfig() const;
    
    /**
     * Update quality configuration
     * @param config New configuration
     */
    void updateConfig(const QualityConfig& config);
    
    /**
     * Check if quality manager is ready
     * @return true if initialized and ready
     */
    bool isReady() const;

private:
    // Quality assessment methods
    float calculateFluencyScore(const std::string& text, const std::string& language);
    float calculateAdequacyScore(const std::string& source, const std::string& translation, 
                                const std::string& sourceLang, const std::string& targetLang);
    float calculateConsistencyScore(const std::string& text, const std::string& language);
    std::vector<float> calculateWordLevelConfidences(const std::string& text, 
                                                    const std::vector<float>& modelScores);
    
    // Quality issue detection
    std::vector<std::string> detectQualityIssues(
        const std::string& source,
        const std::string& translation,
        const std::string& sourceLang,
        const std::string& targetLang,
        const QualityMetrics& metrics
    );
    
    // Text analysis helpers
    std::vector<std::string> tokenizeText(const std::string& text) const;
    float calculateTextComplexity(const std::string& text, const std::string& language) const;
    float calculateSemanticSimilarity(const std::string& text1, const std::string& text2) const;
    bool detectRepeatedPhrases(const std::string& text) const;
    bool detectIncompleteTranslation(const std::string& source, const std::string& translation) const;
    bool detectLanguageMixing(const std::string& text, const std::string& expectedLang) const;
    
    // Alternative generation methods
    std::string generateParaphraseAlternative(const std::string& text, const std::string& language);
    std::string generateSimplifiedAlternative(const std::string& text, const std::string& language);
    std::string generateFormalAlternative(const std::string& text, const std::string& language);
    
    // Fallback strategies
    std::string generateWordByWordFallback(const std::string& sourceText, 
                                          const std::string& sourceLang, 
                                          const std::string& targetLang);
    std::string generateTemplateBasedFallback(const std::string& sourceText,
                                             const std::string& sourceLang,
                                             const std::string& targetLang);
    
    // Configuration and state
    QualityConfig config_;
    bool initialized_;
    std::string configPath_;
    
    // Language-specific quality models (placeholder for future ML models)
    std::unordered_map<std::string, void*> languageQualityModels_;
    
    // Statistics and caching
    struct QualityStats {
        size_t totalAssessments;
        size_t highQualityCount;
        size_t mediumQualityCount;
        size_t lowQualityCount;
        float averageConfidence;
        std::chrono::milliseconds totalAssessmentTime;
        
        QualityStats() : totalAssessments(0), highQualityCount(0), 
                        mediumQualityCount(0), lowQualityCount(0), 
                        averageConfidence(0.0f), totalAssessmentTime(0) {}
    };
    
    QualityStats statistics_;
    
    // Thread safety
    mutable std::mutex assessmentMutex_;
    
    // Helper methods for configuration
    bool loadConfiguration(const std::string& configPath);
    void initializeDefaultConfiguration();
    bool validateConfiguration() const;
    
    // Utility methods
    std::string determineQualityLevel(float confidence) const;
    void updateStatistics(const QualityMetrics& metrics, 
                         std::chrono::milliseconds assessmentTime);
};

} // namespace mt
} // namespace speechrnt