#pragma once

#include "external_service_integrator_interface.hpp"
#include "advanced_stt_config.hpp"
#include "utils/logging.hpp"
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <chrono>

namespace stt {
namespace advanced {

/**
 * Service reliability tracker
 */
class ServiceReliabilityTracker {
public:
    void recordSuccess(const std::string& serviceName);
    void recordFailure(const std::string& serviceName);
    float getReliability(const std::string& serviceName) const;
    void updateLatency(const std::string& serviceName, float latencyMs);
    float getAverageLatency(const std::string& serviceName) const;
    
private:
    struct ServiceStats {
        size_t totalRequests = 0;
        size_t successfulRequests = 0;
        float totalLatency = 0.0f;
        size_t latencyCount = 0;
        std::chrono::steady_clock::time_point lastUpdate;
    };
    
    mutable std::mutex statsMutex_;
    std::unordered_map<std::string, ServiceStats> serviceStats_;
};

/**
 * Cost tracker for external services
 */
class ServiceCostTracker {
public:
    void recordUsage(const std::string& serviceName, float durationMinutes, float costPerMinute);
    float getTotalCost(const std::string& serviceName) const;
    float getDailyCost(const std::string& serviceName) const;
    std::string getCostReport() const;
    void resetDailyCosts();
    
private:
    struct CostInfo {
        float totalCost = 0.0f;
        float dailyCost = 0.0f;
        size_t totalRequests = 0;
        std::chrono::steady_clock::time_point lastReset;
    };
    
    mutable std::mutex costMutex_;
    std::unordered_map<std::string, CostInfo> serviceCosts_;
};

/**
 * Confidence-weighted result fusion implementation
 */
class ConfidenceWeightedFusion : public ResultFusionEngine {
public:
    bool initialize(const ResultFusionConfig& config) override;
    FusedTranscriptionResult fuseResults(const std::vector<TranscriptionResult>& results,
                                        const std::vector<std::string>& serviceNames) override;
    bool updateConfiguration(const ResultFusionConfig& config) override;
    void setServiceWeights(const std::map<std::string, float>& weights) override;
    std::string getFusionStats() const override;
    bool isInitialized() const override;
    
private:
    ResultFusionConfig config_;
    std::map<std::string, float> serviceWeights_;
    mutable std::mutex configMutex_;
    std::atomic<bool> initialized_{false};
    
    // Fusion statistics
    mutable std::mutex statsMutex_;
    size_t totalFusions_ = 0;
    float averageConfidenceImprovement_ = 0.0f;
    
    FusedTranscriptionResult performConfidenceWeightedFusion(
        const std::vector<TranscriptionResult>& results,
        const std::vector<std::string>& serviceNames);
    
    FusedTranscriptionResult performMajorityVoteFusion(
        const std::vector<TranscriptionResult>& results,
        const std::vector<std::string>& serviceNames);
    
    FusedTranscriptionResult performBestConfidenceFusion(
        const std::vector<TranscriptionResult>& results,
        const std::vector<std::string>& serviceNames);
};

/**
 * Service health monitor implementation
 */
class ServiceHealthMonitorImpl : public ServiceHealthMonitor {
public:
    bool initialize(int checkIntervalMs = 30000) override;
    bool addService(std::shared_ptr<ExternalSTTService> service) override;
    bool removeService(const std::string& serviceName) override;
    bool startMonitoring() override;
    void stopMonitoring() override;
    std::map<std::string, ServiceHealthStatus> getAllHealthStatus() const override;
    ServiceHealthStatus getServiceHealth(const std::string& serviceName) const override;
    std::vector<std::string> getHealthyServices() const override;
    void registerHealthChangeCallback(
        std::function<void(const std::string&, const ServiceHealthStatus&)> callback) override;
    bool isMonitoring() const override;
    
private:
    std::unordered_map<std::string, std::shared_ptr<ExternalSTTService>> services_;
    std::unordered_map<std::string, ServiceHealthStatus> healthStatus_;
    std::vector<std::function<void(const std::string&, const ServiceHealthStatus&)>> callbacks_;
    
    std::thread monitorThread_;
    std::atomic<bool> monitoring_{false};
    std::atomic<bool> stopRequested_{false};
    int checkIntervalMs_ = 30000;
    
    mutable std::mutex servicesMutex_;
    mutable std::mutex healthMutex_;
    mutable std::mutex callbacksMutex_;
    
