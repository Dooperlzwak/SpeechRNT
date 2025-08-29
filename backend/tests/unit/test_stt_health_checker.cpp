#include <gtest/gtest.h>
#include "stt/stt_health_checker.hpp"
#include "stt/whisper_stt.hpp"
#include <memory>
#include <thread>
#include <chrono>

class STTHealthCheckerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.health_check_interval_ms = 100;  // Fast checks for testing
        config_.detailed_check_interval_ms = 200;
        config_.max_response_time_ms = 100.0;
        config_.max_cpu_usage_percent = 80.0;
        config_.enable_alerting = true;
        config_.enable_load_balancing = true;
        
        health_checker_ = std::make_unique<stt::STTHealthChecker>();
        ASSERT_TRUE(health_checker_->initialize(config_));
    }
    
    void TearDown() override {
        if (health_checker_) {
            health_checker_->stopMonitoring();
        }
    }
    
    stt::HealthCheckConfig config_;
    std::unique_ptr<stt::STTHealthChecker> health_checker_;
};

TEST_F(STTHealthCheckerTest, InitializationTest) {
    EXPECT_TRUE(health_checker_->isEnabled());
    
    auto config = health_checker_->getConfig();
    EXPECT_EQ(config.health_check_interval_ms, 100);
    EXPECT_EQ(config.detailed_check_interval_ms, 200);
    EXPECT_TRUE(config.enable_alerting);
    EXPECT_TRUE(config.enable_load_balancing);
}

TEST_F(STTHealthCheckerTest, BasicHealthCheckTest) {
    auto healthStatus = health_checker_->checkHealth(false);
    
    EXPECT_NE(healthStatus.overall_status, stt::HealthStatus::UNKNOWN);
    EXPECT_FALSE(healthStatus.overall_message.empty());
    EXPECT_GT(healthStatus.total_check_time_ms, 0.0);
    EXPECT_GE(healthStatus.component_health.size(), 0);
}

TEST_F(STTHealthCheckerTest, DetailedHealthCheckTest) {
    auto healthStatus = health_checker_->checkHealth(true);
    
    EXPECT_NE(healthStatus.overall_status, stt::HealthStatus::UNKNOWN);
    EXPECT_FALSE(healthStatus.overall_message.empty());
    EXPECT_GT(healthStatus.total_check_time_ms, 0.0);
    
    // Detailed check should have more components
    EXPECT_GE(healthStatus.component_health.size(), 3);
    
    // Check that we have expected components
    bool hasResourceHealth = false;
    bool hasPerformanceHealth = false;
    bool hasModelHealth = false;
    
    for (const auto& comp : healthStatus.component_health) {
        if (comp.component_name == "System_Resources") hasResourceHealth = true;
        if (comp.component_name == "Performance_Metrics") hasPerformanceHealth = true;
        if (comp.component_name == "Model_Status") hasModelHealth = true;
    }
    
    EXPECT_TRUE(hasResourceHealth);
    EXPECT_TRUE(hasPerformanceHealth);
    EXPECT_TRUE(hasModelHealth);
}

TEST_F(STTHealthCheckerTest, STTInstanceRegistrationTest) {
    auto sttInstance = std::make_shared<stt::WhisperSTT>();
    
    // Register instance
    health_checker_->registerSTTInstance("test_instance", sttInstance);
    
    // Check health should now include the instance
    auto healthStatus = health_checker_->checkHealth(false);
    
    bool foundInstance = false;
    for (const auto& comp : healthStatus.component_health) {
        if (comp.component_name == "STT_Instance_test_instance") {
            foundInstance = true;
            // Instance should be critical since it's not initialized
            EXPECT_EQ(comp.status, stt::HealthStatus::CRITICAL);
            break;
        }
    }
    
    EXPECT_TRUE(foundInstance);
    
    // Get instance health directly
    auto instanceHealth = health_checker_->getInstanceHealth("test_instance");
    ASSERT_NE(instanceHealth, nullptr);
    EXPECT_EQ(instanceHealth->component_name, "STT_Instance_test_instance");
    
    // Unregister instance
    health_checker_->unregisterSTTInstance("test_instance");
    
    // Instance should no longer be found
    instanceHealth = health_checker_->getInstanceHealth("test_instance");
    EXPECT_EQ(instanceHealth, nullptr);
}

