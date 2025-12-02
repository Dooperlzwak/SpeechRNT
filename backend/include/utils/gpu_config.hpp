#pragma once

#include <map>
#include <string>
#include <vector>

namespace speechrnt {
namespace utils {

/**
 * GPU configuration settings
 */
struct GPUConfig {
  bool enabled;
  int deviceId;
  size_t memoryLimitMB;
  bool enableMemoryPool;
  size_t memoryPoolSizeMB;
  bool enableProfiling;

  GPUConfig()
      : enabled(false), deviceId(0), memoryLimitMB(4096),
        enableMemoryPool(true), memoryPoolSizeMB(1024), enableProfiling(false) {
  }
};

/**
 * Model-specific GPU configuration
 */
struct ModelGPUConfig {
  bool useGPU;
  int deviceId;
  int batchSize;
  bool enableQuantization;
  std::string precision; // "fp32", "fp16", "int8"

  ModelGPUConfig()
      : useGPU(false), deviceId(0), batchSize(1), enableQuantization(false),
        precision("fp32") {}
};

/**
 * GPU configuration manager
 * Handles loading, saving, and validation of GPU settings
 */
class GPUConfigManager {
public:
  static GPUConfigManager &getInstance();

  /**
   * Load configuration from file
   * @param configPath Path to configuration file
   * @return true if loaded successfully
   */
  bool loadConfig(const std::string &configPath = "backend/config/gpu.json");

  /**
   * Save configuration to file
   * @param configPath Path to configuration file
   * @return true if saved successfully
   */
  bool
  saveConfig(const std::string &configPath = "backend/config/gpu.json") const;

  /**
   * Get global GPU configuration
   * @return GPU configuration
   */
  const GPUConfig &getGlobalConfig() const;

  /**
   * Set global GPU configuration
   * @param config GPU configuration
   */
  void setGlobalConfig(const GPUConfig &config);

  /**
   * Get model-specific GPU configuration
   * @param modelName Model name (e.g., "whisper", "marian", "piper")
   * @return Model GPU configuration
   */
  ModelGPUConfig getModelConfig(const std::string &modelName) const;

  /**
   * Set model-specific GPU configuration
   * @param modelName Model name
   * @param config Model GPU configuration
   */
  void setModelConfig(const std::string &modelName,
                      const ModelGPUConfig &config);

  /**
   * Auto-detect optimal GPU configuration
   * @return true if auto-detection successful
   */
  bool autoDetectOptimalConfig();

  /**
   * Validate current configuration
   * @return true if configuration is valid
   */
  bool validateConfig() const;

  /**
   * Get recommended configuration for current hardware
   * @return recommended GPU configuration
   */
  GPUConfig getRecommendedConfig() const;

  /**
   * Get all available model configurations
   * @return map of model name to configuration
   */
  std::map<std::string, ModelGPUConfig> getAllModelConfigs() const;

  /**
   * Reset to default configuration
   */
  void resetToDefaults();

  /**
   * Get configuration as JSON string
   * @return JSON configuration string
   */
  std::string toJSON() const;

  /**
   * Load configuration from JSON string
   * @param json JSON configuration string
   * @return true if loaded successfully
   */
  bool fromJSON(const std::string &json);

private:
  GPUConfigManager() = default;
  ~GPUConfigManager() = default;

  // Prevent copying
  GPUConfigManager(const GPUConfigManager &) = delete;
  GPUConfigManager &operator=(const GPUConfigManager &) = delete;

  // Private methods
  void setDefaultConfigs();
  bool isValidDeviceId(int deviceId) const;
  bool isValidMemoryLimit(size_t memoryMB) const;
  bool isValidPrecision(const std::string &precision) const;

  // Member variables
  GPUConfig globalConfig_;
  std::map<std::string, ModelGPUConfig> modelConfigs_;

  // Default model names
  static const std::string MODEL_WHISPER;
  static const std::string MODEL_MARIAN;
  static const std::string MODEL_PIPER;
};

} // namespace utils
} // namespace speechrnt