#include <gtest/gtest.h>
#include "core/translation_pipeline.hpp"
#include "core/client_session.hpp"
#include "stt/whisper_stt.hpp"
#include "mt/marian_translator.hpp"
#include "mt/language_detector.hpp"
#include "mt/gpu_accelerator.hpp"
#include "mt/quality_manager.hpp"
#include "tts/piper_tts.hpp"
#include "utils/logging.hpp"
#include "utils/performance_monitor.hpp"
#include <chrono>
#include <thread>
#include <atomic>
#include <random>
#include <memory>
#include <vector>
#include <future>

namespace speechrnt {
namespace integration {

class MTEndToEndIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging
        utils::Logger::initialize();
        utils::Logger::setLevel(utils::LogLevel::INFO);
        
        // Initialize performance monitor
        perfMonitor_ = &utils::PerformanceMonitor::getInstance();
        perfMonitor_->initialize(false); // Disable system metrics for tests
        
        // Initialize components
        setupComponents();
        generateTestData();
    }
    
    void TearDown() override {
        if (translator_) {
            translator_->cleanup();
        }
        if (languageDetector_) {
            languageDetector_->cleanup();
        }
        if (gpuAccelerator_) {
            gpuAccelerator_->cleanup();
        }
        if (perfMonitor_) {
            perfMonitor_->cleanup();
        }
    }
    
    void setupComponents() {
        // Initialize Marian Translator
        translator_ = std::make_unique<mt::MarianTranslator>();
        translator_->setModelsPath("backend/data/marian/");
        
        // Initialize Language Detector
        languageDetector_ = std::make_unique<mt::LanguageDetector>();
        languageDetector_->initialize("backend/config/language_detection.json");
        
        // Initialize GPU Accelerator
        gpuAccelerator_ = std::make_unique<mt::GPUAccelerator>();
        gpuAccelerator_->initialize();
        
        // Initialize Quality Manager
        qualityManager_ = std::make_unique<mt::QualityManager>();
        qualityManager_->initialize("backend/config/quality_assessment.json");
        
        // Initialize Translation Pipeline
        pipeline_ = std::make_unique<core::TranslationPipeline>();
        pipeline_->initialize("backend/data/");
    }
    
    void generateTestData() {
        // Generate test audio data (16kHz, mono)
        const int sampleRate = 16000;
        const int durationSeconds = 3;
        const int numSamples = sampleRate * durationSeconds;
        
        testAudioData_.resize(numSamples);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<float> noise(0.0f, 0.1f);
        
        // Generate speech-like pattern
        for (int i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            float signal = 0.3f * std::sin(2.0f * M_PI * 200.0f * t) +
                          0.2f * std::sin(2.0f * M_PI * 400.0f * t) +
                          0.1f * std::sin(2.0f * M_PI * 800.0f * t);
            
            // Add envelope to simulate speech
            float envelope = std::exp(-0.5f * t) * (1.0f + 0.5f * std::sin(10.0f * t));
            testAudioData_[i] = (signal * envelope) + noise(gen);
        }
        
        // Test phrases for different languages
        testPhrases_ = {
            {"Hello, how are you?", "en"},
            {"Hola, ¿cómo estás?", "es"},
            {"Bonjour, comment allez-vous?", "fr"},
            {"Guten Tag, wie geht es Ihnen?", "de"},
            {"Ciao, come stai?", "it"}
        };
        
        // Multi-language conversation scenarios
        conversationScenarios_ = {
            {
                {"Hello, nice to meet you", "en", "es"},
                {"Hola, mucho gusto", "es", "en"},
                {"What is your name?", "en", "es"},
                {"Me llamo María", "es", "en"},
                {"I'm from New York", "en", "es"}
            },
            {
                {"Bonjour, comment ça va?", "fr", "en"},
                {"Hello, I'm fine thank you", "en", "fr"},
                {"Where are you from?", "en", "fr"},
                {"Je suis de Paris", "fr", "en"},
                {"That's wonderful!", "en", "fr"}
            }
        };
    }
    
