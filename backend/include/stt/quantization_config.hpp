#pragma once

#include <string>
#include <unordered_map>
#include <memory>

namespace stt {

/**
 * Quantization levels supported by Whisper models
 */
enum class QuantizationLevel {
    FP32,    // Full precision (32-bit floating point)
    FP16,    // Half precision (16-bit floating point)
    INT8,    // 8-bit integer quantization
    AUTO     // Automatic selection based on available hardware
};

/**
 * Configuration for model quantization
 */
struct QuantizationConfig {
    QuantizationLevel level;
    bool enableGPUAcceleration;
    size_t minGPUMemoryMB;          // Minimum GPU memory required for this level
    float expectedAccuracyLoss;      // Expected accuracy loss compared to FP32
    std::string modelSuffix;         // File suffix for quantized models (e.g., "_fp16", "_int8")
    
    QuantizationConfig() 
        : level(QuantizationLevel::FP32)
        , enableGPUAcceleration(true)
        , minGPUMemoryMB(0)
        , expectedAccuracyLoss(0.0f)
        , modelSuffix("") {}
        
    QuantizationConfig(QuantizationLevel lvl, bool gpu, size_t memMB, float accLoss, const std::string& suffix)
        : level(lvl)
        , enableGPUAcceleration(gpu)
        , minGPUMemoryMB(memMB)
        , expectedAccuracyLoss(accLoss)
        , modelSuffix(suffix) {}
};

/**
 * Model accuracy validation results
 */
struct AccuracyValidationResult {
    float wordErrorRate;             // Word Error Rate (WER)
    float characterErrorRate;        // Character Error Rate (CER)
    float confidenceScore;           // Average confidence score
    size_t totalSamples;            // Number of validation samples
    bool passesThreshold;           // Whether accuracy meets minimum threshold
    std::string validationDetails;   // Detailed validation information
    
    AccuracyValidationResult()
        : wordErrorRate(0.0f)
        , characterErrorRate(0.0f)
        , confidenceScore(0.0f)
        , totalSamples(0)
        , passesThreshold(false) {}
};

/**
 * Quantization manager for handling different model precision levels
 */
class QuantizationManager {
public:
    QuantizationManager();
    ~QuantizationManager() = default;
    
    /**
     * Get quantization configuration for a specific level
     * @param level Quantization level
     * @return Configuration for the level
     */
    QuantizationConfig getConfig(QuantizationLevel level) const;
    
    /**
     * Select optimal quantization level based on available GPU memory
     * @param availableGPUMemoryMB Available GPU memory in MB
     * @param modelSizeMB Estimated model size in MB
     * @return Recommended quantization level
     */
    QuantizationLevel selectOptimalLevel(size_t availableGPUMemoryMB, size_t modelSizeMB) const;
    
    /**
     * Get quantization levels in order of preference (best to worst)
     * @param availableGPUMemoryMB Available GPU memory in MB
     * @return Vector of quantization levels in preference order
     */
    std::vector<QuantizationLevel> getPreferenceOrder(size_t availableGPUMemoryMB) const;
    
    /**
     * Convert quantization level to string
     * @param level Quantization level
     * @return String representation
     */
    std::string levelToString(QuantizationLevel level) const;
    
    /**
     * Convert string to quantization level
     * @param levelStr String representation
     * @return Quantization level, FP32 if invalid
     */
    QuantizationLevel stringToLevel(const std::string& levelStr) const;
    
    /**
     * Get model file path for specific quantization level
     * @param basePath Base model path
     * @param level Quantization level
     * @return Path to quantized model file
     */
    std::string getQuantizedModelPath(const std::string& basePath, QuantizationLevel level) const;
    
    /**
     * Check if quantization level is supported by current hardware
     * @param level Quantization level to check
     * @return true if supported
     */
    bool isLevelSupported(QuantizationLevel level) const;
    
    /**
     * Validate model accuracy for a specific quantization level
     * @param modelPath Path to the quantized model
     * @param level Quantization level
     * @param validationAudioPaths Paths to validation audio files
     * @param expectedTranscriptions Expected transcription results
     * @return Validation results
     */
    AccuracyValidationResult validateModelAccuracy(
        const std::string& modelPath,
        QuantizationLevel level,
        const std::vector<std::string>& validationAudioPaths,
        const std::vector<std::string>& expectedTranscriptions) const;
    
    /**
     * Set minimum accuracy threshold for quantized models
     * @param threshold Minimum accuracy threshold (0.0 to 1.0)
     */
    void setAccuracyThreshold(float threshold);
    
    /**
     * Get current accuracy threshold
     * @return Current accuracy threshold
     */
    float getAccuracyThreshold() const;

private:
    std::unordered_map<QuantizationLevel, QuantizationConfig> configs_;
    float accuracyThreshold_;
    
    void initializeConfigs();
    float calculateWordErrorRate(const std::string& expected, const std::string& actual) const;
    float calculateCharacterErrorRate(const std::string& expected, const std::string& actual) const;
    size_t calculateLevenshteinDistance(const std::string& s1, const std::string& s2) const;
};

} // namespace stt