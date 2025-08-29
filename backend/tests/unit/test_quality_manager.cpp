#include <gtest/gtest.h>
#include "mt/quality_manager.hpp"
#include <memory>

using namespace speechrnt::mt;

class QualityManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        qualityManager = std::make_unique<QualityManager>();
        ASSERT_TRUE(qualityManager->initialize());
    }
    
    void TearDown() override {
        if (qualityManager) {
            qualityManager->cleanup();
        }
    }
    
    std::unique_ptr<QualityManager> qualityManager;
};

TEST_F(QualityManagerTest, InitializationTest) {
    EXPECT_TRUE(qualityManager->isReady());
    
    const auto& config = qualityManager->getConfig();
    EXPECT_GT(config.highQualityThreshold, config.mediumQualityThreshold);
    EXPECT_GT(config.mediumQualityThreshold, config.lowQualityThreshold);
    EXPECT_GT(config.maxAlternatives, 0);
}

TEST_F(QualityManagerTest, BasicQualityAssessment) {
    std::string sourceText = "Hello, how are you today?";
    std::string translatedText = "Hola, ¿cómo estás hoy?";
    std::string sourceLang = "en";
    std::string targetLang = "es";
    
    QualityMetrics metrics = qualityManager->assessTranslationQuality(
        sourceText, translatedText, sourceLang, targetLang);
    
    EXPECT_GE(metrics.overallConfidence, 0.0f);
    EXPECT_LE(metrics.overallConfidence, 1.0f);
    EXPECT_GE(metrics.fluencyScore, 0.0f);
    EXPECT_LE(metrics.fluencyScore, 1.0f);
    EXPECT_GE(metrics.adequacyScore, 0.0f);
    EXPECT_LE(metrics.adequacyScore, 1.0f);
    EXPECT_GE(metrics.consistencyScore, 0.0f);
    EXPECT_LE(metrics.consistencyScore, 1.0f);
    
    EXPECT_TRUE(metrics.qualityLevel == "high" || 
                metrics.qualityLevel == "medium" || 
                metrics.qualityLevel == "low");
}

TEST_F(QualityManagerTest, ConfidenceScoreCalculation) {
    std::string sourceText = "Good morning";
    std::string translatedText = "Buenos días";
    std::vector<float> modelScores = {0.9f, 0.8f};
    
    float confidence = qualityManager->calculateConfidenceScore(
        sourceText, translatedText, modelScores);
    
    EXPECT_GE(confidence, 0.0f);
    EXPECT_LE(confidence, 1.0f);
    EXPECT_GT(confidence, 0.5f); // Should be reasonably high for good translation
}

TEST_F(QualityManagerTest, EmptyInputHandling) {
    QualityMetrics metrics = qualityManager->assessTranslationQuality(
        "", "", "en", "es");
    
    EXPECT_EQ(metrics.overallConfidence, 0.0f);
    EXPECT_EQ(metrics.qualityLevel, "low");
    
    float confidence = qualityManager->calculateConfidenceScore("", "", {});
    EXPECT_EQ(confidence, 0.0f);
}

TEST_F(QualityManagerTest, QualityThresholdTesting) {
    // Test with high quality translation
    std::string sourceText = "The weather is nice today.";
    std::string goodTranslation = "El clima está agradable hoy.";
    
    QualityMetrics goodMetrics = qualityManager->assessTranslationQuality(
        sourceText, goodTranslation, "en", "es");
    
    // Test with poor quality translation (repetitive)
    std::string poorTranslation = "El El El clima clima clima";
    
    QualityMetrics poorMetrics = qualityManager->assessTranslationQuality(
        sourceText, poorTranslation, "en", "es");
    
    EXPECT_GT(goodMetrics.overallConfidence, poorMetrics.overallConfidence);
    
    // Test threshold checking
    EXPECT_TRUE(qualityManager->meetsQualityThreshold(goodMetrics, "low"));
    
    if (poorMetrics.overallConfidence < 0.4f) {
        EXPECT_FALSE(qualityManager->meetsQualityThreshold(poorMetrics, "medium"));
    }
}

TEST_F(QualityManagerTest, TranslationCandidateGeneration) {
    std::string sourceText = "Thank you very much";
    std::string currentTranslation = "Muchas gracias";
    
    auto candidates = qualityManager->generateTranslationCandidates(
        sourceText, currentTranslation, "en", "es", 3);
    
    EXPECT_GE(candidates.size(), 1);
    EXPECT_LE(candidates.size(), 3);
    
    // First candidate should be the current translation
    EXPECT_EQ(candidates[0].translatedText, currentTranslation);
    EXPECT_EQ(candidates[0].rank, 1);
    
    // Candidates should be ranked by quality
    for (size_t i = 1; i < candidates.size(); ++i) {
        EXPECT_GE(candidates[i-1].qualityMetrics.overallConfidence, 
                  candidates[i].qualityMetrics.overallConfidence);
    }
}

TEST_F(QualityManagerTest, FallbackTranslationGeneration) {
    std::string sourceText = "Hello world";
    std::string lowQualityTranslation = "Hola hola hola mundo mundo";
    
    auto fallbacks = qualityManager->getFallbackTranslations(
        sourceText, lowQualityTranslation, "en", "es");
    
    EXPECT_GE(fallbacks.size(), 0);
    
    // Fallbacks should be different from the original low-quality translation
    for (const auto& fallback : fallbacks) {
        EXPECT_NE(fallback, lowQualityTranslation);
        EXPECT_FALSE(fallback.empty());
    }
}

