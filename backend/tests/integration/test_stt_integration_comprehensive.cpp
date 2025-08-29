#include <gtest/gtest.h>
#include "stt/whisper_stt.hpp"
#include "stt/transcription_manager.hpp"
#include "audio/voice_activity_detector.hpp"
#include "audio/audio_buffer_manager.hpp"
#include "core/websocket_server.hpp"
#include "core/client_session.hpp"
#include "utils/performance_monitor.hpp"
#include "fixtures/test_data_generator.hpp"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <memory>
#include <future>
#include <random>
#include <fstream>

namespace stt_integration {

class STTIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize performance monitor
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        perfMonitor.initialize(false);
        
        // Initialize test data generator
        testDataGenerator_ = std::make_unique<fixtures::TestDataGenerator>();
        
        // Initialize WebSocket server for integration tests
        wsServer_ = std::make_unique<core::WebSocketServer>(8085);
        
        // Initialize STT components
        whisperSTT_ = std::make_unique<stt::WhisperSTT>();
        transcriptionManager_ = std::make_unique<stt::TranscriptionManager>();
        vadDetector_ = std::make_unique<audio::VoiceActivityDetector>();
        bufferManager_ = std::make_unique<audio::AudioBufferManager>();
        
        // Generate test audio data
        generateTestData();
    }
    
    void TearDown() override {
        if (wsServer_) {
            wsServer_->stop();
        }
        
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        perfMonitor.cleanup();
    }
    
    void generateTestData() {
        // Generate various test audio samples
        testAudioSamples_.clear();
        
        // Short utterance (0.5 seconds)
        testAudioSamples_["short"] = testDataGenerator_->generateSpeechAudio(0.5f, 16000);
        
        // Medium utterance (2.0 seconds)
        testAudioSamples_["medium"] = testDataGenerator_->generateSpeechAudio(2.0f, 16000);
        
        // Long utterance (5.0 seconds)
        testAudioSamples_["long"] = testDataGenerator_->generateSpeechAudio(5.0f, 16000);
        
        // Noisy audio
        testAudioSamples_["noisy"] = testDataGenerator_->generateNoisyAudio(2.0f, 16000, 0.3f);
        
        // Multilingual samples
        testAudioSamples_["english"] = testDataGenerator_->generateLanguageSpecificAudio("en", 2.0f);
        testAudioSamples_["spanish"] = testDataGenerator_->generateLanguageSpecificAudio("es", 2.0f);
        testAudioSamples_["french"] = testDataGenerator_->generateLanguageSpecificAudio("fr", 2.0f);
        
        // Streaming chunks (for real-time testing)
        auto longAudio = testDataGenerator_->generateSpeechAudio(10.0f, 16000);
        streamingChunks_ = testDataGenerator_->splitIntoChunks(longAudio, 0.5f); // 500ms chunks
    }
    
    std::unique_ptr<fixtures::TestDataGenerator> testDataGenerator_;
    std::unique_ptr<core::WebSocketServer> wsServer_;
    std::unique_ptr<stt::WhisperSTT> whisperSTT_;
    std::unique_ptr<stt::TranscriptionManager> transcriptionManager_;
    std::unique_ptr<audio::VoiceActivityDetector> vadDetector_;
    std::unique_ptr<audio::AudioBufferManager> bufferManager_;
    
    std::map<std::string, std::vector<float>> testAudioSamples_;
    std::vector<std::vector<float>> streamingChunks_;
};

