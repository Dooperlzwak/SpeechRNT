#include "audio/voice_activity_detector.hpp"
#include "audio/silero_vad_impl.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <thread>

using namespace audio;

// Simple test framework
class TestRunner {
public:
    static void runTest(const std::string& testName, std::function<bool()> testFunc) {
        std::cout << "Running " << testName << "... ";
        try {
            if (testFunc()) {
                std::cout << "PASSED" << std::endl;
                passedTests_++;
            } else {
                std::cout << "FAILED" << std::endl;
                failedTests_++;
            }
        } catch (const std::exception& e) {
            std::cout << "FAILED (exception: " << e.what() << ")" << std::endl;
            failedTests_++;
        }
        totalTests_++;
    }
    
    static void printSummary() {
        std::cout << "\n=== Test Summary ===" << std::endl;
        std::cout << "Total tests: " << totalTests_ << std::endl;
        std::cout << "Passed: " << passedTests_ << std::endl;
        std::cout << "Failed: " << failedTests_ << std::endl;
        std::cout << "Success rate: " << (totalTests_ > 0 ? (passedTests_ * 100 / totalTests_) : 0) << "%" << std::endl;
    }
    
    static int getExitCode() {
        return (failedTests_ == 0) ? 0 : 1;
    }
    
private:
    static int totalTests_;
    static int passedTests_;
    static int failedTests_;
};

int TestRunner::totalTests_ = 0;
int TestRunner::passedTests_ = 0;
int TestRunner::failedTests_ = 0;

// Test data generation
std::vector<float> generateSilence(size_t samples) {
    return std::vector<float>(samples, 0.0f);
}

std::vector<float> generateSpeechLikeAudio(size_t samples, uint32_t sampleRate = 16000) {
    std::vector<float> audio;
    audio.reserve(samples);
    
    for (size_t i = 0; i < samples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        // Mix of frequencies typical for speech
        float signal = 0.3f * std::sin(2.0f * M_PI * 200.0f * t) +  // Fundamental
                      0.2f * std::sin(2.0f * M_PI * 400.0f * t) +   // Harmonic
                      0.1f * std::sin(2.0f * M_PI * 800.0f * t);    // Higher harmonic
        
        // Add some noise
        float noise = 0.05f * (static_cast<float>(rand()) / RAND_MAX - 0.5f);
        audio.push_back(signal + noise);
    }
    
    return audio;
}

std::vector<float> generateNoise(size_t samples) {
    std::vector<float> audio;
    audio.reserve(samples);
    
    for (size_t i = 0; i < samples; ++i) {
        audio.push_back(0.02f * (static_cast<float>(rand()) / RAND_MAX - 0.5f));
    }
    
    return audio;
}

// Test functions
bool testSileroVadImplInitialization() {
    SileroVadImpl vad;
    
    // Test initialization
    if (!vad.initialize(16000)) {
        return false;
    }
    
    if (!vad.isInitialized()) {
        return false;
    }
    
    // Test shutdown
    vad.shutdown();
    if (vad.isInitialized()) {
        return false;
    }
    
    return true;
}

bool testSileroVadImplModes() {
    SileroVadImpl vad;
    if (!vad.initialize(16000)) {
        return false;
    }
    
    // Test mode switching
    vad.setVadMode(SileroVadImpl::VadMode::ENERGY_BASED);
    if (vad.getCurrentMode() != SileroVadImpl::VadMode::ENERGY_BASED) {
        return false;
    }
    
    vad.setVadMode(SileroVadImpl::VadMode::HYBRID);
    if (vad.getCurrentMode() != SileroVadImpl::VadMode::HYBRID) {
        return false;
    }
    
    // Test silero mode (may fallback to hybrid if model not loaded)
    vad.setVadMode(SileroVadImpl::VadMode::SILERO);
    auto mode = vad.getCurrentMode();
    if (mode != SileroVadImpl::VadMode::SILERO && mode != SileroVadImpl::VadMode::HYBRID) {
        return false;
    }
    
    return true;
}

bool testSileroVadImplProcessing() {
    SileroVadImpl vad;
    if (!vad.initialize(16000)) {
        return false;
    }
    
    // Force energy-based mode for consistent testing
    vad.setVadMode(SileroVadImpl::VadMode::ENERGY_BASED);
    
    // Generate test audio
    auto silence = generateSilence(1024);
    auto speech = generateSpeechLikeAudio(1024);
    auto noise = generateNoise(1024);
    
    // Test processing
    float silenceProb = vad.processSamples(silence);
    float speechProb = vad.processSamples(speech);
    float noiseProb = vad.processSamples(noise);
    
    // Validate results
    if (silenceProb < 0.0f || silenceProb > 1.0f) return false;
    if (speechProb < 0.0f || speechProb > 1.0f) return false;
    if (noiseProb < 0.0f || noiseProb > 1.0f) return false;
    
    // Speech should have higher probability than silence
    if (speechProb <= silenceProb) return false;
    
    return true;
}

bool testEnergyBasedVAD() {
    EnergyBasedVAD::Config config;
    config.energyThreshold = 0.01f;
    config.useAdaptiveThreshold = true;
    config.useSpectralFeatures = true;
    
    EnergyBasedVAD energyVad(config);
    
    // Generate test audio
    auto silence = generateSilence(1024);
    auto speech = generateSpeechLikeAudio(1024);
    
    // Test processing
    float silenceProb = energyVad.detectVoiceActivity(silence);
    float speechProb = energyVad.detectVoiceActivity(speech);
    
    // Validate results
    if (silenceProb < 0.0f || silenceProb > 1.0f) return false;
    if (speechProb < 0.0f || speechProb > 1.0f) return false;
    
    // Speech should have higher probability than silence
    if (speechProb <= silenceProb) return false;
    
    // Test reset
    energyVad.reset();
    
    // Should still work after reset
    float postResetProb = energyVad.detectVoiceActivity(speech);
    if (postResetProb < 0.0f || postResetProb > 1.0f) return false;
    
    return true;
}

