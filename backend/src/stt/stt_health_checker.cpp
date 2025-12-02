#include "stt/stt_health_checker.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <sstream>
#include <random>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace stt {

STTHealthChecker::STTHealthChecker()
    : performance_monitor_(speechrnt::utils::PerformanceMonitor::getInstance())
    , performance_tracker_(std::make_unique<STTPerformanceTracker>()) {
    
    // Initialize performance tracker
    performance_tracker_->initialize(true);
}

STTHealthChecker::~STTHealthChecker() {
    stopMonitoring();
}

bool STTHealthChecker::initialize(const HealthCheckConfig& config) {
    std::lock_guard<std::mutex> lock(health_mutex_);
    
    config_ = config;
    current_health_.timestamp = std::chrono::steady_clock::now();
    last_health_check_ = std::chrono::steady_clock::now();
    
    // Initialize health history with current status
    health_history_.clear();
    health_history_.push_back(current_health_);
    
    speechrnt::utils::Logger::info("STTHealthChecker initialized with check interval: " + std::to_string(config_.health_check_interval_ms) + "ms");
    return true;
}

bool STTHealthChecker::startMonitoring(bool enableBackgroundMonitoring) {
    if (monitoring_active_.load()) {
        speechrnt::utils::Logger::warn("Health monitoring is already active");
        return true;
    }
    
    should_stop_monitoring_.store(false);
    monitoring_active_.store(true);
    
    if (enableBackgroundMonitoring) {
        // Start health monitoring thread
        health_monitor_thread_ = std::make_unique<std::thread>([this]() {
            healthMonitorLoop();
        });
        
        // Start resource monitoring thread
        resource_monitor_thread_ = std::make_unique<std::thread>([this]() {
            resourceMonitorLoop();
        });
        
        speechrnt::utils::Logger::info("Background health monitoring started");
    }
    
    return true;
}

void STTHealthChecker::stopMonitoring() {
    if (!monitoring_active_.load()) {
        return;
    }
    
    should_stop_monitoring_.store(true);
    monitoring_active_.store(false);
    
    // Wait for threads to finish
    if (health_monitor_thread_ && health_monitor_thread_->joinable()) {
        health_monitor_thread_->join();
    }
    
    if (resource_monitor_thread_ && resource_monitor_thread_->joinable()) {
        resource_monitor_thread_->join();
    }
    
    health_monitor_thread_.reset();
    resource_monitor_thread_.reset();
    
    speechrnt::utils::Logger::info("Health monitoring stopped");
}

SystemHealthStatus STTHealthChecker::checkHealth(bool detailed) {
    if (!enabled_.load()) {
        SystemHealthStatus status;
        status.overall_status = HealthStatus::UNKNOWN;
        status.overall_message = "Health monitoring is disabled";
        return status;
    }
    
    HEALTH_CHECK_TIMER("full_health_check");
    
    SystemHealthStatus status;
    status.timestamp = std::chrono::steady_clock::now();
    
    std::vector<ComponentHealth> components;
    
    // Check all registered STT instances
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        for (const auto& [instanceId, sttInstance] : registered_instances_) {
            ComponentHealth instanceHealth = checkSTTHealth(sttInstance.get());
            instanceHealth.component_name = "STT_Instance_" + instanceId;
            components.push_back(instanceHealth);
            instance_health_[instanceId] = instanceHealth;
        }
    }
    
    // Check system resources
    components.push_back(checkResourceHealth());
    
    // Check performance metrics
    components.push_back(checkPerformanceHealth());
    
    // Check model health
    components.push_back(checkModelHealth());
    
    if (detailed) {
        // Additional detailed checks
        components.push_back(checkCPUHealth());
        components.push_back(checkMemoryHealth());
        components.push_back(checkGPUHealth());
        components.push_back(checkBufferHealth());
        components.push_back(checkLatencyHealth());
        components.push_back(checkThroughputHealth());
        components.push_back(checkErrorRateHealth());
    }
    
    // Determine overall status
    status.overall_status = determineOverallStatus(components);
    status.component_health = components;
    
    // Generate overall message
    int healthy = 0, degraded = 0, unhealthy = 0, critical = 0;
    for (const auto& comp : components) {
        switch (comp.status) {
            case HealthStatus::HEALTHY: healthy++; break;
            case HealthStatus::DEGRADED: degraded++; break;
            case HealthStatus::UNHEALTHY: unhealthy++; break;
            case HealthStatus::CRITICAL: critical++; break;
            default: break;
        }
    }
    
    std::ostringstream oss;
    oss << "System Status: " << healthStatusToString(status.overall_status);
    oss << " (Healthy: " << healthy << ", Degraded: " << degraded;
    oss << ", Unhealthy: " << unhealthy << ", Critical: " << critical << ")";
    status.overall_message = oss.str();
    
    // Update resource usage
    auto perfSummary = performance_monitor_.getSystemSummary();
    status.resource_usage.cpu_usage_percent = perfSummary.count("cpu_usage") ? perfSummary["cpu_usage"] : 0.0;
    status.resource_usage.memory_usage_mb = perfSummary.count("memory_usage") ? perfSummary["memory_usage"] : 0.0;
    status.resource_usage.gpu_memory_usage_mb = perfSummary.count("gpu_memory_usage") ? perfSummary["gpu_memory_usage"] : 0.0;
    status.resource_usage.gpu_utilization_percent = perfSummary.count("gpu_utilization") ? perfSummary["gpu_utilization"] : 0.0;
    status.resource_usage.active_transcriptions = static_cast<int>(perfSummary.count("active_sessions") ? perfSummary["active_sessions"] : 0);
    
    // Calculate total check time
    status.total_check_time_ms = calculateElapsedMs(status.timestamp);
    
    // Update current health and history
    {
        std::lock_guard<std::mutex> lock(health_mutex_);
        current_health_ = status;
        updateHealthHistory(status);
        last_health_check_ = std::chrono::steady_clock::now();
    }
    
    // Check for health changes and generate alerts
    checkForHealthChanges(status);
    
    total_health_checks_.fetch_add(1);
    
    return status;
}

