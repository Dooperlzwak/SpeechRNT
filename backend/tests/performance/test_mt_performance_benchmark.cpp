#include <gtest/gtest.h>
#include "mt/marian_translator.hpp"
#include "mt/language_detector.hpp"
#include "mt/gpu_accelerator.hpp"
#include "mt/quality_manager.hpp"
#include "utils/performance_monitor.hpp"
#include <chrono>
#include <thread>
#include <atomic>
#include <random>
#include <memory>
#include <vector>
#include <future>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <iomanip>

namespace speechrnt {
namespace performance {

class MTPerformanceBenchmark : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize performance monitor
        perfMonitor_ = &utils::PerformanceMonitor::getInstance();
        perfMonitor_->initialize(false);
        
        // Initialize components
        setupComponents();
        generateBenchmarkData();
    }
    
    void TearDown() override {
        if (translator_) {
            translator_->cleanup();
        }
        if (languageDetector_) {
            languageDetector_->cleanup();
        }
        if (gpuAccelerator_) {
            gpuAccelerator_->cleanup();
        }
        if (perfMonitor_) {
            // Export performance results
            exportPerformanceResults();
            perfMonitor_->cleanup();
        }
    }
    
    void setupComponents() {
        translator_ = std::make_unique<mt::MarianTranslator>();
        translator_->setModelsPath("backend/data/marian/");
        
        languageDetector_ = std::make_unique<mt::LanguageDetector>();
        languageDetector_->initialize("backend/config/language_detection.json");
        
        gpuAccelerator_ = std::make_unique<mt::GPUAccelerator>();
        gpuAccelerator_->initialize();
        
        qualityManager_ = std::make_unique<mt::QualityManager>();
        qualityManager_->initialize("backend/config/quality_assessment.json");
    }
    
    void generateBenchmarkData() {
        // Generate test texts of various lengths and complexities
        benchmarkTexts_ = {
            // Short texts (1-10 words)
            {"Hello", "short"},
            {"How are you?", "short"},
            {"Good morning", "short"},
            {"Thank you very much", "short"},
            {"What is your name?", "short"},
            
            // Medium texts (10-30 words)
            {"Hello, how are you doing today? I hope everything is going well with your work.", "medium"},
            {"The weather is beautiful outside and I think we should go for a walk in the park.", "medium"},
            {"Could you please help me find the nearest restaurant that serves Italian food?", "medium"},
            {"I would like to make a reservation for two people at seven o'clock this evening.", "medium"},
            {"The meeting has been postponed until next week due to scheduling conflicts.", "medium"},
            
            // Long texts (30-100 words)
            {"This is a longer text that contains multiple sentences and should test the translation system's ability to handle more complex linguistic structures. The system should maintain good performance even with increased text length and complexity, while preserving the meaning and context of the original message.", "long"},
            {"In today's globalized world, effective communication across language barriers has become increasingly important for businesses, educational institutions, and individuals alike. Machine translation technology plays a crucial role in breaking down these barriers and enabling seamless cross-cultural communication.", "long"},
            {"The development of neural machine translation systems has revolutionized the field of computational linguistics, providing more accurate and contextually appropriate translations compared to traditional statistical methods. These systems can now handle complex grammatical structures and idiomatic expressions with remarkable precision.", "long"},
            
            // Very long texts (100+ words)
            {"This is an extensive text passage designed to thoroughly test the translation system's performance under demanding conditions. It contains multiple sentences with varying complexity levels, different grammatical structures, and diverse vocabulary. The purpose is to evaluate how well the system maintains translation quality and processing speed when dealing with longer inputs that might be encountered in real-world scenarios such as document translation, article processing, or extended conversation handling. The system should demonstrate consistent performance metrics including latency, accuracy, and resource utilization throughout the entire translation process, regardless of the input length or linguistic complexity.", "very_long"},
            {"Machine translation has evolved significantly over the past decades, transitioning from rule-based systems to statistical approaches, and finally to the current state-of-the-art neural networks. This evolution has been driven by advances in computational power, the availability of large parallel corpora, and breakthroughs in deep learning architectures. Modern neural machine translation systems, particularly those based on transformer architectures, have achieved remarkable improvements in translation quality across numerous language pairs. However, challenges remain in handling low-resource languages, domain-specific terminology, and maintaining consistency in longer documents. The integration of additional technologies such as language detection, quality estimation, and post-editing tools has further enhanced the practical utility of these systems in real-world applications.", "very_long"}
        };
        
        // Language pairs for testing
        languagePairs_ = {
            {"en", "es"}, {"en", "fr"}, {"en", "de"}, {"en", "it"},
            {"es", "en"}, {"fr", "en"}, {"de", "en"}, {"it", "en"}
        };
    }
    
