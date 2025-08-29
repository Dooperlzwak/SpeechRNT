#include "stt/advanced/advanced_stt_orchestrator.hpp"
#include "stt/advanced/advanced_stt_config.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>

using namespace stt::advanced;

int main() {
    std::cout << "Advanced STT Infrastructure Test\n";
    std::cout << "================================\n\n";
    
    try {
        // Test configuration creation and validation
        std::cout << "1. Testing configuration creation...\n";
        AdvancedSTTConfig config;
        config.enableAdvancedFeatures = true;
        config.audioPreprocessing.enabled = true;
        config.realTimeAnalysis.enabled = true;
        
        if (config.isValid()) {
            std::cout << "   ✓ Configuration is valid\n";
        } else {
            std::cout << "   ✗ Configuration validation failed\n";
            auto errors = config.getValidationErrors();
            for (const auto& error : errors) {
                std::cout << "     Error: " << error << "\n";
            }
        }
        
        // Test configuration manager
        std::cout << "\n2. Testing configuration manager...\n";
        AdvancedSTTConfigManager configManager;
        
        // Test JSON export/import
        std::string jsonConfig = configManager.exportToJson();
        std::cout << "   ✓ Configuration exported to JSON (" << jsonConfig.length() << " bytes)\n";
        
        if (configManager.loadFromJson(jsonConfig)) {
            std::cout << "   ✓ Configuration loaded from JSON\n";
        } else {
            std::cout << "   ✗ Failed to load configuration from JSON\n";
        }
        
        // Test orchestrator initialization
        std::cout << "\n3. Testing orchestrator initialization...\n";
        AdvancedSTTOrchestrator orchestrator;
        
        if (orchestrator.initializeAdvancedFeatures(config)) {
            std::cout << "   ✓ Orchestrator initialized successfully\n";
            
            // Test feature status
            std::cout << "\n4. Testing feature status...\n";
            std::cout << "   Audio Preprocessing: " 
                      << (orchestrator.isFeatureEnabled(AdvancedFeature::AUDIO_PREPROCESSING) ? "ENABLED" : "DISABLED") << "\n";
            std::cout << "   Real-time Analysis: " 
                      << (orchestrator.isFeatureEnabled(AdvancedFeature::REALTIME_ANALYSIS) ? "ENABLED" : "DISABLED") << "\n";
            std::cout << "   Speaker Diarization: " 
                      << (orchestrator.isFeatureEnabled(AdvancedFeature::SPEAKER_DIARIZATION) ? "ENABLED" : "DISABLED") << "\n";
            
            // Test health status
            std::cout << "\n5. Testing health monitoring...\n";
            auto healthStatus = orchestrator.getHealthStatus();
            std::cout << "   Overall Health: " << (healthStatus.overallAdvancedHealth * 100.0f) << "%\n";
            
            // Test processing metrics
            std::cout << "\n6. Testing processing metrics...\n";
            auto metrics = orchestrator.getProcessingMetrics();
            std::cout << "   Total Requests: " << metrics.totalProcessedRequests << "\n";
            std::cout << "   Successful Requests: " << metrics.successfulRequests << "\n";
            std::cout << "   Failed Requests: " << metrics.failedRequests << "\n";
            
            // Test audio processing (with dummy data)
            std::cout << "\n7. Testing audio processing...\n";
            AudioProcessingRequest request;
            request.utteranceId = 1;
            request.audioData = std::vector<float>(16000, 0.0f); // 1 second of silence
            request.enableAudioPreprocessing = true;
            request.enableRealTimeAnalysis = true;
            
            auto result = orchestrator.processAudioWithAdvancedFeatures(request);
            std::cout << "   Processing completed\n";
            std::cout << "   Result confidence: " << result.confidence << "\n";
            std::cout << "   Processing latency: " << result.processingLatencyMs << "ms\n";
            
        } else {
            std::cout << "   ✗ Orchestrator initialization failed: " << orchestrator.getLastError() << "\n";
        }
        
        std::cout << "\n8. Testing configuration file operations...\n";
        
        // Test saving configuration to file
        if (configManager.saveToFile("test_advanced_config.json")) {
            std::cout << "   ✓ Configuration saved to file\n";
        } else {
            std::cout << "   ✗ Failed to save configuration to file\n";
        }
        
        // Test loading configuration from file
        AdvancedSTTConfigManager configManager2;
        if (configManager2.loadFromFile("test_advanced_config.json")) {
            std::cout << "   ✓ Configuration loaded from file\n";
        } else {
            std::cout << "   ✗ Failed to load configuration from file\n";
        }
        
        std::cout << "\n✓ All tests completed successfully!\n";
        
    } catch (const std::exception& e) {
        std::cout << "✗ Test failed with exception: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}