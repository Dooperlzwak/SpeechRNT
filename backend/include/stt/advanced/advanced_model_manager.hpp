#pragma once

#include "models/model_manager.hpp"
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>
#include <functional>
#include <atomic>
#include <future>

namespace speechrnt {
namespace stt {
namespace advanced {

/**
 * Model performance metrics structure
 */
struct ModelPerformanceMetrics {
    std::string modelId;
    std::string languagePair;
    
    // Accuracy metrics
    float wordErrorRate = 0.0f;
    float characterErrorRate = 0.0f;
    float confidenceScore = 0.0f;
    
    // Performance metrics
    float averageLatencyMs = 0.0f;
    float throughputWordsPerSecond = 0.0f;
    size_t memoryUsageMB = 0;
    float cpuUtilization = 0.0f;
    float gpuUtilization = 0.0f;
    
    // Usage statistics
    size_t totalTranscriptions = 0;
    size_t successfulTranscriptions = 0;
    size_t failedTranscriptions = 0;
    std::chrono::system_clock::time_point lastUsed;
    std::chrono::system_clock::time_point firstUsed;
    
    // Quality metrics
    float audioQualityScore = 0.0f;
    float transcriptionQualityScore = 0.0f;
    
    ModelPerformanceMetrics() {
        auto now = std::chrono::system_clock::now();
        lastUsed = now;
        firstUsed = now;
    }
};

/**
 * A/B testing configuration
 */
struct ABTestConfig {
    std::string testId;
    std::string testName;
    std::string description;
    
    // Models being tested
    std::vector<std::string> modelIds;
    std::vector<float> trafficSplitPercentages; // Must sum to 100.0
    
    // Test criteria
    std::string primaryMetric = "wordErrorRate"; // Primary metric to optimize
    float significanceThreshold = 0.05f;
    size_t minimumSampleSize = 100;
    std::chrono::hours testDuration{24};
    
    // Test status
    bool active = false;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    
    ABTestConfig() {
        auto now = std::chrono::system_clock::now();
        startTime = now;
        endTime = now + testDuration;
    }
};

/**
 * A/B test results
 */
struct ABTestResults {
    std::string testId;
    std::string winningModelId;
    float confidenceLevel = 0.0f;
    bool statisticallySignificant = false;
    
    std::unordered_map<std::string, ModelPerformanceMetrics> modelResults;
    std::unordered_map<std::string, float> metricComparisons;
    
    std::string recommendation;
    std::chrono::system_clock::time_point completedAt;
};

/**
 * Model comparison criteria
 */
enum class ModelComparisonMetric {
    WORD_ERROR_RATE,
    CHARACTER_ERROR_RATE,
    AVERAGE_LATENCY,
    THROUGHPUT,
    MEMORY_USAGE,
    CPU_UTILIZATION,
    GPU_UTILIZATION,
    CONFIDENCE_SCORE,
    TRANSCRIPTION_QUALITY,
    OVERALL_SCORE
};

/**
 * Model selection criteria
 */
struct ModelSelectionCriteria {
    ModelComparisonMetric primaryMetric = ModelComparisonMetric::WORD_ERROR_RATE;
    std::vector<ModelComparisonMetric> secondaryMetrics;
    
    // Thresholds
    float maxAcceptableLatencyMs = 1000.0f;
    float minAcceptableConfidence = 0.8f;
    float maxAcceptableMemoryMB = 2048.0f;
    float maxAcceptableCpuUtilization = 0.8f;
    
    // Weights for composite scoring
    std::unordered_map<ModelComparisonMetric, float> metricWeights;
    
    ModelSelectionCriteria() {
        // Default weights
        metricWeights[ModelComparisonMetric::WORD_ERROR_RATE] = 0.4f;
        metricWeights[ModelComparisonMetric::AVERAGE_LATENCY] = 0.3f;
        metricWeights[ModelComparisonMetric::CONFIDENCE_SCORE] = 0.2f;
        metricWeights[ModelComparisonMetric::MEMORY_USAGE] = 0.1f;
    }
};

/**
 * Advanced Model Manager with A/B testing and performance analytics
 */
class AdvancedModelManager {
public:
    AdvancedModelManager(std::shared_ptr<models::ModelManager> baseModelManager);
    ~AdvancedModelManager();
    
    // Model performance analytics
    
