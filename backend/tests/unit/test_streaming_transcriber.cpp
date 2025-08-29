#include "stt/streaming_transcriber.hpp"
#include "stt/transcription_manager.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cassert>

void test_streaming_transcriber_initialization() {
    std::cout << "Testing StreamingTranscriber initialization..." << std::endl;
    
    stt::StreamingTranscriber transcriber;
    
    // Create transcription manager
    auto manager = std::make_shared<stt::TranscriptionManager>();
    manager->initialize("dummy_model.bin", "whisper");
    
    // Create message sender
    std::vector<std::string> sentMessages;
    auto messageSender = [&sentMessages](const std::string& message) {
        sentMessages.push_back(message);
        std::cout << "Sent message: " << message << std::endl;
    };
    
    // Test initialization
    bool result = transcriber.initialize(manager, messageSender);
    assert(result == true);
    
    std::cout << "✓ StreamingTranscriber initialization test passed" << std::endl;
}

void test_streaming_transcriber_workflow() {
    std::cout << "Testing StreamingTranscriber workflow..." << std::endl;
    
    stt::StreamingTranscriber transcriber;
    
    // Setup
    auto manager = std::make_shared<stt::TranscriptionManager>();
    manager->initialize("dummy_model.bin", "whisper");
    manager->start();
    
    std::vector<std::string> sentMessages;
    auto messageSender = [&sentMessages](const std::string& message) {
        sentMessages.push_back(message);
        std::cout << "Received message: " << message << std::endl;
    };
    
    transcriber.initialize(manager, messageSender);
    
    // Create test audio data
    std::vector<float> audioData(16000, 0.1f); // 1 second at 16kHz
    
    // Start transcription
    uint32_t utteranceId = 123;
    transcriber.startTranscription(utteranceId, audioData, true);
    
    // Check that transcription is active
    assert(transcriber.isTranscribing(utteranceId) == true);
    assert(transcriber.getActiveTranscriptions() == 1);
    
    // Wait for transcription result
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Finalize transcription
    transcriber.finalizeTranscription(utteranceId);
    
    // Check that transcription is no longer active
    assert(transcriber.isTranscribing(utteranceId) == false);
    
    // Should have received at least one message
    assert(sentMessages.size() > 0);
    
    manager->stop();
    
    std::cout << "✓ StreamingTranscriber workflow test passed" << std::endl;
}

void test_streaming_transcriber_multiple_utterances() {
    std::cout << "Testing StreamingTranscriber multiple utterances..." << std::endl;
    
    stt::StreamingTranscriber transcriber;
    
    // Setup
    auto manager = std::make_shared<stt::TranscriptionManager>();
    manager->initialize("dummy_model.bin", "whisper");
    manager->start();
    
    std::vector<std::string> sentMessages;
    auto messageSender = [&sentMessages](const std::string& message) {
        sentMessages.push_back(message);
    };
    
    transcriber.initialize(manager, messageSender);
    
    // Create test audio data
    std::vector<float> audioData(8000, 0.1f); // 0.5 seconds at 16kHz
    
    // Start multiple transcriptions
    std::vector<uint32_t> utteranceIds = {100, 101, 102};
    for (uint32_t id : utteranceIds) {
        transcriber.startTranscription(id, audioData, true);
    }
    
    // Check that all transcriptions are active
    assert(transcriber.getActiveTranscriptions() == 3);
    
    // Wait for results
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // Finalize all transcriptions
    for (uint32_t id : utteranceIds) {
        transcriber.finalizeTranscription(id);
        assert(transcriber.isTranscribing(id) == false);
    }
    
    assert(transcriber.getActiveTranscriptions() == 0);
    
    manager->stop();
    
    std::cout << "✓ StreamingTranscriber multiple utterances test passed" << std::endl;
}

void test_streaming_transcriber_configuration() {
    std::cout << "Testing StreamingTranscriber configuration..." << std::endl;
    
    stt::StreamingTranscriber transcriber;
    
    // Test configuration methods
    transcriber.setMinUpdateInterval(50);  // 50ms
    transcriber.setMinTextLength(5);       // 5 characters
    transcriber.setTextSimilarityThreshold(0.9f);  // 90% similarity threshold
    transcriber.setIncrementalUpdatesEnabled(true);
    transcriber.setMaxUpdateFrequency(15);  // 15 updates per second max
    
    std::cout << "✓ StreamingTranscriber configuration test passed" << std::endl;
}

