#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <mutex>
#include <atomic>
#include <functional>
#include <queue>
#include <thread>
#include <condition_variable>

namespace speechrnt {
namespace utils {

/**
 * Severity levels for diagnostic issues
 */
enum class DiagnosticSeverity {
    INFO = 0,
    WARNING = 1,
    ERROR = 2,
    CRITICAL = 3
};

/**
 * Types of diagnostic issues
 */
enum class DiagnosticType {
    PERFORMANCE_DEGRADATION,
    RESOURCE_EXHAUSTION,
    ERROR_RATE_SPIKE,
    LATENCY_SPIKE,
    ACCURACY_DROP,
    SYSTEM_HEALTH,
    CONFIGURATION_ISSUE,
    EXTERNAL_SERVICE_FAILURE,
    MODEL_PERFORMANCE,
    AUDIO_QUALITY_ISSUE
};

/**
 * Diagnostic issue information
 */
struct DiagnosticIssue {
    std::string issueId;
    DiagnosticType type;
    DiagnosticSeverity severity;
    std::string component;
    std::string description;
    std::string details;
    std::chrono::steady_clock::time_point timestamp;
    std::chrono::steady_clock::time_point resolvedTimestamp;
    bool resolved;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> affectedSessions;
    
    DiagnosticIssue() : resolved(false) {
        timestamp = std::chrono::steady_clock::now();
    }
    
    DiagnosticIssue(DiagnosticType t, DiagnosticSeverity s, const std::string& comp, 
                   const std::string& desc, const std::string& det = "")
        : type(t), severity(s), component(comp), description(desc), details(det), resolved(false) {
        timestamp = std::chrono::steady_clock::now();
        issueId = generateIssueId();
    }
    
    void resolve() {
        resolved = true;
        resolvedTimestamp = std::chrono::steady_clock::now();
    }
    
    double getDurationMs() const {
        auto endTime = resolved ? resolvedTimestamp : std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(endTime - timestamp).count() / 1000.0;
    }
    
private:
    std::string generateIssueId();
};

/**
 * Performance regression detection data
 */
struct PerformanceBaseline {
    std::string metricName;
    double baselineValue;
    double tolerance;
    std::chrono::steady_clock::time_point lastUpdate;
    size_t sampleCount;
    double movingAverage;
    double standardDeviation;
    
    PerformanceBaseline() : baselineValue(0.0), tolerance(0.1), sampleCount(0), 
                           movingAverage(0.0), standardDeviation(0.0) {
        lastUpdate = std::chrono::steady_clock::now();
    }
    
    PerformanceBaseline(const std::string& name, double baseline, double tol = 0.1)
        : metricName(name), baselineValue(baseline), tolerance(tol), sampleCount(0),
          movingAverage(baseline), standardDeviation(0.0) {
        lastUpdate = std::chrono::steady_clock::now();
    }
    
    bool isRegression(double currentValue) const {
        if (sampleCount < 10) return false; // Need enough samples
        
        double threshold = baselineValue * (1.0 + tolerance);
        return currentValue > threshold;
    }
    
    void updateBaseline(double newValue) {
        sampleCount++;
        
        // Update moving average
        double alpha = 0.1; // Smoothing factor
        movingAverage = alpha * newValue + (1.0 - alpha) * movingAverage;
        
        // Update standard deviation (simplified)
        double diff = newValue - movingAverage;
        standardDeviation = alpha * (diff * diff) + (1.0 - alpha) * standardDeviation;
        
        lastUpdate = std::chrono::steady_clock::now();
    }
};

/**
 * Diagnostic data aggregation for trend analysis
 */
struct DiagnosticTrend {
    std::string metricName;
    std::vector<std::pair<std::chrono::steady_clock::time_point, double>> dataPoints;
    double trendSlope;
    double correlation;
    bool isIncreasing;
    bool isDecreasing;
    std::chrono::steady_clock::time_point lastAnalysis;
    
    DiagnosticTrend() : trendSlope(0.0), correlation(0.0), isIncreasing(false), isDecreasing(false) {
        lastAnalysis = std::chrono::steady_clock::now();
    }
    
