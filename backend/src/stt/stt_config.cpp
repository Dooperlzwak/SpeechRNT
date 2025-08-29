#include "stt/stt_config.hpp"
#include "utils/json_utils.hpp"
#include "utils/gpu_manager.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <regex>

namespace stt {

STTConfigManager::STTConfigManager() 
    : isModified_(false)
    , autoSave_(false)
    , lastModified_(std::chrono::steady_clock::now())
    , quantizationManager_(std::make_unique<QuantizationManager>()) {
}

bool STTConfigManager::loadFromFile(const std::string& configPath) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    configFilePath_ = configPath;
    
    if (!std::filesystem::exists(configPath)) {
        std::cout << "Configuration file not found: " << configPath 
                  << ", using defaults" << std::endl;
        config_ = STTConfig(); // Use defaults
        isModified_ = true; // Mark as modified to save defaults
        return true;
    }
    
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open configuration file: " << configPath << std::endl;
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    std::string jsonStr = buffer.str();
    if (jsonStr.empty()) {
        std::cout << "Empty configuration file, using defaults" << std::endl;
        config_ = STTConfig();
        isModified_ = true;
        return true;
    }
    
    STTConfig newConfig;
    if (!parseJsonConfig(jsonStr, newConfig)) {
        std::cerr << "Failed to parse configuration file: " << configPath << std::endl;
        return false;
    }
    
    // Validate the loaded configuration
    auto validationResult = validateConfig(newConfig);
    if (!validationResult.isValid) {
        std::cerr << "Invalid configuration loaded from: " << configPath << std::endl;
        for (const auto& error : validationResult.errors) {
            std::cerr << "  Error: " << error << std::endl;
        }
        return false;
    }
    
    // Print warnings if any
    for (const auto& warning : validationResult.warnings) {
        std::cout << "  Warning: " << warning << std::endl;
    }
    
    config_ = newConfig;
    isModified_ = false;
    lastModified_ = std::chrono::steady_clock::now();
    
    std::cout << "STT configuration loaded from: " << configPath << std::endl;
    return true;
}

bool STTConfigManager::saveToFile(const std::string& configPath) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    // Create directory if it doesn't exist
    std::filesystem::path filePath(configPath);
    std::filesystem::path dirPath = filePath.parent_path();
    
    if (!dirPath.empty() && !std::filesystem::exists(dirPath)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(dirPath, ec)) {
            std::cerr << "Failed to create configuration directory: " << dirPath 
                      << " - " << ec.message() << std::endl;
            return false;
        }
    }
    
    std::ofstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open configuration file for writing: " << configPath << std::endl;
        return false;
    }
    
    std::string jsonStr = configToJson(config_);
    file << jsonStr;
    file.close();
    
    if (file.fail()) {
        std::cerr << "Failed to write configuration file: " << configPath << std::endl;
        return false;
    }
    
    std::cout << "STT configuration saved to: " << configPath << std::endl;
    return true;
}

bool STTConfigManager::loadFromJson(const std::string& jsonStr) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    STTConfig newConfig;
    if (!parseJsonConfig(jsonStr, newConfig)) {
        std::cerr << "Failed to parse JSON configuration" << std::endl;
        return false;
    }
    
    auto validationResult = validateConfig(newConfig);
    if (!validationResult.isValid) {
        std::cerr << "Invalid JSON configuration" << std::endl;
        for (const auto& error : validationResult.errors) {
            std::cerr << "  Error: " << error << std::endl;
        }
        return false;
    }
    
    config_ = newConfig;
    isModified_ = true;
    lastModified_ = std::chrono::steady_clock::now();
    
    autoSaveIfEnabled();
    return true;
}

std::string STTConfigManager::exportToJson() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return configToJson(config_);
}

STTConfig STTConfigManager::getConfig() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return config_;
}

ConfigValidationResult STTConfigManager::updateConfig(const STTConfig& newConfig) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    auto validationResult = validateConfig(newConfig);
    if (!validationResult.isValid) {
        return validationResult;
    }
    
    // Store old config for change notifications
    STTConfig oldConfig = config_;
    config_ = newConfig;
    isModified_ = true;
    lastModified_ = std::chrono::steady_clock::now();
    
    // Notify about changes (simplified - in a full implementation, 
    // we would compare each field and notify specific changes)
    notifyConfigChange("all", "config", "old_config", "new_config");
    
    autoSaveIfEnabled();
    return validationResult;
}

