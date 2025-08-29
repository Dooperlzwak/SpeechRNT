#pragma once

#include "translation_interface.hpp"
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <thread>
#include <vector>

// Forward declarations
namespace speechrnt {
namespace models {
class ModelManager;
}
namespace utils {
class GPUManager;
class GPUMemoryPool;
}
namespace mt {
class QualityManager;
class MarianErrorHandler;
class MTConfig;
}
}

namespace speechrnt {
namespace mt {

/**
 * Marian NMT-based translation engine
 * Provides neural machine translation using Marian models
 */
class MarianTranslator : public TranslationInterface {
public:
    MarianTranslator();
    explicit MarianTranslator(std::shared_ptr<const MTConfig> config);
    ~MarianTranslator() override;
    
    // TranslationInterface implementation
    bool initialize(const std::string& sourceLang, const std::string& targetLang) override;
    TranslationResult translate(const std::string& text) override;
    std::future<TranslationResult> translateAsync(const std::string& text) override;
    bool supportsLanguagePair(const std::string& sourceLang, const std::string& targetLang) const override;
    std::vector<std::string> getSupportedSourceLanguages() const override;
    std::vector<std::string> getSupportedTargetLanguages(const std::string& sourceLang) const override;
    bool isReady() const override;
    void cleanup() override;
    
    /**
     * Set the path to Marian models directory
     * @param modelsPath Path to models directory
     */
    void setModelsPath(const std::string& modelsPath);
    
    /**
     * Update configuration at runtime
     * @param config New configuration to apply
     * @return true if configuration was updated successfully
     */
    bool updateConfiguration(std::shared_ptr<const MTConfig> config);
    
    /**
     * Get current configuration
     * @return Current MT configuration
     */
    std::shared_ptr<const MTConfig> getConfiguration() const;
    
    /**
     * Load model for specific language pair
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return true if model loaded successfully
     */
    bool loadModel(const std::string& sourceLang, const std::string& targetLang);
    
    /**
     * Unload model for specific language pair
     * @param sourceLang Source language code
     * @param targetLang Target language code
     */
    void unloadModel(const std::string& sourceLang, const std::string& targetLang);
    
    /**
     * Check if model is loaded for language pair
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return true if model is loaded
     */
    bool isModelLoaded(const std::string& sourceLang, const std::string& targetLang) const;
    
    /**
     * Initialize with GPU acceleration
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @param gpuDeviceId GPU device ID to use
     * @return true if initialization successful
     */
    bool initializeWithGPU(const std::string& sourceLang, const std::string& targetLang, int gpuDeviceId = 0);
    
    /**
     * Enable/disable GPU acceleration for new models
     * @param enabled true to enable GPU acceleration
     * @param deviceId GPU device ID to use
     */
    void setGPUAcceleration(bool enabled, int deviceId = 0);
    
    /**
     * Check if GPU acceleration is currently enabled
     * @return true if GPU acceleration is enabled
     */
    bool isGPUAccelerationEnabled() const;
    
    /**
     * Get current GPU device ID
     * @return GPU device ID, -1 if GPU not enabled
     */
    int getCurrentGPUDevice() const;
    
    /**
     * Validate GPU device availability and compatibility
     * @param deviceId GPU device ID to validate
     * @return true if device is valid and available
     */
    bool validateGPUDevice(int deviceId) const;
    
    /**
     * Get GPU memory usage for translation models
     * @return memory usage in MB, 0 if GPU not used
     */
    size_t getGPUMemoryUsageMB() const;
    
    /**
     * Check if sufficient GPU memory is available
     * @param requiredMB Required memory in MB
     * @return true if sufficient memory available
     */
    bool hasSufficientGPUMemory(size_t requiredMB) const;
    
    /**
     * Generate multiple translation candidates with quality assessment
     * @param text Text to translate
     * @param maxCandidates Maximum number of candidates to generate
     * @return Vector of translation candidates ranked by quality
     */
    std::vector<TranslationResult> getTranslationCandidates(const std::string& text, int maxCandidates = 3);
    
    /**
     * Get fallback translation options for low-quality results
     * @param text Text to translate
     * @return Vector of fallback translation options
     */
    std::vector<std::string> getFallbackTranslations(const std::string& text);
    
    /**
     * Set quality assessment thresholds
     * @param high High quality threshold (0.0-1.0)
     * @param medium Medium quality threshold (0.0-1.0)
     * @param low Low quality threshold (0.0-1.0)
     */
    void setQualityThresholds(float high, float medium, float low);
    