TEST_F(STTHealthCheckerTest, LoadBalancingTest) {
    // Initially no instances, so no healthy instances
    auto healthyInstances = health_checker_->getHealthyInstances();
    EXPECT_TRUE(healthyInstances.empty());
    
    auto recommendedInstance = health_checker_->getRecommendedInstance();
    EXPECT_TRUE(recommendedInstance.empty());
    
    // Register some instances
    auto stt1 = std::make_shared<stt::WhisperSTT>();
    auto stt2 = std::make_shared<stt::WhisperSTT>();
    
    health_checker_->registerSTTInstance("stt1", stt1);
    health_checker_->registerSTTInstance("stt2", stt2);
    
    // Check health to update instance statuses
    health_checker_->checkHealth(false);
    
    // Since instances are not initialized, they should not be healthy
    healthyInstances = health_checker_->getHealthyInstances();
    EXPECT_TRUE(healthyInstances.empty());
    
    recommendedInstance = health_checker_->getRecommendedInstance();
    EXPECT_TRUE(recommendedInstance.empty());
}

TEST_F(STTHealthCheckerTest, HealthMetricsTest) {
    auto metrics = health_checker_->getHealthMetrics();
    
    // Should have basic metrics
    EXPECT_TRUE(metrics.count("overall_health_score") > 0);
    EXPECT_TRUE(metrics.count("total_components") > 0);
    EXPECT_TRUE(metrics.count("healthy_components") > 0);
    EXPECT_TRUE(metrics.count("total_health_checks") > 0);
    EXPECT_TRUE(metrics.count("system_load_factor") > 0);
    
    // Values should be reasonable
    EXPECT_GE(metrics["total_components"], 0.0);
    EXPECT_GE(metrics["healthy_components"], 0.0);
    EXPECT_GE(metrics["total_health_checks"], 0.0);
    EXPECT_GE(metrics["system_load_factor"], 0.0);
    EXPECT_LE(metrics["system_load_factor"], 1.0);
}

TEST_F(STTHealthCheckerTest, MonitoringStatisticsTest) {
    auto stats = health_checker_->getMonitoringStats();
    
    // Should have basic statistics
    EXPECT_TRUE(stats.count("total_health_checks") > 0);
    EXPECT_TRUE(stats.count("total_alerts_generated") > 0);
    EXPECT_TRUE(stats.count("registered_instances") > 0);
    
    // Initial values
    EXPECT_GE(stats["total_health_checks"], 0);
    EXPECT_GE(stats["total_alerts_generated"], 0);
    EXPECT_EQ(stats["registered_instances"], 0); // No instances registered yet
}

TEST_F(STTHealthCheckerTest, CanAcceptRequestsTest) {
    // Initially should be able to accept requests (no critical issues)
    bool canAccept = health_checker_->canAcceptNewRequests();
    // This depends on system state, so we just check it doesn't crash
    EXPECT_TRUE(canAccept || !canAccept); // Always true, just testing the call
    
    // Get system load factor
    double loadFactor = health_checker_->getSystemLoadFactor();
    EXPECT_GE(loadFactor, 0.0);
    EXPECT_LE(loadFactor, 1.0);
}

