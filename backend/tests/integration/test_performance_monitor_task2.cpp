#include <gtest/gtest.h>
#include "utils/performance_monitor.hpp"
#include "utils/logging.hpp"
#include <thread>
#include <chrono>

using namespace speechrnt::utils;

class PerformanceMonitorTask2Test : public ::testing::Test {
protected:
    void SetUp() override {
        perfMonitor_ = &PerformanceMonitor::getInstance();
        perfMonitor_->initialize(true, 1000); // Enable system metrics, 1s interval
        perfMonitor_->setEnabled(true);
        Logger::info("Performance Monitor Task 2 test setup completed");
    }
    
    void TearDown() override {
        perfMonitor_->setEnabled(false);
        perfMonitor_->clearMetrics();
        Logger::info("Performance Monitor Task 2 test cleanup completed");
    }
    
    PerformanceMonitor* perfMonitor_;
};

TEST_F(PerformanceMonitorTask2Test, CPUMetricsCollection) {
    // Wait for system metrics to be collected
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    
    // Check if CPU metrics are being collected
    auto cpuStats = perfMonitor_->getMetricStats(PerformanceMonitor::METRIC_CPU_USAGE, 1);
    
    EXPECT_GT(cpuStats.count, 0);
    EXPECT_GE(cpuStats.mean, 0.0);
    EXPECT_LE(cpuStats.mean, 100.0);
    
    Logger::info("CPU usage stats - Count: " + std::to_string(cpuStats.count) + 
                ", Mean: " + std::to_string(cpuStats.mean) + "%" +
                ", Min: " + std::to_string(cpuStats.min) + "%" +
                ", Max: " + std::to_string(cpuStats.max) + "%");
}

TEST_F(PerformanceMonitorTask2Test, MemoryMetricsCollection) {
    // Wait for system metrics to be collected
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    
    // Check if memory metrics are being collected
    auto memoryStats = perfMonitor_->getMetricStats(PerformanceMonitor::METRIC_MEMORY_USAGE, 1);
    
    EXPECT_GT(memoryStats.count, 0);
    EXPECT_GT(memoryStats.mean, 0.0);
    
    Logger::info("Memory usage stats - Count: " + std::to_string(memoryStats.count) + 
                ", Mean: " + std::to_string(memoryStats.mean) + "MB" +
                ", Min: " + std::to_string(memoryStats.min) + "MB" +
                ", Max: " + std::to_string(memoryStats.max) + "MB");
}

TEST_F(PerformanceMonitorTask2Test, GPUMetricsCollection) {
    // Wait for system metrics to be collected
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    
    // Check if GPU metrics are being collected (if GPU available)
    auto gpuMemoryStats = perfMonitor_->getMetricStats(PerformanceMonitor::METRIC_GPU_MEMORY_USAGE, 1);
    auto gpuUtilStats = perfMonitor_->getMetricStats(PerformanceMonitor::METRIC_GPU_UTILIZATION, 1);
    
    if (gpuMemoryStats.count > 0) {
        Logger::info("GPU memory usage stats - Count: " + std::to_string(gpuMemoryStats.count) + 
                    ", Mean: " + std::to_string(gpuMemoryStats.mean) + "MB");
        EXPECT_GE(gpuMemoryStats.mean, 0.0);
    } else {
        Logger::info("No GPU memory metrics collected (GPU not available)");
    }
    
    if (gpuUtilStats.count > 0) {
        Logger::info("GPU utilization stats - Count: " + std::to_string(gpuUtilStats.count) + 
                    ", Mean: " + std::to_string(gpuUtilStats.mean) + "%");
        EXPECT_GE(gpuUtilStats.mean, 0.0);
        EXPECT_LE(gpuUtilStats.mean, 100.0);
    } else {
        Logger::info("No GPU utilization metrics collected (GPU not available or NVML not supported)");
    }
}

