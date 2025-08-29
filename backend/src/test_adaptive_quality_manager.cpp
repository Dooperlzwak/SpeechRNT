#include "stt/advanced/adaptive_quality_manager.hpp"
#include "stt/advanced/performance_prediction_system.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace stt::advanced;

void testAdaptiveQualityManager() {
    std::cout << "Testing AdaptiveQualityManager..." << std::endl;
    
    // Initialize manager
    AdaptiveQualityManager manager;
    AdaptiveQualityConfig config;
    config.enableAdaptation = true;
    config.cpuThreshold = 0.8f;
    config.memoryThreshold = 0.8f;
    config.defaultQuality = QualityLevel::MEDIUM;
    config.adaptationIntervalMs = 1000.0f;
    config.enablePredictiveScaling = true;
    
    if (!manager.initialize(config)) {
        std::cerr << "Failed to initialize AdaptiveQualityManager: " << manager.getLastError() << std::endl;
        return;
    }
    
    std::cout << "AdaptiveQualityManager initialized successfully" << std::endl;
    
    // Test resource monitoring
    SystemResources resources = manager.getCurrentResources();
    std::cout << "Current resources - CPU: " << resources.cpuUsage 
              << ", Memory: " << resources.memoryUsage 
              << ", GPU: " << resources.gpuUsage << std::endl;
    
    // Test quality adaptation
    std::vector<TranscriptionRequest> requests;
    TranscriptionRequest req;
    req.requestId = 1;
    req.audioLength = 16000;
    req.isRealTime = true;
    req.requestedQuality = QualityLevel::HIGH;
    req.maxLatencyMs = 1000.0f;
    requests.push_back(req);
    
    QualitySettings adaptedSettings = manager.adaptQuality(resources, requests);
    std::cout << "Adapted quality level: " << static_cast<int>(adaptedSettings.level) << std::endl;
    std::cout << "Thread count: " << adaptedSettings.threadCount << std::endl;
    std::cout << "GPU enabled: " << (adaptedSettings.enableGPU ? "true" : "false") << std::endl;
    
    // Test performance prediction
    float predictedLatency = manager.predictLatency(adaptedSettings, 16000);
    float predictedAccuracy = manager.predictAccuracy(adaptedSettings);
    std::cout << "Predicted latency: " << predictedLatency << "ms" << std::endl;
    std::cout << "Predicted accuracy: " << predictedAccuracy << std::endl;
    
    // Test recording actual performance
    manager.recordActualPerformance(adaptedSettings, 16000, 800.0f, 0.92f);
    std::cout << "Recorded actual performance" << std::endl;
    
    // Test statistics
    std::string stats = manager.getAdaptationStats();
    std::cout << "Adaptation statistics: " << stats << std::endl;
    
    // Test configuration update
    config.cpuThreshold = 0.7f;
    if (manager.updateConfiguration(config)) {
        std::cout << "Configuration updated successfully" << std::endl;
    }
    
    std::cout << "AdaptiveQualityManager test completed successfully" << std::endl;
}

