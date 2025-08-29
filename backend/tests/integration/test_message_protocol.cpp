#include <iostream>
#include <cassert>
#include <memory>
#include "core/message_protocol.hpp"
#include "utils/logging.hpp"

using namespace core;

class MessageProtocolTest {
public:
    static void run() {
        utils::Logger::initialize();
        
        std::cout << "Running Message Protocol Tests..." << std::endl;
        
        testConfigMessageSerialization();
        testConfigMessageParsing();
        testTranscriptionUpdateMessage();
        testTranslationResultMessage();
        testStatusUpdateMessage();
        testErrorMessage();
        testMessageValidation();
        testInvalidMessages();
        
        std::cout << "All message protocol tests passed!" << std::endl;
    }
    
private:
    static void testConfigMessageSerialization() {
        std::cout << "Test: Config Message Serialization... ";
        
        ConfigMessage config("en", "es", "female_voice_1");
        std::string json = config.serialize();
        
        // Should contain expected fields
        assert(json.find("\"type\":\"config\"") != std::string::npos);
        assert(json.find("\"sourceLang\":\"en\"") != std::string::npos);
        assert(json.find("\"targetLang\":\"es\"") != std::string::npos);
        assert(json.find("\"voice\":\"female_voice_1\"") != std::string::npos);
        
        std::cout << "PASSED" << std::endl;
    }
    
    static void testConfigMessageParsing() {
        std::cout << "Test: Config Message Parsing... ";
        
        std::string json = R"({"type":"config","data":{"sourceLang":"de","targetLang":"fr","voice":"male_voice_1"}})";
        
        auto message = MessageProtocol::parseMessage(json);
        assert(message != nullptr);
        assert(message->getType() == MessageType::CONFIG);
        
        auto* configMsg = static_cast<ConfigMessage*>(message.get());
        assert(configMsg->getSourceLang() == "de");
        assert(configMsg->getTargetLang() == "fr");
        assert(configMsg->getVoice() == "male_voice_1");
        
        std::cout << "PASSED" << std::endl;
    }
    
    static void testTranscriptionUpdateMessage() {
        std::cout << "Test: Transcription Update Message... ";
        
        TranscriptionUpdateMessage msg("Hello world", 123, 0.95);
        std::string json = msg.serialize();
        
        assert(json.find("\"type\":\"transcription_update\"") != std::string::npos);
        assert(json.find("\"text\":\"Hello world\"") != std::string::npos);
        assert(json.find("\"utteranceId\":123") != std::string::npos);
        assert(json.find("\"confidence\":0.95") != std::string::npos);
        
        // Test parsing
        auto parsed = MessageProtocol::parseMessage(json);
        assert(parsed != nullptr);
        assert(parsed->getType() == MessageType::TRANSCRIPTION_UPDATE);
        
        auto* transcriptionMsg = static_cast<TranscriptionUpdateMessage*>(parsed.get());
        assert(transcriptionMsg->getText() == "Hello world");
        assert(transcriptionMsg->getUtteranceId() == 123);
        assert(std::abs(transcriptionMsg->getConfidence() - 0.95) < 0.001);
        
        std::cout << "PASSED" << std::endl;
    }
    
    static void testTranslationResultMessage() {
        std::cout << "Test: Translation Result Message... ";
        
        TranslationResultMessage msg("Hello", "Hola", 456);
        std::string json = msg.serialize();
        
        assert(json.find("\"type\":\"translation_result\"") != std::string::npos);
        assert(json.find("\"originalText\":\"Hello\"") != std::string::npos);
        assert(json.find("\"translatedText\":\"Hola\"") != std::string::npos);
        assert(json.find("\"utteranceId\":456") != std::string::npos);
        
        // Test parsing
        auto parsed = MessageProtocol::parseMessage(json);
        assert(parsed != nullptr);
        assert(parsed->getType() == MessageType::TRANSLATION_RESULT);
        
        auto* translationMsg = static_cast<TranslationResultMessage*>(parsed.get());
        assert(translationMsg->getOriginalText() == "Hello");
        assert(translationMsg->getTranslatedText() == "Hola");
        assert(translationMsg->getUtteranceId() == 456);
        
        std::cout << "PASSED" << std::endl;
    }
    