ComponentHealth STTHealthChecker::checkSTTHealth(const STTInterface* sttInstance) {
    if (!sttInstance) {
        return ComponentHealth("STT_Instance", HealthStatus::CRITICAL, "STT instance is null");
    }
    
    ComponentHealth health("STT_Instance", HealthStatus::HEALTHY, "STT instance is operational");
    
    auto start = std::chrono::steady_clock::now();
    
    try {
        // Check if STT is initialized
        if (!sttInstance->isInitialized()) {
            health.status = HealthStatus::CRITICAL;
            health.status_message = "STT instance is not initialized";
            health.details["error"] = sttInstance->getLastError();
            return health;
        }
        
        // Check for recent errors
        std::string lastError = sttInstance->getLastError();
        if (!lastError.empty()) {
            health.status = HealthStatus::DEGRADED;
            health.status_message = "STT instance has recent errors";
            health.details["last_error"] = lastError;
        }
        
        health.response_time_ms = calculateElapsedMs(start);
        health.details["initialized"] = "true";
        health.details["response_time_ms"] = std::to_string(health.response_time_ms);
        
        // Check response time threshold
        if (health.response_time_ms > config_.max_response_time_ms) {
            health.status = HealthStatus::DEGRADED;
            health.status_message = "STT instance response time is high";
        }
        
    } catch (const std::exception& e) {
        health.status = HealthStatus::CRITICAL;
        health.status_message = "Exception during STT health check";
        health.details["exception"] = e.what();
    }
    
    return health;
}

ComponentHealth STTHealthChecker::checkWhisperSTTHealth(const WhisperSTT* whisperSTT) {
    if (!whisperSTT) {
        return ComponentHealth("WhisperSTT", HealthStatus::CRITICAL, "WhisperSTT instance is null");
    }
    
    ComponentHealth health("WhisperSTT", HealthStatus::HEALTHY, "WhisperSTT is operational");
    
    auto start = std::chrono::steady_clock::now();
    
    try {
        // Check basic STT health first
        health = checkSTTHealth(whisperSTT);
        health.component_name = "WhisperSTT";
        
        if (health.status == HealthStatus::CRITICAL) {
            return health;
        }
        
        // WhisperSTT-specific checks
        health.details["streaming_active_count"] = std::to_string(whisperSTT->getActiveStreamingCount());
        health.details["language_detection_enabled"] = whisperSTT->isLanguageDetectionEnabled() ? "true" : "false";
        health.details["auto_language_switching"] = whisperSTT->isAutoLanguageSwitchingEnabled() ? "true" : "false";
        health.details["current_language"] = whisperSTT->getCurrentDetectedLanguage();
        health.details["quantization_level"] = std::to_string(static_cast<int>(whisperSTT->getQuantizationLevel()));
        health.details["confidence_threshold"] = std::to_string(whisperSTT->getConfidenceThreshold());
        
        // Check streaming load
        size_t activeStreaming = whisperSTT->getActiveStreamingCount();
        if (activeStreaming > static_cast<size_t>(config_.max_concurrent_transcriptions)) {
            health.status = HealthStatus::DEGRADED;
            health.status_message = "High number of active streaming transcriptions";
        }
        
        health.response_time_ms = calculateElapsedMs(start);
        
    } catch (const std::exception& e) {
        health.status = HealthStatus::CRITICAL;
        health.status_message = "Exception during WhisperSTT health check";
        health.details["exception"] = e.what();
    }
    
    return health;
}