    void exportPerformanceResults() {
        std::string jsonResults = perfMonitor_->exportMetricsJSON(3600); // Last hour
        
        std::ofstream resultsFile("backend/tests/performance/mt_benchmark_results.json");
        if (resultsFile.is_open()) {
            resultsFile << jsonResults;
            resultsFile.close();
            std::cout << "Performance results exported to mt_benchmark_results.json" << std::endl;
        }
    }
    
    struct BenchmarkResult {
        std::string testName;
        double avgLatency;
        double minLatency;
        double maxLatency;
        double p95Latency;
        double p99Latency;
        double throughput;
        double successRate;
        size_t totalOperations;
    };
    
    BenchmarkResult calculateBenchmarkStats(const std::string& testName, 
                                           const std::vector<double>& latencies,
                                           const std::vector<bool>& successes) {
        BenchmarkResult result;
        result.testName = testName;
        result.totalOperations = latencies.size();
        
        if (latencies.empty()) {
            return result;
        }
        
        // Calculate latency statistics
        std::vector<double> sortedLatencies = latencies;
        std::sort(sortedLatencies.begin(), sortedLatencies.end());
        
        result.avgLatency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
        result.minLatency = sortedLatencies.front();
        result.maxLatency = sortedLatencies.back();
        result.p95Latency = sortedLatencies[static_cast<size_t>(sortedLatencies.size() * 0.95)];
        result.p99Latency = sortedLatencies[static_cast<size_t>(sortedLatencies.size() * 0.99)];
        
        // Calculate throughput (operations per second)
        double totalTime = std::accumulate(latencies.begin(), latencies.end(), 0.0) / 1000.0; // Convert to seconds
        result.throughput = latencies.size() / totalTime;
        
        // Calculate success rate
        int successCount = std::count(successes.begin(), successes.end(), true);
        result.successRate = static_cast<double>(successCount) / successes.size();
        
        return result;
    }
    
    void printBenchmarkResult(const BenchmarkResult& result) {
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "=== " << result.testName << " ===" << std::endl;
        std::cout << "Total Operations: " << result.totalOperations << std::endl;
        std::cout << "Success Rate: " << (result.successRate * 100) << "%" << std::endl;
        std::cout << "Latency Statistics (ms):" << std::endl;
        std::cout << "  Average: " << result.avgLatency << std::endl;
        std::cout << "  Min: " << result.minLatency << std::endl;
        std::cout << "  Max: " << result.maxLatency << std::endl;
        std::cout << "  P95: " << result.p95Latency << std::endl;
        std::cout << "  P99: " << result.p99Latency << std::endl;
        std::cout << "Throughput: " << result.throughput << " ops/sec" << std::endl;
        std::cout << std::endl;
    }
    
    std::unique_ptr<mt::MarianTranslator> translator_;
    std::unique_ptr<mt::LanguageDetector> languageDetector_;
    std::unique_ptr<mt::GPUAccelerator> gpuAccelerator_;
    std::unique_ptr<mt::QualityManager> qualityManager_;
    utils::PerformanceMonitor* perfMonitor_;
    
    std::vector<std::pair<std::string, std::string>> benchmarkTexts_;
    std::vector<std::pair<std::string, std::string>> languagePairs_;
};

