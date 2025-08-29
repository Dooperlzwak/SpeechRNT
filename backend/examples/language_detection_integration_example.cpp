#include "core/translation_pipeline.hpp"
#include "core/pipeline_websocket_integration.hpp"
#include "core/websocket_server.hpp"
#include "core/task_queue.hpp"
#include "mt/language_detector.hpp"
#include "stt/whisper_stt.hpp"
#include "mt/marian_translator.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

using namespace speechrnt;

/**
 * Example demonstrating language detection integration with translation pipeline
 */
int main() {
    utils::Logger::info("Starting Language Detection Integration Example");
    
    try {
        // Create core components
        auto task_queue = std::make_shared<core::TaskQueue>(4);
        auto websocket_server = std::make_shared<core::WebSocketServer>(8080);
        
        // Create AI engines
        auto stt_engine = std::make_shared<stt::WhisperSTT>();
        auto mt_engine = std::make_shared<mt::MarianTranslator>();
        auto language_detector = std::make_shared<mt::LanguageDetector>();
        
        // Initialize engines
        if (!stt_engine->initialize("models/whisper/ggml-base.bin")) {
            std::cerr << "Failed to initialize STT engine" << std::endl;
            return 1;
        }
        
        if (!mt_engine->initialize("en", "es")) {
            std::cerr << "Failed to initialize MT engine" << std::endl;
            return 1;
        }
        
        if (!language_detector->initialize("config/language_detection.json")) {
            std::cerr << "Failed to initialize language detector" << std::endl;
            return 1;
        }
        
        // Configure translation pipeline with language detection
        core::TranslationPipelineConfig pipeline_config;
        pipeline_config.enable_language_detection = true;
        pipeline_config.enable_automatic_language_switching = true;
        pipeline_config.language_detection_confidence_threshold = 0.8f;
        pipeline_config.enable_language_detection_caching = true;
        pipeline_config.notify_language_changes = true;
        pipeline_config.min_transcription_confidence = 0.7f;
        
        // Create and initialize pipeline
        auto pipeline = std::make_shared<core::TranslationPipeline>(pipeline_config);
        if (!pipeline->initialize(stt_engine, mt_engine, language_detector, task_queue)) {
            std::cerr << "Failed to initialize translation pipeline" << std::endl;
            return 1;
        }
        
        // Set up language configuration
        pipeline->setLanguageConfiguration("auto", "en"); // Auto-detect source, translate to English
        
        // Create WebSocket integration
        auto integration = std::make_shared<core::PipelineWebSocketIntegration>(pipeline, websocket_server);
        if (!integration->initialize()) {
            std::cerr << "Failed to initialize WebSocket integration" << std::endl;
            return 1;
        }
        
        // Set up additional callbacks for demonstration
        pipeline->setTranscriptionCompleteCallback([](const core::PipelineResult& result) {
            std::cout << "Transcription complete for utterance " << result.utterance_id 
                      << ": \"" << result.transcription.text << "\"" << std::endl;
        });
        
        pipeline->setLanguageDetectionCompleteCallback([](const core::PipelineResult& result) {
            std::cout << "Language detected: " << result.language_detection.detectedLanguage 
                      << " (confidence: " << result.language_detection.confidence << ")" << std::endl;
        });
        
        pipeline->setLanguageChangeCallback([](const std::string& session_id, 
                                              const std::string& old_lang, 
                                              const std::string& new_lang, 
                                              float confidence) {
            std::cout << "Language change detected for session " << session_id 
                      << ": " << old_lang << " -> " << new_lang 
                      << " (confidence: " << confidence << ")" << std::endl;
        });
        
        pipeline->setTranslationCompleteCallback([](const core::PipelineResult& result) {
            std::cout << "Translation complete for utterance " << result.utterance_id 
                      << ": \"" << result.translation.translatedText << "\"" << std::endl;
        });
        
        // Start WebSocket server in background thread
        std::thread server_thread([&websocket_server]() {
            websocket_server->start();
            websocket_server->run();
        });
        
        std::cout << "Language Detection Integration Example running..." << std::endl;
        std::cout << "WebSocket server listening on port 8080" << std::endl;
        std::cout << "Press Enter to run test scenarios..." << std::endl;
        std::cin.get();
        
        // Simulate some test scenarios
        std::cout << "\\n=== Running Test Scenarios ===" << std::endl;
        
        // Scenario 1: English text
        std::cout << "\\nScenario 1: English text" << std::endl;
        stt::TranscriptionResult english_result;
        english_result.text = "Hello, how are you today? I hope you're having a great day.";
        english_result.confidence = 0.95f;
        english_result.meets_confidence_threshold = true;
        pipeline->processTranscriptionResult(1, "demo_session", english_result);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Scenario 2: Spanish text (should trigger language change)
        std::cout << "\\nScenario 2: Spanish text (language change)" << std::endl;
        stt::TranscriptionResult spanish_result;
        spanish_result.text = "Hola, ¿cómo estás hoy? Espero que tengas un buen día.";
        spanish_result.confidence = 0.92f;
        spanish_result.meets_confidence_threshold = true;
        pipeline->processTranscriptionResult(2, "demo_session", spanish_result);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Scenario 3: French text (another language change)
        std::cout << "\\nScenario 3: French text (another language change)" << std::endl;
        stt::TranscriptionResult french_result;
        french_result.text = "Bonjour, comment allez-vous aujourd'hui? J'espère que vous passez une bonne journée.";
        french_result.confidence = 0.88f;
        french_result.meets_confidence_threshold = true;
        pipeline->processTranscriptionResult(3, "demo_session", french_result);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Scenario 4: Same French text again (should hit cache)
        std::cout << "\\nScenario 4: Same French text (cache test)" << std::endl;
        pipeline->processTranscriptionResult(4, "demo_session", french_result);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Scenario 5: Manual language detection trigger
        std::cout << "\\nScenario 5: Manual language detection" << std::endl;
        pipeline->triggerLanguageDetection(5, "demo_session", "Guten Tag, wie geht es Ihnen heute?");
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Display statistics
        auto stats = pipeline->getStatistics();
        std::cout << "\\n=== Pipeline Statistics ===" << std::endl;
        std::cout << "Total transcriptions processed: " << stats.total_transcriptions_processed << std::endl;
        std::cout << "Language detections performed: " << stats.language_detections_performed << std::endl;
        std::cout << "Language changes detected: " << stats.language_changes_detected << std::endl;
        std::cout << "Language detection cache hits: " << stats.language_detection_cache_hits << std::endl;
        std::cout << "Translations triggered: " << stats.translations_triggered << std::endl;
        std::cout << "Successful translations: " << stats.successful_translations << std::endl;
        std::cout << "Average language detection latency: " << stats.average_language_detection_latency.count() << "ms" << std::endl;
        std::cout << "Average translation latency: " << stats.average_translation_latency.count() << "ms" << std::endl;
        
        std::cout << "\\nPress Enter to shutdown..." << std::endl;
        std::cin.get();
        
        // Cleanup
        std::cout << "Shutting down..." << std::endl;
        integration->shutdown();
        pipeline->shutdown();
        websocket_server->stop();
        task_queue->shutdown();
        
        if (server_thread.joinable()) {
            server_thread.join();
        }
        
        std::cout << "Language Detection Integration Example completed successfully" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}