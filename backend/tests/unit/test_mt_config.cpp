#include "mt/mt_config.hpp"
#include "mt/mt_config_loader.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

using namespace speechrnt::mt;

class MTConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for test files
        testDir_ = std::filesystem::temp_directory_path() / "mt_config_test";
        std::filesystem::create_directories(testDir_);
        
        // Create a test configuration file
        testConfigPath_ = testDir_ / "test_config.json";
        createTestConfigFile();
    }
    
    void TearDown() override {
        // Clean up test files
        std::filesystem::remove_all(testDir_);
    }
    
    void createTestConfigFile() {
        std::ofstream file(testConfigPath_);
        file << R"({
            "version": "1.0.0",
            "environment": "testing",
            "modelsBasePath": "test/models/",
            "gpu": {
                "enabled": true,
                "fallbackToCPU": true,
                "defaultDeviceId": 0,
                "memoryPoolSizeMB": 1024,
                "maxModelMemoryMB": 2048,
                "memoryReservationRatio": 0.8,
                "allowedDeviceIds": [0, 1]
            },
            "quality": {
                "enabled": true,
                "highQualityThreshold": 0.8,
                "mediumQualityThreshold": 0.6,
                "lowQualityThreshold": 0.4,
                "generateAlternatives": true,
                "maxAlternatives": 3,
                "enableFallbackTranslation": true
            },
            "caching": {
                "enabled": true,
                "maxCacheSize": 1000,
                "cacheExpirationTimeMinutes": 60,
                "persistToDisk": false,
                "cacheDirectory": "cache/test"
            },
            "models": {
                "en->es": {
                    "modelPath": "test/models/en-es/model.npz",
                    "vocabPath": "test/models/en-es/vocab.yml",
                    "configPath": "test/models/en-es/config.yml",
                    "modelType": "transformer",
                    "domain": "general",
                    "accuracy": 0.85,
                    "estimatedSizeMB": 180,
                    "quantized": false,
                    "quantizationType": ""
                }
            }
        })";
    }
    
    std::filesystem::path testDir_;
    std::filesystem::path testConfigPath_;
};

TEST_F(MTConfigTest, DefaultConstructor) {
    MTConfig config;
    
    EXPECT_EQ(config.getEnvironment(), "development");
    EXPECT_EQ(config.getModelsBasePath(), "data/marian/");
    EXPECT_TRUE(config.getGPUConfig().enabled);
    EXPECT_TRUE(config.getGPUConfig().fallbackToCPU);
    EXPECT_EQ(config.getGPUConfig().defaultDeviceId, 0);
    EXPECT_TRUE(config.getCachingConfig().enabled);
    EXPECT_TRUE(config.getQualityConfig().enabled);
}

TEST_F(MTConfigTest, LoadFromFile) {
    MTConfig config;
    
    ASSERT_TRUE(config.loadFromFile(testConfigPath_.string()));
    
    EXPECT_EQ(config.getEnvironment(), "testing");
    EXPECT_EQ(config.getModelsBasePath(), "test/models/");
    EXPECT_TRUE(config.getGPUConfig().enabled);
    EXPECT_EQ(config.getGPUConfig().memoryPoolSizeMB, 1024);
    EXPECT_EQ(config.getQualityConfig().highQualityThreshold, 0.8f);
    EXPECT_EQ(config.getCachingConfig().maxCacheSize, 1000);
}

TEST_F(MTConfigTest, SaveToFile) {
    MTConfig config;
    config.setEnvironment("test_save");
    config.setModelsBasePath("test/save/models/");
    
    std::filesystem::path saveConfigPath = testDir_ / "save_config.json";
    
    ASSERT_TRUE(config.saveToFile(saveConfigPath.string()));
    ASSERT_TRUE(std::filesystem::exists(saveConfigPath));
    
    // Load the saved configuration and verify
    MTConfig loadedConfig;
    ASSERT_TRUE(loadedConfig.loadFromFile(saveConfigPath.string()));
    
    EXPECT_EQ(loadedConfig.getEnvironment(), "test_save");
    EXPECT_EQ(loadedConfig.getModelsBasePath(), "test/save/models/");
}