    // Helper function to simulate STT output
    std::string simulateSTTTranscription(const std::vector<float>& audioData, const std::string& expectedLang) {
        // In a real test, this would use actual Whisper STT
        // For integration testing, we simulate realistic STT output
        if (expectedLang == "en") {
            return "Hello, this is a test transcription from speech to text.";
        } else if (expectedLang == "es") {
            return "Hola, esta es una transcripción de prueba de voz a texto.";
        } else if (expectedLang == "fr") {
            return "Bonjour, ceci est une transcription de test de la parole au texte.";
        }
        return "Test transcription";
    }
    
    std::unique_ptr<mt::MarianTranslator> translator_;
    std::unique_ptr<mt::LanguageDetector> languageDetector_;
    std::unique_ptr<mt::GPUAccelerator> gpuAccelerator_;
    std::unique_ptr<mt::QualityManager> qualityManager_;
    std::unique_ptr<core::TranslationPipeline> pipeline_;
    utils::PerformanceMonitor* perfMonitor_;
    
    std::vector<float> testAudioData_;
    std::vector<std::pair<std::string, std::string>> testPhrases_;
    std::vector<std::vector<std::tuple<std::string, std::string, std::string>>> conversationScenarios_;
};

// Test 1: STT → Language Detection → MT Pipeline Integration
TEST_F(MTEndToEndIntegrationTest, STTLanguageDetectionMTPipeline) {
    ASSERT_TRUE(translator_->initialize("en", "es"));
    
    for (const auto& [phrase, expectedLang] : testPhrases_) {
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // Step 1: Simulate STT transcription
        std::string transcribedText = simulateSTTTranscription(testAudioData_, expectedLang);
        auto sttEndTime = std::chrono::high_resolution_clock::now();
        
        // Step 2: Language detection
        auto detectionResult = languageDetector_->detectLanguage(transcribedText);
        auto detectionEndTime = std::chrono::high_resolution_clock::now();
        
        EXPECT_TRUE(detectionResult.isReliable);
        EXPECT_GT(detectionResult.confidence, 0.5f);
        
        // Step 3: Translation based on detected language
        std::string targetLang = (detectionResult.detectedLanguage == "en") ? "es" : "en";
        
        // Switch translator to detected language pair
        ASSERT_TRUE(translator_->initialize(detectionResult.detectedLanguage, targetLang));
        
        auto translationResult = translator_->translate(transcribedText);
        auto translationEndTime = std::chrono::high_resolution_clock::now();
        
        EXPECT_TRUE(translationResult.success);
        EXPECT_FALSE(translationResult.translatedText.empty());
        EXPECT_EQ(translationResult.sourceLang, detectionResult.detectedLanguage);
        EXPECT_EQ(translationResult.targetLang, targetLang);
        
        // Record performance metrics
        auto sttLatency = std::chrono::duration_cast<std::chrono::milliseconds>(
            sttEndTime - startTime).count();
        auto detectionLatency = std::chrono::duration_cast<std::chrono::milliseconds>(
            detectionEndTime - sttEndTime).count();
        auto translationLatency = std::chrono::duration_cast<std::chrono::milliseconds>(
            translationEndTime - detectionEndTime).count();
        auto totalLatency = std::chrono::duration_cast<std::chrono::milliseconds>(
            translationEndTime - startTime).count();
        
        perfMonitor_->recordLatency("integration.stt_latency_ms", sttLatency);
        perfMonitor_->recordLatency("integration.detection_latency_ms", detectionLatency);
        perfMonitor_->recordLatency("integration.translation_latency_ms", translationLatency);
        perfMonitor_->recordLatency("integration.total_pipeline_latency_ms", totalLatency);
        
        std::cout << "Pipeline test - Source: " << detectionResult.detectedLanguage 
                  << ", Target: " << targetLang 
                  << ", Total latency: " << totalLatency << "ms" << std::endl;
        
        // Verify latency targets
        EXPECT_LT(detectionLatency, 100); // Language detection should be < 100ms
        EXPECT_LT(translationLatency, 1000); // Translation should be < 1000ms
        EXPECT_LT(totalLatency, 2000); // Total pipeline should be < 2000ms
    }
}

