#include "mt/mt_config.hpp"
#include "utils/json_utils.hpp"
#include "utils/logging.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <thread>

namespace speechrnt {
namespace mt {

MTConfig::MTConfig() 
    : configVersion_("1.0.0")
    , lastModified_(std::chrono::system_clock::now())
    , configSource_("default")
    , environment_("development")
    , modelsBasePath_("data/marian/") {
    
    // Initialize default supported languages
    languageDetectionConfig_.supportedLanguages = {
        "en", "es", "fr", "de", "it", "pt", "ru", "zh", "ja", "ko", 
        "ar", "hi", "th", "vi", "nl", "sv", "da", "no", "fi", "pl"
    };
    
    // Initialize default fallback languages
    languageDetectionConfig_.fallbackLanguages = {
        {"unknown", "en"},
        {"auto", "en"},
        {"zh-cn", "zh"},
        {"zh-tw", "zh"}
    };
}

bool MTConfig::loadFromFile(const std::string& configPath) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    try {
        if (!std::filesystem::exists(configPath)) {
            utils::Logger::error("Configuration file not found: " + configPath);
            return false;
        }
        
        std::ifstream file(configPath);
        if (!file.is_open()) {
            utils::Logger::error("Failed to open configuration file: " + configPath);
            return false;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string jsonContent = buffer.str();
        
        bool result = loadFromJson(jsonContent);
        if (result) {
            configSource_ = configPath;
            lastModified_ = std::chrono::system_clock::now();
            utils::Logger::info("Successfully loaded MT configuration from: " + configPath);
        }
        
        return result;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to load configuration file: " + std::string(e.what()));
        return false;
    }
}

bool MTConfig::saveToFile(const std::string& configPath) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    try {
        // Create directory if it doesn't exist
        std::filesystem::path filePath(configPath);
        std::filesystem::create_directories(filePath.parent_path());
        
        std::ofstream file(configPath);
        if (!file.is_open()) {
            utils::Logger::error("Failed to create configuration file: " + configPath);
            return false;
        }
        
        std::string jsonContent = toJson();
        file << jsonContent;
        
        utils::Logger::info("Successfully saved MT configuration to: " + configPath);
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to save configuration file: " + std::string(e.what()));
        return false;
    }
}

bool MTConfig::loadFromJson(const std::string& jsonContent) {
    try {
        utils::JsonValue root = utils::JsonParser::parse(jsonContent);
        
        if (root.getType() != utils::JsonType::OBJECT) {
            utils::Logger::error("Invalid JSON configuration: root must be an object");
            return false;
        }
        
        const auto& rootObj = root.asObject();
        
        // Load configuration version
        if (rootObj.find("version") != rootObj.end()) {
            configVersion_ = rootObj.at("version").asString();
        }
        
        // Load environment
        if (rootObj.find("environment") != rootObj.end()) {
            environment_ = rootObj.at("environment").asString();
        }
        
        // Load models base path
        if (rootObj.find("modelsBasePath") != rootObj.end()) {
            modelsBasePath_ = rootObj.at("modelsBasePath").asString();
        }
        
        // Load GPU configuration
        if (rootObj.find("gpu") != rootObj.end()) {
            const auto& gpuObj = rootObj.at("gpu").asObject();
            
            if (gpuObj.find("enabled") != gpuObj.end()) {
                gpuConfig_.enabled = gpuObj.at("enabled").asBool();
            }
            if (gpuObj.find("fallbackToCPU") != gpuObj.end()) {
                gpuConfig_.fallbackToCPU = gpuObj.at("fallbackToCPU").asBool();
            }
            if (gpuObj.find("defaultDeviceId") != gpuObj.end()) {
                gpuConfig_.defaultDeviceId = static_cast<int>(gpuObj.at("defaultDeviceId").asNumber());
            }
            if (gpuObj.find("memoryPoolSizeMB") != gpuObj.end()) {
                gpuConfig_.memoryPoolSizeMB = static_cast<size_t>(gpuObj.at("memoryPoolSizeMB").asNumber());
            }
            if (gpuObj.find("maxModelMemoryMB") != gpuObj.end()) {
                gpuConfig_.maxModelMemoryMB = static_cast<size_t>(gpuObj.at("maxModelMemoryMB").asNumber());
            }
            if (gpuObj.find("memoryReservationRatio") != gpuObj.end()) {
                gpuConfig_.memoryReservationRatio = static_cast<float>(gpuObj.at("memoryReservationRatio").asNumber());
            }
            if (gpuObj.find("allowedDeviceIds") != gpuObj.end()) {
                const auto& deviceArray = gpuObj.at("allowedDeviceIds").asArray();
                gpuConfig_.allowedDeviceIds.clear();
                for (const auto& device : deviceArray) {
                    gpuConfig_.allowedDeviceIds.push_back(static_cast<int>(device.asNumber()));
                }
            }
        }
        
        // Load quality configuration
        if (rootObj.find("quality") != rootObj.end()) {
            const auto& qualityObj = rootObj.at("quality").asObject();
            
            if (qualityObj.find("enabled") != qualityObj.end()) {
                qualityConfig_.enabled = qualityObj.at("enabled").asBool();
            }
            if (qualityObj.find("highQualityThreshold") != qualityObj.end()) {
                qualityConfig_.highQualityThreshold = static_cast<float>(qualityObj.at("highQualityThreshold").asNumber());
            }
            if (qualityObj.find("mediumQualityThreshold") != qualityObj.end()) {
                qualityConfig_.mediumQualityThreshold = static_cast<float>(qualityObj.at("mediumQualityThreshold").asNumber());
            }
            if (qualityObj.find("lowQualityThreshold") != qualityObj.end()) {
                qualityConfig_.lowQualityThreshold = static_cast<float>(qualityObj.at("lowQualityThreshold").asNumber());
            }
            if (qualityObj.find("generateAlternatives") != qualityObj.end()) {
                qualityConfig_.generateAlternatives = qualityObj.at("generateAlternatives").asBool();
            }
            if (qualityObj.find("maxAlternatives") != qualityObj.end()) {
                qualityConfig_.maxAlternatives = static_cast<int>(qualityObj.at("maxAlternatives").asNumber());
            }
            if (qualityObj.find("enableFallbackTranslation") != qualityObj.end()) {
                qualityConfig_.enableFallbackTranslation = qualityObj.at("enableFallbackTranslation").asBool();
            }
        }
        
        // Load caching configuration
        if (rootObj.find("caching") != rootObj.end()) {
            const auto& cachingObj = rootObj.at("caching").asObject();
            
            if (cachingObj.find("enabled") != cachingObj.end()) {
                cachingConfig_.enabled = cachingObj.at("enabled").asBool();
            }
            if (cachingObj.find("maxCacheSize") != cachingObj.end()) {
                cachingConfig_.maxCacheSize = static_cast<size_t>(cachingObj.at("maxCacheSize").asNumber());
            }
            if (cachingObj.find("cacheExpirationTimeMinutes") != cachingObj.end()) {
                cachingConfig_.cacheExpirationTime = std::chrono::minutes(
                    static_cast<int>(cachingObj.at("cacheExpirationTimeMinutes").asNumber()));
            }
            if (cachingObj.find("persistToDisk") != cachingObj.end()) {
                cachingConfig_.persistToDisk = cachingObj.at("persistToDisk").asBool();
            }
            if (cachingObj.find("cacheDirectory") != cachingObj.end()) {
                cachingConfig_.cacheDirectory = cachingObj.at("cacheDirectory").asString();
            }
        }
        
        // Load batch configuration
        if (rootObj.find("batch") != rootObj.end()) {
            const auto& batchObj = rootObj.at("batch").asObject();
            
            if (batchObj.find("maxBatchSize") != batchObj.end()) {
                batchConfig_.maxBatchSize = static_cast<size_t>(batchObj.at("maxBatchSize").asNumber());
            }
            if (batchObj.find("batchTimeoutMs") != batchObj.end()) {
                batchConfig_.batchTimeout = std::chrono::milliseconds(
                    static_cast<int>(batchObj.at("batchTimeoutMs").asNumber()));
            }
            if (batchObj.find("enableBatchOptimization") != batchObj.end()) {
                batchConfig_.enableBatchOptimization = batchObj.at("enableBatchOptimization").asBool();
            }
            if (batchObj.find("optimalBatchSize") != batchObj.end()) {
                batchConfig_.optimalBatchSize = static_cast<size_t>(batchObj.at("optimalBatchSize").asNumber());
            }
        }
        
        // Load streaming configuration
        if (rootObj.find("streaming") != rootObj.end()) {
            const auto& streamingObj = rootObj.at("streaming").asObject();
            
            if (streamingObj.find("enabled") != streamingObj.end()) {
                streamingConfig_.enabled = streamingObj.at("enabled").asBool();
            }
            if (streamingObj.find("sessionTimeoutMinutes") != streamingObj.end()) {
                streamingConfig_.sessionTimeout = std::chrono::minutes(
                    static_cast<int>(streamingObj.at("sessionTimeoutMinutes").asNumber()));
            }
            if (streamingObj.find("maxConcurrentSessions") != streamingObj.end()) {
                streamingConfig_.maxConcurrentSessions = static_cast<size_t>(streamingObj.at("maxConcurrentSessions").asNumber());
            }
            if (streamingObj.find("maxContextLength") != streamingObj.end()) {
                streamingConfig_.maxContextLength = static_cast<size_t>(streamingObj.at("maxContextLength").asNumber());
            }
            if (streamingObj.find("enableContextPreservation") != streamingObj.end()) {
                streamingConfig_.enableContextPreservation = streamingObj.at("enableContextPreservation").asBool();
            }
        }
        
        // Load error handling configuration
        if (rootObj.find("errorHandling") != rootObj.end()) {
            const auto& errorObj = rootObj.at("errorHandling").asObject();
            
            if (errorObj.find("enableRetry") != errorObj.end()) {
                errorHandlingConfig_.enableRetry = errorObj.at("enableRetry").asBool();
            }
            if (errorObj.find("maxRetryAttempts") != errorObj.end()) {
                errorHandlingConfig_.maxRetryAttempts = static_cast<int>(errorObj.at("maxRetryAttempts").asNumber());
            }
            if (errorObj.find("initialRetryDelayMs") != errorObj.end()) {
                errorHandlingConfig_.initialRetryDelay = std::chrono::milliseconds(
                    static_cast<int>(errorObj.at("initialRetryDelayMs").asNumber()));
            }
            if (errorObj.find("retryBackoffMultiplier") != errorObj.end()) {
                errorHandlingConfig_.retryBackoffMultiplier = static_cast<float>(errorObj.at("retryBackoffMultiplier").asNumber());
            }
            if (errorObj.find("maxRetryDelayMs") != errorObj.end()) {
                errorHandlingConfig_.maxRetryDelay = std::chrono::milliseconds(
                    static_cast<int>(errorObj.at("maxRetryDelayMs").asNumber()));
            }
            if (errorObj.find("translationTimeoutMs") != errorObj.end()) {
                errorHandlingConfig_.translationTimeout = std::chrono::milliseconds(
                    static_cast<int>(errorObj.at("translationTimeoutMs").asNumber()));
            }
            if (errorObj.find("enableDegradedMode") != errorObj.end()) {
                errorHandlingConfig_.enableDegradedMode = errorObj.at("enableDegradedMode").asBool();
            }
            if (errorObj.find("enableFallbackTranslation") != errorObj.end()) {
                errorHandlingConfig_.enableFallbackTranslation = errorObj.at("enableFallbackTranslation").asBool();
            }
        }
        
        // Load performance configuration
        if (rootObj.find("performance") != rootObj.end()) {
            const auto& perfObj = rootObj.at("performance").asObject();
            
            if (perfObj.find("enabled") != perfObj.end()) {
                performanceConfig_.enabled = perfObj.at("enabled").asBool();
            }
            if (perfObj.find("metricsCollectionIntervalSeconds") != perfObj.end()) {
                performanceConfig_.metricsCollectionInterval = std::chrono::seconds(
                    static_cast<int>(perfObj.at("metricsCollectionIntervalSeconds").asNumber()));
            }
            if (perfObj.find("enableLatencyTracking") != perfObj.end()) {
                performanceConfig_.enableLatencyTracking = perfObj.at("enableLatencyTracking").asBool();
            }
            if (perfObj.find("enableThroughputTracking") != perfObj.end()) {
                performanceConfig_.enableThroughputTracking = perfObj.at("enableThroughputTracking").asBool();
            }
            if (perfObj.find("enableResourceUsageTracking") != perfObj.end()) {
                performanceConfig_.enableResourceUsageTracking = perfObj.at("enableResourceUsageTracking").asBool();
            }
            if (perfObj.find("maxMetricsHistorySize") != perfObj.end()) {
                performanceConfig_.maxMetricsHistorySize = static_cast<size_t>(perfObj.at("maxMetricsHistorySize").asNumber());
            }
        }
        
        // Load language detection configuration
        if (rootObj.find("languageDetection") != rootObj.end()) {
            const auto& langDetObj = rootObj.at("languageDetection").asObject();
            
            if (langDetObj.find("enabled") != langDetObj.end()) {
                languageDetectionConfig_.enabled = langDetObj.at("enabled").asBool();
            }
            if (langDetObj.find("confidenceThreshold") != langDetObj.end()) {
                languageDetectionConfig_.confidenceThreshold = static_cast<float>(langDetObj.at("confidenceThreshold").asNumber());
            }
            if (langDetObj.find("detectionMethod") != langDetObj.end()) {
                languageDetectionConfig_.detectionMethod = langDetObj.at("detectionMethod").asString();
            }
            if (langDetObj.find("enableHybridDetection") != langDetObj.end()) {
                languageDetectionConfig_.enableHybridDetection = langDetObj.at("enableHybridDetection").asBool();
            }
            if (langDetObj.find("hybridWeightText") != langDetObj.end()) {
                languageDetectionConfig_.hybridWeightText = static_cast<float>(langDetObj.at("hybridWeightText").asNumber());
            }
            if (langDetObj.find("hybridWeightAudio") != langDetObj.end()) {
                languageDetectionConfig_.hybridWeightAudio = static_cast<float>(langDetObj.at("hybridWeightAudio").asNumber());
            }
            
            // Load supported languages array
            if (langDetObj.find("supportedLanguages") != langDetObj.end()) {
                const auto& langArray = langDetObj.at("supportedLanguages").asArray();
                languageDetectionConfig_.supportedLanguages.clear();
                for (const auto& lang : langArray) {
                    languageDetectionConfig_.supportedLanguages.push_back(lang.asString());
                }
            }
            
            // Load fallback languages mapping
            if (langDetObj.find("fallbackLanguages") != langDetObj.end()) {
                const auto& fallbackObj = langDetObj.at("fallbackLanguages").asObject();
                languageDetectionConfig_.fallbackLanguages.clear();
                for (const auto& pair : fallbackObj) {
                    languageDetectionConfig_.fallbackLanguages[pair.first] = pair.second.asString();
                }
            }
        }
        
        // Load model configurations
        if (rootObj.find("models") != rootObj.end()) {
            const auto& modelsObj = rootObj.at("models").asObject();
            
            for (const auto& modelPair : modelsObj) {
                const std::string& languagePairKey = modelPair.first;
                const auto& modelObj = modelPair.second.asObject();
                
                MarianModelConfig config;
                
                if (modelObj.find("modelPath") != modelObj.end()) {
                    config.modelPath = modelObj.at("modelPath").asString();
                }
                if (modelObj.find("vocabPath") != modelObj.end()) {
                    config.vocabPath = modelObj.at("vocabPath").asString();
                }
                if (modelObj.find("configPath") != modelObj.end()) {
                    config.configPath = modelObj.at("configPath").asString();
                }
                if (modelObj.find("modelType") != modelObj.end()) {
                    config.modelType = modelObj.at("modelType").asString();
                }
                if (modelObj.find("domain") != modelObj.end()) {
                    config.domain = modelObj.at("domain").asString();
                }
                if (modelObj.find("accuracy") != modelObj.end()) {
                    config.accuracy = static_cast<float>(modelObj.at("accuracy").asNumber());
                }
                if (modelObj.find("estimatedSizeMB") != modelObj.end()) {
                    config.estimatedSizeMB = static_cast<size_t>(modelObj.at("estimatedSizeMB").asNumber());
                }
                if (modelObj.find("quantized") != modelObj.end()) {
                    config.quantized = modelObj.at("quantized").asBool();
                }
                if (modelObj.find("quantizationType") != modelObj.end()) {
                    config.quantizationType = modelObj.at("quantizationType").asString();
                }
                
                modelConfigs_[languagePairKey] = config;
            }
        }
        
        // Load custom model paths
        if (rootObj.find("customModelPaths") != rootObj.end()) {
            const auto& customPathsObj = rootObj.at("customModelPaths").asObject();
            
            for (const auto& pathPair : customPathsObj) {
                customModelPaths_[pathPair.first] = pathPair.second.asString();
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to parse JSON configuration: " + std::string(e.what()));
        return false;
    }
}

std::string MTConfig::toJson() const {
    std::ostringstream json;
    
    json << "{\n";
    json << "  \"version\": \"" << configVersion_ << "\",\n";
    json << "  \"environment\": \"" << environment_ << "\",\n";
    json << "  \"modelsBasePath\": \"" << modelsBasePath_ << "\",\n";
    
    // GPU configuration
    json << "  \"gpu\": {\n";
    json << "    \"enabled\": " << (gpuConfig_.enabled ? "true" : "false") << ",\n";
    json << "    \"fallbackToCPU\": " << (gpuConfig_.fallbackToCPU ? "true" : "false") << ",\n";
    json << "    \"defaultDeviceId\": " << gpuConfig_.defaultDeviceId << ",\n";
    json << "    \"memoryPoolSizeMB\": " << gpuConfig_.memoryPoolSizeMB << ",\n";
    json << "    \"maxModelMemoryMB\": " << gpuConfig_.maxModelMemoryMB << ",\n";
    json << "    \"memoryReservationRatio\": " << gpuConfig_.memoryReservationRatio << ",\n";
    json << "    \"allowedDeviceIds\": [";
    for (size_t i = 0; i < gpuConfig_.allowedDeviceIds.size(); ++i) {
        if (i > 0) json << ", ";
        json << gpuConfig_.allowedDeviceIds[i];
    }
    json << "]\n";
    json << "  },\n";
    
    // Quality configuration
    json << "  \"quality\": {\n";
    json << "    \"enabled\": " << (qualityConfig_.enabled ? "true" : "false") << ",\n";
    json << "    \"highQualityThreshold\": " << qualityConfig_.highQualityThreshold << ",\n";
    json << "    \"mediumQualityThreshold\": " << qualityConfig_.mediumQualityThreshold << ",\n";
    json << "    \"lowQualityThreshold\": " << qualityConfig_.lowQualityThreshold << ",\n";
    json << "    \"generateAlternatives\": " << (qualityConfig_.generateAlternatives ? "true" : "false") << ",\n";
    json << "    \"maxAlternatives\": " << qualityConfig_.maxAlternatives << ",\n";
    json << "    \"enableFallbackTranslation\": " << (qualityConfig_.enableFallbackTranslation ? "true" : "false") << "\n";
    json << "  },\n";
    
    // Caching configuration
    json << "  \"caching\": {\n";
    json << "    \"enabled\": " << (cachingConfig_.enabled ? "true" : "false") << ",\n";
    json << "    \"maxCacheSize\": " << cachingConfig_.maxCacheSize << ",\n";
    json << "    \"cacheExpirationTimeMinutes\": " << cachingConfig_.cacheExpirationTime.count() << ",\n";
    json << "    \"persistToDisk\": " << (cachingConfig_.persistToDisk ? "true" : "false") << ",\n";
    json << "    \"cacheDirectory\": \"" << cachingConfig_.cacheDirectory << "\"\n";
    json << "  },\n";
    
    // Batch configuration
    json << "  \"batch\": {\n";
    json << "    \"maxBatchSize\": " << batchConfig_.maxBatchSize << ",\n";
    json << "    \"batchTimeoutMs\": " << batchConfig_.batchTimeout.count() << ",\n";
    json << "    \"enableBatchOptimization\": " << (batchConfig_.enableBatchOptimization ? "true" : "false") << ",\n";
    json << "    \"optimalBatchSize\": " << batchConfig_.optimalBatchSize << "\n";
    json << "  },\n";
    
    // Streaming configuration
    json << "  \"streaming\": {\n";
    json << "    \"enabled\": " << (streamingConfig_.enabled ? "true" : "false") << ",\n";
    json << "    \"sessionTimeoutMinutes\": " << streamingConfig_.sessionTimeout.count() << ",\n";
    json << "    \"maxConcurrentSessions\": " << streamingConfig_.maxConcurrentSessions << ",\n";
    json << "    \"maxContextLength\": " << streamingConfig_.maxContextLength << ",\n";
    json << "    \"enableContextPreservation\": " << (streamingConfig_.enableContextPreservation ? "true" : "false") << "\n";
    json << "  },\n";
    
    // Error handling configuration
    json << "  \"errorHandling\": {\n";
    json << "    \"enableRetry\": " << (errorHandlingConfig_.enableRetry ? "true" : "false") << ",\n";
    json << "    \"maxRetryAttempts\": " << errorHandlingConfig_.maxRetryAttempts << ",\n";
    json << "    \"initialRetryDelayMs\": " << errorHandlingConfig_.initialRetryDelay.count() << ",\n";
    json << "    \"retryBackoffMultiplier\": " << errorHandlingConfig_.retryBackoffMultiplier << ",\n";
    json << "    \"maxRetryDelayMs\": " << errorHandlingConfig_.maxRetryDelay.count() << ",\n";
    json << "    \"translationTimeoutMs\": " << errorHandlingConfig_.translationTimeout.count() << ",\n";
    json << "    \"enableDegradedMode\": " << (errorHandlingConfig_.enableDegradedMode ? "true" : "false") << ",\n";
    json << "    \"enableFallbackTranslation\": " << (errorHandlingConfig_.enableFallbackTranslation ? "true" : "false") << "\n";
    json << "  },\n";
    
    // Performance configuration
    json << "  \"performance\": {\n";
    json << "    \"enabled\": " << (performanceConfig_.enabled ? "true" : "false") << ",\n";
    json << "    \"metricsCollectionIntervalSeconds\": " << performanceConfig_.metricsCollectionInterval.count() << ",\n";
    json << "    \"enableLatencyTracking\": " << (performanceConfig_.enableLatencyTracking ? "true" : "false") << ",\n";
    json << "    \"enableThroughputTracking\": " << (performanceConfig_.enableThroughputTracking ? "true" : "false") << ",\n";
    json << "    \"enableResourceUsageTracking\": " << (performanceConfig_.enableResourceUsageTracking ? "true" : "false") << ",\n";
    json << "    \"maxMetricsHistorySize\": " << performanceConfig_.maxMetricsHistorySize << "\n";
    json << "  },\n";
    
    // Language detection configuration
    json << "  \"languageDetection\": {\n";
    json << "    \"enabled\": " << (languageDetectionConfig_.enabled ? "true" : "false") << ",\n";
    json << "    \"confidenceThreshold\": " << languageDetectionConfig_.confidenceThreshold << ",\n";
    json << "    \"detectionMethod\": \"" << languageDetectionConfig_.detectionMethod << "\",\n";
    json << "    \"enableHybridDetection\": " << (languageDetectionConfig_.enableHybridDetection ? "true" : "false") << ",\n";
    json << "    \"hybridWeightText\": " << languageDetectionConfig_.hybridWeightText << ",\n";
    json << "    \"hybridWeightAudio\": " << languageDetectionConfig_.hybridWeightAudio << ",\n";
    json << "    \"supportedLanguages\": [";
    for (size_t i = 0; i < languageDetectionConfig_.supportedLanguages.size(); ++i) {
        if (i > 0) json << ", ";
        json << "\"" << languageDetectionConfig_.supportedLanguages[i] << "\"";
    }
    json << "],\n";
    json << "    \"fallbackLanguages\": {\n";
    bool first = true;
    for (const auto& pair : languageDetectionConfig_.fallbackLanguages) {
        if (!first) json << ",\n";
        json << "      \"" << pair.first << "\": \"" << pair.second << "\"";
        first = false;
    }
    json << "\n    }\n";
    json << "  },\n";
    
    // Model configurations
    json << "  \"models\": {\n";
    first = true;
    for (const auto& modelPair : modelConfigs_) {
        if (!first) json << ",\n";
        const auto& config = modelPair.second;
        json << "    \"" << modelPair.first << "\": {\n";
        json << "      \"modelPath\": \"" << config.modelPath << "\",\n";
        json << "      \"vocabPath\": \"" << config.vocabPath << "\",\n";
        json << "      \"configPath\": \"" << config.configPath << "\",\n";
        json << "      \"modelType\": \"" << config.modelType << "\",\n";
        json << "      \"domain\": \"" << config.domain << "\",\n";
        json << "      \"accuracy\": " << config.accuracy << ",\n";
        json << "      \"estimatedSizeMB\": " << config.estimatedSizeMB << ",\n";
        json << "      \"quantized\": " << (config.quantized ? "true" : "false") << ",\n";
        json << "      \"quantizationType\": \"" << config.quantizationType << "\"\n";
        json << "    }";
        first = false;
    }
    json << "\n  },\n";
    
    // Custom model paths
    json << "  \"customModelPaths\": {\n";
    first = true;
    for (const auto& pathPair : customModelPaths_) {
        if (!first) json << ",\n";
        json << "    \"" << pathPair.first << "\": \"" << pathPair.second << "\"";
        first = false;
    }
    json << "\n  }\n";
    
    json << "}";
    
    return json.str();
}

bool MTConfig::updateConfiguration(const std::string& jsonUpdates) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    try {
        utils::JsonValue updates = utils::JsonParser::parse(jsonUpdates);
        
        if (updates.getType() != utils::JsonType::OBJECT) {
            utils::Logger::error("Invalid configuration update: must be a JSON object");
            return false;
        }
        
        const auto& updatesObj = updates.asObject();
        
        // Apply updates to each configuration section
        for (const auto& update : updatesObj) {
            const std::string& section = update.first;
            
            if (section == "gpu") {
                // Update GPU configuration
                const auto& gpuUpdates = update.second.asObject();
                for (const auto& gpuUpdate : gpuUpdates) {
                    if (gpuUpdate.first == "enabled") {
                        gpuConfig_.enabled = gpuUpdate.second.asBool();
                    } else if (gpuUpdate.first == "fallbackToCPU") {
                        gpuConfig_.fallbackToCPU = gpuUpdate.second.asBool();
                    } else if (gpuUpdate.first == "defaultDeviceId") {
                        gpuConfig_.defaultDeviceId = static_cast<int>(gpuUpdate.second.asNumber());
                    } else if (gpuUpdate.first == "memoryPoolSizeMB") {
                        gpuConfig_.memoryPoolSizeMB = static_cast<size_t>(gpuUpdate.second.asNumber());
                    } else if (gpuUpdate.first == "maxModelMemoryMB") {
                        gpuConfig_.maxModelMemoryMB = static_cast<size_t>(gpuUpdate.second.asNumber());
                    } else if (gpuUpdate.first == "memoryReservationRatio") {
                        gpuConfig_.memoryReservationRatio = static_cast<float>(gpuUpdate.second.asNumber());
                    }
                }
                notifyConfigChange("gpu", "");
                
            } else if (section == "quality") {
                // Update quality configuration
                const auto& qualityUpdates = update.second.asObject();
                for (const auto& qualityUpdate : qualityUpdates) {
                    if (qualityUpdate.first == "enabled") {
                        qualityConfig_.enabled = qualityUpdate.second.asBool();
                    } else if (qualityUpdate.first == "highQualityThreshold") {
                        qualityConfig_.highQualityThreshold = static_cast<float>(qualityUpdate.second.asNumber());
                    } else if (qualityUpdate.first == "mediumQualityThreshold") {
                        qualityConfig_.mediumQualityThreshold = static_cast<float>(qualityUpdate.second.asNumber());
                    } else if (qualityUpdate.first == "lowQualityThreshold") {
                        qualityConfig_.lowQualityThreshold = static_cast<float>(qualityUpdate.second.asNumber());
                    } else if (qualityUpdate.first == "generateAlternatives") {
                        qualityConfig_.generateAlternatives = qualityUpdate.second.asBool();
                    } else if (qualityUpdate.first == "maxAlternatives") {
                        qualityConfig_.maxAlternatives = static_cast<int>(qualityUpdate.second.asNumber());
                    } else if (qualityUpdate.first == "enableFallbackTranslation") {
                        qualityConfig_.enableFallbackTranslation = qualityUpdate.second.asBool();
                    }
                }
                notifyConfigChange("quality", "");
                
            } else if (section == "caching") {
                // Update caching configuration
                const auto& cachingUpdates = update.second.asObject();
                for (const auto& cachingUpdate : cachingUpdates) {
                    if (cachingUpdate.first == "enabled") {
                        cachingConfig_.enabled = cachingUpdate.second.asBool();
                    } else if (cachingUpdate.first == "maxCacheSize") {
                        cachingConfig_.maxCacheSize = static_cast<size_t>(cachingUpdate.second.asNumber());
                    } else if (cachingUpdate.first == "cacheExpirationTimeMinutes") {
                        cachingConfig_.cacheExpirationTime = std::chrono::minutes(
                            static_cast<int>(cachingUpdate.second.asNumber()));
                    } else if (cachingUpdate.first == "persistToDisk") {
                        cachingConfig_.persistToDisk = cachingUpdate.second.asBool();
                    } else if (cachingUpdate.first == "cacheDirectory") {
                        cachingConfig_.cacheDirectory = cachingUpdate.second.asString();
                    }
                }
                notifyConfigChange("caching", "");
                
            } else if (section == "modelsBasePath") {
                modelsBasePath_ = update.second.asString();
                notifyConfigChange("models", "basePath");
                
            } else if (section == "environment") {
                environment_ = update.second.asString();
                notifyConfigChange("general", "environment");
            }
        }
        
        lastModified_ = std::chrono::system_clock::now();
        
        // Validate the updated configuration
        if (!validate()) {
            utils::Logger::error("Configuration validation failed after update");
            return false;
        }
        
        utils::Logger::info("Configuration updated successfully");
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to update configuration: " + std::string(e.what()));
        return false;
    }
}

bool MTConfig::validate() const {
    std::vector<std::string> errors = getValidationErrors();
    return errors.empty();
}

std::vector<std::string> MTConfig::getValidationErrors() const {
    std::vector<std::string> errors;
    
    // Validate GPU configuration
    if (gpuConfig_.defaultDeviceId < 0) {
        errors.push_back("GPU defaultDeviceId must be non-negative");
    }
    if (gpuConfig_.memoryPoolSizeMB == 0) {
        errors.push_back("GPU memoryPoolSizeMB must be greater than 0");
    }
    if (gpuConfig_.memoryReservationRatio < 0.0f || gpuConfig_.memoryReservationRatio > 1.0f) {
        errors.push_back("GPU memoryReservationRatio must be between 0.0 and 1.0");
    }
    
    // Validate quality configuration
    if (qualityConfig_.highQualityThreshold < qualityConfig_.mediumQualityThreshold) {
        errors.push_back("Quality highQualityThreshold must be >= mediumQualityThreshold");
    }
    if (qualityConfig_.mediumQualityThreshold < qualityConfig_.lowQualityThreshold) {
        errors.push_back("Quality mediumQualityThreshold must be >= lowQualityThreshold");
    }
    if (qualityConfig_.maxAlternatives < 0) {
        errors.push_back("Quality maxAlternatives must be non-negative");
    }
    
    // Validate caching configuration
    if (cachingConfig_.maxCacheSize == 0) {
        errors.push_back("Caching maxCacheSize must be greater than 0");
    }
    
    // Validate batch configuration
    if (batchConfig_.maxBatchSize == 0) {
        errors.push_back("Batch maxBatchSize must be greater than 0");
    }
    if (batchConfig_.optimalBatchSize > batchConfig_.maxBatchSize) {
        errors.push_back("Batch optimalBatchSize must be <= maxBatchSize");
    }
    
    // Validate streaming configuration
    if (streamingConfig_.maxConcurrentSessions == 0) {
        errors.push_back("Streaming maxConcurrentSessions must be greater than 0");
    }
    
    // Validate error handling configuration
    if (errorHandlingConfig_.maxRetryAttempts < 0) {
        errors.push_back("ErrorHandling maxRetryAttempts must be non-negative");
    }
    if (errorHandlingConfig_.retryBackoffMultiplier <= 0.0f) {
        errors.push_back("ErrorHandling retryBackoffMultiplier must be positive");
    }
    
    // Validate language detection configuration
    if (languageDetectionConfig_.confidenceThreshold < 0.0f || languageDetectionConfig_.confidenceThreshold > 1.0f) {
        errors.push_back("LanguageDetection confidenceThreshold must be between 0.0 and 1.0");
    }
    if (languageDetectionConfig_.hybridWeightText + languageDetectionConfig_.hybridWeightAudio != 1.0f) {
        errors.push_back("LanguageDetection hybrid weights must sum to 1.0");
    }
    
    return errors;
}

void MTConfig::setEnvironment(const std::string& environment) {
    std::lock_guard<std::mutex> lock(configMutex_);
    environment_ = environment;
    lastModified_ = std::chrono::system_clock::now();
    notifyConfigChange("general", "environment");
}

std::string MTConfig::getEnvironment() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return environment_;
}

bool MTConfig::loadEnvironmentOverrides(const std::string& environment) {
    std::string envConfigPath = getEnvironmentConfigPath(environment);
    
    if (!std::filesystem::exists(envConfigPath)) {
        utils::Logger::info("No environment-specific configuration found for: " + environment);
        return true; // Not an error if no environment config exists
    }
    
    try {
        std::ifstream file(envConfigPath);
        if (!file.is_open()) {
            utils::Logger::error("Failed to open environment configuration: " + envConfigPath);
            return false;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string jsonContent = buffer.str();
        
        // Apply environment overrides
        bool result = updateConfiguration(jsonContent);
        if (result) {
            utils::Logger::info("Applied environment overrides for: " + environment);
        }
        
        return result;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to load environment overrides: " + std::string(e.what()));
        return false;
    }
}

void MTConfig::setModelsBasePath(const std::string& basePath) {
    std::lock_guard<std::mutex> lock(configMutex_);
    modelsBasePath_ = basePath;
    if (!modelsBasePath_.empty() && modelsBasePath_.back() != '/') {
        modelsBasePath_ += '/';
    }
    lastModified_ = std::chrono::system_clock::now();
    notifyConfigChange("models", "basePath");
}

std::string MTConfig::getModelsBasePath() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return modelsBasePath_;
}

void MTConfig::setCustomModelPath(const std::string& sourceLang, const std::string& targetLang, const std::string& modelPath) {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    customModelPaths_[key] = modelPath;
    lastModified_ = std::chrono::system_clock::now();
    notifyConfigChange("models", "customPath");
}

std::string MTConfig::getModelPath(const std::string& sourceLang, const std::string& targetLang) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    
    // Check for custom model path first
    auto customIt = customModelPaths_.find(key);
    if (customIt != customModelPaths_.end()) {
        return customIt->second;
    }
    
