#include "stt/advanced/advanced_stt_orchestrator.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>

using namespace stt::advanced;

/**
 * Example demonstrating the Advanced STT Infrastructure and Base Classes
 * 
 * This example shows how to:
 * 1. Initialize the Advanced STT Orchestrator
 * 2. Configure and enable various advanced features
 * 3. Process audio with advanced features
 * 4. Monitor feature health and performance
 */

void printFeatureStatus(const AdvancedSTTOrchestrator& orchestrator) {
    std::cout << "\n=== Advanced STT Feature Status ===" << std::endl;
    
    const std::vector<std::pair<AdvancedFeature, std::string>> features = {
        {AdvancedFeature::SPEAKER_DIARIZATION, "Speaker Diarization"},
        {AdvancedFeature::AUDIO_PREPROCESSING, "Audio Preprocessing"},
        {AdvancedFeature::CONTEXTUAL_TRANSCRIPTION, "Contextual Transcription"},
        {AdvancedFeature::REALTIME_ANALYSIS, "Real-time Analysis"},
        {AdvancedFeature::ADAPTIVE_QUALITY, "Adaptive Quality"},
        {AdvancedFeature::EXTERNAL_SERVICES, "External Services"},
        {AdvancedFeature::BATCH_PROCESSING, "Batch Processing"}
    };
    
    for (const auto& [feature, name] : features) {
        bool enabled = orchestrator.isFeatureEnabled(feature);
        std::cout << "  " << name << ": " << (enabled ? "ENABLED" : "DISABLED") << std::endl;
    }
    
    auto healthStatus = orchestrator.getHealthStatus();
    std::cout << "  Overall Health: " << (healthStatus.overallAdvancedHealth * 100.0f) << "%" << std::endl;
}

void printProcessingMetrics(const AdvancedSTTOrchestrator& orchestrator) {
    std::cout << "\n=== Processing Metrics ===" << std::endl;
    
    auto metrics = orchestrator.getProcessingMetrics();
    std::cout << "  Total Requests: " << metrics.totalProcessedRequests << std::endl;
    std::cout << "  Successful: " << metrics.successfulRequests << std::endl;
    std::cout << "  Failed: " << metrics.failedRequests << std::endl;
    
    if (metrics.totalProcessedRequests > 0) {
        std::cout << "  Average Processing Time: " << metrics.averageProcessingTime << " ms" << std::endl;
        std::cout << "  Average Confidence: " << metrics.averageConfidence << std::endl;
        
        if (metrics.minLatency > 0) {
            std::cout << "  Min Latency: " << metrics.minLatency << " ms" << std::endl;
            std::cout << "  Max Latency: " << metrics.maxLatency << " ms" << std::endl;
        }
    }
}

std::vector<float> generateTestAudio(float durationSeconds = 2.0f, int sampleRate = 16000) {
    size_t numSamples = static_cast<size_t>(durationSeconds * sampleRate);
    std::vector<float> audio(numSamples);
    
    // Generate a simple sine wave with some noise
    for (size_t i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        
        // 440 Hz sine wave (A4 note)
        float signal = 0.3f * std::sin(2.0f * M_PI * 440.0f * t);
        
        // Add some harmonics
        signal += 0.1f * std::sin(2.0f * M_PI * 880.0f * t);  // Octave
        signal += 0.05f * std::sin(2.0f * M_PI * 1320.0f * t); // Fifth
        
        // Add some noise
        float noise = 0.02f * (static_cast<float>(rand()) / RAND_MAX - 0.5f);
        
        audio[i] = signal + noise;
    }
    
    return audio;
}

