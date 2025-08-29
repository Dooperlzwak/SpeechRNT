#include "mt/gpu_accelerator.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>

using namespace speechrnt::mt;

void printGPUInfo(const std::vector<GPUInfo>& gpus) {
    std::cout << "\n=== Available GPUs ===" << std::endl;
    
    if (gpus.empty()) {
        std::cout << "No GPUs detected." << std::endl;
        return;
    }
    
    for (const auto& gpu : gpus) {
        std::cout << "GPU " << gpu.deviceId << ": " << gpu.deviceName << std::endl;
        std::cout << "  Memory: " << gpu.availableMemoryMB << "/" << gpu.totalMemoryMB << " MB" << std::endl;
        std::cout << "  Compute Capability: " << gpu.computeCapabilityMajor << "." << gpu.computeCapabilityMinor << std::endl;
        std::cout << "  Multiprocessors: " << gpu.multiProcessorCount << std::endl;
        std::cout << "  Compatible: " << (gpu.isCompatible ? "Yes" : "No") << std::endl;
        std::cout << "  FP16 Support: " << (gpu.supportsFloat16 ? "Yes" : "No") << std::endl;
        std::cout << "  INT8 Support: " << (gpu.supportsInt8 ? "Yes" : "No") << std::endl;
        std::cout << "  CUDA Version: " << gpu.cudaVersion << std::endl;
        std::cout << std::endl;
    }
}

void printGPUStats(const GPUStats& stats) {
    std::cout << "\n=== GPU Performance Statistics ===" << std::endl;
    std::cout << "Utilization: " << stats.utilizationPercent << "%" << std::endl;
    std::cout << "Memory Used: " << stats.memoryUsedMB << " MB" << std::endl;
    std::cout << "Temperature: " << stats.temperatureCelsius << "°C" << std::endl;
    std::cout << "Translations Processed: " << stats.translationsProcessed << std::endl;
    std::cout << "Average Translation Time: " << stats.averageTranslationTime.count() << " ms" << std::endl;
    std::cout << "Models Loaded: " << stats.modelsLoaded << std::endl;
    std::cout << "Active Streams: " << stats.activeStreams << std::endl;
    std::cout << "Throughput: " << stats.throughputTranslationsPerSecond << " translations/sec" << std::endl;
    std::cout << std::endl;
}

void demonstrateBasicUsage() {
    std::cout << "\n=== Basic GPU Accelerator Usage ===" << std::endl;
    
    // Create and initialize GPU accelerator
    GPUAccelerator accelerator;
    
    if (!accelerator.initialize()) {
        std::cout << "Failed to initialize GPU accelerator: " << accelerator.getLastGPUError() << std::endl;
        return;
    }
    
    // Get available GPUs
    auto gpus = accelerator.getAvailableGPUs();
    printGPUInfo(gpus);
    
    // Check if GPU is available
    if (!accelerator.isGPUAvailable()) {
        std::cout << "No compatible GPU available. Continuing with CPU fallback enabled." << std::endl;
        accelerator.enableCPUFallback(true);
        return;
    }
    
    // Get current GPU info
    auto currentGPU = accelerator.getCurrentGPUInfo();
    std::cout << "Selected GPU: " << currentGPU.deviceName << " (Device " << currentGPU.deviceId << ")" << std::endl;
    
    // Configure GPU accelerator
    std::cout << "\nConfiguring GPU accelerator..." << std::endl;
    accelerator.configureMemoryPool(1024, true); // 1GB memory pool with defragmentation
    accelerator.configureQuantization(true, "fp16"); // Enable FP16 quantization
    accelerator.configureBatchProcessing(32, 8); // Max batch size 32, optimal 8
    accelerator.configureConcurrentStreams(true, 4); // 4 concurrent streams
    
    // Set performance thresholds
    accelerator.setPerformanceThresholds(80.0f, 85.0f, 90.0f); // Memory, temperature, utilization
    
    std::cout << "GPU accelerator configured successfully." << std::endl;
}

