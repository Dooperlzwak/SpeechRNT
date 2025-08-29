#include <gtest/gtest.h>
#include "core/stt_config_handler.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>

using namespace core;

class STTConfigHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        configHandler = std::make_unique<STTConfigHandler>();
        testConfigPath = "test_stt_handler_config.json";
        
        // Clean up any existing test files
        if (std::filesystem::exists(testConfigPath)) {
            std::filesystem::remove(testConfigPath);
        }
        
        // Create a test configuration file
        createTestConfigFile();
        
        // Set up message sender mock
        sentMessages.clear();
        messageSender = [this](const std::string& message) {
            sentMessages.push_back(message);
        };
    }
    
    void TearDown() override {
        // Clean up test files
        if (std::filesystem::exists(testConfigPath)) {
            std::filesystem::remove(testConfigPath);
        }
    }
    
    void createTestConfigFile() {
        std::string testConfig = R"({
  "model": {
    "defaultModel": "base",
    "modelsPath": "data/whisper/",
    "language": "auto",
    "translateToEnglish": false
  },
  "languageDetection": {
    "enabled": true,
    "threshold": 0.7,
    "autoSwitching": true,
    "consistentDetectionRequired": 2
  },
  "quantization": {
    "level": "AUTO",
    "enableGPUAcceleration": true,
    "gpuDeviceId": 0,
    "accuracyThreshold": 0.85
  },
  "streaming": {
    "partialResultsEnabled": true,
    "minChunkSizeMs": 1000,
    "maxChunkSizeMs": 10000,
    "overlapSizeMs": 200,
    "enableIncrementalUpdates": true
  },
  "confidence": {
    "threshold": 0.5,
    "wordLevelEnabled": true,
    "qualityIndicatorsEnabled": true,
    "filteringEnabled": false
  },
  "performance": {
    "threadCount": 4,
    "temperature": 0.0,
    "maxTokens": 0
  }
})";
        
        std::ofstream file(testConfigPath);
        file << testConfig;
        file.close();
    }
    
    std::string createGetConfigMessage(const std::string& requestId = "test-request-1") {
        return R"({"type": "GET_CONFIG", "requestId": ")" + requestId + R"(", "data": ""})";
    }
    
    std::string createUpdateConfigValueMessage(const std::string& section, 
                                             const std::string& key, 
                                             const std::string& value,
                                             const std::string& requestId = "test-request-2") {
        return R"({"type": "UPDATE_CONFIG_VALUE", "requestId": ")" + requestId + 
               R"(", "data": {"section": ")" + section + 
               R"(", "key": ")" + key + 
               R"(", "value": ")" + value + R"("}})";
    }
    
    std::string createGetSchemaMessage(const std::string& requestId = "test-request-3") {
        return R"({"type": "GET_SCHEMA", "requestId": ")" + requestId + R"(", "data": ""})";
    }
    
    std::string createValidateConfigMessage(const std::string& config, 
                                          const std::string& requestId = "test-request-4") {
        return R"({"type": "VALIDATE_CONFIG", "requestId": ")" + requestId + 
               R"(", "data": )" + config + R"(})";
    }
    
    std::unique_ptr<STTConfigHandler> configHandler;
    std::string testConfigPath;
    std::function<void(const std::string&)> messageSender;
    std::vector<std::string> sentMessages;
};

TEST_F(STTConfigHandlerTest, Initialization) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    // Test double initialization
    EXPECT_FALSE(configHandler->initialize(testConfigPath, messageSender));
}

TEST_F(STTConfigHandlerTest, InitializationWithNonExistentFile) {
    std::string nonExistentPath = "non_existent_config.json";
    
    // Should still succeed and create default configuration
    EXPECT_TRUE(configHandler->initialize(nonExistentPath, messageSender));
    
    auto config = configHandler->getCurrentConfig();
    EXPECT_EQ(config.defaultModel, "base"); // Default value
}

