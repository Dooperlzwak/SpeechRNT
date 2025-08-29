#include <gtest/gtest.h>
#include "models/model_manager.hpp"
#include "utils/gpu_manager.hpp"
#include "utils/logging.hpp"
#include <filesystem>
#include <fstream>

using namespace speechrnt::models;
using namespace speechrnt::utils;

class ModelManagerTask2Test : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test model directory
        testModelPath_ = "test_models/en-es";
        std::filesystem::create_directories(testModelPath_);
        
        // Create dummy model files
        std::ofstream modelFile(testModelPath_ + "/model.bin");
        modelFile << "dummy model data";
        modelFile.close();
        
        std::ofstream vocabFile(testModelPath_ + "/vocab.yml");
        vocabFile << "dummy vocab data";
        vocabFile.close();
        
        // Create metadata file
        std::ofstream metadataFile(testModelPath_ + "/metadata.json");
        metadataFile << R"({
            "version": "1.0.0",
            "checksum": "dummy_checksum",
            "architecture": "transformer",
            "sourceLanguage": "en",
            "targetLanguage": "es",
            "parameterCount": 1000000
        })";
        metadataFile.close();
        
        // Initialize managers
        modelManager_ = std::make_unique<ModelManager>(2048, 5); // 2GB, 5 models max
        
        Logger::info("Test setup completed");
    }
    
    void TearDown() override {
        modelManager_.reset();
        
        // Clean up test files
        if (std::filesystem::exists(testModelPath_)) {
            std::filesystem::remove_all(testModelPath_);
        }
        
        Logger::info("Test cleanup completed");
    }
    
    std::string testModelPath_;
    std::unique_ptr<ModelManager> modelManager_;
};

TEST_F(ModelManagerTask2Test, BasicModelLoading) {
    // Test basic model loading with real implementation
    EXPECT_TRUE(modelManager_->loadModel("en", "es", testModelPath_));
    EXPECT_TRUE(modelManager_->isModelLoaded("en", "es"));
    
    auto modelInfo = modelManager_->getModel("en", "es");
    ASSERT_NE(modelInfo, nullptr);
    EXPECT_TRUE(modelInfo->loaded);
    EXPECT_EQ(modelInfo->languagePair, "en->es");
    EXPECT_GT(modelInfo->memoryUsage, 0);
    
    // Test unloading
    EXPECT_TRUE(modelManager_->unloadModel("en", "es"));
    EXPECT_FALSE(modelManager_->isModelLoaded("en", "es"));
}

TEST_F(ModelManagerTask2Test, GPUModelLoading) {
    // Test GPU model loading
    auto& gpuManager = GPUManager::getInstance();
    gpuManager.initialize();
    
    bool hasGPU = gpuManager.isCudaAvailable();
    
    // Test GPU loading (should fallback to CPU if no GPU available)
    EXPECT_TRUE(modelManager_->loadModelWithGPU("en", "es", testModelPath_, true, -1));
    EXPECT_TRUE(modelManager_->isModelLoaded("en", "es"));
    
    auto modelInfo = modelManager_->getModel("en", "es");
    ASSERT_NE(modelInfo, nullptr);
    EXPECT_TRUE(modelInfo->loaded);
    
    if (hasGPU) {
        Logger::info("GPU available - testing GPU loading");
        // If GPU is available, it should be used
        // Note: In test environment, GPU might not be available
    } else {
        Logger::info("No GPU available - testing CPU fallback");
        // Should fallback to CPU
        EXPECT_FALSE(modelInfo->useGPU);
        EXPECT_EQ(modelInfo->gpuDeviceId, -1);
    }
}

TEST_F(ModelManagerTask2Test, QuantizationModelLoading) {
    // Test quantization model loading
    EXPECT_TRUE(modelManager_->loadModelWithQuantization("en", "es", testModelPath_, QuantizationType::FP16));
    EXPECT_TRUE(modelManager_->isModelLoaded("en", "es"));
    
    auto modelInfo = modelManager_->getModel("en", "es");
    ASSERT_NE(modelInfo, nullptr);
    EXPECT_TRUE(modelInfo->loaded);
    EXPECT_EQ(modelInfo->quantization, QuantizationType::FP16);
    
    // Memory usage should be reduced due to quantization
    size_t originalSize = modelManager_->estimateModelMemoryUsage(testModelPath_);
    EXPECT_LT(modelInfo->memoryUsage, originalSize);
}

