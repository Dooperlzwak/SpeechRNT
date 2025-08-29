#include "utils/advanced_debug.hpp"
#include "utils/production_diagnostics.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

using namespace speechrnt::utils;

/**
 * Example demonstrating advanced debugging features
 */
void demonstrateAdvancedDebugging() {
    std::cout << "\n=== Advanced Debugging Example ===\n";
    
    // Initialize the debug manager
    auto& debugManager = AdvancedDebugManager::getInstance();
    debugManager.initialize(DebugLevel::DEBUG, true, "debug_logs");
    debugManager.setDebugMode(true);
    
    // Register a debug callback
    debugManager.registerDebugCallback([](const std::string& component, DebugLevel level, const std::string& message) {
        std::cout << "[CALLBACK] " << component << ": " << message << std::endl;
    });
    
    // Create a debug session for STT processing
    auto session = debugManager.createSession("STT_Processing", "stt_session_001");
    
    // Simulate STT processing stages
    session->startStage("audio_preprocessing", "Preprocessing incoming audio data");
    session->addStageData("audio_preprocessing", "sample_rate", "16000");
    session->addStageData("audio_preprocessing", "channels", "1");
    session->addStageData("audio_preprocessing", "duration_ms", "2500");
    
    // Simulate some processing time
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    session->addIntermediateResult("audio_preprocessing", "Applied noise reduction filter");
    session->addIntermediateResult("audio_preprocessing", "Normalized audio levels");
    session->completeStage("audio_preprocessing", true);
    
    // Start VAD stage
    session->startStage("voice_activity_detection", "Detecting speech segments");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    session->addStageData("voice_activity_detection", "speech_probability", "0.85");
    session->addIntermediateResult("voice_activity_detection", "Speech detected from 0.2s to 2.3s");
    session->completeStage("voice_activity_detection", true);
    
    // Start transcription stage
    session->startStage("transcription", "Converting speech to text");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    session->addStageData("transcription", "model", "whisper-base");
    session->addStageData("transcription", "language", "en");
    session->addIntermediateResult("transcription", "Partial: Hello world");
    session->addIntermediateResult("transcription", "Final: Hello world, this is a test");
    session->completeStage("transcription", true);
    
    // Analyze audio characteristics
    std::vector<float> sampleAudio(16000, 0.0f); // 1 second of silence
    for (size_t i = 0; i < sampleAudio.size(); ++i) {
        sampleAudio[i] = 0.1f * std::sin(2.0f * M_PI * 440.0f * i / 16000.0f); // 440Hz tone
    }
    
    auto audioCharacteristics = debugManager.analyzeAudioCharacteristics(sampleAudio, 16000, 1, "test_audio");
    session->setAudioCharacteristics(audioCharacteristics);
    session->addAudioSample(sampleAudio, "original_audio");
    
    // Log some debug messages
    session->logInfo("STT processing completed successfully");
    session->logDebug("Confidence score: 0.92");
    
    // Complete the session
    session->complete(true);
    debugManager.completeSession("stt_session_001", true);
    
    // Export debug data
    std::cout << "\n--- Debug Session Export (JSON) ---\n";
    std::cout << session->exportToJSON().substr(0, 500) << "...\n";
    
    // Get debug statistics
    auto debugStats = debugManager.getDebugStatistics();
    std::cout << "\n--- Debug Statistics ---\n";
    for (const auto& stat : debugStats) {
        std::cout << stat.first << ": " << stat.second << std::endl;
    }
    
    // Save session to file
    session->saveToFile("debug_session_example.json", "json");
    std::cout << "\nDebug session saved to debug_session_example.json\n";
}

/**
 * Example demonstrating production diagnostics
 */