TEST_F(PerformanceMonitorTask2Test, SystemSummary) {
    // Wait for system metrics to be collected
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    
    // Get system summary
    auto summary = perfMonitor_->getSystemSummary();
    
    EXPECT_FALSE(summary.empty());
    EXPECT_TRUE(summary.find("memory_usage_mb") != summary.end());
    EXPECT_TRUE(summary.find("cpu_usage_percent") != summary.end());
    EXPECT_TRUE(summary.find("total_metrics_recorded") != summary.end());
    
    Logger::info("System summary:");
    for (const auto& metric : summary) {
        Logger::info("  " + metric.first + ": " + std::to_string(metric.second));
    }
}

TEST_F(PerformanceMonitorTask2Test, MetricsExport) {
    // Record some test metrics
    perfMonitor_->recordMetric("test.metric1", 42.0, "units");
    perfMonitor_->recordMetric("test.metric2", 84.0, "units");
    
    // Wait for system metrics
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    
    // Export metrics as JSON
    std::string jsonExport = perfMonitor_->exportMetricsJSON(1);
    
    EXPECT_FALSE(jsonExport.empty());
    EXPECT_TRUE(jsonExport.find("timestamp") != std::string::npos);
    EXPECT_TRUE(jsonExport.find("metrics") != std::string::npos);
    EXPECT_TRUE(jsonExport.find("test.metric1") != std::string::npos);
    
    Logger::info("Exported metrics JSON (first 200 chars): " + 
                jsonExport.substr(0, 200) + "...");
}

TEST_F(PerformanceMonitorTask2Test, ContinuousMonitoring) {
    // Test continuous monitoring over several collection intervals
    auto startTime = std::chrono::steady_clock::now();
    
    // Wait for multiple collection cycles
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);
    
    // Check that metrics were collected continuously
    auto cpuStats = perfMonitor_->getMetricStats(PerformanceMonitor::METRIC_CPU_USAGE, 1);
    auto memoryStats = perfMonitor_->getMetricStats(PerformanceMonitor::METRIC_MEMORY_USAGE, 1);
    
    // Should have collected multiple samples
    EXPECT_GE(cpuStats.count, 3); // At least 3 samples in 5 seconds with 1s interval
    EXPECT_GE(memoryStats.count, 3);
    
    Logger::info("Continuous monitoring test - Duration: " + std::to_string(duration.count()) + 
                "s, CPU samples: " + std::to_string(cpuStats.count) + 
                ", Memory samples: " + std::to_string(memoryStats.count));
}

TEST_F(PerformanceMonitorTask2Test, LatencyTimerIntegration) {
    // Test latency timer with system metrics
    {
        auto timer = perfMonitor_->startLatencyTimer("test.operation_latency");
        
        // Simulate some work
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Timer will automatically record latency when destroyed
    }
    
    // Check that latency was recorded
    auto latencyStats = perfMonitor_->getMetricStats("test.operation_latency", 1);
    
    EXPECT_EQ(latencyStats.count, 1);
    EXPECT_GE(latencyStats.mean, 45.0); // Should be around 50ms
    EXPECT_LE(latencyStats.mean, 100.0); // Allow some variance
    
    Logger::info("Latency timer test - Recorded latency: " + 
                std::to_string(latencyStats.mean) + "ms");
}

TEST_F(PerformanceMonitorTask2Test, MetricsAvailability) {
    // Wait for system metrics to be collected
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    
    // Get list of available metrics
    auto availableMetrics = perfMonitor_->getAvailableMetrics();
    
    EXPECT_FALSE(availableMetrics.empty());
    
    // Should have system metrics
    bool hasCPUMetric = std::find(availableMetrics.begin(), availableMetrics.end(), 
                                 PerformanceMonitor::METRIC_CPU_USAGE) != availableMetrics.end();
    bool hasMemoryMetric = std::find(availableMetrics.begin(), availableMetrics.end(), 
                                    PerformanceMonitor::METRIC_MEMORY_USAGE) != availableMetrics.end();
    
    EXPECT_TRUE(hasCPUMetric);
    EXPECT_TRUE(hasMemoryMetric);
    
    Logger::info("Available metrics (" + std::to_string(availableMetrics.size()) + "):");
    for (const auto& metric : availableMetrics) {
        Logger::info("  " + metric);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Initialize logging for tests
    Logger::info("Starting Performance Monitor Task 2 integration tests");
    
    return RUN_ALL_TESTS();
}