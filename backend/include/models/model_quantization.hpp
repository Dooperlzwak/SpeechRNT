#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

namespace speechrnt {
namespace models {

/**
 * Quantization precision levels
 */
enum class QuantizationPrecision {
    FP32,    // Full precision (32-bit float)
    FP16,    // Half precision (16-bit float)
    INT8,    // 8-bit integer
    INT4     // 4-bit integer (experimental)
};

/**
 * Quantization configuration
 */
struct QuantizationConfig {
    QuantizationPrecision precision;
    bool enableDynamicQuantization;
    bool enableStaticQuantization;
    float calibrationDataRatio;  // Ratio of data to use for calibration (0.0-1.0)
    std::string calibrationMethod; // "entropy", "percentile", "kl_divergence"
    
    QuantizationConfig()
        : precision(QuantizationPrecision::FP32)
        , enableDynamicQuantization(false)
        , enableStaticQuantization(false)
        , calibrationDataRatio(0.1f)
        , calibrationMethod("entropy") {}
};

/**
 * Quantization statistics
 */
struct QuantizationStats {
    size_t originalModelSizeMB;
    size_t quantizedModelSizeMB;
    float compressionRatio;
    float accuracyLoss;
    float speedupFactor;
    std::string quantizationMethod;
    
    QuantizationStats()
        : originalModelSizeMB(0)
        , quantizedModelSizeMB(0)
        , compressionRatio(1.0f)
        , accuracyLoss(0.0f)
        , speedupFactor(1.0f) {}
};

/**
 * Model quantization interface
 */
class ModelQuantizer {
public:
    virtual ~ModelQuantizer() = default;
    
    /**
     * Quantize a model
     * @param modelPath Path to the original model
     * @param outputPath Path for the quantized model
     * @param config Quantization configuration
     * @return true if quantization successful
     */
    virtual bool quantizeModel(const std::string& modelPath,
                              const std::string& outputPath,
                              const QuantizationConfig& config) = 0;
    
    /**
     * Load quantized model
     * @param modelPath Path to quantized model
     * @return true if loaded successfully
     */
    virtual bool loadQuantizedModel(const std::string& modelPath) = 0;
    
    /**
     * Get quantization statistics
     * @return quantization statistics
     */
    virtual QuantizationStats getQuantizationStats() const = 0;
    
    /**
     * Validate quantized model accuracy
     * @param testDataPath Path to test data
     * @return accuracy score (0.0-1.0)
     */
    virtual float validateAccuracy(const std::string& testDataPath) const = 0;
    
    /**
     * Get supported quantization precisions
     * @return vector of supported precisions
     */
    virtual std::vector<QuantizationPrecision> getSupportedPrecisions() const = 0;
};

/**
 * Whisper model quantizer
 */
class WhisperQuantizer : public ModelQuantizer {
public:
    WhisperQuantizer();
    ~WhisperQuantizer() override;
    
    bool quantizeModel(const std::string& modelPath,
                      const std::string& outputPath,
                      const QuantizationConfig& config) override;
    
    bool loadQuantizedModel(const std::string& modelPath) override;
    
    QuantizationStats getQuantizationStats() const override;
    
    float validateAccuracy(const std::string& testDataPath) const override;
    
    std::vector<QuantizationPrecision> getSupportedPrecisions() const override;

private:
    QuantizationStats stats_;
    std::string loadedModelPath_;
    QuantizationConfig currentConfig_;
    
    // Private methods
    bool quantizeToFP16(const std::string& inputPath, const std::string& outputPath);
    bool quantizeToINT8(const std::string& inputPath, const std::string& outputPath);
    bool calibrateQuantization(const std::string& modelPath, const QuantizationConfig& config);
    size_t getModelSize(const std::string& modelPath) const;
};

/**
 * Marian model quantizer
 */
class MarianQuantizer : public ModelQuantizer {
public:
    MarianQuantizer();
    ~MarianQuantizer() override;
    
    bool quantizeModel(const std::string& modelPath,
                      const std::string& outputPath,
                      const QuantizationConfig& config) override;
    
    bool loadQuantizedModel(const std::string& modelPath) override;
    
    QuantizationStats getQuantizationStats() const override;
    
    float validateAccuracy(const std::string& testDataPath) const override;
    