TEST_F(QualityManagerTest, QualityIssueDetection) {
    // Test repetitive translation
    std::string sourceText = "How are you?";
    std::string repetitiveTranslation = "¿Cómo estás estás estás?";
    
    QualityMetrics metrics = qualityManager->assessTranslationQuality(
        sourceText, repetitiveTranslation, "en", "es");
    
    EXPECT_FALSE(metrics.qualityIssues.empty());
    
    bool foundRepetitionIssue = false;
    for (const auto& issue : metrics.qualityIssues) {
        if (issue.find("repetition") != std::string::npos || 
            issue.find("Repetitive") != std::string::npos) {
            foundRepetitionIssue = true;
            break;
        }
    }
    EXPECT_TRUE(foundRepetitionIssue);
}

TEST_F(QualityManagerTest, QualityThresholdConfiguration) {
    // Test setting custom thresholds
    qualityManager->setQualityThresholds(0.9f, 0.7f, 0.5f);
    
    const auto& config = qualityManager->getConfig();
    EXPECT_FLOAT_EQ(config.highQualityThreshold, 0.9f);
    EXPECT_FLOAT_EQ(config.mediumQualityThreshold, 0.7f);
    EXPECT_FLOAT_EQ(config.lowQualityThreshold, 0.5f);
}

TEST_F(QualityManagerTest, ImprovementSuggestions) {
    // Create a translation with known issues
    std::string sourceText = "This is a test sentence.";
    std::string problematicTranslation = "Esta es una oración oración de prueba.";
    
    QualityMetrics metrics = qualityManager->assessTranslationQuality(
        sourceText, problematicTranslation, "en", "es");
    
    auto suggestions = qualityManager->suggestImprovements(metrics);
    
    EXPECT_GE(suggestions.size(), 0);
    
    // If there are quality issues, there should be suggestions
    if (!metrics.qualityIssues.empty()) {
        EXPECT_GT(suggestions.size(), 0);
    }
}

TEST_F(QualityManagerTest, WordLevelConfidenceScoring) {
    std::string sourceText = "The quick brown fox";
    std::string translatedText = "El rápido zorro marrón";
    std::vector<float> modelScores = {0.9f, 0.8f, 0.7f, 0.85f};
    
    QualityMetrics metrics = qualityManager->assessTranslationQuality(
        sourceText, translatedText, "en", "es", modelScores);
    
    if (qualityManager->getConfig().enableWordLevelScoring) {
        EXPECT_FALSE(metrics.wordLevelConfidences.empty());
        
        for (float confidence : metrics.wordLevelConfidences) {
            EXPECT_GE(confidence, 0.0f);
            EXPECT_LE(confidence, 1.0f);
        }
    }
}

TEST_F(QualityManagerTest, ConfigurationUpdate) {
    QualityConfig newConfig;
    newConfig.highQualityThreshold = 0.95f;
    newConfig.mediumQualityThreshold = 0.75f;
    newConfig.lowQualityThreshold = 0.55f;
    newConfig.maxAlternatives = 5;
    newConfig.enableAlternatives = false;
    
    qualityManager->updateConfig(newConfig);
    
    const auto& config = qualityManager->getConfig();
    EXPECT_FLOAT_EQ(config.highQualityThreshold, 0.95f);
    EXPECT_FLOAT_EQ(config.mediumQualityThreshold, 0.75f);
    EXPECT_FLOAT_EQ(config.lowQualityThreshold, 0.55f);
    EXPECT_EQ(config.maxAlternatives, 5);
    EXPECT_FALSE(config.enableAlternatives);
}

TEST_F(QualityManagerTest, LengthBasedQualityAssessment) {
    std::string sourceText = "This is a reasonably long sentence that should be translated properly.";
    
    // Very short translation (likely incomplete)
    std::string shortTranslation = "Esto";
    QualityMetrics shortMetrics = qualityManager->assessTranslationQuality(
        sourceText, shortTranslation, "en", "es");
    
    // Reasonable length translation
    std::string goodTranslation = "Esta es una oración razonablemente larga que debería traducirse correctamente.";
    QualityMetrics goodMetrics = qualityManager->assessTranslationQuality(
        sourceText, goodTranslation, "en", "es");
    
    EXPECT_GT(goodMetrics.overallConfidence, shortMetrics.overallConfidence);
    EXPECT_GT(goodMetrics.adequacyScore, shortMetrics.adequacyScore);
}

// Performance test
TEST_F(QualityManagerTest, PerformanceTest) {
    std::string sourceText = "This is a performance test to ensure quality assessment is fast enough for real-time use.";
    std::string translatedText = "Esta es una prueba de rendimiento para asegurar que la evaluación de calidad sea lo suficientemente rápida para uso en tiempo real.";
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Perform multiple assessments
    for (int i = 0; i < 100; ++i) {
        QualityMetrics metrics = qualityManager->assessTranslationQuality(
            sourceText, translatedText, "en", "es");
        EXPECT_GE(metrics.overallConfidence, 0.0f);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // Should complete 100 assessments in reasonable time (less than 1 second)
    EXPECT_LT(duration.count(), 1000);
    
    std::cout << "100 quality assessments completed in " << duration.count() << "ms" << std::endl;
}