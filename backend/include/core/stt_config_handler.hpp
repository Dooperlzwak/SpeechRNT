#pragma once

#include "stt/stt_config.hpp"
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>

namespace core {

/**
 * WebSocket message types for STT configuration
 */
enum class STTConfigMessageType {
    GET_CONFIG,
    UPDATE_CONFIG,
    UPDATE_CONFIG_VALUE,
    CONFIG_CHANGED,
    GET_SCHEMA,
    GET_METADATA,
    VALIDATE_CONFIG,
    RESET_CONFIG,
    GET_AVAILABLE_MODELS,
    GET_SUPPORTED_QUANTIZATION_LEVELS
};

/**
 * STT Configuration message structure
 */
struct STTConfigMessage {
    STTConfigMessageType type;
    std::string requestId;
    std::string data;
    bool success = true;
    std::string error;
    
    STTConfigMessage(STTConfigMessageType t, const std::string& reqId = "", const std::string& d = "")
        : type(t), requestId(reqId), data(d) {}
};

/**
 * WebSocket handler for STT configuration management
 * Handles configuration synchronization between frontend and backend
 */
class STTConfigHandler {
public:
    using MessageSender = std::function<void(const std::string& message)>;
    
    STTConfigHandler();
    ~STTConfigHandler() = default;
    
    /**
     * Initialize the configuration handler
     * @param configPath Path to STT configuration file
     * @param messageSender Function to send messages to frontend
     * @return true if initialized successfully
     */
    bool initialize(const std::string& configPath, MessageSender messageSender);
    
    /**
     * Handle incoming WebSocket message
     * @param message JSON message from frontend
     * @return true if message was handled
     */
    bool handleMessage(const std::string& message);
    
    /**
     * Get current STT configuration
     * @return Current configuration
     */
    stt::STTConfig getCurrentConfig() const;
    
    /**
     * Update STT configuration
     * @param config New configuration
     * @return Validation result
     */
    stt::ConfigValidationResult updateConfig(const stt::STTConfig& config);
    
    /**
     * Register callback for configuration changes
     * @param callback Callback function
     */
    void registerConfigChangeCallback(std::function<void(const stt::ConfigChangeNotification&)> callback);
    
    /**
     * Enable/disable automatic configuration broadcasting to frontend
     * @param enable true to enable broadcasting
     */
    void setConfigBroadcastEnabled(bool enable);
    
    /**
     * Broadcast current configuration to frontend
     */
    void broadcastCurrentConfig();
    
    /**
     * Get configuration handler statistics
     * @return JSON string with statistics
     */
    std::string getStatistics() const;

private:
    std::unique_ptr<stt::STTConfigManager> configManager_;
    MessageSender messageSender_;
    bool initialized_;
    bool broadcastEnabled_;
    
    // Statistics
    size_t messagesHandled_;
    size_t configUpdates_;
    size_t validationErrors_;
    std::chrono::steady_clock::time_point startTime_;
    
    // Message handling methods
    void handleGetConfig(const STTConfigMessage& message);
    void handleUpdateConfig(const STTConfigMessage& message);
    void handleUpdateConfigValue(const STTConfigMessage& message);
    void handleGetSchema(const STTConfigMessage& message);
    void handleGetMetadata(const STTConfigMessage& message);
    void handleValidateConfig(const STTConfigMessage& message);
    void handleResetConfig(const STTConfigMessage& message);
    void handleGetAvailableModels(const STTConfigMessage& message);
    void handleGetSupportedQuantizationLevels(const STTConfigMessage& message);
    
    // Helper methods
    STTConfigMessage parseMessage(const std::string& jsonMessage) const;
    std::string serializeMessage(const STTConfigMessage& message) const;
    void sendResponse(const STTConfigMessage& response);
    void sendConfigChangedNotification(const stt::ConfigChangeNotification& notification);
    void onConfigChange(const stt::ConfigChangeNotification& notification);
    
    // JSON parsing helpers
    std::string extractJsonValue(const std::string& json, const std::string& key) const;
    STTConfigMessageType stringToMessageType(const std::string& typeStr) const;
    std::string messageTypeToString(STTConfigMessageType type) const;
};

} // namespace core