TEST_F(STTConfigHandlerTest, GetConfigMessage) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    std::string message = createGetConfigMessage();
    EXPECT_TRUE(configHandler->handleMessage(message));
    
    EXPECT_EQ(sentMessages.size(), 1);
    
    std::string response = sentMessages[0];
    EXPECT_TRUE(response.find("\"type\": \"GET_CONFIG\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"requestId\": \"test-request-1\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"success\": true") != std::string::npos);
    EXPECT_TRUE(response.find("\"defaultModel\"") != std::string::npos);
}

TEST_F(STTConfigHandlerTest, UpdateConfigValueMessage) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    std::string message = createUpdateConfigValueMessage("model", "defaultModel", "large");
    EXPECT_TRUE(configHandler->handleMessage(message));
    
    EXPECT_EQ(sentMessages.size(), 2); // Response + change notification
    
    // Check response message
    std::string response = sentMessages[0];
    EXPECT_TRUE(response.find("\"type\": \"UPDATE_CONFIG_VALUE\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"success\": true") != std::string::npos);
    
    // Check change notification
    std::string notification = sentMessages[1];
    EXPECT_TRUE(notification.find("\"type\": \"CONFIG_CHANGED\"") != std::string::npos);
    
    // Verify configuration was actually updated
    auto config = configHandler->getCurrentConfig();
    EXPECT_EQ(config.defaultModel, "large");
}

TEST_F(STTConfigHandlerTest, UpdateConfigValueInvalidSection) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    std::string message = createUpdateConfigValueMessage("invalid_section", "key", "value");
    EXPECT_TRUE(configHandler->handleMessage(message));
    
    EXPECT_EQ(sentMessages.size(), 1); // Only response, no change notification
    
    std::string response = sentMessages[0];
    EXPECT_TRUE(response.find("\"success\": false") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\"") != std::string::npos);
}

TEST_F(STTConfigHandlerTest, UpdateConfigValueInvalidValue) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    // Try to set an invalid boolean value
    std::string message = createUpdateConfigValueMessage("languageDetection", "enabled", "invalid_bool");
    EXPECT_TRUE(configHandler->handleMessage(message));
    
    EXPECT_EQ(sentMessages.size(), 1); // Only response, no change notification
    
    std::string response = sentMessages[0];
    EXPECT_TRUE(response.find("\"success\": false") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\"") != std::string::npos);
}

TEST_F(STTConfigHandlerTest, GetSchemaMessage) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    std::string message = createGetSchemaMessage();
    EXPECT_TRUE(configHandler->handleMessage(message));
    
    EXPECT_EQ(sentMessages.size(), 1);
    
    std::string response = sentMessages[0];
    EXPECT_TRUE(response.find("\"type\": \"GET_SCHEMA\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"success\": true") != std::string::npos);
    EXPECT_TRUE(response.find("\"type\": \"object\"") != std::string::npos); // JSON schema content
}

TEST_F(STTConfigHandlerTest, GetMetadataMessage) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    std::string message = R"({"type": "GET_METADATA", "requestId": "test-request", "data": ""})";
    EXPECT_TRUE(configHandler->handleMessage(message));
    
    EXPECT_EQ(sentMessages.size(), 1);
    
    std::string response = sentMessages[0];
    EXPECT_TRUE(response.find("\"type\": \"GET_METADATA\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"success\": true") != std::string::npos);
    EXPECT_TRUE(response.find("\"description\"") != std::string::npos); // Metadata content
}

TEST_F(STTConfigHandlerTest, ValidateConfigMessage) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    std::string validConfig = R"({"model": {"defaultModel": "base"}})";
    std::string message = createValidateConfigMessage(validConfig);
    EXPECT_TRUE(configHandler->handleMessage(message));
    
    EXPECT_EQ(sentMessages.size(), 1);
    
    std::string response = sentMessages[0];
    EXPECT_TRUE(response.find("\"type\": \"VALIDATE_CONFIG\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"success\": true") != std::string::npos);
    EXPECT_TRUE(response.find("\"isValid\"") != std::string::npos);
}

