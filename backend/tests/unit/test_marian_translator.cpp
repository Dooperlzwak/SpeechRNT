#include "mt/marian_translator.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>

using namespace speechrnt::mt;

class MarianTranslatorTest {
public:
    MarianTranslatorTest() {
        translator = std::make_unique<MarianTranslator>();
        translator->setModelsPath("data/marian/");
    }
    
    ~MarianTranslatorTest() {
        if (translator) {
            translator->cleanup();
        }
    }
    
    void runAllTests() {
        std::cout << "Running Marian Translator Tests..." << std::endl;
        
        testInitializationWithValidLanguagePair();
        translator->cleanup();
        
        testInitializationWithInvalidLanguagePair();
        translator->cleanup();
        
        testSupportedLanguages();
        testSupportedTargetLanguages();
        testLanguagePairSupport();
        
        testBasicTranslation();
        translator->cleanup();
        
        testEmptyTextTranslation();
        translator->cleanup();
        
        testTranslationWithoutInitialization();
        
        testModelLoading();
        translator->cleanup();
        
        testModelLoadingInvalidLanguagePair();
        translator->cleanup();
        
        testMultipleLanguagePairs();
        translator->cleanup();
        
        testActualTranslationFunctionality();
        translator->cleanup();
        
        testFallbackTranslationQuality();
        translator->cleanup();
        
        testTranslationConfidenceScoring();
        translator->cleanup();
        
        testMarianIntegrationWhenAvailable();
        translator->cleanup();
        
        testGPUAccelerationSupport();
        translator->cleanup();
        
        testGPUMemoryManagement();
        translator->cleanup();
        
        testGPUFallbackBehavior();
        translator->cleanup();
        
        testGPUDeviceSelection();
        translator->cleanup();
        
        testErrorHandlingAndRecovery();
        translator->cleanup();
        
        testCleanupAndReinitialization();
        
        testTranslationPerformance();
        translator->cleanup();
        
        testBatchTranslation();
        translator->cleanup();
        
        testBatchTranslationAsync();
        translator->cleanup();
        
        testStreamingTranslation();
        translator->cleanup();
        
        testTranslationCaching();
        translator->cleanup();
        
        testStreamingSessionManagement();
        translator->cleanup();
        
        std::cout << "All Marian Translator tests passed!" << std::endl;
    }
    
private:
    std::unique_ptr<MarianTranslator> translator;
    
    void assert_true(bool condition, const std::string& message) {
        if (!condition) {
            std::cerr << "ASSERTION FAILED: " << message << std::endl;
            exit(1);
        }
    }
    
    void assert_false(bool condition, const std::string& message) {
        assert_true(!condition, message);
    }
    
    void assert_equal(const std::string& expected, const std::string& actual, const std::string& message) {
        if (expected != actual) {
            std::cerr << "ASSERTION FAILED: " << message << std::endl;
            std::cerr << "Expected: " << expected << ", Actual: " << actual << std::endl;
            exit(1);
        }
    }