    /**
     * Record transcription metrics for a model
     * @param modelId Model identifier
     * @param languagePair Language pair (e.g., "en->es")
     * @param latencyMs Transcription latency in milliseconds
     * @param wordErrorRate Word error rate (0.0 to 1.0)
     * @param confidenceScore Confidence score (0.0 to 1.0)
     * @param audioQualityScore Audio quality score (0.0 to 1.0)
     * @param success Whether transcription was successful
     */
    void recordTranscriptionMetrics(const std::string& modelId,
                                   const std::string& languagePair,
                                   float latencyMs,
                                   float wordErrorRate,
                                   float confidenceScore,
                                   float audioQualityScore,
                                   bool success);
    
    /**
     * Get performance metrics for a specific model
     * @param modelId Model identifier
     * @param languagePair Language pair
     * @return Performance metrics, empty if not found
     */
    ModelPerformanceMetrics getModelMetrics(const std::string& modelId,
                                           const std::string& languagePair) const;
    
    /**
     * Get performance metrics for all models
     * @return Map of model IDs to performance metrics
     */
    std::unordered_map<std::string, ModelPerformanceMetrics> getAllModelMetrics() const;
    
    /**
     * Compare performance between two models
     * @param modelId1 First model identifier
     * @param modelId2 Second model identifier
     * @param languagePair Language pair
     * @param metric Comparison metric
     * @return Comparison result (positive if model1 is better, negative if model2 is better)
     */
    float compareModels(const std::string& modelId1,
                       const std::string& modelId2,
                       const std::string& languagePair,
                       ModelComparisonMetric metric) const;
    
    /**
     * Get model rankings for a language pair
     * @param languagePair Language pair
     * @param metric Ranking metric
     * @return Vector of model IDs ranked by performance (best first)
     */
    std::vector<std::string> rankModels(const std::string& languagePair,
                                       ModelComparisonMetric metric) const;
    
    /**
     * Select best model based on criteria
     * @param languagePair Language pair
     * @param criteria Selection criteria
     * @return Best model ID, empty if none meet criteria
     */
    std::string selectBestModel(const std::string& languagePair,
                               const ModelSelectionCriteria& criteria) const;
    
    /**
     * Generate performance report
     * @param languagePair Language pair (empty for all)
     * @param timeRange Time range in hours (0 for all time)
     * @return JSON formatted performance report
     */
    std::string generatePerformanceReport(const std::string& languagePair = "",
                                         int timeRangeHours = 0) const;
    
    // A/B Testing functionality
    
    /**
     * Create a new A/B test
     * @param config A/B test configuration
     * @return true if test created successfully
     */
    bool createABTest(const ABTestConfig& config);
    
    /**
     * Start an A/B test
     * @param testId Test identifier
     * @return true if test started successfully
     */
    bool startABTest(const std::string& testId);
    
    /**
     * Stop an A/B test
     * @param testId Test identifier
     * @return true if test stopped successfully
     */
    bool stopABTest(const std::string& testId);
    
    /**
     * Get model for transcription based on A/B test configuration
     * @param languagePair Language pair
     * @param sessionId Session identifier for consistent model assignment
     * @return Model ID to use for transcription
     */
    std::string getModelForTranscription(const std::string& languagePair,
                                        const std::string& sessionId = "");
    
    /**
     * Get A/B test results
     * @param testId Test identifier
     * @return Test results, empty if test not found or not completed
     */
    ABTestResults getABTestResults(const std::string& testId) const;
    
    /**
     * Get all active A/B tests
     * @return Vector of active test configurations
     */
    std::vector<ABTestConfig> getActiveABTests() const;
    
    /**
     * Get all completed A/B tests
     * @return Vector of completed test results
     */
    std::vector<ABTestResults> getCompletedABTests() const;
    
    /**
     * Check if A/B test is statistically significant
     * @param testId Test identifier
     * @return true if test has reached statistical significance
     */
    bool isABTestSignificant(const std::string& testId) const;
    
    // Model rollback functionality
    
    /**
     * Create model checkpoint for rollback
     * @param modelId Model identifier
     * @param languagePair Language pair
     * @param checkpointName Checkpoint name
     * @return true if checkpoint created successfully
     */
    bool createModelCheckpoint(const std::string& modelId,
                              const std::string& languagePair,
                              const std::string& checkpointName);
    
    /**
     * Rollback model to previous checkpoint
     * @param modelId Model identifier
     * @param languagePair Language pair
     * @param checkpointName Checkpoint name (empty for latest)
     * @return true if rollback successful
     */
    bool rollbackModel(const std::string& modelId,
                      const std::string& languagePair,
                      const std::string& checkpointName = "");
    