ConfigValidationResult STTConfigManager::updateConfigValue(const std::string& section, 
                                                          const std::string& key, 
                                                          const std::string& value) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    ConfigValidationResult result;
    std::string oldValue;
    bool updated = false;
    
    // Model configuration
    if (section == "model") {
        if (key == "defaultModel") {
            oldValue = config_.defaultModel;
            updated = updateStringValue(section, key, value, config_.defaultModel);
        } else if (key == "modelsPath") {
            oldValue = config_.modelsPath;
            updated = updateStringValue(section, key, value, config_.modelsPath);
        } else if (key == "language") {
            oldValue = config_.language;
            updated = updateStringValue(section, key, value, config_.language);
        } else if (key == "translateToEnglish") {
            oldValue = config_.translateToEnglish ? "true" : "false";
            updated = updateBoolValue(section, key, value, config_.translateToEnglish);
        }
    }
    // Language detection configuration
    else if (section == "languageDetection") {
        if (key == "enabled") {
            oldValue = config_.languageDetectionEnabled ? "true" : "false";
            updated = updateBoolValue(section, key, value, config_.languageDetectionEnabled);
        } else if (key == "threshold") {
            oldValue = std::to_string(config_.languageDetectionThreshold);
            updated = updateFloatValue(section, key, value, config_.languageDetectionThreshold);
        } else if (key == "autoSwitching") {
            oldValue = config_.autoLanguageSwitching ? "true" : "false";
            updated = updateBoolValue(section, key, value, config_.autoLanguageSwitching);
        } else if (key == "consistentDetectionRequired") {
            oldValue = std::to_string(config_.consistentDetectionRequired);
            updated = updateIntValue(section, key, value, config_.consistentDetectionRequired);
        }
    }
    // Quantization configuration
    else if (section == "quantization") {
        if (key == "level") {
            oldValue = quantizationManager_->levelToString(config_.quantizationLevel);
            updated = updateQuantizationLevel(section, key, value, config_.quantizationLevel);
        } else if (key == "enableGPUAcceleration") {
            oldValue = config_.enableGPUAcceleration ? "true" : "false";
            updated = updateBoolValue(section, key, value, config_.enableGPUAcceleration);
        } else if (key == "gpuDeviceId") {
            oldValue = std::to_string(config_.gpuDeviceId);
            updated = updateIntValue(section, key, value, config_.gpuDeviceId);
        } else if (key == "accuracyThreshold") {
            oldValue = std::to_string(config_.accuracyThreshold);
            updated = updateFloatValue(section, key, value, config_.accuracyThreshold);
        }
    }
    // Streaming configuration
    else if (section == "streaming") {
        if (key == "partialResultsEnabled") {
            oldValue = config_.partialResultsEnabled ? "true" : "false";
            updated = updateBoolValue(section, key, value, config_.partialResultsEnabled);
        } else if (key == "minChunkSizeMs") {
            oldValue = std::to_string(config_.minChunkSizeMs);
            updated = updateIntValue(section, key, value, config_.minChunkSizeMs);
        } else if (key == "maxChunkSizeMs") {
            oldValue = std::to_string(config_.maxChunkSizeMs);
            updated = updateIntValue(section, key, value, config_.maxChunkSizeMs);
        } else if (key == "overlapSizeMs") {
            oldValue = std::to_string(config_.overlapSizeMs);
            updated = updateIntValue(section, key, value, config_.overlapSizeMs);
        } else if (key == "enableIncrementalUpdates") {
            oldValue = config_.enableIncrementalUpdates ? "true" : "false";
            updated = updateBoolValue(section, key, value, config_.enableIncrementalUpdates);
        }
    }
    // Confidence configuration
    else if (section == "confidence") {
        if (key == "threshold") {
            oldValue = std::to_string(config_.confidenceThreshold);
            updated = updateFloatValue(section, key, value, config_.confidenceThreshold);
        } else if (key == "wordLevelEnabled") {
            oldValue = config_.wordLevelConfidenceEnabled ? "true" : "false";
            updated = updateBoolValue(section, key, value, config_.wordLevelConfidenceEnabled);
        } else if (key == "qualityIndicatorsEnabled") {
            oldValue = config_.qualityIndicatorsEnabled ? "true" : "false";
            updated = updateBoolValue(section, key, value, config_.qualityIndicatorsEnabled);
        } else if (key == "filteringEnabled") {
            oldValue = config_.confidenceFilteringEnabled ? "true" : "false";
            updated = updateBoolValue(section, key, value, config_.confidenceFilteringEnabled);
        }
    }
    // Performance configuration
    else if (section == "performance") {
        if (key == "threadCount") {
            oldValue = std::to_string(config_.threadCount);
            updated = updateIntValue(section, key, value, config_.threadCount);
        } else if (key == "temperature") {
            oldValue = std::to_string(config_.temperature);
            updated = updateFloatValue(section, key, value, config_.temperature);
        } else if (key == "maxTokens") {
            oldValue = std::to_string(config_.maxTokens);
            updated = updateIntValue(section, key, value, config_.maxTokens);
        }
    }
    
    if (!updated) {
        result.addError("Unknown configuration key: " + section + "." + key);
        return result;
    }
    
    // Validate the updated configuration
    auto validationResult = validateConfig(config_);
    if (!validationResult.isValid) {
        // Revert the change if validation fails
        updateConfigValue(section, key, oldValue);
        return validationResult;
    }
    
    isModified_ = true;
    lastModified_ = std::chrono::steady_clock::now();
    
    // Notify about the change
    notifyConfigChange(section, key, oldValue, value);
    
    autoSaveIfEnabled();
    return result;
}

