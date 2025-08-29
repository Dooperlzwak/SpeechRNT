#include "stt/advanced/adaptive_quality_manager.hpp"
#include "stt/advanced/performance_prediction_system.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

using namespace stt::advanced;

/**
 * Example demonstrating how to use the Adaptive Quality and Performance Scaling system
 */
class AdaptiveQualityExample {
public:
    AdaptiveQualityExample() = default;
    
    void runExample() {
        std::cout << "Adaptive Quality and Performance Scaling Example" << std::endl;
        std::cout << "===============================================" << std::endl;
        
        // Step 1: Initialize the adaptive quality manager
        if (!initializeAdaptiveQuality()) {
            std::cerr << "Failed to initialize adaptive quality system" << std::endl;
            return;
        }
        
        // Step 2: Initialize the performance prediction system
        if (!initializePerformancePrediction()) {
            std::cerr << "Failed to initialize performance prediction system" << std::endl;
            return;
        }
        
        // Step 3: Simulate transcription requests with different system conditions
        simulateTranscriptionScenarios();
        
        // Step 4: Demonstrate optimization recommendations
        demonstrateOptimizationRecommendations();
        
        // Step 5: Show performance statistics and model export
        showPerformanceStatistics();
        
        std::cout << "\nExample completed successfully!" << std::endl;
    }

private:
    std::unique_ptr<AdaptiveQualityManager> qualityManager_;
    std::unique_ptr<PerformancePredictionSystem> predictionSystem_;
    
    bool initializeAdaptiveQuality() {
        std::cout << "\n1. Initializing Adaptive Quality Manager..." << std::endl;
        
        qualityManager_ = std::make_unique<AdaptiveQualityManager>();
        
        // Configure adaptive quality settings
        AdaptiveQualityConfig config;
        config.enableAdaptation = true;
        config.cpuThreshold = 0.75f;
        config.memoryThreshold = 0.80f;
        config.defaultQuality = QualityLevel::MEDIUM;
        config.adaptationIntervalMs = 2000.0f;
        config.enablePredictiveScaling = true;
        
        if (!qualityManager_->initialize(config)) {
            std::cerr << "Error: " << qualityManager_->getLastError() << std::endl;
            return false;
        }
        
        std::cout << "✓ Adaptive Quality Manager initialized" << std::endl;
        std::cout << "  - CPU threshold: " << config.cpuThreshold * 100 << "%" << std::endl;
        std::cout << "  - Memory threshold: " << config.memoryThreshold * 100 << "%" << std::endl;
        std::cout << "  - Default quality: " << static_cast<int>(config.defaultQuality) << std::endl;
        std::cout << "  - Adaptation interval: " << config.adaptationIntervalMs << "ms" << std::endl;
        
        return true;
    }
    
    bool initializePerformancePrediction() {
        std::cout << "\n2. Initializing Performance Prediction System..." << std::endl;
        
        predictionSystem_ = std::make_unique<PerformancePredictionSystem>();
        
        if (!predictionSystem_->initialize()) {
            return false;
        }
        
        std::cout << "✓ Performance Prediction System initialized" << std::endl;
        
        // Run initial calibration
        if (predictionSystem_->runCalibration()) {
            std::cout << "✓ Initial system calibration completed" << std::endl;
        } else {
            std::cout << "⚠ Initial calibration skipped (insufficient data)" << std::endl;
        }
        
        return true;
    }
    
    void simulateTranscriptionScenarios() {
        std::cout << "\n3. Simulating Transcription Scenarios..." << std::endl;
        
        // Scenario 1: Normal system load
        std::cout << "\nScenario 1: Normal System Load" << std::endl;
        simulateScenario("Normal Load", 0.4f, 0.5f, 0.2f, 2);
        
        // Scenario 2: High CPU usage
        std::cout << "\nScenario 2: High CPU Usage" << std::endl;
        simulateScenario("High CPU", 0.9f, 0.6f, 0.3f, 4);
        
        // Scenario 3: Memory constrained
        std::cout << "\nScenario 3: Memory Constrained" << std::endl;
        simulateScenario("Memory Constrained", 0.6f, 0.95f, 0.4f, 3);
        
        // Scenario 4: High concurrent load
        std::cout << "\nScenario 4: High Concurrent Load" << std::endl;
        simulateScenario("High Load", 0.8f, 0.8f, 0.7f, 8);
        
        // Scenario 5: Optimal conditions
        std::cout << "\nScenario 5: Optimal Conditions" << std::endl;
        simulateScenario("Optimal", 0.2f, 0.3f, 0.1f, 1);
    }
    
