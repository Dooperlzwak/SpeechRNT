#include <gtest/gtest.h>
#include "stt/whisper_stt.hpp"
#include "stt/transcription_manager.hpp"
#include "core/websocket_server.hpp"
#include "core/client_session.hpp"
#include "audio/voice_activity_detector.hpp"
#include "utils/performance_monitor.hpp"
#include "fixtures/test_data_generator.hpp"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <memory>
#include <future>
#include <random>
#include <queue>
#include <mutex>

namespace stt_load_testing {

class STTLoadTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize performance monitor
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        perfMonitor.initialize(true);
        
        // Initialize test data generator
        testDataGenerator_ = std::make_unique<fixtures::TestDataGenerator>();
        
        // Initialize WebSocket server for integration load testing
        wsServer_ = std::make_unique<core::WebSocketServer>(8086);
        
        // Initialize STT components
        transcriptionManager_ = std::make_unique<stt::TranscriptionManager>();
        vadDetector_ = std::make_unique<audio::VoiceActivityDetector>();
        
        // Generate load test data
        generateLoadTestData();
    }
    
    void TearDown() override {
        if (wsServer_) {
            wsServer_->stop();
        }
        
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        perfMonitor.cleanup();
    }
    
    void generateLoadTestData() {
        loadTestAudio_.clear();
        
        // Generate various audio samples for load testing
        std::vector<float> durations = {0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f};
        
        for (size_t i = 0; i < durations.size(); ++i) {
            for (int variant = 0; variant < 5; ++variant) {
                std::string key = "load_" + std::to_string(i) + "_" + std::to_string(variant);
                loadTestAudio_[key] = testDataGenerator_->generateSpeechAudio(durations[i], 16000);
            }
        }
        
        // Generate streaming chunks for real-time load testing
        auto longAudio = testDataGenerator_->generateSpeechAudio(30.0f, 16000);
        streamingChunks_ = testDataGenerator_->splitIntoChunks(longAudio, 0.5f); // 500ms chunks
    }
    
    struct LoadTestResult {
        int totalRequests;
        int successfulRequests;
        int timeouts;
        int errors;
        double totalDuration;
        std::vector<double> latencies;
        double throughput;
        double successRate;
        double avgLatency;
        double p95Latency;
        double p99Latency;
    };
    
    LoadTestResult analyzeResults(const std::vector<std::future<std::vector<double>>>& futures,
                                 std::chrono::high_resolution_clock::time_point startTime,
                                 std::chrono::high_resolution_clock::time_point endTime,
                                 int expectedRequests) {
        LoadTestResult result = {};
        result.totalDuration = std::chrono::duration<double>(endTime - startTime).count();
        
        // Collect all latencies
        for (const auto& future : futures) {
            auto latencies = const_cast<std::future<std::vector<double>>&>(future).get();
            result.latencies.insert(result.latencies.end(), latencies.begin(), latencies.end());
        }
        
        result.successfulRequests = result.latencies.size();
        result.totalRequests = expectedRequests;
        result.timeouts = expectedRequests - result.successfulRequests;
        result.errors = 0; // Simplified for this test
        
        if (result.totalDuration > 0) {
            result.throughput = result.successfulRequests / result.totalDuration;
        }
        
        result.successRate = static_cast<double>(result.successfulRequests) / result.totalRequests;
        
        if (!result.latencies.empty()) {
            std::sort(result.latencies.begin(), result.latencies.end());
            result.avgLatency = std::accumulate(result.latencies.begin(), result.latencies.end(), 0.0) / result.latencies.size();
            result.p95Latency = result.latencies[static_cast<size_t>(result.latencies.size() * 0.95)];
            result.p99Latency = result.latencies[static_cast<size_t>(result.latencies.size() * 0.99)];
        }
        
        return result;
    }
    
    std::unique_ptr<fixtures::TestDataGenerator> testDataGenerator_;
    std::unique_ptr<core::WebSocketServer> wsServer_;
    std::unique_ptr<stt::TranscriptionManager> transcriptionManager_;
    std::unique_ptr<audio::VoiceActivityDetector> vadDetector_;
    
    std::map<std::string, std::vector<float>> loadTestAudio_;
    std::vector<std::vector<float>> streamingChunks_;
};