    // Check for configured model path
    auto configIt = modelConfigs_.find(key);
    if (configIt != modelConfigs_.end() && !configIt->second.modelPath.empty()) {
        return configIt->second.modelPath;
    }
    
    // Default path construction
    return modelsBasePath_ + sourceLang + "-" + targetLang;
}

bool MTConfig::hasCustomModelPath(const std::string& sourceLang, const std::string& targetLang) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    return customModelPaths_.find(key) != customModelPaths_.end();
}

void MTConfig::addLanguagePair(const std::string& sourceLang, const std::string& targetLang, const MarianModelConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    modelConfigs_[key] = config;
    lastModified_ = std::chrono::system_clock::now();
    notifyConfigChange("models", "languagePair");
}

void MTConfig::removeLanguagePair(const std::string& sourceLang, const std::string& targetLang) {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    modelConfigs_.erase(key);
    customModelPaths_.erase(key);
    lastModified_ = std::chrono::system_clock::now();
    notifyConfigChange("models", "languagePair");
}

bool MTConfig::hasLanguagePair(const std::string& sourceLang, const std::string& targetLang) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    return modelConfigs_.find(key) != modelConfigs_.end();
}

MarianModelConfig MTConfig::getModelConfig(const std::string& sourceLang, const std::string& targetLang) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    
    auto it = modelConfigs_.find(key);
    if (it != modelConfigs_.end()) {
        return it->second;
    }
    
    // Return default configuration if not found
    MarianModelConfig defaultConfig;
    defaultConfig.modelPath = getModelPath(sourceLang, targetLang);
    return defaultConfig;
}