    void addDataPoint(double value) {
        auto now = std::chrono::steady_clock::now();
        dataPoints.emplace_back(now, value);
        
        // Keep only recent data points (last hour)
        auto cutoff = now - std::chrono::hours(1);
        dataPoints.erase(
            std::remove_if(dataPoints.begin(), dataPoints.end(),
                          [cutoff](const auto& point) { return point.first < cutoff; }),
            dataPoints.end());
        
        // Analyze trend if we have enough points
        if (dataPoints.size() >= 10) {
            analyzeTrend();
        }
    }
    
private:
    void analyzeTrend();
};

/**
 * Alert configuration for automated issue detection
 */
struct AlertRule {
    std::string ruleName;
    std::string metricName;
    std::string condition; // "greater_than", "less_than", "equals", "not_equals"
    double threshold;
    DiagnosticSeverity severity;
    std::chrono::milliseconds cooldownPeriod;
    std::chrono::steady_clock::time_point lastTriggered;
    bool enabled;
    std::string description;
    std::map<std::string, std::string> metadata;
    
    AlertRule() : threshold(0.0), severity(DiagnosticSeverity::WARNING), 
                 cooldownPeriod(std::chrono::minutes(5)), enabled(true) {
        lastTriggered = std::chrono::steady_clock::time_point::min();
    }
    
    AlertRule(const std::string& name, const std::string& metric, const std::string& cond,
             double thresh, DiagnosticSeverity sev = DiagnosticSeverity::WARNING)
        : ruleName(name), metricName(metric), condition(cond), threshold(thresh), 
          severity(sev), cooldownPeriod(std::chrono::minutes(5)), enabled(true) {
        lastTriggered = std::chrono::steady_clock::time_point::min();
    }
    
    bool shouldTrigger(double currentValue) const {
        if (!enabled) return false;
        
        auto now = std::chrono::steady_clock::now();
        if (now - lastTriggered < cooldownPeriod) return false;
        
        if (condition == "greater_than") return currentValue > threshold;
        if (condition == "less_than") return currentValue < threshold;
        if (condition == "equals") return std::abs(currentValue - threshold) < 0.001;
        if (condition == "not_equals") return std::abs(currentValue - threshold) >= 0.001;
        
        return false;
    }
    
    void trigger() {
        lastTriggered = std::chrono::steady_clock::now();
    }
};

/**
 * Production-safe diagnostic data collector
 */
class ProductionDiagnostics {
public:
    static ProductionDiagnostics& getInstance();
    
    /**
     * Initialize production diagnostics
     * @param enableAlerting Enable automated alerting
     * @param enableTrendAnalysis Enable trend analysis
     * @param dataRetentionHours How long to keep diagnostic data
     * @return true if initialization successful
     */
    bool initialize(bool enableAlerting = true, 
                   bool enableTrendAnalysis = true,
                   int dataRetentionHours = 24);
    
    /**
     * Record a diagnostic metric value
     * @param metricName Name of the metric
     * @param value Metric value
     * @param component Component that generated the metric
     * @param metadata Additional metadata
     */
    void recordMetric(const std::string& metricName, double value,
                     const std::string& component = "",
                     const std::map<std::string, std::string>& metadata = {});
    
    /**
     * Report a diagnostic issue
     * @param type Type of issue
     * @param severity Severity level
     * @param component Component reporting the issue
     * @param description Issue description
     * @param details Detailed information
     * @param sessionId Optional session ID
     * @return issue ID
     */
    std::string reportIssue(DiagnosticType type, DiagnosticSeverity severity,
                           const std::string& component, const std::string& description,
                           const std::string& details = "", const std::string& sessionId = "");
    
    /**
     * Resolve a diagnostic issue
     * @param issueId Issue ID to resolve
     * @param resolution Resolution description
     */
    void resolveIssue(const std::string& issueId, const std::string& resolution = "");
    
    /**
     * Add an alert rule for automated issue detection
     * @param rule Alert rule configuration
     */
    void addAlertRule(const AlertRule& rule);
    
    /**
     * Remove an alert rule
     * @param ruleName Name of the rule to remove
     */
    void removeAlertRule(const std::string& ruleName);
    
    /**
     * Enable/disable an alert rule
     * @param ruleName Name of the rule
     * @param enabled true to enable, false to disable
     */
    void setAlertRuleEnabled(const std::string& ruleName, bool enabled);
    
