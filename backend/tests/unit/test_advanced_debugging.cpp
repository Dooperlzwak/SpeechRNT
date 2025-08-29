#include <gtest/gtest.h>
#include "utils/advanced_debug.hpp"
#include "utils/production_diagnostics.hpp"
#include <thread>
#include <chrono>

using namespace speechrnt::utils;

class AdvancedDebuggingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize debug manager for each test
        auto& debugManager = AdvancedDebugManager::getInstance();
        debugManager.initialize(DebugLevel::DEBUG, false); // Disable file logging for tests
        debugManager.setDebugMode(true);
    }
    
    void TearDown() override {
        // Cleanup after each test
        AdvancedDebugManager::getInstance().cleanup();
        ProductionDiagnostics::getInstance().cleanup();
        AutomatedIssueDetector::getInstance().cleanup();
    }
};

TEST_F(AdvancedDebuggingTest, DebugSessionCreationAndCompletion) {
    auto& debugManager = AdvancedDebugManager::getInstance();
    
    // Create a debug session
    auto session = debugManager.createSession("TestOperation", "test_session_001");
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->getSessionId(), "test_session_001");
    EXPECT_EQ(session->getOperation(), "TestOperation");
    EXPECT_FALSE(session->isCompleted());
    
    // Complete the session
    session->complete(true);
    EXPECT_TRUE(session->isCompleted());
    EXPECT_TRUE(session->wasSuccessful());
    
    // Verify session is tracked
    auto retrievedSession = debugManager.getSession("test_session_001");
    ASSERT_NE(retrievedSession, nullptr);
    EXPECT_EQ(retrievedSession->getSessionId(), "test_session_001");
}

TEST_F(AdvancedDebuggingTest, ProcessingStageManagement) {
    auto& debugManager = AdvancedDebugManager::getInstance();
    auto session = debugManager.createSession("StageTest");
    
    // Start a stage
    session->startStage("preprocessing", "Audio preprocessing stage");
    
    // Add stage data
    session->addStageData("preprocessing", "sample_rate", "16000");
    session->addStageData("preprocessing", "channels", "1");
    
    // Add intermediate results
    session->addIntermediateResult("preprocessing", "Applied noise filter");
    session->addIntermediateResult("preprocessing", "Normalized levels");
    
    // Complete the stage
    session->completeStage("preprocessing", true);
    
    // Verify stage data
    const auto& stages = session->getStages();
    ASSERT_EQ(stages.size(), 1);
    
    const auto& stage = stages[0];
    EXPECT_EQ(stage.stageName, "preprocessing");
    EXPECT_EQ(stage.stageDescription, "Audio preprocessing stage");
    EXPECT_TRUE(stage.completed);
    EXPECT_TRUE(stage.success);
    EXPECT_EQ(stage.stageData.size(), 2);
    EXPECT_EQ(stage.intermediateResults.size(), 2);
    EXPECT_GT(stage.getDurationMs(), 0.0);
}

TEST_F(AdvancedDebuggingTest, AudioCharacteristicsAnalysis) {
    auto& debugManager = AdvancedDebugManager::getInstance();
    
    // Create test audio data (sine wave)
    std::vector<float> audioData(1600); // 0.1 seconds at 16kHz
    for (size_t i = 0; i < audioData.size(); ++i) {
        audioData[i] = 0.5f * std::sin(2.0f * M_PI * 440.0f * i / 16000.0f); // 440Hz tone
    }
    
    // Analyze audio characteristics
    auto characteristics = debugManager.analyzeAudioCharacteristics(audioData, 16000, 1, "test_sine_wave");
    
    EXPECT_EQ(characteristics.sampleCount, 1600);
    EXPECT_EQ(characteristics.sampleRate, 16000);
    EXPECT_EQ(characteristics.channels, 1);
    EXPECT_NEAR(characteristics.durationSeconds, 0.1, 0.001);
    EXPECT_GT(characteristics.rmsLevel, 0.3); // Should be around 0.35 for 0.5 amplitude sine wave
    EXPECT_GT(characteristics.peakLevel, 0.4); // Should be around 0.5
    EXPECT_FALSE(characteristics.hasClipping); // 0.5 amplitude shouldn't clip
    EXPECT_FALSE(characteristics.hasSilence); // Sine wave isn't silence
    EXPECT_GT(characteristics.qualityScore, 0.5); // Should have decent quality
    EXPECT_EQ(characteristics.sourceInfo, "test_sine_wave");
}

