#include "stt/advanced/advanced_stt_config.hpp"
#include "utils/logging.hpp"
#include <fstream>
#include <sstream>

// Logging macros for convenience
#define LOG_INFO(msg) utils::Logger::info(msg)
#define LOG_WARNING(msg) utils::Logger::warn(msg)
#define LOG_ERROR(msg) utils::Logger::error(msg)
#define LOG_DEBUG(msg) utils::Logger::debug(msg)

// Simple JSON implementation for basic parsing when nlohmann/json is not available
#ifdef NLOHMANN_JSON_AVAILABLE
#include <nlohmann/json.hpp>
#else
// Basic JSON-like structure for configuration
struct SimpleJson {
    std::map<std::string, std::string> values;
    std::map<std::string, SimpleJson> objects;
    
    bool contains(const std::string& key) const {
        return values.find(key) != values.end() || objects.find(key) != objects.end();
    }
    
    template<typename T>
    T value(const std::string& key, const T& defaultValue) const {
        auto it = values.find(key);
        if (it != values.end()) {
            if constexpr (std::is_same_v<T, bool>) {
                return it->second == "true" || it->second == "1";
            } else if constexpr (std::is_same_v<T, int>) {
                return std::stoi(it->second);
            } else if constexpr (std::is_same_v<T, float>) {
                return std::stof(it->second);
            } else {
                return it->second;
            }
        }
        return defaultValue;
    }
    
    SimpleJson& operator[](const std::string& key) {
        return objects[key];
    }
    
    const SimpleJson& operator[](const std::string& key) const {
        static SimpleJson empty;
        auto it = objects.find(key);
        return it != objects.end() ? it->second : empty;
    }
    
    std::string dump(int indent = 0) const {
        std::ostringstream oss;
        oss << "{\n";
        bool first = true;
        for (const auto& [key, value] : values) {
            if (!first) oss << ",\n";
            oss << std::string(indent + 2, ' ') << "\"" << key << "\": \"" << value << "\"";
            first = false;
        }
        for (const auto& [key, obj] : objects) {
            if (!first) oss << ",\n";
            oss << std::string(indent + 2, ' ') << "\"" << key << "\": " << obj.dump(indent + 2);
            first = false;
        }
        oss << "\n" << std::string(indent, ' ') << "}";
        return oss.str();
    }
};

using json = SimpleJson;
#endif

