#pragma once

#include "stt/stt_interface.hpp"
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <functional>
#include <chrono>

namespace stt {
namespace advanced {

/**
 * External service information
 */
struct ExternalServiceInfo {
    std::string serviceName;
    std::string serviceType; // "cloud", "api", "local"
    std::string endpoint;
    std::string apiKey;
    std::map<std::string, std::string> configuration;
    bool isAvailable;
    float reliability; // 0.0 to 1.0
    float averageLatency;
    float costPerMinute;
    std::vector<std::string> supportedLanguages;
    
    ExternalServiceInfo() 
        : isAvailable(false)
        , reliability(0.0f)
        , averageLatency(0.0f)
        , costPerMinute(0.0f) {}
};

/**
 * Service authentication information
 */
struct ServiceAuthentication {
    std::string authType; // "api_key", "oauth", "bearer_token", "basic"
    std::string credentials;
    std::string tokenEndpoint;
    std::chrono::steady_clock::time_point tokenExpiry;
    std::map<std::string, std::string> additionalHeaders;
    
    ServiceAuthentication() 
        : tokenExpiry(std::chrono::steady_clock::now()) {}
};

/**
 * Rate limiting information
 */
struct RateLimitInfo {
    size_t requestsPerMinute;
    size_t requestsPerHour;
    size_t requestsPerDay;
    size_t currentMinuteRequests;
    size_t currentHourRequests;
    size_t currentDayRequests;
    std::chrono::steady_clock::time_point lastResetTime;
    bool isLimited;
    
    RateLimitInfo() 
        : requestsPerMinute(0)
        , requestsPerHour(0)
        , requestsPerDay(0)
        , currentMinuteRequests(0)
        , currentHourRequests(0)
        , currentDayRequests(0)
        , lastResetTime(std::chrono::steady_clock::now())
        , isLimited(false) {}
};

/**
 * Service health status
 */
struct ServiceHealthStatus {
    std::string serviceName;
    bool isHealthy;
    float responseTime;
    std::string lastError;
    std::chrono::steady_clock::time_point lastHealthCheck;
    size_t consecutiveFailures;
    float successRate; // 0.0 to 1.0
    
    ServiceHealthStatus() 
        : isHealthy(false)
        , responseTime(0.0f)
        , lastHealthCheck(std::chrono::steady_clock::now())
        , consecutiveFailures(0)
        , successRate(0.0f) {}
};

/**
 * Result fusion configuration
 */
struct ResultFusionConfig {
    bool enableFusion;
    std::string fusionStrategy; // "confidence_weighted", "majority_vote", "best_confidence"
    float confidenceThreshold;
    size_t minServicesForFusion;
    std::map<std::string, float> serviceWeights;
    bool enableConsensusFiltering;
    
    ResultFusionConfig() 
        : enableFusion(true)
        , fusionStrategy("confidence_weighted")
        , confidenceThreshold(0.5f)
        , minServicesForFusion(2)
        , enableConsensusFiltering(true) {}
};

/**
 * Fused transcription result
 */
struct FusedTranscriptionResult {
    TranscriptionResult fusedResult;
    std::vector<TranscriptionResult> individualResults;
    std::map<std::string, float> serviceContributions;
    std::string fusionMethod;
    float fusionConfidence;
    size_t servicesUsed;
    
    FusedTranscriptionResult() 
        : fusionConfidence(0.0f)
        , servicesUsed(0) {}
};

/**
 * External STT service interface
 */
class ExternalSTTService {
public:
    virtual ~ExternalSTTService() = default;
    
    /**
     * Initialize the external service
     * @param serviceInfo Service configuration
     * @param auth Authentication information
     * @return true if initialization successful
     */
    virtual bool initialize(const ExternalServiceInfo& serviceInfo,
                           const ServiceAuthentication& auth) = 0;
    
    /**
     * Transcribe audio using external service
     * @param audioData Audio samples
     * @param language Language code (optional)
     * @param callback Callback for result
     * @return true if request submitted successfully
     */
    virtual bool transcribeAsync(const std::vector<float>& audioData,
                                const std::string& language,
                                std::function<void(const TranscriptionResult&)> callback) = 0;
    
