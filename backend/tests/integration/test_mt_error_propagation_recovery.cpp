#include <gtest/gtest.h>
#include "core/translation_pipeline.hpp"
#include "mt/marian_translator.hpp"
#include "mt/language_detector.hpp"
#include "mt/gpu_accelerator.hpp"
#include "mt/quality_manager.hpp"
#include "utils/logging.hpp"
#include <chrono>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <future>
#include <random>

namespace speechrnt {
namespace integration {

class MTErrorPropagationRecoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        utils::Logger::initialize();
        utils::Logger::setLevel(utils::LogLevel::INFO);
        
        setupComponents();
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
    }
    
    void setupComponents() {
        translator_ = std::make_unique<mt::MarianTranslator>();
        translator_->setModelsPath("backend/data/marian/");
        
        languageDetector_ = std::make_unique<mt::LanguageDetector>();
        languageDetector_->initialize("backend/config/language_detection.json");
        
        gpuAccelerator_ = std::make_unique<mt::GPUAccelerator>();
        gpuAccelerator_->initialize();
        
        qualityManager_ = std::make_unique<mt::QualityManager>();
        qualityManager_->initialize("backend/config/quality_assessment.json");
        
        pipeline_ = std::make_unique<core::TranslationPipeline>();
        pipeline_->initialize("backend/data/");
    }
    
    // Helper to simulate various error conditions
    void simulateMemoryPressure() {
        // Simulate memory pressure by attempting to load many models
        std::vector<std::pair<std::string, std::string>> manyPairs = {
            {"en", "es"}, {"en", "fr"}, {"en", "de"}, {"en", "it"}, {"en", "pt"},
            {"es", "en"}, {"fr", "en"}, {"de", "en"}, {"it", "en"}, {"pt", "en"}
        };
        
        for (const auto& [source, target] : manyPairs) {
            translator_->initialize(source, target);
        }
    }
    
