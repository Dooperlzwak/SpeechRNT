#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include <filesystem>
#include "utils/gpu_manager.hpp"
#include "utils/gpu_config.hpp"
#include "utils/performance_monitor.hpp"

using namespace speechrnt::utils;

void printGPUInfo() {
    auto& gpuManager = GPUManager::getInstance();
    
    if (!gpuManager.initialize()) {
        std::cout << "Failed to initialize GPU manager" << std::endl;
        return;
    }
    
    if (!gpuManager.isCudaAvailable()) {
        std::cout << "CUDA is not available on this system" << std::endl;
        return;
    }
    
    std::cout << "=== GPU Information ===" << std::endl;
    std::cout << "Number of GPU devices: " << gpuManager.getDeviceCount() << std::endl;
    
    auto devices = gpuManager.getAllDeviceInfo();
    for (const auto& device : devices) {
        std::cout << "\nDevice " << device.deviceId << ":" << std::endl;
        std::cout << "  Name: " << device.name << std::endl;
        std::cout << "  Total Memory: " << device.totalMemoryMB << " MB" << std::endl;
        std::cout << "  Free Memory: " << device.freeMemoryMB << " MB" << std::endl;
        std::cout << "  Compute Capability: " << device.computeCapabilityMajor 
                  << "." << device.computeCapabilityMinor << std::endl;
        std::cout << "  Multiprocessors: " << device.multiProcessorCount << std::endl;
        std::cout << "  Available: " << (device.isAvailable ? "Yes" : "No") << std::endl;
    }
    
    int recommended = gpuManager.getRecommendedDevice();
    if (recommended >= 0) {
        std::cout << "\nRecommended device for AI workloads: " << recommended << std::endl;
    }
}

void testGPUMemoryOperations() {
    auto& gpuManager = GPUManager::getInstance();
    
    if (!gpuManager.isCudaAvailable()) {
        std::cout << "Skipping GPU memory tests (CUDA not available)" << std::endl;
        return;
    }
    
    std::cout << "\n=== GPU Memory Operations Test ===" << std::endl;
    
    const size_t testSize = 10 * 1024 * 1024; // 10MB
    std::vector<float> hostData(testSize / sizeof(float));
    
    // Fill with test data
    for (size_t i = 0; i < hostData.size(); ++i) {
        hostData[i] = static_cast<float>(i % 1000) / 1000.0f;
    }
    
    // Allocate GPU memory
    std::cout << "Allocating " << testSize / (1024 * 1024) << "MB on GPU..." << std::endl;
    void* devicePtr = gpuManager.allocateGPUMemory(testSize, "memory_test");
    
    if (!devicePtr) {
        std::cout << "Failed to allocate GPU memory: " << gpuManager.getLastError() << std::endl;
        return;
    }
    
    // Test host to device copy
    auto startTime = std::chrono::high_resolution_clock::now();
    bool copySuccess = gpuManager.copyHostToDevice(devicePtr, hostData.data(), testSize);
    auto endTime = std::chrono::high_resolution_clock::now();
    
    if (copySuccess) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        double bandwidth = (testSize / (1024.0 * 1024.0)) / (duration.count() / 1000000.0);
        std::cout << "Host to Device copy: " << duration.count() << " μs" << std::endl;
        std::cout << "Bandwidth: " << bandwidth << " MB/s" << std::endl;
    } else {
        std::cout << "Host to Device copy failed: " << gpuManager.getLastError() << std::endl;
    }
    
    // Test device to host copy
    std::vector<float> resultData(hostData.size());
    startTime = std::chrono::high_resolution_clock::now();
    copySuccess = gpuManager.copyDeviceToHost(resultData.data(), devicePtr, testSize);
    endTime = std::chrono::high_resolution_clock::now();
    
    if (copySuccess) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        double bandwidth = (testSize / (1024.0 * 1024.0)) / (duration.count() / 1000000.0);
        std::cout << "Device to Host copy: " << duration.count() << " μs" << std::endl;
        std::cout << "Bandwidth: " << bandwidth << " MB/s" << std::endl;
        
        // Verify data integrity
        bool dataValid = true;
        for (size_t i = 0; i < std::min(hostData.size(), resultData.size()); ++i) {
            if (std::abs(hostData[i] - resultData[i]) > 1e-6f) {
                dataValid = false;
                break;
            }
        }
        std::cout << "Data integrity: " << (dataValid ? "PASS" : "FAIL") << std::endl;
    } else {
        std::cout << "Device to Host copy failed: " << gpuManager.getLastError() << std::endl;
    }
    
    // Free GPU memory
    if (gpuManager.freeGPUMemory(devicePtr)) {
        std::cout << "GPU memory freed successfully" << std::endl;
    } else {
        std::cout << "Failed to free GPU memory: " << gpuManager.getLastError() << std::endl;
    }
}