int main() {
    std::cout << "Advanced STT Infrastructure Example" << std::endl;
    std::cout << "===================================" << std::endl;
    
    // Initialize logging
    utils::Logger::setLevel(utils::LogLevel::INFO);
    
    try {
        // Create orchestrator
        AdvancedSTTOrchestrator orchestrator;
        
        // Configure advanced features
        AdvancedSTTConfig config;
        
        std::cout << "\n1. Configuring Advanced STT Features..." << std::endl;
        
        // Enable audio preprocessing
        config.audioPreprocessing.enabled = true;
        config.audioPreprocessing.setBoolParameter("enableNoiseReduction", true);
        config.audioPreprocessing.setBoolParameter("enableVolumeNormalization", true);
        config.audioPreprocessing.setFloatParameter("noiseReductionStrength", 0.6f);
        std::cout << "  ✓ Audio preprocessing configured" << std::endl;
        
        // Enable real-time analysis
        config.realTimeAnalysis.enabled = true;
        config.realTimeAnalysis.setIntParameter("analysisBufferSize", 1024);
        config.realTimeAnalysis.setFloatParameter("metricsUpdateIntervalMs", 50.0f);
        config.realTimeAnalysis.setBoolParameter("enableSpectralAnalysis", true);
        std::cout << "  ✓ Real-time analysis configured" << std::endl;
        
        // Enable adaptive quality management
        config.adaptiveQuality.enabled = true;
        config.adaptiveQuality.setFloatParameter("cpuThreshold", 0.8f);
        config.adaptiveQuality.setFloatParameter("memoryThreshold", 0.8f);
        std::cout << "  ✓ Adaptive quality configured" << std::endl;
        
        // Enable speaker diarization
        config.speakerDiarization.enabled = true;
        config.speakerDiarization.setStringParameter("modelPath", "data/speaker_models/");
        config.speakerDiarization.setIntParameter("maxSpeakers", 4);
        config.speakerDiarization.setFloatParameter("speakerChangeThreshold", 0.7f);
        std::cout << "  ✓ Speaker diarization configured" << std::endl;
        
        // Enable external services (optional)
        config.externalServices.enabled = true;
        config.externalServices.setBoolParameter("enableResultFusion", true);
        config.externalServices.setFloatParameter("fallbackThreshold", 0.5f);
        std::cout << "  ✓ External services configured" << std::endl;
        
        // Initialize the orchestrator
        std::cout << "\n2. Initializing Advanced STT Orchestrator..." << std::endl;
        bool initSuccess = orchestrator.initializeAdvancedFeatures(config);
        
        if (!initSuccess) {
            std::cerr << "Failed to initialize orchestrator: " << orchestrator.getLastError() << std::endl;
            return 1;
        }
        
        std::cout << "  ✓ Orchestrator initialized successfully" << std::endl;
        
        // Print feature status
        printFeatureStatus(orchestrator);
        
        // Test feature enable/disable at runtime
        std::cout << "\n3. Testing Runtime Feature Management..." << std::endl;
        
        // Enable contextual transcription at runtime
        FeatureConfig contextualConfig;
        contextualConfig.enabled = true;
        contextualConfig.setStringParameter("modelsPath", "data/contextual_models/");
        contextualConfig.setBoolParameter("enableDomainDetection", true);
        
        bool enableSuccess = orchestrator.enableFeature(AdvancedFeature::CONTEXTUAL_TRANSCRIPTION, contextualConfig);
        if (enableSuccess) {
            std::cout << "  ✓ Contextual transcription enabled at runtime" << std::endl;
        } else {
            std::cout << "  ✗ Failed to enable contextual transcription: " << orchestrator.getLastError() << std::endl;
        }
        
        // Print updated feature status
        printFeatureStatus(orchestrator);
        
        // Test audio processing with advanced features
        std::cout << "\n4. Testing Audio Processing with Advanced Features..." << std::endl;
        
        // Generate test audio
        auto testAudio = generateTestAudio(3.0f); // 3 seconds of test audio
        std::cout << "  ✓ Generated " << testAudio.size() << " samples of test audio" << std::endl;
        
        // Create processing request
        AudioProcessingRequest request;
        request.utteranceId = 1;
        request.audioData = testAudio;
        request.isLive = false;
        
        // Enable all available features for processing
        request.enableSpeakerDiarization = true;
        request.enableAudioPreprocessing = true;
        request.enableContextualTranscription = true;
        request.enableRealTimeAnalysis = true;
        request.enableAdaptiveQuality = true;
        request.enableExternalServices = false; // Disable for this test
        
        request.domainHint = "general";
        request.languageHint = "en";
        request.preferredQuality = QualityLevel::HIGH;
        request.maxLatencyMs = 5000.0f;
        
        std::cout << "  ✓ Processing request configured" << std::endl;
        
        // Process audio
        std::cout << "  → Processing audio with advanced features..." << std::endl;
        auto startTime = std::chrono::steady_clock::now();
        
        auto result = orchestrator.processAudioWithAdvancedFeatures(request);
        
        auto endTime = std::chrono::steady_clock::now();
        auto processingTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();
        
        std::cout << "  ✓ Audio processing completed in " << processingTime << " ms" << std::endl;
        
        // Display results
        std::cout << "\n5. Processing Results:" << std::endl;
        std::cout << "  Transcription: \"" << result.text << "\"" << std::endl;
        std::cout << "  Confidence: " << result.confidence << std::endl;
        std::cout << "  Processing Latency: " << result.processingLatencyMs << " ms" << std::endl;
        std::cout << "  Quality Level Used: " << static_cast<int>(result.usedQualityLevel) << std::endl;
        
        if (!result.speakerSegments.empty()) {
            std::cout << "  Speaker Segments: " << result.speakerSegments.size() << std::endl;
            std::cout << "  Primary Speaker ID: " << result.primarySpeakerId << std::endl;
        }
        
        if (!result.appliedPreprocessing.empty()) {
            std::cout << "  Applied Preprocessing: ";
            for (size_t i = 0; i < result.appliedPreprocessing.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << static_cast<int>(result.appliedPreprocessing[i]);
            }
            std::cout << std::endl;
        }
        
        std::cout << "  Audio Quality Score: " << result.audioQuality.overallQuality << std::endl;
        std::cout << "  SNR: " << result.audioQuality.signalToNoiseRatio << " dB" << std::endl;
        
        if (result.usedExternalService) {
            std::cout << "  External Service Used: " << result.externalServiceName << std::endl;
        }
        
        // Print processing metrics
        printProcessingMetrics(orchestrator);
        
        // Test configuration update
        std::cout << "\n6. Testing Configuration Update..." << std::endl;
        
        AdvancedSTTConfig newConfig = config;
        newConfig.audioPreprocessing.setFloatParameter("noiseReductionStrength", 0.8f);
        newConfig.realTimeAnalysis.setIntParameter("analysisBufferSize", 2048);
        
        bool updateSuccess = orchestrator.updateConfiguration(newConfig);
        if (updateSuccess) {
            std::cout << "  ✓ Configuration updated successfully" << std::endl;
        } else {
            std::cout << "  ✗ Failed to update configuration: " << orchestrator.getLastError() << std::endl;
        }
        
        // Test async processing
        std::cout << "\n7. Testing Asynchronous Processing..." << std::endl;
        
        bool asyncCompleted = false;
        AdvancedTranscriptionResult asyncResult;
        
        request.callback = [&asyncCompleted, &asyncResult](const AdvancedTranscriptionResult& result) {
            asyncResult = result;
            asyncCompleted = true;
            std::cout << "  ✓ Async processing completed with confidence: " << result.confidence << std::endl;
        };
        
        orchestrator.processAudioAsync(request);
        
        // Wait for async completion (with timeout)
        int waitCount = 0;
        while (!asyncCompleted && waitCount < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            waitCount++;
        }
        
        if (asyncCompleted) {
            std::cout << "  ✓ Asynchronous processing test completed" << std::endl;
        } else {
            std::cout << "  ✗ Asynchronous processing timed out" << std::endl;
        }
        
        // Final status
        std::cout << "\n8. Final Status:" << std::endl;
        printFeatureStatus(orchestrator);
        printProcessingMetrics(orchestrator);
        
        // Cleanup
        std::cout << "\n9. Shutting down..." << std::endl;
        orchestrator.shutdown();
        std::cout << "  ✓ Orchestrator shutdown completed" << std::endl;
        
        std::cout << "\n=== Advanced STT Infrastructure Example Completed Successfully ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception occurred: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}