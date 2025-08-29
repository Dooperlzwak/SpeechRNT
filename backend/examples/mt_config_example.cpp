#include "mt/mt_config.hpp"
#include "mt/mt_config_loader.hpp"
#include "mt/marian_translator.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <memory>

using namespace speechrnt::mt;

void demonstrateBasicConfiguration() {
    std::cout << "\n=== Basic Configuration Demo ===\n";
    
    // Initialize the MT configuration manager
    auto& configManager = MTConfigManager::getInstance();
    
    if (!configManager.initialize("config/mt.json")) {
        std::cout << "Failed to initialize configuration manager\n";
        return;
    }
    
    // Get the current configuration
    auto config = configManager.getConfig();
    if (!config) {
        std::cout << "Failed to get configuration\n";
        return;
    }
    
    std::cout << "Configuration loaded successfully!\n";
    std::cout << "Environment: " << config->getEnvironment() << "\n";
    std::cout << "Models base path: " << config->getModelsBasePath() << "\n";
    std::cout << "GPU enabled: " << (config->getGPUConfig().enabled ? "Yes" : "No") << "\n";
    std::cout << "Max batch size: " << config->getBatchConfig().maxBatchSize << "\n";
    std::cout << "Cache enabled: " << (config->getCachingConfig().enabled ? "Yes" : "No") << "\n";
}

void demonstrateRuntimeConfigurationUpdate() {
    std::cout << "\n=== Runtime Configuration Update Demo ===\n";
    
    auto& configManager = MTConfigManager::getInstance();
    auto config = configManager.getConfig();
    
    if (!config) {
        std::cout << "Configuration not available\n";
        return;
    }
    
    std::cout << "Original max batch size: " << config->getBatchConfig().maxBatchSize << "\n";
    
    // Update configuration at runtime
    std::string configUpdate = R"({
        "batch": {
            "maxBatchSize": 64,
            "optimalBatchSize": 16
        },
        "gpu": {
            "memoryPoolSizeMB": 2048
        }
    })";
    
    if (configManager.updateConfig(configUpdate)) {
        auto updatedConfig = configManager.getConfig();
        std::cout << "Updated max batch size: " << updatedConfig->getBatchConfig().maxBatchSize << "\n";
        std::cout << "Updated GPU memory pool: " << updatedConfig->getGPUConfig().memoryPoolSizeMB << " MB\n";
        std::cout << "Configuration updated successfully!\n";
    } else {
        std::cout << "Failed to update configuration\n";
    }
}

void demonstrateEnvironmentSpecificConfiguration() {
    std::cout << "\n=== Environment-Specific Configuration Demo ===\n";
    
    // Load configuration for different environments
    std::vector<std::string> environments = {"development", "production", "testing"};
    
    for (const auto& env : environments) {
        std::cout << "\nLoading configuration for environment: " << env << "\n";
        
        auto config = MTConfigLoader::loadConfiguration("config/mt.json", env);
        if (config) {
            std::cout << "  Environment: " << config->getEnvironment() << "\n";
            std::cout << "  GPU enabled: " << (config->getGPUConfig().enabled ? "Yes" : "No") << "\n";
            std::cout << "  GPU fallback to CPU: " << (config->getGPUConfig().fallbackToCPU ? "Yes" : "No") << "\n";
            std::cout << "  Max batch size: " << config->getBatchConfig().maxBatchSize << "\n";
            std::cout << "  Quality assessment: " << (config->getQualityConfig().enabled ? "Enabled" : "Disabled") << "\n";
            std::cout << "  Caching: " << (config->getCachingConfig().enabled ? "Enabled" : "Disabled") << "\n";
            std::cout << "  Streaming: " << (config->getStreamingConfig().enabled ? "Enabled" : "Disabled") << "\n";
        } else {
            std::cout << "  Failed to load configuration for " << env << "\n";
        }
    }
}

