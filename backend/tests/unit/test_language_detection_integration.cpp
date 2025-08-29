#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "core/translation_pipeline.hpp"
#include "core/pipeline_websocket_integration.hpp"
#include "mt/language_detector.hpp"
#include "stt/stt_interface.hpp"
#include "mt/translation_interface.hpp"
#include "core/task_queue.hpp"
#include "core/websocket_server.hpp"
#include <memory>
#include <thread>
#include <chrono>

using namespace speechrnt;
using namespace testing;

// Mock implementations for testing
class MockSTTInterface : public stt::STTInterface {
public:
    MOCK_METHOD(bool, initialize, (const std::string& modelPath, int n_threads), (override));
    MOCK_METHOD(void, transcribe, (const std::vector<float>& audioData, TranscriptionCallback callback), (override));
    MOCK_METHOD(void, transcribeLive, (const std::vector<float>& audioData, TranscriptionCallback callback), (override));
    MOCK_METHOD(void, setLanguage, (const std::string& language), (override));
    MOCK_METHOD(void, setTranslateToEnglish, (bool translate), (override));
    MOCK_METHOD(void, setTemperature, (float temperature), (override));
    MOCK_METHOD(void, setMaxTokens, (int max_tokens), (override));
    MOCK_METHOD(void, setLanguageDetectionEnabled, (bool enabled), (override));
    MOCK_METHOD(void, setLanguageDetectionThreshold, (float threshold), (override));
    MOCK_METHOD(void, setAutoLanguageSwitching, (bool enabled), (override));
    MOCK_METHOD(bool, isInitialized, (), (const, override));
    MOCK_METHOD(std::string, getLastError, (), (const, override));
};

class MockTranslationInterface : public mt::TranslationInterface {
public:
    MOCK_METHOD(bool, initialize, (const std::string& sourceLang, const std::string& targetLang), (override));
    MOCK_METHOD(mt::TranslationResult, translate, (const std::string& text), (override));
    MOCK_METHOD(void, cleanup, (), (override));
    MOCK_METHOD(bool, isInitialized, (), (const, override));
    MOCK_METHOD(std::string, getLastError, (), (const, override));
    MOCK_METHOD(bool, initializeWithGPU, (const std::string& sourceLang, const std::string& targetLang, int gpuDeviceId), (override));
    MOCK_METHOD(void, setGPUAcceleration, (bool enabled, int deviceId), (override));
    MOCK_METHOD(std::vector<mt::TranslationResult>, translateBatch, (const std::vector<std::string>& texts), (override));
    MOCK_METHOD(std::future<std::vector<mt::TranslationResult>>, translateBatchAsync, (const std::vector<std::string>& texts), (override));
    MOCK_METHOD(void, startStreamingTranslation, (const std::string& sessionId), (override));
    MOCK_METHOD(mt::TranslationResult, addStreamingText, (const std::string& sessionId, const std::string& text), (override));
    MOCK_METHOD(mt::TranslationResult, finalizeStreamingTranslation, (const std::string& sessionId), (override));
    MOCK_METHOD(float, calculateTranslationConfidence, (const std::string& sourceText, const std::string& translatedText), (override));
    MOCK_METHOD(std::vector<mt::TranslationResult>, getTranslationCandidates, (const std::string& text, int maxCandidates), (override));
    MOCK_METHOD(bool, preloadModel, (const std::string& sourceLang, const std::string& targetLang), (override));
    MOCK_METHOD(void, setModelQuantization, (bool enabled, const std::string& quantizationType), (override));
    MOCK_METHOD(bool, isModelQuantizationSupported, (), (const, override));
};

class MockWebSocketServer : public core::WebSocketServer {
public:
    MockWebSocketServer() : WebSocketServer(8080) {}
    
    MOCK_METHOD(void, sendMessage, (const std::string& sessionId, const std::string& message), (override));
    MOCK_METHOD(void, sendBinaryMessage, (const std::string& sessionId, const std::vector<uint8_t>& data), (override));
};

class LanguageDetectionIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create mock components
        mock_stt_ = std::make_shared<MockSTTInterface>();
        mock_mt_ = std::make_shared<MockTranslationInterface>();
        mock_websocket_ = std::make_shared<MockWebSocketServer>();
        
        // Create real components
        language_detector_ = std::make_shared<mt::LanguageDetector>();
        task_queue_ = std::make_shared<core::TaskQueue>(4);
        
        // Initialize language detector
        ASSERT_TRUE(language_detector_->initialize());
        
        // Create pipeline with language detection enabled
        core::TranslationPipelineConfig config;
        config.enable_language_detection = true;
        config.enable_automatic_language_switching = true;
        config.language_detection_confidence_threshold = 0.7f;
        config.enable_language_detection_caching = true;
        config.notify_language_changes = true;
        
        pipeline_ = std::make_shared<core::TranslationPipeline>(config);
        
        // Initialize pipeline
        ASSERT_TRUE(pipeline_->initialize(mock_stt_, mock_mt_, language_detector_, task_queue_));
        
        // Create integration
        integration_ = std::make_shared<core::PipelineWebSocketIntegration>(pipeline_, mock_websocket_);
        ASSERT_TRUE(integration_->initialize());
        
        // Set up language configuration
        pipeline_->setLanguageConfiguration("en", "es");
    }
    
    void TearDown() override {
        if (integration_) {
            integration_->shutdown();
        }
        if (pipeline_) {
            pipeline_->shutdown();
        }
        if (task_queue_) {
            task_queue_->shutdown();
        }
        if (language_detector_) {
            language_detector_->cleanup();
        }
    }
    
    std::shared_ptr<MockSTTInterface> mock_stt_;
    std::shared_ptr<MockTranslationInterface> mock_mt_;
    std::shared_ptr<MockWebSocketServer> mock_websocket_;
    std::shared_ptr<mt::LanguageDetector> language_detector_;
    std::shared_ptr<core::TaskQueue> task_queue_;
    std::shared_ptr<core::TranslationPipeline> pipeline_;
    std::shared_ptr<core::PipelineWebSocketIntegration> integration_;
};

TEST_F(LanguageDetectionIntegrationTest, LanguageDetectionTriggered) {
    // Set up expectations
    EXPECT_CALL(*mock_websocket_, sendMessage(_, _))
        .Times(AtLeast(1));
    
    // Create transcription result that should trigger language detection
    stt::TranscriptionResult transcription;
    transcription.text = "Hola, ¿cómo estás? Me llamo Juan y soy de España.";
    transcription.confidence = 0.9f;
    transcription.meets_confidence_threshold = true;
    transcription.is_partial = false;
    
    // Process transcription
    pipeline_->processTranscriptionResult(1, "test_session", transcription);
    
    // Wait for processing to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify language detection was performed
    auto stats = pipeline_->getStatistics();
    EXPECT_GT(stats.language_detections_performed, 0);
}

TEST_F(LanguageDetectionIntegrationTest, LanguageChangeNotification) {
    std::string received_message;
    bool message_received = false;
    
    // Set up expectation to capture the message
    EXPECT_CALL(*mock_websocket_, sendMessage("test_session", _))
        .WillRepeatedly([&](const std::string& sessionId, const std::string& message) {
            if (message.find("language_change") != std::string::npos) {
                received_message = message;
                message_received = true;
            }
        });
    
    // First, process English text to establish baseline
    stt::TranscriptionResult english_transcription;
    english_transcription.text = "Hello, how are you? My name is John and I am from America.";
    english_transcription.confidence = 0.9f;
    english_transcription.meets_confidence_threshold = true;
    
    pipeline_->processTranscriptionResult(1, "test_session", english_transcription);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Then process Spanish text to trigger language change
    stt::TranscriptionResult spanish_transcription;
    spanish_transcription.text = "Hola, ¿cómo estás? Me llamo Juan y soy de España.";
    spanish_transcription.confidence = 0.9f;
    spanish_transcription.meets_confidence_threshold = true;
    
    pipeline_->processTranscriptionResult(2, "test_session", spanish_transcription);
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Verify language change was detected and notified
    EXPECT_TRUE(message_received);
    EXPECT_THAT(received_message, HasSubstr("language_change"));
    EXPECT_THAT(received_message, HasSubstr("test_session"));
    
    auto stats = pipeline_->getStatistics();
    EXPECT_GT(stats.language_changes_detected, 0);
}

