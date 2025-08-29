#include <gtest/gtest.h>
#include "stt/quantization_config.hpp"
#include "stt/whisper_stt.hpp"
#include <vector>
#include <string>

using namespace stt;

class QuantizationTest : public ::testing::Test {
protected:
    void SetUp() override {
        quantizationManager = std::make_unique<QuantizationManager>();
    }
    
    void TearDown() override {
        quantizationManager.reset();
    }
    
    std::unique_ptr<QuantizationManager> quantizationManager;
};

TEST_F(QuantizationTest, QuantizationLevelToString) {
    EXPECT_EQ(quantizationManager->levelToString(QuantizationLevel::FP32), "FP32");
    EXPECT_EQ(quantizationManager->levelToString(QuantizationLevel::FP16), "FP16");
    EXPECT_EQ(quantizationManager->levelToString(QuantizationLevel::INT8), "INT8");
    EXPECT_EQ(quantizationManager->levelToString(QuantizationLevel::AUTO), "AUTO");
}

TEST_F(QuantizationTest, StringToQuantizationLevel) {
    EXPECT_EQ(quantizationManager->stringToLevel("FP32"), QuantizationLevel::FP32);
    EXPECT_EQ(quantizationManager->stringToLevel("FP16"), QuantizationLevel::FP16);
    EXPECT_EQ(quantizationManager->stringToLevel("INT8"), QuantizationLevel::INT8);
    EXPECT_EQ(quantizationManager->stringToLevel("AUTO"), QuantizationLevel::AUTO);
    EXPECT_EQ(quantizationManager->stringToLevel("INVALID"), QuantizationLevel::FP32); // Default fallback
}

TEST_F(QuantizationTest, GetQuantizationConfig) {
    auto fp32Config = quantizationManager->getConfig(QuantizationLevel::FP32);
    EXPECT_EQ(fp32Config.level, QuantizationLevel::FP32);
    EXPECT_EQ(fp32Config.expectedAccuracyLoss, 0.0f);
    EXPECT_EQ(fp32Config.modelSuffix, "");
    
    auto fp16Config = quantizationManager->getConfig(QuantizationLevel::FP16);
    EXPECT_EQ(fp16Config.level, QuantizationLevel::FP16);
    EXPECT_GT(fp16Config.expectedAccuracyLoss, 0.0f);
    EXPECT_EQ(fp16Config.modelSuffix, "_fp16");
    
    auto int8Config = quantizationManager->getConfig(QuantizationLevel::INT8);
    EXPECT_EQ(int8Config.level, QuantizationLevel::INT8);
    EXPECT_GT(int8Config.expectedAccuracyLoss, fp16Config.expectedAccuracyLoss);
    EXPECT_EQ(int8Config.modelSuffix, "_int8");
}

TEST_F(QuantizationTest, SelectOptimalLevel) {
    // Test with high memory - should select FP32
    auto level = quantizationManager->selectOptimalLevel(4096, 500);
    EXPECT_EQ(level, QuantizationLevel::FP32);
    
    // Test with medium memory - should select FP16
    level = quantizationManager->selectOptimalLevel(1536, 500);
    EXPECT_EQ(level, QuantizationLevel::FP16);
    
    // Test with low memory - should select INT8
    level = quantizationManager->selectOptimalLevel(768, 500);
    EXPECT_EQ(level, QuantizationLevel::INT8);
    
    // Test with very low memory - should fallback to FP32 (CPU)
    level = quantizationManager->selectOptimalLevel(256, 500);
    EXPECT_EQ(level, QuantizationLevel::FP32);
}

TEST_F(QuantizationTest, GetQuantizedModelPath) {
    std::string basePath = "/models/whisper-base.bin";
    
    EXPECT_EQ(quantizationManager->getQuantizedModelPath(basePath, QuantizationLevel::FP32), basePath);
    EXPECT_EQ(quantizationManager->getQuantizedModelPath(basePath, QuantizationLevel::FP16), "/models/whisper-base_fp16.bin");
    EXPECT_EQ(quantizationManager->getQuantizedModelPath(basePath, QuantizationLevel::INT8), "/models/whisper-base_int8.bin");
}

TEST_F(QuantizationTest, AccuracyThreshold) {
    // Test default threshold
    EXPECT_EQ(quantizationManager->getAccuracyThreshold(), 0.85f);
    
    // Test setting threshold
    quantizationManager->setAccuracyThreshold(0.9f);
    EXPECT_EQ(quantizationManager->getAccuracyThreshold(), 0.9f);
    
    // Test clamping
    quantizationManager->setAccuracyThreshold(1.5f);
    EXPECT_EQ(quantizationManager->getAccuracyThreshold(), 1.0f);
    
    quantizationManager->setAccuracyThreshold(-0.1f);
    EXPECT_EQ(quantizationManager->getAccuracyThreshold(), 0.0f);
}

TEST_F(QuantizationTest, PreferenceOrder) {
    // Test with high memory
    auto order = quantizationManager->getPreferenceOrder(4096);
    EXPECT_FALSE(order.empty());
    EXPECT_EQ(order[0], QuantizationLevel::FP32); // Should prefer highest precision
    
    // Test with low memory
    order = quantizationManager->getPreferenceOrder(256);
    EXPECT_FALSE(order.empty());
    // Should still have at least FP32 as fallback
    EXPECT_TRUE(std::find(order.begin(), order.end(), QuantizationLevel::FP32) != order.end());
}

class WhisperSTTQuantizationTest : public ::testing::Test {
protected:
    void SetUp() override {
        whisperSTT = std::make_unique<WhisperSTT>();
    }
    
    void TearDown() override {
        whisperSTT.reset();
    }
    
    std::unique_ptr<WhisperSTT> whisperSTT;
};

TEST_F(WhisperSTTQuantizationTest, SetQuantizationLevel) {
    // Test setting quantization level
    whisperSTT->setQuantizationLevel(QuantizationLevel::FP16);
    EXPECT_EQ(whisperSTT->getQuantizationLevel(), QuantizationLevel::FP16);
    
    whisperSTT->setQuantizationLevel(QuantizationLevel::INT8);
    EXPECT_EQ(whisperSTT->getQuantizationLevel(), QuantizationLevel::INT8);
    
    whisperSTT->setQuantizationLevel(QuantizationLevel::FP32);
    EXPECT_EQ(whisperSTT->getQuantizationLevel(), QuantizationLevel::FP32);
}

TEST_F(WhisperSTTQuantizationTest, GetSupportedQuantizationLevels) {
    auto supportedLevels = whisperSTT->getSupportedQuantizationLevels();
    
    // Should at least support FP32
    EXPECT_FALSE(supportedLevels.empty());
    EXPECT_TRUE(std::find(supportedLevels.begin(), supportedLevels.end(), QuantizationLevel::FP32) != supportedLevels.end());
}

TEST_F(WhisperSTTQuantizationTest, ValidateQuantizedModelWithoutInit) {
    // Test validation without initialization - should return error
    std::vector<std::string> audioPaths = {"test1.wav", "test2.wav"};
    std::vector<std::string> expectedTexts = {"hello world", "test transcription"};
    
    auto result = whisperSTT->validateQuantizedModel(audioPaths, expectedTexts);
    EXPECT_FALSE(result.passesThreshold);
    EXPECT_FALSE(result.validationDetails.empty());
    EXPECT_TRUE(result.validationDetails.find("not initialized") != std::string::npos);
}