ComponentHealth STTHealthChecker::checkResourceHealth() {
    ComponentHealth health("System_Resources", HealthStatus::HEALTHY, "System resources are within normal limits");
    
    auto perfSummary = performance_monitor_.getSystemSummary();
    
    // Check CPU usage
    double cpuUsage = perfSummary.count("cpu_usage") ? perfSummary["cpu_usage"] : 0.0;
    health.details["cpu_usage_percent"] = std::to_string(cpuUsage);
    
    if (cpuUsage > config_.max_cpu_usage_percent) {
        health.status = HealthStatus::DEGRADED;
        health.status_message = "High CPU usage detected";
    }
    
    // Check memory usage
    double memoryUsage = perfSummary.count("memory_usage") ? perfSummary["memory_usage"] : 0.0;
    health.details["memory_usage_mb"] = std::to_string(memoryUsage);
    
    if (memoryUsage > config_.max_memory_usage_mb) {
        health.status = HealthStatus::DEGRADED;
        health.status_message = "High memory usage detected";
    }
    
    // Check GPU resources if available
    double gpuMemory = perfSummary.count("gpu_memory_usage") ? perfSummary["gpu_memory_usage"] : 0.0;
    double gpuUtil = perfSummary.count("gpu_utilization") ? perfSummary["gpu_utilization"] : 0.0;
    
    health.details["gpu_memory_usage_mb"] = std::to_string(gpuMemory);
    health.details["gpu_utilization_percent"] = std::to_string(gpuUtil);
    
    if (gpuMemory > config_.max_gpu_memory_usage_mb) {
        health.status = HealthStatus::DEGRADED;
        health.status_message = "High GPU memory usage detected";
    }
    
    return health;
}

ComponentHealth STTHealthChecker::checkPerformanceHealth() {
    ComponentHealth health("Performance_Metrics", HealthStatus::HEALTHY, "Performance metrics are within acceptable ranges");
    
    auto sttMetrics = performance_monitor_.getSTTMetrics(10); // Last 10 minutes
    
    // Check STT latency
    if (sttMetrics.count("stt_latency")) {
        auto latencyStats = sttMetrics["stt_latency"];
        health.details["avg_latency_ms"] = std::to_string(latencyStats.mean);
        health.details["p95_latency_ms"] = std::to_string(latencyStats.p95);
        
        if (latencyStats.mean > config_.max_latency_ms) {
            health.status = HealthStatus::DEGRADED;
            health.status_message = "High average STT latency detected";
        }
    }
    
    // Check confidence scores
    if (sttMetrics.count("stt_confidence")) {
        auto confidenceStats = sttMetrics["stt_confidence"];
        health.details["avg_confidence"] = std::to_string(confidenceStats.mean);
        health.details["min_confidence"] = std::to_string(confidenceStats.min);
        
        if (confidenceStats.mean < config_.min_confidence_threshold) {
            health.status = HealthStatus::DEGRADED;
            health.status_message = "Low average confidence scores detected";
        }
    }
    
    // Check throughput
    if (sttMetrics.count("stt_throughput")) {
        auto throughputStats = sttMetrics["stt_throughput"];
        health.details["avg_throughput"] = std::to_string(throughputStats.mean);
    }
    
    return health;
}

ComponentHealth STTHealthChecker::checkModelHealth() {
    ComponentHealth health("Model_Status", HealthStatus::HEALTHY, "Models are loaded and operational");
    
    // Check if any STT instances are initialized
    bool anyInitialized = false;
    int totalInstances = 0;
    int initializedInstances = 0;
    
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        totalInstances = static_cast<int>(registered_instances_.size());
        
        for (const auto& [instanceId, sttInstance] : registered_instances_) {
            if (sttInstance && sttInstance->isInitialized()) {
                anyInitialized = true;
                initializedInstances++;
            }
        }
    }
    
    health.details["total_instances"] = std::to_string(totalInstances);
    health.details["initialized_instances"] = std::to_string(initializedInstances);
    
    if (totalInstances == 0) {
        health.status = HealthStatus::CRITICAL;
        health.status_message = "No STT instances registered";
    } else if (!anyInitialized) {
        health.status = HealthStatus::CRITICAL;
        health.status_message = "No STT instances are initialized";
    } else if (initializedInstances < totalInstances) {
        health.status = HealthStatus::DEGRADED;
        health.status_message = "Some STT instances are not initialized";
    }
    
    return health;
}

void STTHealthChecker::registerSTTInstance(const std::string& instanceId, std::shared_ptr<STTInterface> sttInstance) {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    registered_instances_[instanceId] = sttInstance;
    
    // Initialize health status for this instance
    instance_health_[instanceId] = ComponentHealth("STT_Instance_" + instanceId, HealthStatus::UNKNOWN, "Instance registered, health check pending");
    
    speechrnt::utils::Logger::info("STT instance '" + instanceId + "' registered for health monitoring");
}

void STTHealthChecker::unregisterSTTInstance(const std::string& instanceId) {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    registered_instances_.erase(instanceId);
    instance_health_.erase(instanceId);
    
    speechrnt::utils::Logger::info("STT instance '" + instanceId + "' unregistered from health monitoring");
}

std::shared_ptr<ComponentHealth> STTHealthChecker::getInstanceHealth(const std::string& instanceId) {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    
    auto it = instance_health_.find(instanceId);
    if (it != instance_health_.end()) {
        return std::make_shared<ComponentHealth>(it->second);
    }
    
    return nullptr;
}

