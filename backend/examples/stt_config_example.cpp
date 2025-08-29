#include "stt/stt_config.hpp"
#include "core/stt_config_handler.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace stt;
using namespace core;

void demonstrateBasicConfiguration() {
    std::cout << "\n=== Basic STT Configuration Demo ===" << std::endl;
    
    // Create configuration manager
    STTConfigManager configManager;
    
    // Load configuration from file (will use defaults if file doesn't exist)
    std::string configPath = "config/stt.json";
    if (configManager.loadFromFile(configPath)) {
        std::cout << "Configuration loaded from: " << configPath << std::endl;
    } else {
        std::cout << "Failed to load configuration, using defaults" << std::endl;
    }
    
    // Get current configuration
    STTConfig config = configManager.getConfig();
    std::cout << "Current model: " << config.defaultModel << std::endl;
    std::cout << "Language detection enabled: " << (config.languageDetectionEnabled ? "yes" : "no") << std::endl;
    std::cout << "Confidence threshold: " << config.confidenceThreshold << std::endl;
    
    // Update a configuration value
    std::cout << "\nUpdating default model to 'large'..." << std::endl;
    auto result = configManager.updateConfigValue("model", "defaultModel", "large");
    
    if (result.isValid) {
        std::cout << "Configuration updated successfully" << std::endl;
        config = configManager.getConfig();
        std::cout << "New model: " << config.defaultModel << std::endl;
    } else {
        std::cout << "Configuration update failed:" << std::endl;
        for (const auto& error : result.errors) {
            std::cout << "  - " << error << std::endl;
        }
    }
    
    // Show warnings if any
    if (result.hasWarnings()) {
        std::cout << "Warnings:" << std::endl;
        for (const auto& warning : result.warnings) {
            std::cout << "  - " << warning << std::endl;
        }
    }
    
    // Export configuration to JSON
    std::cout << "\nExporting configuration to JSON..." << std::endl;
    std::string jsonConfig = configManager.exportToJson();
    std::cout << "JSON configuration (first 200 chars):" << std::endl;
    std::cout << jsonConfig.substr(0, 200) << "..." << std::endl;
}

void demonstrateConfigurationValidation() {
    std::cout << "\n=== Configuration Validation Demo ===" << std::endl;
    
    STTConfigManager configManager;
    
    // Test valid configuration
    STTConfig validConfig;
    validConfig.defaultModel = "base";
    validConfig.languageDetectionThreshold = 0.7f;
    validConfig.minChunkSizeMs = 1000;
    validConfig.maxChunkSizeMs = 5000;
    
    auto result = configManager.validateConfig(validConfig);
    std::cout << "Valid configuration test: " << (result.isValid ? "PASSED" : "FAILED") << std::endl;
    
    // Test invalid configuration
    STTConfig invalidConfig;
    invalidConfig.defaultModel = "invalid_model";
    invalidConfig.languageDetectionThreshold = 1.5f; // Invalid range
    invalidConfig.minChunkSizeMs = 50; // Too small
    invalidConfig.maxChunkSizeMs = 500; // Smaller than min
    
    result = configManager.validateConfig(invalidConfig);
    std::cout << "Invalid configuration test: " << (result.isValid ? "FAILED" : "PASSED") << std::endl;
    
    if (!result.isValid) {
        std::cout << "Validation errors:" << std::endl;
        for (const auto& error : result.errors) {
            std::cout << "  - " << error << std::endl;
        }
    }
}

