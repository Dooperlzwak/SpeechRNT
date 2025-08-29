#include <gtest/gtest.h>
#include "mt/language_detector.hpp"
#include <vector>
#include <string>

using namespace speechrnt::mt;

class LanguageDetectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        detector = std::make_unique<LanguageDetector>();
        ASSERT_TRUE(detector->initialize());
    }
    
    void TearDown() override {
        if (detector) {
            detector->cleanup();
        }
    }
    
    std::unique_ptr<LanguageDetector> detector;
};

TEST_F(LanguageDetectorTest, InitializationTest) {
    EXPECT_TRUE(detector->isInitialized());
    EXPECT_GT(detector->getSupportedLanguages().size(), 0);
    EXPECT_GE(detector->getConfidenceThreshold(), 0.0f);
    EXPECT_LE(detector->getConfidenceThreshold(), 1.0f);
}

TEST_F(LanguageDetectorTest, EnglishTextDetection) {
    std::string englishText = "The quick brown fox jumps over the lazy dog. This is a test of English language detection.";
    
    LanguageDetectionResult result = detector->detectLanguage(englishText);
    
    EXPECT_EQ(result.detectedLanguage, "en");
    EXPECT_GT(result.confidence, 0.0f);
    EXPECT_EQ(result.detectionMethod, "text_analysis");
    EXPECT_FALSE(result.languageCandidates.empty());
}

TEST_F(LanguageDetectorTest, SpanishTextDetection) {
    std::string spanishText = "El rápido zorro marrón salta sobre el perro perezoso. Esta es una prueba de detección del idioma español.";
    
    LanguageDetectionResult result = detector->detectLanguage(spanishText);
    
    EXPECT_EQ(result.detectedLanguage, "es");
    EXPECT_GT(result.confidence, 0.0f);
    EXPECT_EQ(result.detectionMethod, "text_analysis");
    EXPECT_FALSE(result.languageCandidates.empty());
}

TEST_F(LanguageDetectorTest, FrenchTextDetection) {
    std::string frenchText = "Le renard brun rapide saute par-dessus le chien paresseux. Ceci est un test de détection de la langue française.";
    
    LanguageDetectionResult result = detector->detectLanguage(frenchText);
    
    EXPECT_EQ(result.detectedLanguage, "fr");
    EXPECT_GT(result.confidence, 0.0f);
    EXPECT_EQ(result.detectionMethod, "text_analysis");
    EXPECT_FALSE(result.languageCandidates.empty());
}

TEST_F(LanguageDetectorTest, GermanTextDetection) {
    std::string germanText = "Der schnelle braune Fuchs springt über den faulen Hund. Dies ist ein Test der deutschen Spracherkennung.";
    
    LanguageDetectionResult result = detector->detectLanguage(germanText);
    
    EXPECT_EQ(result.detectedLanguage, "de");
    EXPECT_GT(result.confidence, 0.0f);
    EXPECT_EQ(result.detectionMethod, "text_analysis");
    EXPECT_FALSE(result.languageCandidates.empty());
}

TEST_F(LanguageDetectorTest, EmptyTextHandling) {
    std::string emptyText = "";
    
    LanguageDetectionResult result = detector->detectLanguage(emptyText);
    
    EXPECT_FALSE(result.detectedLanguage.empty());
    EXPECT_EQ(result.confidence, 0.0f);
    EXPECT_FALSE(result.isReliable);
    EXPECT_EQ(result.detectionMethod, "empty_input");
}

TEST_F(LanguageDetectorTest, ShortTextHandling) {
    std::string shortText = "Hi";
    
    LanguageDetectionResult result = detector->detectLanguage(shortText);
    
    EXPECT_FALSE(result.detectedLanguage.empty());
    EXPECT_LT(result.confidence, 0.5f);
    EXPECT_FALSE(result.isReliable);
}

TEST_F(LanguageDetectorTest, ConfidenceThresholdConfiguration) {
    float originalThreshold = detector->getConfidenceThreshold();
    
    detector->setConfidenceThreshold(0.8f);
    EXPECT_FLOAT_EQ(detector->getConfidenceThreshold(), 0.8f);
    
    detector->setConfidenceThreshold(-0.1f); // Should clamp to 0.0
    EXPECT_FLOAT_EQ(detector->getConfidenceThreshold(), 0.0f);
    
    detector->setConfidenceThreshold(1.5f); // Should clamp to 1.0
    EXPECT_FLOAT_EQ(detector->getConfidenceThreshold(), 1.0f);
    
    // Restore original threshold
    detector->setConfidenceThreshold(originalThreshold);
}

