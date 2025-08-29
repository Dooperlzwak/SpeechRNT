#pragma once

#include "stt/stt_health_checker.hpp"
#include "stt/whisper_stt.hpp"
#include "core/websocket_server.hpp"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace stt {

/**
 * STT Health Integration - Manages health monitoring integration with the main application
 * 
 * This class provides a high-level interface for integrating STT health monitoring
 * with the WebSocket server, STT instances, and other system components. It handles
 * automatic registration of STT instances, health-based load balancing, and alerting.
 */
class STTHealthIntegration {
public:
    using LoadBalancingCallback = std::function<std::string()>;
    using AlertNotificationCallback = std::function<void(const HealthAlert& alert)>;
    
    STTHealthIntegration();
    ~STTHealthIntegration();
    
    /**
     * Initialize the health integration system
     * @param config Health check configuration
     * @return true if initialization successful
     */
    bool initialize(const HealthCheckConfig& config = HealthCheckConfig{});
    
    /**
     * Start health monitoring
     * @param enableBackgroundMonitoring Enable continuous background monitoring
     * @return true if started successfully
     */
    bool start(bool enableBackgroundMonitoring = true);
    
    /**
     * Stop health monitoring
     */
    void stop();
    
    /**
     * Integrate with WebSocket server
     * @param server WebSocket server instance
     */
    void integrateWithWebSocketServer(std::shared_ptr<core::WebSocketServer> server);
    
    /**
     * Register STT instance for health monitoring
     * @param instanceId Unique identifier for the instance
     * @param sttInstance STT instance to monitor
     * @param autoLoadBalance Whether to include this instance in load balancing
     */
    void registerSTTInstance(const std::string& instanceId, 
                           std::shared_ptr<STTInterface> sttInstance,
                           bool autoLoadBalance = true);
    
    /**
     * Unregister STT instance from health monitoring
     * @param instanceId Instance identifier to remove
     */
    void unregisterSTTInstance(const std::string& instanceId);
    
    /**
     * Get recommended STT instance for new requests
     * @return instance ID or empty string if none available
     */
    std::string getRecommendedSTTInstance();
    
    /**
     * Get STT instance by ID
     * @param instanceId Instance identifier
     * @return STT instance or nullptr if not found
     */
    std::shared_ptr<STTInterface> getSTTInstance(const std::string& instanceId);
    
    /**
     * Check if system can accept new requests
     * @return true if system is healthy enough for new requests
     */
    bool canAcceptNewRequests();
    
    /**
     * Get current system health status
     * @param detailed Whether to perform detailed health check
     * @return system health status
     */
    SystemHealthStatus getSystemHealth(bool detailed = false);
    
    /**
     * Set custom load balancing callback
     * @param callback Function to call for load balancing decisions
     */
    void setLoadBalancingCallback(LoadBalancingCallback callback);
    
    /**
     * Set alert notification callback
     * @param callback Function to call when alerts are generated
     */
    void setAlertNotificationCallback(AlertNotificationCallback callback);
    
    /**
     * Get health checker instance
     * @return health checker instance
     */
    std::shared_ptr<STTHealthChecker> getHealthChecker() { return health_checker_; }
    
    /**
     * Force immediate health check for all instances
     */
    void forceHealthCheck();
    
    /**
     * Update health check configuration
     * @param config New configuration
     */
    void updateConfiguration(const HealthCheckConfig& config);
    
    /**
     * Get health monitoring statistics
     * @return map of monitoring statistics
     */
    std::map<std::string, uint64_t> getMonitoringStatistics();
    
    /**
     * Export health status to JSON
     * @param includeHistory Whether to include historical data
     * @return JSON string representation
     */
    std::string exportHealthStatusJSON(bool includeHistory = false);
    
    /**
     * Enable or disable health monitoring
     * @param enabled true to enable monitoring
     */
    void setEnabled(bool enabled);
    
    /**
     * Check if health monitoring is enabled
     * @return true if enabled
     */
    bool isEnabled() const;

private:
    // Core components
    std::shared_ptr<STTHealthChecker> health_checker_;
    std::shared_ptr<core::WebSocketServer> websocket_server_;
    
    // STT instance management
    std::mutex instances_mutex_;
    std::map<std::string, std::shared_ptr<STTInterface>> stt_instances_;
    std::set<std::string> load_balanced_instances_;
    
    // Callbacks
    LoadBalancingCallback load_balancing_callback_;
    AlertNotificationCallback alert_notification_callback_;
    
    // State
    std::atomic<bool> initialized_{false};
    std::atomic<bool> started_{false};
    
    // Private methods
    void onHealthChange(const SystemHealthStatus& status);
    void onAlert(const HealthAlert& alert);
    std::string defaultLoadBalancing();
};

/**
 * Singleton accessor for global health integration
 */
class STTHealthManager {
public:
    static STTHealthManager& getInstance();
    
    /**
     * Initialize global health monitoring
     * @param config Health check configuration
     * @return true if initialization successful
     */
    bool initialize(const HealthCheckConfig& config = HealthCheckConfig{});
    
    /**
     * Get the health integration instance
     * @return health integration instance
     */
    std::shared_ptr<STTHealthIntegration> getHealthIntegration() { return health_integration_; }
    
    /**
     * Quick access to register STT instance
     * @param instanceId Instance identifier
     * @param sttInstance STT instance
     */
    void registerSTTInstance(const std::string& instanceId, std::shared_ptr<STTInterface> sttInstance);
    
    /**
     * Quick access to get recommended instance
     * @return recommended instance ID
     */
    std::string getRecommendedInstance();
    
    /**
     * Quick access to check if system can accept requests
     * @return true if system is healthy
     */
    bool canAcceptRequests();

private:
    STTHealthManager() = default;
    ~STTHealthManager() = default;
    
    // Prevent copying
    STTHealthManager(const STTHealthManager&) = delete;
    STTHealthManager& operator=(const STTHealthManager&) = delete;
    
    std::shared_ptr<STTHealthIntegration> health_integration_;
    std::once_flag initialized_flag_;
};

// Convenience macros for health integration
#define HEALTH_MANAGER STTHealthManager::getInstance()
#define REGISTER_STT_FOR_HEALTH(id, instance) HEALTH_MANAGER.registerSTTInstance(id, instance)
#define GET_RECOMMENDED_STT() HEALTH_MANAGER.getRecommendedInstance()
#define CAN_ACCEPT_STT_REQUESTS() HEALTH_MANAGER.canAcceptRequests()

} // namespace stt