#include <gtest/gtest.h>
#include "stt/whisper_stt.hpp"
#include "stt/transcription_manager.hpp"
#include "audio/voice_activity_detector.hpp"
#include "audio/audio_buffer_manager.hpp"
#include "utils/performance_monitor.hpp"
#include "fixtures/test_data_generator.hpp"
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <iomanip>

namespace stt_performance {

class STTPerformanceBenchmark : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize performance monitor
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        perfMonitor.initialize(true); // Enable detailed metrics
        
        // Initialize test data generator
        testDataGenerator_ = std::make_unique<fixtures::TestDataGenerator>();
        
        // Initialize STT components
        whisperSTT_ = std::make_unique<stt::WhisperSTT>();
        transcriptionManager_ = std::make_unique<stt::TranscriptionManager>();
        vadDetector_ = std::make_unique<audio::VoiceActivityDetector>();
        bufferManager_ = std::make_unique<audio::AudioBufferManager>();
        
        // Generate benchmark test data
        generateBenchmarkData();
    }
    
    void TearDown() override {
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        
        // Generate performance report
        generatePerformanceReport();
        
        perfMonitor.cleanup();
    }
    
    void generateBenchmarkData() {
        // Generate audio samples of various lengths for latency testing
        benchmarkAudioSamples_.clear();
        
        // Very short utterances (0.2-0.5 seconds) - for responsiveness testing
        for (int i = 0; i < 10; ++i) {
            float duration = 0.2f + (i * 0.03f); // 0.2s to 0.47s
            benchmarkAudioSamples_["very_short_" + std::to_string(i)] = 
                testDataGenerator_->generateSpeechAudio(duration, 16000);
        }
        
        // Short utterances (0.5-2.0 seconds) - typical user speech
        for (int i = 0; i < 15; ++i) {
            float duration = 0.5f + (i * 0.1f); // 0.5s to 1.9s
            benchmarkAudioSamples_["short_" + std::to_string(i)] = 
                testDataGenerator_->generateSpeechAudio(duration, 16000);
        }
        
        // Medium utterances (2.0-5.0 seconds) - longer sentences
        for (int i = 0; i < 10; ++i) {
            float duration = 2.0f + (i * 0.3f); // 2.0s to 4.7s
            benchmarkAudioSamples_["medium_" + std::to_string(i)] = 
                testDataGenerator_->generateSpeechAudio(duration, 16000);
        }
        
        // Long utterances (5.0-10.0 seconds) - stress testing
        for (int i = 0; i < 5; ++i) {
            float duration = 5.0f + (i * 1.0f); // 5.0s to 9.0s
            benchmarkAudioSamples_["long_" + std::to_string(i)] = 
                testDataGenerator_->generateSpeechAudio(duration, 16000);
        }
        
        // Noisy audio samples for robustness testing
        for (int i = 0; i < 5; ++i) {
            float noiseLevel = 0.1f + (i * 0.1f); // 10% to 50% noise
            benchmarkAudioSamples_["noisy_" + std::to_string(i)] = 
                testDataGenerator_->generateNoisyAudio(2.0f, 16000, noiseLevel);
        }
    }
    
    void generatePerformanceReport() {
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        
        std::ofstream report("stt_performance_report.txt");
        report << "=== STT Performance Benchmark Report ===" << std::endl;
        report << "Generated at: " << std::chrono::system_clock::now().time_since_epoch().count() << std::endl;
        report << std::endl;
        
        // Get all recorded metrics
        auto metrics = perfMonitor.getAllMetrics();
        
        report << "Performance Metrics Summary:" << std::endl;
        report << std::setw(40) << "Metric" << std::setw(15) << "Mean" << std::setw(15) << "P95" << std::setw(15) << "P99" << std::setw(15) << "Max" << std::endl;
        report << std::string(100, '-') << std::endl;
        
        for (const auto& [metricName, stats] : metrics) {
            if (metricName.find("benchmark") != std::string::npos) {
                report << std::setw(40) << metricName 
                       << std::setw(15) << std::fixed << std::setprecision(2) << stats.mean
                       << std::setw(15) << std::fixed << std::setprecision(2) << stats.p95
                       << std::setw(15) << std::fixed << std::setprecision(2) << stats.p99
                       << std::setw(15) << std::fixed << std::setprecision(2) << stats.max
                       << std::endl;
            }
        }
        
        report.close();
        std::cout << "Performance report saved to stt_performance_report.txt" << std::endl;
    }
    
    struct LatencyStats {
        double mean;
        double median;
        double p95;
        double p99;
        double min;
        double max;
        double stddev;
    };
    
    LatencyStats calculateLatencyStats(const std::vector<double>& latencies) {
        if (latencies.empty()) {
            return {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        }
        
        std::vector<double> sorted = latencies;
        std::sort(sorted.begin(), sorted.end());
        
        double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();
        double median = sorted[sorted.size() / 2];
        double p95 = sorted[static_cast<size_t>(sorted.size() * 0.95)];
        double p99 = sorted[static_cast<size_t>(sorted.size() * 0.99)];
        double min = sorted.front();
        double max = sorted.back();
        
        // Calculate standard deviation
        double variance = 0.0;
        for (double latency : latencies) {
            variance += (latency - mean) * (latency - mean);
        }
        variance /= latencies.size();
        double stddev = std::sqrt(variance);
        
        return {mean, median, p95, p99, min, max, stddev};
    }
    
    std::unique_ptr<fixtures::TestDataGenerator> testDataGenerator_;
    std::unique_ptr<stt::WhisperSTT> whisperSTT_;
    std::unique_ptr<stt::TranscriptionManager> transcriptionManager_;
    std::unique_ptr<audio::VoiceActivityDetector> vadDetector_;
    std::unique_ptr<audio::AudioBufferManager> bufferManager_;
    
    std::map<std::string, std::vector<float>> benchmarkAudioSamples_;
};