TEST_F(LanguageDetectorTest, DetectionMethodConfiguration) {
    detector->setDetectionMethod("text_analysis");
    EXPECT_EQ(detector->getDetectionMethod(), "text_analysis");
    
    detector->setDetectionMethod("whisper");
    EXPECT_EQ(detector->getDetectionMethod(), "whisper");
    
    detector->setDetectionMethod("hybrid");
    EXPECT_EQ(detector->getDetectionMethod(), "hybrid");
    
    detector->setDetectionMethod("invalid_method");
    EXPECT_NE(detector->getDetectionMethod(), "invalid_method");
}

TEST_F(LanguageDetectorTest, SupportedLanguagesConfiguration) {
    std::vector<std::string> originalLanguages = detector->getSupportedLanguages();
    
    std::vector<std::string> newLanguages = {"en", "es"};
    detector->setSupportedLanguages(newLanguages);
    
    std::vector<std::string> currentLanguages = detector->getSupportedLanguages();
    EXPECT_EQ(currentLanguages.size(), 2);
    EXPECT_TRUE(std::find(currentLanguages.begin(), currentLanguages.end(), "en") != currentLanguages.end());
    EXPECT_TRUE(std::find(currentLanguages.begin(), currentLanguages.end(), "es") != currentLanguages.end());
    
    // Restore original languages
    detector->setSupportedLanguages(originalLanguages);
}

TEST_F(LanguageDetectorTest, LanguageSupportValidation) {
    EXPECT_TRUE(detector->isLanguageSupported("en"));
    EXPECT_TRUE(detector->isLanguageSupported("es"));
    EXPECT_TRUE(detector->isLanguageSupported("fr"));
    EXPECT_TRUE(detector->isLanguageSupported("de"));
    
    EXPECT_FALSE(detector->isLanguageSupported("zh"));
    EXPECT_FALSE(detector->isLanguageSupported("invalid"));
}

TEST_F(LanguageDetectorTest, FallbackLanguageMapping) {
    std::string fallback = detector->getFallbackLanguage("pt");
    EXPECT_EQ(fallback, "es");
    
    fallback = detector->getFallbackLanguage("it");
    EXPECT_EQ(fallback, "es");
    
    fallback = detector->getFallbackLanguage("nl");
    EXPECT_EQ(fallback, "de");
    
    // Unknown language should fallback to English or first supported language
    fallback = detector->getFallbackLanguage("unknown");
    EXPECT_FALSE(fallback.empty());
}

TEST_F(LanguageDetectorTest, AudioDetectionWithoutCallback) {
    std::vector<float> audioData(1000, 0.5f); // Dummy audio data
    
    LanguageDetectionResult result = detector->detectLanguageFromAudio(audioData);
    
    EXPECT_FALSE(result.detectedLanguage.empty());
    EXPECT_EQ(result.confidence, 0.0f);
    EXPECT_FALSE(result.isReliable);
    EXPECT_EQ(result.detectionMethod, "no_stt_callback");
}

TEST_F(LanguageDetectorTest, AudioDetectionWithCallback) {
    // Set up a mock STT callback
    detector->setSTTLanguageDetectionCallback([](const std::vector<float>& audioData) -> LanguageDetectionResult {
        LanguageDetectionResult result;
        result.detectedLanguage = "en";
        result.confidence = 0.85f;
        result.isReliable = true;
        result.detectionMethod = "whisper";
        result.languageCandidates.emplace_back("en", 0.85f);
        result.languageCandidates.emplace_back("es", 0.10f);
        return result;
    });
    
    std::vector<float> audioData(1000, 0.5f); // Dummy audio data
    
    LanguageDetectionResult result = detector->detectLanguageFromAudio(audioData);
    
    EXPECT_EQ(result.detectedLanguage, "en");
    EXPECT_FLOAT_EQ(result.confidence, 0.85f);
    EXPECT_TRUE(result.isReliable);
    EXPECT_EQ(result.detectionMethod, "whisper");
    EXPECT_EQ(result.languageCandidates.size(), 2);
}

