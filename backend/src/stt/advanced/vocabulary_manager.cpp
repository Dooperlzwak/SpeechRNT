#include "stt/advanced/vocabulary_manager.hpp"
#include "utils/logging.hpp"
#include "utils/json_utils.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <regex>
#include <chrono>
#include <cmath>
#include <optional>

namespace stt {
namespace advanced {

/**
 * Vocabulary manager implementation
 */
class VocabularyManager : public VocabularyManagerInterface {
private:
    bool initialized_;
    std::string lastError_;
    VocabularyLearningConfig config_;
    
    // Vocabulary storage
    mutable std::mutex vocabularyMutex_;
    std::map<std::string, std::map<std::string, VocabularyEntry>> domainVocabularies_; // domain -> term -> entry
    std::vector<VocabularyConflict> unresolvedConflicts_;
    
    // Change callbacks
    std::vector<std::function<void(const VocabularyEntry&, const std::string&)>> changeCallbacks_;
    
    // Statistics
    mutable std::mutex statsMutex_;
    VocabularyStats globalStats_;
    std::map<std::string, VocabularyStats> domainStats_;
    
public:
    VocabularyManager() : initialized_(false) {}
    
    bool initialize(const VocabularyLearningConfig& config) override {
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        config_ = config;
        initialized_ = true;
        lastError_.clear();
        
        // Initialize global statistics
        globalStats_ = VocabularyStats();
        globalStats_.lastUpdateTimestamp = getCurrentTimestamp();
        
        return true;
    }
    
    bool addVocabularyEntry(const VocabularyEntry& entry, bool resolveConflicts) override {
        if (!initialized_) {
            lastError_ = "Vocabulary manager not initialized";
            return false;
        }
        
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        try {
            // Check for conflicts
            auto existingIt = domainVocabularies_[entry.domain].find(entry.term);
            if (existingIt != domainVocabularies_[entry.domain].end()) {
                if (resolveConflicts) {
                    VocabularyConflict conflict;
                    conflict.term = entry.term;
                    conflict.existingEntry = existingIt->second;
                    conflict.newEntry = entry;
                    conflict.conflictReason = "Term already exists in domain";
                    conflict.suggestedResolution = config_.defaultConflictResolution;
                    
                    if (!resolveConflictInternal(conflict, config_.defaultConflictResolution)) {
                        unresolvedConflicts_.push_back(conflict);
                        return false;
                    }
                } else {
                    lastError_ = "Term '" + entry.term + "' already exists in domain '" + entry.domain + "'";
                    return false;
                }
            }
            
            // Check domain size limit
            if (domainVocabularies_[entry.domain].size() >= config_.maximumEntriesPerDomain) {
                lastError_ = "Domain '" + entry.domain + "' has reached maximum entries limit";
                return false;
            }
            
            // Add the entry
            VocabularyEntry newEntry = entry;
            newEntry.addedTimestamp = getCurrentTimestamp();
            newEntry.lastUsedTimestamp = newEntry.addedTimestamp;
            
            domainVocabularies_[entry.domain][entry.term] = newEntry;
            
            // Update statistics
            updateStatistics(entry.domain);
            
            // Notify callbacks
            notifyChangeCallbacks(newEntry, "added");
            
            return true;
            
        } catch (const std::exception& e) {
            lastError_ = "Exception adding vocabulary entry: " + std::string(e.what());
            return false;
        }
    }
    
    size_t addVocabularyEntries(const std::vector<VocabularyEntry>& entries, 
                               bool resolveConflicts) override {
        size_t addedCount = 0;
        
        for (const auto& entry : entries) {
            if (addVocabularyEntry(entry, resolveConflicts)) {
                addedCount++;
            }
        }
        
        return addedCount;
    }
    
    bool removeVocabularyEntry(const std::string& term, const std::string& domain) override {
        if (!initialized_) {
            lastError_ = "Vocabulary manager not initialized";
            return false;
        }
        
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        bool removed = false;
        
        if (domain.empty()) {
            // Remove from all domains
            for (auto& [domainName, vocabulary] : domainVocabularies_) {
                auto it = vocabulary.find(term);
                if (it != vocabulary.end()) {
                    VocabularyEntry removedEntry = it->second;
                    vocabulary.erase(it);
                    updateStatistics(domainName);
                    notifyChangeCallbacks(removedEntry, "removed");
                    removed = true;
                }
            }
        } else {
            // Remove from specific domain
            auto domainIt = domainVocabularies_.find(domain);
            if (domainIt != domainVocabularies_.end()) {
                auto termIt = domainIt->second.find(term);
                if (termIt != domainIt->second.end()) {
                    VocabularyEntry removedEntry = termIt->second;
                    domainIt->second.erase(termIt);
                    updateStatistics(domain);
                    notifyChangeCallbacks(removedEntry, "removed");
                    removed = true;
                }
            }
        }
        
        if (!removed) {
            lastError_ = "Term '" + term + "' not found" + (domain.empty() ? "" : " in domain '" + domain + "'");
        }
        
        return removed;
    }
    
