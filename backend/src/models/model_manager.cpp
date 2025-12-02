#include "models/model_manager.hpp"
#include "models/model_quantization.hpp"
#include "utils/logging.hpp"
#include "utils/gpu_manager.hpp"
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <future>
#include <thread>
#include <functional>

namespace speechrnt {
namespace models {

ModelManager::ModelManager(size_t maxMemoryMB, size_t maxModels)
    : maxMemoryMB_(maxMemoryMB)
    , maxModels_(maxModels)
    , currentMemoryUsage_(0)
    , cacheHits_(0)
    , cacheMisses_(0)
    , evictions_(0)
    , autoValidationEnabled_(true)
    , gpuLoadCount_(0)
    , quantizationCount_(0)
    , validationCount_(0)
    , hotSwapCount_(0)
    , integrityFailures_(0) {
    
    // Initialize supported language pairs
    // In a real implementation, this would be loaded from configuration
    supportedLanguagePairs_["en"] = {"es", "fr", "de", "it", "pt", "ru", "zh", "ja", "ko"};
    supportedLanguagePairs_["es"] = {"en", "fr", "de", "it", "pt"};
    supportedLanguagePairs_["fr"] = {"en", "es", "de", "it", "pt"};
    supportedLanguagePairs_["de"] = {"en", "es", "fr", "it", "pt"};
    supportedLanguagePairs_["it"] = {"en", "es", "fr", "de", "pt"};
    supportedLanguagePairs_["pt"] = {"en", "es", "fr", "de", "it"};
    supportedLanguagePairs_["ru"] = {"en"};
    supportedLanguagePairs_["zh"] = {"en"};
    supportedLanguagePairs_["ja"] = {"en"};
    supportedLanguagePairs_["ko"] = {"en"};
    
    speechrnt::utils::Logger::info("ModelManager initialized with max memory: " + std::to_string(maxMemoryMB) + 
                       "MB, max models: " + std::to_string(maxModels));
}

ModelManager::~ModelManager() {
    clearAll();
    speechrnt::utils::Logger::info("ModelManager destroyed");
}

bool ModelManager::loadModel(const std::string& sourceLang, const std::string& targetLang, const std::string& modelPath) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    
    // Check if model is already loaded
    auto it = cacheMap_.find(key);
    if (it != cacheMap_.end()) {
        updateLRU(key);
        cacheHits_++;
        speechrnt::utils::Logger::debug("Model already loaded for " + key);
        return true;
    }
    
    // Validate language pair
    if (!validateLanguagePair(sourceLang, targetLang)) {
        speechrnt::utils::Logger::error("Invalid language pair: " + sourceLang + " -> " + targetLang);
        return false;
    }
    
    // Create model info
    auto modelInfo = std::make_shared<ModelInfo>();
    modelInfo->modelPath = modelPath;
    modelInfo->languagePair = key;
    modelInfo->lastAccessed = std::chrono::steady_clock::now();
    
    // Estimate memory usage
    modelInfo->memoryUsage = estimateModelMemoryUsage(modelPath);
    
    // Check if we need to evict models to make space
    while (needsEviction() || (currentMemoryUsage_ + modelInfo->memoryUsage > maxMemoryMB_) || 
           (cacheMap_.size() >= maxModels_)) {
        if (cacheMap_.empty()) {
            speechrnt::utils::Logger::error("Cannot load model: insufficient memory or model too large");
            return false;
        }
        performEviction();
    }
    
    // Load the actual model data
    if (!loadModelData(modelInfo)) {
        speechrnt::utils::Logger::error("Failed to load model data for " + key);
        return false;
    }
    
    // Add to cache
    lruList_.push_front(key);
    cacheMap_[key] = std::make_pair(modelInfo, lruList_.begin());
    currentMemoryUsage_ += modelInfo->memoryUsage;
    cacheMisses_++;
    
    speechrnt::utils::Logger::info("Loaded model for " + key + " (memory: " + std::to_string(modelInfo->memoryUsage) + "MB)");
    return true;
}

std::shared_ptr<ModelInfo> ModelManager::getModel(const std::string& sourceLang, const std::string& targetLang) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    auto it = cacheMap_.find(key);
    
    if (it != cacheMap_.end()) {
        updateLRU(key);
        it->second.first->lastAccessed = std::chrono::steady_clock::now();
        cacheHits_++;
        return it->second.first;
    }
    
    cacheMisses_++;
    return nullptr;
}

bool ModelManager::unloadModel(const std::string& sourceLang, const std::string& targetLang) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    auto it = cacheMap_.find(key);
    
    if (it != cacheMap_.end()) {
        auto modelInfo = it->second.first;
        auto listIt = it->second.second;
        
        // Unload model data
        unloadModelData(modelInfo);
        
        // Remove from cache
        currentMemoryUsage_ -= modelInfo->memoryUsage;
        lruList_.erase(listIt);
        cacheMap_.erase(it);
        
        speechrnt::utils::Logger::info("Unloaded model for " + key);
        return true;
    }
    
    return false;
}

bool ModelManager::isModelLoaded(const std::string& sourceLang, const std::string& targetLang) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    auto it = cacheMap_.find(key);
    
    return it != cacheMap_.end() && it->second.first->loaded;
}

std::vector<std::string> ModelManager::getLoadedModels() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::vector<std::string> models;
    for (const auto& pair : cacheMap_) {
        if (pair.second.first->loaded) {
            models.push_back(pair.first);
        }
    }
    
    return models;
}

size_t ModelManager::getCurrentMemoryUsage() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return currentMemoryUsage_;
}

size_t ModelManager::getLoadedModelCount() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return cacheMap_.size();
}

void ModelManager::setMaxMemoryUsage(size_t maxMemoryMB) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    maxMemoryMB_ = maxMemoryMB;
    
    // Evict models if we're over the new limit
    while (currentMemoryUsage_ > maxMemoryMB_ && !cacheMap_.empty()) {
        performEviction();
    }
    
    speechrnt::utils::Logger::info("Max memory usage set to " + std::to_string(maxMemoryMB) + "MB");
}

void ModelManager::setMaxModels(size_t maxModels) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    maxModels_ = maxModels;
    
    // Evict models if we're over the new limit
    while (cacheMap_.size() > maxModels_ && !cacheMap_.empty()) {
        performEviction();
    }
    
    speechrnt::utils::Logger::info("Max models set to " + std::to_string(maxModels));
}

void ModelManager::clearAll() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    // Unload all models
    for (auto& pair : cacheMap_) {
        unloadModelData(pair.second.first);
    }
    
    cacheMap_.clear();
    lruList_.clear();
    currentMemoryUsage_ = 0;
    
    speechrnt::utils::Logger::info("All models cleared from cache");
}

bool ModelManager::validateLanguagePair(const std::string& sourceLang, const std::string& targetLang) const {
    auto it = supportedLanguagePairs_.find(sourceLang);
    if (it == supportedLanguagePairs_.end()) {
        return false;
    }
    
    const auto& targets = it->second;
    return std::find(targets.begin(), targets.end(), targetLang) != targets.end();
}

std::vector<std::pair<std::string, std::string>> ModelManager::getFallbackLanguagePairs(
    const std::string& sourceLang, const std::string& targetLang) const {
    
    std::vector<std::pair<std::string, std::string>> fallbacks;
    
    // If direct pair is not supported, try common fallbacks
    if (!validateLanguagePair(sourceLang, targetLang)) {
        // Try via English as intermediate language
        if (sourceLang != "en" && targetLang != "en") {
            if (validateLanguagePair(sourceLang, "en") && validateLanguagePair("en", targetLang)) {
                fallbacks.push_back({sourceLang, "en"});
                fallbacks.push_back({"en", targetLang});
            }
        }
        
        // Try reverse direction
        if (validateLanguagePair(targetLang, sourceLang)) {
            fallbacks.push_back({targetLang, sourceLang});
        }
    }
    
    return fallbacks;
}