void demonstrateCustomModelPaths() {
    std::cout << "\n=== Custom Model Paths Demo ===\n";
    
    auto config = std::make_shared<MTConfig>();
    
    // Set custom model paths
    config->setCustomModelPath("en", "es", "/custom/models/en-es-medical");
    config->setCustomModelPath("es", "en", "/custom/models/es-en-medical");
    
    std::cout << "Custom model path for en->es: " << config->getModelPath("en", "es") << "\n";
    std::cout << "Custom model path for es->en: " << config->getModelPath("es", "en") << "\n";
    std::cout << "Default model path for en->fr: " << config->getModelPath("en", "fr") << "\n";
    
    std::cout << "Has custom path for en->es: " << (config->hasCustomModelPath("en", "es") ? "Yes" : "No") << "\n";
    std::cout << "Has custom path for en->fr: " << (config->hasCustomModelPath("en", "fr") ? "Yes" : "No") << "\n";
}

void demonstrateConfigurationTuning() {
    std::cout << "\n=== Configuration Tuning Demo ===\n";
    
    auto config = MTConfigLoader::createDefaultConfiguration("development");
    
    std::cout << "Original configuration:\n";
    std::cout << "  Max batch size: " << config->getBatchConfig().maxBatchSize << "\n";
    std::cout << "  GPU memory pool: " << config->getGPUConfig().memoryPoolSizeMB << " MB\n";
    std::cout << "  Cache size: " << config->getCachingConfig().maxCacheSize << "\n";
    
    // Apply tuning parameters
    std::unordered_map<std::string, std::string> tuningParams = {
        {"batch.maxBatchSize", "128"},
        {"gpu.memoryPoolSizeMB", "4096"},
        {"caching.maxCacheSize", "5000"},
        {"quality.highQualityThreshold", "0.9"}
    };
    
    if (MTConfigLoader::applyTuningParameters(*config, tuningParams)) {
        std::cout << "\nTuned configuration:\n";
        std::cout << "  Max batch size: " << config->getBatchConfig().maxBatchSize << "\n";
        std::cout << "  GPU memory pool: " << config->getGPUConfig().memoryPoolSizeMB << " MB\n";
        std::cout << "  Cache size: " << config->getCachingConfig().maxCacheSize << "\n";
        std::cout << "  High quality threshold: " << config->getQualityConfig().highQualityThreshold << "\n";
        std::cout << "Configuration tuning applied successfully!\n";
    } else {
        std::cout << "Failed to apply configuration tuning\n";
    }
}

void demonstrateAutoTuning() {
    std::cout << "\n=== Auto-Tuning Demo ===\n";
    
    auto config = MTConfigLoader::createDefaultConfiguration("development");
    
    std::cout << "Original configuration:\n";
    std::cout << "  GPU enabled: " << (config->getGPUConfig().enabled ? "Yes" : "No") << "\n";
    std::cout << "  Max batch size: " << config->getBatchConfig().maxBatchSize << "\n";
    std::cout << "  Cache size: " << config->getCachingConfig().maxCacheSize << "\n";
    
    // Simulate system resources
    size_t availableGPUMemoryMB = 8192;  // 8GB GPU
    size_t availableRAMMB = 32768;       // 32GB RAM
    int cpuCores = 16;                   // 16 CPU cores
    
    if (MTConfigTuner::autoTuneForSystem(*config, availableGPUMemoryMB, availableRAMMB, cpuCores)) {
        std::cout << "\nAuto-tuned configuration:\n";
        std::cout << "  GPU enabled: " << (config->getGPUConfig().enabled ? "Yes" : "No") << "\n";
        std::cout << "  GPU memory pool: " << config->getGPUConfig().memoryPoolSizeMB << " MB\n";
        std::cout << "  Max batch size: " << config->getBatchConfig().maxBatchSize << "\n";
        std::cout << "  Cache size: " << config->getCachingConfig().maxCacheSize << "\n";
        std::cout << "Auto-tuning completed successfully!\n";
    } else {
        std::cout << "Failed to auto-tune configuration\n";
    }
}

