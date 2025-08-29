#include "audio/realtime_audio_analyzer.hpp"
#include "audio/audio_monitoring_system.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

using namespace audio;

// Generate test audio signal
std::vector<float> generateTestAudio(float frequency, float duration, uint32_t sampleRate) {
    size_t numSamples = static_cast<size_t>(duration * sampleRate);
    std::vector<float> samples(numSamples);
    
    for (size_t i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        samples[i] = 0.5f * std::sin(2.0f * M_PI * frequency * t);
    }
    
    return samples;
}

int main() {
    std::cout << "Testing RealTimeAudioAnalyzer..." << std::endl;
    
    // Create analyzer
    auto analyzer = std::make_shared<RealTimeAudioAnalyzer>(16000, 1024);
    
    if (!analyzer->initialize()) {
        std::cerr << "Failed to initialize analyzer" << std::endl;
        return 1;
    }
    
    std::cout << "Analyzer initialized successfully" << std::endl;
    
    // Create monitoring system
    auto monitoringSystem = monitoring::createBasicMonitoringSystem(analyzer);
    
    if (!monitoringSystem->initialize()) {
        std::cerr << "Failed to initialize monitoring system" << std::endl;
        return 1;
    }
    
    std::cout << "Monitoring system initialized successfully" << std::endl;
    
    // Subscribe to metrics
    auto config = monitoring::createLowLatencyConfig();
    std::string subscriptionId = monitoringSystem->subscribe(config, 
        [](const RealTimeMetrics& metrics) {
            std::cout << "Metrics - Level: " << metrics.levels.currentLevel 
                     << ", Spectral Centroid: " << metrics.spectral.spectralCentroid 
                     << ", Speech Prob: " << metrics.speechProbability << std::endl;
        });
    
    if (subscriptionId.empty()) {
        std::cerr << "Failed to create subscription" << std::endl;
        return 1;
    }
    
    std::cout << "Created subscription: " << subscriptionId << std::endl;
    
    // Generate and process test audio
    std::cout << "Processing test audio..." << std::endl;
    
    // Test with different frequencies
    std::vector<float> frequencies = {440.0f, 880.0f, 1760.0f}; // A4, A5, A6
    
    for (float freq : frequencies) {
        std::cout << "Testing with " << freq << " Hz tone..." << std::endl;
        
        auto testAudio = generateTestAudio(freq, 1.0f, 16000); // 1 second of audio
        
        // Process audio in chunks
        size_t chunkSize = 1024;
        for (size_t i = 0; i < testAudio.size(); i += chunkSize) {
            size_t endIdx = std::min(i + chunkSize, testAudio.size());
            std::vector<float> chunk(testAudio.begin() + i, testAudio.begin() + endIdx);
            
            analyzer->processAudioChunk(chunk);
            
            // Small delay to simulate real-time processing
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // Wait a bit between frequencies
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // Test audio effects
    std::cout << "Testing audio effects..." << std::endl;
    
    auto testAudio = generateTestAudio(440.0f, 0.5f, 16000);
    analyzer->enableRealTimeEffects(true);
    
    AudioEffectsConfig effectsConfig;
    effectsConfig.enableCompressor = true;
    effectsConfig.enableNoiseGate = true;
    effectsConfig.compressorThreshold = -20.0f;
    effectsConfig.compressorRatio = 4.0f;
    
    analyzer->updateEffectsConfig(effectsConfig);
    
    auto processedAudio = analyzer->applyRealTimeEffects(testAudio);
    std::cout << "Applied effects to " << processedAudio.size() << " samples" << std::endl;
    
    // Test dropout detection
    std::cout << "Testing dropout detection..." << std::endl;
    
    // Create audio with a dropout (silence in the middle)
    auto audioWithDropout = generateTestAudio(440.0f, 2.0f, 16000);
    size_t dropoutStart = audioWithDropout.size() / 3;
    size_t dropoutEnd = 2 * audioWithDropout.size() / 3;
    
    for (size_t i = dropoutStart; i < dropoutEnd; ++i) {
        audioWithDropout[i] = 0.0f; // Create silence
    }
    
    analyzer->processAudioChunk(audioWithDropout);
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    auto dropouts = analyzer->getDetectedDropouts();
    std::cout << "Detected " << dropouts.size() << " dropouts" << std::endl;
    
    for (const auto& dropout : dropouts) {
        std::cout << "Dropout at " << dropout.timestampMs << "ms, duration: " 
                 << dropout.durationMs << "ms, severity: " << dropout.severityScore << std::endl;
    }
    
    // Test performance metrics
    auto performance = analyzer->getPerformanceMetrics();
    std::cout << "Performance metrics:" << std::endl;
    std::cout << "  Average processing time: " << performance.averageProcessingTimeMs << "ms" << std::endl;
    std::cout << "  Max processing time: " << performance.maxProcessingTimeMs << "ms" << std::endl;
    std::cout << "  Total samples processed: " << performance.totalSamplesProcessed << std::endl;
    
    // Test monitoring system performance
    auto systemPerf = monitoringSystem->getPerformance();
    std::cout << "Monitoring system performance:" << std::endl;
    std::cout << "  Active subscriptions: " << systemPerf.activeSubscriptions << std::endl;
    std::cout << "  Total callbacks: " << systemPerf.totalCallbacks << std::endl;
    
    // Test system health
    auto health = monitoringSystem->getSystemHealth();
    std::cout << "System health:" << std::endl;
    std::cout << "  Is healthy: " << (health.isHealthy ? "Yes" : "No") << std::endl;
    std::cout << "  Overall score: " << health.overallScore << std::endl;
    
    if (!health.issues.empty()) {
        std::cout << "  Issues:" << std::endl;
        for (const auto& issue : health.issues) {
            std::cout << "    - " << issue << std::endl;
        }
    }
    
    if (!health.warnings.empty()) {
        std::cout << "  Warnings:" << std::endl;
        for (const auto& warning : health.warnings) {
            std::cout << "    - " << warning << std::endl;
        }
    }
    
    std::cout << "Test completed successfully!" << std::endl;
    
    // Cleanup
    monitoringSystem->unsubscribe(subscriptionId);
    monitoringSystem->shutdown();
    
    return 0;
}