#include <gtest/gtest.h>
#include "core/websocket_server.hpp"
#include "core/client_session.hpp"
#include "core/translation_pipeline.hpp"
#include "audio/voice_activity_detector.hpp"
#include "stt/whisper_stt.hpp"
#include "mt/marian_translator.hpp"
#include "tts/piper_tts.hpp"
#include "utils/logging.hpp"
#include <thread>
#include <chrono>
#include <memory>
#include <vector>
#include <atomic>

namespace integration {

class EndToEndConversationTest : public ::testing::Test {
protected:
    void SetUp() override {
        utils::Logger::initialize();
        
        // Start WebSocket server on test port
        server_ = std::make_unique<core::WebSocketServer>(8083);
        server_->start();
        
        // Give server time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Initialize pipeline components
        setupPipeline();
    }
    
    void TearDown() override {
        if (server_) {
            server_->stop();
        }
        pipeline_.reset();
    }
    
    void setupPipeline() {
        pipeline_ = std::make_unique<core::TranslationPipeline>();
        
        // Initialize with test models (mock or lightweight versions)
        pipeline_->initialize("test_models_path");
        
        // Set up language pair
        pipeline_->setLanguagePair("en", "es");
    }
    
    // Simulate audio input for testing
    std::vector<float> generateTestAudio(float frequency, float duration, int sampleRate = 16000) {
        std::vector<float> audio;
        int numSamples = static_cast<int>(duration * sampleRate);
        audio.reserve(numSamples);
        
        for (int i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            float sample = 0.5f * std::sin(2.0f * M_PI * frequency * t);
            audio.push_back(sample);
        }
        
        return audio;
    }
    
    // Generate speech-like audio pattern
    std::vector<float> generateSpeechPattern(float duration, int sampleRate = 16000) {
        std::vector<float> audio;
        int numSamples = static_cast<int>(duration * sampleRate);
        audio.reserve(numSamples);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<float> noise(0.0f, 0.1f);
        
        for (int i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            
            // Create speech-like pattern with multiple frequencies
            float sample = 0.3f * std::sin(2.0f * M_PI * 200.0f * t) +
                          0.2f * std::sin(2.0f * M_PI * 400.0f * t) +
                          0.1f * std::sin(2.0f * M_PI * 800.0f * t) +
                          noise(gen);
            
            // Add envelope to simulate speech
            float envelope = std::exp(-0.5f * t) * (1.0f + 0.5f * std::sin(10.0f * t));
            sample *= envelope;
            
            audio.push_back(std::clamp(sample, -1.0f, 1.0f));
        }
        
        return audio;
    }
    