void demonstrateModelLoading() {
    std::cout << "\n=== Model Loading Demonstration ===" << std::endl;
    
    GPUAccelerator accelerator;
    
    if (!accelerator.initialize() || !accelerator.isGPUAvailable()) {
        std::cout << "GPU not available for model loading demonstration." << std::endl;
        return;
    }
    
    // Simulate loading models for different language pairs
    std::vector<std::pair<std::string, std::string>> languagePairs = {
        {"en-es", "models/en-es.npz"},
        {"es-en", "models/es-en.npz"},
        {"en-fr", "models/en-fr.npz"},
        {"fr-en", "models/fr-en.npz"}
    };
    
    std::cout << "Loading models to GPU..." << std::endl;
    
    for (const auto& [languagePair, modelPath] : languagePairs) {
        void* gpuModelPtr = nullptr;
        
        std::cout << "Loading model for " << languagePair << "..." << std::endl;
        
        if (accelerator.loadModelToGPU(modelPath, languagePair, &gpuModelPtr)) {
            std::cout << "  ✓ Successfully loaded " << languagePair << " model" << std::endl;
        } else {
            std::cout << "  ✗ Failed to load " << languagePair << " model: " 
                     << accelerator.getLastGPUError() << std::endl;
        }
    }
    
    // Display loaded models
    auto loadedModels = accelerator.getLoadedModels();
    std::cout << "\nLoaded models (" << loadedModels.size() << "):" << std::endl;
    
    for (const auto& model : loadedModels) {
        std::cout << "  " << model.languagePair << ": " << model.memorySizeMB << " MB, "
                 << "Precision: " << model.precision << ", "
                 << "Usage: " << model.usageCount << std::endl;
    }
    
    // Test memory optimization
    std::cout << "\nOptimizing GPU memory..." << std::endl;
    if (accelerator.optimizeGPUMemory()) {
        std::cout << "Memory optimization completed." << std::endl;
    }
    
    std::cout << "GPU Memory Usage: " << accelerator.getGPUMemoryUsage() << " MB" << std::endl;
    std::cout << "Available GPU Memory: " << accelerator.getAvailableGPUMemory() << " MB" << std::endl;
}

void demonstrateTranslationAcceleration() {
    std::cout << "\n=== Translation Acceleration Demonstration ===" << std::endl;
    
    GPUAccelerator accelerator;
    
    if (!accelerator.initialize() || !accelerator.isGPUAvailable()) {
        std::cout << "GPU not available for translation demonstration." << std::endl;
        return;
    }
    
    // Simulate loading a model
    void* gpuModelPtr = nullptr;
    std::string modelPath = "models/en-es.npz";
    std::string languagePair = "en-es";
    
    if (!accelerator.loadModelToGPU(modelPath, languagePair, &gpuModelPtr)) {
        std::cout << "Failed to load model for translation demonstration." << std::endl;
        return;
    }
    
    // Single translation test
    std::cout << "Testing single translation..." << std::endl;
    std::string input = "Hello, how are you today?";
    std::string output;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    if (accelerator.accelerateTranslation(gpuModelPtr, input, output)) {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        std::cout << "  Input: " << input << std::endl;
        std::cout << "  Output: " << output << std::endl;
        std::cout << "  Time: " << duration.count() << " ms" << std::endl;
    } else {
        std::cout << "  Translation failed: " << accelerator.getLastGPUError() << std::endl;
    }
    
    // Batch translation test
    std::cout << "\nTesting batch translation..." << std::endl;
    std::vector<std::string> inputs = {
        "Good morning!",
        "How are you?",
        "What time is it?",
        "Thank you very much.",
        "See you later!"
    };
    
    std::vector<std::string> outputs;
    
    startTime = std::chrono::high_resolution_clock::now();
    
    if (accelerator.accelerateBatchTranslation(gpuModelPtr, inputs, outputs)) {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        std::cout << "  Batch size: " << inputs.size() << std::endl;
        std::cout << "  Total time: " << duration.count() << " ms" << std::endl;
        std::cout << "  Average per translation: " << (duration.count() / inputs.size()) << " ms" << std::endl;
        
        for (size_t i = 0; i < inputs.size() && i < outputs.size(); ++i) {
            std::cout << "    \"" << inputs[i] << "\" -> \"" << outputs[i] << "\"" << std::endl;
        }
    } else {
        std::cout << "  Batch translation failed: " << accelerator.getLastGPUError() << std::endl;
    }
}