    bool updateVocabularyEntry(const std::string& term, const std::string& domain,
                              const VocabularyEntry& updatedEntry) override {
        if (!initialized_) {
            lastError_ = "Vocabulary manager not initialized";
            return false;
        }
        
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        auto domainIt = domainVocabularies_.find(domain);
        if (domainIt == domainVocabularies_.end()) {
            lastError_ = "Domain '" + domain + "' not found";
            return false;
        }
        
        auto termIt = domainIt->second.find(term);
        if (termIt == domainIt->second.end()) {
            lastError_ = "Term '" + term + "' not found in domain '" + domain + "'";
            return false;
        }
        
        // Update entry while preserving some metadata
        VocabularyEntry newEntry = updatedEntry;
        newEntry.addedTimestamp = termIt->second.addedTimestamp;
        newEntry.usageCount = termIt->second.usageCount;
        
        termIt->second = newEntry;
        updateStatistics(domain);
        notifyChangeCallbacks(newEntry, "updated");
        
        return true;
    }
    
    std::optional<VocabularyEntry> getVocabularyEntry(const std::string& term,
                                                     const std::string& domain) const override {
        if (!initialized_) {
            return std::nullopt;
        }
        
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        if (domain.empty()) {
            // Search all domains
            for (const auto& [domainName, vocabulary] : domainVocabularies_) {
                auto it = vocabulary.find(term);
                if (it != vocabulary.end()) {
                    return it->second;
                }
            }
        } else {
            // Search specific domain
            auto domainIt = domainVocabularies_.find(domain);
            if (domainIt != domainVocabularies_.end()) {
                auto termIt = domainIt->second.find(term);
                if (termIt != domainIt->second.end()) {
                    return termIt->second;
                }
            }
        }
        
        return std::nullopt;
    }
    