    /**
     * Set performance baseline for regression detection
     * @param metricName Name of the metric
     * @param baselineValue Baseline value
     * @param tolerance Tolerance for regression detection (0.1 = 10%)
     */
    void setPerformanceBaseline(const std::string& metricName, double baselineValue, double tolerance = 0.1);
    
    /**
     * Check for performance regressions
     * @param metricName Name of the metric to check
     * @param currentValue Current metric value
     * @return true if regression detected
     */
    bool checkPerformanceRegression(const std::string& metricName, double currentValue);
    
    /**
     * Get current diagnostic issues
     * @param severityFilter Filter by severity (empty for all)
     * @param componentFilter Filter by component (empty for all)
     * @param unresolvedOnly If true, return only unresolved issues
     * @return vector of diagnostic issues
     */
    std::vector<DiagnosticIssue> getCurrentIssues(DiagnosticSeverity severityFilter = DiagnosticSeverity::INFO,
                                                  const std::string& componentFilter = "",
                                                  bool unresolvedOnly = true) const;
    
    /**
     * Get diagnostic trends for a metric
     * @param metricName Name of the metric
     * @return diagnostic trend data
     */
    DiagnosticTrend getDiagnosticTrend(const std::string& metricName) const;
    
    /**
     * Get system health summary
     * @return map of health indicators
     */
    std::map<std::string, double> getSystemHealthSummary() const;
    
    /**
     * Get diagnostic statistics
     * @return map of diagnostic statistics
     */
    std::map<std::string, double> getDiagnosticStatistics() const;
    
    /**
     * Export diagnostic data for analysis
     * @param format Export format ("json", "csv")
     * @param timeRangeHours Time range in hours
     * @return exported data
     */
    std::string exportDiagnosticData(const std::string& format = "json", int timeRangeHours = 24) const;
    
    /**
     * Register callback for diagnostic alerts
     * @param callback Function to call when alerts are triggered
     */
    void registerAlertCallback(std::function<void(const DiagnosticIssue&)> callback);
    
    /**
     * Enable/disable diagnostic collection
     * @param enabled true to enable collection
     */
    void setEnabled(bool enabled);
    
    /**
     * Check if diagnostics are enabled
     * @return true if enabled
     */
    bool isEnabled() const { return enabled_.load(); }
    
    /**
     * Start automated health monitoring
     * @param intervalSeconds Monitoring interval in seconds
     */
    void startHealthMonitoring(int intervalSeconds = 60);
    
    /**
     * Stop automated health monitoring
     */
    void stopHealthMonitoring();
    
    /**
     * Cleanup and shutdown diagnostics
     */
    void cleanup();

private:
    ProductionDiagnostics() = default;
    ~ProductionDiagnostics();
    
    // Prevent copying
    ProductionDiagnostics(const ProductionDiagnostics&) = delete;
    ProductionDiagnostics& operator=(const ProductionDiagnostics&) = delete;
    
    // Private methods
    void processMetric(const std::string& metricName, double value, const std::string& component);
    void checkAlertRules(const std::string& metricName, double value, const std::string& component);
    void performHealthCheck();
    void cleanupOldData();
    void notifyAlertCallbacks(const DiagnosticIssue& issue);
    std::string generateIssueId();
    
    // Member variables
    std::atomic<bool> initialized_{false};
    std::atomic<bool> enabled_{true};
    std::atomic<bool> alertingEnabled_{false};
    std::atomic<bool> trendAnalysisEnabled_{false};
    std::atomic<int> dataRetentionHours_{24};
    
    // Issues tracking
    mutable std::mutex issuesMutex_;
    std::map<std::string, DiagnosticIssue> activeIssues_;
    std::map<std::string, DiagnosticIssue> resolvedIssues_;
    
    // Alert rules
    mutable std::mutex alertRulesMutex_;
    std::map<std::string, AlertRule> alertRules_;
    
    // Performance baselines
    mutable std::mutex baselinesMutex_;
    std::map<std::string, PerformanceBaseline> performanceBaselines_;
    
    // Trend analysis
    mutable std::mutex trendsMutex_;
    std::map<std::string, DiagnosticTrend> diagnosticTrends_;
    
