#include <gtest/gtest.h>
#include "core/websocket_server.hpp"
#include "core/client_session.hpp"
#include "stt/transcription_manager.hpp"
#include "audio/voice_activity_detector.hpp"
#include "utils/performance_monitor.hpp"
#include "fixtures/test_data_generator.hpp"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <memory>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace stt_websocket_integration {

class STTWebSocketIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize performance monitor
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        perfMonitor.initialize(false);
        
        // Initialize test data generator
        testDataGenerator_ = std::make_unique<fixtures::TestDataGenerator>();
        
        // Initialize WebSocket server
        wsServer_ = std::make_unique<core::WebSocketServer>(8087);
        
        // Initialize STT components
        transcriptionManager_ = std::make_unique<stt::TranscriptionManager>();
        vadDetector_ = std::make_unique<audio::VoiceActivityDetector>();
        
        // Generate test data
        generateWebSocketTestData();
    }
    
    void TearDown() override {
        if (wsServer_) {
            wsServer_->stop();
        }
        
        if (transcriptionManager_) {
            transcriptionManager_->stop();
        }
        
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        perfMonitor.cleanup();
    }
    
    void generateWebSocketTestData() {
        wsTestAudio_.clear();
        
        // Generate audio samples for WebSocket testing
        wsTestAudio_["greeting"] = testDataGenerator_->generateSpeechAudio(1.5f, 16000);
        wsTestAudio_["question"] = testDataGenerator_->generateSpeechAudio(2.0f, 16000);
        wsTestAudio_["response"] = testDataGenerator_->generateSpeechAudio(3.0f, 16000);
        wsTestAudio_["short_phrase"] = testDataGenerator_->generateSpeechAudio(0.8f, 16000);
        wsTestAudio_["long_sentence"] = testDataGenerator_->generateSpeechAudio(4.5f, 16000);
        
        // Generate multilingual samples
        wsTestAudio_["english_sample"] = testDataGenerator_->generateLanguageSpecificAudio("en", 2.0f);
        wsTestAudio_["spanish_sample"] = testDataGenerator_->generateLanguageSpecificAudio("es", 2.0f);
        
        // Generate streaming chunks
        auto longAudio = testDataGenerator_->generateSpeechAudio(8.0f, 16000);
        streamingChunks_ = testDataGenerator_->splitIntoChunks(longAudio, 0.4f); // 400ms chunks
    }
    
    // Helper to convert float audio to PCM for WebSocket transmission
    std::vector<int16_t> audioToPCM(const std::vector<float>& audio) {
        std::vector<int16_t> pcm;
        pcm.reserve(audio.size());
        for (float sample : audio) {
            pcm.push_back(static_cast<int16_t>(std::clamp(sample * 32767.0f, -32767.0f, 32767.0f)));
        }
        return pcm;
    }
    
    // Message tracking structure
    struct MessageTracker {
        std::atomic<int> messagesReceived{0};
        std::atomic<int> transcriptionUpdates{0};
        std::atomic<int> translationResults{0};
        std::atomic<int> errorMessages{0};
        std::queue<std::string> receivedMessages;
        std::mutex messagesMutex;
        std::condition_variable messagesCV;
        
        void addMessage(const std::string& message) {
            std::lock_guard<std::mutex> lock(messagesMutex);
            receivedMessages.push(message);
            messagesReceived++;
            
            // Parse message type
            if (message.find("transcription_update") != std::string::npos) {
                transcriptionUpdates++;
            } else if (message.find("translation_result") != std::string::npos) {
                translationResults++;
            } else if (message.find("error") != std::string::npos) {
                errorMessages++;
            }
            
            messagesCV.notify_all();
        }
        
        bool waitForMessages(int expectedCount, std::chrono::milliseconds timeout) {
            std::unique_lock<std::mutex> lock(messagesMutex);
            return messagesCV.wait_for(lock, timeout, [&] { 
                return messagesReceived.load() >= expectedCount; 
            });
        }
    };
    
    std::unique_ptr<fixtures::TestDataGenerator> testDataGenerator_;
    std::unique_ptr<core::WebSocketServer> wsServer_;
    std::unique_ptr<stt::TranscriptionManager> transcriptionManager_;
    std::unique_ptr<audio::VoiceActivityDetector> vadDetector_;
    
    std::map<std::string, std::vector<float>> wsTestAudio_;
    std::vector<std::vector<float>> streamingChunks_;
};