// Test 2: Multi-language Conversation Scenarios
TEST_F(MTEndToEndIntegrationTest, MultiLanguageConversationScenarios) {
    for (size_t scenarioIdx = 0; scenarioIdx < conversationScenarios_.size(); ++scenarioIdx) {
        const auto& scenario = conversationScenarios_[scenarioIdx];
        
        std::cout << "Testing conversation scenario " << (scenarioIdx + 1) << std::endl;
        
        std::vector<double> turnLatencies;
        
        for (size_t turnIdx = 0; turnIdx < scenario.size(); ++turnIdx) {
            const auto& [text, sourceLang, targetLang] = scenario[turnIdx];
            
            auto turnStartTime = std::chrono::high_resolution_clock::now();
            
            // Initialize translator for this turn
            ASSERT_TRUE(translator_->initialize(sourceLang, targetLang));
            
            // Detect language (should match expected)
            auto detectionResult = languageDetector_->detectLanguage(text);
            
            // Allow some flexibility in language detection for test data
            if (detectionResult.confidence > 0.3f) {
                EXPECT_TRUE(detectionResult.detectedLanguage == sourceLang ||
                           std::find(detectionResult.languageCandidates.begin(),
                                   detectionResult.languageCandidates.end(),
                                   std::make_pair(sourceLang, 0.0f)) != 
                           detectionResult.languageCandidates.end());
            }
            
            // Translate
            auto translationResult = translator_->translate(text);
            EXPECT_TRUE(translationResult.success);
            EXPECT_FALSE(translationResult.translatedText.empty());
            
            // Assess quality
            auto qualityMetrics = qualityManager_->assessTranslationQuality(
                text, translationResult.translatedText, sourceLang, targetLang);
            
            EXPECT_GT(qualityMetrics.overallConfidence, 0.0f);
            EXPECT_FALSE(qualityMetrics.qualityLevel.empty());
            
            auto turnEndTime = std::chrono::high_resolution_clock::now();
            double turnLatency = std::chrono::duration_cast<std::chrono::milliseconds>(
                turnEndTime - turnStartTime).count();
            
            turnLatencies.push_back(turnLatency);
            
            std::cout << "  Turn " << (turnIdx + 1) << ": '" << text 
                      << "' -> '" << translationResult.translatedText 
                      << "' (" << turnLatency << "ms, quality: " 
                      << qualityMetrics.qualityLevel << ")" << std::endl;
            
            // Record metrics
            perfMonitor_->recordLatency("integration.conversation_turn_latency_ms", turnLatency);
            perfMonitor_->recordMetric("integration.translation_quality", 
                                     qualityMetrics.overallConfidence);
        }
        
        // Analyze conversation performance
        double avgTurnLatency = std::accumulate(turnLatencies.begin(), turnLatencies.end(), 0.0) / turnLatencies.size();
        double maxTurnLatency = *std::max_element(turnLatencies.begin(), turnLatencies.end());
        
        perfMonitor_->recordLatency("integration.avg_conversation_turn_latency_ms", avgTurnLatency);
        perfMonitor_->recordLatency("integration.max_conversation_turn_latency_ms", maxTurnLatency);
        
        std::cout << "Scenario " << (scenarioIdx + 1) << " - Avg turn latency: " 
                  << avgTurnLatency << "ms, Max: " << maxTurnLatency << "ms" << std::endl;
        
        // Performance expectations for conversation flow
        EXPECT_LT(avgTurnLatency, 1500.0); // Average turn should be < 1.5s
        EXPECT_LT(maxTurnLatency, 3000.0); // Max turn should be < 3s
    }
}

