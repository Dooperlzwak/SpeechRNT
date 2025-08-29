#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>
#include "tts/coqui_tts.hpp"

using namespace speechrnt::tts;

void testTTSInitialization() {
    std::cout << "Testing TTS initialization..." << std::endl;
    
    auto tts = createCoquiTTS();
    assert(tts != nullptr);
    assert(!tts->isReady());
    
    // Test initialization with mock model path
    bool success = tts->initialize("mock/model/path", "en_female_1");
    assert(success);
    assert(tts->isReady());
    
    std::cout << "✓ TTS initialization test passed" << std::endl;
}

void testVoiceManagement() {
    std::cout << "Testing voice management..." << std::endl;
    
    auto tts = createCoquiTTS();
    tts->initialize("mock/model/path");
    
    // Test getting available voices
    auto voices = tts->getAvailableVoices();
    assert(!voices.empty());
    std::cout << "Found " << voices.size() << " voices" << std::endl;
    
    // Test getting voices for specific language
    auto enVoices = tts->getVoicesForLanguage("en");
    assert(!enVoices.empty());
    std::cout << "Found " << enVoices.size() << " English voices" << std::endl;
    
    // Test setting default voice
    if (!voices.empty()) {
        bool success = tts->setDefaultVoice(voices[0].id);
        assert(success);
        assert(tts->getDefaultVoice() == voices[0].id);
    }
    
    std::cout << "✓ Voice management test passed" << std::endl;
}

void testSynthesis() {
    std::cout << "Testing speech synthesis..." << std::endl;
    
    auto tts = createCoquiTTS();
    tts->initialize("mock/model/path", "en_female_1");
    
    std::string testText = "Hello, this is a test of the speech synthesis system.";
    
    // Test synchronous synthesis
    auto result = tts->synthesize(testText);
    assert(result.success);
    assert(!result.audioData.empty());
    assert(result.duration > 0);
    assert(result.sampleRate > 0);
    assert(result.channels > 0);
    
    std::cout << "Synthesized " << testText.length() << " characters into " 
              << result.audioData.size() << " bytes of audio (" 
              << result.duration << "s)" << std::endl;
    
    std::cout << "✓ Speech synthesis test passed" << std::endl;
}

void testAsyncSynthesis() {
    std::cout << "Testing async speech synthesis..." << std::endl;
    
    auto tts = createCoquiTTS();
    tts->initialize("mock/model/path", "en_male_1");
    
    std::string testText = "This is an asynchronous synthesis test.";
    
    // Test asynchronous synthesis
    auto future = tts->synthesizeAsync(testText);
    auto result = future.get();
    
    assert(result.success);
    assert(!result.audioData.empty());
    
    std::cout << "✓ Async speech synthesis test passed" << std::endl;
}

void testCallbackSynthesis() {
    std::cout << "Testing callback synthesis..." << std::endl;
    
    auto tts = createCoquiTTS();
    tts->initialize("mock/model/path", "es_female_1");
    
    std::string testText = "Esta es una prueba de síntesis con callback.";
    bool callbackCalled = false;
    SynthesisResult callbackResult;
    
    // Test callback synthesis
    tts->synthesizeWithCallback(testText, [&](const SynthesisResult& result) {
        callbackCalled = true;
        callbackResult = result;
    });
    
    // Wait for callback
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    assert(callbackCalled);
    assert(callbackResult.success);
    assert(!callbackResult.audioData.empty());
    
    std::cout << "✓ Callback synthesis test passed" << std::endl;
}

void testSynthesisParameters() {
    std::cout << "Testing synthesis parameters..." << std::endl;
    
    auto tts = createCoquiTTS();
    tts->initialize("mock/model/path");
    
    // Test setting synthesis parameters
    tts->setSynthesisParameters(1.5f, 0.2f, 0.8f);
    
    std::string testText = "Testing parameter changes.";
    auto result = tts->synthesize(testText);
    
    assert(result.success);
    assert(!result.audioData.empty());
    
    std::cout << "✓ Synthesis parameters test passed" << std::endl;
}

void testErrorHandling() {
    std::cout << "Testing error handling..." << std::endl;
    
    auto tts = createCoquiTTS();
    
    // Test synthesis without initialization
    auto result = tts->synthesize("This should fail");
    assert(!result.success);
    assert(!result.errorMessage.empty());
    
    // Test invalid voice ID
    tts->initialize("mock/model/path");
    bool success = tts->setDefaultVoice("invalid_voice_id");
    assert(!success);
    assert(!tts->getLastError().empty());
    
    std::cout << "✓ Error handling test passed" << std::endl;
}

void testMultiLanguage() {
    std::cout << "Testing multi-language support..." << std::endl;
    
    auto tts = createCoquiTTS();
    tts->initialize("mock/model/path");
    
    // Test different languages
    std::vector<std::pair<std::string, std::string>> testCases = {
        {"en", "Hello world"},
        {"es", "Hola mundo"},
        {"fr", "Bonjour le monde"},
        {"de", "Hallo Welt"}
    };
    
    for (const auto& [lang, text] : testCases) {
        auto voices = tts->getVoicesForLanguage(lang);
        if (!voices.empty()) {
            auto result = tts->synthesize(text, voices[0].id);
            assert(result.success);
            assert(!result.audioData.empty());
            std::cout << "Successfully synthesized " << lang << ": \"" << text << "\"" << std::endl;
        }
    }
    
    std::cout << "✓ Multi-language test passed" << std::endl;
}

int main() {
    std::cout << "Running Coqui TTS tests..." << std::endl;
    
    try {
        testTTSInitialization();
        testVoiceManagement();
        testSynthesis();
        testAsyncSynthesis();
        testCallbackSynthesis();
        testSynthesisParameters();
        testErrorHandling();
        testMultiLanguage();
        
        std::cout << "\n✅ All TTS tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "\n❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
}