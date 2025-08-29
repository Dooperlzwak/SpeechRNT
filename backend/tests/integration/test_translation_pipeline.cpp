#include "mt/marian_translator.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <future>
#include <cassert>

using namespace speechrnt::mt;

class TranslationPipelineTest {
public:
    TranslationPipelineTest() {
        translator = std::make_unique<MarianTranslator>();
        translator->setModelsPath("data/marian/");
    }
    
    ~TranslationPipelineTest() {
        if (translator) {
            translator->cleanup();
        }
    }
    
    void runAllTests() {
        std::cout << "Running Translation Pipeline Integration Tests..." << std::endl;
        
        testEndToEndTranslationFlow();
        testBidirectionalTranslation();
        testMultipleLanguagePairSwitching();
        testLongTextTranslation();
        testSpecialCharactersAndPunctuation();
        testErrorRecoveryAndResilience();
        testPerformanceBenchmark();
        
        std::cout << "All Translation Pipeline integration tests passed!" << std::endl;
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

    void testEndToEndTranslationFlow() {
        std::cout << "Testing end-to-end translation flow..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for end-to-end test");
        
        std::vector<std::string> testPhrases = {
            "Hello",
            "How are you?",
            "Good morning",
            "Thank you",
            "Goodbye"
        };
        
        for (const auto& phrase : testPhrases) {
            auto result = translator->translate(phrase);
            assert_true(result.success, "Failed to translate: " + phrase);
            assert_false(result.translatedText.empty(), "Empty translation for: " + phrase);
            assert_true(result.sourceLang == "en", "Source language should be en");
            assert_true(result.targetLang == "es", "Target language should be es");
            assert_true(result.confidence > 0.0f, "Zero confidence for: " + phrase);
            
            std::cout << "'" << phrase << "' -> '" << result.translatedText << "' (confidence: " 
                      << result.confidence << ")" << std::endl;
        }
    }

    void testBidirectionalTranslation() {
        std::cout << "Testing bidirectional translation..." << std::endl;
        // Test English to Spanish
        assert_true(translator->initialize("en", "es"), "Should initialize en->es");
        auto enToEs = translator->translate("Hello");
        assert_true(enToEs.success, "en->es translation should succeed");
        
        // Test Spanish to English
        assert_true(translator->initialize("es", "en"), "Should initialize es->en");
        auto esToEn = translator->translate("Hola");
        assert_true(esToEn.success, "es->en translation should succeed");
        
        std::cout << "EN->ES: 'Hello' -> '" << enToEs.translatedText << "'" << std::endl;
        std::cout << "ES->EN: 'Hola' -> '" << esToEn.translatedText << "'" << std::endl;
    }

TEST_F(TranslationPipelineTest, MultipleLanguagePairSwitching) {
    // Test switching between different language pairs
    std::vector<std::pair<std::string, std::string>> languagePairs = {
        {"en", "es"},
        {"en", "fr"},
        {"en", "de"},
        {"es", "en"}
    };
    
    for (const auto& pair : languagePairs) {
        ASSERT_TRUE(translator->initialize(pair.first, pair.second)) 
            << "Failed to initialize " << pair.first << " -> " << pair.second;
        
        auto result = translator->translate("Hello");
        EXPECT_TRUE(result.success) 
            << "Translation failed for " << pair.first << " -> " << pair.second;
        EXPECT_EQ(result.sourceLang, pair.first);
        EXPECT_EQ(result.targetLang, pair.second);
        
        std::cout << pair.first << "->" << pair.second << ": 'Hello' -> '" 
                  << result.translatedText << "'" << std::endl;
    }
}

TEST_F(TranslationPipelineTest, ConcurrentTranslationRequests) {
    ASSERT_TRUE(translator->initialize("en", "es"));
    
    const int numConcurrentRequests = 10;
    std::vector<std::future<TranslationResult>> futures;
    
    // Launch concurrent translation requests
    for (int i = 0; i < numConcurrentRequests; ++i) {
        std::string text = "Hello world " + std::to_string(i);
        futures.push_back(translator->translateAsync(text));
    }
    
    // Collect results
    std::vector<TranslationResult> results;
    for (auto& future : futures) {
        results.push_back(future.get());
    }
    
    // Verify all translations succeeded
    for (size_t i = 0; i < results.size(); ++i) {
        EXPECT_TRUE(results[i].success) << "Translation " << i << " failed";
        EXPECT_FALSE(results[i].translatedText.empty()) << "Empty translation " << i;
        EXPECT_EQ(results[i].sourceLang, "en");
        EXPECT_EQ(results[i].targetLang, "es");
    }
}

TEST_F(TranslationPipelineTest, LongTextTranslation) {
    ASSERT_TRUE(translator->initialize("en", "es"));
    
    std::string longText = "This is a longer text that contains multiple sentences. "
                          "It should be handled properly by the translation system. "
                          "The system should maintain context and provide accurate translations "
                          "even for longer input texts that might contain complex grammar "
                          "and various linguistic structures.";
    
    auto result = translator->translate(longText);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.translatedText.empty());
    EXPECT_GT(result.translatedText.length(), longText.length() * 0.5); // Reasonable length check
    
