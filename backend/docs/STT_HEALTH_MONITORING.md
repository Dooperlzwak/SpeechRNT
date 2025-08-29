# STT Health Monitoring System

## Overview

The STT Health Monitoring System provides comprehensive health validation, monitoring, and alerting capabilities for the Speech-to-Text pipeline. It monitors model status, resource usage, performance metrics, and provides health-based load balancing and request routing.

## Architecture

### Core Components

1. **STTHealthChecker**: Core health monitoring engine
2. **STTHealthIntegration**: High-level integration with application components
3. **WebSocket Health Endpoints**: HTTP endpoints for health status access
4. **STTHealthManager**: Singleton manager for global health monitoring

### Health Status Levels

- **HEALTHY**: Component is functioning normally
- **DEGRADED**: Component is functioning but with reduced performance
- **UNHEALTHY**: Component has issues but is still operational
- **CRITICAL**: Component is failing and needs immediate attention
- **UNKNOWN**: Health status cannot be determined

## Features

### 1. System Health Validation

The health checker monitors multiple system components:

- **STT Instances**: Model initialization, response times, error states
- **System Resources**: CPU usage, memory usage, GPU resources
- **Performance Metrics**: Latency, throughput, confidence scores
- **Model Status**: Model loading, availability, quantization levels
- **Audio Buffers**: Buffer usage, memory management
- **Error Rates**: System error tracking and analysis

### 2. Health Check Endpoints

The system provides HTTP endpoints for health monitoring:

```
GET /health                 - Basic health status
GET /health/detailed        - Detailed health information
GET /health/metrics         - Health metrics and statistics
GET /health/history         - Historical health data
GET /health/alerts          - Active health alerts
```

#### Basic Health Endpoint (`/health`)

Returns a simple health status suitable for load balancers:

```json
{
  "status": "healthy",
  "service": "SpeechRNT STT",
  "message": "System Status: HEALTHY (Healthy: 5, Degraded: 0, Unhealthy: 0, Critical: 0)",
  "timestamp": 1703123456789,
  "check_time_ms": 15.2,
  "can_accept_requests": true,
  "system_load_factor": 0.35
}
```

#### Detailed Health Endpoint (`/health/detailed`)

Returns comprehensive health information:

```json
{
  "overall_status": "HEALTHY",
  "overall_message": "System Status: HEALTHY",
  "timestamp": 1703123456789,
  "total_check_time_ms": 45.8,
  "components": [
    {
      "name": "STT_Instance_primary",
      "status": "HEALTHY",
      "message": "STT instance is operational",
      "response_time_ms": 12.3,
      "details": {
        "initialized": "true",
        "streaming_active_count": "2",
        "confidence_threshold": "0.7"
      }
    }
  ],
  "resource_usage": {
    "cpu_usage_percent": 45.2,
    "memory_usage_mb": 2048.5,
    "gpu_memory_usage_mb": 1024.0,
    "gpu_utilization_percent": 65.0,
    "active_transcriptions": 3,
    "queued_requests": 1,
    "buffer_usage_mb": 128.5
  }
}
```

### 3. Automated Health Monitoring

The system provides continuous background monitoring:

- **Regular Health Checks**: Configurable interval (default: 5 seconds)
- **Detailed Checks**: Periodic comprehensive checks (default: 30 seconds)
- **Resource Monitoring**: High-frequency resource usage tracking (default: 1 second)

### 4. Alerting System

Automated alert generation for health issues:

```cpp
// Alert callback example
void onHealthAlert(const stt::HealthAlert& alert) {
    std::cout << "Alert: " << alert.component_name 
              << " - " << alert.message << std::endl;
    
    // Send to external monitoring system
    sendToMonitoringSystem(alert);
}
```

Alert features:
- **Severity Levels**: Critical, Unhealthy, Degraded
- **Cooldown Periods**: Prevent alert spam
- **Acknowledgment**: Manual alert acknowledgment
- **Context Information**: Detailed alert context

### 5. Health-based Load Balancing

Intelligent request routing based on health status:

```cpp
// Get recommended instance for new request
std::string instanceId = healthChecker->getRecommendedInstance();
if (!instanceId.empty()) {
    auto sttInstance = getSTTInstance(instanceId);
    // Process request with healthy instance
}
```

Load balancing features:
- **Health-based Selection**: Prefer healthy instances
- **Load Factor Calculation**: Consider response times and resource usage
- **Custom Load Balancing**: Support for custom selection logic
- **Automatic Failover**: Route around unhealthy instances

## Usage Examples

### Basic Setup

```cpp
#include "stt/stt_health_integration.hpp"

// 1. Configure health monitoring
stt::HealthCheckConfig config;
config.health_check_interval_ms = 5000;
config.max_response_time_ms = 500.0;
config.enable_alerting = true;
config.enable_load_balancing = true;

// 2. Initialize health integration
auto healthIntegration = std::make_shared<stt::STTHealthIntegration>();
healthIntegration->initialize(config);

// 3. Register STT instances
auto sttInstance = std::make_shared<stt::WhisperSTT>();
healthIntegration->registerSTTInstance("primary_stt", sttInstance);

// 4. Start monitoring
healthIntegration->start(true); // Enable background monitoring
```

### WebSocket Integration

```cpp
// Integrate with WebSocket server for health endpoints
auto webSocketServer = std::make_shared<core::WebSocketServer>(8080);
healthIntegration->integrateWithWebSocketServer(webSocketServer);

// Start server
webSocketServer->start();
webSocketServer->run();
```

### Using the Singleton Manager

```cpp
#include "stt/stt_health_integration.hpp"

// Initialize global health manager
stt::STTHealthManager::getInstance().initialize();

// Register STT instance
auto stt = std::make_shared<stt::WhisperSTT>();
REGISTER_STT_FOR_HEALTH("main_stt", stt);

// Use in request handling
if (CAN_ACCEPT_STT_REQUESTS()) {
    std::string instanceId = GET_RECOMMENDED_STT();
    // Process request with recommended instance
}
```

### Custom Alert Handling

```cpp
// Set up custom alert handling
healthIntegration->setAlertNotificationCallback([](const stt::HealthAlert& alert) {
    // Log alert
    LOG_WARN("Health Alert: {} - {}", alert.component_name, alert.message);
    
    // Send to external monitoring
    if (alert.severity == stt::HealthStatus::CRITICAL) {
        sendPagerDutyAlert(alert);
    }
    
    // Send to Slack/Teams
    sendSlackNotification(alert);
});
```

### Custom Load Balancing

```cpp
// Implement custom load balancing logic
healthIntegration->setLoadBalancingCallback([]() -> std::string {
    // Custom logic considering:
    // - Geographic location
    // - Model specialization
    // - Business rules
    
    auto healthyInstances = getHealthyInstances();
    if (!healthyInstances.empty()) {
        // Select based on custom criteria
        return selectBestInstance(healthyInstances);
    }
    
    return ""; // No suitable instance
});
```

## Configuration Options

### HealthCheckConfig

```cpp
struct HealthCheckConfig {
    // Check intervals
    int health_check_interval_ms = 5000;      // Basic health check interval
    int detailed_check_interval_ms = 30000;   // Detailed check interval
    int resource_check_interval_ms = 1000;    // Resource monitoring interval
    
    // Thresholds
    double max_response_time_ms = 1000.0;     // Max acceptable response time
    double max_cpu_usage_percent = 80.0;      // CPU usage warning threshold
    double max_memory_usage_mb = 8192.0;      // Memory usage limit
    double max_gpu_memory_usage_mb = 6144.0;  // GPU memory limit
    double max_buffer_usage_mb = 1024.0;      // Audio buffer limit
    int max_concurrent_transcriptions = 10;   // Concurrency limit
    
    // Model health thresholds
    double min_confidence_threshold = 0.3;    // Minimum confidence
    double max_latency_ms = 2000.0;          // Maximum latency
    double min_accuracy_threshold = 0.8;      // Minimum accuracy
    
    // Alerting
    bool enable_alerting = true;
    int alert_cooldown_ms = 60000;           // Alert cooldown period
    
    // Load balancing
    bool enable_load_balancing = true;
    double load_balancing_threshold = 0.7;   // Load threshold
    int min_healthy_instances = 1;           // Minimum healthy instances
};
```