// Benchmark 1: VAD Latency Performance
TEST_F(STTPerformanceBenchmark, VADLatencyBenchmark) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Initialize VAD
    ASSERT_TRUE(vadDetector_->initialize("test_models/silero_vad.onnx"));
    
    const int numIterations = 100;
    std::vector<double> vadLatencies;
    
    std::cout << "Running VAD latency benchmark (" << numIterations << " iterations)..." << std::endl;
    
    for (int i = 0; i < numIterations; ++i) {
        // Use different audio samples for each iteration
        std::string sampleKey = "short_" + std::to_string(i % 15);
        auto testAudio = benchmarkAudioSamples_[sampleKey];
        
        auto startTime = std::chrono::high_resolution_clock::now();
        float vadProbability = vadDetector_->getVoiceActivityProbability(testAudio);
        auto endTime = std::chrono::high_resolution_clock::now();
        
        double latency = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        vadLatencies.push_back(latency);
        
        // Verify VAD is working
        EXPECT_GE(vadProbability, 0.0f);
        EXPECT_LE(vadProbability, 1.0f);
    }
    
    auto stats = calculateLatencyStats(vadLatencies);
    
    // Record metrics
    perfMonitor.recordLatency("benchmark.vad_latency_mean_ms", stats.mean);
    perfMonitor.recordLatency("benchmark.vad_latency_p95_ms", stats.p95);
    perfMonitor.recordLatency("benchmark.vad_latency_p99_ms", stats.p99);
    perfMonitor.recordLatency("benchmark.vad_latency_max_ms", stats.max);
    
    std::cout << "VAD Latency Benchmark Results:" << std::endl;
    std::cout << "  Mean: " << stats.mean << "ms" << std::endl;
    std::cout << "  Median: " << stats.median << "ms" << std::endl;
    std::cout << "  P95: " << stats.p95 << "ms" << std::endl;
    std::cout << "  P99: " << stats.p99 << "ms" << std::endl;
    std::cout << "  Range: [" << stats.min << ", " << stats.max << "]ms" << std::endl;
    std::cout << "  Std Dev: " << stats.stddev << "ms" << std::endl;
    
    // Performance requirements
    EXPECT_LT(stats.p95, 100.0) << "VAD P95 latency should be under 100ms (requirement)";
    EXPECT_LT(stats.mean, 50.0) << "VAD mean latency should be under 50ms";
    EXPECT_LT(stats.max, 200.0) << "VAD max latency should be under 200ms";
}

