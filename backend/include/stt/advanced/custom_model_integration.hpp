#pragma once

#include "models/model_manager.hpp"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <future>
#include <chrono>

namespace speechrnt {
namespace stt {
namespace advanced {

/**
 * Model validation result
 */
struct ModelValidationResult {
    bool isValid = false;
    std::string modelId;
    std::string modelPath;
    
    // Compatibility checks
    bool architectureCompatible = false;
    bool versionCompatible = false;
    bool dependenciesAvailable = false;
    bool formatSupported = false;
    
    // Safety checks
    bool integrityVerified = false;
    bool securityScanPassed = false;
    bool performanceAcceptable = false;
    
    // Detailed information
    std::string modelArchitecture;
    std::string modelVersion;
    std::string frameworkVersion;
    std::vector<std::string> requiredDependencies;
    std::vector<std::string> supportedLanguages;
    
    // Performance metrics from validation
    float validationAccuracy = 0.0f;
    float validationLatencyMs = 0.0f;
    size_t estimatedMemoryMB = 0;
    
    // Issues and warnings
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> recommendations;
    
    std::chrono::system_clock::time_point validatedAt;
    
    ModelValidationResult() {
        validatedAt = std::chrono::system_clock::now();
    }
};

/**
 * Model quantization configuration
 */
struct ModelQuantizationConfig {
    models::QuantizationType quantizationType = models::QuantizationType::NONE;
    
    // Quantization parameters
    float quantizationThreshold = 0.5f;
    bool preserveAccuracy = true;
    float maxAccuracyLoss = 0.05f; // 5% max accuracy loss
    
    // Optimization settings
    bool optimizeForSpeed = false;
    bool optimizeForMemory = true;
    bool enableDynamicQuantization = false;
    
    // Target hardware
    bool targetCPU = true;
    bool targetGPU = false;
    std::string targetDevice = "cpu";
    
    // Calibration dataset
    std::string calibrationDataPath;
    size_t calibrationSamples = 100;
};

/**
 * Model optimization result
 */
struct ModelOptimizationResult {
    bool successful = false;
    std::string originalModelPath;
    std::string optimizedModelPath;
    
    // Optimization metrics
    float sizeReductionPercentage = 0.0f;
    float speedImprovementPercentage = 0.0f;
    float accuracyChangePercentage = 0.0f;
    
    // Before/after comparison
    size_t originalSizeMB = 0;
    size_t optimizedSizeMB = 0;
    float originalLatencyMs = 0.0f;
    float optimizedLatencyMs = 0.0f;
    float originalAccuracy = 0.0f;
    float optimizedAccuracy = 0.0f;
    
    std::vector<std::string> appliedOptimizations;
    std::vector<std::string> optimizationWarnings;
    
    std::chrono::system_clock::time_point optimizedAt;
    
    ModelOptimizationResult() {
        optimizedAt = std::chrono::system_clock::now();
    }
};

/**
 * Model deployment configuration
 */
struct ModelDeploymentConfig {
    std::string modelId;
    std::string modelPath;
    std::string targetEnvironment = "production"; // "development", "staging", "production"
    
    // Deployment strategy
    enum class DeploymentStrategy {
        IMMEDIATE,      // Deploy immediately
        GRADUAL,        // Gradual rollout
        BLUE_GREEN,     // Blue-green deployment
        CANARY          // Canary deployment
    } strategy = DeploymentStrategy::GRADUAL;
    
    // Rollout configuration
    float initialTrafficPercentage = 10.0f;
    float trafficIncrementPercentage = 10.0f;
    std::chrono::minutes rolloutInterval{30};
    
    // Health checks
    bool enableHealthChecks = true;
    float healthCheckThreshold = 0.95f; // 95% success rate
    size_t healthCheckSamples = 50;
    
    // Rollback configuration
    bool enableAutoRollback = true;
    float rollbackThreshold = 0.8f; // Rollback if performance drops below 80%
    std::chrono::minutes rollbackTimeout{10};
    
    // Monitoring
    bool enableDetailedMonitoring = true;
    std::vector<std::string> monitoringMetrics = {"latency", "accuracy", "throughput", "error_rate"};
};

/**
 * Model deployment result
 */
struct ModelDeploymentResult {
    bool successful = false;
    std::string deploymentId;
    std::string modelId;
    
    // Deployment status
    enum class DeploymentStatus {
        PENDING,
        IN_PROGRESS,
        COMPLETED,
        FAILED,
        ROLLED_BACK
    } status = DeploymentStatus::PENDING;
    
    float currentTrafficPercentage = 0.0f;
    std::chrono::system_clock::time_point deploymentStarted;
    std::chrono::system_clock::time_point deploymentCompleted;
    
