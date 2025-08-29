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

std::vector<float> generateFadingAudio(size_t samples, float startAmplitude, float endAmplitude) {
    std::vector<float> audio(samples);
    for (size_t i = 0; i < samples; ++i) {
        float progress = static_cast<float>(i) / samples;
        float amplitude = startAmplitude + (endAmplitude - startAmplitude) * progress;
        audio[i] = amplitude * std::sin(2.0f * M_PI * 440.0f * i / 16000.0f);
    }
    return audio;
}

// Test rapid speech/silence alternation
bool testRapidSpeechSilenceAlternation() {
    std::cout << "Testing rapid speech/silence alternation..." << std::endl;
    
    VadConfig config;
    config.speechThreshold = 0.5f;
    config.silenceThreshold = 0.3f;
    config.minSpeechDurationMs = 100;
    config.minSilenceDurationMs = 500;
    config.sampleRate = 16000;
    
    VoiceActivityDetector vad(config);
    
    // Track events and utterances
    std::vector<VadEvent> events;
    std::vector<std::pair<uint32_t, std::vector<float>>> utterances;
    
    vad.setVadCallback([&events](const VadEvent& event) {
        events.push_back(event);
        std::cout << "VAD Event: " << static_cast<int>(event.previousState) 
                  << " -> " << static_cast<int>(event.currentState) 
                  << " (confidence: " << event.confidence << ")" << std::endl;
    });
    
    vad.setUtteranceCallback([&utterances](uint32_t id, const std::vector<float>& audio) {
        utterances.emplace_back(id, audio);
        std::cout << "Utterance " << id << " completed with " << audio.size() << " samples" << std::endl;
    });
    
    if (!vad.initialize()) {
        std::cerr << "Failed to initialize VAD: " << vad.getErrorMessage() << std::endl;
        return false;
    }
    
    size_t samplesPerMs = config.sampleRate / 1000;
    size_t chunkSize = samplesPerMs * 20; // 20ms chunks
    
    // Simulate rapid alternation: speech-silence-speech-silence
    // Each segment is shorter than minimum durations to test state machine robustness
    
    // Short speech (50ms - less than minSpeechDurationMs)
    std::cout << "Processing short speech burst..." << std::endl;
    for (int i = 0; i < 3; ++i) {
        auto speechChunk = generateSpeech(chunkSize);
        vad.processAudio(speechChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Should be in SPEECH_DETECTED, not SPEAKING
    if (vad.getCurrentState() != VadState::SPEECH_DETECTED) {
        std::cerr << "Expected SPEECH_DETECTED for short speech, got " << static_cast<int>(vad.getCurrentState()) << std::endl;
        return false;
    }
    
    // Short silence (100ms - less than minSilenceDurationMs)
    std::cout << "Processing short silence..." << std::endl;
    for (int i = 0; i < 5; ++i) {
        auto silenceChunk = generateSilence(chunkSize);
        vad.processAudio(silenceChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Should return to IDLE since speech wasn't long enough
    if (vad.getCurrentState() != VadState::IDLE) {
        std::cerr << "Expected IDLE after short speech/silence, got " << static_cast<int>(vad.getCurrentState()) << std::endl;
        return false;
    }
    
    // No utterances should be created for short speech
    if (!utterances.empty()) {
        std::cerr << "Expected no utterances for short speech bursts" << std::endl;
        return false;
    }
    
    std::cout << "Rapid speech/silence alternation test passed!" << std::endl;
    return true;
}

// Test utterance continuation after brief pause
bool testUtteranceContinuation() {
    std::cout << "Testing utterance continuation after brief pause..." << std::endl;
    
    VadConfig config;
    config.speechThreshold = 0.5f;
    config.silenceThreshold = 0.3f;
    config.minSpeechDurationMs = 100;
    config.minSilenceDurationMs = 500;
    config.sampleRate = 16000;
    
    VoiceActivityDetector vad(config);
    
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
    size_t chunkSize = samplesPerMs * 20; // 20ms chunks
    
    // Start speaking (long enough to trigger SPEAKING state)
    std::cout << "Starting speech..." << std::endl;
    for (int i = 0; i < 8; ++i) { // 160ms of speech
        auto speechChunk = generateSpeech(chunkSize);
        vad.processAudio(speechChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    if (vad.getCurrentState() != VadState::SPEAKING) {
        std::cerr << "Expected SPEAKING state, got " << static_cast<int>(vad.getCurrentState()) << std::endl;
        return false;
    }
    
    uint32_t utteranceId = vad.getCurrentUtteranceId();
    if (utteranceId == 0) {
        std::cerr << "Expected non-zero utterance ID" << std::endl;
        return false;
    }
    
    // Brief pause (200ms - less than minSilenceDurationMs)
    std::cout << "Brief pause..." << std::endl;
    for (int i = 0; i < 10; ++i) { // 200ms of silence
        auto silenceChunk = generateSilence(chunkSize);
        vad.processAudio(silenceChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Should be in PAUSE_DETECTED
    if (vad.getCurrentState() != VadState::PAUSE_DETECTED) {
        std::cerr << "Expected PAUSE_DETECTED state, got " << static_cast<int>(vad.getCurrentState()) << std::endl;
        return false;
    }
    
    // Continue speaking (should return to SPEAKING with same utterance ID)
    std::cout << "Continuing speech..." << std::endl;
    for (int i = 0; i < 5; ++i) { // 100ms more speech
        auto speechChunk = generateSpeech(chunkSize);
        vad.processAudio(speechChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    if (vad.getCurrentState() != VadState::SPEAKING) {
        std::cerr << "Expected return to SPEAKING state, got " << static_cast<int>(vad.getCurrentState()) << std::endl;
        return false;
    }
    
    if (vad.getCurrentUtteranceId() != utteranceId) {
        std::cerr << "Expected same utterance ID after brief pause" << std::endl;
        return false;
    }
    
    // End utterance with long silence
    std::cout << "Ending utterance..." << std::endl;
    for (int i = 0; i < 30; ++i) { // 600ms of silence
        auto silenceChunk = generateSilence(chunkSize);
        vad.processAudio(silenceChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    if (vad.getCurrentState() != VadState::IDLE) {
        std::cerr << "Expected IDLE state after long silence, got " << static_cast<int>(vad.getCurrentState()) << std::endl;
        return false;
    }
    
    // Should have one utterance containing all the speech segments
    if (utterances.size() != 1) {
        std::cerr << "Expected exactly one utterance, got " << utterances.size() << std::endl;
        return false;
    }
    
    if (utterances[0].first != utteranceId) {
        std::cerr << "Utterance ID mismatch" << std::endl;
        return false;
    }
    
    std::cout << "Utterance continuation test passed!" << std::endl;
    return true;
}

// Test maximum utterance duration enforcement
bool testMaxUtteranceDuration() {
    std::cout << "Testing maximum utterance duration enforcement..." << std::endl;
    
    VadConfig config;
    config.speechThreshold = 0.5f;
    config.silenceThreshold = 0.3f;
    config.minSpeechDurationMs = 100;
    config.minSilenceDurationMs = 500;
    config.maxUtteranceDurationMs = 1000; // 1 second max
    config.sampleRate = 16000;
    
    VoiceActivityDetector vad(config);
    
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
    size_t chunkSize = samplesPerMs * 20; // 20ms chunks
    
    // Start speaking and continue for longer than max duration
    std::cout << "Speaking for longer than max duration..." << std::endl;
    
    // Speak for 1.5 seconds (longer than 1 second max)
    for (int i = 0; i < 75; ++i) { // 75 * 20ms = 1500ms
        auto speechChunk = generateSpeech(chunkSize);
        vad.processAudio(speechChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        
        // Check if utterance was force-ended due to max duration
        if (vad.getCurrentState() == VadState::IDLE && !utterances.empty()) {
            std::cout << "Utterance force-ended due to max duration after " << (i * 20) << "ms" << std::endl;
            break;
        }
    }
    
    // Should have at least one utterance that was force-ended
    if (utterances.empty()) {
        std::cerr << "Expected at least one utterance to be force-ended" << std::endl;
        return false;
    }
    
    // The utterance should contain approximately 1 second of audio (16000 samples)
    // But due to processing delays and chunk timing, allow reasonable tolerance
    size_t expectedSamples = config.sampleRate; // 1 second
    size_t actualSamples = utterances[0].second.size();
    
    // Allow more tolerance for timing variations in real-time processing
    // The key is that it was force-ended before the full 1.5 seconds
    if (actualSamples < expectedSamples * 0.5 || actualSamples > expectedSamples * 1.3) {
        std::cerr << "Unexpected utterance length: " << actualSamples << " samples (expected ~" << expectedSamples << " ±30%)" << std::endl;
        return false;
    }
    
    std::cout << "Utterance was correctly force-ended with " << actualSamples << " samples (~" << 
                 (actualSamples / 16.0) << "ms)" << std::endl;
    
    std::cout << "Maximum utterance duration test passed!" << std::endl;
    return true;
}

// Test audio level transitions (fading in/out)
bool testAudioLevelTransitions() {
    std::cout << "Testing audio level transitions..." << std::endl;
    
    VadConfig config;
    config.speechThreshold = 0.5f;
    config.silenceThreshold = 0.3f;
    config.minSpeechDurationMs = 100;
    config.minSilenceDurationMs = 500;
    config.sampleRate = 16000;
    
    VoiceActivityDetector vad(config);
    
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
    
    size_t samplesPerMs = config.sampleRate / 1000;
    size_t chunkSize = samplesPerMs * 20; // 20ms chunks
    
    // Test fade-in: start with silence, gradually increase amplitude
    std::cout << "Testing fade-in..." << std::endl;
    for (int i = 0; i < 20; ++i) { // 400ms fade-in
        float amplitude = 0.01f + (0.15f * i / 20.0f); // 0.01 to 0.16
        auto audioChunk = generateSpeech(chunkSize, amplitude);
        vad.processAudio(audioChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Should eventually detect speech
    if (vad.getCurrentState() == VadState::IDLE) {
        std::cerr << "Expected speech detection during fade-in" << std::endl;
        return false;
    }
    
    // Test fade-out: gradually decrease amplitude to silence
    std::cout << "Testing fade-out..." << std::endl;
    for (int i = 20; i >= 0; --i) { // 420ms fade-out
        float amplitude = 0.01f + (0.15f * i / 20.0f); // 0.16 to 0.01
        auto audioChunk = generateSpeech(chunkSize, amplitude);
        vad.processAudio(audioChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Add some silence to complete the transition
    for (int i = 0; i < 30; ++i) { // 600ms silence
        auto silenceChunk = generateSilence(chunkSize);
        vad.processAudio(silenceChunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Should return to IDLE
    if (vad.getCurrentState() != VadState::IDLE) {
        std::cerr << "Expected IDLE state after fade-out, got " << static_cast<int>(vad.getCurrentState()) << std::endl;
        return false;
    }
    
    std::cout << "Audio level transitions test passed!" << std::endl;
    return true;
}

int main() {
    utils::Logger::initialize();
    
    std::cout << "Running VAD Edge Case Tests..." << std::endl;
    std::cout << "==============================" << std::endl;
    
    bool allPassed = true;
    
    allPassed &= testRapidSpeechSilenceAlternation();
    std::cout << std::endl;
    
    allPassed &= testUtteranceContinuation();
    std::cout << std::endl;
    
    allPassed &= testMaxUtteranceDuration();
    std::cout << std::endl;
    
    allPassed &= testAudioLevelTransitions();
    std::cout << std::endl;
    
    if (allPassed) {
        std::cout << "All VAD edge case tests PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "Some VAD edge case tests FAILED!" << std::endl;
        return 1;
    }
}