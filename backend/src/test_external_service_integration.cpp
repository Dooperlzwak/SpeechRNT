#include "stt/advanced/external_service_integrator.hpp"
#include "stt/advanced/external_services/mock_external_service.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace stt::advanced;
using namespace stt::advanced::external_services;

void testBasicServiceIntegration() {
    std::cout << "\n=== Testing Basic Service Integration ===\n";
    
    // Create integrator
    auto integrator = std::make_unique<ExternalServiceIntegrator>();
    
    // Configure external services
    ExternalServicesConfig config;
    config.enabled = true;
    config.enableResultFusion = true;
    config.fallbackThreshold = 0.7f;
    config.enablePrivacyMode = false;
    config.serviceWeights["reliable_service"] = 1.0f;
    config.serviceWeights["fast_service"] = 0.8f;
    config.serviceWeights["unreliable_service"] = 0.6f;
    
    if (!integrator->initialize(config)) {
        std::cout << "Failed to initialize integrator: " << integrator->getLastError() << std::endl;
        return;
    }
    
    // Add mock services
    ExternalServiceInfo reliableServiceInfo;
    reliableServiceInfo.serviceName = "reliable_service";
    reliableServiceInfo.serviceType = "reliable_mock";
    reliableServiceInfo.endpoint = "mock://reliable";
    reliableServiceInfo.isAvailable = true;
    reliableServiceInfo.reliability = 0.99f;
    reliableServiceInfo.averageLatency = 300.0f;
    reliableServiceInfo.costPerMinute = 0.02f;
    reliableServiceInfo.supportedLanguages = {"en", "es", "fr"};
    
    ServiceAuthentication auth;
    auth.authType = "mock";
    auth.credentials = "test_credentials";
    
    if (!integrator->addExternalService(reliableServiceInfo, auth)) {
        std::cout << "Failed to add reliable service: " << integrator->getLastError() << std::endl;
        return;
    }
    
    ExternalServiceInfo fastServiceInfo;
    fastServiceInfo.serviceName = "fast_service";
    fastServiceInfo.serviceType = "fast_mock";
    fastServiceInfo.endpoint = "mock://fast";
    fastServiceInfo.isAvailable = true;
    fastServiceInfo.reliability = 0.95f;
    fastServiceInfo.averageLatency = 150.0f;
    fastServiceInfo.costPerMinute = 0.03f;
    fastServiceInfo.supportedLanguages = {"en", "es", "fr", "de"};
    
    if (!integrator->addExternalService(fastServiceInfo, auth)) {
        std::cout << "Failed to add fast service: " << integrator->getLastError() << std::endl;
        return;
    }
    
    ExternalServiceInfo unreliableServiceInfo;
    unreliableServiceInfo.serviceName = "unreliable_service";
    unreliableServiceInfo.serviceType = "unreliable_mock";
    unreliableServiceInfo.endpoint = "mock://unreliable";
    unreliableServiceInfo.isAvailable = true;
    unreliableServiceInfo.reliability = 0.70f;
    unreliableServiceInfo.averageLatency = 800.0f;
    unreliableServiceInfo.costPerMinute = 0.01f;
    unreliableServiceInfo.supportedLanguages = {"en", "es"};
    
    if (!integrator->addExternalService(unreliableServiceInfo, auth)) {
        std::cout << "Failed to add unreliable service: " << integrator->getLastError() << std::endl;
        return;
    }
    
    std::cout << "Added 3 external services successfully\n";
    
    // Test service availability
    auto availableServices = integrator->getAvailableServices();
    std::cout << "Available services: ";
    for (const auto& service : availableServices) {
        std::cout << service << " ";
    }
    std::cout << std::endl;
    
    // Wait a moment for health monitoring to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto healthyServices = integrator->getHealthyServices();
    std::cout << "Healthy services: ";
    for (const auto& service : healthyServices) {
        std::cout << service << " ";
    }
    std::cout << std::endl;
}