namespace stt {
namespace advanced {

// FeatureConfig implementation

bool FeatureConfig::getBoolParameter(const std::string& key, bool defaultValue) const {
    auto it = parameters.find(key);
    if (it != parameters.end()) {
        return it->second == "true" || it->second == "1";
    }
    return defaultValue;
}

int FeatureConfig::getIntParameter(const std::string& key, int defaultValue) const {
    auto it = parameters.find(key);
    if (it != parameters.end()) {
        try {
            return std::stoi(it->second);
        } catch (const std::exception&) {
            return defaultValue;
        }
    }
    return defaultValue;
}

float FeatureConfig::getFloatParameter(const std::string& key, float defaultValue) const {
    auto it = parameters.find(key);
    if (it != parameters.end()) {
        try {
            return std::stof(it->second);
        } catch (const std::exception&) {
            return defaultValue;
        }
    }
    return defaultValue;
}

std::string FeatureConfig::getStringParameter(const std::string& key, const std::string& defaultValue) const {
    auto it = parameters.find(key);
    if (it != parameters.end()) {
        return it->second;
    }
    return defaultValue;
}

void FeatureConfig::setBoolParameter(const std::string& key, bool value) {
    parameters[key] = value ? "true" : "false";
}

void FeatureConfig::setIntParameter(const std::string& key, int value) {
    parameters[key] = std::to_string(value);
}

void FeatureConfig::setFloatParameter(const std::string& key, float value) {
    parameters[key] = std::to_string(value);
}

void FeatureConfig::setStringParameter(const std::string& key, const std::string& value) {
    parameters[key] = value;
}

// AdvancedSTTConfig implementation

FeatureConfig* AdvancedSTTConfig::getFeatureConfig(AdvancedFeature feature) {
    switch (feature) {
        case AdvancedFeature::SPEAKER_DIARIZATION:
            return &speakerDiarization;
        case AdvancedFeature::AUDIO_PREPROCESSING:
            return &audioPreprocessing;
        case AdvancedFeature::CONTEXTUAL_TRANSCRIPTION:
            return &contextualTranscription;
        case AdvancedFeature::REALTIME_ANALYSIS:
            return &realTimeAnalysis;
        case AdvancedFeature::ADAPTIVE_QUALITY:
            return &adaptiveQuality;
        case AdvancedFeature::EXTERNAL_SERVICES:
            return &externalServices;
        case AdvancedFeature::BATCH_PROCESSING:
            return &batchProcessing;
        default:
            return nullptr;
    }
}

const FeatureConfig* AdvancedSTTConfig::getFeatureConfig(AdvancedFeature feature) const {
    switch (feature) {
        case AdvancedFeature::SPEAKER_DIARIZATION:
            return &speakerDiarization;
        case AdvancedFeature::AUDIO_PREPROCESSING:
            return &audioPreprocessing;
        case AdvancedFeature::CONTEXTUAL_TRANSCRIPTION:
            return &contextualTranscription;
        case AdvancedFeature::REALTIME_ANALYSIS:
            return &realTimeAnalysis;
        case AdvancedFeature::ADAPTIVE_QUALITY:
            return &adaptiveQuality;
        case AdvancedFeature::EXTERNAL_SERVICES:
            return &externalServices;
        case AdvancedFeature::BATCH_PROCESSING:
            return &batchProcessing;
        default:
            return nullptr;
    }
}

bool AdvancedSTTConfig::isValid() const {
    auto errors = getValidationErrors();
    return errors.empty();
}

std::vector<std::string> AdvancedSTTConfig::getValidationErrors() const {
    std::vector<std::string> errors;
    
    // Validate global settings
    if (maxConcurrentProcessing == 0) {
        errors.push_back("maxConcurrentProcessing must be greater than 0");
    }
    
    if (maxMemoryUsageMB <= 0.0f) {
        errors.push_back("maxMemoryUsageMB must be greater than 0");
    }
    
    if (maxProcessingLatencyMs <= 0.0f) {
        errors.push_back("maxProcessingLatencyMs must be greater than 0");
    }
    
    // Validate feature-specific configurations
    if (speakerDiarization.enabled) {
        if (speakerDiarization.maxSpeakers == 0) {
            errors.push_back("Speaker diarization maxSpeakers must be greater than 0");
        }
        if (speakerDiarization.speakerChangeThreshold < 0.0f || speakerDiarization.speakerChangeThreshold > 1.0f) {
            errors.push_back("Speaker diarization speakerChangeThreshold must be between 0.0 and 1.0");
        }
    }
    
    if (realTimeAnalysis.enabled) {
        if (realTimeAnalysis.analysisBufferSize == 0) {
            errors.push_back("Real-time analysis analysisBufferSize must be greater than 0");
        }
        if (realTimeAnalysis.metricsUpdateIntervalMs <= 0.0f) {
            errors.push_back("Real-time analysis metricsUpdateIntervalMs must be greater than 0");
        }
    }
    
    if (adaptiveQuality.enabled) {
        if (adaptiveQuality.cpuThreshold < 0.0f || adaptiveQuality.cpuThreshold > 1.0f) {
            errors.push_back("Adaptive quality cpuThreshold must be between 0.0 and 1.0");
        }
        if (adaptiveQuality.memoryThreshold < 0.0f || adaptiveQuality.memoryThreshold > 1.0f) {
            errors.push_back("Adaptive quality memoryThreshold must be between 0.0 and 1.0");
        }
    }
    
    if (batchProcessing.enabled) {
        if (batchProcessing.maxConcurrentJobs == 0) {
            errors.push_back("Batch processing maxConcurrentJobs must be greater than 0");
        }
        if (batchProcessing.chunkSizeSeconds == 0) {
            errors.push_back("Batch processing chunkSizeSeconds must be greater than 0");
        }
    }
    
    return errors;
}

// AdvancedSTTConfigManager implementation

AdvancedSTTConfigManager::AdvancedSTTConfigManager()
    : isModified_(false)
    , autoSave_(false)
    , lastModified_(std::chrono::steady_clock::now()) {
}

bool AdvancedSTTConfigManager::loadFromFile(const std::string& configPath) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open config file: " + configPath);
            return false;
        }
        
