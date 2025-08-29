#pragma once

#include "stt/quantization_config.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <mutex>

namespace stt {

/**
 * STT-specific configuration structure
 */
struct STTConfig {
    // Model configuration
    std::string defaultModel = "base";
    std::string modelsPath = "data/whisper/";
    std::string language = "auto";
    bool translateToEnglish = false;
    
    // Language detection settings
    bool languageDetectionEnabled = true;
    float languageDetectionThreshold = 0.7f;
    bool autoLanguageSwitching = true;
    int consistentDetectionRequired = 2;
    std::vector<std::string> supportedLanguages;
    
    // Quantization settings
    QuantizationLevel quantizationLevel = QuantizationLevel::AUTO;
    bool enableGPUAcceleration = true;
    int gpuDeviceId = 0;
    float accuracyThreshold = 0.85f;
    
    // Streaming configuration
    bool partialResultsEnabled = true;
    int minChunkSizeMs = 1000;
    int maxChunkSizeMs = 10000;
    int overlapSizeMs = 200;
    bool enableIncrementalUpdates = true;
    
    // Confidence and quality settings
    float confidenceThreshold = 0.5f;
    bool wordLevelConfidenceEnabled = true;
    bool qualityIndicatorsEnabled = true;
    bool confidenceFilteringEnabled = false;
    
    // Performance settings
    int threadCount = 4;
    float temperature = 0.0f;
    int maxTokens = 0;
    bool suppressBlank = true;
    bool suppressNonSpeechTokens = true;
    
    // Audio processing settings
    int sampleRate = 16000;
    int audioBufferSizeMB = 8;
    bool enableNoiseReduction = false;
    float vadThreshold = 0.5f;
    
    // Error recovery settings
    bool enableErrorRecovery = true;
    int maxRetryAttempts = 3;
    float retryBackoffMultiplier = 2.0f;
    int retryInitialDelayMs = 100;
    
    // Health monitoring settings
    bool enableHealthMonitoring = true;
    int healthCheckIntervalMs = 30000;
    float maxLatencyMs = 2000.0f;
    float maxMemoryUsageMB = 4096.0f;
    
    // Text normalization settings
    struct TextNormalizationConfig {
        bool lowercase = false;
        bool removePunctuation = false;
        bool ensureEndingPunctuation = true;
        bool trimWhitespace = true;
        bool collapseWhitespace = true;
    } normalization;
    
    STTConfig() {
        // Initialize supported languages with common ones
        supportedLanguages = {
            "en", "zh", "de", "es", "ru", "ko", "fr", "ja", "pt", "tr", "pl", "ca", "nl", 
            "ar", "sv", "it", "id", "hi", "fi", "vi", "he", "uk", "el", "ms", "cs", "ro", 
            "da", "hu", "ta", "no", "th", "ur", "hr", "bg", "lt", "la", "mi", "ml", "cy", 
            "sk", "te", "fa", "lv", "bn", "sr", "az", "sl", "kn", "et", "mk", "br", "eu", 
            "is", "hy", "ne", "mn", "bs", "kk", "sq", "sw", "gl", "mr", "pa", "si", "km", 
            "sn", "yo", "so", "af", "oc", "ka", "be", "tg", "sd", "gu", "am", "yi", "lo", 
            "uz", "fo", "ht", "ps", "tk", "nn", "mt", "sa", "lb", "my", "bo", "tl", "mg", 
            "as", "tt", "haw", "ln", "ha", "ba", "jw", "su"
        };
    }
};

/**
 * Configuration validation result
 */
struct ConfigValidationResult {
    bool isValid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    void addError(const std::string& error) {
        errors.push_back(error);
        isValid = false;
    }
    
    void addWarning(const std::string& warning) {
        warnings.push_back(warning);
    }
    
    bool hasErrors() const { return !errors.empty(); }
    bool hasWarnings() const { return !warnings.empty(); }
};

/**
 * Configuration change notification
 */
struct ConfigChangeNotification {
    std::string section;
    std::string key;
    std::string oldValue;
    std::string newValue;
    std::chrono::steady_clock::time_point timestamp;
    
    ConfigChangeNotification(const std::string& sec, const std::string& k, 
                           const std::string& oldVal, const std::string& newVal)
        : section(sec), key(k), oldValue(oldVal), newValue(newVal)
        , timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * STT Configuration Manager
 * Handles loading, validation, and runtime updates of STT configuration
 */
class STTConfigManager {
public:
    using ConfigChangeCallback = std::function<void(const ConfigChangeNotification&)>;
    
    STTConfigManager();
    ~STTConfigManager() = default;
    
    /**
     * Load configuration from file
     * @param configPath Path to configuration file
     * @return true if loaded successfully
     */
    bool loadFromFile(const std::string& configPath);
    
    /**
     * Save configuration to file
     * @param configPath Path to configuration file
     * @return true if saved successfully
     */
    bool saveToFile(const std::string& configPath) const;
    
