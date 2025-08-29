#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <mutex>
#include <functional>

namespace speechrnt {
namespace mt {

/**
 * Configuration structure for Marian NMT models
 */
struct MarianModelConfig {
    std::string modelPath;
    std::string vocabPath;
    std::string configPath;
    std::string modelType;          // "transformer", "rnn", etc.
    std::string domain;             // "general", "medical", "legal", etc.
    float accuracy;                 // Expected accuracy score (0.0-1.0)
    size_t estimatedSizeMB;        // Estimated model size in MB
    bool quantized;                // Whether model is quantized
    std::string quantizationType;   // "int8", "int16", "fp16", etc.
    
    MarianModelConfig() 
        : accuracy(0.8f), estimatedSizeMB(200), quantized(false) {}
};

/**
 * Configuration structure for GPU acceleration
 */
struct GPUConfig {
    bool enabled;
    bool fallbackToCPU;
    int defaultDeviceId;
    std::vector<int> allowedDeviceIds;
    size_t memoryPoolSizeMB;
    size_t maxModelMemoryMB;
    float memoryReservationRatio;   // Ratio of GPU memory to reserve (0.0-1.0)
    
    GPUConfig() 
        : enabled(true), fallbackToCPU(true), defaultDeviceId(0), 
          memoryPoolSizeMB(1024), maxModelMemoryMB(2048), memoryReservationRatio(0.8f) {}
};

/**
 * Configuration structure for translation quality assessment
 */
struct QualityConfig {
    bool enabled;
    float highQualityThreshold;
    float mediumQualityThreshold;
    float lowQualityThreshold;
    bool generateAlternatives;
    int maxAlternatives;
    bool enableFallbackTranslation;
    
    QualityConfig() 
        : enabled(true), highQualityThreshold(0.8f), mediumQualityThreshold(0.6f), 
          lowQualityThreshold(0.4f), generateAlternatives(true), maxAlternatives(3), 
          enableFallbackTranslation(true) {}
};

/**
 * Configuration structure for translation caching
 */
struct CachingConfig {
    bool enabled;
    size_t maxCacheSize;
    std::chrono::minutes cacheExpirationTime;
    bool persistToDisk;
    std::string cacheDirectory;
    
    CachingConfig() 
        : enabled(true), maxCacheSize(1000), cacheExpirationTime(std::chrono::minutes(60)), 
          persistToDisk(false), cacheDirectory("cache/translations") {}
};

/**
 * Configuration structure for batch processing
 */
struct BatchConfig {
    size_t maxBatchSize;
    std::chrono::milliseconds batchTimeout;
    bool enableBatchOptimization;
    size_t optimalBatchSize;
    
    BatchConfig() 
        : maxBatchSize(32), batchTimeout(std::chrono::milliseconds(5000)), 
          enableBatchOptimization(true), optimalBatchSize(8) {}
};

/**
 * Configuration structure for streaming translation
 */
struct StreamingConfig {
    bool enabled;
    std::chrono::minutes sessionTimeout;
    size_t maxConcurrentSessions;
    size_t maxContextLength;
    bool enableContextPreservation;
    
    StreamingConfig() 
        : enabled(true), sessionTimeout(std::chrono::minutes(30)), 
          maxConcurrentSessions(100), maxContextLength(1000), enableContextPreservation(true) {}
};

/**
 * Configuration structure for error handling and recovery
 */
struct ErrorHandlingConfig {
    bool enableRetry;
    int maxRetryAttempts;
    std::chrono::milliseconds initialRetryDelay;
    float retryBackoffMultiplier;
    std::chrono::milliseconds maxRetryDelay;
    std::chrono::milliseconds translationTimeout;
    bool enableDegradedMode;
    bool enableFallbackTranslation;
    
    ErrorHandlingConfig() 
        : enableRetry(true), maxRetryAttempts(3), initialRetryDelay(std::chrono::milliseconds(100)), 
          retryBackoffMultiplier(2.0f), maxRetryDelay(std::chrono::milliseconds(10000)), 
          translationTimeout(std::chrono::milliseconds(5000)), enableDegradedMode(true), 
          enableFallbackTranslation(true) {}
};

/**
 * Configuration structure for performance monitoring
 */
struct PerformanceConfig {
    bool enabled;
    std::chrono::seconds metricsCollectionInterval;
    bool enableLatencyTracking;
    bool enableThroughputTracking;
    bool enableResourceUsageTracking;
    size_t maxMetricsHistorySize;
    
