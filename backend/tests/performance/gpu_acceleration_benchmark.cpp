#include <gtest/gtest.h>
#include "utils/gpu_manager.hpp"
#include "utils/performance_monitor.hpp"
#include "stt/whisper_stt.hpp"
#include "mt/marian_translator.hpp"
#include "tts/piper_tts.hpp"
#include <chrono>
#include <vector>
#include <random>
#include <thread>
#include <filesystem>
#include <cmath>
#include <atomic>

using namespace speechrnt;

class GPUAccelerationBenchmark : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize GPU manager
        auto& gpuManager = utils::GPUManager::getInstance();
        gpuManager.initialize();
        
        // Initialize performance monitor
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        perfMonitor.initialize(false); // Disable system metrics for benchmarks
        
        // Generate test audio data (16kHz, mono, 5 seconds)
        generateTestAudioData();
    }
    
    void TearDown() override {
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        perfMonitor.cleanup();
        
        auto& gpuManager = utils::GPUManager::getInstance();
        gpuManager.cleanup();
    }
    
    void generateTestAudioData() {
        const int sampleRate = 16000;
        const int durationSeconds = 5;
        const int numSamples = sampleRate * durationSeconds;
        
        testAudioData_.resize(numSamples);
        
        // Generate sine wave with some noise
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<float> noise(0.0f, 0.1f);
        
        for (int i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            float signal = 0.5f * std::sin(2.0f * M_PI * 440.0f * t); // 440Hz tone
            testAudioData_[i] = signal + noise(gen);
        }
    }
    
    std::vector<float> testAudioData_;
    const std::string testModelPath_ = "backend/data/whisper/ggml-base.en.bin";
    const std::string testText_ = "Hello, this is a test sentence for translation and synthesis benchmarking.";
};

TEST_F(GPUAccelerationBenchmark, GPUManagerInitialization) {
    auto& gpuManager = utils::GPUManager::getInstance();
    
    EXPECT_TRUE(gpuManager.initialize());
    
    if (gpuManager.isCudaAvailable()) {
        EXPECT_GT(gpuManager.getDeviceCount(), 0);
        
        auto deviceInfo = gpuManager.getDeviceInfo(0);
        EXPECT_GT(deviceInfo.totalMemoryMB, 0);
        EXPECT_TRUE(deviceInfo.isAvailable);
        
        std::cout << "GPU Device 0: " << deviceInfo.name 
                  << " (" << deviceInfo.totalMemoryMB << "MB)" << std::endl;
    } else {
        std::cout << "CUDA not available, skipping GPU tests" << std::endl;
    }
}

TEST_F(GPUAccelerationBenchmark, GPUMemoryAllocation) {
    auto& gpuManager = utils::GPUManager::getInstance();
    
    if (!gpuManager.isCudaAvailable()) {
        GTEST_SKIP() << "CUDA not available";
    }
    
    const size_t allocSize = 1024 * 1024; // 1MB
    
    // Test allocation
    void* devicePtr = gpuManager.allocateGPUMemory(allocSize, "benchmark_test");
    EXPECT_NE(devicePtr, nullptr);
    
    // Test memory tracking
    auto allocations = gpuManager.getMemoryAllocations();
    EXPECT_EQ(allocations.size(), 1);
    EXPECT_EQ(allocations[0].sizeBytes, allocSize);
    EXPECT_EQ(allocations[0].tag, "benchmark_test");
    
    // Test deallocation
    EXPECT_TRUE(gpuManager.freeGPUMemory(devicePtr));
    
    allocations = gpuManager.getMemoryAllocations();
    EXPECT_EQ(allocations.size(), 0);
}

