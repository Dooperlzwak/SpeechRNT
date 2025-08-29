#include <gtest/gtest.h>
#include "core/websocket_server.hpp"
#include "core/client_session.hpp"
#include "core/translation_pipeline.hpp"
#include "utils/logging.hpp"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <memory>
#include <random>
#include <future>

namespace performance {

class LoadTestingTest : public ::testing::Test {
protected:
    void SetUp() override {
        utils::Logger::initialize();
        
        // Start server on test port
        server_ = std::make_unique<core::WebSocketServer>(8084);
        server_->start();
        
        // Give server time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    void TearDown() override {
        if (server_) {
            server_->stop();
        }
    }
    
    // Generate test audio data
    std::vector<float> generateTestAudio(float duration, int sampleRate = 16000) {
        std::vector<float> audio;
        int numSamples = static_cast<int>(duration * sampleRate);
        audio.reserve(numSamples);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<float> noise(0.0f, 0.1f);
        
        for (int i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            float sample = 0.3f * std::sin(2.0f * M_PI * 200.0f * t) +
                          0.2f * std::sin(2.0f * M_PI * 400.0f * t) +
                          noise(gen);
            audio.push_back(std::clamp(sample, -1.0f, 1.0f));
        }
        
        return audio;
    }
    
    // Convert float audio to PCM
    std::vector<int16_t> audioToPCM(const std::vector<float>& audio) {
        std::vector<int16_t> pcm;
        pcm.reserve(audio.size());
        for (float sample : audio) {
            pcm.push_back(static_cast<int16_t>(sample * 32767.0f));
        }
        return pcm;
    }
    
