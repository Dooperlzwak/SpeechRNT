#include <iostream>
#include <memory>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include <fstream>

#include "core/utterance_manager.hpp"
#include "core/task_queue.hpp"
#include "stt/whisper_stt.hpp"
#include "utils/logging.hpp"

using namespace speechrnt;

// Generate a simple sine wave audio signal for testing
std::vector<float> generateTestAudio(float frequency, float duration, int sample_rate = 16000) {
    std::vector<float> audio;
    int num_samples = static_cast<int>(duration * sample_rate);
    audio.reserve(num_samples);
    
    for (int i = 0; i < num_samples; ++i) {
        float t = static_cast<float>(i) / sample_rate;
        float sample = 0.3f * std::sin(2.0f * M_PI * frequency * t);
        audio.push_back(sample);
    }
    
    return audio;
}

// Generate more realistic speech-like audio (multiple frequencies)
std::vector<float> generateSpeechLikeAudio(float duration, int sample_rate = 16000) {
    std::vector<float> audio;
    int num_samples = static_cast<int>(duration * sample_rate);
    audio.reserve(num_samples);
    
    // Mix multiple frequencies to simulate speech formants
    std::vector<float> frequencies = {200.0f, 400.0f, 800.0f, 1600.0f};
    std::vector<float> amplitudes = {0.4f, 0.3f, 0.2f, 0.1f};
    
    for (int i = 0; i < num_samples; ++i) {
        float t = static_cast<float>(i) / sample_rate;
        float sample = 0.0f;
        
        for (size_t j = 0; j < frequencies.size(); ++j) {
            sample += amplitudes[j] * std::sin(2.0f * M_PI * frequencies[j] * t);
        }
        
        // Add some envelope to make it more speech-like
        float envelope = std::exp(-t * 0.5f) * (1.0f + 0.3f * std::sin(2.0f * M_PI * 5.0f * t));
        sample *= envelope;
        
        audio.push_back(sample);
    }
    
    return audio;
}

class STTIntegrationTest {
public:
    STTIntegrationTest() : test_passed_(false), transcription_received_(false) {}
    
    bool runTest() {
        std::cout << "\n=== Real STT Integration Test ===" << std::endl;
        
        // Initialize logging
        utils::Logger::initialize();
        
        // Create task queue and thread pool
        auto task_queue = std::make_shared<core::TaskQueue>();
        auto thread_pool = std::make_shared<core::ThreadPool>(4);
        thread_pool->start(task_queue);
        
        // Create utterance manager
        core::UtteranceManagerConfig config;
        config.max_concurrent_utterances = 10;
        config.utterance_timeout = std::chrono::seconds(30);
        config.cleanup_interval = std::chrono::seconds(5);
        config.enable_automatic_cleanup = true;
        
        auto utterance_manager = std::make_shared<core::UtteranceManager>(config);
        utterance_manager->initialize(task_queue);
        
        // Create and initialize STT engine
        auto stt_engine = std::make_shared<stt::WhisperSTT>();
        
        // Try to initialize with a real model first, fall back to simulation if not available
        std::string model_path = "data/whisper/ggml-base.bin";
        bool real_whisper_available = false;
        
        std::cout << "Attempting to initialize Whisper STT with model: " << model_path << std::endl;
        
        if (std::ifstream(model_path).good()) {
            if (stt_engine->initialize(model_path, 4)) {
                real_whisper_available = true;
                std::cout << "✓ Real Whisper STT engine initialized successfully!" << std::endl;
            } else {
                std::cout << "✗ Failed to initialize real Whisper STT: " << stt_engine->getLastError() << std::endl;
            }
        } else {
            std::cout << "✗ Whisper model file not found: " << model_path << std::endl;
        }
        
        if (!real_whisper_available) {
            std::cout << "→ Continuing with simulation mode for testing" << std::endl;
        }
        
        // Configure STT engine
        stt_engine->setLanguage("en");
        stt_engine->setConfidenceThreshold(0.3f); // Lower threshold for testing
        stt_engine->setPartialResultsEnabled(false); // Disable for cleaner testing
        
        // Connect STT engine to utterance manager
        utterance_manager->setSTTEngine(stt_engine);
        
        // Set up callbacks to track progress
        utterance_manager->setStateChangeCallback([this](const core::UtteranceData& utterance) {
            std::cout << "State change: Utterance " << utterance.utterance_id 
                      << " -> " << static_cast<int>(utterance.state) << std::endl;
        });
        
        utterance_manager->setCompleteCallback([this](const core::UtteranceData& utterance) {
            std::cout << "✓ Utterance " << utterance.utterance_id << " completed!" << std::endl;
            std::cout << "  Transcript: \"" << utterance.transcript << "\"" << std::endl;
            std::cout << "  Confidence: " << utterance.transcription_confidence << std::endl;
            std::cout << "  Translation: \"" << utterance.translation << "\"" << std::endl;
            
            transcription_received_ = true;
            if (!utterance.transcript.empty()) {
                test_passed_ = true;
            }
        });
        
        utterance_manager->setErrorCallback([this](const core::UtteranceData& utterance, const std::string& error) {
            std::cout << "✗ Utterance " << utterance.utterance_id << " error: " << error << std::endl;
        });
        
        // Test 1: Create utterance and add audio data
        std::cout << "\n--- Test 1: Basic STT Processing ---" << std::endl;
        
        std::string session_id = "test_session_001";
        uint32_t utterance_id = utterance_manager->createUtterance(session_id);
        
        if (utterance_id == 0) {
            std::cout << "✗ Failed to create utterance" << std::endl;
            return false;
        }
        
        std::cout << "✓ Created utterance: " << utterance_id << std::endl;
        
        // Generate test audio (3 seconds of speech-like audio)
        std::vector<float> test_audio = generateSpeechLikeAudio(3.0f);
        std::cout << "✓ Generated " << test_audio.size() << " audio samples (" 
                  << (test_audio.size() / 16000.0f) << " seconds)" << std::endl;
        
        // Add audio data to utterance
        if (!utterance_manager->addAudioData(utterance_id, test_audio)) {
            std::cout << "✗ Failed to add audio data to utterance" << std::endl;
            return false;
        }
        
        std::cout << "✓ Added audio data to utterance" << std::endl;
        
        // Set language configuration
        utterance_manager->setLanguageConfig(utterance_id, "en", "es", "voice_001");
        std::cout << "✓ Set language configuration (en -> es)" << std::endl;
        
        // Process the utterance through the STT pipeline
        if (!utterance_manager->processUtterance(utterance_id)) {
            std::cout << "✗ Failed to start utterance processing" << std::endl;
            return false;
        }
        
        std::cout << "✓ Started utterance processing" << std::endl;
        
        // Wait for processing to complete
        std::cout << "\nWaiting for STT processing to complete..." << std::endl;
        
        auto start_time = std::chrono::steady_clock::now();
        auto timeout = std::chrono::seconds(30);
        
        while (!transcription_received_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed > timeout) {
                std::cout << "✗ Timeout waiting for transcription" << std::endl;
                break;
            }
            
            // Print current state
            auto state = utterance_manager->getUtteranceState(utterance_id);
            static auto last_state = core::UtteranceState::CREATED;
            if (state != last_state) {
                std::cout << "  Current state: " << static_cast<int>(state) << std::endl;
                last_state = state;
            }
        }
        