void testFallbackTranscription() {
    std::cout << "\n=== Testing Fallback Transcription ===\n";
    
    auto integrator = std::make_unique<ExternalServiceIntegrator>();
    
    ExternalServicesConfig config;
    config.enabled = true;
    config.enableResultFusion = false;
    config.fallbackThreshold = 0.8f;
    config.enablePrivacyMode = false;
    
    integrator->initialize(config);
    
    // Add services with different reliability
    ExternalServiceInfo reliableServiceInfo;
    reliableServiceInfo.serviceName = "reliable_service";
    reliableServiceInfo.serviceType = "reliable_mock";
    reliableServiceInfo.supportedLanguages = {"en"};
    
    ExternalServiceInfo unreliableServiceInfo;
    unreliableServiceInfo.serviceName = "unreliable_service";
    unreliableServiceInfo.serviceType = "unreliable_mock";
    unreliableServiceInfo.supportedLanguages = {"en"};
    
    ServiceAuthentication auth;
    auth.authType = "mock";
    
    integrator->addExternalService(reliableServiceInfo, auth);
    integrator->addExternalService(unreliableServiceInfo, auth);
    
    // Generate test audio data
    std::vector<float> audioData(16000 * 2); // 2 seconds of audio
    std::fill(audioData.begin(), audioData.end(), 0.1f);
    
    // Test fallback with unreliable service first
    std::vector<std::string> preferredServices = {"unreliable_service", "reliable_service"};
    
    bool callbackCalled = false;
    FusedTranscriptionResult result;
    
    integrator->transcribeWithFallback(audioData, "en", preferredServices,
        [&callbackCalled, &result](const FusedTranscriptionResult& fusedResult) {
            result = fusedResult;
            callbackCalled = true;
            std::cout << "Fallback result: \"" << fusedResult.fusedResult.text 
                     << "\" (confidence: " << fusedResult.fusedResult.confidence 
                     << ", method: " << fusedResult.fusionMethod << ")\n";
        });
    
    // Wait for callback
    int waitCount = 0;
    while (!callbackCalled && waitCount < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitCount++;
    }
    
    if (callbackCalled) {
        std::cout << "Fallback transcription completed successfully\n";
    } else {
        std::cout << "Fallback transcription timed out\n";
    }
}

void testResultFusion() {
    std::cout << "\n=== Testing Result Fusion ===\n";
    
    auto integrator = std::make_unique<ExternalServiceIntegrator>();
    
    ExternalServicesConfig config;
    config.enabled = true;
    config.enableResultFusion = true;
    config.fallbackThreshold = 0.5f;
    config.enablePrivacyMode = false;
    config.minServicesForFusion = 2;
    config.serviceWeights["reliable_service"] = 1.0f;
    config.serviceWeights["fast_service"] = 0.9f;
    config.serviceWeights["unreliable_service"] = 0.7f;
    
    integrator->initialize(config);
    
    // Add multiple services
    std::vector<std::string> serviceNames = {"reliable_service", "fast_service", "unreliable_service"};
    std::vector<std::string> serviceTypes = {"reliable_mock", "fast_mock", "unreliable_mock"};
    
    ServiceAuthentication auth;
    auth.authType = "mock";
    
    for (size_t i = 0; i < serviceNames.size(); ++i) {
        ExternalServiceInfo serviceInfo;
        serviceInfo.serviceName = serviceNames[i];
        serviceInfo.serviceType = serviceTypes[i];
        serviceInfo.supportedLanguages = {"en"};
        
        integrator->addExternalService(serviceInfo, auth);
    }
    
    // Generate test audio data
    std::vector<float> audioData(16000 * 3); // 3 seconds of audio
    std::fill(audioData.begin(), audioData.end(), 0.1f);
    
    bool callbackCalled = false;
    FusedTranscriptionResult result;
    
    integrator->transcribeWithFusion(audioData, "en", serviceNames,
        [&callbackCalled, &result](const FusedTranscriptionResult& fusedResult) {
            result = fusedResult;
            callbackCalled = true;
            
            std::cout << "Fusion result: \"" << fusedResult.fusedResult.text 
                     << "\" (confidence: " << fusedResult.fusedResult.confidence 
                     << ", method: " << fusedResult.fusionMethod 
                     << ", services used: " << fusedResult.servicesUsed << ")\n";
            
            std::cout << "Individual results:\n";
            for (size_t i = 0; i < fusedResult.individualResults.size(); ++i) {
                const auto& individualResult = fusedResult.individualResults[i];
                std::cout << "  - \"" << individualResult.text 
                         << "\" (confidence: " << individualResult.confidence << ")\n";
            }
            
            std::cout << "Service contributions:\n";
            for (const auto& [serviceName, contribution] : fusedResult.serviceContributions) {
                std::cout << "  - " << serviceName << ": " << contribution << "\n";
            }
        });
    
    // Wait for callback
    int waitCount = 0;
    while (!callbackCalled && waitCount < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitCount++;
    }
    
    if (callbackCalled) {
        std::cout << "Result fusion completed successfully\n";
    } else {
        std::cout << "Result fusion timed out\n";
    }
}