// Benchmark 2: STT Transcription Latency by Audio Length
TEST_F(STTPerformanceBenchmark, STTLatencyByAudioLength) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Initialize STT
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    // Test different audio length categories
    std::vector<std::string> categories = {"very_short", "short", "medium", "long"};
    std::map<std::string, std::vector<double>> categoryLatencies;
    
    for (const std::string& category : categories) {
        std::cout << "Benchmarking " << category << " audio samples..." << std::endl;
        
        for (const auto& [sampleName, audioData] : benchmarkAudioSamples_) {
            if (sampleName.find(category) == 0) {
                uint32_t utteranceId = std::hash<std::string>{}(sampleName);
                
                std::atomic<bool> transcriptionComplete{false};
                std::string result;
                float confidence = 0.0f;
                
                stt::TranscriptionRequest request;
                request.utterance_id = utteranceId;
                request.audio_data = audioData;
                request.is_live = false;
                request.callback = [&](uint32_t id, const stt::TranscriptionResult& transcriptionResult) {
                    result = transcriptionResult.text;
                    confidence = transcriptionResult.confidence;
                    transcriptionComplete = true;
                };
                
                auto startTime = std::chrono::high_resolution_clock::now();
                transcriptionManager_->submitTranscription(request);
                
                // Wait for completion
                auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(15);
                while (!transcriptionComplete.load() && std::chrono::high_resolution_clock::now() < timeout) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                
                auto endTime = std::chrono::high_resolution_clock::now();
                double latency = std::chrono::duration<double, std::milli>(endTime - startTime).count();
                
                if (transcriptionComplete.load()) {
                    categoryLatencies[category].push_back(latency);
                    
                    // Verify transcription quality
                    EXPECT_FALSE(result.empty()) << "Should get transcription for " << sampleName;
                    EXPECT_GT(confidence, 0.0f) << "Should get confidence score for " << sampleName;
                }
            }
        }
    }
    
    // Analyze results by category
    for (const std::string& category : categories) {
        if (!categoryLatencies[category].empty()) {
            auto stats = calculateLatencyStats(categoryLatencies[category]);
            
            std::string metricPrefix = "benchmark.stt_" + category + "_";
            perfMonitor.recordLatency(metricPrefix + "mean_ms", stats.mean);
            perfMonitor.recordLatency(metricPrefix + "p95_ms", stats.p95);
            perfMonitor.recordLatency(metricPrefix + "p99_ms", stats.p99);
            
            std::cout << category << " Audio STT Latency:" << std::endl;
            std::cout << "  Samples: " << categoryLatencies[category].size() << std::endl;
            std::cout << "  Mean: " << stats.mean << "ms" << std::endl;
            std::cout << "  P95: " << stats.p95 << "ms" << std::endl;
            std::cout << "  P99: " << stats.p99 << "ms" << std::endl;
            std::cout << "  Range: [" << stats.min << ", " << stats.max << "]ms" << std::endl;
        }
    }
    
    // Performance requirements based on audio length
    if (!categoryLatencies["very_short"].empty()) {
        auto veryShortStats = calculateLatencyStats(categoryLatencies["very_short"]);
        EXPECT_LT(veryShortStats.p95, 300.0) << "Very short audio P95 latency should be under 300ms";
    }
    
    if (!categoryLatencies["short"].empty()) {
        auto shortStats = calculateLatencyStats(categoryLatencies["short"]);
        EXPECT_LT(shortStats.p95, 500.0) << "Short audio P95 latency should be under 500ms (requirement)";
    }
    
    if (!categoryLatencies["medium"].empty()) {
        auto mediumStats = calculateLatencyStats(categoryLatencies["medium"]);
        EXPECT_LT(mediumStats.p95, 1000.0) << "Medium audio P95 latency should be under 1000ms";
    }
    
    transcriptionManager_->stop();
}

