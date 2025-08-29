#include <iostream>
#include <memory>
#include <vector>
#include <chrono>
#include <thread>

#include "core/utterance_manager.hpp"
#include "core/task_queue.hpp"
#include "stt/whisper_stt.hpp"
#include "mt/marian_translator.hpp"
#include "utils/logging.hpp"

using namespace speechrnt;

/**
 * Example demonstrating how to integrate real Marian MT with UtteranceManager
 * 
 * This example shows:
 * 1. How to create and configure a MarianTranslator engine
 * 2. How to connect it to the UtteranceManager alongside STT
 * 3. How to process audio through the complete STT → MT pipeline
 * 4. How to handle different language pairs and configurations
 */

int main() {
    std::cout << "MT Integration Example" << std::endl;
    std::cout << "=====================" << std::endl;
    
    try {
        // Initialize logging
        utils::Logger::initialize();
        
        // Step 1: Create task queue and thread pool for processing
        auto task_queue = std::make_shared<core::TaskQueue>();
        auto thread_pool = std::make_shared<core::ThreadPool>(4);
        thread_pool->start(task_queue);
        
        std::cout << "✓ Task queue and thread pool initialized" << std::endl;
        
        // Step 2: Create and configure UtteranceManager
        core::UtteranceManagerConfig config;
        config.max_concurrent_utterances = 5;
        config.utterance_timeout = std::chrono::seconds(60);
        config.cleanup_interval = std::chrono::seconds(10);
        config.enable_automatic_cleanup = true;
        
        auto utterance_manager = std::make_shared<core::UtteranceManager>(config);
        utterance_manager->initialize(task_queue);
        
        std::cout << "✓ UtteranceManager initialized" << std::endl;
        
        // Step 3: Create and initialize STT engine
        auto stt_engine = std::make_shared<stt::WhisperSTT>();
        
        std::string whisper_model_path = "data/whisper/ggml-base.bin";
        bool stt_initialized = false;
        
        std::cout << "Attempting to initialize Whisper STT..." << std::endl;
        
        if (stt_engine->initialize(whisper_model_path, 4)) {
            stt_initialized = true;
            std::cout << "✓ Whisper STT initialized with CPU backend" << std::endl;
        } else {
            std::cout << "✗ STT initialization failed: " << stt_engine->getLastError() << std::endl;
            std::cout << "→ Continuing with STT simulation mode" << std::endl;
        }
        
        // Configure STT engine
        stt_engine->setLanguage("en");
        stt_engine->setConfidenceThreshold(0.5f);
        stt_engine->setPartialResultsEnabled(false);
        
        // Connect STT engine to UtteranceManager
        utterance_manager->setSTTEngine(stt_engine);
        std::cout << "✓ STT engine connected to UtteranceManager" << std::endl;
        
        // Step 4: Create and initialize MT engine
        auto mt_engine = std::make_shared<mt::MarianTranslator>();
        
        bool mt_initialized = false;
        
        std::cout << "Attempting to initialize Marian MT..." << std::endl;
        
        // Try to initialize with English to Spanish translation
        if (mt_engine->initialize("en", "es")) {
            mt_initialized = true;
            std::cout << "✓ Marian MT initialized for English → Spanish" << std::endl;
        } else {
            std::cout << "✗ MT initialization failed" << std::endl;
            std::cout << "→ Continuing with MT simulation mode" << std::endl;
        }
        
        // Connect MT engine to UtteranceManager
        utterance_manager->setMTEngine(mt_engine);
        std::cout << "✓ MT engine connected to UtteranceManager" << std::endl;
        
        // Step 5: Set up callbacks to monitor progress
        bool processing_complete = false;
        std::string final_transcript;
        std::string final_translation;
        
        utterance_manager->setStateChangeCallback([](const core::UtteranceData& utterance) {
            const char* state_names[] = {"CREATED", "TRANSCRIBING", "TRANSLATING", "SYNTHESIZING", "COMPLETE", "ERROR"};
            std::cout << "  State: " << state_names[static_cast<int>(utterance.state)] << std::endl;
        });
        
        utterance_manager->setCompleteCallback([&](const core::UtteranceData& utterance) {
            std::cout << "✓ Processing completed!" << std::endl;
            std::cout << "  Original: \"" << utterance.transcript << "\"" << std::endl;
            std::cout << "  Translation: \"" << utterance.translation << "\"" << std::endl;
            std::cout << "  Language: " << utterance.source_language << " → " << utterance.target_language << std::endl;
            std::cout << "  STT Confidence: " << utterance.transcription_confidence << std::endl;
            
            final_transcript = utterance.transcript;
            final_translation = utterance.translation;
            processing_complete = true;
        });
        
        utterance_manager->setErrorCallback([&](const core::UtteranceData& utterance, const std::string& error) {
            std::cout << "✗ Processing error: " << error << std::endl;
            processing_complete = true;
        });
        
        // Step 6: Test different language pairs
        std::cout << "\n=== Testing Language Pair Support ===" << std::endl;
        
        std::vector<std::pair<std::string, std::string>> test_pairs = {
            {"en", "es"}, // English to Spanish
            {"en", "fr"}, // English to French
            {"en", "de"}, // English to German
            {"es", "en"}, // Spanish to English
            {"fr", "en"}  // French to English
        };
        
        for (const auto& pair : test_pairs) {
            bool supported = mt_engine->supportsLanguagePair(pair.first, pair.second);
            std::cout << "  " << pair.first << " → " << pair.second << ": " 
                      << (supported ? "✓ Supported" : "✗ Not supported") << std::endl;
        }
        
        // Step 7: Create utterance and add audio data
        std::cout << "\n=== Processing Test Utterance ===" << std::endl;
        
        std::string session_id = "mt_example_session";
        uint32_t utterance_id = utterance_manager->createUtterance(session_id);
        
        if (utterance_id == 0) {
            std::cout << "✗ Failed to create utterance" << std::endl;
            return 1;
        }
        
        std::cout << "✓ Created utterance: " << utterance_id << std::endl;
        
        // Generate some test audio (in a real application, this would come from microphone)
        std::vector<float> audio_data;
        int sample_rate = 16000;
        float duration = 3.0f; // 3 seconds
        int num_samples = static_cast<int>(duration * sample_rate);
        
        // Generate a more complex waveform to simulate speech
        for (int i = 0; i < num_samples; ++i) {
            float t = static_cast<float>(i) / sample_rate;
            
            // Mix multiple frequencies to simulate speech formants
            float sample = 0.2f * std::sin(2.0f * M_PI * 200.0f * t) +  // Fundamental
                          0.15f * std::sin(2.0f * M_PI * 400.0f * t) +  // First formant
                          0.1f * std::sin(2.0f * M_PI * 800.0f * t) +   // Second formant
                          0.05f * std::sin(2.0f * M_PI * 1600.0f * t);  // Third formant
            
            // Add envelope to make it more speech-like
            float envelope = std::exp(-t * 0.3f) * (1.0f + 0.2f * std::sin(2.0f * M_PI * 3.0f * t));
            sample *= envelope;
            
            audio_data.push_back(sample);
        }
        
        std::cout << "✓ Generated " << audio_data.size() << " audio samples" << std::endl;
        
        // Add audio data to utterance
        if (!utterance_manager->addAudioData(utterance_id, audio_data)) {
            std::cout << "✗ Failed to add audio data" << std::endl;
            return 1;
        }
        
        std::cout << "✓ Added audio data to utterance" << std::endl;
        
        // Step 8: Configure language settings
        utterance_manager->setLanguageConfig(utterance_id, "en", "es", "default_voice");
        std::cout << "✓ Set language configuration (English to Spanish)" << std::endl;
        
        // Step 9: Start processing
        if (!utterance_manager->processUtterance(utterance_id)) {
            std::cout << "✗ Failed to start processing" << std::endl;
            return 1;
        }
        
        std::cout << "✓ Started processing utterance" << std::endl;
        std::cout << "\nWaiting for STT → MT processing to complete..." << std::endl;
        
        // Step 10: Wait for completion
        auto start_time = std::chrono::steady_clock::now();
        auto timeout = std::chrono::seconds(60);
        
        while (!processing_complete) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed > timeout) {
                std::cout << "✗ Processing timeout" << std::endl;
                break;
            }
        }
        
        // Step 11: Display results
        std::cout << "\n=== Results ===" << std::endl;
        
        auto final_utterance = utterance_manager->getUtterance(utterance_id);
        if (final_utterance) {
            std::cout << "Source language: " << final_utterance->source_language << std::endl;
            std::cout << "Target language: " << final_utterance->target_language << std::endl;
            std::cout << "Original transcript: \"" << final_utterance->transcript << "\"" << std::endl;
            std::cout << "STT confidence: " << final_utterance->transcription_confidence << std::endl;
            std::cout << "Translation: \"" << final_utterance->translation << "\"" << std::endl;
            
            if (!final_utterance->error_message.empty()) {
                std::cout << "Error: " << final_utterance->error_message << std::endl;
            }
        }
        
        // Step 12: Display statistics
        auto stats = utterance_manager->getStatistics();
        std::cout << "\n=== Statistics ===" << std::endl;
        std::cout << "Total utterances: " << stats.total_utterances << std::endl;
        std::cout << "Completed: " << stats.completed_utterances << std::endl;
        std::cout << "Errors: " << stats.error_utterances << std::endl;
        std::cout << "Average processing time: " << stats.average_processing_time.count() << "ms" << std::endl;
        
        // Step 13: Test language pair switching
        std::cout << "\n=== Testing Language Pair Switching ===" << std::endl;
        
        if (mt_engine->supportsLanguagePair("en", "fr")) {
            std::cout << "Testing English to French translation..." << std::endl;
            
            if (mt_engine->initialize("en", "fr")) {
                std::cout << "✓ Successfully switched to English → French" << std::endl;
                
                // Test direct translation
                auto result = mt_engine->translate("Hello, how are you?");
                if (result.success) {
                    std::cout << "  Direct translation: \"" << result.translatedText << "\"" << std::endl;
                    std::cout << "  Confidence: " << result.confidence << std::endl;
                } else {
                    std::cout << "  Translation failed: " << result.errorMessage << std::endl;
                }
            } else {
                std::cout << "✗ Failed to switch to English → French" << std::endl;
            }
        } else {
            std::cout << "English → French not supported" << std::endl;
        }
        
        // Cleanup
        thread_pool->stop();
        utterance_manager->shutdown();
        
        std::cout << "\n✓ Example completed successfully!" << std::endl;
        
        if (stt_initialized) {
            std::cout << "  Real Whisper STT was used for transcription" << std::endl;
        } else {
            std::cout << "  STT simulation mode was used (real model not available)" << std::endl;
        }
        
        if (mt_initialized) {
            std::cout << "  Real Marian MT was used for translation" << std::endl;
        } else {
            std::cout << "  MT simulation mode was used (real model not available)" << std::endl;
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Example failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Example failed with unknown exception" << std::endl;
        return 1;
    }
}