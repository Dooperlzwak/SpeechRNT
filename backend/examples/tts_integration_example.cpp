#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "core/task_queue.hpp"
#include "core/utterance_manager.hpp"
#include "mt/marian_translator.hpp"
#include "stt/whisper_stt.hpp"
#include "tts/piper_tts.hpp"
#include "utils/logging.hpp"

using namespace speechrnt;

/**
 * Comprehensive example showing how to integrate TTS into the speech
 * translation pipeline Demonstrates best practices for TTS setup,
 * configuration, and usage
 */
class TTSIntegrationExample {
public:
  TTSIntegrationExample() {
    std::cout << "=== TTS Integration Example ===" << std::endl;
    std::cout << "This example demonstrates how to integrate Piper TTS into "
                 "the speech translation pipeline."
              << std::endl;
    std::cout << std::endl;
  }

  void runExample() {
    // Step 1: Initialize core components
    initializeComponents();

    // Step 2: Set up TTS engine
    setupTTSEngine();

    // Step 3: Demonstrate voice management
    demonstrateVoiceManagement();

    // Step 4: Test direct TTS synthesis
    testDirectSynthesis();

    // Step 5: Test complete pipeline integration
    testPipelineIntegration();

    // Step 6: Demonstrate advanced features
    demonstrateAdvancedFeatures();

    // Step 7: Show error handling patterns
    demonstrateErrorHandling();

    // Cleanup
    cleanup();

    std::cout << "\n🎉 TTS Integration Example completed!" << std::endl;
  }

private:
  void initializeComponents() {
    std::cout << "\n--- Step 1: Initialize Core Components ---" << std::endl;

    // Create task queue for asynchronous processing
    task_queue_ = std::make_shared<core::TaskQueue>(4);
    task_queue_->start();
    std::cout << "✅ Task queue initialized with 4 worker threads" << std::endl;

    // Create utterance manager
    core::UtteranceManagerConfig config;
    config.max_concurrent_utterances = 5;
    config.utterance_timeout = std::chrono::seconds(30);
    utterance_manager_ = std::make_unique<core::UtteranceManager>(config);
    utterance_manager_->initialize(task_queue_);
    std::cout << "✅ Utterance manager initialized" << std::endl;

    // Set up callbacks for monitoring
    utterance_manager_->setStateChangeCallback(
        [](const core::UtteranceData &utterance) {
          std::cout << "📊 Utterance " << utterance.id << " state changed to: "
                    << static_cast<int>(utterance.state) << std::endl;
        });

    utterance_manager_->setCompleteCallback(
        [](const core::UtteranceData &utterance) {
          std::cout << "✅ Utterance " << utterance.id
                    << " completed successfully" << std::endl;
        });

    utterance_manager_->setErrorCallback(
        [](const core::UtteranceData &utterance, const std::string &error) {
          std::cout << "❌ Utterance " << utterance.id << " failed: " << error
                    << std::endl;
        });
  }

  void setupTTSEngine() {
    std::cout << "\n--- Step 2: Set Up TTS Engine ---" << std::endl;

    // Create TTS engine
    tts_engine_ = tts::createPiperTTS();
    if (!tts_engine_) {
      std::cout << "❌ Failed to create TTS engine" << std::endl;
      return;
    }
    std::cout << "✅ TTS engine created" << std::endl;

    // Initialize with model path
    std::string model_path = "models/tts/piper";
    std::cout << "🔄 Initializing TTS engine with model path: " << model_path
              << std::endl;

    if (tts_engine_->initialize(model_path)) {
      std::cout << "✅ TTS engine initialized with real Piper models"
                << std::endl;
    } else {
      std::cout
          << "⚠️  Real Piper models not available, using mock implementation"
          << std::endl;
      std::cout
          << "   This is normal for development environments without TTS models"
          << std::endl;
    }

    // Check engine status
    if (tts_engine_->isReady()) {
      std::cout << "✅ TTS engine is ready for synthesis" << std::endl;
    } else {
      std::cout << "❌ TTS engine is not ready: " << tts_engine_->getLastError()
                << std::endl;
      return;
    }

    // Connect TTS engine to utterance manager
    utterance_manager_->setTTSEngine(tts_engine_);
    std::cout << "✅ TTS engine connected to utterance manager" << std::endl;
  }