ConfigValidationResult STTConfigManager::validateConfig(const STTConfig& config) const {
    ConfigValidationResult result;
    
    // Validate each configuration section
    auto modelResult = validateModelConfig(config);
    result.errors.insert(result.errors.end(), modelResult.errors.begin(), modelResult.errors.end());
    result.warnings.insert(result.warnings.end(), modelResult.warnings.begin(), modelResult.warnings.end());
    
    auto langResult = validateLanguageConfig(config);
    result.errors.insert(result.errors.end(), langResult.errors.begin(), langResult.errors.end());
    result.warnings.insert(result.warnings.end(), langResult.warnings.begin(), langResult.warnings.end());
    
    auto quantResult = validateQuantizationConfig(config);
    result.errors.insert(result.errors.end(), quantResult.errors.begin(), quantResult.errors.end());
    result.warnings.insert(result.warnings.end(), quantResult.warnings.begin(), quantResult.warnings.end());
    
    auto streamResult = validateStreamingConfig(config);
    result.errors.insert(result.errors.end(), streamResult.errors.begin(), streamResult.errors.end());
    result.warnings.insert(result.warnings.end(), streamResult.warnings.begin(), streamResult.warnings.end());
    
    auto confResult = validateConfidenceConfig(config);
    result.errors.insert(result.errors.end(), confResult.errors.begin(), confResult.errors.end());
    result.warnings.insert(result.warnings.end(), confResult.warnings.begin(), confResult.warnings.end());
    
    auto perfResult = validatePerformanceConfig(config);
    result.errors.insert(result.errors.end(), perfResult.errors.begin(), perfResult.errors.end());
    result.warnings.insert(result.warnings.end(), perfResult.warnings.begin(), perfResult.warnings.end());
    
    auto audioResult = validateAudioConfig(config);
    result.errors.insert(result.errors.end(), audioResult.errors.begin(), audioResult.errors.end());
    result.warnings.insert(result.warnings.end(), audioResult.warnings.begin(), audioResult.warnings.end());
    
    auto errorResult = validateErrorRecoveryConfig(config);
    result.errors.insert(result.errors.end(), errorResult.errors.begin(), errorResult.errors.end());
    result.warnings.insert(result.warnings.end(), errorResult.warnings.begin(), errorResult.warnings.end());
    
    auto healthResult = validateHealthMonitoringConfig(config);
    result.errors.insert(result.errors.end(), healthResult.errors.begin(), healthResult.errors.end());
    result.warnings.insert(result.warnings.end(), healthResult.warnings.begin(), healthResult.warnings.end());
    
    result.isValid = result.errors.empty();
    return result;
}

void STTConfigManager::resetToDefaults() {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    STTConfig oldConfig = config_;
    config_ = STTConfig();
    isModified_ = true;
    lastModified_ = std::chrono::steady_clock::now();
    
    notifyConfigChange("all", "reset", "old_config", "default_config");
    autoSaveIfEnabled();
}

void STTConfigManager::registerChangeCallback(ConfigChangeCallback callback) {
    std::lock_guard<std::mutex> lock(configMutex_);
    changeCallbacks_.push_back(callback);
}

