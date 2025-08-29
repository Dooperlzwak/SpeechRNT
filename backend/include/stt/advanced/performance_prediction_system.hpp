#pragma once

#include "stt/advanced/adaptive_quality_manager_interface.hpp"
#include <memory>
#include <vector>
#include <map>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>

namespace stt {
namespace advanced {

/**
 * Performance benchmark data
 */
struct BenchmarkResult {
    QualitySettings settings;
    SystemResources systemState;
    size_t audioLength;
    float actualLatency;
    float actualAccuracy;
    float cpuUtilization;
    float memoryUtilization;
    float gpuUtilization;
    std::chrono::steady_clock::time_point timestamp;
    std::string audioCharacteristics; // JSON string with audio properties
    
    BenchmarkResult() 
        : audioLength(0)
        , actualLatency(0.0f)
        , actualAccuracy(0.0f)
        , cpuUtilization(0.0f)
        , memoryUtilization(0.0f)
        , gpuUtilization(0.0f)
        , timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * Performance calibration data
 */
struct CalibrationData {
    std::map<QualityLevel, float> latencyBaselines;
    std::map<QualityLevel, float> accuracyBaselines;
    std::map<QualityLevel, float> resourceUsageBaselines;
    float systemPerformanceScore;
    std::chrono::steady_clock::time_point lastCalibration;
    bool isCalibrated;
    
    CalibrationData() 
        : systemPerformanceScore(1.0f)
        , lastCalibration(std::chrono::steady_clock::now())
        , isCalibrated(false) {}
};

/**
 * Request pattern analysis
 */
struct RequestPattern {
    float averageRequestRate; // requests per second
    float peakRequestRate;
    std::vector<size_t> commonAudioLengths;
    std::map<QualityLevel, float> qualityDistribution;
    std::vector<std::string> commonLanguages;
    float averageConcurrency;
    std::chrono::steady_clock::time_point analysisTime;
    
    RequestPattern()
        : averageRequestRate(0.0f)
        , peakRequestRate(0.0f)
        , averageConcurrency(1.0f)
        , analysisTime(std::chrono::steady_clock::now()) {}
};

/**
 * Performance optimization recommendation
 */
struct OptimizationRecommendation {
    enum class RecommendationType {
        QUALITY_ADJUSTMENT,
        RESOURCE_ALLOCATION,
        CONFIGURATION_CHANGE,
        HARDWARE_UPGRADE,
        LOAD_BALANCING
    };
    
    RecommendationType type;
    std::string description;
    float expectedImprovement; // 0.0 to 1.0
    float implementationCost; // 0.0 to 1.0 (complexity/effort)
    std::map<std::string, std::string> parameters;
    float confidence; // 0.0 to 1.0
    
    OptimizationRecommendation()
        : type(RecommendationType::QUALITY_ADJUSTMENT)
        , expectedImprovement(0.0f)
        , implementationCost(0.0f)
        , confidence(0.0f) {}
};

/**
 * Advanced performance predictor with machine learning capabilities
 */
class AdvancedPerformancePredictor {
public:
    AdvancedPerformancePredictor();
    ~AdvancedPerformancePredictor();
    
    /**
     * Initialize the advanced predictor
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * Predict performance with enhanced accuracy
     * @param settings Quality settings
     * @param resources System resources
     * @param audioLength Audio length in samples
     * @param audioCharacteristics Audio properties (JSON)
     * @return Enhanced performance prediction
     */
    PerformancePrediction predictPerformanceAdvanced(const QualitySettings& settings,
                                                    const SystemResources& resources,
                                                    size_t audioLength,
                                                    const std::string& audioCharacteristics = "");
    
    /**
     * Update predictor with benchmark results
     * @param result Benchmark result data
     */
    void updateWithBenchmarkResult(const BenchmarkResult& result);
    
    /**
     * Get performance recommendations
     * @param currentSettings Current quality settings
     * @param resources Current system resources
     * @param requestPattern Current request patterns
     * @return Vector of optimization recommendations
     */
    std::vector<OptimizationRecommendation> getOptimizationRecommendations(
        const QualitySettings& currentSettings,
        const SystemResources& resources,
        const RequestPattern& requestPattern);
    
    /**
     * Predict performance for multiple scenarios
     * @param scenarios Vector of quality settings to evaluate
     * @param resources Current system resources
     * @param audioLength Audio length in samples
     * @return Vector of predictions for each scenario
     */
    std::vector<PerformancePrediction> predictMultipleScenarios(
        const std::vector<QualitySettings>& scenarios,
        const SystemResources& resources,
        size_t audioLength);
    
    /**
     * Get calibration data
     * @return Current calibration data
     */
    CalibrationData getCalibrationData() const;
    
    /**
     * Force recalibration
     * @return true if recalibration successful
     */
    bool recalibrate();
    
    /**
     * Get prediction accuracy statistics
     * @return JSON string with accuracy metrics
     */
    std::string getPredictionAccuracyStats() const;
    
    /**
     * Enable/disable learning mode
     * @param enabled true to enable continuous learning
     */
    void setLearningMode(bool enabled);
    
