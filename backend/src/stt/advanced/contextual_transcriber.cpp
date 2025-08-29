#include "stt/advanced/contextual_transcriber.hpp"
#include "stt/advanced/vocabulary_manager.hpp"
#include "utils/logging.hpp"
#include "utils/json_utils.hpp"
#include <algorithm>
#include <regex>
#include <sstream>
#include <chrono>
#include <cmath>

namespace stt {
namespace advanced {

/**
 * Simple domain classifier implementation
 */
class SimpleDomainClassifier : public DomainClassifier {
private:
    bool initialized_;
    std::map<std::string, std::vector<std::string>> domainKeywords_;
    std::map<std::string, std::vector<std::string>> domainTrainingTexts_;
    
public:
    SimpleDomainClassifier() : initialized_(false) {}
    
    bool initialize(const std::string& modelPath) override {
        // Initialize with default domain keywords
        domainKeywords_["medical"] = {
            "patient", "doctor", "hospital", "diagnosis", "treatment", "medication",
            "symptoms", "disease", "therapy", "clinical", "medical", "health",
            "surgery", "prescription", "examination", "blood", "heart", "lung"
        };
        
        domainKeywords_["technical"] = {
            "software", "hardware", "computer", "system", "network", "database",
            "algorithm", "programming", "code", "server", "application", "technology",
            "development", "engineering", "technical", "digital", "interface", "protocol"
        };
        
        domainKeywords_["legal"] = {
            "court", "law", "legal", "attorney", "judge", "case", "contract",
            "agreement", "lawsuit", "evidence", "testimony", "defendant", "plaintiff",
            "jurisdiction", "statute", "regulation", "compliance", "litigation"
        };
        
        domainKeywords_["business"] = {
            "company", "business", "market", "sales", "revenue", "profit", "customer",
            "client", "meeting", "project", "management", "strategy", "finance",
            "budget", "investment", "corporate", "enterprise", "commercial"
        };
        
        domainKeywords_["general"] = {
            "the", "and", "or", "but", "with", "from", "they", "have", "this",
            "that", "will", "would", "could", "should", "about", "after", "before"
        };
        
        initialized_ = true;
        return true;
    }
    
    std::map<std::string, float> classifyDomain(const std::string& text) override {
        if (!initialized_) {
            return {};
        }
        
        std::map<std::string, float> domainScores;
        std::string lowerText = text;
        std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
        
        // Count keyword matches for each domain
        for (const auto& [domain, keywords] : domainKeywords_) {
            int matches = 0;
            for (const auto& keyword : keywords) {
                if (lowerText.find(keyword) != std::string::npos) {
                    matches++;
                }
            }
            
            // Calculate score as percentage of keywords found
            float score = static_cast<float>(matches) / keywords.size();
            domainScores[domain] = score;
        }
        
        // Normalize scores
        float totalScore = 0.0f;
        for (const auto& [domain, score] : domainScores) {
            totalScore += score;
        }
        
        if (totalScore > 0.0f) {
            for (auto& [domain, score] : domainScores) {
                score /= totalScore;
            }
        }
        
        return domainScores;
    }
    
