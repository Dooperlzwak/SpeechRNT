#include "utils/gpu_config.hpp"
#include "utils/gpu_manager.hpp"
#include "utils/logging.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace utils;

// Simple JSON parsing (for basic configuration)
// In a production system, you might want to use a proper JSON library
namespace {
    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(' ');
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(' ');
        return str.substr(first, (last - first + 1));
    }
    
    std::string extractJsonValue(const std::string& json, const std::string& key) {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return "";
        
        size_t colonPos = json.find(":", keyPos);
        if (colonPos == std::string::npos) return "";
        
        size_t valueStart = colonPos + 1;
        size_t valueEnd = json.find_first_of(",}", valueStart);
        if (valueEnd == std::string::npos) valueEnd = json.length();
        
        std::string value = json.substr(valueStart, valueEnd - valueStart);
        value = trim(value);
        
        // Remove quotes if present
        if (value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.length() - 2);
        }
        
        return value;
    }
    
    bool extractJsonBool(const std::string& json, const std::string& key, bool defaultValue = false) {
        std::string value = extractJsonValue(json, key);
        if (value.empty()) return defaultValue;
        return value == "true";
    }
    
    int extractJsonInt(const std::string& json, const std::string& key, int defaultValue = 0) {
        std::string value = extractJsonValue(json, key);
        if (value.empty()) return defaultValue;
        try {
            return std::stoi(value);
        } catch (...) {
            return defaultValue;
        }
    }
    
    size_t extractJsonSize(const std::string& json, const std::string& key, size_t defaultValue = 0) {
        std::string value = extractJsonValue(json, key);
        if (value.empty()) return defaultValue;
        try {
            return static_cast<size_t>(std::stoull(value));
        } catch (...) {
            return defaultValue;
        }
    }
}