    /**
     * Load configuration from JSON string
     * @param jsonStr JSON configuration string
     * @return true if loaded successfully
     */
    bool loadFromJson(const std::string& jsonStr);
    
    /**
     * Export configuration to JSON string
     * @return JSON configuration string
     */
    std::string exportToJson() const;
    
    /**
     * Get current configuration
     * @return Current STT configuration
     */
    STTConfig getConfig() const;
    
    /**
     * Update configuration
     * @param newConfig New configuration
     * @return Validation result
     */
    ConfigValidationResult updateConfig(const STTConfig& newConfig);
    
    /**
     * Update specific configuration value
     * @param section Configuration section (e.g., "model", "streaming", "confidence")
     * @param key Configuration key
     * @param value New value as string
     * @return Validation result
     */
    ConfigValidationResult updateConfigValue(const std::string& section, 
                                           const std::string& key, 
                                           const std::string& value);
    
    /**
     * Validate configuration
     * @param config Configuration to validate
     * @return Validation result
     */
    ConfigValidationResult validateConfig(const STTConfig& config) const;
    
    /**
     * Reset configuration to defaults
     */
    void resetToDefaults();
    
    /**
     * Register callback for configuration changes
     * @param callback Callback function
     */
    void registerChangeCallback(ConfigChangeCallback callback);
    
    /**
     * Get configuration schema for frontend
     * @return JSON schema describing configuration structure
     */
    std::string getConfigSchema() const;
    
    /**
     * Get configuration metadata (descriptions, constraints, etc.)
     * @return Configuration metadata
     */
    std::string getConfigMetadata() const;
    
    /**
     * Check if configuration has been modified since last save
     * @return true if modified
     */
    bool isModified() const;
    
    /**
     * Get last modification timestamp
     * @return Last modification time
     */
    std::chrono::steady_clock::time_point getLastModified() const;
    
    /**
     * Get configuration file path
     * @return Current configuration file path
     */
    std::string getConfigFilePath() const;
    
    /**
     * Reload configuration from file
     * @return true if reloaded successfully
     */
    bool reloadFromFile();
    
    /**
     * Enable/disable automatic configuration saving
     * @param enable true to enable auto-save
     */
    void setAutoSave(bool enable);
    
    /**
     * Get supported quantization levels for current hardware
     * @return Vector of supported quantization levels
     */
    std::vector<QuantizationLevel> getSupportedQuantizationLevels() const;
    
    /**
     * Get available Whisper models
     * @return Vector of available model names
     */
    std::vector<std::string> getAvailableModels() const;
    
    /**
     * Validate model file exists and is accessible
     * @param modelName Model name to validate
     * @return true if model is valid
     */
    bool validateModelFile(const std::string& modelName) const;

private:
    mutable std::mutex configMutex_;
    STTConfig config_;
    std::string configFilePath_;
    bool isModified_;
    bool autoSave_;
    std::chrono::steady_clock::time_point lastModified_;
    std::vector<ConfigChangeCallback> changeCallbacks_;
    std::unique_ptr<QuantizationManager> quantizationManager_;
    
    // Helper methods
    void notifyConfigChange(const std::string& section, const std::string& key, 
                          const std::string& oldValue, const std::string& newValue);
    bool parseJsonConfig(const std::string& jsonStr, STTConfig& config) const;
    std::string configToJson(const STTConfig& config) const;
    ConfigValidationResult validateModelConfig(const STTConfig& config) const;
    ConfigValidationResult validateLanguageConfig(const STTConfig& config) const;
    ConfigValidationResult validateQuantizationConfig(const STTConfig& config) const;
    ConfigValidationResult validateStreamingConfig(const STTConfig& config) const;
    ConfigValidationResult validateConfidenceConfig(const STTConfig& config) const;
    ConfigValidationResult validatePerformanceConfig(const STTConfig& config) const;
    ConfigValidationResult validateAudioConfig(const STTConfig& config) const;
    ConfigValidationResult validateErrorRecoveryConfig(const STTConfig& config) const;
    ConfigValidationResult validateHealthMonitoringConfig(const STTConfig& config) const;
    
    bool updateStringValue(const std::string& section, const std::string& key, 
                          const std::string& value, std::string& target);
    bool updateBoolValue(const std::string& section, const std::string& key, 
                        const std::string& value, bool& target);
    bool updateIntValue(const std::string& section, const std::string& key, 
                       const std::string& value, int& target);
    bool updateFloatValue(const std::string& section, const std::string& key, 
                         const std::string& value, float& target);
    bool updateQuantizationLevel(const std::string& section, const std::string& key, 
                               const std::string& value, QuantizationLevel& target);
    
    void autoSaveIfEnabled();
    std::string getModelFilePath(const std::string& modelName) const;
    bool isLanguageSupported(const std::string& language) const;
    bool isValidQuantizationLevel(QuantizationLevel level) const;
};

} // namespace stt