void testPerformancePredictionSystem() {
    std::cout << "\nTesting PerformancePredictionSystem..." << std::endl;
    
    // Initialize system
    PerformancePredictionSystem system;
    
    if (!system.initialize()) {
        std::cerr << "Failed to initialize PerformancePredictionSystem" << std::endl;
        return;
    }
    
    std::cout << "PerformancePredictionSystem initialized successfully" << std::endl;
    
    // Test comprehensive prediction
    QualitySettings settings;
    settings.level = QualityLevel::HIGH;
    settings.threadCount = 4;
    settings.enableGPU = true;
    settings.confidenceThreshold = 0.6f;
    settings.enablePreprocessing = true;
    settings.maxBufferSize = 1024;
    
    SystemResources resources;
    resources.cpuUsage = 0.6f;
    resources.memoryUsage = 0.5f;
    resources.gpuUsage = 0.3f;
    resources.activeTranscriptions = 2;
    resources.averageLatency = 500.0f;
    resources.resourceConstrained = false;
    
    PerformancePrediction prediction = system.getComprehensivePrediction(
        settings, resources, 32000, "{\"noise_level\": 0.1, \"speech_rate\": 1.0}");
    
    std::cout << "Comprehensive prediction:" << std::endl;
    std::cout << "  Predicted latency: " << prediction.predictedLatencyMs << "ms" << std::endl;
    std::cout << "  Predicted accuracy: " << prediction.predictedAccuracy << std::endl;
    std::cout << "  Confidence: " << prediction.confidenceInPrediction << std::endl;
    std::cout << "  Recommended quality: " << static_cast<int>(prediction.recommendedQuality) << std::endl;
    std::cout << "  Reasoning: " << prediction.reasoning << std::endl;
    
    // Test optimization recommendations
    std::vector<OptimizationRecommendation> recommendations = 
        system.getOptimizationRecommendations(settings, resources);
    
    std::cout << "Optimization recommendations (" << recommendations.size() << "):" << std::endl;
    for (size_t i = 0; i < recommendations.size(); ++i) {
        const auto& rec = recommendations[i];
        std::cout << "  " << (i + 1) << ". " << rec.description << std::endl;
        std::cout << "     Expected improvement: " << (rec.expectedImprovement * 100) << "%" << std::endl;
        std::cout << "     Implementation cost: " << (rec.implementationCost * 100) << "%" << std::endl;
        std::cout << "     Confidence: " << (rec.confidence * 100) << "%" << std::endl;
    }
    
    // Test recording actual performance
    system.recordActualPerformance(settings, resources, 32000, 750.0f, 0.89f, 
                                  "{\"noise_level\": 0.1, \"speech_rate\": 1.0}");
    std::cout << "Recorded actual performance for learning" << std::endl;
    
    // Test performance statistics
    std::string stats = system.getPerformanceStatistics();
    std::cout << "Performance statistics: " << stats << std::endl;
    
    // Test model export
    std::string exportedModel = system.exportModels();
    std::cout << "Exported model size: " << exportedModel.length() << " characters" << std::endl;
    
    std::cout << "PerformancePredictionSystem test completed successfully" << std::endl;
}

void testResourceMonitor() {
    std::cout << "\nTesting ResourceMonitor..." << std::endl;
    
    ResourceMonitorImpl monitor;
    
    if (!monitor.initialize()) {
        std::cerr << "Failed to initialize ResourceMonitor" << std::endl;
        return;
    }
    
    std::cout << "ResourceMonitor initialized successfully" << std::endl;
    
    // Test resource collection
    SystemResources resources = monitor.getCurrentResources();
    std::cout << "Current system resources:" << std::endl;
    std::cout << "  CPU usage: " << (resources.cpuUsage * 100) << "%" << std::endl;
    std::cout << "  Memory usage: " << (resources.memoryUsage * 100) << "%" << std::endl;
    std::cout << "  GPU usage: " << (resources.gpuUsage * 100) << "%" << std::endl;
    std::cout << "  Available memory: " << resources.availableMemoryMB << " MB" << std::endl;
    std::cout << "  Total memory: " << resources.totalMemoryMB << " MB" << std::endl;
    std::cout << "  Resource constrained: " << (resources.resourceConstrained ? "true" : "false") << std::endl;
    
    // Test threshold setting
    monitor.setResourceThresholds(0.7f, 0.8f, 0.9f);
    std::cout << "Resource thresholds updated" << std::endl;
    
    // Test monitoring
    if (monitor.startMonitoring(500)) {
        std::cout << "Started continuous monitoring" << std::endl;
        
        // Let it monitor for a few seconds
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Get resource history
        std::vector<SystemResources> history = monitor.getResourceHistory(5);
        std::cout << "Resource history (" << history.size() << " samples):" << std::endl;
        for (size_t i = 0; i < history.size(); ++i) {
            std::cout << "  Sample " << (i + 1) << ": CPU=" << (history[i].cpuUsage * 100) 
                      << "%, Memory=" << (history[i].memoryUsage * 100) << "%" << std::endl;
        }
        
        monitor.stopMonitoring();
        std::cout << "Stopped monitoring" << std::endl;
    }
    
    std::cout << "ResourceMonitor test completed successfully" << std::endl;
}

int main() {
    try {
        std::cout << "Starting Adaptive Quality and Performance Scaling Tests" << std::endl;
        std::cout << "======================================================" << std::endl;
        
        testResourceMonitor();
        testAdaptiveQualityManager();
        testPerformancePredictionSystem();
        
        std::cout << "\n======================================================" << std::endl;
        std::cout << "All tests completed successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}