std::vector<std::pair<std::string, std::string>> MTConfig::getAvailableLanguagePairs() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    std::vector<std::pair<std::string, std::string>> pairs;
    
    for (const auto& modelPair : modelConfigs_) {
        // Parse language pair key (format: "source->target")
        const std::string& key = modelPair.first;
        size_t arrowPos = key.find("->");
        if (arrowPos != std::string::npos) {
            std::string sourceLang = key.substr(0, arrowPos);
            std::string targetLang = key.substr(arrowPos + 2);
            pairs.emplace_back(sourceLang, targetLang);
        }
    }
    
    return pairs;
}

std::vector<std::string> MTConfig::getSupportedSourceLanguages() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    std::set<std::string> sourceLanguages;
    
    for (const auto& modelPair : modelConfigs_) {
        const std::string& key = modelPair.first;
        size_t arrowPos = key.find("->");
        if (arrowPos != std::string::npos) {
            std::string sourceLang = key.substr(0, arrowPos);
            sourceLanguages.insert(sourceLang);
        }
    }
    
    return std::vector<std::string>(sourceLanguages.begin(), sourceLanguages.end());
}

std::vector<std::string> MTConfig::getSupportedTargetLanguages(const std::string& sourceLang) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    std::vector<std::string> targetLanguages;
    
    for (const auto& modelPair : modelConfigs_) {
        const std::string& key = modelPair.first;
        size_t arrowPos = key.find("->");
        if (arrowPos != std::string::npos) {
            std::string keySourceLang = key.substr(0, arrowPos);
            if (keySourceLang == sourceLang) {
                std::string targetLang = key.substr(arrowPos + 2);
                targetLanguages.push_back(targetLang);
            }
        }
    }
    
    return targetLanguages;
}

