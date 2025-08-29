#include <gtest/gtest.h>
#include "core/message_protocol.hpp"
#include "core/websocket_server.hpp"
#include <memory>
#include <string>
#include <vector>

namespace core {

class MessageProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test data
    }
    
    void TearDown() override {
        // Cleanup
    }
};

// Test message type detection
TEST_F(MessageProtocolTest, DetectMessageTypes) {
    std::string configJson = R"({"type":"config","data":{"sourceLang":"en","targetLang":"es","voice":"female_1"}})";
    std::string transcriptionJson = R"({"type":"transcription_update","data":{"text":"Hello","utteranceId":1,"confidence":0.95}})";
    std::string translationJson = R"({"type":"translation_result","data":{"originalText":"Hello","translatedText":"Hola","utteranceId":1}})";
    std::string statusJson = R"({"type":"status_update","data":{"state":"listening","utteranceId":1}})";
    std::string endSessionJson = R"({"type":"end_session"})";
    
    EXPECT_EQ(MessageProtocol::getMessageType(configJson), MessageType::CONFIG);
    EXPECT_EQ(MessageProtocol::getMessageType(transcriptionJson), MessageType::TRANSCRIPTION_UPDATE);
    EXPECT_EQ(MessageProtocol::getMessageType(translationJson), MessageType::TRANSLATION_RESULT);
    EXPECT_EQ(MessageProtocol::getMessageType(statusJson), MessageType::STATUS_UPDATE);
    EXPECT_EQ(MessageProtocol::getMessageType(endSessionJson), MessageType::END_SESSION);
}

// Test message validation
TEST_F(MessageProtocolTest, ValidateMessages) {
    std::string validJson = R"({"type":"config","data":{"sourceLang":"en","targetLang":"es","voice":"female_1"}})";
    std::string invalidJson = R"({invalid json})";
    std::string missingTypeJson = R"({"data":{"sourceLang":"en"}})";
    std::string emptyJson = "{}";
    
    EXPECT_TRUE(MessageProtocol::validateMessage(validJson));
    EXPECT_FALSE(MessageProtocol::validateMessage(invalidJson));
    EXPECT_FALSE(MessageProtocol::validateMessage(missingTypeJson));
    EXPECT_FALSE(MessageProtocol::validateMessage(emptyJson));
}

// Test config message parsing
TEST_F(MessageProtocolTest, ParseConfigMessage) {
    std::string configJson = R"({"type":"config","data":{"sourceLang":"en","targetLang":"es","voice":"female_1"}})";
    
    auto message = MessageProtocol::parseMessage(configJson);
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(message->getType(), MessageType::CONFIG);
    
    auto* configMsg = static_cast<ConfigMessage*>(message.get());
    EXPECT_EQ(configMsg->getSourceLang(), "en");
    EXPECT_EQ(configMsg->getTargetLang(), "es");
    EXPECT_EQ(configMsg->getVoice(), "female_1");
}

// Test transcription message creation and serialization
TEST_F(MessageProtocolTest, CreateTranscriptionMessage) {
    TranscriptionUpdateMessage transcription("Hello world", 123, 0.95);
    
    std::string json = transcription.serialize();
    EXPECT_NE(json.find("\"type\":\"transcription_update\""), std::string::npos);
    EXPECT_NE(json.find("\"text\":\"Hello world\""), std::string::npos);
    EXPECT_NE(json.find("\"utteranceId\":123"), std::string::npos);
    EXPECT_NE(json.find("\"confidence\":0.95"), std::string::npos);
}

// Test translation result message
TEST_F(MessageProtocolTest, CreateTranslationMessage) {
    TranslationResultMessage translation("Hello", "Hola", 456);
    
    std::string json = translation.serialize();
    EXPECT_NE(json.find("\"type\":\"translation_result\""), std::string::npos);
    EXPECT_NE(json.find("\"originalText\":\"Hello\""), std::string::npos);
    EXPECT_NE(json.find("\"translatedText\":\"Hola\""), std::string::npos);
    EXPECT_NE(json.find("\"utteranceId\":456"), std::string::npos);
}