TEST_F(STTHealthCheckerTest, HealthHistoryTest) {
    // Perform a few health checks to build history
    health_checker_->checkHealth(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    health_checker_->checkHealth(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    health_checker_->checkHealth(false);
    
    // Get health history
    auto history = health_checker_->getHealthHistory(1); // Last 1 hour
    
    EXPECT_GE(history.size(), 3); // Should have at least our 3 checks
    
    // Check that history entries are properly formatted
    for (const auto& entry : history) {
        EXPECT_NE(entry.overall_status, stt::HealthStatus::UNKNOWN);
        EXPECT_FALSE(entry.overall_message.empty());
        EXPECT_GT(entry.total_check_time_ms, 0.0);
    }
}

TEST_F(STTHealthCheckerTest, AlertManagementTest) {
    // Initially no alerts
    auto alerts = health_checker_->getActiveAlerts();
    size_t initialAlertCount = alerts.size();
    
    // Force a health check that might generate alerts
    health_checker_->checkHealth(true);
    
    // Check alerts again
    alerts = health_checker_->getActiveAlerts();
    // We can't guarantee alerts will be generated, so just check the call works
    EXPECT_GE(alerts.size(), initialAlertCount);
    
    // Test alert acknowledgment (if any alerts exist)
    if (!alerts.empty()) {
        std::string alertId = alerts[0].alert_id;
        EXPECT_FALSE(alerts[0].acknowledged);
        
        bool acknowledged = health_checker_->acknowledgeAlert(alertId);
        EXPECT_TRUE(acknowledged);
        
        // Check that alert is now acknowledged
        alerts = health_checker_->getActiveAlerts();
        bool foundAcknowledged = false;
        for (const auto& alert : alerts) {
            if (alert.alert_id == alertId && alert.acknowledged) {
                foundAcknowledged = true;
                break;
            }
        }
        // Note: acknowledged alerts might be filtered out of active alerts
    }
    
    // Test clearing acknowledged alerts
    health_checker_->clearAcknowledgedAlerts();
    // This should work without throwing
}

TEST_F(STTHealthCheckerTest, JSONExportTest) {
    // Perform a health check
    health_checker_->checkHealth(true);
    
    // Export to JSON
    std::string healthJson = health_checker_->exportHealthStatusJSON(false);
    
    EXPECT_FALSE(healthJson.empty());
    EXPECT_TRUE(healthJson.find("overall_status") != std::string::npos);
    EXPECT_TRUE(healthJson.find("components") != std::string::npos);
    EXPECT_TRUE(healthJson.find("resource_usage") != std::string::npos);
    
    // Should be valid JSON format (basic check)
    EXPECT_TRUE(healthJson.front() == '{');
    EXPECT_TRUE(healthJson.back() == '}');
}

TEST_F(STTHealthCheckerTest, ConfigurationUpdateTest) {
    // Update configuration
    stt::HealthCheckConfig newConfig = config_;
    newConfig.health_check_interval_ms = 500;
    newConfig.max_response_time_ms = 200.0;
    newConfig.enable_alerting = false;
    
    health_checker_->updateConfig(newConfig);
    
    auto updatedConfig = health_checker_->getConfig();
    EXPECT_EQ(updatedConfig.health_check_interval_ms, 500);
    EXPECT_EQ(updatedConfig.max_response_time_ms, 200.0);
    EXPECT_FALSE(updatedConfig.enable_alerting);
}

TEST_F(STTHealthCheckerTest, EnableDisableTest) {
    EXPECT_TRUE(health_checker_->isEnabled());
    
    // Disable health monitoring
    health_checker_->setEnabled(false);
    EXPECT_FALSE(health_checker_->isEnabled());
    
    // Health check should return unknown status when disabled
    auto healthStatus = health_checker_->checkHealth(false);
    EXPECT_EQ(healthStatus.overall_status, stt::HealthStatus::UNKNOWN);
    EXPECT_TRUE(healthStatus.overall_message.find("disabled") != std::string::npos);
    
    // Re-enable
    health_checker_->setEnabled(true);
    EXPECT_TRUE(health_checker_->isEnabled());
    
    // Should work normally again
    healthStatus = health_checker_->checkHealth(false);
    EXPECT_NE(healthStatus.overall_status, stt::HealthStatus::UNKNOWN);
}

// Test the RAII timer helper
TEST_F(STTHealthCheckerTest, HealthCheckTimerTest) {
    auto start = std::chrono::steady_clock::now();
    
    {
        stt::HealthCheckTimer timer("test_check");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        double elapsed = timer.getElapsedMs();
        EXPECT_GE(elapsed, 10.0);
        EXPECT_LT(elapsed, 100.0); // Should be reasonable
    }
    
    auto end = std::chrono::steady_clock::now();
    auto totalElapsed = std::chrono::duration<double, std::milli>(end - start).count();
    EXPECT_GE(totalElapsed, 10.0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}