void MTConfig::registerConfigChangeCallback(const std::string& callbackId, ConfigChangeCallback callback) {
    std::lock_guard<std::mutex> lock(configMutex_);
    changeCallbacks_[callbackId] = callback;
}

void MTConfig::unregisterConfigChangeCallback(const std::string& callbackId) {
    std::lock_guard<std::mutex> lock(configMutex_);
    changeCallbacks_.erase(callbackId);
}

std::shared_ptr<const MTConfig> MTConfig::getSnapshot() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    // Create a deep copy of the current configuration
    auto snapshot = std::make_shared<MTConfig>(*this);
    return snapshot;
}

std::string MTConfig::getLanguagePairKey(const std::string& sourceLang, const std::string& targetLang) const {
    return sourceLang + "->" + targetLang;
}

void MTConfig::notifyConfigChange(const std::string& section, const std::string& key) {
    // Note: This method is called while holding configMutex_
    for (const auto& callbackPair : changeCallbacks_) {
        try {
            callbackPair.second(section, key);
        } catch (const std::exception& e) {
            utils::Logger::error("Configuration change callback failed: " + std::string(e.what()));
        }
    }
}

std::string MTConfig::getEnvironmentConfigPath(const std::string& environment) const {
    return "config/mt_" + environment + ".json";
}

