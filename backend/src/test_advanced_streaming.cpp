#include "audio/advanced_streaming_optimizer.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>

using namespace speechrnt::audio;

int main() {
    std::cout << "Testing Advanced Streaming Optimizations..." << std::endl;
    
    // Initialize logging
    speechrnt::utils::Logger::info("Starting advanced streaming test");
    
    // Create and configure the advanced streaming optimizer
    AdvancedStreamingOptimizer optimizer;
    
    AdvancedStreamingConfig config;
    config.enableNetworkMonitoring = true;
    config.enablePacketRecovery = true;
    config.enableQualityDegradation = true;
    config.enableLoadBalancing = true;
    config.enableUltraLowLatency = true;
    config.targetLatencyMs = 150; // 150ms target
    config.numWorkerThreads = 2;
    config.maxQueueSize = 100;
    
    // Initialize the optimizer
    if (!optimizer.initialize(config)) {
        std::cerr << "Failed to initialize advanced streaming optimizer" << std::endl;
        return 1;
    }
    
    // Start the optimizer
    if (!optimizer.start()) {
        std::cerr << "Failed to start advanced streaming optimizer" << std::endl;
        return 1;
    }
    
    std::cout << "Advanced streaming optimizer started successfully" << std::endl;
    
    // Test processing some audio data
    std::vector<float> testAudio(1024);
    for (size_t i = 0; i < testAudio.size(); ++i) {
        testAudio[i] = std::sin(2.0f * M_PI * 440.0f * i / 16000.0f); // 440Hz sine wave
    }
    
    std::vector<AudioChunk> outputChunks;
    uint32_t streamId = 1;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Process the audio stream
    bool success = optimizer.processStreamWithOptimizations(testAudio, streamId, outputChunks);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    float processingTime = std::chrono::duration_cast<std::chrono::microseconds>(
        endTime - startTime).count() / 1000.0f; // Convert to milliseconds
    
    if (success) {
        std::cout << "Stream processing successful!" << std::endl;
        std::cout << "Processing time: " << processingTime << "ms" << std::endl;
        std::cout << "Output chunks: " << outputChunks.size() << std::endl;
        std::cout << "Ultra-low latency active: " << 
                     (optimizer.isUltraLowLatencyActive() ? "Yes" : "No") << std::endl;
    } else {
        std::cerr << "Stream processing failed" << std::endl;
        return 1;
    }
    
    // Test job submission
    std::cout << "\nTesting job submission..." << std::endl;
    
    bool jobCompleted = false;
    uint64_t jobId = optimizer.submitRealTimeJob([&jobCompleted]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        jobCompleted = true;
        std::cout << "Real-time job completed" << std::endl;
    });
    
    if (jobId > 0) {
        std::cout << "Real-time job submitted with ID: " << jobId << std::endl;
        
        // Wait for job completion
        for (int i = 0; i < 100 && !jobCompleted; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        if (jobCompleted) {
            std::cout << "Job completed successfully" << std::endl;
        } else {
            std::cout << "Job did not complete in time" << std::endl;
        }
    } else {
        std::cerr << "Failed to submit real-time job" << std::endl;
    }
    
    // Get performance metrics
    std::cout << "\nPerformance Metrics:" << std::endl;
    StreamingPerformanceMetrics metrics = optimizer.getPerformanceMetrics();
    std::cout << "End-to-end latency: " << metrics.endToEndLatencyMs << "ms" << std::endl;
    std::cout << "Processing latency: " << metrics.processingLatencyMs << "ms" << std::endl;
    std::cout << "Network latency: " << metrics.networkLatencyMs << "ms" << std::endl;
    std::cout << "CPU usage: " << (metrics.cpuUsage * 100) << "%" << std::endl;
    std::cout << "Memory usage: " << (metrics.memoryUsage * 100) << "%" << std::endl;
    std::cout << "Active streams: " << metrics.activeStreams << std::endl;
    std::cout << "Queued jobs: " << metrics.queuedJobs << std::endl;
    
    // Get optimization statistics
    std::cout << "\nOptimization Statistics:" << std::endl;
    auto stats = optimizer.getOptimizationStats();
    for (const auto& [key, value] : stats) {
        std::cout << key << ": " << value << std::endl;
    }
    
    // Check health status
    std::cout << "\nHealth Status: " << (optimizer.isHealthy() ? "Healthy" : "Unhealthy") << std::endl;
    
    // Test ultra-low latency mode toggle
    std::cout << "\nTesting ultra-low latency mode toggle..." << std::endl;
    optimizer.setUltraLowLatencyMode(false);
    std::cout << "Ultra-low latency disabled: " << 
                 (optimizer.isUltraLowLatencyActive() ? "No" : "Yes") << std::endl;
    
    optimizer.setUltraLowLatencyMode(true);
    std::cout << "Ultra-low latency enabled: " << 
                 (optimizer.isUltraLowLatencyActive() ? "Yes" : "No") << std::endl;
    
    // Test target latency adjustment
    std::cout << "\nTesting target latency adjustment..." << std::endl;
    optimizer.setTargetLatency(100); // 100ms target
    std::cout << "Target latency set to 100ms" << std::endl;
    
    // Process another stream to test the new settings
    success = optimizer.processStreamWithOptimizations(testAudio, streamId + 1, outputChunks);
    if (success) {
        std::cout << "Second stream processing successful with new settings" << std::endl;
    }
    
    // Stop the optimizer
    std::cout << "\nStopping advanced streaming optimizer..." << std::endl;
    optimizer.stop();
    
    std::cout << "Advanced streaming optimization test completed successfully!" << std::endl;
    
    return 0;
}