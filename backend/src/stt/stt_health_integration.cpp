#include "stt/stt_health_integration.hpp"
#include "utils/logging.hpp"
#include <algorithm>

namespace stt {

STTHealthIntegration::STTHealthIntegration()
    : health_checker_(std::make_shared<STTHealthChecker>()) {
}

STTHealthIntegration::~STTHealthIntegration() {
    stop();
}

bool STTHealthIntegration::initialize(const HealthCheckConfig& config) {
    if (initialized_.load()) {
        LOG_WARN("STT Health Integration already initialized");
        return true;
    }
    
    if (!health_checker_->initialize(config)) {
        LOG_ERROR("Failed to initialize STT health checker");
        return false;
    }
    
    // Set up health change callback
    health_checker_->setHealthChangeCallback([this](const SystemHealthStatus& status) {
        onHealthChange(status);
    });
    
    // Set up alert callback
    health_checker_->setAlertCallback([this](const HealthAlert& alert) {
        onAlert(alert);
    });
    
    initialized_.store(true);
    utils::Logger::info("STT Health Integration initialized successfully");
    return true;
}

bool STTHealthIntegration::start(bool enableBackgroundMonitoring) {
    if (!initialized_.load()) {
        utils::Logger::error("STT Health Integration not initialized. Call initialize() first.");
        return false;
    }
    
    if (started_.load()) {
        utils::Logger::warn("STT Health Integration already started");
        return true;
    }
    
    if (!health_checker_->startMonitoring(enableBackgroundMonitoring)) {
        utils::Logger::error("Failed to start health monitoring");
        return false;
    }
    
    started_.store(true);
    utils::Logger::info("STT Health Integration started with background monitoring: " + 
                        std::string(enableBackgroundMonitoring ? "enabled" : "disabled"));
    return true;
}

void STTHealthIntegration::stop() {
    if (!started_.load()) {
        return;
    }
    
    health_checker_->stopMonitoring();
    started_.store(false);
    utils::Logger::info("STT Health Integration stopped");
}

void STTHealthIntegration::integrateWithWebSocketServer(std::shared_ptr<core::WebSocketServer> server) {
    websocket_server_ = server;
    
    if (server) {
        server->setHealthChecker(health_checker_);
        utils::Logger::info("Health monitoring integrated with WebSocket server");
    }
}

void STTHealthIntegration::registerSTTInstance(const std::string& instanceId, 
                                             std::shared_ptr<STTInterface> sttInstance,
                                             bool autoLoadBalance) {
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        stt_instances_[instanceId] = sttInstance;
        
        if (autoLoadBalance) {
            load_balanced_instances_.insert(instanceId);
        }
    }
    
    // Register with health checker
    health_checker_->registerSTTInstance(instanceId, sttInstance);
    
    utils::Logger::info("STT instance '" + instanceId + "' registered for health monitoring (load balanced: " + 
                        std::string(autoLoadBalance ? "true" : "false") + ")");
}

void STTHealthIntegration::unregisterSTTInstance(const std::string& instanceId) {
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        stt_instances_.erase(instanceId);
        load_balanced_instances_.erase(instanceId);
    }
    
    // Unregister from health checker
    health_checker_->unregisterSTTInstance(instanceId);
    
    utils::Logger::info("STT instance '" + instanceId + "' unregistered from health monitoring");
}

std::string STTHealthIntegration::getRecommendedSTTInstance() {
    if (load_balancing_callback_) {
        try {
            return load_balancing_callback_();
        } catch (const std::exception& e) {
            utils::Logger::error("Exception in custom load balancing callback: " + std::string(e.what()));
            // Fall back to default load balancing
        }
    }
    
    return defaultLoadBalancing();
}

std::shared_ptr<STTInterface> STTHealthIntegration::getSTTInstance(const std::string& instanceId) {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    
    auto it = stt_instances_.find(instanceId);
    if (it != stt_instances_.end()) {
        return it->second;
    }
    
    return nullptr;
}

bool STTHealthIntegration::canAcceptNewRequests() {
    if (!started_.load()) {
        return false;
    }
    
    return health_checker_->canAcceptNewRequests();
}

SystemHealthStatus STTHealthIntegration::getSystemHealth(bool detailed) {
    if (!started_.load()) {
        SystemHealthStatus status;
        status.overall_status = HealthStatus::UNKNOWN;
        status.overall_message = "Health monitoring not started";
        return status;
    }
    
    return health_checker_->checkHealth(detailed);
}

void STTHealthIntegration::setLoadBalancingCallback(LoadBalancingCallback callback) {
    load_balancing_callback_ = callback;
    utils::Logger::info("Custom load balancing callback set");
}

void STTHealthIntegration::setAlertNotificationCallback(AlertNotificationCallback callback) {
    alert_notification_callback_ = callback;
    utils::Logger::info("Alert notification callback set");
}

