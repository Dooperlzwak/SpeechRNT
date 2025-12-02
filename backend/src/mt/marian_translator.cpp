#include "mt/marian_translator.hpp"
#include "mt/marian_error_handler.hpp"
#include "mt/quality_manager.hpp"
#include "mt/mt_config.hpp"
#include "models/model_manager.hpp"
#include "utils/logging.hpp"
#include "utils/config.hpp"
#include "utils/gpu_manager.hpp"
#include "utils/gpu_memory_pool.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <future>
#include <chrono>
#include <unordered_map>
#include <thread>

#ifdef MARIAN_AVAILABLE
#include "marian.h"
#include "translator/translator.h"
#include "translator/output_collector.h"
#include "translator/output_printer.h"
#include "common/options.h"
#include "common/timer.h"
#endif

namespace speechrnt {
namespace mt {

MarianTranslator::MarianTranslator() 
    : MarianTranslator(MTConfigManager::getInstance().getConfig()) {
}

MarianTranslator::MarianTranslator(std::shared_ptr<const MTConfig> config)
    : config_(config)
    , modelsPath_(config ? config->getModelsBasePath() : "data/marian/")
    , initialized_(false)
    , modelManager_(std::make_unique<models::ModelManager>(4096, 10))
    , gpuAccelerationEnabled_(config ? config->getGPUConfig().enabled : false)
    , defaultGpuDeviceId_(config ? config->getGPUConfig().defaultDeviceId : 0)
    , gpuInitialized_(false)
    , gpuManager_(&utils::GPUManager::getInstance())
    , qualityManager_(std::make_unique<QualityManager>())
    , errorHandler_(std::make_unique<MarianErrorHandler>())
    , maxBatchSize_(config ? config->getBatchConfig().maxBatchSize : 32)
    , sessionTimeout_(config ? config->getStreamingConfig().sessionTimeout : std::chrono::minutes(30))
    , cachingEnabled_(config ? config->getCachingConfig().enabled : true)
    , maxCacheSize_(config ? config->getCachingConfig().maxCacheSize : 1000)
    , maxConcurrentModels_(5) {
    
    initializeSupportedLanguages();
    initializeLanguagePairMappings();
    
    // Initialize quality manager
    if (qualityManager_->initialize()) {
        speechrnt::utils::Logger::info("QualityManager initialized successfully");
    } else {
        speechrnt::utils::Logger::warn("Failed to initialize QualityManager, quality assessment will be limited");
    }
    
    // Initialize error handler
    if (errorHandler_->initialize()) {
        speechrnt::utils::Logger::info("MarianErrorHandler initialized successfully");
    } else {
        speechrnt::utils::Logger::warn("Failed to initialize MarianErrorHandler, using basic error handling");
    }
    
    // Initialize GPU resources if available
    if (initializeGPUResources()) {
        speechrnt::utils::Logger::info("GPU resources initialized for MarianTranslator");
    } else {
        speechrnt::utils::Logger::info("GPU resources not available, using CPU-only mode");
    }
}

MarianTranslator::~MarianTranslator() {
    cleanup();
    cleanupGPUResources();
}

bool MarianTranslator::initialize(const std::string& sourceLang, const std::string& targetLang) {
    std::lock_guard<std::mutex> lock(translationMutex_);
    
    // Reset state first
    initialized_ = false;
    currentSourceLang_.clear();
    currentTargetLang_.clear();
    
    if (!supportsLanguagePair(sourceLang, targetLang)) {
        speechrnt::utils::Logger::error("Unsupported language pair: " + sourceLang + " -> " + targetLang);
        return false;
    }
    
    // Load the model for this language pair
    if (!loadModel(sourceLang, targetLang)) {
        std::string modelPath = getModelPath(sourceLang, targetLang);
        std::string errorMsg = MarianErrorHandler::handleModelLoadingError(
            "Model loading failed", modelPath);
        speechrnt::utils::Logger::error(errorMsg);
        return false;
    }
    
    currentSourceLang_ = sourceLang;
    currentTargetLang_ = targetLang;
    initialized_ = true;
    
    speechrnt::utils::Logger::info("MarianTranslator initialized for " + sourceLang + " -> " + targetLang);
    return true;
}

bool MarianTranslator::initializeWithGPU(const std::string& sourceLang, const std::string& targetLang, int gpuDeviceId) {
    speechrnt::utils::Logger::info("Initializing MarianTranslator with GPU acceleration (device " + std::to_string(gpuDeviceId) + ")");
    
    // Validate GPU device first
    if (!validateGPUDevice(gpuDeviceId)) {
        std::string error = "GPU device " + std::to_string(gpuDeviceId) + " is not available or invalid";
        speechrnt::utils::Logger::error(error);
        gpuInitializationError_ = error;
        
        // Fall back to CPU
        return fallbackToCPU("Invalid GPU device");
    }
    
    // Set GPU configuration
    gpuAccelerationEnabled_ = true;
    defaultGpuDeviceId_ = gpuDeviceId;
    
    // Set the GPU device
    if (!gpuManager_->setDevice(gpuDeviceId)) {
        std::string error = "Failed to set GPU device " + std::to_string(gpuDeviceId);
        speechrnt::utils::Logger::error(error);
        gpuInitializationError_ = error;
        
        // Fall back to CPU
        return fallbackToCPU("Failed to set GPU device");
    }
    
    // Estimate memory requirements for the model
    size_t requiredMemoryMB = estimateModelMemoryRequirement(sourceLang, targetLang);
    
    // Check if sufficient GPU memory is available
    if (!hasSufficientGPUMemory(requiredMemoryMB)) {
        std::string error = "Insufficient GPU memory. Required: " + std::to_string(requiredMemoryMB) + 
                           "MB, Available: " + std::to_string(gpuManager_->getFreeMemoryMB()) + "MB";
        speechrnt::utils::Logger::error(error);
        gpuInitializationError_ = error;
        
        // Fall back to CPU
        return fallbackToCPU("Insufficient GPU memory");
    }
    
    // Initialize with GPU acceleration
    bool result = initialize(sourceLang, targetLang);
    
    if (result) {
        speechrnt::utils::Logger::info("Successfully initialized MarianTranslator with GPU acceleration");
    } else {
        speechrnt::utils::Logger::error("Failed to initialize with GPU, falling back to CPU");
        return fallbackToCPU("GPU initialization failed");
    }
    
    return result;
}

void MarianTranslator::setGPUAcceleration(bool enabled, int deviceId) {
    std::lock_guard<std::mutex> lock(translationMutex_);
    
    if (enabled) {
        // Validate GPU device before enabling
        if (!validateGPUDevice(deviceId)) {
            speechrnt::utils::Logger::error("Cannot enable GPU acceleration: device " + std::to_string(deviceId) + " is not valid");
            return;
        }
        
        // Set the GPU device
        if (!gpuManager_->setDevice(deviceId)) {
            speechrnt::utils::Logger::error("Cannot enable GPU acceleration: failed to set device " + std::to_string(deviceId));
            return;
        }
        
        gpuAccelerationEnabled_ = true;
        defaultGpuDeviceId_ = deviceId;
        speechrnt::utils::Logger::info("GPU acceleration enabled for device " + std::to_string(deviceId));
        
    } else {
        gpuAccelerationEnabled_ = false;
        speechrnt::utils::Logger::info("GPU acceleration disabled");
        
        // Free any GPU memory allocations for existing models
        for (auto& pair : modelInfoMap_) {
            if (pair.second.gpuMemoryPtr) {
                freeGPUMemoryForModel(pair.first.substr(0, 2), pair.first.substr(5, 2));
            }
        }
    }
}

bool MarianTranslator::isGPUAccelerationEnabled() const {
    std::lock_guard<std::mutex> lock(translationMutex_);
    return gpuAccelerationEnabled_ && gpuInitialized_;
}

int MarianTranslator::getCurrentGPUDevice() const {
    std::lock_guard<std::mutex> lock(translationMutex_);
    return gpuAccelerationEnabled_ ? defaultGpuDeviceId_ : -1;
}

bool MarianTranslator::validateGPUDevice(int deviceId) const {
    if (!gpuManager_->isCudaAvailable()) {
        return false;
    }
    
    if (deviceId < 0 || deviceId >= gpuManager_->getDeviceCount()) {
        return false;
    }
    
    auto deviceInfo = gpuManager_->getDeviceInfo(deviceId);
    return deviceInfo.isAvailable;
}

size_t MarianTranslator::getGPUMemoryUsageMB() const {
    std::lock_guard<std::mutex> lock(translationMutex_);
    
    if (!gpuAccelerationEnabled_ || !gpuInitialized_) {
        return 0;
    }
    
    size_t totalUsage = 0;
    for (const auto& pair : modelInfoMap_) {
        totalUsage += pair.second.gpuMemorySizeMB;
    }
    
    return totalUsage;
}

bool MarianTranslator::hasSufficientGPUMemory(size_t requiredMB) const {
    if (!gpuManager_->isCudaAvailable()) {
        return false;
    }
    
    return gpuManager_->hasSufficientMemory(requiredMB);
}

TranslationResult MarianTranslator::translate(const std::string& text) {
    if (!isReady()) {
        TranslationResult result;
        result.success = false;
        result.errorMessage = "Translator not initialized";
        return result;
    }
    
    if (text.empty()) {
        TranslationResult result;
        result.success = false;
        result.errorMessage = "Empty input text";
        return result;
    }
    
    // Check cache first
    std::string cacheKey = generateCacheKey(text, currentSourceLang_, currentTargetLang_);
    TranslationResult cachedResult;
    
    if (getCachedTranslation(cacheKey, cachedResult)) {
        cachedResult.sourceLang = currentSourceLang_;
        cachedResult.targetLang = currentTargetLang_;
        return cachedResult;
    }
    
    // Perform translation
    TranslationResult result = performTranslation(text, currentSourceLang_, currentTargetLang_);
    
    // Cache successful results
    if (result.success) {
        cacheTranslation(cacheKey, result);
    }
    
    return result;
}

std::future<TranslationResult> MarianTranslator::translateAsync(const std::string& text) {
    return std::async(std::launch::async, [this, text]() {
        return translate(text);
    });
}

bool MarianTranslator::supportsLanguagePair(const std::string& sourceLang, const std::string& targetLang) const {
    // Check if source language is supported
    auto sourceIt = std::find(supportedSourceLanguages_.begin(), supportedSourceLanguages_.end(), sourceLang);
    if (sourceIt == supportedSourceLanguages_.end()) {
        return false;
    }
    
    // Check if target language is supported for this source
    auto targetIt = supportedTargetLanguages_.find(sourceLang);
    if (targetIt == supportedTargetLanguages_.end()) {
        return false;
    }
    
    const auto& targets = targetIt->second;
    return std::find(targets.begin(), targets.end(), targetLang) != targets.end();
}

std::vector<std::string> MarianTranslator::getSupportedSourceLanguages() const {
    return supportedSourceLanguages_;
}

std::vector<std::string> MarianTranslator::getSupportedTargetLanguages(const std::string& sourceLang) const {
    auto it = supportedTargetLanguages_.find(sourceLang);
    if (it != supportedTargetLanguages_.end()) {
        return it->second;
    }
    return {};
}

bool MarianTranslator::isReady() const {
    return initialized_ && isModelLoaded(currentSourceLang_, currentTargetLang_);
}

void MarianTranslator::cleanup() {
    std::lock_guard<std::mutex> lock(modelsMutex_);
    
    // Cleanup quality manager
    if (qualityManager_) {
        qualityManager_->cleanup();
    }
    
    // Free all GPU memory allocations
    for (auto& pair : modelInfoMap_) {
        if (pair.second.gpuMemoryPtr) {
            freeGPUMemoryForModel(pair.first.substr(0, 2), pair.first.substr(5, 2));
        }
    }
    modelInfoMap_.clear();
    
    // Clear all models from ModelManager
    if (modelManager_) {
        modelManager_->clearAll();
    }
    
    initialized_ = false;
    
    speechrnt::utils::Logger::info("MarianTranslator cleaned up");
}

void MarianTranslator::setModelsPath(const std::string& modelsPath) {
    modelsPath_ = modelsPath;
    if (!modelsPath_.empty() && modelsPath_.back() != '/') {
        modelsPath_ += '/';
    }
}

bool MarianTranslator::updateConfiguration(std::shared_ptr<const MTConfig> config) {
    if (!config) {
        speechrnt::utils::Logger::error("Cannot update MarianTranslator with null configuration");
        return false;
    }
    
    std::lock_guard<std::mutex> configLock(configMutex_);
    std::lock_guard<std::mutex> translationLock(translationMutex_);
    
    try {
        // Store old configuration for rollback if needed
        auto oldConfig = config_;
        config_ = config;
        
        // Update configuration-dependent settings
        modelsPath_ = config_->getModelsBasePath();
        
        // Update GPU configuration
        const auto& gpuConfig = config_->getGPUConfig();
        bool oldGpuEnabled = gpuAccelerationEnabled_;
        int oldGpuDevice = defaultGpuDeviceId_;
        
        gpuAccelerationEnabled_ = gpuConfig.enabled;
        defaultGpuDeviceId_ = gpuConfig.defaultDeviceId;
        
        // If GPU settings changed, reinitialize GPU resources
        if (oldGpuEnabled != gpuAccelerationEnabled_ || oldGpuDevice != defaultGpuDeviceId_) {
            if (gpuAccelerationEnabled_) {
                if (!initializeGPUResources()) {
                    speechrnt::utils::Logger::warning("Failed to initialize GPU resources with new configuration");
                    if (!gpuConfig.fallbackToCPU) {
                        // Rollback configuration if fallback is not allowed
                        config_ = oldConfig;
                        gpuAccelerationEnabled_ = oldGpuEnabled;
                        defaultGpuDeviceId_ = oldGpuDevice;
                        return false;
                    }
                }
            } else {
                cleanupGPUResources();
            }
        }
        
        // Update batch processing configuration
        const auto& batchConfig = config_->getBatchConfig();
        maxBatchSize_ = batchConfig.maxBatchSize;
        
        // Update streaming configuration
        const auto& streamingConfig = config_->getStreamingConfig();
        sessionTimeout_ = streamingConfig.sessionTimeout;
        
        // Update caching configuration
        const auto& cachingConfig = config_->getCachingConfig();
        bool oldCachingEnabled = cachingEnabled_;
        size_t oldMaxCacheSize = maxCacheSize_;
        
        cachingEnabled_ = cachingConfig.enabled;
        maxCacheSize_ = cachingConfig.maxCacheSize;
        
        // If caching was disabled, clear the cache
        if (oldCachingEnabled && !cachingEnabled_) {
            clearTranslationCache();
        }
        // If cache size was reduced, trim the cache
        else if (oldMaxCacheSize > maxCacheSize_) {
            // Trim cache to new size (implementation would go here)
            while (translationCache_.size() > maxCacheSize_) {
                evictOldestCacheEntries();
            }
        }
        
        // Update quality manager configuration
        if (qualityManager_) {
            const auto& qualityConfig = config_->getQualityConfig();
            qualityManager_->setQualityThresholds(
                qualityConfig.highQualityThreshold,
                qualityConfig.mediumQualityThreshold,
                qualityConfig.lowQualityThreshold
            );
            qualityManager_->setGenerateAlternatives(qualityConfig.generateAlternatives);
            qualityManager_->setMaxAlternatives(qualityConfig.maxAlternatives);
        }
        
        // Update error handler configuration
        if (errorHandler_) {
            const auto& errorConfig = config_->getErrorHandlingConfig();
            errorHandler_->setRetryConfiguration(
                errorConfig.enableRetry,
                errorConfig.maxRetryAttempts,
                errorConfig.initialRetryDelay,
                errorConfig.retryBackoffMultiplier,
                errorConfig.maxRetryDelay
            );
            errorHandler_->setTranslationTimeout(errorConfig.translationTimeout);
            errorHandler_->setDegradedModeEnabled(errorConfig.enableDegradedMode);
        }
        
        speechrnt::utils::Logger::info("MarianTranslator configuration updated successfully");
        return true;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to update MarianTranslator configuration: " + std::string(e.what()));
        return false;
    }
}

std::shared_ptr<const MTConfig> MarianTranslator::getConfiguration() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return config_;
}

bool MarianTranslator::loadModel(const std::string& sourceLang, const std::string& targetLang) {
    std::lock_guard<std::mutex> lock(modelsMutex_);
    
    std::string modelPath = getModelPath(sourceLang, targetLang);
    
    // Validate model files exist and check for corruption
    if (!validateModelFiles(sourceLang, targetLang)) {
        speechrnt::utils::Logger::error("Model files not found for language pair: " + sourceLang + " -> " + targetLang);
        return false;
    }
    
    // Check for model corruption and attempt recovery if needed
    if (errorHandler_) {
        ErrorContext errorContext;
        errorContext.component = "MarianTranslator";
        errorContext.operation = "loadModel";
        errorContext.languagePair = sourceLang + "->" + targetLang;
        errorContext.modelPath = modelPath;
        
        RecoveryResult corruptionCheck = errorHandler_->checkAndHandleModelCorruption(modelPath, errorContext);
        
        if (!corruptionCheck.successful && corruptionCheck.requiresUserIntervention) {
            speechrnt::utils::Logger::error("Model corruption detected and recovery failed: " + modelPath);
            return false;
        }
    }
    
    // If GPU acceleration is enabled, try to load model to GPU
    if (gpuAccelerationEnabled_ && gpuInitialized_) {
        size_t requiredMemoryMB = estimateModelMemoryRequirement(sourceLang, targetLang);
        
        // Check if sufficient GPU memory is available
        if (hasSufficientGPUMemory(requiredMemoryMB)) {
            // Allocate GPU memory for the model
            if (allocateGPUMemoryForModel(sourceLang, targetLang, requiredMemoryMB)) {
                // Load model to GPU
                if (loadModelToGPU(sourceLang, targetLang)) {
                    speechrnt::utils::Logger::info("Loaded model for language pair " + sourceLang + " -> " + targetLang + " to GPU");
                } else {
                    speechrnt::utils::Logger::warning("Failed to load model to GPU, falling back to CPU");
                    freeGPUMemoryForModel(sourceLang, targetLang);
                }
            } else {
                speechrnt::utils::Logger::warning("Failed to allocate GPU memory for model, using CPU");
            }
        } else {
            speechrnt::utils::Logger::warning("Insufficient GPU memory (" + std::to_string(requiredMemoryMB) + 
                                 "MB required), using CPU for model " + sourceLang + " -> " + targetLang);
        }
    }
    
    // Initialize Marian model (CPU or GPU depending on configuration)
    if (!initializeMarianModel(sourceLang, targetLang)) {
        speechrnt::utils::Logger::warning("Failed to initialize Marian model, using fallback");
    }
    
    // Use ModelManager to load the model
    bool success = modelManager_->loadModel(sourceLang, targetLang, modelPath);
    
    if (success) {
        std::string deviceInfo = gpuAccelerationEnabled_ && gpuInitialized_ ? 
                                " (GPU device " + std::to_string(defaultGpuDeviceId_) + ")" : " (CPU)";
        speechrnt::utils::Logger::info("Loaded model for language pair: " + sourceLang + " -> " + targetLang + deviceInfo);
    }
    
    return success;
}

void MarianTranslator::unloadModel(const std::string& sourceLang, const std::string& targetLang) {
    std::lock_guard<std::mutex> lock(modelsMutex_);
    
    // Free GPU memory if allocated
    freeGPUMemoryForModel(sourceLang, targetLang);
    
    // Cleanup Marian model
    cleanupMarianModel(sourceLang, targetLang);
    
    bool success = modelManager_->unloadModel(sourceLang, targetLang);
    
    if (success) {
        speechrnt::utils::Logger::info("Unloaded model for language pair: " + sourceLang + " -> " + targetLang);
    }
}

bool MarianTranslator::isModelLoaded(const std::string& sourceLang, const std::string& targetLang) const {
    std::lock_guard<std::mutex> lock(modelsMutex_);
    
    return modelManager_->isModelLoaded(sourceLang, targetLang);
}

std::string MarianTranslator::getLanguagePairKey(const std::string& sourceLang, const std::string& targetLang) const {
    return sourceLang + "->" + targetLang;
}

std::string MarianTranslator::getModelPath(const std::string& sourceLang, const std::string& targetLang) const {
    return modelsPath_ + sourceLang + "-" + targetLang;
}

bool MarianTranslator::validateModelFiles(const std::string& sourceLang, const std::string& targetLang) const {
    std::string modelDir = getModelPath(sourceLang, targetLang);
    
    // Check if model directory exists
    if (!std::filesystem::exists(modelDir)) {
        speechrnt::utils::Logger::debug("Model directory does not exist: " + modelDir);
        return false;
    }
    
    // Check for required model files
    std::vector<std::string> requiredFiles = {"model.npz", "vocab.yml", "config.yml"};
    
    for (const auto& file : requiredFiles) {
        std::string filePath = modelDir + "/" + file;
        if (!std::filesystem::exists(filePath)) {
            speechrnt::utils::Logger::debug("Required model file missing: " + filePath);
            return false;
        }
    }
    
    return true;
}

TranslationResult MarianTranslator::performTranslation(const std::string& text, 
                                                      const std::string& sourceLang, 
                                                      const std::string& targetLang) {
    std::lock_guard<std::mutex> lock(translationMutex_);
    
    TranslationResult result;
    result.sourceLang = sourceLang;
    result.targetLang = targetLang;
    
    // Set up error context
    ErrorContext errorContext;
    errorContext.component = "MarianTranslator";
    errorContext.operation = "performTranslation";
    errorContext.languagePair = sourceLang + "->" + targetLang;
    errorContext.modelPath = getModelPath(sourceLang, targetLang);
    
    try {
        // Check if we're in degraded mode
        if (errorHandler_ && errorHandler_->isInDegradedMode()) {
            speechrnt::utils::Logger::info("Operating in degraded mode, using fallback translation");
            result = performFallbackTranslation(text, sourceLang, targetLang);
            result.errorMessage = "Operating in degraded mode";
            return result;
        }
        
        // Execute translation with timeout and retry
        if (errorHandler_) {
            RetryConfig retryConfig(3, std::chrono::milliseconds(100), std::chrono::milliseconds(2000), 2.0, std::chrono::milliseconds(10000));
            
            auto translationOperation = [this, &text, &sourceLang, &targetLang]() -> TranslationResult {
#ifdef MARIAN_AVAILABLE
                return performMarianTranslationWithTimeout(text, sourceLang, targetLang, std::chrono::milliseconds(5000));
#else
                return performFallbackTranslation(text, sourceLang, targetLang);
#endif
            };
            
            try {
                result = errorHandler_->executeWithRetry<TranslationResult>(translationOperation, retryConfig, errorContext);
            } catch (const MarianException& e) {
                // Handle Marian-specific errors with recovery
                RecoveryResult recovery = errorHandler_->handleError(e, errorContext);
                
                if (recovery.successful) {
                    // Retry after recovery
                    result = translationOperation();
                } else {
                    // Use fallback translation
                    result = performFallbackTranslation(text, sourceLang, targetLang);
                    result.errorMessage = "Translation failed, using fallback: " + std::string(e.what());
                }
            }
        } else {
            // Fallback to basic error handling if error handler not available
#ifdef MARIAN_AVAILABLE
            result = performMarianTranslation(text, sourceLang, targetLang);
#else
            result = performFallbackTranslation(text, sourceLang, targetLang);
            speechrnt::utils::Logger::warning("Using fallback translation - Marian NMT not available");
#endif
        }
        
        // Perform quality assessment if translation was successful
        if (result.success && qualityManager_ && qualityManager_->isReady()) {
            auto startTime = std::chrono::high_resolution_clock::now();
            
            // Assess translation quality
            QualityMetrics metrics = qualityManager_->assessTranslationQuality(
                text, result.translatedText, sourceLang, targetLang, result.wordLevelConfidences);
            
            // Store quality metrics in the result
            result.qualityMetrics = std::make_unique<QualityMetrics>(std::move(metrics));
            
            // Update confidence based on quality assessment
            result.confidence = result.qualityMetrics->overallConfidence;
            
            // Generate alternative translations if quality is low
            if (!qualityManager_->meetsQualityThreshold(*result.qualityMetrics, "medium")) {
                auto candidates = qualityManager_->generateTranslationCandidates(
                    text, result.translatedText, sourceLang, targetLang, 3);
                
                // Add alternatives to result (excluding the primary translation)
                for (size_t i = 1; i < candidates.size(); ++i) {
                    result.alternativeTranslations.push_back(candidates[i].translatedText);
                }
                
                speechrnt::utils::Logger::debug("Generated " + std::to_string(result.alternativeTranslations.size()) + 
                                   " alternative translations due to low quality");
            }
            
            auto endTime = std::chrono::high_resolution_clock::now();
            result.processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            speechrnt::utils::Logger::debug("Quality assessment completed: confidence=" + 
                               std::to_string(result.confidence) + ", level=" + 
                               result.qualityMetrics->qualityLevel);
        }
        
        speechrnt::utils::Logger::debug("Translated '" + text + "' from " + sourceLang + " to " + targetLang + ": '" + result.translatedText + "'");
        
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = "Translation failed: " + std::string(e.what());
        speechrnt::utils::Logger::error("Translation error: " + std::string(e.what()));
    }
    
    return result;
}

TranslationResult MarianTranslator::performMarianTranslation(const std::string& text, 
                                                           const std::string& sourceLang, 
                                                           const std::string& targetLang) {
    TranslationResult result;
    result.sourceLang = sourceLang;
    result.targetLang = targetLang;
    
#ifdef MARIAN_AVAILABLE
    try {
        std::string modelPath = getModelPath(sourceLang, targetLang);
        std::string configPath = modelPath + "/config.yml";
        std::string modelFile = modelPath + "/model.npz";
        std::string vocabPath = modelPath + "/vocab.yml";
        
        // Create Marian options
        auto options = marian::New<marian::Options>();
        options->set("model", modelFile);
        options->set("vocabs", std::vector<std::string>{vocabPath, vocabPath});
        options->set("beam-size", 5);
        options->set("normalize", 1.0f);
        options->set("word-penalty", 0.0f);
        
        // Set GPU options if enabled and initialized
        if (gpuAccelerationEnabled_ && gpuInitialized_) {
            std::string modelKey = getLanguagePairKey(sourceLang, targetLang);
            auto it = modelInfoMap_.find(modelKey);
            
            if (it != modelInfoMap_.end() && it->second.gpuEnabled && it->second.gpuMemoryPtr) {
                options->set("device-list", std::vector<size_t>{static_cast<size_t>(it->second.gpuDeviceId)});
                options->set("cpu-threads", 1);
                speechrnt::utils::Logger::debug("Using GPU device " + std::to_string(it->second.gpuDeviceId) + " for translation");
            } else {
                options->set("cpu-threads", std::thread::hardware_concurrency());
                speechrnt::utils::Logger::debug("GPU memory not available for model, using CPU");
            }
        } else {
            options->set("cpu-threads", std::thread::hardware_concurrency());
            speechrnt::utils::Logger::debug("Using CPU for translation");
        }
        
        // Create translator
        auto translator = marian::New<marian::Translate<marian::Search>>(options);
        
        // Prepare input
        std::vector<std::string> inputs = {text};
        std::vector<std::string> outputs;
        std::vector<float> scores;
        
        // Perform translation
        translator->translate(inputs, outputs, scores);
        
        if (!outputs.empty()) {
            result.translatedText = outputs[0];
            result.confidence = calculateActualConfidence(text, outputs[0], scores);
            result.success = true;
            
            // Add GPU usage information to the result
            std::string modelKey = getLanguagePairKey(sourceLang, targetLang);
            auto it = modelInfoMap_.find(modelKey);
            bool usedGPU = (it != modelInfoMap_.end() && it->second.gpuEnabled && it->second.gpuMemoryPtr);
            
            speechrnt::utils::Logger::debug("Marian translation successful: confidence = " + std::to_string(result.confidence) + 
                               ", GPU used: " + (usedGPU ? "yes" : "no"));
        } else {
            throw std::runtime_error("Marian translation returned empty result");
        }
        
    } catch (const MarianException& e) {
        std::string errorMsg = MarianErrorHandler::handleTranslationError(e.what(), text);
        speechrnt::utils::Logger::error("Marian translation failed: " + errorMsg);
        // Fall back to mock translation
        result = performFallbackTranslation(text, sourceLang, targetLang);
        result.errorMessage = "Marian translation failed, using fallback: " + std::string(e.what());
    } catch (const std::exception& e) {
        std::string errorMsg = MarianErrorHandler::handleTranslationError(e.what(), text);
        speechrnt::utils::Logger::error("Translation failed: " + errorMsg);
        // Fall back to mock translation
        result = performFallbackTranslation(text, sourceLang, targetLang);
        result.errorMessage = "Translation failed, using fallback: " + std::string(e.what());
    }
#else
    // If Marian is not available, use fallback
    result = performFallbackTranslation(text, sourceLang, targetLang);
    result.errorMessage = "Marian NMT not available, using fallback translation";
#endif
    
    return result;
}

TranslationResult MarianTranslator::performMarianTranslationWithTimeout(const std::string& text, 
                                                                       const std::string& sourceLang, 
                                                                       const std::string& targetLang,
                                                                       std::chrono::milliseconds timeout) {
    TranslationResult result;
    result.sourceLang = sourceLang;
    result.targetLang = targetLang;
    
    auto startTime = std::chrono::steady_clock::now();
    
    try {
        // Create a future for the translation operation
        auto future = std::async(std::launch::async, [this, &text, &sourceLang, &targetLang]() {
            return performMarianTranslation(text, sourceLang, targetLang);
        });
        
        // Wait for the operation to complete or timeout
        if (future.wait_for(timeout) == std::future_status::timeout) {
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
            
            throw MarianTimeoutException("Translation timed out after " + std::to_string(elapsedMs.count()) + "ms");
        }
        
        result = future.get();
        
        // Record processing time
        auto endTime = std::chrono::steady_clock::now();
        result.processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
    } catch (const MarianTimeoutException& e) {
        result.success = false;
        result.errorMessage = e.what();
        
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        result.processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        
        speechrnt::utils::Logger::warning("Translation timeout: " + std::string(e.what()));
        throw; // Re-throw to be handled by retry mechanism
        
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = "Translation failed: " + std::string(e.what());
        
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        result.processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        
        throw;
    }
    
    return result;
}

TranslationResult MarianTranslator::performFallbackTranslation(const std::string& text, 
                                                             const std::string& sourceLang, 
                                                             const std::string& targetLang) {
    TranslationResult result;
    result.sourceLang = sourceLang;
    result.targetLang = targetLang;
    
    // Enhanced fallback translation with better language support
    if (sourceLang == "en" && targetLang == "es") {
        // English to Spanish translations
        if (text == "Hello" || text == "hello") {
            result.translatedText = "Hola";
        } else if (text == "How are you?" || text == "how are you?") {
            result.translatedText = "¿Cómo estás?";
        } else if (text == "Good morning" || text == "good morning") {
            result.translatedText = "Buenos días";
        } else if (text == "Good afternoon" || text == "good afternoon") {
            result.translatedText = "Buenas tardes";
        } else if (text == "Good evening" || text == "good evening") {
            result.translatedText = "Buenas noches";
        } else if (text == "Thank you" || text == "thank you") {
            result.translatedText = "Gracias";
        } else if (text == "Please" || text == "please") {
            result.translatedText = "Por favor";
        } else if (text == "Excuse me" || text == "excuse me") {
            result.translatedText = "Disculpe";
        } else if (text == "Yes" || text == "yes") {
            result.translatedText = "Sí";
        } else if (text == "No" || text == "no") {
            result.translatedText = "No";
        } else {
            // Simple word-by-word fallback for unknown phrases
            result.translatedText = performSimpleTranslation(text, sourceLang, targetLang);
        }
    } else if (sourceLang == "es" && targetLang == "en") {
        // Spanish to English translations
        if (text == "Hola" || text == "hola") {
            result.translatedText = "Hello";
        } else if (text == "¿Cómo estás?" || text == "¿cómo estás?") {
            result.translatedText = "How are you?";
        } else if (text == "Buenos días" || text == "buenos días") {
            result.translatedText = "Good morning";
        } else if (text == "Buenas tardes" || text == "buenas tardes") {
            result.translatedText = "Good afternoon";
        } else if (text == "Buenas noches" || text == "buenas noches") {
            result.translatedText = "Good evening";
        } else if (text == "Gracias" || text == "gracias") {
            result.translatedText = "Thank you";
        } else if (text == "Por favor" || text == "por favor") {
            result.translatedText = "Please";
        } else if (text == "Disculpe" || text == "disculpe") {
            result.translatedText = "Excuse me";
        } else if (text == "Sí" || text == "sí") {
            result.translatedText = "Yes";
        } else if (text == "No" || text == "no") {
            result.translatedText = "No";
        } else {
            result.translatedText = performSimpleTranslation(text, sourceLang, targetLang);
        }
    } else {
        // Generic fallback for other language pairs
        result.translatedText = performSimpleTranslation(text, sourceLang, targetLang);
    }
    
    // Calculate confidence based on translation method
    if (result.translatedText.find("[" + targetLang + "]") == 0) {
        result.confidence = 0.3f; // Low confidence for generic fallback
    } else {
        result.confidence = 0.75f; // Higher confidence for known phrases
    }
    
    result.success = true;
    return result;
}

std::string MarianTranslator::performSimpleTranslation(const std::string& text, 
                                                      const std::string& sourceLang, 
                                                      const std::string& targetLang) {
    // Simple dictionary-based translation for common words
    static std::unordered_map<std::string, std::unordered_map<std::string, std::string>> dictionary = {
        {"en", {
            {"es", {
                {"hello", "hola"}, {"world", "mundo"}, {"good", "bueno"}, {"bad", "malo"},
                {"big", "grande"}, {"small", "pequeño"}, {"water", "agua"}, {"food", "comida"},
                {"house", "casa"}, {"car", "coche"}, {"time", "tiempo"}, {"day", "día"},
                {"night", "noche"}, {"friend", "amigo"}, {"family", "familia"}
            }},
            {"fr", {
                {"hello", "bonjour"}, {"world", "monde"}, {"good", "bon"}, {"bad", "mauvais"},
                {"big", "grand"}, {"small", "petit"}, {"water", "eau"}, {"food", "nourriture"},
                {"house", "maison"}, {"car", "voiture"}, {"time", "temps"}, {"day", "jour"},
                {"night", "nuit"}, {"friend", "ami"}, {"family", "famille"}
            }}
        }},
        {"es", {
            {"en", {
                {"hola", "hello"}, {"mundo", "world"}, {"bueno", "good"}, {"malo", "bad"},
                {"grande", "big"}, {"pequeño", "small"}, {"agua", "water"}, {"comida", "food"},
                {"casa", "house"}, {"coche", "car"}, {"tiempo", "time"}, {"día", "day"},
                {"noche", "night"}, {"amigo", "friend"}, {"familia", "family"}
            }}
        }}
    };
    
    // Convert to lowercase for lookup
    std::string lowerText = text;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
    
    // Try dictionary lookup
    auto sourceLangIt = dictionary.find(sourceLang);
    if (sourceLangIt != dictionary.end()) {
        auto targetLangIt = sourceLangIt->second.find(targetLang);
        if (targetLangIt != sourceLangIt->second.end()) {
            auto wordIt = targetLangIt->second.find(lowerText);
            if (wordIt != targetLangIt->second.end()) {
                return wordIt->second;
            }
        }
    }
    
    // If no dictionary match, return with language prefix
    return "[" + targetLang + "] " + text;
}

bool MarianTranslator::initializeMarianModel(const std::string& sourceLang, const std::string& targetLang) {
#ifdef MARIAN_AVAILABLE
    try {
        std::string modelPath = getModelPath(sourceLang, targetLang);
        std::string configPath = modelPath + "/config.yml";
        
        // Check if config file exists
        if (!std::filesystem::exists(configPath)) {
            speechrnt::utils::Logger::warning("Marian config file not found: " + configPath);
            return false;
        }
        
        // Initialize Marian logging
        marian::createLoggers();
        
        speechrnt::utils::Logger::info("Initialized Marian model for " + sourceLang + " -> " + targetLang);
        return true;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to initialize Marian model: " + std::string(e.what()));
        return false;
    }
#else
    speechrnt::utils::Logger::debug("Marian NMT not available, skipping model initialization");
    return false;
#endif
}

void MarianTranslator::cleanupMarianModel(const std::string& sourceLang, const std::string& targetLang) {
#ifdef MARIAN_AVAILABLE
    try {
        // Cleanup Marian resources for this language pair
        speechrnt::utils::Logger::debug("Cleaned up Marian model for " + sourceLang + " -> " + targetLang);
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Error cleaning up Marian model: " + std::string(e.what()));
    }
#endif
}

float MarianTranslator::calculateActualConfidence(const std::string& sourceText, 
                                                 const std::string& translatedText, 
                                                 const std::vector<float>& scores) {
    if (scores.empty()) {
        return 0.5f; // Default confidence when no scores available
    }
    
    // Calculate confidence based on Marian scores
    float avgScore = 0.0f;
    for (float score : scores) {
        avgScore += score;
    }
    avgScore /= scores.size();
    
    // Convert log probability to confidence (0-1 range)
    float confidence = std::exp(avgScore);
    
    // Apply length penalty for very short or very long translations
    float lengthRatio = static_cast<float>(translatedText.length()) / static_cast<float>(sourceText.length());
    if (lengthRatio < 0.5f || lengthRatio > 2.0f) {
        confidence *= 0.8f; // Reduce confidence for unusual length ratios
    }
    
    // Clamp to valid range
    return std::max(0.0f, std::min(1.0f, confidence));
}

void MarianTranslator::initializeSupportedLanguages() {
    // Load supported languages from configuration
    try {
        auto config = utils::Config::getInstance();
        auto marianConfig = config.getMarianConfig();
        
        // Load supported languages from config
        if (marianConfig.contains("supportedLanguages")) {
            supportedSourceLanguages_.clear();
            for (const auto& lang : marianConfig["supportedLanguages"]) {
                supportedSourceLanguages_.push_back(lang.get<std::string>());
            }
        } else {
            // Fallback to default languages
            supportedSourceLanguages_ = {"en", "es", "fr", "de", "it", "pt", "ru", "zh", "ja", "ko"};
        }
        
        // Load language pairs from config
        if (marianConfig.contains("languagePairs")) {
            supportedTargetLanguages_.clear();
            for (const auto& [sourceLang, targets] : marianConfig["languagePairs"].items()) {
                std::vector<std::string> targetLanguages;
                for (const auto& target : targets) {
                    targetLanguages.push_back(target.get<std::string>());
                }
                supportedTargetLanguages_[sourceLang] = targetLanguages;
            }
        } else {
            // Fallback to default language pairs
            supportedTargetLanguages_["en"] = {"es", "fr", "de", "it", "pt", "ru", "zh", "ja", "ko"};
            supportedTargetLanguages_["es"] = {"en", "pt", "fr", "it"};
            supportedTargetLanguages_["fr"] = {"en", "es", "de", "it", "pt"};
            supportedTargetLanguages_["de"] = {"en", "fr", "nl", "da", "sv"};
            supportedTargetLanguages_["it"] = {"en", "es", "fr", "pt"};
            supportedTargetLanguages_["pt"] = {"en", "es", "fr", "it"};
            supportedTargetLanguages_["ru"] = {"en"};
            supportedTargetLanguages_["zh"] = {"en", "ja", "ko"};
            supportedTargetLanguages_["ja"] = {"en", "zh", "ko"};
            supportedTargetLanguages_["ko"] = {"en", "zh", "ja"};
        }
        
        // Load max concurrent models setting
        if (marianConfig.contains("maxConcurrentModels")) {
            maxConcurrentModels_ = marianConfig["maxConcurrentModels"].get<size_t>();
        }
        
        speechrnt::utils::Logger::info("Initialized " + std::to_string(supportedSourceLanguages_.size()) + 
                           " source languages with " + std::to_string(supportedTargetLanguages_.size()) + 
                           " language pair mappings");
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to load language configuration: " + std::string(e.what()) + 
                           ", using default configuration");
        
        // Fallback to hardcoded defaults
        supportedSourceLanguages_ = {"en", "es", "fr", "de", "it", "pt", "ru", "zh", "ja", "ko"};
        supportedTargetLanguages_["en"] = {"es", "fr", "de", "it", "pt", "ru", "zh", "ja", "ko"};
        supportedTargetLanguages_["es"] = {"en", "pt", "fr", "it"};
        supportedTargetLanguages_["fr"] = {"en", "es", "de", "it", "pt"};
        supportedTargetLanguages_["de"] = {"en", "fr", "nl", "da", "sv"};
        supportedTargetLanguages_["it"] = {"en", "es", "fr", "pt"};
        supportedTargetLanguages_["pt"] = {"en", "es", "fr", "it"};
        supportedTargetLanguages_["ru"] = {"en"};
        supportedTargetLanguages_["zh"] = {"en", "ja", "ko"};
        supportedTargetLanguages_["ja"] = {"en", "zh", "ko"};
        supportedTargetLanguages_["ko"] = {"en", "zh", "ja"};
    }
}

