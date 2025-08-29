#pragma once

#include "contextual_transcriber_interface.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <functional>

namespace stt {
namespace advanced {

/**
 * Vocabulary learning source types
 */
enum class VocabularySource {
    USER_CORRECTION,
    DOMAIN_TRAINING,
    AUTOMATIC_EXTRACTION,
    MANUAL_ADDITION,
    EXTERNAL_IMPORT
};

/**
 * Vocabulary entry with metadata
 */
struct VocabularyEntry {
    std::string term;
    std::string category; // "domain_term", "proper_noun", "technical_term"
    float probability;
    float confidence;
    VocabularySource source;
    std::string domain;
    int64_t addedTimestamp;
    int64_t lastUsedTimestamp;
    size_t usageCount;
    std::vector<std::string> alternatives; // Alternative spellings/forms
    std::string description; // Optional description
    
    VocabularyEntry() 
        : probability(0.5f), confidence(0.5f), source(VocabularySource::MANUAL_ADDITION)
        , addedTimestamp(0), lastUsedTimestamp(0), usageCount(0) {}
    
    VocabularyEntry(const std::string& t, const std::string& cat, const std::string& dom)
        : term(t), category(cat), domain(dom), probability(0.5f), confidence(0.5f)
        , source(VocabularySource::MANUAL_ADDITION), addedTimestamp(0), lastUsedTimestamp(0)
        , usageCount(0) {}
};

/**
 * Vocabulary conflict resolution strategy
 */
enum class ConflictResolution {
    KEEP_EXISTING,      // Keep existing entry
    REPLACE_WITH_NEW,   // Replace with new entry
    MERGE_ENTRIES,      // Merge information from both entries
    HIGHEST_CONFIDENCE, // Keep entry with highest confidence
    MOST_RECENT,        // Keep most recently added entry
    USER_DECISION       // Require user decision
};

/**
 * Vocabulary conflict information
 */
struct VocabularyConflict {
    std::string term;
    VocabularyEntry existingEntry;
    VocabularyEntry newEntry;
    std::string conflictReason;
    ConflictResolution suggestedResolution;
    
    VocabularyConflict() : suggestedResolution(ConflictResolution::USER_DECISION) {}
};

/**
 * Vocabulary statistics
 */
struct VocabularyStats {
    size_t totalEntries;
    size_t entriesByDomain[10]; // Up to 10 domains
    std::vector<std::string> domainNames;
    size_t entriesByCategory[3]; // domain_term, proper_noun, technical_term
    size_t entriesBySource[5];   // Different sources
    float averageConfidence;
    float averageProbability;
    size_t totalUsageCount;
    int64_t lastUpdateTimestamp;
    
    VocabularyStats() : totalEntries(0), averageConfidence(0.0f), averageProbability(0.0f)
                      , totalUsageCount(0), lastUpdateTimestamp(0) {
        for (int i = 0; i < 10; ++i) entriesByDomain[i] = 0;
        for (int i = 0; i < 3; ++i) entriesByCategory[i] = 0;
        for (int i = 0; i < 5; ++i) entriesBySource[i] = 0;
    }
};

/**
 * Vocabulary import/export format
 */
struct VocabularyExportData {
    std::string version;
    std::string exportTimestamp;
    std::string domain;
    std::vector<VocabularyEntry> entries;
    VocabularyStats statistics;
    std::map<std::string, std::string> metadata;
    
    VocabularyExportData() : version("1.0") {}
};

/**
 * Vocabulary learning configuration
 */
struct VocabularyLearningConfig {
    bool enableAutomaticLearning = true;
    float minimumConfidenceThreshold = 0.7f;
    size_t maximumEntriesPerDomain = 10000;
    bool enableConflictResolution = true;
    ConflictResolution defaultConflictResolution = ConflictResolution::HIGHEST_CONFIDENCE;
    bool enableUsageTracking = true;
    bool enableProbabilityUpdates = true;
    float learningRate = 0.1f; // For probability updates
    size_t maxAlternativesPerEntry = 5;
    