std::vector<std::string> STTHealthChecker::getHealthyInstances() {
    std::vector<std::string> healthyInstances;
    
    std::lock_guard<std::mutex> lock(instances_mutex_);
    for (const auto& [instanceId, health] : instance_health_) {
        if (isInstanceHealthy(health)) {
            healthyInstances.push_back(instanceId);
        }
    }
    
    return healthyInstances;
}

std::string STTHealthChecker::getRecommendedInstance() {
    if (!config_.enable_load_balancing) {
        // Return first healthy instance
        auto healthyInstances = getHealthyInstances();
        return healthyInstances.empty() ? "" : healthyInstances[0];
    }
    
    return selectLeastLoadedInstance();
}

void STTHealthChecker::setHealthChangeCallback(HealthChangeCallback callback) {
    health_change_callback_ = callback;
}

void STTHealthChecker::setAlertCallback(AlertCallback callback) {
    alert_callback_ = callback;
}

void STTHealthChecker::updateConfig(const HealthCheckConfig& config) {
    std::lock_guard<std::mutex> lock(health_mutex_);
    config_ = config;
    speechrnt::utils::Logger::info("Health check configuration updated");
}

std::vector<SystemHealthStatus> STTHealthChecker::getHealthHistory(int hours) {
    std::lock_guard<std::mutex> lock(health_mutex_);
    
    auto cutoffTime = std::chrono::steady_clock::now() - std::chrono::hours(hours);
    std::vector<SystemHealthStatus> filteredHistory;
    
    for (const auto& status : health_history_) {
        if (status.timestamp >= cutoffTime) {
            filteredHistory.push_back(status);
        }
    }
    
    return filteredHistory;
}

std::vector<HealthAlert> STTHealthChecker::getActiveAlerts() {
    std::lock_guard<std::mutex> lock(alerts_mutex_);
    
    std::vector<HealthAlert> activeAlerts;
    for (const auto& alert : active_alerts_) {
        if (!alert.acknowledged) {
            activeAlerts.push_back(alert);
        }
    }
    
    return activeAlerts;
}

bool STTHealthChecker::acknowledgeAlert(const std::string& alertId) {
    std::lock_guard<std::mutex> lock(alerts_mutex_);
    
    for (auto& alert : active_alerts_) {
        if (alert.alert_id == alertId) {
            alert.acknowledged = true;
            speechrnt::utils::Logger::info("Alert '" + alertId + "' acknowledged");
            return true;
        }
    }
    
    return false;
}

void STTHealthChecker::clearAcknowledgedAlerts() {
    std::lock_guard<std::mutex> lock(alerts_mutex_);
    
    active_alerts_.erase(
        std::remove_if(active_alerts_.begin(), active_alerts_.end(),
                      [](const HealthAlert& alert) { return alert.acknowledged; }),
        active_alerts_.end()
    );
}

std::map<std::string, double> STTHealthChecker::getHealthMetrics() {
    std::map<std::string, double> metrics;
    
    {
        std::lock_guard<std::mutex> lock(health_mutex_);
        metrics["overall_health_score"] = static_cast<double>(current_health_.overall_status);
        metrics["total_components"] = static_cast<double>(current_health_.component_health.size());
        metrics["healthy_components"] = 0.0;
        metrics["degraded_components"] = 0.0;
        metrics["unhealthy_components"] = 0.0;
        metrics["critical_components"] = 0.0;
        
        for (const auto& comp : current_health_.component_health) {
            switch (comp.status) {
                case HealthStatus::HEALTHY: metrics["healthy_components"] += 1.0; break;
                case HealthStatus::DEGRADED: metrics["degraded_components"] += 1.0; break;
                case HealthStatus::UNHEALTHY: metrics["unhealthy_components"] += 1.0; break;
                case HealthStatus::CRITICAL: metrics["critical_components"] += 1.0; break;
                default: break;
            }
        }
        
        metrics["last_check_time_ms"] = current_health_.total_check_time_ms;
    }
    
    {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        metrics["active_alerts"] = static_cast<double>(active_alerts_.size());
        metrics["unacknowledged_alerts"] = 0.0;
        
        for (const auto& alert : active_alerts_) {
            if (!alert.acknowledged) {
                metrics["unacknowledged_alerts"] += 1.0;
            }
        }
    }
    
    metrics["total_health_checks"] = static_cast<double>(total_health_checks_.load());
    metrics["total_alerts_generated"] = static_cast<double>(total_alerts_generated_.load());
    metrics["system_load_factor"] = getSystemLoadFactor();
    
    return metrics;
}

std::string STTHealthChecker::exportHealthStatusJSON(bool includeHistory) {
    std::lock_guard<std::mutex> lock(health_mutex_);
    return formatHealthStatusJSON(current_health_);
}

