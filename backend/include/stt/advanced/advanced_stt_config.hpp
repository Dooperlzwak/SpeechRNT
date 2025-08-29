#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <mutex>
#include <chrono>

namespace stt {
namespace advanced {

/**
 * Advanced feature enumeration
 */
enum class AdvancedFeature {
    SPEAKER_DIARIZATION,
    AUDIO_PREPROCESSING,
    CONTEXTUAL_TRANSCRIPTION,
    REALTIME_ANALYSIS,
    ADAPTIVE_QUALITY,
    EXTERNAL_SERVICES,
    BATCH_PROCESSING,
    EMOTION_DETECTION,
    MODEL_MANAGEMENT,
    DEBUGGING_DIAGNOSTICS,
    CUSTOM_AUDIO_FORMATS,
    MONITORING_ANALYTICS
};

/**
 * Quality levels for adaptive processing
 */
enum class QualityLevel {
    ULTRA_LOW,    // Fastest, lowest quality
    LOW,          // Fast, reduced quality
    MEDIUM,       // Balanced
    HIGH,         // Slower, better quality
    ULTRA_HIGH    // Slowest, best quality
};

/**
 * Audio preprocessing types
 */
enum class PreprocessingType {
    NOISE_REDUCTION,
    VOLUME_NORMALIZATION,
    ECHO_CANCELLATION,
    SPECTRAL_SUBTRACTION,
    WIENER_FILTERING,
    ADAPTIVE_FILTERING
};

/**
 * Generic feature configuration base class
 */
struct FeatureConfig {
    bool enabled = false;
    std::unordered_map<std::string, std::string> parameters;
    
    virtual ~FeatureConfig() = default;
    
    // Helper methods for parameter access
    bool getBoolParameter(const std::string& key, bool defaultValue = false) const;
    int getIntParameter(const std::string& key, int defaultValue = 0) const;
    float getFloatParameter(const std::string& key, float defaultValue = 0.0f) const;
    std::string getStringParameter(const std::string& key, const std::string& defaultValue = "") const;
    
    void setBoolParameter(const std::string& key, bool value);
    void setIntParameter(const std::string& key, int value);
    void setFloatParameter(const std::string& key, float value);
    void setStringParameter(const std::string& key, const std::string& value);
};

/**
 * Speaker diarization configuration
 */
struct SpeakerDiarizationConfig : public FeatureConfig {
    std::string modelPath;
    size_t maxSpeakers = 10;
    float speakerChangeThreshold = 0.7f;
    bool enableSpeakerProfiles = false;
    bool enableRealTimeProcessing = true;
    float embeddingThreshold = 0.8f;
    
    SpeakerDiarizationConfig() {
        enabled = false;
        setStringParameter("modelPath", "data/speaker_models/");
        setIntParameter("maxSpeakers", 10);
        setFloatParameter("speakerChangeThreshold", 0.7f);
        setBoolParameter("enableSpeakerProfiles", false);
        setBoolParameter("enableRealTimeProcessing", true);
        setFloatParameter("embeddingThreshold", 0.8f);
    }
};

/**
 * Audio preprocessing configuration
 */
struct AudioPreprocessingConfig : public FeatureConfig {
    bool enableNoiseReduction = true;
    bool enableVolumeNormalization = true;
    bool enableEchoCancellation = false;
    float noiseReductionStrength = 0.5f;
    bool adaptivePreprocessing = true;
    std::vector<PreprocessingType> enabledFilters;
    
    AudioPreprocessingConfig() {
        enabled = true;
        setBoolParameter("enableNoiseReduction", true);
        setBoolParameter("enableVolumeNormalization", true);
        setBoolParameter("enableEchoCancellation", false);
        setFloatParameter("noiseReductionStrength", 0.5f);
        setBoolParameter("adaptivePreprocessing", true);
        
        enabledFilters = {
            PreprocessingType::NOISE_REDUCTION,
            PreprocessingType::VOLUME_NORMALIZATION
        };
    }
};

/**
 * Contextual transcription configuration
 */
struct ContextualTranscriptionConfig : public FeatureConfig {
    std::string modelsPath;
    std::vector<std::string> enabledDomains;
    bool enableDomainDetection = true;
    float contextualWeight = 0.3f;
    size_t maxContextHistory = 10;
    bool enableCustomVocabulary = true;
    