// Test 1: End-to-End STT Pipeline
TEST_F(STTIntegrationTest, EndToEndSTTPipeline) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Initialize STT pipeline components
    ASSERT_TRUE(whisperSTT_->initialize("test_models/whisper-base.bin"));
    ASSERT_TRUE(vadDetector_->initialize("test_models/silero_vad.onnx"));
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    
    transcriptionManager_->start();
    
    // Test different audio samples
    std::vector<std::string> testCases = {"short", "medium", "long", "noisy"};
    
    for (const std::string& testCase : testCases) {
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // Step 1: Voice Activity Detection
        auto vadStartTime = std::chrono::high_resolution_clock::now();
        float vadProbability = vadDetector_->getVoiceActivityProbability(testAudioSamples_[testCase]);
        auto vadEndTime = std::chrono::high_resolution_clock::now();
        
        double vadLatency = std::chrono::duration<double, std::milli>(vadEndTime - vadStartTime).count();
        perfMonitor.recordLatency("integration.vad_latency_" + testCase + "_ms", vadLatency);
        
        EXPECT_GT(vadProbability, 0.5f) << "VAD should detect speech in " << testCase << " sample";
        
        // Step 2: Audio Buffer Management
        uint32_t utteranceId = 1000 + std::hash<std::string>{}(testCase);
        bufferManager_->addAudioData(utteranceId, testAudioSamples_[testCase]);
        
        auto bufferedAudio = bufferManager_->getBufferedAudio(utteranceId);
        EXPECT_EQ(bufferedAudio.size(), testAudioSamples_[testCase].size());
        
        // Step 3: STT Transcription
        std::atomic<bool> transcriptionComplete{false};
        std::string transcriptionResult;
        float confidence = 0.0f;
        
        stt::TranscriptionRequest request;
        request.utterance_id = utteranceId;
        request.audio_data = bufferedAudio;
        request.is_live = false;
        request.callback = [&](uint32_t id, const stt::TranscriptionResult& result) {
            transcriptionResult = result.text;
            confidence = result.confidence;
            transcriptionComplete = true;
        };
        
        auto sttStartTime = std::chrono::high_resolution_clock::now();
        transcriptionManager_->submitTranscription(request);
        
        // Wait for transcription to complete
        auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(10);
        while (!transcriptionComplete.load() && std::chrono::high_resolution_clock::now() < timeout) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        auto sttEndTime = std::chrono::high_resolution_clock::now();
        double sttLatency = std::chrono::duration<double, std::milli>(sttEndTime - sttStartTime).count();
        perfMonitor.recordLatency("integration.stt_latency_" + testCase + "_ms", sttLatency);
        
        ASSERT_TRUE(transcriptionComplete.load()) << "Transcription should complete for " << testCase;
        EXPECT_FALSE(transcriptionResult.empty()) << "Should get transcription result for " << testCase;
        EXPECT_GT(confidence, 0.0f) << "Should get confidence score for " << testCase;
        
        // Step 4: End-to-end latency measurement
        auto endTime = std::chrono::high_resolution_clock::now();
        double totalLatency = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        perfMonitor.recordLatency("integration.end_to_end_latency_" + testCase + "_ms", totalLatency);
        
        std::cout << "Test case: " << testCase << std::endl;
        std::cout << "  VAD latency: " << vadLatency << "ms" << std::endl;
        std::cout << "  STT latency: " << sttLatency << "ms" << std::endl;
        std::cout << "  Total latency: " << totalLatency << "ms" << std::endl;
        std::cout << "  Transcription: " << transcriptionResult << std::endl;
        std::cout << "  Confidence: " << confidence << std::endl;
        
        // Clean up
        bufferManager_->finalizeBuffer(utteranceId);
    }
    
    transcriptionManager_->stop();
}