// MTConfigManager implementation

MTConfigManager& MTConfigManager::getInstance() {
    static MTConfigManager instance;
    return instance;
}

bool MTConfigManager::initialize(const std::string& configPath) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    
    configPath_ = configPath;
    config_ = std::make_shared<MTConfig>();
    
    bool result = config_->loadFromFile(configPath);
    if (result) {
        utils::Logger::info("MTConfigManager initialized with config: " + configPath);
    } else {
        utils::Logger::error("Failed to initialize MTConfigManager with config: " + configPath);
    }
    
    return result;
}

bool MTConfigManager::reload() {
    std::lock_guard<std::mutex> lock(managerMutex_);
    
    if (configPath_.empty() || !config_) {
        utils::Logger::error("MTConfigManager not initialized");
        return false;
    }
    
    auto newConfig = std::make_shared<MTConfig>();
    bool result = newConfig->loadFromFile(configPath_);
    
    if (result) {
        config_ = newConfig;
        utils::Logger::info("MTConfigManager configuration reloaded");
    } else {
        utils::Logger::error("Failed to reload MTConfigManager configuration");
    }
    
    return result;
}

void MTConfigManager::shutdown() {
    std::lock_guard<std::mutex> lock(managerMutex_);
    
    stopConfigFileWatcher();
    config_.reset();
    configPath_.clear();
    
    utils::Logger::info("MTConfigManager shut down");
}