std::unordered_map<std::string, size_t> ModelManager::getMemoryStats() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::unordered_map<std::string, size_t> stats;
    for (const auto& pair : cacheMap_) {
        stats[pair.first] = pair.second.first->memoryUsage;
    }
    
    return stats;
}

std::string ModelManager::getLanguagePairKey(const std::string& sourceLang, const std::string& targetLang) const {
    return sourceLang + "->" + targetLang;
}

void ModelManager::updateLRU(const std::string& key) {
    auto it = cacheMap_.find(key);
    if (it != cacheMap_.end()) {
        // Move to front of LRU list
        lruList_.erase(it->second.second);
        lruList_.push_front(key);
        it->second.second = lruList_.begin();
    }
}

void ModelManager::evictLRU() {
    if (!lruList_.empty()) {
        std::string lruKey = lruList_.back();
        auto it = cacheMap_.find(lruKey);
        
        if (it != cacheMap_.end()) {
            auto modelInfo = it->second.first;
            
            // Unload model data
            unloadModelData(modelInfo);
            
            // Remove from cache
            currentMemoryUsage_ -= modelInfo->memoryUsage;
            lruList_.pop_back();
            cacheMap_.erase(it);
            evictions_++;
            
            speechrnt::utils::Logger::info("Evicted LRU model: " + lruKey);
        }
    }
}

bool ModelManager::loadModelData(std::shared_ptr<ModelInfo> modelInfo) {
    // Validate model files first
    if (!validateModelFiles(modelInfo->modelPath)) {
        speechrnt::utils::Logger::error("Model validation failed for: " + modelInfo->modelPath);
        return false;
    }
    
    try {
        // Initialize GPU manager if not already done
        auto& gpuManager = utils::GPUManager::getInstance();
        if (!gpuManager.initialize()) {
            speechrnt::utils::Logger::warn("GPU manager initialization failed, using CPU-only mode");
        }
        
        // For now, we'll use a placeholder structure to represent loaded model data
        // In a real implementation, this would load actual Marian NMT models
        struct ModelData {
            std::string modelPath;
            std::string languagePair;
            bool isLoaded;
            std::chrono::system_clock::time_point loadTime;
            
            ModelData(const std::string& path, const std::string& pair) 
                : modelPath(path), languagePair(pair), isLoaded(true), 
                  loadTime(std::chrono::system_clock::now()) {}
        };
        
        // Create model data structure
        auto* modelData = new ModelData(modelInfo->modelPath, modelInfo->languagePair);
        modelInfo->modelData = static_cast<void*>(modelData);
        modelInfo->loaded = true;
        modelInfo->loadedAt = std::chrono::system_clock::now();
        modelInfo->accessCount = 0;
        
        speechrnt::utils::Logger::info("Successfully loaded model: " + modelInfo->languagePair + 
                           " from " + modelInfo->modelPath);
        
        return true;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to load model data: " + std::string(e.what()));
        return false;
    }
}

void ModelManager::unloadModelData(std::shared_ptr<ModelInfo> modelInfo) {
    if (modelInfo && modelInfo->loaded && modelInfo->modelData) {
        try {
            // Clean up the model data structure
            struct ModelData {
                std::string modelPath;
                std::string languagePair;
                bool isLoaded;
                std::chrono::system_clock::time_point loadTime;
            };
            
            auto* modelData = static_cast<ModelData*>(modelInfo->modelData);
            delete modelData;
            
            modelInfo->modelData = nullptr;
            modelInfo->loaded = false;
            
            speechrnt::utils::Logger::info("Successfully unloaded model: " + modelInfo->languagePair);
            
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Error during model cleanup: " + std::string(e.what()));
            // Still mark as unloaded to prevent memory leaks
            modelInfo->modelData = nullptr;
            modelInfo->loaded = false;
        }
    }
}

size_t ModelManager::estimateModelMemoryUsage(const std::string& modelPath) const {
    // Estimate memory usage based on model files
    size_t totalSize = 0;
    
    try {
        if (std::filesystem::exists(modelPath)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(modelPath)) {
                if (entry.is_regular_file()) {
                    totalSize += entry.file_size();
                }
            }
        }
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::warn("Failed to estimate model size: " + std::string(e.what()));
        // Default estimate for unknown models
        return 500; // 500MB default
    }
    
    // Convert bytes to MB and add overhead (models typically use 1.5-2x file size in memory)
    size_t memoryMB = (totalSize / (1024 * 1024)) * 2;
    
    // Minimum 100MB, maximum 2GB per model
    return std::max(static_cast<size_t>(100), std::min(static_cast<size_t>(2048), memoryMB));
}

bool ModelManager::needsEviction() const {
    return currentMemoryUsage_ > maxMemoryMB_ || cacheMap_.size() > maxModels_;
}

void ModelManager::performEviction() {
    evictLRU();
}

// Enhanced features implementation

bool ModelManager::loadModelWithGPU(const std::string& sourceLang, const std::string& targetLang, 
                                   const std::string& modelPath, bool useGPU, int gpuDeviceId) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    
    // Check if model is already loaded
    auto it = cacheMap_.find(key);
    if (it != cacheMap_.end()) {
        updateLRU(key);
        cacheHits_++;
        speechrnt::utils::Logger::debug("Model already loaded for " + key);
        return true;
    }
    
    // Validate language pair
    if (!validateLanguagePair(sourceLang, targetLang)) {
        speechrnt::utils::Logger::error("Invalid language pair: " + sourceLang + " -> " + targetLang);
        return false;
    }
    
    // Validate model integrity if auto-validation is enabled
    if (autoValidationEnabled_ && !validateModelIntegrity(modelPath)) {
        speechrnt::utils::Logger::error("Model integrity validation failed for " + key);
        integrityFailures_++;
        return false;
    }
    
    // Create model info
    auto modelInfo = std::make_shared<ModelInfo>();
    modelInfo->modelPath = modelPath;
    modelInfo->languagePair = key;
    modelInfo->lastAccessed = std::chrono::steady_clock::now();
    modelInfo->useGPU = useGPU;
    modelInfo->gpuDeviceId = gpuDeviceId;
    modelInfo->quantization = QuantizationType::NONE;
    
    // Load metadata
    modelInfo->metadata = loadModelMetadata(modelPath);
    
    // Calculate integrity hash
    modelInfo->integrityHash = calculateModelHash(modelPath);
    modelInfo->validated = true;
    
    // Estimate memory usage
    modelInfo->memoryUsage = estimateModelMemoryUsage(modelPath);
    
    // Check GPU memory if using GPU
    if (useGPU) {
        int selectedDevice = gpuDeviceId;
        if (selectedDevice == -1) {
            selectedDevice = selectOptimalGPUDevice(modelInfo->memoryUsage);
            if (selectedDevice == -1) {
                speechrnt::utils::Logger::warn("No suitable GPU device found, falling back to CPU for " + key);
                modelInfo->useGPU = false;
                modelInfo->gpuDeviceId = -1;
            } else {
                modelInfo->gpuDeviceId = selectedDevice;
            }
        } else if (!isGPUMemorySufficient(modelInfo->memoryUsage, selectedDevice)) {
            speechrnt::utils::Logger::warn("Insufficient GPU memory on device " + std::to_string(selectedDevice) + 
                               ", falling back to CPU for " + key);
            modelInfo->useGPU = false;
            modelInfo->gpuDeviceId = -1;
        }
    }
    
    // Check if we need to evict models to make space
    while (needsEviction() || (currentMemoryUsage_ + modelInfo->memoryUsage > maxMemoryMB_) || 
           (cacheMap_.size() >= maxModels_)) {
        if (cacheMap_.empty()) {
            speechrnt::utils::Logger::error("Cannot load model: insufficient memory or model too large");
            return false;
        }
        performEviction();
    }
    
    // Load the actual model data with GPU support
    if (!loadModelDataWithGPU(modelInfo, modelInfo->useGPU, modelInfo->gpuDeviceId)) {
        speechrnt::utils::Logger::error("Failed to load model data for " + key);
        return false;
    }
    
    // Add to cache
    lruList_.push_front(key);
    cacheMap_[key] = std::make_pair(modelInfo, lruList_.begin());
    currentMemoryUsage_ += modelInfo->memoryUsage;
    cacheMisses_++;
    
    if (modelInfo->useGPU) {
        gpuLoadCount_++;
    }
    
    speechrnt::utils::Logger::info("Loaded model for " + key + " (memory: " + std::to_string(modelInfo->memoryUsage) + 
                       "MB, GPU: " + (modelInfo->useGPU ? "enabled" : "disabled") + ")");
    return true;
}