// Test 2: Performance Benchmarking for Latency Requirements
TEST_F(STTIntegrationTest, PerformanceBenchmarkingLatencyRequirements) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Initialize components
    ASSERT_TRUE(whisperSTT_->initialize("test_models/whisper-base.bin"));
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    const int numIterations = 50;
    std::vector<double> vadLatencies, sttLatencies, endToEndLatencies;
    
    for (int i = 0; i < numIterations; ++i) {
        auto testAudio = testAudioSamples_["medium"];
        uint32_t utteranceId = 2000 + i;
        
        auto overallStart = std::chrono::high_resolution_clock::now();
        
        // VAD benchmark
        auto vadStart = std::chrono::high_resolution_clock::now();
        float vadProb = vadDetector_->getVoiceActivityProbability(testAudio);
        auto vadEnd = std::chrono::high_resolution_clock::now();
        double vadLatency = std::chrono::duration<double, std::milli>(vadEnd - vadStart).count();
        vadLatencies.push_back(vadLatency);
        
        // STT benchmark
        std::atomic<bool> sttComplete{false};
        
        stt::TranscriptionRequest request;
        request.utterance_id = utteranceId;
        request.audio_data = testAudio;
        request.is_live = false;
        request.callback = [&](uint32_t id, const stt::TranscriptionResult& result) {
            sttComplete = true;
        };
        
        auto sttStart = std::chrono::high_resolution_clock::now();
        transcriptionManager_->submitTranscription(request);
        
        // Wait for completion
        while (!sttComplete.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        auto sttEnd = std::chrono::high_resolution_clock::now();
        double sttLatency = std::chrono::duration<double, std::milli>(sttEnd - sttStart).count();
        sttLatencies.push_back(sttLatency);
        
        auto overallEnd = std::chrono::high_resolution_clock::now();
        double totalLatency = std::chrono::duration<double, std::milli>(overallEnd - overallStart).count();
        endToEndLatencies.push_back(totalLatency);
    }
    
    // Calculate statistics
    auto calculateStats = [](const std::vector<double>& values) {
        std::vector<double> sorted = values;
        std::sort(sorted.begin(), sorted.end());
        
        double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();
        double median = sorted[sorted.size() / 2];
        double p95 = sorted[static_cast<size_t>(sorted.size() * 0.95)];
        double p99 = sorted[static_cast<size_t>(sorted.size() * 0.99)];
        
        return std::make_tuple(mean, median, p95, p99, sorted.front(), sorted.back());
    };
    
    auto [vadMean, vadMedian, vadP95, vadP99, vadMin, vadMax] = calculateStats(vadLatencies);
    auto [sttMean, sttMedian, sttP95, sttP99, sttMin, sttMax] = calculateStats(sttLatencies);
    auto [e2eMean, e2eMedian, e2eP95, e2eP99, e2eMin, e2eMax] = calculateStats(endToEndLatencies);
    
    // Record metrics
    perfMonitor.recordLatency("benchmark.vad_mean_latency_ms", vadMean);
    perfMonitor.recordLatency("benchmark.vad_p95_latency_ms", vadP95);
    perfMonitor.recordLatency("benchmark.stt_mean_latency_ms", sttMean);
    perfMonitor.recordLatency("benchmark.stt_p95_latency_ms", sttP95);
    perfMonitor.recordLatency("benchmark.end_to_end_mean_latency_ms", e2eMean);
    perfMonitor.recordLatency("benchmark.end_to_end_p95_latency_ms", e2eP95);
    
    std::cout << "Performance Benchmark Results (" << numIterations << " iterations):" << std::endl;
    std::cout << "VAD Latency - Mean: " << vadMean << "ms, P95: " << vadP95 << "ms, Range: [" << vadMin << ", " << vadMax << "]ms" << std::endl;
    std::cout << "STT Latency - Mean: " << sttMean << "ms, P95: " << sttP95 << "ms, Range: [" << sttMin << ", " << sttMax << "]ms" << std::endl;
    std::cout << "End-to-End - Mean: " << e2eMean << "ms, P95: " << e2eP95 << "ms, Range: [" << e2eMin << ", " << e2eMax << "]ms" << std::endl;
    
    // Performance requirements validation
    EXPECT_LT(vadP95, 100.0) << "VAD P95 latency should be under 100ms";
    EXPECT_LT(sttP95, 500.0) << "STT P95 latency should be under 500ms";
    EXPECT_LT(e2eP95, 600.0) << "End-to-end P95 latency should be under 600ms";
    
    transcriptionManager_->stop();
}