    // Health metrics during deployment
    float averageLatencyMs = 0.0f;
    float successRate = 0.0f;
    float errorRate = 0.0f;
    
    std::vector<std::string> deploymentLogs;
    std::vector<std::string> healthCheckResults;
    
    ModelDeploymentResult() {
        deploymentStarted = std::chrono::system_clock::now();
    }
};

/**
 * Custom Model Integration Pipeline
 * Handles validation, quantization, optimization, and deployment of custom models
 */
class CustomModelIntegration {
public:
    CustomModelIntegration(std::shared_ptr<models::ModelManager> modelManager);
    ~CustomModelIntegration();
    
    // Model validation
    
    /**
     * Validate a custom model for integration
     * @param modelPath Path to the model files
     * @param modelId Unique identifier for the model
     * @return Validation result with detailed information
     */
    ModelValidationResult validateModel(const std::string& modelPath, const std::string& modelId);
    
    /**
     * Validate model asynchronously
     * @param modelPath Path to the model files
     * @param modelId Unique identifier for the model
     * @return Future that resolves to validation result
     */
    std::future<ModelValidationResult> validateModelAsync(const std::string& modelPath, 
                                                          const std::string& modelId);
    
    /**
     * Check model compatibility with current system
     * @param modelPath Path to the model files
     * @return true if model is compatible
     */
    bool checkModelCompatibility(const std::string& modelPath);
    
    /**
     * Verify model integrity and security
     * @param modelPath Path to the model files
     * @return true if model passes security checks
     */
    bool verifyModelSecurity(const std::string& modelPath);
    
    /**
     * Run performance validation on model
     * @param modelPath Path to the model files
     * @param testDataPath Path to test dataset
     * @return Performance metrics from validation
     */
    std::pair<float, float> validateModelPerformance(const std::string& modelPath,
                                                     const std::string& testDataPath);
    
    // Model quantization and optimization
    
    /**
     * Quantize a model for optimization
     * @param modelPath Path to the original model
     * @param outputPath Path for the quantized model
     * @param config Quantization configuration
     * @return Optimization result
     */
    ModelOptimizationResult quantizeModel(const std::string& modelPath,
                                         const std::string& outputPath,
                                         const ModelQuantizationConfig& config);
    
    /**
     * Optimize model for target hardware
     * @param modelPath Path to the model
     * @param outputPath Path for the optimized model
     * @param targetDevice Target device ("cpu", "gpu", "tpu")
     * @return Optimization result
     */
    ModelOptimizationResult optimizeModelForHardware(const std::string& modelPath,
                                                     const std::string& outputPath,
                                                     const std::string& targetDevice);
    
    /**
     * Apply multiple optimizations to a model
     * @param modelPath Path to the original model
     * @param outputPath Path for the optimized model
     * @param optimizations List of optimizations to apply
     * @return Optimization result
     */
    ModelOptimizationResult applyModelOptimizations(const std::string& modelPath,
                                                    const std::string& outputPath,
                                                    const std::vector<std::string>& optimizations);
    
    // Model deployment
    
    /**
     * Deploy a validated model
     * @param modelPath Path to the model
     * @param modelId Model identifier
     * @param config Deployment configuration
     * @return Deployment result
     */
    ModelDeploymentResult deployModel(const std::string& modelPath,
                                     const std::string& modelId,
                                     const ModelDeploymentConfig& config);
    
    /**
     * Deploy model asynchronously
     * @param modelPath Path to the model
     * @param modelId Model identifier
     * @param config Deployment configuration
     * @return Future that resolves to deployment result
     */
    std::future<ModelDeploymentResult> deployModelAsync(const std::string& modelPath,
                                                        const std::string& modelId,
                                                        const ModelDeploymentConfig& config);
    
    /**
     * Get deployment status
     * @param deploymentId Deployment identifier
     * @return Current deployment result
     */
    ModelDeploymentResult getDeploymentStatus(const std::string& deploymentId);
    
    /**
     * Rollback a deployment
     * @param deploymentId Deployment identifier
     * @return true if rollback successful
     */
    bool rollbackDeployment(const std::string& deploymentId);
    
    /**
     * Cancel an ongoing deployment
     * @param deploymentId Deployment identifier
     * @return true if cancellation successful
     */
    bool cancelDeployment(const std::string& deploymentId);
    
    // Model lifecycle management
    
    /**
     * Create a model checkpoint for rollback
     * @param modelId Model identifier
     * @param checkpointName Checkpoint name
     * @return true if checkpoint created successfully
     */
    bool createModelCheckpoint(const std::string& modelId, const std::string& checkpointName);
    