    void monitoringLoop();
    void checkServiceHealth(const std::string& serviceName, 
                           std::shared_ptr<ExternalSTTService> service);
    void notifyHealthChange(const std::string& serviceName, 
                           const ServiceHealthStatus& status);
};

/**
 * External service integrator implementation
 */
class ExternalServiceIntegrator : public ExternalServiceIntegratorInterface {
public:
    ExternalServiceIntegrator();
    ~ExternalServiceIntegrator();
    
    // Core interface methods
    bool initialize(const ExternalServicesConfig& config) override;
    bool addExternalService(const ExternalServiceInfo& serviceInfo,
                           const ServiceAuthentication& auth) override;
    bool removeExternalService(const std::string& serviceName) override;
    
    // Transcription methods
    bool transcribeWithFallback(const std::vector<float>& audioData,
                               const std::string& language,
                               const std::vector<std::string>& preferredServices,
                               std::function<void(const FusedTranscriptionResult&)> callback) override;
    
    bool transcribeWithFusion(const std::vector<float>& audioData,
                             const std::string& language,
                             const std::vector<std::string>& services,
                             std::function<void(const FusedTranscriptionResult&)> callback) override;
    
    // Service management
    std::vector<std::string> getAvailableServices() const override;
    std::vector<std::string> getHealthyServices() const override;
    ServiceHealthStatus getServiceHealth(const std::string& serviceName) const override;
    
    // Configuration management
    bool updateServiceConfig(const std::string& serviceName,
                            const ExternalServiceInfo& serviceInfo) override;
    bool updateServiceAuth(const std::string& serviceName,
                          const ServiceAuthentication& auth) override;
    
    // Feature control
    void setResultFusionEnabled(bool enabled) override;
    void setFallbackThreshold(float threshold) override;
    void setPrivacyMode(bool enabled) override;
    
    // Analytics and monitoring
    std::string getServiceUsageStats() const override;
    std::string getCostTracking() const override;
    size_t cancelAllPendingRequests() override;
    
    // Configuration
    bool updateConfiguration(const ExternalServicesConfig& config) override;
    ExternalServicesConfig getCurrentConfiguration() const override;
    
    // Status
    bool isInitialized() const override;
    std::string getLastError() const override;
    void reset() override;
    
private:
    // Configuration
    ExternalServicesConfig config_;
    mutable std::mutex configMutex_;
    
    // Services
    std::unordered_map<std::string, std::shared_ptr<ExternalSTTService>> services_;
    std::unordered_map<std::string, ServiceAuthentication> serviceAuth_;
    mutable std::mutex servicesMutex_;
    
    // Components
    std::unique_ptr<ResultFusionEngine> fusionEngine_;
    std::unique_ptr<ServiceHealthMonitor> healthMonitor_;
    std::unique_ptr<ServiceReliabilityTracker> reliabilityTracker_;
    std::unique_ptr<ServiceCostTracker> costTracker_;
    
    // State
    std::atomic<bool> initialized_{false};
    std::atomic<bool> privacyMode_{false};
    std::atomic<float> fallbackThreshold_{0.5f};
    std::string lastError_;
    mutable std::mutex errorMutex_;
    
    // Request tracking
    std::atomic<size_t> nextRequestId_{1};
    std::unordered_map<size_t, std::function<void()>> pendingRequests_;
    mutable std::mutex requestsMutex_;
    
    // Helper methods
    void setLastError(const std::string& error);
    std::vector<std::string> selectServicesForRequest(
        const std::vector<std::string>& preferredServices,
        const std::string& language) const;
    
    bool isServiceCompatible(const std::string& serviceName, 
                            const std::string& language) const;
    
    void handleTranscriptionResult(size_t requestId,
                                  const std::string& serviceName,
                                  const TranscriptionResult& result,
                                  std::function<void(const FusedTranscriptionResult&)> callback);
    
    void handleTranscriptionError(size_t requestId,
                                 const std::string& serviceName,
                                 const std::string& error);
    
    // Service factory methods
    std::shared_ptr<ExternalSTTService> createService(const ExternalServiceInfo& serviceInfo);
    
    // Privacy and data locality checks
    bool isDataLocalityCompliant(const std::string& serviceName) const;
    bool shouldUseService(const std::string& serviceName) const;
};

} // namespace advanced
} // namespace stt