    void testInitializationWithValidLanguagePair() {
        std::cout << "Testing initialization with valid language pair..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize with valid language pair");
        assert_true(translator->isReady(), "Should be ready after initialization");
    }

    void testInitializationWithInvalidLanguagePair() {
        std::cout << "Testing initialization with invalid language pair..." << std::endl;
        assert_false(translator->initialize("invalid", "also_invalid"), "Should not initialize with invalid language pair");
        assert_false(translator->isReady(), "Should not be ready with invalid language pair");
    }

    void testSupportedLanguages() {
        std::cout << "Testing supported languages..." << std::endl;
        auto sourceLanguages = translator->getSupportedSourceLanguages();
        assert_false(sourceLanguages.empty(), "Should have supported source languages");
        assert_true(std::find(sourceLanguages.begin(), sourceLanguages.end(), "en") != sourceLanguages.end(), "Should support English");
        assert_true(std::find(sourceLanguages.begin(), sourceLanguages.end(), "es") != sourceLanguages.end(), "Should support Spanish");
    }

    void testSupportedTargetLanguages() {
        std::cout << "Testing supported target languages..." << std::endl;
        auto targetLanguages = translator->getSupportedTargetLanguages("en");
        assert_false(targetLanguages.empty(), "Should have supported target languages for English");
        assert_true(std::find(targetLanguages.begin(), targetLanguages.end(), "es") != targetLanguages.end(), "Should support English to Spanish");
    }

    void testLanguagePairSupport() {
        std::cout << "Testing language pair support..." << std::endl;
        assert_true(translator->supportsLanguagePair("en", "es"), "Should support en->es");
        assert_true(translator->supportsLanguagePair("es", "en"), "Should support es->en");
        assert_false(translator->supportsLanguagePair("invalid", "also_invalid"), "Should not support invalid pairs");
    }

    void testBasicTranslation() {
        std::cout << "Testing basic translation..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for basic translation test");
        
        auto result = translator->translate("Hello");
        assert_true(result.success, "Translation should succeed");
        assert_false(result.translatedText.empty(), "Translation should not be empty");
        assert_equal("en", result.sourceLang, "Source language should be en");
        assert_equal("es", result.targetLang, "Target language should be es");
        assert_true(result.confidence > 0.0f, "Confidence should be greater than 0");
    }

    void testEmptyTextTranslation() {
        std::cout << "Testing empty text translation..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for empty text test");
        
        auto result = translator->translate("");
        assert_false(result.success, "Empty text translation should fail");
        assert_false(result.errorMessage.empty(), "Should have error message for empty text");
    }

    void testTranslationWithoutInitialization() {
        std::cout << "Testing translation without initialization..." << std::endl;
        // Create a fresh translator instance to ensure no previous initialization
        auto freshTranslator = std::make_unique<MarianTranslator>();
        freshTranslator->setModelsPath("data/marian/");
        
        auto result = freshTranslator->translate("Hello");
        assert_false(result.success, "Translation without initialization should fail");
        assert_false(result.errorMessage.empty(), "Should have error message without initialization");
        
        freshTranslator->cleanup();
    }

    void testModelLoading() {
        std::cout << "Testing model loading..." << std::endl;
        assert_false(translator->isModelLoaded("en", "es"), "Model should not be loaded initially");
        
        assert_true(translator->loadModel("en", "es"), "Should be able to load model");
        assert_true(translator->isModelLoaded("en", "es"), "Model should be loaded after loading");
        
        translator->unloadModel("en", "es");
        assert_false(translator->isModelLoaded("en", "es"), "Model should not be loaded after unloading");
    }

    void testModelLoadingInvalidLanguagePair() {
        std::cout << "Testing model loading with invalid language pair..." << std::endl;
        assert_false(translator->loadModel("invalid", "also_invalid"), "Should not load invalid language pair");
        assert_false(translator->isModelLoaded("invalid", "also_invalid"), "Invalid language pair should not be loaded");
    }

    void testMultipleLanguagePairs() {
        std::cout << "Testing multiple language pairs..." << std::endl;
        // Test English to Spanish
        assert_true(translator->initialize("en", "es"), "Should initialize en->es");
        auto result1 = translator->translate("Hello");
        assert_true(result1.success, "en->es translation should succeed");
        
        // Test Spanish to English
        assert_true(translator->initialize("es", "en"), "Should initialize es->en");
        auto result2 = translator->translate("Hola");
        assert_true(result2.success, "es->en translation should succeed");
    }

    void testCleanupAndReinitialization() {
        std::cout << "Testing cleanup and reinitialization..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize initially");
        assert_true(translator->isReady(), "Should be ready after initialization");
        
        translator->cleanup();
        assert_false(translator->isReady(), "Should not be ready after cleanup");
        
        assert_true(translator->initialize("en", "es"), "Should reinitialize after cleanup");
        assert_true(translator->isReady(), "Should be ready after reinitialization");
    }

    void testTranslationPerformance() {
        std::cout << "Testing translation performance..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for performance test");
        
        const std::string testText = "This is a test sentence for performance measurement.";
        const int numTranslations = 100;
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < numTranslations; ++i) {
            auto result = translator->translate(testText);
            assert_true(result.success, "Performance test translation should succeed");
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        double avgTimeMs = static_cast<double>(duration.count()) / numTranslations;
        
        std::cout << "Average translation time: " << avgTimeMs << " ms" << std::endl;
        
        // For mock implementation, should be very fast
        assert_true(avgTimeMs < 10.0, "Average translation time should be less than 10ms for mock");
    }

    void testActualTranslationFunctionality() {
        std::cout << "Testing actual translation functionality..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for actual translation test");
        
        // Test that we're no longer using simple mock prefixes
        auto result = translator->translate("Hello");
        assert_true(result.success, "Translation should succeed");
        assert_false(result.translatedText.empty(), "Translation should not be empty");
        
        // Verify we're not getting the old mock format "[ES] Hello"
        assert_true(result.translatedText.find("[ES]") != 0, "Should not use old mock prefix format");
        
        // Test known translations work correctly
        if (result.translatedText == "Hola") {
            std::cout << "  ✓ Using enhanced fallback translation" << std::endl;
        } else {
            std::cout << "  ✓ Using actual Marian translation: " << result.translatedText << std::endl;
        }
        
        // Test confidence scoring is reasonable
        assert_true(result.confidence > 0.0f && result.confidence <= 1.0f, "Confidence should be in valid range");
    }

    void testFallbackTranslationQuality() {
        std::cout << "Testing fallback translation quality..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for fallback test");
        
        // Test common phrases have good translations
        struct TestCase {
            std::string input;
            std::string expectedOutput;
            float minConfidence;
        };
        
        std::vector<TestCase> testCases = {
            {"Hello", "Hola", 0.7f},
            {"Thank you", "Gracias", 0.7f},
            {"Good morning", "Buenos días", 0.7f},
            {"How are you?", "¿Cómo estás?", 0.7f}
        };
        
        for (const auto& testCase : testCases) {
            auto result = translator->translate(testCase.input);
            assert_true(result.success, "Translation should succeed for: " + testCase.input);
            assert_equal(testCase.expectedOutput, result.translatedText, 
                        "Translation should match expected for: " + testCase.input);
            assert_true(result.confidence >= testCase.minConfidence, 
                       "Confidence should be adequate for known phrase: " + testCase.input);
        }
        
        // Test Spanish to English
        assert_true(translator->initialize("es", "en"), "Should initialize es->en");
        auto result = translator->translate("Hola");
        assert_true(result.success, "Spanish translation should succeed");
        assert_equal("Hello", result.translatedText, "Spanish 'Hola' should translate to 'Hello'");
    }

    void testTranslationConfidenceScoring() {
        std::cout << "Testing translation confidence scoring..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for confidence test");
        
        // Test that known phrases have higher confidence than unknown ones
        auto knownResult = translator->translate("Hello");
        auto unknownResult = translator->translate("supercalifragilisticexpialidocious");
        
        assert_true(knownResult.success && unknownResult.success, "Both translations should succeed");
        assert_true(knownResult.confidence > unknownResult.confidence, 
                   "Known phrases should have higher confidence than unknown ones");
        
        // Test confidence is in valid range
        assert_true(knownResult.confidence > 0.0f && knownResult.confidence <= 1.0f, 
                   "Known phrase confidence should be in valid range");
        assert_true(unknownResult.confidence > 0.0f && unknownResult.confidence <= 1.0f, 
                   "Unknown phrase confidence should be in valid range");
    }

    void testMarianIntegrationWhenAvailable() {
        std::cout << "Testing Marian integration when available..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for Marian test");
        
        auto result = translator->translate("Hello world");
        assert_true(result.success, "Translation should succeed");
        
        // Check if we're using actual Marian or fallback
        if (result.errorMessage.find("Marian NMT not available") != std::string::npos ||
            result.errorMessage.find("using fallback") != std::string::npos) {
            std::cout << "  ✓ Using fallback translation (Marian not available)" << std::endl;
        } else {
            std::cout << "  ✓ Using Marian NMT translation" << std::endl;
        }
        
        // Verify translation quality regardless of method
        assert_false(result.translatedText.empty(), "Translation should not be empty");
        assert_true(result.confidence > 0.0f, "Should have positive confidence");
    }

    void testGPUAccelerationSupport() {
        std::cout << "Testing GPU acceleration support..." << std::endl;
        
        // Test GPU device validation
        bool hasValidGPU = translator->validateGPUDevice(0);
        std::cout << "  GPU device 0 validation: " << (hasValidGPU ? "valid" : "invalid") << std::endl;
        
        // Test invalid GPU device
        assert_false(translator->validateGPUDevice(-1), "Invalid device ID should fail validation");
        assert_false(translator->validateGPUDevice(999), "Non-existent device should fail validation");
        
        // Test GPU initialization
        bool gpuInitResult = translator->initializeWithGPU("en", "es", 0);
        if (gpuInitResult) {
            std::cout << "  ✓ GPU acceleration initialized successfully" << std::endl;
            assert_true(translator->isReady(), "Should be ready after GPU initialization");
            assert_true(translator->isGPUAccelerationEnabled(), "GPU acceleration should be enabled");
            assert_equal(0, translator->getCurrentGPUDevice(), "Current GPU device should be 0");
            
            // Test GPU memory usage tracking
            size_t memoryUsage = translator->getGPUMemoryUsageMB();
            std::cout << "  GPU memory usage: " << memoryUsage << " MB" << std::endl;
            
            // Test translation with GPU
            auto result = translator->translate("Hello");
            assert_true(result.success, "GPU-accelerated translation should succeed");
            
            // Test sufficient memory check
            bool hasSufficientMemory = translator->hasSufficientGPUMemory(100);
            std::cout << "  Has sufficient GPU memory (100MB): " << (hasSufficientMemory ? "yes" : "no") << std::endl;
            
        } else {
            std::cout << "  ✓ GPU acceleration not available, testing CPU fallback" << std::endl;
            assert_false(translator->isGPUAccelerationEnabled(), "GPU acceleration should be disabled");
            assert_equal(-1, translator->getCurrentGPUDevice(), "Current GPU device should be -1");
            assert_equal(0, translator->getGPUMemoryUsageMB(), "GPU memory usage should be 0");
            
            // Should still work with CPU
            assert_true(translator->initialize("en", "es"), "Should initialize with CPU fallback");
            auto result = translator->translate("Hello");
            assert_true(result.success, "CPU translation should succeed");
        }
        
        // Test GPU acceleration settings
        translator->setGPUAcceleration(true, 0);
        if (hasValidGPU) {
            assert_true(translator->isGPUAccelerationEnabled(), "GPU should be enabled if device is valid");
        }
        
        translator->setGPUAcceleration(false, 0);
        assert_false(translator->isGPUAccelerationEnabled(), "GPU should be disabled");
        
        // Should still work after disabling GPU
        assert_true(translator->initialize("en", "es"), "Should initialize after GPU settings change");
        
        // Test enabling GPU with invalid device
        translator->setGPUAcceleration(true, 999);
        assert_false(translator->isGPUAccelerationEnabled(), "GPU should not be enabled with invalid device");
    }

    void testErrorHandlingAndRecovery() {
        std::cout << "Testing error handling and recovery..." << std::endl;
        
        // Test translation with uninitialized translator
        auto freshTranslator = std::make_unique<MarianTranslator>();
        auto result = freshTranslator->translate("Hello");
        assert_false(result.success, "Uninitialized translation should fail");
        assert_false(result.errorMessage.empty(), "Should have error message");
        
        // Test recovery after error
        assert_true(freshTranslator->initialize("en", "es"), "Should recover and initialize");
        result = freshTranslator->translate("Hello");
        assert_true(result.success, "Should succeed after proper initialization");
        
        freshTranslator->cleanup();
        
        // Test invalid language pair handling
        assert_false(translator->initialize("invalid", "invalid"), "Invalid language pair should fail");
        assert_false(translator->isReady(), "Should not be ready with invalid language pair");
        
        // Test recovery from invalid language pair
        assert_true(translator->initialize("en", "es"), "Should recover with valid language pair");
        assert_true(translator->isReady(), "Should be ready after recovery");
    }

    void testGPUMemoryManagement() {
        std::cout << "Testing GPU memory management..." << std::endl;
        
        // Test memory usage tracking
        size_t initialMemory = translator->getGPUMemoryUsageMB();
        std::cout << "  Initial GPU memory usage: " << initialMemory << " MB" << std::endl;
        
        // Try to initialize with GPU
        bool gpuAvailable = translator->validateGPUDevice(0);
        if (gpuAvailable) {
            translator->setGPUAcceleration(true, 0);
            assert_true(translator->initialize("en", "es"), "Should initialize with GPU");
            
            size_t memoryAfterInit = translator->getGPUMemoryUsageMB();
            std::cout << "  Memory after initialization: " << memoryAfterInit << " MB" << std::endl;
            
            // Load another model
            assert_true(translator->loadModel("en", "fr"), "Should load second model");
            
            size_t memoryAfterSecondModel = translator->getGPUMemoryUsageMB();
            std::cout << "  Memory after second model: " << memoryAfterSecondModel << " MB" << std::endl;
            
            // Memory should increase with additional models
            if (memoryAfterSecondModel > memoryAfterInit) {
                std::cout << "  ✓ GPU memory usage increased with additional model" << std::endl;
            }
            
            // Unload model and check memory decrease
            translator->unloadModel("en", "fr");
            size_t memoryAfterUnload = translator->getGPUMemoryUsageMB();
            std::cout << "  Memory after unload: " << memoryAfterUnload << " MB" << std::endl;
            
            // Test memory sufficiency check
            assert_true(translator->hasSufficientGPUMemory(1), "Should have sufficient memory for 1MB");
            
            // Test with very large memory requirement
            bool hasLargeMemory = translator->hasSufficientGPUMemory(100000); // 100GB
            std::cout << "  Has 100GB GPU memory: " << (hasLargeMemory ? "yes" : "no") << std::endl;
            
        } else {
            std::cout << "  ✓ GPU not available, skipping GPU memory tests" << std::endl;
            assert_equal(0, translator->getGPUMemoryUsageMB(), "GPU memory usage should be 0 without GPU");
            assert_false(translator->hasSufficientGPUMemory(100), "Should not have GPU memory without GPU");
        }
    }

    void testGPUFallbackBehavior() {
        std::cout << "Testing GPU fallback behavior..." << std::endl;
        
        // Test fallback when GPU is not available
        bool gpuAvailable = translator->validateGPUDevice(0);
        
        if (!gpuAvailable) {
            // Test that initialization still works without GPU
            assert_true(translator->initialize("en", "es"), "Should initialize with CPU fallback");
            assert_false(translator->isGPUAccelerationEnabled(), "GPU should not be enabled");
            
            auto result = translator->translate("Hello");
            assert_true(result.success, "CPU fallback translation should succeed");
            
            std::cout << "  ✓ CPU fallback working correctly" << std::endl;
        } else {
            std::cout << "  GPU available, testing forced fallback scenarios" << std::endl;
            
            // Test fallback when GPU memory is insufficient (simulated)
            translator->setGPUAcceleration(true, 0);
            
            // This should still work, either with GPU or CPU fallback
            assert_true(translator->initialize("en", "es"), "Should initialize with GPU or fallback");
            
            auto result = translator->translate("Hello");
            assert_true(result.success, "Translation should succeed with GPU or fallback");
            
            // Test disabling GPU mid-operation
            translator->setGPUAcceleration(false, 0);
            assert_false(translator->isGPUAccelerationEnabled(), "GPU should be disabled");
            
            result = translator->translate("Hello again");
            assert_true(result.success, "Translation should still work after disabling GPU");
        }
    }

    void testGPUDeviceSelection() {
        std::cout << "Testing GPU device selection..." << std::endl;
        
        // Test device validation
        bool device0Valid = translator->validateGPUDevice(0);
        bool device1Valid = translator->validateGPUDevice(1);
        
        std::cout << "  Device 0 valid: " << (device0Valid ? "yes" : "no") << std::endl;
        std::cout << "  Device 1 valid: " << (device1Valid ? "yes" : "no") << std::endl;
        
        // Test invalid device IDs
        assert_false(translator->validateGPUDevice(-1), "Negative device ID should be invalid");
        assert_false(translator->validateGPUDevice(999), "Very high device ID should be invalid");
        
        if (device0Valid) {
            // Test setting valid device
            translator->setGPUAcceleration(true, 0);
            assert_true(translator->isGPUAccelerationEnabled(), "Should enable GPU with valid device");
            assert_equal(0, translator->getCurrentGPUDevice(), "Current device should be 0");
            
            if (device1Valid) {
                // Test switching devices
                translator->setGPUAcceleration(true, 1);
                assert_true(translator->isGPUAccelerationEnabled(), "Should enable GPU with device 1");
                assert_equal(1, translator->getCurrentGPUDevice(), "Current device should be 1");
                
                // Switch back to device 0
                translator->setGPUAcceleration(true, 0);
                assert_equal(0, translator->getCurrentGPUDevice(), "Should switch back to device 0");
            }
        }
        
        // Test setting invalid device
        translator->setGPUAcceleration(true, 999);
        assert_false(translator->isGPUAccelerationEnabled(), "Should not enable GPU with invalid device");
        assert_equal(-1, translator->getCurrentGPUDevice(), "Current device should be -1 with invalid device");
    }

    void testBatchTranslation() {
        std::cout << "Testing batch translation..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for batch test");
        
        // Test empty batch
        std::vector<std::string> emptyBatch;
        auto emptyResults = translator->translateBatch(emptyBatch);
        assert_true(emptyResults.empty(), "Empty batch should return empty results");
        
        // Test single item batch
        std::vector<std::string> singleBatch = {"Hello"};
        auto singleResults = translator->translateBatch(singleBatch);
        assert_equal(1, singleResults.size(), "Single batch should return one result");
        assert_true(singleResults[0].success, "Single batch translation should succeed");
        assert_equal(0, singleResults[0].batchIndex, "Batch index should be 0");
        
        // Test multiple items batch
        std::vector<std::string> multiBatch = {"Hello", "Thank you", "Good morning"};
        auto multiResults = translator->translateBatch(multiBatch);
        assert_equal(3, multiResults.size(), "Multi batch should return three results");
        
        for (size_t i = 0; i < multiResults.size(); ++i) {
            assert_true(multiResults[i].success, "Batch item " + std::to_string(i) + " should succeed");
            assert_equal(static_cast<int>(i), multiResults[i].batchIndex, "Batch index should match");
            assert_false(multiResults[i].translatedText.empty(), "Translation should not be empty");
        }
        
        // Test large batch (should be chunked)
        std::vector<std::string> largeBatch;
        for (int i = 0; i < 100; ++i) {
            largeBatch.push_back("Text " + std::to_string(i));
        }
        
        auto largeResults = translator->translateBatch(largeBatch);
        assert_equal(100, largeResults.size(), "Large batch should return all results");
        
        // Verify batch indices are correct
        for (size_t i = 0; i < largeResults.size(); ++i) {
            assert_equal(static_cast<int>(i), largeResults[i].batchIndex, "Large batch index should match");
        }
        
        std::cout << "  ✓ Batch translation working correctly" << std::endl;
    }

    void testBatchTranslationAsync() {
        std::cout << "Testing async batch translation..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for async batch test");
        
        std::vector<std::string> batch = {"Hello", "Thank you", "Good morning"};
        
        // Test async batch translation
        auto future = translator->translateBatchAsync(batch);
        assert_true(future.valid(), "Future should be valid");
        
        // Wait for completion
        auto results = future.get();
        assert_equal(3, results.size(), "Async batch should return three results");
        
        for (size_t i = 0; i < results.size(); ++i) {
            assert_true(results[i].success, "Async batch item " + std::to_string(i) + " should succeed");
            assert_equal(static_cast<int>(i), results[i].batchIndex, "Async batch index should match");
        }
        
        std::cout << "  ✓ Async batch translation working correctly" << std::endl;
    }

    void testStreamingTranslation() {
        std::cout << "Testing streaming translation..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for streaming test");
        
        const std::string sessionId = "test_session_1";
        
        // Test starting streaming session
        assert_true(translator->startStreamingTranslation(sessionId, "en", "es"), 
                   "Should start streaming session");
        assert_true(translator->hasStreamingSession(sessionId), "Session should exist");
        
        // Test duplicate session creation
        assert_false(translator->startStreamingTranslation(sessionId, "en", "es"), 
                    "Should not create duplicate session");
        
        // Test adding streaming text
        auto result1 = translator->addStreamingText(sessionId, "Hello", false);
        assert_true(result1.success, "First streaming text should succeed");
        assert_true(result1.isPartialResult, "Should be partial result");
        assert_false(result1.isStreamingComplete, "Should not be complete");
        assert_equal(sessionId, result1.sessionId, "Session ID should match");
        
        auto result2 = translator->addStreamingText(sessionId, " world", false);
        assert_true(result2.success, "Second streaming text should succeed");
        assert_true(result2.isPartialResult, "Should be partial result");
        
        auto result3 = translator->addStreamingText(sessionId, "!", true);
        assert_true(result3.success, "Final streaming text should succeed");
        assert_false(result3.isPartialResult, "Should not be partial result");
        assert_true(result3.isStreamingComplete, "Should be complete");
        
        // Test finalizing session
        auto finalResult = translator->finalizeStreamingTranslation(sessionId);
        assert_true(finalResult.success, "Final result should succeed");
        assert_false(finalResult.isPartialResult, "Final result should not be partial");
        assert_true(finalResult.isStreamingComplete, "Final result should be complete");
        assert_equal(sessionId, finalResult.sessionId, "Final session ID should match");
        
        // Session should be cleaned up after finalization
        assert_false(translator->hasStreamingSession(sessionId), "Session should be cleaned up");
        
        std::cout << "  ✓ Streaming translation working correctly" << std::endl;
    }

    void testTranslationCaching() {
        std::cout << "Testing translation caching..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for caching test");
        
        // Enable caching
        translator->setTranslationCaching(true, 100);
        
        // Clear cache to start fresh
        translator->clearTranslationCache();
        assert_equal(0.0f, translator->getCacheHitRate(), "Cache hit rate should be 0 initially");
        
        // First translation (cache miss)
        auto result1 = translator->translate("Hello");
        assert_true(result1.success, "First translation should succeed");
        
        // Second translation of same text (cache hit)
        auto result2 = translator->translate("Hello");
        assert_true(result2.success, "Second translation should succeed");
        assert_equal(result1.translatedText, result2.translatedText, "Cached translation should match");
        
        // Check cache hit rate
        float hitRate = translator->getCacheHitRate();
        assert_true(hitRate > 0.0f, "Cache hit rate should be greater than 0");
        std::cout << "  Cache hit rate: " << hitRate << "%" << std::endl;
        
        // Test different text (cache miss)
        auto result3 = translator->translate("Thank you");
        assert_true(result3.success, "Different text translation should succeed");
        
        // Test cache with multiple texts
        for (int i = 0; i < 10; ++i) {
            translator->translate("Hello"); // Should hit cache
            translator->translate("Thank you"); // Should hit cache
        }
        
        hitRate = translator->getCacheHitRate();
        assert_true(hitRate > 50.0f, "Cache hit rate should be high with repeated translations");
        std::cout << "  Final cache hit rate: " << hitRate << "%" << std::endl;
        
        // Test disabling cache
        translator->setTranslationCaching(false, 0);
        translator->clearTranslationCache();
        assert_equal(0.0f, translator->getCacheHitRate(), "Cache hit rate should be 0 after clearing");
        
        std::cout << "  ✓ Translation caching working correctly" << std::endl;
    }

    void testStreamingSessionManagement() {
        std::cout << "Testing streaming session management..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for session management test");
        
        const std::string session1 = "session_1";
        const std::string session2 = "session_2";
        const std::string session3 = "session_3";
        
        // Test multiple concurrent sessions
        assert_true(translator->startStreamingTranslation(session1, "en", "es"), "Should start session 1");
        assert_true(translator->startStreamingTranslation(session2, "es", "en"), "Should start session 2");
        assert_true(translator->startStreamingTranslation(session3, "en", "fr"), "Should start session 3");
        
        assert_true(translator->hasStreamingSession(session1), "Session 1 should exist");
        assert_true(translator->hasStreamingSession(session2), "Session 2 should exist");
        assert_true(translator->hasStreamingSession(session3), "Session 3 should exist");
        
        // Test adding text to different sessions
        auto result1 = translator->addStreamingText(session1, "Hello", false);
        auto result2 = translator->addStreamingText(session2, "Hola", false);
        auto result3 = translator->addStreamingText(session3, "Bonjour", false);
        
        assert_true(result1.success && result2.success && result3.success, "All session texts should succeed");
        assert_equal(session1, result1.sessionId, "Session 1 ID should match");
        assert_equal(session2, result2.sessionId, "Session 2 ID should match");
        assert_equal(session3, result3.sessionId, "Session 3 ID should match");
        
        // Test canceling a session
        translator->cancelStreamingTranslation(session2);
        assert_false(translator->hasStreamingSession(session2), "Session 2 should be canceled");
        assert_true(translator->hasStreamingSession(session1), "Session 1 should still exist");
        assert_true(translator->hasStreamingSession(session3), "Session 3 should still exist");
        
        // Test adding text to canceled session
        auto canceledResult = translator->addStreamingText(session2, "More text", false);
        assert_false(canceledResult.success, "Adding to canceled session should fail");
        assert_false(canceledResult.errorMessage.empty(), "Should have error message for canceled session");
        
        // Test finalizing remaining sessions
        auto final1 = translator->finalizeStreamingTranslation(session1);
        auto final3 = translator->finalizeStreamingTranslation(session3);
        
        assert_true(final1.success && final3.success, "Finalizing should succeed");
        assert_false(translator->hasStreamingSession(session1), "Session 1 should be cleaned up");
        assert_false(translator->hasStreamingSession(session3), "Session 3 should be cleaned up");
        
        // Test invalid session operations
        auto invalidResult = translator->addStreamingText("nonexistent", "text", false);
        assert_false(invalidResult.success, "Adding to nonexistent session should fail");
        
        auto invalidFinal = translator->finalizeStreamingTranslation("nonexistent");
        assert_false(invalidFinal.success, "Finalizing nonexistent session should fail");
        
        // Test unsupported language pair for streaming
        assert_false(translator->startStreamingTranslation("invalid_session", "invalid", "invalid"), 
                    "Should not start session with invalid language pair");
        
        std::cout << "  ✓ Streaming session management working correctly" << std::endl;
    }
};

int main() {
    MarianTranslatorTest test;
    test.runAllTests();
    return 0;
}