// GPU-specific private methods implementation

bool MarianTranslator::initializeGPUResources() {
    if (!gpuManager_->initialize()) {
        gpuInitializationError_ = "Failed to initialize GPU manager";
        return false;
    }
    
    if (!gpuManager_->isCudaAvailable()) {
        gpuInitializationError_ = "CUDA not available";
        return false;
    }
    
    // Create GPU memory pool for efficient memory management
    utils::GPUMemoryPool::PoolConfig poolConfig;
    poolConfig.initialPoolSizeMB = 512;
    poolConfig.maxPoolSizeMB = 2048;
    poolConfig.blockSizeMB = 64;
    poolConfig.enableDefragmentation = true;
    
    gpuMemoryPool_ = std::make_unique<utils::GPUMemoryPool>(poolConfig);
    
    if (!gpuMemoryPool_->initialize()) {
        gpuInitializationError_ = "Failed to initialize GPU memory pool";
        return false;
    }
    
    gpuInitialized_ = true;
    return true;
}

void MarianTranslator::cleanupGPUResources() {
    std::lock_guard<std::mutex> lock(translationMutex_);
    
    // Free all GPU memory allocations
    for (auto& pair : modelInfoMap_) {
        if (pair.second.gpuMemoryPtr) {
            freeGPUMemoryForModel(pair.first.substr(0, 2), pair.first.substr(5, 2));
        }
    }
    
    // Cleanup GPU memory pool
    if (gpuMemoryPool_) {
        gpuMemoryPool_->cleanup();
        gpuMemoryPool_.reset();
    }
    
    gpuInitialized_ = false;
}

