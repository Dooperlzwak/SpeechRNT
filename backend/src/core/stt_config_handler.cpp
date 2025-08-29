#include "core/stt_config_handler.hpp"
#include <iostream>
#include <sstream>
#include <regex>
#include <chrono>

namespace core {

STTConfigHandler::STTConfigHandler()
    : configManager_(std::make_unique<stt::STTConfigManager>())
    , initialized_(false)
    , broadcastEnabled_(true)
    , messagesHandled_(0)
    , configUpdates_(0)
    , validationErrors_(0)
    , startTime_(std::chrono::steady_clock::now()) {
}

bool STTConfigHandler::initialize(const std::string& configPath, MessageSender messageSender) {
    if (initialized_) {
        std::cerr << "STTConfigHandler already initialized" << std::endl;
        return false;
    }
    
    messageSender_ = messageSender;
    
    if (!configManager_->loadFromFile(configPath)) {
        std::cerr << "Failed to load STT configuration from: " << configPath << std::endl;
        return false;
    }
    
    // Register for configuration change notifications
    configManager_->registerChangeCallback(
        [this](const stt::ConfigChangeNotification& notification) {
            onConfigChange(notification);
        }
    );
    
    // Enable auto-save for configuration changes
    configManager_->setAutoSave(true);
    
    initialized_ = true;
    std::cout << "STTConfigHandler initialized with config: " << configPath << std::endl;
    
    return true;
}

bool STTConfigHandler::handleMessage(const std::string& message) {
    if (!initialized_) {
        std::cerr << "STTConfigHandler not initialized" << std::endl;
        return false;
    }
    
    try {
        messagesHandled_++;
        
        STTConfigMessage configMessage = parseMessage(message);
        
        switch (configMessage.type) {
            case STTConfigMessageType::GET_CONFIG:
                handleGetConfig(configMessage);
                break;
            case STTConfigMessageType::UPDATE_CONFIG:
                handleUpdateConfig(configMessage);
                break;
            case STTConfigMessageType::UPDATE_CONFIG_VALUE:
                handleUpdateConfigValue(configMessage);
                break;
            case STTConfigMessageType::GET_SCHEMA:
                handleGetSchema(configMessage);
                break;
            case STTConfigMessageType::GET_METADATA:
                handleGetMetadata(configMessage);
                break;
            case STTConfigMessageType::VALIDATE_CONFIG:
                handleValidateConfig(configMessage);
                break;
            case STTConfigMessageType::RESET_CONFIG:
                handleResetConfig(configMessage);
                break;
            case STTConfigMessageType::GET_AVAILABLE_MODELS:
                handleGetAvailableModels(configMessage);
                break;
            case STTConfigMessageType::GET_SUPPORTED_QUANTIZATION_LEVELS:
                handleGetSupportedQuantizationLevels(configMessage);
                break;
            default:
                std::cerr << "Unknown STT config message type" << std::endl;
                return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error handling STT config message: " << e.what() << std::endl;
        return false;
    }
}

stt::STTConfig STTConfigHandler::getCurrentConfig() const {
    if (!initialized_) {
        return stt::STTConfig();
    }
    return configManager_->getConfig();
}

stt::ConfigValidationResult STTConfigHandler::updateConfig(const stt::STTConfig& config) {
    if (!initialized_) {
        stt::ConfigValidationResult result;
        result.addError("STTConfigHandler not initialized");
        return result;
    }
    
    configUpdates_++;
    return configManager_->updateConfig(config);
}

void STTConfigHandler::registerConfigChangeCallback(std::function<void(const stt::ConfigChangeNotification&)> callback) {
    configManager_->registerChangeCallback(callback);
}

void STTConfigHandler::setConfigBroadcastEnabled(bool enable) {
    broadcastEnabled_ = enable;
}

void STTConfigHandler::broadcastCurrentConfig() {
    if (!initialized_ || !broadcastEnabled_) {
        return;
    }
    
    STTConfigMessage message(STTConfigMessageType::CONFIG_CHANGED);
    message.data = configManager_->exportToJson();
    sendResponse(message);
}

std::string STTConfigHandler::getStatistics() const {
    auto now = std::chrono::steady_clock::now();
    auto uptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
    
    std::ostringstream stats;
    stats << "{\n";
    stats << "  \"initialized\": " << (initialized_ ? "true" : "false") << ",\n";
    stats << "  \"uptimeMs\": " << uptimeMs << ",\n";
    stats << "  \"messagesHandled\": " << messagesHandled_ << ",\n";
    stats << "  \"configUpdates\": " << configUpdates_ << ",\n";
    stats << "  \"validationErrors\": " << validationErrors_ << ",\n";
    stats << "  \"broadcastEnabled\": " << (broadcastEnabled_ ? "true" : "false") << ",\n";
    stats << "  \"configModified\": " << (configManager_->isModified() ? "true" : "false") << ",\n";
    stats << "  \"configPath\": \"" << configManager_->getConfigFilePath() << "\"\n";
    stats << "}";
    
    return stats.str();
}

// Message handling methods
void STTConfigHandler::handleGetConfig(const STTConfigMessage& message) {
    STTConfigMessage response(STTConfigMessageType::GET_CONFIG, message.requestId);
    response.data = configManager_->exportToJson();
    sendResponse(response);
}

void STTConfigHandler::handleUpdateConfig(const STTConfigMessage& message) {
    STTConfigMessage response(STTConfigMessageType::UPDATE_CONFIG, message.requestId);
    
    if (!configManager_->loadFromJson(message.data)) {
        response.success = false;
        response.error = "Failed to parse configuration JSON";
        validationErrors_++;
    } else {
        configUpdates_++;
        response.data = configManager_->exportToJson();
    }
    
    sendResponse(response);
}

void STTConfigHandler::handleUpdateConfigValue(const STTConfigMessage& message) {
    STTConfigMessage response(STTConfigMessageType::UPDATE_CONFIG_VALUE, message.requestId);
    
    // Parse section, key, and value from message data
    // Expected format: {"section": "model", "key": "defaultModel", "value": "base"}
    std::string section = extractJsonValue(message.data, "section");
    std::string key = extractJsonValue(message.data, "key");
    std::string value = extractJsonValue(message.data, "value");
    
    if (section.empty() || key.empty()) {
        response.success = false;
        response.error = "Missing section or key in update request";
        validationErrors_++;
    } else {
        auto validationResult = configManager_->updateConfigValue(section, key, value);
        
        if (!validationResult.isValid) {
            response.success = false;
            response.error = "Validation failed: ";
            for (const auto& error : validationResult.errors) {
                response.error += error + "; ";
            }
            validationErrors_++;
        } else {
            configUpdates_++;
            response.data = configManager_->exportToJson();
            
            // Include warnings if any
            if (validationResult.hasWarnings()) {
                response.data += ", \"warnings\": [";
                for (size_t i = 0; i < validationResult.warnings.size(); ++i) {
                    if (i > 0) response.data += ", ";
                    response.data += "\"" + validationResult.warnings[i] + "\"";
                }
                response.data += "]";
            }
        }
    }
    
    sendResponse(response);
}

void STTConfigHandler::handleGetSchema(const STTConfigMessage& message) {
    STTConfigMessage response(STTConfigMessageType::GET_SCHEMA, message.requestId);
    response.data = configManager_->getConfigSchema();
    sendResponse(response);
}

void STTConfigHandler::handleGetMetadata(const STTConfigMessage& message) {
    STTConfigMessage response(STTConfigMessageType::GET_METADATA, message.requestId);
    response.data = configManager_->getConfigMetadata();
    sendResponse(response);
}

void STTConfigHandler::handleValidateConfig(const STTConfigMessage& message) {
    STTConfigMessage response(STTConfigMessageType::VALIDATE_CONFIG, message.requestId);
    
    // Parse configuration from message data
    stt::STTConfig config;
    if (!configManager_->loadFromJson(message.data)) {
        response.success = false;
        response.error = "Failed to parse configuration JSON";
        validationErrors_++;
    } else {
        auto validationResult = configManager_->validateConfig(config);
        
        std::ostringstream resultJson;
        resultJson << "{\n";
        resultJson << "  \"isValid\": " << (validationResult.isValid ? "true" : "false") << ",\n";
        resultJson << "  \"errors\": [";
        for (size_t i = 0; i < validationResult.errors.size(); ++i) {
            if (i > 0) resultJson << ", ";
            resultJson << "\"" << validationResult.errors[i] << "\"";
        }
        resultJson << "],\n";
        resultJson << "  \"warnings\": [";
        for (size_t i = 0; i < validationResult.warnings.size(); ++i) {
            if (i > 0) resultJson << ", ";
            resultJson << "\"" << validationResult.warnings[i] << "\"";
        }
        resultJson << "]\n";
        resultJson << "}";
        
        response.data = resultJson.str();
        
        if (!validationResult.isValid) {
            validationErrors_++;
        }
    }
    
    sendResponse(response);
}

void STTConfigHandler::handleResetConfig(const STTConfigMessage& message) {
    STTConfigMessage response(STTConfigMessageType::RESET_CONFIG, message.requestId);
    
    configManager_->resetToDefaults();
    configUpdates_++;
    response.data = configManager_->exportToJson();
    
    sendResponse(response);
}

void STTConfigHandler::handleGetAvailableModels(const STTConfigMessage& message) {
    STTConfigMessage response(STTConfigMessageType::GET_AVAILABLE_MODELS, message.requestId);
    
    auto models = configManager_->getAvailableModels();
    
    std::ostringstream modelsJson;
    modelsJson << "[";
    for (size_t i = 0; i < models.size(); ++i) {
        if (i > 0) modelsJson << ", ";
        modelsJson << "\"" << models[i] << "\"";
    }
    modelsJson << "]";
    
    response.data = modelsJson.str();
    sendResponse(response);
}

void STTConfigHandler::handleGetSupportedQuantizationLevels(const STTConfigMessage& message) {
    STTConfigMessage response(STTConfigMessageType::GET_SUPPORTED_QUANTIZATION_LEVELS, message.requestId);
    
    auto levels = configManager_->getSupportedQuantizationLevels();
    
    std::ostringstream levelsJson;
    levelsJson << "[";
    for (size_t i = 0; i < levels.size(); ++i) {
        if (i > 0) levelsJson << ", ";
        levelsJson << "\"" << configManager_->getQuantizationManager().levelToString(levels[i]) << "\"";
    }
    levelsJson << "]";
    
    response.data = levelsJson.str();
    sendResponse(response);
}

// Helper methods
STTConfigMessage STTConfigHandler::parseMessage(const std::string& jsonMessage) const {
    // Simplified JSON parsing - in production, use a proper JSON library
    std::string type = extractJsonValue(jsonMessage, "type");
    std::string requestId = extractJsonValue(jsonMessage, "requestId");
    std::string data = extractJsonValue(jsonMessage, "data");
    
    STTConfigMessageType messageType = stringToMessageType(type);
    
    return STTConfigMessage(messageType, requestId, data);
}

std::string STTConfigHandler::serializeMessage(const STTConfigMessage& message) const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"type\": \"" << messageTypeToString(message.type) << "\",\n";
    json << "  \"requestId\": \"" << message.requestId << "\",\n";
    json << "  \"success\": " << (message.success ? "true" : "false") << ",\n";
    
    if (!message.error.empty()) {
        json << "  \"error\": \"" << message.error << "\",\n";
    }
    
    if (!message.data.empty()) {
        json << "  \"data\": " << message.data << "\n";
    } else {
        json << "  \"data\": null\n";
    }
    
    json << "}";
    
    return json.str();
}

void STTConfigHandler::sendResponse(const STTConfigMessage& response) {
    if (messageSender_) {
        std::string serialized = serializeMessage(response);
        messageSender_(serialized);
    }
}

void STTConfigHandler::sendConfigChangedNotification(const stt::ConfigChangeNotification& notification) {
    if (!broadcastEnabled_) {
        return;
    }
    
    STTConfigMessage message(STTConfigMessageType::CONFIG_CHANGED);
    
    std::ostringstream notificationJson;
    notificationJson << "{\n";
    notificationJson << "  \"section\": \"" << notification.section << "\",\n";
    notificationJson << "  \"key\": \"" << notification.key << "\",\n";
    notificationJson << "  \"oldValue\": \"" << notification.oldValue << "\",\n";
    notificationJson << "  \"newValue\": \"" << notification.newValue << "\",\n";
    notificationJson << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::milliseconds>(
        notification.timestamp.time_since_epoch()).count() << ",\n";
    notificationJson << "  \"config\": " << configManager_->exportToJson() << "\n";
    notificationJson << "}";
    
    message.data = notificationJson.str();
    sendResponse(message);
}

void STTConfigHandler::onConfigChange(const stt::ConfigChangeNotification& notification) {
    sendConfigChangedNotification(notification);
}

std::string STTConfigHandler::extractJsonValue(const std::string& json, const std::string& key) const {
    // Simplified JSON value extraction - in production, use a proper JSON library
    std::regex keyRegex("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch match;
    
    if (std::regex_search(json, match, keyRegex)) {
        return match[1].str();
    }
    
    // Try without quotes for non-string values
    std::regex valueRegex("\"" + key + "\"\\s*:\\s*([^,}\\]]+)");
    if (std::regex_search(json, match, valueRegex)) {
        std::string value = match[1].str();
        // Trim whitespace
        value.erase(0, value.find_first_not_of(" \t\n\r"));
        value.erase(value.find_last_not_of(" \t\n\r") + 1);
        return value;
    }
    
    return "";
}

STTConfigMessageType STTConfigHandler::stringToMessageType(const std::string& typeStr) const {
    if (typeStr == "GET_CONFIG") return STTConfigMessageType::GET_CONFIG;
    if (typeStr == "UPDATE_CONFIG") return STTConfigMessageType::UPDATE_CONFIG;
    if (typeStr == "UPDATE_CONFIG_VALUE") return STTConfigMessageType::UPDATE_CONFIG_VALUE;
    if (typeStr == "CONFIG_CHANGED") return STTConfigMessageType::CONFIG_CHANGED;
    if (typeStr == "GET_SCHEMA") return STTConfigMessageType::GET_SCHEMA;
    if (typeStr == "GET_METADATA") return STTConfigMessageType::GET_METADATA;
    if (typeStr == "VALIDATE_CONFIG") return STTConfigMessageType::VALIDATE_CONFIG;
    if (typeStr == "RESET_CONFIG") return STTConfigMessageType::RESET_CONFIG;
    if (typeStr == "GET_AVAILABLE_MODELS") return STTConfigMessageType::GET_AVAILABLE_MODELS;
    if (typeStr == "GET_SUPPORTED_QUANTIZATION_LEVELS") return STTConfigMessageType::GET_SUPPORTED_QUANTIZATION_LEVELS;
    
    return STTConfigMessageType::GET_CONFIG; // Default fallback
}

std::string STTConfigHandler::messageTypeToString(STTConfigMessageType type) const {
    switch (type) {
        case STTConfigMessageType::GET_CONFIG: return "GET_CONFIG";
        case STTConfigMessageType::UPDATE_CONFIG: return "UPDATE_CONFIG";
        case STTConfigMessageType::UPDATE_CONFIG_VALUE: return "UPDATE_CONFIG_VALUE";
        case STTConfigMessageType::CONFIG_CHANGED: return "CONFIG_CHANGED";
        case STTConfigMessageType::GET_SCHEMA: return "GET_SCHEMA";
        case STTConfigMessageType::GET_METADATA: return "GET_METADATA";
        case STTConfigMessageType::VALIDATE_CONFIG: return "VALIDATE_CONFIG";
        case STTConfigMessageType::RESET_CONFIG: return "RESET_CONFIG";
        case STTConfigMessageType::GET_AVAILABLE_MODELS: return "GET_AVAILABLE_MODELS";
        case STTConfigMessageType::GET_SUPPORTED_QUANTIZATION_LEVELS: return "GET_SUPPORTED_QUANTIZATION_LEVELS";
        default: return "UNKNOWN";
    }
}

} // namespace core