    VocabularyLearningConfig() = default;
};

/**
 * Advanced vocabulary manager interface
 */
class VocabularyManagerInterface {
public:
    virtual ~VocabularyManagerInterface() = default;
    
    /**
     * Initialize the vocabulary manager
     * @param config Learning configuration
     * @return true if initialization successful
     */
    virtual bool initialize(const VocabularyLearningConfig& config) = 0;
    
    /**
     * Add vocabulary entry
     * @param entry Vocabulary entry to add
     * @param resolveConflicts Whether to resolve conflicts automatically
     * @return true if added successfully
     */
    virtual bool addVocabularyEntry(const VocabularyEntry& entry, bool resolveConflicts = true) = 0;
    
    /**
     * Add multiple vocabulary entries
     * @param entries Vector of vocabulary entries
     * @param resolveConflicts Whether to resolve conflicts automatically
     * @return Number of entries successfully added
     */
    virtual size_t addVocabularyEntries(const std::vector<VocabularyEntry>& entries, 
                                       bool resolveConflicts = true) = 0;
    
    /**
     * Remove vocabulary entry
     * @param term Term to remove
     * @param domain Domain (empty for all domains)
     * @return true if removed successfully
     */
    virtual bool removeVocabularyEntry(const std::string& term, const std::string& domain = "") = 0;
    
    /**
     * Update vocabulary entry
     * @param term Term to update
     * @param domain Domain
     * @param updatedEntry Updated entry data
     * @return true if updated successfully
     */
    virtual bool updateVocabularyEntry(const std::string& term, const std::string& domain,
                                      const VocabularyEntry& updatedEntry) = 0;
    
    /**
     * Get vocabulary entry
     * @param term Term to find
     * @param domain Domain (empty for any domain)
     * @return Vocabulary entry if found
     */
    virtual std::optional<VocabularyEntry> getVocabularyEntry(const std::string& term,
                                                             const std::string& domain = "") const = 0;
    
    /**
     * Search vocabulary entries
     * @param query Search query
     * @param domain Domain filter (empty for all domains)
     * @param maxResults Maximum number of results
     * @return Vector of matching entries
     */
    virtual std::vector<VocabularyEntry> searchVocabulary(const std::string& query,
                                                         const std::string& domain = "",
                                                         size_t maxResults = 50) const = 0;
    
    /**
     * Get all entries for a domain
     * @param domain Domain name
     * @return Vector of vocabulary entries
     */
    virtual std::vector<VocabularyEntry> getDomainVocabulary(const std::string& domain) const = 0;
    
    /**
     * Learn from user corrections
     * @param corrections Vector of contextual corrections
     * @param domain Domain context
     * @return Number of new entries learned
     */
    virtual size_t learnFromCorrections(const std::vector<ContextualCorrection>& corrections,
                                       const std::string& domain) = 0;
    
    /**
     * Learn from training text
     * @param text Training text
     * @param domain Domain for the text
     * @param extractionMethod Method for extracting terms
     * @return Number of new entries learned
     */
    virtual size_t learnFromText(const std::string& text, const std::string& domain,
                                const std::string& extractionMethod = "keyword") = 0;
    
    /**
     * Update term usage statistics
     * @param term Term that was used
     * @param domain Domain context
     * @param success Whether the term was used successfully
     */
    virtual void updateUsageStatistics(const std::string& term, const std::string& domain,
                                      bool success = true) = 0;
    
    /**
     * Get vocabulary conflicts
     * @return Vector of unresolved conflicts
     */
    virtual std::vector<VocabularyConflict> getVocabularyConflicts() const = 0;
    
    /**
     * Resolve vocabulary conflict
     * @param conflict Conflict to resolve
     * @param resolution Resolution strategy
     * @return true if resolved successfully
     */
    virtual bool resolveVocabularyConflict(const VocabularyConflict& conflict,
                                          ConflictResolution resolution) = 0;
    
