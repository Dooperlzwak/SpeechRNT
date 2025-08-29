#include "stt/stt_health_integration.hpp"
#include "stt/whisper_stt.hpp"
#include "core/websocket_server.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <thread>
#include <chrono>

/**
 * Example demonstrating STT Health Monitoring System usage
 * 
 * This example shows how to:
 * 1. Initialize the health monitoring system
 * 2. Register STT instances for monitoring
 * 3. Integrate with WebSocket server for health endpoints
 * 4. Use health-based load balancing
 * 5. Handle health alerts
 */

void printHealthStatus(const stt::SystemHealthStatus& status) {
    std::cout << "\n=== System Health Status ===" << std::endl;
    std::cout << "Overall Status: " << 
        (status.overall_status == stt::HealthStatus::HEALTHY ? "HEALTHY" :
         status.overall_status == stt::HealthStatus::DEGRADED ? "DEGRADED" :
         status.overall_status == stt::HealthStatus::UNHEALTHY ? "UNHEALTHY" :
         status.overall_status == stt::HealthStatus::CRITICAL ? "CRITICAL" : "UNKNOWN") << std::endl;
    std::cout << "Message: " << status.overall_message << std::endl;
    std::cout << "Check Time: " << status.total_check_time_ms << "ms" << std::endl;
    std::cout << "Components: " << status.component_health.size() << std::endl;
    
    for (const auto& comp : status.component_health) {
        std::cout << "  - " << comp.component_name << ": " << 
            (comp.status == stt::HealthStatus::HEALTHY ? "HEALTHY" :
             comp.status == stt::HealthStatus::DEGRADED ? "DEGRADED" :
             comp.status == stt::HealthStatus::UNHEALTHY ? "UNHEALTHY" :
             comp.status == stt::HealthStatus::CRITICAL ? "CRITICAL" : "UNKNOWN") 
            << " (" << comp.response_time_ms << "ms)" << std::endl;
        std::cout << "    Message: " << comp.status_message << std::endl;
    }
    
    std::cout << "Resource Usage:" << std::endl;
    std::cout << "  CPU: " << status.resource_usage.cpu_usage_percent << "%" << std::endl;
    std::cout << "  Memory: " << status.resource_usage.memory_usage_mb << "MB" << std::endl;
    std::cout << "  GPU Memory: " << status.resource_usage.gpu_memory_usage_mb << "MB" << std::endl;
    std::cout << "  Active Transcriptions: " << status.resource_usage.active_transcriptions << std::endl;
    std::cout << "============================\n" << std::endl;
}

void onHealthAlert(const stt::HealthAlert& alert) {
    std::cout << "\n🚨 HEALTH ALERT 🚨" << std::endl;
    std::cout << "Alert ID: " << alert.alert_id << std::endl;
    std::cout << "Component: " << alert.component_name << std::endl;
    std::cout << "Severity: " << 
        (alert.severity == stt::HealthStatus::CRITICAL ? "CRITICAL" :
         alert.severity == stt::HealthStatus::UNHEALTHY ? "UNHEALTHY" :
         alert.severity == stt::HealthStatus::DEGRADED ? "DEGRADED" : "UNKNOWN") << std::endl;
    std::cout << "Message: " << alert.message << std::endl;
    std::cout << "Acknowledged: " << (alert.acknowledged ? "Yes" : "No") << std::endl;
    std::cout << "==================\n" << std::endl;
}

std::string customLoadBalancing() {
    // Example custom load balancing logic
    // In practice, this could consider additional factors like:
    // - Geographic location
    // - Specialized model capabilities
    // - Custom business logic
    
    std::cout << "Custom load balancing called" << std::endl;
    
    // Fall back to default health-based selection
    return "";
}