std::string STTConfigManager::getConfigSchema() const {
    // Return JSON schema describing the configuration structure
    return R"({
  "type": "object",
  "properties": {
    "model": {
      "type": "object",
      "properties": {
        "defaultModel": {"type": "string", "enum": ["tiny", "base", "small", "medium", "large"]},
        "modelsPath": {"type": "string"},
        "language": {"type": "string"},
        "translateToEnglish": {"type": "boolean"}
      }
    },
    "languageDetection": {
      "type": "object",
      "properties": {
        "enabled": {"type": "boolean"},
        "threshold": {"type": "number", "minimum": 0.0, "maximum": 1.0},
        "autoSwitching": {"type": "boolean"},
        "consistentDetectionRequired": {"type": "integer", "minimum": 1}
      }
    },
    "quantization": {
      "type": "object",
      "properties": {
        "level": {"type": "string", "enum": ["FP32", "FP16", "INT8", "AUTO"]},
        "enableGPUAcceleration": {"type": "boolean"},
        "gpuDeviceId": {"type": "integer", "minimum": 0},
        "accuracyThreshold": {"type": "number", "minimum": 0.0, "maximum": 1.0}
      }
    },
    "streaming": {
      "type": "object",
      "properties": {
        "partialResultsEnabled": {"type": "boolean"},
        "minChunkSizeMs": {"type": "integer", "minimum": 100},
        "maxChunkSizeMs": {"type": "integer", "minimum": 1000},
        "overlapSizeMs": {"type": "integer", "minimum": 0},
        "enableIncrementalUpdates": {"type": "boolean"}
      }
    },
    "confidence": {
      "type": "object",
      "properties": {
        "threshold": {"type": "number", "minimum": 0.0, "maximum": 1.0},
        "wordLevelEnabled": {"type": "boolean"},
        "qualityIndicatorsEnabled": {"type": "boolean"},
        "filteringEnabled": {"type": "boolean"}
      }
    },
    "performance": {
      "type": "object",
      "properties": {
        "threadCount": {"type": "integer", "minimum": 1},
        "temperature": {"type": "number", "minimum": 0.0, "maximum": 1.0},
        "maxTokens": {"type": "integer", "minimum": 0}
      }
    }
  }
})";
}

std::string STTConfigManager::getConfigMetadata() const {
    return R"({
  "model": {
    "defaultModel": {
      "description": "Default Whisper model to use for transcription",
      "options": ["tiny", "base", "small", "medium", "large"],
      "default": "base"
    },
    "modelsPath": {
      "description": "Path to directory containing Whisper model files",
      "default": "data/whisper/"
    },
    "language": {
      "description": "Default language for transcription (use 'auto' for detection)",
      "default": "auto"
    },
    "translateToEnglish": {
      "description": "Whether to translate non-English speech to English",
      "default": false
    }
  },
  "languageDetection": {
    "enabled": {
      "description": "Enable automatic language detection",
      "default": true
    },
    "threshold": {
      "description": "Confidence threshold for language detection (0.0-1.0)",
      "default": 0.7,
      "range": [0.0, 1.0]
    },
    "autoSwitching": {
      "description": "Automatically switch to detected language",
      "default": true
    },
    "consistentDetectionRequired": {
      "description": "Number of consistent detections required before switching",
      "default": 2,
      "minimum": 1
    }
  },
  "quantization": {
    "level": {
      "description": "Model quantization level for memory optimization",
      "options": ["FP32", "FP16", "INT8", "AUTO"],
      "default": "AUTO"
    },
    "enableGPUAcceleration": {
      "description": "Use GPU acceleration when available",
      "default": true
    },
    "gpuDeviceId": {
      "description": "GPU device ID to use (0 for first GPU)",
      "default": 0,
      "minimum": 0
    },
    "accuracyThreshold": {
      "description": "Minimum accuracy threshold for quantized models",
      "default": 0.85,
      "range": [0.0, 1.0]
    }
  }
})";
}

bool STTConfigManager::isModified() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return isModified_;
}

std::chrono::steady_clock::time_point STTConfigManager::getLastModified() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return lastModified_;
}

std::string STTConfigManager::getConfigFilePath() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return configFilePath_;
}

bool STTConfigManager::reloadFromFile() {
    if (configFilePath_.empty()) {
        std::cerr << "No configuration file path set" << std::endl;
        return false;
    }
    return loadFromFile(configFilePath_);
}