// Benchmark 1: Translation Latency by Text Length
TEST_F(MTPerformanceBenchmark, TranslationLatencyByTextLength) {
    ASSERT_TRUE(translator_->initialize("en", "es"));
    
    std::map<std::string, std::vector<double>> latenciesByCategory;
    std::map<std::string, std::vector<bool>> successesByCategory;
    
    const int iterationsPerText = 10;
    
    for (const auto& [text, category] : benchmarkTexts_) {
        for (int i = 0; i < iterationsPerText; ++i) {
            auto startTime = std::chrono::high_resolution_clock::now();
            
            auto result = translator_->translate(text);
            
            auto endTime = std::chrono::high_resolution_clock::now();
            double latency = std::chrono::duration_cast<std::chrono::microseconds>(
                endTime - startTime).count() / 1000.0;
            
            latenciesByCategory[category].push_back(latency);
            successesByCategory[category].push_back(result.success);
            
            perfMonitor_->recordLatency("benchmark.translation_latency_" + category + "_ms", latency);
            perfMonitor_->recordMetric("benchmark.translation_success_" + category, result.success ? 1.0 : 0.0);
        }
    }
    
    // Analyze results by category
    for (const auto& [category, latencies] : latenciesByCategory) {
        auto result = calculateBenchmarkStats("Translation Latency - " + category, 
                                            latencies, successesByCategory[category]);
        printBenchmarkResult(result);
        
        // Performance expectations based on text length
        if (category == "short") {
            EXPECT_LT(result.p95Latency, 200.0);
        } else if (category == "medium") {
            EXPECT_LT(result.p95Latency, 500.0);
        } else if (category == "long") {
            EXPECT_LT(result.p95Latency, 1000.0);
        } else if (category == "very_long") {
            EXPECT_LT(result.p95Latency, 2000.0);
        }
        
        EXPECT_GT(result.successRate, 0.95); // 95% success rate minimum
    }
}

// Benchmark 2: Language Detection Performance
TEST_F(MTPerformanceBenchmark, LanguageDetectionPerformance) {
    std::vector<double> latencies;
    std::vector<bool> successes;
    
    const int iterations = 100;
    
    for (int i = 0; i < iterations; ++i) {
        const auto& [text, category] = benchmarkTexts_[i % benchmarkTexts_.size()];
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        auto result = languageDetector_->detectLanguage(text);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        double latency = std::chrono::duration_cast<std::chrono::microseconds>(
            endTime - startTime).count() / 1000.0;
        
        latencies.push_back(latency);
        successes.push_back(result.confidence > 0.3f); // Reasonable confidence threshold
        
        perfMonitor_->recordLatency("benchmark.language_detection_latency_ms", latency);
        perfMonitor_->recordMetric("benchmark.language_detection_confidence", result.confidence);
    }
    
    auto result = calculateBenchmarkStats("Language Detection Performance", latencies, successes);
    printBenchmarkResult(result);
    
    // Language detection should be very fast
    EXPECT_LT(result.p95Latency, 50.0); // Should be under 50ms P95
    EXPECT_GT(result.successRate, 0.80); // 80% reasonable detection rate
    EXPECT_GT(result.throughput, 100.0); // Should handle 100+ detections per second
}

// Benchmark 3: Multi-Language Pair Performance
TEST_F(MTPerformanceBenchmark, MultiLanguagePairPerformance) {
    std::map<std::string, std::vector<double>> latenciesByPair;
    std::map<std::string, std::vector<bool>> successesByPair;
    
    const int iterationsPerPair = 20;
    const std::string testText = "This is a test sentence for multi-language pair performance evaluation.";
    
    for (const auto& [sourceLang, targetLang] : languagePairs_) {
        std::string pairKey = sourceLang + "->" + targetLang;
        
        // Initialize translator for this language pair
        bool initSuccess = translator_->initialize(sourceLang, targetLang);
        if (!initSuccess) {
            std::cout << "Skipping unsupported language pair: " << pairKey << std::endl;
            continue;
        }
        
        for (int i = 0; i < iterationsPerPair; ++i) {
            auto startTime = std::chrono::high_resolution_clock::now();
            
            auto result = translator_->translate(testText);
            
            auto endTime = std::chrono::high_resolution_clock::now();
            double latency = std::chrono::duration_cast<std::chrono::microseconds>(
                endTime - startTime).count() / 1000.0;
            
            latenciesByPair[pairKey].push_back(latency);
            successesByPair[pairKey].push_back(result.success);
            
            perfMonitor_->recordLatency("benchmark.translation_" + pairKey + "_latency_ms", latency);
        }
    }
    
    // Analyze results by language pair
    for (const auto& [pairKey, latencies] : latenciesByPair) {
        auto result = calculateBenchmarkStats("Translation Performance - " + pairKey, 
                                            latencies, successesByPair[pairKey]);
        printBenchmarkResult(result);
        
        EXPECT_LT(result.p95Latency, 1000.0); // All pairs should be under 1s P95
        EXPECT_GT(result.successRate, 0.90); // 90% success rate minimum
    }
}