void testServiceHealthMonitoring() {
    std::cout << "\n=== Testing Service Health Monitoring ===\n";
    
    auto integrator = std::make_unique<ExternalServiceIntegrator>();
    
    ExternalServicesConfig config;
    config.enabled = true;
    integrator->initialize(config);
    
    // Add a service
    ExternalServiceInfo serviceInfo;
    serviceInfo.serviceName = "test_service";
    serviceInfo.serviceType = "mock";
    serviceInfo.supportedLanguages = {"en"};
    
    ServiceAuthentication auth;
    auth.authType = "mock";
    
    integrator->addExternalService(serviceInfo, auth);
    
    // Wait for health monitoring to run
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Check health status
    auto healthStatus = integrator->getServiceHealth("test_service");
    std::cout << "Service health status:\n";
    std::cout << "  - Service: " << healthStatus.serviceName << "\n";
    std::cout << "  - Healthy: " << (healthStatus.isHealthy ? "Yes" : "No") << "\n";
    std::cout << "  - Response time: " << healthStatus.responseTime << "ms\n";
    std::cout << "  - Success rate: " << healthStatus.successRate << "\n";
    std::cout << "  - Consecutive failures: " << healthStatus.consecutiveFailures << "\n";
    
    if (!healthStatus.lastError.empty()) {
        std::cout << "  - Last error: " << healthStatus.lastError << "\n";
    }
}

void testServiceUsageStats() {
    std::cout << "\n=== Testing Service Usage Statistics ===\n";
    
    auto integrator = std::make_unique<ExternalServiceIntegrator>();
    
    ExternalServicesConfig config;
    config.enabled = true;
    integrator->initialize(config);
    
    // Add services
    std::vector<std::string> serviceNames = {"service1", "service2"};
    
    ServiceAuthentication auth;
    auth.authType = "mock";
    
    for (const auto& serviceName : serviceNames) {
        ExternalServiceInfo serviceInfo;
        serviceInfo.serviceName = serviceName;
        serviceInfo.serviceType = "mock";
        serviceInfo.supportedLanguages = {"en"};
        
        integrator->addExternalService(serviceInfo, auth);
    }
    
    // Perform some transcriptions to generate stats
    std::vector<float> audioData(16000); // 1 second of audio
    std::fill(audioData.begin(), audioData.end(), 0.1f);
    
    for (int i = 0; i < 3; ++i) {
        integrator->transcribeWithFallback(audioData, "en", serviceNames,
            [](const FusedTranscriptionResult& result) {
                // Callback for transcription result
            });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Wait for transcriptions to complete
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Get usage statistics
    std::string usageStats = integrator->getServiceUsageStats();
    std::cout << "Service usage statistics:\n" << usageStats << std::endl;
    
    // Get cost tracking
    std::string costTracking = integrator->getCostTracking();
    std::cout << "Cost tracking:\n" << costTracking << std::endl;
}

int main() {
    std::cout << "External Service Integration Test\n";
    std::cout << "================================\n";
    
    try {
        testBasicServiceIntegration();
        testFallbackTranscription();
        testResultFusion();
        testServiceHealthMonitoring();
        testServiceUsageStats();
        
        std::cout << "\n=== All Tests Completed ===\n";
        
    } catch (const std::exception& e) {
        std::cout << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}