// Benchmark 3: Concurrent Transcription Throughput
TEST_F(STTPerformanceBenchmark, ConcurrentTranscriptionThroughput) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Initialize STT
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    // Test different concurrency levels
    std::vector<int> concurrencyLevels = {1, 2, 4, 8, 16};
    
    for (int concurrency : concurrencyLevels) {
        std::cout << "Testing concurrency level: " << concurrency << std::endl;
        
        const int transcriptionsPerThread = 10;
        std::vector<std::future<std::vector<double>>> futures;
        std::atomic<int> completedTranscriptions{0};
        
        auto overallStart = std::chrono::high_resolution_clock::now();
        
        // Start concurrent transcription tasks
        for (int threadId = 0; threadId < concurrency; ++threadId) {
            futures.push_back(std::async(std::launch::async, [&, threadId]() {
                std::vector<double> threadLatencies;
                
                for (int i = 0; i < transcriptionsPerThread; ++i) {
                    uint32_t utteranceId = threadId * 1000 + i;
                    
                    // Use short audio samples for throughput testing
                    std::string sampleKey = "short_" + std::to_string(i % 15);
                    auto testAudio = benchmarkAudioSamples_[sampleKey];
                    
                    std::atomic<bool> transcriptionComplete{false};
                    
                    stt::TranscriptionRequest request;
                    request.utterance_id = utteranceId;
                    request.audio_data = testAudio;
                    request.is_live = false;
                    request.callback = [&](uint32_t id, const stt::TranscriptionResult& result) {
                        transcriptionComplete = true;
                        completedTranscriptions++;
                    };
                    
                    auto requestStart = std::chrono::high_resolution_clock::now();
                    transcriptionManager_->submitTranscription(request);
                    
                    // Wait for completion
                    auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(10);
                    while (!transcriptionComplete.load() && std::chrono::high_resolution_clock::now() < timeout) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    
                    if (transcriptionComplete.load()) {
                        auto requestEnd = std::chrono::high_resolution_clock::now();
                        double latency = std::chrono::duration<double, std::milli>(requestEnd - requestStart).count();
                        threadLatencies.push_back(latency);
                    }
                }
                
                return threadLatencies;
            }));
        }
        
        // Collect results
        std::vector<double> allLatencies;
        for (auto& future : futures) {
            auto threadLatencies = future.get();
            allLatencies.insert(allLatencies.end(), threadLatencies.begin(), threadLatencies.end());
        }
        
        auto overallEnd = std::chrono::high_resolution_clock::now();
        double totalDuration = std::chrono::duration<double>(overallEnd - overallStart).count();
        
        // Calculate throughput metrics
        double throughput = completedTranscriptions.load() / totalDuration;
        double successRate = static_cast<double>(allLatencies.size()) / (concurrency * transcriptionsPerThread);
        
        if (!allLatencies.empty()) {
            auto stats = calculateLatencyStats(allLatencies);
            
            std::string metricPrefix = "benchmark.concurrent_" + std::to_string(concurrency) + "_";
            perfMonitor.recordThroughput(metricPrefix + "throughput_per_sec", throughput);
            perfMonitor.recordLatency(metricPrefix + "avg_latency_ms", stats.mean);
            perfMonitor.recordLatency(metricPrefix + "p95_latency_ms", stats.p95);
            perfMonitor.recordMetric(metricPrefix + "success_rate", successRate);
            
            std::cout << "  Throughput: " << throughput << " transcriptions/sec" << std::endl;
            std::cout << "  Success rate: " << (successRate * 100) << "%" << std::endl;
            std::cout << "  Average latency: " << stats.mean << "ms" << std::endl;
            std::cout << "  P95 latency: " << stats.p95 << "ms" << std::endl;
        }
    }
    
    transcriptionManager_->stop();
}

