#include <gtest/gtest.h>
#include "stt/advanced/contextual_transcriber.hpp"
#include "stt/stt_interface.hpp"

using namespace stt::advanced;

class ContextualTranscriberTest : public ::testing::Test {
protected:
    void SetUp() override {
        transcriber_ = createContextualTranscriber();
        ASSERT_TRUE(transcriber_->initialize("test_models"));
    }
    
    void TearDown() override {
        transcriber_.reset();
    }
    
    std::unique_ptr<ContextualTranscriberInterface> transcriber_;
};

TEST_F(ContextualTranscriberTest, InitializationTest) {
    EXPECT_TRUE(transcriber_->isInitialized());
    EXPECT_TRUE(transcriber_->getLastError().empty());
}

TEST_F(ContextualTranscriberTest, DomainDetectionTest) {
    std::string medicalText = "The patient has acute myocardial infarction and needs immediate treatment";
    std::string detectedDomain = transcriber_->detectDomain(medicalText);
    
    EXPECT_EQ(detectedDomain, "medical");
}

TEST_F(ContextualTranscriberTest, CustomVocabularyTest) {
    std::vector<std::string> customTerms = {"myocardial", "infarction", "cardiovascular"};
    bool success = transcriber_->addCustomVocabulary(customTerms, "medical");
    
    EXPECT_TRUE(success);
    
    auto availableDomains = transcriber_->getAvailableDomains();
    EXPECT_TRUE(std::find(availableDomains.begin(), availableDomains.end(), "medical") != availableDomains.end());
}

TEST_F(ContextualTranscriberTest, ConversationContextTest) {
    uint32_t utteranceId = 1;
    std::string utterance = "The patient is experiencing chest pain";
    
    transcriber_->updateConversationContext(utteranceId, utterance, "doctor");
    
    auto context = transcriber_->getConversationContext(utteranceId);
    EXPECT_EQ(context.utteranceId, utteranceId);
    EXPECT_EQ(context.speakerInfo, "doctor");
    EXPECT_FALSE(context.previousUtterances.empty());
    EXPECT_EQ(context.previousUtterances[0], utterance);
}

TEST_F(ContextualTranscriberTest, TranscriptionEnhancementTest) {
    // Set up context
    uint32_t utteranceId = 1;
    transcriber_->updateConversationContext(utteranceId, "Patient has heart problems", "doctor");
    transcriber_->setDomainHint(utteranceId, "medical");
    
    // Create base transcription result
    TranscriptionResult baseResult;
    baseResult.text = "Patient has myocardial infraction";  // Intentional misspelling
    baseResult.confidence = 0.8f;
    baseResult.utteranceId = utteranceId;
    
    // Get conversation context
    auto context = transcriber_->getConversationContext(utteranceId);
    
    // Enhance transcription
    auto enhancedResult = transcriber_->enhanceTranscription(baseResult, context);
    
    EXPECT_TRUE(enhancedResult.contextUsed);
    EXPECT_EQ(enhancedResult.detectedDomain, "medical");
    EXPECT_FALSE(enhancedResult.corrections.empty());
    
    // Check if the misspelling was corrected
    bool foundCorrection = false;
    for (const auto& correction : enhancedResult.corrections) {
        if (correction.originalText == "infraction" && correction.correctedText == "infarction") {
            foundCorrection = true;
            break;
        }
    }
    EXPECT_TRUE(foundCorrection);
}

TEST_F(ContextualTranscriberTest, VocabularyLearningTest) {
    std::vector<ContextualCorrection> corrections;
    
    ContextualCorrection correction;
    correction.originalText = "hart";
    correction.correctedText = "heart";
    correction.correctionType = "domain_term";
    correction.confidence = 0.9f;
    correction.reasoning = "Medical term correction";
    corrections.push_back(correction);
    
    // Learn from corrections (this would be called internally during enhancement)
    // For testing, we'll use the vocabulary manager directly through the transcriber
    auto stats = transcriber_->getVocabularyStatistics("medical");
    size_t initialEntries = stats.totalEntries;
    
    // Add custom vocabulary to trigger learning
    transcriber_->addCustomVocabulary({"heart", "cardiac", "cardiovascular"}, "medical");
    
    auto newStats = transcriber_->getVocabularyStatistics("medical");
    EXPECT_GT(newStats.totalEntries, initialEntries);
}

TEST_F(ContextualTranscriberTest, VocabularySearchTest) {
    // Add some vocabulary
    transcriber_->addCustomVocabulary({"myocardial", "infarction", "cardiovascular"}, "medical");
    
    // Search for terms
    auto results = transcriber_->searchVocabulary("cardio", "medical", 5);
    
    EXPECT_FALSE(results.empty());
    
    // Should find "cardiovascular"
    bool foundCardiovascular = false;
    for (const auto& entry : results) {
        if (entry.term == "cardiovascular") {
            foundCardiovascular = true;
            break;
        }
    }
    EXPECT_TRUE(foundCardiovascular);
}

TEST_F(ContextualTranscriberTest, VocabularyExportImportTest) {
    // Add some vocabulary
    transcriber_->addCustomVocabulary({"test1", "test2", "test3"}, "test_domain");
    
    // Export vocabulary
    std::string exportedData = transcriber_->exportVocabulary("test_domain", "json");
    EXPECT_FALSE(exportedData.empty());
    
    // Clear vocabulary
    transcriber_->removeCustomVocabulary("test_domain");
    
    // Import vocabulary back
    size_t importedCount = transcriber_->importVocabulary(exportedData, "json");
    EXPECT_GT(importedCount, 0);
    
    // Verify vocabulary was restored
    auto results = transcriber_->searchVocabulary("test", "test_domain", 10);
    EXPECT_GE(results.size(), 3);
}

TEST_F(ContextualTranscriberTest, ConfigurationTest) {
    // Test configuration updates
    ContextualTranscriptionConfig config;
    config.enabled = true;
    config.contextualWeight = 0.5f;
    config.enableDomainDetection = true;
    config.maxContextHistory = 15;
    
    bool success = transcriber_->updateConfiguration(config);
    EXPECT_TRUE(success);
    
    auto currentConfig = transcriber_->getCurrentConfiguration();
    EXPECT_EQ(currentConfig.contextualWeight, 0.5f);
    EXPECT_EQ(currentConfig.maxContextHistory, 15);
}

TEST_F(ContextualTranscriberTest, ProcessingStatsTest) {
    std::string stats = transcriber_->getProcessingStats();
    EXPECT_FALSE(stats.empty());
    
    // Should contain JSON with expected fields
    EXPECT_TRUE(stats.find("totalTranscriptions") != std::string::npos);
    EXPECT_TRUE(stats.find("enhancedTranscriptions") != std::string::npos);
    EXPECT_TRUE(stats.find("vocabularyStats") != std::string::npos);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}