  void demonstrateVoiceManagement() {
    std::cout << "\n--- Step 3: Voice Management ---" << std::endl;

    // Get available voices
    auto voices = tts_engine_->getAvailableVoices();
    std::cout << "📢 Available voices (" << voices.size()
              << " total):" << std::endl;

    for (const auto &voice : voices) {
      std::cout << "  - " << voice.id << ": " << voice.name << " ("
                << voice.language << ", " << voice.gender << ")" << std::endl;
      std::cout << "    Description: " << voice.description << std::endl;
      std::cout << "    Available: " << (voice.isAvailable ? "Yes" : "No")
                << std::endl;
    }

    if (voices.empty()) {
      std::cout << "⚠️  No voices available" << std::endl;
      return;
    }

    // Test language-specific voice queries
    std::vector<std::string> test_languages = {"en", "es", "fr", "de"};
    for (const auto &lang : test_languages) {
      auto lang_voices = tts_engine_->getVoicesForLanguage(lang);
      std::cout << "🌐 Voices for " << lang << ": " << lang_voices.size()
                << std::endl;
      for (const auto &voice : lang_voices) {
        std::cout << "    " << voice.id << " (" << voice.name << ", "
                  << voice.gender << ")" << std::endl;
      }
    }

    // Set default voice
    if (!voices.empty()) {
      std::string default_voice = voices[0].id;
      if (tts_engine_->setDefaultVoice(default_voice)) {
        std::cout << "✅ Default voice set to: " << default_voice << std::endl;
      } else {
        std::cout << "❌ Failed to set default voice" << std::endl;
      }

      std::cout << "🎤 Current default voice: "
                << tts_engine_->getDefaultVoice() << std::endl;
    }
  }

  void testDirectSynthesis() {
    std::cout << "\n--- Step 4: Direct TTS Synthesis ---" << std::endl;

    // Test basic synthesis
    std::string test_text =
        "Hello, this is a test of the text-to-speech synthesis system.";
    \n std::cout << "🔊 Synthesizing: \"" << test_text << "\"" << std::endl;

    auto start_time = std::chrono::steady_clock::now();
    auto result = tts_engine_->synthesize(test_text);
    auto end_time = std::chrono::steady_clock::now();

    auto synthesis_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (result.success) {
      std::cout << "✅ Synthesis successful!" << std::endl;
      std::cout << "   Audio data: " << result.audioData.size() << " bytes"
                << std::endl;
      std::cout << "   Duration: " << result.duration << " seconds"
                << std::endl;
      std::cout << "   Sample rate: " << result.sampleRate << " Hz"
                << std::endl;
      std::cout << "   Channels: " << result.channels << std::endl;
      std::cout << "   Voice used: " << result.voiceId << std::endl;
      std::cout << "   Synthesis time: " << synthesis_time.count() << " ms"
                << std::endl;

      // Save audio to file for testing
      saveAudioToFile(result.audioData, "test_synthesis.wav");
    } else {
      std::cout << "❌ Synthesis failed: " << result.errorMessage << std::endl;
    }

    // Test synthesis with specific voice
    auto voices = tts_engine_->getAvailableVoices();
    if (voices.size() > 1) {
      std::cout << "\n🎤 Testing with specific voice: " << voices[1].id
                << std::endl;
      auto voice_result = tts_engine_->synthesize(test_text, voices[1].id);

      if (voice_result.success) {
        std::cout << "✅ Voice-specific synthesis successful ("
                  << voice_result.audioData.size() << " bytes)" << std::endl;
      } else {
        std::cout << "❌ Voice-specific synthesis failed: "
                  << voice_result.errorMessage << std::endl;
      }
    }

    // Test asynchronous synthesis
    std::cout << "\n⏳ Testing asynchronous synthesis..." << std::endl;
    auto future_result =
        tts_engine_->synthesizeAsync("This is an asynchronous synthesis test.");

    // Do other work while synthesis is running
    std::cout << "🔄 Doing other work while synthesis runs..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Get result
    auto async_result = future_result.get();
    if (async_result.success) {
      std::cout << "✅ Asynchronous synthesis successful ("
                << async_result.audioData.size() << " bytes)" << std::endl;
    } else {
      std::cout << "❌ Asynchronous synthesis failed: "
                << async_result.errorMessage << std::endl;
    }

    // Test callback-based synthesis
    std::cout << "\n📞 Testing callback-based synthesis..." << std::endl;
    bool callback_completed = false;

    tts_engine_->synthesizeWithCallback(
        "This is a callback-based synthesis test.",
        [&callback_completed](const tts::SynthesisResult &result) {
          if (result.success) {
            std::cout << "✅ Callback synthesis successful ("
                      << result.audioData.size() << " bytes)" << std::endl;
          } else {
            std::cout << "❌ Callback synthesis failed: " << result.errorMessage
                      << std::endl;
          }
          callback_completed = true;
        });

    // Wait for callback
    auto callback_start = std::chrono::steady_clock::now();
    while (!callback_completed &&
           std::chrono::steady_clock::now() - callback_start <
               std::chrono::seconds(5)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!callback_completed) {
      std::cout << "⚠️  Callback synthesis timed out" << std::endl;
    }
  }