    std::cout << "Long text translation:" << std::endl;
    std::cout << "Original: " << longText << std::endl;
    std::cout << "Translated: " << result.translatedText << std::endl;
}

TEST_F(TranslationPipelineTest, SpecialCharactersAndPunctuation) {
    ASSERT_TRUE(translator->initialize("en", "es"));
    
    std::vector<std::string> specialTexts = {
        "Hello, world!",
        "What's your name?",
        "I'm fine, thank you.",
        "Numbers: 1, 2, 3, 100, 1000",
        "Email: test@example.com",
        "Price: $19.99"
    };
    
    for (const auto& text : specialTexts) {
        auto result = translator->translate(text);
        EXPECT_TRUE(result.success) << "Failed to translate: " << text;
        EXPECT_FALSE(result.translatedText.empty()) << "Empty translation for: " << text;
        
        std::cout << "'" << text << "' -> '" << result.translatedText << "'" << std::endl;
    }
}

TEST_F(TranslationPipelineTest, ErrorRecoveryAndResilience) {
    ASSERT_TRUE(translator->initialize("en", "es"));
    
    // Test empty string handling
    auto emptyResult = translator->translate("");
    EXPECT_FALSE(emptyResult.success);
    
    // Test that translator still works after error
    auto validResult = translator->translate("Hello");
    EXPECT_TRUE(validResult.success);
    
    // Test very long string (potential memory issues)
    std::string veryLongText(10000, 'a');
    auto longResult = translator->translate(veryLongText);
    // Should either succeed or fail gracefully
    if (!longResult.success) {
        EXPECT_FALSE(longResult.errorMessage.empty());
    }
    
    // Test that translator still works after potential error
    auto recoveryResult = translator->translate("Hello again");
    EXPECT_TRUE(recoveryResult.success);
}