bool ModelManager::loadModelWithQuantization(const std::string& sourceLang, const std::string& targetLang,
                                            const std::string& modelPath, QuantizationType quantization) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    
    // Check if model is already loaded
    auto it = cacheMap_.find(key);
    if (it != cacheMap_.end()) {
        updateLRU(key);
        cacheHits_++;
        speechrnt::utils::Logger::debug("Model already loaded for " + key);
        return true;
    }
    
    // Validate language pair
    if (!validateLanguagePair(sourceLang, targetLang)) {
        speechrnt::utils::Logger::error("Invalid language pair: " + sourceLang + " -> " + targetLang);
        return false;
    }
    
    // Check if quantization is supported
    if (quantization != QuantizationType::NONE && !isQuantizationSupported(modelPath, quantization)) {
        speechrnt::utils::Logger::error("Quantization type " + getQuantizationString(quantization) + 
                             " not supported for model " + key);
        return false;
    }
    
    // Validate model integrity if auto-validation is enabled
    if (autoValidationEnabled_ && !validateModelIntegrity(modelPath)) {
        speechrnt::utils::Logger::error("Model integrity validation failed for " + key);
        integrityFailures_++;
        return false;
    }
    
    // Create model info
    auto modelInfo = std::make_shared<ModelInfo>();
    modelInfo->modelPath = modelPath;
    modelInfo->languagePair = key;
    modelInfo->lastAccessed = std::chrono::steady_clock::now();
    modelInfo->useGPU = false;
    modelInfo->gpuDeviceId = -1;
    modelInfo->quantization = quantization;
    
    // Load metadata
    modelInfo->metadata = loadModelMetadata(modelPath);
    
    // Calculate integrity hash
    modelInfo->integrityHash = calculateModelHash(modelPath);
    modelInfo->validated = true;
    
    // Estimate memory usage (quantization typically reduces memory usage)
    modelInfo->memoryUsage = estimateModelMemoryUsage(modelPath);
    if (quantization != QuantizationType::NONE) {
        // Estimate memory reduction based on quantization type
        float reductionFactor = 1.0f;
        switch (quantization) {
            case QuantizationType::INT8:
                reductionFactor = 0.25f; // 8-bit vs 32-bit
                break;
            case QuantizationType::INT16:
                reductionFactor = 0.5f; // 16-bit vs 32-bit
                break;
            case QuantizationType::FP16:
                reductionFactor = 0.5f; // 16-bit vs 32-bit
                break;
            case QuantizationType::DYNAMIC:
                reductionFactor = 0.6f; // Variable reduction
                break;
            default:
                break;
        }
        modelInfo->memoryUsage = static_cast<size_t>(modelInfo->memoryUsage * reductionFactor);
    }
    
    // Check if we need to evict models to make space
    while (needsEviction() || (currentMemoryUsage_ + modelInfo->memoryUsage > maxMemoryMB_) || 
           (cacheMap_.size() >= maxModels_)) {
        if (cacheMap_.empty()) {
            speechrnt::utils::Logger::error("Cannot load model: insufficient memory or model too large");
            return false;
        }
        performEviction();
    }
    
    // Load the actual model data with quantization
    if (!loadModelDataWithQuantization(modelInfo, quantization)) {
        speechrnt::utils::Logger::error("Failed to load model data with quantization for " + key);
        return false;
    }
    
    // Add to cache
    lruList_.push_front(key);
    cacheMap_[key] = std::make_pair(modelInfo, lruList_.begin());
    currentMemoryUsage_ += modelInfo->memoryUsage;
    cacheMisses_++;
    
    if (quantization != QuantizationType::NONE) {
        quantizationCount_++;
    }
    
    speechrnt::utils::Logger::info("Loaded model for " + key + " (memory: " + std::to_string(modelInfo->memoryUsage) + 
                       "MB, quantization: " + getQuantizationString(quantization) + ")");
    return true;
}

bool ModelManager::loadModelAdvanced(const std::string& sourceLang, const std::string& targetLang,
                                    const std::string& modelPath, bool useGPU, int gpuDeviceId,
                                    QuantizationType quantization) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    
    // Check if model is already loaded
    auto it = cacheMap_.find(key);
    if (it != cacheMap_.end()) {
        updateLRU(key);
        cacheHits_++;
        speechrnt::utils::Logger::debug("Model already loaded for " + key);
        return true;
    }
    
    // Validate language pair
    if (!validateLanguagePair(sourceLang, targetLang)) {
        speechrnt::utils::Logger::error("Invalid language pair: " + sourceLang + " -> " + targetLang);
        return false;
    }
    
    // Check if quantization is supported
    if (quantization != QuantizationType::NONE && !isQuantizationSupported(modelPath, quantization)) {
        speechrnt::utils::Logger::error("Quantization type " + getQuantizationString(quantization) + 
                             " not supported for model " + key);
        return false;
    }
    
    // Validate model integrity if auto-validation is enabled
    if (autoValidationEnabled_ && !validateModelIntegrity(modelPath)) {
        speechrnt::utils::Logger::error("Model integrity validation failed for " + key);
        integrityFailures_++;
        return false;
    }
    
    // Create model info
    auto modelInfo = std::make_shared<ModelInfo>();
    modelInfo->modelPath = modelPath;
    modelInfo->languagePair = key;
    modelInfo->lastAccessed = std::chrono::steady_clock::now();
    modelInfo->useGPU = useGPU;
    modelInfo->gpuDeviceId = gpuDeviceId;
    modelInfo->quantization = quantization;
    
    // Load metadata
    modelInfo->metadata = loadModelMetadata(modelPath);
    
    // Calculate integrity hash
    modelInfo->integrityHash = calculateModelHash(modelPath);
    modelInfo->validated = true;
    
    // Estimate memory usage
    modelInfo->memoryUsage = estimateModelMemoryUsage(modelPath);
    if (quantization != QuantizationType::NONE) {
        // Apply quantization memory reduction
        float reductionFactor = 1.0f;
        switch (quantization) {
            case QuantizationType::INT8:
                reductionFactor = 0.25f;
                break;
            case QuantizationType::INT16:
                reductionFactor = 0.5f;
                break;
            case QuantizationType::FP16:
                reductionFactor = 0.5f;
                break;
            case QuantizationType::DYNAMIC:
                reductionFactor = 0.6f;
                break;
            default:
                break;
        }
        modelInfo->memoryUsage = static_cast<size_t>(modelInfo->memoryUsage * reductionFactor);
    }
    
    // Check GPU memory if using GPU
    if (useGPU) {
        int selectedDevice = gpuDeviceId;
        if (selectedDevice == -1) {
            selectedDevice = selectOptimalGPUDevice(modelInfo->memoryUsage);
            if (selectedDevice == -1) {
                speechrnt::utils::Logger::warn("No suitable GPU device found, falling back to CPU for " + key);
                modelInfo->useGPU = false;
                modelInfo->gpuDeviceId = -1;
            } else {
                modelInfo->gpuDeviceId = selectedDevice;
            }
        } else if (!isGPUMemorySufficient(modelInfo->memoryUsage, selectedDevice)) {
            speechrnt::utils::Logger::warn("Insufficient GPU memory on device " + std::to_string(selectedDevice) + 
                               ", falling back to CPU for " + key);
            modelInfo->useGPU = false;
            modelInfo->gpuDeviceId = -1;
        }
    }
    
    // Check if we need to evict models to make space
    while (needsEviction() || (currentMemoryUsage_ + modelInfo->memoryUsage > maxMemoryMB_) || 
           (cacheMap_.size() >= maxModels_)) {
        if (cacheMap_.empty()) {
            speechrnt::utils::Logger::error("Cannot load model: insufficient memory or model too large");
            return false;
        }
        performEviction();
    }
    
    // Load the actual model data with advanced configuration
    if (!loadModelDataAdvanced(modelInfo, modelInfo->useGPU, modelInfo->gpuDeviceId, quantization)) {
        speechrnt::utils::Logger::error("Failed to load model data with advanced configuration for " + key);
        return false;
    }
    
    // Add to cache
    lruList_.push_front(key);
    cacheMap_[key] = std::make_pair(modelInfo, lruList_.begin());
    currentMemoryUsage_ += modelInfo->memoryUsage;
    cacheMisses_++;
    
    if (modelInfo->useGPU) {
        gpuLoadCount_++;
    }
    if (quantization != QuantizationType::NONE) {
        quantizationCount_++;
    }
    
    speechrnt::utils::Logger::info("Loaded model for " + key + " (memory: " + std::to_string(modelInfo->memoryUsage) + 
                       "MB, GPU: " + (modelInfo->useGPU ? "enabled" : "disabled") + 
                       ", quantization: " + getQuantizationString(quantization) + ")");
    return true;
}

