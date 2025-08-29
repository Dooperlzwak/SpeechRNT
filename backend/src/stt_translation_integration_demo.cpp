#include "stt/stt_translation_integration.hpp"
#include "stt/whisper_stt.hpp"
#include "stt/streaming_transcriber.hpp"
#include "stt/transcription_manager.hpp"
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
 * Demonstration of STT-Translation Pipeline Integration
 * 
 * This demo shows how to:
 * 1. Set up the complete STT-Translation pipeline
 * 2. Process audio with automatic translation triggering
 * 3. Handle confidence-based translation gating
 * 4. Work with multiple transcription candidates
 * 5. Use streaming transcription with translation
 */

// Mock audio data generator for testing
std::vector<float> generateMockAudioData(int durationMs, float frequency = 440.0f) {
    const int sampleRate = 16000;
    const int numSamples = (durationMs * sampleRate) / 1000;
    
    std::vector<float> audioData(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        audioData[i] = 0.3f * std::sin(2.0f * M_PI * frequency * t);
    }
    
    return audioData;
}

class STTTranslationDemo {
public:
    STTTranslationDemo() = default;
    
    bool initialize() {
        utils::Logger::info("Initializing STT-Translation Integration Demo");
        
        // 1. Create and initialize STT engine
        sttEngine_ = std::make_shared<stt::WhisperSTT>();
        if (!sttEngine_->initialize("models/whisper-base.bin", 4)) {
            utils::Logger::error("Failed to initialize STT engine");
            return false;
        }
        
        // Configure STT engine for optimal translation integration
        sttEngine_->setLanguageDetectionEnabled(true);
        sttEngine_->setAutoLanguageSwitching(true);
        sttEngine_->setConfidenceThreshold(0.7f);
        sttEngine_->setWordLevelConfidenceEnabled(true);
        
        // 2. Create and initialize translation engine
        auto mtEngine = std::make_shared<speechrnt::mt::MarianTranslator>();
        if (!mtEngine->initialize("en", "es")) {
            utils::Logger::error("Failed to initialize translation engine");
            return false;
        }
        
        // 3. Create task queue for pipeline processing
        taskQueue_ = std::make_shared<speechrnt::core::TaskQueue>(4); // 4 worker threads
        
        // 4. Create and initialize translation pipeline
        speechrnt::core::TranslationPipelineConfig pipelineConfig;
        pipelineConfig.enable_automatic_translation = true;
        pipelineConfig.enable_confidence_gating = true;
        pipelineConfig.enable_multiple_candidates = true;
        pipelineConfig.min_transcription_confidence = 0.7f;
        pipelineConfig.candidate_confidence_threshold = 0.5f;
        pipelineConfig.max_transcription_candidates = 3;
        
        translationPipeline_ = std::make_shared<speechrnt::core::TranslationPipeline>(pipelineConfig);
        if (!translationPipeline_->initialize(sttEngine_, mtEngine, taskQueue_)) {
            utils::Logger::error("Failed to initialize translation pipeline");
            return false;
        }
        
        // Set up translation pipeline callbacks
        translationPipeline_->setTranscriptionCompleteCallback(
            [this](const speechrnt::core::PipelineResult& result) {
                handleTranscriptionComplete(result);
            }
        );
        
        translationPipeline_->setTranslationCompleteCallback(
            [this](const speechrnt::core::PipelineResult& result) {
                handleTranslationComplete(result);
            }
        );
        
        translationPipeline_->setPipelineErrorCallback(
            [this](const speechrnt::core::PipelineResult& result, const std::string& error) {
                handlePipelineError(result, error);
            }
        );
        
        // 5. Create streaming transcriber (optional)
        streamingTranscriber_ = std::make_shared<stt::StreamingTranscriber>();
        auto transcriptionManager = std::make_shared<stt::TranscriptionManager>();
        transcriptionManager->initialize(sttEngine_);
        
        auto messageSender = [this](const std::string& message) {
            handleStreamingMessage(message);
        };
        
        if (!streamingTranscriber_->initializeWithTranslationPipeline(
                transcriptionManager, messageSender, translationPipeline_)) {
            utils::Logger::error("Failed to initialize streaming transcriber");
            return false;
        }
        
        // 6. Create and initialize STT-Translation integration
        stt::STTTranslationConfig integrationConfig;
        integrationConfig.enable_automatic_translation = true;
        integrationConfig.enable_confidence_gating = true;
        integrationConfig.enable_multiple_candidates = true;
        integrationConfig.min_transcription_confidence = 0.7f;
        integrationConfig.candidate_confidence_threshold = 0.5f;
        integrationConfig.max_transcription_candidates = 3;
        
        integration_ = std::make_shared<stt::STTTranslationIntegration>(integrationConfig);
        if (!integration_->initializeWithStreaming(sttEngine_, streamingTranscriber_, translationPipeline_)) {
            utils::Logger::error("Failed to initialize STT-Translation integration");
            return false;
        }
        
        // Set up integration callbacks
        integration_->setTranscriptionReadyCallback(
            [this](uint32_t utteranceId, const stt::TranscriptionResult& result, const std::vector<stt::TranscriptionResult>& candidates) {
                handleTranscriptionReady(utteranceId, result, candidates);
            }
        );
        
        integration_->setTranslationTriggeredCallback(
            [this](uint32_t utteranceId, const std::string& sessionId, bool automatic) {
                handleTranslationTriggered(utteranceId, sessionId, automatic);
            }
        );
        
        utils::Logger::info("STT-Translation Integration Demo initialized successfully");
        return true;
    }
    