// Test 3: GPU vs CPU Performance Comparison
TEST_F(MTEndToEndIntegrationTest, GPUvsCPUPerformanceComparison) {
    if (!gpuAccelerator_->isGPUAvailable()) {
        GTEST_SKIP() << "GPU not available for performance comparison";
    }
    
    const std::string testText = "This is a comprehensive test sentence for performance comparison between GPU and CPU translation processing.";
    const int numIterations = 20;
    
    std::vector<double> cpuLatencies;
    std::vector<double> gpuLatencies;
    
    // Test CPU performance
    std::cout << "Testing CPU performance..." << std::endl;
    ASSERT_TRUE(translator_->initialize("en", "es"));
    translator_->setGPUAcceleration(false);
    
    for (int i = 0; i < numIterations; ++i) {
        auto startTime = std::chrono::high_resolution_clock::now();
        
        auto result = translator_->translate(testText);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        double latency = std::chrono::duration_cast<std::chrono::microseconds>(
            endTime - startTime).count() / 1000.0; // Convert to ms
        
        EXPECT_TRUE(result.success);
        EXPECT_FALSE(result.usedGPUAcceleration);
        
        cpuLatencies.push_back(latency);
        perfMonitor_->recordLatency("integration.cpu_translation_latency_ms", latency);
    }
    
    // Test GPU performance
    std::cout << "Testing GPU performance..." << std::endl;
    if (gpuAccelerator_->selectGPU(0)) {
        ASSERT_TRUE(translator_->initializeWithGPU("en", "es", 0));
        
        for (int i = 0; i < numIterations; ++i) {
            auto startTime = std::chrono::high_resolution_clock::now();
            
            auto result = translator_->translate(testText);
            
            auto endTime = std::chrono::high_resolution_clock::now();
            double latency = std::chrono::duration_cast<std::chrono::microseconds>(
                endTime - startTime).count() / 1000.0; // Convert to ms
            
            EXPECT_TRUE(result.success);
            EXPECT_TRUE(result.usedGPUAcceleration);
            
            gpuLatencies.push_back(latency);
            perfMonitor_->recordLatency("integration.gpu_translation_latency_ms", latency);
        }
    }
    
    // Calculate statistics
    double avgCPULatency = std::accumulate(cpuLatencies.begin(), cpuLatencies.end(), 0.0) / cpuLatencies.size();
    double avgGPULatency = std::accumulate(gpuLatencies.begin(), gpuLatencies.end(), 0.0) / gpuLatencies.size();
    
    double cpuThroughput = 1000.0 / avgCPULatency; // translations per second
    double gpuThroughput = 1000.0 / avgGPULatency;
    
    double speedup = avgCPULatency / avgGPULatency;
    
    perfMonitor_->recordMetric("integration.cpu_avg_latency_ms", avgCPULatency);
    perfMonitor_->recordMetric("integration.gpu_avg_latency_ms", avgGPULatency);
    perfMonitor_->recordMetric("integration.gpu_speedup", speedup);
    perfMonitor_->recordThroughput("integration.cpu_throughput_tps", cpuThroughput);
    perfMonitor_->recordThroughput("integration.gpu_throughput_tps", gpuThroughput);
    
    std::cout << "Performance Comparison Results:" << std::endl;
    std::cout << "  CPU - Avg latency: " << avgCPULatency << "ms, Throughput: " << cpuThroughput << " t/s" << std::endl;
    std::cout << "  GPU - Avg latency: " << avgGPULatency << "ms, Throughput: " << gpuThroughput << " t/s" << std::endl;
    std::cout << "  GPU Speedup: " << speedup << "x" << std::endl;
    
    // GPU should provide some performance benefit (even if minimal with fallback translation)
    EXPECT_GT(speedup, 0.8); // Allow for some variance in test environment
    
    // Test GPU memory usage
    auto gpuStats = gpuAccelerator_->getGPUStatistics();
    EXPECT_GT(gpuStats.translationsProcessed, 0);
    
    perfMonitor_->recordMetric("integration.gpu_memory_used_mb", gpuStats.memoryUsedMB);
    perfMonitor_->recordMetric("integration.gpu_utilization_percent", gpuStats.utilizationPercent);
    
    std::cout << "  GPU Memory Used: " << gpuStats.memoryUsedMB << "MB" << std::endl;
    std::cout << "  GPU Utilization: " << gpuStats.utilizationPercent << "%" << std::endl;
}