// Load Test 1: High Concurrency Transcription Load
TEST_F(STTLoadTest, HighConcurrencyTranscriptionLoad) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Initialize STT
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    // Test different concurrency levels
    std::vector<int> concurrencyLevels = {10, 25, 50, 100};
    const int requestsPerThread = 20;
    
    for (int concurrency : concurrencyLevels) {
        std::cout << "Testing high concurrency load: " << concurrency << " concurrent threads" << std::endl;
        
        std::vector<std::future<std::vector<double>>> futures;
        std::atomic<int> globalRequestId{0};
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // Start concurrent transcription threads
        for (int threadId = 0; threadId < concurrency; ++threadId) {
            futures.push_back(std::async(std::launch::async, [&, threadId]() {
                std::vector<double> threadLatencies;
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> audioDist(0, loadTestAudio_.size() - 1);
                
                for (int reqId = 0; reqId < requestsPerThread; ++reqId) {
                    uint32_t utteranceId = globalRequestId.fetch_add(1);
                    
                    // Select random audio sample
                    auto audioIt = loadTestAudio_.begin();
                    std::advance(audioIt, audioDist(gen));
                    auto testAudio = audioIt->second;
                    
                    std::atomic<bool> transcriptionComplete{false};
                    
                    stt::TranscriptionRequest request;
                    request.utterance_id = utteranceId;
                    request.audio_data = testAudio;
                    request.is_live = false;
                    request.callback = [&](uint32_t id, const stt::TranscriptionResult& result) {
                        transcriptionComplete = true;
                    };
                    
                    auto requestStart = std::chrono::high_resolution_clock::now();
                    transcriptionManager_->submitTranscription(request);
                    
                    // Wait for completion with timeout
                    auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(20);
                    while (!transcriptionComplete.load() && std::chrono::high_resolution_clock::now() < timeout) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    
                    if (transcriptionComplete.load()) {
                        auto requestEnd = std::chrono::high_resolution_clock::now();
                        double latency = std::chrono::duration<double, std::milli>(requestEnd - requestStart).count();
                        threadLatencies.push_back(latency);
                    }
                    
                    // Small random delay to simulate realistic usage patterns
                    std::uniform_int_distribution<> delayDist(50, 200);
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayDist(gen)));
                }
                
                return threadLatencies;
            }));
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        
        // Analyze results
        auto result = analyzeResults(futures, startTime, endTime, concurrency * requestsPerThread);
        
        // Record metrics
        std::string metricPrefix = "load_test.concurrency_" + std::to_string(concurrency) + ".";
        perfMonitor.recordThroughput(metricPrefix + "throughput_per_sec", result.throughput);
        perfMonitor.recordMetric(metricPrefix + "success_rate", result.successRate);
        perfMonitor.recordLatency(metricPrefix + "avg_latency_ms", result.avgLatency);
        perfMonitor.recordLatency(metricPrefix + "p95_latency_ms", result.p95Latency);
        perfMonitor.recordLatency(metricPrefix + "p99_latency_ms", result.p99Latency);
        
        std::cout << "  Results:" << std::endl;
        std::cout << "    Total requests: " << result.totalRequests << std::endl;
        std::cout << "    Successful: " << result.successfulRequests << std::endl;
        std::cout << "    Success rate: " << (result.successRate * 100) << "%" << std::endl;
        std::cout << "    Throughput: " << result.throughput << " req/sec" << std::endl;
        std::cout << "    Avg latency: " << result.avgLatency << "ms" << std::endl;
        std::cout << "    P95 latency: " << result.p95Latency << "ms" << std::endl;
        std::cout << "    P99 latency: " << result.p99Latency << "ms" << std::endl;
        
        // Performance assertions based on concurrency level
        if (concurrency <= 25) {
            EXPECT_GT(result.successRate, 0.95) << "Success rate should be >95% for low concurrency";
            EXPECT_LT(result.p95Latency, 2000.0) << "P95 latency should be <2s for low concurrency";
        } else if (concurrency <= 50) {
            EXPECT_GT(result.successRate, 0.90) << "Success rate should be >90% for medium concurrency";
            EXPECT_LT(result.p95Latency, 3000.0) << "P95 latency should be <3s for medium concurrency";
        } else {
            EXPECT_GT(result.successRate, 0.80) << "Success rate should be >80% for high concurrency";
            EXPECT_LT(result.p95Latency, 5000.0) << "P95 latency should be <5s for high concurrency";
        }
    }
    
    transcriptionManager_->stop();
}