    void runDemo() {
        if (!integration_ || !integration_->isReady()) {
            utils::Logger::error("Integration not ready for demo");
            return;
        }
        
        utils::Logger::info("Starting STT-Translation Integration Demo");
        
        // Demo 1: Basic transcription with automatic translation
        demonstrateBasicTranscription();
        
        // Demo 2: Multiple candidates processing
        demonstrateMultipleCandidates();
        
        // Demo 3: Confidence-based gating
        demonstrateConfidenceGating();
        
        // Demo 4: Streaming transcription with translation
        demonstrateStreamingTranscription();
        
        // Demo 5: Manual translation triggering
        demonstrateManualTranslation();
        
        // Wait for all processing to complete
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // Print statistics
        printStatistics();
        
        utils::Logger::info("STT-Translation Integration Demo completed");
    }
    
    void shutdown() {
        if (integration_) {
            integration_->shutdown();
        }
        if (translationPipeline_) {
            translationPipeline_->shutdown();
        }
        if (taskQueue_) {
            taskQueue_->shutdown();
        }
        
        utils::Logger::info("STT-Translation Integration Demo shutdown completed");
    }

private:
    std::shared_ptr<stt::WhisperSTT> sttEngine_;
    std::shared_ptr<stt::StreamingTranscriber> streamingTranscriber_;
    std::shared_ptr<speechrnt::core::TranslationPipeline> translationPipeline_;
    std::shared_ptr<speechrnt::core::TaskQueue> taskQueue_;
    std::shared_ptr<stt::STTTranslationIntegration> integration_;
    
    uint32_t nextUtteranceId_ = 1;
    
