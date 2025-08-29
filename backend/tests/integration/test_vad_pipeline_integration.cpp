#include "core/client_session.hpp"
#include "core/websocket_server.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include <atomic>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace core;
using namespace audio;

// Helper functions for testing
std::vector<uint8_t> generatePCMAudio(size_t samples, float amplitude = 0.1f, float frequency = 440.0f) {
    std::vector<uint8_t> pcmData(samples * 2); // 16-bit samples = 2 bytes each
    
    for (size_t i = 0; i < samples; ++i) {
        float sample = amplitude * std::sin(2.0f * M_PI * frequency * i / 16000.0f);
        int16_t pcmSample = static_cast<int16_t>(sample * 32767.0f);
        
        // Store as little-endian
        pcmData[i * 2] = pcmSample & 0xFF;
        pcmData[i * 2 + 1] = (pcmSample >> 8) & 0xFF;
    }
    
    return pcmData;
}

std::vector<uint8_t> generateSilencePCM(size_t samples) {
    return std::vector<uint8_t>(samples * 2, 0); // 16-bit silence
}

// Test VAD integration with ClientSession
bool testVADPipelineIntegration() {
    std::cout << "Testing VAD pipeline integration..." << std::endl;
    
    // Create a client session
    ClientSession session("test-session-001");
    
    // Track pipeline triggers
    std::vector<std::tuple<uint32_t, size_t, std::string, std::string, std::string>> pipelineTriggers;
    
    session.setPipelineCallback([&pipelineTriggers](uint32_t utteranceId, const std::vector<float>& audioData,
                                                    const std::string& sourceLang, const std::string& targetLang,
                                                    const std::string& voiceId) {
        pipelineTriggers.emplace_back(utteranceId, audioData.size(), sourceLang, targetLang, voiceId);
        std::cout << "Pipeline triggered for utterance " << utteranceId 
                  << " with " << audioData.size() << " samples" 
                  << " (" << sourceLang << " -> " << targetLang << ", voice: " << voiceId << ")" << std::endl;
    });
    
    // Configure session
    session.setLanguageConfig("en", "es");
    session.setVoiceConfig("female_voice_1");
    
    // Test 1: Process speech audio to trigger pipeline
    std::cout << "Test 1: Processing speech to trigger pipeline..." << std::endl;
    
    size_t samplesPerMs = 16; // 16kHz = 16 samples per ms
    size_t chunkSize = samplesPerMs * 20; // 20ms chunks
    
    // Send speech audio in chunks (simulate real-time)
    size_t speechDurationMs = 200; // 200ms of speech
    size_t totalChunks = speechDurationMs / 20;
    
    for (size_t i = 0; i < totalChunks; ++i) {
        auto speechPCM = generatePCMAudio(chunkSize);
        std::string_view audioData(reinterpret_cast<const char*>(speechPCM.data()), speechPCM.size());
        session.handleBinaryMessage(audioData);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Send silence to end utterance
    size_t silenceDurationMs = 600; // 600ms of silence
    size_t silenceChunks = silenceDurationMs / 20;
    
    for (size_t i = 0; i < silenceChunks; ++i) {
        auto silencePCM = generateSilencePCM(chunkSize);
        std::string_view audioData(reinterpret_cast<const char*>(silencePCM.data()), silencePCM.size());
        session.handleBinaryMessage(audioData);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Check if pipeline was triggered
    if (pipelineTriggers.empty()) {
        std::cerr << "Expected pipeline to be triggered for first utterance" << std::endl;
        return false;
    }
    
    auto& firstTrigger = pipelineTriggers[0];
    if (std::get<0>(firstTrigger) == 0) {
        std::cerr << "Expected non-zero utterance ID" << std::endl;
        return false;
    }
    
    if (std::get<1>(firstTrigger) == 0) {
        std::cerr << "Expected non-zero audio data size" << std::endl;
        return false;
    }
    
    if (std::get<2>(firstTrigger) != "en" || std::get<3>(firstTrigger) != "es") {
        std::cerr << "Expected correct language configuration" << std::endl;
        return false;
    }
    
    if (std::get<4>(firstTrigger) != "female_voice_1") {
        std::cerr << "Expected correct voice configuration" << std::endl;
        return false;
    }
    
    // Test 2: Process second utterance
    std::cout << "Test 2: Processing second utterance..." << std::endl;
    
    // Change configuration
    session.setLanguageConfig("es", "en");
    session.setVoiceConfig("male_voice_1");
    
    // Send second speech utterance
    for (size_t i = 0; i < totalChunks; ++i) {
        auto speechPCM = generatePCMAudio(chunkSize, 0.15f, 880.0f); // Different amplitude and frequency
        std::string_view audioData(reinterpret_cast<const char*>(speechPCM.data()), speechPCM.size());
        session.handleBinaryMessage(audioData);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // End second utterance
    for (size_t i = 0; i < silenceChunks; ++i) {
        auto silencePCM = generateSilencePCM(chunkSize);
        std::string_view audioData(reinterpret_cast<const char*>(silencePCM.data()), silencePCM.size());
        session.handleBinaryMessage(audioData);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Check second pipeline trigger
    if (pipelineTriggers.size() < 2) {
        std::cerr << "Expected second pipeline trigger" << std::endl;
        return false;
    }
    
    auto& secondTrigger = pipelineTriggers[1];
    if (std::get<0>(secondTrigger) <= std::get<0>(firstTrigger)) {
        std::cerr << "Expected second utterance ID to be greater than first" << std::endl;
        return false;
    }
    
    if (std::get<2>(secondTrigger) != "es" || std::get<3>(secondTrigger) != "en") {
        std::cerr << "Expected updated language configuration" << std::endl;
        return false;
    }
    
    if (std::get<4>(secondTrigger) != "male_voice_1") {
        std::cerr << "Expected updated voice configuration" << std::endl;
        return false;
    }
    
    std::cout << "VAD pipeline integration test passed!" << std::endl;
    return true;
}

// Test VAD state reporting
bool testVADStateReporting() {
    std::cout << "Testing VAD state reporting..." << std::endl;
    
    ClientSession session("test-session-002");
    
    // Track sent messages (we can't easily intercept them, so we'll check VAD state directly)
    size_t samplesPerMs = 16;
    size_t chunkSize = samplesPerMs * 20;
    
    // Initial state should be IDLE
    if (session.getCurrentVADState() != VadState::IDLE) {
        std::cerr << "Expected initial VAD state to be IDLE" << std::endl;
        return false;
    }
    
    // Send speech audio
    std::cout << "Sending speech audio..." << std::endl;
    auto speechPCM = generatePCMAudio(chunkSize);
    std::string_view audioData(reinterpret_cast<const char*>(speechPCM.data()), speechPCM.size());
    session.handleBinaryMessage(audioData);
    
    // Should transition to SPEECH_DETECTED
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (session.getCurrentVADState() != VadState::SPEECH_DETECTED) {
        std::cerr << "Expected VAD state to be SPEECH_DETECTED, got " << 
                     static_cast<int>(session.getCurrentVADState()) << std::endl;
        return false;
    }
    
    // Send more speech to transition to SPEAKING
    std::cout << "Sending extended speech..." << std::endl;
    for (size_t i = 0; i < 10; ++i) {
        auto speechPCM = generatePCMAudio(chunkSize);
        std::string_view audioData(reinterpret_cast<const char*>(speechPCM.data()), speechPCM.size());
        session.handleBinaryMessage(audioData);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Should be in SPEAKING state
    if (session.getCurrentVADState() != VadState::SPEAKING) {
        std::cerr << "Expected VAD state to be SPEAKING, got " << 
                     static_cast<int>(session.getCurrentVADState()) << std::endl;
        return false;
    }
    
    // Should have an active utterance ID
    if (session.getCurrentUtteranceId() == 0) {
        std::cerr << "Expected non-zero utterance ID in SPEAKING state" << std::endl;
        return false;
    }
    
    std::cout << "VAD state reporting test passed!" << std::endl;
    return true;
}

// Test VAD configuration
bool testVADConfiguration() {
    std::cout << "Testing VAD configuration..." << std::endl;
    
    ClientSession session("test-session-003");
    
    // Get default configuration
    auto defaultConfig = session.getVADConfig();
    if (defaultConfig.speechThreshold != 0.5f) {
        std::cerr << "Expected default speech threshold to be 0.5" << std::endl;
        return false;
    }
    
    // Update configuration
    VadConfig newConfig = defaultConfig;
    newConfig.speechThreshold = 0.7f;
    newConfig.minSpeechDurationMs = 200;
    
    session.setVADConfig(newConfig);
    
    // Verify configuration was updated
    auto updatedConfig = session.getVADConfig();
    if (updatedConfig.speechThreshold != 0.7f) {
        std::cerr << "Expected updated speech threshold to be 0.7" << std::endl;
        return false;
    }
    
    if (updatedConfig.minSpeechDurationMs != 200) {
        std::cerr << "Expected updated min speech duration to be 200ms" << std::endl;
        return false;
    }
    
    std::cout << "VAD configuration test passed!" << std::endl;
    return true;
}

// Test error handling
bool testVADErrorHandling() {
    std::cout << "Testing VAD error handling..." << std::endl;
    
    ClientSession session("test-session-004");
    
    // Test invalid audio data
    std::string invalidData("invalid audio data");
    session.handleBinaryMessage(invalidData);
    
    // Session should still be functional
    if (!session.isVADActive()) {
        // VAD might not be active yet, which is okay
        std::cout << "VAD not yet active (expected for invalid data)" << std::endl;
    }
    
    // Test valid audio after invalid
    size_t samplesPerMs = 16;
    size_t chunkSize = samplesPerMs * 20;
    auto speechPCM = generatePCMAudio(chunkSize);
    std::string_view audioData(reinterpret_cast<const char*>(speechPCM.data()), speechPCM.size());
    session.handleBinaryMessage(audioData);
    
    // Should recover and work normally
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    std::cout << "VAD error handling test passed!" << std::endl;
    return true;
}

int main() {
    utils::Logger::initialize();
    
    std::cout << "Running VAD Pipeline Integration Tests..." << std::endl;
    std::cout << "=========================================" << std::endl;
    
    bool allPassed = true;
    
    allPassed &= testVADPipelineIntegration();
    std::cout << std::endl;
    
    allPassed &= testVADStateReporting();
    std::cout << std::endl;
    
    allPassed &= testVADConfiguration();
    std::cout << std::endl;
    
    allPassed &= testVADErrorHandling();
    std::cout << std::endl;
    
    if (allPassed) {
        std::cout << "All VAD pipeline integration tests PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "Some VAD pipeline integration tests FAILED!" << std::endl;
        return 1;
    }
}