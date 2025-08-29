#include "utils/production_diagnostics.hpp"
#include "utils/logging.hpp"
#include "utils/performance_monitor.hpp"
#include <algorithm>
#include <random>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <numeric>

namespace speechrnt {
namespace utils {

// DiagnosticIssue Implementation
std::string DiagnosticIssue::generateIssueId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::ostringstream issueId;
    issueId << "issue_";
    
    auto now = std::chrono::steady_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    issueId << timestamp << "_";
    
    // Add random hex suffix
    for (int i = 0; i < 6; ++i) {
        issueId << std::hex << dis(gen);
    }
    
    return issueId.str();
}

// DiagnosticTrend Implementation
void DiagnosticTrend::analyzeTrend() {
    if (dataPoints.size() < 10) {
        return;
    }
    
    // Simple linear regression to calculate trend slope
    size_t n = dataPoints.size();
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0;
    
    for (size_t i = 0; i < n; ++i) {
        double x = static_cast<double>(i);
        double y = dataPoints[i].second;
        
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
    }
    
    double meanX = sumX / n;
    double meanY = sumY / n;
    
    // Calculate slope and correlation
    double numerator = sumXY - n * meanX * meanY;
    double denominatorSlope = sumX2 - n * meanX * meanX;
    
    if (std::abs(denominatorSlope) > 1e-10) {
        trendSlope = numerator / denominatorSlope;
        
        // Calculate correlation coefficient
        double sumY2 = 0.0;
        for (const auto& point : dataPoints) {
            sumY2 += point.second * point.second;
        }
        
        double denominatorCorr = std::sqrt((sumX2 - n * meanX * meanX) * (sumY2 - n * meanY * meanY));
        if (std::abs(denominatorCorr) > 1e-10) {
            correlation = numerator / denominatorCorr;
        }
    }
    
    // Determine trend direction
    isIncreasing = trendSlope > 0.01 && std::abs(correlation) > 0.5;
    isDecreasing = trendSlope < -0.01 && std::abs(correlation) > 0.5;
    
    lastAnalysis = std::chrono::steady_clock::now();
}

// ProductionDiagnostics Implementation
ProductionDiagnostics& ProductionDiagnostics::getInstance() {
    static ProductionDiagnostics instance;
    return instance;
}

ProductionDiagnostics::~ProductionDiagnostics() {
    cleanup();
}

bool ProductionDiagnostics::initialize(bool enableAlerting, bool enableTrendAnalysis, int dataRetentionHours) {
    if (initialized_.load()) {
        return true;
    }
    
    alertingEnabled_ = enableAlerting;
    trendAnalysisEnabled_ = enableTrendAnalysis;
    dataRetentionHours_ = dataRetentionHours;
    
    // Set up default alert rules
    if (enableAlerting) {
        // STT latency alerts
        addAlertRule(AlertRule("stt_high_latency", "stt.latency_ms", "greater_than", 2000.0, DiagnosticSeverity::WARNING));
        addAlertRule(AlertRule("stt_critical_latency", "stt.latency_ms", "greater_than", 5000.0, DiagnosticSeverity::CRITICAL));
        
        // Error rate alerts
        addAlertRule(AlertRule("high_error_rate", "errors.count", "greater_than", 10.0, DiagnosticSeverity::ERROR));
        
        // Memory usage alerts
        addAlertRule(AlertRule("high_memory_usage", "system.memory_usage_mb", "greater_than", 8192.0, DiagnosticSeverity::WARNING));
        addAlertRule(AlertRule("critical_memory_usage", "system.memory_usage_mb", "greater_than", 12288.0, DiagnosticSeverity::CRITICAL));
        
        // GPU memory alerts
        addAlertRule(AlertRule("high_gpu_memory", "gpu.memory_usage_mb", "greater_than", 6144.0, DiagnosticSeverity::WARNING));
        
        // Confidence score alerts
        addAlertRule(AlertRule("low_confidence", "stt.confidence_score", "less_than", 0.5, DiagnosticSeverity::WARNING));
        
        // Throughput alerts
        addAlertRule(AlertRule("low_throughput", "stt.throughput_ops_per_sec", "less_than", 1.0, DiagnosticSeverity::WARNING));
    }
    
    // Set up default performance baselines
    setPerformanceBaseline("stt.latency_ms", 1000.0, 0.2); // 20% tolerance
    setPerformanceBaseline("stt.confidence_score", 0.8, 0.1); // 10% tolerance
    setPerformanceBaseline("stt.throughput_ops_per_sec", 5.0, 0.3); // 30% tolerance
    setPerformanceBaseline("system.memory_usage_mb", 4096.0, 0.5); // 50% tolerance
    
    initialized_ = true;
    Logger::info("Production diagnostics initialized (alerting: " + 
                std::string(enableAlerting ? "enabled" : "disabled") + 
                ", trend analysis: " + std::string(enableTrendAnalysis ? "enabled" : "disabled") + 
                ", retention: " + std::to_string(dataRetentionHours) + "h)");
    
    return true;
}

void ProductionDiagnostics::recordMetric(const std::string& metricName, double value,
                                       const std::string& component,
                                       const std::map<std::string, std::string>& metadata) {
    if (!enabled_.load()) {
        return;
    }
    
    processMetric(metricName, value, component);
    
    // Check for performance regressions
    checkPerformanceRegression(metricName, value);
    
    // Check alert rules
    if (alertingEnabled_.load()) {
        checkAlertRules(metricName, value, component);
    }
    
    // Update trend analysis
    if (trendAnalysisEnabled_.load()) {
        std::lock_guard<std::mutex> lock(trendsMutex_);
        auto& trend = diagnosticTrends_[metricName];
        trend.metricName = metricName;
        trend.addDataPoint(value);
    }
}

std::string ProductionDiagnostics::reportIssue(DiagnosticType type, DiagnosticSeverity severity,
                                              const std::string& component, const std::string& description,
                                              const std::string& details, const std::string& sessionId) {
    if (!enabled_.load()) {
        return "";
    }
    
    DiagnosticIssue issue(type, severity, component, description, details);
    
    if (!sessionId.empty()) {
        issue.affectedSessions.push_back(sessionId);
    }
    
    {
        std::lock_guard<std::mutex> lock(issuesMutex_);
        activeIssues_[issue.issueId] = issue;
    }
    
    totalIssuesReported_++;
    if (severity == DiagnosticSeverity::CRITICAL) {
        criticalIssuesReported_++;
    }
    
    // Notify alert callbacks
    notifyAlertCallbacks(issue);
    
    // Log the issue
    std::string severityStr;
    switch (severity) {
        case DiagnosticSeverity::INFO: severityStr = "INFO"; break;
        case DiagnosticSeverity::WARNING: severityStr = "WARNING"; break;
        case DiagnosticSeverity::ERROR: severityStr = "ERROR"; break;
        case DiagnosticSeverity::CRITICAL: severityStr = "CRITICAL"; break;
    }
    
    Logger::warn("Diagnostic issue reported [" + severityStr + "] " + component + ": " + description + 
                " (ID: " + issue.issueId + ")");
    
    return issue.issueId;
}

void ProductionDiagnostics::resolveIssue(const std::string& issueId, const std::string& resolution) {
    std::lock_guard<std::mutex> lock(issuesMutex_);
    
    auto it = activeIssues_.find(issueId);
    if (it != activeIssues_.end()) {
        it->second.resolve();
        if (!resolution.empty()) {
            it->second.metadata["resolution"] = resolution;
        }
        
        resolvedIssues_[issueId] = it->second;
        activeIssues_.erase(it);
        
        issuesResolved_++;
        
        Logger::info("Diagnostic issue resolved: " + issueId + 
                    (resolution.empty() ? "" : " - " + resolution));
    }
}

void ProductionDiagnostics::addAlertRule(const AlertRule& rule) {
    std::lock_guard<std::mutex> lock(alertRulesMutex_);
    alertRules_[rule.ruleName] = rule;
    
    Logger::debug("Added alert rule: " + rule.ruleName + " for metric: " + rule.metricName);
}

void ProductionDiagnostics::removeAlertRule(const std::string& ruleName) {
    std::lock_guard<std::mutex> lock(alertRulesMutex_);
    alertRules_.erase(ruleName);
    
    Logger::debug("Removed alert rule: " + ruleName);
}

void ProductionDiagnostics::setAlertRuleEnabled(const std::string& ruleName, bool enabled) {
    std::lock_guard<std::mutex> lock(alertRulesMutex_);
    
    auto it = alertRules_.find(ruleName);
    if (it != alertRules_.end()) {
        it->second.enabled = enabled;
        Logger::debug("Alert rule " + ruleName + " " + (enabled ? "enabled" : "disabled"));
    }
}

void ProductionDiagnostics::setPerformanceBaseline(const std::string& metricName, double baselineValue, double tolerance) {
    std::lock_guard<std::mutex> lock(baselinesMutex_);
    performanceBaselines_[metricName] = PerformanceBaseline(metricName, baselineValue, tolerance);
    
    Logger::debug("Set performance baseline for " + metricName + ": " + std::to_string(baselineValue) + 
                 " (tolerance: " + std::to_string(tolerance * 100) + "%)");
}

bool ProductionDiagnostics::checkPerformanceRegression(const std::string& metricName, double currentValue) {
    std::lock_guard<std::mutex> lock(baselinesMutex_);
    
    auto it = performanceBaselines_.find(metricName);
    if (it == performanceBaselines_.end()) {
        return false;
    }
    
    auto& baseline = it->second;
    baseline.updateBaseline(currentValue);
    
    if (baseline.isRegression(currentValue)) {
        regressionsDetected_++;
        
        // Report regression as a diagnostic issue
        std::string description = "Performance regression detected for " + metricName;
        std::string details = "Current value: " + std::to_string(currentValue) + 
                             ", Baseline: " + std::to_string(baseline.baselineValue) + 
                             ", Threshold: " + std::to_string(baseline.baselineValue * (1.0 + baseline.tolerance));
        
        reportIssue(DiagnosticType::PERFORMANCE_DEGRADATION, DiagnosticSeverity::WARNING,
                   "PerformanceMonitor", description, details);
        
        return true;
    }
    
    return false;
}

std::vector<DiagnosticIssue> ProductionDiagnostics::getCurrentIssues(DiagnosticSeverity severityFilter,
                                                                    const std::string& componentFilter,
                                                                    bool unresolvedOnly) const {
    std::lock_guard<std::mutex> lock(issuesMutex_);
    
    std::vector<DiagnosticIssue> filteredIssues;
    
    // Check active issues
    for (const auto& pair : activeIssues_) {
        const auto& issue = pair.second;
        
        if (issue.severity >= severityFilter &&
            (componentFilter.empty() || issue.component == componentFilter)) {
            filteredIssues.push_back(issue);
        }
    }
    
    // Check resolved issues if requested
    if (!unresolvedOnly) {
        for (const auto& pair : resolvedIssues_) {
            const auto& issue = pair.second;
            
            if (issue.severity >= severityFilter &&
                (componentFilter.empty() || issue.component == componentFilter)) {
                filteredIssues.push_back(issue);
            }
        }
    }
    
    // Sort by timestamp (newest first)
    std::sort(filteredIssues.begin(), filteredIssues.end(),
              [](const DiagnosticIssue& a, const DiagnosticIssue& b) {
                  return a.timestamp > b.timestamp;
              });
    
    return filteredIssues;
}

DiagnosticTrend ProductionDiagnostics::getDiagnosticTrend(const std::string& metricName) const {
    std::lock_guard<std::mutex> lock(trendsMutex_);
    
    auto it = diagnosticTrends_.find(metricName);
    if (it != diagnosticTrends_.end()) {
        return it->second;
    }
    
    return DiagnosticTrend();
}

std::map<std::string, double> ProductionDiagnostics::getSystemHealthSummary() const {
    std::map<std::string, double> healthSummary;
    
    // Count issues by severity
    {
        std::lock_guard<std::mutex> lock(issuesMutex_);
        
        int criticalCount = 0, errorCount = 0, warningCount = 0, infoCount = 0;
        
        for (const auto& pair : activeIssues_) {
            switch (pair.second.severity) {
                case DiagnosticSeverity::CRITICAL: criticalCount++; break;
                case DiagnosticSeverity::ERROR: errorCount++; break;
                case DiagnosticSeverity::WARNING: warningCount++; break;
                case DiagnosticSeverity::INFO: infoCount++; break;
            }
        }
        
        healthSummary["active_critical_issues"] = criticalCount;
        healthSummary["active_error_issues"] = errorCount;
        healthSummary["active_warning_issues"] = warningCount;
        healthSummary["active_info_issues"] = infoCount;
        healthSummary["total_active_issues"] = activeIssues_.size();
    }
    
    // Calculate overall health score (0.0 to 1.0)
    double healthScore = 1.0;
    healthScore -= healthSummary["active_critical_issues"] * 0.3;
    healthScore -= healthSummary["active_error_issues"] * 0.2;
    healthScore -= healthSummary["active_warning_issues"] * 0.1;
    healthScore = std::max(0.0, healthScore);
    
    healthSummary["overall_health_score"] = healthScore;
    
    // Add performance metrics from PerformanceMonitor
    auto& perfMonitor = PerformanceMonitor::getInstance();
    if (perfMonitor.isEnabled()) {
        auto systemSummary = perfMonitor.getSystemSummary();
        for (const auto& metric : systemSummary) {
            healthSummary["perf_" + metric.first] = metric.second;
        }
    }
    
    return healthSummary;
}

std::map<std::string, double> ProductionDiagnostics::getDiagnosticStatistics() const {
    std::map<std::string, double> stats;
    
    stats["total_issues_reported"] = static_cast<double>(totalIssuesReported_.load());
    stats["critical_issues_reported"] = static_cast<double>(criticalIssuesReported_.load());
    stats["issues_resolved"] = static_cast<double>(issuesResolved_.load());
    stats["alerts_triggered"] = static_cast<double>(alertsTriggered_.load());
    stats["regressions_detected"] = static_cast<double>(regressionsDetected_.load());
    
    {
        std::lock_guard<std::mutex> lock(issuesMutex_);
        stats["active_issues"] = static_cast<double>(activeIssues_.size());
        stats["resolved_issues"] = static_cast<double>(resolvedIssues_.size());
    }
    
    {
        std::lock_guard<std::mutex> lock(alertRulesMutex_);
        stats["alert_rules_count"] = static_cast<double>(alertRules_.size());
        
        int enabledRules = 0;
        for (const auto& rule : alertRules_) {
            if (rule.second.enabled) enabledRules++;
        }
        stats["enabled_alert_rules"] = static_cast<double>(enabledRules);
    }
    
    {
        std::lock_guard<std::mutex> lock(baselinesMutex_);
        stats["performance_baselines_count"] = static_cast<double>(performanceBaselines_.size());
    }
    
    // Calculate resolution rate
    if (totalIssuesReported_.load() > 0) {
        stats["resolution_rate"] = static_cast<double>(issuesResolved_.load()) / totalIssuesReported_.load();
    } else {
        stats["resolution_rate"] = 0.0;
    }
    
    return stats;
}

std::string ProductionDiagnostics::exportDiagnosticData(const std::string& format, int timeRangeHours) const {
    auto cutoffTime = std::chrono::steady_clock::now() - std::chrono::hours(timeRangeHours);
    
    if (format == "json") {
        std::ostringstream json;
        json << "{\n";
        json << "  \"exportTimestamp\": \"" << std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << "\",\n";
        json << "  \"timeRangeHours\": " << timeRangeHours << ",\n";
        
        // Export issues
        json << "  \"issues\": [\n";
        {
            std::lock_guard<std::mutex> lock(issuesMutex_);
            
            std::vector<DiagnosticIssue> recentIssues;
            for (const auto& pair : activeIssues_) {
                if (pair.second.timestamp >= cutoffTime) {
                    recentIssues.push_back(pair.second);
                }
            }
            for (const auto& pair : resolvedIssues_) {
                if (pair.second.timestamp >= cutoffTime) {
                    recentIssues.push_back(pair.second);
                }
            }
            
            for (size_t i = 0; i < recentIssues.size(); ++i) {
                const auto& issue = recentIssues[i];
                json << "    {\n";
                json << "      \"issueId\": \"" << issue.issueId << "\",\n";
                json << "      \"type\": " << static_cast<int>(issue.type) << ",\n";
                json << "      \"severity\": " << static_cast<int>(issue.severity) << ",\n";
                json << "      \"component\": \"" << issue.component << "\",\n";
                json << "      \"description\": \"" << issue.description << "\",\n";
                json << "      \"details\": \"" << issue.details << "\",\n";
                json << "      \"resolved\": " << (issue.resolved ? "true" : "false") << ",\n";
                json << "      \"durationMs\": " << issue.getDurationMs() << "\n";
                json << "    }";
                if (i < recentIssues.size() - 1) json << ",";
                json << "\n";
            }
        }
        json << "  ],\n";
        
        // Export statistics
        json << "  \"statistics\": {\n";
        auto stats = getDiagnosticStatistics();
        size_t statIndex = 0;
        for (const auto& stat : stats) {
            json << "    \"" << stat.first << "\": " << stat.second;
            if (++statIndex < stats.size()) json << ",";
            json << "\n";
        }
        json << "  }\n";
        
        json << "}\n";
        return json.str();
    } else {
        // CSV format
        std::ostringstream csv;
        csv << "timestamp,issueId,type,severity,component,description,resolved,durationMs\n";
        
        std::lock_guard<std::mutex> lock(issuesMutex_);
        
        std::vector<DiagnosticIssue> recentIssues;
        for (const auto& pair : activeIssues_) {
            if (pair.second.timestamp >= cutoffTime) {
                recentIssues.push_back(pair.second);
            }
        }
        for (const auto& pair : resolvedIssues_) {
            if (pair.second.timestamp >= cutoffTime) {
                recentIssues.push_back(pair.second);
            }
        }
        
        for (const auto& issue : recentIssues) {
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                issue.timestamp.time_since_epoch()).count();
            
            csv << timestamp << ","
                << issue.issueId << ","
                << static_cast<int>(issue.type) << ","
                << static_cast<int>(issue.severity) << ","
                << issue.component << ","
                << "\"" << issue.description << "\","
                << (issue.resolved ? "true" : "false") << ","
                << issue.getDurationMs() << "\n";
        }
        
        return csv.str();
    }
}