    std::vector<QuantizationPrecision> getSupportedPrecisions() const override;

private:
    QuantizationStats stats_;
    std::string loadedModelPath_;
    QuantizationConfig currentConfig_;
    
    // Private methods
    bool quantizeMarianModel(const std::string& inputPath, const std::string& outputPath,
                           QuantizationPrecision precision);
    bool validateMarianQuantization(const std::string& modelPath) const;
};

/**
 * Coqui TTS model quantizer
 */
class CoquiQuantizer : public ModelQuantizer {
public:
    CoquiQuantizer();
    ~CoquiQuantizer() override;
    
    bool quantizeModel(const std::string& modelPath,
                      const std::string& outputPath,
                      const QuantizationConfig& config) override;
    
    bool loadQuantizedModel(const std::string& modelPath) override;
    
    QuantizationStats getQuantizationStats() const override;
    
    float validateAccuracy(const std::string& testDataPath) const override;
    
    std::vector<QuantizationPrecision> getSupportedPrecisions() const override;

private:
    QuantizationStats stats_;
    std::string loadedModelPath_;
    QuantizationConfig currentConfig_;
    
    // Private methods
    bool quantizeCoquiModel(const std::string& inputPath, const std::string& outputPath,
                          QuantizationPrecision precision);
    float evaluateAudioQuality(const std::string& originalAudio, 
                              const std::string& quantizedAudio) const;
};

/**
 * Quantization manager for all models
 */
class QuantizationManager {
public:
    static QuantizationManager& getInstance();
    
    /**
     * Initialize quantization manager
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * Register a model quantizer
     * @param modelType Model type identifier
     * @param quantizer Quantizer instance
     */
    void registerQuantizer(const std::string& modelType, 
                          std::unique_ptr<ModelQuantizer> quantizer);
    
    /**
     * Quantize a model
     * @param modelType Model type (whisper, marian, coqui)
     * @param modelPath Path to original model
     * @param outputPath Path for quantized model
     * @param config Quantization configuration
     * @return true if quantization successful
     */
    bool quantizeModel(const std::string& modelType,
                      const std::string& modelPath,
                      const std::string& outputPath,
                      const QuantizationConfig& config);
    
    /**
     * Get recommended quantization configuration for hardware
     * @param modelType Model type
     * @param availableMemoryMB Available memory in MB
     * @param targetSpeedup Target speedup factor
     * @return recommended configuration
     */
    QuantizationConfig getRecommendedConfig(const std::string& modelType,
                                          size_t availableMemoryMB,
                                          float targetSpeedup = 2.0f) const;
    
    /**
     * Batch quantize multiple models
     * @param modelSpecs Map of model type to (input path, output path, config)
     * @return map of model type to success status
     */
    std::map<std::string, bool> batchQuantize(
        const std::map<std::string, std::tuple<std::string, std::string, QuantizationConfig>>& modelSpecs);
    
    /**
     * Get quantization statistics for all models
     * @return map of model type to statistics
     */
    std::map<std::string, QuantizationStats> getAllQuantizationStats() const;
    
    /**
     * Validate all quantized models
     * @param testDataPaths Map of model type to test data path
     * @return map of model type to accuracy score
     */
    std::map<std::string, float> validateAllModels(
        const std::map<std::string, std::string>& testDataPaths) const;
    
    /**
     * Get supported precisions for a model type
     * @param modelType Model type
     * @return vector of supported precisions
     */
    std::vector<QuantizationPrecision> getSupportedPrecisions(const std::string& modelType) const;
    
    /**
     * Convert precision enum to string
     * @param precision Precision enum value
     * @return string representation
     */
    static std::string precisionToString(QuantizationPrecision precision);
    
    /**
     * Convert string to precision enum
     * @param precisionStr String representation
     * @return precision enum value
     */
    static QuantizationPrecision stringToPrecision(const std::string& precisionStr);

private:
    QuantizationManager() = default;
    ~QuantizationManager() = default;
    
    // Prevent copying
    QuantizationManager(const QuantizationManager&) = delete;
    QuantizationManager& operator=(const QuantizationManager&) = delete;
    
    // Private methods
    void initializeDefaultQuantizers();
    bool isValidModelType(const std::string& modelType) const;
    
    // Member variables
    std::map<std::string, std::unique_ptr<ModelQuantizer>> quantizers_;
    bool initialized_;
};

} // namespace models
} // namespace speechrnt