    void simulateNetworkLatency() {
        // Simulate network latency with sleep
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::unique_ptr<mt::MarianTranslator> translator_;
    std::unique_ptr<mt::LanguageDetector> languageDetector_;
    std::unique_ptr<mt::GPUAccelerator> gpuAccelerator_;
    std::unique_ptr<mt::QualityManager> qualityManager_;
    std::unique_ptr<core::TranslationPipeline> pipeline_;
};

// Test 1: Invalid Input Error Propagation and Recovery
TEST_F(MTErrorPropagationRecoveryTest, InvalidInputErrorPropagationRecovery) {
    ASSERT_TRUE(translator_->initialize("en", "es"));
    
    // Test various invalid inputs
    std::vector<std::pair<std::string, std::string>> invalidInputs = {
        {"", "empty_string"},
        {std::string(1000000, 'a'), "extremely_long_string"},
        {"\x00\x01\x02\x03", "binary_data"},
        {"🚀🌟💫🎉🔥", "only_emojis"},
        {std::string(1, '\0'), "null_character"}
    };
    
    int recoverySuccessCount = 0;
    
    for (const auto& [invalidInput, description] : invalidInputs) {
        std::cout << "Testing invalid input: " << description << std::endl;
        
        // Attempt translation with invalid input
        auto invalidResult = translator_->translate(invalidInput);
        
        // Should fail gracefully
        EXPECT_FALSE(invalidResult.success);
        EXPECT_FALSE(invalidResult.errorMessage.empty());
        
        std::cout << "  Error message: " << invalidResult.errorMessage << std::endl;
        
        // Verify system can recover with valid input
        auto recoveryResult = translator_->translate("This is a valid recovery test.");
        if (recoveryResult.success) {
            recoverySuccessCount++;
            std::cout << "  Recovery successful: " << recoveryResult.translatedText << std::endl;
        } else {
            std::cout << "  Recovery failed: " << recoveryResult.errorMessage << std::endl;
        }
    }
    
    // Should recover from all invalid input scenarios
    EXPECT_EQ(recoverySuccessCount, invalidInputs.size());
}

// Test 2: Language Detection Error Cascade and Recovery
TEST_F(MTErrorPropagationRecoveryTest, LanguageDetectionErrorCascadeRecovery) {
    // Test language detection failures and their impact on translation pipeline
    
    std::vector<std::pair<std::string, std::string>> problematicTexts = {
        {"a", "single_character"},
        {"123456", "only_numbers"},
        {"!@#$%^&*()", "only_symbols"},
        {"", "empty_text"},
        {std::string(5, ' '), "only_spaces"}
    };
    
    int pipelineRecoveryCount = 0;
    
    for (const auto& [text, description] : problematicTexts) {
        std::cout << "Testing problematic text: " << description << std::endl;
        
        // Step 1: Language detection
        auto detectionResult = languageDetector_->detectLanguage(text);
        
        std::cout << "  Detection confidence: " << detectionResult.confidence << std::endl;
        std::cout << "  Is reliable: " << (detectionResult.isReliable ? "yes" : "no") << std::endl;
        
        // Step 2: Handle detection failure
        std::string sourceLang;
        if (!detectionResult.isReliable || detectionResult.confidence < 0.3f) {
            // Use fallback language
            sourceLang = languageDetector_->getFallbackLanguage("unknown");
            std::cout << "  Using fallback language: " << sourceLang << std::endl;
        } else {
            sourceLang = detectionResult.detectedLanguage;
        }
        
        // Step 3: Attempt translation with detected/fallback language
        if (!sourceLang.empty()) {
            bool initSuccess = translator_->initialize(sourceLang, "es");
            if (initSuccess) {
                auto translationResult = translator_->translate(text);
                
                if (translationResult.success) {
                    std::cout << "  Translation successful: " << translationResult.translatedText << std::endl;
                } else {
                    std::cout << "  Translation failed: " << translationResult.errorMessage << std::endl;
                }
                
                // Test recovery with valid text
                auto recoveryResult = translator_->translate("Recovery test after problematic input.");
                if (recoveryResult.success) {
                    pipelineRecoveryCount++;
                    std::cout << "  Pipeline recovery successful" << std::endl;
                }
            }
        }
    }
    
    // Pipeline should recover from most problematic inputs
    EXPECT_GT(pipelineRecoveryCount, problematicTexts.size() / 2);
}

// Test 3: GPU Acceleration Failure and CPU Fallback
TEST_F(MTErrorPropagationRecoveryTest, GPUAccelerationFailureCPUFallback) {
    if (!gpuAccelerator_->isGPUAvailable()) {
        GTEST_SKIP() << "GPU not available for fallback testing";
    }
    
    // Test various GPU failure scenarios
    std::vector<std::pair<int, std::string>> invalidGPUConfigs = {
        {999, "invalid_device_id"},
        {-1, "negative_device_id"},
        {100, "out_of_range_device_id"}
    };
    
    int fallbackSuccessCount = 0;
    
    for (const auto& [deviceId, description] : invalidGPUConfigs) {
        std::cout << "Testing GPU failure scenario: " << description << std::endl;
        
        // Attempt GPU initialization with invalid config
        bool gpuInitResult = translator_->initializeWithGPU("en", "es", deviceId);
        
        if (!gpuInitResult) {
            std::cout << "  GPU initialization failed as expected" << std::endl;
            
            // Should automatically fall back to CPU
            bool cpuInitResult = translator_->initialize("en", "es");
            EXPECT_TRUE(cpuInitResult);
            
            if (cpuInitResult) {
                translator_->setGPUAcceleration(false);
                
                auto result = translator_->translate("CPU fallback test after GPU failure.");
                
                if (result.success) {
                    EXPECT_FALSE(result.usedGPUAcceleration);
                    fallbackSuccessCount++;
                    std::cout << "  CPU fallback successful: " << result.translatedText << std::endl;
                } else {
                    std::cout << "  CPU fallback failed: " << result.errorMessage << std::endl;
                }
            }
        }
    }
    
    EXPECT_EQ(fallbackSuccessCount, invalidGPUConfigs.size());
    
    // Test GPU memory exhaustion simulation
    std::cout << "Testing GPU memory exhaustion scenario..." << std::endl;
    
    if (gpuAccelerator_->selectGPU(0)) {
        // Try to allocate excessive GPU memory
        const size_t excessiveSize = 1024 * 1024 * 1024; // 1GB
        void* ptr = gpuAccelerator_->allocateGPUMemory(excessiveSize, "exhaustion_test");
        
        if (!ptr) {
            std::cout << "  GPU memory allocation failed as expected" << std::endl;
            
            // Should still be able to use CPU
            ASSERT_TRUE(translator_->initialize("en", "es"));
            translator_->setGPUAcceleration(false);
            
            auto result = translator_->translate("Memory exhaustion recovery test.");
            EXPECT_TRUE(result.success);
            EXPECT_FALSE(result.usedGPUAcceleration);
            
            std::cout << "  Recovery after memory exhaustion successful" << std::endl;
        } else {
            // Clean up if allocation somehow succeeded
            gpuAccelerator_->freeGPUMemory(ptr);
        }
    }
}

// Test 4: Quality Assessment Failure and Alternative Generation
TEST_F(MTErrorPropagationRecoveryTest, QualityAssessmentFailureAlternativeGeneration) {
    ASSERT_TRUE(translator_->initialize("en", "es"));
    
    // Set very high quality thresholds to trigger failures
    qualityManager_->setQualityThresholds(0.95f, 0.90f, 0.85f);
    
    std::vector<std::string> testTexts = {
        "Simple test",
        "This is a more complex sentence with multiple clauses and technical terminology.",
        "Ambiguous text that might be difficult to translate accurately.",
        "Text with numbers 123 and symbols @#$ that could cause issues.",
        "Very short",
        "An extremely long sentence that contains multiple subordinate clauses, complex grammatical structures, technical jargon, and various linguistic elements that might challenge the translation system's ability to maintain high quality output while preserving the original meaning and context of the source text."
    };
    
    int alternativeGenerationSuccessCount = 0;
    
    for (const std::string& text : testTexts) {
        std::cout << "Testing quality assessment for: " << text.substr(0, 50) << "..." << std::endl;
        
        // Step 1: Initial translation
        auto initialResult = translator_->translate(text);
        ASSERT_TRUE(initialResult.success);
        
        // Step 2: Quality assessment
        auto qualityMetrics = qualityManager_->assessTranslationQuality(
            text, initialResult.translatedText, "en", "es");
        
        std::cout << "  Initial quality: " << qualityMetrics.overallConfidence 
                  << " (" << qualityMetrics.qualityLevel << ")" << std::endl;
        
        // Step 3: Check if quality meets threshold
        bool meetsHighQuality = qualityManager_->meetsQualityThreshold(qualityMetrics, "high");
        
        if (!meetsHighQuality) {
            std::cout << "  Quality below threshold, generating alternatives..." << std::endl;
            
            // Step 4: Generate alternatives
            auto alternatives = translator_->getTranslationCandidates(text, 3);
            
            EXPECT_FALSE(alternatives.empty());
            
            if (!alternatives.empty()) {
                // Find best alternative
                mt::TranslationResult bestAlternative;
                float bestQuality = 0.0f;
                
                for (const auto& alt : alternatives) {
                    if (alt.success) {
                        auto altQuality = qualityManager_->assessTranslationQuality(
                            text, alt.translatedText, "en", "es");
                        
                        if (altQuality.overallConfidence > bestQuality) {
                            bestQuality = altQuality.overallConfidence;
                            bestAlternative = alt;
                        }
                    }
                }
                
                if (bestAlternative.success) {
                    alternativeGenerationSuccessCount++;
                    std::cout << "  Best alternative quality: " << bestQuality << std::endl;
                    std::cout << "  Best alternative: " << bestAlternative.translatedText << std::endl;
                    
                    // Alternative should be at least as good as original
                    EXPECT_GE(bestQuality, qualityMetrics.overallConfidence - 0.1f);
                }
            }
        } else {
            std::cout << "  Quality meets threshold, no alternatives needed" << std::endl;
            alternativeGenerationSuccessCount++; // Count as success
        }
    }
    
    // Should successfully handle quality issues for most texts
    EXPECT_GT(alternativeGenerationSuccessCount, testTexts.size() / 2);
}

// Test 5: Concurrent Error Handling and System Stability
TEST_F(MTErrorPropagationRecoveryTest, ConcurrentErrorHandlingSystemStability) {
    ASSERT_TRUE(translator_->initialize("en", "es"));
    
    const int numConcurrentThreads = 8;
    const int operationsPerThread = 10;
    
    std::atomic<int> totalOperations{0};
    std::atomic<int> successfulOperations{0};
    std::atomic<int> errorRecoveries{0};
    std::atomic<int> systemStabilityChecks{0};
    
    std::vector<std::future<void>> futures;
    
    // Launch concurrent threads with mixed valid/invalid operations
    for (int t = 0; t < numConcurrentThreads; ++t) {
        futures.push_back(std::async(std::launch::async, [&, t]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> errorTypeDist(0, 4);
            
            for (int op = 0; op < operationsPerThread; ++op) {
                totalOperations++;
                
                int errorType = errorTypeDist(gen);
                bool operationSuccessful = false;
                
                try {
                    switch (errorType) {
                        case 0: {
                            // Valid operation
                            auto result = translator_->translate("Valid concurrent operation " + 
                                                               std::to_string(t) + "_" + std::to_string(op));
                            operationSuccessful = result.success;
                            break;
                        }
                        case 1: {
                            // Empty string error
                            auto result = translator_->translate("");
                            // Should fail, but system should remain stable
                            if (!result.success) {
                                // Try recovery
                                auto recovery = translator_->translate("Recovery after empty string");
                                if (recovery.success) {
                                    errorRecoveries++;
                                    operationSuccessful = true;
                                }
                            }
                            break;
                        }
                        case 2: {
                            // Invalid language pair error
                            bool initResult = translator_->initialize("invalid", "also_invalid");
                            if (!initResult) {
                                // Try recovery with valid pair
                                bool recoveryInit = translator_->initialize("en", "es");
                                if (recoveryInit) {
                                    auto recovery = translator_->translate("Recovery after invalid language");
                                    if (recovery.success) {
                                        errorRecoveries++;
                                        operationSuccessful = true;
                                    }
                                }
                            }
                            break;
                        }
                        case 3: {
                            // Language detection with problematic input
                            auto detection = languageDetector_->detectLanguage("123!@#");
                            if (!detection.isReliable) {
                                // Use fallback
                                std::string fallback = languageDetector_->getFallbackLanguage("unknown");
                                if (!fallback.empty()) {
                                    bool initResult = translator_->initialize(fallback, "es");
                                    if (initResult) {
                                        auto recovery = translator_->translate("Fallback recovery test");
                                        if (recovery.success) {
                                            errorRecoveries++;
                                            operationSuccessful = true;
                                        }
                                    }
                                }
                            } else {
                                operationSuccessful = true;
                            }
                            break;
                        }
                        case 4: {
                            // Quality assessment with poor input
                            auto translation = translator_->translate("a");
                            if (translation.success) {
                                auto quality = qualityManager_->assessTranslationQuality(
                                    "a", translation.translatedText, "en", "es");
                                
                                if (quality.overallConfidence < 0.5f) {
                                    // Generate alternatives
                                    auto alternatives = translator_->getTranslationCandidates("a", 2);
                                    if (!alternatives.empty()) {
                                        errorRecoveries++;
                                        operationSuccessful = true;
                                    }
                                } else {
                                    operationSuccessful = true;
                                }
                            }
                            break;
                        }
                    }
                } catch (const std::exception& e) {
                    std::cout << "Thread " << t << " caught exception: " << e.what() << std::endl;
                    // System should not crash, but operation fails
                    operationSuccessful = false;
                }
                
                if (operationSuccessful) {
                    successfulOperations++;
                }
                
                // Periodic system stability check
                if (op % 3 == 0) {
                    // Verify system is still responsive
                    auto stabilityCheck = translator_->translate("Stability check");
                    if (stabilityCheck.success) {
                        systemStabilityChecks++;
                    }
                }
                
                // Small delay to simulate realistic usage
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }));
    }
    
    // Wait for all threads to complete
    for (auto& future : futures) {
        future.wait();
    }
    
    // Analyze results
    int expectedOperations = numConcurrentThreads * operationsPerThread;
    double successRate = static_cast<double>(successfulOperations.load()) / totalOperations.load();
    double recoveryRate = static_cast<double>(errorRecoveries.load()) / totalOperations.load();
    double stabilityRate = static_cast<double>(systemStabilityChecks.load()) / (numConcurrentThreads * (operationsPerThread / 3));
    
    std::cout << "Concurrent Error Handling Results:" << std::endl;
    std::cout << "  Total operations: " << totalOperations.load() << std::endl;
    std::cout << "  Successful operations: " << successfulOperations.load() << std::endl;
    std::cout << "  Error recoveries: " << errorRecoveries.load() << std::endl;
    std::cout << "  Stability checks: " << systemStabilityChecks.load() << std::endl;
    std::cout << "  Success rate: " << (successRate * 100) << "%" << std::endl;
    std::cout << "  Recovery rate: " << (recoveryRate * 100) << "%" << std::endl;
    std::cout << "  Stability rate: " << (stabilityRate * 100) << "%" << std::endl;
    
    EXPECT_EQ(totalOperations.load(), expectedOperations);
    EXPECT_GT(successRate, 0.60); // At least 60% success rate with mixed operations
    EXPECT_GT(recoveryRate, 0.20); // At least 20% recovery rate
    EXPECT_GT(stabilityRate, 0.80); // System should remain stable 80% of the time
    
    // System should not have crashed
    EXPECT_TRUE(true); // If we reach here, system didn't crash
}

// Test 6: Memory Pressure and Resource Exhaustion Recovery
TEST_F(MTErrorPropagationRecoveryTest, MemoryPressureResourceExhaustionRecovery) {
    // Simulate memory pressure by loading many models
    std::vector<std::pair<std::string, std::string>> languagePairs = {
        {"en", "es"}, {"en", "fr"}, {"en", "de"}, {"en", "it"}, {"en", "pt"},
        {"es", "en"}, {"fr", "en"}, {"de", "en"}, {"it", "en"}, {"pt", "en"},
        {"es", "fr"}, {"fr", "de"}, {"de", "it"}, {"it", "pt"}, {"pt", "es"}
    };
    
    int successfulLoads = 0;
    int recoveryAttempts = 0;
    int successfulRecoveries = 0;
    
    std::cout << "Testing memory pressure with multiple model loads..." << std::endl;
    
    for (const auto& [source, target] : languagePairs) {
        bool loadSuccess = translator_->initialize(source, target);
        
        if (loadSuccess) {
            successfulLoads++;
            
            // Test translation to ensure model is actually loaded
            auto result = translator_->translate("Memory pressure test for " + source + " to " + target);
            
            if (!result.success) {
                std::cout << "  Translation failed for " << source << "->" << target 
                          << ": " << result.errorMessage << std::endl;
                
                // Attempt recovery
                recoveryAttempts++;
                
                // Try with a simpler, more common language pair
                if (translator_->initialize("en", "es")) {
                    auto recoveryResult = translator_->translate("Recovery test");
                    if (recoveryResult.success) {
                        successfulRecoveries++;
                        std::cout << "  Recovery successful for " << source << "->" << target << std::endl;
                    }
                }
            }
        } else {
            std::cout << "  Failed to load model for " << source << "->" << target << std::endl;
            
            // Attempt recovery with resource cleanup
            recoveryAttempts++;
            
            // Force cleanup and try with basic pair
            translator_->cleanup();
            translator_->setModelsPath("backend/data/marian/");
            
            if (translator_->initialize("en", "es")) {
                auto recoveryResult = translator_->translate("Recovery after cleanup");
                if (recoveryResult.success) {
                    successfulRecoveries++;
                    std::cout << "  Recovery after cleanup successful" << std::endl;
                }
            }
        }
        
        // Check memory usage if GPU is available
        if (gpuAccelerator_->isGPUAvailable()) {
            size_t memoryUsage = gpuAccelerator_->getCurrentMemoryUsageMB();
            if (memoryUsage > 1000) { // If using more than 1GB
                std::cout << "  High memory usage detected: " << memoryUsage << "MB" << std::endl;
            }
        }
    }
    
    std::cout << "Memory pressure test results:" << std::endl;
    std::cout << "  Successful loads: " << successfulLoads << "/" << languagePairs.size() << std::endl;
    std::cout << "  Recovery attempts: " << recoveryAttempts << std::endl;
    std::cout << "  Successful recoveries: " << successfulRecoveries << std::endl;
    
    // Should be able to load at least some models
    EXPECT_GT(successfulLoads, 0);
    
    // Should have reasonable recovery rate
    if (recoveryAttempts > 0) {
        double recoveryRate = static_cast<double>(successfulRecoveries) / recoveryAttempts;
        EXPECT_GT(recoveryRate, 0.50); // At least 50% recovery rate
        std::cout << "  Recovery rate: " << (recoveryRate * 100) << "%" << std::endl;
    }
    
    // Final system stability check
    translator_->cleanup();
    translator_->setModelsPath("backend/data/marian/");
    ASSERT_TRUE(translator_->initialize("en", "es"));
    
    auto finalCheck = translator_->translate("Final stability check after memory pressure test");
    EXPECT_TRUE(finalCheck.success);
    
    std::cout << "Final stability check: " << (finalCheck.success ? "PASSED" : "FAILED") << std::endl;
}

// Test 7: Pipeline Error Propagation and Isolation
TEST_F(MTErrorPropagationRecoveryTest, PipelineErrorPropagationIsolation) {
    // Test that errors in one part of the pipeline don't affect other parts
    
    struct PipelineTest {
        std::string description;
        std::function<bool()> errorCondition;
        std::function<bool()> recoveryTest;
    };
    
    std::vector<PipelineTest> pipelineTests = {
        {
            "Language Detection Error",
            [this]() {
                // Cause language detection error
                auto result = languageDetector_->detectLanguage("");
                return !result.isReliable;
            },
            [this]() {
                // Translation should still work with explicit language
                translator_->initialize("en", "es");
                auto result = translator_->translate("Recovery test");
                return result.success;
            }
        },
        {
            "Translation Error",
            [this]() {
                // Cause translation error
                translator_->initialize("en", "es");
                auto result = translator_->translate("");
                return !result.success;
            },
            [this]() {
                // Language detection should still work
                auto result = languageDetector_->detectLanguage("Hello world");
                return result.confidence > 0.3f;
            }
        },
        {
            "Quality Assessment Error",
            [this]() {
                // Cause quality assessment error with invalid input
                auto quality = qualityManager_->assessTranslationQuality("", "", "en", "es");
                return quality.overallConfidence == 0.0f;
            },
            [this]() {
                // Translation should still work
                translator_->initialize("en", "es");
                auto result = translator_->translate("Quality error recovery test");
                return result.success;
            }
        }
    };
    
    int isolationSuccessCount = 0;
    
    for (const auto& test : pipelineTests) {
        std::cout << "Testing pipeline isolation: " << test.description << std::endl;
        
        // Trigger error condition
        bool errorTriggered = test.errorCondition();
        EXPECT_TRUE(errorTriggered) << "Error condition should have been triggered";
        
        if (errorTriggered) {
            // Test that other components still work
            bool recoverySuccessful = test.recoveryTest();
            
            if (recoverySuccessful) {
                isolationSuccessCount++;
                std::cout << "  Isolation successful - other components unaffected" << std::endl;
            } else {
                std::cout << "  Isolation failed - error propagated to other components" << std::endl;
            }
        }
    }
    
    // All pipeline components should be isolated from each other's errors
    EXPECT_EQ(isolationSuccessCount, pipelineTests.size());
    
    // Final integration test - all components should work together after errors
    std::cout << "Testing full pipeline integration after errors..." << std::endl;
    
    ASSERT_TRUE(translator_->initialize("en", "es"));
    
    std::string testText = "Final integration test after error isolation testing.";
    
    // Full pipeline: Detection -> Translation -> Quality Assessment
    auto detection = languageDetector_->detectLanguage(testText);
    auto translation = translator_->translate(testText);
    auto quality = qualityManager_->assessTranslationQuality(
        testText, translation.translatedText, "en", "es");
    
    EXPECT_TRUE(translation.success);
    EXPECT_GT(quality.overallConfidence, 0.0f);
    
    std::cout << "Full pipeline integration: " << (translation.success ? "PASSED" : "FAILED") << std::endl;
}

} // namespace integration
} // namespace speechrnt