TEST_F(TranslationPipelineTest, PerformanceBenchmark) {
    ASSERT_TRUE(translator->initialize("en", "es"));
    
    const std::string testText = "This is a test sentence for performance measurement.";
    const int numTranslations = 50;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    std::vector<TranslationResult> results;
    for (int i = 0; i < numTranslations; ++i) {
        results.push_back(translator->translate(testText));
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // Verify all translations succeeded
    for (const auto& result : results) {
        EXPECT_TRUE(result.success);
    }
    
    double avgTimeMs = static_cast<double>(duration.count()) / numTranslations;
    double throughput = 1000.0 / avgTimeMs; // translations per second
    
    std::cout << "Performance metrics:" << std::endl;
    std::cout << "  Total time: " << duration.count() << " ms" << std::endl;
    std::cout << "  Average time per translation: " << avgTimeMs << " ms" << std::endl;
    std::cout << "  Throughput: " << throughput << " translations/second" << std::endl;
    
    // Performance expectations for mock implementation
    EXPECT_LT(avgTimeMs, 50.0); // Less than 50ms per translation
    EXPECT_GT(throughput, 20.0); // At least 20 translations per second
}

TEST_F(TranslationPipelineTest, ModelManagementStressTest) {
    // Test rapid model loading/unloading
    std::vector<std::pair<std::string, std::string>> languagePairs = {
        {"en", "es"}, {"en", "fr"}, {"en", "de"}, {"es", "en"}
    };
    
    for (int iteration = 0; iteration < 3; ++iteration) {
        for (const auto& pair : languagePairs) {
            EXPECT_TRUE(translator->loadModel(pair.first, pair.second));
            EXPECT_TRUE(translator->isModelLoaded(pair.first, pair.second));
            
            // Quick translation test
            ASSERT_TRUE(translator->initialize(pair.first, pair.second));
            auto result = translator->translate("Test");
            EXPECT_TRUE(result.success);
            
            translator->unloadModel(pair.first, pair.second);
            EXPECT_FALSE(translator->isModelLoaded(pair.first, pair.second));
        }
    }
}   
 void testMultipleLanguagePairSwitching() {
        std::cout << "Testing multiple language pair switching..." << std::endl;
        std::vector<std::pair<std::string, std::string>> languagePairs = {
            {"en", "es"},
            {"en", "fr"},
            {"en", "de"},
            {"es", "en"}
        };
        
        for (const auto& pair : languagePairs) {
            assert_true(translator->initialize(pair.first, pair.second), 
                       "Failed to initialize " + pair.first + " -> " + pair.second);
            
            auto result = translator->translate("Hello");
            assert_true(result.success, 
                       "Translation failed for " + pair.first + " -> " + pair.second);
            assert_true(result.sourceLang == pair.first, "Source language mismatch");
            assert_true(result.targetLang == pair.second, "Target language mismatch");
            
            std::cout << pair.first << "->" << pair.second << ": 'Hello' -> '" 
                      << result.translatedText << "'" << std::endl;
        }
    }

    void testLongTextTranslation() {
        std::cout << "Testing long text translation..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for long text test");
        
        std::string longText = "This is a longer text that contains multiple sentences. "
                              "It should be handled properly by the translation system. "
                              "The system should maintain context and provide accurate translations "
                              "even for longer input texts that might contain complex grammar "
                              "and various linguistic structures.";
        
        auto result = translator->translate(longText);
        assert_true(result.success, "Long text translation should succeed");
        assert_false(result.translatedText.empty(), "Long text translation should not be empty");
        assert_true(result.translatedText.length() > longText.length() * 0.5, "Translation should have reasonable length");
        
        std::cout << "Long text translation successful, length: " << result.translatedText.length() << std::endl;
    }

    void testSpecialCharactersAndPunctuation() {
        std::cout << "Testing special characters and punctuation..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for special chars test");
        
        std::vector<std::string> specialTexts = {
            "Hello, world!",
            "What's your name?",
            "I'm fine, thank you.",
            "Numbers: 1, 2, 3, 100, 1000"
        };
        
        for (const auto& text : specialTexts) {
            auto result = translator->translate(text);
            assert_true(result.success, "Failed to translate: " + text);
            assert_false(result.translatedText.empty(), "Empty translation for: " + text);
            
            std::cout << "'" << text << "' -> '" << result.translatedText << "'" << std::endl;
        }
    }

    void testErrorRecoveryAndResilience() {
        std::cout << "Testing error recovery and resilience..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for error recovery test");
        
        // Test empty string handling
        auto emptyResult = translator->translate("");
        assert_false(emptyResult.success, "Empty string should fail");
        
        // Test that translator still works after error
        auto validResult = translator->translate("Hello");
        assert_true(validResult.success, "Should work after error");
        
        // Test recovery after potential error
        auto recoveryResult = translator->translate("Hello again");
        assert_true(recoveryResult.success, "Should work after recovery");
        
        std::cout << "Error recovery test passed" << std::endl;
    }

    void testPerformanceBenchmark() {
        std::cout << "Testing performance benchmark..." << std::endl;
        assert_true(translator->initialize("en", "es"), "Should initialize for performance test");
        
        const std::string testText = "This is a test sentence for performance measurement.";
        const int numTranslations = 50;
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        std::vector<TranslationResult> results;
        for (int i = 0; i < numTranslations; ++i) {
            results.push_back(translator->translate(testText));
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        // Verify all translations succeeded
        for (const auto& result : results) {
            assert_true(result.success, "Performance test translation should succeed");
        }
        
        double avgTimeMs = static_cast<double>(duration.count()) / numTranslations;
        double throughput = 1000.0 / avgTimeMs; // translations per second
        
        std::cout << "Performance metrics:" << std::endl;
        std::cout << "  Total time: " << duration.count() << " ms" << std::endl;
        std::cout << "  Average time per translation: " << avgTimeMs << " ms" << std::endl;
        std::cout << "  Throughput: " << throughput << " translations/second" << std::endl;
        
        // Performance expectations for mock implementation
        assert_true(avgTimeMs < 50.0, "Average time should be less than 50ms");
        assert_true(throughput > 20.0, "Throughput should be at least 20 translations/second");
    }
};

int main() {
    TranslationPipelineTest test;
    test.runAllTests();
    return 0;
}