        std::string jsonStr((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        file.close();
        
        if (!parseJsonConfig(jsonStr, config_)) {
            LOG_ERROR("Failed to parse config file: " + configPath);
            return false;
        }
        
        configFilePath_ = configPath;
        isModified_ = false;
        lastModified_ = std::chrono::steady_clock::now();
        
        LOG_INFO("Advanced STT configuration loaded from: " + configPath);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception loading config file: " + std::string(e.what()));
        return false;
    }
}

bool AdvancedSTTConfigManager::saveToFile(const std::string& configPath) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    try {
        std::string jsonStr = configToJson(config_);
        
        std::ofstream file(configPath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open config file for writing: " + configPath);
            return false;
        }
        
        file << jsonStr;
        file.close();
        
        LOG_INFO("Advanced STT configuration saved to: " + configPath);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception saving config file: " + std::string(e.what()));
        return false;
    }
}

bool AdvancedSTTConfigManager::loadFromJson(const std::string& jsonStr) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    AdvancedSTTConfig newConfig;
    if (!parseJsonConfig(jsonStr, newConfig)) {
        return false;
    }
    
    config_ = newConfig;
    isModified_ = true;
    lastModified_ = std::chrono::steady_clock::now();
    
    autoSaveIfEnabled();
    return true;
}

std::string AdvancedSTTConfigManager::exportToJson() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return configToJson(config_);
}

AdvancedSTTConfig AdvancedSTTConfigManager::getConfig() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return config_;
}

ConfigValidationResult AdvancedSTTConfigManager::updateConfig(const AdvancedSTTConfig& newConfig) {
    ConfigValidationResult result = validateConfig(newConfig);
    
    if (result.isValid) {
        std::lock_guard<std::mutex> lock(configMutex_);
        
        AdvancedSTTConfig oldConfig = config_;
        config_ = newConfig;
        isModified_ = true;
        lastModified_ = std::chrono::steady_clock::now();
        
        // Notify callbacks of changes
        // (In a real implementation, we would compare old and new configs and notify specific changes)
        
        autoSaveIfEnabled();
        
        LOG_INFO("Advanced STT configuration updated");
    }
    
    return result;
}

ConfigValidationResult AdvancedSTTConfigManager::updateFeatureConfig(AdvancedFeature feature, 
                                                                     const FeatureConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    ConfigValidationResult result = validateFeatureConfig(feature, config);
    
    if (result.isValid) {
        FeatureConfig* featureConfig = config_.getFeatureConfig(feature);
        if (featureConfig) {
            *featureConfig = config;
            isModified_ = true;
            lastModified_ = std::chrono::steady_clock::now();
            
            autoSaveIfEnabled();
            
            LOG_INFO("Feature configuration updated: " + featureToString(feature));
        } else {
            result.addError("Invalid feature type");
        }
    }
    
    return result;
}

