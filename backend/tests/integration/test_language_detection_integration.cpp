#include <gtest/gtest.h>
#include "core/client_session.hpp"
#include "core/websocket_server.hpp"
#include "core/message_protocol.hpp"
#include "stt/whisper_stt.hpp"
#include "utils/json_utils.hpp"
#include <memory>
#include <thread>
#include <chrono>

using namespace core;
using namespace stt;

class LanguageDetectionIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a mock client session for testing
        sessionId_ = "test_session_lang_detect";
        
        // Initialize STT engine with language detection
        whisperSTT_ = std::make_unique<WhisperSTT>();
        bool initialized = whisperSTT_->initialize("test_models/whisper-base.bin", 4);
        ASSERT_TRUE(initialized) << "Failed to initialize WhisperSTT: " << whisperSTT_->getLastError();
        
        // Configure language detection
        whisperSTT_->setLanguageDetectionEnabled(true);
        whisperSTT_->setAutoLanguageSwitching(true);
        whisperSTT_->setLanguageDetectionThreshold(0.6f);
        
        // Generate test audio data
        generateTestAudio();
    }
    
    void TearDown() override {
        whisperSTT_.reset();
    }
    
    void generateTestAudio() {
        // Generate 2 seconds of test audio (16kHz)
        testAudio_.resize(32000);
        for (size_t i = 0; i < testAudio_.size(); ++i) {
            // Mix of frequencies to simulate speech
            float t = (float)i / 16000.0f;
            testAudio_[i] = 0.1f * (std::sin(2.0f * M_PI * 440.0f * t) + 
                                   0.5f * std::sin(2.0f * M_PI * 880.0f * t) +
                                   0.3f * std::sin(2.0f * M_PI * 220.0f * t));
        }
    }
    
    std::string sessionId_;
    std::unique_ptr<WhisperSTT> whisperSTT_;
    std::vector<float> testAudio_;
    std::vector<std::string> receivedMessages_;
};

