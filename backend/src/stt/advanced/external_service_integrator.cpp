#include "stt/advanced/external_service_integrator.hpp"
#include "stt/advanced/external_services/mock_external_service.hpp"
#include "utils/json_utils.hpp"
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace stt {
namespace advanced {

// ServiceReliabilityTracker implementation
void ServiceReliabilityTracker::recordSuccess(const std::string& serviceName) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    auto& stats = serviceStats_[serviceName];
    stats.totalRequests++;
    stats.successfulRequests++;
    stats.lastUpdate = std::chrono::steady_clock::now();
}

void ServiceReliabilityTracker::recordFailure(const std::string& serviceName) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    auto& stats = serviceStats_[serviceName];
    stats.totalRequests++;
    stats.lastUpdate = std::chrono::steady_clock::now();
}

float ServiceReliabilityTracker::getReliability(const std::string& serviceName) const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    auto it = serviceStats_.find(serviceName);
    if (it == serviceStats_.end() || it->second.totalRequests == 0) {
        return 0.0f;
    }
    return static_cast<float>(it->second.successfulRequests) / it->second.totalRequests;
}

void ServiceReliabilityTracker::updateLatency(const std::string& serviceName, float latencyMs) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    auto& stats = serviceStats_[serviceName];
    stats.totalLatency += latencyMs;
    stats.latencyCount++;
}

float ServiceReliabilityTracker::getAverageLatency(const std::string& serviceName) const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    auto it = serviceStats_.find(serviceName);
    if (it == serviceStats_.end() || it->second.latencyCount == 0) {
        return 0.0f;
    }
    return it->second.totalLatency / it->second.latencyCount;
}

// ServiceCostTracker implementation
void ServiceCostTracker::recordUsage(const std::string& serviceName, float durationMinutes, float costPerMinute) {
    std::lock_guard<std::mutex> lock(costMutex_);
    auto& cost = serviceCosts_[serviceName];
    float sessionCost = durationMinutes * costPerMinute;
    cost.totalCost += sessionCost;
    cost.dailyCost += sessionCost;
    cost.totalRequests++;
}

float ServiceCostTracker::getTotalCost(const std::string& serviceName) const {
    std::lock_guard<std::mutex> lock(costMutex_);
    auto it = serviceCosts_.find(serviceName);
    return (it != serviceCosts_.end()) ? it->second.totalCost : 0.0f;
}

float ServiceCostTracker::getDailyCost(const std::string& serviceName) const {
    std::lock_guard<std::mutex> lock(costMutex_);
    auto it = serviceCosts_.find(serviceName);
    return (it != serviceCosts_.end()) ? it->second.dailyCost : 0.0f;
}

std::string ServiceCostTracker::getCostReport() const {
    std::lock_guard<std::mutex> lock(costMutex_);
    std::ostringstream report;
    report << std::fixed << std::setprecision(2);
    report << "{\n";
    report << "  \"services\": [\n";
    
    bool first = true;
    for (const auto& [serviceName, cost] : serviceCosts_) {
        if (!first) report << ",\n";
        report << "    {\n";
        report << "      \"name\": \"" << serviceName << "\",\n";
        report << "      \"totalCost\": " << cost.totalCost << ",\n";
        report << "      \"dailyCost\": " << cost.dailyCost << ",\n";
        report << "      \"totalRequests\": " << cost.totalRequests << "\n";
        report << "    }";
        first = false;
    }
    
    report << "\n  ]\n";
    report << "}";
    return report.str();
}

void ServiceCostTracker::resetDailyCosts() {
    std::lock_guard<std::mutex> lock(costMutex_);
    for (auto& [serviceName, cost] : serviceCosts_) {
        cost.dailyCost = 0.0f;
        cost.lastReset = std::chrono::steady_clock::now();
    }
}

// ConfidenceWeightedFusion implementation
bool ConfidenceWeightedFusion::initialize(const ResultFusionConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_ = config;
    serviceWeights_ = config.serviceWeights;
    initialized_ = true;
    LOG_INFO("Confidence weighted fusion initialized with strategy: " + config.fusionStrategy);
    return true;
}

