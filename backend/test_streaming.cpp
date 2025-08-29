#include "stt/whisper_stt.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

int main() {
    std::cout << "Testing WhisperSTT streaming capabilities..." << std::endl;
    
    // Create WhisperSTT instance
    auto whisperSTT = std::make_unique<stt::WhisperSTT>();
    
    // Initialize with a dummy model path (will use simulation mode)
    if (!whisperSTT->initialize("dummy_model.bin")) {
        std::cerr << "Failed to initialize WhisperSTT: " << whisperSTT->getLastError() << std::endl;
        return 1;
    }
    
    std::cout << "WhisperSTT initialized successfully" << std::endl;
    
    // Test streaming functionality
    uint32_t utteranceId = 1;
    
    // Set up callback
    auto callback = [](const stt::TranscriptionResult& result) {
        std::cout << "Transcription result: \"" << result.text 
                  << "\" (confidence: " << result.confidence 
                  << ", partial: " << (result.is_partial ? "true" : "false") << ")" << std::endl;
    };
    
    // Start streaming transcription
    whisperSTT->startStreamingTranscription(utteranceId);
    whisperSTT->setStreamingCallback(utteranceId, callback);
    
    std::cout << "Started streaming transcription for utterance " << utteranceId << std::endl;
    
    // Simulate adding audio chunks
    std::vector<float> audioChunk(16000, 0.1f); // 1 second of dummy audio
    
    for (int i = 0; i < 3; ++i) {
        std::cout << "Adding audio chunk " << (i + 1) << std::endl;
        whisperSTT->addAudioChunk(utteranceId, audioChunk);
        
        // Wait a bit between chunks
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // Finalize transcription
    std::cout << "Finalizing transcription..." << std::endl;
    whisperSTT->finalizeStreamingTranscription(utteranceId);
    
    // Wait for any pending operations
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    std::cout << "Test completed successfully!" << std::endl;
    return 0;
}