namespace speechrnt {
namespace utils {

// Static constants
const std::string GPUConfigManager::MODEL_WHISPER = "whisper";
const std::string GPUConfigManager::MODEL_MARIAN = "marian";
const std::string GPUConfigManager::MODEL_COQUI = "coqui";

GPUConfigManager& GPUConfigManager::getInstance() {
    static GPUConfigManager instance;
    return instance;
}

bool GPUConfigManager::loadConfig(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        Logger::warn("GPU config file not found: " + configPath + ", using defaults");
        setDefaultConfigs();
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    
    return fromJSON(json);
}

bool GPUConfigManager::saveConfig(const std::string& configPath) const {
    std::ofstream file(configPath);
    if (!file.is_open()) {
        Logger::error("Failed to open GPU config file for writing: " + configPath);
        return false;
    }
    
    file << toJSON();
    file.close();
    
    Logger::info("GPU configuration saved to: " + configPath);
    return true;
}

const GPUConfig& GPUConfigManager::getGlobalConfig() const {
    return globalConfig_;
}

void GPUConfigManager::setGlobalConfig(const GPUConfig& config) {
    globalConfig_ = config;
    Logger::info("Global GPU configuration updated");
}

ModelGPUConfig GPUConfigManager::getModelConfig(const std::string& modelName) const {
    auto it = modelConfigs_.find(modelName);
    if (it != modelConfigs_.end()) {
        return it->second;
    }
    
    // Return default config if not found
    return ModelGPUConfig();
}

void GPUConfigManager::setModelConfig(const std::string& modelName, const ModelGPUConfig& config) {
    modelConfigs_[modelName] = config;
    Logger::info("GPU configuration updated for model: " + modelName);
}

bool GPUConfigManager::autoDetectOptimalConfig() {
    auto& gpuManager = GPUManager::getInstance();
    
    if (!gpuManager.initialize()) {
        Logger::warn("Failed to initialize GPU manager for auto-detection");
        return false;
    }
    
    if (!gpuManager.isCudaAvailable()) {
        Logger::info("CUDA not available, disabling GPU acceleration");
        globalConfig_.enabled = false;
        
        // Disable GPU for all models
        for (auto& pair : modelConfigs_) {
            pair.second.useGPU = false;
        }
        
        return true;
    }
    
    // Find the best GPU device
    int recommendedDevice = gpuManager.getRecommendedDevice();
    if (recommendedDevice < 0) {
        Logger::warn("No suitable GPU device found");
        globalConfig_.enabled = false;
        return false;
    }
    
    auto deviceInfo = gpuManager.getDeviceInfo(recommendedDevice);
    
    // Configure global settings based on GPU capabilities
    globalConfig_.enabled = true;
    globalConfig_.deviceId = recommendedDevice;
    globalConfig_.memoryLimitMB = std::min(static_cast<size_t>(deviceInfo.totalMemoryMB * 0.8), 
                                          static_cast<size_t>(8192)); // Max 8GB or 80% of total
    globalConfig_.enableMemoryPool = deviceInfo.totalMemoryMB >= 4096; // Enable pool for 4GB+ GPUs
    globalConfig_.memoryPoolSizeMB = std::min(static_cast<size_t>(1024), 
                                             globalConfig_.memoryLimitMB / 4);
    
    // Configure model-specific settings
    ModelGPUConfig whisperConfig;
    whisperConfig.useGPU = true;
    whisperConfig.deviceId = recommendedDevice;
    whisperConfig.batchSize = deviceInfo.totalMemoryMB >= 8192 ? 4 : 1;
    whisperConfig.enableQuantization = deviceInfo.totalMemoryMB < 6144; // Enable for <6GB GPUs
    whisperConfig.precision = deviceInfo.totalMemoryMB >= 8192 ? "fp32" : "fp16";
    setModelConfig(MODEL_WHISPER, whisperConfig);
    
    ModelGPUConfig marianConfig;
    marianConfig.useGPU = true;
    marianConfig.deviceId = recommendedDevice;
    marianConfig.batchSize = deviceInfo.totalMemoryMB >= 6144 ? 2 : 1;
    marianConfig.enableQuantization = deviceInfo.totalMemoryMB < 4096;
    marianConfig.precision = deviceInfo.totalMemoryMB >= 6144 ? "fp32" : "fp16";
    setModelConfig(MODEL_MARIAN, marianConfig);
    
    ModelGPUConfig coquiConfig;
    coquiConfig.useGPU = true;
    coquiConfig.deviceId = recommendedDevice;
    coquiConfig.batchSize = 1; // TTS typically uses batch size 1
    coquiConfig.enableQuantization = deviceInfo.totalMemoryMB < 4096;
    coquiConfig.precision = "fp32"; // TTS often needs higher precision
    setModelConfig(MODEL_COQUI, coquiConfig);
    
    Logger::info("Auto-detected optimal GPU configuration for device: " + deviceInfo.name);
    return true;
}

bool GPUConfigManager::validateConfig() const {
    // Validate global config
    if (globalConfig_.enabled) {
        if (!isValidDeviceId(globalConfig_.deviceId)) {
            Logger::error("Invalid GPU device ID: " + std::to_string(globalConfig_.deviceId));
            return false;
        }
        
        if (!isValidMemoryLimit(globalConfig_.memoryLimitMB)) {
            Logger::error("Invalid memory limit: " + std::to_string(globalConfig_.memoryLimitMB) + "MB");
            return false;
        }
    }
    
    // Validate model configs
    for (const auto& pair : modelConfigs_) {
        const auto& config = pair.second;
        
        if (config.useGPU) {
            if (!isValidDeviceId(config.deviceId)) {
                Logger::error("Invalid device ID for model " + pair.first + ": " + 
                            std::to_string(config.deviceId));
                return false;
            }
            
            if (!isValidPrecision(config.precision)) {
                Logger::error("Invalid precision for model " + pair.first + ": " + config.precision);
                return false;
            }
            
            if (config.batchSize <= 0 || config.batchSize > 32) {
                Logger::error("Invalid batch size for model " + pair.first + ": " + 
                            std::to_string(config.batchSize));
                return false;
            }
        }
    }
    
    return true;
}

GPUConfig GPUConfigManager::getRecommendedConfig() const {
    auto& gpuManager = GPUManager::getInstance();
    
    GPUConfig recommended;
    
    if (!gpuManager.isCudaAvailable()) {
        recommended.enabled = false;
        return recommended;
    }
    
    int bestDevice = gpuManager.getRecommendedDevice();
    if (bestDevice < 0) {
        recommended.enabled = false;
        return recommended;
    }
    
    auto deviceInfo = gpuManager.getDeviceInfo(bestDevice);
    
    recommended.enabled = true;
    recommended.deviceId = bestDevice;
    recommended.memoryLimitMB = static_cast<size_t>(deviceInfo.totalMemoryMB * 0.8);
    recommended.enableMemoryPool = deviceInfo.totalMemoryMB >= 4096;
    recommended.memoryPoolSizeMB = std::min(static_cast<size_t>(1024), 
                                           recommended.memoryLimitMB / 4);
    recommended.enableProfiling = false; // Disabled by default for performance
    
    return recommended;
}

std::map<std::string, ModelGPUConfig> GPUConfigManager::getAllModelConfigs() const {
    return modelConfigs_;
}

void GPUConfigManager::resetToDefaults() {
    setDefaultConfigs();
    Logger::info("GPU configuration reset to defaults");
}

std::string GPUConfigManager::toJSON() const {
    std::ostringstream json;
    
    json << "{\n";
    json << "  \"global\": {\n";
    json << "    \"enabled\": " << (globalConfig_.enabled ? "true" : "false") << ",\n";
    json << "    \"deviceId\": " << globalConfig_.deviceId << ",\n";
    json << "    \"memoryLimitMB\": " << globalConfig_.memoryLimitMB << ",\n";
    json << "    \"enableMemoryPool\": " << (globalConfig_.enableMemoryPool ? "true" : "false") << ",\n";
    json << "    \"memoryPoolSizeMB\": " << globalConfig_.memoryPoolSizeMB << ",\n";
    json << "    \"enableProfiling\": " << (globalConfig_.enableProfiling ? "true" : "false") << "\n";
    json << "  },\n";
    
    json << "  \"models\": {\n";
    
    size_t modelCount = 0;
    for (const auto& pair : modelConfigs_) {
        const auto& config = pair.second;
        
        json << "    \"" << pair.first << "\": {\n";
        json << "      \"useGPU\": " << (config.useGPU ? "true" : "false") << ",\n";
        json << "      \"deviceId\": " << config.deviceId << ",\n";
        json << "      \"batchSize\": " << config.batchSize << ",\n";
        json << "      \"enableQuantization\": " << (config.enableQuantization ? "true" : "false") << ",\n";
        json << "      \"precision\": \"" << config.precision << "\"\n";
        json << "    }";
        
        if (++modelCount < modelConfigs_.size()) {
            json << ",";
        }
        json << "\n";
    }
    
    json << "  }\n";
    json << "}\n";
    
    return json.str();
}

bool GPUConfigManager::fromJSON(const std::string& json) {
    try {
        // Parse global config
        size_t globalStart = json.find("\"global\"");
        if (globalStart != std::string::npos) {
            size_t globalBlockStart = json.find("{", globalStart);
            size_t globalBlockEnd = json.find("}", globalBlockStart);
            
            if (globalBlockStart != std::string::npos && globalBlockEnd != std::string::npos) {
                std::string globalBlock = json.substr(globalBlockStart, 
                                                     globalBlockEnd - globalBlockStart + 1);
                
                globalConfig_.enabled = extractJsonBool(globalBlock, "enabled", false);
                globalConfig_.deviceId = extractJsonInt(globalBlock, "deviceId", 0);
                globalConfig_.memoryLimitMB = extractJsonSize(globalBlock, "memoryLimitMB", 4096);
                globalConfig_.enableMemoryPool = extractJsonBool(globalBlock, "enableMemoryPool", true);
                globalConfig_.memoryPoolSizeMB = extractJsonSize(globalBlock, "memoryPoolSizeMB", 1024);
                globalConfig_.enableProfiling = extractJsonBool(globalBlock, "enableProfiling", false);
            }
        }
        
        // Parse model configs
        size_t modelsStart = json.find("\"models\"");
        if (modelsStart != std::string::npos) {
            // Simple parsing for known models
            std::vector<std::string> modelNames = {MODEL_WHISPER, MODEL_MARIAN, MODEL_COQUI};
            
            for (const auto& modelName : modelNames) {
                size_t modelStart = json.find("\"" + modelName + "\"", modelsStart);
                if (modelStart != std::string::npos) {
                    size_t modelBlockStart = json.find("{", modelStart);
                    size_t modelBlockEnd = json.find("}", modelBlockStart);
                    
                    if (modelBlockStart != std::string::npos && modelBlockEnd != std::string::npos) {
                        std::string modelBlock = json.substr(modelBlockStart, 
                                                            modelBlockEnd - modelBlockStart + 1);
                        
                        ModelGPUConfig config;
                        config.useGPU = extractJsonBool(modelBlock, "useGPU", false);
                        config.deviceId = extractJsonInt(modelBlock, "deviceId", 0);
                        config.batchSize = extractJsonInt(modelBlock, "batchSize", 1);
                        config.enableQuantization = extractJsonBool(modelBlock, "enableQuantization", false);
                        config.precision = extractJsonValue(modelBlock, "precision");
                        if (config.precision.empty()) config.precision = "fp32";
                        
                        modelConfigs_[modelName] = config;
                    }
                }
            }
        }
        
        Logger::info("GPU configuration loaded from JSON");
        return validateConfig();
        
    } catch (const std::exception& e) {
        Logger::error("Failed to parse GPU configuration JSON: " + std::string(e.what()));
        setDefaultConfigs();
        return false;
    }
}

void GPUConfigManager::setDefaultConfigs() {
    // Set default global config
    globalConfig_ = GPUConfig();
    
    // Set default model configs
    modelConfigs_.clear();
    
    ModelGPUConfig defaultModelConfig;
    modelConfigs_[MODEL_WHISPER] = defaultModelConfig;
    modelConfigs_[MODEL_MARIAN] = defaultModelConfig;
    modelConfigs_[MODEL_COQUI] = defaultModelConfig;
}

bool GPUConfigManager::isValidDeviceId(int deviceId) const {
    auto& gpuManager = GPUManager::getInstance();
    return deviceId >= 0 && deviceId < gpuManager.getDeviceCount();
}

bool GPUConfigManager::isValidMemoryLimit(size_t memoryMB) const {
    return memoryMB >= 512 && memoryMB <= 32768; // Between 512MB and 32GB
}

bool GPUConfigManager::isValidPrecision(const std::string& precision) const {
    return precision == "fp32" || precision == "fp16" || precision == "int8";
}

} // namespace utils
} // namespace speechrnt