FusedTranscriptionResult ConfidenceWeightedFusion::fuseResults(
    const std::vector<TranscriptionResult>& results,
    const std::vector<std::string>& serviceNames) {
    
    if (!initialized_) {
        LOG_ERROR("Fusion engine not initialized");
        return FusedTranscriptionResult{};
    }
    
    if (results.empty() || results.size() != serviceNames.size()) {
        LOG_ERROR("Invalid input for result fusion");
        return FusedTranscriptionResult{};
    }
    
    std::lock_guard<std::mutex> lock(configMutex_);
    
    FusedTranscriptionResult fusedResult;
    
    if (config_.fusionStrategy == "confidence_weighted") {
        fusedResult = performConfidenceWeightedFusion(results, serviceNames);
    } else if (config_.fusionStrategy == "majority_vote") {
        fusedResult = performMajorityVoteFusion(results, serviceNames);
    } else if (config_.fusionStrategy == "best_confidence") {
        fusedResult = performBestConfidenceFusion(results, serviceNames);
    } else {
        LOG_WARNING("Unknown fusion strategy: " + config_.fusionStrategy + ", using confidence_weighted");
        fusedResult = performConfidenceWeightedFusion(results, serviceNames);
    }
    
    // Update statistics
    {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        totalFusions_++;
        
        // Calculate confidence improvement
        float maxOriginalConfidence = 0.0f;
        for (const auto& result : results) {
            maxOriginalConfidence = std::max(maxOriginalConfidence, result.confidence);
        }
        
        float improvement = fusedResult.fusionConfidence - maxOriginalConfidence;
        averageConfidenceImprovement_ = 
            (averageConfidenceImprovement_ * (totalFusions_ - 1) + improvement) / totalFusions_;
    }
    
    return fusedResult;
}

FusedTranscriptionResult ConfidenceWeightedFusion::performConfidenceWeightedFusion(
    const std::vector<TranscriptionResult>& results,
    const std::vector<std::string>& serviceNames) {
    
    FusedTranscriptionResult fusedResult;
    fusedResult.individualResults = results;
    fusedResult.fusionMethod = "confidence_weighted";
    fusedResult.servicesUsed = results.size();
    
    // Calculate weighted confidence and select best text
    float totalWeight = 0.0f;
    float weightedConfidence = 0.0f;
    std::string bestText;
    float bestScore = 0.0f;
    
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        const auto& serviceName = serviceNames[i];
        
        // Get service weight (default to 1.0 if not specified)
        float serviceWeight = 1.0f;
        auto weightIt = serviceWeights_.find(serviceName);
        if (weightIt != serviceWeights_.end()) {
            serviceWeight = weightIt->second;
        }
        
        // Calculate combined score (confidence * service weight)
        float combinedScore = result.confidence * serviceWeight;
        
        // Update weighted confidence
        totalWeight += serviceWeight;
        weightedConfidence += result.confidence * serviceWeight;
        
        // Track best result
        if (combinedScore > bestScore) {
            bestScore = combinedScore;
            bestText = result.text;
        }
        
        // Record service contribution
        fusedResult.serviceContributions[serviceName] = combinedScore;
    }
    
    // Set fused result
    fusedResult.fusedResult.text = bestText;
    fusedResult.fusedResult.confidence = (totalWeight > 0) ? weightedConfidence / totalWeight : 0.0f;
    fusedResult.fusedResult.language = results[0].language; // Assume same language
    fusedResult.fusedResult.processingTimeMs = 
        *std::max_element(results.begin(), results.end(),
                         [](const auto& a, const auto& b) {
                             return a.processingTimeMs < b.processingTimeMs;
                         }).processingTimeMs;
    
    fusedResult.fusionConfidence = fusedResult.fusedResult.confidence;
    
    return fusedResult;
}

FusedTranscriptionResult ConfidenceWeightedFusion::performMajorityVoteFusion(
    const std::vector<TranscriptionResult>& results,
    const std::vector<std::string>& serviceNames) {
    
    FusedTranscriptionResult fusedResult;
    fusedResult.individualResults = results;
    fusedResult.fusionMethod = "majority_vote";
    fusedResult.servicesUsed = results.size();
    
    // Count occurrences of each text result
    std::map<std::string, std::vector<size_t>> textVotes;
    for (size_t i = 0; i < results.size(); ++i) {
        textVotes[results[i].text].push_back(i);
    }
    
    // Find majority result
    std::string majorityText;
    size_t maxVotes = 0;
    float totalConfidence = 0.0f;
    
    for (const auto& [text, votes] : textVotes) {
        if (votes.size() > maxVotes) {
            maxVotes = votes.size();
            majorityText = text;
            
            // Calculate average confidence for this text
            totalConfidence = 0.0f;
            for (size_t idx : votes) {
                totalConfidence += results[idx].confidence;
            }
        }
    }
    
    // Set fused result
    fusedResult.fusedResult.text = majorityText;
    fusedResult.fusedResult.confidence = (maxVotes > 0) ? totalConfidence / maxVotes : 0.0f;
    fusedResult.fusedResult.language = results[0].language;
    fusedResult.fusedResult.processingTimeMs = 
        std::accumulate(results.begin(), results.end(), 0.0f,
                       [](float sum, const auto& result) {
                           return sum + result.processingTimeMs;
                       }) / results.size();
    
    fusedResult.fusionConfidence = fusedResult.fusedResult.confidence;
    
    // Record service contributions
    for (size_t i = 0; i < results.size(); ++i) {
        float contribution = (results[i].text == majorityText) ? 1.0f : 0.0f;
        fusedResult.serviceContributions[serviceNames[i]] = contribution;
    }
    
    return fusedResult;
}