    // Alert callbacks
    mutable std::mutex callbacksMutex_;
    std::vector<std::function<void(const DiagnosticIssue&)>> alertCallbacks_;
    
    // Health monitoring
    std::unique_ptr<std::thread> healthMonitorThread_;
    std::atomic<bool> healthMonitorRunning_{false};
    int healthMonitorInterval_;
    
    // Statistics
    std::atomic<uint64_t> totalIssuesReported_{0};
    std::atomic<uint64_t> criticalIssuesReported_{0};
    std::atomic<uint64_t> issuesResolved_{0};
    std::atomic<uint64_t> alertsTriggered_{0};
    std::atomic<uint64_t> regressionsDetected_{0};
};

/**
 * Automated issue detection system
 */
class AutomatedIssueDetector {
public:
    static AutomatedIssueDetector& getInstance();
    
    /**
     * Initialize the issue detector
     * @param checkIntervalSeconds Interval between checks in seconds
     * @return true if initialization successful
     */
    bool initialize(int checkIntervalSeconds = 30);
    
    /**
     * Add a detection rule
     * @param ruleName Name of the rule
     * @param metricName Metric to monitor
     * @param detectionFunction Function that returns true if issue detected
     * @param severity Severity of detected issues
     * @param description Description of the rule
     */
    void addDetectionRule(const std::string& ruleName, const std::string& metricName,
                         std::function<bool(double)> detectionFunction,
                         DiagnosticSeverity severity = DiagnosticSeverity::WARNING,
                         const std::string& description = "");
    
    /**
     * Remove a detection rule
     * @param ruleName Name of the rule to remove
     */
    void removeDetectionRule(const std::string& ruleName);
    
    /**
     * Start automated detection
     */
    void startDetection();
    
    /**
     * Stop automated detection
     */
    void stopDetection();
    
    /**
     * Manually trigger detection check
     */
    void triggerDetectionCheck();
    
    /**
     * Get detection statistics
     * @return map of detection statistics
     */
    std::map<std::string, double> getDetectionStatistics() const;
    
    /**
     * Cleanup and shutdown detector
     */
    void cleanup();

private:
    AutomatedIssueDetector() = default;
    ~AutomatedIssueDetector();
    
    // Prevent copying
    AutomatedIssueDetector(const AutomatedIssueDetector&) = delete;
    AutomatedIssueDetector& operator=(const AutomatedIssueDetector&) = delete;
    
    struct DetectionRule {
        std::string ruleName;
        std::string metricName;
        std::function<bool(double)> detectionFunction;
        DiagnosticSeverity severity;
        std::string description;
        std::chrono::steady_clock::time_point lastTriggered;
        std::chrono::milliseconds cooldownPeriod;
        bool enabled;
        
        DetectionRule() : severity(DiagnosticSeverity::WARNING), 
                         cooldownPeriod(std::chrono::minutes(5)), enabled(true) {
            lastTriggered = std::chrono::steady_clock::time_point::min();
        }
    };
    
    // Private methods
    void runDetectionLoop();
    void checkDetectionRules();
    
    // Member variables
    std::atomic<bool> initialized_{false};
    std::atomic<bool> detectionRunning_{false};
    int checkInterval_;
    
    mutable std::mutex rulesMutex_;
    std::map<std::string, DetectionRule> detectionRules_;
    
    std::unique_ptr<std::thread> detectionThread_;
    std::condition_variable detectionCondition_;
    std::mutex detectionMutex_;
    
    // Statistics
    std::atomic<uint64_t> totalChecks_{0};
    std::atomic<uint64_t> issuesDetected_{0};
    std::atomic<uint64_t> falsePositives_{0};
};

// Convenience macros for production diagnostics
#define REPORT_DIAGNOSTIC_ISSUE(type, severity, component, description) \
    ProductionDiagnostics::getInstance().reportIssue(type, severity, component, description)

#define RECORD_DIAGNOSTIC_METRIC(name, value, component) \
    ProductionDiagnostics::getInstance().recordMetric(name, value, component)

#define CHECK_PERFORMANCE_REGRESSION(metric, value) \
    ProductionDiagnostics::getInstance().checkPerformanceRegression(metric, value)

} // namespace utils
} // namespace speechrnt