bool MarianTranslator::allocateGPUMemoryForModel(const std::string& sourceLang, const std::string& targetLang, size_t requiredMB) {
    if (!gpuInitialized_ || !gpuMemoryPool_) {
        return false;
    }
    
    std::string modelKey = getLanguagePairKey(sourceLang, targetLang);
    auto it = modelInfoMap_.find(modelKey);
    
    if (it == modelInfoMap_.end()) {
        modelInfoMap_[modelKey] = ModelInfo();
        it = modelInfoMap_.find(modelKey);
    }
    
    // Free existing allocation if any
    if (it->second.gpuMemoryPtr) {
        freeGPUMemoryForModel(sourceLang, targetLang);
    }
    
    // Allocate GPU memory
    size_t requiredBytes = requiredMB * 1024 * 1024;
    std::string tag = "marian_model_" + sourceLang + "_" + targetLang;
    
    void* gpuPtr = gpuMemoryPool_->allocate(requiredBytes, tag);
    if (!gpuPtr) {
        speechrnt::utils::Logger::error("Failed to allocate " + std::to_string(requiredMB) + "MB GPU memory for model " + modelKey);
        return false;
    }
    
    it->second.gpuMemoryPtr = gpuPtr;
    it->second.gpuMemorySizeMB = requiredMB;
    it->second.gpuEnabled = true;
    it->second.gpuDeviceId = defaultGpuDeviceId_;
    
    speechrnt::utils::Logger::info("Allocated " + std::to_string(requiredMB) + "MB GPU memory for model " + modelKey);
    return true;
}