FusedTranscriptionResult ConfidenceWeightedFusion::performBestConfidenceFusion(
    const std::vector<TranscriptionResult>& results,
    const std::vector<std::string>& serviceNames) {
    
    FusedTranscriptionResult fusedResult;
    fusedResult.individualResults = results;
    fusedResult.fusionMethod = "best_confidence";
    fusedResult.servicesUsed = results.size();
    
    // Find result with highest confidence
    size_t bestIndex = 0;
    float bestConfidence = results[0].confidence;
    
    for (size_t i = 1; i < results.size(); ++i) {
        if (results[i].confidence > bestConfidence) {
            bestConfidence = results[i].confidence;
            bestIndex = i;
        }
    }
    
    // Use best result
    fusedResult.fusedResult = results[bestIndex];
    fusedResult.fusionConfidence = bestConfidence;
    
    // Record service contributions
    for (size_t i = 0; i < results.size(); ++i) {
        float contribution = (i == bestIndex) ? 1.0f : 0.0f;
        fusedResult.serviceContributions[serviceNames[i]] = contribution;
    }
    
    return fusedResult;
}

bool ConfidenceWeightedFusion::updateConfiguration(const ResultFusionConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_ = config;
    serviceWeights_ = config.serviceWeights;
    LOG_INFO("Fusion configuration updated");
    return true;
}

void ConfidenceWeightedFusion::setServiceWeights(const std::map<std::string, float>& weights) {
    std::lock_guard<std::mutex> lock(configMutex_);
    serviceWeights_ = weights;
    config_.serviceWeights = weights;
}

std::string ConfidenceWeightedFusion::getFusionStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    std::ostringstream stats;
    stats << std::fixed << std::setprecision(3);
    stats << "{\n";
    stats << "  \"totalFusions\": " << totalFusions_ << ",\n";
    stats << "  \"averageConfidenceImprovement\": " << averageConfidenceImprovement_ << "\n";
    stats << "}";
    return stats.str();
}

bool ConfidenceWeightedFusion::isInitialized() const {
    return initialized_;
}

// ServiceHealthMonitorImpl implementation
bool ServiceHealthMonitorImpl::initialize(int checkIntervalMs) {
    checkIntervalMs_ = checkIntervalMs;
    LOG_INFO("Service health monitor initialized with " + std::to_string(checkIntervalMs) + "ms interval");
    return true;
}

bool ServiceHealthMonitorImpl::addService(std::shared_ptr<ExternalSTTService> service) {
    if (!service) {
        LOG_ERROR("Cannot add null service to health monitor");
        return false;
    }
    
    auto serviceInfo = service->getServiceInfo();
    std::lock_guard<std::mutex> lock(servicesMutex_);
    services_[serviceInfo.serviceName] = service;
    
    // Initialize health status
    ServiceHealthStatus status;
    status.serviceName = serviceInfo.serviceName;
    status.isHealthy = false;
    status.lastHealthCheck = std::chrono::steady_clock::now();
    
    {
        std::lock_guard<std::mutex> healthLock(healthMutex_);
        healthStatus_[serviceInfo.serviceName] = status;
    }
    
    LOG_INFO("Added service to health monitor: " + serviceInfo.serviceName);
    return true;
}

bool ServiceHealthMonitorImpl::removeService(const std::string& serviceName) {
    std::lock_guard<std::mutex> lock(servicesMutex_);
    auto it = services_.find(serviceName);
    if (it == services_.end()) {
        return false;
    }
    
    services_.erase(it);
    
    {
        std::lock_guard<std::mutex> healthLock(healthMutex_);
        healthStatus_.erase(serviceName);
    }
    
    LOG_INFO("Removed service from health monitor: " + serviceName);
    return true;
}