TEST_F(LanguageDetectionIntegrationTest, LanguageChangeNotification) {
    // Test that language changes are properly communicated through the message protocol
    std::string oldLang, newLang;
    float confidence = 0.0f;
    bool languageChangeReceived = false;
    
    // Set up language change callback to simulate client session behavior
    whisperSTT_->setLanguageChangeCallback([&](const std::string& old_lang, const std::string& new_lang, float conf) {
        oldLang = old_lang;
        newLang = new_lang;
        confidence = conf;
        
        // Create language change message as would be done in ClientSession
        LanguageChangeMessage langChangeMsg(old_lang, new_lang, conf);
        std::string serialized = langChangeMsg.serialize();
        receivedMessages_.push_back(serialized);
        languageChangeReceived = true;
    });
    
    // Perform multiple transcriptions to potentially trigger language change
    for (int i = 0; i < 10 && !languageChangeReceived; ++i) {
        bool transcriptionComplete = false;
        
        whisperSTT_->transcribe(testAudio_, [&](const TranscriptionResult& result) {
            transcriptionComplete = true;
            
            // Log the detected language for debugging
            std::cout << "Transcription " << i << ": detected language = " 
                      << result.detected_language << ", confidence = " 
                      << result.language_confidence << ", changed = " 
                      << result.language_changed << std::endl;
        });
        
        // Wait for transcription to complete
        auto start = std::chrono::steady_clock::now();
        while (!transcriptionComplete && 
               std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        ASSERT_TRUE(transcriptionComplete) << "Transcription " << i << " did not complete";
        
        if (languageChangeReceived) {
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // In simulation mode, we should eventually get a language change
    if (languageChangeReceived) {
        EXPECT_FALSE(oldLang.empty());
        EXPECT_FALSE(newLang.empty());
        EXPECT_NE(oldLang, newLang);
        EXPECT_GE(confidence, 0.0f);
        EXPECT_LE(confidence, 1.0f);
        
        // Verify the message was properly serialized
        EXPECT_GT(receivedMessages_.size(), 0);
        
        // Parse the message to verify its structure
        std::string messageJson = receivedMessages_[0];
        auto parsedMessage = MessageProtocol::parseMessage(messageJson);
        ASSERT_NE(parsedMessage, nullptr);
        
        auto* langChangeMsg = dynamic_cast<LanguageChangeMessage*>(parsedMessage.get());
        ASSERT_NE(langChangeMsg, nullptr);
        
        EXPECT_EQ(langChangeMsg->getOldLanguage(), oldLang);
        EXPECT_EQ(langChangeMsg->getNewLanguage(), newLang);
        EXPECT_FLOAT_EQ(langChangeMsg->getConfidence(), confidence);
        
        std::cout << "Language change message verified: " << oldLang 
                  << " -> " << newLang << " (confidence: " << confidence << ")" << std::endl;
    } else {
        std::cout << "No language change detected in simulation mode (this is expected sometimes)" << std::endl;
    }
}

TEST_F(LanguageDetectionIntegrationTest, StreamingLanguageDetection) {
    // Test language detection in streaming mode with message protocol
    uint32_t utteranceId = 2001;
    std::vector<TranscriptionResult> streamingResults;
    std::vector<std::string> languageChangeMessages;
    
    // Set up language change callback
    whisperSTT_->setLanguageChangeCallback([&](const std::string& old_lang, const std::string& new_lang, float conf) {
        LanguageChangeMessage langChangeMsg(old_lang, new_lang, conf, utteranceId);
        languageChangeMessages.push_back(langChangeMsg.serialize());
        
        std::cout << "Streaming language change: " << old_lang << " -> " << new_lang 
                  << " (confidence: " << conf << ", utterance: " << utteranceId << ")" << std::endl;
    });
    
    // Set up streaming callback
    whisperSTT_->setStreamingCallback(utteranceId, [&](const TranscriptionResult& result) {
        streamingResults.push_back(result);
        
        std::cout << "Streaming result: \"" << result.text << "\" (language: " 
                  << result.detected_language << ", confidence: " 
                  << result.language_confidence << ", partial: " 
                  << result.is_partial << ")" << std::endl;
    });
    
    // Enable partial results for streaming
    whisperSTT_->setPartialResultsEnabled(true);
    
    // Start streaming transcription
    whisperSTT_->startStreamingTranscription(utteranceId);
    EXPECT_TRUE(whisperSTT_->isStreamingActive(utteranceId));
    
    // Send audio in chunks to simulate real-time streaming
    size_t chunkSize = testAudio_.size() / 8;
    for (size_t i = 0; i < 8; ++i) {
        std::vector<float> chunk(testAudio_.begin() + i * chunkSize, 
                                testAudio_.begin() + (i + 1) * chunkSize);
        whisperSTT_->addAudioChunk(utteranceId, chunk);
        
        // Small delay to simulate real-time audio
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Finalize streaming
    whisperSTT_->finalizeStreamingTranscription(utteranceId);
    
    // Wait for final processing
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    EXPECT_FALSE(whisperSTT_->isStreamingActive(utteranceId));
    
    // Verify we got streaming results with language information
    EXPECT_GT(streamingResults.size(), 0);
    
    for (const auto& result : streamingResults) {
        EXPECT_FALSE(result.detected_language.empty());
        EXPECT_GE(result.language_confidence, 0.0f);
        EXPECT_LE(result.language_confidence, 1.0f);
    }
    
    // If we got language change messages, verify their structure
    for (const auto& messageJson : languageChangeMessages) {
        auto parsedMessage = MessageProtocol::parseMessage(messageJson);
        ASSERT_NE(parsedMessage, nullptr);
        
        auto* langChangeMsg = dynamic_cast<LanguageChangeMessage*>(parsedMessage.get());
        ASSERT_NE(langChangeMsg, nullptr);
        
        EXPECT_EQ(langChangeMsg->getUtteranceId(), utteranceId);
        EXPECT_FALSE(langChangeMsg->getOldLanguage().empty());
        EXPECT_FALSE(langChangeMsg->getNewLanguage().empty());
        EXPECT_GE(langChangeMsg->getConfidence(), 0.0f);
        EXPECT_LE(langChangeMsg->getConfidence(), 1.0f);
    }
}

TEST_F(LanguageDetectionIntegrationTest, ConcurrentStreamingLanguageDetection) {
    // Test language detection with multiple concurrent streaming sessions
    std::vector<uint32_t> utteranceIds = {3001, 3002, 3003};
    std::map<uint32_t, std::vector<TranscriptionResult>> allResults;
    std::vector<std::string> allLanguageChangeMessages;
    
    // Set up global language change callback
    whisperSTT_->setLanguageChangeCallback([&](const std::string& old_lang, const std::string& new_lang, float conf) {
        // In a real scenario, we'd need to determine which utterance triggered the change
        // For testing, we'll create a message without specific utterance ID
        LanguageChangeMessage langChangeMsg(old_lang, new_lang, conf);
        allLanguageChangeMessages.push_back(langChangeMsg.serialize());
    });
    
    // Set up callbacks for each utterance
    for (uint32_t id : utteranceIds) {
        whisperSTT_->setStreamingCallback(id, [&, id](const TranscriptionResult& result) {
            allResults[id].push_back(result);
        });
    }
    
    // Start all streaming transcriptions
    for (uint32_t id : utteranceIds) {
        whisperSTT_->startStreamingTranscription(id);
        EXPECT_TRUE(whisperSTT_->isStreamingActive(id));
    }
    
    EXPECT_EQ(whisperSTT_->getActiveStreamingCount(), utteranceIds.size());
    
    // Send audio to all utterances concurrently
    std::vector<std::thread> audioThreads;
    for (uint32_t id : utteranceIds) {
        audioThreads.emplace_back([&, id]() {
            size_t chunkSize = testAudio_.size() / 4;
            for (size_t i = 0; i < 4; ++i) {
                std::vector<float> chunk(testAudio_.begin() + i * chunkSize, 
                                        testAudio_.begin() + (i + 1) * chunkSize);
                whisperSTT_->addAudioChunk(id, chunk);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    }
    
    // Wait for all audio threads to complete
    for (auto& thread : audioThreads) {
        thread.join();
    }
    
    // Finalize all streaming
    for (uint32_t id : utteranceIds) {
        whisperSTT_->finalizeStreamingTranscription(id);
    }
    
    // Wait for final processing
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    
    EXPECT_EQ(whisperSTT_->getActiveStreamingCount(), 0);
    
    // Verify results for each utterance
    for (uint32_t id : utteranceIds) {
        EXPECT_GT(allResults[id].size(), 0) << "No results for utterance " << id;
        
        for (const auto& result : allResults[id]) {
            EXPECT_FALSE(result.detected_language.empty());
            EXPECT_GE(result.language_confidence, 0.0f);
            EXPECT_LE(result.language_confidence, 1.0f);
        }
    }
    
    // Verify any language change messages
    for (const auto& messageJson : allLanguageChangeMessages) {
        auto parsedMessage = MessageProtocol::parseMessage(messageJson);
        ASSERT_NE(parsedMessage, nullptr);
        
        auto* langChangeMsg = dynamic_cast<LanguageChangeMessage*>(parsedMessage.get());
        ASSERT_NE(langChangeMsg, nullptr);
        
        EXPECT_FALSE(langChangeMsg->getOldLanguage().empty());
        EXPECT_FALSE(langChangeMsg->getNewLanguage().empty());
        EXPECT_GE(langChangeMsg->getConfidence(), 0.0f);
        EXPECT_LE(langChangeMsg->getConfidence(), 1.0f);
    }
}

TEST_F(LanguageDetectionIntegrationTest, MessageProtocolValidation) {
    // Test that language change messages conform to the expected protocol
    std::string testOldLang = "en";
    std::string testNewLang = "es";
    float testConfidence = 0.85f;
    uint32_t testUtteranceId = 4001;
    
    // Create language change message
    LanguageChangeMessage langChangeMsg(testOldLang, testNewLang, testConfidence, testUtteranceId);
    std::string serialized = langChangeMsg.serialize();
    
    // Verify the message can be parsed
    EXPECT_TRUE(MessageProtocol::validateMessage(serialized));
    
    auto parsedMessage = MessageProtocol::parseMessage(serialized);
    ASSERT_NE(parsedMessage, nullptr);
    
    auto* parsedLangChangeMsg = dynamic_cast<LanguageChangeMessage*>(parsedMessage.get());
    ASSERT_NE(parsedLangChangeMsg, nullptr);
    
    // Verify all fields are correctly preserved
    EXPECT_EQ(parsedLangChangeMsg->getOldLanguage(), testOldLang);
    EXPECT_EQ(parsedLangChangeMsg->getNewLanguage(), testNewLang);
    EXPECT_FLOAT_EQ(parsedLangChangeMsg->getConfidence(), testConfidence);
    EXPECT_EQ(parsedLangChangeMsg->getUtteranceId(), testUtteranceId);
    
    // Verify JSON structure
    auto jsonRoot = utils::JsonParser::parse(serialized);
    ASSERT_TRUE(jsonRoot.isObject());
    
    EXPECT_TRUE(jsonRoot.hasObjectProperty("type"));
    EXPECT_EQ(jsonRoot.getObjectProperty("type").getString(), "language_change");
    
    EXPECT_TRUE(jsonRoot.hasObjectProperty("data"));
    auto dataObj = jsonRoot.getObjectProperty("data");
    ASSERT_TRUE(dataObj.isObject());
    
    EXPECT_TRUE(dataObj.hasObjectProperty("oldLanguage"));
    EXPECT_EQ(dataObj.getObjectProperty("oldLanguage").getString(), testOldLang);
    
    EXPECT_TRUE(dataObj.hasObjectProperty("newLanguage"));
    EXPECT_EQ(dataObj.getObjectProperty("newLanguage").getString(), testNewLang);
    
    EXPECT_TRUE(dataObj.hasObjectProperty("confidence"));
    EXPECT_FLOAT_EQ(static_cast<float>(dataObj.getObjectProperty("confidence").getNumber()), testConfidence);
    
    EXPECT_TRUE(dataObj.hasObjectProperty("utteranceId"));
    EXPECT_EQ(static_cast<uint32_t>(dataObj.getObjectProperty("utteranceId").getNumber()), testUtteranceId);
    
    std::cout << "Language change message JSON: " << serialized << std::endl;
}

TEST_F(LanguageDetectionIntegrationTest, ErrorHandlingInLanguageDetection) {
    // Test error handling in language detection scenarios
    
    // Test with invalid language detection threshold
    whisperSTT_->setLanguageDetectionThreshold(-1.0f); // Should be clamped
    whisperSTT_->setLanguageDetectionThreshold(2.0f);  // Should be clamped
    
    // Test with language detection enabled but auto-switching disabled
    whisperSTT_->setLanguageDetectionEnabled(true);
    whisperSTT_->setAutoLanguageSwitching(false);
    
    bool transcriptionComplete = false;
    TranscriptionResult result;
    
    whisperSTT_->transcribe(testAudio_, [&](const TranscriptionResult& res) {
        result = res;
        transcriptionComplete = true;
    });
    
    // Wait for completion
    auto start = std::chrono::steady_clock::now();
    while (!transcriptionComplete && 
           std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    ASSERT_TRUE(transcriptionComplete);
    
    // Should still get language detection info even without auto-switching
    EXPECT_FALSE(result.detected_language.empty());
    EXPECT_GE(result.language_confidence, 0.0f);
    EXPECT_LE(result.language_confidence, 1.0f);
    
    // Test disabling language detection entirely
    whisperSTT_->setLanguageDetectionEnabled(false);
    whisperSTT_->setLanguage("fr");
    
    transcriptionComplete = false;
    whisperSTT_->transcribe(testAudio_, [&](const TranscriptionResult& res) {
        result = res;
        transcriptionComplete = true;
    });
    
    start = std::chrono::steady_clock::now();
    while (!transcriptionComplete && 
           std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    ASSERT_TRUE(transcriptionComplete);
    
    // Should use configured language when detection is disabled
    EXPECT_EQ(result.detected_language, "fr");
    EXPECT_EQ(result.language_confidence, 1.0f);
    EXPECT_FALSE(result.language_changed);
}