    /**
     * Check if translation meets quality threshold
     * @param result Translation result to check
     * @param requiredLevel Required quality level ("high", "medium", "low")
     * @return true if meets threshold
     */
    bool meetsQualityThreshold(const TranslationResult& result, const std::string& requiredLevel) const;
    
    // Batch translation methods
    std::vector<TranslationResult> translateBatch(const std::vector<std::string>& texts) override;
    std::future<std::vector<TranslationResult>> translateBatchAsync(const std::vector<std::string>& texts) override;
    
    // Streaming translation methods
    bool startStreamingTranslation(const std::string& sessionId, const std::string& sourceLang, const std::string& targetLang) override;
    TranslationResult addStreamingText(const std::string& sessionId, const std::string& text, bool isComplete = false) override;
    TranslationResult finalizeStreamingTranslation(const std::string& sessionId) override;
    void cancelStreamingTranslation(const std::string& sessionId) override;
    bool hasStreamingSession(const std::string& sessionId) const override;
    
    /**
     * Set maximum batch size for batch translation
     * @param maxBatchSize Maximum number of texts to process in one batch
     */
    void setMaxBatchSize(size_t maxBatchSize);
    
    /**
     * Enable/disable translation caching
     * @param enabled true to enable caching
     * @param maxCacheSize Maximum number of cached translations
     */
    void setTranslationCaching(bool enabled, size_t maxCacheSize = 1000);
    
    /**
     * Clear translation cache
     */
    void clearTranslationCache();
    
    /**
     * Get cache hit rate statistics
     * @return Cache hit rate as percentage (0.0-100.0)
     */
    float getCacheHitRate() const;
    
    // Multi-language pair support methods
    
    /**
     * Initialize multiple language pairs simultaneously
     * @param languagePairs Vector of source-target language pairs
     * @return true if all pairs initialized successfully
     */
    bool initializeMultipleLanguagePairs(const std::vector<std::pair<std::string, std::string>>& languagePairs);
    
    /**
     * Switch to a different language pair for translation
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return true if switch successful
     */
    bool switchLanguagePair(const std::string& sourceLang, const std::string& targetLang);
    
    /**
     * Translate text with explicit language pair (without switching default)
     * @param text Text to translate
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return Translation result
     */
    TranslationResult translateWithLanguagePair(const std::string& text, const std::string& sourceLang, const std::string& targetLang);
    
    /**
     * Translate text asynchronously with explicit language pair
     * @param text Text to translate
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return Future containing translation result
     */
    std::future<TranslationResult> translateWithLanguagePairAsync(const std::string& text, const std::string& sourceLang, const std::string& targetLang);
    
    /**
     * Get all currently loaded language pairs
     * @return Vector of loaded language pairs
     */
    std::vector<std::pair<std::string, std::string>> getLoadedLanguagePairs() const;
    
    /**
     * Validate language pair and provide detailed error information
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return Validation result with error details
     */
    struct LanguagePairValidationResult {
        bool isValid;
        bool sourceSupported;
        bool targetSupported;
        bool modelAvailable;
        std::string errorMessage;
        std::vector<std::string> suggestions;
        std::string downloadRecommendation;
    };
    LanguagePairValidationResult validateLanguagePairDetailed(const std::string& sourceLang, const std::string& targetLang) const;
    
    /**
     * Get bidirectional language pair support information
     * @param lang1 First language code
     * @param lang2 Second language code
     * @return Information about bidirectional support
     */
    struct BidirectionalSupportInfo {
        bool lang1ToLang2Supported;
        bool lang2ToLang1Supported;
        bool bothDirectionsAvailable;
        std::string lang1ToLang2ModelPath;
        std::string lang2ToLang1ModelPath;
        std::vector<std::string> missingModels;
    };
    BidirectionalSupportInfo getBidirectionalSupportInfo(const std::string& lang1, const std::string& lang2) const;
    
    /**
     * Preload multiple language pairs for faster switching
     * @param languagePairs Vector of language pairs to preload
     * @param maxConcurrentModels Maximum number of models to keep loaded
     * @return Number of successfully preloaded pairs
     */
    size_t preloadLanguagePairs(const std::vector<std::pair<std::string, std::string>>& languagePairs, size_t maxConcurrentModels = 5);
    
    /**
     * Get model download recommendations for missing language pairs
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return Download recommendation information
     */
    struct ModelDownloadRecommendation {
        bool modelAvailable;
        std::string modelName;
        std::string downloadUrl;
        std::string modelSize;
        std::string description;
        std::vector<std::string> alternativeLanguagePairs;
    };
    ModelDownloadRecommendation getModelDownloadRecommendation(const std::string& sourceLang, const std::string& targetLang) const;
    
