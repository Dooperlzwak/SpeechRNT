#include "stt/performance_optimized_stt.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <random>

/**
 * Demonstration program for Task 18: Memory Usage and Performance Optimization
 * 
 * This program demonstrates all the implemented optimizations:
 * 1. Memory pooling for audio buffers and transcription results
 * 2. GPU memory optimization for model loading and inference
 * 3. Efficient data structures for streaming transcription state
 * 4. Optimized thread usage and synchronization
 */

namespace {

// Generate test audio data
std::vector<float> generateTestAudio(size_t samples, float frequency = 440.0f, uint32_t sampleRate = 16000) {
    std::vector<float> audio(samples);
    
    for (size_t i = 0; i < samples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        audio[i] = 0.5f * std::sin(2.0f * M_PI * frequency * t);
    }
    
    return audio;
}

// Generate random audio chunks for streaming test
std::vector<std::vector<float>> generateAudioChunks(size_t numChunks, size_t samplesPerChunk) {
    std::vector<std::vector<float>> chunks;
    chunks.reserve(numChunks);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    
    for (size_t i = 0; i < numChunks; ++i) {
        std::vector<float> chunk(samplesPerChunk);
        for (size_t j = 0; j < samplesPerChunk; ++j) {
            chunk[j] = dist(gen);
        }
        chunks.push_back(std::move(chunk));
    }
    
    return chunks;
}

void demonstrateMemoryPooling() {
    std::cout << "\n=== Memory Pooling Demonstration ===\n";
    
    // Test audio buffer pool
    utils::AudioBufferPool audioPool(50, 100);
    
    std::cout << "Testing audio buffer pool...\n";
    
    // Acquire multiple buffers
    std::vector<utils::AudioBufferPool::AudioBufferPtr> buffers;
    for (int i = 0; i < 20; ++i) {
        auto buffer = audioPool.acquireBuffer(16000); // 1 second of audio
        if (buffer) {
            buffer->data = generateTestAudio(16000);
            buffers.push_back(buffer);
        }
    }
    
    auto stats = audioPool.getStatistics();
    std::cout << "Audio pool stats: " << stats.totalInUse << "/" << stats.totalAllocated 
              << " buffers in use\n";
    
    // Release buffers (automatic via RAII)
    buffers.clear();
    
    stats = audioPool.getStatistics();
    std::cout << "After release: " << stats.totalInUse << "/" << stats.totalAllocated 
              << " buffers in use\n";
    
    // Test transcription result pool
    utils::TranscriptionResultPool resultPool(30, 60);
    
    std::cout << "\nTesting transcription result pool...\n";
    
    std::vector<utils::TranscriptionResultPool::TranscriptionResultPtr> results;
    for (int i = 0; i < 15; ++i) {
        auto result = resultPool.acquireResult();
        if (result) {
            result->text = "Test transcription " + std::to_string(i);
            result->confidence = 0.9f;
            results.push_back(result);
        }
    }
    
    auto resultStats = resultPool.getStatistics();
    std::cout << "Result pool stats: " << resultStats.totalInUse << "/" 
              << resultStats.totalAllocated << " results in use\n";
    
    results.clear();
    
    resultStats = resultPool.getStatistics();
    std::cout << "After release: " << resultStats.totalInUse << "/" 
              << resultStats.totalAllocated << " results in use\n";
}

void demonstrateThreadPoolOptimization() {
    std::cout << "\n=== Thread Pool Optimization Demonstration ===\n";
    
    utils::OptimizedThreadPool::PoolConfig config;
    config.numThreads = 4;
    config.enableWorkStealing = true;
    config.enablePriority = true;
    
    utils::OptimizedThreadPool threadPool(config);
    
    if (!threadPool.initialize()) {
        std::cout << "Failed to initialize thread pool\n";
        return;
    }
    
    std::cout << "Thread pool initialized with " << config.numThreads << " threads\n";
    
    // Submit tasks with different priorities
    std::vector<std::future<int>> futures;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Submit high priority tasks
    for (int i = 0; i < 10; ++i) {
        futures.push_back(threadPool.submit(utils::OptimizedThreadPool::Priority::HIGH,
            [i]() -> int {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                return i * i;
            }));
    }
    
    // Submit normal priority tasks
    for (int i = 0; i < 20; ++i) {
        futures.push_back(threadPool.submit(utils::OptimizedThreadPool::Priority::NORMAL,
            [i]() -> int {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                return i + 100;
            }));
    }
    
    // Submit low priority tasks
    for (int i = 0; i < 15; ++i) {
        futures.push_back(threadPool.submit(utils::OptimizedThreadPool::Priority::LOW,
            [i]() -> int {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                return i + 200;
            }));
    }
    
    // Wait for all tasks to complete
    int completedTasks = 0;
    for (auto& future : futures) {
        try {
            future.get();
            completedTasks++;
        } catch (const std::exception& e) {
            std::cout << "Task failed: " << e.what() << "\n";
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "Completed " << completedTasks << " tasks in " << duration.count() << "ms\n";
    
    auto stats = threadPool.getStatistics();
    std::cout << "Thread pool stats:\n";
    std::cout << "  Completed tasks: " << stats.completedTasks << "\n";
    std::cout << "  Failed tasks: " << stats.failedTasks << "\n";
    std::cout << "  Average task time: " << stats.averageTaskTime << "μs\n";
    std::cout << "  Work stealing events: " << stats.workStealingEvents << "\n";
    std::cout << "  Health status: " << (threadPool.isHealthy() ? "HEALTHY" : "UNHEALTHY") << "\n";
    
    threadPool.shutdown();
}

void demonstrateStreamingStateOptimization() {
    std::cout << "\n=== Streaming State Optimization Demonstration ===\n";
    
    stt::OptimizedStreamingState::OptimizationConfig config;
    config.maxConcurrentUtterances = 20;
    config.audioBufferPoolSize = 50;
    config.resultPoolSize = 100;
    config.enableAsyncProcessing = true;
    config.workerThreadCount = 2;
    
    stt::OptimizedStreamingState streamingState(config);
    
    if (!streamingState.initialize()) {
        std::cout << "Failed to initialize streaming state manager\n";
        return;
    }
    
    std::cout << "Streaming state manager initialized\n";
    
    // Create multiple utterances
    std::vector<uint32_t> utteranceIds;
    for (uint32_t i = 1; i <= 10; ++i) {
        if (streamingState.createUtterance(i)) {
            utteranceIds.push_back(i);
        }
    }
    
    std::cout << "Created " << utteranceIds.size() << " utterances\n";
    
    // Add audio chunks to each utterance
    auto audioChunks = generateAudioChunks(5, 8000); // 5 chunks of 0.5 seconds each
    
    for (uint32_t utteranceId : utteranceIds) {
        for (const auto& chunk : audioChunks) {
            streamingState.addAudioChunk(utteranceId, chunk);
        }
        
        // Set some transcription results
        streamingState.setTranscriptionResult(utteranceId, 
            "Test transcription for utterance " + std::to_string(utteranceId),
            0.85f + (utteranceId * 0.01f), false);
    }
    
    // Get statistics
    auto stats = streamingState.getStatistics();
    std::cout << "Streaming state stats:\n";
    std::cout << "  Active utterances: " << stats.activeUtterances << "\n";
    std::cout << "  Total utterances: " << stats.totalUtterances << "\n";
    std::cout << "  Memory usage: " << stats.totalMemoryUsageMB << "MB\n";
    std::cout << "  Total audio processed: " << (stats.totalAudioProcessed / 16000.0) << " seconds\n";
    std::cout << "  Health status: " << (streamingState.isHealthy() ? "HEALTHY" : "UNHEALTHY") << "\n";
    
    // Finalize some utterances
    for (size_t i = 0; i < utteranceIds.size() / 2; ++i) {
        streamingState.finalizeAudioBuffer(utteranceIds[i]);
    }
    
    // Perform cleanup
    streamingState.performCleanup();
    
    auto finalStats = streamingState.getStatistics();
    std::cout << "After cleanup - Active utterances: " << finalStats.activeUtterances << "\n";
    
    streamingState.shutdown();
}

void demonstrateIntegratedOptimization() {
    std::cout << "\n=== Integrated Performance Optimization Demonstration ===\n";
    
    // Create optimized STT system using factory
    auto optimizedSTT = stt::OptimizedSTTFactory::createOptimized();
    
    if (!optimizedSTT) {
        std::cout << "Failed to create optimized STT system\n";
        return;
    }
    
    std::cout << "Created integrated optimized STT system\n";
    
    // Test async transcription
    std::cout << "\nTesting async transcription...\n";
    
    std::vector<std::future<stt::TranscriptionResult>> transcriptionFutures;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Submit multiple transcription tasks
    for (int i = 0; i < 5; ++i) {
        auto audioData = generateTestAudio(32000); // 2 seconds of audio
        auto future = optimizedSTT->transcribeAsync(audioData, "en");
        transcriptionFutures.push_back(std::move(future));
    }
    
    // Wait for results
    int completedTranscriptions = 0;
    for (auto& future : transcriptionFutures) {
        try {
            auto result = future.get();
            completedTranscriptions++;
            std::cout << "Transcription " << completedTranscriptions 
                      << ": \"" << result.text << "\" (confidence: " << result.confidence << ")\n";
        } catch (const std::exception& e) {
            std::cout << "Transcription failed: " << e.what() << "\n";
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "Completed " << completedTranscriptions << " transcriptions in " 
              << duration.count() << "ms\n";
    
    // Test streaming transcription
    std::cout << "\nTesting streaming transcription...\n";
    
    uint32_t utteranceId = 1001;
    bool streamingStarted = optimizedSTT->startStreamingTranscription(utteranceId,
        [](const stt::TranscriptionResult& result) {
            std::cout << "Streaming result: \"" << result.text 
                      << "\" (partial: " << (result.is_partial ? "yes" : "no") << ")\n";
        });
    
    if (streamingStarted) {
        // Add audio chunks
        auto chunks = generateAudioChunks(10, 8000);
        for (const auto& chunk : chunks) {
            optimizedSTT->addAudioChunk(utteranceId, chunk);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        optimizedSTT->finalizeStreamingTranscription(utteranceId);
        std::cout << "Streaming transcription completed\n";
    }
    
    // Get performance statistics
    std::cout << "\n" << optimizedSTT->getPerformanceReport() << "\n";
    
    // Test memory optimization
    std::cout << "Performing memory optimization...\n";
    optimizedSTT->optimizeMemoryUsage();
    
    // Wait a bit for optimization to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "Memory usage after optimization: " 
              << optimizedSTT->getCurrentMemoryUsageMB() << "MB\n";
    
    // Test garbage collection
    std::cout << "Performing garbage collection...\n";
    optimizedSTT->performGarbageCollection();
    
    std::cout << "Memory usage after GC: " 
              << optimizedSTT->getCurrentMemoryUsageMB() << "MB\n";
    
    std::cout << "System health: " << (optimizedSTT->isSystemHealthy() ? "HEALTHY" : "UNHEALTHY") << "\n";
}

} // anonymous namespace

int main() {
    std::cout << "Performance Optimization Demo - Task 18 Implementation\n";
    std::cout << "=====================================================\n";
    
    try {
        // Initialize logging
        utils::Logger::setLevel(utils::Logger::Level::INFO);
        
        // Demonstrate individual optimizations
        demonstrateMemoryPooling();
        demonstrateThreadPoolOptimization();
        demonstrateStreamingStateOptimization();
        
        // Demonstrate integrated optimization
        demonstrateIntegratedOptimization();
        
        std::cout << "\n=== Performance Optimization Demo Completed Successfully ===\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Demo failed with unknown exception" << std::endl;
        return 1;
    }
    
    return 0;
}