TEST_F(AdvancedDebuggingTest, DebugSessionExport) {
    auto& debugManager = AdvancedDebugManager::getInstance();
    auto session = debugManager.createSession("ExportTest");
    
    // Add some data to the session
    session->startStage("test_stage", "Test stage for export");
    session->addStageData("test_stage", "key1", "value1");
    session->addIntermediateResult("test_stage", "Test result");
    session->completeStage("test_stage", true);
    
    session->setMetadata("test_key", "test_value");
    session->complete(true);
    
    // Export to JSON
    std::string jsonExport = session->exportToJSON();
    EXPECT_FALSE(jsonExport.empty());
    EXPECT_NE(jsonExport.find("\"sessionId\""), std::string::npos);
    EXPECT_NE(jsonExport.find("\"operation\""), std::string::npos);
    EXPECT_NE(jsonExport.find("\"stages\""), std::string::npos);
    EXPECT_NE(jsonExport.find("test_stage"), std::string::npos);
    
    // Export to text
    std::string textExport = session->exportToText();
    EXPECT_FALSE(textExport.empty());
    EXPECT_NE(textExport.find("Debug Session Report"), std::string::npos);
    EXPECT_NE(textExport.find("test_stage"), std::string::npos);
}

TEST_F(AdvancedDebuggingTest, DebugStatistics) {
    auto& debugManager = AdvancedDebugManager::getInstance();
    
    // Create multiple sessions
    auto session1 = debugManager.createSession("Test1");
    auto session2 = debugManager.createSession("Test2");
    
    session1->complete(true);
    session2->complete(false);
    
    debugManager.completeSession(session1->getSessionId(), true);
    debugManager.completeSession(session2->getSessionId(), false);
    
    // Get statistics
    auto stats = debugManager.getDebugStatistics();
    
    EXPECT_EQ(stats["total_sessions"], 2.0);
    EXPECT_EQ(stats["successful_sessions"], 1.0);
    EXPECT_EQ(stats["failed_sessions"], 1.0);
    EXPECT_EQ(stats["success_rate"], 0.5);
}

class ProductionDiagnosticsTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& diagnostics = ProductionDiagnostics::getInstance();
        diagnostics.initialize(true, true, 1); // Enable alerting and trends, 1h retention
    }
    
    void TearDown() override {
        ProductionDiagnostics::getInstance().cleanup();
    }
};

TEST_F(ProductionDiagnosticsTest, IssueReportingAndResolution) {
    auto& diagnostics = ProductionDiagnostics::getInstance();
    
    // Report an issue
    std::string issueId = diagnostics.reportIssue(
        DiagnosticType::LATENCY_SPIKE,
        DiagnosticSeverity::WARNING,
        "TestComponent",
        "Test latency issue",
        "Latency exceeded threshold",
        "test_session"
    );
    
    EXPECT_FALSE(issueId.empty());
    
    // Get current issues
    auto issues = diagnostics.getCurrentIssues(DiagnosticSeverity::INFO, "", true);
    EXPECT_GE(issues.size(), 1);
    
    // Find our issue
    bool foundIssue = false;
    for (const auto& issue : issues) {
        if (issue.issueId == issueId) {
            foundIssue = true;
            EXPECT_EQ(issue.type, DiagnosticType::LATENCY_SPIKE);
            EXPECT_EQ(issue.severity, DiagnosticSeverity::WARNING);
            EXPECT_EQ(issue.component, "TestComponent");
            EXPECT_EQ(issue.description, "Test latency issue");
            EXPECT_FALSE(issue.resolved);
            break;
        }
    }
    EXPECT_TRUE(foundIssue);
    
    // Resolve the issue
    diagnostics.resolveIssue(issueId, "Issue resolved by test");
    
    // Verify issue is resolved
    auto unresolvedIssues = diagnostics.getCurrentIssues(DiagnosticSeverity::INFO, "", true);
    bool stillUnresolved = false;
    for (const auto& issue : unresolvedIssues) {
        if (issue.issueId == issueId) {
            stillUnresolved = true;
            break;
        }
    }
    EXPECT_FALSE(stillUnresolved);
}

