#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>

namespace validation {

enum class TranslationErrorType {
    UNTRANSLATED_TEXT,
    OVER_TRANSLATION,
    UNDER_TRANSLATION,
    REPETITION,
    GRAMMAR_ERROR,
    FORMATTING_ERROR,
    SEMANTIC_ERROR,
    FLUENCY_ERROR
};

struct TranslationError {
    TranslationErrorType type;
    std::string description;
    double severity; // 0.0 to 1.0
};

struct TranslationQualityMetrics {
    double bleuScore = -1.0;           // BLEU score (0.0 to 1.0)
    double semanticSimilarity = -1.0;  // Semantic similarity (0.0 to 1.0)
    double fluencyScore = -1.0;        // Fluency score (0.0 to 1.0)
    double adequacyScore = -1.0;       // Adequacy score (0.0 to 1.0)
    double overallQuality = -1.0;      // Overall quality (0.0 to 1.0)
    
    std::vector<TranslationError> errorTypes;
    
    // Metadata
    size_t sourceLength = 0;
    size_t targetLength = 0;
    double lengthRatio = 0.0;
    std::chrono::system_clock::time_point evaluationTimestamp;
};

struct ValidationReport {
    size_t totalEvaluations = 0;
    double averageQuality = 0.0;
    double averageBLEU = -1.0;
    double averageSemanticSimilarity = -1.0;
    double averageFluency = -1.0;
    double averageAdequacy = -1.0;
    
    std::map<std::string, int> qualityDistribution;
    std::map<TranslationErrorType, std::pair<int, double>> errorAnalysis; // count, percentage
    
    std::vector<std::string> recommendations;
    std::chrono::system_clock::time_point timestamp;
};

// Forward declarations for metric calculators
class BLEUCalculator;
class SemanticSimilarityCalculator;
class FluencyEvaluator;

class TranslationQualityValidator {
public:
    TranslationQualityValidator();
    ~TranslationQualityValidator() = default;
    
    // Main evaluation function
    TranslationQualityMetrics evaluateTranslation(
        const std::string& sourceText,
        const std::string& translatedText,
        const std::string& sourceLang,
        const std::string& targetLang,
        const std::string& referenceTranslation = ""
    );
    
    // Batch evaluation
    std::vector<TranslationQualityMetrics> evaluateTranslations(
        const std::vector<std::tuple<std::string, std::string, std::string, std::string>>& translations
    );
    
    // Generate validation report
    ValidationReport generateValidationReport(
        const std::vector<TranslationQualityMetrics>& evaluations
    );
    
    // Configuration
    void setQualityThresholds(double bleu, double semantic, double fluency, double adequacy);
    void addReferenceTranslation(const std::string& source, const std::string& target, 
                                const std::string& sourceLang, const std::string& targetLang);
    
private:
    void initializeMetrics();
    void loadReferenceTranslations();
    
    // Individual metric calculations
    double calculateAdequacy(const std::string& sourceText, const std::string& translatedText,
                           const std::string& sourceLang, const std::string& targetLang);
    
    // Error detection
    std::vector<TranslationError> detectTranslationErrors(
        const std::string& sourceText, const std::string& translatedText,
        const std::string& sourceLang, const std::string& targetLang
    );
    
    // Helper functions
    std::vector<std::string> extractContentWords(const std::string& text, const std::string& language);
    bool isConceptPreserved(const std::string& sourceWord, const std::vector<std::string>& targetWords,
                           const std::string& sourceLang, const std::string& targetLang);
    double calculateStringSimilarity(const std::string& str1, const std::string& str2);
    
    // Error detection helpers
    bool containsUntranslatedText(const std::string& translatedText, 
                                 const std::string& sourceLang, const std::string& targetLang);
    bool containsRepetition(const std::string& text);
    std::vector<TranslationError> detectGrammarIssues(const std::string& text, const std::string& language);
    
    // Quality calculation
    double calculateOverallQuality(const TranslationQualityMetrics& metrics);
    
    // Member variables
    std::unique_ptr<BLEUCalculator> bleuCalculator_;
    std::unique_ptr<SemanticSimilarityCalculator> semanticCalculator_;
    std::unique_ptr<FluencyEvaluator> fluencyEvaluator_;
    
    // Reference translations for evaluation
    std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> referenceTranslations_;
    
    // Quality thresholds
    double bleuThreshold_ = 0.4;
    double semanticThreshold_ = 0.6;
    double fluencyThreshold_ = 0.7;
    double adequacyThreshold_ = 0.6;
};

// BLEU Score Calculator
class BLEUCalculator {
public:
    double calculateBLEU(const std::string& candidate, const std::string& reference, int maxN = 4);
    
private:
    std::vector<std::string> tokenize(const std::string& text);
    std::map<std::string, int> getNGrams(const std::vector<std::string>& tokens, int n);
    double calculatePrecision(const std::vector<std::string>& candidate, 
                             const std::vector<std::string>& reference, int n);
    double calculateBrevityPenalty(size_t candidateLength, size_t referenceLength);
};

// Semantic Similarity Calculator
class SemanticSimilarityCalculator {
public:
    double calculateSimilarity(const std::string& sourceText, const std::string& translatedText,
                              const std::string& sourceLang, const std::string& targetLang);
    
private:
    // Simplified semantic similarity - in practice would use embeddings
    double calculateLexicalSimilarity(const std::string& text1, const std::string& text2);
    double calculateStructuralSimilarity(const std::string& text1, const std::string& text2);
};

// Fluency Evaluator
class FluencyEvaluator {
public:
    double evaluateFluency(const std::string& text, const std::string& language);
    
private:
    double evaluateGrammar(const std::string& text, const std::string& language);
    double evaluateNaturalness(const std::string& text, const std::string& language);
    double evaluateCoherence(const std::string& text);
};

} // namespace validation