#include <gtest/gtest.h>
#include "utils/performance_monitor.hpp"
#include "audio/streaming_optimizer.hpp"
#include "models/model_quantization.hpp"
#include "stt/whisper_stt.hpp"
#include "mt/marian_translator.hpp"
#include "tts/piper_tts.hpp"
#include <chrono>
#include <vector>
#include <random>
#include <thread>
#include <atomic>

using namespace speechrnt;

class LatencyBenchmark : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize performance monitor
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        perfMonitor.initialize(false); // Disable system metrics for benchmarks
        
        // Generate test data
        generateTestData();
    }
    
    void TearDown() override {
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        perfMonitor.cleanup();
    }
    
    void generateTestData() {
        // Generate 5 seconds of test audio at 16kHz
        const int sampleRate = 16000;
        const int durationSeconds = 5;
        const int numSamples = sampleRate * durationSeconds;
        
        testAudioData_.resize(numSamples);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<float> noise(0.0f, 0.1f);
        
        for (int i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            float signal = 0.3f * std::sin(2.0f * M_PI * 440.0f * t); // 440Hz tone
            testAudioData_[i] = signal + noise(gen);
        }
        
        // Generate test text for translation
        testTexts_ = {
            "Hello, how are you today?",
            "The weather is beautiful outside.",
            "I would like to order some food.",
            "Can you help me find the nearest hospital?",
            "Thank you very much for your assistance."
        };
    }
    
    std::vector<float> testAudioData_;
    std::vector<std::string> testTexts_;
};

TEST_F(LatencyBenchmark, AudioStreamingOptimization) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    audio::StreamingOptimizer optimizer;
    ASSERT_TRUE(optimizer.initialize(16000, 1, 50)); // 50ms target latency
    
    // Test different chunk sizes and measure latency
    std::vector<int> targetLatencies = {25, 50, 100, 200}; // milliseconds
    
    for (int targetLatency : targetLatencies) {
        optimizer.initialize(16000, 1, targetLatency);
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        std::vector<audio::AudioChunk> outputChunks;
        bool success = optimizer.processStream(testAudioData_, outputChunks);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            endTime - startTime).count() / 1000.0; // Convert to ms
        
        EXPECT_TRUE(success);
        EXPECT_FALSE(outputChunks.empty());
        
        perfMonitor.recordLatency("benchmark.streaming_latency_" + std::to_string(targetLatency) + "ms", 
                                 latency);
        
        std::cout << "Target: " << targetLatency << "ms, Actual: " << latency 
                  << "ms, Chunks: " << outputChunks.size() << std::endl;
        
        // Verify latency is reasonable (within 2x target)
        EXPECT_LT(latency, targetLatency * 2.0);
    }
    
    // Test adaptive chunking
    optimizer.setAdaptiveChunking(true);
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    std::vector<audio::AudioChunk> adaptiveChunks;
    bool success = optimizer.processStream(testAudioData_, adaptiveChunks);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto adaptiveLatency = std::chrono::duration_cast<std::chrono::microseconds>(
        endTime - startTime).count() / 1000.0;
    
    EXPECT_TRUE(success);
    perfMonitor.recordLatency("benchmark.adaptive_streaming_latency_ms", adaptiveLatency);
    
    std::cout << "Adaptive chunking latency: " << adaptiveLatency << "ms" << std::endl;
}

TEST_F(LatencyBenchmark, WebSocketOptimization) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    audio::WebSocketOptimizer wsOptimizer;
    ASSERT_TRUE(wsOptimizer.initialize(65536, true)); // 64KB max message, compression enabled
    
    // Test different message sizes
    std::vector<size_t> messageSizes = {1024, 4096, 16384, 65536}; // bytes
    
    for (size_t maxSize : messageSizes) {
        wsOptimizer.setMaxMessageSize(maxSize);
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        std::vector<std::vector<uint8_t>> optimizedMessages;
        bool success = wsOptimizer.optimizeForTransmission(testAudioData_, optimizedMessages);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            endTime - startTime).count() / 1000.0;
        
        EXPECT_TRUE(success);
        EXPECT_FALSE(optimizedMessages.empty());
        
        perfMonitor.recordLatency("benchmark.websocket_optimization_" + std::to_string(maxSize) + "b", 
                                 latency);
        
        std::cout << "Max size: " << maxSize << "B, Latency: " << latency 
                  << "ms, Messages: " << optimizedMessages.size() << std::endl;
    }
    
    // Test batching
    audio::StreamingOptimizer streamOptimizer;
    streamOptimizer.initialize(16000, 1, 50);
    
    std::vector<audio::AudioChunk> chunks;
    streamOptimizer.processStream(testAudioData_, chunks);
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    std::vector<std::vector<uint8_t>> batchedMessages;
    bool success = wsOptimizer.batchChunks(chunks, batchedMessages);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto batchLatency = std::chrono::duration_cast<std::chrono::microseconds>(
        endTime - startTime).count() / 1000.0;
    
    EXPECT_TRUE(success);
    perfMonitor.recordLatency("benchmark.websocket_batching_latency_ms", batchLatency);
    
    std::cout << "Batching latency: " << batchLatency << "ms, Batched messages: " 
              << batchedMessages.size() << std::endl;
}