    /**
     * Detect performance degradation
     * @param modelId Model identifier
     * @param languagePair Language pair
     * @param thresholdPercentage Degradation threshold (e.g., 10.0 for 10%)
     * @return true if performance degradation detected
     */
    bool detectPerformanceDegradation(const std::string& modelId,
                                     const std::string& languagePair,
                                     float thresholdPercentage = 10.0f) const;
    
    /**
     * Auto-rollback on performance degradation
     * @param enabled Enable/disable auto-rollback
     * @param thresholdPercentage Degradation threshold for auto-rollback
     */
    void setAutoRollback(bool enabled, float thresholdPercentage = 15.0f);
    
    // Configuration and management
    
    /**
     * Set performance monitoring callback
     * @param callback Function to call when performance metrics are updated
     */
    void setPerformanceCallback(std::function<void(const ModelPerformanceMetrics&)> callback);
    
    /**
     * Set A/B test completion callback
     * @param callback Function to call when A/B test completes
     */
    void setABTestCallback(std::function<void(const ABTestResults&)> callback);
    
    /**
     * Enable/disable detailed metrics collection
     * @param enabled Enable detailed metrics
     */
    void setDetailedMetrics(bool enabled);
    
    /**
     * Set metrics retention period
     * @param hours Number of hours to retain metrics
     */
    void setMetricsRetention(int hours);
    
    /**
     * Clear old metrics data
     */
    void clearOldMetrics();
    
    /**
     * Export metrics to file
     * @param filePath Output file path
     * @param format Export format ("json", "csv")
     * @return true if export successful
     */
    bool exportMetrics(const std::string& filePath, const std::string& format = "json") const;
    
    /**
     * Import metrics from file
     * @param filePath Input file path
     * @return true if import successful
     */
    bool importMetrics(const std::string& filePath);

private:
    // Base model manager
    std::shared_ptr<models::ModelManager> baseModelManager_;
    
    // Performance metrics storage
    mutable std::mutex metricsMutex_;
    std::unordered_map<std::string, ModelPerformanceMetrics> modelMetrics_;
    
    // A/B testing
    mutable std::mutex abTestMutex_;
    std::unordered_map<std::string, ABTestConfig> activeABTests_;
    std::unordered_map<std::string, ABTestResults> completedABTests_;
    std::unordered_map<std::string, std::string> sessionModelAssignments_;
    
    // Model checkpoints for rollback
    mutable std::mutex checkpointMutex_;
    std::unordered_map<std::string, std::vector<std::string>> modelCheckpoints_;
    
    // Configuration
    std::atomic<bool> detailedMetricsEnabled_{true};
    std::atomic<bool> autoRollbackEnabled_{false};
    std::atomic<float> autoRollbackThreshold_{15.0f};
    std::atomic<int> metricsRetentionHours_{168}; // 1 week default
    
    // Callbacks
    std::function<void(const ModelPerformanceMetrics&)> performanceCallback_;
    std::function<void(const ABTestResults&)> abTestCallback_;
    
    // Background processing
    std::atomic<bool> backgroundProcessingEnabled_{true};
    std::thread backgroundThread_;
    
    // Private methods
    std::string getMetricsKey(const std::string& modelId, const std::string& languagePair) const;
    void updateModelMetrics(const std::string& key, const ModelPerformanceMetrics& metrics);
    float calculateCompositeScore(const ModelPerformanceMetrics& metrics,
                                 const ModelSelectionCriteria& criteria) const;
    bool isMetricBetter(float value1, float value2, ModelComparisonMetric metric) const;
    std::string assignModelForSession(const std::string& languagePair, const std::string& sessionId);
    void processABTestResults();
    void checkPerformanceDegradation();
    void backgroundProcessingLoop();
    float calculateStatisticalSignificance(const std::vector<float>& group1,
                                          const std::vector<float>& group2) const;
    void cleanupOldMetrics();
    std::string formatMetricsAsJson(const std::unordered_map<std::string, ModelPerformanceMetrics>& metrics) const;
    std::string formatMetricsAsCsv(const std::unordered_map<std::string, ModelPerformanceMetrics>& metrics) const;
    bool parseMetricsFromJson(const std::string& jsonData);
};

} // namespace advanced
} // namespace stt
} // namespace speechrnt