bool ModelManager::validateModelIntegrity(const std::string& modelPath) {
    validationCount_++;
    
    // Check if files exist
    if (!validateModelFiles(modelPath)) {
        return false;
    }
    
    // Calculate current hash
    std::string currentHash = calculateModelHash(modelPath);
    if (currentHash.empty()) {
        speechrnt::utils::Logger::error("Failed to calculate model hash for " + modelPath);
        return false;
    }
    
    // Check if we have a stored hash for comparison
    std::string metadataPath = modelPath + "/metadata.json";
    if (std::filesystem::exists(metadataPath)) {
        ModelMetadata metadata = loadModelMetadata(modelPath);
        if (!metadata.checksum.empty() && metadata.checksum != currentHash) {
            speechrnt::utils::Logger::error("Model integrity check failed: hash mismatch for " + modelPath);
            return false;
        }
    }
    
    // Use custom validation callback if set
    if (validationCallback_) {
        if (!validationCallback_(modelPath)) {
            speechrnt::utils::Logger::error("Custom validation failed for " + modelPath);
            return false;
        }
    }
    
    speechrnt::utils::Logger::debug("Model integrity validation passed for " + modelPath);
    return true;
}

ModelMetadata ModelManager::getModelMetadata(const std::string& sourceLang, const std::string& targetLang) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    auto it = cacheMap_.find(key);
    
    if (it != cacheMap_.end()) {
        return it->second.first->metadata;
    }
    
    return ModelMetadata(); // Return empty metadata if not found
}

bool ModelManager::updateModelMetadata(const std::string& sourceLang, const std::string& targetLang,
                                      const ModelMetadata& metadata) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    auto it = cacheMap_.find(key);
    
    if (it != cacheMap_.end()) {
        it->second.first->metadata = metadata;
        
        // Save metadata to file
        if (!saveModelMetadata(it->second.first->modelPath, metadata)) {
            speechrnt::utils::Logger::warn("Failed to save metadata to file for " + key);
        }
        
        speechrnt::utils::Logger::info("Updated metadata for model " + key);
        return true;
    }
    
    return false;
}

bool ModelManager::hotSwapModel(const std::string& sourceLang, const std::string& targetLang,
                               const std::string& newModelPath) {
    std::lock_guard<std::mutex> hotSwapLock(hotSwapMutex_);
    std::lock_guard<std::mutex> cacheLock(cacheMutex_);
    
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    auto it = cacheMap_.find(key);
    
    if (it == cacheMap_.end()) {
        speechrnt::utils::Logger::error("Cannot hot-swap: model " + key + " not currently loaded");
        return false;
    }
    
    auto currentModel = it->second.first;
    
    // Validate new model integrity
    if (autoValidationEnabled_ && !validateModelIntegrity(newModelPath)) {
        speechrnt::utils::Logger::error("New model integrity validation failed for hot-swap of " + key);
        return false;
    }
    
    // Create new model info with same configuration as current model
    auto newModelInfo = std::make_shared<ModelInfo>();
    newModelInfo->modelPath = newModelPath;
    newModelInfo->languagePair = key;
    newModelInfo->lastAccessed = std::chrono::steady_clock::now();
    newModelInfo->useGPU = currentModel->useGPU;
    newModelInfo->gpuDeviceId = currentModel->gpuDeviceId;
    newModelInfo->quantization = currentModel->quantization;
    
    // Load metadata for new model
    newModelInfo->metadata = loadModelMetadata(newModelPath);
    newModelInfo->integrityHash = calculateModelHash(newModelPath);
    newModelInfo->validated = true;
    
    // Estimate memory usage for new model
    newModelInfo->memoryUsage = estimateModelMemoryUsage(newModelPath);
    if (newModelInfo->quantization != QuantizationType::NONE) {
        // Apply quantization memory reduction
        float reductionFactor = 1.0f;
        switch (newModelInfo->quantization) {
            case QuantizationType::INT8:
                reductionFactor = 0.25f;
                break;
            case QuantizationType::INT16:
                reductionFactor = 0.5f;
                break;
            case QuantizationType::FP16:
                reductionFactor = 0.5f;
                break;
            case QuantizationType::DYNAMIC:
                reductionFactor = 0.6f;
                break;
            default:
                break;
        }
        newModelInfo->memoryUsage = static_cast<size_t>(newModelInfo->memoryUsage * reductionFactor);
    }
    
    // Check if we have enough memory for the new model
    size_t availableMemory = maxMemoryMB_ - (currentMemoryUsage_ - currentModel->memoryUsage);
    if (newModelInfo->memoryUsage > availableMemory) {
        speechrnt::utils::Logger::error("Insufficient memory for hot-swap of " + key);
        return false;
    }
    
    // Load new model data
    if (!loadModelDataAdvanced(newModelInfo, newModelInfo->useGPU, 
                              newModelInfo->gpuDeviceId, newModelInfo->quantization)) {
        speechrnt::utils::Logger::error("Failed to load new model data for hot-swap of " + key);
        return false;
    }
    
    // Perform atomic swap
    unloadModelData(currentModel);
    currentMemoryUsage_ = currentMemoryUsage_ - currentModel->memoryUsage + newModelInfo->memoryUsage;
    
    // Update cache entry
    it->second.first = newModelInfo;
    updateLRU(key);
    
    hotSwapCount_++;
    
    speechrnt::utils::Logger::info("Successfully hot-swapped model " + key + " to " + newModelPath);
    return true;
}