void MarianTranslator::freeGPUMemoryForModel(const std::string& sourceLang, const std::string& targetLang) {
    if (!gpuInitialized_ || !gpuMemoryPool_) {
        return;
    }
    
    std::string modelKey = getLanguagePairKey(sourceLang, targetLang);
    auto it = modelInfoMap_.find(modelKey);
    
    if (it != modelInfoMap_.end() && it->second.gpuMemoryPtr) {
        gpuMemoryPool_->deallocate(it->second.gpuMemoryPtr);
        
        speechrnt::utils::Logger::info("Freed " + std::to_string(it->second.gpuMemorySizeMB) + "MB GPU memory for model " + modelKey);
        
        it->second.gpuMemoryPtr = nullptr;
        it->second.gpuMemorySizeMB = 0;
        it->second.gpuEnabled = false;
        it->second.gpuDeviceId = -1;
    }
}

bool MarianTranslator::loadModelToGPU(const std::string& sourceLang, const std::string& targetLang) {
    if (!gpuInitialized_) {
        return false;
    }
    
    std::string modelKey = getLanguagePairKey(sourceLang, targetLang);
    auto it = modelInfoMap_.find(modelKey);
    
    if (it == modelInfoMap_.end() || !it->second.gpuMemoryPtr) {
        speechrnt::utils::Logger::error("GPU memory not allocated for model " + modelKey);
        return false;
    }
    
    try {
        // In a real implementation, this would load the Marian model to GPU
        // For now, we simulate the loading process
        
#ifdef MARIAN_AVAILABLE
        // Load model configuration and weights to GPU memory
        std::string modelPath = getModelPath(sourceLang, targetLang);
        std::string configPath = modelPath + "/config.yml";
        std::string modelFile = modelPath + "/model.npz";
        
        // Create Marian options with GPU settings
        auto options = marian::New<marian::Options>();
        options->set("model", modelFile);
        options->set("device-list", std::vector<size_t>{static_cast<size_t>(it->second.gpuDeviceId)});
        options->set("cpu-threads", 1);
        
        // Load model to GPU (simplified simulation)
        speechrnt::utils::Logger::info("Loading Marian model " + modelKey + " to GPU device " + std::to_string(it->second.gpuDeviceId));
        
        // In real implementation, model would be loaded here
        it->second.marianModel = reinterpret_cast<void*>(0x1); // Placeholder
        
#else
        speechrnt::utils::Logger::debug("Marian not available, simulating GPU model loading for " + modelKey);
        it->second.marianModel = reinterpret_cast<void*>(0x1); // Placeholder
#endif
        
        it->second.loaded = true;
        speechrnt::utils::Logger::info("Successfully loaded model " + modelKey + " to GPU");
        return true;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to load model " + modelKey + " to GPU: " + std::string(e.what()));
        return false;
    }
}