void STTConfigManager::setAutoSave(bool enable) {
    std::lock_guard<std::mutex> lock(configMutex_);
    autoSave_ = enable;
}

std::vector<QuantizationLevel> STTConfigManager::getSupportedQuantizationLevels() const {
    auto& gpuManager = utils::GPUManager::getInstance();
    size_t availableMemoryMB = 0;
    
    if (gpuManager.isCudaAvailable()) {
        auto deviceInfo = gpuManager.getDeviceInfo(0);
        availableMemoryMB = deviceInfo.totalMemoryMB;
    }
    
    return quantizationManager_->getPreferenceOrder(availableMemoryMB);
}

std::vector<std::string> STTConfigManager::getAvailableModels() const {
    std::vector<std::string> models;
    std::string modelsPath = config_.modelsPath;
    
    // Standard Whisper model names
    std::vector<std::string> standardModels = {"tiny", "base", "small", "medium", "large"};
    
    for (const auto& model : standardModels) {
        if (validateModelFile(model)) {
            models.push_back(model);
        }
    }
    
    return models;
}

bool STTConfigManager::validateModelFile(const std::string& modelName) const {
    std::string modelPath = getModelFilePath(modelName);
    return std::filesystem::exists(modelPath) && std::filesystem::is_regular_file(modelPath);
}

// Private helper methods implementation continues...
void STTConfigManager::notifyConfigChange(const std::string& section, const std::string& key, 
                                        const std::string& oldValue, const std::string& newValue) {
    ConfigChangeNotification notification(section, key, oldValue, newValue);
    
    for (const auto& callback : changeCallbacks_) {
        try {
            callback(notification);
        } catch (const std::exception& e) {
            std::cerr << "Error in config change callback: " << e.what() << std::endl;
        }
    }
}

