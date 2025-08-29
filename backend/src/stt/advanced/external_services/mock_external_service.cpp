#include "stt/advanced/external_services/mock_external_service.hpp"
#include "utils/logging.hpp"
#include <chrono>
#include <sstream>

namespace stt {
namespace advanced {
namespace external_services {

MockExternalService::MockExternalService() 
    : gen_(rd_())
    , dist_(0.0f, 1.0f) {
    
    // Initialize rate limit info
    rateLimitInfo_.requestsPerMinute = 60;
    rateLimitInfo_.requestsPerHour = 1000;
    rateLimitInfo_.requestsPerDay = 10000;
    rateLimitInfo_.lastResetTime = std::chrono::steady_clock::now();
    
    // Initialize health status
    healthStatus_.isHealthy = true;
    healthStatus_.responseTime = 0.0f;
    healthStatus_.consecutiveFailures = 0;
    healthStatus_.successRate = 1.0f;
    healthStatus_.lastHealthCheck = std::chrono::steady_clock::now();
}

bool MockExternalService::initialize(const ExternalServiceInfo& serviceInfo,
                                    const ServiceAuthentication& auth) {
    serviceInfo_ = serviceInfo;
    auth_ = auth;
    
    // Update health status with service name
    healthStatus_.serviceName = serviceInfo.serviceName;
    
    initialized_ = true;
    LOG_INFO("Mock external service initialized: " + serviceInfo.serviceName);
    return true;
}

bool MockExternalService::transcribeAsync(const std::vector<float>& audioData,
                                         const std::string& language,
                                         std::function<void(const TranscriptionResult&)> callback) {
    if (!initialized_) {
        setLastError("Service not initialized");
        return false;
    }
    
    if (!isAvailable()) {
        setLastError("Service not available");
        return false;
    }
    
    // Check rate limits
    auto now = std::chrono::steady_clock::now();
    auto timeSinceReset = std::chrono::duration_cast<std::chrono::minutes>(
        now - rateLimitInfo_.lastResetTime).count();
    
    if (timeSinceReset >= 1) {
        // Reset minute counter
        rateLimitInfo_.currentMinuteRequests = 0;
        rateLimitInfo_.lastResetTime = now;
    }
    
    if (rateLimitInfo_.currentMinuteRequests >= rateLimitInfo_.requestsPerMinute) {
        rateLimitInfo_.isLimited = true;
        setLastError("Rate limit exceeded");
        return false;
    }
    
    rateLimitInfo_.currentMinuteRequests++;
    rateLimitInfo_.isLimited = false;
    
    pendingRequests_++;
    
    // Simulate async processing
    std::thread([this, audioData, language, callback]() {
        try {
            // Simulate processing delay
            simulateProcessingDelay();
            
            // Check if we should simulate failure
            if (shouldSimulateFailure()) {
                TranscriptionResult failedResult;
                failedResult.text = "";
                failedResult.confidence = 0.0f;
                failedResult.language = language;
                failedResult.processingTimeMs = simulatedLatencyMs_;
                
                callback(failedResult);
            } else {
                auto result = generateMockTranscription(audioData, language);
                callback(result);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Mock service async transcription failed: " + std::string(e.what()));
            TranscriptionResult errorResult;
            errorResult.text = "";
            errorResult.confidence = 0.0f;
            errorResult.language = language;
            callback(errorResult);
        }
        
        pendingRequests_--;
    }).detach();
    
    return true;
}

TranscriptionResult MockExternalService::transcribeSync(const std::vector<float>& audioData,
                                                       const std::string& language) {
    if (!initialized_) {
        setLastError("Service not initialized");
        return TranscriptionResult{};
    }
    
    if (!isAvailable()) {
        setLastError("Service not available");
        return TranscriptionResult{};
    }
    
    // Simulate processing delay
    simulateProcessingDelay();
    
    // Check if we should simulate failure
    if (shouldSimulateFailure()) {
        TranscriptionResult failedResult;
        failedResult.text = "";
        failedResult.confidence = 0.0f;
        failedResult.language = language;
        failedResult.processingTimeMs = simulatedLatencyMs_;
        return failedResult;
    }
    
    return generateMockTranscription(audioData, language);
}

ServiceHealthStatus MockExternalService::checkHealth() {
    auto now = std::chrono::steady_clock::now();
    auto startTime = now;
    
    // Simulate health check delay
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    auto endTime = std::chrono::steady_clock::now();
    float responseTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    
    healthStatus_.responseTime = responseTime;
    healthStatus_.lastHealthCheck = endTime;
    healthStatus_.isHealthy = simulatedHealthy_ && simulatedAvailable_;
    
    // Simulate occasional health check failures
    if (shouldSimulateFailure()) {
        healthStatus_.isHealthy = false;
        healthStatus_.consecutiveFailures++;
        healthStatus_.lastError = "Simulated health check failure";
    } else {
        healthStatus_.consecutiveFailures = 0;
        healthStatus_.lastError = "";
    }
    
    // Update success rate
    static size_t totalHealthChecks = 0;
    static size_t successfulHealthChecks = 0;
    
    totalHealthChecks++;
    if (healthStatus_.isHealthy) {
        successfulHealthChecks++;
    }
    
    healthStatus_.successRate = (totalHealthChecks > 0) ? 
        static_cast<float>(successfulHealthChecks) / totalHealthChecks : 0.0f;
    
    return healthStatus_;
}

RateLimitInfo MockExternalService::getRateLimitInfo() const {
    return rateLimitInfo_;
}

ExternalServiceInfo MockExternalService::getServiceInfo() const {
    return serviceInfo_;
}

bool MockExternalService::updateAuthentication(const ServiceAuthentication& auth) {
    auth_ = auth;
    LOG_INFO("Mock service authentication updated: " + serviceInfo_.serviceName);
    return true;
}

size_t MockExternalService::cancelPendingRequests() {
    size_t cancelled = pendingRequests_.load();
    pendingRequests_ = 0;
    LOG_INFO("Cancelled " + std::to_string(cancelled) + " pending requests for: " + serviceInfo_.serviceName);
    return cancelled;
}

std::vector<std::string> MockExternalService::getSupportedLanguages() const {
    return serviceInfo_.supportedLanguages;
}

bool MockExternalService::isAvailable() const {
    return initialized_ && simulatedAvailable_;
}

std::string MockExternalService::getLastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}

void MockExternalService::setSimulatedLatency(float latencyMs) {
    simulatedLatencyMs_ = std::max(0.0f, latencyMs);
    LOG_INFO("Mock service " + serviceInfo_.serviceName + " latency set to: " + std::to_string(latencyMs) + "ms");
}

void MockExternalService::setSimulatedReliability(float reliability) {
    simulatedReliability_ = std::clamp(reliability, 0.0f, 1.0f);
    LOG_INFO("Mock service " + serviceInfo_.serviceName + " reliability set to: " + std::to_string(reliability));
}

void MockExternalService::setSimulatedAvailability(bool available) {
    simulatedAvailable_ = available;
    LOG_INFO("Mock service " + serviceInfo_.serviceName + " availability set to: " + (available ? "true" : "false"));
}

void MockExternalService::setSimulatedHealthy(bool healthy) {
    simulatedHealthy_ = healthy;
    LOG_INFO("Mock service " + serviceInfo_.serviceName + " health set to: " + (healthy ? "healthy" : "unhealthy"));
}

void MockExternalService::setLastError(const std::string& error) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_ = error;
    LOG_ERROR("Mock service " + serviceInfo_.serviceName + " error: " + error);
}

TranscriptionResult MockExternalService::generateMockTranscription(const std::vector<float>& audioData,
                                                                  const std::string& language) {
    TranscriptionResult result;
    
    // Generate mock transcription based on audio length
    float audioDurationSeconds = static_cast<float>(audioData.size()) / 16000.0f; // Assume 16kHz
    
    // Generate different mock texts based on duration
    if (audioDurationSeconds < 1.0f) {
        result.text = "Hello";
    } else if (audioDurationSeconds < 3.0f) {
        result.text = "Hello, how are you?";
    } else if (audioDurationSeconds < 5.0f) {
        result.text = "Hello, how are you doing today?";
    } else {
        result.text = "Hello, how are you doing today? I hope you're having a great day.";
    }
    
    // Add language-specific variations
    if (language == "es") {
        result.text = "Hola, ¿cómo estás?";
    } else if (language == "fr") {
        result.text = "Bonjour, comment allez-vous?";
    } else if (language == "de") {
        result.text = "Hallo, wie geht es Ihnen?";
    }
    
    // Generate confidence based on reliability
    float baseConfidence = 0.85f + (dist_(gen_) * 0.15f); // 0.85 to 1.0
    result.confidence = baseConfidence * simulatedReliability_;
    
    result.language = language.empty() ? "en" : language;
    result.processingTimeMs = simulatedLatencyMs_;
    
    // Add some random variation to processing time
    float variation = (dist_(gen_) - 0.5f) * 0.2f; // ±10% variation
    result.processingTimeMs *= (1.0f + variation);
    
    return result;
}

bool MockExternalService::shouldSimulateFailure() const {
    return dist_(gen_) > simulatedReliability_;
}

void MockExternalService::simulateProcessingDelay() const {
    if (simulatedLatencyMs_ > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(simulatedLatencyMs_)));
    }
}