TEST_F(LatencyBenchmark, ModelQuantizationPerformance) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    auto& quantManager = models::QuantizationManager::getInstance();
    
    ASSERT_TRUE(quantManager.initialize());
    
    // Test quantization for different precisions
    std::vector<models::QuantizationPrecision> precisions = {
        models::QuantizationPrecision::FP32,
        models::QuantizationPrecision::FP16,
        models::QuantizationPrecision::INT8
    };
    
    std::vector<std::string> modelTypes = {"whisper", "marian", "piper"};
    
    for (const std::string& modelType : modelTypes) {
        for (models::QuantizationPrecision precision : precisions) {
            models::QuantizationConfig config;
            config.precision = precision;
            
            // Create dummy model file for testing
            std::string inputPath = "test_" + modelType + "_model.bin";
            std::string outputPath = "test_" + modelType + "_quantized_" + 
                                   models::QuantizationManager::precisionToString(precision) + ".bin";
            
            // Create a dummy model file
            std::ofstream dummyFile(inputPath, std::ios::binary);
            std::vector<uint8_t> dummyData(1024 * 1024, 0x42); // 1MB dummy data
            dummyFile.write(reinterpret_cast<const char*>(dummyData.data()), dummyData.size());
            dummyFile.close();
            
            auto startTime = std::chrono::high_resolution_clock::now();
            
            bool success = quantManager.quantizeModel(modelType, inputPath, outputPath, config);
            
            auto endTime = std::chrono::high_resolution_clock::now();
            auto quantizationLatency = std::chrono::duration_cast<std::chrono::milliseconds>(
                endTime - startTime).count();
            
            if (success) {
                std::string metricName = "benchmark.quantization_" + modelType + "_" + 
                                       models::QuantizationManager::precisionToString(precision) + "_ms";
                perfMonitor.recordLatency(metricName, static_cast<double>(quantizationLatency));
                
                std::cout << "Quantized " << modelType << " to " 
                          << models::QuantizationManager::precisionToString(precision) 
                          << " in " << quantizationLatency << "ms" << std::endl;
            }
            
            // Cleanup
            std::filesystem::remove(inputPath);
            std::filesystem::remove(outputPath);
        }
    }
}

TEST_F(LatencyBenchmark, ConcurrentProcessingLatency) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    const int numThreads = 4;
    const int operationsPerThread = 10;
    
    std::atomic<int> completedOperations{0};
    std::vector<std::thread> threads;
    std::vector<double> threadLatencies(numThreads);
    
    // Test concurrent audio processing
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            audio::StreamingOptimizer optimizer;
            optimizer.initialize(16000, 1, 50);
            
            auto threadStartTime = std::chrono::high_resolution_clock::now();
            
            for (int op = 0; op < operationsPerThread; ++op) {
                std::vector<audio::AudioChunk> chunks;
                optimizer.processStream(testAudioData_, chunks);
                completedOperations++;
            }
            
            auto threadEndTime = std::chrono::high_resolution_clock::now();
            threadLatencies[t] = std::chrono::duration_cast<std::chrono::microseconds>(
                threadEndTime - threadStartTime).count() / 1000.0;
        });
    }
    
    auto overallStartTime = std::chrono::high_resolution_clock::now();
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto overallEndTime = std::chrono::high_resolution_clock::now();
    auto overallLatency = std::chrono::duration_cast<std::chrono::milliseconds>(
        overallEndTime - overallStartTime).count();
    
    EXPECT_EQ(completedOperations.load(), numThreads * operationsPerThread);
    
    // Calculate statistics
    double avgThreadLatency = 0.0;
    double maxThreadLatency = 0.0;
    for (double latency : threadLatencies) {
        avgThreadLatency += latency;
        maxThreadLatency = std::max(maxThreadLatency, latency);
    }
    avgThreadLatency /= numThreads;
    
    perfMonitor.recordLatency("benchmark.concurrent_processing_overall_ms", 
                             static_cast<double>(overallLatency));
    perfMonitor.recordLatency("benchmark.concurrent_processing_avg_thread_ms", avgThreadLatency);
    perfMonitor.recordLatency("benchmark.concurrent_processing_max_thread_ms", maxThreadLatency);
    
    double throughput = static_cast<double>(completedOperations.load()) / (overallLatency / 1000.0);
    perfMonitor.recordThroughput("benchmark.concurrent_processing_ops_per_sec", throughput);
    
    std::cout << "Concurrent processing results:" << std::endl;
    std::cout << "  Overall latency: " << overallLatency << "ms" << std::endl;
    std::cout << "  Average thread latency: " << avgThreadLatency << "ms" << std::endl;
    std::cout << "  Max thread latency: " << maxThreadLatency << "ms" << std::endl;
    std::cout << "  Throughput: " << throughput << " ops/sec" << std::endl;
}