## Monitoring and Metrics

### Health Metrics

The system tracks various health metrics:

- **overall_health_score**: Numeric health score
- **healthy_components**: Number of healthy components
- **degraded_components**: Number of degraded components
- **unhealthy_components**: Number of unhealthy components
- **critical_components**: Number of critical components
- **active_alerts**: Number of active alerts
- **system_load_factor**: Overall system load (0.0-1.0)
- **total_health_checks**: Total health checks performed

### Performance Integration

The health system integrates with the performance monitoring system:

```cpp
// STT-specific metrics tracked:
// - stt_latency: Transcription latency
// - stt_confidence: Confidence scores
// - stt_throughput: Transcription throughput
// - stt_concurrent_transcriptions: Active transcriptions
// - stt_buffer_usage: Audio buffer usage
// - vad_accuracy: VAD accuracy
// - vad_response_time: VAD response time
```

## Best Practices

### 1. Configuration

- Set appropriate thresholds based on your hardware and requirements
- Enable alerting for production environments
- Use shorter intervals for critical systems
- Configure alert cooldowns to prevent spam

### 2. Instance Management

- Register all STT instances for monitoring
- Use descriptive instance IDs
- Enable load balancing for production instances
- Monitor instance health before processing requests

### 3. Alert Handling

- Implement proper alert acknowledgment workflows
- Integrate with external monitoring systems
- Set up escalation procedures for critical alerts
- Regularly review and tune alert thresholds

### 4. Load Balancing

- Implement custom load balancing for complex scenarios
- Consider geographic and business factors
- Monitor load balancing effectiveness
- Have fallback strategies for instance failures

### 5. Monitoring

- Regularly review health history and trends
- Monitor system load factors
- Track alert patterns and frequencies
- Use health endpoints for external monitoring

## Troubleshooting

### Common Issues

1. **High Response Times**
   - Check system resource usage
   - Verify model loading and initialization
   - Review concurrent transcription limits

2. **Memory Issues**
   - Monitor audio buffer usage
   - Check for memory leaks in STT instances
   - Adjust buffer size limits

3. **Alert Spam**
   - Increase alert cooldown periods
   - Adjust health check thresholds
   - Review alert severity levels

4. **Load Balancing Issues**
   - Verify instance registration
   - Check health status of instances
   - Review load balancing configuration

### Debug Information

Enable detailed logging for troubleshooting:

```cpp
// Enable debug logging
utils::Logger::setLevel(utils::LogLevel::DEBUG);

// Force health check for immediate status
healthChecker->forceHealthCheck();

// Export detailed health status
std::string healthJson = healthChecker->exportHealthStatusJSON(true);
std::cout << healthJson << std::endl;
```

## Integration with External Systems

### Prometheus Metrics

```cpp
// Example Prometheus integration
void exportPrometheusMetrics(const std::map<std::string, double>& metrics) {
    for (const auto& [name, value] : metrics) {
        prometheus::Counter& counter = prometheus::BuildCounter()
            .Name("speechrnt_health_" + name)
            .Help("Health metric: " + name)
            .Register(*registry);
        counter.Set(value);
    }
}
```

### Grafana Dashboards

Create dashboards using the health metrics:
- System health overview
- Component status trends
- Alert frequency analysis
- Load balancing effectiveness

### External Monitoring

Integrate with monitoring systems:
- PagerDuty for critical alerts
- Slack/Teams for notifications
- DataDog/New Relic for metrics
- Custom webhook endpoints

## API Reference

See the header files for complete API documentation:
- `backend/include/stt/stt_health_checker.hpp`
- `backend/include/stt/stt_health_integration.hpp`

## Examples

Complete examples are available in:
- `backend/examples/stt_health_monitoring_example.cpp`
- `backend/tests/unit/test_stt_health_checker.cpp`