    void simulateScenario(const std::string& scenarioName, 
                         float cpuUsage, 
                         float memoryUsage, 
                         float gpuUsage, 
                         size_t concurrentRequests) {
        
        // Create system resources for this scenario
        SystemResources resources;
        resources.cpuUsage = cpuUsage;
        resources.memoryUsage = memoryUsage;
        resources.gpuUsage = gpuUsage;
        resources.activeTranscriptions = concurrentRequests;
        resources.averageLatency = 500.0f + (cpuUsage * 1000.0f);
        resources.resourceConstrained = (cpuUsage > 0.8f || memoryUsage > 0.8f);
        resources.availableMemoryMB = static_cast<size_t>(8192 * (1.0f - memoryUsage));
        resources.totalMemoryMB = 8192;
        
        // Create transcription requests
        std::vector<TranscriptionRequest> requests;
        for (size_t i = 0; i < concurrentRequests; ++i) {
            TranscriptionRequest req;
            req.requestId = static_cast<uint32_t>(i + 1);
            req.audioLength = 16000 + (i * 8000); // Varying audio lengths
            req.isRealTime = true;
            req.requestedQuality = QualityLevel::HIGH;
            req.maxLatencyMs = 1500.0f;
            req.language = "en";
            req.enableAdvancedFeatures = true;
            requests.push_back(req);
        }
        
        std::cout << "  System State:" << std::endl;
        std::cout << "    CPU: " << (cpuUsage * 100) << "%, Memory: " << (memoryUsage * 100) 
                  << "%, GPU: " << (gpuUsage * 100) << "%" << std::endl;
        std::cout << "    Concurrent requests: " << concurrentRequests << std::endl;
        std::cout << "    Resource constrained: " << (resources.resourceConstrained ? "Yes" : "No") << std::endl;
        
        // Get adapted quality settings
        QualitySettings adaptedSettings = qualityManager_->adaptQuality(resources, requests);
        
        std::cout << "  Adapted Settings:" << std::endl;
        std::cout << "    Quality level: " << static_cast<int>(adaptedSettings.level) << std::endl;
        std::cout << "    Thread count: " << adaptedSettings.threadCount << std::endl;
        std::cout << "    GPU enabled: " << (adaptedSettings.enableGPU ? "Yes" : "No") << std::endl;
        std::cout << "    Confidence threshold: " << adaptedSettings.confidenceThreshold << std::endl;
        std::cout << "    Buffer size: " << adaptedSettings.maxBufferSize << std::endl;
        
        // Get performance prediction
        PerformancePrediction prediction = predictionSystem_->getComprehensivePrediction(
            adaptedSettings, resources, 24000, "{\"scenario\": \"" + scenarioName + "\"}");
        
        std::cout << "  Performance Prediction:" << std::endl;
        std::cout << "    Predicted latency: " << prediction.predictedLatencyMs << "ms" << std::endl;
        std::cout << "    Predicted accuracy: " << (prediction.predictedAccuracy * 100) << "%" << std::endl;
        std::cout << "    Confidence: " << (prediction.confidenceInPrediction * 100) << "%" << std::endl;
        std::cout << "    Recommended quality: " << static_cast<int>(prediction.recommendedQuality) << std::endl;
        std::cout << "    Reasoning: " << prediction.reasoning << std::endl;
        
        // Simulate actual transcription and record performance
        float actualLatency = prediction.predictedLatencyMs + ((rand() % 200) - 100); // Add some noise
        float actualAccuracy = prediction.predictedAccuracy + ((rand() % 20 - 10) / 1000.0f); // Add some noise
        
        actualLatency = std::max(50.0f, actualLatency);
        actualAccuracy = std::clamp(actualAccuracy, 0.3f, 0.99f);
        
        // Record actual performance for learning
        predictionSystem_->recordActualPerformance(
            adaptedSettings, resources, 24000, actualLatency, actualAccuracy, 
            "{\"scenario\": \"" + scenarioName + "\"}");
        
        qualityManager_->recordActualPerformance(
            adaptedSettings, 24000, actualLatency, actualAccuracy);
        
        std::cout << "  Actual Performance:" << std::endl;
        std::cout << "    Actual latency: " << actualLatency << "ms" << std::endl;
        std::cout << "    Actual accuracy: " << (actualAccuracy * 100) << "%" << std::endl;
        
        // Small delay to simulate processing time
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    void demonstrateOptimizationRecommendations() {
        std::cout << "\n4. Optimization Recommendations..." << std::endl;
        
        // Create a scenario with suboptimal settings
        QualitySettings suboptimalSettings;
        suboptimalSettings.level = QualityLevel::ULTRA_HIGH;
        suboptimalSettings.threadCount = 8;
        suboptimalSettings.enableGPU = false; // Suboptimal: not using GPU
        suboptimalSettings.confidenceThreshold = 0.9f; // Too high
        suboptimalSettings.enablePreprocessing = true;
        suboptimalSettings.maxBufferSize = 4096; // Large buffer
        
        SystemResources constrainedResources;
        constrainedResources.cpuUsage = 0.95f; // Very high CPU
        constrainedResources.memoryUsage = 0.85f; // High memory
        constrainedResources.gpuUsage = 0.1f; // Low GPU usage
        constrainedResources.activeTranscriptions = 6;
        constrainedResources.resourceConstrained = true;
        
        std::cout << "  Current suboptimal configuration:" << std::endl;
        std::cout << "    Quality: ULTRA_HIGH, Threads: 8, GPU: Disabled" << std::endl;
        std::cout << "    System: CPU 95%, Memory 85%, GPU 10%" << std::endl;
        
        // Get optimization recommendations
        std::vector<OptimizationRecommendation> recommendations = 
            predictionSystem_->getOptimizationRecommendations(suboptimalSettings, constrainedResources);
        
        std::cout << "  Optimization Recommendations (" << recommendations.size() << "):" << std::endl;
        
        for (size_t i = 0; i < recommendations.size(); ++i) {
            const auto& rec = recommendations[i];
            std::cout << "    " << (i + 1) << ". " << rec.description << std::endl;
            std::cout << "       Expected improvement: " << (rec.expectedImprovement * 100) << "%" << std::endl;
            std::cout << "       Implementation cost: " << (rec.implementationCost * 100) << "%" << std::endl;
            std::cout << "       Confidence: " << (rec.confidence * 100) << "%" << std::endl;
            
            // Show parameters if available
            if (!rec.parameters.empty()) {
                std::cout << "       Parameters: ";
                for (const auto& param : rec.parameters) {
                    std::cout << param.first << "=" << param.second << " ";
                }
                std::cout << std::endl;
            }
        }
    }
    
    void showPerformanceStatistics() {
        std::cout << "\n5. Performance Statistics and Model Export..." << std::endl;
        
        // Get adaptation statistics from quality manager
        std::string adaptationStats = qualityManager_->getAdaptationStats();
        std::cout << "  Adaptation Statistics:" << std::endl;
        std::cout << "    " << adaptationStats << std::endl;
        
        // Get performance statistics from prediction system
        std::string performanceStats = predictionSystem_->getPerformanceStatistics();
        std::cout << "  Performance Statistics:" << std::endl;
        std::cout << "    " << performanceStats << std::endl;
        
        // Export prediction models
        std::string exportedModels = predictionSystem_->exportModels();
        std::cout << "  Exported Models:" << std::endl;
        std::cout << "    Model data size: " << exportedModels.length() << " characters" << std::endl;
        
        // Show current quality settings
        QualitySettings currentSettings = qualityManager_->getCurrentQualitySettings();
        std::cout << "  Current Quality Settings:" << std::endl;
        std::cout << "    Level: " << static_cast<int>(currentSettings.level) << std::endl;
        std::cout << "    Threads: " << currentSettings.threadCount << std::endl;
        std::cout << "    GPU: " << (currentSettings.enableGPU ? "Enabled" : "Disabled") << std::endl;
        std::cout << "    Confidence: " << currentSettings.confidenceThreshold << std::endl;
        
        // Show current system resources
        SystemResources currentResources = qualityManager_->getCurrentResources();
        std::cout << "  Current System Resources:" << std::endl;
        std::cout << "    CPU: " << (currentResources.cpuUsage * 100) << "%" << std::endl;
        std::cout << "    Memory: " << (currentResources.memoryUsage * 100) << "%" << std::endl;
        std::cout << "    GPU: " << (currentResources.gpuUsage * 100) << "%" << std::endl;
        std::cout << "    Available Memory: " << currentResources.availableMemoryMB << " MB" << std::endl;
    }
};

int main() {
    try {
        AdaptiveQualityExample example;
        example.runExample();
        
    } catch (const std::exception& e) {
        std::cerr << "Example failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}