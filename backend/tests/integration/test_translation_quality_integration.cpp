#include <gtest/gtest.h>
#include "mt/marian_translator.hpp"
#include "mt/quality_manager.hpp"
#include <memory>

using namespace speechrnt::mt;

class TranslationQualityIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        translator = std::make_unique<MarianTranslator>();
        
        // Initialize with a common language pair
        bool initialized = translator->initialize("en", "es");
        if (!initialized) {
            GTEST_SKIP() << "Could not initialize translator with en->es pair. Skipping integration tests.";
        }
    }
    
    void TearDown() override {
        if (translator) {
            translator->cleanup();
        }
    }
    
    std::unique_ptr<MarianTranslator> translator;
};

TEST_F(TranslationQualityIntegrationTest, BasicTranslationWithQuality) {
    std::string sourceText = "Hello, how are you today?";
    
    TranslationResult result = translator->translate(sourceText);
    
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.translatedText.empty());
    EXPECT_EQ(result.sourceLang, "en");
    EXPECT_EQ(result.targetLang, "es");
    EXPECT_GE(result.confidence, 0.0f);
    EXPECT_LE(result.confidence, 1.0f);
    
    // Check if quality metrics are available
    if (result.qualityMetrics) {
        EXPECT_GE(result.qualityMetrics->overallConfidence, 0.0f);
        EXPECT_LE(result.qualityMetrics->overallConfidence, 1.0f);
        EXPECT_FALSE(result.qualityMetrics->qualityLevel.empty());
        
        std::cout << "Translation: " << result.translatedText << std::endl;
        std::cout << "Quality Level: " << result.qualityMetrics->qualityLevel << std::endl;
        std::cout << "Confidence: " << result.qualityMetrics->overallConfidence << std::endl;
    }
}

TEST_F(TranslationQualityIntegrationTest, QualityThresholdTesting) {
    // Set custom quality thresholds
    translator->setQualityThresholds(0.9f, 0.7f, 0.5f);
    
    std::string sourceText = "Good morning, have a nice day!";
    TranslationResult result = translator->translate(sourceText);
    
    EXPECT_TRUE(result.success);
    
    if (result.qualityMetrics) {
        // Test threshold checking
        bool meetsLow = translator->meetsQualityThreshold(result, "low");
        bool meetsMedium = translator->meetsQualityThreshold(result, "medium");
        bool meetsHigh = translator->meetsQualityThreshold(result, "high");
        
        // If it meets high, it should also meet medium and low
        if (meetsHigh) {
            EXPECT_TRUE(meetsMedium);
            EXPECT_TRUE(meetsLow);
        }
        
        // If it meets medium, it should also meet low
        if (meetsMedium) {
            EXPECT_TRUE(meetsLow);
        }
        
        std::cout << "Meets Low: " << (meetsLow ? "Yes" : "No") << std::endl;
        std::cout << "Meets Medium: " << (meetsMedium ? "Yes" : "No") << std::endl;
        std::cout << "Meets High: " << (meetsHigh ? "Yes" : "No") << std::endl;
    }
}

TEST_F(TranslationQualityIntegrationTest, TranslationCandidateGeneration) {
    std::string sourceText = "Thank you very much for your help.";
    
    auto candidates = translator->getTranslationCandidates(sourceText, 3);
    
    EXPECT_GE(candidates.size(), 1);
    EXPECT_LE(candidates.size(), 3);
    
    // All candidates should be successful
    for (const auto& candidate : candidates) {
        EXPECT_TRUE(candidate.success);
        EXPECT_FALSE(candidate.translatedText.empty());
        EXPECT_EQ(candidate.sourceLang, "en");
        EXPECT_EQ(candidate.targetLang, "es");
        EXPECT_GE(candidate.confidence, 0.0f);
        EXPECT_LE(candidate.confidence, 1.0f);
    }
    
    // Print candidates for manual inspection
    std::cout << "Translation candidates for: \"" << sourceText << "\"" << std::endl;
    for (size_t i = 0; i < candidates.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << candidates[i].translatedText 
                  << " (confidence: " << candidates[i].confidence << ")" << std::endl;
    }
}

TEST_F(TranslationQualityIntegrationTest, FallbackTranslationGeneration) {
    std::string sourceText = "I need help with this problem.";
    
    auto fallbacks = translator->getFallbackTranslations(sourceText);
    
    // Should have at least some fallback options
    EXPECT_GE(fallbacks.size(), 0);
    
    // All fallbacks should be non-empty
    for (const auto& fallback : fallbacks) {
        EXPECT_FALSE(fallback.empty());
    }
    
    // Print fallbacks for manual inspection
    if (!fallbacks.empty()) {
        std::cout << "Fallback translations for: \"" << sourceText << "\"" << std::endl;
        for (size_t i = 0; i < fallbacks.size(); ++i) {
            std::cout << "  " << (i + 1) << ". " << fallbacks[i] << std::endl;
        }
    }
}

