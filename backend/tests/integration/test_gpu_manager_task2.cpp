#include <gtest/gtest.h>
#include "utils/gpu_manager.hpp"
#include "utils/logging.hpp"
#include <thread>
#include <chrono>

using namespace speechrnt::utils;

class GPUManagerTask2Test : public ::testing::Test {
protected:
    void SetUp() override {
        gpuManager_ = &GPUManager::getInstance();
        gpuManager_->initialize();
        Logger::info("GPU Manager Task 2 test setup completed");
    }
    
    void TearDown() override {
        gpuManager_->cleanup();
        Logger::info("GPU Manager Task 2 test cleanup completed");
    }
    
    GPUManager* gpuManager_;
};

TEST_F(GPUManagerTask2Test, BasicGPUInfo) {
    // Test basic GPU information
    bool cudaAvailable = gpuManager_->isCudaAvailable();
    int deviceCount = gpuManager_->getDeviceCount();
    
    Logger::info("CUDA available: " + std::string(cudaAvailable ? "yes" : "no"));
    Logger::info("Device count: " + std::to_string(deviceCount));
    
    if (cudaAvailable && deviceCount > 0) {
        auto deviceInfo = gpuManager_->getDeviceInfo(0);
        EXPECT_GE(deviceInfo.deviceId, 0);
        EXPECT_FALSE(deviceInfo.name.empty());
        EXPECT_GT(deviceInfo.totalMemoryMB, 0);
        
        Logger::info("Device 0: " + deviceInfo.name + 
                    " (" + std::to_string(deviceInfo.totalMemoryMB) + "MB)");
    }
}

TEST_F(GPUManagerTask2Test, DetailedGPUMetrics) {
    // Test detailed GPU metrics collection
    auto metrics = gpuManager_->getDetailedGPUMetrics();
    
    if (gpuManager_->isCudaAvailable()) {
        // Should have basic metrics even without NVML
        EXPECT_TRUE(metrics.find("total_memory_mb") != metrics.end());
        EXPECT_TRUE(metrics.find("free_memory_mb") != metrics.end());
        EXPECT_TRUE(metrics.find("memory_utilization_percent") != metrics.end());
        
        Logger::info("GPU metrics collected: " + std::to_string(metrics.size()) + " metrics");
        
        for (const auto& metric : metrics) {
            Logger::info("  " + metric.first + ": " + std::to_string(metric.second));
        }
    } else {
        // Should be empty if no GPU available
        EXPECT_TRUE(metrics.empty());
        Logger::info("No GPU available - metrics collection skipped");
    }
}

TEST_F(GPUManagerTask2Test, GPUUtilizationMonitoring) {
    // Test GPU utilization monitoring
    float utilization = gpuManager_->getGPUUtilization();
    
    if (gpuManager_->isCudaAvailable()) {
        // Should return a valid value or -1 if not supported
        EXPECT_TRUE(utilization >= -1.0f && utilization <= 100.0f);
        Logger::info("GPU utilization: " + std::to_string(utilization) + "%");
    } else {
        EXPECT_EQ(utilization, -1.0f);
        Logger::info("GPU utilization not available (no CUDA)");
    }
}

TEST_F(GPUManagerTask2Test, GPUTemperatureMonitoring) {
    // Test GPU temperature monitoring
    float temperature = gpuManager_->getGPUTemperature();
    
    if (gpuManager_->isCudaAvailable()) {
        // Should return a valid temperature or -1 if not supported
        EXPECT_TRUE(temperature >= -1.0f);
        if (temperature > 0) {
            EXPECT_LT(temperature, 150.0f); // Reasonable upper bound
        }
        Logger::info("GPU temperature: " + std::to_string(temperature) + "°C");
    } else {
        EXPECT_EQ(temperature, -1.0f);
        Logger::info("GPU temperature not available (no CUDA)");
    }
}