bool ServiceHealthMonitorImpl::startMonitoring() {
    if (monitoring_) {
        LOG_WARNING("Health monitoring already started");
        return true;
    }
    
    stopRequested_ = false;
    monitoring_ = true;
    monitorThread_ = std::thread(&ServiceHealthMonitorImpl::monitoringLoop, this);
    
    LOG_INFO("Started service health monitoring");
    return true;
}

void ServiceHealthMonitorImpl::stopMonitoring() {
    if (!monitoring_) {
        return;
    }
    
    stopRequested_ = true;
    monitoring_ = false;
    
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
    
    LOG_INFO("Stopped service health monitoring");
}

void ServiceHealthMonitorImpl::monitoringLoop() {
    while (!stopRequested_) {
        std::unordered_map<std::string, std::shared_ptr<ExternalSTTService>> currentServices;
        
        // Copy services to avoid holding lock during health checks
        {
            std::lock_guard<std::mutex> lock(servicesMutex_);
            currentServices = services_;
        }
        
        // Check health of all services
        for (const auto& [serviceName, service] : currentServices) {
            if (stopRequested_) break;
            checkServiceHealth(serviceName, service);
        }
        
        // Wait for next check interval
        std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs_));
    }
}

void ServiceHealthMonitorImpl::checkServiceHealth(const std::string& serviceName, 
                                                 std::shared_ptr<ExternalSTTService> service) {
    auto startTime = std::chrono::steady_clock::now();
    ServiceHealthStatus newStatus = service->checkHealth();
    auto endTime = std::chrono::steady_clock::now();
    
    newStatus.responseTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    newStatus.lastHealthCheck = endTime;
    
    ServiceHealthStatus oldStatus;
    bool statusChanged = false;
    
    {
        std::lock_guard<std::mutex> lock(healthMutex_);
        auto it = healthStatus_.find(serviceName);
        if (it != healthStatus_.end()) {
            oldStatus = it->second;
            statusChanged = (oldStatus.isHealthy != newStatus.isHealthy);
        }
        healthStatus_[serviceName] = newStatus;
    }
    
    if (statusChanged) {
        notifyHealthChange(serviceName, newStatus);
    }
}

void ServiceHealthMonitorImpl::notifyHealthChange(const std::string& serviceName, 
                                                 const ServiceHealthStatus& status) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    for (const auto& callback : callbacks_) {
        try {
            callback(serviceName, status);
        } catch (const std::exception& e) {
            LOG_ERROR("Health change callback failed: " + std::string(e.what()));
        }
    }
}

std::map<std::string, ServiceHealthStatus> ServiceHealthMonitorImpl::getAllHealthStatus() const {
    std::lock_guard<std::mutex> lock(healthMutex_);
    return healthStatus_;
}

ServiceHealthStatus ServiceHealthMonitorImpl::getServiceHealth(const std::string& serviceName) const {
    std::lock_guard<std::mutex> lock(healthMutex_);
    auto it = healthStatus_.find(serviceName);
    return (it != healthStatus_.end()) ? it->second : ServiceHealthStatus{};
}

std::vector<std::string> ServiceHealthMonitorImpl::getHealthyServices() const {
    std::vector<std::string> healthyServices;
    std::lock_guard<std::mutex> lock(healthMutex_);
    
    for (const auto& [serviceName, status] : healthStatus_) {
        if (status.isHealthy) {
            healthyServices.push_back(serviceName);
        }
    }
    
    return healthyServices;
}

void ServiceHealthMonitorImpl::registerHealthChangeCallback(
    std::function<void(const std::string&, const ServiceHealthStatus&)> callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    callbacks_.push_back(callback);
}

bool ServiceHealthMonitorImpl::isMonitoring() const {
    return monitoring_;
}

// ExternalServiceIntegrator implementation
ExternalServiceIntegrator::ExternalServiceIntegrator() 
    : fusionEngine_(std::make_unique<ConfidenceWeightedFusion>())
    , healthMonitor_(std::make_unique<ServiceHealthMonitorImpl>())
    , reliabilityTracker_(std::make_unique<ServiceReliabilityTracker>())
    , costTracker_(std::make_unique<ServiceCostTracker>()) {
}

ExternalServiceIntegrator::~ExternalServiceIntegrator() {
    if (initialized_) {
        reset();
    }
}