// Test 4: Real-time Performance and Latency Measurement
TEST_F(MTEndToEndIntegrationTest, RealTimePerformanceLatencyMeasurement) {
    ASSERT_TRUE(translator_->initialize("en", "es"));
    
    // Test different text lengths to measure scaling
    std::vector<std::pair<std::string, std::string>> testCases = {
        {"Short", "Hello"},
        {"Medium", "Hello, how are you doing today? I hope everything is going well."},
        {"Long", "This is a much longer text that contains multiple sentences and should test the translation system's ability to handle longer inputs efficiently. The system should maintain good performance even with increased text length and complexity."},
        {"Very Long", "This is an even longer text that spans multiple sentences and contains various linguistic structures. It includes different types of sentences, punctuation marks, and should thoroughly test the translation pipeline's performance under more demanding conditions. The goal is to measure how latency scales with input length and complexity, ensuring that the system can handle real-world usage scenarios effectively."}
    };
    
    for (const auto& [category, text] : testCases) {
        const int numRuns = 10;
        std::vector<double> latencies;
        std::vector<double> confidences;
        
        for (int run = 0; run < numRuns; ++run) {
            auto startTime = std::chrono::high_resolution_clock::now();
            
            // Full pipeline: Language detection + Translation + Quality assessment
            auto detectionResult = languageDetector_->detectLanguage(text);
            auto translationResult = translator_->translate(text);
            auto qualityMetrics = qualityManager_->assessTranslationQuality(
                text, translationResult.translatedText, "en", "es");
            
            auto endTime = std::chrono::high_resolution_clock::now();
            double latency = std::chrono::duration_cast<std::chrono::microseconds>(
                endTime - startTime).count() / 1000.0; // Convert to ms
            
            EXPECT_TRUE(translationResult.success);
            EXPECT_GT(qualityMetrics.overallConfidence, 0.0f);
            
            latencies.push_back(latency);
            confidences.push_back(qualityMetrics.overallConfidence);
            
            perfMonitor_->recordLatency("integration.realtime_" + category + "_latency_ms", latency);
            perfMonitor_->recordMetric("integration.realtime_" + category + "_confidence", 
                                     qualityMetrics.overallConfidence);
        }
        
        // Calculate statistics
        double avgLatency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
        double maxLatency = *std::max_element(latencies.begin(), latencies.end());
        double minLatency = *std::min_element(latencies.begin(), latencies.end());
        double avgConfidence = std::accumulate(confidences.begin(), confidences.end(), 0.0) / confidences.size();
        
        // Calculate percentiles
        std::sort(latencies.begin(), latencies.end());
        double p95Latency = latencies[static_cast<size_t>(latencies.size() * 0.95)];
        double p99Latency = latencies[static_cast<size_t>(latencies.size() * 0.99)];
        
        perfMonitor_->recordMetric("integration.realtime_" + category + "_avg_latency_ms", avgLatency);
        perfMonitor_->recordMetric("integration.realtime_" + category + "_p95_latency_ms", p95Latency);
        perfMonitor_->recordMetric("integration.realtime_" + category + "_p99_latency_ms", p99Latency);
        
        std::cout << category << " text performance:" << std::endl;
        std::cout << "  Text length: " << text.length() << " chars" << std::endl;
        std::cout << "  Avg latency: " << avgLatency << "ms" << std::endl;
        std::cout << "  P95 latency: " << p95Latency << "ms" << std::endl;
        std::cout << "  P99 latency: " << p99Latency << "ms" << std::endl;
        std::cout << "  Min/Max: " << minLatency << "/" << maxLatency << "ms" << std::endl;
        std::cout << "  Avg confidence: " << avgConfidence << std::endl;
        
        // Real-time performance expectations
        if (category == "Short") {
            EXPECT_LT(p95Latency, 500.0); // Short text should be < 500ms P95
        } else if (category == "Medium") {
            EXPECT_LT(p95Latency, 1000.0); // Medium text should be < 1000ms P95
        } else {
            EXPECT_LT(p95Latency, 2000.0); // Long text should be < 2000ms P95
        }
        
        // Confidence should be reasonable
        EXPECT_GT(avgConfidence, 0.3); // At least 30% confidence on average
    }
    
    // Test concurrent processing performance
    std::cout << "Testing concurrent processing..." << std::endl;
    
    const int numConcurrentRequests = 8;
    const std::string concurrentTestText = "Concurrent processing test sentence for performance measurement.";
    
    std::vector<std::future<std::pair<double, bool>>> futures;
    auto concurrentStartTime = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < numConcurrentRequests; ++i) {
        futures.push_back(std::async(std::launch::async, [this, concurrentTestText]() {
            auto startTime = std::chrono::high_resolution_clock::now();
            
            auto result = translator_->translate(concurrentTestText);
            
            auto endTime = std::chrono::high_resolution_clock::now();
            double latency = std::chrono::duration_cast<std::chrono::microseconds>(
                endTime - startTime).count() / 1000.0;
            
            return std::make_pair(latency, result.success);
        }));
    }
    
    // Collect results
    std::vector<double> concurrentLatencies;
    int successCount = 0;
    
    for (auto& future : futures) {
        auto [latency, success] = future.get();
        concurrentLatencies.push_back(latency);
        if (success) successCount++;
    }
    
    auto concurrentEndTime = std::chrono::high_resolution_clock::now();
    double totalConcurrentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        concurrentEndTime - concurrentStartTime).count();
    
    double avgConcurrentLatency = std::accumulate(concurrentLatencies.begin(), concurrentLatencies.end(), 0.0) / concurrentLatencies.size();
    double concurrentThroughput = static_cast<double>(numConcurrentRequests) / (totalConcurrentTime / 1000.0);
    
    perfMonitor_->recordLatency("integration.concurrent_avg_latency_ms", avgConcurrentLatency);
    perfMonitor_->recordThroughput("integration.concurrent_throughput_rps", concurrentThroughput);
    perfMonitor_->recordMetric("integration.concurrent_success_rate", 
                             static_cast<double>(successCount) / numConcurrentRequests);
    
    std::cout << "Concurrent processing results:" << std::endl;
    std::cout << "  Total time: " << totalConcurrentTime << "ms" << std::endl;
    std::cout << "  Avg latency: " << avgConcurrentLatency << "ms" << std::endl;
    std::cout << "  Throughput: " << concurrentThroughput << " req/s" << std::endl;
    std::cout << "  Success rate: " << (static_cast<double>(successCount) / numConcurrentRequests * 100) << "%" << std::endl;
    
    EXPECT_EQ(successCount, numConcurrentRequests); // All requests should succeed
    EXPECT_GT(concurrentThroughput, 1.0); // Should handle at least 1 request per second
}