bool STTHealthChecker::canAcceptNewRequests() {
    std::lock_guard<std::mutex> lock(health_mutex_);
    
    // Check overall system health
    if (current_health_.overall_status == HealthStatus::CRITICAL) {
        return false;
    }
    
    // Check if we have healthy instances
    auto healthyInstances = getHealthyInstances();
    if (healthyInstances.size() < static_cast<size_t>(config_.min_healthy_instances)) {
        return false;
    }
    
    // Check system load
    double loadFactor = getSystemLoadFactor();
    if (loadFactor > config_.load_balancing_threshold) {
        return false;
    }
    
    return true;
}

double STTHealthChecker::getSystemLoadFactor() {
    auto perfSummary = performance_monitor_.getSystemSummary();
    
    double cpuLoad = (perfSummary.count("cpu_usage") ? perfSummary["cpu_usage"] : 0.0) / 100.0;
    double memoryLoad = (perfSummary.count("memory_usage") ? perfSummary["memory_usage"] : 0.0) / config_.max_memory_usage_mb;
    double gpuLoad = (perfSummary.count("gpu_utilization") ? perfSummary["gpu_utilization"] : 0.0) / 100.0;
    
    // Calculate weighted average
    return (cpuLoad * 0.4 + memoryLoad * 0.3 + gpuLoad * 0.3);
}

void STTHealthChecker::forceHealthCheck() {
    if (enabled_.load()) {
        checkHealth(true);
    }
}

void STTHealthChecker::setEnabled(bool enabled) {
    enabled_.store(enabled);
    LOG_INFO("Health monitoring {}", enabled ? "enabled" : "disabled");
}

std::map<std::string, uint64_t> STTHealthChecker::getMonitoringStats() {
    std::map<std::string, uint64_t> stats;
    
    stats["total_health_checks"] = total_health_checks_.load();
    stats["total_alerts_generated"] = total_alerts_generated_.load();
    stats["total_health_changes"] = total_health_changes_.load();
    
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        stats["registered_instances"] = registered_instances_.size();
    }
    
    {
        std::lock_guard<std::mutex> lock(health_mutex_);
        stats["health_history_entries"] = health_history_.size();
    }
    
    {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        stats["active_alerts"] = active_alerts_.size();
    }
    
    return stats;
}

// Private methods implementation

void STTHealthChecker::healthMonitorLoop() {
    speechrnt::utils::Logger::info("Health monitor loop started");
    
    while (!should_stop_monitoring_.load()) {
        try {
            performHealthCheck(false);
            
            // Sleep for the configured interval
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.health_check_interval_ms));
            
            // Perform detailed check periodically
            static int detailedCheckCounter = 0;
            detailedCheckCounter++;
            
            if (detailedCheckCounter * config_.health_check_interval_ms >= config_.detailed_check_interval_ms) {
                performHealthCheck(true);
                detailedCheckCounter = 0;
            }
            
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Exception in health monitor loop: " + std::string(e.what()));
        }
    }
    
    speechrnt::utils::Logger::info("Health monitor loop stopped");
}

void STTHealthChecker::resourceMonitorLoop() {
    speechrnt::utils::Logger::info("Resource monitor loop started");
    
    while (!should_stop_monitoring_.load()) {
        try {
            // Update resource metrics
            checkResourceHealth();
            
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.resource_check_interval_ms));
            
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Exception in resource monitor loop: " + std::string(e.what()));
        }
    }
    
    speechrnt::utils::Logger::info("Resource monitor loop stopped");
}

void STTHealthChecker::performHealthCheck(bool detailed) {
    checkHealth(detailed);
}

void STTHealthChecker::updateHealthHistory(const SystemHealthStatus& status) {
    health_history_.push_back(status);
    
    // Keep only last 24 hours of history
    auto cutoffTime = std::chrono::steady_clock::now() - std::chrono::hours(24);
    health_history_.erase(
        std::remove_if(health_history_.begin(), health_history_.end(),
                      [cutoffTime](const SystemHealthStatus& s) { return s.timestamp < cutoffTime; }),
        health_history_.end()
    );
}

void STTHealthChecker::checkForHealthChanges(const SystemHealthStatus& newStatus) {
    bool healthChanged = false;
    
    {
        std::lock_guard<std::mutex> lock(health_mutex_);
        if (current_health_.overall_status != newStatus.overall_status) {
            healthChanged = true;
        }
    }
    
    if (healthChanged) {
        total_health_changes_.fetch_add(1);
        
        if (health_change_callback_) {
            health_change_callback_(newStatus);
        }
        
        // Generate alert for significant health changes
        if (newStatus.overall_status == HealthStatus::CRITICAL || 
            newStatus.overall_status == HealthStatus::UNHEALTHY) {
            generateAlert("System", newStatus.overall_status, newStatus.overall_message);
        }
    }
}