// Benchmark 4: Concurrent Translation Load
TEST_F(MTPerformanceBenchmark, ConcurrentTranslationLoad) {
    ASSERT_TRUE(translator_->initialize("en", "es"));
    
    const std::vector<int> threadCounts = {1, 2, 4, 8, 16};
    const int operationsPerThread = 10;
    const std::string testText = "Concurrent load testing sentence for performance evaluation.";
    
    for (int numThreads : threadCounts) {
        std::vector<std::future<std::pair<double, bool>>> futures;
        std::atomic<int> completedOps{0};
        
        auto overallStartTime = std::chrono::high_resolution_clock::now();
        
        // Launch concurrent translation tasks
        for (int t = 0; t < numThreads; ++t) {
            futures.push_back(std::async(std::launch::async, [&, t]() {
                double totalLatency = 0.0;
                bool allSuccessful = true;
                
                for (int op = 0; op < operationsPerThread; ++op) {
                    auto startTime = std::chrono::high_resolution_clock::now();
                    
                    auto result = translator_->translate(testText + " " + std::to_string(t) + "_" + std::to_string(op));
                    
                    auto endTime = std::chrono::high_resolution_clock::now();
                    double latency = std::chrono::duration_cast<std::chrono::microseconds>(
                        endTime - startTime).count() / 1000.0;
                    
                    totalLatency += latency;
                    if (!result.success) {
                        allSuccessful = false;
                    }
                    
                    completedOps++;
                }
                
                return std::make_pair(totalLatency / operationsPerThread, allSuccessful);
            }));
        }
        
        // Collect results
        std::vector<double> avgLatencies;
        std::vector<bool> threadSuccesses;
        
        for (auto& future : futures) {
            auto [avgLatency, success] = future.get();
            avgLatencies.push_back(avgLatency);
            threadSuccesses.push_back(success);
        }
        
        auto overallEndTime = std::chrono::high_resolution_clock::now();
        double overallTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            overallEndTime - overallStartTime).count();
        
        // Calculate metrics
        double avgLatency = std::accumulate(avgLatencies.begin(), avgLatencies.end(), 0.0) / avgLatencies.size();
        double throughput = static_cast<double>(numThreads * operationsPerThread) / (overallTime / 1000.0);
        int successfulThreads = std::count(threadSuccesses.begin(), threadSuccesses.end(), true);
        double successRate = static_cast<double>(successfulThreads) / numThreads;
        
        perfMonitor_->recordLatency("benchmark.concurrent_" + std::to_string(numThreads) + "_threads_avg_latency_ms", avgLatency);
        perfMonitor_->recordThroughput("benchmark.concurrent_" + std::to_string(numThreads) + "_threads_throughput_ops", throughput);
        perfMonitor_->recordMetric("benchmark.concurrent_" + std::to_string(numThreads) + "_threads_success_rate", successRate);
        
        std::cout << "Concurrent Load - " << numThreads << " threads:" << std::endl;
        std::cout << "  Overall time: " << overallTime << "ms" << std::endl;
        std::cout << "  Avg latency: " << avgLatency << "ms" << std::endl;
        std::cout << "  Throughput: " << throughput << " ops/sec" << std::endl;
        std::cout << "  Success rate: " << (successRate * 100) << "%" << std::endl;
        std::cout << "  Completed operations: " << completedOps.load() << std::endl;
        std::cout << std::endl;
        
        EXPECT_EQ(completedOps.load(), numThreads * operationsPerThread);
        EXPECT_GT(successRate, 0.90); // 90% thread success rate
        
        // Throughput should generally increase with more threads (up to a point)
        if (numThreads <= 4) {
            EXPECT_GT(throughput, numThreads * 0.5); // At least 0.5 ops/sec per thread
        }
    }
}