void ProductionDiagnostics::registerAlertCallback(std::function<void(const DiagnosticIssue&)> callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    alertCallbacks_.push_back(callback);
}

void ProductionDiagnostics::setEnabled(bool enabled) {
    enabled_ = enabled;
    Logger::info("Production diagnostics " + std::string(enabled ? "enabled" : "disabled"));
}

void ProductionDiagnostics::startHealthMonitoring(int intervalSeconds) {
    if (healthMonitorRunning_.load()) {
        return;
    }
    
    healthMonitorInterval_ = intervalSeconds;
    healthMonitorRunning_ = true;
    
    healthMonitorThread_ = std::make_unique<std::thread>([this]() {
        Logger::info("Health monitoring started (interval: " + std::to_string(healthMonitorInterval_) + "s)");
        
        while (healthMonitorRunning_.load()) {
            try {
                performHealthCheck();
                cleanupOldData();
                
                std::this_thread::sleep_for(std::chrono::seconds(healthMonitorInterval_));
            } catch (const std::exception& e) {
                Logger::warn("Error in health monitoring: " + std::string(e.what()));
            }
        }
        
        Logger::info("Health monitoring stopped");
    });
}

void ProductionDiagnostics::stopHealthMonitoring() {
    if (!healthMonitorRunning_.load()) {
        return;
    }
    
    healthMonitorRunning_ = false;
    
    if (healthMonitorThread_ && healthMonitorThread_->joinable()) {
        healthMonitorThread_->join();
    }
    
    healthMonitorThread_.reset();
}