    PerformanceConfig() 
        : enabled(true), metricsCollectionInterval(std::chrono::seconds(30)), 
          enableLatencyTracking(true), enableThroughputTracking(true), 
          enableResourceUsageTracking(true), maxMetricsHistorySize(1000) {}
};

/**
 * Configuration structure for language detection
 */
struct LanguageDetectionConfig {
    bool enabled;
    float confidenceThreshold;
    std::string detectionMethod;    // "whisper", "text_analysis", "hybrid"
    std::vector<std::string> supportedLanguages;
    std::unordered_map<std::string, std::string> fallbackLanguages;
    bool enableHybridDetection;
    float hybridWeightText;
    float hybridWeightAudio;
    
    LanguageDetectionConfig() 
        : enabled(true), confidenceThreshold(0.7f), detectionMethod("hybrid"), 
          enableHybridDetection(true), hybridWeightText(0.6f), hybridWeightAudio(0.4f) {}
};

/**
 * Main MT configuration class
 */
class MTConfig {
public:
    MTConfig();
    ~MTConfig() = default;
    
    // Configuration loading and saving
    bool loadFromFile(const std::string& configPath);
    bool saveToFile(const std::string& configPath) const;
    bool loadFromJson(const std::string& jsonContent);
    std::string toJson() const;
    
    // Runtime configuration updates
    bool updateConfiguration(const std::string& jsonUpdates);
    bool updateModelConfig(const std::string& sourceLang, const std::string& targetLang, const MarianModelConfig& config);
    bool updateGPUConfig(const GPUConfig& config);
    bool updateQualityConfig(const QualityConfig& config);
    bool updateCachingConfig(const CachingConfig& config);
    bool updateBatchConfig(const BatchConfig& config);
    bool updateStreamingConfig(const StreamingConfig& config);
    bool updateErrorHandlingConfig(const ErrorHandlingConfig& config);
    bool updatePerformanceConfig(const PerformanceConfig& config);
    bool updateLanguageDetectionConfig(const LanguageDetectionConfig& config);
    
    // Configuration validation
    bool validate() const;
    std::vector<std::string> getValidationErrors() const;
    
    // Environment-specific configuration
    void setEnvironment(const std::string& environment);
    std::string getEnvironment() const;
    bool loadEnvironmentOverrides(const std::string& environment);
    
    // Custom model paths and locations
    void setModelsBasePath(const std::string& basePath);
    std::string getModelsBasePath() const;
    void setCustomModelPath(const std::string& sourceLang, const std::string& targetLang, const std::string& modelPath);
    std::string getModelPath(const std::string& sourceLang, const std::string& targetLang) const;
    bool hasCustomModelPath(const std::string& sourceLang, const std::string& targetLang) const;
    
    // Language pair configuration
    void addLanguagePair(const std::string& sourceLang, const std::string& targetLang, const MarianModelConfig& config);
    void removeLanguagePair(const std::string& sourceLang, const std::string& targetLang);
    bool hasLanguagePair(const std::string& sourceLang, const std::string& targetLang) const;
    MarianModelConfig getModelConfig(const std::string& sourceLang, const std::string& targetLang) const;
    std::vector<std::pair<std::string, std::string>> getAvailableLanguagePairs() const;
    
    // Supported languages
    std::vector<std::string> getSupportedSourceLanguages() const;
    std::vector<std::string> getSupportedTargetLanguages(const std::string& sourceLang) const;
    
    // Configuration change notifications
    using ConfigChangeCallback = std::function<void(const std::string& section, const std::string& key)>;
    void registerConfigChangeCallback(const std::string& callbackId, ConfigChangeCallback callback);
    void unregisterConfigChangeCallback(const std::string& callbackId);
    
    // Getters for configuration sections
    const GPUConfig& getGPUConfig() const { return gpuConfig_; }
    const QualityConfig& getQualityConfig() const { return qualityConfig_; }
    const CachingConfig& getCachingConfig() const { return cachingConfig_; }
    const BatchConfig& getBatchConfig() const { return batchConfig_; }
    const StreamingConfig& getStreamingConfig() const { return streamingConfig_; }
    const ErrorHandlingConfig& getErrorHandlingConfig() const { return errorHandlingConfig_; }
    const PerformanceConfig& getPerformanceConfig() const { return performanceConfig_; }
    const LanguageDetectionConfig& getLanguageDetectionConfig() const { return languageDetectionConfig_; }
    
