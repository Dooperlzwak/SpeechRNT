#include <gtest/gtest.h>
#include "models/model_manager.hpp"
#include "utils/logging.hpp"
#include <filesystem>
#include <fstream>

using namespace speechrnt::models;

class EnhancedModelManagerTest : public ::testing::Test {
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
        
        modelManager_ = std::make_unique<ModelManager>(2048, 5);
    }
    
    void TearDown() override {
        modelManager_.reset();
        std::filesystem::remove_all("test_models");
    }
    
    std::string testModelPath_;
    std::unique_ptr<ModelManager> modelManager_;
};

TEST_F(EnhancedModelManagerTest, LoadModelWithGPU) {
    // Test GPU model loading (should fall back to CPU in test environment)
    bool result = modelManager_->loadModelWithGPU("en", "es", testModelPath_, true, -1);
    EXPECT_TRUE(result);
    
    // Verify model is loaded
    EXPECT_TRUE(modelManager_->isModelLoaded("en", "es"));
    
    // Get model info
    auto modelInfo = modelManager_->getModel("en", "es");
    ASSERT_NE(modelInfo, nullptr);
    EXPECT_EQ(modelInfo->languagePair, "en->es");
    EXPECT_TRUE(modelInfo->validated);
}

TEST_F(EnhancedModelManagerTest, LoadModelWithQuantization) {
    // Test quantization model loading
    bool result = modelManager_->loadModelWithQuantization("en", "es", testModelPath_, QuantizationType::INT8);
    EXPECT_TRUE(result);
    
    // Verify model is loaded with quantization
    auto modelInfo = modelManager_->getModel("en", "es");
    ASSERT_NE(modelInfo, nullptr);
    EXPECT_EQ(modelInfo->quantization, QuantizationType::INT8);
}

TEST_F(EnhancedModelManagerTest, LoadModelAdvanced) {
    // Test advanced model loading with both GPU and quantization
    bool result = modelManager_->loadModelAdvanced("en", "es", testModelPath_, true, -1, QuantizationType::FP16);
    EXPECT_TRUE(result);
    
    // Verify model configuration
    auto modelInfo = modelManager_->getModel("en", "es");
    ASSERT_NE(modelInfo, nullptr);
    EXPECT_EQ(modelInfo->quantization, QuantizationType::FP16);
    // GPU should be disabled in test environment
    EXPECT_FALSE(modelInfo->useGPU);
}

TEST_F(EnhancedModelManagerTest, ValidateModelIntegrity) {
    // Test model integrity validation
    bool result = modelManager_->validateModelIntegrity(testModelPath_);
    EXPECT_TRUE(result);
    
    // Test with non-existent path
    result = modelManager_->validateModelIntegrity("non_existent_path");
    EXPECT_FALSE(result);
}

TEST_F(EnhancedModelManagerTest, ModelMetadata) {
    // Load model first
    modelManager_->loadModel("en", "es", testModelPath_);
    
    // Get metadata
    ModelMetadata metadata = modelManager_->getModelMetadata("en", "es");
    EXPECT_EQ(metadata.version, "1.0.0");
    EXPECT_EQ(metadata.sourceLanguage, "en");
    EXPECT_EQ(metadata.targetLanguage, "es");
    
    // Update metadata
    metadata.version = "1.1.0";
    bool result = modelManager_->updateModelMetadata("en", "es", metadata);
    EXPECT_TRUE(result);
    
    // Verify update
    ModelMetadata updatedMetadata = modelManager_->getModelMetadata("en", "es");
    EXPECT_EQ(updatedMetadata.version, "1.1.0");
}

TEST_F(EnhancedModelManagerTest, HotSwapModel) {
    // Create second test model
    std::string newModelPath = "test_models/en-es-v2";
    std::filesystem::create_directories(newModelPath);
    
    std::ofstream modelFile(newModelPath + "/model.bin");
    modelFile << "new dummy model data";
    modelFile.close();
    
    std::ofstream vocabFile(newModelPath + "/vocab.yml");
    vocabFile << "new dummy vocab data";
    vocabFile.close();
    
    // Load initial model
    modelManager_->loadModel("en", "es", testModelPath_);
    
    // Perform hot swap
    bool result = modelManager_->hotSwapModel("en", "es", newModelPath);
    EXPECT_TRUE(result);
    
    // Verify model path changed
    auto modelInfo = modelManager_->getModel("en", "es");
    ASSERT_NE(modelInfo, nullptr);
    EXPECT_EQ(modelInfo->modelPath, newModelPath);
}

TEST_F(EnhancedModelManagerTest, QuantizationSupport) {
    // Test quantization support checking
    bool supported = modelManager_->isQuantizationSupported(testModelPath_, QuantizationType::INT8);
    EXPECT_TRUE(supported);
    
    // Get supported quantizations
    auto supportedTypes = modelManager_->getSupportedQuantizations(testModelPath_);
    EXPECT_FALSE(supportedTypes.empty());
    EXPECT_TRUE(std::find(supportedTypes.begin(), supportedTypes.end(), QuantizationType::INT8) != supportedTypes.end());
}

TEST_F(EnhancedModelManagerTest, AutoValidation) {
    // Test auto-validation setting
    modelManager_->setAutoValidation(false);
    
    // Load model without validation
    bool result = modelManager_->loadModel("en", "es", testModelPath_);
    EXPECT_TRUE(result);
    
    // Re-enable validation
    modelManager_->setAutoValidation(true);
}

TEST_F(EnhancedModelManagerTest, DetailedStatistics) {
    // Load model
    modelManager_->loadModelWithQuantization("en", "es", testModelPath_, QuantizationType::FP16);
    
    // Get detailed statistics
    auto stats = modelManager_->getDetailedStats();
    EXPECT_FALSE(stats.empty());
    
    auto modelStats = stats["en->es"];
    EXPECT_FALSE(modelStats.empty());
    EXPECT_EQ(modelStats["quantization"], "fp16");
    EXPECT_EQ(modelStats["validated"], "true");
}

TEST_F(EnhancedModelManagerTest, ModelVersion) {
    // Load model
    modelManager_->loadModel("en", "es", testModelPath_);
    
    // Get version
    std::string version = modelManager_->getModelVersion("en", "es");
    EXPECT_EQ(version, "1.0.0");
    
    // Test version checking (simplified)
    bool newerAvailable = modelManager_->isNewerVersionAvailable("en", "es", "dummy_repo");
    EXPECT_FALSE(newerAvailable); // Should be false since repo doesn't exist
}

int main(int argc, char** argv) {
    // Initialize logging for tests
    speechrnt::utils::Logger::setLevel(speechrnt::utils::LogLevel::INFO);
    
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}