TEST_F(LatencyBenchmark, EndToEndPipelineLatency) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Simulate end-to-end pipeline latency
    std::vector<std::string> pipelineStages = {
        "audio_capture",
        "vad_processing", 
        "stt_transcription",
        "translation",
        "tts_synthesis",
        "audio_playback"
    };
    
    // Simulate realistic latencies for each stage (in milliseconds)
    std::map<std::string, std::pair<double, double>> stageLatencies = {
        {"audio_capture", {5.0, 15.0}},      // 5-15ms
        {"vad_processing", {2.0, 8.0}},      // 2-8ms
        {"stt_transcription", {100.0, 500.0}}, // 100-500ms
        {"translation", {50.0, 300.0}},      // 50-300ms
        {"tts_synthesis", {200.0, 800.0}},   // 200-800ms
        {"audio_playback", {10.0, 50.0}}     // 10-50ms
    };
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    const int numRuns = 20;
    
    for (int run = 0; run < numRuns; ++run) {
        auto pipelineStartTime = std::chrono::high_resolution_clock::now();
        double totalLatency = 0.0;
        
        for (const std::string& stage : pipelineStages) {
            auto stageStartTime = std::chrono::high_resolution_clock::now();
            
            // Simulate stage processing time
            auto [minLatency, maxLatency] = stageLatencies[stage];
            std::uniform_real_distribution<double> latencyDist(minLatency, maxLatency);
            double stageLatency = latencyDist(gen);
            
            // Simulate processing delay
            std::this_thread::sleep_for(std::chrono::microseconds(
                static_cast<int>(stageLatency * 100))); // Scale down for testing
            
            auto stageEndTime = std::chrono::high_resolution_clock::now();
            double actualStageLatency = std::chrono::duration_cast<std::chrono::microseconds>(
                stageEndTime - stageStartTime).count() / 1000.0;
            
            perfMonitor.recordLatency("benchmark.pipeline_" + stage + "_latency_ms", 
                                     actualStageLatency);
            totalLatency += actualStageLatency;
        }
        
        auto pipelineEndTime = std::chrono::high_resolution_clock::now();
        double actualPipelineLatency = std::chrono::duration_cast<std::chrono::microseconds>(
            pipelineEndTime - pipelineStartTime).count() / 1000.0;
        
        perfMonitor.recordLatency("benchmark.pipeline_end_to_end_latency_ms", 
                                 actualPipelineLatency);
        
        std::cout << "Run " << (run + 1) << ": End-to-end latency = " 
                  << actualPipelineLatency << "ms" << std::endl;
    }
    
    // Analyze results
    auto endToEndStats = perfMonitor.getMetricStats("benchmark.pipeline_end_to_end_latency_ms");
    
    std::cout << "\nEnd-to-end pipeline latency statistics:" << std::endl;
    std::cout << "  Mean: " << endToEndStats.mean << "ms" << std::endl;
    std::cout << "  Min: " << endToEndStats.min << "ms" << std::endl;
    std::cout << "  Max: " << endToEndStats.max << "ms" << std::endl;
    std::cout << "  P95: " << endToEndStats.p95 << "ms" << std::endl;
    std::cout << "  P99: " << endToEndStats.p99 << "ms" << std::endl;
    
    // Verify latency targets
    EXPECT_LT(endToEndStats.p95, 2000.0); // P95 should be under 2 seconds
    EXPECT_LT(endToEndStats.mean, 1000.0); // Mean should be under 1 second
}

// Benchmark main function
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "=== Latency and Throughput Benchmark Suite ===" << std::endl;
    std::cout << "Testing streaming optimizations and latency measurements" << std::endl;
    
    return RUN_ALL_TESTS();
}