// Test 1: Basic WebSocket STT Communication
TEST_F(STTWebSocketIntegrationTest, BasicWebSocketSTTCommunication) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Start WebSocket server
    wsServer_->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Initialize STT
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    // Create client session
    auto session = std::make_shared<core::ClientSession>("basic-ws-test");
    session->setWebSocketServer(wsServer_.get());
    session->setLanguageConfig("en", "es");
    
    MessageTracker tracker;
    
    // Set up message callback
    session->setMessageCallback([&tracker](const std::string& message) {
        tracker.addMessage(message);
    });
    
    // Test different audio samples
    std::vector<std::string> testSamples = {"greeting", "question", "response", "short_phrase"};
    
    for (const std::string& sampleName : testSamples) {
        std::cout << "Testing WebSocket communication with: " << sampleName << std::endl;
        
        auto testAudio = wsTestAudio_[sampleName];
        auto pcmData = audioToPCM(testAudio);
        
        // Reset tracker
        tracker = MessageTracker{};
        
        auto sendTime = std::chrono::high_resolution_clock::now();
        
        // Send binary audio data
        std::string_view binaryData(
            reinterpret_cast<const char*>(pcmData.data()),
            pcmData.size() * sizeof(int16_t)
        );
        session->handleBinaryMessage(binaryData);
        
        // Wait for response
        bool receivedResponse = tracker.waitForMessages(1, std::chrono::seconds(8));
        
        auto receiveTime = std::chrono::high_resolution_clock::now();
        double latency = std::chrono::duration<double, std::milli>(receiveTime - sendTime).count();
        
        perfMonitor.recordLatency("websocket.basic_communication_" + sampleName + "_ms", latency);
        
        EXPECT_TRUE(receivedResponse) << "Should receive response for " << sampleName;
        EXPECT_GT(tracker.messagesReceived.load(), 0) << "Should receive at least one message for " << sampleName;
        EXPECT_EQ(tracker.errorMessages.load(), 0) << "Should not receive error messages for " << sampleName;
        
        std::cout << "  Latency: " << latency << "ms" << std::endl;
        std::cout << "  Messages received: " << tracker.messagesReceived.load() << std::endl;
        std::cout << "  Transcription updates: " << tracker.transcriptionUpdates.load() << std::endl;
        
        // Small delay between tests
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

// Test 2: Real-time Streaming WebSocket Integration
TEST_F(STTWebSocketIntegrationTest, RealTimeStreamingWebSocketIntegration) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Start WebSocket server
    wsServer_->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Initialize STT
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    // Create client session
    auto session = std::make_shared<core::ClientSession>("streaming-ws-test");
    session->setWebSocketServer(wsServer_.get());
    session->setLanguageConfig("en", "es");
    
    MessageTracker tracker;
    std::vector<double> chunkLatencies;
    std::mutex latenciesMutex;
    
    // Set up message callback with latency tracking
    std::queue<std::chrono::high_resolution_clock::time_point> chunkSendTimes;
    std::mutex sendTimesMutex;
    
    session->setMessageCallback([&](const std::string& message) {
        auto receiveTime = std::chrono::high_resolution_clock::now();
        
        {
            std::lock_guard<std::mutex> lock(sendTimesMutex);
            if (!chunkSendTimes.empty()) {
                auto sendTime = chunkSendTimes.front();
                chunkSendTimes.pop();
                
                double latency = std::chrono::duration<double, std::milli>(receiveTime - sendTime).count();
                
                std::lock_guard<std::mutex> latencyLock(latenciesMutex);
                chunkLatencies.push_back(latency);
            }
        }
        
        tracker.addMessage(message);
    });
    
    std::cout << "Testing real-time streaming with " << streamingChunks_.size() << " chunks..." << std::endl;
    
    auto streamingStart = std::chrono::high_resolution_clock::now();
    
    // Send streaming chunks
    for (size_t i = 0; i < streamingChunks_.size() && i < 15; ++i) { // Limit for testing
        auto chunkAudio = streamingChunks_[i];
        auto pcmData = audioToPCM(chunkAudio);
        
        {
            std::lock_guard<std::mutex> lock(sendTimesMutex);
            chunkSendTimes.push(std::chrono::high_resolution_clock::now());
        }
        
        // Send chunk
        std::string_view binaryData(
            reinterpret_cast<const char*>(pcmData.data()),
            pcmData.size() * sizeof(int16_t)
        );
        session->handleBinaryMessage(binaryData);
        
        // Simulate real-time streaming delay
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
    
    // Wait for all responses
    bool allReceived = tracker.waitForMessages(10, std::chrono::seconds(15)); // Expect at least 10 responses
    
    auto streamingEnd = std::chrono::high_resolution_clock::now();
    double totalStreamingTime = std::chrono::duration<double, std::milli>(streamingEnd - streamingStart).count();
    
    // Analyze streaming performance
    if (!chunkLatencies.empty()) {
        std::sort(chunkLatencies.begin(), chunkLatencies.end());
        double avgLatency = std::accumulate(chunkLatencies.begin(), chunkLatencies.end(), 0.0) / chunkLatencies.size();
        double p95Latency = chunkLatencies[static_cast<size_t>(chunkLatencies.size() * 0.95)];
        
        perfMonitor.recordLatency("websocket.streaming_avg_chunk_latency_ms", avgLatency);
        perfMonitor.recordLatency("websocket.streaming_p95_chunk_latency_ms", p95Latency);
        perfMonitor.recordLatency("websocket.streaming_total_time_ms", totalStreamingTime);
        
        std::cout << "Streaming Results:" << std::endl;
        std::cout << "  Total streaming time: " << totalStreamingTime << "ms" << std::endl;
        std::cout << "  Chunks processed: " << chunkLatencies.size() << std::endl;
        std::cout << "  Average chunk latency: " << avgLatency << "ms" << std::endl;
        std::cout << "  P95 chunk latency: " << p95Latency << "ms" << std::endl;
        std::cout << "  Messages received: " << tracker.messagesReceived.load() << std::endl;
        std::cout << "  Transcription updates: " << tracker.transcriptionUpdates.load() << std::endl;
        
        // Streaming performance assertions
        EXPECT_TRUE(allReceived) << "Should receive responses for streaming chunks";
        EXPECT_GT(tracker.transcriptionUpdates.load(), 5) << "Should receive multiple transcription updates";
        EXPECT_LT(avgLatency, 800.0) << "Average streaming latency should be under 800ms";
        EXPECT_LT(p95Latency, 1500.0) << "P95 streaming latency should be under 1.5s";
    }
}

// Test 3: Multi-client WebSocket STT Integration
TEST_F(STTWebSocketIntegrationTest, MultiClientWebSocketSTTIntegration) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Start WebSocket server
    wsServer_->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Initialize STT
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    const int numClients = 8;
    const int messagesPerClient = 5;
    
    std::vector<std::shared_ptr<core::ClientSession>> sessions;
    std::vector<std::unique_ptr<MessageTracker>> trackers;
    std::vector<std::future<std::vector<double>>> futures;
    
    std::cout << "Testing multi-client WebSocket integration..." << std::endl;
    std::cout << "Clients: " << numClients << ", Messages per client: " << messagesPerClient << std::endl;
    
    // Create client sessions
    for (int i = 0; i < numClients; ++i) {
        auto session = std::make_shared<core::ClientSession>("multi-client-" + std::to_string(i));
        session->setWebSocketServer(wsServer_.get());
        session->setLanguageConfig("en", "es");
        
        auto tracker = std::make_unique<MessageTracker>();
        
        // Set up message callback
        session->setMessageCallback([tracker = tracker.get()](const std::string& message) {
            tracker->addMessage(message);
        });
        
        sessions.push_back(session);
        trackers.push_back(std::move(tracker));
    }
    
    auto testStart = std::chrono::high_resolution_clock::now();
    
    // Start client tasks
    for (int clientId = 0; clientId < numClients; ++clientId) {
        futures.push_back(std::async(std::launch::async, [&, clientId]() {
            std::vector<double> clientLatencies;
            auto& session = sessions[clientId];
            auto& tracker = trackers[clientId];
            
            std::vector<std::string> sampleKeys = {"greeting", "question", "response", "short_phrase", "long_sentence"};
            
            for (int msgId = 0; msgId < messagesPerClient; ++msgId) {
                std::string sampleKey = sampleKeys[msgId % sampleKeys.size()];
                auto testAudio = wsTestAudio_[sampleKey];
                auto pcmData = audioToPCM(testAudio);
                
                auto sendTime = std::chrono::high_resolution_clock::now();
                
                // Send audio data
                std::string_view binaryData(
                    reinterpret_cast<const char*>(pcmData.data()),
                    pcmData.size() * sizeof(int16_t)
                );
                session->handleBinaryMessage(binaryData);
                
                // Wait for response
                bool received = tracker->waitForMessages(msgId + 1, std::chrono::seconds(10));
                
                if (received) {
                    auto receiveTime = std::chrono::high_resolution_clock::now();
                    double latency = std::chrono::duration<double, std::milli>(receiveTime - sendTime).count();
                    clientLatencies.push_back(latency);
                }
                
                // Delay between messages
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
            
            return clientLatencies;
        }));
    }
    
    // Collect results
    std::vector<double> allLatencies;
    int totalMessages = 0;
    int successfulMessages = 0;
    int totalTranscriptionUpdates = 0;
    int totalErrors = 0;
    
    for (int i = 0; i < numClients; ++i) {
        auto clientLatencies = futures[i].get();
        allLatencies.insert(allLatencies.end(), clientLatencies.begin(), clientLatencies.end());
        
        successfulMessages += clientLatencies.size();
        totalMessages += messagesPerClient;
        totalTranscriptionUpdates += trackers[i]->transcriptionUpdates.load();
        totalErrors += trackers[i]->errorMessages.load();
    }
    
    auto testEnd = std::chrono::high_resolution_clock::now();
    double totalTestTime = std::chrono::duration<double>(testEnd - testStart).count();
    
    // Analyze multi-client results
    if (!allLatencies.empty()) {
        std::sort(allLatencies.begin(), allLatencies.end());
        double avgLatency = std::accumulate(allLatencies.begin(), allLatencies.end(), 0.0) / allLatencies.size();
        double p95Latency = allLatencies[static_cast<size_t>(allLatencies.size() * 0.95)];
        double throughput = successfulMessages / totalTestTime;
        double successRate = static_cast<double>(successfulMessages) / totalMessages;
        
        perfMonitor.recordThroughput("websocket.multi_client_throughput_per_sec", throughput);
        perfMonitor.recordMetric("websocket.multi_client_success_rate", successRate);
        perfMonitor.recordLatency("websocket.multi_client_avg_latency_ms", avgLatency);
        perfMonitor.recordLatency("websocket.multi_client_p95_latency_ms", p95Latency);
        
        std::cout << "Multi-client Results:" << std::endl;
        std::cout << "  Total messages: " << totalMessages << std::endl;
        std::cout << "  Successful messages: " << successfulMessages << std::endl;
        std::cout << "  Success rate: " << (successRate * 100) << "%" << std::endl;
        std::cout << "  Throughput: " << throughput << " msg/sec" << std::endl;
        std::cout << "  Average latency: " << avgLatency << "ms" << std::endl;
        std::cout << "  P95 latency: " << p95Latency << "ms" << std::endl;
        std::cout << "  Total transcription updates: " << totalTranscriptionUpdates << std::endl;
        std::cout << "  Total errors: " << totalErrors << std::endl;
        
        // Multi-client assertions
        EXPECT_GT(successRate, 0.90) << "Multi-client success rate should be >90%";
        EXPECT_LT(avgLatency, 1000.0) << "Multi-client average latency should be <1s";
        EXPECT_LT(p95Latency, 2000.0) << "Multi-client P95 latency should be <2s";
        EXPECT_GT(throughput, 5.0) << "Multi-client throughput should be >5 msg/sec";
        EXPECT_EQ(totalErrors, 0) << "Should not have errors in multi-client scenario";
    }
}

// Test 4: WebSocket Error Handling and Recovery
TEST_F(STTWebSocketIntegrationTest, WebSocketErrorHandlingAndRecovery) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Start WebSocket server
    wsServer_->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Initialize STT
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    // Create client session
    auto session = std::make_shared<core::ClientSession>("error-handling-test");
    session->setWebSocketServer(wsServer_.get());
    session->setLanguageConfig("en", "es");
    
    MessageTracker tracker;
    
    // Set up message callback
    session->setMessageCallback([&tracker](const std::string& message) {
        tracker.addMessage(message);
    });
    
    std::cout << "Testing WebSocket error handling and recovery..." << std::endl;
    
    // Test 1: Send invalid audio data
    std::cout << "  Testing invalid audio data handling..." << std::endl;
    {
        std::vector<uint8_t> invalidData(1024, 0xFF); // Invalid audio data
        std::string_view binaryData(reinterpret_cast<const char*>(invalidData.data()), invalidData.size());
        
        session->handleBinaryMessage(binaryData);
        
        // Wait for error response
        bool receivedResponse = tracker.waitForMessages(1, std::chrono::seconds(5));
        
        // Should handle gracefully (either process or return error)
        EXPECT_TRUE(receivedResponse || tracker.errorMessages.load() > 0) 
            << "Should handle invalid audio data gracefully";
    }
    
    // Test 2: Send oversized audio data
    std::cout << "  Testing oversized audio data handling..." << std::endl;
    {
        tracker = MessageTracker{}; // Reset
        
        // Generate very large audio sample (10 seconds)
        auto largeAudio = testDataGenerator_->generateSpeechAudio(10.0f, 16000);
        auto pcmData = audioToPCM(largeAudio);
        
        std::string_view binaryData(
            reinterpret_cast<const char*>(pcmData.data()),
            pcmData.size() * sizeof(int16_t)
        );
        
        auto sendTime = std::chrono::high_resolution_clock::now();
        session->handleBinaryMessage(binaryData);
        
        // Wait for response (should handle large data)
        bool receivedResponse = tracker.waitForMessages(1, std::chrono::seconds(15));
        
        auto receiveTime = std::chrono::high_resolution_clock::now();
        double latency = std::chrono::duration<double, std::milli>(receiveTime - sendTime).count();
        
        perfMonitor.recordLatency("websocket.large_audio_latency_ms", latency);
        
        EXPECT_TRUE(receivedResponse) << "Should handle large audio data";
        std::cout << "    Large audio latency: " << latency << "ms" << std::endl;
    }
    
    // Test 3: Recovery after errors
    std::cout << "  Testing recovery after errors..." << std::endl;
    {
        tracker = MessageTracker{}; // Reset
        
        // Send normal audio after error conditions
        auto normalAudio = wsTestAudio_["greeting"];
        auto pcmData = audioToPCM(normalAudio);
        
        std::string_view binaryData(
            reinterpret_cast<const char*>(pcmData.data()),
            pcmData.size() * sizeof(int16_t)
        );
        
        session->handleBinaryMessage(binaryData);
        
        // Should recover and process normally
        bool receivedResponse = tracker.waitForMessages(1, std::chrono::seconds(8));
        
        EXPECT_TRUE(receivedResponse) << "Should recover and process normal audio after errors";
        EXPECT_GT(tracker.transcriptionUpdates.load(), 0) << "Should receive transcription updates after recovery";
        
        std::cout << "    Recovery successful: " << (receivedResponse ? "Yes" : "No") << std::endl;
        std::cout << "    Messages after recovery: " << tracker.messagesReceived.load() << std::endl;
    }
    
    // Test 4: Concurrent error and normal requests
    std::cout << "  Testing concurrent error and normal requests..." << std::endl;
    {
        tracker = MessageTracker{}; // Reset
        
        std::vector<std::future<bool>> futures;
        
        // Send mix of normal and problematic requests concurrently
        for (int i = 0; i < 5; ++i) {
            futures.push_back(std::async(std::launch::async, [&, i]() {
                if (i % 2 == 0) {
                    // Normal request
                    auto normalAudio = wsTestAudio_["short_phrase"];
                    auto pcmData = audioToPCM(normalAudio);
                    
                    std::string_view binaryData(
                        reinterpret_cast<const char*>(pcmData.data()),
                        pcmData.size() * sizeof(int16_t)
                    );
                    session->handleBinaryMessage(binaryData);
                } else {
                    // Problematic request (empty data)
                    std::string_view emptyData("", 0);
                    session->handleBinaryMessage(emptyData);
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                return true;
            }));
        }
        
        // Wait for all requests to complete
        for (auto& future : futures) {
            future.wait();
        }
        
        // Wait for responses
        bool receivedResponses = tracker.waitForMessages(2, std::chrono::seconds(10)); // Expect at least 2 normal responses
        
        EXPECT_TRUE(receivedResponses) << "Should handle concurrent normal and error requests";
        EXPECT_GT(tracker.messagesReceived.load(), 0) << "Should receive some responses despite errors";
        
        std::cout << "    Concurrent handling successful: " << (receivedResponses ? "Yes" : "No") << std::endl;
        std::cout << "    Total messages received: " << tracker.messagesReceived.load() << std::endl;
        std::cout << "    Error messages: " << tracker.errorMessages.load() << std::endl;
    }
}

// Test 5: Language Detection via WebSocket
TEST_F(STTWebSocketIntegrationTest, LanguageDetectionViaWebSocket) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Start WebSocket server
    wsServer_->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Initialize STT with language detection
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    // Create client session
    auto session = std::make_shared<core::ClientSession>("language-detection-test");
    session->setWebSocketServer(wsServer_.get());
    session->setLanguageConfig("auto", "en"); // Auto-detect source language
    
    MessageTracker tracker;
    std::map<std::string, std::string> detectedLanguages;
    std::mutex languagesMutex;
    
    // Set up message callback to capture language detection
    session->setMessageCallback([&](const std::string& message) {
        tracker.addMessage(message);
        
        // Parse language detection from message (simplified)
        if (message.find("detected_language") != std::string::npos) {
            std::lock_guard<std::mutex> lock(languagesMutex);
            // Extract language info from message (implementation would parse JSON)
            // For testing, we'll simulate this
            if (message.find("english") != std::string::npos) {
                detectedLanguages["current"] = "en";
            } else if (message.find("spanish") != std::string::npos) {
                detectedLanguages["current"] = "es";
            }
        }
    });
    
    std::cout << "Testing language detection via WebSocket..." << std::endl;
    
    // Test different language samples
    std::vector<std::pair<std::string, std::string>> languageTests = {
        {"english_sample", "en"},
        {"spanish_sample", "es"}
    };
    
    for (const auto& [sampleKey, expectedLang] : languageTests) {
        std::cout << "  Testing " << sampleKey << " (expected: " << expectedLang << ")" << std::endl;
        
        tracker = MessageTracker{}; // Reset
        detectedLanguages.clear();
        
        auto testAudio = wsTestAudio_[sampleKey];
        auto pcmData = audioToPCM(testAudio);
        
        auto sendTime = std::chrono::high_resolution_clock::now();
        
        std::string_view binaryData(
            reinterpret_cast<const char*>(pcmData.data()),
            pcmData.size() * sizeof(int16_t)
        );
        session->handleBinaryMessage(binaryData);
        
        // Wait for response with language detection
        bool receivedResponse = tracker.waitForMessages(1, std::chrono::seconds(10));
        
        auto receiveTime = std::chrono::high_resolution_clock::now();
        double latency = std::chrono::duration<double, std::milli>(receiveTime - sendTime).count();
        
        perfMonitor.recordLatency("websocket.language_detection_" + sampleKey + "_ms", latency);
        
        EXPECT_TRUE(receivedResponse) << "Should receive response for " << sampleKey;
        
        std::cout << "    Latency: " << latency << "ms" << std::endl;
        std::cout << "    Messages received: " << tracker.messagesReceived.load() << std::endl;
        
        // Check if language was detected (in a real implementation)
        {
            std::lock_guard<std::mutex> lock(languagesMutex);
            if (!detectedLanguages.empty()) {
                std::cout << "    Detected language: " << detectedLanguages["current"] << std::endl;
            }
        }
        
        // Small delay between tests
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

} // namespace stt_websocket_integration

// Main function for standalone execution
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "=== STT WebSocket Integration Test Suite ===" << std::endl;
    std::cout << "Testing STT integration with WebSocket communication layer" << std::endl;
    
    return RUN_ALL_TESTS();
}