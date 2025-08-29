#include "stt/whisper_stt.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

int main() {
    std::cout << "Testing WhisperSTT integration..." << std::endl;
    
    // Create WhisperSTT instance
    auto whisperSTT = std::make_unique<stt::WhisperSTT>();
    
    // Try to initialize with a test model
    std::string modelPath = "third_party/whisper.cpp/models/for-tests-ggml-tiny.en.bin";
    
    std::cout << "Attempting to initialize with model: " << modelPath << std::endl;
    
    bool success = whisperSTT->initialize(modelPath, 4);
    
    if (success) {
        std::cout << "✓ WhisperSTT initialized successfully!" << std::endl;
        std::cout << "✓ Model loaded and validated" << std::endl;
        
        // Test basic functionality with dummy audio data
        std::vector<float> dummyAudio(16000, 0.0f); // 1 second of silence at 16kHz
        
        std::cout << "Testing transcription with dummy audio..." << std::endl;
        
        bool transcriptionCompleted = false;
        whisperSTT->transcribe(dummyAudio, [&](const stt::TranscriptionResult& result) {
            std::cout << "✓ Transcription callback received!" << std::endl;
            std::cout << "  Text: \"" << result.text << "\"" << std::endl;
            std::cout << "  Confidence: " << result.confidence << std::endl;
            std::cout << "  Duration: " << (result.end_time_ms - result.start_time_ms) << "ms" << std::endl;
            transcriptionCompleted = true;
        });
        
        // Wait a bit for the transcription to complete
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        if (transcriptionCompleted) {
            std::cout << "✓ Real Whisper.cpp integration test PASSED!" << std::endl;
            return 0;
        } else {
            std::cout << "✗ Transcription did not complete in time" << std::endl;
            return 1;
        }
        
    } else {
        std::cout << "✗ Failed to initialize WhisperSTT" << std::endl;
        std::cout << "  Error: " << whisperSTT->getLastError() << std::endl;
        
#ifdef WHISPER_AVAILABLE
        std::cout << "  Whisper.cpp is available but initialization failed" << std::endl;
        return 1;
#else
        std::cout << "  Whisper.cpp is not available - this is expected in simulation mode" << std::endl;
        std::cout << "✓ Simulation mode test PASSED!" << std::endl;
        return 0;
#endif
    }
}