ConfigValidationResult AdvancedSTTConfigManager::updateConfigValue(AdvancedFeature feature,
                                                                   const std::string& section,
                                                                   const std::string& key,
                                                                   const std::string& value) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    ConfigValidationResult result;
    
    FeatureConfig* featureConfig = config_.getFeatureConfig(feature);
    if (!featureConfig) {
        result.addError("Invalid feature type");
        return result;
    }
    
    // Update the parameter
    std::string oldValue = featureConfig->getStringParameter(key, "");
    featureConfig->setStringParameter(key, value);
    
    // Validate the updated configuration
    result = validateFeatureConfig(feature, *featureConfig);
    
    if (result.isValid) {
        isModified_ = true;
        lastModified_ = std::chrono::steady_clock::now();
        
        // Notify change
        notifyConfigChange(feature, section, key, oldValue, value);
        
        autoSaveIfEnabled();
        
        LOG_INFO("Configuration value updated - Feature: " + featureToString(feature) + 
                ", Key: " + key + ", Value: " + value);
    } else {
        // Revert the change
        featureConfig->setStringParameter(key, oldValue);
    }
    
    return result;
}

ConfigValidationResult AdvancedSTTConfigManager::validateConfig(const AdvancedSTTConfig& config) const {
    ConfigValidationResult result;
    
    auto errors = config.getValidationErrors();
    for (const auto& error : errors) {
        result.addError(error);
    }
    
    // Additional validation can be added here
    
    return result;
}

void AdvancedSTTConfigManager::resetToDefaults() {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    config_ = AdvancedSTTConfig{};
    isModified_ = true;
    lastModified_ = std::chrono::steady_clock::now();
    
    autoSaveIfEnabled();
    
    LOG_INFO("Advanced STT configuration reset to defaults");
}

void AdvancedSTTConfigManager::registerChangeCallback(ConfigChangeCallback callback) {
    std::lock_guard<std::mutex> lock(configMutex_);
    changeCallbacks_.push_back(callback);
}

std::string AdvancedSTTConfigManager::getConfigSchema() const {
    // Return JSON schema for the configuration
    // This would be a detailed schema describing all configuration options
#ifdef NLOHMANN_JSON_AVAILABLE
    nlohmann::json schema = {
#else
    json schema;
    // Simplified schema when full JSON is not available
    schema.values["type"] = "object";
    return schema.dump(2);
    /*
#endif
        {"type", "object"},
        {"properties", {
            {"enableAdvancedFeatures", {
                {"type", "boolean"},
                {"description", "Enable advanced STT features"}
            }},
            {"speakerDiarization", {
                {"type", "object"},
                {"properties", {
                    {"enabled", {"type", "boolean"}},
                    {"maxSpeakers", {"type", "integer", "minimum", 1}},
                    {"speakerChangeThreshold", {"type", "number", "minimum", 0.0, "maximum", 1.0}}
                }}
            }},
            {"audioPreprocessing", {
                {"type", "object"},
                {"properties", {
                    {"enabled", {"type", "boolean"}},
                    {"enableNoiseReduction", {"type", "boolean"}},
                    {"enableVolumeNormalization", {"type", "boolean"}}
                }}
            }}
            // Add more schema definitions for other features
        }}
    };
    
    return schema.dump(2);
#else
    */
#endif
}

std::string AdvancedSTTConfigManager::getConfigMetadata() const {
    // Return metadata about configuration options
#ifdef NLOHMANN_JSON_AVAILABLE
    nlohmann::json metadata = {
#else
    json metadata;
    metadata.values["version"] = "1.0";
    metadata.values["description"] = "Advanced STT Configuration Metadata";
    return metadata.dump(2);
    /*
#endif
        {"version", "1.0"},
        {"description", "Advanced STT Configuration Metadata"},
        {"features", {
            {"speakerDiarization", {
                {"displayName", "Speaker Diarization"},
                {"description", "Identify and separate different speakers in audio"},
                {"category", "Audio Analysis"}
            }},
            {"audioPreprocessing", {
                {"displayName", "Audio Preprocessing"},
                {"description", "Enhance audio quality before transcription"},
                {"category", "Audio Enhancement"}
            }}
            // Add more metadata for other features
        }}
    };
    
    return metadata.dump(2);
#else
    */
#endif
}

bool AdvancedSTTConfigManager::isModified() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return isModified_;
}

std::chrono::steady_clock::time_point AdvancedSTTConfigManager::getLastModified() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return lastModified_;
}