std::shared_ptr<const MTConfig> MTConfigManager::getConfig() const {
    std::lock_guard<std::mutex> lock(managerMutex_);
    return config_;
}

bool MTConfigManager::updateConfig(const std::string& jsonUpdates) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    
    if (!config_) {
        utils::Logger::error("MTConfigManager not initialized");
        return false;
    }
    
    return config_->updateConfiguration(jsonUpdates);
}

void MTConfigManager::setEnvironment(const std::string& environment) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    
    currentEnvironment_ = environment;
    
    if (config_) {
        config_->setEnvironment(environment);
        config_->loadEnvironmentOverrides(environment);
    }
}

std::string MTConfigManager::getCurrentEnvironment() const {
    std::lock_guard<std::mutex> lock(managerMutex_);
    return currentEnvironment_;
}

void MTConfigManager::enableConfigFileWatching(bool enabled) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    
    if (enabled && !configFileWatchingEnabled_) {
        startConfigFileWatcher();
    } else if (!enabled && configFileWatchingEnabled_) {
        stopConfigFileWatcher();
    }
    
    configFileWatchingEnabled_ = enabled;
}

bool MTConfigManager::isConfigFileWatchingEnabled() const {
    std::lock_guard<std::mutex> lock(managerMutex_);
    return configFileWatchingEnabled_;
}

void MTConfigManager::startConfigFileWatcher() {
    // Note: This is a simplified implementation
    // In a production system, you would use a proper file watching library
    watcherRunning_ = true;
    watcherThread_ = std::make_unique<std::thread>([this]() {
        auto lastWriteTime = std::filesystem::last_write_time(configPath_);
        
        while (watcherRunning_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            try {
                auto currentWriteTime = std::filesystem::last_write_time(configPath_);
                if (currentWriteTime != lastWriteTime) {
                    lastWriteTime = currentWriteTime;
                    onConfigFileChanged();
                }
            } catch (const std::exception& e) {
                utils::Logger::error("Config file watcher error: " + std::string(e.what()));
            }
        }
    });
}

void MTConfigManager::stopConfigFileWatcher() {
    watcherRunning_ = false;
    if (watcherThread_ && watcherThread_->joinable()) {
        watcherThread_->join();
    }
    watcherThread_.reset();
}

void MTConfigManager::onConfigFileChanged() {
    utils::Logger::info("Configuration file changed, reloading...");
    reload();
}

} // namespace mt
} // namespace speechrnt