// Test 3: Load Testing for Concurrent Transcription Scenarios
TEST_F(STTIntegrationTest, LoadTestingConcurrentTranscriptions) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Initialize components
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    const int numConcurrentClients = 10;
    const int transcriptionsPerClient = 20;
    
    std::vector<std::future<std::vector<double>>> futures;
    std::atomic<int> totalTranscriptions{0};
    std::atomic<int> successfulTranscriptions{0};
    
    auto overallStart = std::chrono::high_resolution_clock::now();
    
    // Start concurrent transcription tasks
    for (int clientId = 0; clientId < numConcurrentClients; ++clientId) {
        futures.push_back(std::async(std::launch::async, [&, clientId]() {
            std::vector<double> clientLatencies;
            
            for (int i = 0; i < transcriptionsPerClient; ++i) {
                uint32_t utteranceId = clientId * 1000 + i;
                
                // Use different audio samples for variety
                std::string sampleKey = (i % 2 == 0) ? "medium" : "short";
                auto testAudio = testAudioSamples_[sampleKey];
                
                std::atomic<bool> transcriptionComplete{false};
                std::string result;
                
                stt::TranscriptionRequest request;
                request.utterance_id = utteranceId;
                request.audio_data = testAudio;
                request.is_live = false;
                request.callback = [&](uint32_t id, const stt::TranscriptionResult& transcriptionResult) {
                    result = transcriptionResult.text;
                    transcriptionComplete = true;
                    successfulTranscriptions++;
                };
                
                auto requestStart = std::chrono::high_resolution_clock::now();
                transcriptionManager_->submitTranscription(request);
                totalTranscriptions++;
                
                // Wait for completion with timeout
                auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(15);
                while (!transcriptionComplete.load() && std::chrono::high_resolution_clock::now() < timeout) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                
                if (transcriptionComplete.load()) {
                    auto requestEnd = std::chrono::high_resolution_clock::now();
                    double latency = std::chrono::duration<double, std::milli>(requestEnd - requestStart).count();
                    clientLatencies.push_back(latency);
                }
                
                // Small delay between requests to simulate realistic usage
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            return clientLatencies;
        }));
    }
    
    // Collect results
    std::vector<double> allLatencies;
    for (auto& future : futures) {
        auto clientLatencies = future.get();
        allLatencies.insert(allLatencies.end(), clientLatencies.begin(), clientLatencies.end());
    }
    
    auto overallEnd = std::chrono::high_resolution_clock::now();
    double totalDuration = std::chrono::duration<double>(overallEnd - overallStart).count();
    
    // Calculate metrics
    double throughput = successfulTranscriptions.load() / totalDuration;
    double successRate = static_cast<double>(successfulTranscriptions.load()) / totalTranscriptions.load();
    
    if (!allLatencies.empty()) {
        std::sort(allLatencies.begin(), allLatencies.end());
        double avgLatency = std::accumulate(allLatencies.begin(), allLatencies.end(), 0.0) / allLatencies.size();
        double p95Latency = allLatencies[static_cast<size_t>(allLatencies.size() * 0.95)];
        
        perfMonitor.recordThroughput("load_test.transcription_throughput_per_sec", throughput);
        perfMonitor.recordLatency("load_test.avg_latency_ms", avgLatency);
        perfMonitor.recordLatency("load_test.p95_latency_ms", p95Latency);
        
        std::cout << "Load Test Results:" << std::endl;
        std::cout << "  Concurrent clients: " << numConcurrentClients << std::endl;
        std::cout << "  Total transcriptions: " << totalTranscriptions.load() << std::endl;
        std::cout << "  Successful transcriptions: " << successfulTranscriptions.load() << std::endl;
        std::cout << "  Success rate: " << (successRate * 100) << "%" << std::endl;
        std::cout << "  Throughput: " << throughput << " transcriptions/sec" << std::endl;
        std::cout << "  Average latency: " << avgLatency << "ms" << std::endl;
        std::cout << "  P95 latency: " << p95Latency << "ms" << std::endl;
        
        // Load test assertions
        EXPECT_GT(successRate, 0.95) << "Success rate should be above 95%";
        EXPECT_GT(throughput, 5.0) << "Throughput should be at least 5 transcriptions/sec";
        EXPECT_LT(p95Latency, 2000.0) << "P95 latency should be under 2 seconds under load";
    }
    
    transcriptionManager_->stop();
}