void demonstrateConfigurationChangeNotifications() {
    std::cout << "\n=== Configuration Change Notifications Demo ===" << std::endl;
    
    STTConfigManager configManager;
    
    // Register change callback
    configManager.registerChangeCallback(
        [](const ConfigChangeNotification& notification) {
            std::cout << "Configuration changed:" << std::endl;
            std::cout << "  Section: " << notification.section << std::endl;
            std::cout << "  Key: " << notification.key << std::endl;
            std::cout << "  Old value: " << notification.oldValue << std::endl;
            std::cout << "  New value: " << notification.newValue << std::endl;
            
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - notification.timestamp).count();
            std::cout << "  Time since change: " << elapsed << "ms ago" << std::endl;
        }
    );
    
    // Make some configuration changes
    std::cout << "Making configuration changes..." << std::endl;
    
    configManager.updateConfigValue("model", "defaultModel", "small");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    configManager.updateConfigValue("languageDetection", "enabled", "false");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    configManager.updateConfigValue("streaming", "minChunkSizeMs", "1500");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void demonstrateWebSocketHandler() {
    std::cout << "\n=== WebSocket Configuration Handler Demo ===" << std::endl;
    
    // Create configuration handler
    STTConfigHandler configHandler;
    
    // Set up message sender (simulates WebSocket sending)
    std::vector<std::string> sentMessages;
    auto messageSender = [&sentMessages](const std::string& message) {
        sentMessages.push_back(message);
        std::cout << "Sent message: " << message.substr(0, 100) << "..." << std::endl;
    };
    
    // Initialize handler
    std::string configPath = "config/stt.json";
    if (configHandler.initialize(configPath, messageSender)) {
        std::cout << "Configuration handler initialized" << std::endl;
    } else {
        std::cout << "Failed to initialize configuration handler" << std::endl;
        return;
    }
    
    // Simulate incoming WebSocket messages
    std::cout << "\nSimulating WebSocket messages..." << std::endl;
    
    // Get configuration message
    std::string getConfigMsg = R"({"type": "GET_CONFIG", "requestId": "demo-1", "data": ""})";
    std::cout << "Handling GET_CONFIG message..." << std::endl;
    configHandler.handleMessage(getConfigMsg);
    
    // Update configuration value message
    std::string updateMsg = R"({"type": "UPDATE_CONFIG_VALUE", "requestId": "demo-2", "data": {"section": "model", "key": "defaultModel", "value": "medium"}})";
    std::cout << "Handling UPDATE_CONFIG_VALUE message..." << std::endl;
    configHandler.handleMessage(updateMsg);
    
    // Get schema message
    std::string schemaMsg = R"({"type": "GET_SCHEMA", "requestId": "demo-3", "data": ""})";
    std::cout << "Handling GET_SCHEMA message..." << std::endl;
    configHandler.handleMessage(schemaMsg);
    
    // Get available models message
    std::string modelsMsg = R"({"type": "GET_AVAILABLE_MODELS", "requestId": "demo-4", "data": ""})";
    std::cout << "Handling GET_AVAILABLE_MODELS message..." << std::endl;
    configHandler.handleMessage(modelsMsg);
    
    std::cout << "Total messages sent: " << sentMessages.size() << std::endl;
    
    // Show handler statistics
    std::cout << "\nHandler statistics:" << std::endl;
    std::string stats = configHandler.getStatistics();
    std::cout << stats << std::endl;
}

void demonstrateAdvancedFeatures() {
    std::cout << "\n=== Advanced Configuration Features Demo ===" << std::endl;
    
    STTConfigManager configManager;
    
    // Get configuration schema
    std::cout << "Getting configuration schema..." << std::endl;
    std::string schema = configManager.getConfigSchema();
    std::cout << "Schema size: " << schema.length() << " characters" << std::endl;
    
    // Get configuration metadata
    std::cout << "Getting configuration metadata..." << std::endl;
    std::string metadata = configManager.getConfigMetadata();
    std::cout << "Metadata size: " << metadata.length() << " characters" << std::endl;
    
    // Get available models
    std::cout << "Getting available models..." << std::endl;
    auto models = configManager.getAvailableModels();
    std::cout << "Available models: ";
    for (size_t i = 0; i < models.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << models[i];
    }
    if (models.empty()) {
        std::cout << "(none found - models directory may not exist)";
    }
    std::cout << std::endl;
    
    // Get supported quantization levels
    std::cout << "Getting supported quantization levels..." << std::endl;
    auto levels = configManager.getSupportedQuantizationLevels();
    std::cout << "Supported quantization levels: ";
    for (size_t i = 0; i < levels.size(); ++i) {
        if (i > 0) std::cout << ", ";
        // Note: This would need access to QuantizationManager to convert to string
        std::cout << static_cast<int>(levels[i]);
    }
    std::cout << std::endl;
    
    // Test auto-save functionality
    std::cout << "Testing auto-save functionality..." << std::endl;
    configManager.setAutoSave(true);
    configManager.loadFromFile("config/stt.json"); // Set file path
    
    std::cout << "Configuration modified: " << (configManager.isModified() ? "yes" : "no") << std::endl;
    
    configManager.updateConfigValue("model", "defaultModel", "large");
    std::cout << "After update, configuration modified: " << (configManager.isModified() ? "yes" : "no") << std::endl;
    
    // Reset to defaults
    std::cout << "Resetting configuration to defaults..." << std::endl;
    configManager.resetToDefaults();
    
    STTConfig config = configManager.getConfig();
    std::cout << "After reset, default model: " << config.defaultModel << std::endl;
}

int main() {
    std::cout << "STT Configuration System Demo" << std::endl;
    std::cout << "=============================" << std::endl;
    
    try {
        demonstrateBasicConfiguration();
        demonstrateConfigurationValidation();
        demonstrateConfigurationChangeNotifications();
        demonstrateWebSocketHandler();
        demonstrateAdvancedFeatures();
        
        std::cout << "\n=== Demo completed successfully ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}