TEST_F(GPUManagerTask2Test, GPUPowerMonitoring) {
    // Test GPU power monitoring
    float power = gpuManager_->getGPUPowerUsage();
    
    if (gpuManager_->isCudaAvailable()) {
        // Should return a valid power value or -1 if not supported
        EXPECT_TRUE(power >= -1.0f);
        if (power > 0) {
            EXPECT_LT(power, 1000.0f); // Reasonable upper bound (1000W)
        }
        Logger::info("GPU power usage: " + std::to_string(power) + "W");
    } else {
        EXPECT_EQ(power, -1.0f);
        Logger::info("GPU power monitoring not available (no CUDA)");
    }
}

TEST_F(GPUManagerTask2Test, NVMLAvailability) {
    // Test NVML availability
    bool nvmlAvailable = gpuManager_->isNVMLAvailable();
    Logger::info("NVML available: " + std::string(nvmlAvailable ? "yes" : "no"));
    
    if (nvmlAvailable) {
        // If NVML is available, detailed metrics should work better
        auto metrics = gpuManager_->getDetailedGPUMetrics();
        
        // Should have more detailed metrics with NVML
        if (gpuManager_->isCudaAvailable()) {
            EXPECT_FALSE(metrics.empty());
        }
    }
}

TEST_F(GPUManagerTask2Test, MemoryAllocationWithMetrics) {
    if (!gpuManager_->isCudaAvailable()) {
        GTEST_SKIP() << "CUDA not available, skipping memory allocation test";
    }
    
    // Test memory allocation and monitoring
    size_t allocSize = 64 * 1024 * 1024; // 64MB
    void* ptr = gpuManager_->allocateGPUMemory(allocSize, "test_allocation");
    
    if (ptr != nullptr) {
        // Check memory usage increased
        size_t memoryUsage = gpuManager_->getCurrentMemoryUsageMB();
        EXPECT_GT(memoryUsage, 0);
        
        auto allocations = gpuManager_->getMemoryAllocations();
        EXPECT_FALSE(allocations.empty());
        
        // Get detailed metrics after allocation
        auto metrics = gpuManager_->getDetailedGPUMetrics();
        if (!metrics.empty()) {
            Logger::info("Memory metrics after allocation:");
            for (const auto& metric : metrics) {
                if (metric.first.find("memory") != std::string::npos) {
                    Logger::info("  " + metric.first + ": " + std::to_string(metric.second));
                }
            }
        }
        
        // Free memory
        EXPECT_TRUE(gpuManager_->freeGPUMemory(ptr));
        
        // Check memory usage decreased
        size_t memoryUsageAfter = gpuManager_->getCurrentMemoryUsageMB();
        EXPECT_LE(memoryUsageAfter, memoryUsage);
        
    } else {
        Logger::warn("GPU memory allocation failed - insufficient memory or GPU not available");
    }
}

TEST_F(GPUManagerTask2Test, ContinuousMetricsCollection) {
    if (!gpuManager_->isCudaAvailable()) {
        GTEST_SKIP() << "CUDA not available, skipping continuous metrics test";
    }
    
    // Test continuous metrics collection over time
    std::vector<float> utilizationSamples;
    std::vector<float> temperatureSamples;
    
    for (int i = 0; i < 5; ++i) {
        float util = gpuManager_->getGPUUtilization();
        float temp = gpuManager_->getGPUTemperature();
        
        if (util >= 0) utilizationSamples.push_back(util);
        if (temp >= 0) temperatureSamples.push_back(temp);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    Logger::info("Collected " + std::to_string(utilizationSamples.size()) + 
                " utilization samples and " + std::to_string(temperatureSamples.size()) + 
                " temperature samples");
    
    // Should have collected some samples if monitoring is working
    if (!utilizationSamples.empty()) {
        float avgUtil = 0;
        for (float util : utilizationSamples) {
            avgUtil += util;
        }
        avgUtil /= utilizationSamples.size();
        Logger::info("Average GPU utilization: " + std::to_string(avgUtil) + "%");
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Initialize logging for tests
    Logger::info("Starting GPU Manager Task 2 integration tests");
    
    return RUN_ALL_TESTS();
}