std::future<bool> ModelManager::hotSwapModelAsync(const std::string& sourceLang, const std::string& targetLang,
                                                 const std::string& newModelPath) {
    return std::async(std::launch::async, [this, sourceLang, targetLang, newModelPath]() {
        return hotSwapModel(sourceLang, targetLang, newModelPath);
    });
}

bool ModelManager::isQuantizationSupported(const std::string& modelPath, QuantizationType quantization) const {
    // Check if model format supports the requested quantization
    // This is a simplified implementation - in practice, you'd check model format and capabilities
    
    if (!std::filesystem::exists(modelPath)) {
        return false;
    }
    
    // For now, assume all quantization types are supported for existing models
    // In a real implementation, you'd check model format, architecture, etc.
    switch (quantization) {
        case QuantizationType::NONE:
            return true;
        case QuantizationType::INT8:
        case QuantizationType::INT16:
        case QuantizationType::FP16:
        case QuantizationType::DYNAMIC:
            return true; // Assume supported for now
        default:
            return false;
    }
}

std::vector<QuantizationType> ModelManager::getSupportedQuantizations(const std::string& modelPath) const {
    std::vector<QuantizationType> supported;
    
    if (!std::filesystem::exists(modelPath)) {
        return supported;
    }
    
    // For now, return all quantization types as supported
    // In a real implementation, you'd analyze the model to determine supported quantizations
    supported.push_back(QuantizationType::NONE);
    supported.push_back(QuantizationType::FP16);
    supported.push_back(QuantizationType::INT8);
    supported.push_back(QuantizationType::INT16);
    supported.push_back(QuantizationType::DYNAMIC);
    
    return supported;
}

void ModelManager::setAutoValidation(bool enabled) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    autoValidationEnabled_ = enabled;
    speechrnt::utils::Logger::info("Auto-validation " + std::string(enabled ? "enabled" : "disabled"));
}

void ModelManager::setValidationCallback(std::function<bool(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    validationCallback_ = callback;
    speechrnt::utils::Logger::info("Custom validation callback set");
}

std::string ModelManager::getModelVersion(const std::string& sourceLang, const std::string& targetLang) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::string key = getLanguagePairKey(sourceLang, targetLang);
    auto it = cacheMap_.find(key);
    
    if (it != cacheMap_.end()) {
        return it->second.first->metadata.version;
    }
    
    return "";
}

bool ModelManager::isNewerVersionAvailable(const std::string& sourceLang, const std::string& targetLang,
                                          const std::string& repositoryPath) const {
    // This is a simplified implementation
    // In practice, you'd check a model repository or registry for newer versions
    
    std::string currentVersion = getModelVersion(sourceLang, targetLang);
    if (currentVersion.empty()) {
        return false; // No current version to compare
    }
    
    // Check repository for newer version (simplified)
    std::string versionFile = repositoryPath + "/" + sourceLang + "-" + targetLang + "/version.txt";
    if (std::filesystem::exists(versionFile)) {
        std::ifstream file(versionFile);
        std::string repositoryVersion;
        if (file >> repositoryVersion) {
            // Simple string comparison - in practice, you'd use semantic versioning
            return repositoryVersion > currentVersion;
        }
    }
    
    return false;
}

std::unordered_map<std::string, std::unordered_map<std::string, std::string>> ModelManager::getDetailedStats() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> stats;
    
    for (const auto& pair : cacheMap_) {
        const auto& modelInfo = pair.second.first;
        std::unordered_map<std::string, std::string> modelStats;
        
        modelStats["memory_usage_mb"] = std::to_string(modelInfo->memoryUsage);
        modelStats["gpu_enabled"] = modelInfo->useGPU ? "true" : "false";
        modelStats["gpu_device_id"] = std::to_string(modelInfo->gpuDeviceId);
        modelStats["quantization"] = getQuantizationString(modelInfo->quantization);
        modelStats["version"] = modelInfo->metadata.version;
        modelStats["access_count"] = std::to_string(modelInfo->accessCount);
        modelStats["validated"] = modelInfo->validated ? "true" : "false";
        modelStats["integrity_hash"] = modelInfo->integrityHash;
        
        auto loadedTime = std::chrono::system_clock::to_time_t(modelInfo->loadedAt);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&loadedTime), "%Y-%m-%d %H:%M:%S");
        modelStats["loaded_at"] = ss.str();
        
        stats[pair.first] = modelStats;
    }
    
    return stats;
}

// Private helper methods implementation

bool ModelManager::loadModelDataWithGPU(std::shared_ptr<ModelInfo> modelInfo, bool useGPU, int gpuDeviceId) {
    if (!validateModelFiles(modelInfo->modelPath)) {
        speechrnt::utils::Logger::error("Model validation failed for GPU loading: " + modelInfo->modelPath);
        return false;
    }
    
    try {
        // Initialize and configure GPU if requested
        auto& gpuManager = utils::GPUManager::getInstance();
        if (!gpuManager.initialize()) {
            speechrnt::utils::Logger::warn("GPU manager initialization failed");
            if (useGPU) {
                speechrnt::utils::Logger::warn("Falling back to CPU-only mode");
                modelInfo->useGPU = false;
                modelInfo->gpuDeviceId = -1;
                useGPU = false;
            }
        }
        
        if (useGPU && gpuManager.isCudaAvailable()) {
            // Validate GPU device and memory requirements
            if (gpuDeviceId >= 0) {
                if (!gpuManager.setDevice(gpuDeviceId)) {
                    speechrnt::utils::Logger::warn("Failed to set GPU device " + std::to_string(gpuDeviceId) + 
                                       ", falling back to CPU");
                    modelInfo->useGPU = false;
                    modelInfo->gpuDeviceId = -1;
                    useGPU = false;
                } else {
                    // Check if GPU has sufficient memory
                    if (!isGPUMemorySufficient(modelInfo->memoryUsage, gpuDeviceId)) {
                        speechrnt::utils::Logger::warn("Insufficient GPU memory on device " + std::to_string(gpuDeviceId) + 
                                           ", falling back to CPU");
                        modelInfo->useGPU = false;
                        modelInfo->gpuDeviceId = -1;
                        useGPU = false;
                    }
                }
            } else {
                // Auto-select optimal GPU device
                int optimalDevice = selectOptimalGPUDevice(modelInfo->memoryUsage);
                if (optimalDevice >= 0) {
                    if (gpuManager.setDevice(optimalDevice)) {
                        modelInfo->gpuDeviceId = optimalDevice;
                        speechrnt::utils::Logger::info("Auto-selected GPU device " + std::to_string(optimalDevice) + 
                                           " for model " + modelInfo->languagePair);
                    } else {
                        speechrnt::utils::Logger::warn("Failed to set auto-selected GPU device, falling back to CPU");
                        modelInfo->useGPU = false;
                        modelInfo->gpuDeviceId = -1;
                        useGPU = false;
                    }
                } else {
                    speechrnt::utils::Logger::warn("No suitable GPU device found, falling back to CPU");
                    modelInfo->useGPU = false;
                    modelInfo->gpuDeviceId = -1;
                    useGPU = false;
                }
            }
        } else if (useGPU) {
            speechrnt::utils::Logger::warn("CUDA not available, falling back to CPU");
            modelInfo->useGPU = false;
            modelInfo->gpuDeviceId = -1;
            useGPU = false;
        }
        
        // Enhanced model data structure with GPU support
        struct GPUModelData {
            std::string modelPath;
            std::string languagePair;
            bool isLoaded;
            bool useGPU;
            int gpuDeviceId;
            void* gpuMemoryPtr;
            size_t gpuMemorySize;
            std::chrono::system_clock::time_point loadTime;
            
            GPUModelData(const std::string& path, const std::string& pair, bool gpu, int deviceId) 
                : modelPath(path), languagePair(pair), isLoaded(true), useGPU(gpu), 
                  gpuDeviceId(deviceId), gpuMemoryPtr(nullptr), gpuMemorySize(0),
                  loadTime(std::chrono::system_clock::now()) {}
                  
            ~GPUModelData() {
                if (gpuMemoryPtr && useGPU) {
                    auto& gpuManager = utils::GPUManager::getInstance();
                    gpuManager.freeGPUMemory(gpuMemoryPtr);
                }
            }
        };
        
        // Create enhanced model data
        auto* modelData = new GPUModelData(modelInfo->modelPath, modelInfo->languagePair, 
                                          useGPU, modelInfo->gpuDeviceId);
        
        // Allocate GPU memory if using GPU
        if (useGPU && gpuManager.isCudaAvailable()) {
            size_t memoryBytes = modelInfo->memoryUsage * 1024 * 1024; // Convert MB to bytes
            modelData->gpuMemoryPtr = gpuManager.allocateGPUMemory(memoryBytes, 
                                                                  "model_" + modelInfo->languagePair);
            if (modelData->gpuMemoryPtr) {
                modelData->gpuMemorySize = memoryBytes;
                speechrnt::utils::Logger::info("Allocated " + std::to_string(modelInfo->memoryUsage) + 
                                   "MB GPU memory for model " + modelInfo->languagePair);
            } else {
                speechrnt::utils::Logger::warn("Failed to allocate GPU memory, falling back to CPU");
                modelData->useGPU = false;
                modelInfo->useGPU = false;
                modelInfo->gpuDeviceId = -1;
            }
        }
        
        modelInfo->modelData = static_cast<void*>(modelData);
        modelInfo->loaded = true;
        modelInfo->loadedAt = std::chrono::system_clock::now();
        modelInfo->accessCount = 0;
        
        speechrnt::utils::Logger::info("Successfully loaded model with GPU support: " + modelInfo->languagePair + 
                           " (GPU: " + (modelInfo->useGPU ? "enabled" : "disabled") + ")");
        
        return true;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to load model with GPU support: " + std::string(e.what()));
        return false;
    }
}