void testGPUConfiguration() {
    std::cout << "\n=== GPU Configuration Test ===" << std::endl;
    
    auto& configManager = GPUConfigManager::getInstance();
    
    // Test auto-detection
    std::cout << "Auto-detecting optimal configuration..." << std::endl;
    if (configManager.autoDetectOptimalConfig()) {
        std::cout << "Auto-detection successful" << std::endl;
        
        auto globalConfig = configManager.getGlobalConfig();
        std::cout << "Global config:" << std::endl;
        std::cout << "  Enabled: " << (globalConfig.enabled ? "Yes" : "No") << std::endl;
        std::cout << "  Device ID: " << globalConfig.deviceId << std::endl;
        std::cout << "  Memory Limit: " << globalConfig.memoryLimitMB << " MB" << std::endl;
        std::cout << "  Memory Pool: " << (globalConfig.enableMemoryPool ? "Enabled" : "Disabled") << std::endl;
        
        // Show model configurations
        auto modelConfigs = configManager.getAllModelConfigs();
        std::cout << "\nModel configurations:" << std::endl;
        for (const auto& pair : modelConfigs) {
            const auto& config = pair.second;
            std::cout << "  " << pair.first << ":" << std::endl;
            std::cout << "    Use GPU: " << (config.useGPU ? "Yes" : "No") << std::endl;
            std::cout << "    Device ID: " << config.deviceId << std::endl;
            std::cout << "    Batch Size: " << config.batchSize << std::endl;
            std::cout << "    Precision: " << config.precision << std::endl;
            std::cout << "    Quantization: " << (config.enableQuantization ? "Enabled" : "Disabled") << std::endl;
        }
        
        // Test JSON export/import
        std::cout << "\nTesting configuration serialization..." << std::endl;
        std::string jsonConfig = configManager.toJSON();
        std::cout << "Configuration exported to JSON (" << jsonConfig.length() << " bytes)" << std::endl;
        
        // Save to file
        if (configManager.saveConfig("gpu_test_config.json")) {
            std::cout << "Configuration saved to gpu_test_config.json" << std::endl;
        }
        
    } else {
        std::cout << "Auto-detection failed" << std::endl;
    }
}

void testPerformanceMonitoring() {
    std::cout << "\n=== Performance Monitoring Test ===" << std::endl;
    
    auto& perfMonitor = PerformanceMonitor::getInstance();
    perfMonitor.initialize(false); // Disable system metrics for test
    
    // Record some test metrics
    perfMonitor.recordLatency("test.gpu_operation", 15.5);
    perfMonitor.recordLatency("test.gpu_operation", 12.3);
    perfMonitor.recordLatency("test.gpu_operation", 18.7);
    
    perfMonitor.recordThroughput("test.gpu_throughput", 150.0);
    perfMonitor.recordCounter("test.gpu_operations", 5);
    
    // Test latency timer
    {
        auto timer = perfMonitor.startLatencyTimer("test.timed_operation");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // Timer automatically records when destroyed
    }
    
    // Get statistics
    auto latencyStats = perfMonitor.getMetricStats("test.gpu_operation");
    std::cout << "GPU operation latency stats:" << std::endl;
    std::cout << "  Count: " << latencyStats.count << std::endl;
    std::cout << "  Mean: " << latencyStats.mean << " ms" << std::endl;
    std::cout << "  Min: " << latencyStats.min << " ms" << std::endl;
    std::cout << "  Max: " << latencyStats.max << " ms" << std::endl;
    std::cout << "  P95: " << latencyStats.p95 << " ms" << std::endl;
    
    // Export metrics
    std::string metricsJson = perfMonitor.exportMetricsJSON(60);
    std::cout << "\nMetrics exported to JSON (" << metricsJson.length() << " bytes)" << std::endl;
    
    perfMonitor.cleanup();
}

int main(int argc, char* argv[]) {
    std::cout << "SpeechRNT GPU Test Utility" << std::endl;
    std::cout << "=========================" << std::endl;
    
    try {
        printGPUInfo();
        testGPUMemoryOperations();
        testGPUConfiguration();
        testPerformanceMonitoring();
        
        std::cout << "\n=== All tests completed ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error during testing: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}