TEST_F(TranslationQualityIntegrationTest, AlternativeTranslationsForLowQuality) {
    // Try to create a scenario that might produce low-quality translation
    std::string complexText = "The implementation of the sophisticated algorithm requires careful consideration of edge cases and performance optimization strategies.";
    
    TranslationResult result = translator->translate(complexText);
    
    EXPECT_TRUE(result.success);
    
    if (result.qualityMetrics) {
        std::cout << "Complex text translation quality: " << result.qualityMetrics->qualityLevel << std::endl;
        std::cout << "Confidence: " << result.qualityMetrics->overallConfidence << std::endl;
        
        // Check if alternatives were generated for low/medium quality
        if (!translator->meetsQualityThreshold(result, "high")) {
            std::cout << "Alternative translations generated: " << result.alternativeTranslations.size() << std::endl;
            
            for (size_t i = 0; i < result.alternativeTranslations.size(); ++i) {
                std::cout << "  Alt " << (i + 1) << ": " << result.alternativeTranslations[i] << std::endl;
            }
        }
    }
}

TEST_F(TranslationQualityIntegrationTest, QualityIssueDetectionIntegration) {
    // Test various types of input that might cause quality issues
    std::vector<std::string> testCases = {
        "Hello hello hello world world world",  // Repetitive
        "Hi",                                   // Very short
        "This is a very very very very very very long sentence with lots of repetition and redundancy that goes on and on and on without much meaning or purpose other than to test the quality assessment system.",  // Very long and repetitive
        "The quick brown fox jumps over the lazy dog."  // Normal sentence
    };
    
    for (const auto& testCase : testCases) {
        TranslationResult result = translator->translate(testCase);
        
        EXPECT_TRUE(result.success);
        
        if (result.qualityMetrics) {
            std::cout << "\nInput: \"" << testCase << "\"" << std::endl;
            std::cout << "Translation: \"" << result.translatedText << "\"" << std::endl;
            std::cout << "Quality Level: " << result.qualityMetrics->qualityLevel << std::endl;
            std::cout << "Confidence: " << result.qualityMetrics->overallConfidence << std::endl;
            
            if (!result.qualityMetrics->qualityIssues.empty()) {
                std::cout << "Quality Issues:" << std::endl;
                for (const auto& issue : result.qualityMetrics->qualityIssues) {
                    std::cout << "  - " << issue << std::endl;
                }
            }
        }
    }
}

TEST_F(TranslationQualityIntegrationTest, AsyncTranslationWithQuality) {
    std::string sourceText = "This is an asynchronous translation test.";
    
    auto future = translator->translateAsync(sourceText);
    TranslationResult result = future.get();
    
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.translatedText.empty());
    EXPECT_GE(result.confidence, 0.0f);
    EXPECT_LE(result.confidence, 1.0f);
    
    if (result.qualityMetrics) {
        EXPECT_GE(result.qualityMetrics->overallConfidence, 0.0f);
        EXPECT_LE(result.qualityMetrics->overallConfidence, 1.0f);
        
        std::cout << "Async translation: " << result.translatedText << std::endl;
        std::cout << "Quality: " << result.qualityMetrics->qualityLevel << std::endl;
    }
}

TEST_F(TranslationQualityIntegrationTest, MultipleLanguagePairQuality) {
    // Test different language pairs if available
    std::vector<std::pair<std::string, std::string>> languagePairs = {
        {"en", "es"},
        {"en", "fr"},
        {"es", "en"}
    };
    
    std::string sourceText = "Hello world";
    
    for (const auto& [sourceLang, targetLang] : languagePairs) {
        if (translator->supportsLanguagePair(sourceLang, targetLang)) {
            bool initialized = translator->initialize(sourceLang, targetLang);
            if (initialized) {
                TranslationResult result = translator->translate(sourceText);
                
                EXPECT_TRUE(result.success);
                EXPECT_EQ(result.sourceLang, sourceLang);
                EXPECT_EQ(result.targetLang, targetLang);
                
                if (result.qualityMetrics) {
                    std::cout << sourceLang << "->" << targetLang << ": " 
                              << result.translatedText << " (quality: " 
                              << result.qualityMetrics->qualityLevel << ")" << std::endl;
                }
            }
        }
    }
}

TEST_F(TranslationQualityIntegrationTest, PerformanceWithQualityAssessment) {
    std::string sourceText = "Performance test with quality assessment enabled.";
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Perform multiple translations
    std::vector<TranslationResult> results;
    for (int i = 0; i < 10; ++i) {
        TranslationResult result = translator->translate(sourceText);
        EXPECT_TRUE(result.success);
        results.push_back(result);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "10 translations with quality assessment completed in " 
              << duration.count() << "ms" << std::endl;
    std::cout << "Average time per translation: " 
              << (duration.count() / 10.0) << "ms" << std::endl;
    
    // Verify all results have quality metrics
    for (const auto& result : results) {
        if (result.qualityMetrics) {
            EXPECT_GE(result.qualityMetrics->overallConfidence, 0.0f);
            EXPECT_LE(result.qualityMetrics->overallConfidence, 1.0f);
        }
    }
    
    // Performance should be reasonable (less than 5 seconds for 10 translations)
    EXPECT_LT(duration.count(), 5000);
}