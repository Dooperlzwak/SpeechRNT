#pragma once

#include "stt/stt_interface.hpp"
#include "stt/whisper_stt.hpp"
#include "stt/stt_performance_tracker.hpp"
#include "utils/performance_monitor.hpp"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <atomic>
#include <mutex>
#include <functional>
#include <thread>

namespace stt {

/**
 * Health status levels for different components
 */
enum class HealthStatus {
    HEALTHY,        // Component is functioning normally
    DEGRADED,       // Component is functioning but with reduced performance
    UNHEALTHY,      // Component has issues but is still operational
    CRITICAL,       // Component is failing and needs immediate attention
    UNKNOWN         // Health status cannot be determined
};

/**
 * Health check result for individual components
 */
struct ComponentHealth {
    std::string component_name;
    HealthStatus status;
    std::string status_message;
    std::map<std::string, std::string> details;
    std::chrono::steady_clock::time_point last_check;
    double response_time_ms;
    
    ComponentHealth() 
        : status(HealthStatus::UNKNOWN)
        , last_check(std::chrono::steady_clock::now())
        , response_time_ms(0.0) {}
        
    ComponentHealth(const std::string& name, HealthStatus stat, const std::string& message)
        : component_name(name)
        , status(stat)
        , status_message(message)
        , last_check(std::chrono::steady_clock::now())
        , response_time_ms(0.0) {}
};

/**
 * Overall system health status
 */
struct SystemHealthStatus {
    HealthStatus overall_status;
    std::string overall_message;
    std::vector<ComponentHealth> component_health;
    std::map<std::string, double> system_metrics;
    std::chrono::steady_clock::time_point timestamp;
    double total_check_time_ms;
    
    // Resource usage information
    struct ResourceUsage {
        double cpu_usage_percent;
        double memory_usage_mb;
        double gpu_memory_usage_mb;
        double gpu_utilization_percent;
        int active_transcriptions;
        int queued_requests;
        double buffer_usage_mb;
        
        ResourceUsage() 
            : cpu_usage_percent(0.0)
            , memory_usage_mb(0.0)
            , gpu_memory_usage_mb(0.0)
            , gpu_utilization_percent(0.0)
            , active_transcriptions(0)
            , queued_requests(0)
            , buffer_usage_mb(0.0) {}
    } resource_usage;
    
    SystemHealthStatus() 
        : overall_status(HealthStatus::UNKNOWN)
        , timestamp(std::chrono::steady_clock::now())
        , total_check_time_ms(0.0) {}
};

/**
 * Health check configuration
 */
struct HealthCheckConfig {
    // Check intervals
    int health_check_interval_ms = 5000;      // 5 seconds
    int detailed_check_interval_ms = 30000;   // 30 seconds
    int resource_check_interval_ms = 1000;    // 1 second
    
    // Thresholds
    double max_response_time_ms = 1000.0;     // Max acceptable response time
    double max_cpu_usage_percent = 80.0;      // Max CPU usage before warning
    double max_memory_usage_mb = 8192.0;      // Max memory usage (8GB)
    double max_gpu_memory_usage_mb = 6144.0;  // Max GPU memory usage (6GB)
    double max_buffer_usage_mb = 1024.0;      // Max audio buffer usage (1GB)
    int max_concurrent_transcriptions = 10;   // Max concurrent transcriptions
    int max_queue_size = 50;                  // Max queued requests
    
    // Model health thresholds
    double min_confidence_threshold = 0.3;    // Minimum acceptable confidence
    double max_latency_ms = 2000.0;          // Maximum acceptable latency
    double min_accuracy_threshold = 0.8;      // Minimum acceptable accuracy
    
    // Alerting configuration
    bool enable_alerting = true;
    int alert_cooldown_ms = 60000;           // 1 minute cooldown between alerts
    std::vector<std::string> alert_recipients;
    
    // Load balancing configuration
    bool enable_load_balancing = true;
    double load_balancing_threshold = 0.7;   // Threshold for load balancing decisions
    int min_healthy_instances = 1;           // Minimum healthy instances required
};

/**
 * Health alert information
 */
struct HealthAlert {
    std::string alert_id;
    std::string component_name;
    HealthStatus severity;
    std::string message;
    std::map<std::string, std::string> context;
    std::chrono::steady_clock::time_point timestamp;
    bool acknowledged;
    
