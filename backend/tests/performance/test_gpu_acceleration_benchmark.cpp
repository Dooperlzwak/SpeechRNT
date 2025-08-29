#include "mt/marian_translator.hpp"
#include "utils/gpu_manager.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <cassert>

using namespace speechrnt::mt;
using namespace speechrnt::utils;

class GPUAccelerationBenchmark {
public:
    GPUAccelerationBenchmark() {
        cpuTranslator = std::make_unique<MarianTranslator>();
        gpuTranslator = std::make_unique<MarianTranslator>();
        
        cpuTranslator->setModelsPath("data/marian/");
        gpuTranslator->setModelsPath("data/marian/");
    }
    
    ~GPUAccelerationBenchmark() {
        if (cpuTranslator) {
            cpuTranslator->cleanup();
        }
        if (gpuTranslator) {
            gpuTranslator->cleanup();
        }
    }
    
    void runBenchmarks() {
        std::cout << "Running GPU Acceleration Benchmarks..." << std::endl;
        
        // Check GPU availability
        GPUManager& gpuManager = GPUManager::getInstance();
        if (!gpuManager.initialize() || !gpuManager.isCudaAvailable()) {
            std::cout << "GPU not available, skipping GPU benchmarks" << std::endl;
            runCPUOnlyBenchmarks();
            return;
        }
        
        std::cout << "GPU available with " << gpuManager.getDeviceCount() << " device(s)" << std::endl;
        
        // Run comparative benchmarks
        benchmarkInitializationTime();
        benchmarkTranslationLatency();
        benchmarkTranslationThroughput();
        benchmarkMemoryUsage();
        benchmarkConcurrentTranslations();
        benchmarkModelSwitching();
        benchmarkGPUFallback();
        
        std::cout << "All GPU acceleration benchmarks completed!" << std::endl;
    }

private:
    std::unique_ptr<MarianTranslator> cpuTranslator;
    std::unique_ptr<MarianTranslator> gpuTranslator;
    
    void runCPUOnlyBenchmarks() {
        std::cout << "Running CPU-only benchmarks..." << std::endl;
        
        // Initialize CPU translator
        assert(cpuTranslator->initialize("en", "es"));
        
        // Benchmark CPU translation
        auto startTime = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < 100; ++i) {
            auto result = cpuTranslator->translate("Hello world, this is a test sentence.");
            assert(result.success);
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        std::cout << "CPU-only translation time (100 translations): " << duration.count() << " ms" << std::endl;
        std::cout << "Average CPU translation time: " << (duration.count() / 100.0) << " ms" << std::endl;
    }
    
    void benchmarkInitializationTime() {
        std::cout << "\n=== Initialization Time Benchmark ===" << std::endl;
        
        // Benchmark CPU initialization
        auto startTime = std::chrono::high_resolution_clock::now();
        bool cpuInit = cpuTranslator->initialize("en", "es");
        auto endTime = std::chrono::high_resolution_clock::now();
        auto cpuInitTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        std::cout << "CPU initialization: " << cpuInitTime.count() << " ms (success: " << cpuInit << ")" << std::endl;
        
        // Benchmark GPU initialization
        startTime = std::chrono::high_resolution_clock::now();
        bool gpuInit = gpuTranslator->initializeWithGPU("en", "es", 0);
        endTime = std::chrono::high_resolution_clock::now();
        auto gpuInitTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        std::cout << "GPU initialization: " << gpuInitTime.count() << " ms (success: " << gpuInit << ")" << std::endl;
        
        if (cpuInit && gpuInit) {
            double speedup = static_cast<double>(cpuInitTime.count()) / gpuInitTime.count();
            std::cout << "GPU initialization speedup: " << speedup << "x" << std::endl;
        }
    }
    
    void benchmarkTranslationLatency() {
        std::cout << "\n=== Translation Latency Benchmark ===" << std::endl;
        
        std::vector<std::string> testSentences = {
            "Hello world",
            "This is a test sentence for translation.",
            "Machine translation has improved significantly with neural networks.",
            "The quick brown fox jumps over the lazy dog in the beautiful garden.",
            "Artificial intelligence and machine learning are transforming the way we communicate across language barriers."
        };
        
        for (size_t i = 0; i < testSentences.size(); ++i) {
            const std::string& sentence = testSentences[i];
            std::cout << "\nSentence " << (i + 1) << " (" << sentence.length() << " chars): \"" 
                      << sentence.substr(0, 50) << (sentence.length() > 50 ? "..." : "") << "\"" << std::endl;
            
            // CPU translation
            auto startTime = std::chrono::high_resolution_clock::now();
            auto cpuResult = cpuTranslator->translate(sentence);
            auto endTime = std::chrono::high_resolution_clock::now();
            auto cpuTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
            
            std::cout << "  CPU: " << cpuTime.count() << " μs (confidence: " << cpuResult.confidence << ")" << std::endl;
            
            // GPU translation (if available)
            if (gpuTranslator->isGPUAccelerationEnabled()) {
                startTime = std::chrono::high_resolution_clock::now();
                auto gpuResult = gpuTranslator->translate(sentence);
                endTime = std::chrono::high_resolution_clock::now();
                auto gpuTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
                
                std::cout << "  GPU: " << gpuTime.count() << " μs (confidence: " << gpuResult.confidence << ")" << std::endl;
                
                if (cpuTime.count() > 0) {
                    double speedup = static_cast<double>(cpuTime.count()) / gpuTime.count();
                    std::cout << "  GPU speedup: " << speedup << "x" << std::endl;
                }
            }
        }
    }
    