void STTHealthIntegration::forceHealthCheck() {
    if (started_.load()) {
        health_checker_->forceHealthCheck();
    }
}

void STTHealthIntegration::updateConfiguration(const HealthCheckConfig& config) {
    health_checker_->updateConfig(config);
    utils::Logger::info("Health check configuration updated");
}

std::map<std::string, uint64_t> STTHealthIntegration::getMonitoringStatistics() {
    auto stats = health_checker_->getMonitoringStats();
    
    // Add integration-specific statistics
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        stats["total_registered_instances"] = stt_instances_.size();
        stats["load_balanced_instances"] = load_balanced_instances_.size();
    }
    
    stats["integration_initialized"] = initialized_.load() ? 1 : 0;
    stats["integration_started"] = started_.load() ? 1 : 0;
    
    return stats;
}

std::string STTHealthIntegration::exportHealthStatusJSON(bool includeHistory) {
    if (!started_.load()) {
        return "{\"status\":\"not_started\",\"message\":\"Health monitoring not started\"}";
    }
    
    return health_checker_->exportHealthStatusJSON(includeHistory);
}

void STTHealthIntegration::setEnabled(bool enabled) {
    health_checker_->setEnabled(enabled);
    utils::Logger::info("Health monitoring " + std::string(enabled ? "enabled" : "disabled"));
}

bool STTHealthIntegration::isEnabled() const {
    return health_checker_->isEnabled();
}

// Private methods

void STTHealthIntegration::onHealthChange(const SystemHealthStatus& status) {
    std::string statusStr = (status.overall_status == HealthStatus::HEALTHY ? "HEALTHY" :
                            status.overall_status == HealthStatus::DEGRADED ? "DEGRADED" :
                            status.overall_status == HealthStatus::UNHEALTHY ? "UNHEALTHY" :
                            status.overall_status == HealthStatus::CRITICAL ? "CRITICAL" : "UNKNOWN");
    utils::Logger::info("System health changed to: " + statusStr + " - " + status.overall_message);
    
    // Additional health change handling can be added here
    // For example, notifying external monitoring systems
}

void STTHealthIntegration::onAlert(const HealthAlert& alert) {
    std::string severityStr = (alert.severity == HealthStatus::CRITICAL ? "CRITICAL" :
                              alert.severity == HealthStatus::UNHEALTHY ? "UNHEALTHY" :
                              alert.severity == HealthStatus::DEGRADED ? "DEGRADED" : "UNKNOWN");
    utils::Logger::warn("Health alert generated: " + alert.component_name + " - " + severityStr + " - " + alert.message);
    
    // Call custom alert notification callback if set
    if (alert_notification_callback_) {
        try {
            alert_notification_callback_(alert);
        } catch (const std::exception& e) {
            utils::Logger::error("Exception in alert notification callback: " + std::string(e.what()));
        }
    }
    
    // Additional alert handling can be added here
    // For example, sending notifications to external systems
}

std::string STTHealthIntegration::defaultLoadBalancing() {
    if (!started_.load()) {
        return "";
    }
    
    // Use health checker's recommendation, but filter by load-balanced instances
    std::string recommended = health_checker_->getRecommendedInstance();
    
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        
        // Check if recommended instance is in our load-balanced set
        if (!recommended.empty() && load_balanced_instances_.count(recommended) > 0) {
            return recommended;
        }
        
        // If not, find the first healthy load-balanced instance
        auto healthyInstances = health_checker_->getHealthyInstances();
        for (const auto& instanceId : healthyInstances) {
            if (load_balanced_instances_.count(instanceId) > 0) {
                return instanceId;
            }
        }
    }
    
    return ""; // No healthy load-balanced instances available
}

// STTHealthManager implementation

STTHealthManager& STTHealthManager::getInstance() {
    static STTHealthManager instance;
    return instance;
}

bool STTHealthManager::initialize(const HealthCheckConfig& config) {
    std::call_once(initialized_flag_, [this, &config]() {
        health_integration_ = std::make_shared<STTHealthIntegration>();
        health_integration_->initialize(config);
        health_integration_->start(true); // Enable background monitoring by default
    });
    
    return health_integration_ != nullptr;
}

void STTHealthManager::registerSTTInstance(const std::string& instanceId, std::shared_ptr<STTInterface> sttInstance) {
    if (!health_integration_) {
        utils::Logger::error("STTHealthManager not initialized. Call initialize() first.");
        return;
    }
    
    health_integration_->registerSTTInstance(instanceId, sttInstance, true);
}

std::string STTHealthManager::getRecommendedInstance() {
    if (!health_integration_) {
        utils::Logger::error("STTHealthManager not initialized. Call initialize() first.");
        return "";
    }
    
    return health_integration_->getRecommendedSTTInstance();
}

bool STTHealthManager::canAcceptRequests() {
    if (!health_integration_) {
        return false;
    }
    
    return health_integration_->canAcceptNewRequests();
}

} // namespace stt