void AdvancedSTTConfigManager::setAutoSave(bool enable) {
    std::lock_guard<std::mutex> lock(configMutex_);
    autoSave_ = enable;
}

bool AdvancedSTTConfigManager::reloadFromFile() {
    if (configFilePath_.empty()) {
        LOG_ERROR("No config file path set for reload");
        return false;
    }
    
    return loadFromFile(configFilePath_);
}

// Private helper methods

void AdvancedSTTConfigManager::notifyConfigChange(AdvancedFeature feature,
                                                  const std::string& section,
                                                  const std::string& key,
                                                  const std::string& oldValue,
                                                  const std::string& newValue) {
    ConfigChangeNotification notification(feature, section, key, oldValue, newValue);
    
    for (const auto& callback : changeCallbacks_) {
        try {
            callback(notification);
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in config change callback: " + std::string(e.what()));
        }
    }
}

bool AdvancedSTTConfigManager::parseJsonConfig(const std::string& jsonStr, AdvancedSTTConfig& config) const {
    try {
#ifdef NLOHMANN_JSON_AVAILABLE
        nlohmann::json j = nlohmann::json::parse(jsonStr);
#else
        // Simple JSON parsing for basic configuration
        json j;
        // For now, use default values when full JSON parsing is not available
        LOG_WARNING("Using simplified JSON parsing - some configuration options may not be loaded");
#endif
        
        // Parse global settings
        if (j.contains("enableAdvancedFeatures")) {
            config.enableAdvancedFeatures = j["enableAdvancedFeatures"];
        }
        
        if (j.contains("configVersion")) {
            config.configVersion = j["configVersion"];
        }
        
        if (j.contains("enableDebugMode")) {
            config.enableDebugMode = j["enableDebugMode"];
        }
        
        // Parse feature configurations
        if (j.contains("speakerDiarization")) {
            auto& sd = j["speakerDiarization"];
            config.speakerDiarization.enabled = sd.value("enabled", false);
            config.speakerDiarization.maxSpeakers = sd.value("maxSpeakers", 10);
            config.speakerDiarization.speakerChangeThreshold = sd.value("speakerChangeThreshold", 0.7f);
        }
        
        if (j.contains("audioPreprocessing")) {
            auto& ap = j["audioPreprocessing"];
            config.audioPreprocessing.enabled = ap.value("enabled", true);
            config.audioPreprocessing.enableNoiseReduction = ap.value("enableNoiseReduction", true);
            config.audioPreprocessing.enableVolumeNormalization = ap.value("enableVolumeNormalization", true);
        }
        
        // Parse other feature configurations...
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("JSON parsing error: " + std::string(e.what()));
        return false;
    }
}

std::string AdvancedSTTConfigManager::configToJson(const AdvancedSTTConfig& config) const {
#ifdef NLOHMANN_JSON_AVAILABLE
    nlohmann::json j;
#else
    json j;
#endif
    
    // Global settings
    j["enableAdvancedFeatures"] = config.enableAdvancedFeatures;
    j["configVersion"] = config.configVersion;
    j["enableDebugMode"] = config.enableDebugMode;
    j["enableMetricsCollection"] = config.enableMetricsCollection;
    j["logLevel"] = config.logLevel;
    j["maxConcurrentProcessing"] = config.maxConcurrentProcessing;
    j["maxMemoryUsageMB"] = config.maxMemoryUsageMB;
    j["maxProcessingLatencyMs"] = config.maxProcessingLatencyMs;
    
    // Feature configurations
    j["speakerDiarization"] = {
        {"enabled", config.speakerDiarization.enabled},
        {"maxSpeakers", config.speakerDiarization.maxSpeakers},
        {"speakerChangeThreshold", config.speakerDiarization.speakerChangeThreshold},
        {"enableSpeakerProfiles", config.speakerDiarization.enableSpeakerProfiles}
    };
    
    j["audioPreprocessing"] = {
        {"enabled", config.audioPreprocessing.enabled},
        {"enableNoiseReduction", config.audioPreprocessing.enableNoiseReduction},
        {"enableVolumeNormalization", config.audioPreprocessing.enableVolumeNormalization},
        {"enableEchoCancellation", config.audioPreprocessing.enableEchoCancellation},
        {"adaptivePreprocessing", config.audioPreprocessing.adaptivePreprocessing}
    };
    
    // Add other feature configurations...
    
    return j.dump(2);
}

