#include "stt/quantization_config.hpp"
#include "utils/gpu_manager.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <numeric>

namespace stt {

QuantizationManager::QuantizationManager() 
    : accuracyThreshold_(0.85f) {  // Default 85% accuracy threshold
    initializeConfigs();
}

void QuantizationManager::initializeConfigs() {
    // FP32 - Full precision (baseline)
    configs_[QuantizationLevel::FP32] = QuantizationConfig(
        QuantizationLevel::FP32,
        true,   // GPU acceleration enabled
        2048,   // Minimum 2GB GPU memory
        0.0f,   // No accuracy loss (baseline)
        ""      // No suffix for full precision
    );
    
    // FP16 - Half precision
    configs_[QuantizationLevel::FP16] = QuantizationConfig(
        QuantizationLevel::FP16,
        true,   // GPU acceleration enabled
        1024,   // Minimum 1GB GPU memory
        0.02f,  // ~2% accuracy loss expected
        "_fp16"
    );
    
    // INT8 - 8-bit quantization
    configs_[QuantizationLevel::INT8] = QuantizationConfig(
        QuantizationLevel::INT8,
        true,   // GPU acceleration enabled
        512,    // Minimum 512MB GPU memory
        0.05f,  // ~5% accuracy loss expected
        "_int8"
    );
    
    // AUTO will be resolved at runtime
    configs_[QuantizationLevel::AUTO] = QuantizationConfig(
        QuantizationLevel::AUTO,
        true,
        0,
        0.0f,
        "_auto"
    );
}

QuantizationConfig QuantizationManager::getConfig(QuantizationLevel level) const {
    auto it = configs_.find(level);
    if (it != configs_.end()) {
        return it->second;
    }
    
    // Return FP32 as default
    return configs_.at(QuantizationLevel::FP32);
}

QuantizationLevel QuantizationManager::selectOptimalLevel(size_t availableGPUMemoryMB, size_t modelSizeMB) const {
    auto& gpuManager = speechrnt::utils::GPUManager::getInstance();
    
    // If no GPU available, use FP32 on CPU
    if (!gpuManager.isCudaAvailable()) {
        speechrnt::utils::Logger::info("No GPU available, using FP32 on CPU");
        return QuantizationLevel::FP32;
    }
    
    // Calculate memory requirements for each quantization level
    // These are rough estimates based on model size and overhead
    size_t fp32MemoryMB = modelSizeMB * 3;  // Model + activations + overhead
    size_t fp16MemoryMB = modelSizeMB * 2;  // ~50% reduction
    size_t int8MemoryMB = modelSizeMB * 1;  // ~75% reduction
    
    // Select the best quantization level that fits in available memory
    if (availableGPUMemoryMB >= fp32MemoryMB && availableGPUMemoryMB >= configs_.at(QuantizationLevel::FP32).minGPUMemoryMB) {
        speechrnt::utils::Logger::info("Selected FP32 quantization (available: " + std::to_string(availableGPUMemoryMB) + "MB, required: " + std::to_string(fp32MemoryMB) + "MB)");
        return QuantizationLevel::FP32;
    }
    
    if (availableGPUMemoryMB >= fp16MemoryMB && availableGPUMemoryMB >= configs_.at(QuantizationLevel::FP16).minGPUMemoryMB) {
        speechrnt::utils::Logger::info("Selected FP16 quantization (available: " + std::to_string(availableGPUMemoryMB) + "MB, required: " + std::to_string(fp16MemoryMB) + "MB)");
        return QuantizationLevel::FP16;
    }
    
    if (availableGPUMemoryMB >= int8MemoryMB && availableGPUMemoryMB >= configs_.at(QuantizationLevel::INT8).minGPUMemoryMB) {
        speechrnt::utils::Logger::info("Selected INT8 quantization (available: " + std::to_string(availableGPUMemoryMB) + "MB, required: " + std::to_string(int8MemoryMB) + "MB)");
        return QuantizationLevel::INT8;
    }
    
    // If nothing fits, fallback to FP32 on CPU
    speechrnt::utils::Logger::warn("Insufficient GPU memory for any quantization level, falling back to FP32 on CPU");
    return QuantizationLevel::FP32;
}

std::vector<QuantizationLevel> QuantizationManager::getPreferenceOrder(size_t availableGPUMemoryMB) const {
    std::vector<QuantizationLevel> order;
    
    // Always prefer higher precision when memory allows
    if (availableGPUMemoryMB >= configs_.at(QuantizationLevel::FP32).minGPUMemoryMB) {
        order.push_back(QuantizationLevel::FP32);
    }
    
    if (availableGPUMemoryMB >= configs_.at(QuantizationLevel::FP16).minGPUMemoryMB) {
        order.push_back(QuantizationLevel::FP16);
    }
    
    if (availableGPUMemoryMB >= configs_.at(QuantizationLevel::INT8).minGPUMemoryMB) {
        order.push_back(QuantizationLevel::INT8);
    }
    
    // If no GPU levels are suitable, add FP32 as CPU fallback
    if (order.empty()) {
        order.push_back(QuantizationLevel::FP32);
    }
    
    return order;
}

std::string QuantizationManager::levelToString(QuantizationLevel level) const {
    switch (level) {
        case QuantizationLevel::FP32: return "FP32";
        case QuantizationLevel::FP16: return "FP16";
        case QuantizationLevel::INT8: return "INT8";
        case QuantizationLevel::AUTO: return "AUTO";
        default: return "UNKNOWN";
    }
}

QuantizationLevel QuantizationManager::stringToLevel(const std::string& levelStr) const {
    if (levelStr == "FP32") return QuantizationLevel::FP32;
    if (levelStr == "FP16") return QuantizationLevel::FP16;
    if (levelStr == "INT8") return QuantizationLevel::INT8;
    if (levelStr == "AUTO") return QuantizationLevel::AUTO;
    
    speechrnt::utils::Logger::warn("Unknown quantization level: " + levelStr + ", defaulting to FP32");
    return QuantizationLevel::FP32;
}

std::string QuantizationManager::getQuantizedModelPath(const std::string& basePath, QuantizationLevel level) const {
    if (level == QuantizationLevel::FP32) {
        return basePath;  // Use original path for FP32
    }
    
    auto config = getConfig(level);
    std::filesystem::path path(basePath);
    
    // Insert quantization suffix before file extension
    std::string stem = path.stem().string();
    std::string extension = path.extension().string();
    std::string directory = path.parent_path().string();
    
    std::string quantizedPath = directory + "/" + stem + config.modelSuffix + extension;
    
    return quantizedPath;
}

bool QuantizationManager::isLevelSupported(QuantizationLevel level) const {
    auto& gpuManager = speechrnt::utils::GPUManager::getInstance();
    
    switch (level) {
        case QuantizationLevel::FP32:
            return true;  // Always supported (can fallback to CPU)
            
        case QuantizationLevel::FP16:
            // FP16 requires GPU with compute capability 5.3+
            if (gpuManager.isCudaAvailable()) {
                auto devices = gpuManager.getAllDeviceInfo();
                for (const auto& device : devices) {
                    if (device.isAvailable && 
                        (device.computeCapabilityMajor > 5 || 
                         (device.computeCapabilityMajor == 5 && device.computeCapabilityMinor >= 3))) {
                        return true;
                    }
                }
            }
            return false;
            
        case QuantizationLevel::INT8:
            // INT8 requires GPU with compute capability 6.1+
            if (gpuManager.isCudaAvailable()) {
                auto devices = gpuManager.getAllDeviceInfo();
                for (const auto& device : devices) {
                    if (device.isAvailable && 
                        (device.computeCapabilityMajor > 6 || 
                         (device.computeCapabilityMajor == 6 && device.computeCapabilityMinor >= 1))) {
                        return true;
                    }
                }
            }
            return false;
            
        case QuantizationLevel::AUTO:
            return true;  // AUTO is always supported (will resolve to supported level)
            
        default:
            return false;
    }
}

AccuracyValidationResult QuantizationManager::validateModelAccuracy(
    const std::string& modelPath,
    QuantizationLevel level,
    const std::vector<std::string>& validationAudioPaths,
    const std::vector<std::string>& expectedTranscriptions) const {
    
    AccuracyValidationResult result;
    
    if (validationAudioPaths.size() != expectedTranscriptions.size()) {
        result.validationDetails = "Mismatch between audio paths and expected transcriptions count";
        return result;
    }
    
    if (validationAudioPaths.empty()) {
        result.validationDetails = "No validation data provided";
        return result;
    }
    
    // Check if model file exists
    if (!std::filesystem::exists(modelPath)) {
        result.validationDetails = "Model file not found: " + modelPath;
        return result;
    }
    
    result.totalSamples = validationAudioPaths.size();
    
    // TODO: In a real implementation, this would:
    // 1. Load the quantized model
    // 2. Run transcription on validation audio files
    // 3. Compare results with expected transcriptions
    // 4. Calculate WER, CER, and confidence scores
    
    // For now, simulate validation based on quantization level
    float simulatedAccuracy = 1.0f - getConfig(level).expectedAccuracyLoss;
    
    // Simulate some variation in accuracy
    float variation = 0.02f;  // ±2% variation
    simulatedAccuracy += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * variation;
    simulatedAccuracy = std::max(0.0f, std::min(1.0f, simulatedAccuracy));
    
    result.wordErrorRate = 1.0f - simulatedAccuracy;
    result.characterErrorRate = result.wordErrorRate * 0.8f;  // CER typically lower than WER
    result.confidenceScore = simulatedAccuracy;
    result.passesThreshold = simulatedAccuracy >= accuracyThreshold_;
    
    std::ostringstream details;
    details << "Quantization level: " << levelToString(level) << ", ";
    details << "Simulated accuracy: " << (simulatedAccuracy * 100.0f) << "%, ";
    details << "Threshold: " << (accuracyThreshold_ * 100.0f) << "%, ";
    details << "Status: " << (result.passesThreshold ? "PASS" : "FAIL");
    
    result.validationDetails = details.str();
    
    speechrnt::utils::Logger::info("Model accuracy validation: " + result.validationDetails);
    
    return result;
}

void QuantizationManager::setAccuracyThreshold(float threshold) {
    accuracyThreshold_ = std::max(0.0f, std::min(1.0f, threshold));
    speechrnt::utils::Logger::info("Accuracy threshold set to: " + std::to_string(accuracyThreshold_ * 100.0f) + "%");
}

float QuantizationManager::getAccuracyThreshold() const {
    return accuracyThreshold_;
}

float QuantizationManager::calculateWordErrorRate(const std::string& expected, const std::string& actual) const {
    // Simple word-based WER calculation
    std::istringstream expectedStream(expected);
    std::istringstream actualStream(actual);
    
    std::vector<std::string> expectedWords;
    std::vector<std::string> actualWords;
    
    std::string word;
    while (expectedStream >> word) {
        expectedWords.push_back(word);
    }
    
    while (actualStream >> word) {
        actualWords.push_back(word);
    }
    
    if (expectedWords.empty()) {
        return actualWords.empty() ? 0.0f : 1.0f;
    }
    
    size_t distance = calculateLevenshteinDistance(
        std::accumulate(expectedWords.begin(), expectedWords.end(), std::string()),
        std::accumulate(actualWords.begin(), actualWords.end(), std::string())
    );
    
    return static_cast<float>(distance) / static_cast<float>(expectedWords.size());
}

float QuantizationManager::calculateCharacterErrorRate(const std::string& expected, const std::string& actual) const {
    if (expected.empty()) {
        return actual.empty() ? 0.0f : 1.0f;
    }
    
    size_t distance = calculateLevenshteinDistance(expected, actual);
    return static_cast<float>(distance) / static_cast<float>(expected.length());
}

size_t QuantizationManager::calculateLevenshteinDistance(const std::string& s1, const std::string& s2) const {
    const size_t len1 = s1.size();
    const size_t len2 = s2.size();
    
    std::vector<std::vector<size_t>> dp(len1 + 1, std::vector<size_t>(len2 + 1));
    
    for (size_t i = 0; i <= len1; ++i) {
        dp[i][0] = i;
    }
    
    for (size_t j = 0; j <= len2; ++j) {
        dp[0][j] = j;
    }
    
    for (size_t i = 1; i <= len1; ++i) {
        for (size_t j = 1; j <= len2; ++j) {
            if (s1[i - 1] == s2[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1];
            } else {
                dp[i][j] = 1 + std::min({dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]});
            }
        }
    }
    
    return dp[len1][len2];
}

} // namespace stt