TEST_F(MTConfigTest, UpdateConfiguration) {
    MTConfig config;
    
    std::string updates = R"({
        "gpu": {
            "memoryPoolSizeMB": 2048,
            "enabled": false
        },
        "quality": {
            "highQualityThreshold": 0.9
        }
    })";
    
    ASSERT_TRUE(config.updateConfiguration(updates));
    
    EXPECT_FALSE(config.getGPUConfig().enabled);
    EXPECT_EQ(config.getGPUConfig().memoryPoolSizeMB, 2048);
    EXPECT_EQ(config.getQualityConfig().highQualityThreshold, 0.9f);
}

TEST_F(MTConfigTest, LanguagePairManagement) {
    MTConfig config;
    
    // Add a language pair
    MarianModelConfig modelConfig;
    modelConfig.modelPath = "test/en-fr/model.npz";
    modelConfig.accuracy = 0.85f;
    modelConfig.estimatedSizeMB = 200;
    
    config.addLanguagePair("en", "fr", modelConfig);
    
    EXPECT_TRUE(config.hasLanguagePair("en", "fr"));
    EXPECT_FALSE(config.hasLanguagePair("fr", "en"));
    
    auto retrievedConfig = config.getModelConfig("en", "fr");
    EXPECT_EQ(retrievedConfig.modelPath, "test/en-fr/model.npz");
    EXPECT_EQ(retrievedConfig.accuracy, 0.85f);
    
    // Remove the language pair
    config.removeLanguagePair("en", "fr");
    EXPECT_FALSE(config.hasLanguagePair("en", "fr"));
}

TEST_F(MTConfigTest, CustomModelPaths) {
    MTConfig config;
    
    config.setCustomModelPath("en", "es", "/custom/en-es");
    config.setCustomModelPath("es", "en", "/custom/es-en");
    
    EXPECT_TRUE(config.hasCustomModelPath("en", "es"));
    EXPECT_TRUE(config.hasCustomModelPath("es", "en"));
    EXPECT_FALSE(config.hasCustomModelPath("en", "fr"));
    
    EXPECT_EQ(config.getModelPath("en", "es"), "/custom/en-es");
    EXPECT_EQ(config.getModelPath("es", "en"), "/custom/es-en");
    
    // Should return default path for non-custom pairs
    std::string defaultPath = config.getModelPath("en", "fr");
    EXPECT_TRUE(defaultPath.find("en-fr") != std::string::npos);
}

TEST_F(MTConfigTest, ConfigurationValidation) {
    MTConfig config;
    
    // Valid configuration should pass
    EXPECT_TRUE(config.validate());
    EXPECT_TRUE(config.getValidationErrors().empty());
    
    // Invalid GPU configuration
    auto gpuConfig = config.getGPUConfig();
    gpuConfig.memoryReservationRatio = 1.5f; // Invalid: > 1.0
    config.updateGPUConfig(gpuConfig);
    
    EXPECT_FALSE(config.validate());
    auto errors = config.getValidationErrors();
    EXPECT_FALSE(errors.empty());
    EXPECT_TRUE(std::any_of(errors.begin(), errors.end(), 
        [](const std::string& error) { 
            return error.find("memoryReservationRatio") != std::string::npos; 
        }));
}

TEST_F(MTConfigTest, EnvironmentOverrides) {
    MTConfig config;
    config.loadFromFile(testConfigPath_.string());
    
    // Create environment override file
    std::filesystem::path envConfigPath = testDir_ / "test_production.json";
    std::ofstream envFile(envConfigPath);
    envFile << R"({
        "gpu": {
            "memoryPoolSizeMB": 4096,
            "fallbackToCPU": false
        },
        "caching": {
            "maxCacheSize": 5000
        }
    })";
    envFile.close();
    
    // Mock the environment config path
    config.setEnvironment("production");
    
    // The actual environment override loading would happen in the loader
    // Here we just test the update mechanism
    std::string overrides = R"({
        "gpu": {
            "memoryPoolSizeMB": 4096,
            "fallbackToCPU": false
        },
        "caching": {
            "maxCacheSize": 5000
        }
    })";
    
    ASSERT_TRUE(config.updateConfiguration(overrides));
    
    EXPECT_EQ(config.getGPUConfig().memoryPoolSizeMB, 4096);
    EXPECT_FALSE(config.getGPUConfig().fallbackToCPU);
    EXPECT_EQ(config.getCachingConfig().maxCacheSize, 5000);
}