// Benchmark 5: GPU vs CPU Performance Comparison
TEST_F(MTPerformanceBenchmark, GPUvsCPUPerformanceComparison) {
    if (!gpuAccelerator_->isGPUAvailable()) {
        GTEST_SKIP() << "GPU not available for performance comparison";
    }
    
    const std::string testText = "GPU versus CPU performance comparison test sentence with moderate complexity.";
    const int iterations = 50;
    
    std::vector<double> cpuLatencies;
    std::vector<double> gpuLatencies;
    std::vector<bool> cpuSuccesses;
    std::vector<bool> gpuSuccesses;
    
    // Test CPU performance
    std::cout << "Benchmarking CPU performance..." << std::endl;
    ASSERT_TRUE(translator_->initialize("en", "es"));
    translator_->setGPUAcceleration(false);
    
    for (int i = 0; i < iterations; ++i) {
        auto startTime = std::chrono::high_resolution_clock::now();
        
        auto result = translator_->translate(testText);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        double latency = std::chrono::duration_cast<std::chrono::microseconds>(
            endTime - startTime).count() / 1000.0;
        
        cpuLatencies.push_back(latency);
        cpuSuccesses.push_back(result.success);
        
        EXPECT_FALSE(result.usedGPUAcceleration);
        
        perfMonitor_->recordLatency("benchmark.cpu_translation_latency_ms", latency);
    }
    
    // Test GPU performance
    std::cout << "Benchmarking GPU performance..." << std::endl;
    if (gpuAccelerator_->selectGPU(0)) {
        ASSERT_TRUE(translator_->initializeWithGPU("en", "es", 0));
        
        for (int i = 0; i < iterations; ++i) {
            auto startTime = std::chrono::high_resolution_clock::now();
            
            auto result = translator_->translate(testText);
            
            auto endTime = std::chrono::high_resolution_clock::now();
            double latency = std::chrono::duration_cast<std::chrono::microseconds>(
                endTime - startTime).count() / 1000.0;
            
            gpuLatencies.push_back(latency);
            gpuSuccesses.push_back(result.success);
            
            EXPECT_TRUE(result.usedGPUAcceleration);
            
            perfMonitor_->recordLatency("benchmark.gpu_translation_latency_ms", latency);
        }
    }
    
    // Compare results
    auto cpuResult = calculateBenchmarkStats("CPU Translation Performance", cpuLatencies, cpuSuccesses);
    auto gpuResult = calculateBenchmarkStats("GPU Translation Performance", gpuLatencies, gpuSuccesses);
    
    printBenchmarkResult(cpuResult);
    printBenchmarkResult(gpuResult);
    
    if (!gpuLatencies.empty()) {
        double speedup = cpuResult.avgLatency / gpuResult.avgLatency;
        double throughputImprovement = gpuResult.throughput / cpuResult.throughput;
        
        perfMonitor_->recordMetric("benchmark.gpu_speedup", speedup);
        perfMonitor_->recordMetric("benchmark.gpu_throughput_improvement", throughputImprovement);
        
        std::cout << "GPU vs CPU Comparison:" << std::endl;
        std::cout << "  Speedup: " << speedup << "x" << std::endl;
        std::cout << "  Throughput improvement: " << throughputImprovement << "x" << std::endl;
        
        // GPU should provide some benefit (even if minimal with fallback)
        EXPECT_GT(speedup, 0.8); // Allow for some variance
        
        // Test GPU memory usage
        auto gpuStats = gpuAccelerator_->getGPUStatistics();
        perfMonitor_->recordMetric("benchmark.gpu_memory_used_mb", gpuStats.memoryUsedMB);
        perfMonitor_->recordMetric("benchmark.gpu_utilization_percent", gpuStats.utilizationPercent);
        
        std::cout << "  GPU Memory Used: " << gpuStats.memoryUsedMB << "MB" << std::endl;
        std::cout << "  GPU Utilization: " << gpuStats.utilizationPercent << "%" << std::endl;
    }
}