bool ModelManager::loadModelDataWithQuantization(std::shared_ptr<ModelInfo> modelInfo, QuantizationType quantization) {
    if (!validateModelFiles(modelInfo->modelPath)) {
        speechrnt::utils::Logger::error("Model validation failed for quantization loading: " + modelInfo->modelPath);
        return false;
    }
    
    // Validate quantization support
    if (quantization != QuantizationType::NONE && !isQuantizationSupported(modelInfo->modelPath, quantization)) {
        speechrnt::utils::Logger::error("Quantization type " + getQuantizationString(quantization) + 
                             " not supported for model: " + modelInfo->modelPath);
        return false;
    }
    
    try {
        // Initialize quantization manager
        auto& quantizationManager = models::QuantizationManager::getInstance();
        if (!quantizationManager.initialize()) {
            speechrnt::utils::Logger::warn("Quantization manager initialization failed");
            if (quantization != QuantizationType::NONE) {
                speechrnt::utils::Logger::warn("Falling back to non-quantized model");
                quantization = QuantizationType::NONE;
                modelInfo->quantization = QuantizationType::NONE;
            }
        }
        
        // Enhanced model data structure with quantization support
        struct QuantizedModelData {
            std::string modelPath;
            std::string languagePair;
            bool isLoaded;
            QuantizationType quantization;
            void* quantizedModelPtr;
            size_t originalSize;
            size_t quantizedSize;
            float compressionRatio;
            std::chrono::system_clock::time_point loadTime;
            
            QuantizedModelData(const std::string& path, const std::string& pair, QuantizationType quant) 
                : modelPath(path), languagePair(pair), isLoaded(true), quantization(quant),
                  quantizedModelPtr(nullptr), originalSize(0), quantizedSize(0), compressionRatio(1.0f),
                  loadTime(std::chrono::system_clock::now()) {}
                  
            ~QuantizedModelData() {
                // Cleanup quantized model data
                if (quantizedModelPtr) {
                    // In real implementation, this would properly clean up quantized model
                    quantizedModelPtr = nullptr;
                }
            }
        };
        
        // Create quantized model data
        auto* modelData = new QuantizedModelData(modelInfo->modelPath, modelInfo->languagePair, quantization);
        
        // Apply quantization if requested
        if (quantization != QuantizationType::NONE) {
            // Get original model size
            modelData->originalSize = estimateModelMemoryUsage(modelInfo->modelPath);
            
            // Apply memory reduction based on quantization type
            float reductionFactor = 1.0f;
            switch (quantization) {
                case QuantizationType::INT8:
                    reductionFactor = 0.25f; // 8-bit vs 32-bit
                    break;
                case QuantizationType::INT16:
                    reductionFactor = 0.5f; // 16-bit vs 32-bit
                    break;
                case QuantizationType::FP16:
                    reductionFactor = 0.5f; // 16-bit vs 32-bit
                    break;
                case QuantizationType::DYNAMIC:
                    reductionFactor = 0.6f; // Variable reduction
                    break;
                default:
                    break;
            }
            
            modelData->quantizedSize = static_cast<size_t>(modelData->originalSize * reductionFactor);
            modelData->compressionRatio = static_cast<float>(modelData->originalSize) / 
                                         static_cast<float>(modelData->quantizedSize);
            
            // Update model info with quantized memory usage
            modelInfo->memoryUsage = modelData->quantizedSize;
            
            speechrnt::utils::Logger::info("Applied " + getQuantizationString(quantization) + " quantization: " +
                               std::to_string(modelData->compressionRatio) + "x compression ratio");
        } else {
            modelData->originalSize = modelInfo->memoryUsage;
            modelData->quantizedSize = modelInfo->memoryUsage;
            modelData->compressionRatio = 1.0f;
        }
        
        // Simulate quantized model loading
        // In real implementation, this would load the actual quantized model
        modelData->quantizedModelPtr = reinterpret_cast<void*>(0x2); // Dummy pointer for quantized model
        
        modelInfo->modelData = static_cast<void*>(modelData);
        modelInfo->loaded = true;
        modelInfo->loadedAt = std::chrono::system_clock::now();
        modelInfo->accessCount = 0;
        
        speechrnt::utils::Logger::info("Successfully loaded model with quantization: " + modelInfo->languagePair + 
                           " (" + getQuantizationString(quantization) + ")");
        
        return true;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to load model with quantization: " + std::string(e.what()));
        return false;
    }
}

