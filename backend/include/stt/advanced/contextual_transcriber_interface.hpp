#pragma once

#include "stt/stt_interface.hpp"
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <cstdint>

namespace stt {
namespace advanced {

/**
 * Contextual vocabulary structure
 */
struct ContextualVocabulary {
    std::vector<std::string> domainTerms;
    std::vector<std::string> properNouns;
    std::vector<std::string> technicalTerms;
    std::map<std::string, float> termProbabilities;
    std::string domain;
    float vocabularyWeight = 1.0f;
    
    ContextualVocabulary() = default;
    
    explicit ContextualVocabulary(const std::string& domainName) : domain(domainName) {}
};

/**
 * Conversation context information
 */
struct ConversationContext {
    std::vector<std::string> previousUtterances;
    std::string currentTopic;
    std::string domain;
    std::map<std::string, float> contextualWeights;
    uint32_t utteranceId;
    int64_t timestampMs;
    std::string speakerInfo;
    
    ConversationContext() : utteranceId(0), timestampMs(0) {}
};

/**
 * Contextual correction information
 */
struct ContextualCorrection {
    std::string originalText;
    std::string correctedText;
    std::string correctionType; // "domain_term", "proper_noun", "context_aware", etc.
    float confidence;
    size_t startPosition;
    size_t endPosition;
    std::string reasoning; // Explanation for the correction
    
    ContextualCorrection() : confidence(0.0f), startPosition(0), endPosition(0) {}
    
    ContextualCorrection(const std::string& original, const std::string& corrected,
                        const std::string& type, float conf, size_t start, size_t end)
        : originalText(original), correctedText(corrected), correctionType(type)
        , confidence(conf), startPosition(start), endPosition(end) {}
};

/**
 * Contextual transcription result
 */
struct ContextualResult {
    std::string enhancedText;
    std::vector<std::string> alternativeTranscriptions;
    std::vector<ContextualCorrection> corrections;
    float contextualConfidence;
    std::string detectedDomain;
    std::string detectedTopic;
    std::map<std::string, float> domainProbabilities;
    bool contextUsed;
    std::string processingInfo; // Debug information
    
    ContextualResult() : contextualConfidence(0.0f), contextUsed(false) {}
};

/**
 * Domain classifier interface
 */
class DomainClassifier {
public:
    virtual ~DomainClassifier() = default;
    
    /**
     * Initialize the domain classifier
     * @param modelPath Path to domain classification model
     * @return true if initialization successful
     */
    virtual bool initialize(const std::string& modelPath) = 0;
    
    /**
     * Classify domain from text
     * @param text Input text to classify
     * @return Map of domain to probability
     */
    virtual std::map<std::string, float> classifyDomain(const std::string& text) = 0;
    
    /**
     * Get most likely domain
     * @param text Input text to classify
     * @return Most likely domain name
     */
    virtual std::string getMostLikelyDomain(const std::string& text) = 0;
    
    /**
     * Add custom domain
     * @param domainName Domain name
     * @param trainingTexts Training texts for the domain
     * @return true if added successfully
     */
    virtual bool addCustomDomain(const std::string& domainName,
                                const std::vector<std::string>& trainingTexts) = 0;
    
    /**
     * Get supported domains
     * @return Vector of supported domain names
     */
    virtual std::vector<std::string> getSupportedDomains() const = 0;
    
    /**
     * Check if classifier is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Contextual language model interface
 */
class ContextualLanguageModel {
public:
    virtual ~ContextualLanguageModel() = default;
    
    /**
     * Initialize the contextual language model
     * @param modelPath Path to language model
     * @return true if initialization successful
     */
    virtual bool initialize(const std::string& modelPath) = 0;
    
    /**
     * Score text based on context
     * @param text Text to score
     * @param context Conversation context
     * @return Context-aware probability score
     */
    virtual float scoreTextWithContext(const std::string& text,
                                      const ConversationContext& context) = 0;
    
    /**
     * Generate alternative transcriptions based on context
     * @param baseText Base transcription text
     * @param context Conversation context
     * @param maxAlternatives Maximum number of alternatives
     * @return Vector of alternative transcriptions with scores
     */
    virtual std::vector<std::pair<std::string, float>> generateAlternatives(
        const std::string& baseText,
        const ConversationContext& context,
        size_t maxAlternatives = 5) = 0;
    
    /**
     * Predict next words based on context
     * @param partialText Partial text
     * @param context Conversation context
     * @param maxPredictions Maximum number of predictions
     * @return Vector of word predictions with probabilities
     */
    virtual std::vector<std::pair<std::string, float>> predictNextWords(
        const std::string& partialText,
        const ConversationContext& context,
        size_t maxPredictions = 10) = 0;
    
    /**
     * Update model with conversation data
     * @param utterances Recent utterances for adaptation
     * @param domain Domain context
     */
    virtual void updateWithConversation(const std::vector<std::string>& utterances,
                                       const std::string& domain) = 0;
    
    /**
     * Check if model is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Vocabulary matcher interface
 */
class VocabularyMatcher {
public:
    virtual ~VocabularyMatcher() = default;
    
    /**
     * Initialize the vocabulary matcher
     * @return true if initialization successful
     */
    virtual bool initialize() = 0;
    
    /**
     * Add vocabulary for a domain
     * @param domain Domain name
     * @param vocabulary Contextual vocabulary
     * @return true if added successfully
     */
    virtual bool addDomainVocabulary(const std::string& domain,
                                    const ContextualVocabulary& vocabulary) = 0;
    