// Benchmark 6: Quality Assessment Performance
TEST_F(MTPerformanceBenchmark, QualityAssessmentPerformance) {
    ASSERT_TRUE(translator_->initialize("en", "es"));
    
    std::vector<double> latencies;
    std::vector<bool> successes;
    std::vector<double> qualityScores;
    
    const int iterations = 100;
    
    for (int i = 0; i < iterations; ++i) {
        const auto& [text, category] = benchmarkTexts_[i % benchmarkTexts_.size()];
        
        // First translate
        auto translationResult = translator_->translate(text);
        ASSERT_TRUE(translationResult.success);
        
        // Then assess quality
        auto startTime = std::chrono::high_resolution_clock::now();
        
        auto qualityMetrics = qualityManager_->assessTranslationQuality(
            text, translationResult.translatedText, "en", "es");
        
        auto endTime = std::chrono::high_resolution_clock::now();
        double latency = std::chrono::duration_cast<std::chrono::microseconds>(
            endTime - startTime).count() / 1000.0;
        
        latencies.push_back(latency);
        successes.push_back(qualityMetrics.overallConfidence > 0.0f);
        qualityScores.push_back(qualityMetrics.overallConfidence);
        
        perfMonitor_->recordLatency("benchmark.quality_assessment_latency_ms", latency);
        perfMonitor_->recordMetric("benchmark.quality_score", qualityMetrics.overallConfidence);
    }
    
    auto result = calculateBenchmarkStats("Quality Assessment Performance", latencies, successes);
    printBenchmarkResult(result);
    
    double avgQualityScore = std::accumulate(qualityScores.begin(), qualityScores.end(), 0.0) / qualityScores.size();
    perfMonitor_->recordMetric("benchmark.avg_quality_score", avgQualityScore);
    
    std::cout << "Average Quality Score: " << avgQualityScore << std::endl;
    
    // Quality assessment should be fast
    EXPECT_LT(result.p95Latency, 100.0); // Should be under 100ms P95
    EXPECT_GT(result.successRate, 0.95); // 95% success rate
    EXPECT_GT(result.throughput, 50.0); // Should handle 50+ assessments per second
    EXPECT_GT(avgQualityScore, 0.2); // Reasonable average quality
}