TEST_F(MTConfigTest, ConfigurationSnapshot) {
    MTConfig config;
    config.setEnvironment("snapshot_test");
    
    auto snapshot = config.getSnapshot();
    ASSERT_NE(snapshot, nullptr);
    
    EXPECT_EQ(snapshot->getEnvironment(), "snapshot_test");
    
    // Modify original config
    config.setEnvironment("modified");
    
    // Snapshot should remain unchanged
    EXPECT_EQ(snapshot->getEnvironment(), "snapshot_test");
    EXPECT_EQ(config.getEnvironment(), "modified");
}

// MTConfigLoader tests

class MTConfigLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = std::filesystem::temp_directory_path() / "mt_config_loader_test";
        std::filesystem::create_directories(testDir_);
        
        // Create main config file
        mainConfigPath_ = testDir_ / "main_config.json";
        createMainConfigFile();
        
        // Create environment override file
        envConfigPath_ = testDir_ / "config" / "mt_production.json";
        std::filesystem::create_directories(envConfigPath_.parent_path());
        createEnvironmentConfigFile();
    }
    
    void TearDown() override {
        std::filesystem::remove_all(testDir_);
    }
    
    void createMainConfigFile() {
        std::ofstream file(mainConfigPath_);
        file << R"({
            "version": "1.0.0",
            "environment": "development",
            "gpu": {
                "enabled": true,
                "memoryPoolSizeMB": 1024
            },
            "quality": {
                "highQualityThreshold": 0.8
            }
        })";
    }
    
    void createEnvironmentConfigFile() {
        std::ofstream file(envConfigPath_);
        file << R"({
            "gpu": {
                "memoryPoolSizeMB": 4096,
                "fallbackToCPU": false
            },
            "quality": {
                "highQualityThreshold": 0.9
            }
        })";
    }
    
    std::filesystem::path testDir_;
    std::filesystem::path mainConfigPath_;
    std::filesystem::path envConfigPath_;
};

TEST_F(MTConfigLoaderTest, LoadConfiguration) {
    auto config = MTConfigLoader::loadConfiguration(mainConfigPath_.string(), "development");
    
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->getEnvironment(), "development");
    EXPECT_TRUE(config->getGPUConfig().enabled);
    EXPECT_EQ(config->getGPUConfig().memoryPoolSizeMB, 1024);
}

TEST_F(MTConfigLoaderTest, CreateDefaultConfiguration) {
    auto config = MTConfigLoader::createDefaultConfiguration("testing");
    
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->getEnvironment(), "testing");
    EXPECT_TRUE(config->validate());
}

TEST_F(MTConfigLoaderTest, ConfigurationTemplates) {
    auto templates = MTConfigLoader::getConfigurationTemplates();
    
    EXPECT_FALSE(templates.empty());
    EXPECT_TRUE(templates.find("development") != templates.end());
    EXPECT_TRUE(templates.find("production") != templates.end());
    EXPECT_TRUE(templates.find("testing") != templates.end());
    
    auto devTemplate = templates["development"];
    ASSERT_NE(devTemplate, nullptr);
    EXPECT_EQ(devTemplate->getEnvironment(), "development");
    
    auto prodTemplate = templates["production"];
    ASSERT_NE(prodTemplate, nullptr);
    EXPECT_EQ(prodTemplate->getEnvironment(), "production");
}

TEST_F(MTConfigLoaderTest, ApplyTuningParameters) {
    auto config = MTConfigLoader::createDefaultConfiguration("development");
    
    std::unordered_map<std::string, std::string> tuningParams = {
        {"gpu.memoryPoolSizeMB", "2048"},
        {"batch.maxBatchSize", "64"},
        {"quality.highQualityThreshold", "0.9"}
    };
    
    ASSERT_TRUE(MTConfigLoader::applyTuningParameters(*config, tuningParams));
    
    EXPECT_EQ(config->getGPUConfig().memoryPoolSizeMB, 2048);
    EXPECT_EQ(config->getBatchConfig().maxBatchSize, 64);
    EXPECT_EQ(config->getQualityConfig().highQualityThreshold, 0.9f);
}

