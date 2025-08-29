#include "mt/quality_manager.hpp"
#include "utils/json_utils.hpp"
#include "utils/logging.hpp"
#include <fstream>
#include <algorithm>
#include <numeric>
#include <regex>
#include <sstream>
#include <cmath>
#include <random>

namespace speechrnt {
namespace mt {

QualityManager::QualityManager() 
    : initialized_(false) {
    initializeDefaultConfiguration();
}

QualityManager::~QualityManager() {
    cleanup();
}

bool QualityManager::initialize(const std::string& configPath) {
    std::lock_guard<std::mutex> lock(assessmentMutex_);
    
    configPath_ = configPath;
    
    // Load configuration from file if it exists
    if (!loadConfiguration(configPath)) {
        utils::Logger::warn("Failed to load quality configuration from " + configPath + 
                           ", using defaults");
    }
    
    // Validate configuration
    if (!validateConfiguration()) {
        utils::Logger::error("Invalid quality configuration");
        return false;
    }
    
    // Initialize language-specific quality models (placeholder for future ML models)
    // For now, we'll use rule-based approaches
    
    initialized_ = true;
    utils::Logger::info("QualityManager initialized successfully");
    
    return true;
}

void QualityManager::cleanup() {
    std::lock_guard<std::mutex> lock(assessmentMutex_);
    
    // Clean up language quality models
    for (auto& [lang, model] : languageQualityModels_) {
        if (model) {
            // Placeholder for model cleanup
            model = nullptr;
        }
    }
    languageQualityModels_.clear();
    
    initialized_ = false;
    utils::Logger::info("QualityManager cleaned up");
}

QualityMetrics QualityManager::assessTranslationQuality(
    const std::string& sourceText,
    const std::string& translatedText,
    const std::string& sourceLang,
    const std::string& targetLang,
    const std::vector<float>& modelScores) {
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    QualityMetrics metrics;
    
    if (!initialized_) {
        utils::Logger::warn("QualityManager not initialized");
        return metrics;
    }
    
    if (sourceText.empty() || translatedText.empty()) {
        utils::Logger::warn("Empty source or translated text provided");
        return metrics;
    }
    
    try {
        // Calculate individual quality scores
        metrics.fluencyScore = calculateFluencyScore(translatedText, targetLang);
        metrics.adequacyScore = calculateAdequacyScore(sourceText, translatedText, 
                                                      sourceLang, targetLang);
        metrics.consistencyScore = calculateConsistencyScore(translatedText, targetLang);
        
        // Calculate word-level confidences if enabled
        if (config_.enableWordLevelScoring) {
            metrics.wordLevelConfidences = calculateWordLevelConfidences(translatedText, modelScores);
        }
        
        // Calculate overall confidence
        metrics.overallConfidence = calculateConfidenceScore(sourceText, translatedText, modelScores);
        
        // Determine quality level
        metrics.qualityLevel = determineQualityLevel(metrics.overallConfidence);
        
        // Detect quality issues if enabled
        if (config_.enableQualityIssueDetection) {
            metrics.qualityIssues = detectQualityIssues(sourceText, translatedText, 
                                                        sourceLang, targetLang, metrics);
        }
        
        // Update statistics
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        updateStatistics(metrics, duration);
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error in quality assessment: " + std::string(e.what()));
        metrics.qualityLevel = "low";
        metrics.qualityIssues.push_back("Assessment error: " + std::string(e.what()));
    }
    
    return metrics;
}

float QualityManager::calculateConfidenceScore(
    const std::string& sourceText,
    const std::string& translatedText,
    const std::vector<float>& modelScores) {
    
    if (sourceText.empty() || translatedText.empty()) {
        return 0.0f;
    }
    
    float confidence = 0.0f;
    int scoreCount = 0;
    
    // Use model scores if available
    if (!modelScores.empty()) {
        float modelConfidence = std::accumulate(modelScores.begin(), modelScores.end(), 0.0f) / 
                               modelScores.size();
        confidence += modelConfidence * 0.4f; // 40% weight for model scores
        scoreCount++;
    }
    
    // Length ratio score (penalize very short or very long translations)
    float lengthRatio = static_cast<float>(translatedText.length()) / sourceText.length();
    float lengthScore = 1.0f - std::abs(1.0f - lengthRatio) * 0.5f;
    lengthScore = std::max(0.0f, std::min(1.0f, lengthScore));
    confidence += lengthScore * 0.2f; // 20% weight for length ratio
    scoreCount++;
    
    // Character diversity score (avoid repetitive translations)
    std::unordered_map<char, int> charCounts;
    for (char c : translatedText) {
        if (std::isalnum(c)) {
            charCounts[std::tolower(c)]++;
        }
    }
    
    float diversity = 0.0f;
    if (!charCounts.empty()) {
        float entropy = 0.0f;
        int totalChars = translatedText.length();
        for (const auto& [ch, count] : charCounts) {
            float prob = static_cast<float>(count) / totalChars;
            if (prob > 0) {
                entropy -= prob * std::log2(prob);
            }
        }
        diversity = std::min(1.0f, entropy / 4.0f); // Normalize to 0-1
    }
    confidence += diversity * 0.2f; // 20% weight for diversity
    scoreCount++;
    
    // Basic completeness check
    float completeness = 1.0f;
    if (translatedText.length() < sourceText.length() * 0.3f) {
        completeness = 0.5f; // Penalize very short translations
    }
    confidence += completeness * 0.2f; // 20% weight for completeness
    scoreCount++;
    
    if (scoreCount > 0) {
        confidence = confidence; // Already weighted
    }
    
    return std::max(0.0f, std::min(1.0f, confidence));
}

std::vector<TranslationCandidate> QualityManager::generateTranslationCandidates(
    const std::string& sourceText,
    const std::string& currentTranslation,
    const std::string& sourceLang,
    const std::string& targetLang,
    int maxCandidates) {
    
    std::vector<TranslationCandidate> candidates;
    
    if (!initialized_ || !config_.enableAlternatives) {
        return candidates;
    }
    
    maxCandidates = std::min(maxCandidates, config_.maxAlternatives);
    
    try {
        // Add the current translation as the first candidate
        TranslationCandidate primary;
        primary.translatedText = currentTranslation;
        primary.qualityMetrics = assessTranslationQuality(sourceText, currentTranslation, 
                                                          sourceLang, targetLang);
        primary.modelScore = primary.qualityMetrics.overallConfidence;
        primary.rank = 1;
        candidates.push_back(primary);
        
        // Generate alternative candidates
        std::vector<std::string> alternatives;
        
        if (maxCandidates > 1) {
            // Generate paraphrase alternative
            std::string paraphrase = generateParaphraseAlternative(currentTranslation, targetLang);
            if (!paraphrase.empty() && paraphrase != currentTranslation) {
                alternatives.push_back(paraphrase);
            }
        }
        
        if (maxCandidates > 2) {
            // Generate simplified alternative
            std::string simplified = generateSimplifiedAlternative(currentTranslation, targetLang);
            if (!simplified.empty() && simplified != currentTranslation) {
                alternatives.push_back(simplified);
            }
        }
        
        if (maxCandidates > 3) {
            // Generate formal alternative
            std::string formal = generateFormalAlternative(currentTranslation, targetLang);
            if (!formal.empty() && formal != currentTranslation) {
                alternatives.push_back(formal);
            }
        }
        
        // Assess quality of alternatives and add to candidates
        for (size_t i = 0; i < alternatives.size() && candidates.size() < maxCandidates; ++i) {
            TranslationCandidate candidate;
            candidate.translatedText = alternatives[i];
            candidate.qualityMetrics = assessTranslationQuality(sourceText, alternatives[i], 
                                                               sourceLang, targetLang);
            candidate.modelScore = candidate.qualityMetrics.overallConfidence;
            candidate.rank = static_cast<int>(candidates.size() + 1);
            candidates.push_back(candidate);
        }
        
        // Sort candidates by quality score (descending)
        std::sort(candidates.begin(), candidates.end(), 
                 [](const TranslationCandidate& a, const TranslationCandidate& b) {
                     return a.qualityMetrics.overallConfidence > b.qualityMetrics.overallConfidence;
                 });
        
        // Update ranks after sorting
        for (size_t i = 0; i < candidates.size(); ++i) {
            candidates[i].rank = static_cast<int>(i + 1);
        }
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error generating translation candidates: " + std::string(e.what()));
    }
    
    return candidates;
}

bool QualityManager::meetsQualityThreshold(const QualityMetrics& metrics, 
                                          const std::string& requiredLevel) const {
    float threshold = 0.0f;
    
    if (requiredLevel == "high") {
        threshold = config_.highQualityThreshold;
    } else if (requiredLevel == "medium") {
        threshold = config_.mediumQualityThreshold;
    } else if (requiredLevel == "low") {
        threshold = config_.lowQualityThreshold;
    } else {
        utils::Logger::warn("Unknown quality level: " + requiredLevel);
        return false;
    }
    
    return metrics.overallConfidence >= threshold;
}

std::vector<std::string> QualityManager::getFallbackTranslations(
    const std::string& sourceText,
    const std::string& lowQualityTranslation,
    const std::string& sourceLang,
    const std::string& targetLang) {
    
    std::vector<std::string> fallbacks;
    
    if (!initialized_) {
        return fallbacks;
    }
    
    try {
        // Word-by-word fallback
        std::string wordByWord = generateWordByWordFallback(sourceText, sourceLang, targetLang);
        if (!wordByWord.empty()) {
            fallbacks.push_back(wordByWord);
        }
        
        // Template-based fallback
        std::string templateBased = generateTemplateBasedFallback(sourceText, sourceLang, targetLang);
        if (!templateBased.empty()) {
            fallbacks.push_back(templateBased);
        }
        
        // If original translation isn't completely broken, include a simplified version
        if (!lowQualityTranslation.empty() && lowQualityTranslation.length() > 3) {
            std::string simplified = generateSimplifiedAlternative(lowQualityTranslation, targetLang);
            if (!simplified.empty() && simplified != lowQualityTranslation) {
                fallbacks.push_back(simplified);
            }
        }
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error generating fallback translations: " + std::string(e.what()));
    }
    
    return fallbacks;
}

std::vector<std::string> QualityManager::suggestImprovements(const QualityMetrics& metrics) {
    std::vector<std::string> suggestions;
    
    if (metrics.fluencyScore < 0.6f) {
        suggestions.push_back("Consider using a more natural phrasing");
        suggestions.push_back("Check for grammatical correctness");
    }
    
    if (metrics.adequacyScore < 0.6f) {
        suggestions.push_back("Ensure all source meaning is preserved");
        suggestions.push_back("Check for missing or added information");
    }
    
    if (metrics.consistencyScore < 0.6f) {
        suggestions.push_back("Maintain consistent terminology");
        suggestions.push_back("Check for contradictory statements");
    }
    
    if (metrics.overallConfidence < 0.5f) {
        suggestions.push_back("Consider using a simpler sentence structure");
        suggestions.push_back("Break down complex sentences");
    }
    
    // Add specific suggestions based on detected issues
    for (const auto& issue : metrics.qualityIssues) {
        if (issue.find("repetition") != std::string::npos) {
            suggestions.push_back("Remove repetitive phrases");
        } else if (issue.find("incomplete") != std::string::npos) {
            suggestions.push_back("Complete the translation");
        } else if (issue.find("language mixing") != std::string::npos) {
            suggestions.push_back("Ensure consistent target language usage");
        }
    }
    
    return suggestions;
}

void QualityManager::setQualityThresholds(float high, float medium, float low) {
    std::lock_guard<std::mutex> lock(assessmentMutex_);
    
    config_.highQualityThreshold = std::max(0.0f, std::min(1.0f, high));
    config_.mediumQualityThreshold = std::max(0.0f, std::min(1.0f, medium));
    config_.lowQualityThreshold = std::max(0.0f, std::min(1.0f, low));
    
    utils::Logger::info("Quality thresholds updated: high=" + std::to_string(high) + 
                       ", medium=" + std::to_string(medium) + ", low=" + std::to_string(low));
}

const QualityConfig& QualityManager::getConfig() const {
    return config_;
}

void QualityManager::updateConfig(const QualityConfig& config) {
    std::lock_guard<std::mutex> lock(assessmentMutex_);
    config_ = config;
    utils::Logger::info("Quality configuration updated");
}

bool QualityManager::isReady() const {
    return initialized_;
}

// Private methods implementation

float QualityManager::calculateFluencyScore(const std::string& text, const std::string& language) {
    if (text.empty()) {
        return 0.0f;
    }
    
    float score = 1.0f;
    
    // Check for basic fluency indicators
    
    // 1. Sentence structure (basic check for complete sentences)
    std::regex sentencePattern(R"([.!?]+)");
    std::sregex_iterator iter(text.begin(), text.end(), sentencePattern);
    std::sregex_iterator end;
    int sentenceCount = std::distance(iter, end);
    
    if (sentenceCount == 0 && text.length() > 20) {
        score -= 0.2f; // Penalize lack of sentence endings in longer text
    }
    
    // 2. Repetition check
    if (detectRepeatedPhrases(text)) {
        score -= 0.3f;
    }
    
    // 3. Length reasonableness
    if (text.length() < 3) {
        score -= 0.4f;
    }
    
    // 4. Character diversity
    std::unordered_map<char, int> charCounts;
    for (char c : text) {
        if (std::isalpha(c)) {
            charCounts[std::tolower(c)]++;
        }
    }
    
    if (charCounts.size() < 3 && text.length() > 10) {
        score -= 0.2f; // Low character diversity
    }
    
    return std::max(0.0f, std::min(1.0f, score));
}

float QualityManager::calculateAdequacyScore(const std::string& source, const std::string& translation,
                                            const std::string& sourceLang, const std::string& targetLang) {
    if (source.empty() || translation.empty()) {
        return 0.0f;
    }
    
    float score = 1.0f;
    
    // 1. Length ratio check (very different lengths might indicate missing content)
    float lengthRatio = static_cast<float>(translation.length()) / source.length();
    if (lengthRatio < 0.3f || lengthRatio > 3.0f) {
        score -= 0.3f;
    }
    
    // 2. Check for incomplete translation
    if (detectIncompleteTranslation(source, translation)) {
        score -= 0.4f;
    }
    
    // 3. Basic semantic similarity (simplified approach)
    float similarity = calculateSemanticSimilarity(source, translation);
    score = score * 0.7f + similarity * 0.3f;
    
    return std::max(0.0f, std::min(1.0f, score));
}

float QualityManager::calculateConsistencyScore(const std::string& text, const std::string& language) {
    if (text.empty()) {
        return 0.0f;
    }
    
    float score = 1.0f;
    
    // 1. Language mixing check
    if (detectLanguageMixing(text, language)) {
        score -= 0.4f;
    }
    
    // 2. Repeated phrases check
    if (detectRepeatedPhrases(text)) {
        score -= 0.3f;
    }
    
    // 3. Basic consistency in style (simplified check)
    std::vector<std::string> tokens = tokenizeText(text);
    if (tokens.size() > 5) {
        // Check for consistent capitalization patterns
        int capitalizedCount = 0;
        for (const auto& token : tokens) {
            if (!token.empty() && std::isupper(token[0])) {
                capitalizedCount++;
            }
        }
        
        float capitalizationRatio = static_cast<float>(capitalizedCount) / tokens.size();
        if (capitalizationRatio > 0.8f || capitalizationRatio < 0.1f) {
            // Very high or very low capitalization might indicate inconsistency
            score -= 0.1f;
        }
    }
    
    return std::max(0.0f, std::min(1.0f, score));
}

std::vector<float> QualityManager::calculateWordLevelConfidences(const std::string& text,
                                                                const std::vector<float>& modelScores) {
    std::vector<std::string> tokens = tokenizeText(text);
    std::vector<float> confidences;
    
    if (tokens.empty()) {
        return confidences;
    }
    
    // If model scores are available and match token count, use them
    if (!modelScores.empty() && modelScores.size() == tokens.size()) {
        return modelScores;
    }
    
    // Otherwise, generate estimated confidences based on word characteristics
    for (const auto& token : tokens) {
        float confidence = 0.8f; // Base confidence
        
        // Adjust based on word length (very short or very long words might be less reliable)
        if (token.length() <= 2) {
            confidence -= 0.1f;
        } else if (token.length() > 15) {
            confidence -= 0.2f;
        }
        
        // Adjust based on character patterns
        bool hasNumbers = std::any_of(token.begin(), token.end(), ::isdigit);
        bool hasSpecialChars = std::any_of(token.begin(), token.end(), 
                                          [](char c) { return !std::isalnum(c) && c != ' '; });
        
        if (hasNumbers) confidence += 0.1f; // Numbers are usually translated accurately
        if (hasSpecialChars) confidence -= 0.1f; // Special characters might cause issues
        
        confidences.push_back(std::max(0.0f, std::min(1.0f, confidence)));
    }
    
    return confidences;
}

std::vector<std::string> QualityManager::detectQualityIssues(
    const std::string& source,
    const std::string& translation,
    const std::string& sourceLang,
    const std::string& targetLang,
    const QualityMetrics& metrics) {
    
    std::vector<std::string> issues;
    
    // Check for repetition
    if (detectRepeatedPhrases(translation)) {
        issues.push_back("Repetitive phrases detected");
    }
    
    // Check for incomplete translation
    if (detectIncompleteTranslation(source, translation)) {
        issues.push_back("Translation appears incomplete");
    }
    
    // Check for language mixing
    if (detectLanguageMixing(translation, targetLang)) {
        issues.push_back("Language mixing detected");
    }
    
    // Check for very low scores
    if (metrics.fluencyScore < 0.4f) {
        issues.push_back("Low fluency score");
    }
    
    if (metrics.adequacyScore < 0.4f) {
        issues.push_back("Low adequacy score");
    }
    
    if (metrics.consistencyScore < 0.4f) {
        issues.push_back("Low consistency score");
    }
    
    // Check for extreme length differences
    float lengthRatio = static_cast<float>(translation.length()) / source.length();
    if (lengthRatio < 0.2f) {
        issues.push_back("Translation significantly shorter than source");
    } else if (lengthRatio > 4.0f) {
        issues.push_back("Translation significantly longer than source");
    }
    
    return issues;
}

std::vector<std::string> QualityManager::tokenizeText(const std::string& text) const {
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string token;
    
    while (iss >> token) {
        // Remove punctuation from the end
        while (!token.empty() && std::ispunct(token.back())) {
            token.pop_back();
        }
        
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    
    return tokens;
}

float QualityManager::calculateTextComplexity(const std::string& text, const std::string& language) const {
    if (text.empty()) {
        return 0.0f;
    }
    
    std::vector<std::string> tokens = tokenizeText(text);
    if (tokens.empty()) {
        return 0.0f;
    }
    
    // Calculate average word length
    float avgWordLength = 0.0f;
    for (const auto& token : tokens) {
        avgWordLength += token.length();
    }
    avgWordLength /= tokens.size();
    
    // Normalize complexity (0.0 = simple, 1.0 = complex)
    float complexity = std::min(1.0f, avgWordLength / 10.0f);
    
    return complexity;
}

float QualityManager::calculateSemanticSimilarity(const std::string& text1, const std::string& text2) const {
    // Simplified semantic similarity based on common words
    std::vector<std::string> tokens1 = tokenizeText(text1);
    std::vector<std::string> tokens2 = tokenizeText(text2);
    
    if (tokens1.empty() || tokens2.empty()) {
        return 0.0f;
    }
    
    // Convert to lowercase for comparison
    std::unordered_map<std::string, int> wordCount1, wordCount2;
    
    for (auto token : tokens1) {
        std::transform(token.begin(), token.end(), token.begin(), ::tolower);
        wordCount1[token]++;
    }
    
    for (auto token : tokens2) {
        std::transform(token.begin(), token.end(), token.begin(), ::tolower);
        wordCount2[token]++;
    }
    
    // Calculate Jaccard similarity
    std::unordered_set<std::string> allWords;
    for (const auto& [word, count] : wordCount1) {
        allWords.insert(word);
    }
    for (const auto& [word, count] : wordCount2) {
        allWords.insert(word);
    }
    
    int intersection = 0;
    for (const auto& word : allWords) {
        if (wordCount1.count(word) && wordCount2.count(word)) {
            intersection++;
        }
    }
    
    float similarity = static_cast<float>(intersection) / allWords.size();
    return similarity;
}

bool QualityManager::detectRepeatedPhrases(const std::string& text) const {
    std::vector<std::string> tokens = tokenizeText(text);
    
    if (tokens.size() < 4) {
        return false;
    }
    
    // Check for repeated 2-word phrases
    std::unordered_map<std::string, int> phrases;
    for (size_t i = 0; i < tokens.size() - 1; ++i) {
        std::string phrase = tokens[i] + " " + tokens[i + 1];
        phrases[phrase]++;
        
        if (phrases[phrase] > 2) {
            return true;
        }
    }
    
    return false;
}

bool QualityManager::detectIncompleteTranslation(const std::string& source, const std::string& translation) const {
    // Very basic check for incomplete translation
    if (translation.length() < source.length() * 0.2f && source.length() > 20) {
        return true;
    }
    
    // Check if translation ends abruptly (no proper sentence ending)
    if (translation.length() > 10) {
        char lastChar = translation.back();
        if (lastChar != '.' && lastChar != '!' && lastChar != '?' && lastChar != ':') {
            // Check if it looks like it was cut off mid-word
            std::vector<std::string> tokens = tokenizeText(translation);
            if (!tokens.empty()) {
                const std::string& lastToken = tokens.back();
                if (lastToken.length() > 1 && std::islower(lastToken.back())) {
                    return true; // Likely cut off mid-word
                }
            }
        }
    }
    
    return false;
}

bool QualityManager::detectLanguageMixing(const std::string& text, const std::string& expectedLang) const {
    // Simplified language mixing detection
    // This is a placeholder - in a real implementation, you'd use proper language detection
    
    if (expectedLang == "en") {
        // Check for common non-English characters
        for (char c : text) {
            if (c < 0 || c > 127) { // Non-ASCII characters
                return true;
            }
        }
    }
    
    return false;
}

std::string QualityManager::generateParaphraseAlternative(const std::string& text, const std::string& language) {
    // Simplified paraphrasing - in a real implementation, this would use ML models
    std::string paraphrase = text;
    
    // Basic synonym replacements (placeholder)
    std::unordered_map<std::string, std::string> synonyms = {
        {"good", "excellent"},
        {"bad", "poor"},
        {"big", "large"},
        {"small", "tiny"},
        {"fast", "quick"},
        {"slow", "gradual"}
    };
    
    for (const auto& [original, replacement] : synonyms) {
        size_t pos = paraphrase.find(original);
        if (pos != std::string::npos) {
            paraphrase.replace(pos, original.length(), replacement);
            break; // Only replace one word to create a variation
        }
    }
    
    return paraphrase;
}

std::string QualityManager::generateSimplifiedAlternative(const std::string& text, const std::string& language) {
    // Simplified version generation
    std::string simplified = text;
    
    // Remove complex punctuation
    std::regex complexPunct(R"([;:—–])");
    simplified = std::regex_replace(simplified, complexPunct, ",");
    
    // Break long sentences (very basic approach)
    std::regex longSentence(R"(,\s+)");
    simplified = std::regex_replace(simplified, longSentence, ". ");
    
    return simplified;
}

std::string QualityManager::generateFormalAlternative(const std::string& text, const std::string& language) {
    // Generate more formal version
    std::string formal = text;
    
    // Basic formalization (placeholder)
    std::unordered_map<std::string, std::string> formalizations = {
        {"can't", "cannot"},
        {"won't", "will not"},
        {"don't", "do not"},
        {"isn't", "is not"},
        {"aren't", "are not"}
    };
    
    for (const auto& [informal, formal_form] : formalizations) {
        size_t pos = formal.find(informal);
        if (pos != std::string::npos) {
            formal.replace(pos, informal.length(), formal_form);
        }
    }
    
    return formal;
}

std::string QualityManager::generateWordByWordFallback(const std::string& sourceText,
                                                      const std::string& sourceLang,
                                                      const std::string& targetLang) {
    // Very basic word-by-word translation fallback
    // In a real implementation, this would use a dictionary or simple translation service
    
    std::vector<std::string> tokens = tokenizeText(sourceText);
    std::string fallback;
    
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) fallback += " ";
        
        // Placeholder: just add a prefix to indicate this is a fallback
        fallback += "[" + tokens[i] + "]";
    }
    