void demonstrateProductionDiagnostics() {
    std::cout << "\n=== Production Diagnostics Example ===\n";
    
    // Initialize production diagnostics
    auto& diagnostics = ProductionDiagnostics::getInstance();
    diagnostics.initialize(true, true, 24); // Enable alerting and trend analysis, 24h retention
    
    // Register alert callback
    diagnostics.registerAlertCallback([](const DiagnosticIssue& issue) {
        std::cout << "[ALERT] " << issue.component << ": " << issue.description 
                  << " (Severity: " << static_cast<int>(issue.severity) << ")" << std::endl;
    });
    
    // Add custom alert rules
    AlertRule customRule("custom_latency_alert", "stt.processing_latency", "greater_than", 1500.0, DiagnosticSeverity::WARNING);
    customRule.description = "STT processing latency exceeds 1.5 seconds";
    diagnostics.addAlertRule(customRule);
    
    // Set performance baselines
    diagnostics.setPerformanceBaseline("stt.processing_latency", 800.0, 0.25); // 25% tolerance
    diagnostics.setPerformanceBaseline("stt.confidence_score", 0.85, 0.15); // 15% tolerance
    
    // Simulate recording metrics that trigger alerts
    std::cout << "\nSimulating metrics that will trigger alerts...\n";
    
    // Record normal metrics first
    diagnostics.recordMetric("stt.processing_latency", 750.0, "STT");
    diagnostics.recordMetric("stt.confidence_score", 0.88, "STT");
    diagnostics.recordMetric("system.memory_usage_mb", 4096.0, "System");
    
    // Record metrics that will trigger alerts
    diagnostics.recordMetric("stt.processing_latency", 2500.0, "STT"); // Will trigger alert
    diagnostics.recordMetric("stt.confidence_score", 0.45, "STT"); // Will trigger alert
    diagnostics.recordMetric("system.memory_usage_mb", 10240.0, "System"); // Will trigger alert
    
    // Manually report some issues
    std::string issueId1 = diagnostics.reportIssue(
        DiagnosticType::AUDIO_QUALITY_ISSUE,
        DiagnosticSeverity::WARNING,
        "AudioProcessor",
        "Poor audio quality detected",
        "SNR below threshold: 8.5dB",
        "session_123"
    );
    
    std::string issueId2 = diagnostics.reportIssue(
        DiagnosticType::MODEL_PERFORMANCE,
        DiagnosticSeverity::ERROR,
        "WhisperSTT",
        "Model inference failure",
        "CUDA out of memory error",
        "session_124"
    );
    
    // Wait a bit for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Get current issues
    auto currentIssues = diagnostics.getCurrentIssues(DiagnosticSeverity::INFO, "", true);
    std::cout << "\n--- Current Issues (" << currentIssues.size() << ") ---\n";
    for (const auto& issue : currentIssues) {
        std::cout << "ID: " << issue.issueId << std::endl;
        std::cout << "  Component: " << issue.component << std::endl;
        std::cout << "  Description: " << issue.description << std::endl;
        std::cout << "  Severity: " << static_cast<int>(issue.severity) << std::endl;
        std::cout << "  Duration: " << issue.getDurationMs() << "ms" << std::endl;
        std::cout << std::endl;
    }
    
    // Resolve some issues
    diagnostics.resolveIssue(issueId1, "Audio quality improved after filter adjustment");
    diagnostics.resolveIssue(issueId2, "Restarted with smaller batch size");
    
    // Get system health summary
    auto healthSummary = diagnostics.getSystemHealthSummary();
    std::cout << "--- System Health Summary ---\n";
    for (const auto& metric : healthSummary) {
        std::cout << metric.first << ": " << metric.second << std::endl;
    }
    
    // Get diagnostic statistics
    auto diagStats = diagnostics.getDiagnosticStatistics();
    std::cout << "\n--- Diagnostic Statistics ---\n";
    for (const auto& stat : diagStats) {
        std::cout << stat.first << ": " << stat.second << std::endl;
    }
    
    // Export diagnostic data
    std::cout << "\n--- Diagnostic Data Export (JSON) ---\n";
    std::string exportData = diagnostics.exportDiagnosticData("json", 1);
    std::cout << exportData.substr(0, 500) << "...\n";
}

/**
 * Example demonstrating automated issue detection
 */