bool testVoiceActivityDetectorIntegration() {
    VadConfig config;
    config.speechThreshold = 0.5f;
    config.silenceThreshold = 0.3f;
    config.sampleRate = 16000;
    config.minSpeechDurationMs = 100;
    config.minSilenceDurationMs = 500;
    
    VoiceActivityDetector detector(config);
    if (!detector.initialize()) {
        return false;
    }
    
    // Test enhanced VAD methods
    int currentMode = detector.getCurrentVadMode();
    if (currentMode < 0 || currentMode > 2) {
        return false;
    }
    
    // Test mode switching
    detector.setVadMode(1); // Energy-based
    if (detector.getCurrentVadMode() != 1) {
        return false;
    }
    
    detector.setVadMode(2); // Hybrid
    if (detector.getCurrentVadMode() != 2) {
        return false;
    }
    
    // Test audio processing
    auto speech = generateSpeechLikeAudio(1024);
    auto silence = generateSilence(1024);
    
    detector.processAudio(speech);
    detector.processAudio(silence);
    
    auto stats = detector.getStatistics();
    if (stats.totalAudioProcessed == 0) {
        return false;
    }
    
    return true;
}

bool testVADStateMachine() {
    VadConfig config;
    config.speechThreshold = 0.3f;
    config.silenceThreshold = 0.1f;
    config.sampleRate = 16000;
    config.minSpeechDurationMs = 50;  // Short for testing
    config.minSilenceDurationMs = 100; // Short for testing
    
    VoiceActivityDetector detector(config);
    if (!detector.initialize()) {
        return false;
    }
    
    // Force energy-based mode for predictable behavior
    detector.setVadMode(1);
    
    // Start with silence
    if (detector.getCurrentState() != VadState::IDLE) {
        return false;
    }
    
    // Process silence - should stay in IDLE
    auto silence = generateSilence(512);
    detector.processAudio(silence);
    if (detector.getCurrentState() != VadState::IDLE) {
        return false;
    }
    
    // Process speech - should eventually transition to SPEAKING
    auto speech = generateSpeechLikeAudio(1024);
    for (int i = 0; i < 5; ++i) {
        detector.processAudio(speech);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Should be in SPEECH_DETECTED or SPEAKING state
    auto state = detector.getCurrentState();
    if (state != VadState::SPEECH_DETECTED && state != VadState::SPEAKING) {
        return false;
    }
    
    return true;
}

bool testVADPerformance() {
    SileroVadImpl vad;
    if (!vad.initialize(16000)) {
        return false;
    }
    
    // Generate test audio
    auto testAudio = generateSpeechLikeAudio(1024);
    
    // Measure processing time
    const int iterations = 100;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        vad.processSamples(testAudio);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    double avgTimeMs = duration.count() / (1000.0 * iterations);
    
    std::cout << " (avg processing time: " << avgTimeMs << "ms) ";
    
    // Should be reasonably fast (less than 10ms per chunk)
    if (avgTimeMs > 10.0) {
        return false;
    }
    
    // Check statistics
    auto stats = vad.getStatistics();
    if (stats.totalProcessedChunks != iterations) {
        return false;
    }
    
    if (stats.averageProcessingTimeMs <= 0.0) {
        return false;
    }
    
    return true;
}

bool testVADErrorHandling() {
    SileroVadImpl vad;
    
    // Test processing without initialization
    auto testAudio = generateSpeechLikeAudio(1024);
    float result = vad.processSamples(testAudio);
    if (result != 0.0f) {
        return false;
    }
    
    // Test with empty audio
    if (!vad.initialize(16000)) {
        return false;
    }
    
    std::vector<float> emptyAudio;
    result = vad.processSamples(emptyAudio);
    if (result != 0.0f) {
        return false;
    }
    
    // Test with very short audio
    std::vector<float> shortAudio = {0.1f, -0.1f};
    result = vad.processSamples(shortAudio);
    if (result < 0.0f || result > 1.0f) {
        return false;
    }
    
    return true;
}

int main() {
    std::cout << "=== Silero-VAD Integration Tests ===" << std::endl;
    
    // Initialize logging
    utils::Logger::setLevel(utils::Logger::Level::INFO);
    
    // Run tests
    TestRunner::runTest("SileroVadImpl Initialization", testSileroVadImplInitialization);
    TestRunner::runTest("SileroVadImpl Modes", testSileroVadImplModes);
    TestRunner::runTest("SileroVadImpl Processing", testSileroVadImplProcessing);
    TestRunner::runTest("EnergyBasedVAD", testEnergyBasedVAD);
    TestRunner::runTest("VoiceActivityDetector Integration", testVoiceActivityDetectorIntegration);
    TestRunner::runTest("VAD State Machine", testVADStateMachine);
    TestRunner::runTest("VAD Performance", testVADPerformance);
    TestRunner::runTest("VAD Error Handling", testVADErrorHandling);
    
    // Print summary
    TestRunner::printSummary();
    
    return TestRunner::getExitCode();
}