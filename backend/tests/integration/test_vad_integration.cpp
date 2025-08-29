#include "audio/voice_activity_detector.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace audio;

// Helper functions for testing
std::vector<float> generateSilence(size_t samples) {
    return std::vector<float>(samples, 0.0f);
}

std::vector<float> generateSpeech(size_t samples, float amplitude = 0.1f, float frequency = 440.0f) {
    std::vector<float> speech(samples);
    for (size_t i = 0; i < samples; ++i) {
        speech[i] = amplitude * std::sin(2.0f * M_PI * frequency * i / 16000.0f);
    }
    return speech;
}

std::vector<float> generateNoise(size_t samples, float amplitude = 0.02f) {
    std::vector<float> noise(samples);
    for (size_t i = 0; i < samples; ++i) {
        noise[i] = amplitude * (2.0f * (rand() / float(RAND_MAX)) - 1.0f);
    }
    return noise;
}

// Test state machine transitions
bool testStateTransitions() {
    std::cout << "Testing VAD state transitions..." << std::endl;
    
    VadConfig config;
    config.speechThreshold = 0.5f;
    config.silenceThreshold = 0.3f;
    config.minSpeechDurationMs = 100;
    config.minSilenceDurationMs = 500;
    config.sampleRate = 16000;
    
    VoiceActivityDetector vad(config);
    
    // Track events
    std::vector<VadEvent> events;
    vad.setVadCallback([&events](const VadEvent& event) {
        events.push_back(event);
        std::cout << "VAD Event: " << static_cast<int>(event.previousState) 
                  << " -> " << static_cast<int>(event.currentState) 
                  << " (confidence: " << event.confidence << ")" << std::endl;
    });
    
    if (!vad.initialize()) {
        std::cerr << "Failed to initialize VAD: " << vad.getErrorMessage() << std::endl;
        return false;
    }
    
    // Test 1: IDLE -> SPEECH_DETECTED
    std::cout << "Test 1: Processing speech audio..." << std::endl;
    auto speechAudio = generateSpeech(1024);
    vad.processAudio(speechAudio);
    
    if (vad.getCurrentState() != VadState::SPEECH_DETECTED) {
        std::cerr << "Expected SPEECH_DETECTED state, got " << static_cast<int>(vad.getCurrentState()) << std::endl;
        return false;
    }
    
    // Test 2: SPEECH_DETECTED -> SPEAKING (after minimum duration)
    std::cout << "Test 2: Processing extended speech..." << std::endl;
    size_t samplesPerMs = config.sampleRate / 1000;
    
    // Process speech in smaller chunks to simulate real-time processing
    size_t chunkSize = samplesPerMs * 20; // 20ms chunks
    size_t totalChunks = (config.minSpeechDurationMs + 50) / 20;
    
    for (size_t i = 0; i < totalChunks; ++i) {
        auto speechChunk = generateSpeech(chunkSize);
        vad.processAudio(speechChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20)); // Simulate real-time
    }
    
    if (vad.getCurrentState() != VadState::SPEAKING) {
        std::cerr << "Expected SPEAKING state, got " << static_cast<int>(vad.getCurrentState()) << std::endl;
        return false;
    }
    
    if (vad.getCurrentUtteranceId() == 0) {
        std::cerr << "Expected non-zero utterance ID" << std::endl;
        return false;
    }
    
    // Test 3: SPEAKING -> PAUSE_DETECTED
    std::cout << "Test 3: Processing silence..." << std::endl;
    auto silenceAudio = generateSilence(samplesPerMs * 100);
    vad.processAudio(silenceAudio);
    
    if (vad.getCurrentState() != VadState::PAUSE_DETECTED) {
        std::cerr << "Expected PAUSE_DETECTED state, got " << static_cast<int>(vad.getCurrentState()) << std::endl;
        return false;
    }
    
    // Test 4: PAUSE_DETECTED -> IDLE (after minimum silence duration)
    std::cout << "Test 4: Processing extended silence..." << std::endl;
    size_t silenceChunks = (config.minSilenceDurationMs + 100) / 20;
    
    for (size_t i = 0; i < silenceChunks; ++i) {
        auto silenceChunk = generateSilence(chunkSize);
        vad.processAudio(silenceChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    if (vad.getCurrentState() != VadState::IDLE) {
        std::cerr << "Expected IDLE state, got " << static_cast<int>(vad.getCurrentState()) << std::endl;
        return false;
    }
    
    std::cout << "State transition test passed!" << std::endl;
    return true;
}

// Test utterance management
bool testUtteranceManagement() {
    std::cout << "Testing utterance management..." << std::endl;
    
    VadConfig config;
    config.speechThreshold = 0.5f;
    config.silenceThreshold = 0.3f;
    config.minSpeechDurationMs = 100;
    config.minSilenceDurationMs = 500;
    config.sampleRate = 16000;
    
    VoiceActivityDetector vad(config);
    
    // Track utterances
    std::vector<std::pair<uint32_t, std::vector<float>>> utterances;
    vad.setUtteranceCallback([&utterances](uint32_t id, const std::vector<float>& audio) {
        utterances.emplace_back(id, audio);
        std::cout << "Utterance " << id << " completed with " << audio.size() << " samples" << std::endl;
    });
    
    if (!vad.initialize()) {
        std::cerr << "Failed to initialize VAD: " << vad.getErrorMessage() << std::endl;
        return false;
    }
    
    size_t samplesPerMs = config.sampleRate / 1000;
    
    // Create first utterance
    std::cout << "Creating first utterance..." << std::endl;
    size_t chunkSize = samplesPerMs * 20; // 20ms chunks
    size_t totalChunks = (config.minSpeechDurationMs + 100) / 20;
    
    for (size_t i = 0; i < totalChunks; ++i) {
        auto speechChunk = generateSpeech(chunkSize);
        vad.processAudio(speechChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    uint32_t firstUtteranceId = vad.getCurrentUtteranceId();
    if (firstUtteranceId == 0) {
        std::cerr << "Expected non-zero utterance ID for first utterance" << std::endl;
        return false;
    }
    
    // End first utterance
    size_t silenceChunks = (config.minSilenceDurationMs + 100) / 20;
    for (size_t i = 0; i < silenceChunks; ++i) {
        auto silenceChunk = generateSilence(chunkSize);
        vad.processAudio(silenceChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    if (utterances.empty()) {
        std::cerr << "Expected first utterance to be completed" << std::endl;
        return false;
    }
    
    if (utterances[0].first != firstUtteranceId) {
        std::cerr << "Utterance ID mismatch" << std::endl;
        return false;
    }
    
    // Create second utterance
    std::cout << "Creating second utterance..." << std::endl;
    for (size_t i = 0; i < totalChunks; ++i) {
        auto speechChunk = generateSpeech(chunkSize);
        vad.processAudio(speechChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    uint32_t secondUtteranceId = vad.getCurrentUtteranceId();
    if (secondUtteranceId <= firstUtteranceId) {
        std::cerr << "Expected second utterance ID to be greater than first" << std::endl;
        return false;
    }
    
    // End second utterance
    for (size_t i = 0; i < silenceChunks; ++i) {
        auto silenceChunk = generateSilence(chunkSize);
        vad.processAudio(silenceChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    if (utterances.size() != 2) {
        std::cerr << "Expected two completed utterances, got " << utterances.size() << std::endl;
        return false;
    }
    
    std::cout << "Utterance management test passed!" << std::endl;
    return true;
}

// Test noise rejection
bool testNoiseRejection() {
    std::cout << "Testing noise rejection..." << std::endl;
    
    VadConfig config;
    config.speechThreshold = 0.5f;
    config.silenceThreshold = 0.3f;
    config.minSpeechDurationMs = 100;
    config.minSilenceDurationMs = 500;
    config.sampleRate = 16000;
    
    VoiceActivityDetector vad(config);
    
    // Track events
    std::vector<VadEvent> events;
    vad.setVadCallback([&events](const VadEvent& event) {
        events.push_back(event);
    });
    
    if (!vad.initialize()) {
        std::cerr << "Failed to initialize VAD: " << vad.getErrorMessage() << std::endl;
        return false;
    }
    
    // Process low-level noise
    std::cout << "Processing low-level noise..." << std::endl;
    auto noiseAudio = generateNoise(2048, 0.01f); // Very low amplitude
    vad.processAudio(noiseAudio);
    
    if (vad.getCurrentState() != VadState::IDLE) {
        std::cerr << "Expected to remain in IDLE state with noise, got " << static_cast<int>(vad.getCurrentState()) << std::endl;
        return false;
    }
    
    if (!events.empty()) {
        std::cerr << "Expected no state transitions with noise" << std::endl;
        return false;
    }
    
    std::cout << "Noise rejection test passed!" << std::endl;
    return true;
}

// Test statistics tracking
bool testStatistics() {
    std::cout << "Testing statistics tracking..." << std::endl;
    
    VadConfig config;
    VoiceActivityDetector vad(config);
    
    if (!vad.initialize()) {
        std::cerr << "Failed to initialize VAD: " << vad.getErrorMessage() << std::endl;
        return false;
    }
    
    // Process some audio
    auto speechAudio = generateSpeech(1024);
    vad.processAudio(speechAudio);
    
    auto stats = vad.getStatistics();
    if (stats.totalAudioProcessed == 0) {
        std::cerr << "Expected non-zero audio processed count" << std::endl;
        return false;
    }
    
    if (stats.averageConfidence <= 0.0) {
        std::cerr << "Expected positive average confidence" << std::endl;
        return false;
    }
    
    // Reset statistics
    vad.resetStatistics();
    stats = vad.getStatistics();
    if (stats.totalAudioProcessed != 0) {
        std::cerr << "Expected zero audio processed count after reset" << std::endl;
        return false;
    }
    
    std::cout << "Statistics test passed!" << std::endl;
    return true;
}

// Test error handling
bool testErrorHandling() {
    std::cout << "Testing error handling..." << std::endl;
    
    VadConfig config;
    VoiceActivityDetector vad(config);
    
    // Try to process audio without initialization
    auto speechAudio = generateSpeech(1024);
    vad.processAudio(speechAudio);
    
    if (vad.getLastError() != VoiceActivityDetector::ErrorCode::NOT_INITIALIZED) {
        std::cerr << "Expected NOT_INITIALIZED error" << std::endl;
        return false;
    }
    
    // Test invalid configuration
    VadConfig invalidConfig;
    invalidConfig.speechThreshold = 1.5f; // Invalid
    
    try {
        VoiceActivityDetector invalidVad(invalidConfig);
        std::cerr << "Expected exception for invalid configuration" << std::endl;
        return false;
    } catch (const std::invalid_argument&) {
        // Expected
    }
    
    std::cout << "Error handling test passed!" << std::endl;
    return true;
}

int main() {
    utils::Logger::initialize();
    
    std::cout << "Running VAD Integration Tests..." << std::endl;
    std::cout << "=================================" << std::endl;
    
    bool allPassed = true;
    
    allPassed &= testStateTransitions();
    std::cout << std::endl;
    
    allPassed &= testUtteranceManagement();
    std::cout << std::endl;
    
    allPassed &= testNoiseRejection();
    std::cout << std::endl;
    
    allPassed &= testStatistics();
    std::cout << std::endl;
    
    allPassed &= testErrorHandling();
    std::cout << std::endl;
    
    if (allPassed) {
        std::cout << "All VAD integration tests PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "Some VAD integration tests FAILED!" << std::endl;
        return 1;
    }
}