void STTHealthChecker::generateAlert(const std::string& component, HealthStatus severity, 
                                   const std::string& message, const std::map<std::string, std::string>& context) {
    if (!config_.enable_alerting) {
        return;
    }
    
    std::string alertKey = component + "_" + std::to_string(static_cast<int>(severity));
    
    if (isAlertCooldownActive(alertKey)) {
        return;
    }
    
    HealthAlert alert;
    alert.alert_id = generateAlertId();
    alert.component_name = component;
    alert.severity = severity;
    alert.message = message;
    alert.context = context;
    alert.timestamp = std::chrono::steady_clock::now();
    alert.acknowledged = false;
    
    {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        active_alerts_.push_back(alert);
    }
    
    updateAlertCooldown(alertKey);
    total_alerts_generated_.fetch_add(1);
    
    if (alert_callback_) {
        alert_callback_(alert);
    }
    
    speechrnt::utils::Logger::warn("Health alert generated: " + component + " - " + healthStatusToString(severity) + " - " + message);
}

bool STTHealthChecker::isAlertCooldownActive(const std::string& alertKey) {
    auto it = alert_cooldowns_.find(alertKey);
    if (it == alert_cooldowns_.end()) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
    
    return elapsed < config_.alert_cooldown_ms;
}

void STTHealthChecker::updateAlertCooldown(const std::string& alertKey) {
    alert_cooldowns_[alertKey] = std::chrono::steady_clock::now();
}

// Component-specific health check implementations

ComponentHealth STTHealthChecker::checkCPUHealth() {
    ComponentHealth health("CPU", HealthStatus::HEALTHY, "CPU usage is normal");
    
    auto perfSummary = performance_monitor_.getSystemSummary();
    double cpuUsage = perfSummary.count("cpu_usage") ? perfSummary["cpu_usage"] : 0.0;
    
    health.details["usage_percent"] = std::to_string(cpuUsage);
    
    if (cpuUsage > config_.max_cpu_usage_percent * 0.9) {
        health.status = HealthStatus::CRITICAL;
        health.status_message = "Critical CPU usage";
    } else if (cpuUsage > config_.max_cpu_usage_percent * 0.8) {
        health.status = HealthStatus::UNHEALTHY;
        health.status_message = "High CPU usage";
    } else if (cpuUsage > config_.max_cpu_usage_percent * 0.6) {
        health.status = HealthStatus::DEGRADED;
        health.status_message = "Elevated CPU usage";
    }
    
    return health;
}

ComponentHealth STTHealthChecker::checkMemoryHealth() {
    ComponentHealth health("Memory", HealthStatus::HEALTHY, "Memory usage is normal");
    
    auto perfSummary = performance_monitor_.getSystemSummary();
    double memoryUsage = perfSummary.count("memory_usage") ? perfSummary["memory_usage"] : 0.0;
    
    health.details["usage_mb"] = std::to_string(memoryUsage);
    health.details["max_mb"] = std::to_string(config_.max_memory_usage_mb);
    
    double usagePercent = (memoryUsage / config_.max_memory_usage_mb) * 100.0;
    health.details["usage_percent"] = std::to_string(usagePercent);
    
    if (usagePercent > 90.0) {
        health.status = HealthStatus::CRITICAL;
        health.status_message = "Critical memory usage";
    } else if (usagePercent > 80.0) {
        health.status = HealthStatus::UNHEALTHY;
        health.status_message = "High memory usage";
    } else if (usagePercent > 60.0) {
        health.status = HealthStatus::DEGRADED;
        health.status_message = "Elevated memory usage";
    }
    
    return health;
}

ComponentHealth STTHealthChecker::checkGPUHealth() {
    ComponentHealth health("GPU", HealthStatus::HEALTHY, "GPU resources are normal");
    
    auto perfSummary = performance_monitor_.getSystemSummary();
    double gpuMemory = perfSummary.count("gpu_memory_usage") ? perfSummary["gpu_memory_usage"] : 0.0;
    double gpuUtil = perfSummary.count("gpu_utilization") ? perfSummary["gpu_utilization"] : 0.0;
    
    health.details["memory_usage_mb"] = std::to_string(gpuMemory);
    health.details["utilization_percent"] = std::to_string(gpuUtil);
    
    double memoryPercent = (gpuMemory / config_.max_gpu_memory_usage_mb) * 100.0;
    
    if (memoryPercent > 90.0 || gpuUtil > 95.0) {
        health.status = HealthStatus::CRITICAL;
        health.status_message = "Critical GPU resource usage";
    } else if (memoryPercent > 80.0 || gpuUtil > 85.0) {
        health.status = HealthStatus::UNHEALTHY;
        health.status_message = "High GPU resource usage";
    } else if (memoryPercent > 60.0 || gpuUtil > 70.0) {
        health.status = HealthStatus::DEGRADED;
        health.status_message = "Elevated GPU resource usage";
    }
    
    return health;
}

