#include <iostream>
#include <memory>
#include <vector>
#include <chrono>
#include <thread>

#include "core/utterance_manager.hpp"
#include "core/task_queue.hpp"
#include "stt/whisper_stt.hpp"
#include "utils/logging.hpp"

using namespace speechrnt;

/**
 * Example demonstrating how to integrate real Whisper STT with UtteranceManager
 * 
 * This example shows:
 * 1. How to create and configure a WhisperSTT engine
 * 2. How to connect it to the UtteranceManager
 * 3. How to process audio through the STT pipeline
 * 4. How to handle results and errors
 */

int main() {
    std::cout << "STT Integration Example" << std::endl;
    std::cout << "======================" << std::endl;
    
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
        config.utterance_timeout = std::chrono::seconds(30);
        config.cleanup_interval = std::chrono::seconds(10);
        config.enable_automatic_cleanup = true;
        
        auto utterance_manager = std::make_shared<core::UtteranceManager>(config);
        utterance_manager->initialize(task_queue);
        
        std::cout << "✓ UtteranceManager initialized" << std::endl;
        
        // Step 3: Create and initialize STT engine
        auto stt_engine = std::make_shared<stt::WhisperSTT>();
        
        // Try to initialize with a real Whisper model
        std::string model_path = "data/whisper/ggml-base.bin";
        bool initialized = false;
        
        std::cout << "Attempting to initialize Whisper STT..." << std::endl;
        
        // Try CPU initialization first
        if (stt_engine->initialize(model_path, 4)) {
            initialized = true;
            std::cout << "✓ Whisper STT initialized with CPU backend" << std::endl;
        } else {
            std::cout << "✗ CPU initialization failed: " << stt_engine->getLastError() << std::endl;
            
            // Try GPU initialization if CPU failed
            if (stt_engine->initializeWithGPU(model_path, 0, 4)) {
                initialized = true;
                std::cout << "✓ Whisper STT initialized with GPU backend" << std::endl;
            } else {
                std::cout << "✗ GPU initialization failed: " << stt_engine->getLastError() << std::endl;
            }
        }
        
        if (!initialized) {
            std::cout << "→ Continuing with simulation mode" << std::endl;
        }
        
        // Step 4: Configure STT engine
        stt_engine->setLanguage("en");
        stt_engine->setConfidenceThreshold(0.5f);
        stt_engine->setPartialResultsEnabled(false);
        stt_engine->setWordLevelConfidenceEnabled(true);
        stt_engine->setLanguageDetectionEnabled(true);
        
        std::cout << "✓ STT engine configured" << std::endl;
        
        // Step 5: Connect STT engine to UtteranceManager
        utterance_manager->setSTTEngine(stt_engine);
        
        std::cout << "✓ STT engine connected to UtteranceManager" << std::endl;
        
        // Step 6: Set up callbacks to monitor progress
        bool processing_complete = false;
        std::string final_transcript;
        
        utterance_manager->setStateChangeCallback([](const core::UtteranceData& utterance) {
            const char* state_names[] = {"CREATED", "TRANSCRIBING", "TRANSLATING", "SYNTHESIZING", "COMPLETE", "ERROR"};
            std::cout << "  State: " << state_names[static_cast<int>(utterance.state)] << std::endl;
        });
        
        utterance_manager->setCompleteCallback([&](const core::UtteranceData& utterance) {
            std::cout << "✓ Processing completed!" << std::endl;
            std::cout << "  Transcript: \"" << utterance.transcript << "\"" << std::endl;
            std::cout << "  Confidence: " << utterance.transcription_confidence << std::endl;
            final_transcript = utterance.transcript;
            processing_complete = true;
        });
        
        utterance_manager->setErrorCallback([&](const core::UtteranceData& utterance, const std::string& error) {
            std::cout << "✗ Processing error: " << error << std::endl;
            processing_complete = true;
        });
        
        // Step 7: Create utterance and add audio data
        std::string session_id = "example_session";
        uint32_t utterance_id = utterance_manager->createUtterance(session_id);
        
        if (utterance_id == 0) {
            std::cout << "✗ Failed to create utterance" << std::endl;
            return 1;
        }
        
        std::cout << "✓ Created utterance: " << utterance_id << std::endl;
        
        // Generate some test audio (in a real application, this would come from microphone)
        std::vector<float> audio_data;
        int sample_rate = 16000;
        float duration = 2.0f; // 2 seconds
        int num_samples = static_cast<int>(duration * sample_rate);
        
        // Generate a simple sine wave (in reality, this would be speech audio)
        for (int i = 0; i < num_samples; ++i) {
            float t = static_cast<float>(i) / sample_rate;
            float sample = 0.3f * std::sin(2.0f * M_PI * 440.0f * t); // 440 Hz tone
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
        std::cout << "\nWaiting for processing to complete..." << std::endl;
        
        // Step 10: Wait for completion
        auto start_time = std::chrono::steady_clock::now();
        auto timeout = std::chrono::seconds(30);
        
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
            std::cout << "Final transcript: \"" << final_utterance->transcript << "\"" << std::endl;
            std::cout << "Confidence: " << final_utterance->transcription_confidence << std::endl;
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
        
        // Cleanup
        thread_pool->stop();
        utterance_manager->shutdown();
        
        std::cout << "\n✓ Example completed successfully!" << std::endl;
        
        if (initialized) {
            std::cout << "  Real Whisper STT was used for transcription" << std::endl;
        } else {
            std::cout << "  Simulation mode was used (real model not available)" << std::endl;
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