void ProductionDiagnostics::cleanup() {
    if (!initialized_.load()) {
        return;
    }
    
    Logger::info("Cleaning up production diagnostics");
    
    stopHealthMonitoring();
    
    // Clear all data
    {
        std::lock_guard<std::mutex> lock(issuesMutex_);
        activeIssues_.clear();
        resolvedIssues_.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(alertRulesMutex_);
        alertRules_.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(baselinesMutex_);
        performanceBaselines_.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(trendsMutex_);
        diagnosticTrends_.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        alertCallbacks_.clear();
    }
    
    initialized_ = false;
}

void ProductionDiagnostics::processMetric(const std::string& metricName, double value, const std::string& component) {
    // This method can be extended to perform additional metric processing
    // For now, it's a placeholder for future enhancements
}

void ProductionDiagnostics::checkAlertRules(const std::string& metricName, double value, const std::string& component) {
    std::lock_guard<std::mutex> lock(alertRulesMutex_);
    
    for (auto& pair : alertRules_) {
        auto& rule = pair.second;
        
        if (rule.metricName == metricName && rule.shouldTrigger(value)) {
            rule.trigger();
            alertsTriggered_++;
            
            // Create diagnostic issue
            std::string description = "Alert triggered: " + rule.ruleName;
            std::string details = "Metric: " + metricName + ", Value: " + std::to_string(value) + 
                                 ", Threshold: " + std::to_string(rule.threshold) + 
                                 ", Condition: " + rule.condition;
            
            DiagnosticType issueType = DiagnosticType::SYSTEM_HEALTH;
            if (metricName.find("latency") != std::string::npos) {
                issueType = DiagnosticType::LATENCY_SPIKE;
            } else if (metricName.find("error") != std::string::npos) {
                issueType = DiagnosticType::ERROR_RATE_SPIKE;
            } else if (metricName.find("memory") != std::string::npos) {
                issueType = DiagnosticType::RESOURCE_EXHAUSTION;
            }
            
            reportIssue(issueType, rule.severity, component.empty() ? "AlertSystem" : component, 
                       description, details);
        }
    }
}