TEST_F(LanguageDetectionIntegrationTest, LanguageDetectionCaching) {
    // Set up expectations
    EXPECT_CALL(*mock_websocket_, sendMessage(_, _))
        .Times(AtLeast(1));
    
    // Create identical transcription results
    stt::TranscriptionResult transcription;
    transcription.text = "Bonjour, comment allez-vous?";
    transcription.confidence = 0.9f;
    transcription.meets_confidence_threshold = true;
    
    // Process same text twice
    pipeline_->processTranscriptionResult(1, "test_session", transcription);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    pipeline_->processTranscriptionResult(2, "test_session", transcription);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Verify caching worked
    auto stats = pipeline_->getStatistics();
    EXPECT_GT(stats.language_detection_cache_hits, 0);
}

TEST_F(LanguageDetectionIntegrationTest, ManualLanguageDetectionTrigger) {
    // Set up expectations
    EXPECT_CALL(*mock_websocket_, sendMessage(_, _))
        .Times(AtLeast(1));
    
    // Manually trigger language detection
    pipeline_->triggerLanguageDetection(1, "test_session", "Guten Tag, wie geht es Ihnen?");
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify detection was performed
    auto stats = pipeline_->getStatistics();
    EXPECT_GT(stats.language_detections_performed, 0);
}

TEST_F(LanguageDetectionIntegrationTest, ConfidenceGating) {
    // Set up expectations - should not send translation messages for low confidence
    EXPECT_CALL(*mock_websocket_, sendMessage(_, _))
        .Times(AtLeast(1)); // Will still send language detection results
    
    // Create low confidence transcription
    stt::TranscriptionResult transcription;
    transcription.text = "Hello world";
    transcription.confidence = 0.3f; // Below threshold
    transcription.meets_confidence_threshold = false;
    
    pipeline_->processTranscriptionResult(1, "test_session", transcription);
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify confidence gating worked
    auto stats = pipeline_->getStatistics();
    EXPECT_EQ(stats.translations_triggered, 0);
    EXPECT_GT(stats.confidence_gate_rejections, 0);
}

TEST_F(LanguageDetectionIntegrationTest, ConfigurationMethods) {
    // Test configuration setters
    pipeline_->setLanguageDetectionEnabled(false);
    pipeline_->setAutomaticLanguageSwitchingEnabled(false);
    pipeline_->setLanguageDetectionConfidenceThreshold(0.9f);
    pipeline_->setLanguageDetectionCachingEnabled(false);
    pipeline_->setLanguageChangeNotificationsEnabled(false);
    
    // Verify configuration was applied
    auto config = pipeline_->getConfiguration();
    EXPECT_FALSE(config.enable_language_detection);
    EXPECT_FALSE(config.enable_automatic_language_switching);
    EXPECT_FLOAT_EQ(config.language_detection_confidence_threshold, 0.9f);
    EXPECT_FALSE(config.enable_language_detection_caching);
    EXPECT_FALSE(config.notify_language_changes);
}

TEST_F(LanguageDetectionIntegrationTest, CacheManagement) {
    // Add some cached results
    stt::TranscriptionResult transcription;
    transcription.text = "Test text for caching";
    transcription.confidence = 0.9f;
    transcription.meets_confidence_threshold = true;
    
    pipeline_->processTranscriptionResult(1, "test_session", transcription);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Clear cache
    pipeline_->clearLanguageDetectionCache();
    
    // Process same text again - should not hit cache
    pipeline_->processTranscriptionResult(2, "test_session", transcription);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Verify cache was cleared
    auto stats = pipeline_->getStatistics();
    EXPECT_EQ(stats.language_detection_cache_hits, 0);
}

} // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}