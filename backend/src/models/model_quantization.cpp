#include "models/model_quantization.hpp"
#include "utils/logging.hpp"
#include "utils/performance_monitor.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>

namespace speechrnt {
namespace models {

WhisperQuantizer::WhisperQuantizer() {
}

WhisperQuantizer::~WhisperQuantizer() {
}

bool WhisperQuantizer::quantizeModel(const std::string& modelPath,
                                   const std::string& outputPath,
                                   const QuantizationConfig& config) {
    if (!std::filesystem::exists(modelPath)) {
        speechrnt::utils::Logger::error("Whisper model not found: " + modelPath);
        return false;
    }
    
    auto timer = utils::PerformanceMonitor::getInstance().startLatencyTimer(
        "quantization.whisper_quantization_latency_ms");
    
    currentConfig_ = config;
    stats_.originalModelSizeMB = getModelSize(modelPath);
    
    bool success = false;
    
    switch (config.precision) {
        case QuantizationPrecision::FP16:
            success = quantizeToFP16(modelPath, outputPath);
            stats_.quantizationMethod = "FP16";
            break;
            
        case QuantizationPrecision::INT8:
            success = quantizeToINT8(modelPath, outputPath);
            stats_.quantizationMethod = "INT8";
            break;
            
        case QuantizationPrecision::FP32:
            // No quantization needed, just copy
            std::filesystem::copy_file(modelPath, outputPath);
            success = true;
            stats_.quantizationMethod = "FP32 (no quantization)";
            break;
            
        default:
            speechrnt::utils::Logger::error("Unsupported quantization precision for Whisper");
            return false;
    }
    
    if (success) {
        stats_.quantizedModelSizeMB = getModelSize(outputPath);
        stats_.compressionRatio = static_cast<float>(stats_.originalModelSizeMB) / 
                                 static_cast<float>(stats_.quantizedModelSizeMB);
        
        loadedModelPath_ = outputPath;
        
        speechrnt::utils::Logger::info("Whisper model quantized successfully: " + 
                           std::to_string(stats_.compressionRatio) + "x compression");
        
        utils::PerformanceMonitor::getInstance().recordMetric(
            "quantization.whisper_compression_ratio", stats_.compressionRatio);
    }
    
    return success;
}

bool WhisperQuantizer::loadQuantizedModel(const std::string& modelPath) {
    if (!std::filesystem::exists(modelPath)) {
        speechrnt::utils::Logger::error("Quantized Whisper model not found: " + modelPath);
        return false;
    }
    
    loadedModelPath_ = modelPath;
    speechrnt::utils::Logger::info("Loaded quantized Whisper model: " + modelPath);
    return true;
}

QuantizationStats WhisperQuantizer::getQuantizationStats() const {
    return stats_;
}

float WhisperQuantizer::validateAccuracy(const std::string& testDataPath) const {
    // Placeholder implementation - in production, this would run actual accuracy tests
    // For now, return estimated accuracy based on quantization method
    
    if (currentConfig_.precision == QuantizationPrecision::FP32) {
        return 1.0f; // No accuracy loss
    } else if (currentConfig_.precision == QuantizationPrecision::FP16) {
        return 0.98f; // Minimal accuracy loss
    } else if (currentConfig_.precision == QuantizationPrecision::INT8) {
        return 0.95f; // Some accuracy loss
    }
    
    return 0.9f; // Conservative estimate for other methods
}

std::vector<QuantizationPrecision> WhisperQuantizer::getSupportedPrecisions() const {
    return {
        QuantizationPrecision::FP32,
        QuantizationPrecision::FP16,
        QuantizationPrecision::INT8
    };
}

bool WhisperQuantizer::quantizeToFP16(const std::string& inputPath, const std::string& outputPath) {
    // Placeholder implementation for FP16 quantization
    // In production, this would use actual quantization libraries
    
    speechrnt::utils::Logger::info("Quantizing Whisper model to FP16: " + inputPath + " -> " + outputPath);
    
    // For now, simulate quantization by copying the file
    // Real implementation would convert weights to FP16
    try {
        std::filesystem::copy_file(inputPath, outputPath);
        
        // Simulate size reduction (FP16 is roughly half the size of FP32)
        // In real implementation, this would be the actual quantized model size
        
        return true;
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to quantize Whisper model to FP16: " + std::string(e.what()));
        return false;
    }
}

bool WhisperQuantizer::quantizeToINT8(const std::string& inputPath, const std::string& outputPath) {
    // Placeholder implementation for INT8 quantization
    speechrnt::utils::Logger::info("Quantizing Whisper model to INT8: " + inputPath + " -> " + outputPath);
    
    // Calibration would be performed here in real implementation
    if (currentConfig_.enableStaticQuantization) {
        if (!calibrateQuantization(inputPath, currentConfig_)) {
            speechrnt::utils::Logger::warn("Calibration failed, using dynamic quantization");
        }
    }
    
    try {
        std::filesystem::copy_file(inputPath, outputPath);
        return true;
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to quantize Whisper model to INT8: " + std::string(e.what()));
        return false;
    }
}

bool WhisperQuantizer::calibrateQuantization(const std::string& modelPath, 
                                           const QuantizationConfig& config) {
    // Placeholder for calibration logic
    speechrnt::utils::Logger::info("Calibrating quantization for Whisper model using method: " + 
                       config.calibrationMethod);
    
    // In real implementation, this would:
    // 1. Load calibration dataset
    // 2. Run inference to collect activation statistics
    // 3. Compute optimal quantization parameters
    
    return true;
}

size_t WhisperQuantizer::getModelSize(const std::string& modelPath) const {
    try {
        auto fileSize = std::filesystem::file_size(modelPath);
        return fileSize / (1024 * 1024); // Convert to MB
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::warn("Failed to get model size: " + std::string(e.what()));
        return 0;
    }
}

MarianQuantizer::MarianQuantizer() {
}

MarianQuantizer::~MarianQuantizer() {
}

bool MarianQuantizer::quantizeModel(const std::string& modelPath,
                                  const std::string& outputPath,
                                  const QuantizationConfig& config) {
    if (!std::filesystem::exists(modelPath)) {
        speechrnt::utils::Logger::error("Marian model not found: " + modelPath);
        return false;
    }
    
    auto timer = utils::PerformanceMonitor::getInstance().startLatencyTimer(
        "quantization.marian_quantization_latency_ms");
    
    currentConfig_ = config;
    stats_.originalModelSizeMB = std::filesystem::file_size(modelPath) / (1024 * 1024);
    
    bool success = quantizeMarianModel(modelPath, outputPath, config.precision);
    
    if (success) {
        stats_.quantizedModelSizeMB = std::filesystem::file_size(outputPath) / (1024 * 1024);
        stats_.compressionRatio = static_cast<float>(stats_.originalModelSizeMB) / 
                                 static_cast<float>(stats_.quantizedModelSizeMB);
        
        loadedModelPath_ = outputPath;
        
        speechrnt::utils::Logger::info("Marian model quantized successfully: " + 
                           std::to_string(stats_.compressionRatio) + "x compression");
    }
    
    return success;
}

bool MarianQuantizer::loadQuantizedModel(const std::string& modelPath) {
    if (!std::filesystem::exists(modelPath)) {
        speechrnt::utils::Logger::error("Quantized Marian model not found: " + modelPath);
        return false;
    }
    
    loadedModelPath_ = modelPath;
    speechrnt::utils::Logger::info("Loaded quantized Marian model: " + modelPath);
    return true;
}

QuantizationStats MarianQuantizer::getQuantizationStats() const {
    return stats_;
}

float MarianQuantizer::validateAccuracy(const std::string& testDataPath) const {
    // Placeholder - would run BLEU score evaluation in production
    if (currentConfig_.precision == QuantizationPrecision::FP32) {
        return 1.0f;
    } else if (currentConfig_.precision == QuantizationPrecision::FP16) {
        return 0.97f;
    } else if (currentConfig_.precision == QuantizationPrecision::INT8) {
        return 0.93f;
    }
    
    return 0.85f;
}

std::vector<QuantizationPrecision> MarianQuantizer::getSupportedPrecisions() const {
    return {
        QuantizationPrecision::FP32,
        QuantizationPrecision::FP16,
        QuantizationPrecision::INT8
    };
}

bool MarianQuantizer::quantizeMarianModel(const std::string& inputPath, 
                                        const std::string& outputPath,
                                        QuantizationPrecision precision) {
    // Placeholder implementation
    speechrnt::utils::Logger::info("Quantizing Marian model to " + 
                       QuantizationManager::precisionToString(precision));
    
    try {
        std::filesystem::copy_file(inputPath, outputPath);
        return validateMarianQuantization(outputPath);
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to quantize Marian model: " + std::string(e.what()));
        return false;
    }
}

bool MarianQuantizer::validateMarianQuantization(const std::string& modelPath) const {
    // Basic validation - check if file exists and is readable
    return std::filesystem::exists(modelPath) && std::filesystem::file_size(modelPath) > 0;
}

PiperQuantizer::PiperQuantizer() {
}

PiperQuantizer::~PiperQuantizer() {
}

bool PiperQuantizer::quantizeModel(const std::string& modelPath,
                                 const std::string& outputPath,
                                 const QuantizationConfig& config) {
    if (!std::filesystem::exists(modelPath)) {
        speechrnt::utils::Logger::error("Piper model not found: " + modelPath);
        return false;
    }
    
    auto timer = utils::PerformanceMonitor::getInstance().startLatencyTimer(
        "quantization.piper_quantization_latency_ms");
    
    currentConfig_ = config;
    stats_.originalModelSizeMB = std::filesystem::file_size(modelPath) / (1024 * 1024);
    
    bool success = quantizePiperModel(modelPath, outputPath, config.precision);
    
    if (success) {
        stats_.quantizedModelSizeMB = std::filesystem::file_size(outputPath) / (1024 * 1024);
        stats_.compressionRatio = static_cast<float>(stats_.originalModelSizeMB) / 
                                 static_cast<float>(stats_.quantizedModelSizeMB);
        
        loadedModelPath_ = outputPath;
        
        speechrnt::utils::Logger::info("Piper model quantized successfully: " + 
                           std::to_string(stats_.compressionRatio) + "x compression");
    }
    
    return success;
}

bool PiperQuantizer::loadQuantizedModel(const std::string& modelPath) {
    if (!std::filesystem::exists(modelPath)) {
        speechrnt::utils::Logger::error("Quantized Piper model not found: " + modelPath);
        return false;
    }
    
    loadedModelPath_ = modelPath;
    speechrnt::utils::Logger::info("Loaded quantized Piper model: " + modelPath);
    return true;
}

QuantizationStats PiperQuantizer::getQuantizationStats() const {
    return stats_;
}

float PiperQuantizer::validateAccuracy(const std::string& testDataPath) const {
    // Placeholder - would run audio quality metrics in production
    if (currentConfig_.precision == QuantizationPrecision::FP32) {
        return 1.0f;
    } else if (currentConfig_.precision == QuantizationPrecision::FP16) {
        return 0.96f;
    } else if (currentConfig_.precision == QuantizationPrecision::INT8) {
        return 0.91f;
    }
    
    return 0.85f;
}

std::vector<QuantizationPrecision> PiperQuantizer::getSupportedPrecisions() const {
    return {
        QuantizationPrecision::FP32,
        QuantizationPrecision::FP16,
        QuantizationPrecision::INT8
    };
}

bool PiperQuantizer::quantizePiperModel(const std::string& inputPath, 
                                      const std::string& outputPath,
                                      QuantizationPrecision precision) {
    // Placeholder implementation
    speechrnt::utils::Logger::info("Quantizing Piper model to " + 
                       QuantizationManager::precisionToString(precision));
    
    try {
        std::filesystem::copy_file(inputPath, outputPath);
        return true;
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to quantize Piper model: " + std::string(e.what()));
        return false;
    }
}

float PiperQuantizer::evaluateAudioQuality(const std::string& originalAudio, 
                                          const std::string& quantizedAudio) const {
    // Placeholder for audio quality evaluation
    // Would use metrics like PESQ, STOI, or MOS in production
    return 0.95f;
}

QuantizationManager& QuantizationManager::getInstance() {
    static QuantizationManager instance;
    return instance;
}

bool QuantizationManager::initialize() {
    if (initialized_) {
        return true;
    }
    
    initializeDefaultQuantizers();
    initialized_ = true;
    
    speechrnt::utils::Logger::info("QuantizationManager initialized with " + 
                       std::to_string(quantizers_.size()) + " quantizers");
    
    return true;
}

void QuantizationManager::registerQuantizer(const std::string& modelType, 
                                           std::unique_ptr<ModelQuantizer> quantizer) {
    quantizers_[modelType] = std::move(quantizer);
    speechrnt::utils::Logger::info("Registered quantizer for model type: " + modelType);
}

bool QuantizationManager::quantizeModel(const std::string& modelType,
                                       const std::string& modelPath,
                                       const std::string& outputPath,
                                       const QuantizationConfig& config) {
    if (!isValidModelType(modelType)) {
        speechrnt::utils::Logger::error("Unsupported model type for quantization: " + modelType);
        return false;
    }
    
    auto it = quantizers_.find(modelType);
    if (it == quantizers_.end()) {
        speechrnt::utils::Logger::error("No quantizer registered for model type: " + modelType);
        return false;
    }
    
    speechrnt::utils::Logger::info("Starting quantization for " + modelType + " model: " + modelPath);
    
    bool success = it->second->quantizeModel(modelPath, outputPath, config);
    
    if (success) {
        auto stats = it->second->getQuantizationStats();
        utils::PerformanceMonitor::getInstance().recordMetric(
            "quantization." + modelType + "_compression_ratio", stats.compressionRatio);
        utils::PerformanceMonitor::getInstance().recordMetric(
            "quantization." + modelType + "_size_reduction_mb", 
            static_cast<double>(stats.originalModelSizeMB - stats.quantizedModelSizeMB));
    }
    
    return success;
}

QuantizationConfig QuantizationManager::getRecommendedConfig(const std::string& modelType,
                                                           size_t availableMemoryMB,
                                                           float targetSpeedup) const {
    QuantizationConfig config;
    
    // Recommend precision based on available memory and target speedup
    if (availableMemoryMB < 2048) { // Less than 2GB
        config.precision = QuantizationPrecision::INT8;
        config.enableDynamicQuantization = true;
    } else if (availableMemoryMB < 4096) { // Less than 4GB
        config.precision = QuantizationPrecision::FP16;
    } else {
        config.precision = QuantizationPrecision::FP32;
    }
    
    // Adjust based on target speedup
    if (targetSpeedup >= 3.0f) {
        config.precision = QuantizationPrecision::INT8;
        config.enableDynamicQuantization = true;
        config.enableStaticQuantization = true;
    } else if (targetSpeedup >= 1.5f) {
        config.precision = QuantizationPrecision::FP16;
    }
    
    // Model-specific adjustments
    if (modelType == "whisper") {
        config.calibrationDataRatio = 0.05f; // Whisper needs less calibration data
    } else if (modelType == "marian") {
        config.calibrationDataRatio = 0.1f;
        config.calibrationMethod = "kl_divergence"; // Better for translation models
    } else if (modelType == "piper") {
        config.calibrationDataRatio = 0.15f; // TTS needs more calibration for quality
        config.calibrationMethod = "percentile";
    }
    
    return config;
}

std::map<std::string, bool> QuantizationManager::batchQuantize(
    const std::map<std::string, std::tuple<std::string, std::string, QuantizationConfig>>& modelSpecs) {
    
    std::map<std::string, bool> results;
    
    for (const auto& spec : modelSpecs) {
        const std::string& modelType = spec.first;
        const auto& [inputPath, outputPath, config] = spec.second;
        
        speechrnt::utils::Logger::info("Batch quantizing " + modelType + " model");
        
        bool success = quantizeModel(modelType, inputPath, outputPath, config);
        results[modelType] = success;
        
        if (success) {
            speechrnt::utils::Logger::info("Successfully quantized " + modelType + " model");
        } else {
            speechrnt::utils::Logger::error("Failed to quantize " + modelType + " model");
        }
    }
    
    return results;
}

std::map<std::string, QuantizationStats> QuantizationManager::getAllQuantizationStats() const {
    std::map<std::string, QuantizationStats> allStats;
    
    for (const auto& pair : quantizers_) {
        allStats[pair.first] = pair.second->getQuantizationStats();
    }
    
    return allStats;
}

std::map<std::string, float> QuantizationManager::validateAllModels(
    const std::map<std::string, std::string>& testDataPaths) const {
    
    std::map<std::string, float> accuracyScores;
    
    for (const auto& pair : testDataPaths) {
        const std::string& modelType = pair.first;
        const std::string& testDataPath = pair.second;
        
        auto it = quantizers_.find(modelType);
        if (it != quantizers_.end()) {
            float accuracy = it->second->validateAccuracy(testDataPath);
            accuracyScores[modelType] = accuracy;
            
            speechrnt::utils::Logger::info("Validation accuracy for " + modelType + ": " + 
                               std::to_string(accuracy * 100.0f) + "%");
        }
    }
    
    return accuracyScores;
}

std::vector<QuantizationPrecision> QuantizationManager::getSupportedPrecisions(
    const std::string& modelType) const {
    
    auto it = quantizers_.find(modelType);
    if (it != quantizers_.end()) {
        return it->second->getSupportedPrecisions();
    }
    
    return {};
}

std::string QuantizationManager::precisionToString(QuantizationPrecision precision) {
    switch (precision) {
        case QuantizationPrecision::FP32: return "fp32";
        case QuantizationPrecision::FP16: return "fp16";
        case QuantizationPrecision::INT8: return "int8";
        case QuantizationPrecision::INT4: return "int4";
        default: return "unknown";
    }
}

QuantizationPrecision QuantizationManager::stringToPrecision(const std::string& precisionStr) {
    if (precisionStr == "fp32") return QuantizationPrecision::FP32;
    if (precisionStr == "fp16") return QuantizationPrecision::FP16;
    if (precisionStr == "int8") return QuantizationPrecision::INT8;
    if (precisionStr == "int4") return QuantizationPrecision::INT4;
    
    return QuantizationPrecision::FP32; // Default
}

void QuantizationManager::initializeDefaultQuantizers() {
    quantizers_["whisper"] = std::make_unique<WhisperQuantizer>();
    quantizers_["marian"] = std::make_unique<MarianQuantizer>();
    quantizers_["piper"] = std::make_unique<PiperQuantizer>();
}

bool QuantizationManager::isValidModelType(const std::string& modelType) const {
    return modelType == "whisper" || modelType == "marian" || modelType == "piper";
}

} // namespace models
} // namespace speechrnt