    /**
     * Export prediction model
     * @return Serialized model data
     */
    std::string exportModel() const;
    
    /**
     * Import prediction model
     * @param modelData Serialized model data
     * @return true if import successful
     */
    bool importModel(const std::string& modelData);
    
    /**
     * Check if predictor is initialized
     * @return true if initialized
     */
    bool isInitialized() const;

private:
    struct PredictionModel {
        // Linear regression coefficients
        std::map<std::string, float> latencyCoefficients;
        std::map<std::string, float> accuracyCoefficients;
        
        // Neural network weights (simplified)
        std::vector<std::vector<float>> hiddenWeights;
        std::vector<float> outputWeights;
        
        // Feature scaling parameters
        std::map<std::string, std::pair<float, float>> featureScaling; // min, max
        
        // Model metadata
        size_t trainingDataSize;
        float modelAccuracy;
        std::chrono::steady_clock::time_point lastTraining;
        
        PredictionModel() 
            : trainingDataSize(0)
            , modelAccuracy(0.0f)
            , lastTraining(std::chrono::steady_clock::now()) {}
    };
    
    void trainModel();
    void updateLinearModel();
    void updateNeuralNetwork();
    std::vector<float> extractFeatures(const QualitySettings& settings,
                                      const SystemResources& resources,
                                      size_t audioLength,
                                      const std::string& audioCharacteristics);
    float predictWithLinearModel(const std::vector<float>& features, bool isLatency);
    float predictWithNeuralNetwork(const std::vector<float>& features, bool isLatency);
    std::vector<float> normalizeFeatures(const std::vector<float>& features);
    void updateFeatureScaling(const std::vector<float>& features);
    float calculatePredictionConfidence(const std::vector<float>& features);
    
    mutable std::mutex predictorMutex_;
    std::atomic<bool> initialized_;
    std::atomic<bool> learningMode_;
    
    // Prediction models
    PredictionModel linearModel_;
    PredictionModel neuralModel_;
    bool useNeuralNetwork_;
    
    // Training data
    std::vector<BenchmarkResult> trainingData_;
    static const size_t MAX_TRAINING_DATA = 1000;
    
    // Calibration data
    CalibrationData calibrationData_;
    
    // Prediction accuracy tracking
    struct PredictionAccuracy {
        float latencyMeanError;
        float latencyStdError;
        float accuracyMeanError;
        float accuracyStdError;
        size_t totalPredictions;
        size_t correctPredictions;
        
        PredictionAccuracy()
            : latencyMeanError(0.0f)
            , latencyStdError(0.0f)
            , accuracyMeanError(0.0f)
            , accuracyStdError(0.0f)
            , totalPredictions(0)
            , correctPredictions(0) {}
    };
    
    PredictionAccuracy accuracyStats_;
    
    // Model training thread
    std::thread trainingThread_;
    std::atomic<bool> trainingActive_;
    std::chrono::steady_clock::time_point lastTraining_;
};

/**
 * Performance benchmarking system
 */
class PerformanceBenchmarkSystem {
public:
    PerformanceBenchmarkSystem();
    ~PerformanceBenchmarkSystem();
    
    /**
     * Initialize benchmarking system
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * Run comprehensive performance benchmark
     * @param testAudioPath Path to test audio file
     * @return Vector of benchmark results
     */
    std::vector<BenchmarkResult> runComprehensiveBenchmark(const std::string& testAudioPath = "");
    
    /**
     * Run quick performance calibration
     * @return Calibration data
     */
    CalibrationData runQuickCalibration();
    
    /**
     * Benchmark specific quality settings
     * @param settings Quality settings to benchmark
     * @param audioData Audio data for testing
     * @return Benchmark result
     */
    BenchmarkResult benchmarkQualitySettings(const QualitySettings& settings,
                                            const std::vector<float>& audioData);
    
    /**
     * Run stress test
     * @param concurrentRequests Number of concurrent requests
     * @param durationSeconds Test duration in seconds
     * @return Stress test results
     */
    std::vector<BenchmarkResult> runStressTest(size_t concurrentRequests, int durationSeconds);
    
    /**
     * Generate synthetic test audio
     * @param lengthSeconds Audio length in seconds
     * @param characteristics Audio characteristics (noise, speech rate, etc.)
     * @return Generated audio data
     */
    std::vector<float> generateTestAudio(float lengthSeconds, 
                                        const std::map<std::string, float>& characteristics = {});
    
    /**
     * Get benchmark history
     * @param samples Number of historical samples
     * @return Vector of benchmark results
     */
    std::vector<BenchmarkResult> getBenchmarkHistory(size_t samples) const;
    
    /**
     * Export benchmark results
     * @param format Export format ("json", "csv")
     * @return Exported data as string
     */
    std::string exportBenchmarkResults(const std::string& format = "json") const;
    
    /**
     * Set benchmark configuration
     * @param config Benchmark configuration parameters
     */
    void setBenchmarkConfig(const std::map<std::string, std::string>& config);
    