    return fallback;
}

std::string QualityManager::generateTemplateBasedFallback(const std::string& sourceText,
                                                         const std::string& sourceLang,
                                                         const std::string& targetLang) {
    // Template-based fallback for common phrases
    std::unordered_map<std::string, std::string> templates = {
        {"hello", "Hello"},
        {"goodbye", "Goodbye"},
        {"thank you", "Thank you"},
        {"please", "Please"},
        {"yes", "Yes"},
        {"no", "No"}
    };
    
    std::string lowerSource = sourceText;
    std::transform(lowerSource.begin(), lowerSource.end(), lowerSource.begin(), ::tolower);
    
    for (const auto& [pattern, translation] : templates) {
        if (lowerSource.find(pattern) != std::string::npos) {
            return translation;
        }
    }
    
    return ""; // No template match
}

bool QualityManager::loadConfiguration(const std::string& configPath) {
    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            return false;
        }
        
        // For now, use default configuration
        // In a real implementation, this would parse JSON configuration
        initializeDefaultConfiguration();
        
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("Error loading quality configuration: " + std::string(e.what()));
        return false;
    }
}

void QualityManager::initializeDefaultConfiguration() {
    config_ = QualityConfig(); // Use default values
}

bool QualityManager::validateConfiguration() const {
    if (config_.highQualityThreshold < config_.mediumQualityThreshold ||
        config_.mediumQualityThreshold < config_.lowQualityThreshold) {
        return false;
    }
    
    if (config_.maxAlternatives < 1 || config_.maxAlternatives > 10) {
        return false;
    }
    
    return true;
}

std::string QualityManager::determineQualityLevel(float confidence) const {
    if (confidence >= config_.highQualityThreshold) {
        return "high";
    } else if (confidence >= config_.mediumQualityThreshold) {
        return "medium";
    } else {
        return "low";
    }
}

void QualityManager::updateStatistics(const QualityMetrics& metrics,
                                     std::chrono::milliseconds assessmentTime) {
    statistics_.totalAssessments++;
    
    if (metrics.qualityLevel == "high") {
        statistics_.highQualityCount++;
    } else if (metrics.qualityLevel == "medium") {
        statistics_.mediumQualityCount++;
    } else {
        statistics_.lowQualityCount++;
    }
    
    // Update average confidence (running average)
    float totalConfidence = statistics_.averageConfidence * (statistics_.totalAssessments - 1) + 
                           metrics.overallConfidence;
    statistics_.averageConfidence = totalConfidence / statistics_.totalAssessments;
    
    statistics_.totalAssessmentTime += assessmentTime;
}

} // namespace mt
} // namespace speechrnt