bool ModelManager::loadModelDataAdvanced(std::shared_ptr<ModelInfo> modelInfo, bool useGPU, 
                                        int gpuDeviceId, QuantizationType quantization) {
    if (!validateModelFiles(modelInfo->modelPath)) {
        speechrnt::utils::Logger::error("Model validation failed for advanced loading: " + modelInfo->modelPath);
        return false;
    }
    
    // Validate quantization support if requested
    if (quantization != QuantizationType::NONE && !isQuantizationSupported(modelInfo->modelPath, quantization)) {
        speechrnt::utils::Logger::error("Quantization type " + getQuantizationString(quantization) + 
                             " not supported for model: " + modelInfo->modelPath);
        return false;
    }
    
    try {
        // Initialize managers
        auto& gpuManager = utils::GPUManager::getInstance();
        auto& quantizationManager = models::QuantizationManager::getInstance();
        
        if (!gpuManager.initialize()) {
            speechrnt::utils::Logger::warn("GPU manager initialization failed");
            if (useGPU) {
                speechrnt::utils::Logger::warn("Falling back to CPU-only mode");
                modelInfo->useGPU = false;
                modelInfo->gpuDeviceId = -1;
                useGPU = false;
            }
        }
        
        if (!quantizationManager.initialize()) {
            speechrnt::utils::Logger::warn("Quantization manager initialization failed");
            if (quantization != QuantizationType::NONE) {
                speechrnt::utils::Logger::warn("Falling back to non-quantized model");
                quantization = QuantizationType::NONE;
                modelInfo->quantization = QuantizationType::NONE;
            }
        }
        
        // Handle GPU configuration
        if (useGPU && gpuManager.isCudaAvailable()) {
            if (gpuDeviceId >= 0) {
                if (!gpuManager.setDevice(gpuDeviceId)) {
                    speechrnt::utils::Logger::warn("Failed to set GPU device " + std::to_string(gpuDeviceId) + 
                                       ", falling back to CPU");
                    modelInfo->useGPU = false;
                    modelInfo->gpuDeviceId = -1;
                    useGPU = false;
                } else {
                    // Check GPU memory after applying quantization memory reduction
                    size_t requiredMemory = modelInfo->memoryUsage;
                    if (quantization != QuantizationType::NONE) {
                        float reductionFactor = 1.0f;
                        switch (quantization) {
                            case QuantizationType::INT8: reductionFactor = 0.25f; break;
                            case QuantizationType::INT16: reductionFactor = 0.5f; break;
                            case QuantizationType::FP16: reductionFactor = 0.5f; break;
                            case QuantizationType::DYNAMIC: reductionFactor = 0.6f; break;
                            default: break;
                        }
                        requiredMemory = static_cast<size_t>(requiredMemory * reductionFactor);
                    }
                    
                    if (!isGPUMemorySufficient(requiredMemory, gpuDeviceId)) {
                        speechrnt::utils::Logger::warn("Insufficient GPU memory on device " + std::to_string(gpuDeviceId) + 
                                           ", falling back to CPU");
                        modelInfo->useGPU = false;
                        modelInfo->gpuDeviceId = -1;
                        useGPU = false;
                    }
                }
            } else {
                // Auto-select optimal GPU device considering quantization
                size_t requiredMemory = modelInfo->memoryUsage;
                if (quantization != QuantizationType::NONE) {
                    float reductionFactor = 1.0f;
                    switch (quantization) {
                        case QuantizationType::INT8: reductionFactor = 0.25f; break;
                        case QuantizationType::INT16: reductionFactor = 0.5f; break;
                        case QuantizationType::FP16: reductionFactor = 0.5f; break;
                        case QuantizationType::DYNAMIC: reductionFactor = 0.6f; break;
                        default: break;
                    }
                    requiredMemory = static_cast<size_t>(requiredMemory * reductionFactor);
                }
                
                int optimalDevice = selectOptimalGPUDevice(requiredMemory);
                if (optimalDevice >= 0) {
                    if (gpuManager.setDevice(optimalDevice)) {
                        modelInfo->gpuDeviceId = optimalDevice;
                        speechrnt::utils::Logger::info("Auto-selected GPU device " + std::to_string(optimalDevice) + 
                                           " for advanced model loading");
                    } else {
                        speechrnt::utils::Logger::warn("Failed to set auto-selected GPU device, falling back to CPU");
                        modelInfo->useGPU = false;
                        modelInfo->gpuDeviceId = -1;
                        useGPU = false;
                    }
                } else {
                    speechrnt::utils::Logger::warn("No suitable GPU device found, falling back to CPU");
                    modelInfo->useGPU = false;
                    modelInfo->gpuDeviceId = -1;
                    useGPU = false;
                }
            }
        } else if (useGPU) {
            speechrnt::utils::Logger::warn("CUDA not available, falling back to CPU");
            modelInfo->useGPU = false;
            modelInfo->gpuDeviceId = -1;
            useGPU = false;
        }
        
        // Advanced model data structure combining GPU and quantization support
        struct AdvancedModelData {
            std::string modelPath;
            std::string languagePair;
            bool isLoaded;
            bool useGPU;
            int gpuDeviceId;
            QuantizationType quantization;
            void* modelPtr;
            void* gpuMemoryPtr;
            size_t gpuMemorySize;
            size_t originalSize;
            size_t finalSize;
            float compressionRatio;
            std::chrono::system_clock::time_point loadTime;
            
            AdvancedModelData(const std::string& path, const std::string& pair, bool gpu, 
                            int deviceId, QuantizationType quant) 
                : modelPath(path), languagePair(pair), isLoaded(true), useGPU(gpu), 
                  gpuDeviceId(deviceId), quantization(quant), modelPtr(nullptr),
                  gpuMemoryPtr(nullptr), gpuMemorySize(0), originalSize(0), finalSize(0),
                  compressionRatio(1.0f), loadTime(std::chrono::system_clock::now()) {}
                  
            ~AdvancedModelData() {
                if (gpuMemoryPtr && useGPU) {
                    auto& gpuManager = utils::GPUManager::getInstance();
                    gpuManager.freeGPUMemory(gpuMemoryPtr);
                }
                if (modelPtr) {
                    // Cleanup model data
                    modelPtr = nullptr;
                }
            }
        };
        
        // Create advanced model data
        auto* modelData = new AdvancedModelData(modelInfo->modelPath, modelInfo->languagePair, 
                                              useGPU, modelInfo->gpuDeviceId, quantization);
        
        // Calculate memory requirements with quantization
        modelData->originalSize = modelInfo->memoryUsage;
        if (quantization != QuantizationType::NONE) {
            float reductionFactor = 1.0f;
            switch (quantization) {
                case QuantizationType::INT8: reductionFactor = 0.25f; break;
                case QuantizationType::INT16: reductionFactor = 0.5f; break;
                case QuantizationType::FP16: reductionFactor = 0.5f; break;
                case QuantizationType::DYNAMIC: reductionFactor = 0.6f; break;
                default: break;
            }
            modelData->finalSize = static_cast<size_t>(modelData->originalSize * reductionFactor);
            modelData->compressionRatio = static_cast<float>(modelData->originalSize) / 
                                         static_cast<float>(modelData->finalSize);
            
            // Update model info with final memory usage
            modelInfo->memoryUsage = modelData->finalSize;
        } else {
            modelData->finalSize = modelData->originalSize;
            modelData->compressionRatio = 1.0f;
        }
        
        // Allocate GPU memory if using GPU
        if (useGPU && gpuManager.isCudaAvailable()) {
            size_t memoryBytes = modelData->finalSize * 1024 * 1024; // Convert MB to bytes
            modelData->gpuMemoryPtr = gpuManager.allocateGPUMemory(memoryBytes, 
                                                                  "advanced_model_" + modelInfo->languagePair);
            if (modelData->gpuMemoryPtr) {
                modelData->gpuMemorySize = memoryBytes;
                speechrnt::utils::Logger::info("Allocated " + std::to_string(modelData->finalSize) + 
                                   "MB GPU memory for advanced model " + modelInfo->languagePair);
            } else {
                speechrnt::utils::Logger::warn("Failed to allocate GPU memory, falling back to CPU");
                modelData->useGPU = false;
                modelInfo->useGPU = false;
                modelInfo->gpuDeviceId = -1;
            }
        }
        
        // Simulate advanced model loading
        // In real implementation, this would load the actual model with all optimizations
        modelData->modelPtr = reinterpret_cast<void*>(0x3); // Dummy pointer for advanced model
        
        modelInfo->modelData = static_cast<void*>(modelData);
        modelInfo->loaded = true;
        modelInfo->loadedAt = std::chrono::system_clock::now();
        modelInfo->accessCount = 0;
        
        speechrnt::utils::Logger::info("Successfully loaded advanced model: " + modelInfo->languagePair + 
                           " (GPU: " + (modelInfo->useGPU ? "enabled" : "disabled") + 
                           ", Quantization: " + getQuantizationString(quantization) + 
                           ", Compression: " + std::to_string(modelData->compressionRatio) + "x)");
        
        return true;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to load advanced model: " + std::string(e.what()));
        return false;
    }
}