    // Configuration metadata
    std::string getConfigVersion() const { return configVersion_; }
    std::chrono::system_clock::time_point getLastModified() const { return lastModified_; }
    std::string getConfigSource() const { return configSource_; }
    
    // Thread-safe configuration access
    std::shared_ptr<const MTConfig> getSnapshot() const;
    
private:
    // Configuration sections
    GPUConfig gpuConfig_;
    QualityConfig qualityConfig_;
    CachingConfig cachingConfig_;
    BatchConfig batchConfig_;
    StreamingConfig streamingConfig_;
    ErrorHandlingConfig errorHandlingConfig_;
    PerformanceConfig performanceConfig_;
    LanguageDetectionConfig languageDetectionConfig_;
    
    // Model configurations
    std::unordered_map<std::string, MarianModelConfig> modelConfigs_;
    std::unordered_map<std::string, std::string> customModelPaths_;
    
    // Configuration metadata
    std::string configVersion_;
    std::chrono::system_clock::time_point lastModified_;
    std::string configSource_;
    std::string environment_;
    std::string modelsBasePath_;
    
    // Change notification system
    std::unordered_map<std::string, ConfigChangeCallback> changeCallbacks_;
    mutable std::mutex configMutex_;
    
    // Private helper methods
    std::string getLanguagePairKey(const std::string& sourceLang, const std::string& targetLang) const;
    void notifyConfigChange(const std::string& section, const std::string& key);
    bool validateGPUConfig() const;
    bool validateQualityConfig() const;
    bool validateCachingConfig() const;
    bool validateBatchConfig() const;
    bool validateStreamingConfig() const;
    bool validateErrorHandlingConfig() const;
    bool validatePerformanceConfig() const;
    bool validateLanguageDetectionConfig() const;
    bool validateModelConfigs() const;
    
    // JSON serialization helpers
    void serializeGPUConfig(std::string& json) const;
    void serializeQualityConfig(std::string& json) const;
    void serializeCachingConfig(std::string& json) const;
    void serializeBatchConfig(std::string& json) const;
    void serializeStreamingConfig(std::string& json) const;
    void serializeErrorHandlingConfig(std::string& json) const;
    void serializePerformanceConfig(std::string& json) const;
    void serializeLanguageDetectionConfig(std::string& json) const;
    void serializeModelConfigs(std::string& json) const;
    
    // JSON deserialization helpers
    bool deserializeGPUConfig(const std::string& json);
    bool deserializeQualityConfig(const std::string& json);
    bool deserializeCachingConfig(const std::string& json);
    bool deserializeBatchConfig(const std::string& json);
    bool deserializeStreamingConfig(const std::string& json);
    bool deserializeErrorHandlingConfig(const std::string& json);
    bool deserializePerformanceConfig(const std::string& json);
    bool deserializeLanguageDetectionConfig(const std::string& json);
    bool deserializeModelConfigs(const std::string& json);
    
    // Environment override helpers
    void applyEnvironmentOverrides(const std::string& environment);
    std::string getEnvironmentConfigPath(const std::string& environment) const;
};

/**
 * Global MT configuration manager
 */
class MTConfigManager {
public:
    static MTConfigManager& getInstance();
    
    // Configuration management
    bool initialize(const std::string& configPath);
    bool reload();
    void shutdown();
    
    // Configuration access
    std::shared_ptr<const MTConfig> getConfig() const;
    bool updateConfig(const std::string& jsonUpdates);
    
    // Environment management
    void setEnvironment(const std::string& environment);
    std::string getCurrentEnvironment() const;
    
    // Configuration file watching
    void enableConfigFileWatching(bool enabled);
    bool isConfigFileWatchingEnabled() const;
    
private:
    MTConfigManager() = default;
    ~MTConfigManager() = default;
    
    MTConfigManager(const MTConfigManager&) = delete;
    MTConfigManager& operator=(const MTConfigManager&) = delete;
    
    std::shared_ptr<MTConfig> config_;
    std::string configPath_;
    std::string currentEnvironment_;
    bool configFileWatchingEnabled_;
    mutable std::mutex managerMutex_;
    
    // File watching implementation
    void startConfigFileWatcher();
    void stopConfigFileWatcher();
    void onConfigFileChanged();
    
    std::unique_ptr<std::thread> watcherThread_;
    std::atomic<bool> watcherRunning_;
};

} // namespace mt
} // namespace speechrnt