    std::vector<VocabularyEntry> searchVocabulary(const std::string& query,
                                                 const std::string& domain,
                                                 size_t maxResults) const override {
        if (!initialized_) {
            return {};
        }
        
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        std::vector<std::pair<VocabularyEntry, float>> scoredResults;
        std::string lowerQuery = query;
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
        
        auto searchInDomain = [&](const std::map<std::string, VocabularyEntry>& vocabulary) {
            for (const auto& [term, entry] : vocabulary) {
                std::string lowerTerm = term;
                std::transform(lowerTerm.begin(), lowerTerm.end(), lowerTerm.begin(), ::tolower);
                
                float score = 0.0f;
                
                // Exact match
                if (lowerTerm == lowerQuery) {
                    score = 1.0f;
                } 
                // Starts with query
                else if (lowerTerm.find(lowerQuery) == 0) {
                    score = 0.8f;
                }
                // Contains query
                else if (lowerTerm.find(lowerQuery) != std::string::npos) {
                    score = 0.6f;
                }
                // Check alternatives
                else {
                    for (const auto& alt : entry.alternatives) {
                        std::string lowerAlt = alt;
                        std::transform(lowerAlt.begin(), lowerAlt.end(), lowerAlt.begin(), ::tolower);
                        if (lowerAlt.find(lowerQuery) != std::string::npos) {
                            score = 0.4f;
                            break;
                        }
                    }
                }
                
                if (score > 0.0f) {
                    // Boost score based on usage and confidence
                    score *= (0.5f + 0.3f * entry.confidence + 0.2f * std::min(1.0f, entry.usageCount / 10.0f));
                    scoredResults.push_back({entry, score});
                }
            }
        };
        
        if (domain.empty()) {
            // Search all domains
            for (const auto& [domainName, vocabulary] : domainVocabularies_) {
                searchInDomain(vocabulary);
            }
        } else {
            // Search specific domain
            auto domainIt = domainVocabularies_.find(domain);
            if (domainIt != domainVocabularies_.end()) {
                searchInDomain(domainIt->second);
            }
        }
        
        // Sort by score
        std::sort(scoredResults.begin(), scoredResults.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        
        // Extract entries
        std::vector<VocabularyEntry> results;
        size_t count = std::min(maxResults, scoredResults.size());
        for (size_t i = 0; i < count; ++i) {
            results.push_back(scoredResults[i].first);
        }
        
        return results;
    }
    
    std::vector<VocabularyEntry> getDomainVocabulary(const std::string& domain) const override {
        if (!initialized_) {
            return {};
        }
        
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        std::vector<VocabularyEntry> entries;
        auto domainIt = domainVocabularies_.find(domain);
        if (domainIt != domainVocabularies_.end()) {
            for (const auto& [term, entry] : domainIt->second) {
                entries.push_back(entry);
            }
        }
        
        return entries;
    }
    
    size_t learnFromCorrections(const std::vector<ContextualCorrection>& corrections,
                               const std::string& domain) override {
        if (!initialized_ || !config_.enableAutomaticLearning) {
            return 0;
        }
        
        size_t learnedCount = 0;
        
        for (const auto& correction : corrections) {
            if (correction.confidence >= config_.minimumConfidenceThreshold) {
                VocabularyEntry entry;
                entry.term = correction.correctedText;
                entry.category = correction.correctionType;
                entry.domain = domain;
                entry.probability = correction.confidence;
                entry.confidence = correction.confidence;
                entry.source = VocabularySource::USER_CORRECTION;
                entry.description = correction.reasoning;
                
                // Add original text as alternative
                if (correction.originalText != correction.correctedText) {
                    entry.alternatives.push_back(correction.originalText);
                }
                
                if (addVocabularyEntry(entry, true)) {
                    learnedCount++;
                }
            }
        }
        
        return learnedCount;
    }
    
    size_t learnFromText(const std::string& text, const std::string& domain,
                        const std::string& extractionMethod) override {
        if (!initialized_ || !config_.enableAutomaticLearning) {
            return 0;
        }
        
        size_t learnedCount = 0;
        
        if (extractionMethod == "keyword") {
            learnedCount = extractKeywords(text, domain);
        } else if (extractionMethod == "named_entity") {
            learnedCount = extractNamedEntities(text, domain);
        } else if (extractionMethod == "technical_terms") {
            learnedCount = extractTechnicalTerms(text, domain);
        }
        
        return learnedCount;
    }
    
    void updateUsageStatistics(const std::string& term, const std::string& domain,
                              bool success) override {
        if (!initialized_ || !config_.enableUsageTracking) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        auto domainIt = domainVocabularies_.find(domain);
        if (domainIt != domainVocabularies_.end()) {
            auto termIt = domainIt->second.find(term);
            if (termIt != domainIt->second.end()) {
                termIt->second.usageCount++;
                termIt->second.lastUsedTimestamp = getCurrentTimestamp();
                
                // Update probability based on success rate
                if (config_.enableProbabilityUpdates) {
                    float currentProb = termIt->second.probability;
                    float targetProb = success ? 1.0f : 0.0f;
                    termIt->second.probability = currentProb + config_.learningRate * (targetProb - currentProb);
                    termIt->second.probability = std::max(0.0f, std::min(1.0f, termIt->second.probability));
                }
                
                updateStatistics(domain);
                notifyChangeCallbacks(termIt->second, "usage_updated");
            }
        }
    }
    
    std::vector<VocabularyConflict> getVocabularyConflicts() const override {
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        return unresolvedConflicts_;
    }
    
    bool resolveVocabularyConflict(const VocabularyConflict& conflict,
                                  ConflictResolution resolution) override {
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        // Find and remove the conflict from unresolved list
        auto conflictIt = std::find_if(unresolvedConflicts_.begin(), unresolvedConflicts_.end(),
            [&conflict](const VocabularyConflict& c) { 
                return c.term == conflict.term && c.existingEntry.domain == conflict.existingEntry.domain; 
            });
        
        if (conflictIt == unresolvedConflicts_.end()) {
            lastError_ = "Conflict not found";
            return false;
        }
        
        bool resolved = resolveConflictInternal(conflict, resolution);
        if (resolved) {
            unresolvedConflicts_.erase(conflictIt);
        }
        
        return resolved;
    }
    
    void setConflictResolutionStrategy(ConflictResolution strategy) override {
        config_.defaultConflictResolution = strategy;
    }
    
    VocabularyStats getVocabularyStatistics(const std::string& domain) const override {
        std::lock_guard<std::mutex> lock(statsMutex_);
        
        if (domain.empty()) {
            return globalStats_;
        } else {
            auto it = domainStats_.find(domain);
            if (it != domainStats_.end()) {
                return it->second;
            }
            return VocabularyStats();
        }
    }
    
    std::string exportVocabulary(const std::string& domain, const std::string& format) const override {
        if (!initialized_) {
            return "";
        }
        
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        if (format == "json") {
            return exportToJson(domain);
        } else if (format == "csv") {
            return exportToCsv(domain);
        } else if (format == "xml") {
            return exportToXml(domain);
        }
        
        return "";
    }
    
    size_t importVocabulary(const std::string& data, const std::string& format,
                           ConflictResolution mergeStrategy) override {
        if (!initialized_) {
            return 0;
        }
        
        if (format == "json") {
            return importFromJson(data, mergeStrategy);
        } else if (format == "csv") {
            return importFromCsv(data, mergeStrategy);
        } else if (format == "xml") {
            return importFromXml(data, mergeStrategy);
        }
        
        return 0;
    }
    
    size_t clearVocabulary(const std::string& domain) override {
        if (!initialized_) {
            return 0;
        }
        
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        size_t removedCount = 0;
        
        if (domain.empty()) {
            // Clear all domains
            for (auto& [domainName, vocabulary] : domainVocabularies_) {
                removedCount += vocabulary.size();
                vocabulary.clear();
                updateStatistics(domainName);
            }
        } else {
            // Clear specific domain
            auto domainIt = domainVocabularies_.find(domain);
            if (domainIt != domainVocabularies_.end()) {
                removedCount = domainIt->second.size();
                domainIt->second.clear();
                updateStatistics(domain);
            }
        }
        
        return removedCount;
    }
    
    std::vector<std::string> getSupportedDomains() const override {
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        std::vector<std::string> domains;
        for (const auto& [domain, _] : domainVocabularies_) {
            domains.push_back(domain);
        }
        
        return domains;
    }
    
    bool createDomain(const std::string& domain, const std::string& description) override {
        if (!initialized_) {
            lastError_ = "Vocabulary manager not initialized";
            return false;
        }
        
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        if (domainVocabularies_.find(domain) != domainVocabularies_.end()) {
            lastError_ = "Domain '" + domain + "' already exists";
            return false;
        }
        
        domainVocabularies_[domain] = std::map<std::string, VocabularyEntry>();
        updateStatistics(domain);
        
        return true;
    }
    
    bool removeDomain(const std::string& domain) override {
        if (!initialized_) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        auto it = domainVocabularies_.find(domain);
        if (it != domainVocabularies_.end()) {
            domainVocabularies_.erase(it);
            
            std::lock_guard<std::mutex> statsLock(statsMutex_);
            domainStats_.erase(domain);
            
            return true;
        }
        
        return false;
    }
    
    size_t optimizeVocabulary(const std::string& domain, float aggressiveness) override {
        if (!initialized_) {
            return 0;
        }
        
        std::lock_guard<std::mutex> lock(vocabularyMutex_);
        
        size_t removedCount = 0;
        float confidenceThreshold = 0.3f + (1.0f - aggressiveness) * 0.4f; // 0.3 to 0.7
        int64_t usageThreshold = static_cast<int64_t>((1.0f - aggressiveness) * 30); // 0 to 30 days
        int64_t currentTime = getCurrentTimestamp();
        int64_t timeThreshold = currentTime - (usageThreshold * 24 * 60 * 60 * 1000); // Convert days to ms
        
        auto optimizeDomain = [&](std::map<std::string, VocabularyEntry>& vocabulary) {
            auto it = vocabulary.begin();
            while (it != vocabulary.end()) {
                const auto& entry = it->second;
                bool shouldRemove = false;
                
                // Remove low confidence entries
                if (entry.confidence < confidenceThreshold) {
                    shouldRemove = true;
                }
                
                // Remove unused entries (if aggressiveness is high)
                if (aggressiveness > 0.7f && entry.usageCount == 0 && 
                    entry.lastUsedTimestamp < timeThreshold) {
                    shouldRemove = true;
                }
                
                // Remove very low probability entries
                if (entry.probability < 0.1f) {
                    shouldRemove = true;
                }
                
                if (shouldRemove) {
                    VocabularyEntry removedEntry = it->second;
                    it = vocabulary.erase(it);
                    removedCount++;
                    notifyChangeCallbacks(removedEntry, "optimized_removed");
                } else {
                    ++it;
                }
            }
        };
        
        if (domain.empty()) {
            // Optimize all domains
            for (auto& [domainName, vocabulary] : domainVocabularies_) {
                optimizeDomain(vocabulary);
                updateStatistics(domainName);
            }
        } else {
            // Optimize specific domain
            auto domainIt = domainVocabularies_.find(domain);
            if (domainIt != domainVocabularies_.end()) {
                optimizeDomain(domainIt->second);
                updateStatistics(domain);
            }
        }
        
        return removedCount;
    }
    
    bool backupVocabulary(const std::string& filePath) const override {
        try {
            std::string jsonData = exportVocabulary("", "json");
            std::ofstream file(filePath);
            if (file.is_open()) {
                file << jsonData;
                file.close();
                return true;
            }
        } catch (const std::exception& e) {
            // Log error
        }
        
        return false;
    }
    
    bool restoreVocabulary(const std::string& filePath,
                          ConflictResolution mergeStrategy) override {
        try {
            std::ifstream file(filePath);
            if (file.is_open()) {
                std::string jsonData((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
                file.close();
                
                size_t importedCount = importVocabulary(jsonData, "json", mergeStrategy);
                return importedCount > 0;
            }
        } catch (const std::exception& e) {
            lastError_ = "Exception restoring vocabulary: " + std::string(e.what());
        }
        
        return false;
    }
    
    void registerChangeCallback(std::function<void(const VocabularyEntry&, const std::string&)> callback) override {
        changeCallbacks_.push_back(callback);
    }
    
    VocabularyLearningConfig getLearningConfiguration() const override {
        return config_;
    }
    
    bool updateLearningConfiguration(const VocabularyLearningConfig& config) override {
        config_ = config;
        return true;
    }
    
    bool isInitialized() const override {
        return initialized_;
    }
    
    std::string getLastError() const override {
        return lastError_;
    }
    
    void reset() override {
        std::lock_guard<std::mutex> vocabLock(vocabularyMutex_);
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        
        domainVocabularies_.clear();
        unresolvedConflicts_.clear();
        globalStats_ = VocabularyStats();
        domainStats_.clear();
        lastError_.clear();
    }

private:
    int64_t getCurrentTimestamp() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    void updateStatistics(const std::string& domain) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        
        // Update domain statistics
        VocabularyStats& domainStat = domainStats_[domain];
        domainStat = VocabularyStats();
        domainStat.lastUpdateTimestamp = getCurrentTimestamp();
        
        if (domainVocabularies_.find(domain) != domainVocabularies_.end()) {
            const auto& vocabulary = domainVocabularies_[domain];
            domainStat.totalEntries = vocabulary.size();
            
            float totalConfidence = 0.0f;
            float totalProbability = 0.0f;
            
            for (const auto& [term, entry] : vocabulary) {
                totalConfidence += entry.confidence;
                totalProbability += entry.probability;
                domainStat.totalUsageCount += entry.usageCount;
                
                // Update category counts
                if (entry.category == "domain_term") domainStat.entriesByCategory[0]++;
                else if (entry.category == "proper_noun") domainStat.entriesByCategory[1]++;
                else if (entry.category == "technical_term") domainStat.entriesByCategory[2]++;
                
                // Update source counts
                domainStat.entriesBySource[static_cast<int>(entry.source)]++;
            }
            
            if (domainStat.totalEntries > 0) {
                domainStat.averageConfidence = totalConfidence / domainStat.totalEntries;
                domainStat.averageProbability = totalProbability / domainStat.totalEntries;
            }
        }
        
        // Update global statistics
        globalStats_ = VocabularyStats();
        globalStats_.lastUpdateTimestamp = getCurrentTimestamp();
        
        for (const auto& [domainName, stats] : domainStats_) {
            globalStats_.totalEntries += stats.totalEntries;
            globalStats_.totalUsageCount += stats.totalUsageCount;
            
            for (int i = 0; i < 3; ++i) {
                globalStats_.entriesByCategory[i] += stats.entriesByCategory[i];
            }
            for (int i = 0; i < 5; ++i) {
                globalStats_.entriesBySource[i] += stats.entriesBySource[i];
            }
        }
        
        if (globalStats_.totalEntries > 0) {
            float totalConfidence = 0.0f;
            float totalProbability = 0.0f;
            
            for (const auto& [domainName, vocabulary] : domainVocabularies_) {
                for (const auto& [term, entry] : vocabulary) {
                    totalConfidence += entry.confidence;
                    totalProbability += entry.probability;
                }
            }
            
            globalStats_.averageConfidence = totalConfidence / globalStats_.totalEntries;
            globalStats_.averageProbability = totalProbability / globalStats_.totalEntries;
        }
    }
    
    void notifyChangeCallbacks(const VocabularyEntry& entry, const std::string& action) {
        for (const auto& callback : changeCallbacks_) {
            try {
                callback(entry, action);
            } catch (const std::exception& e) {
                // Log callback error but continue
            }
        }
    }
    
    bool resolveConflictInternal(const VocabularyConflict& conflict, ConflictResolution resolution) {
        auto& vocabulary = domainVocabularies_[conflict.existingEntry.domain];
        
        switch (resolution) {
            case ConflictResolution::KEEP_EXISTING:
                return true; // Do nothing
                
            case ConflictResolution::REPLACE_WITH_NEW:
                vocabulary[conflict.term] = conflict.newEntry;
                notifyChangeCallbacks(conflict.newEntry, "conflict_resolved_replaced");
                return true;
                
            case ConflictResolution::MERGE_ENTRIES: {
                VocabularyEntry mergedEntry = conflict.existingEntry;
                
                // Merge alternatives
                for (const auto& alt : conflict.newEntry.alternatives) {
                    if (std::find(mergedEntry.alternatives.begin(), mergedEntry.alternatives.end(), alt) 
                        == mergedEntry.alternatives.end()) {
                        mergedEntry.alternatives.push_back(alt);
                    }
                }
                
                // Use higher confidence and probability
                mergedEntry.confidence = std::max(mergedEntry.confidence, conflict.newEntry.confidence);
                mergedEntry.probability = std::max(mergedEntry.probability, conflict.newEntry.probability);
                
                // Update usage count
                mergedEntry.usageCount += conflict.newEntry.usageCount;
                
                // Use more recent timestamp
                mergedEntry.lastUsedTimestamp = std::max(mergedEntry.lastUsedTimestamp, 
                                                        conflict.newEntry.lastUsedTimestamp);
                
                vocabulary[conflict.term] = mergedEntry;
                notifyChangeCallbacks(mergedEntry, "conflict_resolved_merged");
                return true;
            }
            
            case ConflictResolution::HIGHEST_CONFIDENCE:
                if (conflict.newEntry.confidence > conflict.existingEntry.confidence) {
                    vocabulary[conflict.term] = conflict.newEntry;
                    notifyChangeCallbacks(conflict.newEntry, "conflict_resolved_higher_confidence");
                }
                return true;
                
            case ConflictResolution::MOST_RECENT:
                if (conflict.newEntry.addedTimestamp > conflict.existingEntry.addedTimestamp) {
                    vocabulary[conflict.term] = conflict.newEntry;
                    notifyChangeCallbacks(conflict.newEntry, "conflict_resolved_more_recent");
                }
                return true;
                
            case ConflictResolution::USER_DECISION:
                return false; // Requires user intervention
        }
        
        return false;
    }
    
    size_t extractKeywords(const std::string& text, const std::string& domain) {
        // Simple keyword extraction based on word frequency and length
        std::map<std::string, int> wordCounts;
        std::istringstream iss(text);
        std::string word;
        
        while (iss >> word) {
            // Clean word
            std::transform(word.begin(), word.end(), word.begin(), ::tolower);
            word.erase(std::remove_if(word.begin(), word.end(), 
                [](char c) { return !std::isalnum(c); }), word.end());
            
            if (word.length() > 3 && word.length() < 20) { // Reasonable word length
                wordCounts[word]++;
            }
        }
        
        // Extract top keywords
        std::vector<std::pair<std::string, int>> sortedWords(wordCounts.begin(), wordCounts.end());
        std::sort(sortedWords.begin(), sortedWords.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        
        size_t learnedCount = 0;
        size_t maxKeywords = std::min(static_cast<size_t>(10), sortedWords.size());
        
        for (size_t i = 0; i < maxKeywords; ++i) {
            const auto& [term, count] = sortedWords[i];
            
            if (count > 1) { // Only consider words that appear multiple times
                VocabularyEntry entry;
                entry.term = term;
                entry.category = "domain_term";
                entry.domain = domain;
                entry.probability = std::min(1.0f, static_cast<float>(count) / 10.0f);
                entry.confidence = 0.6f; // Medium confidence for extracted keywords
                entry.source = VocabularySource::AUTOMATIC_EXTRACTION;
                entry.description = "Extracted from training text";
                
                if (addVocabularyEntry(entry, true)) {
                    learnedCount++;
                }
            }
        }
        
        return learnedCount;
    }
    
    size_t extractNamedEntities(const std::string& text, const std::string& domain) {
        // Simple named entity extraction (capitalized words)
        std::regex namedEntityRegex(R"(\b[A-Z][a-zA-Z]+(?:\s+[A-Z][a-zA-Z]+)*\b)");
        std::sregex_iterator iter(text.begin(), text.end(), namedEntityRegex);
        std::sregex_iterator end;
        
        std::set<std::string> entities;
        for (; iter != end; ++iter) {
            std::string entity = iter->str();
            if (entity.length() > 2 && entity.length() < 50) {
                entities.insert(entity);
            }
        }
        
        size_t learnedCount = 0;
        for (const auto& entity : entities) {
            VocabularyEntry entry;
            entry.term = entity;
            entry.category = "proper_noun";
            entry.domain = domain;
            entry.probability = 0.7f;
            entry.confidence = 0.8f; // Higher confidence for named entities
            entry.source = VocabularySource::AUTOMATIC_EXTRACTION;
            entry.description = "Extracted named entity";
            
            if (addVocabularyEntry(entry, true)) {
                learnedCount++;
            }
        }
        
        return learnedCount;
    }
    
    size_t extractTechnicalTerms(const std::string& text, const std::string& domain) {
        // Simple technical term extraction (words with specific patterns)
        std::vector<std::regex> technicalPatterns = {
            std::regex(R"(\b\w+(?:API|SDK|HTTP|TCP|UDP|JSON|XML|HTML|CSS|SQL)\b)", std::regex_constants::icase),
            std::regex(R"(\b(?:micro|macro|multi|inter|intra|pre|post|sub|super)\w+\b)", std::regex_constants::icase),
            std::regex(R"(\b\w+(?:tion|sion|ment|ness|ity|ism|ology|graphy)\b)", std::regex_constants::icase)
        };
        
        std::set<std::string> technicalTerms;
        
        for (const auto& pattern : technicalPatterns) {
            std::sregex_iterator iter(text.begin(), text.end(), pattern);
            std::sregex_iterator end;
            
            for (; iter != end; ++iter) {
                std::string term = iter->str();
                if (term.length() > 4 && term.length() < 30) {
                    technicalTerms.insert(term);
                }
            }
        }
        
        size_t learnedCount = 0;
        for (const auto& term : technicalTerms) {
            VocabularyEntry entry;
            entry.term = term;
            entry.category = "technical_term";
            entry.domain = domain;
            entry.probability = 0.6f;
            entry.confidence = 0.7f;
            entry.source = VocabularySource::AUTOMATIC_EXTRACTION;
            entry.description = "Extracted technical term";
            
            if (addVocabularyEntry(entry, true)) {
                learnedCount++;
            }
        }
        
        return learnedCount;
    }
    
    std::string exportToJson(const std::string& domain) const {
        // Simplified JSON export implementation
        std::ostringstream json;
        json << "{\"version\":\"1.0\",\"exportTimestamp\":\"" << getCurrentTimestamp() << "\",";
        
        if (domain.empty()) {
            json << "\"domains\":[";
            bool firstDomain = true;
            for (const auto& [domainName, vocabulary] : domainVocabularies_) {
                if (!firstDomain) json << ",";
                json << "{\"name\":\"" << domainName << "\",\"entries\":[";
                
                bool firstEntry = true;
                for (const auto& [term, entry] : vocabulary) {
                    if (!firstEntry) json << ",";
                    json << "{\"term\":\"" << entry.term << "\",\"category\":\"" << entry.category 
                         << "\",\"confidence\":" << entry.confidence << ",\"probability\":" << entry.probability << "}";
                    firstEntry = false;
                }
                
                json << "]}";
                firstDomain = false;
            }
            json << "]";
        } else {
            json << "\"domain\":\"" << domain << "\",\"entries\":[";
            auto domainIt = domainVocabularies_.find(domain);
            if (domainIt != domainVocabularies_.end()) {
                bool firstEntry = true;
                for (const auto& [term, entry] : domainIt->second) {
                    if (!firstEntry) json << ",";
                    json << "{\"term\":\"" << entry.term << "\",\"category\":\"" << entry.category 
                         << "\",\"confidence\":" << entry.confidence << ",\"probability\":" << entry.probability << "}";
                    firstEntry = false;
                }
            }
            json << "]";
        }
        
        json << "}";
        return json.str();
    }
    
    std::string exportToCsv(const std::string& domain) const {
        std::ostringstream csv;
        csv << "domain,term,category,confidence,probability,usage_count,added_timestamp\n";
        
        auto exportDomain = [&](const std::string& domainName, const std::map<std::string, VocabularyEntry>& vocabulary) {
            for (const auto& [term, entry] : vocabulary) {
                csv << domainName << "," << entry.term << "," << entry.category << ","
                    << entry.confidence << "," << entry.probability << "," << entry.usageCount << ","
                    << entry.addedTimestamp << "\n";
            }
        };
        
        if (domain.empty()) {
            for (const auto& [domainName, vocabulary] : domainVocabularies_) {
                exportDomain(domainName, vocabulary);
            }
        } else {
            auto domainIt = domainVocabularies_.find(domain);
            if (domainIt != domainVocabularies_.end()) {
                exportDomain(domain, domainIt->second);
            }
        }
        
        return csv.str();
    }
    
    std::string exportToXml(const std::string& domain) const {
        // Simplified XML export implementation
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<vocabulary>\n";
        
        auto exportDomain = [&](const std::string& domainName, const std::map<std::string, VocabularyEntry>& vocabulary) {
            xml << "  <domain name=\"" << domainName << "\">\n";
            for (const auto& [term, entry] : vocabulary) {
                xml << "    <entry term=\"" << entry.term << "\" category=\"" << entry.category 
                    << "\" confidence=\"" << entry.confidence << "\" probability=\"" << entry.probability << "\"/>\n";
            }
            xml << "  </domain>\n";
        };
        
        if (domain.empty()) {
            for (const auto& [domainName, vocabulary] : domainVocabularies_) {
                exportDomain(domainName, vocabulary);
            }
        } else {
            auto domainIt = domainVocabularies_.find(domain);
            if (domainIt != domainVocabularies_.end()) {
                exportDomain(domain, domainIt->second);
            }
        }
        
        xml << "</vocabulary>\n";
        return xml.str();
    }
    
    size_t importFromJson(const std::string& data, ConflictResolution mergeStrategy) {
        // Simplified JSON import implementation
        // In a real implementation, this would use a proper JSON parser
        size_t importedCount = 0;
        
        // This is a placeholder implementation
        // Real implementation would parse JSON and extract vocabulary entries
        
        return importedCount;
    }
    
    size_t importFromCsv(const std::string& data, ConflictResolution mergeStrategy) {
        std::istringstream stream(data);
        std::string line;
        size_t importedCount = 0;
        
        // Skip header line
        if (std::getline(stream, line)) {
            while (std::getline(stream, line)) {
                std::istringstream lineStream(line);
                std::string domain, term, category, confidenceStr, probabilityStr, usageCountStr, timestampStr;
                
                if (std::getline(lineStream, domain, ',') &&
                    std::getline(lineStream, term, ',') &&
                    std::getline(lineStream, category, ',') &&
                    std::getline(lineStream, confidenceStr, ',') &&
                    std::getline(lineStream, probabilityStr, ',') &&
                    std::getline(lineStream, usageCountStr, ',') &&
                    std::getline(lineStream, timestampStr)) {
                    
                    VocabularyEntry entry;
                    entry.term = term;
                    entry.category = category;
                    entry.domain = domain;
                    entry.confidence = std::stof(confidenceStr);
                    entry.probability = std::stof(probabilityStr);
                    entry.usageCount = std::stoull(usageCountStr);
                    entry.addedTimestamp = std::stoll(timestampStr);
                    entry.source = VocabularySource::EXTERNAL_IMPORT;
                    
                    if (addVocabularyEntry(entry, mergeStrategy != ConflictResolution::USER_DECISION)) {
                        importedCount++;
                    }
                }
            }
        }
        
        return importedCount;
    }
    
    size_t importFromXml(const std::string& data, ConflictResolution mergeStrategy) {
        // Simplified XML import implementation
        // In a real implementation, this would use a proper XML parser
        size_t importedCount = 0;
        
        // This is a placeholder implementation
        // Real implementation would parse XML and extract vocabulary entries
        
        return importedCount;
    }
};

// Factory function implementation
std::unique_ptr<VocabularyManagerInterface> createVocabularyManager() {
    return std::make_unique<VocabularyManager>();
}

} // namespace advanced
} // namespace stt