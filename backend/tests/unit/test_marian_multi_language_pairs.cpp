#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mt/marian_translator.hpp"
#include "utils/logging.hpp"
#include <thread>
#include <chrono>

using namespace speechrnt::mt;
using namespace testing;

class MarianMultiLanguagePairTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging for tests
        speechrnt::utils::Logger::setLevel(speechrnt::utils::LogLevel::INFO);
        
        translator = std::make_unique<MarianTranslator>();
        translator->setModelsPath("test_data/marian/");
    }
    
    void TearDown() override {
        if (translator) {
            translator->cleanup();
        }
    }
    
    std::unique_ptr<MarianTranslator> translator;
};

// Test multi-language pair initialization
TEST_F(MarianMultiLanguagePairTest, InitializeMultipleLanguagePairs) {
    std::vector<std::pair<std::string, std::string>> languagePairs = {
        {"en", "es"}, {"en", "fr"}, {"es", "en"}, {"fr", "en"}
    };
    
    // Note: This test will use fallback translation since actual models aren't available
    bool result = translator->initializeMultipleLanguagePairs(languagePairs);
    
    // Should succeed with fallback translation
    EXPECT_TRUE(result);
    
    // Check that language pairs are loaded
    auto loadedPairs = translator->getLoadedLanguagePairs();
    EXPECT_GE(loadedPairs.size(), 1); // At least some pairs should be loaded
}

// Test language pair switching
TEST_F(MarianMultiLanguagePairTest, SwitchLanguagePair) {
    // Initialize with English to Spanish
    ASSERT_TRUE(translator->initialize("en", "es"));
    
    // Switch to French to English
    bool switchResult = translator->switchLanguagePair("fr", "en");
    EXPECT_TRUE(switchResult);
    
    // Test translation with new language pair
    auto result = translator->translate("Bonjour");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.sourceLang, "fr");
    EXPECT_EQ(result.targetLang, "en");
}

// Test translation with explicit language pair
TEST_F(MarianMultiLanguagePairTest, TranslateWithLanguagePair) {
    // Test translation without initializing specific pair
    auto result = translator->translateWithLanguagePair("Hello", "en", "es");
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.sourceLang, "en");
    EXPECT_EQ(result.targetLang, "es");
    EXPECT_FALSE(result.translatedText.empty());
}

// Test async translation with language pair
TEST_F(MarianMultiLanguagePairTest, TranslateWithLanguagePairAsync) {
    auto future = translator->translateWithLanguagePairAsync("Hello", "en", "fr");
    auto result = future.get();
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.sourceLang, "en");
    EXPECT_EQ(result.targetLang, "fr");
    EXPECT_FALSE(result.translatedText.empty());
}

// Test language pair validation
TEST_F(MarianMultiLanguagePairTest, ValidateLanguagePairDetailed) {
    // Test valid language pair
    auto validation = translator->validateLanguagePairDetailed("en", "es");
    EXPECT_TRUE(validation.sourceSupported);
    EXPECT_TRUE(validation.targetSupported);
    
    // Test invalid source language
    auto invalidSource = translator->validateLanguagePairDetailed("xx", "es");
    EXPECT_FALSE(invalidSource.isValid);
    EXPECT_FALSE(invalidSource.sourceSupported);
    EXPECT_FALSE(invalidSource.suggestions.empty());
    
    // Test invalid target language
    auto invalidTarget = translator->validateLanguagePairDetailed("en", "xx");
    EXPECT_FALSE(invalidTarget.isValid);
    EXPECT_TRUE(invalidTarget.sourceSupported);
    EXPECT_FALSE(invalidTarget.targetSupported);
    
    // Test unsupported language pair
    auto unsupported = translator->validateLanguagePairDetailed("zh", "ar");
    // This might be valid or invalid depending on configuration
    if (!unsupported.isValid) {
        EXPECT_FALSE(unsupported.suggestions.empty());
    }
}

// Test bidirectional support information
TEST_F(MarianMultiLanguagePairTest, GetBidirectionalSupportInfo) {
    auto info = translator->getBidirectionalSupportInfo("en", "es");
    
    // English-Spanish should be bidirectional
    EXPECT_TRUE(info.lang1ToLang2Supported);
    EXPECT_TRUE(info.lang2ToLang1Supported);
    EXPECT_TRUE(info.bothDirectionsAvailable);
    
    // Test with languages that might not be bidirectional
    auto limitedInfo = translator->getBidirectionalSupportInfo("en", "zh");
    // Results depend on configuration, but should provide meaningful information
    EXPECT_FALSE(info.lang1ToLang2ModelPath.empty() || info.lang2ToLang1ModelPath.empty());
}

// Test preloading language pairs
TEST_F(MarianMultiLanguagePairTest, PreloadLanguagePairs) {
    std::vector<std::pair<std::string, std::string>> pairsToPreload = {
        {"en", "es"}, {"en", "fr"}, {"es", "en"}
    };
    
    size_t loaded = translator->preloadLanguagePairs(pairsToPreload, 3);
    EXPECT_GT(loaded, 0);
    EXPECT_LE(loaded, pairsToPreload.size());
    
    // Check that pairs are actually loaded
    auto loadedPairs = translator->getLoadedLanguagePairs();
    EXPECT_GE(loadedPairs.size(), loaded);
}