void demonstrateAutomatedIssueDetection() {
    std::cout << "\n=== Automated Issue Detection Example ===\n";
    
    // Initialize automated issue detector
    auto& detector = AutomatedIssueDetector::getInstance();
    detector.initialize(5); // Check every 5 seconds
    
    // Add custom detection rules
    detector.addDetectionRule(
        "high_error_rate",
        "errors.count",
        [](double value) { return value > 5.0; }, // More than 5 errors
        DiagnosticSeverity::ERROR,
        "Error rate exceeds threshold"
    );
    
    detector.addDetectionRule(
        "memory_pressure",
        "system.memory_usage_mb",
        [](double value) { return value > 8192.0; }, // More than 8GB
        DiagnosticSeverity::WARNING,
        "Memory usage is high"
    );
    
    detector.addDetectionRule(
        "confidence_degradation",
        "stt.confidence_score",
        [](double value) { return value < 0.6; }, // Below 60%
        DiagnosticSeverity::WARNING,
        "STT confidence is degrading"
    );
    
    // Start detection
    detector.startDetection();
    
    std::cout << "Automated issue detection started. Simulating metrics...\n";
    
    // Simulate metrics over time
    auto& diagnostics = ProductionDiagnostics::getInstance();
    
    for (int i = 0; i < 10; ++i) {
        // Simulate gradually increasing error rate
        double errorRate = i * 0.8;
        diagnostics.recordMetric("errors.count", errorRate, "System");
        
        // Simulate memory pressure
        double memoryUsage = 6000.0 + i * 300.0;
        diagnostics.recordMetric("system.memory_usage_mb", memoryUsage, "System");
        
        // Simulate degrading confidence
        double confidence = 0.9 - i * 0.05;
        diagnostics.recordMetric("stt.confidence_score", confidence, "STT");
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    
    // Trigger manual detection check
    detector.triggerDetectionCheck();
    
    // Wait for detection to process
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Get detection statistics
    auto detectionStats = detector.getDetectionStatistics();
    std::cout << "\n--- Detection Statistics ---\n";
    for (const auto& stat : detectionStats) {
        std::cout << stat.first << ": " << stat.second << std::endl;
    }
    
    // Stop detection
    detector.stopDetection();
    std::cout << "\nAutomated issue detection stopped.\n";
}

/**
 * Example demonstrating integration with existing performance monitoring
 */
void demonstrateIntegrationExample() {
    std::cout << "\n=== Integration Example ===\n";
    
    // This example shows how the advanced debugging and diagnostics
    // integrate with the existing performance monitoring system
    
    auto& debugManager = AdvancedDebugManager::getInstance();
    auto& diagnostics = ProductionDiagnostics::getInstance();
    
    // Create a debug session for a complex operation
    auto session = debugManager.createSession("ComplexSTTOperation");
    
    // Start monitoring health
    diagnostics.startHealthMonitoring(10); // Check every 10 seconds
    
    // Simulate a complex STT operation with multiple stages
    session->startStage("initialization", "Setting up STT pipeline");
    session->addStageData("initialization", "model_path", "/models/whisper-large");
    session->addStageData("initialization", "gpu_enabled", "true");
    
    // Simulate some processing time and record metrics
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    diagnostics.recordMetric("stt.model_load_time_ms", 150.0, "STT");
    session->completeStage("initialization", true);
    
    // Audio processing stage
    session->startStage("audio_processing", "Processing audio input");
    std::this_thread::sleep_for(std::chrono::milliseconds(75));
    diagnostics.recordMetric("stt.preprocessing_latency_ms", 75.0, "STT");
    session->addIntermediateResult("audio_processing", "Applied noise reduction");
    session->completeStage("audio_processing", true);
    
    // Inference stage
    session->startStage("inference", "Running STT inference");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    diagnostics.recordMetric("stt.inference_latency_ms", 300.0, "STT");
    diagnostics.recordMetric("stt.confidence_score", 0.87, "STT");
    session->addIntermediateResult("inference", "Transcription: Hello, this is a test message");
    session->completeStage("inference", true);
    
    // Post-processing stage
    session->startStage("post_processing", "Post-processing results");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    diagnostics.recordMetric("stt.postprocessing_latency_ms", 25.0, "STT");
    session->completeStage("post_processing", true);
    
    // Complete the session
    session->complete(true);
    debugManager.completeSession(session->getSessionId(), true);
    
    // Record overall latency
    double totalLatency = session->getTotalDurationMs();
    diagnostics.recordMetric("stt.latency_ms", totalLatency, "STT");
    
    std::cout << "Complex STT operation completed in " << totalLatency << "ms\n";
    
    // Check if this triggered any performance regressions
    bool regression = diagnostics.checkPerformanceRegression("stt.latency_ms", totalLatency);
    if (regression) {
        std::cout << "Performance regression detected!\n";
    }
    
    // Get final health summary
    auto healthSummary = diagnostics.getSystemHealthSummary();
    std::cout << "\nFinal health score: " << healthSummary["overall_health_score"] << std::endl;
    
    // Stop health monitoring
    diagnostics.stopHealthMonitoring();
}

int main() {
    try {
        // Initialize logging
        Logger::initialize();
        
        std::cout << "Advanced STT Debugging and Diagnostics Example\n";
        std::cout << "===============================================\n";
        
        // Run examples
        demonstrateAdvancedDebugging();
        demonstrateProductionDiagnostics();
        demonstrateAutomatedIssueDetection();
        demonstrateIntegrationExample();
        
        std::cout << "\n=== Example Complete ===\n";
        std::cout << "Check the debug_logs directory for detailed debug output.\n";
        std::cout << "The debug_session_example.json file contains the exported debug session.\n";
        
        // Cleanup
        AdvancedDebugManager::getInstance().cleanup();
        ProductionDiagnostics::getInstance().cleanup();
        AutomatedIssueDetector::getInstance().cleanup();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}