    /**
     * List available model checkpoints
     * @param modelId Model identifier
     * @return Vector of checkpoint names
     */
    std::vector<std::string> listModelCheckpoints(const std::string& modelId);
    
    /**
     * Restore model from checkpoint
     * @param modelId Model identifier
     * @param checkpointName Checkpoint name
     * @return true if restore successful
     */
    bool restoreFromCheckpoint(const std::string& modelId, const std::string& checkpointName);
    
    /**
     * Delete a model checkpoint
     * @param modelId Model identifier
     * @param checkpointName Checkpoint name
     * @return true if deletion successful
     */
    bool deleteCheckpoint(const std::string& modelId, const std::string& checkpointName);
    
    // Configuration and callbacks
    
    /**
     * Set validation callback for custom validation logic
     * @param callback Function to call for custom validation
     */
    void setValidationCallback(std::function<bool(const std::string&)> callback);
    
    /**
     * Set deployment callback for deployment events
     * @param callback Function to call on deployment events
     */
    void setDeploymentCallback(std::function<void(const ModelDeploymentResult&)> callback);
    
    /**
     * Set security scan callback
     * @param callback Function to call for security scanning
     */
    void setSecurityCallback(std::function<bool(const std::string&)> callback);
    
    /**
     * Enable/disable automatic optimization
     * @param enabled Enable automatic optimization
     */
    void setAutoOptimization(bool enabled);
    
    /**
     * Set optimization preferences
     * @param optimizeForSpeed Prioritize speed optimization
     * @param optimizeForMemory Prioritize memory optimization
     * @param optimizeForAccuracy Prioritize accuracy preservation
     */
    void setOptimizationPreferences(bool optimizeForSpeed, bool optimizeForMemory, bool optimizeForAccuracy);
    
    /**
     * Get integration statistics
     * @return Map of statistics
     */
    std::unordered_map<std::string, std::string> getIntegrationStats() const;

private:
    // Base model manager
    std::shared_ptr<models::ModelManager> modelManager_;
    
    // Validation state
    mutable std::mutex validationMutex_;
    std::unordered_map<std::string, ModelValidationResult> validationResults_;
    
    // Deployment state
    mutable std::mutex deploymentMutex_;
    std::unordered_map<std::string, ModelDeploymentResult> activeDeployments_;
    std::unordered_map<std::string, ModelDeploymentResult> completedDeployments_;
    
    // Checkpoints
    mutable std::mutex checkpointMutex_;
    std::unordered_map<std::string, std::vector<std::string>> modelCheckpoints_;
    
    // Configuration
    std::atomic<bool> autoOptimizationEnabled_{true};
    std::atomic<bool> optimizeForSpeed_{false};
    std::atomic<bool> optimizeForMemory_{true};
    std::atomic<bool> optimizeForAccuracy_{true};
    
    // Callbacks
    std::function<bool(const std::string&)> validationCallback_;
    std::function<void(const ModelDeploymentResult&)> deploymentCallback_;
    std::function<bool(const std::string&)> securityCallback_;
    
    // Statistics
    mutable std::mutex statsMutex_;
    size_t totalValidations_ = 0;
    size_t successfulValidations_ = 0;
    size_t totalDeployments_ = 0;
    size_t successfulDeployments_ = 0;
    size_t totalOptimizations_ = 0;
    size_t successfulOptimizations_ = 0;
    
    // Background processing
    std::atomic<bool> backgroundProcessingEnabled_{true};
    std::thread backgroundThread_;
    
    // Private methods
    bool validateModelFormat(const std::string& modelPath);
    bool validateModelArchitecture(const std::string& modelPath);
    bool validateModelDependencies(const std::string& modelPath);
    std::string detectModelArchitecture(const std::string& modelPath);
    std::string detectModelVersion(const std::string& modelPath);
    std::vector<std::string> extractModelDependencies(const std::string& modelPath);
    std::vector<std::string> extractSupportedLanguages(const std::string& modelPath);
    bool runSecurityScan(const std::string& modelPath);
    std::string generateDeploymentId();
    void processDeploymentQueue();
    void monitorActiveDeployments();
    void backgroundProcessingLoop();
    bool performGradualRollout(const std::string& deploymentId);
    bool performHealthCheck(const std::string& modelId);
    void updateDeploymentMetrics(const std::string& deploymentId);
    std::string createModelBackup(const std::string& modelPath);
    bool restoreModelBackup(const std::string& backupPath, const std::string& targetPath);
};

} // namespace advanced
} // namespace stt
} // namespace speechrnt