    /**
     * Transcribe audio synchronously
     * @param audioData Audio samples
     * @param language Language code (optional)
     * @return Transcription result
     */
    virtual TranscriptionResult transcribeSync(const std::vector<float>& audioData,
                                              const std::string& language = "") = 0;
    
    /**
     * Check service health
     * @return Service health status
     */
    virtual ServiceHealthStatus checkHealth() = 0;
    
    /**
     * Get rate limit information
     * @return Current rate limit status
     */
    virtual RateLimitInfo getRateLimitInfo() const = 0;
    
    /**
     * Get service information
     * @return Service information
     */
    virtual ExternalServiceInfo getServiceInfo() const = 0;
    
    /**
     * Update authentication
     * @param auth New authentication information
     * @return true if update successful
     */
    virtual bool updateAuthentication(const ServiceAuthentication& auth) = 0;
    
    /**
     * Cancel pending requests
     * @return Number of cancelled requests
     */
    virtual size_t cancelPendingRequests() = 0;
    
    /**
     * Get supported languages
     * @return Vector of supported language codes
     */
    virtual std::vector<std::string> getSupportedLanguages() const = 0;
    
    /**
     * Check if service is available
     * @return true if service is available
     */
    virtual bool isAvailable() const = 0;
    
    /**
     * Get last error message
     * @return Last error message
     */
    virtual std::string getLastError() const = 0;
};

/**
 * Result fusion engine interface
 */
class ResultFusionEngine {
public:
    virtual ~ResultFusionEngine() = default;
    
    /**
     * Initialize fusion engine
     * @param config Fusion configuration
     * @return true if initialization successful
     */
    virtual bool initialize(const ResultFusionConfig& config) = 0;
    
    /**
     * Fuse multiple transcription results
     * @param results Vector of transcription results from different services
     * @param serviceNames Names of services that provided results
     * @return Fused transcription result
     */
    virtual FusedTranscriptionResult fuseResults(const std::vector<TranscriptionResult>& results,
                                                 const std::vector<std::string>& serviceNames) = 0;
    
    /**
     * Update fusion configuration
     * @param config New fusion configuration
     * @return true if update successful
     */
    virtual bool updateConfiguration(const ResultFusionConfig& config) = 0;
    
    /**
     * Set service weights for fusion
     * @param weights Map of service name to weight
     */
    virtual void setServiceWeights(const std::map<std::string, float>& weights) = 0;
    
    /**
     * Get fusion statistics
     * @return Statistics as JSON string
     */
    virtual std::string getFusionStats() const = 0;
    
    /**
     * Check if engine is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Service health monitor interface
 */
class ServiceHealthMonitor {
public:
    virtual ~ServiceHealthMonitor() = default;
    
    /**
     * Initialize health monitor
     * @param checkIntervalMs Health check interval in milliseconds
     * @return true if initialization successful
     */
    virtual bool initialize(int checkIntervalMs = 30000) = 0;
    
    /**
     * Add service to monitor
     * @param service External service to monitor
     * @return true if added successfully
     */
    virtual bool addService(std::shared_ptr<ExternalSTTService> service) = 0;
    
    /**
     * Remove service from monitoring
     * @param serviceName Name of service to remove
     * @return true if removed successfully
     */
    virtual bool removeService(const std::string& serviceName) = 0;
    
    /**
     * Start health monitoring
     * @return true if started successfully
     */
    virtual bool startMonitoring() = 0;
    
    /**
     * Stop health monitoring
     */
    virtual void stopMonitoring() = 0;
    
    /**
     * Get health status for all services
     * @return Map of service name to health status
     */
    virtual std::map<std::string, ServiceHealthStatus> getAllHealthStatus() const = 0;
    
    /**
     * Get health status for specific service
     * @param serviceName Service name
     * @return Health status
     */
    virtual ServiceHealthStatus getServiceHealth(const std::string& serviceName) const = 0;
    
    /**
     * Get healthy services
     * @return Vector of healthy service names
     */
    virtual std::vector<std::string> getHealthyServices() const = 0;
    
    /**
     * Register health change callback
     * @param callback Callback function
     */
    virtual void registerHealthChangeCallback(
        std::function<void(const std::string&, const ServiceHealthStatus&)> callback) = 0;
    