int main() {
    std::cout << "STT Health Monitoring System Example" << std::endl;
    std::cout << "====================================" << std::endl;
    
    try {
        // 1. Configure health monitoring
        stt::HealthCheckConfig config;
        config.health_check_interval_ms = 3000;  // Check every 3 seconds
        config.detailed_check_interval_ms = 15000; // Detailed check every 15 seconds
        config.max_response_time_ms = 500.0;      // 500ms max response time
        config.max_cpu_usage_percent = 70.0;      // 70% max CPU usage
        config.enable_alerting = true;
        config.enable_load_balancing = true;
        
        // 2. Initialize health integration
        auto healthIntegration = std::make_shared<stt::STTHealthIntegration>();
        if (!healthIntegration->initialize(config)) {
            std::cerr << "Failed to initialize health integration" << std::endl;
            return 1;
        }
        
        // 3. Set up alert callback
        healthIntegration->setAlertNotificationCallback(onHealthAlert);
        
        // 4. Set up custom load balancing (optional)
        healthIntegration->setLoadBalancingCallback(customLoadBalancing);
        
        // 5. Create and register STT instances
        std::cout << "Creating STT instances..." << std::endl;
        
        // Create multiple STT instances for demonstration
        auto stt1 = std::make_shared<stt::WhisperSTT>();
        auto stt2 = std::make_shared<stt::WhisperSTT>();
        auto stt3 = std::make_shared<stt::WhisperSTT>();
        
        // Initialize STT instances (in practice, you'd use real model paths)
        // For this example, we'll register them even if not fully initialized
        // to demonstrate the health monitoring capabilities
        
        healthIntegration->registerSTTInstance("whisper_primary", stt1, true);
        healthIntegration->registerSTTInstance("whisper_secondary", stt2, true);
        healthIntegration->registerSTTInstance("whisper_backup", stt3, false); // Not load balanced
        
        std::cout << "STT instances registered" << std::endl;
        
        // 6. Create and integrate WebSocket server
        auto webSocketServer = std::make_shared<core::WebSocketServer>(8080);
        healthIntegration->integrateWithWebSocketServer(webSocketServer);
        
        std::cout << "WebSocket server integrated" << std::endl;
        
        // 7. Start health monitoring
        if (!healthIntegration->start(true)) {
            std::cerr << "Failed to start health monitoring" << std::endl;
            return 1;
        }
        
        std::cout << "Health monitoring started" << std::endl;
        std::cout << "\nHealth endpoints available at:" << std::endl;
        std::cout << "  http://localhost:8080/health" << std::endl;
        std::cout << "  http://localhost:8080/health/detailed" << std::endl;
        std::cout << "  http://localhost:8080/health/metrics" << std::endl;
        std::cout << "  http://localhost:8080/health/history" << std::endl;
        std::cout << "  http://localhost:8080/health/alerts" << std::endl;
        
        // 8. Start WebSocket server in a separate thread
        std::thread serverThread([webSocketServer]() {
            webSocketServer->start();
            webSocketServer->run();
        });
        
        // 9. Demonstrate health monitoring features
        std::cout << "\nDemonstrating health monitoring features..." << std::endl;
        
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            // Get current health status
            auto healthStatus = healthIntegration->getSystemHealth(i % 3 == 0); // Detailed every 3rd check
            printHealthStatus(healthStatus);
            
            // Demonstrate load balancing
            std::string recommendedInstance = healthIntegration->getRecommendedSTTInstance();
            std::cout << "Recommended STT instance: " << 
                (recommendedInstance.empty() ? "None available" : recommendedInstance) << std::endl;
            
            // Check if system can accept new requests
            bool canAccept = healthIntegration->canAcceptNewRequests();
            std::cout << "Can accept new requests: " << (canAccept ? "Yes" : "No") << std::endl;
            
            // Show monitoring statistics
            auto stats = healthIntegration->getMonitoringStatistics();
            std::cout << "Monitoring stats:" << std::endl;
            for (const auto& [key, value] : stats) {
                std::cout << "  " << key << ": " << value << std::endl;
            }
            
            std::cout << "\n--- Waiting for next check ---\n" << std::endl;
        }
        
        // 10. Demonstrate alert acknowledgment
        std::cout << "Checking for active alerts..." << std::endl;
        auto activeAlerts = healthIntegration->getHealthChecker()->getActiveAlerts();
        if (!activeAlerts.empty()) {
            std::cout << "Found " << activeAlerts.size() << " active alerts" << std::endl;
            
            // Acknowledge the first alert
            if (healthIntegration->getHealthChecker()->acknowledgeAlert(activeAlerts[0].alert_id)) {
                std::cout << "Alert " << activeAlerts[0].alert_id << " acknowledged" << std::endl;
            }
        } else {
            std::cout << "No active alerts" << std::endl;
        }
        
        // 11. Export health status to JSON
        std::cout << "\nExporting health status to JSON..." << std::endl;
        std::string healthJson = healthIntegration->exportHealthStatusJSON(false);
        std::cout << "Health JSON (truncated): " << healthJson.substr(0, 200) << "..." << std::endl;
        
        // 12. Cleanup
        std::cout << "\nStopping health monitoring..." << std::endl;
        healthIntegration->stop();
        
        webSocketServer->stop();
        if (serverThread.joinable()) {
            serverThread.join();
        }
        
        std::cout << "Example completed successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

// Alternative example using the singleton manager
void demonstrateSingletonUsage() {
    std::cout << "\n=== Singleton Manager Usage ===" << std::endl;
    
    // Initialize global health manager
    stt::HealthCheckConfig config;
    config.health_check_interval_ms = 5000;
    
    if (!stt::STTHealthManager::getInstance().initialize(config)) {
        std::cerr << "Failed to initialize health manager" << std::endl;
        return;
    }
    
    // Register STT instances using convenience macros
    auto stt = std::make_shared<stt::WhisperSTT>();
    REGISTER_STT_FOR_HEALTH("main_stt", stt);
    
    // Use convenience functions
    std::string recommended = GET_RECOMMENDED_STT();
    bool canAccept = CAN_ACCEPT_STT_REQUESTS();
    
    std::cout << "Recommended instance: " << recommended << std::endl;
    std::cout << "Can accept requests: " << (canAccept ? "Yes" : "No") << std::endl;
    
    std::cout << "Singleton usage demonstrated" << std::endl;
}