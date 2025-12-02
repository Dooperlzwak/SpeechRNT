#include "stt/advanced/performance_prediction_system.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>
#include <fstream>

namespace stt {
namespace advanced {

// AdvancedPerformancePredictor implementation
AdvancedPerformancePredictor::AdvancedPerformancePredictor()
    : initialized_(false)
    , learningMode_(true)
    , useNeuralNetwork_(false)
    , trainingActive_(false)
    , lastTraining_(std::chrono::steady_clock::now()) {
}

AdvancedPerformancePredictor::~AdvancedPerformancePredictor() {
    trainingActive_ = false;
    if (trainingThread_.joinable()) {
        trainingThread_.join();
    }
}

bool AdvancedPerformancePredictor::initialize() {
    std::lock_guard<std::mutex> lock(predictorMutex_);
    
    try {
        // Initialize linear model with default coefficients
        linearModel_.latencyCoefficients = {
            {"cpu_usage", 500.0f},
            {"memory_usage", 200.0f},
            {"gpu_usage", -100.0f},
            {"quality_level", 300.0f},
            {"thread_count", -50.0f},
            {"audio_length", 0.1f},
            {"enable_gpu", -200.0f},
            {"enable_preprocessing", 100.0f},
            {"buffer_size", 0.01f},
            {"base", 100.0f}
        };
        
        linearModel_.accuracyCoefficients = {
            {"cpu_usage", -0.1f},
            {"memory_usage", -0.05f},
            {"gpu_usage", 0.02f},
            {"quality_level", 0.15f},
            {"thread_count", 0.01f},
            {"audio_length", 0.0f},
            {"enable_gpu", 0.05f},
            {"enable_preprocessing", 0.03f},
            {"buffer_size", 0.0f},
            {"base", 0.75f}
        };
        
        // Initialize feature scaling parameters
        linearModel_.featureScaling = {
            {"cpu_usage", {0.0f, 1.0f}},
            {"memory_usage", {0.0f, 1.0f}},
            {"gpu_usage", {0.0f, 1.0f}},
            {"quality_level", {0.0f, 4.0f}},
            {"thread_count", {1.0f, 8.0f}},
            {"audio_length", {0.0f, 160000.0f}},
            {"enable_gpu", {0.0f, 1.0f}},
            {"enable_preprocessing", {0.0f, 1.0f}},
            {"buffer_size", {256.0f, 4096.0f}}
        };
        
        initialized_ = true;
        speechrnt::utils::Logger::info("AdvancedPerformancePredictor initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to initialize AdvancedPerformancePredictor: " + std::string(e.what()));
        return false;
    }
}Perfo
rmancePrediction AdvancedPerformancePredictor::predictPerformanceAdvanced(
    const QualitySettings& settings,
    const SystemResources& resources,
    size_t audioLength,
    const std::string& audioCharacteristics) {
    
    if (!initialized_) {
        return PerformancePrediction{};
    }
    
    std::lock_guard<std::mutex> lock(predictorMutex_);
    
    PerformancePrediction prediction;
    
    try {
        // Extract features
        std::vector<float> features = extractFeatures(settings, resources, audioLength, audioCharacteristics);
        std::vector<float> normalizedFeatures = normalizeFeatures(features);
        
        // Predict using linear model (simplified implementation)
        prediction.predictedLatencyMs = predictWithLinearModel(normalizedFeatures, true);
        prediction.predictedAccuracy = predictWithLinearModel(normalizedFeatures, false);
        
        // Calculate confidence
        prediction.confidenceInPrediction = calculatePredictionConfidence(normalizedFeatures);
        
        // Determine recommended quality
        prediction.recommendedQuality = settings.level;
        
        // Adjust recommendation based on prediction
        if (prediction.predictedLatencyMs > 2000.0f && prediction.recommendedQuality > QualityLevel::ULTRA_LOW) {
            prediction.recommendedQuality = static_cast<QualityLevel>(
                static_cast<int>(prediction.recommendedQuality) - 1);
            prediction.reasoning = "Reduced quality to meet latency requirements";
        } else if (prediction.predictedLatencyMs < 500.0f && prediction.recommendedQuality < QualityLevel::ULTRA_HIGH) {
            prediction.recommendedQuality = static_cast<QualityLevel>(
                static_cast<int>(prediction.recommendedQuality) + 1);
            prediction.reasoning = "Increased quality due to available performance headroom";
        } else {
            prediction.reasoning = "Current quality level is optimal for predicted performance";
        }
        
        // Clamp values to reasonable ranges
        prediction.predictedLatencyMs = std::clamp(prediction.predictedLatencyMs, 50.0f, 10000.0f);
        prediction.predictedAccuracy = std::clamp(prediction.predictedAccuracy, 0.3f, 0.99f);
        prediction.confidenceInPrediction = std::clamp(prediction.confidenceInPrediction, 0.1f, 0.95f);
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Prediction failed: " + std::string(e.what()));
        // Return conservative prediction
        prediction.predictedLatencyMs = 1500.0f;
        prediction.predictedAccuracy = 0.8f;
        prediction.confidenceInPrediction = 0.5f;
        prediction.recommendedQuality = QualityLevel::MEDIUM;
        prediction.reasoning = "Conservative prediction due to error";
    }
    
    return prediction;
}

void AdvancedPerformancePredictor::updateWithBenchmarkResult(const BenchmarkResult& result) {
    if (!initialized_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(predictorMutex_);
    
    // Add to training data
    trainingData_.push_back(result);
    if (trainingData_.size() > MAX_TRAINING_DATA) {
        trainingData_.erase(trainingData_.begin());
    }
    
    // Update feature scaling
    std::vector<float> features = extractFeatures(result.settings, result.systemState, 
                                                 result.audioLength, result.audioCharacteristics);
    updateFeatureScaling(features);
    
    // Update accuracy statistics
    accuracyStats_.totalPredictions++;
    
    // Trigger model retraining if enough new data
    if (learningMode_ && trainingData_.size() % 20 == 0) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastTraining_).count();
        
        if (elapsed >= 10) { // Retrain every 10 minutes
            if (!trainingActive_) {
                trainingActive_ = true;
                if (trainingThread_.joinable()) {
                    trainingThread_.join();
                }
                trainingThread_ = std::thread(&AdvancedPerformancePredictor::trainModel, this);
            }
        }
    }
}

std::vector<OptimizationRecommendation> AdvancedPerformancePredictor::getOptimizationRecommendations(
    const QualitySettings& currentSettings,
    const SystemResources& resources,
    const RequestPattern& requestPattern) {
    
    std::vector<OptimizationRecommendation> recommendations;
    
    if (!initialized_) {
        return recommendations;
    }
    
    try {
        // Analyze current performance
        PerformancePrediction currentPrediction = predictPerformanceAdvanced(
            currentSettings, resources, 16000); // 1 second of audio
        
        // Quality adjustment recommendations
        if (currentPrediction.predictedLatencyMs > 2000.0f) {
            OptimizationRecommendation rec;
            rec.type = OptimizationRecommendation::RecommendationType::QUALITY_ADJUSTMENT;
            rec.description = "Reduce quality level to improve latency";
            rec.expectedImprovement = 0.3f;
            rec.implementationCost = 0.1f;
            rec.confidence = 0.8f;
            rec.parameters["target_quality"] = std::to_string(static_cast<int>(QualityLevel::LOW));
            recommendations.push_back(rec);
        }
        
        // Resource allocation recommendations
        if (resources.cpuUsage > 0.8f && currentSettings.threadCount > 2) {
            OptimizationRecommendation rec;
            rec.type = OptimizationRecommendation::RecommendationType::RESOURCE_ALLOCATION;
            rec.description = "Reduce thread count to lower CPU usage";
            rec.expectedImprovement = 0.2f;
            rec.implementationCost = 0.1f;
            rec.confidence = 0.7f;
            rec.parameters["target_threads"] = std::to_string(currentSettings.threadCount - 1);
            recommendations.push_back(rec);
        }
        
        // GPU utilization recommendations
        if (!currentSettings.enableGPU && resources.gpuUsage < 0.3f) {
            OptimizationRecommendation rec;
            rec.type = OptimizationRecommendation::RecommendationType::CONFIGURATION_CHANGE;
            rec.description = "Enable GPU acceleration to improve performance";
            rec.expectedImprovement = 0.4f;
            rec.implementationCost = 0.2f;
            rec.confidence = 0.6f;
            rec.parameters["enable_gpu"] = "true";
            recommendations.push_back(rec);
        }
        
        // Sort by expected improvement / implementation cost ratio
        std::sort(recommendations.begin(), recommendations.end(),
                 [](const OptimizationRecommendation& a, const OptimizationRecommendation& b) {
                     float ratioA = a.expectedImprovement / (a.implementationCost + 0.1f);
                     float ratioB = b.expectedImprovement / (b.implementationCost + 0.1f);
                     return ratioA > ratioB;
                 });
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to generate optimization recommendations: " + std::string(e.what()));
    }
    
    return recommendations;
}

std::vector<PerformancePrediction> AdvancedPerformancePredictor::predictMultipleScenarios(
    const std::vector<QualitySettings>& scenarios,
    const SystemResources& resources,
    size_t audioLength) {
    
    std::vector<PerformancePrediction> predictions;
    
    for (const auto& settings : scenarios) {
        predictions.push_back(predictPerformanceAdvanced(settings, resources, audioLength));
    }
    
    return predictions;
}

CalibrationData AdvancedPerformancePredictor::getCalibrationData() const {
    std::lock_guard<std::mutex> lock(predictorMutex_);
    return calibrationData_;
}

bool AdvancedPerformancePredictor::recalibrate() {
    if (!initialized_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(predictorMutex_);
    
    try {
        // Reset calibration data
        calibrationData_ = CalibrationData{};
        
        // Calculate baselines from training data
        if (trainingData_.size() >= 10) {
            std::map<QualityLevel, std::vector<float>> latencyByQuality;
            std::map<QualityLevel, std::vector<float>> accuracyByQuality;
            
            for (const auto& data : trainingData_) {
                latencyByQuality[data.settings.level].push_back(data.actualLatency);
                accuracyByQuality[data.settings.level].push_back(data.actualAccuracy);
            }
            
            // Calculate averages
            for (const auto& pair : latencyByQuality) {
                if (!pair.second.empty()) {
                    float avg = std::accumulate(pair.second.begin(), pair.second.end(), 0.0f) / pair.second.size();
                    calibrationData_.latencyBaselines[pair.first] = avg;
                }
            }
            
            for (const auto& pair : accuracyByQuality) {
                if (!pair.second.empty()) {
                    float avg = std::accumulate(pair.second.begin(), pair.second.end(), 0.0f) / pair.second.size();
                    calibrationData_.accuracyBaselines[pair.first] = avg;
                }
            }
            
            calibrationData_.isCalibrated = true;
            calibrationData_.lastCalibration = std::chrono::steady_clock::now();
            
            speechrnt::utils::Logger::info("Performance predictor recalibrated successfully");
            return true;
        }
        
        speechrnt::utils::Logger::warning("Insufficient data for recalibration");
        return false;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Recalibration failed: " + std::string(e.what()));
        return false;
    }
}

std::string AdvancedPerformancePredictor::getPredictionAccuracyStats() const {
    std::lock_guard<std::mutex> lock(predictorMutex_);
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "{\n";
    oss << "  \"latency_mean_error\": " << accuracyStats_.latencyMeanError << ",\n";
    oss << "  \"latency_std_error\": " << accuracyStats_.latencyStdError << ",\n";
    oss << "  \"accuracy_mean_error\": " << accuracyStats_.accuracyMeanError << ",\n";
    oss << "  \"accuracy_std_error\": " << accuracyStats_.accuracyStdError << ",\n";
    oss << "  \"total_predictions\": " << accuracyStats_.totalPredictions << ",\n";
    oss << "  \"correct_predictions\": " << accuracyStats_.correctPredictions << ",\n";
    oss << "  \"prediction_accuracy\": " << (accuracyStats_.totalPredictions > 0 ? 
                                           static_cast<float>(accuracyStats_.correctPredictions) / accuracyStats_.totalPredictions : 0.0f) << ",\n";
    oss << "  \"training_data_size\": " << trainingData_.size() << "\n";
    oss << "}";
    
    return oss.str();
}

void AdvancedPerformancePredictor::setLearningMode(bool enabled) {
    learningMode_ = enabled;
    speechrnt::utils::Logger::info("Learning mode " + std::string(enabled ? "enabled" : "disabled"));
}

std::string AdvancedPerformancePredictor::exportModel() const {
    std::lock_guard<std::mutex> lock(predictorMutex_);
    
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"training_data_size\": " << trainingData_.size() << ",\n";
    oss << "  \"linear_model\": {\n";
    oss << "    \"latency_coefficients\": {\n";
    
    bool first = true;
    for (const auto& coeff : linearModel_.latencyCoefficients) {
        if (!first) oss << ",\n";
        oss << "      \"" << coeff.first << "\": " << coeff.second;
        first = false;
    }
    
    oss << "\n    },\n";
    oss << "    \"accuracy_coefficients\": {\n";
    
    first = true;
    for (const auto& coeff : linearModel_.accuracyCoefficients) {
        if (!first) oss << ",\n";
        oss << "      \"" << coeff.first << "\": " << coeff.second;
        first = false;
    }
    
    oss << "\n    }\n";
    oss << "  }\n";
    oss << "}";
    
    return oss.str();
}

bool AdvancedPerformancePredictor::importModel(const std::string& modelData) {
    // Simplified implementation - would need proper JSON parsing
    speechrnt::utils::Logger::info("Model import requested - feature not fully implemented");
    return false;
}

bool AdvancedPerformancePredictor::isInitialized() const {
    return initialized_;
}st
d::vector<float> AdvancedPerformancePredictor::extractFeatures(const QualitySettings& settings,
                                                                const SystemResources& resources,
                                                                size_t audioLength,
                                                                const std::string& audioCharacteristics) {
    std::vector<float> features;
    
    features.push_back(resources.cpuUsage);
    features.push_back(resources.memoryUsage);
    features.push_back(resources.gpuUsage);
    features.push_back(static_cast<float>(static_cast<int>(settings.level)));
    features.push_back(static_cast<float>(settings.threadCount));
    features.push_back(static_cast<float>(audioLength));
    features.push_back(settings.enableGPU ? 1.0f : 0.0f);
    features.push_back(settings.enablePreprocessing ? 1.0f : 0.0f);
    features.push_back(static_cast<float>(settings.maxBufferSize));
    
    return features;
}

float AdvancedPerformancePredictor::predictWithLinearModel(const std::vector<float>& features, bool isLatency) {
    const auto& coefficients = isLatency ? linearModel_.latencyCoefficients : linearModel_.accuracyCoefficients;
    
    float prediction = coefficients.at("base");
    
    if (features.size() >= 9) {
        prediction += features[0] * coefficients.at("cpu_usage");
        prediction += features[1] * coefficients.at("memory_usage");
        prediction += features[2] * coefficients.at("gpu_usage");
        prediction += features[3] * coefficients.at("quality_level");
        prediction += features[4] * coefficients.at("thread_count");
        prediction += features[5] * coefficients.at("audio_length");
        prediction += features[6] * coefficients.at("enable_gpu");
        prediction += features[7] * coefficients.at("enable_preprocessing");
        prediction += features[8] * coefficients.at("buffer_size");
    }
    
    return prediction;
}

float AdvancedPerformancePredictor::predictWithNeuralNetwork(const std::vector<float>& features, bool isLatency) {
    // Simplified implementation - would use actual neural network
    return predictWithLinearModel(features, isLatency);
}

std::vector<float> AdvancedPerformancePredictor::normalizeFeatures(const std::vector<float>& features) {
    std::vector<float> normalized = features;
    
    const std::vector<std::string> featureNames = {
        "cpu_usage", "memory_usage", "gpu_usage", "quality_level", "thread_count",
        "audio_length", "enable_gpu", "enable_preprocessing", "buffer_size"
    };
    
    for (size_t i = 0; i < std::min(features.size(), featureNames.size()); ++i) {
        const auto& scaling = linearModel_.featureScaling.at(featureNames[i]);
        float range = scaling.second - scaling.first;
        if (range > 0.0f) {
            normalized[i] = (features[i] - scaling.first) / range;
        }
    }
    
    return normalized;
}

void AdvancedPerformancePredictor::updateFeatureScaling(const std::vector<float>& features) {
    const std::vector<std::string> featureNames = {
        "cpu_usage", "memory_usage", "gpu_usage", "quality_level", "thread_count",
        "audio_length", "enable_gpu", "enable_preprocessing", "buffer_size"
    };
    
    for (size_t i = 0; i < std::min(features.size(), featureNames.size()); ++i) {
        auto& scaling = linearModel_.featureScaling[featureNames[i]];
        scaling.first = std::min(scaling.first, features[i]);
        scaling.second = std::max(scaling.second, features[i]);
    }
}

float AdvancedPerformancePredictor::calculatePredictionConfidence(const std::vector<float>& features) {
    // Simplified confidence calculation based on feature values
    float confidence = 0.8f;
    
    // Reduce confidence for extreme values
    for (float feature : features) {
        if (feature < 0.0f || feature > 1.0f) {
            confidence *= 0.9f;
        }
    }
    
    // Reduce confidence if insufficient training data
    if (trainingData_.size() < 50) {
        confidence *= 0.7f;
    }
    
    return std::clamp(confidence, 0.1f, 0.95f);
}

void AdvancedPerformancePredictor::trainModel() {
    std::lock_guard<std::mutex> lock(predictorMutex_);
    
    try {
        if (trainingData_.size() < 10) {
            trainingActive_ = false;
            return;
        }
        
        speechrnt::utils::Logger::info("Starting model training with " + std::to_string(trainingData_.size()) + " samples");
        
        // Update linear model
        updateLinearModel();
        
        lastTraining_ = std::chrono::steady_clock::now();
        trainingActive_ = false;
        
        speechrnt::utils::Logger::info("Model training completed");
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Model training failed: " + std::string(e.what()));
        trainingActive_ = false;
    }
}

void AdvancedPerformancePredictor::updateLinearModel() {
    // Simplified linear regression update
    std::map<std::string, float> latencySum, accuracySum;
    std::map<std::string, int> counts;
    
    for (const auto& data : trainingData_) {
        std::vector<float> features = extractFeatures(data.settings, data.systemState, 
                                                     data.audioLength, data.audioCharacteristics);
        
        const std::vector<std::string> featureNames = {
            "cpu_usage", "memory_usage", "gpu_usage", "quality_level", "thread_count",
            "audio_length", "enable_gpu", "enable_preprocessing", "buffer_size"
        };
        
        for (size_t i = 0; i < std::min(features.size(), featureNames.size()); ++i) {
            latencySum[featureNames[i]] += features[i] * data.actualLatency;
            accuracySum[featureNames[i]] += features[i] * data.actualAccuracy;
            counts[featureNames[i]]++;
        }
    }
    
    // Update coefficients (simplified)
    for (const auto& pair : counts) {
        if (pair.second > 0) {
            linearModel_.latencyCoefficients[pair.first] = latencySum[pair.first] / pair.second;
            linearModel_.accuracyCoefficients[pair.first] = accuracySum[pair.first] / pair.second;
        }
    }
    
    linearModel_.trainingDataSize = trainingData_.size();
    linearModel_.lastTraining = std::chrono::steady_clock::now();
}

void AdvancedPerformancePredictor::updateNeuralNetwork() {
    // Simplified neural network training placeholder
    speechrnt::utils::Logger::info("Neural network training not fully implemented - using linear model");
    
    neuralModel_.trainingDataSize = trainingData_.size();
    neuralModel_.lastTraining = std::chrono::steady_clock::now();
}

// PerformancePredictionSystem implementation
PerformancePredictionSystem::PerformancePredictionSystem()
    : initialized_(false) {
}

PerformancePredictionSystem::~PerformancePredictionSystem() = default;

bool PerformancePredictionSystem::initialize() {
    std::lock_guard<std::mutex> lock(systemMutex_);
    
    try {
        predictor_ = std::make_unique<AdvancedPerformancePredictor>();
        
        if (!predictor_->initialize()) {
            return false;
        }
        
        initialized_ = true;
        speechrnt::utils::Logger::info("PerformancePredictionSystem initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to initialize PerformancePredictionSystem: " + std::string(e.what()));
        return false;
    }
}

PerformancePrediction PerformancePredictionSystem::getComprehensivePrediction(
    const QualitySettings& settings,
    const SystemResources& resources,
    size_t audioLength,
    const std::string& audioCharacteristics) {
    
    if (!initialized_ || !predictor_) {
        return PerformancePrediction{};
    }
    
    return predictor_->predictPerformanceAdvanced(settings, resources, audioLength, audioCharacteristics);
}

std::vector<OptimizationRecommendation> PerformancePredictionSystem::getOptimizationRecommendations(
    const QualitySettings& currentSettings,
    const SystemResources& resources) {
    
    if (!initialized_ || !predictor_) {
        return {};
    }
    
    RequestPattern pattern; // Simplified - would use actual pattern analyzer
    return predictor_->getOptimizationRecommendations(currentSettings, resources, pattern);
}

void PerformancePredictionSystem::recordActualPerformance(const QualitySettings& settings,
                                                         const SystemResources& resources,
                                                         size_t audioLength,
                                                         float actualLatency,
                                                         float actualAccuracy,
                                                         const std::string& audioCharacteristics) {
    if (!initialized_ || !predictor_) {
        return;
    }
    
    BenchmarkResult result;
    result.settings = settings;
    result.systemState = resources;
    result.audioLength = audioLength;
    result.actualLatency = actualLatency;
    result.actualAccuracy = actualAccuracy;
    result.audioCharacteristics = audioCharacteristics;
    result.timestamp = std::chrono::steady_clock::now();
    
    predictor_->updateWithBenchmarkResult(result);
}

bool PerformancePredictionSystem::runCalibration() {
    if (!initialized_ || !predictor_) {
        return false;
    }
    
    return predictor_->recalibrate();
}

std::vector<BenchmarkResult> PerformancePredictionSystem::runBenchmark() {
    // Simplified implementation
    return {};
}

std::string PerformancePredictionSystem::getPerformanceStatistics() const {
    if (!initialized_ || !predictor_) {
        return "{}";
    }
    
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"prediction_accuracy\": " << predictor_->getPredictionAccuracyStats() << "\n";
    oss << "}";
    
    return oss.str();
}

std::string PerformancePredictionSystem::exportModels() const {
    if (!initialized_ || !predictor_) {
        return "{}";
    }
    
    return predictor_->exportModel();
}

bool PerformancePredictionSystem::importModels(const std::string& modelData) {
    if (!initialized_ || !predictor_) {
        return false;
    }
    
    return predictor_->importModel(modelData);
}

bool PerformancePredictionSystem::isInitialized() const {
    return initialized_;
}

} // namespace advanced
} // namespace stt