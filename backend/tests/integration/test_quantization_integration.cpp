#include <gtest/gtest.h>
#include "stt/whisper_stt.hpp"
#include "stt/quantization_config.hpp"
#include <vector>
#include <string>
#include <memory>
#include <cmath>
#include <chrono>

using namespace stt;

class QuantizationIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        whisperSTT = std::make_unique<WhisperSTT>();
    }
    
    void TearDown() override {
        whisperSTT.reset();
    }
    
    std::unique_ptr<WhisperSTT> whisperSTT;
    
    // Helper method to generate test audio data
    std::vector<float> generateTestAudio(size_t samples = 16000) {
        std::vector<float> audio(samples);
        for (size_t i = 0; i < samples; ++i) {
            // Generate a simple sine wave
            audio[i] = 0.5f * sin(2.0f * M_PI * 440.0f * i / 16000.0f);
        }
        return audio;
    }
};

TEST_F(QuantizationIntegrationTest, QuantizationLevelConfiguration) {
    // Test setting different quantization levels
    whisperSTT->setQuantizationLevel(QuantizationLevel::FP32);
    EXPECT_EQ(whisperSTT->getQuantizationLevel(), QuantizationLevel::FP32);
    
    whisperSTT->setQuantizationLevel(QuantizationLevel::FP16);
    EXPECT_EQ(whisperSTT->getQuantizationLevel(), QuantizationLevel::FP16);
    
    whisperSTT->setQuantizationLevel(QuantizationLevel::INT8);
    EXPECT_EQ(whisperSTT->getQuantizationLevel(), QuantizationLevel::INT8);
}

TEST_F(QuantizationIntegrationTest, SupportedQuantizationLevels) {
    auto supportedLevels = whisperSTT->getSupportedQuantizationLevels();
    
    // Should at least support FP32
    EXPECT_FALSE(supportedLevels.empty());
    EXPECT_TRUE(std::find(supportedLevels.begin(), supportedLevels.end(), QuantizationLevel::FP32) != supportedLevels.end());
    
    // Log supported levels for debugging
    std::cout << "Supported quantization levels: ";
    for (auto level : supportedLevels) {
        QuantizationManager manager;
        std::cout << manager.levelToString(level) << " ";
    }
    std::cout << std::endl;
}

TEST_F(QuantizationIntegrationTest, QuantizationManagerFunctionality) {
    QuantizationManager manager;
    
    // Test optimal level selection with different memory scenarios
    auto level = manager.selectOptimalLevel(4096, 500);  // High memory
    EXPECT_TRUE(level == QuantizationLevel::FP32 || level == QuantizationLevel::FP16);
    
    level = manager.selectOptimalLevel(1024, 500);  // Medium memory
    EXPECT_TRUE(level == QuantizationLevel::FP16 || level == QuantizationLevel::INT8);
    
    level = manager.selectOptimalLevel(512, 500);   // Low memory
    EXPECT_TRUE(level == QuantizationLevel::INT8 || level == QuantizationLevel::FP32);
}

TEST_F(QuantizationIntegrationTest, ModelPathGeneration) {
    QuantizationManager manager;
    std::string basePath = "/models/whisper-base.bin";
    
    // Test path generation for different quantization levels
    EXPECT_EQ(manager.getQuantizedModelPath(basePath, QuantizationLevel::FP32), basePath);
    EXPECT_EQ(manager.getQuantizedModelPath(basePath, QuantizationLevel::FP16), "/models/whisper-base_fp16.bin");
    EXPECT_EQ(manager.getQuantizedModelPath(basePath, QuantizationLevel::INT8), "/models/whisper-base_int8.bin");
}

TEST_F(QuantizationIntegrationTest, AccuracyValidationSimulation) {
    // Test accuracy validation with mock data
    std::vector<std::string> audioPaths = {"test1.wav", "test2.wav"};
    std::vector<std::string> expectedTexts = {"hello world", "test transcription"};
    
    // This should work even without initialization (returns error result)
    auto result = whisperSTT->validateQuantizedModel(audioPaths, expectedTexts);
    EXPECT_EQ(result.totalSamples, 0);  // Should be 0 since not initialized
    EXPECT_FALSE(result.validationDetails.empty());
}

TEST_F(QuantizationIntegrationTest, QuantizationConfigValidation) {
    QuantizationManager manager;
    
    // Test configuration retrieval for all levels
    auto fp32Config = manager.getConfig(QuantizationLevel::FP32);
    EXPECT_EQ(fp32Config.level, QuantizationLevel::FP32);
    EXPECT_EQ(fp32Config.expectedAccuracyLoss, 0.0f);
    
    auto fp16Config = manager.getConfig(QuantizationLevel::FP16);
    EXPECT_EQ(fp16Config.level, QuantizationLevel::FP16);
    EXPECT_GT(fp16Config.expectedAccuracyLoss, 0.0f);
    EXPECT_LT(fp16Config.minGPUMemoryMB, fp32Config.minGPUMemoryMB);
    
    auto int8Config = manager.getConfig(QuantizationLevel::INT8);
    EXPECT_EQ(int8Config.level, QuantizationLevel::INT8);
    EXPECT_GT(int8Config.expectedAccuracyLoss, fp16Config.expectedAccuracyLoss);
    EXPECT_LT(int8Config.minGPUMemoryMB, fp16Config.minGPUMemoryMB);
}

TEST_F(QuantizationIntegrationTest, AutoQuantizationSelection) {
    // Test AUTO quantization level selection
    whisperSTT->setQuantizationLevel(QuantizationLevel::AUTO);
    
    // The actual level should be resolved to a concrete level
    auto actualLevel = whisperSTT->getQuantizationLevel();
    EXPECT_TRUE(actualLevel == QuantizationLevel::FP32 || 
                actualLevel == QuantizationLevel::FP16 || 
                actualLevel == QuantizationLevel::INT8);
    EXPECT_NE(actualLevel, QuantizationLevel::AUTO);
}

// Performance test to ensure quantization doesn't significantly impact initialization time
TEST_F(QuantizationIntegrationTest, QuantizationPerformanceImpact) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // Test multiple quantization level changes
    for (int i = 0; i < 10; ++i) {
        whisperSTT->setQuantizationLevel(QuantizationLevel::FP32);
        whisperSTT->setQuantizationLevel(QuantizationLevel::FP16);
        whisperSTT->setQuantizationLevel(QuantizationLevel::INT8);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Should complete within reasonable time (less than 1 second)
    EXPECT_LT(duration.count(), 1000);
    
    std::cout << "Quantization level changes took: " << duration.count() << "ms" << std::endl;
}