TEST_F(STTConfigHandlerTest, ResetConfigMessage) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    // First, modify the configuration
    std::string updateMessage = createUpdateConfigValueMessage("model", "defaultModel", "large");
    EXPECT_TRUE(configHandler->handleMessage(updateMessage));
    
    auto config = configHandler->getCurrentConfig();
    EXPECT_EQ(config.defaultModel, "large");
    
    // Clear sent messages
    sentMessages.clear();
    
    // Reset configuration
    std::string resetMessage = R"({"type": "RESET_CONFIG", "requestId": "test-reset", "data": ""})";
    EXPECT_TRUE(configHandler->handleMessage(resetMessage));
    
    EXPECT_EQ(sentMessages.size(), 2); // Response + change notification
    
    std::string response = sentMessages[0];
    EXPECT_TRUE(response.find("\"type\": \"RESET_CONFIG\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"success\": true") != std::string::npos);
    
    // Verify configuration was reset
    config = configHandler->getCurrentConfig();
    EXPECT_EQ(config.defaultModel, "base"); // Back to default
}

TEST_F(STTConfigHandlerTest, GetAvailableModelsMessage) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    std::string message = R"({"type": "GET_AVAILABLE_MODELS", "requestId": "test-models", "data": ""})";
    EXPECT_TRUE(configHandler->handleMessage(message));
    
    EXPECT_EQ(sentMessages.size(), 1);
    
    std::string response = sentMessages[0];
    EXPECT_TRUE(response.find("\"type\": \"GET_AVAILABLE_MODELS\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"success\": true") != std::string::npos);
    EXPECT_TRUE(response.find("[") != std::string::npos); // Array of models
}

TEST_F(STTConfigHandlerTest, GetSupportedQuantizationLevelsMessage) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    std::string message = R"({"type": "GET_SUPPORTED_QUANTIZATION_LEVELS", "requestId": "test-quant", "data": ""})";
    EXPECT_TRUE(configHandler->handleMessage(message));
    
    EXPECT_EQ(sentMessages.size(), 1);
    
    std::string response = sentMessages[0];
    EXPECT_TRUE(response.find("\"type\": \"GET_SUPPORTED_QUANTIZATION_LEVELS\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"success\": true") != std::string::npos);
    EXPECT_TRUE(response.find("[") != std::string::npos); // Array of levels
}

TEST_F(STTConfigHandlerTest, ConfigChangeCallback) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    bool callbackCalled = false;
    stt::ConfigChangeNotification receivedNotification("", "", "", "");
    
    configHandler->registerConfigChangeCallback(
        [&](const stt::ConfigChangeNotification& notification) {
            callbackCalled = true;
            receivedNotification = notification;
        }
    );
    
    // Update configuration to trigger callback
    std::string message = createUpdateConfigValueMessage("model", "defaultModel", "small");
    EXPECT_TRUE(configHandler->handleMessage(message));
    
    EXPECT_TRUE(callbackCalled);
    EXPECT_EQ(receivedNotification.section, "model");
    EXPECT_EQ(receivedNotification.key, "defaultModel");
    EXPECT_EQ(receivedNotification.newValue, "small");
}

TEST_F(STTConfigHandlerTest, BroadcastConfiguration) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    // Clear any initialization messages
    sentMessages.clear();
    
    configHandler->broadcastCurrentConfig();
    
    EXPECT_EQ(sentMessages.size(), 1);
    
    std::string broadcast = sentMessages[0];
    EXPECT_TRUE(broadcast.find("\"type\": \"CONFIG_CHANGED\"") != std::string::npos);
    EXPECT_TRUE(broadcast.find("\"defaultModel\"") != std::string::npos);
}

TEST_F(STTConfigHandlerTest, BroadcastDisabled) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    configHandler->setConfigBroadcastEnabled(false);
    
    // Clear any initialization messages
    sentMessages.clear();
    
    configHandler->broadcastCurrentConfig();
    
    EXPECT_EQ(sentMessages.size(), 0); // No broadcast when disabled
}