    /**
     * Get statistics about loaded models and language pairs
     * @return Model usage statistics
     */
    struct ModelStatistics {
        size_t totalLoadedModels;
        size_t totalSupportedPairs;
        size_t gpuModels;
        size_t cpuModels;
        size_t totalMemoryUsageMB;
        std::vector<std::pair<std::string, std::string>> mostUsedPairs;
        std::vector<std::pair<std::string, std::string>> leastUsedPairs;
    };
    ModelStatistics getModelStatistics() const;
    
    // Error handling and recovery methods
    
    /**
     * Check if the translator is operating in degraded mode
     * @return true if in degraded mode
     */
    bool isInDegradedMode() const;
    
    /**
     * Get error handling statistics
     * @return Error statistics from the error handler
     */
    ErrorStatistics getErrorStatistics() const;
    
    /**
     * Force exit from degraded mode (admin operation)
     * @return true if successfully exited degraded mode
     */
    bool forceExitDegradedMode();
    
    /**
     * Get degraded mode status information
     * @return Degraded mode status details
     */
    MarianErrorHandler::DegradedModeStatus getDegradedModeStatus() const;

private:
    struct ModelInfo {
        std::string modelPath;
        std::string vocabPath;
        std::string configPath;
        bool loaded;
        bool gpuEnabled;
        int gpuDeviceId;
        void* marianModel; // Opaque pointer to Marian model
        void* gpuMemoryPtr; // GPU memory allocation for model
        size_t gpuMemorySizeMB; // Size of GPU memory allocation
        
        ModelInfo() : loaded(false), gpuEnabled(false), gpuDeviceId(-1), 
                     marianModel(nullptr), gpuMemoryPtr(nullptr), gpuMemorySizeMB(0) {}
    };
    
    struct StreamingSession {
        std::string sessionId;
        std::string sourceLang;
        std::string targetLang;
        std::string accumulatedText;
        std::string contextBuffer;
        std::vector<std::string> textChunks;
        std::vector<TranslationResult> partialResults;
        std::chrono::steady_clock::time_point lastActivity;
        bool isActive;
        
        StreamingSession() : isActive(false) {}
        StreamingSession(const std::string& id, const std::string& src, const std::string& tgt)
            : sessionId(id), sourceLang(src), targetLang(tgt), isActive(true) {
            lastActivity = std::chrono::steady_clock::now();
        }
    };
    
    struct CacheEntry {
        std::string translatedText;
        float confidence;
        std::chrono::steady_clock::time_point timestamp;
        size_t accessCount;
        
        CacheEntry() : confidence(0.0f), accessCount(0) {}
        CacheEntry(const std::string& text, float conf)
            : translatedText(text), confidence(conf), accessCount(1) {
            timestamp = std::chrono::steady_clock::now();
        }
    };
    
    struct CacheStats {
        size_t totalRequests;
        size_t cacheHits;
        size_t cacheMisses;
        
        CacheStats() : totalRequests(0), cacheHits(0), cacheMisses(0) {}
        
        float getHitRate() const {
            return totalRequests > 0 ? (static_cast<float>(cacheHits) / totalRequests) * 100.0f : 0.0f;
        }
    };
    
    // Private methods
    std::string getLanguagePairKey(const std::string& sourceLang, const std::string& targetLang) const;
    std::string getModelPath(const std::string& sourceLang, const std::string& targetLang) const;
    bool validateModelFiles(const std::string& sourceLang, const std::string& targetLang) const;
    TranslationResult performTranslation(const std::string& text, const std::string& sourceLang, const std::string& targetLang);
    TranslationResult performMarianTranslation(const std::string& text, const std::string& sourceLang, const std::string& targetLang);
    TranslationResult performMarianTranslationWithTimeout(const std::string& text, const std::string& sourceLang, const std::string& targetLang, std::chrono::milliseconds timeout);
    TranslationResult performFallbackTranslation(const std::string& text, const std::string& sourceLang, const std::string& targetLang);
    std::string performSimpleTranslation(const std::string& text, const std::string& sourceLang, const std::string& targetLang);
    bool initializeMarianModel(const std::string& sourceLang, const std::string& targetLang);
    void cleanupMarianModel(const std::string& sourceLang, const std::string& targetLang);
    float calculateActualConfidence(const std::string& sourceText, const std::string& translatedText, const std::vector<float>& scores);
    void initializeSupportedLanguages();
    
