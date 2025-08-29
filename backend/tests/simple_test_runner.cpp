#include <iostream>
#include <cassert>
#include <memory>
#include <thread>
#include <chrono>
#include "core/websocket_server.hpp"
#include "core/client_session.hpp"
#include "core/message_protocol.hpp"
#include "utils/logging.hpp"

// Simple test framework
class TestRunner {
public:
    static void run() {
        utils::Logger::initialize();
        
        std::cout << "Running WebSocket Server Tests..." << std::endl;
        
        testServerCreation();
        testServerStartStop();
        testSessionCreation();
        testSessionConfiguration();
        testAudioBufferManagement();
        
        testMessageProtocol();
        
        std::cout << "All tests passed!" << std::endl;
    }
    
private:
    static void testServerCreation() {
        std::cout << "Test: Server Creation... ";
        auto server = std::make_unique<core::WebSocketServer>(8081);
        assert(server != nullptr);
        std::cout << "PASSED" << std::endl;
    }
    
    static void testServerStartStop() {
        std::cout << "Test: Server Start/Stop... ";
        auto server = std::make_unique<core::WebSocketServer>(8082);
        
        server->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        server->stop();
        
        // Starting again should not cause issues
        server->start();
        server->stop();
        
        std::cout << "PASSED" << std::endl;
    }
    
    static void testSessionCreation() {
        std::cout << "Test: Session Creation... ";
        auto session = std::make_unique<core::ClientSession>("test-session-123");
        
        assert(session->getSessionId() == "test-session-123");
        assert(session->isConnected() == true);
        
        std::cout << "PASSED" << std::endl;
    }
    
    static void testSessionConfiguration() {
        std::cout << "Test: Session Configuration... ";
        auto session = std::make_unique<core::ClientSession>("test-session-config");
        
        // Test language configuration
        session->setLanguageConfig("en", "fr");
        session->setVoiceConfig("female_voice_2");
        
        // Test message handling (should not throw)
        std::string configMessage = R"({"type":"config","data":{"sourceLang":"de","targetLang":"it","voice":"male_voice_1"}})";
        session->handleMessage(configMessage);
        
        std::string endSessionMessage = R"({"type":"end_session"})";
        session->handleMessage(endSessionMessage);
        assert(session->isConnected() == false);
        
        std::cout << "PASSED" << std::endl;
    }
    
    static void testAudioBufferManagement() {
        std::cout << "Test: Audio Buffer Management... ";
        auto session = std::make_unique<core::ClientSession>("test-audio-session");
        
        // Test binary audio processing
        std::vector<int16_t> pcmData = {16384, -16384, 8192, -8192};
        std::string_view binaryData(reinterpret_cast<const char*>(pcmData.data()), pcmData.size() * 2);
        
        session->handleBinaryMessage(binaryData);
        
        auto buffer = session->getAudioBuffer();
        assert(buffer != nullptr);
        
        auto samples = buffer->getAllSamples();
        assert(samples.size() == 4);
        assert(std::abs(samples[0] - 0.5f) < 0.01f);
        assert(std::abs(samples[1] - (-0.5f)) < 0.01f);
        
        session->clearAudioBuffer();
        buffer = session->getAudioBuffer();
        auto samplesAfterClear = buffer->getAllSamples();
        assert(samplesAfterClear.size() == 0);
        
        std::cout << "PASSED" << std::endl;
    }
    
    static void testMessageProtocol() {
        std::cout << "Test: Message Protocol... ";
        
        // Test config message serialization and parsing
        core::ConfigMessage config("en", "es", "female_voice_1");
        std::string json = config.serialize();
        
        std::cout << "Generated JSON: " << json << std::endl;
        
        assert(json.find("\"type\":\"config\"") != std::string::npos);
        assert(json.find("\"sourceLang\":\"en\"") != std::string::npos);
        
        auto parsed = core::MessageProtocol::parseMessage(json);
        assert(parsed != nullptr);
        assert(parsed->getType() == core::MessageType::CONFIG);
        
        auto* configMsg = static_cast<core::ConfigMessage*>(parsed.get());
        assert(configMsg->getSourceLang() == "en");
        assert(configMsg->getTargetLang() == "es");
        assert(configMsg->getVoice() == "female_voice_1");
        
        // Test transcription message
        core::TranscriptionUpdateMessage transcription("Hello world", 123, 0.95);
        std::string transcriptionJson = transcription.serialize();
        
        assert(transcriptionJson.find("\"type\":\"transcription_update\"") != std::string::npos);
        assert(transcriptionJson.find("\"text\":\"Hello world\"") != std::string::npos);
        
        // Test message validation
        assert(core::MessageProtocol::validateMessage(json));
        assert(!core::MessageProtocol::validateMessage("{invalid json}"));
        assert(core::MessageProtocol::getMessageType(json) == core::MessageType::CONFIG);
        
        std::cout << "PASSED" << std::endl;
    }
};

int main() {
    try {
        TestRunner::run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}