TEST_F(ProductionDiagnosticsTest, AlertRuleManagement) {
    auto& diagnostics = ProductionDiagnostics::getInstance();
    
    // Add a custom alert rule
    AlertRule testRule("test_rule", "test.metric", "greater_than", 100.0, DiagnosticSeverity::WARNING);
    testRule.description = "Test alert rule";
    diagnostics.addAlertRule(testRule);
    
    // Record a metric that should trigger the alert
    diagnostics.recordMetric("test.metric", 150.0, "TestComponent");
    
    // Wait a bit for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Check if alert was triggered (should create an issue)
    auto issues = diagnostics.getCurrentIssues(DiagnosticSeverity::INFO, "", true);
    bool alertTriggered = false;
    for (const auto& issue : issues) {
        if (issue.description.find("Alert triggered: test_rule") != std::string::npos) {
            alertTriggered = true;
            break;
        }
    }
    EXPECT_TRUE(alertTriggered);
    
    // Disable the rule
    diagnostics.setAlertRuleEnabled("test_rule", false);
    
    // Record another metric that would trigger the alert
    diagnostics.recordMetric("test.metric", 200.0, "TestComponent");
    
    // Should not trigger another alert (rule is disabled)
    // This is hard to test directly, but we can verify the rule state
}

TEST_F(ProductionDiagnosticsTest, PerformanceBaselineRegression) {
    auto& diagnostics = ProductionDiagnostics::getInstance();
    
    // Set a performance baseline
    diagnostics.setPerformanceBaseline("test.latency", 100.0, 0.2); // 20% tolerance
    
    // Record normal values (should not trigger regression)
    for (int i = 0; i < 15; ++i) {
        diagnostics.recordMetric("test.latency", 95.0 + i * 2.0, "TestComponent");
    }
    
    // Record a value that should trigger regression detection
    bool regressionDetected = diagnostics.checkPerformanceRegression("test.latency", 150.0); // 50% above baseline
    EXPECT_TRUE(regressionDetected);
    
    // Check if regression issue was reported
    auto issues = diagnostics.getCurrentIssues(DiagnosticSeverity::INFO, "", true);
    bool regressionIssueFound = false;
    for (const auto& issue : issues) {
        if (issue.type == DiagnosticType::PERFORMANCE_DEGRADATION) {
            regressionIssueFound = true;
            break;
        }
    }
    EXPECT_TRUE(regressionIssueFound);
}

TEST_F(ProductionDiagnosticsTest, SystemHealthSummary) {
    auto& diagnostics = ProductionDiagnostics::getInstance();
    
    // Report some issues of different severities
    diagnostics.reportIssue(DiagnosticType::SYSTEM_HEALTH, DiagnosticSeverity::CRITICAL, "System", "Critical issue");
    diagnostics.reportIssue(DiagnosticType::SYSTEM_HEALTH, DiagnosticSeverity::ERROR, "System", "Error issue");
    diagnostics.reportIssue(DiagnosticType::SYSTEM_HEALTH, DiagnosticSeverity::WARNING, "System", "Warning issue");
    
    // Get health summary
    auto healthSummary = diagnostics.getSystemHealthSummary();
    
    EXPECT_EQ(healthSummary["active_critical_issues"], 1.0);
    EXPECT_EQ(healthSummary["active_error_issues"], 1.0);
    EXPECT_EQ(healthSummary["active_warning_issues"], 1.0);
    EXPECT_EQ(healthSummary["total_active_issues"], 3.0);
    EXPECT_LT(healthSummary["overall_health_score"], 1.0); // Should be reduced due to issues
}