// Load Test 2: Sustained Load Over Time
TEST_F(STTLoadTest, SustainedLoadOverTime) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Initialize STT
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    const int testDurationMinutes = 2; // 2 minutes for CI/testing
    const int concurrentThreads = 15;
    const int requestsPerMinute = 30; // Per thread
    
    std::cout << "Running sustained load test for " << testDurationMinutes << " minutes..." << std::endl;
    std::cout << "Concurrent threads: " << concurrentThreads << std::endl;
    std::cout << "Requests per minute per thread: " << requestsPerMinute << std::endl;
    
    std::atomic<bool> stopTest{false};
    std::vector<std::future<std::vector<double>>> futures;
    std::atomic<int> totalRequests{0};
    std::atomic<int> successfulRequests{0};
    
    // Metrics collection thread
    std::vector<double> throughputSamples;
    std::vector<size_t> memorySamples;
    std::thread metricsThread([&]() {
        while (!stopTest.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(30)); // Sample every 30 seconds
            
            if (!stopTest.load()) {
                double currentThroughput = successfulRequests.load() / 30.0; // Requests per second in last 30s
                size_t currentMemory = getCurrentMemoryUsage();
                
                throughputSamples.push_back(currentThroughput);
                memorySamples.push_back(currentMemory);
                
                std::cout << "  [" << (throughputSamples.size() * 30) << "s] Throughput: " 
                          << currentThroughput << " req/sec, Memory: " 
                          << (currentMemory / 1024 / 1024) << " MB" << std::endl;
                
                successfulRequests = 0; // Reset counter for next interval
            }
        }
    });
    
    auto testStart = std::chrono::high_resolution_clock::now();
    
    // Start sustained load threads
    for (int threadId = 0; threadId < concurrentThreads; ++threadId) {
        futures.push_back(std::async(std::launch::async, [&, threadId]() {
            std::vector<double> threadLatencies;
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> audioDist(0, loadTestAudio_.size() - 1);
            
            auto threadStart = std::chrono::high_resolution_clock::now();
            int requestCount = 0;
            
            while (!stopTest.load()) {
                uint32_t utteranceId = threadId * 10000 + requestCount++;
                totalRequests++;
                
                // Select random audio sample
                auto audioIt = loadTestAudio_.begin();
                std::advance(audioIt, audioDist(gen));
                auto testAudio = audioIt->second;
                
                std::atomic<bool> transcriptionComplete{false};
                
                stt::TranscriptionRequest request;
                request.utterance_id = utteranceId;
                request.audio_data = testAudio;
                request.is_live = false;
                request.callback = [&](uint32_t id, const stt::TranscriptionResult& result) {
                    transcriptionComplete = true;
                    successfulRequests++;
                };
                
                auto requestStart = std::chrono::high_resolution_clock::now();
                transcriptionManager_->submitTranscription(request);
                
                // Wait for completion with timeout
                auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(15);
                while (!transcriptionComplete.load() && 
                       std::chrono::high_resolution_clock::now() < timeout && 
                       !stopTest.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
                if (transcriptionComplete.load()) {
                    auto requestEnd = std::chrono::high_resolution_clock::now();
                    double latency = std::chrono::duration<double, std::milli>(requestEnd - requestStart).count();
                    threadLatencies.push_back(latency);
                }
                
                // Calculate delay to maintain target rate
                auto elapsed = std::chrono::high_resolution_clock::now() - threadStart;
                auto expectedRequests = std::chrono::duration<double, std::milli>(elapsed).count() / (60000.0 / requestsPerMinute);
                
                if (requestCount > expectedRequests) {
                    auto delayMs = static_cast<int>((requestCount - expectedRequests) * (60000.0 / requestsPerMinute));
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                }
            }
            
            return threadLatencies;
        }));
    }
    
    // Run test for specified duration
    std::this_thread::sleep_for(std::chrono::minutes(testDurationMinutes));
    stopTest = true;
    
    auto testEnd = std::chrono::high_resolution_clock::now();
    
    // Wait for all threads to complete
    metricsThread.join();
    
    // Collect results
    auto result = analyzeResults(futures, testStart, testEnd, totalRequests.load());
    
    // Analyze sustained performance
    if (!throughputSamples.empty()) {
        double avgThroughput = std::accumulate(throughputSamples.begin(), throughputSamples.end(), 0.0) / throughputSamples.size();
        double minThroughput = *std::min_element(throughputSamples.begin(), throughputSamples.end());
        double maxThroughput = *std::max_element(throughputSamples.begin(), throughputSamples.end());
        
        perfMonitor.recordThroughput("sustained_load.avg_throughput_per_sec", avgThroughput);
        perfMonitor.recordThroughput("sustained_load.min_throughput_per_sec", minThroughput);
        perfMonitor.recordThroughput("sustained_load.max_throughput_per_sec", maxThroughput);
    }
    
    if (!memorySamples.empty()) {
        size_t avgMemory = std::accumulate(memorySamples.begin(), memorySamples.end(), 0ULL) / memorySamples.size();
        size_t maxMemory = *std::max_element(memorySamples.begin(), memorySamples.end());
        
        perfMonitor.recordMetric("sustained_load.avg_memory_mb", static_cast<double>(avgMemory / 1024 / 1024));
        perfMonitor.recordMetric("sustained_load.max_memory_mb", static_cast<double>(maxMemory / 1024 / 1024));
    }
    
    perfMonitor.recordMetric("sustained_load.total_duration_sec", result.totalDuration);
    perfMonitor.recordMetric("sustained_load.success_rate", result.successRate);
    perfMonitor.recordLatency("sustained_load.avg_latency_ms", result.avgLatency);
    perfMonitor.recordLatency("sustained_load.p95_latency_ms", result.p95Latency);
    
    std::cout << "Sustained Load Test Results:" << std::endl;
    std::cout << "  Duration: " << result.totalDuration << " seconds" << std::endl;
    std::cout << "  Total requests: " << result.totalRequests << std::endl;
    std::cout << "  Successful requests: " << result.successfulRequests << std::endl;
    std::cout << "  Success rate: " << (result.successRate * 100) << "%" << std::endl;
    std::cout << "  Overall throughput: " << result.throughput << " req/sec" << std::endl;
    std::cout << "  Average latency: " << result.avgLatency << "ms" << std::endl;
    std::cout << "  P95 latency: " << result.p95Latency << "ms" << std::endl;
    
    // Sustained load assertions
    EXPECT_GT(result.successRate, 0.85) << "Success rate should remain >85% during sustained load";
    EXPECT_LT(result.p95Latency, 3000.0) << "P95 latency should remain <3s during sustained load";
    EXPECT_GT(result.throughput, 5.0) << "Should maintain >5 req/sec throughput during sustained load";
    
    transcriptionManager_->stop();
}