// Test 4: Integration Tests with WebSocket Communication Layer
TEST_F(STTIntegrationTest, WebSocketCommunicationIntegration) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Start WebSocket server
    wsServer_->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Allow server to start
    
    const int numClients = 5;
    const int messagesPerClient = 10;
    
    std::vector<std::shared_ptr<core::ClientSession>> sessions;
    std::vector<std::future<std::vector<double>>> futures;
    
    // Create client sessions
    for (int i = 0; i < numClients; ++i) {
        auto session = std::make_shared<core::ClientSession>("websocket-test-" + std::to_string(i));
        session->setWebSocketServer(wsServer_.get());
        session->setLanguageConfig("en", "es");
        sessions.push_back(session);
    }
    
    // Start WebSocket communication tests
    for (int clientId = 0; clientId < numClients; ++clientId) {
        futures.push_back(std::async(std::launch::async, [&, clientId]() {
            std::vector<double> messageLatencies;
            auto& session = sessions[clientId];
            
            std::atomic<int> messagesReceived{0};
            std::chrono::high_resolution_clock::time_point lastMessageTime;
            
            // Set up message callback
            session->setMessageCallback([&](const std::string& message) {
                auto receiveTime = std::chrono::high_resolution_clock::now();
                double latency = std::chrono::duration<double, std::milli>(receiveTime - lastMessageTime).count();
                messageLatencies.push_back(latency);
                messagesReceived++;
            });
            
            // Send audio messages and measure WebSocket latency
            for (int msgId = 0; msgId < messagesPerClient; ++msgId) {
                // Convert test audio to PCM format for WebSocket transmission
                auto testAudio = testAudioSamples_["short"];
                std::vector<int16_t> pcmData;
                pcmData.reserve(testAudio.size());
                
                for (float sample : testAudio) {
                    pcmData.push_back(static_cast<int16_t>(sample * 32767.0f));
                }
                
                lastMessageTime = std::chrono::high_resolution_clock::now();
                
                // Send binary audio data
                std::string_view binaryData(
                    reinterpret_cast<const char*>(pcmData.data()),
                    pcmData.size() * sizeof(int16_t)
                );
                session->handleBinaryMessage(binaryData);
                
                // Wait for response
                auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(5);
                int initialCount = messagesReceived.load();
                while (messagesReceived.load() == initialCount && 
                       std::chrono::high_resolution_clock::now() < timeout) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                
                // Delay between messages
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            
            return messageLatencies;
        }));
    }
    
    // Collect WebSocket communication results
    std::vector<double> allWebSocketLatencies;
    int totalMessages = 0;
    
    for (auto& future : futures) {
        auto clientLatencies = future.get();
        allWebSocketLatencies.insert(allWebSocketLatencies.end(), clientLatencies.begin(), clientLatencies.end());
        totalMessages += clientLatencies.size();
    }
    
    if (!allWebSocketLatencies.empty()) {
        std::sort(allWebSocketLatencies.begin(), allWebSocketLatencies.end());
        double avgWSLatency = std::accumulate(allWebSocketLatencies.begin(), allWebSocketLatencies.end(), 0.0) / allWebSocketLatencies.size();
        double p95WSLatency = allWebSocketLatencies[static_cast<size_t>(allWebSocketLatencies.size() * 0.95)];
        
        perfMonitor.recordLatency("websocket.avg_message_latency_ms", avgWSLatency);
        perfMonitor.recordLatency("websocket.p95_message_latency_ms", p95WSLatency);
        
        std::cout << "WebSocket Integration Results:" << std::endl;
        std::cout << "  Total messages processed: " << totalMessages << std::endl;
        std::cout << "  Average WebSocket latency: " << avgWSLatency << "ms" << std::endl;
        std::cout << "  P95 WebSocket latency: " << p95WSLatency << "ms" << std::endl;
        
        // WebSocket performance assertions
        EXPECT_LT(avgWSLatency, 200.0) << "Average WebSocket latency should be under 200ms";
        EXPECT_LT(p95WSLatency, 500.0) << "P95 WebSocket latency should be under 500ms";
        EXPECT_GT(totalMessages, numClients * messagesPerClient * 0.8) << "Should process at least 80% of messages";
    }
}