bool ExternalServiceIntegrator::initialize(const ExternalServicesConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    if (initialized_) {
        LOG_WARNING("External service integrator already initialized");
        return true;
    }
    
    config_ = config;
    
    // Initialize fusion engine
    ResultFusionConfig fusionConfig;
    fusionConfig.enableFusion = config.enableResultFusion;
    fusionConfig.confidenceThreshold = config.fallbackThreshold;
    fusionConfig.serviceWeights = config.serviceWeights;
    
    if (!fusionEngine_->initialize(fusionConfig)) {
        setLastError("Failed to initialize fusion engine");
        return false;
    }
    
    // Initialize health monitor
    if (!healthMonitor_->initialize(30000)) { // 30 second intervals
        setLastError("Failed to initialize health monitor");
        return false;
    }
    
    // Set initial configuration
    privacyMode_ = config.enablePrivacyMode;
    fallbackThreshold_ = config.fallbackThreshold;
    
    initialized_ = true;
    LOG_INFO("External service integrator initialized successfully");
    return true;
}

bool ExternalServiceIntegrator::addExternalService(const ExternalServiceInfo& serviceInfo,
                                                  const ServiceAuthentication& auth) {
    if (!initialized_) {
        setLastError("Integrator not initialized");
        return false;
    }
    
    if (privacyMode_ && serviceInfo.serviceType != "local") {
        setLastError("Cannot add non-local service in privacy mode");
        return false;
    }
    
    auto service = createService(serviceInfo);
    if (!service) {
        setLastError("Failed to create service: " + serviceInfo.serviceName);
        return false;
    }
    
    if (!service->initialize(serviceInfo, auth)) {
        setLastError("Failed to initialize service: " + serviceInfo.serviceName);
        return false;
    }
    
    {
        std::lock_guard<std::mutex> lock(servicesMutex_);
        services_[serviceInfo.serviceName] = service;
        serviceAuth_[serviceInfo.serviceName] = auth;
    }
    
    // Add to health monitor
    healthMonitor_->addService(service);
    
    LOG_INFO("Added external service: " + serviceInfo.serviceName);
    return true;
}

bool ExternalServiceIntegrator::removeExternalService(const std::string& serviceName) {
    std::lock_guard<std::mutex> lock(servicesMutex_);
    
    auto it = services_.find(serviceName);
    if (it == services_.end()) {
        setLastError("Service not found: " + serviceName);
        return false;
    }
    
    // Cancel pending requests for this service
    it->second->cancelPendingRequests();
    
    // Remove from collections
    services_.erase(it);
    serviceAuth_.erase(serviceName);
    
    // Remove from health monitor
    healthMonitor_->removeService(serviceName);
    
    LOG_INFO("Removed external service: " + serviceName);
    return true;
}

bool ExternalServiceIntegrator::transcribeWithFallback(
    const std::vector<float>& audioData,
    const std::string& language,
    const std::vector<std::string>& preferredServices,
    std::function<void(const FusedTranscriptionResult&)> callback) {
    
    if (!initialized_) {
        setLastError("Integrator not initialized");
        return false;
    }
    
    auto selectedServices = selectServicesForRequest(preferredServices, language);
    if (selectedServices.empty()) {
        setLastError("No available services for request");
        return false;
    }
    
    size_t requestId = nextRequestId_++;
    
    // Try services in order until one succeeds
    auto tryNextService = [this, audioData, language, selectedServices, callback, requestId](size_t serviceIndex) mutable {
        if (serviceIndex >= selectedServices.size()) {
            // All services failed
            FusedTranscriptionResult failedResult;
            failedResult.fusedResult.text = "";
            failedResult.fusedResult.confidence = 0.0f;
            failedResult.fusionMethod = "fallback_failed";
            callback(failedResult);
            return;
        }
        
        const std::string& serviceName = selectedServices[serviceIndex];
        std::shared_ptr<ExternalSTTService> service;
        
        {
            std::lock_guard<std::mutex> lock(servicesMutex_);
            auto it = services_.find(serviceName);
            if (it == services_.end()) {
                // Service not available, try next
                tryNextService(serviceIndex + 1);
                return;
            }
            service = it->second;
        }
        
        // Try transcription with this service
        auto startTime = std::chrono::steady_clock::now();
        
        service->transcribeAsync(audioData, language, 
            [this, serviceName, startTime, callback, requestId, tryNextService, serviceIndex]
            (const TranscriptionResult& result) {
                
                auto endTime = std::chrono::steady_clock::now();
                float latencyMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
                
                reliabilityTracker_->updateLatency(serviceName, latencyMs);
                
                if (result.confidence >= fallbackThreshold_) {
                    // Success - use this result
                    reliabilityTracker_->recordSuccess(serviceName);
                    
                    FusedTranscriptionResult fusedResult;
                    fusedResult.fusedResult = result;
                    fusedResult.individualResults = {result};
                    fusedResult.serviceContributions[serviceName] = 1.0f;
                    fusedResult.fusionMethod = "fallback_success";
                    fusedResult.fusionConfidence = result.confidence;
                    fusedResult.servicesUsed = 1;
                    
                    callback(fusedResult);
                } else {
                    // Confidence too low, try next service
                    reliabilityTracker_->recordFailure(serviceName);
                    tryNextService(serviceIndex + 1);
                }
            });
    };
    
    // Start with first service
    tryNextService(0);
    
    return true;
}