TEST_F(GPUAccelerationBenchmark, STTPerformanceComparison) {
    auto& gpuManager = utils::GPUManager::getInstance();
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    if (!std::filesystem::exists(testModelPath_)) {
        GTEST_SKIP() << "Whisper model not found: " << testModelPath_;
    }
    
    // Test CPU performance
    {
        stt::WhisperSTT sttCPU;
        ASSERT_TRUE(sttCPU.initialize(testModelPath_, 4));
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        bool transcriptionComplete = false;
        sttCPU.transcribe(testAudioData_, [&](const stt::TranscriptionResult& result) {
            transcriptionComplete = true;
            EXPECT_FALSE(result.text.empty());
        });
        
        // Wait for completion (with timeout)
        int waitCount = 0;
        while (!transcriptionComplete && waitCount < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            waitCount++;
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto cpuDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        perfMonitor.recordMetric("benchmark.stt.cpu_latency_ms", cpuDuration.count());
        
        std::cout << "STT CPU latency: " << cpuDuration.count() << "ms" << std::endl;
    }
    
    // Test GPU performance (if available)
    if (gpuManager.isCudaAvailable()) {
        stt::WhisperSTT sttGPU;
        ASSERT_TRUE(sttGPU.initializeWithGPU(testModelPath_, 0, 4));
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        bool transcriptionComplete = false;
        sttGPU.transcribe(testAudioData_, [&](const stt::TranscriptionResult& result) {
            transcriptionComplete = true;
            EXPECT_FALSE(result.text.empty());
        });
        
        // Wait for completion (with timeout)
        int waitCount = 0;
        while (!transcriptionComplete && waitCount < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            waitCount++;
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto gpuDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        perfMonitor.recordMetric("benchmark.stt.gpu_latency_ms", gpuDuration.count());
        
        std::cout << "STT GPU latency: " << gpuDuration.count() << "ms" << std::endl;
        
        // Compare performance
        auto cpuStats = perfMonitor.getMetricStats("benchmark.stt.cpu_latency_ms");
        auto gpuStats = perfMonitor.getMetricStats("benchmark.stt.gpu_latency_ms");
        
        if (cpuStats.count > 0 && gpuStats.count > 0) {
            double speedup = cpuStats.mean / gpuStats.mean;
            std::cout << "GPU speedup: " << speedup << "x" << std::endl;
            perfMonitor.recordMetric("benchmark.stt.gpu_speedup", speedup);
        }
    }
}

TEST_F(GPUAccelerationBenchmark, MemoryUsageMonitoring) {
    auto& gpuManager = utils::GPUManager::getInstance();
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    if (!gpuManager.isCudaAvailable()) {
        GTEST_SKIP() << "CUDA not available";
    }
    
    // Record initial memory usage
    size_t initialMemory = gpuManager.getCurrentMemoryUsageMB();
    perfMonitor.recordMetric("benchmark.gpu.initial_memory_mb", initialMemory);
    
    // Allocate some memory
    std::vector<void*> allocations;
    const size_t allocSize = 10 * 1024 * 1024; // 10MB
    const int numAllocs = 5;
    
    for (int i = 0; i < numAllocs; ++i) {
        void* ptr = gpuManager.allocateGPUMemory(allocSize, "benchmark_alloc_" + std::to_string(i));
        ASSERT_NE(ptr, nullptr);
        allocations.push_back(ptr);
        
        size_t currentMemory = gpuManager.getCurrentMemoryUsageMB();
        perfMonitor.recordMetric("benchmark.gpu.memory_usage_mb", currentMemory);
    }
    
    // Verify memory usage increased
    size_t peakMemory = gpuManager.getCurrentMemoryUsageMB();
    EXPECT_GT(peakMemory, initialMemory);
    
    std::cout << "Peak GPU memory usage: " << peakMemory << "MB" << std::endl;
    
    // Free all allocations
    for (void* ptr : allocations) {
        EXPECT_TRUE(gpuManager.freeGPUMemory(ptr));
    }
    
    // Verify memory was freed
    size_t finalMemory = gpuManager.getCurrentMemoryUsageMB();
    EXPECT_LE(finalMemory, initialMemory + 1); // Allow for small overhead
    
    perfMonitor.recordMetric("benchmark.gpu.final_memory_mb", finalMemory);
}

TEST_F(GPUAccelerationBenchmark, ConcurrentGPUOperations) {
    auto& gpuManager = utils::GPUManager::getInstance();
    
    if (!gpuManager.isCudaAvailable()) {
        GTEST_SKIP() << "CUDA not available";
    }
    
    const int numThreads = 4;
    const size_t allocSize = 1024 * 1024; // 1MB per thread
    
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    std::atomic<int> errorCount{0};
    
    // Launch concurrent allocation/deallocation threads
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            try {
                void* ptr = gpuManager.allocateGPUMemory(allocSize, "concurrent_test_" + std::to_string(i));
                if (ptr) {
                    // Simulate some work
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    
                    if (gpuManager.freeGPUMemory(ptr)) {
                        successCount++;
                    } else {
                        errorCount++;
                    }
                } else {
                    errorCount++;
                }
            } catch (const std::exception& e) {
                errorCount++;
                std::cerr << "Thread " << i << " error: " << e.what() << std::endl;
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(successCount.load(), numThreads);
    EXPECT_EQ(errorCount.load(), 0);
    
    std::cout << "Concurrent operations: " << successCount.load() << " successful, " 
              << errorCount.load() << " errors" << std::endl;
}

TEST_F(GPUAccelerationBenchmark, PerformanceMetricsCollection) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Record various metrics
    perfMonitor.recordLatency("test.latency", 150.5);
    perfMonitor.recordThroughput("test.throughput", 25.0);
    perfMonitor.recordCounter("test.counter", 5);
    perfMonitor.recordMetric("test.custom", 42.0, "units");
    
    // Verify metrics were recorded
    auto latencyStats = perfMonitor.getMetricStats("test.latency");
    EXPECT_EQ(latencyStats.count, 1);
    EXPECT_DOUBLE_EQ(latencyStats.mean, 150.5);
    
    auto throughputStats = perfMonitor.getMetricStats("test.throughput");
    EXPECT_EQ(throughputStats.count, 1);
    EXPECT_DOUBLE_EQ(throughputStats.mean, 25.0);
    
    // Test metrics export
    std::string jsonExport = perfMonitor.exportMetricsJSON(60);
    EXPECT_FALSE(jsonExport.empty());
    EXPECT_NE(jsonExport.find("test.latency"), std::string::npos);
    
    std::cout << "Metrics export sample:\n" << jsonExport.substr(0, 200) << "..." << std::endl;
}

// Benchmark main function
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "=== GPU Acceleration Benchmark Suite ===" << std::endl;
    std::cout << "Testing GPU acceleration and performance monitoring" << std::endl;
    
    return RUN_ALL_TESTS();
}