    ContextualTranscriptionConfig() {
        enabled = false;
        setStringParameter("modelsPath", "data/contextual_models/");
        setBoolParameter("enableDomainDetection", true);
        setFloatParameter("contextualWeight", 0.3f);
        setIntParameter("maxContextHistory", 10);
        setBoolParameter("enableCustomVocabulary", true);
        
        enabledDomains = {"general", "technical", "medical", "legal"};
    }
};

/**
 * Real-time analysis configuration
 */
struct RealTimeAnalysisConfig : public FeatureConfig {
    size_t analysisBufferSize = 1024;
    float metricsUpdateIntervalMs = 50.0f;
    bool enableSpectralAnalysis = true;
    bool enableAudioEffects = false;
    bool enableLevelMetering = true;
    bool enableNoiseEstimation = true;
    
    RealTimeAnalysisConfig() {
        enabled = true;
        setIntParameter("analysisBufferSize", 1024);
        setFloatParameter("metricsUpdateIntervalMs", 50.0f);
        setBoolParameter("enableSpectralAnalysis", true);
        setBoolParameter("enableAudioEffects", false);
        setBoolParameter("enableLevelMetering", true);
        setBoolParameter("enableNoiseEstimation", true);
    }
};

/**
 * Adaptive quality configuration
 */
struct AdaptiveQualityConfig : public FeatureConfig {
    bool enableAdaptation = true;
    float cpuThreshold = 0.8f;
    float memoryThreshold = 0.8f;
    QualityLevel defaultQuality = QualityLevel::MEDIUM;
    float adaptationIntervalMs = 1000.0f;
    bool enablePredictiveScaling = true;
    
    AdaptiveQualityConfig() {
        enabled = true;
        setBoolParameter("enableAdaptation", true);
        setFloatParameter("cpuThreshold", 0.8f);
        setFloatParameter("memoryThreshold", 0.8f);
        setIntParameter("defaultQuality", static_cast<int>(QualityLevel::MEDIUM));
        setFloatParameter("adaptationIntervalMs", 1000.0f);
        setBoolParameter("enablePredictiveScaling", true);
    }
};

/**
 * External services configuration
 */
struct ExternalServicesConfig : public FeatureConfig {
    std::vector<std::string> enabledServices;
    bool enableResultFusion = true;
    float fallbackThreshold = 0.5f;
    std::unordered_map<std::string, std::string> serviceConfigs;
    bool enablePrivacyMode = true;
    
    ExternalServicesConfig() {
        enabled = false;
        setBoolParameter("enableResultFusion", true);
        setFloatParameter("fallbackThreshold", 0.5f);
        setBoolParameter("enablePrivacyMode", true);
    }
};

/**
 * Batch processing configuration
 */
struct BatchProcessingConfig : public FeatureConfig {
    size_t maxConcurrentJobs = 4;
    size_t chunkSizeSeconds = 30;
    bool enableParallelProcessing = true;
    std::string outputFormat = "json";
    bool enableProgressTracking = true;
    
    BatchProcessingConfig() {
        enabled = true;
        setIntParameter("maxConcurrentJobs", 4);
        setIntParameter("chunkSizeSeconds", 30);
        setBoolParameter("enableParallelProcessing", true);
        setStringParameter("outputFormat", "json");
        setBoolParameter("enableProgressTracking", true);
    }
};

/**
 * Main advanced STT configuration
 */
struct AdvancedSTTConfig {
    // Feature configurations
    SpeakerDiarizationConfig speakerDiarization;
    AudioPreprocessingConfig audioPreprocessing;
    ContextualTranscriptionConfig contextualTranscription;
    RealTimeAnalysisConfig realTimeAnalysis;
    AdaptiveQualityConfig adaptiveQuality;
    ExternalServicesConfig externalServices;
    BatchProcessingConfig batchProcessing;
    