void ProductionDiagnostics::performHealthCheck() {
    // Get current system health metrics
    auto healthSummary = getSystemHealthSummary();
    
    // Check for critical health issues
    if (healthSummary["active_critical_issues"] > 0) {
        Logger::warn("Health check: " + std::to_string(static_cast<int>(healthSummary["active_critical_issues"])) + 
                    " critical issues detected");
    }
    
    if (healthSummary["overall_health_score"] < 0.5) {
        reportIssue(DiagnosticType::SYSTEM_HEALTH, DiagnosticSeverity::WARNING,
                   "HealthMonitor", "System health score below threshold",
                   "Health score: " + std::to_string(healthSummary["overall_health_score"]));
    }
    
    // Check performance metrics from PerformanceMonitor
    auto& perfMonitor = PerformanceMonitor::getInstance();
    if (perfMonitor.isEnabled()) {
        auto sttMetrics = perfMonitor.getSTTMetrics(5); // Last 5 minutes
        
        // Check STT latency
        if (sttMetrics.find("overall_latency") != sttMetrics.end()) {
            auto latencyStats = sttMetrics["overall_latency"];
            if (latencyStats.count > 0 && latencyStats.p95 > 3000.0) { // 3 second P95
                reportIssue(DiagnosticType::LATENCY_SPIKE, DiagnosticSeverity::WARNING,
                           "STT", "High STT latency detected",
                           "P95 latency: " + std::to_string(latencyStats.p95) + "ms");
            }
        }
        
        // Check confidence scores
        if (sttMetrics.find("confidence_score") != sttMetrics.end()) {
            auto confidenceStats = sttMetrics["confidence_score"];
            if (confidenceStats.count > 0 && confidenceStats.mean < 0.6) {
                reportIssue(DiagnosticType::ACCURACY_DROP, DiagnosticSeverity::WARNING,
                           "STT", "Low confidence scores detected",
                           "Mean confidence: " + std::to_string(confidenceStats.mean));
            }
        }
    }
}

