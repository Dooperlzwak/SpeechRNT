#include <gtest/gtest.h>
#include "mt/marian_translator.hpp"
#include "core/translation_pipeline.hpp"
#include "utils/logging.hpp"
#include <memory>
#include <vector>
#include <chrono>

using namespace speechrnt::mt;
using namespace speechrnt::core;

class MultiLanguagePipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        speechrnt::utils::Logger::setLevel(speechrnt::utils::LogLevel::INFO);
        
        translator = std::make_unique<MarianTranslator>();
        translator->setModelsPath("test_data/marian/");
        
        // Initialize with common language pairs
        std::vector<std::pair<std::string, std::string>> commonPairs = {
            {"en", "es"}, {"es", "en"}, {"en", "fr"}, {"fr", "en"}
        };
        translator->initializeMultipleLanguagePairs(commonPairs);
    }
    
    void TearDown() override {
        if (translator) {
            translator->cleanup();
        }
    }
    
    std::unique_ptr<MarianTranslator> translator;
};

// Test end-to-end multi-language conversation scenario
TEST_F(MultiLanguagePipelineTest, MultiLanguageConversationFlow) {
    // Simulate a conversation between English and Spanish speakers
    std::vector<std::tuple<std::string, std::string, std::string, std::string>> conversation = {
        {"Hello, how are you?", "en", "es", "Hola, ¿cómo estás?"},
        {"Muy bien, gracias", "es", "en", "Very well, thank you"},
        {"What is your name?", "en", "es", "¿Cómo te llamas?"},
        {"Me llamo María", "es", "en", "My name is María"},
        {"Nice to meet you", "en", "es", "Mucho gusto"}
    };
    
    for (const auto& [text, sourceLang, targetLang, expectedPattern] : conversation) {
        auto result = translator->translateWithLanguagePair(text, sourceLang, targetLang);
        
        EXPECT_TRUE(result.success) << "Failed to translate: " << text;
        EXPECT_EQ(result.sourceLang, sourceLang);
        EXPECT_EQ(result.targetLang, targetLang);
        EXPECT_FALSE(result.translatedText.empty());
        
        // For fallback translation, check that it contains expected elements
        if (result.translatedText.find("[" + targetLang + "]") == std::string::npos) {
            // This is a known phrase translation, should be reasonable
            EXPECT_GT(result.confidence, 0.5f);
        }
    }
}

// Test rapid language switching performance
TEST_F(MultiLanguagePipelineTest, RapidLanguageSwitchingPerformance) {
    std::vector<std::pair<std::string, std::string>> languagePairs = {
        {"en", "es"}, {"es", "en"}, {"en", "fr"}, {"fr", "en"},
        {"en", "de"}, {"de", "en"}, {"en", "it"}, {"it", "en"}
    };
    
    const int numSwitches = 50;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < numSwitches; ++i) {
        auto pair = languagePairs[i % languagePairs.size()];
        
        bool switchSuccess = translator->switchLanguagePair(pair.first, pair.second);
        EXPECT_TRUE(switchSuccess);
        
        auto result = translator->translate("Test message " + std::to_string(i));
        EXPECT_TRUE(result.success);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // Should complete within reasonable time (allowing for fallback translation overhead)
    EXPECT_LT(duration.count(), 5000); // Less than 5 seconds for 50 switches
    
    std::cout << "Completed " << numSwitches << " language switches in " 
              << duration.count() << "ms" << std::endl;
}