    void benchmarkTranslationThroughput() {
        std::cout << "\n=== Translation Throughput Benchmark ===" << std::endl;
        
        const std::string testSentence = "This is a standard test sentence for throughput measurement.";
        const int numTranslations = 1000;
        
        // CPU throughput
        auto startTime = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < numTranslations; ++i) {
            auto result = cpuTranslator->translate(testSentence);
            assert(result.success);
        }
        auto endTime = std::chrono::high_resolution_clock::now();
        auto cpuTotalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        double cpuThroughput = static_cast<double>(numTranslations) / (cpuTotalTime.count() / 1000.0);
        std::cout << "CPU throughput: " << cpuThroughput << " translations/second" << std::endl;
        
        // GPU throughput (if available)
        if (gpuTranslator->isGPUAccelerationEnabled()) {
            startTime = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < numTranslations; ++i) {
                auto result = gpuTranslator->translate(testSentence);
                assert(result.success);
            }
            endTime = std::chrono::high_resolution_clock::now();
            auto gpuTotalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            double gpuThroughput = static_cast<double>(numTranslations) / (gpuTotalTime.count() / 1000.0);
            std::cout << "GPU throughput: " << gpuThroughput << " translations/second" << std::endl;
            
            double throughputSpeedup = gpuThroughput / cpuThroughput;
            std::cout << "GPU throughput speedup: " << throughputSpeedup << "x" << std::endl;
        }
    }
    
    void benchmarkMemoryUsage() {
        std::cout << "\n=== Memory Usage Benchmark ===" << std::endl;
        
        // CPU memory usage (system memory)
        std::cout << "CPU translator memory usage: System memory (not tracked)" << std::endl;
        
        // GPU memory usage
        if (gpuTranslator->isGPUAccelerationEnabled()) {
            size_t gpuMemoryUsage = gpuTranslator->getGPUMemoryUsageMB();
            std::cout << "GPU translator memory usage: " << gpuMemoryUsage << " MB" << std::endl;
            
            // Load additional models and measure memory growth
            std::vector<std::pair<std::string, std::string>> languagePairs = {
                {"en", "fr"}, {"en", "de"}, {"es", "en"}
            };
            
            for (const auto& pair : languagePairs) {
                if (gpuTranslator->supportsLanguagePair(pair.first, pair.second)) {
                    bool loaded = gpuTranslator->loadModel(pair.first, pair.second);
                    if (loaded) {
                        size_t newMemoryUsage = gpuTranslator->getGPUMemoryUsageMB();
                        std::cout << "Memory after loading " << pair.first << "->" << pair.second 
                                  << ": " << newMemoryUsage << " MB (+" << (newMemoryUsage - gpuMemoryUsage) << " MB)" << std::endl;
                        gpuMemoryUsage = newMemoryUsage;
                    }
                }
            }
        }
    }
    
    void benchmarkConcurrentTranslations() {
        std::cout << "\n=== Concurrent Translation Benchmark ===" << std::endl;
        
        const int numThreads = 4;
        const int translationsPerThread = 100;
        const std::string testSentence = "Concurrent translation test sentence.";
        
        // CPU concurrent benchmark
        std::vector<std::thread> cpuThreads;
        std::vector<std::chrono::milliseconds> cpuThreadTimes(numThreads);
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        for (int t = 0; t < numThreads; ++t) {
            cpuThreads.emplace_back([this, t, &cpuThreadTimes, testSentence, translationsPerThread]() {
                auto threadStart = std::chrono::high_resolution_clock::now();
                
                for (int i = 0; i < translationsPerThread; ++i) {
                    auto result = cpuTranslator->translate(testSentence);
                    assert(result.success);
                }
                
                auto threadEnd = std::chrono::high_resolution_clock::now();
                cpuThreadTimes[t] = std::chrono::duration_cast<std::chrono::milliseconds>(threadEnd - threadStart);
            });
        }
        
        for (auto& thread : cpuThreads) {
            thread.join();
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto cpuTotalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        std::cout << "CPU concurrent translation (" << numThreads << " threads, " 
                  << translationsPerThread << " each): " << cpuTotalTime.count() << " ms" << std::endl;
        
        // GPU concurrent benchmark (if available)
        if (gpuTranslator->isGPUAccelerationEnabled()) {
            std::vector<std::thread> gpuThreads;
            std::vector<std::chrono::milliseconds> gpuThreadTimes(numThreads);
            
            startTime = std::chrono::high_resolution_clock::now();
            
            for (int t = 0; t < numThreads; ++t) {
                gpuThreads.emplace_back([this, t, &gpuThreadTimes, testSentence, translationsPerThread]() {
                    auto threadStart = std::chrono::high_resolution_clock::now();
                    
                    for (int i = 0; i < translationsPerThread; ++i) {
                        auto result = gpuTranslator->translate(testSentence);
                        assert(result.success);
                    }
                    
                    auto threadEnd = std::chrono::high_resolution_clock::now();
                    gpuThreadTimes[t] = std::chrono::duration_cast<std::chrono::milliseconds>(threadEnd - threadStart);
                });
            }
            
            for (auto& thread : gpuThreads) {
                thread.join();
            }
            
            endTime = std::chrono::high_resolution_clock::now();
            auto gpuTotalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            std::cout << "GPU concurrent translation (" << numThreads << " threads, " 
                      << translationsPerThread << " each): " << gpuTotalTime.count() << " ms" << std::endl;
            
            if (cpuTotalTime.count() > 0) {
                double concurrentSpeedup = static_cast<double>(cpuTotalTime.count()) / gpuTotalTime.count();
                std::cout << "GPU concurrent speedup: " << concurrentSpeedup << "x" << std::endl;
            }
        }
    }
    
    void benchmarkModelSwitching() {
        std::cout << "\n=== Model Switching Benchmark ===" << std::endl;
        
        std::vector<std::pair<std::string, std::string>> languagePairs = {
            {"en", "es"}, {"en", "fr"}, {"es", "en"}, {"fr", "en"}
        };
        
        const std::string testSentence = "Model switching test sentence.";
        
        // CPU model switching
        auto startTime = std::chrono::high_resolution_clock::now();
        
        for (const auto& pair : languagePairs) {
            if (cpuTranslator->supportsLanguagePair(pair.first, pair.second)) {
                cpuTranslator->initialize(pair.first, pair.second);
                auto result = cpuTranslator->translate(testSentence);
                assert(result.success);
            }
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto cpuSwitchTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        std::cout << "CPU model switching time: " << cpuSwitchTime.count() << " ms" << std::endl;
        
        // GPU model switching (if available)
        if (gpuTranslator->isGPUAccelerationEnabled()) {
            startTime = std::chrono::high_resolution_clock::now();
            
            for (const auto& pair : languagePairs) {
                if (gpuTranslator->supportsLanguagePair(pair.first, pair.second)) {
                    gpuTranslator->initializeWithGPU(pair.first, pair.second, 0);
                    auto result = gpuTranslator->translate(testSentence);
                    assert(result.success);
                }
            }
            
            endTime = std::chrono::high_resolution_clock::now();
            auto gpuSwitchTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            std::cout << "GPU model switching time: " << gpuSwitchTime.count() << " ms" << std::endl;
            
            if (cpuSwitchTime.count() > 0) {
                double switchSpeedup = static_cast<double>(cpuSwitchTime.count()) / gpuSwitchTime.count();
                std::cout << "GPU model switching speedup: " << switchSpeedup << "x" << std::endl;
            }
        }
    }
    
    void benchmarkGPUFallback() {
        std::cout << "\n=== GPU Fallback Benchmark ===" << std::endl;
        
        if (!gpuTranslator->isGPUAccelerationEnabled()) {
            std::cout << "GPU not available, testing CPU-only fallback" << std::endl;
            
            // Test that fallback works correctly
            bool initResult = gpuTranslator->initializeWithGPU("en", "es", 0);
            std::cout << "GPU initialization result: " << (initResult ? "success" : "failed (expected)") << std::endl;
            
            // Should still be able to translate
            auto result = gpuTranslator->translate("Fallback test sentence");
            assert(result.success);
            std::cout << "Fallback translation successful" << std::endl;
            
            return;
        }
        
        // Test GPU to CPU fallback by disabling GPU mid-operation
        std::cout << "Testing GPU to CPU fallback..." << std::endl;
        
        // Start with GPU enabled
        assert(gpuTranslator->isGPUAccelerationEnabled());
        
        auto result1 = gpuTranslator->translate("First translation with GPU");
        assert(result1.success);
        
        // Disable GPU acceleration
        gpuTranslator->setGPUAcceleration(false, 0);
        assert(!gpuTranslator->isGPUAccelerationEnabled());
        
        // Should still work with CPU
        auto result2 = gpuTranslator->translate("Second translation with CPU fallback");
        assert(result2.success);
        
        std::cout << "GPU to CPU fallback successful" << std::endl;
        
        // Re-enable GPU
        gpuTranslator->setGPUAcceleration(true, 0);
        if (gpuTranslator->isGPUAccelerationEnabled()) {
            auto result3 = gpuTranslator->translate("Third translation back to GPU");
            assert(result3.success);
            std::cout << "GPU re-enablement successful" << std::endl;
        }
    }
};

int main() {
    GPUAccelerationBenchmark benchmark;
    benchmark.runBenchmarks();
    return 0;
}