    static void testStatusUpdateMessage() {
        std::cout << "Test: Status Update Message... ";
        
        StatusUpdateMessage msg(StatusUpdateMessage::State::LISTENING, 789);
        std::string json = msg.serialize();
        
        assert(json.find("\"type\":\"status_update\"") != std::string::npos);
        assert(json.find("\"state\":\"listening\"") != std::string::npos);
        assert(json.find("\"utteranceId\":789") != std::string::npos);
        
        // Test parsing
        auto parsed = MessageProtocol::parseMessage(json);
        assert(parsed != nullptr);
        assert(parsed->getType() == MessageType::STATUS_UPDATE);
        
        auto* statusMsg = static_cast<StatusUpdateMessage*>(parsed.get());
        assert(statusMsg->getState() == StatusUpdateMessage::State::LISTENING);
        assert(statusMsg->getUtteranceId() == 789);
        
        std::cout << "PASSED" << std::endl;
    }
    
    static void testErrorMessage() {
        std::cout << "Test: Error Message... ";
        
        ErrorMessage msg("Something went wrong", "ERR_001", 999);
        std::string json = msg.serialize();
        
        assert(json.find("\"type\":\"error\"") != std::string::npos);
        assert(json.find("\"message\":\"Something went wrong\"") != std::string::npos);
        assert(json.find("\"code\":\"ERR_001\"") != std::string::npos);
        assert(json.find("\"utteranceId\":999") != std::string::npos);
        
        // Test parsing
        auto parsed = MessageProtocol::parseMessage(json);
        assert(parsed != nullptr);
        assert(parsed->getType() == MessageType::ERROR);
        
        auto* errorMsg = static_cast<ErrorMessage*>(parsed.get());
        assert(errorMsg->getMessage() == "Something went wrong");
        assert(errorMsg->getCode() == "ERR_001");
        assert(errorMsg->getUtteranceId() == 999);
        
        std::cout << "PASSED" << std::endl;
    }
    
    static void testMessageValidation() {
        std::cout << "Test: Message Validation... ";
        
        // Valid messages
        assert(MessageProtocol::validateMessage(R"({"type":"ping"})"));
        assert(MessageProtocol::validateMessage(R"({"type":"end_session"})"));
        assert(MessageProtocol::validateMessage(R"({"type":"config","data":{"sourceLang":"en","targetLang":"es","voice":"voice1"}})"));
        
        // Valid message type detection
        assert(MessageProtocol::getMessageType(R"({"type":"ping"})") == MessageType::PING);
        assert(MessageProtocol::getMessageType(R"({"type":"config","data":{}})") == MessageType::CONFIG);
        
        std::cout << "PASSED" << std::endl;
    }
    
    static void testInvalidMessages() {
        std::cout << "Test: Invalid Message Handling... ";
        
        // Invalid JSON
        assert(!MessageProtocol::validateMessage("{invalid json}"));
        assert(MessageProtocol::getMessageType("{invalid json}") == MessageType::UNKNOWN);
        
        // Missing type field
        assert(!MessageProtocol::validateMessage(R"({"data":{}})"));
        
        // Unknown message type
        assert(!MessageProtocol::validateMessage(R"({"type":"unknown"})"));
        assert(MessageProtocol::getMessageType(R"({"type":"unknown"})") == MessageType::UNKNOWN);
        
        // Invalid config message (missing required fields)
        assert(!MessageProtocol::validateMessage(R"({"type":"config","data":{"sourceLang":"en"}})"));
        
        // Parsing should return null for invalid messages
        assert(MessageProtocol::parseMessage("{invalid json}") == nullptr);
        assert(MessageProtocol::parseMessage(R"({"type":"unknown"})") == nullptr);
        
        std::cout << "PASSED" << std::endl;
    }
};

int main() {
    try {
        MessageProtocolTest::run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}