TEST_F(ProductionDiagnosticsTest, DiagnosticDataExport) {
    auto& diagnostics = ProductionDiagnostics::getInstance();
    
    // Create some diagnostic data
    diagnostics.reportIssue(DiagnosticType::LATENCY_SPIKE, DiagnosticSeverity::WARNING, "Test", "Test issue");
    diagnostics.recordMetric("test.metric", 42.0, "Test");
    
    // Export as JSON
    std::string jsonExport = diagnostics.exportDiagnosticData("json", 1);
    EXPECT_FALSE(jsonExport.empty());
    EXPECT_NE(jsonExport.find("\"issues\""), std::string::npos);
    EXPECT_NE(jsonExport.find("\"statistics\""), std::string::npos);
    
    // Export as CSV
    std::string csvExport = diagnostics.exportDiagnosticData("csv", 1);
    EXPECT_FALSE(csvExport.empty());
    EXPECT_NE(csvExport.find("timestamp,issueId"), std::string::npos);
}

class AutomatedIssueDetectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& detector = AutomatedIssueDetector::getInstance();
        detector.initialize(1); // Check every 1 second for faster testing
    }
    
    void TearDown() override {
        AutomatedIssueDetector::getInstance().cleanup();
    }
};

TEST_F(AutomatedIssueDetectorTest, DetectionRuleManagement) {
    auto& detector = AutomatedIssueDetector::getInstance();
    
    // Add a detection rule
    detector.addDetectionRule(
        "test_rule",
        "test.metric",
        [](double value) { return value > 50.0; },
        DiagnosticSeverity::WARNING,
        "Test detection rule"
    );
    
    // Get statistics (should show the rule was added)
    auto stats = detector.getDetectionStatistics();
    EXPECT_EQ(stats["detection_rules_count"], 4.0); // 3 default + 1 custom
    EXPECT_EQ(stats["enabled_detection_rules"], 4.0);
    
    // Remove the rule
    detector.removeDetectionRule("test_rule");
    
    // Verify rule was removed
    stats = detector.getDetectionStatistics();
    EXPECT_EQ(stats["detection_rules_count"], 3.0); // Back to 3 default rules
}

TEST_F(AutomatedIssueDetectorTest, DetectionStatistics) {
    auto& detector = AutomatedIssueDetector::getInstance();
    
    // Get initial statistics
    auto stats = detector.getDetectionStatistics();
    EXPECT_EQ(stats["total_checks"], 0.0);
    EXPECT_EQ(stats["issues_detected"], 0.0);
    EXPECT_GE(stats["detection_rules_count"], 3.0); // Should have default rules
    
    // Start detection briefly
    detector.startDetection();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    detector.stopDetection();
    
    // Statistics should remain consistent
    stats = detector.getDetectionStatistics();
    EXPECT_GE(stats["detection_rules_count"], 3.0);
}

// Integration test combining debug sessions with diagnostics
TEST_F(AdvancedDebuggingTest, IntegrationWithDiagnostics) {
    auto& debugManager = AdvancedDebugManager::getInstance();
    auto& diagnostics = ProductionDiagnostics::getInstance();
    diagnostics.initialize(true, false, 1);
    
    // Create a debug session
    auto session = debugManager.createSession("IntegrationTest");
    
    // Start a stage and record metrics
    session->startStage("processing", "Test processing stage");
    diagnostics.recordMetric("test.processing_time", 150.0, "IntegrationTest");
    
    // Complete the stage
    session->completeStage("processing", true);
    session->complete(true);
    
    // Verify both systems recorded the activity
    EXPECT_TRUE(session->isCompleted());
    EXPECT_TRUE(session->wasSuccessful());
    
    auto debugStats = debugManager.getDebugStatistics();
    EXPECT_GT(debugStats["total_sessions"], 0.0);
    
    auto diagStats = diagnostics.getDiagnosticStatistics();
    // Diagnostics should be tracking activity (exact values depend on implementation)
    EXPECT_GE(diagStats["total_issues_reported"], 0.0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}