// Benchmark 7: Memory Usage and Model Management
TEST_F(MTPerformanceBenchmark, MemoryUsageAndModelManagement) {
    const std::vector<std::pair<std::string, std::string>> testPairs = {
        {"en", "es"}, {"en", "fr"}, {"en", "de"}, {"es", "en"}, {"fr", "en"}
    };
    
    std::vector<double> initLatencies;
    std::vector<bool> initSuccesses;
    
    size_t initialMemoryMB = 0;
    if (gpuAccelerator_->isGPUAvailable()) {
        initialMemoryMB = gpuAccelerator_->getCurrentMemoryUsageMB();
    }
    
    for (const auto& [sourceLang, targetLang] : testPairs) {
        auto startTime = std::chrono::high_resolution_clock::now();
        
        bool initSuccess = translator_->initialize(sourceLang, targetLang);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        double latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();
        
        initLatencies.push_back(latency);
        initSuccesses.push_back(initSuccess);
        
        if (initSuccess) {
            // Test translation to ensure model is loaded
            auto result = translator_->translate("Memory test");
            EXPECT_TRUE(result.success);
            
            // Record memory usage
            if (gpuAccelerator_->isGPUAvailable()) {
                size_t currentMemoryMB = gpuAccelerator_->getCurrentMemoryUsageMB();
                perfMonitor_->recordMetric("benchmark.memory_usage_" + sourceLang + "_" + targetLang + "_mb", 
                                         currentMemoryMB);
            }
        }
        
        perfMonitor_->recordLatency("benchmark.model_init_" + sourceLang + "_" + targetLang + "_ms", latency);
    }
    
    auto result = calculateBenchmarkStats("Model Management Performance", initLatencies, initSuccesses);
    printBenchmarkResult(result);
    
    // Model initialization should be reasonable
    EXPECT_LT(result.p95Latency, 5000.0); // Should be under 5 seconds P95
    EXPECT_GT(result.successRate, 0.80); // 80% success rate (some pairs might not be available)
    
    // Test model statistics
    auto modelStats = translator_->getModelStatistics();
    perfMonitor_->recordMetric("benchmark.total_loaded_models", modelStats.totalLoadedModels);
    perfMonitor_->recordMetric("benchmark.gpu_models", modelStats.gpuModels);
    perfMonitor_->recordMetric("benchmark.cpu_models", modelStats.cpuModels);
    perfMonitor_->recordMetric("benchmark.total_memory_usage_mb", modelStats.totalMemoryUsageMB);
    
    std::cout << "Model Statistics:" << std::endl;
    std::cout << "  Total loaded models: " << modelStats.totalLoadedModels << std::endl;
    std::cout << "  GPU models: " << modelStats.gpuModels << std::endl;
    std::cout << "  CPU models: " << modelStats.cpuModels << std::endl;
    std::cout << "  Total memory usage: " << modelStats.totalMemoryUsageMB << "MB" << std::endl;
}

// Benchmark 8: Batch Translation Performance
TEST_F(MTPerformanceBenchmark, BatchTranslationPerformance) {
    ASSERT_TRUE(translator_->initialize("en", "es"));
    
    const std::vector<int> batchSizes = {1, 5, 10, 20, 50};
    
    // Prepare batch texts
    std::vector<std::string> batchTexts;
    for (size_t i = 0; i < 50; ++i) {
        const auto& [text, category] = benchmarkTexts_[i % benchmarkTexts_.size()];
        batchTexts.push_back(text + " (batch item " + std::to_string(i) + ")");
    }
    
    for (int batchSize : batchSizes) {
        std::vector<std::string> currentBatch(batchTexts.begin(), batchTexts.begin() + batchSize);
        
        const int iterations = 10;
        std::vector<double> latencies;
        std::vector<bool> successes;
        
        for (int i = 0; i < iterations; ++i) {
            auto startTime = std::chrono::high_resolution_clock::now();
            
            auto results = translator_->translateBatch(currentBatch);
            
            auto endTime = std::chrono::high_resolution_clock::now();
            double latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                endTime - startTime).count();
            
            latencies.push_back(latency);
            
            // Check if all translations succeeded
            bool allSuccessful = true;
            for (const auto& result : results) {
                if (!result.success) {
                    allSuccessful = false;
                    break;
                }
            }
            successes.push_back(allSuccessful && results.size() == currentBatch.size());
            
            perfMonitor_->recordLatency("benchmark.batch_" + std::to_string(batchSize) + "_latency_ms", latency);
        }
        
        auto result = calculateBenchmarkStats("Batch Translation - Size " + std::to_string(batchSize), 
                                            latencies, successes);
        printBenchmarkResult(result);
        
        // Calculate per-item latency
        double avgPerItemLatency = result.avgLatency / batchSize;
        perfMonitor_->recordLatency("benchmark.batch_" + std::to_string(batchSize) + "_per_item_latency_ms", 
                                   avgPerItemLatency);
        
        std::cout << "  Per-item latency: " << avgPerItemLatency << "ms" << std::endl;
        
        EXPECT_GT(result.successRate, 0.90); // 90% batch success rate
        
        // Larger batches should have better per-item performance
        if (batchSize > 1) {
            EXPECT_LT(avgPerItemLatency, 1000.0 / batchSize + 500.0); // Some efficiency gain expected
        }
    }
}

} // namespace performance
} // namespace speechrnt