// Test status update message
TEST_F(MessageProtocolTest, CreateStatusMessage) {
    StatusUpdateMessage status("thinking", 789);
    
    std::string json = status.serialize();
    EXPECT_NE(json.find("\"type\":\"status_update\""), std::string::npos);
    EXPECT_NE(json.find("\"state\":\"thinking\""), std::string::npos);
    EXPECT_NE(json.find("\"utteranceId\":789"), std::string::npos);
}

// Test audio start message
TEST_F(MessageProtocolTest, CreateAudioStartMessage) {
    AudioStartMessage audioStart(101, 2.5);
    
    std::string json = audioStart.serialize();
    EXPECT_NE(json.find("\"type\":\"audio_start\""), std::string::npos);
    EXPECT_NE(json.find("\"utteranceId\":101"), std::string::npos);
    EXPECT_NE(json.find("\"duration\":2.5"), std::string::npos);
}

// Test error message
TEST_F(MessageProtocolTest, CreateErrorMessage) {
    ErrorMessage error("Translation failed", "TRANSLATION_ERROR", 202);
    
    std::string json = error.serialize();
    EXPECT_NE(json.find("\"type\":\"error\""), std::string::npos);
    EXPECT_NE(json.find("\"message\":\"Translation failed\""), std::string::npos);
    EXPECT_NE(json.find("\"code\":\"TRANSLATION_ERROR\""), std::string::npos);
    EXPECT_NE(json.find("\"utteranceId\":202"), std::string::npos);
}

// Test message parsing edge cases
TEST_F(MessageProtocolTest, ParseEdgeCases) {
    // Empty message
    auto emptyMsg = MessageProtocol::parseMessage("");
    EXPECT_EQ(emptyMsg, nullptr);
    
    // Very large message
    std::string largeText(10000, 'a');
    std::string largeJson = R"({"type":"transcription_update","data":{"text":")" + largeText + R"(","utteranceId":1,"confidence":0.95}})";
    auto largeMsg = MessageProtocol::parseMessage(largeJson);
    EXPECT_NE(largeMsg, nullptr);
    
    // Unicode characters
    std::string unicodeJson = R"({"type":"transcription_update","data":{"text":"Hëllö wörld 🌍","utteranceId":1,"confidence":0.95}})";
    auto unicodeMsg = MessageProtocol::parseMessage(unicodeJson);
    EXPECT_NE(unicodeMsg, nullptr);
}

// Test message serialization consistency
TEST_F(MessageProtocolTest, SerializationConsistency) {
    ConfigMessage original("en", "fr", "male_voice_2");
    std::string serialized = original.serialize();
    
    auto parsed = MessageProtocol::parseMessage(serialized);
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->getType(), MessageType::CONFIG);
    
    auto* parsedConfig = static_cast<ConfigMessage*>(parsed.get());
    EXPECT_EQ(parsedConfig->getSourceLang(), original.getSourceLang());
    EXPECT_EQ(parsedConfig->getTargetLang(), original.getTargetLang());
    EXPECT_EQ(parsedConfig->getVoice(), original.getVoice());
}

// Test concurrent message processing
TEST_F(MessageProtocolTest, ConcurrentMessageProcessing) {
    const int numThreads = 10;
    const int messagesPerThread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < messagesPerThread; ++i) {
                std::string json = R"({"type":"transcription_update","data":{"text":"Message )" + 
                                 std::to_string(t * messagesPerThread + i) + 
                                 R"(","utteranceId":)" + std::to_string(i) + R"(,"confidence":0.95}})";
                
                auto message = MessageProtocol::parseMessage(json);
                if (message && message->getType() == MessageType::TRANSCRIPTION_UPDATE) {
                    successCount++;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(successCount.load(), numThreads * messagesPerThread);
}

} // namespace core