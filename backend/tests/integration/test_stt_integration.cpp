#include "stt/transcription_manager.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cassert>

void test_transcription_manager_initialization() {
    std::cout << "Testing TranscriptionManager initialization..." << std::endl;
    
    stt::TranscriptionManager manager;
    
    // Test initialization with dummy model path
    bool result = manager.initialize("dummy_model.bin", "whisper");
    assert(result == true);
    assert(manager.isInitialized() == true);
    
    std::cout << "✓ TranscriptionManager initialization test passed" << std::endl;
}

void test_transcription_manager_workflow() {
    std::cout << "Testing TranscriptionManager workflow..." << std::endl;
    
    stt::TranscriptionManager manager;
    manager.initialize("dummy_model.bin", "whisper");
    manager.start();
    
    // Create test audio data
    std::vector<float> audioData(16000, 0.1f); // 1 second at 16kHz
    
    bool callback_called = false;
    std::string transcribed_text;
    uint32_t received_id = 0;
    
    // Create transcription request
    stt::TranscriptionRequest request;
    request.utterance_id = 123;
    request.audio_data = audioData;
    request.is_live = false;
    request.callback = [&](uint32_t id, const stt::TranscriptionResult& result) {
        callback_called = true;
        received_id = id;
        transcribed_text = result.text;
        std::cout << "Received transcription for utterance " << id << ": " << result.text << std::endl;
    };
    
    // Submit request
    manager.submitTranscription(request);
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    manager.stop();
    
    assert(callback_called == true);
    assert(received_id == 123);
    assert(!transcribed_text.empty());
    
    std::cout << "✓ TranscriptionManager workflow test passed" << std::endl;
}

void test_transcription_manager_configuration() {
    std::cout << "Testing TranscriptionManager configuration..." << std::endl;
    
    stt::TranscriptionManager manager;
    manager.initialize("dummy_model.bin", "whisper");
    
    // Test configuration methods
    manager.setLanguage("es");
    manager.setTranslateToEnglish(true);
    manager.setTemperature(0.5f);
    manager.setMaxTokens(100);
    
    std::cout << "✓ TranscriptionManager configuration test passed" << std::endl;
}

void test_transcription_manager_queue() {
    std::cout << "Testing TranscriptionManager queue handling..." << std::endl;
    
    stt::TranscriptionManager manager;
    manager.initialize("dummy_model.bin", "whisper");
    manager.start();
    
    // Submit multiple requests
    std::vector<float> audioData(8000, 0.1f); // 0.5 seconds at 16kHz
    int completed_requests = 0;
    
    for (int i = 0; i < 3; ++i) {
        stt::TranscriptionRequest request;
        request.utterance_id = i;
        request.audio_data = audioData;
        request.is_live = false;
        request.callback = [&](uint32_t id, const stt::TranscriptionResult& result) {
            completed_requests++;
            std::cout << "Completed request " << id << std::endl;
        };
        
        manager.submitTranscription(request);
    }
    
    // Wait for all requests to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    manager.stop();
    
    assert(completed_requests == 3);
    
    std::cout << "✓ TranscriptionManager queue test passed" << std::endl;
}

int main() {
    std::cout << "Running STT integration tests..." << std::endl;
    
    try {
        test_transcription_manager_initialization();
        test_transcription_manager_workflow();
        test_transcription_manager_configuration();
        test_transcription_manager_queue();
        
        std::cout << "\n✅ All STT integration tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "\n❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
}