// Benchmark 4: Memory Usage Under Load
TEST_F(STTPerformanceBenchmark, MemoryUsageBenchmark) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Initialize components
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    const int numIterations = 100;
    const int transcriptionsPerIteration = 5;
    
    std::vector<size_t> memoryUsageSamples;
    
    for (int iteration = 0; iteration < numIterations; ++iteration) {
        // Submit multiple transcriptions
        std::vector<std::future<void>> futures;
        
        for (int i = 0; i < transcriptionsPerIteration; ++i) {
            futures.push_back(std::async(std::launch::async, [&, iteration, i]() {
                uint32_t utteranceId = iteration * 100 + i;
                
                std::string sampleKey = "medium_" + std::to_string(i % 10);
                auto testAudio = benchmarkAudioSamples_[sampleKey];
                
                std::atomic<bool> transcriptionComplete{false};
                
                stt::TranscriptionRequest request;
                request.utterance_id = utteranceId;
                request.audio_data = testAudio;
                request.is_live = false;
                request.callback = [&](uint32_t id, const stt::TranscriptionResult& result) {
                    transcriptionComplete = true;
                };
                
                transcriptionManager_->submitTranscription(request);
                
                // Wait for completion
                auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(8);
                while (!transcriptionComplete.load() && std::chrono::high_resolution_clock::now() < timeout) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }));
        }
        
        // Wait for all transcriptions to complete
        for (auto& future : futures) {
            future.wait();
        }
        
        // Sample memory usage
        size_t currentMemory = getCurrentMemoryUsage();
        memoryUsageSamples.push_back(currentMemory);
        
        // Log memory usage periodically
        if (iteration % 20 == 0) {
            std::cout << "Iteration " << iteration << ", Memory: " << (currentMemory / 1024 / 1024) << " MB" << std::endl;
        }
        
        // Small delay between iterations
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Analyze memory usage
    if (!memoryUsageSamples.empty()) {
        size_t minMemory = *std::min_element(memoryUsageSamples.begin(), memoryUsageSamples.end());
        size_t maxMemory = *std::max_element(memoryUsageSamples.begin(), memoryUsageSamples.end());
        size_t avgMemory = std::accumulate(memoryUsageSamples.begin(), memoryUsageSamples.end(), 0ULL) / memoryUsageSamples.size();
        
        perfMonitor.recordMetric("benchmark.memory_min_mb", static_cast<double>(minMemory / 1024 / 1024));
        perfMonitor.recordMetric("benchmark.memory_max_mb", static_cast<double>(maxMemory / 1024 / 1024));
        perfMonitor.recordMetric("benchmark.memory_avg_mb", static_cast<double>(avgMemory / 1024 / 1024));
        
        std::cout << "Memory Usage Benchmark Results:" << std::endl;
        std::cout << "  Min memory: " << (minMemory / 1024 / 1024) << " MB" << std::endl;
        std::cout << "  Max memory: " << (maxMemory / 1024 / 1024) << " MB" << std::endl;
        std::cout << "  Avg memory: " << (avgMemory / 1024 / 1024) << " MB" << std::endl;
        std::cout << "  Memory growth: " << ((maxMemory - minMemory) / 1024 / 1024) << " MB" << std::endl;
        
        // Memory usage assertions
        EXPECT_LT(maxMemory, 2ULL * 1024 * 1024 * 1024) << "Max memory should be under 2GB";
        EXPECT_LT(maxMemory - minMemory, 500ULL * 1024 * 1024) << "Memory growth should be under 500MB";
    }
    
    transcriptionManager_->stop();
}

