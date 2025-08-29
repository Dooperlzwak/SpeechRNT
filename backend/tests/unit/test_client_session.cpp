#include <gtest/gtest.h>
#include "core/client_session.hpp"
#include <vector>
#include <string>

class ClientSessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        session = std::make_unique<core::ClientSession>("test-session-123");
    }
    
    void TearDown() override {
        session.reset();
    }
    
    std::unique_ptr<core::ClientSession> session;
};

TEST_F(ClientSessionTest, SessionCreation) {
    EXPECT_EQ(session->getSessionId(), "test-session-123");
    EXPECT_TRUE(session->isConnected());
}

TEST_F(ClientSessionTest, ConfigMessageHandling) {
    std::string configMessage = R"({
        "type": "config",
        "data": {
            "sourceLang": "en",
            "targetLang": "fr",
            "voice": "female_voice_2"
        }
    })";
    
    // This should not throw and should process the config
    EXPECT_NO_THROW(session->handleMessage(configMessage));
}

TEST_F(ClientSessionTest, ControlMessageHandling) {
    std::string endSessionMessage = R"({
        "type": "end_session"
    })";
    
    EXPECT_TRUE(session->isConnected());
    session->handleMessage(endSessionMessage);
    EXPECT_FALSE(session->isConnected());
}

TEST_F(ClientSessionTest, AudioBufferManagement) {
    std::vector<float> audioChunk1 = {0.1f, 0.2f, 0.3f};
    std::vector<float> audioChunk2 = {0.4f, 0.5f, 0.6f};
    
    session->addAudioChunk(audioChunk1);
    session->addAudioChunk(audioChunk2);
    
    auto buffer = session->getAudioBuffer();
    EXPECT_EQ(buffer.size(), 6);
    EXPECT_FLOAT_EQ(buffer[0], 0.1f);
    EXPECT_FLOAT_EQ(buffer[3], 0.4f);
    EXPECT_FLOAT_EQ(buffer[5], 0.6f);
    
    session->clearAudioBuffer();
    buffer = session->getAudioBuffer();
    EXPECT_EQ(buffer.size(), 0);
}

TEST_F(ClientSessionTest, BinaryAudioProcessing) {
    // Create mock 16-bit PCM data (4 samples)
    std::vector<int16_t> pcmData = {16384, -16384, 8192, -8192}; // 0.5, -0.5, 0.25, -0.25 in float
    std::string_view binaryData(reinterpret_cast<const char*>(pcmData.data()), pcmData.size() * 2);
    
    session->handleBinaryMessage(binaryData);
    
    auto buffer = session->getAudioBuffer();
    EXPECT_EQ(buffer.size(), 4);
    EXPECT_NEAR(buffer[0], 0.5f, 0.01f);
    EXPECT_NEAR(buffer[1], -0.5f, 0.01f);
    EXPECT_NEAR(buffer[2], 0.25f, 0.01f);
    EXPECT_NEAR(buffer[3], -0.25f, 0.01f);
}

TEST_F(ClientSessionTest, LanguageConfiguration) {
    session->setLanguageConfig("de", "it");
    // Since we don't have getters, we test indirectly by ensuring no exceptions
    EXPECT_NO_THROW(session->setLanguageConfig("de", "it"));
}

TEST_F(ClientSessionTest, VoiceConfiguration) {
    session->setVoiceConfig("male_voice_1");
    // Since we don't have getters, we test indirectly by ensuring no exceptions
    EXPECT_NO_THROW(session->setVoiceConfig("male_voice_1"));
}

TEST_F(ClientSessionTest, DisconnectedSessionHandling) {
    std::string message = R"({"type":"config","data":{"sourceLang":"en"}})";
    
    // End the session
    session->handleMessage(R"({"type":"end_session"})");
    EXPECT_FALSE(session->isConnected());
    
    // Further messages should be handled gracefully
    EXPECT_NO_THROW(session->handleMessage(message));
    EXPECT_NO_THROW(session->handleBinaryMessage("test"));
}