    std::string getMostLikelyDomain(const std::string& text) override {
        auto scores = classifyDomain(text);
        if (scores.empty()) {
            return "general";
        }
        
        auto maxElement = std::max_element(scores.begin(), scores.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        
        return maxElement->first;
    }
    
    bool addCustomDomain(const std::string& domainName,
                        const std::vector<std::string>& trainingTexts) override {
        if (!initialized_) {
            return false;
        }
        
        domainTrainingTexts_[domainName] = trainingTexts;
        
        // Extract keywords from training texts (simple approach)
        std::map<std::string, int> wordCounts;
        for (const auto& text : trainingTexts) {
            std::istringstream iss(text);
            std::string word;
            while (iss >> word) {
                // Simple word cleaning
                std::transform(word.begin(), word.end(), word.begin(), ::tolower);
                word.erase(std::remove_if(word.begin(), word.end(), 
                    [](char c) { return !std::isalnum(c); }), word.end());
                
                if (word.length() > 3) { // Only consider words longer than 3 characters
                    wordCounts[word]++;
                }
            }
        }
        
        // Select top keywords
        std::vector<std::pair<std::string, int>> sortedWords(wordCounts.begin(), wordCounts.end());
        std::sort(sortedWords.begin(), sortedWords.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        
        std::vector<std::string> keywords;
        size_t maxKeywords = std::min(static_cast<size_t>(20), sortedWords.size());
        for (size_t i = 0; i < maxKeywords; ++i) {
            keywords.push_back(sortedWords[i].first);
        }
        
        domainKeywords_[domainName] = keywords;
        return true;
    }
    
    std::vector<std::string> getSupportedDomains() const override {
        std::vector<std::string> domains;
        for (const auto& [domain, _] : domainKeywords_) {
            domains.push_back(domain);
        }
        return domains;
    }
    
    bool isInitialized() const override {
        return initialized_;
    }
};

/**
 * Simple contextual language model implementation
 */
class SimpleContextualLanguageModel : public ContextualLanguageModel {
private:
    bool initialized_;
    std::map<std::string, std::map<std::string, float>> ngramProbabilities_;
    
public:
    SimpleContextualLanguageModel() : initialized_(false) {}
    
    bool initialize(const std::string& modelPath) override {
        // Initialize with basic n-gram probabilities
        // This is a simplified implementation
        initialized_ = true;
        return true;
    }
    
    float scoreTextWithContext(const std::string& text,
                              const ConversationContext& context) override {
        if (!initialized_) {
            return 0.0f;
        }
        
        // Simple scoring based on context similarity
        float score = 0.5f; // Base score
        
        // Boost score if text contains words from previous utterances
        for (const auto& prevUtterance : context.previousUtterances) {
            std::istringstream prevStream(prevUtterance);
            std::istringstream textStream(text);
            std::string prevWord, textWord;
            
            std::set<std::string> prevWords, textWords;
            while (prevStream >> prevWord) {
                std::transform(prevWord.begin(), prevWord.end(), prevWord.begin(), ::tolower);
                prevWords.insert(prevWord);
            }
            while (textStream >> textWord) {
                std::transform(textWord.begin(), textWord.end(), textWord.begin(), ::tolower);
                textWords.insert(textWord);
            }
            
            // Calculate word overlap
            std::vector<std::string> intersection;
            std::set_intersection(prevWords.begin(), prevWords.end(),
                                textWords.begin(), textWords.end(),
                                std::back_inserter(intersection));
            
            if (!prevWords.empty()) {
                float overlap = static_cast<float>(intersection.size()) / prevWords.size();
                score += overlap * 0.2f; // Boost by up to 20%
            }
        }
        
        // Apply contextual weights
        for (const auto& [word, weight] : context.contextualWeights) {
            if (text.find(word) != std::string::npos) {
                score += weight * 0.1f;
            }
        }
        
        return std::min(1.0f, score);
    }
    
    std::vector<std::pair<std::string, float>> generateAlternatives(
        const std::string& baseText,
        const ConversationContext& context,
        size_t maxAlternatives = 5) override {
        
        std::vector<std::pair<std::string, float>> alternatives;
        
        // Simple alternative generation (placeholder implementation)
        alternatives.push_back({baseText, scoreTextWithContext(baseText, context)});
        
        // Generate some basic alternatives by word substitution
        std::istringstream iss(baseText);
        std::vector<std::string> words;
        std::string word;
        while (iss >> word) {
            words.push_back(word);
        }
        
        // Try some common substitutions
        std::map<std::string, std::vector<std::string>> substitutions = {
            {"to", {"too", "two"}},
            {"there", {"their", "they're"}},
            {"your", {"you're"}},
            {"its", {"it's"}},
            {"then", {"than"}}
        };
        
        for (size_t i = 0; i < words.size() && alternatives.size() < maxAlternatives; ++i) {
            std::string lowerWord = words[i];
            std::transform(lowerWord.begin(), lowerWord.end(), lowerWord.begin(), ::tolower);
            
            if (substitutions.find(lowerWord) != substitutions.end()) {
                for (const auto& substitute : substitutions[lowerWord]) {
                    std::vector<std::string> altWords = words;
                    altWords[i] = substitute;
                    
                    std::ostringstream altStream;
                    for (size_t j = 0; j < altWords.size(); ++j) {
                        if (j > 0) altStream << " ";
                        altStream << altWords[j];
                    }
                    
                    std::string altText = altStream.str();
                    float altScore = scoreTextWithContext(altText, context);
                    alternatives.push_back({altText, altScore});
                    
                    if (alternatives.size() >= maxAlternatives) break;
                }
            }
        }
        
        // Sort by score
        std::sort(alternatives.begin(), alternatives.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        
        return alternatives;
    }
    
    std::vector<std::pair<std::string, float>> predictNextWords(
        const std::string& partialText,
        const ConversationContext& context,
        size_t maxPredictions = 10) override {
        
        std::vector<std::pair<std::string, float>> predictions;
        
        // Simple prediction based on common word patterns
        std::vector<std::string> commonWords = {
            "the", "and", "or", "but", "with", "from", "they", "have",
            "this", "that", "will", "would", "could", "should"
        };
        
        for (const auto& word : commonWords) {
            if (predictions.size() >= maxPredictions) break;
            predictions.push_back({word, 0.1f});
        }
        
        return predictions;
    }
    
    void updateWithConversation(const std::vector<std::string>& utterances,
                               const std::string& domain) override {
        // Update model with conversation data (placeholder implementation)
        // In a real implementation, this would update n-gram probabilities
    }
    
    bool isInitialized() const override {
        return initialized_;
    }
};

/**
 * Simple vocabulary matcher implementation
 */
class SimpleVocabularyMatcher : public VocabularyMatcher {
private:
    bool initialized_;
    std::map<std::string, ContextualVocabulary> domainVocabularies_;
    
    // Helper function to calculate string similarity
    float calculateSimilarity(const std::string& str1, const std::string& str2) {
        if (str1 == str2) return 1.0f;
        
        // Simple Levenshtein distance-based similarity
        size_t len1 = str1.length();
        size_t len2 = str2.length();
        
        if (len1 == 0) return len2 == 0 ? 1.0f : 0.0f;
        if (len2 == 0) return 0.0f;
        
        std::vector<std::vector<size_t>> dp(len1 + 1, std::vector<size_t>(len2 + 1));
        
        for (size_t i = 0; i <= len1; ++i) dp[i][0] = i;
        for (size_t j = 0; j <= len2; ++j) dp[0][j] = j;
        
        for (size_t i = 1; i <= len1; ++i) {
            for (size_t j = 1; j <= len2; ++j) {
                size_t cost = (str1[i-1] == str2[j-1]) ? 0 : 1;
                dp[i][j] = std::min({
                    dp[i-1][j] + 1,      // deletion
                    dp[i][j-1] + 1,      // insertion
                    dp[i-1][j-1] + cost  // substitution
                });
            }
        }
        
        size_t maxLen = std::max(len1, len2);
        return 1.0f - static_cast<float>(dp[len1][len2]) / maxLen;
    }
    
public:
    SimpleVocabularyMatcher() : initialized_(false) {}
    
    bool initialize() override {
        initialized_ = true;
        return true;
    }
    
    bool addDomainVocabulary(const std::string& domain,
                            const ContextualVocabulary& vocabulary) override {
        if (!initialized_) {
            return false;
        }
        
        domainVocabularies_[domain] = vocabulary;
        return true;
    }
    
    std::vector<ContextualCorrection> matchAndCorrect(
        const std::string& text,
        const std::string& domain,
        const ConversationContext& context) override {
        
        std::vector<ContextualCorrection> corrections;
        
        if (!initialized_ || domainVocabularies_.find(domain) == domainVocabularies_.end()) {
            return corrections;
        }
        
        const auto& vocabulary = domainVocabularies_[domain];
        
        // Tokenize text
        std::istringstream iss(text);
        std::vector<std::string> words;
        std::string word;
        size_t position = 0;
        
        while (iss >> word) {
            size_t wordStart = text.find(word, position);
            size_t wordEnd = wordStart + word.length();
            
            // Check against domain terms
            for (const auto& domainTerm : vocabulary.domainTerms) {
                float similarity = calculateSimilarity(word, domainTerm);
                if (similarity > 0.7f && similarity < 1.0f) { // Similar but not exact
                    ContextualCorrection correction;
                    correction.originalText = word;
                    correction.correctedText = domainTerm;
                    correction.correctionType = "domain_term";
                    correction.confidence = similarity;
                    correction.startPosition = wordStart;
                    correction.endPosition = wordEnd;
                    correction.reasoning = "Matched domain-specific term: " + domainTerm;
                    corrections.push_back(correction);
                }
            }
            
            // Check against proper nouns
            for (const auto& properNoun : vocabulary.properNouns) {
                float similarity = calculateSimilarity(word, properNoun);
                if (similarity > 0.8f && similarity < 1.0f) { // Higher threshold for proper nouns
                    ContextualCorrection correction;
                    correction.originalText = word;
                    correction.correctedText = properNoun;
                    correction.correctionType = "proper_noun";
                    correction.confidence = similarity;
                    correction.startPosition = wordStart;
                    correction.endPosition = wordEnd;
                    correction.reasoning = "Matched proper noun: " + properNoun;
                    corrections.push_back(correction);
                }
            }
            
            // Check against technical terms
            for (const auto& techTerm : vocabulary.technicalTerms) {
                float similarity = calculateSimilarity(word, techTerm);
                if (similarity > 0.75f && similarity < 1.0f) {
                    ContextualCorrection correction;
                    correction.originalText = word;
                    correction.correctedText = techTerm;
                    correction.correctionType = "technical_term";
                    correction.confidence = similarity;
                    correction.startPosition = wordStart;
                    correction.endPosition = wordEnd;
                    correction.reasoning = "Matched technical term: " + techTerm;
                    corrections.push_back(correction);
                }
            }
            
            position = wordEnd;
        }
        
        // Sort corrections by confidence
        std::sort(corrections.begin(), corrections.end(),
            [](const auto& a, const auto& b) { return a.confidence > b.confidence; });
        
        return corrections;
    }
    
    std::vector<std::pair<std::string, float>> findBestMatches(
        const std::string& term,
        const std::string& domain,
        size_t maxMatches = 5) override {
        
        std::vector<std::pair<std::string, float>> matches;
        
        if (!initialized_ || domainVocabularies_.find(domain) == domainVocabularies_.end()) {
            return matches;
        }
        
        const auto& vocabulary = domainVocabularies_[domain];
        
        // Check all vocabulary terms
        std::vector<std::string> allTerms;
        allTerms.insert(allTerms.end(), vocabulary.domainTerms.begin(), vocabulary.domainTerms.end());
        allTerms.insert(allTerms.end(), vocabulary.properNouns.begin(), vocabulary.properNouns.end());
        allTerms.insert(allTerms.end(), vocabulary.technicalTerms.begin(), vocabulary.technicalTerms.end());
        
        for (const auto& vocabTerm : allTerms) {
            float similarity = calculateSimilarity(term, vocabTerm);
            if (similarity > 0.5f) { // Minimum similarity threshold
                matches.push_back({vocabTerm, similarity});
            }
        }
        
        // Sort by similarity and limit results
        std::sort(matches.begin(), matches.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        
        if (matches.size() > maxMatches) {
            matches.resize(maxMatches);
        }
        
        return matches;
    }
    
    void learnFromCorrections(const std::vector<ContextualCorrection>& corrections,
                             const std::string& domain) override {
        if (!initialized_) {
            return;
        }
        
        // Add corrected terms to vocabulary
        if (domainVocabularies_.find(domain) == domainVocabularies_.end()) {
            domainVocabularies_[domain] = ContextualVocabulary(domain);
        }
        
        auto& vocabulary = domainVocabularies_[domain];
        
        for (const auto& correction : corrections) {
            // Add corrected term to appropriate category
            if (correction.correctionType == "domain_term") {
                if (std::find(vocabulary.domainTerms.begin(), vocabulary.domainTerms.end(),
                             correction.correctedText) == vocabulary.domainTerms.end()) {
                    vocabulary.domainTerms.push_back(correction.correctedText);
                }
            } else if (correction.correctionType == "proper_noun") {
                if (std::find(vocabulary.properNouns.begin(), vocabulary.properNouns.end(),
                             correction.correctedText) == vocabulary.properNouns.end()) {
                    vocabulary.properNouns.push_back(correction.correctedText);
                }
            } else if (correction.correctionType == "technical_term") {
                if (std::find(vocabulary.technicalTerms.begin(), vocabulary.technicalTerms.end(),
                             correction.correctedText) == vocabulary.technicalTerms.end()) {
                    vocabulary.technicalTerms.push_back(correction.correctedText);
                }
            }
            
            // Update term probabilities
            vocabulary.termProbabilities[correction.correctedText] = correction.confidence;
        }
    }
    
    ContextualVocabulary getDomainVocabulary(const std::string& domain) const override {
        auto it = domainVocabularies_.find(domain);
        if (it != domainVocabularies_.end()) {
            return it->second;
        }
        return ContextualVocabulary();
    }
    
    bool removeDomainVocabulary(const std::string& domain) override {
        return domainVocabularies_.erase(domain) > 0;
    }
    
    std::vector<std::string> getSupportedDomains() const override {
        std::vector<std::string> domains;
        for (const auto& [domain, _] : domainVocabularies_) {
            domains.push_back(domain);
        }
        return domains;
    }
    
    bool isInitialized() const override {
        return initialized_;
    }
};

/**
 * Main contextual transcriber implementation
 */
class ContextualTranscriber : public ContextualTranscriberInterface {
private:
    bool initialized_;
    std::string lastError_;
    ContextualTranscriptionConfig config_;
    
    // Component instances
    std::unique_ptr<DomainClassifier> domainClassifier_;
    std::unique_ptr<ContextualLanguageModel> contextualLM_;
    std::unique_ptr<VocabularyMatcher> vocabularyMatcher_;
    std::unique_ptr<VocabularyManagerInterface> vocabularyManager_;
    
    // Context management
    std::map<uint32_t, ConversationContext> conversationContexts_;
    std::map<std::string, ContextualVocabulary> domainVocabularies_;
    
    // Configuration
    float contextualWeight_;
    bool domainDetectionEnabled_;
    size_t maxContextHistory_;
    
    // Statistics
    mutable std::mutex statsMutex_;
    size_t totalTranscriptions_;
    size_t enhancedTranscriptions_;
    size_t totalCorrections_;
    std::chrono::steady_clock::time_point startTime_;
    
public:
    ContextualTranscriber() 
        : initialized_(false)
        , contextualWeight_(0.3f)
        , domainDetectionEnabled_(true)
        , maxContextHistory_(10)
        , totalTranscriptions_(0)
        , enhancedTranscriptions_(0)
        , totalCorrections_(0)
        , startTime_(std::chrono::steady_clock::now()) {
        
        domainClassifier_ = std::make_unique<SimpleDomainClassifier>();
        contextualLM_ = std::make_unique<SimpleContextualLanguageModel>();
        vocabularyMatcher_ = std::make_unique<SimpleVocabularyMatcher>();
        vocabularyManager_ = createVocabularyManager();
    }
    
    bool initialize(const std::string& modelsPath) override {
        try {
            // Initialize components
            if (!domainClassifier_->initialize(modelsPath + "/domain_classifier")) {
                lastError_ = "Failed to initialize domain classifier";
                return false;
            }
            
            if (!contextualLM_->initialize(modelsPath + "/contextual_lm")) {
                lastError_ = "Failed to initialize contextual language model";
                return false;
            }
            
            if (!vocabularyMatcher_->initialize()) {
                lastError_ = "Failed to initialize vocabulary matcher";
                return false;
            }
            
            // Initialize vocabulary manager
            VocabularyLearningConfig vocabConfig;
            vocabConfig.enableAutomaticLearning = true;
            vocabConfig.minimumConfidenceThreshold = 0.7f;
            vocabConfig.enableConflictResolution = true;
            
            if (!vocabularyManager_->initialize(vocabConfig)) {
                lastError_ = "Failed to initialize vocabulary manager";
                return false;
            }
            
            // Load default vocabularies
            loadDefaultVocabularies();
            
            initialized_ = true;
            lastError_.clear();
            return true;
            
        } catch (const std::exception& e) {
            lastError_ = "Exception during initialization: " + std::string(e.what());
            return false;
        }
    }
    
    ContextualResult enhanceTranscription(const TranscriptionResult& baseResult,
                                         const ConversationContext& context) override {
        std::lock_guard<std::mutex> lock(statsMutex_);
        totalTranscriptions_++;
        
        ContextualResult result;
        result.enhancedText = baseResult.text;
        result.contextualConfidence = baseResult.confidence;
        result.contextUsed = false;
        
        if (!initialized_) {
            lastError_ = "Contextual transcriber not initialized";
            return result;
        }
        
        try {
            // Detect domain if enabled
            if (domainDetectionEnabled_) {
                auto domainScores = domainClassifier_->classifyDomain(baseResult.text);
                result.domainProbabilities = domainScores;
                
                if (!domainScores.empty()) {
                    auto maxDomain = std::max_element(domainScores.begin(), domainScores.end(),
                        [](const auto& a, const auto& b) { return a.second < b.second; });
                    result.detectedDomain = maxDomain->first;
                }
            }
            
            // Use provided domain or detected domain
            std::string targetDomain = context.domain.empty() ? result.detectedDomain : context.domain;
            
            if (!targetDomain.empty()) {
                // Apply vocabulary corrections
                auto corrections = vocabularyMatcher_->matchAndCorrect(
                    baseResult.text, targetDomain, context);
                
                if (!corrections.empty()) {
                    result.corrections = corrections;
                    result.enhancedText = applyCorrections(baseResult.text, corrections);
                    result.contextUsed = true;
                    enhancedTranscriptions_++;
                    totalCorrections_ += corrections.size();
                    
                    // Learn from corrections using vocabulary manager
                    if (vocabularyManager_) {
                        vocabularyManager_->learnFromCorrections(corrections, targetDomain);
                    }
                }
                
                // Generate alternatives using contextual language model
                auto alternatives = contextualLM_->generateAlternatives(
                    result.enhancedText, context, 3);
                
                for (const auto& [altText, score] : alternatives) {
                    if (altText != result.enhancedText) {
                        result.alternativeTranscriptions.push_back(altText);
                    }
                }
                
                // Update contextual confidence
                if (result.contextUsed) {
                    float contextScore = contextualLM_->scoreTextWithContext(result.enhancedText, context);
                    result.contextualConfidence = (baseResult.confidence + contextScore * contextualWeight_) / 
                                                 (1.0f + contextualWeight_);
                }
            }
            
            result.processingInfo = "Domain: " + targetDomain + 
                                  ", Corrections: " + std::to_string(result.corrections.size()) +
                                  ", Alternatives: " + std::to_string(result.alternativeTranscriptions.size());
            
        } catch (const std::exception& e) {
            lastError_ = "Exception during transcription enhancement: " + std::string(e.what());
            result.processingInfo = "Error: " + lastError_;
        }
        
        return result;
    }
    
    bool addDomainVocabulary(const std::string& domain,
                            const ContextualVocabulary& vocabulary) override {
        if (!initialized_) {
            lastError_ = "Contextual transcriber not initialized";
            return false;
        }
        
        domainVocabularies_[domain] = vocabulary;
        return vocabularyMatcher_->addDomainVocabulary(domain, vocabulary);
    }
    
    void updateConversationContext(uint32_t utteranceId,
                                  const std::string& utterance,
                                  const std::string& speakerInfo) override {
        if (!initialized_) {
            return;
        }
        
        auto& context = conversationContexts_[utteranceId];
        context.utteranceId = utteranceId;
        context.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        context.speakerInfo = speakerInfo;
        
        // Add to previous utterances
        context.previousUtterances.push_back(utterance);
        
        // Maintain history limit
        if (context.previousUtterances.size() > maxContextHistory_) {
            context.previousUtterances.erase(context.previousUtterances.begin());
        }
        
        // Update topic detection (simple keyword-based approach)
        updateTopicDetection(context, utterance);
        
        // Update contextual weights based on conversation history
        updateContextualWeights(context);
    }
    
    ConversationContext getConversationContext(uint32_t utteranceId) const override {
        auto it = conversationContexts_.find(utteranceId);
        if (it != conversationContexts_.end()) {
            return it->second;
        }
        return ConversationContext();
    }
    
    void clearConversationContext(uint32_t utteranceId) override {
        if (utteranceId == 0) {
            conversationContexts_.clear();
        } else {
            conversationContexts_.erase(utteranceId);
        }
    }
    
    std::string detectDomain(const std::string& text) override {
        if (!initialized_ || !domainClassifier_) {
            return "general";
        }
        
        return domainClassifier_->getMostLikelyDomain(text);
    }
    
    void setDomainHint(uint32_t utteranceId, const std::string& domain) override {
        if (!initialized_) {
            return;
        }
        
        auto& context = conversationContexts_[utteranceId];
        context.domain = domain;
    }
    
    void setContextualWeight(float weight) override {
        contextualWeight_ = std::max(0.0f, std::min(1.0f, weight));
    }
    
    void setDomainDetectionEnabled(bool enabled) override {
        domainDetectionEnabled_ = enabled;
    }
    
    void setMaxContextHistory(size_t maxHistory) override {
        maxContextHistory_ = maxHistory;
        
        // Trim existing contexts if necessary
        for (auto& [id, context] : conversationContexts_) {
            if (context.previousUtterances.size() > maxHistory) {
                context.previousUtterances.resize(maxHistory);
            }
        }
    }
    
    bool addCustomVocabulary(const std::vector<std::string>& terms,
                            const std::string& domain) override {
        if (!initialized_) {
            lastError_ = "Contextual transcriber not initialized";
            return false;
        }
        
        // Add to traditional vocabulary system
        ContextualVocabulary vocabulary(domain);
        vocabulary.domainTerms = terms;
        bool success = addDomainVocabulary(domain, vocabulary);
        
        // Add to advanced vocabulary manager
        if (vocabularyManager_) {
            std::vector<VocabularyEntry> entries;
            for (const auto& term : terms) {
                VocabularyEntry entry;
                entry.term = term;
                entry.category = "domain_term";
                entry.domain = domain;
                entry.probability = 0.8f;
                entry.confidence = 0.9f;
                entry.source = VocabularySource::MANUAL_ADDITION;
                entry.description = "Custom vocabulary term";
                entries.push_back(entry);
            }
            
            size_t addedCount = vocabularyManager_->addVocabularyEntries(entries, true);
            success = success && (addedCount > 0);
        }
        
        return success;
    }
    
    bool removeCustomVocabulary(const std::string& domain) override {
        if (!initialized_) {
            return false;
        }
        
        bool success = true;
        
        if (domain.empty()) {
            domainVocabularies_.clear();
            if (vocabularyManager_) {
                vocabularyManager_->clearVocabulary();
            }
        } else {
            domainVocabularies_.erase(domain);
            success = vocabularyMatcher_->removeDomainVocabulary(domain);
            if (vocabularyManager_) {
                vocabularyManager_->removeDomain(domain);
            }
        }
        
        return success;
    }
    
    std::vector<std::string> getAvailableDomains() const override {
        if (!initialized_) {
            return {};
        }
        
        return vocabularyMatcher_->getSupportedDomains();
    }
    
    bool updateConfiguration(const ContextualTranscriptionConfig& config) override {
        config_ = config;
        contextualWeight_ = config.contextualWeight;
        domainDetectionEnabled_ = config.enableDomainDetection;
        maxContextHistory_ = config.maxContextHistory;
        return true;
    }
    
    ContextualTranscriptionConfig getCurrentConfiguration() const override {
        return config_;
    }
    
    bool isInitialized() const override {
        return initialized_;
    }
    
    std::string getLastError() const override {
        return lastError_;
    }
    
    std::string getProcessingStats() const override {
        std::lock_guard<std::mutex> lock(statsMutex_);
        
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
        
        std::ostringstream stats;
        stats << "{"
              << "\"totalTranscriptions\":" << totalTranscriptions_ << ","
              << "\"enhancedTranscriptions\":" << enhancedTranscriptions_ << ","
              << "\"totalCorrections\":" << totalCorrections_ << ","
              << "\"enhancementRate\":" << (totalTranscriptions_ > 0 ? 
                  static_cast<float>(enhancedTranscriptions_) / totalTranscriptions_ : 0.0f) << ","
              << "\"averageCorrectionsPerTranscription\":" << (enhancedTranscriptions_ > 0 ?
                  static_cast<float>(totalCorrections_) / enhancedTranscriptions_ : 0.0f) << ","
              << "\"uptimeSeconds\":" << uptime << ","
              << "\"activeContexts\":" << conversationContexts_.size() << ","
              << "\"availableDomains\":" << domainVocabularies_.size();
        
        // Add vocabulary manager statistics
        if (vocabularyManager_) {
            auto vocabStats = vocabularyManager_->getVocabularyStatistics();
            stats << ",\"vocabularyStats\":{"
                  << "\"totalEntries\":" << vocabStats.totalEntries << ","
                  << "\"averageConfidence\":" << vocabStats.averageConfidence << ","
                  << "\"totalUsageCount\":" << vocabStats.totalUsageCount
                  << "}";
        }
        
        stats << "}";
        return stats.str();
    }
    
    void reset() override {
        conversationContexts_.clear();
        totalTranscriptions_ = 0;
        enhancedTranscriptions_ = 0;
        totalCorrections_ = 0;
        startTime_ = std::chrono::steady_clock::now();
        lastError_.clear();
        
        if (vocabularyManager_) {
            vocabularyManager_->reset();
        }
    }
    
    // Additional vocabulary management methods
    
    /**
     * Export vocabulary data
     * @param domain Domain to export (empty for all)
     * @param format Export format ("json", "csv", "xml")
     * @return Exported vocabulary data
     */
    std::string exportVocabulary(const std::string& domain = "", const std::string& format = "json") const {
        if (!vocabularyManager_) {
            return "";
        }
        return vocabularyManager_->exportVocabulary(domain, format);
    }
    
    /**
     * Import vocabulary data
     * @param data Vocabulary data to import
     * @param format Import format ("json", "csv", "xml")
     * @return Number of entries imported
     */
    size_t importVocabulary(const std::string& data, const std::string& format = "json") {
        if (!vocabularyManager_) {
            return 0;
        }
        return vocabularyManager_->importVocabulary(data, format);
    }
    
    /**
     * Learn vocabulary from training text
     * @param text Training text
     * @param domain Domain for the text
     * @param extractionMethod Extraction method ("keyword", "named_entity", "technical_terms")
     * @return Number of terms learned
     */
    size_t learnFromTrainingText(const std::string& text, const std::string& domain, 
                                const std::string& extractionMethod = "keyword") {
        if (!vocabularyManager_) {
            return 0;
        }
        return vocabularyManager_->learnFromText(text, domain, extractionMethod);
    }
    
    /**
     * Get vocabulary statistics
     * @param domain Domain filter (empty for all domains)
     * @return Vocabulary statistics
     */
    VocabularyStats getVocabularyStatistics(const std::string& domain = "") const {
        if (!vocabularyManager_) {
            return VocabularyStats();
        }
        return vocabularyManager_->getVocabularyStatistics(domain);
    }
    
    /**
     * Search vocabulary entries
     * @param query Search query
     * @param domain Domain filter (empty for all domains)
     * @param maxResults Maximum number of results
     * @return Vector of matching vocabulary entries
     */
    std::vector<VocabularyEntry> searchVocabulary(const std::string& query,
                                                 const std::string& domain = "",
                                                 size_t maxResults = 50) const {
        if (!vocabularyManager_) {
            return {};
        }
        return vocabularyManager_->searchVocabulary(query, domain, maxResults);
    }
    
    /**
     * Optimize vocabulary by removing low-confidence entries
     * @param domain Domain to optimize (empty for all domains)
     * @param aggressiveness Optimization aggressiveness (0.0 to 1.0)
     * @return Number of entries removed
     */
    size_t optimizeVocabulary(const std::string& domain = "", float aggressiveness = 0.5f) {
        if (!vocabularyManager_) {
            return 0;
        }
        return vocabularyManager_->optimizeVocabulary(domain, aggressiveness);
    }
    
    /**
     * Get vocabulary conflicts that need resolution
     * @return Vector of unresolved conflicts
     */
    std::vector<VocabularyConflict> getVocabularyConflicts() const {
        if (!vocabularyManager_) {
            return {};
        }
        return vocabularyManager_->getVocabularyConflicts();
    }
    
    /**
     * Resolve a vocabulary conflict
     * @param conflict Conflict to resolve
     * @param resolution Resolution strategy
     * @return true if resolved successfully
     */
    bool resolveVocabularyConflict(const VocabularyConflict& conflict,
                                  ConflictResolution resolution) {
        if (!vocabularyManager_) {
            return false;
        }
        return vocabularyManager_->resolveVocabularyConflict(conflict, resolution);
    }

private:
    void loadDefaultVocabularies() {
        // Load medical vocabulary
        ContextualVocabulary medicalVocab("medical");
        medicalVocab.domainTerms = {
            "patient", "doctor", "physician", "nurse", "hospital", "clinic",
            "diagnosis", "treatment", "medication", "prescription", "therapy",
            "symptoms", "disease", "condition", "examination", "surgery",
            "blood", "heart", "lung", "brain", "kidney", "liver"
        };
        medicalVocab.technicalTerms = {
            "hypertension", "diabetes", "pneumonia", "bronchitis", "arthritis",
            "myocardial", "infarction", "cardiovascular", "respiratory",
            "neurological", "gastrointestinal", "dermatological"
        };
        addDomainVocabulary("medical", medicalVocab);
        
        // Load technical vocabulary
        ContextualVocabulary technicalVocab("technical");
        technicalVocab.domainTerms = {
            "software", "hardware", "computer", "system", "network", "database",
            "server", "application", "program", "code", "algorithm", "data",
            "interface", "protocol", "framework", "library", "API", "SDK"
        };
        technicalVocab.technicalTerms = {
            "JavaScript", "Python", "C++", "Java", "HTML", "CSS", "SQL",
            "HTTP", "HTTPS", "TCP", "UDP", "REST", "JSON", "XML",
            "microservices", "containerization", "virtualization"
        };
        addDomainVocabulary("technical", technicalVocab);
        
        // Load legal vocabulary
        ContextualVocabulary legalVocab("legal");
        legalVocab.domainTerms = {
            "court", "law", "legal", "attorney", "lawyer", "judge", "jury",
            "case", "contract", "agreement", "lawsuit", "litigation",
            "evidence", "testimony", "witness", "defendant", "plaintiff"
        };
        legalVocab.technicalTerms = {
            "jurisdiction", "statute", "regulation", "compliance", "liability",
            "negligence", "breach", "damages", "injunction", "subpoena",
            "deposition", "arbitration", "mediation"
        };
        addDomainVocabulary("legal", legalVocab);
    }
    
    std::string applyCorrections(const std::string& originalText,
                                const std::vector<ContextualCorrection>& corrections) {
        std::string result = originalText;
        
        // Sort corrections by position (reverse order to maintain positions)
        auto sortedCorrections = corrections;
        std::sort(sortedCorrections.begin(), sortedCorrections.end(),
            [](const auto& a, const auto& b) { return a.startPosition > b.startPosition; });
        
        // Apply corrections
        for (const auto& correction : sortedCorrections) {
            if (correction.confidence > 0.7f) { // Only apply high-confidence corrections
                result.replace(correction.startPosition, 
                              correction.endPosition - correction.startPosition,
                              correction.correctedText);
            }
        }
        
        return result;
    }
    
    void updateTopicDetection(ConversationContext& context, const std::string& utterance) {
        // Simple topic detection based on keyword frequency
        std::map<std::string, int> topicKeywords = {
            {"medical", 0}, {"technical", 0}, {"legal", 0}, {"business", 0}
        };
        
        std::string lowerUtterance = utterance;
        std::transform(lowerUtterance.begin(), lowerUtterance.end(), 
                      lowerUtterance.begin(), ::tolower);
        
        // Count topic-related keywords
        for (const auto& [domain, vocab] : domainVocabularies_) {
            for (const auto& term : vocab.domainTerms) {
                if (lowerUtterance.find(term) != std::string::npos) {
                    topicKeywords[domain]++;
                }
            }
        }
        
        // Find most frequent topic
        auto maxTopic = std::max_element(topicKeywords.begin(), topicKeywords.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        
        if (maxTopic->second > 0) {
            context.currentTopic = maxTopic->first;
        }
    }
    
    void updateContextualWeights(ConversationContext& context) {
        // Update contextual weights based on word frequency in conversation
        std::map<std::string, int> wordCounts;
        
        for (const auto& utterance : context.previousUtterances) {
            std::istringstream iss(utterance);
            std::string word;
            while (iss >> word) {
                std::transform(word.begin(), word.end(), word.begin(), ::tolower);
                wordCounts[word]++;
            }
        }
        
        // Convert counts to weights
        context.contextualWeights.clear();
        int totalWords = 0;
        for (const auto& [word, count] : wordCounts) {
            totalWords += count;
        }
        
        if (totalWords > 0) {
            for (const auto& [word, count] : wordCounts) {
                if (count > 1) { // Only consider words that appear multiple times
                    context.contextualWeights[word] = static_cast<float>(count) / totalWords;
                }
            }
        }
    }
};

// Factory function to create contextual transcriber instance
std::unique_ptr<ContextualTranscriberInterface> createContextualTranscriber() {
    return std::make_unique<ContextualTranscriber>();
}

} // namespace advanced
} // namespace stt