void ProductionDiagnostics::cleanupOldData() {
    auto cutoffTime = std::chrono::steady_clock::now() - std::chrono::hours(dataRetentionHours_.load());
    
    // Clean up old resolved issues
    {
        std::lock_guard<std::mutex> lock(issuesMutex_);
        
        auto it = resolvedIssues_.begin();
        while (it != resolvedIssues_.end()) {
            if (it->second.resolvedTimestamp < cutoffTime) {
                it = resolvedIssues_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // Clean up old trend data
    {
        std::lock_guard<std::mutex> lock(trendsMutex_);
        
        for (auto& pair : diagnosticTrends_) {
            auto& trend = pair.second;
            trend.dataPoints.erase(
                std::remove_if(trend.dataPoints.begin(), trend.dataPoints.end(),
                              [cutoffTime](const auto& point) { return point.first < cutoffTime; }),
                trend.dataPoints.end());
        }
    }
}

void ProductionDiagnostics::notifyAlertCallbacks(const DiagnosticIssue& issue) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    
    for (const auto& callback : alertCallbacks_) {
        try {
            callback(issue);
        } catch (const std::exception& e) {
            Logger::warn("Error in alert callback: " + std::string(e.what()));
        }
    }
}

std::string ProductionDiagnostics::generateIssueId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::ostringstream issueId;
    issueId << "diag_";
    
    auto now = std::chrono::steady_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    issueId << timestamp << "_";
    
    // Add random hex suffix
    for (int i = 0; i < 6; ++i) {
        issueId << std::hex << dis(gen);
    }
    
    return issueId.str();
}

// AutomatedIssueDetector Implementation
AutomatedIssueDetector& AutomatedIssueDetector::getInstance() {
    static AutomatedIssueDetector instance;
    return instance;
}

AutomatedIssueDetector::~AutomatedIssueDetector() {
    cleanup();
}

bool AutomatedIssueDetector::initialize(int checkIntervalSeconds) {
    if (initialized_.load()) {
        return true;
    }
    
    checkInterval_ = checkIntervalSeconds;
    
    // Add default detection rules
    addDetectionRule("memory_leak_detection", "system.memory_usage_mb",
                    [](double value) { return value > 16384.0; }, // 16GB threshold
                    DiagnosticSeverity::CRITICAL, "Memory usage exceeds 16GB");
    
    addDetectionRule("stt_failure_detection", "stt.confidence_score",
                    [](double value) { return value < 0.3; }, // Very low confidence
                    DiagnosticSeverity::ERROR, "STT confidence below 30%");
    
    addDetectionRule("throughput_drop_detection", "stt.throughput_ops_per_sec",
                    [](double value) { return value < 0.5; }, // Very low throughput
                    DiagnosticSeverity::WARNING, "STT throughput below 0.5 ops/sec");
    
    initialized_ = true;
    Logger::info("Automated issue detector initialized (check interval: " + 
                std::to_string(checkIntervalSeconds) + "s)");
    
    return true;
}

void AutomatedIssueDetector::addDetectionRule(const std::string& ruleName, const std::string& metricName,
                                             std::function<bool(double)> detectionFunction,
                                             DiagnosticSeverity severity, const std::string& description) {
    std::lock_guard<std::mutex> lock(rulesMutex_);
    
    DetectionRule rule;
    rule.ruleName = ruleName;
    rule.metricName = metricName;
    rule.detectionFunction = detectionFunction;
    rule.severity = severity;
    rule.description = description;
    rule.enabled = true;
    
    detectionRules_[ruleName] = rule;
    
    Logger::debug("Added detection rule: " + ruleName + " for metric: " + metricName);
}

void AutomatedIssueDetector::removeDetectionRule(const std::string& ruleName) {
    std::lock_guard<std::mutex> lock(rulesMutex_);
    detectionRules_.erase(ruleName);
    
    Logger::debug("Removed detection rule: " + ruleName);
}

void AutomatedIssueDetector::startDetection() {
    if (detectionRunning_.load()) {
        return;
    }
    
    detectionRunning_ = true;
    
    detectionThread_ = std::make_unique<std::thread>([this]() {
        runDetectionLoop();
    });
    
    Logger::info("Automated issue detection started");
}

void AutomatedIssueDetector::stopDetection() {
    if (!detectionRunning_.load()) {
        return;
    }
    
    detectionRunning_ = false;
    detectionCondition_.notify_all();
    
    if (detectionThread_ && detectionThread_->joinable()) {
        detectionThread_->join();
    }
    
    detectionThread_.reset();
    Logger::info("Automated issue detection stopped");
}

void AutomatedIssueDetector::triggerDetectionCheck() {
    detectionCondition_.notify_all();
}

std::map<std::string, double> AutomatedIssueDetector::getDetectionStatistics() const {
    std::map<std::string, double> stats;
    
    stats["total_checks"] = static_cast<double>(totalChecks_.load());
    stats["issues_detected"] = static_cast<double>(issuesDetected_.load());
    stats["false_positives"] = static_cast<double>(falsePositives_.load());
    
    {
        std::lock_guard<std::mutex> lock(rulesMutex_);
        stats["detection_rules_count"] = static_cast<double>(detectionRules_.size());
        
        int enabledRules = 0;
        for (const auto& rule : detectionRules_) {
            if (rule.second.enabled) enabledRules++;
        }
        stats["enabled_detection_rules"] = static_cast<double>(enabledRules);
    }
    
    if (totalChecks_.load() > 0) {
        stats["detection_rate"] = static_cast<double>(issuesDetected_.load()) / totalChecks_.load();
    } else {
        stats["detection_rate"] = 0.0;
    }
    
    return stats;
}

void AutomatedIssueDetector::cleanup() {
    if (!initialized_.load()) {
        return;
    }
    
    Logger::info("Cleaning up automated issue detector");
    
    stopDetection();
    
    {
        std::lock_guard<std::mutex> lock(rulesMutex_);
        detectionRules_.clear();
    }
    
    initialized_ = false;
}

void AutomatedIssueDetector::runDetectionLoop() {
    Logger::info("Detection loop started");
    
    while (detectionRunning_.load()) {
        try {
            checkDetectionRules();
            
            std::unique_lock<std::mutex> lock(detectionMutex_);
            detectionCondition_.wait_for(lock, std::chrono::seconds(checkInterval_),
                                        [this] { return !detectionRunning_.load(); });
        } catch (const std::exception& e) {
            Logger::warn("Error in detection loop: " + std::string(e.what()));
        }
    }
    
    Logger::info("Detection loop stopped");
}

void AutomatedIssueDetector::checkDetectionRules() {
    totalChecks_++;
    
    auto& perfMonitor = PerformanceMonitor::getInstance();
    if (!perfMonitor.isEnabled()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(rulesMutex_);
    
    for (auto& pair : detectionRules_) {
        auto& rule = pair.second;
        
        if (!rule.enabled) {
            continue;
        }
        
        // Check cooldown period
        auto now = std::chrono::steady_clock::now();
        if (now - rule.lastTriggered < rule.cooldownPeriod) {
            continue;
        }
        
        // Get recent metric data
        auto recentMetrics = perfMonitor.getRecentMetrics(rule.metricName, 10);
        if (recentMetrics.empty()) {
            continue;
        }
        
        // Use the most recent value
        double currentValue = recentMetrics.back().value;
        
        try {
            if (rule.detectionFunction(currentValue)) {
                rule.lastTriggered = now;
                issuesDetected_++;
                
                // Report the detected issue
                auto& diagnostics = ProductionDiagnostics::getInstance();
                std::string description = "Automated detection: " + rule.description;
                std::string details = "Rule: " + rule.ruleName + ", Metric: " + rule.metricName + 
                                     ", Value: " + std::to_string(currentValue);
                
                diagnostics.reportIssue(DiagnosticType::SYSTEM_HEALTH, rule.severity,
                                       "AutomatedDetector", description, details);
                
                Logger::warn("Automated issue detected: " + rule.ruleName + 
                           " (value: " + std::to_string(currentValue) + ")");
            }
        } catch (const std::exception& e) {
            Logger::warn("Error in detection rule " + rule.ruleName + ": " + std::string(e.what()));
        }
    }
}

} // namespace utils
} // namespace speechrnt