bool MarianTranslator::fallbackToCPU(const std::string& reason) {
    speechrnt::utils::Logger::warn("Falling back to CPU processing: " + reason);
    
    // Set up error context for the fallback
    ErrorContext errorContext;
    errorContext.component = "MarianTranslator";
    errorContext.operation = "fallbackToCPU";
    errorContext.gpuDeviceId = defaultGpuDeviceId_;
    errorContext.additionalInfo = reason;
    
    try {
        // Use error handler for structured GPU fallback if available
        if (errorHandler_) {
            RecoveryResult recovery = errorHandler_->handleGPUErrorWithFallback(reason, errorContext);
            
            if (!recovery.successful) {
                speechrnt::utils::Logger::error("GPU fallback handling failed: " + recovery.message);
            }
        }
        
        // Disable GPU acceleration
        gpuAccelerationEnabled_ = false;
        
        // Free any GPU memory allocations
        for (auto& pair : modelInfoMap_) {
            if (pair.second.gpuMemoryPtr) {
                freeGPUMemoryForModel(pair.first.substr(0, 2), pair.first.substr(5, 2));
            }
        }
        
        // Try to initialize with CPU
        if (initialized_) {
            // Already initialized, just switch to CPU mode
            speechrnt::utils::Logger::info("Switched to CPU mode successfully");
            return true;
        } else {
            // Initialize with CPU
            bool success = initialize(currentSourceLang_, currentTargetLang_);
            
            if (success) {
                speechrnt::utils::Logger::info("Successfully fell back to CPU processing");
            } else {
                speechrnt::utils::Logger::error("CPU fallback initialization failed");
                
                // Enter degraded mode if CPU fallback fails
                if (errorHandler_) {
                    errorHandler_->enterDegradedMode("CPU fallback initialization failed", errorContext);
                }
            }
            
            return success;
        }
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Exception during CPU fallback: " + std::string(e.what()));
        
        // Enter degraded mode if fallback fails completely
        if (errorHandler_) {
            errorHandler_->enterDegradedMode("CPU fallback exception: " + std::string(e.what()), errorContext);
        }
        
        return false;
    }
}

size_t MarianTranslator::estimateModelMemoryRequirement(const std::string& sourceLang, const std::string& targetLang) const {
    // Estimate memory requirements based on model size
    // This is a simplified estimation - in practice, this would be based on actual model files
    
    // Base memory requirement for Marian models (in MB)
    size_t baseMemoryMB = 256;
    
    // Additional memory based on language complexity
    std::unordered_map<std::string, size_t> languageComplexity = {
        {"en", 100}, {"es", 120}, {"fr", 120}, {"de", 140}, {"it", 110},
        {"pt", 110}, {"ru", 180}, {"zh", 200}, {"ja", 220}, {"ko", 200}
    };
    
    size_t sourceComplexity = languageComplexity.count(sourceLang) ? languageComplexity[sourceLang] : 150;
    size_t targetComplexity = languageComplexity.count(targetLang) ? languageComplexity[targetLang] : 150;
    
    size_t totalMemoryMB = baseMemoryMB + sourceComplexity + targetComplexity;
    
    // Add buffer for GPU operations
    totalMemoryMB = static_cast<size_t>(totalMemoryMB * 1.2);
    
    return totalMemoryMB;
}

std::vector<TranslationResult> MarianTranslator::getTranslationCandidates(const std::string& text, int maxCandidates) {
    std::vector<TranslationResult> candidates;
    
    if (!isReady() || !qualityManager_ || !qualityManager_->isReady()) {
        speechrnt::utils::Logger::warn("Cannot generate translation candidates: translator or quality manager not ready");
        return candidates;
    }
    
    try {
        // Get the primary translation
        TranslationResult primary = translate(text);
        if (!primary.success) {
            return candidates;
        }
        
        candidates.push_back(primary);
        
        // Generate additional candidates using quality manager
        if (maxCandidates > 1) {
            auto qualityCandidates = qualityManager_->generateTranslationCandidates(
                text, primary.translatedText, currentSourceLang_, currentTargetLang_, maxCandidates);
            
            // Convert quality candidates to translation results
            for (size_t i = 1; i < qualityCandidates.size() && candidates.size() < maxCandidates; ++i) {
                TranslationResult candidate;
                candidate.translatedText = qualityCandidates[i].translatedText;
                candidate.confidence = qualityCandidates[i].qualityMetrics.overallConfidence;
                candidate.sourceLang = currentSourceLang_;
                candidate.targetLang = currentTargetLang_;
                candidate.success = true;
                candidate.qualityMetrics = std::make_unique<QualityMetrics>(qualityCandidates[i].qualityMetrics);
                candidate.usedGPUAcceleration = primary.usedGPUAcceleration;
                
                candidates.push_back(candidate);
            }
        }
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Error generating translation candidates: " + std::string(e.what()));
    }
    
    return candidates;
}

std::vector<std::string> MarianTranslator::getFallbackTranslations(const std::string& text) {
    std::vector<std::string> fallbacks;
    
    if (!isReady() || !qualityManager_ || !qualityManager_->isReady()) {
        speechrnt::utils::Logger::warn("Cannot generate fallback translations: translator or quality manager not ready");
        return fallbacks;
    }
    
    try {
        // Get the primary translation first
        TranslationResult primary = translate(text);
        
        // Generate fallback translations using quality manager
        fallbacks = qualityManager_->getFallbackTranslations(
            text, primary.success ? primary.translatedText : "", 
            currentSourceLang_, currentTargetLang_);
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Error generating fallback translations: " + std::string(e.what()));
    }
    
    return fallbacks;
}

void MarianTranslator::setQualityThresholds(float high, float medium, float low) {
    if (qualityManager_ && qualityManager_->isReady()) {
        qualityManager_->setQualityThresholds(high, medium, low);
        speechrnt::utils::Logger::info("Quality thresholds updated in MarianTranslator");
    } else {
        speechrnt::utils::Logger::warn("Cannot set quality thresholds: quality manager not ready");
    }
}

bool MarianTranslator::meetsQualityThreshold(const TranslationResult& result, const std::string& requiredLevel) const {
    if (!qualityManager_ || !qualityManager_->isReady() || !result.qualityMetrics) {
        speechrnt::utils::Logger::warn("Cannot check quality threshold: quality manager or metrics not available");
        return false;
    }
    
    return qualityManager_->meetsQualityThreshold(*result.qualityMetrics, requiredLevel);
}

// Batch translation methods

std::vector<TranslationResult> MarianTranslator::translateBatch(const std::vector<std::string>& texts) {
    if (texts.empty()) {
        speechrnt::utils::Logger::warn("Empty batch translation request");
        return {};
    }
    
    if (texts.size() > maxBatchSize_) {
        speechrnt::utils::Logger::warn("Batch size (" + std::to_string(texts.size()) + 
                           ") exceeds maximum (" + std::to_string(maxBatchSize_) + ")");
        
        // Process in chunks
        std::vector<TranslationResult> allResults;
        for (size_t i = 0; i < texts.size(); i += maxBatchSize_) {
            size_t end = std::min(i + maxBatchSize_, texts.size());
            std::vector<std::string> chunk(texts.begin() + i, texts.begin() + end);
            auto chunkResults = processBatch(chunk);
            
            // Update batch indices
            for (size_t j = 0; j < chunkResults.size(); ++j) {
                chunkResults[j].batchIndex = static_cast<int>(i + j);
            }
            
            allResults.insert(allResults.end(), chunkResults.begin(), chunkResults.end());
        }
        return allResults;
    }
    
    return processBatch(texts);
}

std::future<std::vector<TranslationResult>> MarianTranslator::translateBatchAsync(const std::vector<std::string>& texts) {
    return std::async(std::launch::async, [this, texts]() {
        return translateBatch(texts);
    });
}