    std::unique_ptr<core::WebSocketServer> server_;
    std::unique_ptr<core::TranslationPipeline> pipeline_;
};

// Test complete conversation flow
TEST_F(EndToEndConversationTest, CompleteConversationFlow) {
    // Create a client session
    auto session = std::make_shared<core::ClientSession>("test-session-e2e");
    session->setWebSocketServer(server_.get());
    
    // Configure session
    session->setLanguageConfig("en", "es");
    session->setVoiceConfig("female_voice_1");
    
    // Track conversation state
    std::atomic<bool> transcriptionReceived{false};
    std::atomic<bool> translationReceived{false};
    std::atomic<bool> audioReceived{false};
    std::atomic<int> statusUpdates{0};
    
    // Set up message handlers (in real implementation, these would be WebSocket callbacks)
    session->setMessageCallback([&](const std::string& message) {
        // Parse message and track state
        if (message.find("transcription_update") != std::string::npos) {
            transcriptionReceived = true;
        } else if (message.find("translation_result") != std::string::npos) {
            translationReceived = true;
        } else if (message.find("status_update") != std::string::npos) {
            statusUpdates++;
        }
    });
    
    session->setBinaryCallback([&](const std::vector<uint8_t>& data) {
        if (!data.empty()) {
            audioReceived = true;
        }
    });
    
    // Generate test speech audio
    auto speechAudio = generateSpeechPattern(2.0f); // 2 seconds of speech
    
    // Convert to PCM format for transmission
    std::vector<int16_t> pcmData;
    pcmData.reserve(speechAudio.size());
    for (float sample : speechAudio) {
        pcmData.push_back(static_cast<int16_t>(sample * 32767.0f));
    }
    
    // Send audio data in chunks (simulating real-time streaming)
    const size_t chunkSize = 1024;
    for (size_t i = 0; i < pcmData.size(); i += chunkSize) {
        size_t currentChunkSize = std::min(chunkSize, pcmData.size() - i);
        
        std::string_view binaryData(
            reinterpret_cast<const char*>(pcmData.data() + i),
            currentChunkSize * sizeof(int16_t)
        );
        
        session->handleBinaryMessage(binaryData);
        
        // Small delay to simulate real-time streaming
        std::this_thread::sleep_for(std::chrono::milliseconds(64)); // ~16kHz chunk rate
    }
    
    // Wait for processing to complete
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // Verify that all stages of the pipeline were triggered
    EXPECT_TRUE(transcriptionReceived.load()) << "Transcription should have been received";
    EXPECT_TRUE(translationReceived.load()) << "Translation should have been received";
    EXPECT_TRUE(audioReceived.load()) << "Synthesized audio should have been received";
    EXPECT_GT(statusUpdates.load(), 0) << "Status updates should have been sent";
}

// Test VAD triggering and utterance boundaries
TEST_F(EndToEndConversationTest, VADUtteranceBoundaries) {
    auto session = std::make_shared<core::ClientSession>("test-session-vad");
    session->setWebSocketServer(server_.get());
    session->setLanguageConfig("en", "es");
    
    std::atomic<int> utteranceCount{0};
    std::atomic<bool> vadTriggered{false};
    
    session->setMessageCallback([&](const std::string& message) {
        if (message.find("status_update") != std::string::npos) {
            if (message.find("listening") != std::string::npos) {
                vadTriggered = true;
            } else if (message.find("thinking") != std::string::npos) {
                utteranceCount++;
            }
        }
    });
    
    // Generate audio with speech and silence patterns
    std::vector<float> audioSequence;
    
    // Silence (0.5 seconds)
    auto silence1 = std::vector<float>(8000, 0.0f);
    audioSequence.insert(audioSequence.end(), silence1.begin(), silence1.end());
    
    // Speech (1 second)
    auto speech1 = generateSpeechPattern(1.0f);
    audioSequence.insert(audioSequence.end(), speech1.begin(), speech1.end());
    
    // Silence (0.5 seconds)
    auto silence2 = std::vector<float>(8000, 0.0f);
    audioSequence.insert(audioSequence.end(), silence2.begin(), silence2.end());
    
    // Speech (1 second)
    auto speech2 = generateSpeechPattern(1.0f);
    audioSequence.insert(audioSequence.end(), speech2.begin(), speech2.end());
    
    // Final silence (0.5 seconds)
    auto silence3 = std::vector<float>(8000, 0.0f);
    audioSequence.insert(audioSequence.end(), silence3.begin(), silence3.end());
    
    // Send audio sequence
    std::vector<int16_t> pcmData;
    pcmData.reserve(audioSequence.size());
    for (float sample : audioSequence) {
        pcmData.push_back(static_cast<int16_t>(sample * 32767.0f));
    }
    
    const size_t chunkSize = 1024;
    for (size_t i = 0; i < pcmData.size(); i += chunkSize) {
        size_t currentChunkSize = std::min(chunkSize, pcmData.size() - i);
        
        std::string_view binaryData(
            reinterpret_cast<const char*>(pcmData.data() + i),
            currentChunkSize * sizeof(int16_t)
        );
        
        session->handleBinaryMessage(binaryData);
        std::this_thread::sleep_for(std::chrono::milliseconds(64));
    }
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::seconds(4));
    
    // Should have detected 2 utterances
    EXPECT_TRUE(vadTriggered.load()) << "VAD should have been triggered";
    EXPECT_EQ(utteranceCount.load(), 2) << "Should have detected 2 separate utterances";
}