    HealthAlert() 
        : severity(HealthStatus::UNKNOWN)
        , timestamp(std::chrono::steady_clock::now())
        , acknowledged(false) {}
};

/**
 * STT Health Checker - Comprehensive health monitoring for STT system
 * 
 * This class provides system health validation, monitoring, and alerting
 * capabilities for the STT pipeline. It monitors model status, resource
 * usage, performance metrics, and provides health-based load balancing.
 */
class STTHealthChecker {
public:
    using HealthChangeCallback = std::function<void(const SystemHealthStatus& status)>;
    using AlertCallback = std::function<void(const HealthAlert& alert)>;
    
    STTHealthChecker();
    ~STTHealthChecker();
    
    /**
     * Initialize the health checker
     * @param config Health check configuration
     * @return true if initialization successful
     */
    bool initialize(const HealthCheckConfig& config = HealthCheckConfig{});
    
    /**
     * Start automated health monitoring
     * @param enableBackgroundMonitoring Enable continuous background monitoring
     * @return true if started successfully
     */
    bool startMonitoring(bool enableBackgroundMonitoring = true);
    
    /**
     * Stop automated health monitoring
     */
    void stopMonitoring();
    
    /**
     * Perform immediate health check
     * @param detailed Whether to perform detailed checks
     * @return current system health status
     */
    SystemHealthStatus checkHealth(bool detailed = false);
    
    /**
     * Check health of specific STT component
     * @param sttInstance STT instance to check
     * @return component health status
     */
    ComponentHealth checkSTTHealth(const STTInterface* sttInstance);
    
    /**
     * Check health of Whisper STT specifically
     * @param whisperSTT Whisper STT instance to check
     * @return component health status
     */
    ComponentHealth checkWhisperSTTHealth(const WhisperSTT* whisperSTT);
    
    /**
     * Check system resource health
     * @return resource health status
     */
    ComponentHealth checkResourceHealth();
    
    /**
     * Check performance metrics health
     * @return performance health status
     */
    ComponentHealth checkPerformanceHealth();
    
    /**
     * Check model loading and availability
     * @return model health status
     */
    ComponentHealth checkModelHealth();
    
    /**
     * Register STT instance for monitoring
     * @param instanceId Unique identifier for the instance
     * @param sttInstance STT instance to monitor
     */
    void registerSTTInstance(const std::string& instanceId, std::shared_ptr<STTInterface> sttInstance);
    
    /**
     * Unregister STT instance from monitoring
     * @param instanceId Instance identifier to remove
     */
    void unregisterSTTInstance(const std::string& instanceId);
    
    /**
     * Get health status for specific instance
     * @param instanceId Instance identifier
     * @return health status or nullptr if not found
     */
    std::shared_ptr<ComponentHealth> getInstanceHealth(const std::string& instanceId);
    
    /**
     * Get list of healthy STT instances for load balancing
     * @return vector of healthy instance IDs
     */
    std::vector<std::string> getHealthyInstances();
    
    /**
     * Get recommended instance for new requests (load balancing)
     * @return instance ID or empty string if none available
     */
    std::string getRecommendedInstance();
    
    /**
     * Set health change callback
     * @param callback Function to call when health status changes
     */
    void setHealthChangeCallback(HealthChangeCallback callback);
    
    /**
     * Set alert callback
     * @param callback Function to call when alerts are generated
     */
    void setAlertCallback(AlertCallback callback);
    
    /**
     * Get current health configuration
     * @return current configuration
     */
    const HealthCheckConfig& getConfig() const { return config_; }
    
    /**
     * Update health check configuration
     * @param config New configuration
     */
    void updateConfig(const HealthCheckConfig& config);
    
    /**
     * Get health history for analysis
     * @param hours Number of hours of history to retrieve
     * @return vector of historical health statuses
     */
    std::vector<SystemHealthStatus> getHealthHistory(int hours = 24);
    
    /**
     * Get active alerts
     * @return vector of active alerts
     */
    std::vector<HealthAlert> getActiveAlerts();
    
    /**
     * Acknowledge alert
     * @param alertId Alert ID to acknowledge
     * @return true if alert was found and acknowledged
     */
    bool acknowledgeAlert(const std::string& alertId);
    
    /**
     * Clear acknowledged alerts
     */
    void clearAcknowledgedAlerts();
    
    /**
     * Get health metrics for monitoring dashboards
     * @return map of health metrics
     */
    std::map<std::string, double> getHealthMetrics();
    
    /**
     * Export health status to JSON
     * @param includeHistory Whether to include historical data
     * @return JSON string representation
     */
    std::string exportHealthStatusJSON(bool includeHistory = false);
    