// Test model download recommendations
TEST_F(MarianMultiLanguagePairTest, GetModelDownloadRecommendation) {
    auto recommendation = translator->getModelDownloadRecommendation("en", "es");
    
    EXPECT_FALSE(recommendation.modelName.empty());
    EXPECT_FALSE(recommendation.downloadUrl.empty());
    EXPECT_FALSE(recommendation.modelSize.empty());
    EXPECT_FALSE(recommendation.description.empty());
    
    // Test for unsupported language pair
    auto unsupportedRec = translator->getModelDownloadRecommendation("xx", "yy");
    EXPECT_FALSE(unsupportedRec.modelName.empty()); // Should still provide some recommendation
}

// Test model statistics
TEST_F(MarianMultiLanguagePairTest, GetModelStatistics) {
    // Load some language pairs first
    translator->translateWithLanguagePair("Hello", "en", "es");
    translator->translateWithLanguagePair("Hola", "es", "en");
    
    auto stats = translator->getModelStatistics();
    
    EXPECT_GE(stats.totalSupportedPairs, 1);
    EXPECT_GE(stats.totalLoadedModels, 0);
    EXPECT_EQ(stats.gpuModels + stats.cpuModels, stats.totalLoadedModels);
}

// Test concurrent language pair usage
TEST_F(MarianMultiLanguagePairTest, ConcurrentLanguagePairUsage) {
    const int numThreads = 4;
    const int translationsPerThread = 5;
    std::vector<std::thread> threads;
    std::vector<std::vector<TranslationResult>> results(numThreads);
    
    // Language pairs to test concurrently
    std::vector<std::pair<std::string, std::string>> testPairs = {
        {"en", "es"}, {"en", "fr"}, {"es", "en"}, {"fr", "en"}
    };
    
    // Launch concurrent translation threads
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([this, i, &results, &testPairs, translationsPerThread]() {
            for (int j = 0; j < translationsPerThread; ++j) {
                auto pair = testPairs[j % testPairs.size()];
                std::string text = "Test text " + std::to_string(i) + "_" + std::to_string(j);
                
                auto result = translator->translateWithLanguagePair(text, pair.first, pair.second);
                results[i].push_back(result);
                
                // Small delay to simulate real usage
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify all translations succeeded
    int totalTranslations = 0;
    int successfulTranslations = 0;
    
    for (const auto& threadResults : results) {
        for (const auto& result : threadResults) {
            totalTranslations++;
            if (result.success) {
                successfulTranslations++;
            }
        }
    }
    
    EXPECT_EQ(totalTranslations, numThreads * translationsPerThread);
    EXPECT_GT(successfulTranslations, 0); // At least some should succeed
    
    // Check that we have reasonable success rate (should be high with fallback translation)
    double successRate = static_cast<double>(successfulTranslations) / totalTranslations;
    EXPECT_GT(successRate, 0.8); // Expect at least 80% success rate
}

// Test language pair switching under load
TEST_F(MarianMultiLanguagePairTest, LanguagePairSwitchingUnderLoad) {
    std::vector<std::pair<std::string, std::string>> pairs = {
        {"en", "es"}, {"en", "fr"}, {"es", "en"}, {"fr", "en"}
    };
    
    // Rapidly switch between language pairs and translate
    for (int i = 0; i < 20; ++i) {
        auto pair = pairs[i % pairs.size()];
        
        bool switchSuccess = translator->switchLanguagePair(pair.first, pair.second);
        EXPECT_TRUE(switchSuccess);
        
        auto result = translator->translate("Test " + std::to_string(i));
        EXPECT_TRUE(result.success);
        EXPECT_EQ(result.sourceLang, pair.first);
        EXPECT_EQ(result.targetLang, pair.second);
    }
}

// Test error handling for invalid language pairs
TEST_F(MarianMultiLanguagePairTest, ErrorHandlingInvalidLanguagePairs) {
    // Test with completely invalid language codes
    auto result1 = translator->translateWithLanguagePair("Hello", "invalid", "es");
    EXPECT_FALSE(result1.success);
    EXPECT_FALSE(result1.errorMessage.empty());
    
    auto result2 = translator->translateWithLanguagePair("Hello", "en", "invalid");
    EXPECT_FALSE(result2.success);
    EXPECT_FALSE(result2.errorMessage.empty());
    
    // Test switching to invalid language pair
    bool switchResult = translator->switchLanguagePair("invalid", "also_invalid");
    EXPECT_FALSE(switchResult);
}

// Test memory management with multiple language pairs
TEST_F(MarianMultiLanguagePairTest, MemoryManagementMultiplePairs) {
    // Set a low limit for concurrent models
    std::vector<std::pair<std::string, std::string>> manyPairs = {
        {"en", "es"}, {"en", "fr"}, {"en", "de"}, {"en", "it"}, {"en", "pt"},
        {"es", "en"}, {"fr", "en"}, {"de", "en"}, {"it", "en"}, {"pt", "en"}
    };
    
    // Try to load more pairs than the limit
    size_t loaded = translator->preloadLanguagePairs(manyPairs, 3); // Limit to 3 concurrent models
    
    // Should not load more than the limit
    auto loadedPairs = translator->getLoadedLanguagePairs();
    EXPECT_LE(loadedPairs.size(), 3);
    
    // But should still be able to translate with any pair (loading on demand)
    for (const auto& pair : manyPairs) {
        auto result = translator->translateWithLanguagePair("Test", pair.first, pair.second);
        EXPECT_TRUE(result.success);
    }
}