// Benchmark 5: Noise Robustness Performance
TEST_F(STTPerformanceBenchmark, NoiseRobustnessBenchmark) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Initialize STT
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    std::vector<double> cleanAudioLatencies;
    std::vector<double> noisyAudioLatencies;
    std::vector<float> cleanAudioConfidences;
    std::vector<float> noisyAudioConfidences;
    
    // Test clean audio samples
    std::cout << "Testing clean audio performance..." << std::endl;
    for (int i = 0; i < 10; ++i) {
        std::string sampleKey = "short_" + std::to_string(i);
        auto testAudio = benchmarkAudioSamples_[sampleKey];
        uint32_t utteranceId = 7000 + i;
        
        std::atomic<bool> transcriptionComplete{false};
        float confidence = 0.0f;
        
        stt::TranscriptionRequest request;
        request.utterance_id = utteranceId;
        request.audio_data = testAudio;
        request.is_live = false;
        request.callback = [&](uint32_t id, const stt::TranscriptionResult& result) {
            confidence = result.confidence;
            transcriptionComplete = true;
        };
        
        auto startTime = std::chrono::high_resolution_clock::now();
        transcriptionManager_->submitTranscription(request);
        
        // Wait for completion
        auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(8);
        while (!transcriptionComplete.load() && std::chrono::high_resolution_clock::now() < timeout) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        if (transcriptionComplete.load()) {
            auto endTime = std::chrono::high_resolution_clock::now();
            double latency = std::chrono::duration<double, std::milli>(endTime - startTime).count();
            cleanAudioLatencies.push_back(latency);
            cleanAudioConfidences.push_back(confidence);
        }
    }
    
    // Test noisy audio samples
    std::cout << "Testing noisy audio performance..." << std::endl;
    for (int i = 0; i < 5; ++i) {
        std::string sampleKey = "noisy_" + std::to_string(i);
        auto testAudio = benchmarkAudioSamples_[sampleKey];
        uint32_t utteranceId = 8000 + i;
        
        std::atomic<bool> transcriptionComplete{false};
        float confidence = 0.0f;
        
        stt::TranscriptionRequest request;
        request.utterance_id = utteranceId;
        request.audio_data = testAudio;
        request.is_live = false;
        request.callback = [&](uint32_t id, const stt::TranscriptionResult& result) {
            confidence = result.confidence;
            transcriptionComplete = true;
        };
        
        auto startTime = std::chrono::high_resolution_clock::now();
        transcriptionManager_->submitTranscription(request);
        
        // Wait for completion
        auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(10);
        while (!transcriptionComplete.load() && std::chrono::high_resolution_clock::now() < timeout) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        if (transcriptionComplete.load()) {
            auto endTime = std::chrono::high_resolution_clock::now();
            double latency = std::chrono::duration<double, std::milli>(endTime - startTime).count();
            noisyAudioLatencies.push_back(latency);
            noisyAudioConfidences.push_back(confidence);
        }
    }
    
    // Compare performance
    if (!cleanAudioLatencies.empty() && !noisyAudioLatencies.empty()) {
        auto cleanStats = calculateLatencyStats(cleanAudioLatencies);
        auto noisyStats = calculateLatencyStats(noisyAudioLatencies);
        
        float avgCleanConfidence = std::accumulate(cleanAudioConfidences.begin(), cleanAudioConfidences.end(), 0.0f) / cleanAudioConfidences.size();
        float avgNoisyConfidence = std::accumulate(noisyAudioConfidences.begin(), noisyAudioConfidences.end(), 0.0f) / noisyAudioConfidences.size();
        
        perfMonitor.recordLatency("benchmark.clean_audio_latency_ms", cleanStats.mean);
        perfMonitor.recordLatency("benchmark.noisy_audio_latency_ms", noisyStats.mean);
        perfMonitor.recordMetric("benchmark.clean_audio_confidence", static_cast<double>(avgCleanConfidence));
        perfMonitor.recordMetric("benchmark.noisy_audio_confidence", static_cast<double>(avgNoisyConfidence));
        
        std::cout << "Noise Robustness Results:" << std::endl;
        std::cout << "  Clean audio - Latency: " << cleanStats.mean << "ms, Confidence: " << avgCleanConfidence << std::endl;
        std::cout << "  Noisy audio - Latency: " << noisyStats.mean << "ms, Confidence: " << avgNoisyConfidence << std::endl;
        std::cout << "  Latency degradation: " << ((noisyStats.mean - cleanStats.mean) / cleanStats.mean * 100) << "%" << std::endl;
        std::cout << "  Confidence degradation: " << ((avgCleanConfidence - avgNoisyConfidence) / avgCleanConfidence * 100) << "%" << std::endl;
        
        // Robustness assertions
        EXPECT_LT(noisyStats.mean / cleanStats.mean, 2.0) << "Noisy audio latency should not be more than 2x clean audio";
        EXPECT_GT(avgNoisyConfidence, 0.3f) << "Noisy audio should still have reasonable confidence";
    }
    
    transcriptionManager_->stop();
}

// Helper function to get current memory usage
size_t STTPerformanceBenchmark::getCurrentMemoryUsage() {
    // Platform-specific memory usage implementation
    // This is a simplified mock implementation
    static size_t baseMemory = 200 * 1024 * 1024; // 200MB base
    static std::atomic<size_t> memoryCounter{0};
    
    return baseMemory + memoryCounter.fetch_add(1024 * 1024); // Simulate 1MB growth per call
}

} // namespace stt_performance

// Main function for standalone execution
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "=== STT Performance Benchmark Suite ===" << std::endl;
    std::cout << "Testing STT latency requirements, throughput, and robustness" << std::endl;
    
    return RUN_ALL_TESTS();
}