ConfigValidationResult AdvancedSTTConfigManager::validateFeatureConfig(AdvancedFeature feature,
                                                                       const FeatureConfig& config) const {
    ConfigValidationResult result;
    
    switch (feature) {
        case AdvancedFeature::SPEAKER_DIARIZATION: {
            int maxSpeakers = config.getIntParameter("maxSpeakers", 10);
            if (maxSpeakers <= 0) {
                result.addError("maxSpeakers must be greater than 0");
            }
            
            float threshold = config.getFloatParameter("speakerChangeThreshold", 0.7f);
            if (threshold < 0.0f || threshold > 1.0f) {
                result.addError("speakerChangeThreshold must be between 0.0 and 1.0");
            }
            break;
        }
        
        case AdvancedFeature::REALTIME_ANALYSIS: {
            int bufferSize = config.getIntParameter("analysisBufferSize", 1024);
            if (bufferSize <= 0) {
                result.addError("analysisBufferSize must be greater than 0");
            }
            break;
        }
        
        // Add validation for other features...
        
        default:
            break;
    }
    
    return result;
}

void AdvancedSTTConfigManager::autoSaveIfEnabled() {
    if (autoSave_ && !configFilePath_.empty()) {
        saveToFile(configFilePath_);
        isModified_ = false;
    }
}

std::string AdvancedSTTConfigManager::featureToString(AdvancedFeature feature) const {
    switch (feature) {
        case AdvancedFeature::SPEAKER_DIARIZATION: return "SpeakerDiarization";
        case AdvancedFeature::AUDIO_PREPROCESSING: return "AudioPreprocessing";
        case AdvancedFeature::CONTEXTUAL_TRANSCRIPTION: return "ContextualTranscription";
        case AdvancedFeature::REALTIME_ANALYSIS: return "RealtimeAnalysis";
        case AdvancedFeature::ADAPTIVE_QUALITY: return "AdaptiveQuality";
        case AdvancedFeature::EXTERNAL_SERVICES: return "ExternalServices";
        case AdvancedFeature::BATCH_PROCESSING: return "BatchProcessing";
        default: return "Unknown";
    }
}

AdvancedFeature AdvancedSTTConfigManager::stringToFeature(const std::string& featureStr) const {
    if (featureStr == "SpeakerDiarization") return AdvancedFeature::SPEAKER_DIARIZATION;
    if (featureStr == "AudioPreprocessing") return AdvancedFeature::AUDIO_PREPROCESSING;
    if (featureStr == "ContextualTranscription") return AdvancedFeature::CONTEXTUAL_TRANSCRIPTION;
    if (featureStr == "RealtimeAnalysis") return AdvancedFeature::REALTIME_ANALYSIS;
    if (featureStr == "AdaptiveQuality") return AdvancedFeature::ADAPTIVE_QUALITY;
    if (featureStr == "ExternalServices") return AdvancedFeature::EXTERNAL_SERVICES;
    if (featureStr == "BatchProcessing") return AdvancedFeature::BATCH_PROCESSING;
    return AdvancedFeature::SPEAKER_DIARIZATION; // Default
}

} // namespace advanced
} // namespace stt