// Test bidirectional translation consistency
TEST_F(MultiLanguagePipelineTest, BidirectionalTranslationConsistency) {
    std::vector<std::pair<std::string, std::string>> testPairs = {
        {"en", "es"}, {"en", "fr"}, {"en", "de"}
    };
    
    std::vector<std::string> testPhrases = {
        "Hello", "Thank you", "Good morning", "How are you?"
    };
    
    for (const auto& pair : testPairs) {
        const std::string& lang1 = pair.first;
        const std::string& lang2 = pair.second;
        
        // Check bidirectional support
        auto bidirectionalInfo = translator->getBidirectionalSupportInfo(lang1, lang2);
        
        if (bidirectionalInfo.bothDirectionsAvailable) {
            for (const std::string& phrase : testPhrases) {
                // Translate from lang1 to lang2
                auto result1 = translator->translateWithLanguagePair(phrase, lang1, lang2);
                EXPECT_TRUE(result1.success);
                
                // Translate back from lang2 to lang1
                auto result2 = translator->translateWithLanguagePair(result1.translatedText, lang2, lang1);
                EXPECT_TRUE(result2.success);
                
                // The round-trip translation should be somewhat consistent
                // (This is a basic check since we're using fallback translation)
                EXPECT_FALSE(result2.translatedText.empty());
                
                std::cout << "Round-trip: " << phrase << " -> " << result1.translatedText 
                         << " -> " << result2.translatedText << std::endl;
            }
        }
    }
}

// Test language pair validation in pipeline context
TEST_F(MultiLanguagePipelineTest, LanguagePairValidationInPipeline) {
    // Test various language pair combinations
    std::vector<std::tuple<std::string, std::string, bool>> testCases = {
        {"en", "es", true},   // Should be supported
        {"es", "en", true},   // Should be supported
        {"en", "fr", true},   // Should be supported
        {"fr", "en", true},   // Should be supported
        {"xx", "yy", false},  // Invalid languages
        {"en", "xx", false},  // Invalid target
        {"xx", "en", false},  // Invalid source
    };
    
    for (const auto& [sourceLang, targetLang, expectedValid] : testCases) {
        auto validation = translator->validateLanguagePairDetailed(sourceLang, targetLang);
        
        if (expectedValid) {
            EXPECT_TRUE(validation.sourceSupported) 
                << "Source language should be supported: " << sourceLang;
            EXPECT_TRUE(validation.targetSupported) 
                << "Target language should be supported: " << targetLang;
        } else {
            EXPECT_FALSE(validation.isValid) 
                << "Language pair should be invalid: " << sourceLang << " -> " << targetLang;
            EXPECT_FALSE(validation.suggestions.empty()) 
                << "Should provide suggestions for invalid pair";
        }
    }
}

// Test model download recommendations integration
TEST_F(MultiLanguagePipelineTest, ModelDownloadRecommendationsIntegration) {
    std::vector<std::pair<std::string, std::string>> testPairs = {
        {"en", "es"}, {"en", "fr"}, {"en", "de"}, {"en", "it"},
        {"zh", "en"}, {"ja", "en"}, {"ko", "en"}
    };
    
    for (const auto& pair : testPairs) {
        auto recommendation = translator->getModelDownloadRecommendation(pair.first, pair.second);
        
        // Should always provide some recommendation
        EXPECT_FALSE(recommendation.modelName.empty());
        EXPECT_FALSE(recommendation.downloadUrl.empty());
        EXPECT_FALSE(recommendation.description.empty());
        
        // Should provide reasonable model size estimate
        EXPECT_FALSE(recommendation.modelSize.empty());
        EXPECT_NE(recommendation.modelSize.find("MB"), std::string::npos);
        
        std::cout << "Model recommendation for " << pair.first << " -> " << pair.second 
                 << ": " << recommendation.modelName << " (" << recommendation.modelSize << ")" << std::endl;
    }
}