// Test concurrent session handling
TEST_F(EndToEndConversationTest, ConcurrentSessions) {
    const int numSessions = 5;
    std::vector<std::shared_ptr<core::ClientSession>> sessions;
    std::vector<std::atomic<bool>> transcriptionFlags(numSessions);
    std::vector<std::atomic<bool>> translationFlags(numSessions);
    
    // Create multiple sessions
    for (int i = 0; i < numSessions; ++i) {
        auto session = std::make_shared<core::ClientSession>("test-session-" + std::to_string(i));
        session->setWebSocketServer(server_.get());
        session->setLanguageConfig("en", "es");
        
        // Set up callbacks for this session
        session->setMessageCallback([&, i](const std::string& message) {
            if (message.find("transcription_update") != std::string::npos) {
                transcriptionFlags[i] = true;
            } else if (message.find("translation_result") != std::string::npos) {
                translationFlags[i] = true;
            }
        });
        
        sessions.push_back(session);
    }
    
    // Start concurrent audio processing
    std::vector<std::thread> audioThreads;
    
    for (int i = 0; i < numSessions; ++i) {
        audioThreads.emplace_back([&, i]() {
            auto speechAudio = generateSpeechPattern(1.5f);
            
            std::vector<int16_t> pcmData;
            pcmData.reserve(speechAudio.size());
            for (float sample : speechAudio) {
                pcmData.push_back(static_cast<int16_t>(sample * 32767.0f));
            }
            
            const size_t chunkSize = 1024;
            for (size_t j = 0; j < pcmData.size(); j += chunkSize) {
                size_t currentChunkSize = std::min(chunkSize, pcmData.size() - j);
                
                std::string_view binaryData(
                    reinterpret_cast<const char*>(pcmData.data() + j),
                    currentChunkSize * sizeof(int16_t)
                );
                
                sessions[i]->handleBinaryMessage(binaryData);
                std::this_thread::sleep_for(std::chrono::milliseconds(64));
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : audioThreads) {
        thread.join();
    }
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // Verify all sessions processed successfully
    for (int i = 0; i < numSessions; ++i) {
        EXPECT_TRUE(transcriptionFlags[i].load()) << "Session " << i << " should have received transcription";
        EXPECT_TRUE(translationFlags[i].load()) << "Session " << i << " should have received translation";
    }
}

// Test error handling and recovery
TEST_F(EndToEndConversationTest, ErrorHandlingAndRecovery) {
    auto session = std::make_shared<core::ClientSession>("test-session-error");
    session->setWebSocketServer(server_.get());
    session->setLanguageConfig("en", "invalid_language"); // Invalid target language
    
    std::atomic<bool> errorReceived{false};
    std::atomic<bool> recoverySuccessful{false};
    
    session->setMessageCallback([&](const std::string& message) {
        if (message.find("error") != std::string::npos) {
            errorReceived = true;
            
            // Attempt recovery by setting valid language
            session->setLanguageConfig("en", "es");
            recoverySuccessful = true;
        }
    });
    
    // Send audio that should trigger error
    auto speechAudio = generateSpeechPattern(1.0f);
    std::vector<int16_t> pcmData;
    pcmData.reserve(speechAudio.size());
    for (float sample : speechAudio) {
        pcmData.push_back(static_cast<int16_t>(sample * 32767.0f));
    }
    
    std::string_view binaryData(
        reinterpret_cast<const char*>(pcmData.data()),
        pcmData.size() * sizeof(int16_t)
    );
    
    session->handleBinaryMessage(binaryData);
    
    // Wait for error and recovery
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    EXPECT_TRUE(errorReceived.load()) << "Error should have been received for invalid language";
    EXPECT_TRUE(recoverySuccessful.load()) << "Recovery should have been attempted";
}

// Test latency measurements
TEST_F(EndToEndConversationTest, LatencyMeasurement) {
    auto session = std::make_shared<core::ClientSession>("test-session-latency");
    session->setWebSocketServer(server_.get());
    session->setLanguageConfig("en", "es");
    
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point transcriptionTime;
    std::chrono::steady_clock::time_point translationTime;
    std::chrono::steady_clock::time_point audioTime;
    
    session->setMessageCallback([&](const std::string& message) {
        auto now = std::chrono::steady_clock::now();
        
        if (message.find("transcription_update") != std::string::npos) {
            transcriptionTime = now;
        } else if (message.find("translation_result") != std::string::npos) {
            translationTime = now;
        }
    });
    
    session->setBinaryCallback([&](const std::vector<uint8_t>& data) {
        if (!data.empty()) {
            audioTime = std::chrono::steady_clock::now();
        }
    });
    
    // Record start time and send audio
    startTime = std::chrono::steady_clock::now();
    
    auto speechAudio = generateSpeechPattern(1.0f);
    std::vector<int16_t> pcmData;
    pcmData.reserve(speechAudio.size());
    for (float sample : speechAudio) {
        pcmData.push_back(static_cast<int16_t>(sample * 32767.0f));
    }
    
    std::string_view binaryData(
        reinterpret_cast<const char*>(pcmData.data()),
        pcmData.size() * sizeof(int16_t)
    );
    
    session->handleBinaryMessage(binaryData);
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // Calculate latencies
    if (transcriptionTime != std::chrono::steady_clock::time_point{}) {
        auto transcriptionLatency = std::chrono::duration_cast<std::chrono::milliseconds>(
            transcriptionTime - startTime).count();
        EXPECT_LT(transcriptionLatency, 1000) << "Transcription latency should be < 1000ms";
        std::cout << "Transcription latency: " << transcriptionLatency << "ms" << std::endl;
    }
    
    if (translationTime != std::chrono::steady_clock::time_point{}) {
        auto translationLatency = std::chrono::duration_cast<std::chrono::milliseconds>(
            translationTime - startTime).count();
        EXPECT_LT(translationLatency, 1500) << "Translation latency should be < 1500ms";
        std::cout << "Translation latency: " << translationLatency << "ms" << std::endl;
    }
    
    if (audioTime != std::chrono::steady_clock::time_point{}) {
        auto endToEndLatency = std::chrono::duration_cast<std::chrono::milliseconds>(
            audioTime - startTime).count();
        EXPECT_LT(endToEndLatency, 2000) << "End-to-end latency should be < 2000ms";
        std::cout << "End-to-end latency: " << endToEndLatency << "ms" << std::endl;
    }
}

} // namespace integration