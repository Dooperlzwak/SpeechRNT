#include "stt/stt_translation_integration.hpp"
#include "stt/whisper_stt.hpp"
#include "core/translation_pipeline.hpp"
#include "core/task_queue.hpp"
#include "mt/marian_translator.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>

/**
 * Simple test for STT-Translation Pipeline Integration
 * 
 * This test verifies that the integration components work together correctly.
 */

// Generate simple test audio data
std::vector<float> generateTestAudio(int samples = 16000) {
    std::vector<float> audio(samples);
    for (int i = 0; i < samples; ++i) {
        // Simple sine wave
        float t = static_cast<float>(i) / 16000.0f;
        audio[i] = 0.3f * std::sin(2.0f * 3.14159f * 440.0f * t);
    }
    return audio;
}

int main() {
    try {
        utils::Logger::info("Starting STT-Translation Integration Test");
        
        // 1. Create STT engine (in simulation mode)
        auto sttEngine = std::make_shared<stt::WhisperSTT>();
        if (!sttEngine->initialize("mock_model.bin", 2)) {
            std::cerr << "Failed to initialize STT engine" << std::endl;
            return 1;
        }
        
        // Configure STT for integration
        sttEngine->setLanguageDetectionEnabled(true);
        sttEngine->setConfidenceThreshold(0.7f);
        sttEngine->setWordLevelConfidenceEnabled(true);
        
        // 2. Create translation engine
        auto mtEngine = std::make_shared<speechrnt::mt::MarianTranslator>();
        if (!mtEngine->initialize("en", "es")) {
            std::cerr << "Failed to initialize translation engine" << std::endl;
            return 1;
        }
        
        // 3. Create task queue
        auto taskQueue = std::make_shared<speechrnt::core::TaskQueue>(2);
        
        // 4. Create translation pipeline
        speechrnt::core::TranslationPipelineConfig pipelineConfig;
        pipelineConfig.enable_automatic_translation = true;
        pipelineConfig.enable_confidence_gating = true;
        pipelineConfig.enable_multiple_candidates = true;
        pipelineConfig.min_transcription_confidence = 0.7f;
        pipelineConfig.max_transcription_candidates = 2;
        
        auto translationPipeline = std::make_shared<speechrnt::core::TranslationPipeline>(pipelineConfig);
        if (!translationPipeline->initialize(sttEngine, mtEngine, taskQueue)) {
            std::cerr << "Failed to initialize translation pipeline" << std::endl;
            return 1;
        }
        
        // Set up pipeline callbacks
        bool transcriptionCompleted = false;
        bool translationCompleted = false;
        
        translationPipeline->setTranscriptionCompleteCallback(
            [&transcriptionCompleted](const speechrnt::core::PipelineResult& result) {
                std::cout << "Transcription completed: \"" << result.transcription.text 
                          << "\" (confidence: " << result.transcription.confidence << ")" << std::endl;
                transcriptionCompleted = true;
            }
        );
        
        translationPipeline->setTranslationCompleteCallback(
            [&translationCompleted](const speechrnt::core::PipelineResult& result) {
                std::cout << "Translation completed: \"" << result.translation.translatedText 
                          << "\" (confidence: " << result.translation.confidence << ")" << std::endl;
                translationCompleted = true;
            }
        );
        
        // 5. Create STT-Translation integration
        stt::STTTranslationConfig integrationConfig;
        integrationConfig.enable_automatic_translation = true;
        integrationConfig.enable_confidence_gating = true;
        integrationConfig.enable_multiple_candidates = true;
        integrationConfig.min_transcription_confidence = 0.7f;
        integrationConfig.max_transcription_candidates = 2;
        
        auto integration = std::make_shared<stt::STTTranslationIntegration>(integrationConfig);
        if (!integration->initialize(sttEngine, translationPipeline)) {
            std::cerr << "Failed to initialize STT-Translation integration" << std::endl;
            return 1;
        }
        
        // Set up integration callbacks
        bool transcriptionReady = false;
        bool translationTriggered = false;
        
        integration->setTranscriptionReadyCallback(
            [&transcriptionReady](uint32_t utteranceId, const stt::TranscriptionResult& result, 
                                 const std::vector<stt::TranscriptionResult>& candidates) {
                std::cout << "Transcription ready for utterance " << utteranceId 
                          << " with " << candidates.size() << " candidates" << std::endl;
                transcriptionReady = true;
            }
        );
        
        integration->setTranslationTriggeredCallback(
            [&translationTriggered](uint32_t utteranceId, const std::string& sessionId, bool automatic) {
                std::cout << "Translation " << (automatic ? "automatically" : "manually") 
                          << " triggered for utterance " << utteranceId << std::endl;
                translationTriggered = true;
            }
        );
        
        // 6. Test the integration
        std::cout << "Testing STT-Translation integration..." << std::endl;
        
        auto testAudio = generateTestAudio(32000); // 2 seconds of audio
        uint32_t utteranceId = 1;
        std::string sessionId = "test_session";
        
        integration->processTranscriptionWithTranslation(utteranceId, sessionId, testAudio, true);
        
        // Wait for processing to complete
        int maxWaitTime = 10; // 10 seconds max
        int waitTime = 0;
        
        while (waitTime < maxWaitTime && (!transcriptionReady || !translationTriggered)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            waitTime++;
        }
        
        // Check results
        bool success = true;
        
        if (!transcriptionReady) {
            std::cerr << "ERROR: Transcription ready callback was not called" << std::endl;
            success = false;
        }
        
        if (!translationTriggered) {
            std::cerr << "ERROR: Translation triggered callback was not called" << std::endl;
            success = false;
        }
        
        // Print statistics
        auto stats = integration->getStatistics();
        std::cout << "\n=== Integration Statistics ===" << std::endl;
        std::cout << "Total transcriptions processed: " << stats.total_transcriptions_processed << std::endl;
        std::cout << "Automatic translations triggered: " << stats.automatic_translations_triggered << std::endl;
        std::cout << "Manual translations triggered: " << stats.manual_translations_triggered << std::endl;
        std::cout << "Confidence gate rejections: " << stats.confidence_gate_rejections << std::endl;
        std::cout << "Candidates generated: " << stats.candidates_generated << std::endl;
        std::cout << "Average transcription confidence: " << stats.average_transcription_confidence << std::endl;
        
        auto pipelineStats = translationPipeline->getStatistics();
        std::cout << "\n=== Pipeline Statistics ===" << std::endl;
        std::cout << "Successful translations: " << pipelineStats.successful_translations << std::endl;
        std::cout << "Failed translations: " << pipelineStats.failed_translations << std::endl;
        std::cout << "Average translation latency: " << pipelineStats.average_translation_latency.count() << "ms" << std::endl;
        
        // Test manual translation
        std::cout << "\nTesting manual translation..." << std::endl;
        
        stt::TranscriptionResult manualResult;
        manualResult.text = "Manual translation test";
        manualResult.confidence = 0.9f;
        manualResult.is_partial = false;
        manualResult.meets_confidence_threshold = true;
        manualResult.quality_level = "high";
        
        integration->triggerManualTranslation(2, "manual_session", manualResult, false);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // Cleanup
        integration->shutdown();
        translationPipeline->shutdown();
        taskQueue->shutdown();
        
        if (success) {
            std::cout << "\n✓ STT-Translation Integration Test PASSED" << std::endl;
            utils::Logger::info("STT-Translation Integration Test completed successfully");
            return 0;
        } else {
            std::cout << "\n✗ STT-Translation Integration Test FAILED" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}