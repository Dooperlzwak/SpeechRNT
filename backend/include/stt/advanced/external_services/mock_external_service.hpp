#pragma once

#include "../external_service_integrator_interface.hpp"
#include <random>
#include <thread>

namespace stt {
namespace advanced {
namespace external_services {

/**
 * Mock external STT service for testing and demonstration
 */
class MockExternalService : public ExternalSTTService {
public:
    MockExternalService();
    ~MockExternalService() override = default;
    
    // ExternalSTTService interface
    bool initialize(const ExternalServiceInfo& serviceInfo,
                   const ServiceAuthentication& auth) override;
    
    bool transcribeAsync(const std::vector<float>& audioData,
                        const std::string& language,
                        std::function<void(const TranscriptionResult&)> callback) override;
    
    TranscriptionResult transcribeSync(const std::vector<float>& audioData,
                                      const std::string& language = "") override;
    
    ServiceHealthStatus checkHealth() override;
    RateLimitInfo getRateLimitInfo() const override;
    ExternalServiceInfo getServiceInfo() const override;
    bool updateAuthentication(const ServiceAuthentication& auth) override;
    size_t cancelPendingRequests() override;
    std::vector<std::string> getSupportedLanguages() const override;
    bool isAvailable() const override;
    std::string getLastError() const override;
    
    // Mock-specific methods for testing
    void setSimulatedLatency(float latencyMs);
    void setSimulatedReliability(float reliability); // 0.0 to 1.0
    void setSimulatedAvailability(bool available);
    void setSimulatedHealthy(bool healthy);
    
private:
    ExternalServiceInfo serviceInfo_;
    ServiceAuthentication auth_;
    RateLimitInfo rateLimitInfo_;
    ServiceHealthStatus healthStatus_;
    
    // Simulation parameters
    float simulatedLatencyMs_ = 500.0f;
    float simulatedReliability_ = 0.95f;
    bool simulatedAvailable_ = true;
    bool simulatedHealthy_ = true;
    
    // State
    std::atomic<bool> initialized_{false};
    std::atomic<size_t> pendingRequests_{0};
    std::string lastError_;
    mutable std::mutex errorMutex_;
    
    // Random number generation
    mutable std::random_device rd_;
    mutable std::mt19937 gen_;
    mutable std::uniform_real_distribution<float> dist_;
    
    // Helper methods
    void setLastError(const std::string& error);
    TranscriptionResult generateMockTranscription(const std::vector<float>& audioData,
                                                 const std::string& language);
    bool shouldSimulateFailure() const;
    void simulateProcessingDelay() const;
};

/**
 * Factory for creating mock external services
 */
class MockServiceFactory {
public:
    static std::shared_ptr<ExternalSTTService> createMockService(
        const std::string& serviceName,
        const std::string& serviceType = "mock");
    
    static std::shared_ptr<ExternalSTTService> createReliableService(
        const std::string& serviceName);
    
    static std::shared_ptr<ExternalSTTService> createUnreliableService(
        const std::string& serviceName);
    
    static std::shared_ptr<ExternalSTTService> createFastService(
        const std::string& serviceName);
    
    static std::shared_ptr<ExternalSTTService> createSlowService(
        const std::string& serviceName);
};

} // namespace external_services
} // namespace advanced
} // namespace stt