// Test 5: Error Propagation and Recovery Validation
TEST_F(MTEndToEndIntegrationTest, ErrorPropagationAndRecoveryValidation) {
    // Test various error scenarios and recovery mechanisms
    
    // Scenario 1: Invalid language pair
    std::cout << "Testing invalid language pair error handling..." << std::endl;
    {
        bool initResult = translator_->initialize("invalid_lang", "also_invalid");
        EXPECT_FALSE(initResult);
        
        // Should recover and work with valid language pair
        ASSERT_TRUE(translator_->initialize("en", "es"));
        auto result = translator_->translate("Recovery test");
        EXPECT_TRUE(result.success);
        
        perfMonitor_->recordCounter("integration.error_recovery_invalid_lang", 1);
    }
    
    // Scenario 2: Empty text handling
    std::cout << "Testing empty text error handling..." << std::endl;
    {
        ASSERT_TRUE(translator_->initialize("en", "es"));
        
        auto emptyResult = translator_->translate("");
        EXPECT_FALSE(emptyResult.success);
        EXPECT_FALSE(emptyResult.errorMessage.empty());
        
        // Should recover and work with valid text
        auto validResult = translator_->translate("Valid text after empty");
        EXPECT_TRUE(validResult.success);
        
        perfMonitor_->recordCounter("integration.error_recovery_empty_text", 1);
    }
    
    // Scenario 3: Language detection failure recovery
    std::cout << "Testing language detection failure recovery..." << std::endl;
    {
        // Test with ambiguous or very short text
        std::string ambiguousText = "a";
        auto detectionResult = languageDetector_->detectLanguage(ambiguousText);
        
        if (!detectionResult.isReliable || detectionResult.confidence < 0.5f) {
            // Should fall back to default language
            std::string fallbackLang = languageDetector_->getFallbackLanguage("unknown");
            EXPECT_FALSE(fallbackLang.empty());
            
            // Translation should still work with fallback
            ASSERT_TRUE(translator_->initialize(fallbackLang, "es"));
            auto result = translator_->translate("Fallback test");
            EXPECT_TRUE(result.success);
        }
        
        perfMonitor_->recordCounter("integration.error_recovery_detection_failure", 1);
    }
    
    // Scenario 4: GPU acceleration failure and CPU fallback
    std::cout << "Testing GPU failure and CPU fallback..." << std::endl;
    if (gpuAccelerator_->isGPUAvailable()) {
        // Try to initialize with invalid GPU device
        bool gpuInitResult = translator_->initializeWithGPU("en", "es", 999); // Invalid device ID
        
        if (!gpuInitResult) {
            // Should fall back to CPU
            ASSERT_TRUE(translator_->initialize("en", "es"));
            translator_->setGPUAcceleration(false);
            
            auto result = translator_->translate("CPU fallback test");
            EXPECT_TRUE(result.success);
            EXPECT_FALSE(result.usedGPUAcceleration);
            
            perfMonitor_->recordCounter("integration.error_recovery_gpu_fallback", 1);
        }
    }
    
    // Scenario 5: Quality threshold failure and alternative generation
    std::cout << "Testing quality threshold failure and alternatives..." << std::endl;
    {
        ASSERT_TRUE(translator_->initialize("en", "es"));
        
        // Set very high quality threshold to trigger failure
        qualityManager_->setQualityThresholds(0.95f, 0.90f, 0.80f);
        
        std::string testText = "Test text for quality assessment";
        auto translationResult = translator_->translate(testText);
        EXPECT_TRUE(translationResult.success);
        
        auto qualityMetrics = qualityManager_->assessTranslationQuality(
            testText, translationResult.translatedText, "en", "es");
        
        if (!qualityManager_->meetsQualityThreshold(qualityMetrics, "high")) {
            // Should generate alternatives
            auto alternatives = translator_->getTranslationCandidates(testText, 3);
            EXPECT_FALSE(alternatives.empty());
            
            // At least one alternative should be available
            bool hasValidAlternative = false;
            for (const auto& alt : alternatives) {
                if (alt.success && !alt.translatedText.empty()) {
                    hasValidAlternative = true;
                    break;
                }
            }
            EXPECT_TRUE(hasValidAlternative);
        }
        
        perfMonitor_->recordCounter("integration.error_recovery_quality_threshold", 1);
    }
    
    // Scenario 6: Concurrent error handling
    std::cout << "Testing concurrent error handling..." << std::endl;
    {
        const int numConcurrentErrors = 5;
        std::vector<std::future<bool>> errorFutures;
        
        for (int i = 0; i < numConcurrentErrors; ++i) {
            errorFutures.push_back(std::async(std::launch::async, [this, i]() {
                // Alternate between error conditions and valid requests
                if (i % 2 == 0) {
                    // Try invalid operation
                    auto result = translator_->translate(""); // Empty text
                    return !result.success; // Should fail
                } else {
                    // Valid operation
                    auto result = translator_->translate("Valid concurrent test " + std::to_string(i));
                    return result.success; // Should succeed
                }
            }));
        }
        
        int expectedErrors = 0;
        int expectedSuccesses = 0;
        
        for (int i = 0; i < numConcurrentErrors; ++i) {
            bool result = errorFutures[i].get();
            if (i % 2 == 0) {
                EXPECT_TRUE(result); // Error case should return true (error detected)
                expectedErrors++;
            } else {
                EXPECT_TRUE(result); // Success case should return true (success)
                expectedSuccesses++;
            }
        }
        
        perfMonitor_->recordCounter("integration.concurrent_errors_handled", expectedErrors);
        perfMonitor_->recordCounter("integration.concurrent_successes", expectedSuccesses);
    }
    
    // Scenario 7: Memory pressure and recovery
    std::cout << "Testing memory pressure and recovery..." << std::endl;
    {
        // Simulate memory pressure by loading multiple language pairs
        std::vector<std::pair<std::string, std::string>> languagePairs = {
            {"en", "es"}, {"en", "fr"}, {"en", "de"}, {"en", "it"},
            {"es", "en"}, {"fr", "en"}, {"de", "en"}, {"it", "en"}
        };
        
        int successfulLoads = 0;
        for (const auto& [source, target] : languagePairs) {
            if (translator_->initialize(source, target)) {
                successfulLoads++;
                
                // Quick translation test
                auto result = translator_->translate("Memory pressure test");
                if (!result.success) {
                    // If translation fails due to memory pressure, should still handle gracefully
                    EXPECT_FALSE(result.errorMessage.empty());
                }
            }
        }
        
        // Should handle at least some language pairs
        EXPECT_GT(successfulLoads, 0);
        
        perfMonitor_->recordMetric("integration.memory_pressure_successful_loads", successfulLoads);
        perfMonitor_->recordCounter("integration.error_recovery_memory_pressure", 1);
    }
    
    std::cout << "Error propagation and recovery tests completed successfully." << std::endl;
}