TEST_F(STTConfigHandlerTest, Statistics) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    // Handle some messages to generate statistics
    std::string message1 = createGetConfigMessage();
    std::string message2 = createUpdateConfigValueMessage("model", "defaultModel", "large");
    
    EXPECT_TRUE(configHandler->handleMessage(message1));
    EXPECT_TRUE(configHandler->handleMessage(message2));
    
    std::string stats = configHandler->getStatistics();
    
    EXPECT_FALSE(stats.empty());
    EXPECT_TRUE(stats.find("\"initialized\": true") != std::string::npos);
    EXPECT_TRUE(stats.find("\"messagesHandled\": 2") != std::string::npos);
    EXPECT_TRUE(stats.find("\"configUpdates\": 1") != std::string::npos);
    EXPECT_TRUE(stats.find("\"uptimeMs\"") != std::string::npos);
}

TEST_F(STTConfigHandlerTest, InvalidMessageHandling) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    // Test malformed JSON
    std::string invalidJson = R"({"type": "GET_CONFIG", "requestId": "test", "data":})";
    EXPECT_FALSE(configHandler->handleMessage(invalidJson));
    
    // Test unknown message type
    std::string unknownType = R"({"type": "UNKNOWN_TYPE", "requestId": "test", "data": ""})";
    EXPECT_FALSE(configHandler->handleMessage(unknownType));
}

TEST_F(STTConfigHandlerTest, ConcurrentMessageHandling) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    const int numThreads = 5;
    const int messagesPerThread = 10;
    std::vector<std::thread> threads;
    std::atomic<int> successCount(0);
    
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < messagesPerThread; ++j) {
                std::string requestId = "thread-" + std::to_string(i) + "-msg-" + std::to_string(j);
                std::string message = createGetConfigMessage(requestId);
                
                if (configHandler->handleMessage(message)) {
                    successCount++;
                }
                
                // Small delay to simulate realistic usage
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(successCount.load(), numThreads * messagesPerThread);
    
    // Verify statistics reflect all handled messages
    std::string stats = configHandler->getStatistics();
    EXPECT_TRUE(stats.find("\"messagesHandled\": " + std::to_string(numThreads * messagesPerThread)) != std::string::npos);
}

TEST_F(STTConfigHandlerTest, ConfigurationPersistence) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    // Update configuration
    std::string message = createUpdateConfigValueMessage("model", "defaultModel", "large");
    EXPECT_TRUE(configHandler->handleMessage(message));
    
    // Create new handler and load same configuration file
    auto newHandler = std::make_unique<STTConfigHandler>();
    std::vector<std::string> newMessages;
    auto newMessageSender = [&newMessages](const std::string& msg) {
        newMessages.push_back(msg);
    };
    
    EXPECT_TRUE(newHandler->initialize(testConfigPath, newMessageSender));
    
    // Verify configuration was persisted
    auto config = newHandler->getCurrentConfig();
    EXPECT_EQ(config.defaultModel, "large");
}

// Performance test for message handling
TEST_F(STTConfigHandlerTest, MessageHandlingPerformance) {
    EXPECT_TRUE(configHandler->initialize(testConfigPath, messageSender));
    
    const int numMessages = 1000;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < numMessages; ++i) {
        std::string requestId = "perf-test-" + std::to_string(i);
        std::string message = createGetConfigMessage(requestId);
        EXPECT_TRUE(configHandler->handleMessage(message));
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "Handled " << numMessages << " messages in " << duration.count() << "ms" << std::endl;
    std::cout << "Average: " << (duration.count() / static_cast<double>(numMessages)) << "ms per message" << std::endl;
    
    // Performance should be reasonable (less than 1ms per message on average)
    EXPECT_LT(duration.count() / static_cast<double>(numMessages), 1.0);
    
    // Verify all messages were handled
    EXPECT_EQ(sentMessages.size(), numMessages);
}