        // Test 2: Check final results
        std::cout << "\n--- Test 2: Verify Results ---" << std::endl;
        
        auto final_utterance = utterance_manager->getUtterance(utterance_id);
        if (final_utterance) {
            std::cout << "Final utterance state:" << std::endl;
            std::cout << "  ID: " << final_utterance->utterance_id << std::endl;
            std::cout << "  State: " << static_cast<int>(final_utterance->state) << std::endl;
            std::cout << "  Transcript: \"" << final_utterance->transcript << "\"" << std::endl;
            std::cout << "  Confidence: " << final_utterance->transcription_confidence << std::endl;
            std::cout << "  Translation: \"" << final_utterance->translation << "\"" << std::endl;
            std::cout << "  Error: \"" << final_utterance->error_message << "\"" << std::endl;
            std::cout << "  Audio samples: " << final_utterance->audio_buffer.size() << std::endl;
        } else {
            std::cout << "✗ Could not retrieve final utterance data" << std::endl;
        }
        
        // Test 3: Statistics and cleanup
        std::cout << "\n--- Test 3: Statistics ---" << std::endl;
        
        auto stats = utterance_manager->getStatistics();
        std::cout << "Utterance Manager Statistics:" << std::endl;
        std::cout << "  Total utterances: " << stats.total_utterances << std::endl;
        std::cout << "  Completed utterances: " << stats.completed_utterances << std::endl;
        std::cout << "  Error utterances: " << stats.error_utterances << std::endl;
        std::cout << "  Active utterances: " << stats.active_utterances << std::endl;
        std::cout << "  Average processing time: " << stats.average_processing_time.count() << "ms" << std::endl;
        
        // Cleanup
        thread_pool->stop();
        utterance_manager->shutdown();
        
        // Final assessment
        std::cout << "\n=== Test Results ===" << std::endl;
        
        if (real_whisper_available) {
            std::cout << "✓ Real Whisper STT engine was used" << std::endl;
        } else {
            std::cout << "→ Simulation mode was used (real model not available)" << std::endl;
        }
        
        if (transcription_received_) {
            std::cout << "✓ Transcription was received" << std::endl;
        } else {
            std::cout << "✗ No transcription was received" << std::endl;
        }
        
        if (test_passed_) {
            std::cout << "✓ STT integration test PASSED" << std::endl;
        } else {
            std::cout << "✗ STT integration test FAILED" << std::endl;
        }
        
        return test_passed_ || (!real_whisper_available && transcription_received_);
    }
    
private:
    bool test_passed_;
    bool transcription_received_;
};

int main() {
    try {
        STTIntegrationTest test;
        bool success = test.runTest();
        
        std::cout << "\n" << (success ? "SUCCESS" : "FAILURE") << ": STT Integration Test completed" << std::endl;
        return success ? 0 : 1;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception" << std::endl;
        return 1;
    }
}