    /**
     * Check if monitor is running
     * @return true if monitoring is active
     */
    virtual bool isMonitoring() const = 0;
};

/**
 * External service integrator interface
 */
class ExternalServiceIntegratorInterface {
public:
    virtual ~ExternalServiceIntegratorInterface() = default;
    
    /**
     * Initialize the service integrator
     * @param config External services configuration
     * @return true if initialization successful
     */
    virtual bool initialize(const ExternalServicesConfig& config) = 0;
    
    /**
     * Add external service
     * @param serviceInfo Service information
     * @param auth Authentication information
     * @return true if added successfully
     */
    virtual bool addExternalService(const ExternalServiceInfo& serviceInfo,
                                   const ServiceAuthentication& auth) = 0;
    
    /**
     * Remove external service
     * @param serviceName Service name to remove
     * @return true if removed successfully
     */
    virtual bool removeExternalService(const std::string& serviceName) = 0;
    
    /**
     * Transcribe using external services with fallback
     * @param audioData Audio samples
     * @param language Language code
     * @param preferredServices Preferred services in order
     * @param callback Callback for result
     * @return true if request submitted successfully
     */
    virtual bool transcribeWithFallback(const std::vector<float>& audioData,
                                       const std::string& language,
                                       const std::vector<std::string>& preferredServices,
                                       std::function<void(const FusedTranscriptionResult&)> callback) = 0;
    
    /**
     * Transcribe using multiple services and fuse results
     * @param audioData Audio samples
     * @param language Language code
     * @param services Services to use for fusion
     * @param callback Callback for fused result
     * @return true if request submitted successfully
     */
    virtual bool transcribeWithFusion(const std::vector<float>& audioData,
                                     const std::string& language,
                                     const std::vector<std::string>& services,
                                     std::function<void(const FusedTranscriptionResult&)> callback) = 0;
    
    /**
     * Get available services
     * @return Vector of available service names
     */
    virtual std::vector<std::string> getAvailableServices() const = 0;
    
    /**
     * Get healthy services
     * @return Vector of healthy service names
     */
    virtual std::vector<std::string> getHealthyServices() const = 0;
    
    /**
     * Get service health status
     * @param serviceName Service name
     * @return Health status
     */
    virtual ServiceHealthStatus getServiceHealth(const std::string& serviceName) const = 0;
    
    /**
     * Update service configuration
     * @param serviceName Service name
     * @param serviceInfo New service information
     * @return true if update successful
     */
    virtual bool updateServiceConfig(const std::string& serviceName,
                                    const ExternalServiceInfo& serviceInfo) = 0;
    
    /**
     * Update service authentication
     * @param serviceName Service name
     * @param auth New authentication information
     * @return true if update successful
     */
    virtual bool updateServiceAuth(const std::string& serviceName,
                                  const ServiceAuthentication& auth) = 0;
    
    /**
     * Enable or disable result fusion
     * @param enabled true to enable result fusion
     */
    virtual void setResultFusionEnabled(bool enabled) = 0;
    
    /**
     * Set fallback threshold
     * @param threshold Confidence threshold for fallback (0.0 to 1.0)
     */
    virtual void setFallbackThreshold(float threshold) = 0;
    
    /**
     * Enable or disable privacy mode
     * @param enabled true to enable privacy mode (local processing only)
     */
    virtual void setPrivacyMode(bool enabled) = 0;
    
    /**
     * Get service usage statistics
     * @return Statistics as JSON string
     */
    virtual std::string getServiceUsageStats() const = 0;
    
    /**
     * Get cost tracking information
     * @return Cost information as JSON string
     */
    virtual std::string getCostTracking() const = 0;
    
    /**
     * Cancel all pending requests
     * @return Number of cancelled requests
     */
    virtual size_t cancelAllPendingRequests() = 0;
    
    /**
     * Update configuration
     * @param config New external services configuration
     * @return true if update successful
     */
    virtual bool updateConfiguration(const ExternalServicesConfig& config) = 0;
    
    /**
     * Get current configuration
     * @return Current external services configuration
     */
    virtual ExternalServicesConfig getCurrentConfiguration() const = 0;
    
    /**
     * Check if integrator is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
    
    /**
     * Get last error message
     * @return Last error message
     */
    virtual std::string getLastError() const = 0;
    
    /**
     * Reset integrator state
     */
    virtual void reset() = 0;
};

} // namespace advanced
} // namespace stt