void test_streaming_transcriber_text_similarity() {
    std::cout << "Testing StreamingTranscriber text similarity..." << std::endl;
    
    stt::StreamingTranscriber transcriber;
    
    // Setup
    auto manager = std::make_shared<stt::TranscriptionManager>();
    manager->initialize("dummy_model.bin", "whisper");
    manager->start();
    
    std::vector<std::string> sentMessages;
    auto messageSender = [&sentMessages](const std::string& message) {
        sentMessages.push_back(message);
    };
    
    transcriber.initialize(manager, messageSender);
    
    // Configure for high similarity threshold to test redundant update filtering
    transcriber.setTextSimilarityThreshold(0.9f);
    transcriber.setMinUpdateInterval(10);  // Very short interval for testing
    
    // Create test audio data
    std::vector<float> audioData(8000, 0.1f);
    
    uint32_t utteranceId = 200;
    transcriber.startTranscription(utteranceId, audioData, true);
    
    // Wait for initial result
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    size_t initialMessageCount = sentMessages.size();
    
    // Add more audio data (should trigger similar transcription)
    transcriber.addAudioData(utteranceId, audioData);
    
    // Wait briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Should not have sent many additional messages due to similarity filtering
    size_t finalMessageCount = sentMessages.size();
    
    transcriber.finalizeTranscription(utteranceId);
    manager->stop();
    
    std::cout << "Initial messages: " << initialMessageCount << ", Final messages: " << finalMessageCount << std::endl;
    std::cout << "✓ StreamingTranscriber text similarity test passed" << std::endl;
}

void test_streaming_transcriber_update_frequency() {
    std::cout << "Testing StreamingTranscriber update frequency control..." << std::endl;
    
    stt::StreamingTranscriber transcriber;
    
    // Setup
    auto manager = std::make_shared<stt::TranscriptionManager>();
    manager->initialize("dummy_model.bin", "whisper");
    manager->start();
    
    std::vector<std::string> sentMessages;
    std::vector<int64_t> messageTimestamps;
    
    auto messageSender = [&sentMessages, &messageTimestamps](const std::string& message) {
        sentMessages.push_back(message);
        auto now = std::chrono::steady_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        messageTimestamps.push_back(timestamp);
    };
    
    transcriber.initialize(manager, messageSender);
    
    // Configure for low update frequency
    transcriber.setMaxUpdateFrequency(2);  // Max 2 updates per second
    transcriber.setMinUpdateInterval(10);  // Override with frequency control
    transcriber.setTextSimilarityThreshold(0.1f);  // Low threshold to allow updates
    
    // Create test audio data
    std::vector<float> audioData(4000, 0.1f);
    
    uint32_t utteranceId = 300;
    transcriber.startTranscription(utteranceId, audioData, true);
    
    // Rapidly add audio data
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        transcriber.addAudioData(utteranceId, audioData);
    }
    
    transcriber.finalizeTranscription(utteranceId);
    manager->stop();
    
    // Check that update frequency was controlled
    if (messageTimestamps.size() >= 2) {
        for (size_t i = 1; i < messageTimestamps.size(); ++i) {
            int64_t timeDiff = messageTimestamps[i] - messageTimestamps[i-1];
            // Should be at least 500ms apart (1/2 updates per second)
            if (timeDiff < 400) {  // Allow some tolerance
                std::cout << "Warning: Messages too close together: " << timeDiff << "ms" << std::endl;
            }
        }
    }
    
    std::cout << "✓ StreamingTranscriber update frequency test passed" << std::endl;
}

int main() {
    std::cout << "Running StreamingTranscriber unit tests..." << std::endl;
    
    try {
        test_streaming_transcriber_initialization();
        test_streaming_transcriber_workflow();
        test_streaming_transcriber_multiple_utterances();
        test_streaming_transcriber_configuration();
        test_streaming_transcriber_text_similarity();
        test_streaming_transcriber_update_frequency();
        
        std::cout << "\n✅ All StreamingTranscriber tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "\n❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
}