ComponentHealth STTHealthChecker::checkBufferHealth() {
    ComponentHealth health("Audio_Buffers", HealthStatus::HEALTHY, "Audio buffer usage is normal");
    
    auto sttMetrics = performance_monitor_.getSTTMetrics(5);
    
    if (sttMetrics.count("stt_buffer_usage")) {
        auto bufferStats = sttMetrics["stt_buffer_usage"];
        double bufferUsage = bufferStats.mean;
        
        health.details["usage_mb"] = std::to_string(bufferUsage);
        health.details["max_mb"] = std::to_string(config_.max_buffer_usage_mb);
        
        double usagePercent = (bufferUsage / config_.max_buffer_usage_mb) * 100.0;
        health.details["usage_percent"] = std::to_string(usagePercent);
        
        if (usagePercent > 90.0) {
            health.status = HealthStatus::CRITICAL;
            health.status_message = "Critical audio buffer usage";
        } else if (usagePercent > 80.0) {
            health.status = HealthStatus::UNHEALTHY;
            health.status_message = "High audio buffer usage";
        } else if (usagePercent > 60.0) {
            health.status = HealthStatus::DEGRADED;
            health.status_message = "Elevated audio buffer usage";
        }
    }
    
    return health;
}

ComponentHealth STTHealthChecker::checkLatencyHealth() {
    ComponentHealth health("Latency", HealthStatus::HEALTHY, "Latency is within acceptable limits");
    
    auto sttMetrics = performance_monitor_.getSTTMetrics(10);
    
    if (sttMetrics.count("stt_latency")) {
        auto latencyStats = sttMetrics["stt_latency"];
        
        health.details["avg_latency_ms"] = std::to_string(latencyStats.mean);
        health.details["p95_latency_ms"] = std::to_string(latencyStats.p95);
        health.details["p99_latency_ms"] = std::to_string(latencyStats.p99);
        health.details["max_latency_ms"] = std::to_string(latencyStats.max);
        
        if (latencyStats.p95 > config_.max_latency_ms * 1.5) {
            health.status = HealthStatus::CRITICAL;
            health.status_message = "Critical latency levels";
        } else if (latencyStats.mean > config_.max_latency_ms) {
            health.status = HealthStatus::UNHEALTHY;
            health.status_message = "High average latency";
        } else if (latencyStats.p95 > config_.max_latency_ms) {
            health.status = HealthStatus::DEGRADED;
            health.status_message = "High P95 latency";
        }
    }
    
    return health;
}

ComponentHealth STTHealthChecker::checkThroughputHealth() {
    ComponentHealth health("Throughput", HealthStatus::HEALTHY, "Throughput is normal");
    
    auto sttMetrics = performance_monitor_.getSTTMetrics(10);
    
    if (sttMetrics.count("stt_throughput")) {
        auto throughputStats = sttMetrics["stt_throughput"];
        
        health.details["avg_throughput"] = std::to_string(throughputStats.mean);
        health.details["min_throughput"] = std::to_string(throughputStats.min);
        health.details["max_throughput"] = std::to_string(throughputStats.max);
        
        // Throughput health is generally informational
        // Low throughput might indicate system issues but isn't necessarily unhealthy
        if (throughputStats.mean < 0.1) {
            health.status = HealthStatus::DEGRADED;
            health.status_message = "Low throughput detected";
        }
    }
    
    return health;
}

ComponentHealth STTHealthChecker::checkErrorRateHealth() {
    ComponentHealth health("Error_Rate", HealthStatus::HEALTHY, "Error rate is low");
    
    auto perfSummary = performance_monitor_.getSystemSummary();
    double errorCount = perfSummary.count("errors_count") ? perfSummary["errors_count"] : 0.0;
    
    health.details["error_count"] = std::to_string(errorCount);
    
    // Simple error rate check - could be enhanced with more sophisticated metrics
    if (errorCount > 100) {
        health.status = HealthStatus::CRITICAL;
        health.status_message = "High error rate";
    } else if (errorCount > 50) {
        health.status = HealthStatus::UNHEALTHY;
        health.status_message = "Elevated error rate";
    } else if (errorCount > 10) {
        health.status = HealthStatus::DEGRADED;
        health.status_message = "Some errors detected";
    }
    
    return health;
}

// Load balancing helper methods

double STTHealthChecker::calculateInstanceLoad(const std::string& instanceId) {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    
    auto healthIt = instance_health_.find(instanceId);
    if (healthIt == instance_health_.end()) {
        return 1.0; // Unknown instances get maximum load
    }
    
    const auto& health = healthIt->second;
    
    // Calculate load based on health status and response time
    double healthLoad = 0.0;
    switch (health.status) {
        case HealthStatus::HEALTHY: healthLoad = 0.0; break;
        case HealthStatus::DEGRADED: healthLoad = 0.3; break;
        case HealthStatus::UNHEALTHY: healthLoad = 0.7; break;
        case HealthStatus::CRITICAL: healthLoad = 1.0; break;
        default: healthLoad = 1.0; break;
    }
    
    // Factor in response time
    double responseTimeLoad = std::min(health.response_time_ms / config_.max_response_time_ms, 1.0);
    
    return (healthLoad * 0.7 + responseTimeLoad * 0.3);
}

