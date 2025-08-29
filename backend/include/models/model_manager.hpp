#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <list>
#include <mutex>
#include <chrono>
#include <future>
#include <functional>

namespace speechrnt {
namespace models {

/**
 * Model quantization types
 */
enum class QuantizationType {
    NONE,
    INT8,
    INT16,
    FP16,
    DYNAMIC
};

/**
 * Model metadata structure
 */
struct ModelMetadata {
    std::string version;
    std::string checksum;
    std::string architecture;
    std::string sourceLanguage;
    std::string targetLanguage;
    size_t parameterCount;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point lastModified;
    std::unordered_map<std::string, std::string> customProperties;
    
    ModelMetadata() : parameterCount(0) {}
};

/**
 * Enhanced model information structure
 */
struct ModelInfo {
    std::string modelPath;
    std::string languagePair;
    size_t memoryUsage;
    std::chrono::steady_clock::time_point lastAccessed;
    bool loaded;
    void* modelData; // Opaque pointer to actual model
    
    // Enhanced features
    bool useGPU;
    int gpuDeviceId;
    QuantizationType quantization;
    ModelMetadata metadata;
    std::string integrityHash;
    bool validated;
    std::chrono::system_clock::time_point loadedAt;
    size_t accessCount;
    
    ModelInfo() : memoryUsage(0), loaded(false), modelData(nullptr), 
                  useGPU(false), gpuDeviceId(-1), quantization(QuantizationType::NONE),
                  validated(false), accessCount(0) {}
};

/**
 * Enhanced LRU Cache for managing AI models with advanced features
 * Supports GPU acceleration, quantization, hot-swapping, and integrity validation
 */
class ModelManager {
public:
    /**
     * Constructor
     * @param maxMemoryMB Maximum memory usage in MB
     * @param maxModels Maximum number of models to keep loaded
     */
    ModelManager(size_t maxMemoryMB = 4096, size_t maxModels = 10);
    
    ~ModelManager();
    
    // Basic model management (existing interface)
    /**
     * Load a model for the specified language pair
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @param modelPath Path to the model files
     * @return true if model loaded successfully
     */
    bool loadModel(const std::string& sourceLang, const std::string& targetLang, const std::string& modelPath);
    
    /**
     * Get a loaded model for the specified language pair
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return Pointer to model info, nullptr if not loaded
     */
    std::shared_ptr<ModelInfo> getModel(const std::string& sourceLang, const std::string& targetLang);
    
    /**
     * Unload a specific model
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return true if model was unloaded
     */
    bool unloadModel(const std::string& sourceLang, const std::string& targetLang);
    
    /**
     * Check if a model is loaded
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return true if model is loaded
     */
    bool isModelLoaded(const std::string& sourceLang, const std::string& targetLang) const;
    
    /**
     * Get list of currently loaded models
     * @return Vector of language pair keys
     */
    std::vector<std::string> getLoadedModels() const;
    
    /**
     * Get current memory usage in MB
     * @return Memory usage in MB
     */
    size_t getCurrentMemoryUsage() const;
    
    /**
     * Get number of loaded models
     * @return Number of loaded models
     */
    size_t getLoadedModelCount() const;
    
    /**
     * Set maximum memory usage
     * @param maxMemoryMB Maximum memory in MB
     */
    void setMaxMemoryUsage(size_t maxMemoryMB);
    
    /**
     * Set maximum number of models
     * @param maxModels Maximum number of models
     */
    void setMaxModels(size_t maxModels);
    
    /**
     * Clear all loaded models
     */
    void clearAll();
    
    /**
     * Validate language pair format and support
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return true if language pair is valid
     */
    bool validateLanguagePair(const std::string& sourceLang, const std::string& targetLang) const;
    
    /**
     * Get fallback language pairs for unsupported combinations
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return Vector of fallback language pairs
     */
    std::vector<std::pair<std::string, std::string>> getFallbackLanguagePairs(
        const std::string& sourceLang, const std::string& targetLang) const;
    
    /**
     * Get memory usage statistics
     * @return Map of model keys to memory usage
     */
    std::unordered_map<std::string, size_t> getMemoryStats() const;
    
    // Enhanced features for advanced model management
    
    /**
     * Load model with GPU acceleration support
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @param modelPath Path to the model files
     * @param useGPU Enable GPU acceleration
     * @param gpuDeviceId GPU device ID to use (-1 for auto-select)
     * @return true if model loaded successfully
     */
    bool loadModelWithGPU(const std::string& sourceLang, const std::string& targetLang, 
                         const std::string& modelPath, bool useGPU = true, int gpuDeviceId = -1);
    
    /**
     * Load model with quantization support
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @param modelPath Path to the model files
     * @param quantization Quantization type to apply
     * @return true if model loaded successfully
     */
    bool loadModelWithQuantization(const std::string& sourceLang, const std::string& targetLang,
                                  const std::string& modelPath, QuantizationType quantization);
    
    /**
     * Load model with full configuration
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @param modelPath Path to the model files
     * @param useGPU Enable GPU acceleration
     * @param gpuDeviceId GPU device ID to use
     * @param quantization Quantization type to apply
     * @return true if model loaded successfully
     */
    bool loadModelAdvanced(const std::string& sourceLang, const std::string& targetLang,
                          const std::string& modelPath, bool useGPU, int gpuDeviceId,
                          QuantizationType quantization);
    
    /**
     * Validate model files and integrity
     * @param modelPath Path to the model files
     * @return true if model is valid and intact
     */
    bool validateModelIntegrity(const std::string& modelPath);
    