  void testPipelineIntegration() {
    std::cout << "\n--- Step 5: Complete Pipeline Integration ---" << std::endl;

    // Set up complete pipeline with all engines
    setupCompletePipeline();

    // Create test utterance
    uint32_t utterance_id =
        utterance_manager_->createUtterance("tts_example_session");
    if (utterance_id == 0) {
      std::cout << "❌ Failed to create utterance" << std::endl;
      return;
    }
    std::cout << "✅ Created utterance: " << utterance_id << std::endl;

    // Configure language settings
    utterance_manager_->setLanguageConfig(utterance_id, "en", "es",
                                          "es_female_1");
    std::cout << "✅ Language configuration set: en → es (voice: es_female_1)"
              << std::endl;

    // Add mock audio data
    std::vector<float> audio_data(16000 * 2, 0.1f); // 2 seconds of audio
    utterance_manager_->addAudioData(utterance_id, audio_data);
    std::cout << "✅ Audio data added: " << audio_data.size() << " samples"
              << std::endl;

    // Process through complete pipeline
    std::cout << "🔄 Starting complete pipeline processing..." << std::endl;
    if (!utterance_manager_->processUtterance(utterance_id)) {
      std::cout << "❌ Failed to start utterance processing" << std::endl;
      return;
    }

    // Monitor progress
    auto start_time = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(15);
    core::UtteranceState last_state = core::UtteranceState::LISTENING;

    while (std::chrono::steady_clock::now() - start_time < timeout) {
      auto current_state = utterance_manager_->getUtteranceState(utterance_id);

      if (current_state != last_state) {
        std::cout << "📊 Pipeline state: " << stateToString(current_state)
                  << std::endl;
        last_state = current_state;
      }

      if (current_state == core::UtteranceState::COMPLETE) {
        std::cout << "✅ Pipeline processing completed!" << std::endl;
        break;
      } else if (current_state == core::UtteranceState::ERROR) {
        auto utterance = utterance_manager_->getUtterance(utterance_id);
        std::cout << "❌ Pipeline processing failed: "
                  << utterance->error_message << std::endl;
        return;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Check final results
    auto final_utterance = utterance_manager_->getUtterance(utterance_id);
    if (final_utterance &&
        final_utterance->state == core::UtteranceState::COMPLETE) {
      std::cout << "\n📋 Final Results:" << std::endl;
      std::cout << "   Transcript: \"" << final_utterance->transcript << "\""
                << std::endl;
      std::cout << "   Translation: \"" << final_utterance->translation << "\""
                << std::endl;
      std::cout << "   Synthesized audio: "
                << final_utterance->synthesized_audio.size() << " bytes"
                << std::endl;
      std::cout << "   Voice used: " << final_utterance->voice_id << std::endl;

      // Save final audio
      if (!final_utterance->synthesized_audio.empty()) {
        saveAudioToFile(final_utterance->synthesized_audio,
                        "pipeline_result.wav");
      }
    } else {
      std::cout << "❌ Pipeline did not complete successfully" << std::endl;
    }
  }

  void demonstrateAdvancedFeatures() {
    std::cout << "\n--- Step 6: Advanced TTS Features ---" << std::endl;

    // Test synthesis parameters
    std::cout << "🎛️  Testing synthesis parameters..." << std::endl;

    std::string test_text =
        "This text will be synthesized with different parameters.";

    // Test different speeds
    std::vector<float> speeds = {0.5f, 1.0f, 1.5f};
    for (float speed : speeds) {
      std::cout << "🏃 Testing speed: " << speed << "x" << std::endl;
      tts_engine_->setSynthesisParameters(speed, 0.0f, 1.0f);

      auto result = tts_engine_->synthesize(test_text);
      if (result.success) {
        std::cout << "  ✅ Speed " << speed << "x successful ("
                  << result.duration << "s duration)" << std::endl;
      } else {
        std::cout << "  ❌ Speed " << speed
                  << "x failed: " << result.errorMessage << std::endl;
      }
    }

    // Test different volumes
    std::vector<float> volumes = {0.3f, 0.7f, 1.0f};
    for (float volume : volumes) {
      std::cout << "🔊 Testing volume: " << (volume * 100) << "%" << std::endl;
      tts_engine_->setSynthesisParameters(1.0f, 0.0f, volume);

      auto result = tts_engine_->synthesize(test_text);
      if (result.success) {
        std::cout << "  ✅ Volume " << (volume * 100) << "% successful"
                  << std::endl;
      } else {
        std::cout << "  ❌ Volume " << (volume * 100)
                  << "% failed: " << result.errorMessage << std::endl;
      }
    }

    // Reset to default parameters
    tts_engine_->setSynthesisParameters(1.0f, 0.0f, 1.0f);
    std::cout << "🔄 Reset to default synthesis parameters" << std::endl;
  }

  void demonstrateErrorHandling() {
    std::cout << "\n--- Step 7: Error Handling Patterns ---" << std::endl;

    // Test empty text
    std::cout << "🧪 Testing empty text handling..." << std::endl;
    auto empty_result = tts_engine_->synthesize("");
    if (!empty_result.success) {
      std::cout << "✅ Empty text properly rejected: "
                << empty_result.errorMessage << std::endl;
    } else {
      std::cout << "⚠️  Empty text was processed (may be acceptable)"
                << std::endl;
    }

    // Test very long text
    std::cout << "🧪 Testing very long text handling..." << std::endl;
    std::string long_text(10000, 'A'); // 10k characters
    auto long_result = tts_engine_->synthesize(long_text);
    if (long_result.success) {
      std::cout << "✅ Long text processed successfully ("
                << long_result.audioData.size() << " bytes)" << std::endl;
    } else {
      std::cout << "⚠️  Long text failed: " << long_result.errorMessage
                << std::endl;
    }

    // Test invalid voice ID
    std::cout << "🧪 Testing invalid voice ID handling..." << std::endl;
    auto invalid_voice_result =
        tts_engine_->synthesize("Test text", "invalid_voice_12345");
    if (!invalid_voice_result.success) {
      std::cout << "✅ Invalid voice ID properly rejected: "
                << invalid_voice_result.errorMessage << std::endl;
    } else {
      std::cout << "⚠️  Invalid voice ID was handled (fell back to default)"
                << std::endl;
    }

    // Test engine cleanup and reinitialization
    std::cout << "🧪 Testing engine cleanup and reinitialization..."
              << std::endl;
    tts_engine_->cleanup();

    auto after_cleanup_result = tts_engine_->synthesize("Test after cleanup");
    if (!after_cleanup_result.success) {
      std::cout << "✅ Synthesis properly failed after cleanup" << std::endl;
    } else {
      std::cout << "⚠️  Synthesis worked after cleanup (unexpected)"
                << std::endl;
    }

    // Reinitialize
    if (tts_engine_->initialize("models/tts/piper")) {
      std::cout << "✅ Engine reinitialized successfully" << std::endl;
    } else {
      std::cout << "⚠️  Engine reinitialization failed (using mock)"
                << std::endl;
    }
  }

  void setupCompletePipeline() {
    // Create and initialize STT engine
    auto stt_engine = std::make_shared<stt::WhisperSTT>();
    stt_engine->initialize("models/whisper/ggml-base.en.bin");
    utterance_manager_->setSTTEngine(stt_engine);

    // Create and initialize MT engine
    auto mt_engine = std::make_shared<mt::MarianTranslator>();
    mt_engine->initialize("en", "es");
    utterance_manager_->setMTEngine(mt_engine);

    // TTS engine is already set up
    std::cout << "✅ Complete pipeline configured (STT → MT → TTS)"
              << std::endl;
  }

  void saveAudioToFile(const std::vector<uint8_t> &audio_data,
                       const std::string &filename) {
    try {
      std::ofstream file(filename, std::ios::binary);
      if (file.is_open()) {
        file.write(reinterpret_cast<const char *>(audio_data.data()),
                   audio_data.size());
        file.close();
        std::cout << "💾 Audio saved to: " << filename << " ("
                  << audio_data.size() << " bytes)" << std::endl;
      } else {
        std::cout << "⚠️  Failed to save audio to: " << filename << std::endl;
      }
    } catch (const std::exception &e) {
      std::cout << "⚠️  Error saving audio: " << e.what() << std::endl;
    }
  }

  std::string stateToString(core::UtteranceState state) {
    switch (state) {
    case core::UtteranceState::LISTENING:
      return "LISTENING";
    case core::UtteranceState::TRANSCRIBING:
      return "TRANSCRIBING";
    case core::UtteranceState::TRANSLATING:
      return "TRANSLATING";
    case core::UtteranceState::SYNTHESIZING:
      return "SYNTHESIZING";
    case core::UtteranceState::COMPLETE:
      return "COMPLETE";
    case core::UtteranceState::ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
    }
  }

  void cleanup() {
    std::cout << "\n--- Cleanup ---" << std::endl;

    if (tts_engine_) {
      tts_engine_->cleanup();
      std::cout << "✅ TTS engine cleaned up" << std::endl;
    }

    if (utterance_manager_) {
      utterance_manager_->shutdown();
      std::cout << "✅ Utterance manager shut down" << std::endl;
    }

    if (task_queue_) {
      task_queue_->stop();
      std::cout << "✅ Task queue stopped" << std::endl;
    }
  }

private:
  std::shared_ptr<core::TaskQueue> task_queue_;
  std::unique_ptr<core::UtteranceManager> utterance_manager_;
  std::shared_ptr<tts::TTSInterface> tts_engine_;
};

int main() {
  std::cout << "Starting TTS Integration Example..." << std::endl;

  try {
    TTSIntegrationExample example;
    example.runExample();
    return 0;
  } catch (const std::exception &e) {
    std::cout << "💥 Example crashed: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cout << "💥 Example crashed with unknown exception!" << std::endl;
    return 1;
  }
}