// Load Test 3: WebSocket Integration Load Testing
TEST_F(STTLoadTest, WebSocketIntegrationLoadTest) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Start WebSocket server
    wsServer_->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // Allow server to start
    
    const int numClients = 20;
    const int messagesPerClient = 25;
    
    std::vector<std::shared_ptr<core::ClientSession>> sessions;
    std::vector<std::future<std::vector<double>>> futures;
    
    std::cout << "Running WebSocket integration load test..." << std::endl;
    std::cout << "Clients: " << numClients << ", Messages per client: " << messagesPerClient << std::endl;
    
    // Create client sessions
    for (int i = 0; i < numClients; ++i) {
        auto session = std::make_shared<core::ClientSession>("load-test-ws-" + std::to_string(i));
        session->setWebSocketServer(wsServer_.get());
        session->setLanguageConfig("en", "es");
        sessions.push_back(session);
    }
    
    auto testStart = std::chrono::high_resolution_clock::now();
    
    // Start WebSocket load test threads
    for (int clientId = 0; clientId < numClients; ++clientId) {
        futures.push_back(std::async(std::launch::async, [&, clientId]() {
            std::vector<double> clientLatencies;
            auto& session = sessions[clientId];
            
            std::atomic<int> messagesReceived{0};
            std::queue<std::chrono::high_resolution_clock::time_point> messageTimes;
            std::mutex timesMutex;
            
            // Set up message callback
            session->setMessageCallback([&](const std::string& message) {
                std::lock_guard<std::mutex> lock(timesMutex);
                if (!messageTimes.empty()) {
                    auto sendTime = messageTimes.front();
                    messageTimes.pop();
                    
                    auto receiveTime = std::chrono::high_resolution_clock::now();
                    double latency = std::chrono::duration<double, std::milli>(receiveTime - sendTime).count();
                    clientLatencies.push_back(latency);
                    messagesReceived++;
                }
            });
            
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> audioDist(0, loadTestAudio_.size() - 1);
            
            // Send messages
            for (int msgId = 0; msgId < messagesPerClient; ++msgId) {
                // Select random audio sample
                auto audioIt = loadTestAudio_.begin();
                std::advance(audioIt, audioDist(gen));
                auto testAudio = audioIt->second;
                
                // Convert to PCM for WebSocket transmission
                std::vector<int16_t> pcmData;
                pcmData.reserve(testAudio.size());
                for (float sample : testAudio) {
                    pcmData.push_back(static_cast<int16_t>(sample * 32767.0f));
                }
                
                {
                    std::lock_guard<std::mutex> lock(timesMutex);
                    messageTimes.push(std::chrono::high_resolution_clock::now());
                }
                
                // Send binary audio data
                std::string_view binaryData(
                    reinterpret_cast<const char*>(pcmData.data()),
                    pcmData.size() * sizeof(int16_t)
                );
                session->handleBinaryMessage(binaryData);
                
                // Delay between messages
                std::uniform_int_distribution<> delayDist(100, 300);
                std::this_thread::sleep_for(std::chrono::milliseconds(delayDist(gen)));
            }
            
            // Wait for remaining responses
            auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(30);
            while (messagesReceived.load() < messagesPerClient && 
                   std::chrono::high_resolution_clock::now() < timeout) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            
            return clientLatencies;
        }));
    }
    
    auto testEnd = std::chrono::high_resolution_clock::now();
    
    // Analyze WebSocket load test results
    auto result = analyzeResults(futures, testStart, testEnd, numClients * messagesPerClient);
    
    // Record WebSocket-specific metrics
    perfMonitor.recordThroughput("websocket_load.throughput_per_sec", result.throughput);
    perfMonitor.recordMetric("websocket_load.success_rate", result.successRate);
    perfMonitor.recordLatency("websocket_load.avg_latency_ms", result.avgLatency);
    perfMonitor.recordLatency("websocket_load.p95_latency_ms", result.p95Latency);
    perfMonitor.recordLatency("websocket_load.p99_latency_ms", result.p99Latency);
    
    std::cout << "WebSocket Load Test Results:" << std::endl;
    std::cout << "  Total messages: " << result.totalRequests << std::endl;
    std::cout << "  Successful responses: " << result.successfulRequests << std::endl;
    std::cout << "  Success rate: " << (result.successRate * 100) << "%" << std::endl;
    std::cout << "  Throughput: " << result.throughput << " msg/sec" << std::endl;
    std::cout << "  Average latency: " << result.avgLatency << "ms" << std::endl;
    std::cout << "  P95 latency: " << result.p95Latency << "ms" << std::endl;
    std::cout << "  P99 latency: " << result.p99Latency << "ms" << std::endl;
    
    // WebSocket load test assertions
    EXPECT_GT(result.successRate, 0.90) << "WebSocket success rate should be >90%";
    EXPECT_LT(result.p95Latency, 2000.0) << "WebSocket P95 latency should be <2s";
    EXPECT_GT(result.throughput, 8.0) << "WebSocket throughput should be >8 msg/sec";
}