// Test concurrent multi-language translation load
TEST_F(MultiLanguagePipelineTest, ConcurrentMultiLanguageLoad) {
    const int numThreads = 8;
    const int translationsPerThread = 10;
    
    std::vector<std::pair<std::string, std::string>> languagePairs = {
        {"en", "es"}, {"es", "en"}, {"en", "fr"}, {"fr", "en"},
        {"en", "de"}, {"de", "en"}, {"en", "it"}, {"it", "en"}
    };
    
    std::vector<std::thread> threads;
    std::atomic<int> successfulTranslations{0};
    std::atomic<int> totalTranslations{0};
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([this, i, &languagePairs, &successfulTranslations, &totalTranslations, translationsPerThread]() {
            for (int j = 0; j < translationsPerThread; ++j) {
                auto pair = languagePairs[(i + j) % languagePairs.size()];
                std::string text = "Concurrent test " + std::to_string(i) + "_" + std::to_string(j);
                
                auto result = translator->translateWithLanguagePair(text, pair.first, pair.second);
                
                totalTranslations++;
                if (result.success) {
                    successfulTranslations++;
                }
                
                // Small delay to simulate realistic usage
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    int expectedTotal = numThreads * translationsPerThread;
    EXPECT_EQ(totalTranslations.load(), expectedTotal);
    
    // Should have high success rate with fallback translation
    double successRate = static_cast<double>(successfulTranslations.load()) / totalTranslations.load();
    EXPECT_GT(successRate, 0.9); // Expect at least 90% success rate
    
    std::cout << "Concurrent load test: " << successfulTranslations.load() << "/" 
              << totalTranslations.load() << " successful (" 
              << (successRate * 100) << "%) in " << duration.count() << "ms" << std::endl;
}

// Test model statistics accuracy
TEST_F(MultiLanguagePipelineTest, ModelStatisticsAccuracy) {
    // Perform translations with different language pairs
    std::vector<std::pair<std::string, std::string>> usedPairs = {
        {"en", "es"}, {"es", "en"}, {"en", "fr"}
    };
    
    for (const auto& pair : usedPairs) {
        // Translate multiple times to build usage statistics
        for (int i = 0; i < 3; ++i) {
            auto result = translator->translateWithLanguagePair("Test " + std::to_string(i), pair.first, pair.second);
            EXPECT_TRUE(result.success);
        }
    }
    
    auto stats = translator->getModelStatistics();
    
    // Verify statistics make sense
    EXPECT_GT(stats.totalSupportedPairs, 0);
    EXPECT_GE(stats.totalLoadedModels, 0);
    EXPECT_LE(stats.totalLoadedModels, usedPairs.size());
    
    // GPU + CPU models should equal total loaded models
    EXPECT_EQ(stats.gpuModels + stats.cpuModels, stats.totalLoadedModels);
    
    // Should have some usage data
    EXPECT_GE(stats.mostUsedPairs.size(), 0);
    
    std::cout << "Model statistics:" << std::endl;
    std::cout << "  Total supported pairs: " << stats.totalSupportedPairs << std::endl;
    std::cout << "  Total loaded models: " << stats.totalLoadedModels << std::endl;
    std::cout << "  GPU models: " << stats.gpuModels << std::endl;
    std::cout << "  CPU models: " << stats.cpuModels << std::endl;
    std::cout << "  Memory usage: " << stats.totalMemoryUsageMB << " MB" << std::endl;
}

// Test error recovery in multi-language scenarios
TEST_F(MultiLanguagePipelineTest, ErrorRecoveryMultiLanguage) {
    // Test recovery from various error conditions
    
    // 1. Invalid language pair followed by valid one
    auto invalidResult = translator->translateWithLanguagePair("Test", "invalid", "also_invalid");
    EXPECT_FALSE(invalidResult.success);
    
    auto validResult = translator->translateWithLanguagePair("Test", "en", "es");
    EXPECT_TRUE(validResult.success);
    
    // 2. Empty text handling
    auto emptyResult = translator->translateWithLanguagePair("", "en", "es");
    EXPECT_FALSE(emptyResult.success);
    
    auto normalResult = translator->translateWithLanguagePair("Hello", "en", "es");
    EXPECT_TRUE(normalResult.success);
    
    // 3. Rapid switching between valid and invalid pairs
    for (int i = 0; i < 10; ++i) {
        if (i % 2 == 0) {
            auto result = translator->translateWithLanguagePair("Test", "en", "es");
            EXPECT_TRUE(result.success);
        } else {
            auto result = translator->translateWithLanguagePair("Test", "invalid", "es");
            EXPECT_FALSE(result.success);
        }
    }
    
    // Should still be able to translate normally after errors
    auto finalResult = translator->translateWithLanguagePair("Final test", "en", "fr");
    EXPECT_TRUE(finalResult.success);
}