bool ExternalServiceIntegrator::transcribeWithFusion(
    const std::vector<float>& audioData,
    const std::string& language,
    const std::vector<std::string>& services,
    std::function<void(const FusedTranscriptionResult&)> callback) {
    
    if (!initialized_) {
        setLastError("Integrator not initialized");
        return false;
    }
    
    if (!config_.enableResultFusion) {
        setLastError("Result fusion is disabled");
        return false;
    }
    
    auto selectedServices = selectServicesForRequest(services, language);
    if (selectedServices.size() < config_.minServicesForFusion) {
        setLastError("Insufficient services for fusion");
        return false;
    }
    
    size_t requestId = nextRequestId_++;
    
    // Collect results from all services
    auto results = std::make_shared<std::vector<TranscriptionResult>>();
    auto serviceNames = std::make_shared<std::vector<std::string>>();
    auto completedCount = std::make_shared<std::atomic<size_t>>(0);
    auto totalServices = selectedServices.size();
    
    for (const auto& serviceName : selectedServices) {
        std::shared_ptr<ExternalSTTService> service;
        
        {
            std::lock_guard<std::mutex> lock(servicesMutex_);
            auto it = services_.find(serviceName);
            if (it == services_.end()) {
                continue;
            }
            service = it->second;
        }
        
        auto startTime = std::chrono::steady_clock::now();
        
        service->transcribeAsync(audioData, language,
            [this, serviceName, startTime, results, serviceNames, completedCount, totalServices, callback]
            (const TranscriptionResult& result) {
                
                auto endTime = std::chrono::steady_clock::now();
                float latencyMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
                
                reliabilityTracker_->updateLatency(serviceName, latencyMs);
                
                // Add result to collection
                results->push_back(result);
                serviceNames->push_back(serviceName);
                
                size_t completed = ++(*completedCount);
                
                if (completed == totalServices) {
                    // All services completed, perform fusion
                    auto fusedResult = fusionEngine_->fuseResults(*results, *serviceNames);
                    callback(fusedResult);
                }
            });
    }
    
    return true;
}

std::vector<std::string> ExternalServiceIntegrator::getAvailableServices() const {
    std::vector<std::string> availableServices;
    std::lock_guard<std::mutex> lock(servicesMutex_);
    
    for (const auto& [serviceName, service] : services_) {
        if (service->isAvailable()) {
            availableServices.push_back(serviceName);
        }
    }
    
    return availableServices;
}

std::vector<std::string> ExternalServiceIntegrator::getHealthyServices() const {
    return healthMonitor_->getHealthyServices();
}

ServiceHealthStatus ExternalServiceIntegrator::getServiceHealth(const std::string& serviceName) const {
    return healthMonitor_->getServiceHealth(serviceName);
}

bool ExternalServiceIntegrator::updateServiceConfig(const std::string& serviceName,
                                                   const ExternalServiceInfo& serviceInfo) {
    std::lock_guard<std::mutex> lock(servicesMutex_);
    
    auto it = services_.find(serviceName);
    if (it == services_.end()) {
        setLastError("Service not found: " + serviceName);
        return false;
    }
    
    // Re-initialize service with new config
    auto authIt = serviceAuth_.find(serviceName);
    if (authIt == serviceAuth_.end()) {
        setLastError("Authentication not found for service: " + serviceName);
        return false;
    }
    
    if (!it->second->initialize(serviceInfo, authIt->second)) {
        setLastError("Failed to update service config: " + serviceName);
        return false;
    }
    
    LOG_INFO("Updated service config: " + serviceName);
    return true;
}

