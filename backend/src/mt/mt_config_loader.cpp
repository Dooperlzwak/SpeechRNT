#include "mt/mt_config_loader.hpp"
#include "utils/logging.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace speechrnt {
namespace mt {

std::shared_ptr<MTConfig> MTConfigLoader::loadConfiguration(
    const std::string& configPath, 
    const std::string& environment) {
    
    try {
        auto config = std::make_shared<MTConfig>();
        
        // Load main configuration file
        if (!config->loadFromFile(configPath)) {
            utils::Logger::error("Failed to load main configuration from: " + configPath);
            return nullptr;
        }
        
        // Set environment
        config->setEnvironment(environment);
        
        // Apply environment-specific overrides
        if (!applyEnvironmentOverrides(*config, environment)) {
            utils::Logger::warning("Failed to apply environment overrides for: " + environment);
            // Continue with base configuration
        }
        
        // Optimize configuration for the environment
        optimizeForEnvironment(*config, environment);
        
        // Validate the final configuration
        auto errors = validateConfiguration(*config);
        if (!errors.empty()) {
            utils::Logger::error("Configuration validation failed:");
            for (const auto& error : errors) {
                utils::Logger::error("  - " + error);
            }
            return nullptr;
        }
        
        utils::Logger::info("Successfully loaded MT configuration for environment: " + environment);
        return config;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Exception while loading configuration: " + std::string(e.what()));
        return nullptr;
    }
}

std::shared_ptr<MTConfig> MTConfigLoader::loadConfigurationFromJson(
    const std::string& jsonContent,
    const std::string& environment) {
    
    try {
        auto config = std::make_shared<MTConfig>();
        
        // Load from JSON content
        if (!config->loadFromJson(jsonContent)) {
            utils::Logger::error("Failed to parse JSON configuration");
            return nullptr;
        }
        
        // Set environment
        config->setEnvironment(environment);
        
        // Apply environment-specific overrides
        applyEnvironmentOverrides(*config, environment);
        
        // Optimize for environment
        optimizeForEnvironment(*config, environment);
        
        // Validate configuration
        auto errors = validateConfiguration(*config);
        if (!errors.empty()) {
            utils::Logger::error("Configuration validation failed:");
            for (const auto& error : errors) {
                utils::Logger::error("  - " + error);
            }
            return nullptr;
        }
        
        return config;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Exception while loading configuration from JSON: " + std::string(e.what()));
        return nullptr;
    }
}

std::shared_ptr<MTConfig> MTConfigLoader::createDefaultConfiguration(const std::string& environment) {
    auto config = std::make_shared<MTConfig>();
    
    // Set environment
    config->setEnvironment(environment);
    
    // Set default language pairs
    setDefaultLanguagePairs(*config);
    
    // Optimize for environment
    optimizeForEnvironment(*config, environment);
    
    utils::Logger::info("Created default MT configuration for environment: " + environment);
    return config;
}

std::vector<std::string> MTConfigLoader::validateConfiguration(const MTConfig& config) {
    return config.getValidationErrors();
}

std::shared_ptr<MTConfig> MTConfigLoader::mergeConfigurations(
    const MTConfig& base,
    const MTConfig& overlay) {
    
    try {
        // Create a copy of the base configuration
        auto merged = std::make_shared<MTConfig>(base);
        
        // Convert overlay to JSON and apply as updates
        std::string overlayJson = overlay.toJson();
        if (!merged->updateConfiguration(overlayJson)) {
            utils::Logger::error("Failed to merge configurations");
            return nullptr;
        }
        
        return merged;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Exception while merging configurations: " + std::string(e.what()));
        return nullptr;
    }
}

bool MTConfigLoader::saveConfiguration(const MTConfig& config, const std::string& configPath) {
    return config.saveToFile(configPath);
}

std::unordered_map<std::string, std::shared_ptr<MTConfig>> MTConfigLoader::getConfigurationTemplates() {
    std::unordered_map<std::string, std::shared_ptr<MTConfig>> templates;
    
    templates["development"] = createDevelopmentTemplate();
    templates["production"] = createProductionTemplate();
    templates["testing"] = createTestingTemplate();
    templates["high_performance"] = createHighPerformanceTemplate();
    templates["low_resource"] = createLowResourceTemplate();
    
    return templates;
}

bool MTConfigLoader::applyTuningParameters(
    MTConfig& config,
    const std::unordered_map<std::string, std::string>& tuningParams) {
    
    try {
        // Build JSON update string from tuning parameters
        std::ostringstream jsonUpdate;
        jsonUpdate << "{";
        
        bool first = true;
        for (const auto& param : tuningParams) {
            if (!first) jsonUpdate << ",";
            
            const std::string& path = param.first;
            const std::string& value = param.second;
            
            // Parse parameter path (e.g., "gpu.memoryPoolSizeMB")
            size_t dotPos = path.find('.');
            if (dotPos != std::string::npos) {
                std::string section = path.substr(0, dotPos);
                std::string key = path.substr(dotPos + 1);
                
                jsonUpdate << "\"" << section << "\":{\"" << key << "\":";
                
                // Try to determine if value is numeric or string
                try {
                    std::stod(value);
                    jsonUpdate << value; // Numeric value
                } catch (...) {
                    if (value == "true" || value == "false") {
                        jsonUpdate << value; // Boolean value
                    } else {
                        jsonUpdate << "\"" << value << "\""; // String value
                    }
                }
                
                jsonUpdate << "}";
            }
            
            first = false;
        }
        
        jsonUpdate << "}";
        
        // Apply the updates
        return config.updateConfiguration(jsonUpdate.str());
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to apply tuning parameters: " + std::string(e.what()));
        return false;
    }
}

std::unordered_map<std::string, std::string> MTConfigLoader::getParameterDocumentation() {
    std::unordered_map<std::string, std::string> docs;
    
    // GPU configuration documentation
    docs["gpu.enabled"] = "Enable GPU acceleration for translation models";
    docs["gpu.fallbackToCPU"] = "Automatically fallback to CPU if GPU initialization fails";
    docs["gpu.defaultDeviceId"] = "Default GPU device ID to use (0-based)";
    docs["gpu.memoryPoolSizeMB"] = "Size of GPU memory pool in MB";
    docs["gpu.maxModelMemoryMB"] = "Maximum memory per model in MB";
    docs["gpu.memoryReservationRatio"] = "Ratio of GPU memory to reserve (0.0-1.0)";
    
    // Quality configuration documentation
    docs["quality.enabled"] = "Enable translation quality assessment";
    docs["quality.highQualityThreshold"] = "Threshold for high quality translations (0.0-1.0)";
    docs["quality.mediumQualityThreshold"] = "Threshold for medium quality translations (0.0-1.0)";
    docs["quality.lowQualityThreshold"] = "Threshold for low quality translations (0.0-1.0)";
    docs["quality.generateAlternatives"] = "Generate alternative translations for low quality results";
    docs["quality.maxAlternatives"] = "Maximum number of alternative translations to generate";
    
    // Caching configuration documentation
    docs["caching.enabled"] = "Enable translation result caching";
    docs["caching.maxCacheSize"] = "Maximum number of cached translations";
    docs["caching.cacheExpirationTimeMinutes"] = "Cache expiration time in minutes";
    docs["caching.persistToDisk"] = "Persist cache to disk for persistence across restarts";
    docs["caching.cacheDirectory"] = "Directory for persistent cache storage";
    
    // Batch configuration documentation
    docs["batch.maxBatchSize"] = "Maximum number of texts to process in one batch";
    docs["batch.batchTimeoutMs"] = "Timeout for batch processing in milliseconds";
    docs["batch.enableBatchOptimization"] = "Enable batch processing optimizations";
    docs["batch.optimalBatchSize"] = "Optimal batch size for best performance";
    
    // Streaming configuration documentation
    docs["streaming.enabled"] = "Enable streaming translation support";
    docs["streaming.sessionTimeoutMinutes"] = "Streaming session timeout in minutes";
    docs["streaming.maxConcurrentSessions"] = "Maximum number of concurrent streaming sessions";
    docs["streaming.maxContextLength"] = "Maximum context length for streaming translations";
    docs["streaming.enableContextPreservation"] = "Preserve context across streaming chunks";
    
    // Error handling configuration documentation
    docs["errorHandling.enableRetry"] = "Enable automatic retry on translation failures";
    docs["errorHandling.maxRetryAttempts"] = "Maximum number of retry attempts";
    docs["errorHandling.initialRetryDelayMs"] = "Initial retry delay in milliseconds";
    docs["errorHandling.retryBackoffMultiplier"] = "Backoff multiplier for retry delays";
    docs["errorHandling.maxRetryDelayMs"] = "Maximum retry delay in milliseconds";
    docs["errorHandling.translationTimeoutMs"] = "Translation timeout in milliseconds";
    docs["errorHandling.enableDegradedMode"] = "Enable degraded mode operation on critical errors";
    docs["errorHandling.enableFallbackTranslation"] = "Enable fallback translation on model failures";
    
    // Performance configuration documentation
    docs["performance.enabled"] = "Enable performance monitoring";
    docs["performance.metricsCollectionIntervalSeconds"] = "Metrics collection interval in seconds";
    docs["performance.enableLatencyTracking"] = "Track translation latency metrics";
    docs["performance.enableThroughputTracking"] = "Track translation throughput metrics";
    docs["performance.enableResourceUsageTracking"] = "Track resource usage metrics";
    docs["performance.maxMetricsHistorySize"] = "Maximum number of metrics history entries";
    
    // Language detection configuration documentation
    docs["languageDetection.enabled"] = "Enable automatic language detection";
    docs["languageDetection.confidenceThreshold"] = "Minimum confidence threshold for language detection";
    docs["languageDetection.detectionMethod"] = "Language detection method (whisper, text_analysis, hybrid)";
    docs["languageDetection.enableHybridDetection"] = "Enable hybrid detection combining text and audio";
    docs["languageDetection.hybridWeightText"] = "Weight for text-based detection in hybrid mode";
    docs["languageDetection.hybridWeightAudio"] = "Weight for audio-based detection in hybrid mode";
    
    return docs;
}

bool MTConfigLoader::applyEnvironmentOverrides(MTConfig& config, const std::string& environment) {
    std::string envConfigPath = getEnvironmentConfigPath(environment);
    
    if (std::filesystem::exists(envConfigPath)) {
        return config.loadEnvironmentOverrides(environment);
    }
    
    return true; // No overrides to apply
}

std::string MTConfigLoader::getEnvironmentConfigPath(const std::string& environment) {
    return "config/mt_" + environment + ".json";
}

void MTConfigLoader::setDefaultLanguagePairs(MTConfig& config) {
    // Add common language pairs with default configurations
    std::vector<std::pair<std::string, std::string>> defaultPairs = {
        {"en", "es"}, {"es", "en"},
        {"en", "fr"}, {"fr", "en"},
        {"en", "de"}, {"de", "en"},
        {"en", "it"}, {"it", "en"},
        {"en", "pt"}, {"pt", "en"},
        {"en", "ru"}, {"ru", "en"},
        {"en", "zh"}, {"zh", "en"},
        {"en", "ja"}, {"ja", "en"},
        {"en", "ko"}, {"ko", "en"}
    };
    
    for (const auto& pair : defaultPairs) {
        MarianModelConfig modelConfig;
        modelConfig.modelPath = config.getModelsBasePath() + pair.first + "-" + pair.second + "/model.npz";
        modelConfig.vocabPath = config.getModelsBasePath() + pair.first + "-" + pair.second + "/vocab.yml";
        modelConfig.configPath = config.getModelsBasePath() + pair.first + "-" + pair.second + "/config.yml";
        modelConfig.modelType = "transformer";
        modelConfig.domain = "general";
        modelConfig.accuracy = 0.8f;
        modelConfig.estimatedSizeMB = 200;
        
        config.addLanguagePair(pair.first, pair.second, modelConfig);
    }
}

void MTConfigLoader::optimizeForEnvironment(MTConfig& config, const std::string& environment) {
    if (environment == "production") {
        // Production optimizations
        auto gpuConfig = config.getGPUConfig();
        gpuConfig.memoryPoolSizeMB = std::max(gpuConfig.memoryPoolSizeMB, static_cast<size_t>(4096));
        gpuConfig.fallbackToCPU = false; // Strict GPU requirement in production
        config.updateGPUConfig(gpuConfig);
        
        auto cachingConfig = config.getCachingConfig();
        cachingConfig.maxCacheSize = std::max(cachingConfig.maxCacheSize, static_cast<size_t>(5000));
        cachingConfig.persistToDisk = true;
        config.updateCachingConfig(cachingConfig);
        
        auto batchConfig = config.getBatchConfig();
        batchConfig.maxBatchSize = std::max(batchConfig.maxBatchSize, static_cast<size_t>(64));
        config.updateBatchConfig(batchConfig);
        
    } else if (environment == "testing") {
        // Testing optimizations - prefer speed and simplicity
        auto gpuConfig = config.getGPUConfig();
        gpuConfig.enabled = false; // Use CPU for consistent testing
        config.updateGPUConfig(gpuConfig);
        
        auto qualityConfig = config.getQualityConfig();
        qualityConfig.enabled = false; // Skip quality assessment in tests
        config.updateQualityConfig(qualityConfig);
        
        auto cachingConfig = config.getCachingConfig();
        cachingConfig.enabled = false; // No caching in tests
        config.updateCachingConfig(cachingConfig);
        
        auto streamingConfig = config.getStreamingConfig();
        streamingConfig.enabled = false; // No streaming in tests
        config.updateStreamingConfig(streamingConfig);
        
    } else if (environment == "development") {
        // Development optimizations - balance between features and performance
        auto errorConfig = config.getErrorHandlingConfig();
        errorConfig.enableDegradedMode = true;
        errorConfig.enableFallbackTranslation = true;
        config.updateErrorHandlingConfig(errorConfig);
        
        auto performanceConfig = config.getPerformanceConfig();
        performanceConfig.enabled = true; // Enable monitoring for development
        config.updatePerformanceConfig(performanceConfig);
    }
}

std::shared_ptr<MTConfig> MTConfigLoader::createDevelopmentTemplate() {
    auto config = std::make_shared<MTConfig>();
    config->setEnvironment("development");
    
    // Development-friendly settings
    auto gpuConfig = config->getGPUConfig();
    gpuConfig.enabled = true;
    gpuConfig.fallbackToCPU = true;
    config->updateGPUConfig(gpuConfig);
    
    auto errorConfig = config->getErrorHandlingConfig();
    errorConfig.enableDegradedMode = true;
    errorConfig.enableFallbackTranslation = true;
    config->updateErrorHandlingConfig(errorConfig);
    
    setDefaultLanguagePairs(*config);
    return config;
}

std::shared_ptr<MTConfig> MTConfigLoader::createProductionTemplate() {
    auto config = std::make_shared<MTConfig>();
    config->setEnvironment("production");
    
    // Production-optimized settings
    auto gpuConfig = config->getGPUConfig();
    gpuConfig.enabled = true;
    gpuConfig.fallbackToCPU = false;
    gpuConfig.memoryPoolSizeMB = 4096;
    gpuConfig.maxModelMemoryMB = 8192;
    config->updateGPUConfig(gpuConfig);
    
    auto qualityConfig = config->getQualityConfig();
    qualityConfig.highQualityThreshold = 0.85f;
    qualityConfig.mediumQualityThreshold = 0.7f;
    qualityConfig.lowQualityThreshold = 0.5f;
    config->updateQualityConfig(qualityConfig);
    
    auto cachingConfig = config->getCachingConfig();
    cachingConfig.maxCacheSize = 5000;
    cachingConfig.persistToDisk = true;
    config->updateCachingConfig(cachingConfig);
    
    auto batchConfig = config->getBatchConfig();
    batchConfig.maxBatchSize = 64;
    batchConfig.optimalBatchSize = 16;
    config->updateBatchConfig(batchConfig);
    
    auto streamingConfig = config->getStreamingConfig();
    streamingConfig.maxConcurrentSessions = 500;
    streamingConfig.sessionTimeout = std::chrono::minutes(60);
    config->updateStreamingConfig(streamingConfig);
    
    auto errorConfig = config->getErrorHandlingConfig();
    errorConfig.maxRetryAttempts = 5;
    errorConfig.translationTimeout = std::chrono::milliseconds(3000);
    errorConfig.enableDegradedMode = false;
    config->updateErrorHandlingConfig(errorConfig);
    
    setDefaultLanguagePairs(*config);
    return config;
}

std::shared_ptr<MTConfig> MTConfigLoader::createTestingTemplate() {
    auto config = std::make_shared<MTConfig>();
    config->setEnvironment("testing");
    
    // Testing-optimized settings
    auto gpuConfig = config->getGPUConfig();
    gpuConfig.enabled = false;
    config->updateGPUConfig(gpuConfig);
    
    auto qualityConfig = config->getQualityConfig();
    qualityConfig.enabled = false;
    config->updateQualityConfig(qualityConfig);
    
    auto cachingConfig = config->getCachingConfig();
    cachingConfig.enabled = false;
    config->updateCachingConfig(cachingConfig);
    
    auto batchConfig = config->getBatchConfig();
    batchConfig.maxBatchSize = 4;
    batchConfig.optimalBatchSize = 2;
    config->updateBatchConfig(batchConfig);
    
    auto streamingConfig = config->getStreamingConfig();
    streamingConfig.enabled = false;
    config->updateStreamingConfig(streamingConfig);
    
    auto errorConfig = config->getErrorHandlingConfig();
    errorConfig.maxRetryAttempts = 1;
    errorConfig.translationTimeout = std::chrono::milliseconds(10000);
    errorConfig.enableDegradedMode = true;
    config->updateErrorHandlingConfig(errorConfig);
    
    auto performanceConfig = config->getPerformanceConfig();
    performanceConfig.enabled = false;
    config->updatePerformanceConfig(performanceConfig);
    
    setDefaultLanguagePairs(*config);
    return config;
}

std::shared_ptr<MTConfig> MTConfigLoader::createHighPerformanceTemplate() {
    auto config = std::make_shared<MTConfig>();
    config->setEnvironment("high_performance");
    
    // High-performance settings
    auto gpuConfig = config->getGPUConfig();
    gpuConfig.enabled = true;
    gpuConfig.fallbackToCPU = false;
    gpuConfig.memoryPoolSizeMB = 8192;
    gpuConfig.maxModelMemoryMB = 16384;
    gpuConfig.memoryReservationRatio = 0.9f;
    config->updateGPUConfig(gpuConfig);
    
    auto batchConfig = config->getBatchConfig();
    batchConfig.maxBatchSize = 128;
    batchConfig.optimalBatchSize = 32;
    batchConfig.enableBatchOptimization = true;
    config->updateBatchConfig(batchConfig);
    
    auto cachingConfig = config->getCachingConfig();
    cachingConfig.maxCacheSize = 10000;
    cachingConfig.persistToDisk = true;
    config->updateCachingConfig(cachingConfig);
    
    auto streamingConfig = config->getStreamingConfig();
    streamingConfig.maxConcurrentSessions = 1000;
    config->updateStreamingConfig(streamingConfig);
    
    auto errorConfig = config->getErrorHandlingConfig();
    errorConfig.translationTimeout = std::chrono::milliseconds(1000);
    errorConfig.maxRetryAttempts = 2;
    config->updateErrorHandlingConfig(errorConfig);
    
    setDefaultLanguagePairs(*config);
    return config;
}

std::shared_ptr<MTConfig> MTConfigLoader::createLowResourceTemplate() {
    auto config = std::make_shared<MTConfig>();
    config->setEnvironment("low_resource");
    
    // Low-resource settings
    auto gpuConfig = config->getGPUConfig();
    gpuConfig.enabled = false; // Use CPU to save GPU memory
    config->updateGPUConfig(gpuConfig);
    
    auto qualityConfig = config->getQualityConfig();
    qualityConfig.generateAlternatives = false;
    qualityConfig.maxAlternatives = 1;
    config->updateQualityConfig(qualityConfig);
    
    auto cachingConfig = config->getCachingConfig();
    cachingConfig.maxCacheSize = 100;
    cachingConfig.persistToDisk = false;
    config->updateCachingConfig(cachingConfig);
    
    auto batchConfig = config->getBatchConfig();
    batchConfig.maxBatchSize = 4;
    batchConfig.optimalBatchSize = 1;
    config->updateBatchConfig(batchConfig);
    
    auto streamingConfig = config->getStreamingConfig();
    streamingConfig.maxConcurrentSessions = 10;
    streamingConfig.maxContextLength = 100;
    config->updateStreamingConfig(streamingConfig);
    
    auto performanceConfig = config->getPerformanceConfig();
    performanceConfig.enabled = false;
    config->updatePerformanceConfig(performanceConfig);
    
    setDefaultLanguagePairs(*config);
    return config;
}

// MTConfigTuner implementation

bool MTConfigTuner::autoTuneForSystem(
    MTConfig& config,
    size_t availableGPUMemoryMB,
    size_t availableRAMMB,
    int cpuCores) {
    
    try {
        // Tune GPU settings based on available GPU memory
        tuneGPUSettings(config, availableGPUMemoryMB);
        
        // Tune batch settings based on CPU cores and RAM
        tuneBatchSettings(config, cpuCores, availableRAMMB);
        
        // Tune caching settings based on available RAM
        tuneCachingSettings(config, availableRAMMB);
        
        utils::Logger::info("Auto-tuned MT configuration for system resources");
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to auto-tune configuration: " + std::string(e.what()));
        return false;
    }
}

bool MTConfigTuner::tuneForPerformance(
    MTConfig& config,
    int targetLatencyMs,
    int targetThroughputTPS,
    size_t maxMemoryUsageMB) {
    
    try {
        // Tune for low latency
        if (targetLatencyMs < 1000) {
            auto errorConfig = config.getErrorHandlingConfig();
            errorConfig.translationTimeout = std::chrono::milliseconds(targetLatencyMs * 2);
            errorConfig.maxRetryAttempts = 1;
            config.updateErrorHandlingConfig(errorConfig);
            
            auto qualityConfig = config.getQualityConfig();
            qualityConfig.generateAlternatives = false;
            config.updateQualityConfig(qualityConfig);
        }
        
        // Tune for high throughput
        if (targetThroughputTPS > 100) {
            auto batchConfig = config.getBatchConfig();
            batchConfig.maxBatchSize = std::min(static_cast<size_t>(128), maxMemoryUsageMB / 50);
            batchConfig.optimalBatchSize = batchConfig.maxBatchSize / 4;
            config.updateBatchConfig(batchConfig);
            
            auto gpuConfig = config.getGPUConfig();
            gpuConfig.enabled = true;
            gpuConfig.memoryPoolSizeMB = std::min(static_cast<size_t>(4096), maxMemoryUsageMB / 2);
            config.updateGPUConfig(gpuConfig);
        }
        
        // Tune memory usage
        auto cachingConfig = config.getCachingConfig();
        cachingConfig.maxCacheSize = std::min(static_cast<size_t>(5000), maxMemoryUsageMB / 10);
        config.updateCachingConfig(cachingConfig);
        
        utils::Logger::info("Tuned MT configuration for performance targets");
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to tune configuration for performance: " + std::string(e.what()));
        return false;
    }
}

bool MTConfigTuner::tuneForUseCase(MTConfig& config, const std::string& useCase) {
    try {
        if (useCase == "realtime") {
            // Optimize for real-time translation
            auto errorConfig = config.getErrorHandlingConfig();
            errorConfig.translationTimeout = std::chrono::milliseconds(500);
            errorConfig.maxRetryAttempts = 1;
            config.updateErrorHandlingConfig(errorConfig);
            
            auto qualityConfig = config.getQualityConfig();
            qualityConfig.generateAlternatives = false;
            config.updateQualityConfig(qualityConfig);
            
            auto batchConfig = config.getBatchConfig();
            batchConfig.maxBatchSize = 1;
            config.updateBatchConfig(batchConfig);
            
        } else if (useCase == "batch") {
            // Optimize for batch processing
            auto batchConfig = config.getBatchConfig();
            batchConfig.maxBatchSize = 128;
            batchConfig.optimalBatchSize = 32;
            batchConfig.enableBatchOptimization = true;
            config.updateBatchConfig(batchConfig);
            
            auto errorConfig = config.getErrorHandlingConfig();
            errorConfig.translationTimeout = std::chrono::milliseconds(10000);
            errorConfig.maxRetryAttempts = 3;
            config.updateErrorHandlingConfig(errorConfig);
            
        } else if (useCase == "streaming") {
            // Optimize for streaming translation
            auto streamingConfig = config.getStreamingConfig();
            streamingConfig.enabled = true;
            streamingConfig.maxConcurrentSessions = 1000;
            streamingConfig.enableContextPreservation = true;
            config.updateStreamingConfig(streamingConfig);
            
            auto cachingConfig = config.getCachingConfig();
            cachingConfig.maxCacheSize = 10000;
            config.updateCachingConfig(cachingConfig);
            
        } else if (useCase == "quality") {
            // Optimize for translation quality
            auto qualityConfig = config.getQualityConfig();
            qualityConfig.enabled = true;
            qualityConfig.generateAlternatives = true;
            qualityConfig.maxAlternatives = 5;
            qualityConfig.highQualityThreshold = 0.9f;
            config.updateQualityConfig(qualityConfig);
            
            auto errorConfig = config.getErrorHandlingConfig();
            errorConfig.translationTimeout = std::chrono::milliseconds(10000);
            errorConfig.maxRetryAttempts = 5;
            config.updateErrorHandlingConfig(errorConfig);
        }
        
        utils::Logger::info("Tuned MT configuration for use case: " + useCase);
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to tune configuration for use case: " + std::string(e.what()));
        return false;
    }
}

void MTConfigTuner::tuneGPUSettings(MTConfig& config, size_t availableGPUMemoryMB) {
    auto gpuConfig = config.getGPUConfig();
    
    if (availableGPUMemoryMB == 0) {
        gpuConfig.enabled = false;
    } else {
        gpuConfig.enabled = true;
        gpuConfig.memoryPoolSizeMB = std::min(availableGPUMemoryMB / 2, static_cast<size_t>(4096));
        gpuConfig.maxModelMemoryMB = std::min(availableGPUMemoryMB / 4, static_cast<size_t>(2048));
        gpuConfig.memoryReservationRatio = availableGPUMemoryMB > 8192 ? 0.8f : 0.6f;
    }
    
    config.updateGPUConfig(gpuConfig);
}

void MTConfigTuner::tuneBatchSettings(MTConfig& config, int cpuCores, size_t availableRAMMB) {
    auto batchConfig = config.getBatchConfig();
    
    // Scale batch size with CPU cores and available RAM
    size_t maxBatchSize = std::min(
        static_cast<size_t>(cpuCores * 8),
        availableRAMMB / 100  // Assume ~100MB per batch item
    );
    
    batchConfig.maxBatchSize = std::max(static_cast<size_t>(4), maxBatchSize);
    batchConfig.optimalBatchSize = batchConfig.maxBatchSize / 4;
    
    config.updateBatchConfig(batchConfig);
}

void MTConfigTuner::tuneCachingSettings(MTConfig& config, size_t availableRAMMB) {
    auto cachingConfig = config.getCachingConfig();
    
    // Scale cache size with available RAM
    size_t maxCacheSize = availableRAMMB / 10; // Assume ~10MB per 1000 cache entries
    cachingConfig.maxCacheSize = std::max(static_cast<size_t>(100), maxCacheSize);
    
    // Enable disk persistence for large caches
    if (cachingConfig.maxCacheSize > 5000) {
        cachingConfig.persistToDisk = true;
    }
    
    config.updateCachingConfig(cachingConfig);
}

void MTConfigTuner::tuneQualitySettings(MTConfig& config, const std::string& useCase) {
    auto qualityConfig = config.getQualityConfig();
    
    if (useCase == "realtime") {
        qualityConfig.generateAlternatives = false;
        qualityConfig.maxAlternatives = 1;
    } else if (useCase == "quality") {
        qualityConfig.generateAlternatives = true;
        qualityConfig.maxAlternatives = 5;
        qualityConfig.highQualityThreshold = 0.9f;
    }
    
    config.updateQualityConfig(qualityConfig);
}

void MTConfigTuner::tuneErrorHandlingSettings(MTConfig& config, const std::string& useCase) {
    auto errorConfig = config.getErrorHandlingConfig();
    
    if (useCase == "realtime") {
        errorConfig.translationTimeout = std::chrono::milliseconds(500);
        errorConfig.maxRetryAttempts = 1;
    } else if (useCase == "batch") {
        errorConfig.translationTimeout = std::chrono::milliseconds(10000);
        errorConfig.maxRetryAttempts = 3;
    } else if (useCase == "quality") {
        errorConfig.translationTimeout = std::chrono::milliseconds(15000);
        errorConfig.maxRetryAttempts = 5;
    }
    
    config.updateErrorHandlingConfig(errorConfig);
}

} // namespace mt
} // namespace speechrnt