void demonstrateStreamingTranslation() {
    std::cout << "\n=== Streaming Translation Demonstration ===" << std::endl;
    
    GPUAccelerator accelerator;
    
    if (!accelerator.initialize() || !accelerator.isGPUAvailable()) {
        std::cout << "GPU not available for streaming demonstration." << std::endl;
        return;
    }
    
    // Simulate loading a model
    void* gpuModelPtr = nullptr;
    std::string modelPath = "models/en-es.npz";
    std::string languagePair = "en-es";
    
    if (!accelerator.loadModelToGPU(modelPath, languagePair, &gpuModelPtr)) {
        std::cout << "Failed to load model for streaming demonstration." << std::endl;
        return;
    }
    
    // Start streaming session
    std::string sessionId = "demo_session_001";
    
    if (!accelerator.startStreamingSession(gpuModelPtr, sessionId)) {
        std::cout << "Failed to start streaming session." << std::endl;
        return;
    }
    
    std::cout << "Started streaming session: " << sessionId << std::endl;
    
    // Simulate streaming chunks
    std::vector<std::string> chunks = {
        "Hello",
        " there,",
        " how",
        " are",
        " you",
        " doing",
        " today?"
    };
    
    std::string accumulatedOutput;
    
    for (const auto& chunk : chunks) {
        std::string outputChunk;
        
        if (accelerator.processStreamingChunk(sessionId, chunk, outputChunk)) {
            std::cout << "  Chunk: \"" << chunk << "\" -> \"" << outputChunk << "\"" << std::endl;
            accumulatedOutput = outputChunk; // In real implementation, this would be incremental
        } else {
            std::cout << "  Failed to process chunk: " << chunk << std::endl;
        }
        
        // Simulate real-time delay
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // End streaming session
    if (accelerator.endStreamingSession(sessionId)) {
        std::cout << "Streaming session ended successfully." << std::endl;
        std::cout << "Final output: \"" << accumulatedOutput << "\"" << std::endl;
    }
}

void demonstratePerformanceMonitoring() {
    std::cout << "\n=== Performance Monitoring Demonstration ===" << std::endl;
    
    GPUAccelerator accelerator;
    
    if (!accelerator.initialize() || !accelerator.isGPUAvailable()) {
        std::cout << "GPU not available for performance monitoring demonstration." << std::endl;
        return;
    }
    
    // Start performance monitoring
    std::cout << "Starting performance monitoring..." << std::endl;
    accelerator.startPerformanceMonitoring(1000); // 1 second interval
    
    // Simulate some GPU work
    void* gpuModelPtr = nullptr;
    std::string modelPath = "models/en-es.npz";
    std::string languagePair = "en-es";
    
    if (accelerator.loadModelToGPU(modelPath, languagePair, &gpuModelPtr)) {
        // Perform some translations to generate activity
        for (int i = 0; i < 10; ++i) {
            std::string input = "Test translation " + std::to_string(i);
            std::string output;
            accelerator.accelerateTranslation(gpuModelPtr, input, output);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    
    // Wait a bit for monitoring data to accumulate
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // Get and display statistics
    auto stats = accelerator.getGPUStatistics();
    printGPUStats(stats);
    
    // Check performance alerts
    auto alerts = accelerator.getPerformanceAlerts();
    if (!alerts.empty()) {
        std::cout << "Performance Alerts:" << std::endl;
        for (const auto& alert : alerts) {
            std::cout << "  ⚠️  " << alert << std::endl;
        }
    } else {
        std::cout << "No performance alerts." << std::endl;
    }
    
    // Get performance history
    auto history = accelerator.getPerformanceHistory(5); // Last 5 minutes
    std::cout << "Performance history entries: " << history.size() << std::endl;
    
    // Stop monitoring
    accelerator.stopPerformanceMonitoring();
    std::cout << "Performance monitoring stopped." << std::endl;
}

void demonstrateErrorHandling() {
    std::cout << "\n=== Error Handling Demonstration ===" << std::endl;
    
    GPUAccelerator accelerator;
    
    if (!accelerator.initialize()) {
        std::cout << "Initialization failed: " << accelerator.getLastGPUError() << std::endl;
        return;
    }
    
    // Test CPU fallback configuration
    std::cout << "Configuring CPU fallback..." << std::endl;
    accelerator.enableCPUFallback(true);
    std::cout << "CPU fallback enabled: " << (accelerator.isCPUFallbackEnabled() ? "Yes" : "No") << std::endl;
    
    // Test error handling
    std::cout << "Testing error handling..." << std::endl;
    std::string testError = "Simulated GPU error for testing";
    
    if (accelerator.handleGPUError(testError)) {
        std::cout << "Error handled successfully with recovery." << std::endl;
    } else {
        std::cout << "Error handling failed, fallback to CPU." << std::endl;
    }
    
    std::cout << "Last error: " << accelerator.getLastGPUError() << std::endl;
    
    // Test GPU operational status
    if (accelerator.isGPUAvailable()) {
        std::cout << "GPU operational status: " << (accelerator.isGPUOperational() ? "Operational" : "Not operational") << std::endl;
        
        // Test device reset
        std::cout << "Testing GPU device reset..." << std::endl;
        if (accelerator.resetGPUDevice()) {
            std::cout << "GPU device reset successful." << std::endl;
        } else {
            std::cout << "GPU device reset failed." << std::endl;
        }
    }
}

int main() {
    std::cout << "GPU Accelerator Example" << std::endl;
    std::cout << "======================" << std::endl;
    
    try {
        // Run demonstrations
        demonstrateBasicUsage();
        demonstrateModelLoading();
        demonstrateTranslationAcceleration();
        demonstrateStreamingTranslation();
        demonstratePerformanceMonitoring();
        demonstrateErrorHandling();
        
        std::cout << "\n=== Example completed successfully ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception occurred: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}