bool ExternalServiceIntegrator::updateServiceAuth(const std::string& serviceName,
                                                 const ServiceAuthentication& auth) {
    std::lock_guard<std::mutex> lock(servicesMutex_);
    
    auto it = services_.find(serviceName);
    if (it == services_.end()) {
        setLastError("Service not found: " + serviceName);
        return false;
    }
    
    if (!it->second->updateAuthentication(auth)) {
        setLastError("Failed to update service authentication: " + serviceName);
        return false;
    }
    
    serviceAuth_[serviceName] = auth;
    LOG_INFO("Updated service authentication: " + serviceName);
    return true;
}

void ExternalServiceIntegrator::setResultFusionEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.enableResultFusion = enabled;
    
    ResultFusionConfig fusionConfig;
    fusionConfig.enableFusion = enabled;
    fusionConfig.confidenceThreshold = config_.fallbackThreshold;
    fusionConfig.serviceWeights = config_.serviceWeights;
    
    fusionEngine_->updateConfiguration(fusionConfig);
    LOG_INFO("Result fusion " + std::string(enabled ? "enabled" : "disabled"));
}

void ExternalServiceIntegrator::setFallbackThreshold(float threshold) {
    fallbackThreshold_ = std::clamp(threshold, 0.0f, 1.0f);
    LOG_INFO("Fallback threshold set to: " + std::to_string(threshold));
}

void ExternalServiceIntegrator::setPrivacyMode(bool enabled) {
    privacyMode_ = enabled;
    
    if (enabled) {
        // Remove non-local services
        std::vector<std::string> servicesToRemove;
        {
            std::lock_guard<std::mutex> lock(servicesMutex_);
            for (const auto& [serviceName, service] : services_) {
                auto serviceInfo = service->getServiceInfo();
                if (serviceInfo.serviceType != "local") {
                    servicesToRemove.push_back(serviceName);
                }
            }
        }
        
        for (const auto& serviceName : servicesToRemove) {
            removeExternalService(serviceName);
        }
    }
    
    LOG_INFO("Privacy mode " + std::string(enabled ? "enabled" : "disabled"));
}

std::string ExternalServiceIntegrator::getServiceUsageStats() const {
    std::ostringstream stats;
    stats << "{\n";
    stats << "  \"services\": [\n";
    
    std::lock_guard<std::mutex> lock(servicesMutex_);
    bool first = true;
    
    for (const auto& [serviceName, service] : services_) {
        if (!first) stats << ",\n";
        
        float reliability = reliabilityTracker_->getReliability(serviceName);
        float avgLatency = reliabilityTracker_->getAverageLatency(serviceName);
        
        stats << "    {\n";
        stats << "      \"name\": \"" << serviceName << "\",\n";
        stats << "      \"reliability\": " << std::fixed << std::setprecision(3) << reliability << ",\n";
        stats << "      \"averageLatency\": " << avgLatency << ",\n";
        stats << "      \"isAvailable\": " << (service->isAvailable() ? "true" : "false") << "\n";
        stats << "    }";
        
        first = false;
    }
    
    stats << "\n  ]\n";
    stats << "}";
    return stats.str();
}

std::string ExternalServiceIntegrator::getCostTracking() const {
    return costTracker_->getCostReport();
}

size_t ExternalServiceIntegrator::cancelAllPendingRequests() {
    size_t totalCancelled = 0;
    std::lock_guard<std::mutex> lock(servicesMutex_);
    
    for (const auto& [serviceName, service] : services_) {
        totalCancelled += service->cancelPendingRequests();
    }
    
    {
        std::lock_guard<std::mutex> requestsLock(requestsMutex_);
        pendingRequests_.clear();
    }
    
    LOG_INFO("Cancelled " + std::to_string(totalCancelled) + " pending requests");
    return totalCancelled;
}

bool ExternalServiceIntegrator::updateConfiguration(const ExternalServicesConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_ = config;
    
    // Update fusion engine
    ResultFusionConfig fusionConfig;
    fusionConfig.enableFusion = config.enableResultFusion;
    fusionConfig.confidenceThreshold = config.fallbackThreshold;
    fusionConfig.serviceWeights = config.serviceWeights;
    
    fusionEngine_->updateConfiguration(fusionConfig);
    
    // Update other settings
    privacyMode_ = config.enablePrivacyMode;
    fallbackThreshold_ = config.fallbackThreshold;
    
    LOG_INFO("External service integrator configuration updated");
    return true;
}