// Test 5: Streaming Transcription Integration
TEST_F(STTIntegrationTest, StreamingTranscriptionIntegration) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Initialize streaming components
    ASSERT_TRUE(whisperSTT_->initialize("test_models/whisper-base.bin"));
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    uint32_t utteranceId = 5000;
    std::vector<std::string> partialResults;
    std::string finalResult;
    std::atomic<bool> streamingComplete{false};
    
    // Set up streaming callback
    auto streamingCallback = [&](uint32_t id, const stt::TranscriptionResult& result) {
        if (result.is_partial) {
            partialResults.push_back(result.text);
        } else {
            finalResult = result.text;
            streamingComplete = true;
        }
    };
    
    // Start streaming transcription
    whisperSTT_->startStreamingTranscription(utteranceId);
    
    auto streamingStart = std::chrono::high_resolution_clock::now();
    
    // Send audio chunks incrementally
    for (size_t i = 0; i < streamingChunks_.size(); ++i) {
        whisperSTT_->addAudioChunk(utteranceId, streamingChunks_[i]);
        
        // Small delay to simulate real-time streaming
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Check for partial results periodically
        if (i % 3 == 0) {
            stt::TranscriptionRequest request;
            request.utterance_id = utteranceId;
            request.audio_data = streamingChunks_[i];
            request.is_live = true;
            request.callback = streamingCallback;
            
            transcriptionManager_->submitTranscription(request);
        }
    }
    
    // Finalize streaming
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    
    // Wait for final result
    auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(10);
    while (!streamingComplete.load() && std::chrono::high_resolution_clock::now() < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    auto streamingEnd = std::chrono::high_resolution_clock::now();
    double streamingLatency = std::chrono::duration<double, std::milli>(streamingEnd - streamingStart).count();
    
    perfMonitor.recordLatency("streaming.total_latency_ms", streamingLatency);
    perfMonitor.recordMetric("streaming.partial_results_count", static_cast<double>(partialResults.size()));
    
    std::cout << "Streaming Transcription Results:" << std::endl;
    std::cout << "  Total streaming latency: " << streamingLatency << "ms" << std::endl;
    std::cout << "  Partial results received: " << partialResults.size() << std::endl;
    std::cout << "  Final result: " << finalResult << std::endl;
    
    // Streaming assertions
    EXPECT_TRUE(streamingComplete.load()) << "Streaming transcription should complete";
    EXPECT_GT(partialResults.size(), 0) << "Should receive partial results during streaming";
    EXPECT_FALSE(finalResult.empty()) << "Should receive final transcription result";
    
    transcriptionManager_->stop();
}

// Test 6: Language Detection and Multi-language Support
TEST_F(STTIntegrationTest, LanguageDetectionIntegration) {
    auto& perfMonitor = utils::PerformanceMonitor::getInstance();
    
    // Initialize with language detection enabled
    ASSERT_TRUE(whisperSTT_->initialize("test_models/whisper-base.bin"));
    whisperSTT_->enableLanguageDetection(true);
    
    ASSERT_TRUE(transcriptionManager_->initialize("test_models/whisper-base.bin", "whisper"));
    transcriptionManager_->start();
    
    // Test different languages
    std::vector<std::string> languages = {"english", "spanish", "french"};
    std::map<std::string, std::string> detectedLanguages;
    std::map<std::string, float> languageConfidences;
    
    for (const std::string& lang : languages) {
        uint32_t utteranceId = 6000 + std::hash<std::string>{}(lang);
        
        std::atomic<bool> detectionComplete{false};
        
        stt::TranscriptionRequest request;
        request.utterance_id = utteranceId;
        request.audio_data = testAudioSamples_[lang];
        request.is_live = false;
        request.callback = [&](uint32_t id, const stt::TranscriptionResult& result) {
            detectedLanguages[lang] = result.detected_language;
            languageConfidences[lang] = result.language_confidence;
            detectionComplete = true;
        };
        
        auto detectionStart = std::chrono::high_resolution_clock::now();
        transcriptionManager_->submitTranscription(request);
        
        // Wait for detection
        auto timeout = std::chrono::high_resolution_clock::now() + std::chrono::seconds(8);
        while (!detectionComplete.load() && std::chrono::high_resolution_clock::now() < timeout) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        auto detectionEnd = std::chrono::high_resolution_clock::now();
        double detectionLatency = std::chrono::duration<double, std::milli>(detectionEnd - detectionStart).count();
        
        perfMonitor.recordLatency("language_detection.latency_" + lang + "_ms", detectionLatency);
        
        ASSERT_TRUE(detectionComplete.load()) << "Language detection should complete for " << lang;
    }
    
    std::cout << "Language Detection Results:" << std::endl;
    for (const std::string& lang : languages) {
        std::cout << "  " << lang << " -> detected: " << detectedLanguages[lang] 
                  << ", confidence: " << languageConfidences[lang] << std::endl;
        
        // Language detection assertions
        EXPECT_FALSE(detectedLanguages[lang].empty()) << "Should detect language for " << lang;
        EXPECT_GT(languageConfidences[lang], 0.0f) << "Should have confidence score for " << lang;
    }
    
    transcriptionManager_->stop();
}

} // namespace stt_integration

// Main function for standalone execution
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "=== STT Integration Test Suite ===" << std::endl;
    std::cout << "Testing end-to-end STT pipeline, performance, load handling, and WebSocket integration" << std::endl;
    
    return RUN_ALL_TESTS();
}