    /**
     * Get model metadata
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return Model metadata, empty if not found
     */
    ModelMetadata getModelMetadata(const std::string& sourceLang, const std::string& targetLang) const;
    
    /**
     * Update model metadata
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @param metadata New metadata to set
     * @return true if metadata updated successfully
     */
    bool updateModelMetadata(const std::string& sourceLang, const std::string& targetLang,
                            const ModelMetadata& metadata);
    
    /**
     * Hot-swap a model without interrupting service
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @param newModelPath Path to the new model files
     * @return true if hot-swap successful
     */
    bool hotSwapModel(const std::string& sourceLang, const std::string& targetLang,
                     const std::string& newModelPath);
    
    /**
     * Hot-swap model asynchronously
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @param newModelPath Path to the new model files
     * @return Future that resolves when hot-swap completes
     */
    std::future<bool> hotSwapModelAsync(const std::string& sourceLang, const std::string& targetLang,
                                       const std::string& newModelPath);
    
    /**
     * Check if quantization is supported for a model
     * @param modelPath Path to the model files
     * @param quantization Quantization type to check
     * @return true if quantization is supported
     */
    bool isQuantizationSupported(const std::string& modelPath, QuantizationType quantization) const;
    
    /**
     * Get available quantization types for a model
     * @param modelPath Path to the model files
     * @return Vector of supported quantization types
     */
    std::vector<QuantizationType> getSupportedQuantizations(const std::string& modelPath) const;
    
    /**
     * Enable/disable automatic model validation
     * @param enabled true to enable automatic validation
     */
    void setAutoValidation(bool enabled);
    
    /**
     * Set model validation callback
     * @param callback Function to call for custom validation
     */
    void setValidationCallback(std::function<bool(const std::string&)> callback);
    
    /**
     * Get model version information
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return Version string, empty if not available
     */
    std::string getModelVersion(const std::string& sourceLang, const std::string& targetLang) const;
    
    /**
     * Check if a newer version of a model is available
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @param repositoryPath Path to model repository
     * @return true if newer version available
     */
    bool isNewerVersionAvailable(const std::string& sourceLang, const std::string& targetLang,
                                const std::string& repositoryPath) const;
    
    /**
     * Get detailed model statistics
     * @return Map of model keys to detailed statistics
     */
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> getDetailedStats() const;

private:
    // LRU cache implementation using list + unordered_map
    using CacheList = std::list<std::string>;
    using CacheMap = std::unordered_map<std::string, std::pair<std::shared_ptr<ModelInfo>, CacheList::iterator>>;
    
    // Private methods - existing
    std::string getLanguagePairKey(const std::string& sourceLang, const std::string& targetLang) const;
    void updateLRU(const std::string& key);
    void evictLRU();
    bool loadModelData(std::shared_ptr<ModelInfo> modelInfo);
    void unloadModelData(std::shared_ptr<ModelInfo> modelInfo);
    size_t estimateModelMemoryUsage(const std::string& modelPath) const;
    bool needsEviction() const;
    void performEviction();
    
    // Private methods - enhanced features
    bool loadModelDataWithGPU(std::shared_ptr<ModelInfo> modelInfo, bool useGPU, int gpuDeviceId);
    bool loadModelDataWithQuantization(std::shared_ptr<ModelInfo> modelInfo, QuantizationType quantization);
    bool loadModelDataAdvanced(std::shared_ptr<ModelInfo> modelInfo, bool useGPU, int gpuDeviceId, QuantizationType quantization);
    std::string calculateModelHash(const std::string& modelPath) const;
    bool validateModelFiles(const std::string& modelPath) const;
    ModelMetadata loadModelMetadata(const std::string& modelPath) const;
    bool saveModelMetadata(const std::string& modelPath, const ModelMetadata& metadata) const;
    void* quantizeModel(void* originalModel, QuantizationType quantization) const;
    bool transferModelToGPU(void* modelData, int gpuDeviceId) const;
    bool transferModelToCPU(void* modelData) const;
    std::string getQuantizationString(QuantizationType quantization) const;
    QuantizationType parseQuantizationType(const std::string& quantizationStr) const;
    bool isGPUMemorySufficient(size_t requiredMemoryMB, int gpuDeviceId) const;
    int selectOptimalGPUDevice(size_t requiredMemoryMB) const;
    
    // Member variables - existing
    mutable std::mutex cacheMutex_;
    CacheList lruList_;
    CacheMap cacheMap_;
    
    size_t maxMemoryMB_;
    size_t maxModels_;
    size_t currentMemoryUsage_;
    
    // Supported language pairs (loaded from configuration)
    std::unordered_map<std::string, std::vector<std::string>> supportedLanguagePairs_;
    
    // Statistics - existing
    size_t cacheHits_;
    size_t cacheMisses_;
    size_t evictions_;
    
    // Member variables - enhanced features
    bool autoValidationEnabled_;
    std::function<bool(const std::string&)> validationCallback_;
    std::unordered_map<std::string, std::string> modelVersions_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> lastValidationTimes_;
    
    // Hot-swap support
    mutable std::mutex hotSwapMutex_;
    std::unordered_map<std::string, std::shared_ptr<ModelInfo>> pendingSwaps_;
    
    // Enhanced statistics
    size_t gpuLoadCount_;
    size_t quantizationCount_;
    size_t validationCount_;
    size_t hotSwapCount_;
    size_t integrityFailures_;
};

} // namespace models
} // namespace speechrnt