bool STTConfigManager::parseJsonConfig(const std::string& jsonStr, STTConfig& config) const {
    // This is a simplified JSON parsing implementation
    // In a production system, you would use a proper JSON library like nlohmann/json
    
    try {
        // For now, we'll implement basic parsing for key configuration values
        // This would be replaced with proper JSON parsing in a real implementation
        
        // Parse model configuration
        if (jsonStr.find("\"defaultModel\"") != std::string::npos) {
            std::regex modelRegex(R"("defaultModel"\s*:\s*"([^"]+)")");
            std::smatch match;
            if (std::regex_search(jsonStr, match, modelRegex)) {
                config.defaultModel = match[1].str();
            }
        }
        
        // Parse language detection settings
        if (jsonStr.find("\"languageDetectionEnabled\"") != std::string::npos) {
            std::regex enabledRegex(R"("languageDetectionEnabled"\s*:\s*(true|false))");
            std::smatch match;
            if (std::regex_search(jsonStr, match, enabledRegex)) {
                config.languageDetectionEnabled = (match[1].str() == "true");
            }
        }

    // Parse normalization settings (very simple boolean toggles)
    auto parseBool = [&](const std::string& key, bool& target) {
        std::regex re("\\\"" + key + "\\\"\\s*:\\s*(true|false)");
        std::smatch m;
        if (std::regex_search(jsonStr, m, re)) {
            target = (m[1].str() == "true");
        }
    };
    parseBool("lowercase", config.normalization.lowercase);
    parseBool("removePunctuation", config.normalization.removePunctuation);
    parseBool("ensureEndingPunctuation", config.normalization.ensureEndingPunctuation);
    parseBool("trimWhitespace", config.normalization.trimWhitespace);
    parseBool("collapseWhitespace", config.normalization.collapseWhitespace);
        
        // Parse quantization level
        if (jsonStr.find("\"quantizationLevel\"") != std::string::npos) {
            std::regex quantRegex(R"("quantizationLevel"\s*:\s*"([^"]+)")");
            std::smatch match;
            if (std::regex_search(jsonStr, match, quantRegex)) {
                config.quantizationLevel = quantizationManager_->stringToLevel(match[1].str());
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
        return false;
    }
}

std::string STTConfigManager::configToJson(const STTConfig& config) const {
    std::ostringstream json;
    
    json << "{\n";
    json << "  \"model\": {\n";
    json << "    \"defaultModel\": \"" << config.defaultModel << "\",\n";
    json << "    \"modelsPath\": \"" << config.modelsPath << "\",\n";
    json << "    \"language\": \"" << config.language << "\",\n";
    json << "    \"translateToEnglish\": " << (config.translateToEnglish ? "true" : "false") << "\n";
    json << "  },\n";
    
    json << "  \"languageDetection\": {\n";
    json << "    \"enabled\": " << (config.languageDetectionEnabled ? "true" : "false") << ",\n";
    json << "    \"threshold\": " << config.languageDetectionThreshold << ",\n";
    json << "    \"autoSwitching\": " << (config.autoLanguageSwitching ? "true" : "false") << ",\n";
    json << "    \"consistentDetectionRequired\": " << config.consistentDetectionRequired << "\n";
    json << "  },\n";
    
    json << "  \"quantization\": {\n";
    json << "    \"level\": \"" << quantizationManager_->levelToString(config.quantizationLevel) << "\",\n";
    json << "    \"enableGPUAcceleration\": " << (config.enableGPUAcceleration ? "true" : "false") << ",\n";
    json << "    \"gpuDeviceId\": " << config.gpuDeviceId << ",\n";
    json << "    \"accuracyThreshold\": " << config.accuracyThreshold << "\n";
    json << "  },\n";
    
    json << "  \"streaming\": {\n";
    json << "    \"partialResultsEnabled\": " << (config.partialResultsEnabled ? "true" : "false") << ",\n";
    json << "    \"minChunkSizeMs\": " << config.minChunkSizeMs << ",\n";
    json << "    \"maxChunkSizeMs\": " << config.maxChunkSizeMs << ",\n";
    json << "    \"overlapSizeMs\": " << config.overlapSizeMs << ",\n";
    json << "    \"enableIncrementalUpdates\": " << (config.enableIncrementalUpdates ? "true" : "false") << "\n";
    json << "  },\n";
    
    json << "  \"confidence\": {\n";
    json << "    \"threshold\": " << config.confidenceThreshold << ",\n";
    json << "    \"wordLevelEnabled\": " << (config.wordLevelConfidenceEnabled ? "true" : "false") << ",\n";
    json << "    \"qualityIndicatorsEnabled\": " << (config.qualityIndicatorsEnabled ? "true" : "false") << ",\n";
    json << "    \"filteringEnabled\": " << (config.confidenceFilteringEnabled ? "true" : "false") << "\n";
    json << "  },\n";
    
    json << "  \"performance\": {\n";
    json << "    \"threadCount\": " << config.threadCount << ",\n";
    json << "    \"temperature\": " << config.temperature << ",\n";
    json << "    \"maxTokens\": " << config.maxTokens << ",\n";
    json << "    \"suppressBlank\": " << (config.suppressBlank ? "true" : "false") << ",\n";
    json << "    \"suppressNonSpeechTokens\": " << (config.suppressNonSpeechTokens ? "true" : "false") << "\n";
    json << "  },\n";
    
    json << "  \"audio\": {\n";
    json << "    \"sampleRate\": " << config.sampleRate << ",\n";
    json << "    \"audioBufferSizeMB\": " << config.audioBufferSizeMB << ",\n";
    json << "    \"enableNoiseReduction\": " << (config.enableNoiseReduction ? "true" : "false") << ",\n";
    json << "    \"vadThreshold\": " << config.vadThreshold << "\n";
    json << "  },\n";
    
    json << "  \"errorRecovery\": {\n";
    json << "    \"enabled\": " << (config.enableErrorRecovery ? "true" : "false") << ",\n";
    json << "    \"maxRetryAttempts\": " << config.maxRetryAttempts << ",\n";
    json << "    \"retryBackoffMultiplier\": " << config.retryBackoffMultiplier << ",\n";
    json << "    \"retryInitialDelayMs\": " << config.retryInitialDelayMs << "\n";
    json << "  },\n";
    
    json << "  \"healthMonitoring\": {\n";
    json << "    \"enabled\": " << (config.enableHealthMonitoring ? "true" : "false") << ",\n";
    json << "    \"healthCheckIntervalMs\": " << config.healthCheckIntervalMs << ",\n";
    json << "    \"maxLatencyMs\": " << config.maxLatencyMs << ",\n";
    json << "    \"maxMemoryUsageMB\": " << config.maxMemoryUsageMB << "\n";
    json << "  },\n";
    
    // Text normalization
    json << "  \"normalization\": {\n";
    json << "    \"lowercase\": " << (config.normalization.lowercase ? "true" : "false") << ",\n";
    json << "    \"removePunctuation\": " << (config.normalization.removePunctuation ? "true" : "false") << ",\n";
    json << "    \"ensureEndingPunctuation\": " << (config.normalization.ensureEndingPunctuation ? "true" : "false") << ",\n";
    json << "    \"trimWhitespace\": " << (config.normalization.trimWhitespace ? "true" : "false") << ",\n";
    json << "    \"collapseWhitespace\": " << (config.normalization.collapseWhitespace ? "true" : "false") << "\n";
    json << "  }\n";
    
    json << "}";
    
    return json.str();
}

// Validation helper methods
ConfigValidationResult STTConfigManager::validateModelConfig(const STTConfig& config) const {
    ConfigValidationResult result;
    
    // Validate default model
    std::vector<std::string> validModels = {"tiny", "base", "small", "medium", "large"};
    if (std::find(validModels.begin(), validModels.end(), config.defaultModel) == validModels.end()) {
        result.addError("Invalid default model: " + config.defaultModel);
    }
    
    // Validate models path
    if (config.modelsPath.empty()) {
        result.addError("Models path cannot be empty");
    } else if (!std::filesystem::exists(config.modelsPath)) {
        result.addWarning("Models path does not exist: " + config.modelsPath);
    }
    
    // Validate language
    if (!config.language.empty() && config.language != "auto" && !isLanguageSupported(config.language)) {
        result.addWarning("Language may not be supported: " + config.language);
    }
    
    return result;
}

ConfigValidationResult STTConfigManager::validateLanguageConfig(const STTConfig& config) const {
    ConfigValidationResult result;
    
    // Validate threshold range
    if (config.languageDetectionThreshold < 0.0f || config.languageDetectionThreshold > 1.0f) {
        result.addError("Language detection threshold must be between 0.0 and 1.0");
    }
    
    // Validate consistent detection required
    if (config.consistentDetectionRequired < 1) {
        result.addError("Consistent detection required must be at least 1");
    }
    
    return result;
}

ConfigValidationResult STTConfigManager::validateQuantizationConfig(const STTConfig& config) const {
    ConfigValidationResult result;
    
    // Validate quantization level
    if (!isValidQuantizationLevel(config.quantizationLevel)) {
        result.addError("Invalid quantization level");
    }
    
    // Validate GPU device ID
    if (config.gpuDeviceId < 0) {
        result.addError("GPU device ID must be non-negative");
    }
    
    // Validate accuracy threshold
    if (config.accuracyThreshold < 0.0f || config.accuracyThreshold > 1.0f) {
        result.addError("Accuracy threshold must be between 0.0 and 1.0");
    }
    
    return result;
}

ConfigValidationResult STTConfigManager::validateStreamingConfig(const STTConfig& config) const {
    ConfigValidationResult result;
    
    // Validate chunk sizes
    if (config.minChunkSizeMs < 100) {
        result.addError("Minimum chunk size must be at least 100ms");
    }
    
    if (config.maxChunkSizeMs < config.minChunkSizeMs) {
        result.addError("Maximum chunk size must be greater than minimum chunk size");
    }
    
    if (config.overlapSizeMs < 0) {
        result.addError("Overlap size must be non-negative");
    }
    
    if (config.overlapSizeMs >= config.minChunkSizeMs) {
        result.addWarning("Overlap size should be smaller than minimum chunk size");
    }
    
    return result;
}

ConfigValidationResult STTConfigManager::validateConfidenceConfig(const STTConfig& config) const {
    ConfigValidationResult result;
    
    // Validate confidence threshold
    if (config.confidenceThreshold < 0.0f || config.confidenceThreshold > 1.0f) {
        result.addError("Confidence threshold must be between 0.0 and 1.0");
    }
    
    return result;
}

ConfigValidationResult STTConfigManager::validatePerformanceConfig(const STTConfig& config) const {
    ConfigValidationResult result;
    
    // Validate thread count
    if (config.threadCount < 1) {
        result.addError("Thread count must be at least 1");
    }
    
    if (config.threadCount > std::thread::hardware_concurrency() * 2) {
        result.addWarning("Thread count is higher than recommended (2x CPU cores)");
    }
    
    // Validate temperature
    if (config.temperature < 0.0f || config.temperature > 1.0f) {
        result.addError("Temperature must be between 0.0 and 1.0");
    }
    
    // Validate max tokens
    if (config.maxTokens < 0) {
        result.addError("Max tokens must be non-negative");
    }
    
    return result;
}

ConfigValidationResult STTConfigManager::validateAudioConfig(const STTConfig& config) const {
    ConfigValidationResult result;
    
    // Validate sample rate
    if (config.sampleRate != 16000 && config.sampleRate != 22050 && config.sampleRate != 44100) {
        result.addWarning("Sample rate should typically be 16000, 22050, or 44100 Hz");
    }
    
    // Validate buffer size
    if (config.audioBufferSizeMB < 1) {
        result.addError("Audio buffer size must be at least 1MB");
    }
    
    if (config.audioBufferSizeMB > 64) {
        result.addWarning("Audio buffer size is very large (>64MB)");
    }
    
    // Validate VAD threshold
    if (config.vadThreshold < 0.0f || config.vadThreshold > 1.0f) {
        result.addError("VAD threshold must be between 0.0 and 1.0");
    }
    
    return result;
}

ConfigValidationResult STTConfigManager::validateErrorRecoveryConfig(const STTConfig& config) const {
    ConfigValidationResult result;
    
    // Validate retry attempts
    if (config.maxRetryAttempts < 0) {
        result.addError("Max retry attempts must be non-negative");
    }
    
    if (config.maxRetryAttempts > 10) {
        result.addWarning("Max retry attempts is very high (>10)");
    }
    
    // Validate backoff multiplier
    if (config.retryBackoffMultiplier < 1.0f) {
        result.addError("Retry backoff multiplier must be at least 1.0");
    }
    
    // Validate initial delay
    if (config.retryInitialDelayMs < 0) {
        result.addError("Retry initial delay must be non-negative");
    }
    
    return result;
}

ConfigValidationResult STTConfigManager::validateHealthMonitoringConfig(const STTConfig& config) const {
    ConfigValidationResult result;
    
    // Validate health check interval
    if (config.healthCheckIntervalMs < 1000) {
        result.addError("Health check interval must be at least 1000ms");
    }
    
    // Validate latency threshold
    if (config.maxLatencyMs <= 0.0f) {
        result.addError("Max latency must be positive");
    }
    
    // Validate memory usage threshold
    if (config.maxMemoryUsageMB <= 0.0f) {
        result.addError("Max memory usage must be positive");
    }
    
    return result;
}

// Update helper methods
bool STTConfigManager::updateStringValue(const std::string& section, const std::string& key, 
                                        const std::string& value, std::string& target) {
    target = value;
    return true;
}

bool STTConfigManager::updateBoolValue(const std::string& section, const std::string& key, 
                                      const std::string& value, bool& target) {
    if (value == "true" || value == "1") {
        target = true;
        return true;
    } else if (value == "false" || value == "0") {
        target = false;
        return true;
    }
    return false;
}

bool STTConfigManager::updateIntValue(const std::string& section, const std::string& key, 
                                     const std::string& value, int& target) {
    try {
        target = std::stoi(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool STTConfigManager::updateFloatValue(const std::string& section, const std::string& key, 
                                       const std::string& value, float& target) {
    try {
        target = std::stof(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool STTConfigManager::updateQuantizationLevel(const std::string& section, const std::string& key, 
                                              const std::string& value, QuantizationLevel& target) {
    QuantizationLevel level = quantizationManager_->stringToLevel(value);
    if (level != QuantizationLevel::FP32 || value == "FP32") {
        target = level;
        return true;
    }
    return false;
}

void STTConfigManager::autoSaveIfEnabled() {
    if (autoSave_ && !configFilePath_.empty()) {
        saveToFile(configFilePath_);
        isModified_ = false;
    }
}

std::string STTConfigManager::getModelFilePath(const std::string& modelName) const {
    std::string basePath = config_.modelsPath;
    if (basePath.back() != '/' && basePath.back() != '\\') {
        basePath += "/";
    }
    
    // Standard Whisper model file naming convention
    return basePath + "ggml-" + modelName + ".bin";
}

bool STTConfigManager::isLanguageSupported(const std::string& language) const {
    return std::find(config_.supportedLanguages.begin(), 
                    config_.supportedLanguages.end(), 
                    language) != config_.supportedLanguages.end();
}

bool STTConfigManager::isValidQuantizationLevel(QuantizationLevel level) const {
    return level == QuantizationLevel::FP32 || 
           level == QuantizationLevel::FP16 || 
           level == QuantizationLevel::INT8 || 
           level == QuantizationLevel::AUTO;
}

} // namespace stt