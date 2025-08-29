#include "stt/whisper_stt.hpp"
#include "stt/quantization_config.hpp"
#include "utils/gpu_manager.hpp"
#include <iostream>
#include <vector>
#include <memory>

using namespace stt;

int main() {
    std::cout << "=== Whisper STT Quantization Example ===" << std::endl;
    
    // Create WhisperSTT instance
    auto whisperSTT = std::make_unique<WhisperSTT>();
    
    // Check supported quantization levels
    std::cout << "\n1. Checking supported quantization levels:" << std::endl;
    auto supportedLevels = whisperSTT->getSupportedQuantizationLevels();
    
    QuantizationManager manager;
    for (auto level : supportedLevels) {
        std::cout << "  - " << manager.levelToString(level) << std::endl;
    }
    
    // Check GPU availability
    std::cout << "\n2. GPU Information:" << std::endl;
    auto& gpuManager = speechrnt::utils::GPUManager::getInstance();
    gpuManager.initialize();
    
    if (gpuManager.isCudaAvailable()) {
        std::cout << "  CUDA available: Yes" << std::endl;
        std::cout << "  GPU devices: " << gpuManager.getDeviceCount() << std::endl;
        
        for (int i = 0; i < gpuManager.getDeviceCount(); ++i) {
            auto deviceInfo = gpuManager.getDeviceInfo(i);
            std::cout << "    Device " << i << ": " << deviceInfo.name 
                      << " (" << deviceInfo.totalMemoryMB << "MB)" << std::endl;
        }
    } else {
        std::cout << "  CUDA available: No" << std::endl;
    }
    
    // Demonstrate automatic quantization level selection
    std::cout << "\n3. Automatic quantization level selection:" << std::endl;
    
    // Simulate different memory scenarios
    std::vector<std::pair<size_t, std::string>> memoryScenarios = {
        {4096, "High-end GPU (4GB+)"},
        {2048, "Mid-range GPU (2GB)"},
        {1024, "Entry-level GPU (1GB)"},
        {512, "Low memory GPU (512MB)"},
        {256, "Very low memory (256MB)"}
    };
    
    for (const auto& scenario : memoryScenarios) {
        size_t memoryMB = scenario.first;
        const std::string& description = scenario.second;
        
        auto optimalLevel = manager.selectOptimalLevel(memoryMB, 500); // 500MB model
        std::cout << "  " << description << " -> " << manager.levelToString(optimalLevel) << std::endl;
    }
    
    // Demonstrate quantization configuration
    std::cout << "\n4. Quantization configurations:" << std::endl;
    
    std::vector<QuantizationLevel> levels = {
        QuantizationLevel::FP32,
        QuantizationLevel::FP16,
        QuantizationLevel::INT8
    };
    
    for (auto level : levels) {
        auto config = manager.getConfig(level);
        std::cout << "  " << manager.levelToString(level) << ":" << std::endl;
        std::cout << "    Min GPU Memory: " << config.minGPUMemoryMB << "MB" << std::endl;
        std::cout << "    Expected Accuracy Loss: " << (config.expectedAccuracyLoss * 100.0f) << "%" << std::endl;
        std::cout << "    Model Suffix: " << (config.modelSuffix.empty() ? "(none)" : config.modelSuffix) << std::endl;
    }
    
    // Demonstrate model path generation
    std::cout << "\n5. Quantized model paths:" << std::endl;
    std::string basePath = "/models/whisper-base.bin";
    
    for (auto level : levels) {
        std::string quantizedPath = manager.getQuantizedModelPath(basePath, level);
        std::cout << "  " << manager.levelToString(level) << ": " << quantizedPath << std::endl;
    }
    
    // Demonstrate setting quantization levels
    std::cout << "\n6. Setting quantization levels:" << std::endl;
    
    for (auto level : levels) {
        whisperSTT->setQuantizationLevel(level);
        auto currentLevel = whisperSTT->getQuantizationLevel();
        std::cout << "  Set to " << manager.levelToString(level) 
                  << " -> Current: " << manager.levelToString(currentLevel) << std::endl;
    }
    
    // Demonstrate AUTO selection
    std::cout << "\n7. AUTO quantization selection:" << std::endl;
    whisperSTT->setQuantizationLevel(QuantizationLevel::AUTO);
    auto autoSelectedLevel = whisperSTT->getQuantizationLevel();
    std::cout << "  AUTO selected: " << manager.levelToString(autoSelectedLevel) << std::endl;
    
    // Demonstrate accuracy validation (simulation)
    std::cout << "\n8. Accuracy validation example:" << std::endl;
    
    std::vector<std::string> validationAudioPaths = {
        "test_audio1.wav",
        "test_audio2.wav",
        "test_audio3.wav"
    };
    
    std::vector<std::string> expectedTranscriptions = {
        "hello world",
        "this is a test",
        "speech recognition"
    };
    
    // Note: This will show error since STT is not initialized with a real model
    auto validationResult = whisperSTT->validateQuantizedModel(validationAudioPaths, expectedTranscriptions);
    std::cout << "  Validation result: " << validationResult.validationDetails << std::endl;
    
    // Demonstrate accuracy threshold configuration
    std::cout << "\n9. Accuracy threshold configuration:" << std::endl;
    std::cout << "  Default threshold: " << (manager.getAccuracyThreshold() * 100.0f) << "%" << std::endl;
    
    manager.setAccuracyThreshold(0.9f);
    std::cout << "  New threshold: " << (manager.getAccuracyThreshold() * 100.0f) << "%" << std::endl;
    
    std::cout << "\n=== Example completed ===" << std::endl;
    
    return 0;
}