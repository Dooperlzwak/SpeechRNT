#include "tts/piper_tts.hpp"
#include "tts/voice_manager.hpp"
#include <cassert>
#include <iostream>
#include <memory>

using namespace speechrnt::tts;

void testVoiceManagerInitialization() {
  std::cout << "Testing VoiceManager initialization..." << std::endl;

  VoiceManager manager;
  assert(!manager.isReady());

  // Create and initialize TTS engine
  auto tts_unique = createPiperTTS();
  tts_unique->initialize("mock/model/path");
  std::shared_ptr<TTSInterface> tts = std::move(tts_unique);

  // Initialize voice manager
  bool success = manager.initialize(tts);
  assert(success);
  assert(manager.isReady());

  std::cout << "✓ VoiceManager initialization test passed" << std::endl;
}

void testLanguageVoiceSelection() {
  std::cout << "Testing language voice selection..." << std::endl;

  VoiceManager manager;
  auto tts_unique = createPiperTTS();
  tts_unique->initialize("mock/model/path");
  std::shared_ptr<TTSInterface> tts = std::move(tts_unique);
  manager.initialize(tts);

  // Test getting best voice for language
  std::string enVoice = manager.getBestVoiceForLanguage("en");
  assert(!enVoice.empty());
  std::cout << "Best English voice: " << enVoice << std::endl;

  // Test with gender preference
  std::string femaleVoice = manager.getBestVoiceForLanguage("en", "female");
  assert(!femaleVoice.empty());
  std::cout << "Best English female voice: " << femaleVoice << std::endl;

  std::string maleVoice = manager.getBestVoiceForLanguage("en", "male");
  assert(!maleVoice.empty());
  std::cout << "Best English male voice: " << maleVoice << std::endl;

  std::cout << "✓ Language voice selection test passed" << std::endl;
}

void testVoicePreferences() {
  std::cout << "Testing voice preferences..." << std::endl;

  VoiceManager manager;
  auto tts_unique = createPiperTTS();
  tts_unique->initialize("mock/model/path");
  std::shared_ptr<TTSInterface> tts = std::move(tts_unique);
  manager.initialize(tts);

  // Set preference for English
  std::string preferredVoice = "en_male_1";
  manager.setLanguagePreference("en", preferredVoice);

  // Check that preference is returned
  std::string retrievedPref = manager.getLanguagePreference("en");
  assert(retrievedPref == preferredVoice);

  // Check that getBestVoiceForLanguage returns the preferred voice
  std::string bestVoice = manager.getBestVoiceForLanguage("en");
  assert(bestVoice == preferredVoice);

  std::cout << "✓ Voice preferences test passed" << std::endl;
}

void testVoiceInformation() {
  std::cout << "Testing voice information retrieval..." << std::endl;

  VoiceManager manager;
  auto tts_unique = createPiperTTS();
  tts_unique->initialize("mock/model/path");
  std::shared_ptr<TTSInterface> tts = std::move(tts_unique);
  manager.initialize(tts);

  // Test getting all voices
  auto allVoices = manager.getAllVoices();
  assert(!allVoices.empty());
  std::cout << "Total voices: " << allVoices.size() << std::endl;

  // Test getting voices for specific language
  auto enVoices = manager.getVoicesForLanguage("en");
  assert(!enVoices.empty());
  std::cout << "English voices: " << enVoices.size() << std::endl;

  // Test getting voice info by ID
  if (!allVoices.empty()) {
    VoiceInfo info = manager.getVoiceInfo(allVoices[0].id);
    assert(!info.id.empty());
    assert(info.id == allVoices[0].id);
    std::cout << "Voice info: " << info.name << " (" << info.language << ", "
              << info.gender << ")" << std::endl;
  }

  // Test voice availability
  if (!allVoices.empty()) {
    bool available = manager.isVoiceAvailable(allVoices[0].id);
    assert(available);
  }

  bool unavailable = manager.isVoiceAvailable("nonexistent_voice");
  assert(!unavailable);

  std::cout << "✓ Voice information test passed" << std::endl;
}

void testSupportedLanguages() {
  std::cout << "Testing supported languages..." << std::endl;

  VoiceManager manager;
  auto tts_unique = createPiperTTS();
  tts_unique->initialize("mock/model/path");
  std::shared_ptr<TTSInterface> tts = std::move(tts_unique);
  manager.initialize(tts);

  auto languages = manager.getSupportedLanguages();
  assert(!languages.empty());

  std::cout << "Supported languages: ";
  for (const auto &lang : languages) {
    std::cout << lang << " ";
  }
  std::cout << std::endl;

  // Verify that each language has at least one voice
  for (const auto &lang : languages) {
    auto voices = manager.getVoicesForLanguage(lang);
    assert(!voices.empty());
  }

  std::cout << "✓ Supported languages test passed" << std::endl;
}

void testVoiceRefresh() {
  std::cout << "Testing voice refresh..." << std::endl;

  VoiceManager manager;
  auto tts_unique = createPiperTTS();
  tts_unique->initialize("mock/model/path");
  std::shared_ptr<TTSInterface> tts = std::move(tts_unique);
  manager.initialize(tts);

  auto voicesBefore = manager.getAllVoices();

  // Refresh voices
  bool success = manager.refreshVoices();
  assert(success);

  auto voicesAfter = manager.getAllVoices();
  assert(voicesBefore.size() == voicesAfter.size());

  std::cout << "✓ Voice refresh test passed" << std::endl;
}

void testErrorHandling() {
  std::cout << "Testing VoiceManager error handling..." << std::endl;

  VoiceManager manager;

  // Test initialization with null TTS engine
  bool success = manager.initialize(nullptr);
  assert(!success);
  assert(!manager.isReady());

  // Test operations on uninitialized manager
  std::string voice = manager.getBestVoiceForLanguage("en");
  assert(voice.empty());

  auto voices = manager.getAllVoices();
  assert(voices.empty());

  std::cout << "✓ VoiceManager error handling test passed" << std::endl;
}

int main() {
  std::cout << "Running VoiceManager tests..." << std::endl;

  try {
    testVoiceManagerInitialization();
    testLanguageVoiceSelection();
    testVoicePreferences();
    testVoiceInformation();
    testSupportedLanguages();
    testVoiceRefresh();
    testErrorHandling();

    std::cout << "\n✅ All VoiceManager tests passed!" << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "\n❌ Test failed with exception: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "\n❌ Test failed with unknown exception" << std::endl;
    return 1;
  }
}