TEST_F(MTConfigLoaderTest, MergeConfigurations) {
    auto baseConfig = MTConfigLoader::createDefaultConfiguration("development");
    auto overlayConfig = MTConfigLoader::createDefaultConfiguration("production");
    
    // Modify overlay config
    auto gpuConfig = overlayConfig->getGPUConfig();
    gpuConfig.memoryPoolSizeMB = 8192;
    overlayConfig->updateGPUConfig(gpuConfig);
    
    auto mergedConfig = MTConfigLoader::mergeConfigurations(*baseConfig, *overlayConfig);
    
    ASSERT_NE(mergedConfig, nullptr);
    EXPECT_EQ(mergedConfig->getGPUConfig().memoryPoolSizeMB, 8192);
}

// MTConfigTuner tests

TEST(MTConfigTunerTest, AutoTuneForSystem) {
    auto config = MTConfigLoader::createDefaultConfiguration("development");
    
    size_t availableGPUMemoryMB = 8192;
    size_t availableRAMMB = 32768;
    int cpuCores = 16;
    
    ASSERT_TRUE(MTConfigTuner::autoTuneForSystem(*config, availableGPUMemoryMB, availableRAMMB, cpuCores));
    
    // GPU should be enabled with appropriate memory settings
    EXPECT_TRUE(config->getGPUConfig().enabled);
    EXPECT_GT(config->getGPUConfig().memoryPoolSizeMB, 0);
    EXPECT_LE(config->getGPUConfig().memoryPoolSizeMB, availableGPUMemoryMB / 2);
    
    // Batch size should be scaled with CPU cores
    EXPECT_GT(config->getBatchConfig().maxBatchSize, 4);
    
    // Cache size should be scaled with available RAM
    EXPECT_GT(config->getCachingConfig().maxCacheSize, 100);
}

TEST(MTConfigTunerTest, TuneForPerformance) {
    auto config = MTConfigLoader::createDefaultConfiguration("development");
    
    int targetLatencyMs = 500;
    int targetThroughputTPS = 200;
    size_t maxMemoryUsageMB = 16384;
    
    ASSERT_TRUE(MTConfigTuner::tuneForPerformance(*config, targetLatencyMs, targetThroughputTPS, maxMemoryUsageMB));
    
    // For low latency, should disable alternatives and reduce retry attempts
    EXPECT_FALSE(config->getQualityConfig().generateAlternatives);
    EXPECT_EQ(config->getErrorHandlingConfig().maxRetryAttempts, 1);
    
    // For high throughput, should enable GPU and increase batch size
    EXPECT_TRUE(config->getGPUConfig().enabled);
    EXPECT_GT(config->getBatchConfig().maxBatchSize, 32);
}

TEST(MTConfigTunerTest, TuneForUseCase) {
    auto config = MTConfigLoader::createDefaultConfiguration("development");
    
    // Test real-time use case
    ASSERT_TRUE(MTConfigTuner::tuneForUseCase(*config, "realtime"));
    EXPECT_EQ(config->getBatchConfig().maxBatchSize, 1);
    EXPECT_FALSE(config->getQualityConfig().generateAlternatives);
    EXPECT_LE(config->getErrorHandlingConfig().translationTimeout.count(), 1000);
    
    // Test batch use case
    config = MTConfigLoader::createDefaultConfiguration("development");
    ASSERT_TRUE(MTConfigTuner::tuneForUseCase(*config, "batch"));
    EXPECT_GE(config->getBatchConfig().maxBatchSize, 64);
    EXPECT_TRUE(config->getBatchConfig().enableBatchOptimization);
    
    // Test quality use case
    config = MTConfigLoader::createDefaultConfiguration("development");
    ASSERT_TRUE(MTConfigTuner::tuneForUseCase(*config, "quality"));
    EXPECT_TRUE(config->getQualityConfig().generateAlternatives);
    EXPECT_GE(config->getQualityConfig().maxAlternatives, 3);
    EXPECT_GE(config->getQualityConfig().highQualityThreshold, 0.85f);
}

// MTConfigManager tests

TEST(MTConfigManagerTest, SingletonInstance) {
    auto& manager1 = MTConfigManager::getInstance();
    auto& manager2 = MTConfigManager::getInstance();
    
    EXPECT_EQ(&manager1, &manager2);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}