ExternalServicesConfig ExternalServiceIntegrator::getCurrentConfiguration() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return config_;
}

bool ExternalServiceIntegrator::isInitialized() const {
    return initialized_;
}

std::string ExternalServiceIntegrator::getLastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}

void ExternalServiceIntegrator::reset() {
    if (!initialized_) {
        return;
    }
    
    // Cancel all pending requests
    cancelAllPendingRequests();
    
    // Stop health monitoring
    healthMonitor_->stopMonitoring();
    
    // Clear services
    {
        std::lock_guard<std::mutex> lock(servicesMutex_);
        services_.clear();
        serviceAuth_.clear();
    }
    
    initialized_ = false;
    LOG_INFO("External service integrator reset");
}

// Private helper methods
void ExternalServiceIntegrator::setLastError(const std::string& error) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_ = error;
    LOG_ERROR("External service integrator error: " + error);
}

std::vector<std::string> ExternalServiceIntegrator::selectServicesForRequest(
    const std::vector<std::string>& preferredServices,
    const std::string& language) const {
    
    std::vector<std::string> selectedServices;
    std::lock_guard<std::mutex> lock(servicesMutex_);
    
    // First, try preferred services in order
    for (const auto& serviceName : preferredServices) {
        if (shouldUseService(serviceName) && isServiceCompatible(serviceName, language)) {
            selectedServices.push_back(serviceName);
        }
    }
    
    // If no preferred services available, use any compatible service
    if (selectedServices.empty()) {
        for (const auto& [serviceName, service] : services_) {
            if (shouldUseService(serviceName) && isServiceCompatible(serviceName, language)) {
                selectedServices.push_back(serviceName);
            }
        }
    }
    
    return selectedServices;
}

bool ExternalServiceIntegrator::isServiceCompatible(const std::string& serviceName, 
                                                   const std::string& language) const {
    auto it = services_.find(serviceName);
    if (it == services_.end()) {
        return false;
    }
    
    if (language.empty()) {
        return true; // No language requirement
    }
    
    auto supportedLanguages = it->second->getSupportedLanguages();
    return std::find(supportedLanguages.begin(), supportedLanguages.end(), language) != supportedLanguages.end();
}

bool ExternalServiceIntegrator::isDataLocalityCompliant(const std::string& serviceName) const {
    auto it = services_.find(serviceName);
    if (it == services_.end()) {
        return false;
    }
    
    auto serviceInfo = it->second->getServiceInfo();
    
    // In privacy mode, only allow local services
    if (privacyMode_ && serviceInfo.serviceType != "local") {
        return false;
    }
    
    return true;
}

bool ExternalServiceIntegrator::shouldUseService(const std::string& serviceName) const {
    // Check if service exists and is available
    auto it = services_.find(serviceName);
    if (it == services_.end() || !it->second->isAvailable()) {
        return false;
    }
    
    // Check data locality compliance
    if (!isDataLocalityCompliant(serviceName)) {
        return false;
    }
    
    // Check health status
    auto healthStatus = healthMonitor_->getServiceHealth(serviceName);
    if (!healthStatus.isHealthy) {
        return false;
    }
    
    return true;
}

std::shared_ptr<ExternalSTTService> ExternalServiceIntegrator::createService(const ExternalServiceInfo& serviceInfo) {
    // Create service based on service type
    if (serviceInfo.serviceType == "mock" || 
        serviceInfo.serviceType == "reliable_mock" ||
        serviceInfo.serviceType == "unreliable_mock" ||
        serviceInfo.serviceType == "fast_mock" ||
        serviceInfo.serviceType == "slow_mock") {
        
        return external_services::MockServiceFactory::createMockService(
            serviceInfo.serviceName, serviceInfo.serviceType);
    }
    
    // Add support for other service types here
    // Example:
    // if (serviceInfo.serviceType == "google_cloud") {
    //     return std::make_shared<GoogleCloudSTTService>();
    // }
    // if (serviceInfo.serviceType == "azure") {
    //     return std::make_shared<AzureSTTService>();
    // }
    // if (serviceInfo.serviceType == "aws") {
    //     return std::make_shared<AWSTranscribeService>();
    // }
    
    LOG_WARNING("Unsupported service type: " + serviceInfo.serviceType + " for service: " + serviceInfo.serviceName);
    return nullptr;
}

} // namespace advanced
} // namespace stt