    /**
     * Check if system is initialized
     * @return true if initialized
     */
    bool isInitialized() const;

private:
    void runSingleBenchmark(const QualitySettings& settings,
                           const std::vector<float>& audioData,
                           BenchmarkResult& result);
    SystemResources captureSystemState();
    std::string analyzeAudioCharacteristics(const std::vector<float>& audioData);
    std::vector<QualitySettings> generateBenchmarkScenarios();
    
    mutable std::mutex benchmarkMutex_;
    std::atomic<bool> initialized_;
    
    // Benchmark history
    std::vector<BenchmarkResult> benchmarkHistory_;
    static const size_t MAX_BENCHMARK_HISTORY = 500;
    
    // Configuration
    std::map<std::string, std::string> benchmarkConfig_;
    
    // Test audio samples
    std::map<std::string, std::vector<float>> testAudioSamples_;
};

/**
 * Request pattern analyzer
 */
class RequestPatternAnalyzer {
public:
    RequestPatternAnalyzer();
    ~RequestPatternAnalyzer();
    
    /**
     * Initialize pattern analyzer
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * Record transcription request
     * @param request Transcription request data
     */
    void recordRequest(const TranscriptionRequest& request);
    
    /**
     * Analyze current request patterns
     * @return Current request pattern analysis
     */
    RequestPattern analyzeCurrentPattern();
    
    /**
     * Predict future request load
     * @param timeHorizonMinutes Prediction time horizon in minutes
     * @return Predicted request pattern
     */
    RequestPattern predictFutureLoad(int timeHorizonMinutes);
    
    /**
     * Get request statistics
     * @return JSON string with request statistics
     */
    std::string getRequestStatistics() const;
    
    /**
     * Reset pattern analysis
     */
    void reset();
    
    /**
     * Check if analyzer is initialized
     * @return true if initialized
     */
    bool isInitialized() const;

private:
    struct RequestRecord {
        TranscriptionRequest request;
        std::chrono::steady_clock::time_point timestamp;
        
        RequestRecord(const TranscriptionRequest& req)
            : request(req), timestamp(std::chrono::steady_clock::now()) {}
    };
    
    void cleanupOldRecords();
    float calculateRequestRate(const std::vector<RequestRecord>& records, int windowMinutes);
    
    mutable std::mutex analyzerMutex_;
    std::atomic<bool> initialized_;
    
    // Request history
    std::vector<RequestRecord> requestHistory_;
    static const size_t MAX_REQUEST_HISTORY = 10000;
    
    // Analysis parameters
    static const int ANALYSIS_WINDOW_MINUTES = 60;
    static const int CLEANUP_INTERVAL_MINUTES = 10;
    
    std::chrono::steady_clock::time_point lastCleanup_;
};

/**
 * Integrated performance prediction system
 */
class PerformancePredictionSystem {
public:
    PerformancePredictionSystem();
    ~PerformancePredictionSystem();
    
    /**
     * Initialize the complete prediction system
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * Get comprehensive performance prediction
     * @param settings Quality settings
     * @param resources System resources
     * @param audioLength Audio length
     * @param audioCharacteristics Audio characteristics
     * @return Enhanced performance prediction
     */
    PerformancePrediction getComprehensivePrediction(const QualitySettings& settings,
                                                    const SystemResources& resources,
                                                    size_t audioLength,
                                                    const std::string& audioCharacteristics = "");
    
    /**
     * Get optimization recommendations
     * @param currentSettings Current quality settings
     * @param resources Current system resources
     * @return Vector of optimization recommendations
     */
    std::vector<OptimizationRecommendation> getOptimizationRecommendations(
        const QualitySettings& currentSettings,
        const SystemResources& resources);
    
    /**
     * Record actual performance for learning
     * @param settings Settings used
     * @param resources System resources during processing
     * @param audioLength Audio length processed
     * @param actualLatency Actual processing latency
     * @param actualAccuracy Actual transcription accuracy
     * @param audioCharacteristics Audio characteristics
     */
    void recordActualPerformance(const QualitySettings& settings,
                                const SystemResources& resources,
                                size_t audioLength,
                                float actualLatency,
                                float actualAccuracy,
                                const std::string& audioCharacteristics = "");
    
    /**
     * Run system calibration
     * @return true if calibration successful
     */
    bool runCalibration();
    
    /**
     * Run performance benchmark
     * @return Vector of benchmark results
     */
    std::vector<BenchmarkResult> runBenchmark();
    
    /**
     * Get system performance statistics
     * @return JSON string with performance statistics
     */
    std::string getPerformanceStatistics() const;
    
    /**
     * Export prediction models
     * @return Serialized model data
     */
    std::string exportModels() const;
    
    /**
     * Import prediction models
     * @param modelData Serialized model data
     * @return true if import successful
     */
    bool importModels(const std::string& modelData);
    
    /**
     * Check if system is initialized
     * @return true if initialized
     */
    bool isInitialized() const;

private:
    std::unique_ptr<AdvancedPerformancePredictor> predictor_;
    std::unique_ptr<PerformanceBenchmarkSystem> benchmarkSystem_;
    std::unique_ptr<RequestPatternAnalyzer> patternAnalyzer_;
    
    mutable std::mutex systemMutex_;
    std::atomic<bool> initialized_;
};

} // namespace advanced
} // namespace stt