// MockServiceFactory implementation
std::shared_ptr<ExternalSTTService> MockServiceFactory::createMockService(
    const std::string& serviceName,
    const std::string& serviceType) {
    
    auto service = std::make_shared<MockExternalService>();
    
    ExternalServiceInfo serviceInfo;
    serviceInfo.serviceName = serviceName;
    serviceInfo.serviceType = serviceType;
    serviceInfo.endpoint = "mock://localhost/" + serviceName;
    serviceInfo.isAvailable = true;
    serviceInfo.reliability = 0.95f;
    serviceInfo.averageLatency = 500.0f;
    serviceInfo.costPerMinute = 0.01f;
    serviceInfo.supportedLanguages = {"en", "es", "fr", "de", "it", "pt", "ru", "zh", "ja", "ko"};
    
    ServiceAuthentication auth;
    auth.authType = "mock";
    auth.credentials = "mock_credentials";
    
    service->initialize(serviceInfo, auth);
    
    return service;
}

std::shared_ptr<ExternalSTTService> MockServiceFactory::createReliableService(
    const std::string& serviceName) {
    
    auto service = std::static_pointer_cast<MockExternalService>(
        createMockService(serviceName, "reliable_mock"));
    
    service->setSimulatedReliability(0.99f);
    service->setSimulatedLatency(300.0f);
    
    return service;
}

std::shared_ptr<ExternalSTTService> MockServiceFactory::createUnreliableService(
    const std::string& serviceName) {
    
    auto service = std::static_pointer_cast<MockExternalService>(
        createMockService(serviceName, "unreliable_mock"));
    
    service->setSimulatedReliability(0.70f);
    service->setSimulatedLatency(800.0f);
    
    return service;
}

std::shared_ptr<ExternalSTTService> MockServiceFactory::createFastService(
    const std::string& serviceName) {
    
    auto service = std::static_pointer_cast<MockExternalService>(
        createMockService(serviceName, "fast_mock"));
    
    service->setSimulatedReliability(0.95f);
    service->setSimulatedLatency(150.0f);
    
    return service;
}

std::shared_ptr<ExternalSTTService> MockServiceFactory::createSlowService(
    const std::string& serviceName) {
    
    auto service = std::static_pointer_cast<MockExternalService>(
        createMockService(serviceName, "slow_mock"));
    
    service->setSimulatedReliability(0.95f);
    service->setSimulatedLatency(1200.0f);
    
    return service;
}

} // namespace external_services
} // namespace advanced
} // namespace stt