TEST_F(ModelManagerTask2Test, AdvancedModelLoading) {
    // Test advanced model loading with both GPU and quantization
    EXPECT_TRUE(modelManager_->loadModelAdvanced("en", "es", testModelPath_, 
                                                true, -1, QuantizationType::INT8));
    EXPECT_TRUE(modelManager_->isModelLoaded("en", "es"));
    
    auto modelInfo = modelManager_->getModel("en", "es");
    ASSERT_NE(modelInfo, nullptr);
    EXPECT_TRUE(modelInfo->loaded);
    EXPECT_EQ(modelInfo->quantization, QuantizationType::INT8);
    
    // Memory usage should be significantly reduced due to INT8 quantization
    size_t originalSize = modelManager_->estimateModelMemoryUsage(testModelPath_);
    EXPECT_LT(modelInfo->memoryUsage, originalSize * 0.5); // Should be less than half
}

TEST_F(ModelManagerTask2Test, ModelValidation) {
    // Test model validation
    EXPECT_TRUE(modelManager_->validateModelIntegrity(testModelPath_));
    
    // Test with non-existent model
    EXPECT_FALSE(modelManager_->validateModelIntegrity("non_existent_model"));
}

TEST_F(ModelManagerTask2Test, QuantizationSupport) {
    // Test quantization support checking
    EXPECT_TRUE(modelManager_->isQuantizationSupported(testModelPath_, QuantizationType::FP16));
    EXPECT_TRUE(modelManager_->isQuantizationSupported(testModelPath_, QuantizationType::INT8));
    
    auto supportedQuantizations = modelManager_->getSupportedQuantizations(testModelPath_);
    EXPECT_FALSE(supportedQuantizations.empty());
    EXPECT_TRUE(std::find(supportedQuantizations.begin(), supportedQuantizations.end(), 
                         QuantizationType::FP16) != supportedQuantizations.end());
}

TEST_F(ModelManagerTask2Test, ModelMetadata) {
    // Test metadata loading and updating
    EXPECT_TRUE(modelManager_->loadModel("en", "es", testModelPath_));
    
    auto metadata = modelManager_->getModelMetadata("en", "es");
    EXPECT_EQ(metadata.version, "1.0.0");
    EXPECT_EQ(metadata.sourceLanguage, "en");
    EXPECT_EQ(metadata.targetLanguage, "es");
    
    // Test metadata update
    metadata.version = "1.1.0";
    EXPECT_TRUE(modelManager_->updateModelMetadata("en", "es", metadata));
    
    auto updatedMetadata = modelManager_->getModelMetadata("en", "es");
    EXPECT_EQ(updatedMetadata.version, "1.1.0");
}

TEST_F(ModelManagerTask2Test, DetailedStats) {
    // Test detailed statistics
    EXPECT_TRUE(modelManager_->loadModelAdvanced("en", "es", testModelPath_, 
                                                false, -1, QuantizationType::FP16));
    
    auto stats = modelManager_->getDetailedStats();
    EXPECT_FALSE(stats.empty());
    
    auto modelStats = stats["en->es"];
    EXPECT_FALSE(modelStats.empty());
    EXPECT_TRUE(modelStats.find("memory_usage_mb") != modelStats.end());
    EXPECT_TRUE(modelStats.find("quantization") != modelStats.end());
    EXPECT_TRUE(modelStats.find("gpu_enabled") != modelStats.end());
}

TEST_F(ModelManagerTask2Test, MemoryManagement) {
    // Test memory management with multiple models
    std::string testModelPath2 = "test_models/es-en";
    std::filesystem::create_directories(testModelPath2);
    
    std::ofstream modelFile2(testModelPath2 + "/model.bin");
    modelFile2 << "dummy model data 2";
    modelFile2.close();
    
    std::ofstream vocabFile2(testModelPath2 + "/vocab.yml");
    vocabFile2 << "dummy vocab data 2";
    vocabFile2.close();
    
    // Load multiple models
    EXPECT_TRUE(modelManager_->loadModel("en", "es", testModelPath_));
    EXPECT_TRUE(modelManager_->loadModel("es", "en", testModelPath2));
    
    EXPECT_EQ(modelManager_->getLoadedModelCount(), 2);
    EXPECT_GT(modelManager_->getCurrentMemoryUsage(), 0);
    
    auto memoryStats = modelManager_->getMemoryStats();
    EXPECT_EQ(memoryStats.size(), 2);
    
    // Clean up
    std::filesystem::remove_all(testModelPath2);
}

TEST_F(ModelManagerTask2Test, ErrorHandling) {
    // Test error handling for invalid models
    EXPECT_FALSE(modelManager_->loadModel("en", "es", "non_existent_path"));
    EXPECT_FALSE(modelManager_->isModelLoaded("en", "es"));
    
    // Test unsupported language pair
    EXPECT_FALSE(modelManager_->validateLanguagePair("invalid", "invalid"));
    
    // Test quantization with invalid model
    EXPECT_FALSE(modelManager_->loadModelWithQuantization("en", "es", "non_existent_path", 
                                                         QuantizationType::INT8));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Initialize logging for tests
    Logger::info("Starting ModelManager Task 2 integration tests");
    
    return RUN_ALL_TESTS();
}