std::string ModelManager::calculateModelHash(const std::string& modelPath) const {
    // Calculate SHA-256 hash of all model files
    std::stringstream hashStream;
    
    try {
        if (!std::filesystem::exists(modelPath)) {
            return "";
        }
        
        // Collect all file hashes
        std::vector<std::string> fileHashes;
        
        for (const auto& entry : std::filesystem::recursive_directory_iterator(modelPath)) {
            if (entry.is_regular_file()) {
                std::ifstream file(entry.path(), std::ios::binary);
                if (!file) continue;
                
                // Simple hash calculation (in practice, use proper SHA-256)
                std::string content((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
                
                std::hash<std::string> hasher;
                size_t fileHash = hasher(content);
                fileHashes.push_back(std::to_string(fileHash));
            }
        }
        
        // Combine all file hashes
        std::sort(fileHashes.begin(), fileHashes.end());
        std::string combinedHashes;
        for (const auto& hash : fileHashes) {
            combinedHashes += hash;
        }
        
        // Calculate final hash
        std::hash<std::string> finalHasher;
        size_t finalHash = finalHasher(combinedHashes);
        
        hashStream << std::hex << finalHash;
        return hashStream.str();
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::warn("Failed to calculate model hash: " + std::string(e.what()));
        return "";
    }
}

bool ModelManager::validateModelFiles(const std::string& modelPath) const {
    if (!std::filesystem::exists(modelPath)) {
        speechrnt::utils::Logger::error("Model path does not exist: " + modelPath);
        return false;
    }
    
    if (!std::filesystem::is_directory(modelPath)) {
        speechrnt::utils::Logger::error("Model path is not a directory: " + modelPath);
        return false;
    }
    
    // Check for essential model files (simplified check)
    std::vector<std::string> requiredFiles = {"model.bin", "vocab.yml"};
    
    for (const auto& requiredFile : requiredFiles) {
        std::string filePath = modelPath + "/" + requiredFile;
        if (!std::filesystem::exists(filePath)) {
            // Try alternative extensions
            std::vector<std::string> alternatives = {".npz", ".pt", ".onnx"};
            bool found = false;
            
            for (const auto& ext : alternatives) {
                if (std::filesystem::exists(modelPath + "/model" + ext)) {
                    found = true;
                    break;
                }
            }
            
            if (!found && requiredFile == "model.bin") {
                speechrnt::utils::Logger::error("Required model file not found: " + requiredFile);
                return false;
            }
        }
    }
    
    return true;
}

ModelMetadata ModelManager::loadModelMetadata(const std::string& modelPath) const {
    ModelMetadata metadata;
    
    std::string metadataPath = modelPath + "/metadata.json";
    if (!std::filesystem::exists(metadataPath)) {
        // Create default metadata
        metadata.version = "1.0.0";
        metadata.architecture = "transformer";
        metadata.createdAt = std::chrono::system_clock::now();
        metadata.lastModified = std::chrono::system_clock::now();
        return metadata;
    }
    
    try {
        std::ifstream file(metadataPath);
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        
        // Simple JSON parsing (in practice, use a proper JSON library)
        // For now, just extract basic fields
        if (content.find("\"version\"") != std::string::npos) {
            size_t start = content.find("\"version\"") + 10;
            size_t end = content.find("\"", start + 1);
            if (end != std::string::npos) {
                metadata.version = content.substr(start + 1, end - start - 1);
            }
        }
        
        if (content.find("\"checksum\"") != std::string::npos) {
            size_t start = content.find("\"checksum\"") + 11;
            size_t end = content.find("\"", start + 1);
            if (end != std::string::npos) {
                metadata.checksum = content.substr(start + 1, end - start - 1);
            }
        }
        
        metadata.lastModified = std::chrono::system_clock::now();
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::warn("Failed to load metadata: " + std::string(e.what()));
    }
    
    return metadata;
}

bool ModelManager::saveModelMetadata(const std::string& modelPath, const ModelMetadata& metadata) const {
    std::string metadataPath = modelPath + "/metadata.json";
    
    try {
        std::ofstream file(metadataPath);
        if (!file) {
            return false;
        }
        
        // Simple JSON generation (in practice, use a proper JSON library)
        file << "{\n";
        file << "  \"version\": \"" << metadata.version << "\",\n";
        file << "  \"checksum\": \"" << metadata.checksum << "\",\n";
        file << "  \"architecture\": \"" << metadata.architecture << "\",\n";
        file << "  \"sourceLanguage\": \"" << metadata.sourceLanguage << "\",\n";
        file << "  \"targetLanguage\": \"" << metadata.targetLanguage << "\",\n";
        file << "  \"parameterCount\": " << metadata.parameterCount << "\n";
        file << "}\n";
        
        return true;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::warn("Failed to save metadata: " + std::string(e.what()));
        return false;
    }
}

std::string ModelManager::getQuantizationString(QuantizationType quantization) const {
    switch (quantization) {
        case QuantizationType::NONE:
            return "none";
        case QuantizationType::INT8:
            return "int8";
        case QuantizationType::INT16:
            return "int16";
        case QuantizationType::FP16:
            return "fp16";
        case QuantizationType::DYNAMIC:
            return "dynamic";
        default:
            return "unknown";
    }
}

QuantizationType ModelManager::parseQuantizationType(const std::string& quantizationStr) const {
    if (quantizationStr == "none") return QuantizationType::NONE;
    if (quantizationStr == "int8") return QuantizationType::INT8;
    if (quantizationStr == "int16") return QuantizationType::INT16;
    if (quantizationStr == "fp16") return QuantizationType::FP16;
    if (quantizationStr == "dynamic") return QuantizationType::DYNAMIC;
    return QuantizationType::NONE;
}

bool ModelManager::isGPUMemorySufficient(size_t requiredMemoryMB, int gpuDeviceId) const {
    auto& gpuManager = utils::GPUManager::getInstance();
    
    if (!gpuManager.isCudaAvailable()) {
        return false;
    }
    
    if (gpuDeviceId >= 0) {
        auto deviceInfo = gpuManager.getDeviceInfo(gpuDeviceId);
        return deviceInfo.freeMemoryMB >= requiredMemoryMB;
    }
    
    return gpuManager.hasSufficientMemory(requiredMemoryMB);
}

int ModelManager::selectOptimalGPUDevice(size_t requiredMemoryMB) const {
    auto& gpuManager = utils::GPUManager::getInstance();
    
    if (!gpuManager.isCudaAvailable()) {
        return -1;
    }
    
    auto devices = gpuManager.getAllDeviceInfo();
    int bestDevice = -1;
    size_t maxFreeMemory = 0;
    
    for (const auto& device : devices) {
        if (device.isAvailable && device.freeMemoryMB >= requiredMemoryMB) {
            if (device.freeMemoryMB > maxFreeMemory) {
                bestDevice = device.deviceId;
                maxFreeMemory = device.freeMemoryMB;
            }
        }
    }
    
    return bestDevice;
}

} // namespace models
} // namespace speechrnt