    /**
     * Check if system is healthy enough for new requests
     * @return true if system can handle new requests
     */
    bool canAcceptNewRequests();
    
    /**
     * Get system load factor (0.0 = no load, 1.0 = maximum load)
     * @return current system load factor
     */
    double getSystemLoadFactor();
    
    /**
     * Force health check for all registered instances
     */
    void forceHealthCheck();
    
    /**
     * Enable or disable health monitoring
     * @param enabled true to enable monitoring
     */
    void setEnabled(bool enabled);
    
    /**
     * Check if health monitoring is enabled
     * @return true if enabled
     */
    bool isEnabled() const { return enabled_; }
    
    /**
     * Get monitoring statistics
     * @return map of monitoring statistics
     */
    std::map<std::string, uint64_t> getMonitoringStats();

private:
    // Configuration
    HealthCheckConfig config_;
    std::atomic<bool> enabled_{true};
    std::atomic<bool> monitoring_active_{false};
    
    // Registered STT instances
    std::mutex instances_mutex_;
    std::map<std::string, std::shared_ptr<STTInterface>> registered_instances_;
    std::map<std::string, ComponentHealth> instance_health_;
    
    // Health monitoring threads
    std::unique_ptr<std::thread> health_monitor_thread_;
    std::unique_ptr<std::thread> resource_monitor_thread_;
    std::atomic<bool> should_stop_monitoring_{false};
    
    // Health status tracking
    std::mutex health_mutex_;
    SystemHealthStatus current_health_;
    std::vector<SystemHealthStatus> health_history_;
    std::chrono::steady_clock::time_point last_health_check_;
    
    // Alert management
    std::mutex alerts_mutex_;
    std::vector<HealthAlert> active_alerts_;
    std::map<std::string, std::chrono::steady_clock::time_point> alert_cooldowns_;
    
    // Callbacks
    HealthChangeCallback health_change_callback_;
    AlertCallback alert_callback_;
    
    // Performance monitoring integration
    speechrnt::utils::PerformanceMonitor& performance_monitor_;
    std::unique_ptr<STTPerformanceTracker> performance_tracker_;
    
    // Statistics
    std::atomic<uint64_t> total_health_checks_{0};
    std::atomic<uint64_t> total_alerts_generated_{0};
    std::atomic<uint64_t> total_health_changes_{0};
    
    // Private methods
    void healthMonitorLoop();
    void resourceMonitorLoop();
    void performHealthCheck(bool detailed);
    void updateHealthHistory(const SystemHealthStatus& status);
    void checkForHealthChanges(const SystemHealthStatus& newStatus);
    void generateAlert(const std::string& component, HealthStatus severity, const std::string& message, const std::map<std::string, std::string>& context = {});
    bool isAlertCooldownActive(const std::string& alertKey);
    void updateAlertCooldown(const std::string& alertKey);
    void cleanupOldAlerts();
    void cleanupOldHealthHistory();
    
    // Component-specific health checks
    ComponentHealth checkCPUHealth();
    ComponentHealth checkMemoryHealth();
    ComponentHealth checkGPUHealth();
    ComponentHealth checkBufferHealth();
    ComponentHealth checkLatencyHealth();
    ComponentHealth checkThroughputHealth();
    ComponentHealth checkErrorRateHealth();
    
    // Load balancing helpers
    double calculateInstanceLoad(const std::string& instanceId);
    std::string selectLeastLoadedInstance();
    bool isInstanceHealthy(const ComponentHealth& health);
    
    // Utility methods
    HealthStatus determineOverallStatus(const std::vector<ComponentHealth>& components);
    std::string healthStatusToString(HealthStatus status);
    std::string generateAlertId();
    double calculateElapsedMs(const std::chrono::steady_clock::time_point& start);
    std::string formatHealthStatusJSON(const SystemHealthStatus& status);
};

/**
 * RAII helper for automatic health check timing
 */
class HealthCheckTimer {
public:
    HealthCheckTimer(const std::string& checkName);
    ~HealthCheckTimer();
    
    double getElapsedMs() const;

private:
    std::string check_name_;
    std::chrono::steady_clock::time_point start_time_;
};

// Convenience macros for health monitoring
#define HEALTH_CHECK_TIMER(name) HealthCheckTimer timer(name)
#define REGISTER_STT_INSTANCE(checker, id, instance) checker.registerSTTInstance(id, instance)
#define CHECK_SYSTEM_HEALTH(checker) checker.checkHealth(false)

} // namespace stt