// Load Test 4: Streaming Transcription Load
TEST_F(STTLoadTest, StreamingTranscriptionLoad) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Initialize STT for streaming
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    const int numStreamingSessions = 10;
    const int chunksPerSession = 20;
    
    std::cout << "Running streaming transcription load test..." << std::endl;
    std::cout << "Streaming sessions: " << numStreamingSessions << std::endl;
    std::cout << "Chunks per session: " << chunksPerSession << std::endl;
    
    std::vector<std::future<std::vector<double>>> futures;
    std::atomic<int> totalChunks{0};
    std::atomic<int> processedChunks{0};
    
    auto testStart = std::chrono::high_resolution_clock::now();
    
    // Start streaming sessions
    for (int sessionId = 0; sessionId < numStreamingSessions; ++sessionId) {
        futures.push_back(std::async(std::launch::async, [&, sessionId]() {
            std::vector<double> sessionLatencies;
            uint32_t utteranceId = 9000 + sessionId;
            
            std::atomic<int> partialResults{0};
            std::atomic<bool> finalResultReceived{false};
            
            // Set up streaming callback
            auto streamingCallback = [&](uint32_t id, const stt::TranscriptionResult& result) {
                if (result.is_partial) {
                    partialResults++;
                } else {
                    finalResultReceived = true;
                }
                processedChunks++;
            };
            
            // Process streaming chunks
            for (int chunkId = 0; chunkId < chunksPerSession && chunkId < streamingChunks_.size(); ++chunkId) {
                totalChunks++;
                
                auto chunkStart = std::chrono::high_resolution_clock::now();
                
                stt::TranscriptionRequest request;
                request.utterance_id = utteranceId;
                request.audio_data = streamingChunks_[chunkId];
                request.is_live = true;
                request.callback = streamingCallback;
                
                transcriptionManager_->submitTranscription(request);
                
                // Wait for processing
                auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(5);
                int initialProcessed = processedChunks.load();
                while (processedChunks.load() == initialProcessed && 
                       std::chrono::high_resolution_clock::now() < timeout) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                
                if (processedChunks.load() > initialProcessed) {
                    auto chunkEnd = std::chrono::high_resolution_clock::now();
                    double latency = std::chrono::duration<double, std::milli>(chunkEnd - chunkStart).count();
                    sessionLatencies.push_back(latency);
                }
                
                // Simulate real-time streaming delay
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            
            return sessionLatencies;
        }));
    }
    
    auto testEnd = std::chrono::high_resolution_clock::now();
    
    // Analyze streaming load results
    auto result = analyzeResults(futures, testStart, testEnd, totalChunks.load());
    
    // Record streaming-specific metrics
    perfMonitor.recordThroughput("streaming_load.chunk_throughput_per_sec", result.throughput);
    perfMonitor.recordMetric("streaming_load.success_rate", result.successRate);
    perfMonitor.recordLatency("streaming_load.avg_chunk_latency_ms", result.avgLatency);
    perfMonitor.recordLatency("streaming_load.p95_chunk_latency_ms", result.p95Latency);
    
    std::cout << "Streaming Load Test Results:" << std::endl;
    std::cout << "  Total chunks: " << result.totalRequests << std::endl;
    std::cout << "  Processed chunks: " << result.successfulRequests << std::endl;
    std::cout << "  Success rate: " << (result.successRate * 100) << "%" << std::endl;
    std::cout << "  Chunk throughput: " << result.throughput << " chunks/sec" << std::endl;
    std::cout << "  Average chunk latency: " << result.avgLatency << "ms" << std::endl;
    std::cout << "  P95 chunk latency: " << result.p95Latency << "ms" << std::endl;
    
    // Streaming load assertions
    EXPECT_GT(result.successRate, 0.85) << "Streaming success rate should be >85%";
    EXPECT_LT(result.p95Latency, 1000.0) << "Streaming P95 latency should be <1s";
    EXPECT_GT(result.throughput, 10.0) << "Should process >10 chunks/sec";
    
    transcriptionManager_->stop();
}

// Helper function to get current memory usage
size_t STTLoadTest::getCurrentMemoryUsage() {
    // Platform-specific memory usage implementation
    // This is a simplified mock implementation for testing
    static size_t baseMemory = 300 * 1024 * 1024; // 300MB base
    static std::atomic<size_t> memoryCounter{0};
    
    return baseMemory + memoryCounter.fetch_add(512 * 1024); // Simulate 512KB growth per call
}

} // namespace stt_load_testing

// Main function for standalone execution
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "=== STT Load Testing Suite ===" << std::endl;
    std::cout << "Testing concurrent transcription scenarios and system limits" << std::endl;
    
    return RUN_ALL_TESTS();
}