// Test 6: Streaming Translation Integration
TEST_F(MTEndToEndIntegrationTest, StreamingTranslationIntegration) {
    ASSERT_TRUE(translator_->initialize("en", "es"));
    
    const std::string sessionId = "streaming_test_session";
    const std::vector<std::string> textChunks = {
        "Hello,",
        " this is",
        " a streaming",
        " translation",
        " test that",
        " should work",
        " incrementally."
    };
    
    std::cout << "Testing streaming translation..." << std::endl;
    
    // Start streaming session
    translator_->startStreamingTranslation(sessionId);
    
    std::vector<double> chunkLatencies;
    std::string accumulatedTranslation;
    
    for (size_t i = 0; i < textChunks.size(); ++i) {
        auto startTime = std::chrono::high_resolution_clock::now();
        
        auto result = translator_->addStreamingText(sessionId, textChunks[i]);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        double latency = std::chrono::duration_cast<std::chrono::microseconds>(
            endTime - startTime).count() / 1000.0;
        
        EXPECT_TRUE(result.success);
        EXPECT_TRUE(result.isPartialResult);
        EXPECT_FALSE(result.isStreamingComplete);
        
        chunkLatencies.push_back(latency);
        accumulatedTranslation = result.translatedText;
        
        perfMonitor_->recordLatency("integration.streaming_chunk_latency_ms", latency);
        
        std::cout << "  Chunk " << (i + 1) << ": '" << textChunks[i] 
                  << "' -> partial: '" << result.translatedText 
                  << "' (" << latency << "ms)" << std::endl;
        
        // Streaming chunks should be fast
        EXPECT_LT(latency, 200.0); // Each chunk should be < 200ms
    }
    
    // Finalize streaming translation
    auto finalStartTime = std::chrono::high_resolution_clock::now();
    auto finalResult = translator_->finalizeStreamingTranslation(sessionId);
    auto finalEndTime = std::chrono::high_resolution_clock::now();
    
    double finalLatency = std::chrono::duration_cast<std::chrono::microseconds>(
        finalEndTime - finalStartTime).count() / 1000.0;
    
    EXPECT_TRUE(finalResult.success);
    EXPECT_FALSE(finalResult.isPartialResult);
    EXPECT_TRUE(finalResult.isStreamingComplete);
    EXPECT_FALSE(finalResult.translatedText.empty());
    
    perfMonitor_->recordLatency("integration.streaming_finalize_latency_ms", finalLatency);
    
    double avgChunkLatency = std::accumulate(chunkLatencies.begin(), chunkLatencies.end(), 0.0) / chunkLatencies.size();
    double totalStreamingTime = std::accumulate(chunkLatencies.begin(), chunkLatencies.end(), 0.0) + finalLatency;
    
    perfMonitor_->recordLatency("integration.streaming_avg_chunk_latency_ms", avgChunkLatency);
    perfMonitor_->recordLatency("integration.streaming_total_time_ms", totalStreamingTime);
    
    std::cout << "Streaming translation completed:" << std::endl;
    std::cout << "  Final result: '" << finalResult.translatedText << "'" << std::endl;
    std::cout << "  Avg chunk latency: " << avgChunkLatency << "ms" << std::endl;
    std::cout << "  Total streaming time: " << totalStreamingTime << "ms" << std::endl;
    std::cout << "  Finalize latency: " << finalLatency << "ms" << std::endl;
    
    // Performance expectations for streaming
    EXPECT_LT(avgChunkLatency, 150.0); // Average chunk processing should be fast
    EXPECT_LT(finalLatency, 500.0); // Finalization should be reasonable
}

} // namespace integration
} // namespace speechrnt