    /**
     * Set conflict resolution strategy
     * @param strategy Default conflict resolution strategy
     */
    virtual void setConflictResolutionStrategy(ConflictResolution strategy) = 0;
    
    /**
     * Get vocabulary statistics
     * @param domain Domain filter (empty for all domains)
     * @return Vocabulary statistics
     */
    virtual VocabularyStats getVocabularyStatistics(const std::string& domain = "") const = 0;
    
    /**
     * Export vocabulary data
     * @param domain Domain to export (empty for all domains)
     * @param format Export format ("json", "csv", "xml")
     * @return Exported data as string
     */
    virtual std::string exportVocabulary(const std::string& domain = "",
                                        const std::string& format = "json") const = 0;
    
    /**
     * Import vocabulary data
     * @param data Vocabulary data to import
     * @param format Import format ("json", "csv", "xml")
     * @param mergeStrategy How to handle existing entries
     * @return Number of entries imported
     */
    virtual size_t importVocabulary(const std::string& data, const std::string& format = "json",
                                   ConflictResolution mergeStrategy = ConflictResolution::MERGE_ENTRIES) = 0;
    
    /**
     * Clear vocabulary for domain
     * @param domain Domain to clear (empty for all domains)
     * @return Number of entries removed
     */
    virtual size_t clearVocabulary(const std::string& domain = "") = 0;
    
    /**
     * Get supported domains
     * @return Vector of domain names
     */
    virtual std::vector<std::string> getSupportedDomains() const = 0;
    
    /**
     * Create new domain
     * @param domain Domain name
     * @param description Domain description
     * @return true if created successfully
     */
    virtual bool createDomain(const std::string& domain, const std::string& description = "") = 0;
    
    /**
     * Remove domain and all its vocabulary
     * @param domain Domain name
     * @return true if removed successfully
     */
    virtual bool removeDomain(const std::string& domain) = 0;
    
    /**
     * Optimize vocabulary (remove low-confidence, unused entries)
     * @param domain Domain to optimize (empty for all domains)
     * @param aggressiveness Optimization aggressiveness (0.0 to 1.0)
     * @return Number of entries removed
     */
    virtual size_t optimizeVocabulary(const std::string& domain = "", float aggressiveness = 0.5f) = 0;
    
    /**
     * Backup vocabulary to file
     * @param filePath Path to backup file
     * @return true if backup successful
     */
    virtual bool backupVocabulary(const std::string& filePath) const = 0;
    
    /**
     * Restore vocabulary from file
     * @param filePath Path to backup file
     * @param mergeStrategy How to handle existing entries
     * @return true if restore successful
     */
    virtual bool restoreVocabulary(const std::string& filePath,
                                  ConflictResolution mergeStrategy = ConflictResolution::MERGE_ENTRIES) = 0;
    
    /**
     * Register callback for vocabulary changes
     * @param callback Callback function
     */
    virtual void registerChangeCallback(std::function<void(const VocabularyEntry&, const std::string&)> callback) = 0;
    
    /**
     * Get learning configuration
     * @return Current learning configuration
     */
    virtual VocabularyLearningConfig getLearningConfiguration() const = 0;
    
    /**
     * Update learning configuration
     * @param config New learning configuration
     * @return true if updated successfully
     */
    virtual bool updateLearningConfiguration(const VocabularyLearningConfig& config) = 0;
    
    /**
     * Check if manager is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
    
    /**
     * Get last error message
     * @return Last error message
     */
    virtual std::string getLastError() const = 0;
    
    /**
     * Reset vocabulary manager
     */
    virtual void reset() = 0;
};

/**
 * Factory function to create vocabulary manager
 * @return Unique pointer to vocabulary manager interface
 */
std::unique_ptr<VocabularyManagerInterface> createVocabularyManager();

} // namespace advanced
} // namespace stt