std::vector<TranslationResult> MarianTranslator::processBatch(const std::vector<std::string>& texts) {
    std::vector<TranslationResult> results;
    results.reserve(texts.size());
    
    // Create indexed pairs for optimization
    std::vector<std::pair<size_t, std::string>> indexedTexts;
    for (size_t i = 0; i < texts.size(); ++i) {
        indexedTexts.emplace_back(i, texts[i]);
    }
    
    // Optimize batch order for better cache utilization
    optimizeBatchOrder(indexedTexts);
    
    // Process each text in the optimized order
    std::vector<std::pair<size_t, TranslationResult>> indexedResults;
    for (const auto& [originalIndex, text] : indexedTexts) {
        auto result = translate(text);
        result.batchIndex = static_cast<int>(originalIndex);
        indexedResults.emplace_back(originalIndex, std::move(result));
    }
    
    // Restore original order
    std::sort(indexedResults.begin(), indexedResults.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    
    for (auto& [index, result] : indexedResults) {
        results.push_back(std::move(result));
    }
    
    return results;
}

void MarianTranslator::optimizeBatchOrder(std::vector<std::pair<size_t, std::string>>& indexedTexts) {
    // Sort by text length to optimize GPU batch processing
    // Shorter texts first to minimize padding overhead
    std::sort(indexedTexts.begin(), indexedTexts.end(),
              [](const auto& a, const auto& b) {
                  return a.second.length() < b.second.length();
              });
}

// Streaming translation methods

bool MarianTranslator::startStreamingTranslation(const std::string& sessionId, const std::string& sourceLang, const std::string& targetLang) {
    std::lock_guard<std::mutex> lock(streamingMutex_);
    
    if (streamingSessions_.find(sessionId) != streamingSessions_.end()) {
        speechrnt::utils::Logger::warn("Streaming session already exists: " + sessionId);
        return false;
    }
    
    if (!supportsLanguagePair(sourceLang, targetLang)) {
        speechrnt::utils::Logger::error("Unsupported language pair for streaming: " + sourceLang + " -> " + targetLang);
        return false;
    }
    
    // Clean up expired sessions first
    cleanupExpiredSessions();
    
    streamingSessions_[sessionId] = StreamingSession(sessionId, sourceLang, targetLang);
    speechrnt::utils::Logger::info("Started streaming translation session: " + sessionId + " (" + sourceLang + " -> " + targetLang + ")");
    
    return true;
}

TranslationResult MarianTranslator::addStreamingText(const std::string& sessionId, const std::string& text, bool isComplete) {
    std::lock_guard<std::mutex> lock(streamingMutex_);
    
    auto it = streamingSessions_.find(sessionId);
    if (it == streamingSessions_.end()) {
        TranslationResult result;
        result.success = false;
        result.errorMessage = "Streaming session not found: " + sessionId;
        return result;
    }
    
    StreamingSession& session = it->second;
    session.lastActivity = std::chrono::steady_clock::now();
    
    // Add text chunk to session
    session.textChunks.push_back(text);
    session.accumulatedText += text;
    
    // Preserve context from previous translations
    std::string contextualText = preserveContext(session.contextBuffer, text);
    
    // Perform translation with context
    TranslationResult result = translateWithContext(text, session.contextBuffer, session.sourceLang, session.targetLang);
    
    // Update session state
    result.sessionId = sessionId;
    result.isPartialResult = !isComplete;
    result.isStreamingComplete = isComplete;
    
    if (result.success) {
        session.partialResults.push_back(result);
        
        // Update context buffer with recent translations for better context preservation
        if (session.partialResults.size() > 3) {
            // Keep only the last 3 translations for context
            session.contextBuffer = session.partialResults[session.partialResults.size() - 3].translatedText + " " +
                                   session.partialResults[session.partialResults.size() - 2].translatedText + " " +
                                   session.partialResults[session.partialResults.size() - 1].translatedText;
        } else {
            session.contextBuffer += result.translatedText + " ";
        }
    }
    
    return result;
}

TranslationResult MarianTranslator::finalizeStreamingTranslation(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(streamingMutex_);
    
    auto it = streamingSessions_.find(sessionId);
    if (it == streamingSessions_.end()) {
        TranslationResult result;
        result.success = false;
        result.errorMessage = "Streaming session not found: " + sessionId;
        return result;
    }
    
    StreamingSession& session = it->second;
    
    // Perform final translation on accumulated text
    TranslationResult finalResult = translate(session.accumulatedText);
    finalResult.sessionId = sessionId;
    finalResult.isPartialResult = false;
    finalResult.isStreamingComplete = true;
    
    // Clean up session
    streamingSessions_.erase(it);
    
    speechrnt::utils::Logger::info("Finalized streaming translation session: " + sessionId);
    return finalResult;
}

void MarianTranslator::cancelStreamingTranslation(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(streamingMutex_);
    
    auto it = streamingSessions_.find(sessionId);
    if (it != streamingSessions_.end()) {
        streamingSessions_.erase(it);
        speechrnt::utils::Logger::info("Cancelled streaming translation session: " + sessionId);
    }
}

bool MarianTranslator::hasStreamingSession(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(streamingMutex_);
    return streamingSessions_.find(sessionId) != streamingSessions_.end();
}

void MarianTranslator::cleanupExpiredSessions() {
    auto now = std::chrono::steady_clock::now();
    auto it = streamingSessions_.begin();
    
    while (it != streamingSessions_.end()) {
        if (now - it->second.lastActivity > sessionTimeout_) {
            speechrnt::utils::Logger::info("Cleaning up expired streaming session: " + it->first);
            it = streamingSessions_.erase(it);
        } else {
            ++it;
        }
    }
}

// Configuration methods

void MarianTranslator::setMaxBatchSize(size_t maxBatchSize) {
    maxBatchSize_ = maxBatchSize;
    speechrnt::utils::Logger::info("Maximum batch size set to: " + std::to_string(maxBatchSize));
}

void MarianTranslator::setTranslationCaching(bool enabled, size_t maxCacheSize) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    cachingEnabled_ = enabled;
    maxCacheSize_ = maxCacheSize;
    
    if (!enabled) {
        translationCache_.clear();
        cacheStats_ = CacheStats();
    } else if (translationCache_.size() > maxCacheSize) {
        evictOldestCacheEntries();
    }
    
    speechrnt::utils::Logger::info("Translation caching " + std::string(enabled ? "enabled" : "disabled") + 
                       " with max size: " + std::to_string(maxCacheSize));
}

void MarianTranslator::clearTranslationCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    translationCache_.clear();
    cacheStats_ = CacheStats();
    speechrnt::utils::Logger::info("Translation cache cleared");
}

float MarianTranslator::getCacheHitRate() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return cacheStats_.getHitRate();
}

// Private helper methods

std::string MarianTranslator::generateCacheKey(const std::string& text, const std::string& sourceLang, const std::string& targetLang) const {
    return sourceLang + "|" + targetLang + "|" + text;
}