std::string STTHealthChecker::selectLeastLoadedInstance() {
    std::string bestInstance;
    double lowestLoad = 1.0;
    
    std::lock_guard<std::mutex> lock(instances_mutex_);
    
    for (const auto& [instanceId, health] : instance_health_) {
        if (isInstanceHealthy(health)) {
            double load = calculateInstanceLoad(instanceId);
            if (load < lowestLoad) {
                lowestLoad = load;
                bestInstance = instanceId;
            }
        }
    }
    
    return bestInstance;
}

bool STTHealthChecker::isInstanceHealthy(const ComponentHealth& health) {
    return health.status == HealthStatus::HEALTHY || health.status == HealthStatus::DEGRADED;
}

// Utility methods

HealthStatus STTHealthChecker::determineOverallStatus(const std::vector<ComponentHealth>& components) {
    if (components.empty()) {
        return HealthStatus::UNKNOWN;
    }
    
    int critical = 0, unhealthy = 0, degraded = 0, healthy = 0;
    
    for (const auto& comp : components) {
        switch (comp.status) {
            case HealthStatus::CRITICAL: critical++; break;
            case HealthStatus::UNHEALTHY: unhealthy++; break;
            case HealthStatus::DEGRADED: degraded++; break;
            case HealthStatus::HEALTHY: healthy++; break;
            default: break;
        }
    }
    
    // Determine overall status based on component statuses
    if (critical > 0) {
        return HealthStatus::CRITICAL;
    } else if (unhealthy > 0) {
        return HealthStatus::UNHEALTHY;
    } else if (degraded > 0) {
        return HealthStatus::DEGRADED;
    } else if (healthy > 0) {
        return HealthStatus::HEALTHY;
    } else {
        return HealthStatus::UNKNOWN;
    }
}

std::string STTHealthChecker::healthStatusToString(HealthStatus status) {
    switch (status) {
        case HealthStatus::HEALTHY: return "HEALTHY";
        case HealthStatus::DEGRADED: return "DEGRADED";
        case HealthStatus::UNHEALTHY: return "UNHEALTHY";
        case HealthStatus::CRITICAL: return "CRITICAL";
        case HealthStatus::UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

std::string STTHealthChecker::generateAlertId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(1000, 9999);
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << "ALERT_" << time_t << "_" << dis(gen);
    return oss.str();
}

double STTHealthChecker::calculateElapsedMs(const std::chrono::steady_clock::time_point& start) {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now - start).count();
}

std::string STTHealthChecker::formatHealthStatusJSON(const SystemHealthStatus& status) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"overall_status\": \"" << healthStatusToString(status.overall_status) << "\",\n";
    json << "  \"overall_message\": \"" << status.overall_message << "\",\n";
    json << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::milliseconds>(status.timestamp.time_since_epoch()).count() << ",\n";
    json << "  \"total_check_time_ms\": " << status.total_check_time_ms << ",\n";
    json << "  \"components\": [\n";
    
    for (size_t i = 0; i < status.component_health.size(); ++i) {
        const auto& comp = status.component_health[i];
        json << "    {\n";
        json << "      \"name\": \"" << comp.component_name << "\",\n";
        json << "      \"status\": \"" << healthStatusToString(comp.status) << "\",\n";
        json << "      \"message\": \"" << comp.status_message << "\",\n";
        json << "      \"response_time_ms\": " << comp.response_time_ms << ",\n";
        json << "      \"details\": {\n";
        
        size_t detailIndex = 0;
        for (const auto& [key, value] : comp.details) {
            json << "        \"" << key << "\": \"" << value << "\"";
            if (++detailIndex < comp.details.size()) json << ",";
            json << "\n";
        }
        
        json << "      }\n";
        json << "    }";
        if (i < status.component_health.size() - 1) json << ",";
        json << "\n";
    }
    
    json << "  ],\n";
    json << "  \"resource_usage\": {\n";
    json << "    \"cpu_usage_percent\": " << status.resource_usage.cpu_usage_percent << ",\n";
    json << "    \"memory_usage_mb\": " << status.resource_usage.memory_usage_mb << ",\n";
    json << "    \"gpu_memory_usage_mb\": " << status.resource_usage.gpu_memory_usage_mb << ",\n";
    json << "    \"gpu_utilization_percent\": " << status.resource_usage.gpu_utilization_percent << ",\n";
    json << "    \"active_transcriptions\": " << status.resource_usage.active_transcriptions << ",\n";
    json << "    \"queued_requests\": " << status.resource_usage.queued_requests << ",\n";
    json << "    \"buffer_usage_mb\": " << status.resource_usage.buffer_usage_mb << "\n";
    json << "  }\n";
    json << "}";
    
    return json.str();
}

// HealthCheckTimer implementation

HealthCheckTimer::HealthCheckTimer(const std::string& checkName)
    : check_name_(checkName)
    , start_time_(std::chrono::steady_clock::now()) {
}

HealthCheckTimer::~HealthCheckTimer() {
    double elapsedMs = getElapsedMs();
    speechrnt::utils::Logger::debug("Health check '" + check_name_ + "' completed in " + std::to_string(elapsedMs) + "ms");
}

double HealthCheckTimer::getElapsedMs() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now - start_time_).count();
}

} // namespace stt