    std::unique_ptr<core::WebSocketServer> server_;
};

// Test concurrent client connections
TEST_F(LoadTestingTest, ConcurrentClientConnections) {
    const int numClients = 50;
    const int testDurationSeconds = 10;
    
    std::vector<std::shared_ptr<core::ClientSession>> sessions;
    std::vector<std::thread> clientThreads;
    std::atomic<int> successfulConnections{0};
    std::atomic<int> messagesProcessed{0};
    std::atomic<bool> stopTest{false};
    
    // Create client sessions
    for (int i = 0; i < numClients; ++i) {
        auto session = std::make_shared<core::ClientSession>("load-test-" + std::to_string(i));
        session->setWebSocketServer(server_.get());
        session->setLanguageConfig("en", "es");
        
        sessions.push_back(session);
        successfulConnections++;
    }
    
    // Start client threads
    for (int i = 0; i < numClients; ++i) {
        clientThreads.emplace_back([&, i]() {
            auto& session = sessions[i];
            
            // Set up message callback
            session->setMessageCallback([&](const std::string& message) {
                messagesProcessed++;
            });
            
            // Generate and send audio data
            auto audioData = generateTestAudio(1.0f); // 1 second of audio
            auto pcmData = audioToPCM(audioData);
            
            const size_t chunkSize = 1024;
            int messageCount = 0;
            
            while (!stopTest.load() && messageCount < 100) {
                // Send audio in chunks
                for (size_t j = 0; j < pcmData.size(); j += chunkSize) {
                    if (stopTest.load()) break;
                    
                    size_t currentChunkSize = std::min(chunkSize, pcmData.size() - j);
                    std::string_view binaryData(
                        reinterpret_cast<const char*>(pcmData.data() + j),
                        currentChunkSize * sizeof(int16_t)
                    );
                    
                    session->handleBinaryMessage(binaryData);
                    messageCount++;
                    
                    // Small delay to simulate real-time streaming
                    std::this_thread::sleep_for(std::chrono::milliseconds(64));
                }
                
                // Pause between utterances
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
    }
    
    // Run test for specified duration
    std::this_thread::sleep_for(std::chrono::seconds(testDurationSeconds));
    stopTest = true;
    
    // Wait for all threads to complete
    for (auto& thread : clientThreads) {
        thread.join();
    }
    
    // Verify results
    EXPECT_EQ(successfulConnections.load(), numClients);
    EXPECT_GT(messagesProcessed.load(), 0);
    
    std::cout << "Load Test Results:" << std::endl;
    std::cout << "  Concurrent clients: " << numClients << std::endl;
    std::cout << "  Successful connections: " << successfulConnections.load() << std::endl;
    std::cout << "  Messages processed: " << messagesProcessed.load() << std::endl;
    std::cout << "  Messages per second: " << messagesProcessed.load() / testDurationSeconds << std::endl;
}

// Test memory usage under load
TEST_F(LoadTestingTest, MemoryUsageUnderLoad) {
    const int numSessions = 20;
    const int audioChunksPerSession = 100;
    
    std::vector<std::shared_ptr<core::ClientSession>> sessions;
    
    // Measure initial memory usage
    size_t initialMemory = getCurrentMemoryUsage();
    
    // Create sessions and send audio data
    for (int i = 0; i < numSessions; ++i) {
        auto session = std::make_shared<core::ClientSession>("memory-test-" + std::to_string(i));
        session->setWebSocketServer(server_.get());
        session->setLanguageConfig("en", "es");
        
        // Send multiple audio chunks
        auto audioData = generateTestAudio(2.0f); // 2 seconds of audio
        auto pcmData = audioToPCM(audioData);
        
        for (int j = 0; j < audioChunksPerSession; ++j) {
            std::string_view binaryData(
                reinterpret_cast<const char*>(pcmData.data()),
                pcmData.size() * sizeof(int16_t)
            );
            session->handleBinaryMessage(binaryData);
        }
        
        sessions.push_back(session);
    }
    
    // Measure peak memory usage
    size_t peakMemory = getCurrentMemoryUsage();
    
    // Clean up sessions
    sessions.clear();
    
    // Force garbage collection (if applicable)
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Measure final memory usage
    size_t finalMemory = getCurrentMemoryUsage();
    
    // Verify memory usage is reasonable
    size_t memoryIncrease = peakMemory - initialMemory;
    size_t memoryPerSession = memoryIncrease / numSessions;
    
    std::cout << "Memory Usage Results:" << std::endl;
    std::cout << "  Initial memory: " << initialMemory / 1024 / 1024 << " MB" << std::endl;
    std::cout << "  Peak memory: " << peakMemory / 1024 / 1024 << " MB" << std::endl;
    std::cout << "  Final memory: " << finalMemory / 1024 / 1024 << " MB" << std::endl;
    std::cout << "  Memory per session: " << memoryPerSession / 1024 << " KB" << std::endl;
    
    // Memory should be reasonable (less than 10MB per session)
    EXPECT_LT(memoryPerSession, 10 * 1024 * 1024);
    
    // Memory should be mostly cleaned up
    EXPECT_LT(finalMemory - initialMemory, memoryIncrease / 2);
}

// Test throughput and latency under load
TEST_F(LoadTestingTest, ThroughputAndLatency) {
    const int numClients = 10;
    const int messagesPerClient = 50;
    
    std::vector<std::shared_ptr<core::ClientSession>> sessions;
    std::vector<std::future<std::vector<double>>> futures;
    
    // Create sessions
    for (int i = 0; i < numClients; ++i) {
        auto session = std::make_shared<core::ClientSession>("throughput-test-" + std::to_string(i));
        session->setWebSocketServer(server_.get());
        session->setLanguageConfig("en", "es");
        sessions.push_back(session);
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Start client tasks
    for (int i = 0; i < numClients; ++i) {
        futures.push_back(std::async(std::launch::async, [&, i]() {
            std::vector<double> latencies;
            auto& session = sessions[i];
            
            std::atomic<bool> responseReceived{false};
            std::chrono::high_resolution_clock::time_point requestTime;
            
            // Set up response callback
            session->setMessageCallback([&](const std::string& message) {
                if (message.find("transcription_update") != std::string::npos) {
                    auto responseTime = std::chrono::high_resolution_clock::now();
                    auto latency = std::chrono::duration<double, std::milli>(
                        responseTime - requestTime).count();
                    latencies.push_back(latency);
                    responseReceived = true;
                }
            });
            
            // Send messages and measure latency
            auto audioData = generateTestAudio(0.5f); // 0.5 seconds of audio
            auto pcmData = audioToPCM(audioData);
            
            for (int j = 0; j < messagesPerClient; ++j) {
                requestTime = std::chrono::high_resolution_clock::now();
                responseReceived = false;
                
                std::string_view binaryData(
                    reinterpret_cast<const char*>(pcmData.data()),
                    pcmData.size() * sizeof(int16_t)
                );
                session->handleBinaryMessage(binaryData);
                
                // Wait for response with timeout
                auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(5);
                while (!responseReceived.load() && 
                       std::chrono::high_resolution_clock::now() < timeout) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                
                if (!responseReceived.load()) {
                    latencies.push_back(-1.0); // Timeout indicator
                }
                
                // Small delay between requests
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            return latencies;
        }));
    }
    
    // Collect results
    std::vector<double> allLatencies;
    int totalMessages = 0;
    int timeouts = 0;
    
    for (auto& future : futures) {
        auto latencies = future.get();
        for (double latency : latencies) {
            if (latency >= 0) {
                allLatencies.push_back(latency);
            } else {
                timeouts++;
            }
            totalMessages++;
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalDuration = std::chrono::duration<double>(endTime - startTime).count();
    
    // Calculate statistics
    if (!allLatencies.empty()) {
        std::sort(allLatencies.begin(), allLatencies.end());
        
        double avgLatency = std::accumulate(allLatencies.begin(), allLatencies.end(), 0.0) / allLatencies.size();
        double medianLatency = allLatencies[allLatencies.size() / 2];
        double p95Latency = allLatencies[static_cast<size_t>(allLatencies.size() * 0.95)];
        double maxLatency = allLatencies.back();
        
        double throughput = totalMessages / totalDuration;
        
        std::cout << "Throughput and Latency Results:" << std::endl;
        std::cout << "  Total messages: " << totalMessages << std::endl;
        std::cout << "  Successful responses: " << allLatencies.size() << std::endl;
        std::cout << "  Timeouts: " << timeouts << std::endl;
        std::cout << "  Throughput: " << throughput << " messages/second" << std::endl;
        std::cout << "  Average latency: " << avgLatency << " ms" << std::endl;
        std::cout << "  Median latency: " << medianLatency << " ms" << std::endl;
        std::cout << "  95th percentile latency: " << p95Latency << " ms" << std::endl;
        std::cout << "  Max latency: " << maxLatency << " ms" << std::endl;
        
        // Performance assertions
        EXPECT_GT(throughput, 10.0); // At least 10 messages per second
        EXPECT_LT(avgLatency, 1000.0); // Average latency under 1 second
        EXPECT_LT(p95Latency, 2000.0); // 95th percentile under 2 seconds
        EXPECT_LT(timeouts, totalMessages * 0.05); // Less than 5% timeouts
    }
}

// Test resource cleanup under stress
TEST_F(LoadTestingTest, ResourceCleanupUnderStress) {
    const int numIterations = 100;
    const int sessionsPerIteration = 10;
    
    for (int iteration = 0; iteration < numIterations; ++iteration) {
        std::vector<std::shared_ptr<core::ClientSession>> sessions;
        
        // Create sessions
        for (int i = 0; i < sessionsPerIteration; ++i) {
            auto session = std::make_shared<core::ClientSession>(
                "cleanup-test-" + std::to_string(iteration) + "-" + std::to_string(i)
            );
            session->setWebSocketServer(server_.get());
            session->setLanguageConfig("en", "es");
            
            // Send some audio data
            auto audioData = generateTestAudio(0.2f); // Short audio clip
            auto pcmData = audioToPCM(audioData);
            
            std::string_view binaryData(
                reinterpret_cast<const char*>(pcmData.data()),
                pcmData.size() * sizeof(int16_t)
            );
            session->handleBinaryMessage(binaryData);
            
            sessions.push_back(session);
        }
        
        // Explicitly clear sessions to test cleanup
        sessions.clear();
        
        // Small delay to allow cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Check memory usage periodically
        if (iteration % 20 == 0) {
            size_t currentMemory = getCurrentMemoryUsage();
            std::cout << "Iteration " << iteration << ", Memory: " 
                     << currentMemory / 1024 / 1024 << " MB" << std::endl;
        }
    }
    
    // Final memory check
    std::this_thread::sleep_for(std::chrono::seconds(1));
    size_t finalMemory = getCurrentMemoryUsage();
    
    std::cout << "Resource Cleanup Test Completed" << std::endl;
    std::cout << "Final memory usage: " << finalMemory / 1024 / 1024 << " MB" << std::endl;
    
    // Memory should be stable (not continuously growing)
    EXPECT_LT(finalMemory, 500 * 1024 * 1024); // Less than 500MB
}

// Test pipeline performance under concurrent load
TEST_F(LoadTestingTest, PipelinePerformanceUnderLoad) {
    const int numConcurrentPipelines = 5;
    const int utterancesPerPipeline = 20;
    
    std::vector<std::future<std::vector<double>>> futures;
    
    for (int i = 0; i < numConcurrentPipelines; ++i) {
        futures.push_back(std::async(std::launch::async, [&, i]() {
            std::vector<double> processingTimes;
            
            auto session = std::make_shared<core::ClientSession>("pipeline-test-" + std::to_string(i));
            session->setWebSocketServer(server_.get());
            session->setLanguageConfig("en", "es");
            
            std::atomic<bool> processingComplete{false};
            std::chrono::high_resolution_clock::time_point startTime;
            
            session->setMessageCallback([&](const std::string& message) {
                if (message.find("translation_result") != std::string::npos) {
                    auto endTime = std::chrono::high_resolution_clock::now();
                    auto processingTime = std::chrono::duration<double, std::milli>(
                        endTime - startTime).count();
                    processingTimes.push_back(processingTime);
                    processingComplete = true;
                }
            });
            
            for (int j = 0; j < utterancesPerPipeline; ++j) {
                startTime = std::chrono::high_resolution_clock::now();
                processingComplete = false;
                
                // Generate varied audio lengths
                float audioDuration = 0.5f + (j % 3) * 0.5f; // 0.5 to 2.0 seconds
                auto audioData = generateTestAudio(audioDuration);
                auto pcmData = audioToPCM(audioData);
                
                std::string_view binaryData(
                    reinterpret_cast<const char*>(pcmData.data()),
                    pcmData.size() * sizeof(int16_t)
                );
                session->handleBinaryMessage(binaryData);
                
                // Wait for processing to complete
                auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(10);
                while (!processingComplete.load() && 
                       std::chrono::high_resolution_clock::now() < timeout) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                
                if (!processingComplete.load()) {
                    processingTimes.push_back(-1.0); // Timeout
                }
                
                // Brief pause between utterances
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            
            return processingTimes;
        }));
    }
    
    // Collect results
    std::vector<double> allProcessingTimes;
    int totalUtterances = 0;
    int timeouts = 0;
    
    for (auto& future : futures) {
        auto times = future.get();
        for (double time : times) {
            if (time >= 0) {
                allProcessingTimes.push_back(time);
            } else {
                timeouts++;
            }
            totalUtterances++;
        }
    }
    
    // Calculate statistics
    if (!allProcessingTimes.empty()) {
        std::sort(allProcessingTimes.begin(), allProcessingTimes.end());
        
        double avgTime = std::accumulate(allProcessingTimes.begin(), allProcessingTimes.end(), 0.0) / allProcessingTimes.size();
        double medianTime = allProcessingTimes[allProcessingTimes.size() / 2];
        double p95Time = allProcessingTimes[static_cast<size_t>(allProcessingTimes.size() * 0.95)];
        
        std::cout << "Pipeline Performance Results:" << std::endl;
        std::cout << "  Concurrent pipelines: " << numConcurrentPipelines << std::endl;
        std::cout << "  Total utterances: " << totalUtterances << std::endl;
        std::cout << "  Successful processing: " << allProcessingTimes.size() << std::endl;
        std::cout << "  Timeouts: " << timeouts << std::endl;
        std::cout << "  Average processing time: " << avgTime << " ms" << std::endl;
        std::cout << "  Median processing time: " << medianTime << " ms" << std::endl;
        std::cout << "  95th percentile time: " << p95Time << " ms" << std::endl;
        
        // Performance assertions
        EXPECT_LT(avgTime, 3000.0); // Average under 3 seconds
        EXPECT_LT(p95Time, 5000.0); // 95th percentile under 5 seconds
        EXPECT_LT(timeouts, totalUtterances * 0.1); // Less than 10% timeouts
    }
}

// Helper function to get current memory usage (platform-specific implementation needed)
size_t LoadTestingTest::getCurrentMemoryUsage() {
    // This is a simplified implementation
    // In practice, you would use platform-specific APIs like:
    // - Linux: /proc/self/status or getrusage()
    // - Windows: GetProcessMemoryInfo()
    // - macOS: task_info()
    
    // For now, return a mock value
    static size_t baseMemory = 100 * 1024 * 1024; // 100MB base
    static std::atomic<size_t> memoryCounter{0};
    
    return baseMemory + memoryCounter.fetch_add(1024); // Simulate memory growth
}

} // namespace performance