    /**
     * Match and correct terms in text
     * @param text Input text
     * @param domain Target domain
     * @param context Conversation context
     * @return Vector of corrections
     */
    virtual std::vector<ContextualCorrection> matchAndCorrect(
        const std::string& text,
        const std::string& domain,
        const ConversationContext& context) = 0;
    
    /**
     * Find best matching terms
     * @param term Input term to match
     * @param domain Target domain
     * @param maxMatches Maximum number of matches
     * @return Vector of matching terms with confidence scores
     */
    virtual std::vector<std::pair<std::string, float>> findBestMatches(
        const std::string& term,
        const std::string& domain,
        size_t maxMatches = 5) = 0;
    
    /**
     * Update vocabulary from user corrections
     * @param corrections User-provided corrections
     * @param domain Domain context
     */
    virtual void learnFromCorrections(const std::vector<ContextualCorrection>& corrections,
                                     const std::string& domain) = 0;
    
    /**
     * Get vocabulary for domain
     * @param domain Domain name
     * @return Contextual vocabulary for the domain
     */
    virtual ContextualVocabulary getDomainVocabulary(const std::string& domain) const = 0;
    
    /**
     * Remove domain vocabulary
     * @param domain Domain name
     * @return true if removed successfully
     */
    virtual bool removeDomainVocabulary(const std::string& domain) = 0;
    
    /**
     * Get supported domains
     * @return Vector of domain names with vocabularies
     */
    virtual std::vector<std::string> getSupportedDomains() const = 0;
    
    /**
     * Check if matcher is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Contextual transcriber interface
 */
class ContextualTranscriberInterface {
public:
    virtual ~ContextualTranscriberInterface() = default;
    
    /**
     * Initialize the contextual transcriber
     * @param modelsPath Path to contextual models
     * @return true if initialization successful
     */
    virtual bool initialize(const std::string& modelsPath) = 0;
    
    /**
     * Enhance transcription with contextual information
     * @param baseResult Base transcription result
     * @param context Conversation context
     * @return Enhanced contextual result
     */
    virtual ContextualResult enhanceTranscription(const TranscriptionResult& baseResult,
                                                 const ConversationContext& context) = 0;
    
    /**
     * Add domain vocabulary
     * @param domain Domain name
     * @param vocabulary Contextual vocabulary
     * @return true if added successfully
     */
    virtual bool addDomainVocabulary(const std::string& domain,
                                    const ContextualVocabulary& vocabulary) = 0;
    
    /**
     * Update conversation context
     * @param utteranceId Utterance identifier
     * @param utterance New utterance text
     * @param speakerInfo Speaker information (optional)
     */
    virtual void updateConversationContext(uint32_t utteranceId,
                                          const std::string& utterance,
                                          const std::string& speakerInfo = "") = 0;
    
    /**
     * Get conversation context
     * @param utteranceId Utterance identifier
     * @return Current conversation context
     */
    virtual ConversationContext getConversationContext(uint32_t utteranceId) const = 0;
    
    /**
     * Clear conversation context
     * @param utteranceId Utterance identifier (0 to clear all)
     */
    virtual void clearConversationContext(uint32_t utteranceId = 0) = 0;
    
    /**
     * Detect domain from text
     * @param text Input text
     * @return Detected domain name
     */
    virtual std::string detectDomain(const std::string& text) = 0;
    
    /**
     * Set domain hint for utterance
     * @param utteranceId Utterance identifier
     * @param domain Domain hint
     */
    virtual void setDomainHint(uint32_t utteranceId, const std::string& domain) = 0;
    
    /**
     * Set contextual weight
     * @param weight Weight for contextual corrections (0.0 to 1.0)
     */
    virtual void setContextualWeight(float weight) = 0;
    
    /**
     * Enable or disable domain detection
     * @param enabled true to enable automatic domain detection
     */
    virtual void setDomainDetectionEnabled(bool enabled) = 0;
    
    /**
     * Set maximum context history size
     * @param maxHistory Maximum number of previous utterances to keep
     */
    virtual void setMaxContextHistory(size_t maxHistory) = 0;
    
    /**
     * Add custom vocabulary terms
     * @param terms Custom vocabulary terms
     * @param domain Domain for the terms (optional)
     * @return true if added successfully
     */
    virtual bool addCustomVocabulary(const std::vector<std::string>& terms,
                                    const std::string& domain = "custom") = 0;
    
    /**
     * Remove custom vocabulary
     * @param domain Domain to remove (empty to remove all custom vocabulary)
     * @return true if removed successfully
     */
    virtual bool removeCustomVocabulary(const std::string& domain = "") = 0;
    
    /**
     * Get available domains
     * @return Vector of available domain names
     */
    virtual std::vector<std::string> getAvailableDomains() const = 0;
    
    /**
     * Update configuration
     * @param config New contextual transcription configuration
     * @return true if update successful
     */
    virtual bool updateConfiguration(const ContextualTranscriptionConfig& config) = 0;
    
    /**
     * Get current configuration
     * @return Current contextual transcription configuration
     */
    virtual ContextualTranscriptionConfig getCurrentConfiguration() const = 0;
    
    /**
     * Check if transcriber is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
    
    /**
     * Get last error message
     * @return Last error message
     */
    virtual std::string getLastError() const = 0;
    
    /**
     * Get processing statistics
     * @return Statistics as JSON string
     */
    virtual std::string getProcessingStats() const = 0;
    
    /**
     * Reset transcriber state
     */
    virtual void reset() = 0;
};

} // namespace advanced
} // namespace stt