    void demonstrateBasicTranscription() {
        utils::Logger::info("=== Demo 1: Basic Transcription with Automatic Translation ===");
        
        auto audioData = generateMockAudioData(3000); // 3 seconds of audio
        uint32_t utteranceId = nextUtteranceId_++;
        std::string sessionId = "demo_session_1";
        
        integration_->processTranscriptionWithTranslation(utteranceId, sessionId, audioData, true);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    void demonstrateMultipleCandidates() {
        utils::Logger::info("=== Demo 2: Multiple Candidates Processing ===");
        
        auto audioData = generateMockAudioData(2500, 880.0f); // Different frequency
        uint32_t utteranceId = nextUtteranceId_++;
        std::string sessionId = "demo_session_2";
        
        integration_->processTranscriptionWithTranslation(utteranceId, sessionId, audioData, true);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    void demonstrateConfidenceGating() {
        utils::Logger::info("=== Demo 3: Confidence-based Gating ===");
        
        // Create low-quality audio that should fail confidence gating
        auto audioData = generateMockAudioData(1000, 100.0f); // Very low frequency, short duration
        uint32_t utteranceId = nextUtteranceId_++;
        std::string sessionId = "demo_session_3";
        
        integration_->processTranscriptionWithTranslation(utteranceId, sessionId, audioData, false);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    void demonstrateStreamingTranscription() {
        utils::Logger::info("=== Demo 4: Streaming Transcription with Translation ===");
        
        uint32_t utteranceId = nextUtteranceId_++;
        std::string sessionId = "demo_session_4";
        
        // Simulate streaming audio chunks
        for (int i = 0; i < 3; ++i) {
            auto audioChunk = generateMockAudioData(1000, 440.0f + i * 100.0f);
            integration_->processStreamingTranscription(utteranceId, sessionId, audioChunk);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        
        // Finalize the streaming transcription
        if (streamingTranscriber_) {
            streamingTranscriber_->finalizeTranscription(utteranceId);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    void demonstrateManualTranslation() {
        utils::Logger::info("=== Demo 5: Manual Translation Triggering ===");
        
        // Create a mock transcription result
        stt::TranscriptionResult mockResult;
        mockResult.text = "This is a manual translation test";
        mockResult.confidence = 0.95f;
        mockResult.is_partial = false;
        mockResult.meets_confidence_threshold = true;
        mockResult.quality_level = "high";
        
        uint32_t utteranceId = nextUtteranceId_++;
        std::string sessionId = "demo_session_5";
        
        integration_->triggerManualTranslation(utteranceId, sessionId, mockResult, false);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    void printStatistics() {
        utils::Logger::info("=== Integration Statistics ===");
        
        auto stats = integration_->getStatistics();
        std::cout << "Total transcriptions processed: " << stats.total_transcriptions_processed << std::endl;
        std::cout << "Automatic translations triggered: " << stats.automatic_translations_triggered << std::endl;
        std::cout << "Manual translations triggered: " << stats.manual_translations_triggered << std::endl;
        std::cout << "Confidence gate rejections: " << stats.confidence_gate_rejections << std::endl;
        std::cout << "Candidates generated: " << stats.candidates_generated << std::endl;
        std::cout << "Average transcription confidence: " << stats.average_transcription_confidence << std::endl;
        
        auto pipelineStats = translationPipeline_->getStatistics();
        std::cout << "Pipeline successful translations: " << pipelineStats.successful_translations << std::endl;
        std::cout << "Pipeline failed translations: " << pipelineStats.failed_translations << std::endl;
        std::cout << "Average translation latency: " << pipelineStats.average_translation_latency.count() << "ms" << std::endl;
    }
    
    // Event handlers
    void handleTranscriptionComplete(const speechrnt::core::PipelineResult& result) {
        utils::Logger::info("Transcription completed for utterance " + std::to_string(result.utterance_id) + 
                           ": \"" + result.transcription.text + "\" (confidence: " + 
                           std::to_string(result.transcription.confidence) + ")");
    }
    
    void handleTranslationComplete(const speechrnt::core::PipelineResult& result) {
        utils::Logger::info("Translation completed for utterance " + std::to_string(result.utterance_id) + 
                           ": \"" + result.translation.translatedText + "\" (confidence: " + 
                           std::to_string(result.translation.confidence) + ")");
    }
    
    void handlePipelineError(const speechrnt::core::PipelineResult& result, const std::string& error) {
        utils::Logger::error("Pipeline error for utterance " + std::to_string(result.utterance_id) + ": " + error);
    }
    
    void handleStreamingMessage(const std::string& message) {
        utils::Logger::debug("Streaming message: " + message);
    }
    
    void handleTranscriptionReady(uint32_t utteranceId, const stt::TranscriptionResult& result, const std::vector<stt::TranscriptionResult>& candidates) {
        utils::Logger::info("Transcription ready for utterance " + std::to_string(utteranceId) + 
                           " with " + std::to_string(candidates.size()) + " candidates");
    }
    
    void handleTranslationTriggered(uint32_t utteranceId, const std::string& sessionId, bool automatic) {
        utils::Logger::info("Translation " + std::string(automatic ? "automatically" : "manually") + 
                           " triggered for utterance " + std::to_string(utteranceId));
    }
};

int main() {
    try {
        STTTranslationDemo demo;
        
        if (!demo.initialize()) {
            std::cerr << "Failed to initialize demo" << std::endl;
            return 1;
        }
        
        demo.runDemo();
        demo.shutdown();
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        return 1;
    }
}