void demonstrateConfigurationTemplates() {
    std::cout << "\n=== Configuration Templates Demo ===\n";
    
    auto templates = MTConfigLoader::getConfigurationTemplates();
    
    for (const auto& templatePair : templates) {
        const std::string& templateName = templatePair.first;
        const auto& config = templatePair.second;
        
        std::cout << "\nTemplate: " << templateName << "\n";
        std::cout << "  Environment: " << config->getEnvironment() << "\n";
        std::cout << "  GPU enabled: " << (config->getGPUConfig().enabled ? "Yes" : "No") << "\n";
        std::cout << "  Max batch size: " << config->getBatchConfig().maxBatchSize << "\n";
        std::cout << "  Quality assessment: " << (config->getQualityConfig().enabled ? "Enabled" : "Disabled") << "\n";
        std::cout << "  Caching: " << (config->getCachingConfig().enabled ? "Enabled" : "Disabled") << "\n";
    }
}

void demonstrateTranslatorIntegration() {
    std::cout << "\n=== Translator Integration Demo ===\n";
    
    // Load configuration
    auto config = MTConfigLoader::loadConfiguration("config/mt.json", "development");
    if (!config) {
        std::cout << "Failed to load configuration\n";
        return;
    }
    
    // Create translator with configuration
    MarianTranslator translator(config);
    
    std::cout << "Created MarianTranslator with configuration\n";
    std::cout << "Configuration environment: " << translator.getConfiguration()->getEnvironment() << "\n";
    std::cout << "GPU acceleration enabled: " << (translator.isGPUAccelerationEnabled() ? "Yes" : "No") << "\n";
    
    // Update configuration at runtime
    auto newConfig = std::make_shared<MTConfig>(*config);
    auto gpuConfig = newConfig->getGPUConfig();
    gpuConfig.memoryPoolSizeMB = 4096;
    newConfig->updateGPUConfig(gpuConfig);
    
    if (translator.updateConfiguration(newConfig)) {
        std::cout << "Updated translator configuration successfully\n";
        std::cout << "New GPU memory pool size: " << 
                     translator.getConfiguration()->getGPUConfig().memoryPoolSizeMB << " MB\n";
    } else {
        std::cout << "Failed to update translator configuration\n";
    }
}

void demonstrateConfigurationValidation() {
    std::cout << "\n=== Configuration Validation Demo ===\n";
    
    // Create a configuration with some invalid values
    auto config = std::make_shared<MTConfig>();
    
    // Set invalid values
    auto gpuConfig = config->getGPUConfig();
    gpuConfig.memoryReservationRatio = 1.5f; // Invalid: > 1.0
    config->updateGPUConfig(gpuConfig);
    
    auto qualityConfig = config->getQualityConfig();
    qualityConfig.highQualityThreshold = 0.5f;
    qualityConfig.mediumQualityThreshold = 0.8f; // Invalid: medium > high
    config->updateQualityConfig(qualityConfig);
    
    auto batchConfig = config->getBatchConfig();
    batchConfig.maxBatchSize = 0; // Invalid: must be > 0
    config->updateBatchConfig(batchConfig);
    
    // Validate configuration
    auto errors = MTConfigLoader::validateConfiguration(*config);
    
    if (errors.empty()) {
        std::cout << "Configuration is valid\n";
    } else {
        std::cout << "Configuration validation failed with " << errors.size() << " errors:\n";
        for (const auto& error : errors) {
            std::cout << "  - " << error << "\n";
        }
    }
}

int main() {
    std::cout << "MT Configuration System Demo\n";
    std::cout << "============================\n";
    
    try {
        demonstrateBasicConfiguration();
        demonstrateRuntimeConfigurationUpdate();
        demonstrateEnvironmentSpecificConfiguration();
        demonstrateCustomModelPaths();
        demonstrateConfigurationTuning();
        demonstrateAutoTuning();
        demonstrateConfigurationTemplates();
        demonstrateTranslatorIntegration();
        demonstrateConfigurationValidation();
        
        std::cout << "\n=== Demo completed successfully! ===\n";
        
    } catch (const std::exception& e) {
        std::cout << "Demo failed with exception: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}