    // GPU-specific methods
    bool initializeGPUResources();
    void cleanupGPUResources();
    bool allocateGPUMemoryForModel(const std::string& sourceLang, const std::string& targetLang, size_t requiredMB);
    void freeGPUMemoryForModel(const std::string& sourceLang, const std::string& targetLang);
    bool loadModelToGPU(const std::string& sourceLang, const std::string& targetLang);
    bool fallbackToCPU(const std::string& reason);
    size_t estimateModelMemoryRequirement(const std::string& sourceLang, const std::string& targetLang) const;
    
    // Member variables
    std::string modelsPath_;
    std::string currentSourceLang_;
    std::string currentTargetLang_;
    bool initialized_;
    
    // Model management
    std::unique_ptr<models::ModelManager> modelManager_;
    mutable std::mutex modelsMutex_;
    
    // Supported languages
    std::vector<std::string> supportedSourceLanguages_;
    std::unordered_map<std::string, std::vector<std::string>> supportedTargetLanguages_;
    
    // Thread safety
    mutable std::mutex translationMutex_;
    
    // GPU configuration and management
    bool gpuAccelerationEnabled_;
    int defaultGpuDeviceId_;
    bool gpuInitialized_;
    std::string gpuInitializationError_;
    
    // GPU resources
    utils::GPUManager* gpuManager_;
    std::unique_ptr<utils::GPUMemoryPool> gpuMemoryPool_;
    
    // Model-specific GPU memory tracking
    std::unordered_map<std::string, ModelInfo> modelInfoMap_;
    
    // Quality assessment
    std::unique_ptr<QualityManager> qualityManager_;
    
    // Error handling and recovery
    std::unique_ptr<MarianErrorHandler> errorHandler_;
    
    // Configuration
    std::shared_ptr<const MTConfig> config_;
    mutable std::mutex configMutex_;
    
    // Batch processing
    size_t maxBatchSize_;
    
    // Streaming translation sessions
    std::unordered_map<std::string, StreamingSession> streamingSessions_;
    mutable std::mutex streamingMutex_;
    std::chrono::minutes sessionTimeout_;
    
    // Translation caching
    bool cachingEnabled_;
    size_t maxCacheSize_;
    std::unordered_map<std::string, CacheEntry> translationCache_;
    mutable std::mutex cacheMutex_;
    CacheStats cacheStats_;
    
    // Multi-language pair support
    std::vector<std::pair<std::string, std::string>> loadedLanguagePairs_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> modelLastUsed_;
    std::unordered_map<std::string, size_t> modelUsageCount_;
    size_t maxConcurrentModels_;
    mutable std::mutex languagePairMutex_;
    
    // Language pair mappings and metadata
    std::unordered_map<std::string, std::vector<std::string>> availableLanguagePairs_;
    std::unordered_map<std::string, ModelDownloadRecommendation> modelDownloadInfo_;
    std::unordered_map<std::string, std::string> languageNameMappings_;
    std::vector<std::string> allSupportedLanguages_;
    
    // Private helper methods for batch processing
    std::vector<TranslationResult> processBatch(const std::vector<std::string>& texts);
    void optimizeBatchOrder(std::vector<std::pair<size_t, std::string>>& indexedTexts);
    
    // Private helper methods for streaming
    void cleanupExpiredSessions();
    std::string generateCacheKey(const std::string& text, const std::string& sourceLang, const std::string& targetLang) const;
    bool getCachedTranslation(const std::string& cacheKey, TranslationResult& result);
    void cacheTranslation(const std::string& cacheKey, const TranslationResult& result);
    void evictOldestCacheEntries();
    std::string preserveContext(const std::string& previousText, const std::string& newText);
    TranslationResult translateWithContext(const std::string& text, const std::string& context, const std::string& sourceLang, const std::string& targetLang);
    
    // Multi-language pair support helpers
    bool loadLanguagePairModel(const std::string& sourceLang, const std::string& targetLang);
    void unloadLeastRecentlyUsedModel();
    void updateModelUsageStatistics(const std::string& sourceLang, const std::string& targetLang);
    std::vector<std::string> getSuggestedAlternativeLanguages(const std::string& language) const;
    bool isLanguagePairModelAvailable(const std::string& sourceLang, const std::string& targetLang) const;
    std::string getModelDownloadUrl(const std::string& sourceLang, const std::string& targetLang) const;
    size_t estimateModelSize(const std::string& sourceLang, const std::string& targetLang) const;
    void initializeLanguagePairMappings();
    bool validateLanguageCode(const std::string& languageCode) const;
};

} // namespace mt
} // namespace speechrnt