    // Global settings
    bool enableAdvancedFeatures = true;
    std::string configVersion = "1.0";
    bool enableDebugMode = false;
    bool enableMetricsCollection = true;
    std::string logLevel = "INFO";
    
    // Performance settings
    size_t maxConcurrentProcessing = 8;
    float maxMemoryUsageMB = 8192.0f;
    float maxProcessingLatencyMs = 5000.0f;
    
    AdvancedSTTConfig() = default;
    
    /**
     * Get feature configuration by type
     */
    FeatureConfig* getFeatureConfig(AdvancedFeature feature);
    const FeatureConfig* getFeatureConfig(AdvancedFeature feature) const;
    
    /**
     * Validate configuration
     */
    bool isValid() const;
    
    /**
     * Get validation errors
     */
    std::vector<std::string> getValidationErrors() const;
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
    AdvancedFeature feature;
    std::string section;
    std::string key;
    std::string oldValue;
    std::string newValue;
    std::chrono::steady_clock::time_point timestamp;
    
    ConfigChangeNotification(AdvancedFeature feat, const std::string& sec, const std::string& k, 
                           const std::string& oldVal, const std::string& newVal)
        : feature(feat), section(sec), key(k), oldValue(oldVal), newValue(newVal)
        , timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * Advanced STT Configuration Manager
 * Handles loading, validation, and runtime updates of advanced STT configuration
 */
class AdvancedSTTConfigManager {
public:
    using ConfigChangeCallback = std::function<void(const ConfigChangeNotification&)>;
    
    AdvancedSTTConfigManager();
    ~AdvancedSTTConfigManager() = default;
    
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
     * @return Current advanced STT configuration
     */
    AdvancedSTTConfig getConfig() const;
    
    /**
     * Update configuration
     * @param newConfig New configuration
     * @return Validation result
     */
    ConfigValidationResult updateConfig(const AdvancedSTTConfig& newConfig);
    
    /**
     * Update specific feature configuration
     * @param feature Feature to update
     * @param config New feature configuration
     * @return Validation result
     */
    ConfigValidationResult updateFeatureConfig(AdvancedFeature feature, const FeatureConfig& config);
    
    /**
     * Update specific configuration value
     * @param feature Feature to update
     * @param section Configuration section
     * @param key Configuration key
     * @param value New value as string
     * @return Validation result
     */
    ConfigValidationResult updateConfigValue(AdvancedFeature feature,
                                           const std::string& section, 
                                           const std::string& key, 
                                           const std::string& value);
    
    /**
     * Validate configuration
     * @param config Configuration to validate
     * @return Validation result
     */
    ConfigValidationResult validateConfig(const AdvancedSTTConfig& config) const;
    
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
     * Enable/disable automatic configuration saving
     * @param enable true to enable auto-save
     */
    void setAutoSave(bool enable);
    
    /**
     * Reload configuration from file
     * @return true if reloaded successfully
     */
    bool reloadFromFile();

private:
    mutable std::mutex configMutex_;
    AdvancedSTTConfig config_;
    std::string configFilePath_;
    bool isModified_;
    bool autoSave_;
    std::chrono::steady_clock::time_point lastModified_;
    std::vector<ConfigChangeCallback> changeCallbacks_;
    
    // Helper methods
    void notifyConfigChange(AdvancedFeature feature, const std::string& section, 
                          const std::string& key, const std::string& oldValue, 
                          const std::string& newValue);
    bool parseJsonConfig(const std::string& jsonStr, AdvancedSTTConfig& config) const;
    std::string configToJson(const AdvancedSTTConfig& config) const;
    ConfigValidationResult validateFeatureConfig(AdvancedFeature feature, 
                                               const FeatureConfig& config) const;
    void autoSaveIfEnabled();
    std::string featureToString(AdvancedFeature feature) const;
    AdvancedFeature stringToFeature(const std::string& featureStr) const;
};

} // namespace advanced
} // namespace stt