TEST_F(LanguageDetectorTest, HybridDetectionAgreement) {
    // Set up a mock STT callback that agrees with text detection
    detector->setSTTLanguageDetectionCallback([](const std::vector<float>& audioData) -> LanguageDetectionResult {
        LanguageDetectionResult result;
        result.detectedLanguage = "en";
        result.confidence = 0.80f;
        result.isReliable = true;
        result.detectionMethod = "whisper";
        result.languageCandidates.emplace_back("en", 0.80f);
        return result;
    });
    
    std::string englishText = "The quick brown fox jumps over the lazy dog.";
    std::vector<float> audioData(1000, 0.5f);
    
    LanguageDetectionResult result = detector->detectLanguageHybrid(englishText, audioData);
    
    EXPECT_EQ(result.detectedLanguage, "en");
    EXPECT_GT(result.confidence, 0.0f);
    EXPECT_EQ(result.detectionMethod, "hybrid");
}

TEST_F(LanguageDetectorTest, HybridDetectionDisagreement) {
    // Set up a mock STT callback that disagrees with text detection
    detector->setSTTLanguageDetectionCallback([](const std::vector<float>& audioData) -> LanguageDetectionResult {
        LanguageDetectionResult result;
        result.detectedLanguage = "es";
        result.confidence = 0.90f; // Higher confidence than text
        result.isReliable = true;
        result.detectionMethod = "whisper";
        result.languageCandidates.emplace_back("es", 0.90f);
        return result;
    });
    
    std::string englishText = "The quick brown fox jumps over the lazy dog.";
    std::vector<float> audioData(1000, 0.5f);
    
    LanguageDetectionResult result = detector->detectLanguageHybrid(englishText, audioData);
    
    // Should prefer the method with higher confidence (audio in this case)
    EXPECT_EQ(result.detectedLanguage, "es");
    EXPECT_FLOAT_EQ(result.confidence, 0.90f);
    EXPECT_TRUE(result.detectionMethod.find("hybrid") != std::string::npos);
}

TEST_F(LanguageDetectorTest, LanguageCandidatesOrdering) {
    std::string mixedText = "Hello mundo, comment allez-vous? Wie geht es dir?";
    
    LanguageDetectionResult result = detector->detectLanguage(mixedText);
    
    EXPECT_FALSE(result.languageCandidates.empty());
    
    // Candidates should be ordered by confidence (highest first)
    for (size_t i = 1; i < result.languageCandidates.size(); ++i) {
        EXPECT_GE(result.languageCandidates[i-1].second, result.languageCandidates[i].second);
    }
}

TEST_F(LanguageDetectorTest, ReliabilityThreshold) {
    detector->setConfidenceThreshold(0.8f);
    
    std::string clearEnglishText = "The quick brown fox jumps over the lazy dog. This is clearly English text with common English words.";
    LanguageDetectionResult result = detector->detectLanguage(clearEnglishText);
    
    // With clear text, reliability should depend on confidence threshold
    if (result.confidence >= 0.8f) {
        EXPECT_TRUE(result.isReliable);
    } else {
        EXPECT_FALSE(result.isReliable);
    }
}

TEST_F(LanguageDetectorTest, ThreadSafety) {
    // Basic thread safety test - create multiple threads doing detection
    std::vector<std::thread> threads;
    std::vector<LanguageDetectionResult> results(4);
    
    std::string texts[] = {
        "The quick brown fox jumps over the lazy dog.",
        "El rápido zorro marrón salta sobre el perro perezoso.",
        "Le renard brun rapide saute par-dessus le chien paresseux.",
        "Der schnelle braune Fuchs springt über den faulen Hund."
    };
    
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([this, &results, &texts, i]() {
            results[i] = detector->detectLanguage(texts[i]);
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify all detections completed successfully
    for (const auto& result : results) {
        EXPECT_FALSE(result.detectedLanguage.empty());
        EXPECT_GE(result.confidence, 0.0f);
    }
}

// Test fixture for testing without initialization
class LanguageDetectorUninitializedTest : public ::testing::Test {
protected:
    void SetUp() override {
        detector = std::make_unique<LanguageDetector>();
        // Don't initialize
    }
    
    std::unique_ptr<LanguageDetector> detector;
};

TEST_F(LanguageDetectorUninitializedTest, UninitializedBehavior) {
    EXPECT_FALSE(detector->isInitialized());
    
    std::string text = "Hello world";
    LanguageDetectionResult result = detector->detectLanguage(text);
    
    EXPECT_FALSE(result.detectedLanguage.empty());
    EXPECT_EQ(result.confidence, 0.0f);
    EXPECT_FALSE(result.isReliable);
}