bool MarianTranslator::getCachedTranslation(const std::string& cacheKey, TranslationResult& result) {
    if (!cachingEnabled_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cacheStats_.totalRequests++;
    
    auto it = translationCache_.find(cacheKey);
    if (it != translationCache_.end()) {
        // Update access statistics
        it->second.accessCount++;
        it->second.timestamp = std::chrono::steady_clock::now();
        
        // Create result from cache
        result.translatedText = it->second.translatedText;
        result.confidence = it->second.confidence;
        result.success = true;
        
        cacheStats_.cacheHits++;
        return true;
    }
    
    cacheStats_.cacheMisses++;
    return false;
}

void MarianTranslator::cacheTranslation(const std::string& cacheKey, const TranslationResult& result) {
    if (!cachingEnabled_ || !result.success) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    // Check if cache is full
    if (translationCache_.size() >= maxCacheSize_) {
        evictOldestCacheEntries();
    }
    
    translationCache_[cacheKey] = CacheEntry(result.translatedText, result.confidence);
}

void MarianTranslator::evictOldestCacheEntries() {
    if (translationCache_.empty()) {
        return;
    }
    
    // Remove 25% of oldest entries
    size_t entriesToRemove = std::max(size_t(1), maxCacheSize_ / 4);
    
    // Create vector of entries with timestamps
    std::vector<std::pair<std::chrono::steady_clock::time_point, std::string>> timestampedKeys;
    for (const auto& [key, entry] : translationCache_) {
        timestampedKeys.emplace_back(entry.timestamp, key);
    }
    
    // Sort by timestamp (oldest first)
    std::sort(timestampedKeys.begin(), timestampedKeys.end());
    
    // Remove oldest entries
    for (size_t i = 0; i < std::min(entriesToRemove, timestampedKeys.size()); ++i) {
        translationCache_.erase(timestampedKeys[i].second);
    }
}

std::string MarianTranslator::preserveContext(const std::string& previousText, const std::string& newText) {
    if (previousText.empty()) {
        return newText;
    }
    
    // Simple context preservation - keep last few words from previous text
    std::istringstream iss(previousText);
    std::vector<std::string> words;
    std::string word;
    
    while (iss >> word) {
        words.push_back(word);
    }
    
    // Keep last 5 words for context
    size_t contextWords = std::min(size_t(5), words.size());
    std::string context;
    
    if (contextWords > 0) {
        for (size_t i = words.size() - contextWords; i < words.size(); ++i) {
            if (!context.empty()) context += " ";
            context += words[i];
        }
        return context + " " + newText;
    }
    
    return newText;
}

TranslationResult MarianTranslator::translateWithContext(const std::string& text, const std::string& context, const std::string& sourceLang, const std::string& targetLang) {
    // Check cache first
    std::string cacheKey = generateCacheKey(text, sourceLang, targetLang);
    TranslationResult cachedResult;
    
    if (getCachedTranslation(cacheKey, cachedResult)) {
        return cachedResult;
    }
    
    // Prepare contextual text for translation
    std::string contextualText = context.empty() ? text : preserveContext(context, text);
    
    // Perform translation
    TranslationResult result = performTranslation(contextualText, sourceLang, targetLang);
    
    // If we used context, extract only the relevant part of the translation
    if (!context.empty() && result.success) {
        // This is a simplified approach - in a real implementation, you might need
        // more sophisticated context handling based on the specific MT model
        result.translatedText = result.translatedText; // Keep full translation for now
    }
    
    // Cache the result
    if (result.success) {
        cacheTranslation(cacheKey, result);
    }
    
    return result;
}

// Multi-language pair support implementation

bool MarianTranslator::initializeMultipleLanguagePairs(const std::vector<std::pair<std::string, std::string>>& languagePairs) {
    std::lock_guard<std::mutex> lock(languagePairMutex_);
    
    speechrnt::utils::Logger::info("Initializing " + std::to_string(languagePairs.size()) + " language pairs");
    
    bool allSuccessful = true;
    size_t successfulPairs = 0;
    
    for (const auto& pair : languagePairs) {
        const std::string& sourceLang = pair.first;
        const std::string& targetLang = pair.second;
        
        // Validate language pair first
        auto validation = validateLanguagePairDetailed(sourceLang, targetLang);
        if (!validation.isValid) {
            speechrnt::utils::Logger::error("Invalid language pair " + sourceLang + " -> " + targetLang + ": " + validation.errorMessage);
            allSuccessful = false;
            continue;
        }
        
        // Check if we need to unload least recently used models
        if (loadedLanguagePairs_.size() >= maxConcurrentModels_) {
            unloadLeastRecentlyUsedModel();
        }
        
        // Load the model for this language pair
        if (loadLanguagePairModel(sourceLang, targetLang)) {
            loadedLanguagePairs_.push_back(pair);
            updateModelUsageStatistics(sourceLang, targetLang);
            successfulPairs++;
            speechrnt::utils::Logger::info("Successfully loaded language pair: " + sourceLang + " -> " + targetLang);
        } else {
            speechrnt::utils::Logger::error("Failed to load language pair: " + sourceLang + " -> " + targetLang);
            allSuccessful = false;
        }
    }
    
    speechrnt::utils::Logger::info("Loaded " + std::to_string(successfulPairs) + " out of " + 
                       std::to_string(languagePairs.size()) + " language pairs");
    
    return allSuccessful;
}

bool MarianTranslator::switchLanguagePair(const std::string& sourceLang, const std::string& targetLang) {
    std::lock_guard<std::mutex> lock(translationMutex_);
    
    // Validate the language pair
    auto validation = validateLanguagePairDetailed(sourceLang, targetLang);
    if (!validation.isValid) {
        speechrnt::utils::Logger::error("Cannot switch to invalid language pair " + sourceLang + " -> " + targetLang + ": " + validation.errorMessage);
        return false;
    }
    
    // Check if the model is already loaded
    if (!isModelLoaded(sourceLang, targetLang)) {
        // Load the model if not already loaded
        if (!loadLanguagePairModel(sourceLang, targetLang)) {
            speechrnt::utils::Logger::error("Failed to load model for language pair: " + sourceLang + " -> " + targetLang);
            return false;
        }
    }
    
    // Update current language pair
    currentSourceLang_ = sourceLang;
    currentTargetLang_ = targetLang;
    initialized_ = true;
    
    // Update usage statistics
    updateModelUsageStatistics(sourceLang, targetLang);
    
    speechrnt::utils::Logger::info("Switched to language pair: " + sourceLang + " -> " + targetLang);
    return true;
}

TranslationResult MarianTranslator::translateWithLanguagePair(const std::string& text, 
                                                             const std::string& sourceLang, 
                                                             const std::string& targetLang) {
    // Validate language pair
    auto validation = validateLanguagePairDetailed(sourceLang, targetLang);
    if (!validation.isValid) {
        TranslationResult result;
        result.success = false;
        result.errorMessage = "Invalid language pair " + sourceLang + " -> " + targetLang + ": " + validation.errorMessage;
        result.sourceLang = sourceLang;
        result.targetLang = targetLang;
        return result;
    }
    
    // Check if model is loaded, load if necessary
    if (!isModelLoaded(sourceLang, targetLang)) {
        std::lock_guard<std::mutex> lock(languagePairMutex_);
        
        // Check if we need to unload least recently used models
        if (loadedLanguagePairs_.size() >= maxConcurrentModels_) {
            unloadLeastRecentlyUsedModel();
        }
        
        if (!loadLanguagePairModel(sourceLang, targetLang)) {
            TranslationResult result;
            result.success = false;
            result.errorMessage = "Failed to load model for language pair: " + sourceLang + " -> " + targetLang;
            result.sourceLang = sourceLang;
            result.targetLang = targetLang;
            return result;
        }
        
        // Add to loaded pairs if not already present
        auto pairIt = std::find(loadedLanguagePairs_.begin(), loadedLanguagePairs_.end(), 
                               std::make_pair(sourceLang, targetLang));
        if (pairIt == loadedLanguagePairs_.end()) {
            loadedLanguagePairs_.push_back(std::make_pair(sourceLang, targetLang));
        }
    }
    
    // Update usage statistics
    updateModelUsageStatistics(sourceLang, targetLang);
    
    // Perform translation
    return performTranslation(text, sourceLang, targetLang);
}

std::future<TranslationResult> MarianTranslator::translateWithLanguagePairAsync(const std::string& text, 
                                                                               const std::string& sourceLang, 
                                                                               const std::string& targetLang) {
    return std::async(std::launch::async, [this, text, sourceLang, targetLang]() {
        return translateWithLanguagePair(text, sourceLang, targetLang);
    });
}

std::vector<std::pair<std::string, std::string>> MarianTranslator::getLoadedLanguagePairs() const {
    std::lock_guard<std::mutex> lock(languagePairMutex_);
    return loadedLanguagePairs_;
}

MarianTranslator::LanguagePairValidationResult MarianTranslator::validateLanguagePairDetailed(const std::string& sourceLang, 
                                                                                              const std::string& targetLang) const {
    LanguagePairValidationResult result;
    result.isValid = false;
    result.sourceSupported = false;
    result.targetSupported = false;
    result.modelAvailable = false;
    
    // Validate source language
    if (!validateLanguageCode(sourceLang)) {
        result.errorMessage = "Invalid source language code: " + sourceLang;
        result.suggestions = getSuggestedAlternativeLanguages(sourceLang);
        return result;
    }
    
    // Check if source language is supported
    auto sourceIt = std::find(supportedSourceLanguages_.begin(), supportedSourceLanguages_.end(), sourceLang);
    if (sourceIt == supportedSourceLanguages_.end()) {
        result.errorMessage = "Source language not supported: " + sourceLang;
        result.suggestions = getSuggestedAlternativeLanguages(sourceLang);
        return result;
    }
    result.sourceSupported = true;
    
    // Validate target language
    if (!validateLanguageCode(targetLang)) {
        result.errorMessage = "Invalid target language code: " + targetLang;
        result.suggestions = getSuggestedAlternativeLanguages(targetLang);
        return result;
    }
    
    // Check if target language is supported for this source
    auto targetIt = supportedTargetLanguages_.find(sourceLang);
    if (targetIt == supportedTargetLanguages_.end()) {
        result.errorMessage = "No target languages available for source language: " + sourceLang;
        return result;
    }
    
    const auto& targets = targetIt->second;
    auto targetLangIt = std::find(targets.begin(), targets.end(), targetLang);
    if (targetLangIt == targets.end()) {
        result.errorMessage = "Target language " + targetLang + " not supported for source language " + sourceLang;
        result.suggestions = targets; // Suggest all available target languages
        result.targetSupported = false;
        return result;
    }
    result.targetSupported = true;
    
    // Check if model is available
    if (isLanguagePairModelAvailable(sourceLang, targetLang)) {
        result.modelAvailable = true;
        result.isValid = true;
    } else {
        result.errorMessage = "Model not available for language pair: " + sourceLang + " -> " + targetLang;
        result.downloadRecommendation = getModelDownloadUrl(sourceLang, targetLang);
        
        // Check for alternative language pairs
        auto bidirectionalInfo = getBidirectionalSupportInfo(sourceLang, targetLang);
        if (bidirectionalInfo.lang2ToLang1Supported) {
            result.suggestions.push_back("Try reverse direction: " + targetLang + " -> " + sourceLang);
        }
    }
    
    return result;
}

MarianTranslator::BidirectionalSupportInfo MarianTranslator::getBidirectionalSupportInfo(const std::string& lang1, 
                                                                                        const std::string& lang2) const {
    BidirectionalSupportInfo info;
    info.lang1ToLang2Supported = supportsLanguagePair(lang1, lang2);
    info.lang2ToLang1Supported = supportsLanguagePair(lang2, lang1);
    info.bothDirectionsAvailable = info.lang1ToLang2Supported && info.lang2ToLang1Supported;
    
    if (info.lang1ToLang2Supported) {
        info.lang1ToLang2ModelPath = getModelPath(lang1, lang2);
    } else {
        info.missingModels.push_back(lang1 + " -> " + lang2);
    }
    
    if (info.lang2ToLang1Supported) {
        info.lang2ToLang1ModelPath = getModelPath(lang2, lang1);
    } else {
        info.missingModels.push_back(lang2 + " -> " + lang1);
    }
    
    return info;
}

size_t MarianTranslator::preloadLanguagePairs(const std::vector<std::pair<std::string, std::string>>& languagePairs, 
                                             size_t maxConcurrentModels) {
    std::lock_guard<std::mutex> lock(languagePairMutex_);
    
    maxConcurrentModels_ = maxConcurrentModels;
    size_t successfullyLoaded = 0;
    
    speechrnt::utils::Logger::info("Preloading " + std::to_string(languagePairs.size()) + " language pairs (max concurrent: " + 
                       std::to_string(maxConcurrentModels) + ")");
    
    for (const auto& pair : languagePairs) {
        if (loadedLanguagePairs_.size() >= maxConcurrentModels_) {
            speechrnt::utils::Logger::info("Reached maximum concurrent models limit (" + std::to_string(maxConcurrentModels_) + ")");
            break;
        }
        
        const std::string& sourceLang = pair.first;
        const std::string& targetLang = pair.second;
        
        // Skip if already loaded
        auto existingIt = std::find(loadedLanguagePairs_.begin(), loadedLanguagePairs_.end(), pair);
        if (existingIt != loadedLanguagePairs_.end()) {
            speechrnt::utils::Logger::debug("Language pair already loaded: " + sourceLang + " -> " + targetLang);
            successfullyLoaded++;
            continue;
        }
        
        // Validate and load
        auto validation = validateLanguagePairDetailed(sourceLang, targetLang);
        if (validation.isValid && loadLanguagePairModel(sourceLang, targetLang)) {
            loadedLanguagePairs_.push_back(pair);
            updateModelUsageStatistics(sourceLang, targetLang);
            successfullyLoaded++;
            speechrnt::utils::Logger::info("Preloaded language pair: " + sourceLang + " -> " + targetLang);
        } else {
            speechrnt::utils::Logger::warning("Failed to preload language pair: " + sourceLang + " -> " + targetLang);
        }
    }
    
    speechrnt::utils::Logger::info("Successfully preloaded " + std::to_string(successfullyLoaded) + " language pairs");
    return successfullyLoaded;
}

MarianTranslator::ModelDownloadRecommendation MarianTranslator::getModelDownloadRecommendation(const std::string& sourceLang, 
                                                                                              const std::string& targetLang) const {
    ModelDownloadRecommendation recommendation;
    
    std::string pairKey = sourceLang + "-" + targetLang;
    auto it = modelDownloadInfo_.find(pairKey);
    
    if (it != modelDownloadInfo_.end()) {
        recommendation = it->second;
    } else {
        // Generate default recommendation
        recommendation.modelAvailable = false;
        recommendation.modelName = "marian-" + sourceLang + "-" + targetLang;
        recommendation.downloadUrl = getModelDownloadUrl(sourceLang, targetLang);
        recommendation.modelSize = std::to_string(estimateModelSize(sourceLang, targetLang)) + " MB";
        recommendation.description = "Neural machine translation model for " + sourceLang + " to " + targetLang;
        
        // Suggest alternative language pairs
        auto bidirectionalInfo = getBidirectionalSupportInfo(sourceLang, targetLang);
        if (bidirectionalInfo.lang2ToLang1Supported) {
            recommendation.alternativeLanguagePairs.push_back(targetLang + " -> " + sourceLang);
        }
        
        // Suggest similar language pairs
        auto sourceTargets = getSupportedTargetLanguages(sourceLang);
        for (const auto& target : sourceTargets) {
            if (target != targetLang) {
                recommendation.alternativeLanguagePairs.push_back(sourceLang + " -> " + target);
            }
        }
    }
    
    return recommendation;
}

MarianTranslator::ModelStatistics MarianTranslator::getModelStatistics() const {
    std::lock_guard<std::mutex> lock(languagePairMutex_);
    
    ModelStatistics stats;
    stats.totalLoadedModels = loadedLanguagePairs_.size();
    stats.totalSupportedPairs = 0;
    stats.gpuModels = 0;
    stats.cpuModels = 0;
    stats.totalMemoryUsageMB = getGPUMemoryUsageMB();
    
    // Count total supported pairs
    for (const auto& sourcePair : supportedTargetLanguages_) {
        stats.totalSupportedPairs += sourcePair.second.size();
    }
    
    // Count GPU vs CPU models
    for (const auto& pair : loadedLanguagePairs_) {
        std::string modelKey = getLanguagePairKey(pair.first, pair.second);
        auto it = modelInfoMap_.find(modelKey);
        if (it != modelInfoMap_.end() && it->second.gpuEnabled) {
            stats.gpuModels++;
        } else {
            stats.cpuModels++;
        }
    }
    
    // Get most and least used pairs
    std::vector<std::pair<std::pair<std::string, std::string>, size_t>> usagePairs;
    for (const auto& pair : loadedLanguagePairs_) {
        std::string modelKey = getLanguagePairKey(pair.first, pair.second);
        auto usageIt = modelUsageCount_.find(modelKey);
        size_t usage = (usageIt != modelUsageCount_.end()) ? usageIt->second : 0;
        usagePairs.push_back(std::make_pair(pair, usage));
    }
    
    // Sort by usage count
    std::sort(usagePairs.begin(), usagePairs.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Extract most and least used pairs (up to 5 each)
    size_t maxPairs = std::min(usagePairs.size(), size_t(5));
    for (size_t i = 0; i < maxPairs; ++i) {
        stats.mostUsedPairs.push_back(usagePairs[i].first);
    }
    
    // Least used pairs (reverse order)
    for (size_t i = 0; i < maxPairs; ++i) {
        size_t index = usagePairs.size() - 1 - i;
        stats.leastUsedPairs.push_back(usagePairs[index].first);
    }
    
    return stats;
}

// Private helper methods implementation

bool MarianTranslator::loadLanguagePairModel(const std::string& sourceLang, const std::string& targetLang) {
    return loadModel(sourceLang, targetLang);
}

void MarianTranslator::unloadLeastRecentlyUsedModel() {
    if (loadedLanguagePairs_.empty()) {
        return;
    }
    
    // Find the least recently used model
    auto oldestTime = std::chrono::steady_clock::now();
    std::pair<std::string, std::string> lruPair;
    bool foundLRU = false;
    
    for (const auto& pair : loadedLanguagePairs_) {
        std::string modelKey = getLanguagePairKey(pair.first, pair.second);
        auto it = modelLastUsed_.find(modelKey);
        
        if (it != modelLastUsed_.end() && it->second < oldestTime) {
            oldestTime = it->second;
            lruPair = pair;
            foundLRU = true;
        }
    }
    
    if (foundLRU) {
        speechrnt::utils::Logger::info("Unloading least recently used model: " + lruPair.first + " -> " + lruPair.second);
        unloadModel(lruPair.first, lruPair.second);
        
        // Remove from loaded pairs
        auto it = std::find(loadedLanguagePairs_.begin(), loadedLanguagePairs_.end(), lruPair);
        if (it != loadedLanguagePairs_.end()) {
            loadedLanguagePairs_.erase(it);
        }
        
        // Clean up usage tracking
        std::string modelKey = getLanguagePairKey(lruPair.first, lruPair.second);
        modelLastUsed_.erase(modelKey);
    }
}

void MarianTranslator::updateModelUsageStatistics(const std::string& sourceLang, const std::string& targetLang) {
    std::string modelKey = getLanguagePairKey(sourceLang, targetLang);
    
    // Update last used time
    modelLastUsed_[modelKey] = std::chrono::steady_clock::now();
    
    // Update usage count
    modelUsageCount_[modelKey]++;
}

std::vector<std::string> MarianTranslator::getSuggestedAlternativeLanguages(const std::string& language) const {
    std::vector<std::string> suggestions;
    
    // Language similarity mappings for suggestions
    static const std::unordered_map<std::string, std::vector<std::string>> similarLanguages = {
        {"en", {"es", "fr", "de", "it", "pt"}},
        {"es", {"en", "pt", "it", "fr", "ca"}},
        {"fr", {"en", "es", "it", "pt", "de"}},
        {"de", {"en", "nl", "da", "sv", "no"}},
        {"it", {"es", "fr", "pt", "en", "ro"}},
        {"pt", {"es", "it", "fr", "en", "gl"}},
        {"ru", {"uk", "be", "bg", "sr", "en"}},
        {"zh", {"ja", "ko", "en", "vi", "th"}},
        {"ja", {"zh", "ko", "en", "vi", "th"}},
        {"ko", {"zh", "ja", "en", "vi", "th"}}
    };
    
    auto it = similarLanguages.find(language);
    if (it != similarLanguages.end()) {
        // Filter suggestions to only include supported languages
        for (const auto& suggestion : it->second) {
            if (std::find(allSupportedLanguages_.begin(), allSupportedLanguages_.end(), suggestion) != allSupportedLanguages_.end()) {
                suggestions.push_back(suggestion);
            }
        }
    }
    
    // If no specific suggestions, return some common languages
    if (suggestions.empty()) {
        std::vector<std::string> commonLanguages = {"en", "es", "fr", "de", "it", "pt", "ru", "zh", "ja", "ko"};
        for (const auto& common : commonLanguages) {
            if (std::find(allSupportedLanguages_.begin(), allSupportedLanguages_.end(), common) != allSupportedLanguages_.end()) {
                suggestions.push_back(common);
            }
        }
    }
    
    return suggestions;
}

bool MarianTranslator::isLanguagePairModelAvailable(const std::string& sourceLang, const std::string& targetLang) const {
    return validateModelFiles(sourceLang, targetLang);
}

std::string MarianTranslator::getModelDownloadUrl(const std::string& sourceLang, const std::string& targetLang) const {
    // This would typically point to a model repository or download service
    return "https://models.speechrnt.com/marian/" + sourceLang + "-" + targetLang + "/latest";
}

size_t MarianTranslator::estimateModelSize(const std::string& sourceLang, const std::string& targetLang) const {
    // Rough estimates based on typical Marian model sizes
    static const std::unordered_map<std::string, size_t> languageComplexity = {
        {"en", 100}, {"es", 120}, {"fr", 130}, {"de", 140}, {"it", 110},
        {"pt", 115}, {"ru", 180}, {"zh", 200}, {"ja", 190}, {"ko", 185},
        {"ar", 170}, {"hi", 160}, {"th", 150}, {"vi", 140}
    };
    
    auto sourceIt = languageComplexity.find(sourceLang);
    auto targetIt = languageComplexity.find(targetLang);
    
    size_t baseSize = 150; // Base model size in MB
    if (sourceIt != languageComplexity.end()) {
        baseSize += sourceIt->second;
    }
    if (targetIt != languageComplexity.end()) {
        baseSize += targetIt->second;
    }
    
    return baseSize;
}

void MarianTranslator::initializeLanguagePairMappings() {
    // Initialize language name mappings
    languageNameMappings_ = {
        {"en", "English"}, {"es", "Spanish"}, {"fr", "French"}, {"de", "German"},
        {"it", "Italian"}, {"pt", "Portuguese"}, {"ru", "Russian"}, {"zh", "Chinese"},
        {"ja", "Japanese"}, {"ko", "Korean"}, {"ar", "Arabic"}, {"hi", "Hindi"},
        {"th", "Thai"}, {"vi", "Vietnamese"}, {"nl", "Dutch"}, {"sv", "Swedish"},
        {"da", "Danish"}, {"no", "Norwegian"}, {"fi", "Finnish"}, {"pl", "Polish"}
    };
    
    // Initialize all supported languages list
    allSupportedLanguages_ = supportedSourceLanguages_;
    
    // Add any additional target languages that might not be source languages
    for (const auto& sourcePair : supportedTargetLanguages_) {
        for (const auto& target : sourcePair.second) {
            if (std::find(allSupportedLanguages_.begin(), allSupportedLanguages_.end(), target) == allSupportedLanguages_.end()) {
                allSupportedLanguages_.push_back(target);
            }
        }
    }
    
    // Initialize model download information (this would typically be loaded from a configuration file)
    modelDownloadInfo_ = {
        {"en-es", {"marian-en-es", "https://models.speechrnt.com/marian/en-es/latest", "180 MB", "English to Spanish translation model", true}},
        {"es-en", {"marian-es-en", "https://models.speechrnt.com/marian/es-en/latest", "180 MB", "Spanish to English translation model", true}},
        {"en-fr", {"marian-en-fr", "https://models.speechrnt.com/marian/en-fr/latest", "190 MB", "English to French translation model", true}},
        {"fr-en", {"marian-fr-en", "https://models.speechrnt.com/marian/fr-en/latest", "190 MB", "French to English translation model", true}},
        {"en-de", {"marian-en-de", "https://models.speechrnt.com/marian/en-de/latest", "200 MB", "English to German translation model", true}},
        {"de-en", {"marian-de-en", "https://models.speechrnt.com/marian/de-en/latest", "200 MB", "German to English translation model", true}}
    };
    
    speechrnt::utils::Logger::info("Initialized language pair mappings for " + std::to_string(allSupportedLanguages_.size()) + " languages");
}

bool MarianTranslator::validateLanguageCode(const std::string& languageCode) const {
    // Basic validation: language code should be 2-3 characters, lowercase
    if (languageCode.length() < 2 || languageCode.length() > 3) {
        return false;
    }
    
    // Check if all characters are lowercase letters
    for (char c : languageCode) {
        if (!std::islower(c)) {
            return false;
        }
    }
    
    return true;
}

// Error handling and recovery methods

bool MarianTranslator::isInDegradedMode() const {
    if (errorHandler_) {
        return errorHandler_->isInDegradedMode();
    }
    return false;
}

ErrorStatistics MarianTranslator::getErrorStatistics() const {
    if (errorHandler_) {
        return errorHandler_->getErrorStatistics();
    }
    return ErrorStatistics();
}

bool MarianTranslator::forceExitDegradedMode() {
    if (errorHandler_) {
        bool success = errorHandler_->exitDegradedMode();
        if (success) {
            speechrnt::utils::Logger::info("Forced exit from degraded mode");
        } else {
            speechrnt::utils::Logger::error("Failed to force exit from degraded mode");
        }
        return success;
    }
    return false;
}

MarianErrorHandler::DegradedModeStatus MarianTranslator::getDegradedModeStatus() const {
    if (errorHandler_) {
        return errorHandler_->getDegradedModeStatus();
    }
    
    // Return empty status if no error handler
    MarianErrorHandler::DegradedModeStatus status;
    status.active = false;
    return status;
}

} // namespace mt
} // namespace speechrnt