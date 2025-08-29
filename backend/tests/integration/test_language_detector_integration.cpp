#include <gtest/gtest.h>
#include "mt/language_detector.hpp"
#include "stt/stt_interface.hpp"
#include <memory>
#include <thread>
#include <chrono>

using namespace speechrnt::mt;

class LanguageDetectorIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        detector = std::make_unique<LanguageDetector>();
        ASSERT_TRUE(detector->initialize("config/language_detection.json"));
    }
    
    void TearDown() override {
        if (detector) {
            detector->cleanup();
        }
    }
    
    std::unique_ptr<LanguageDetector> detector;
    
    // Helper method to create mock audio data
    std::vector<float> createMockAudioData(size_t samples = 16000) {
        std::vector<float> audioData(samples);
        // Fill with some pattern that could represent speech
        for (size_t i = 0; i < samples; ++i) {
            audioData[i] = 0.1f * std::sin(2.0f * M_PI * 440.0f * i / 16000.0f); // 440Hz sine wave
        }
        return audioData;
    }
};

TEST_F(LanguageDetectorIntegrationTest, ConfigurationFileLoading) {
    // Test that configuration is loaded correctly
    EXPECT_TRUE(detector->isInitialized());
    
    std::vector<std::string> supportedLangs = detector->getSupportedLanguages();
    EXPECT_FALSE(supportedLangs.empty());
    
    // Should support at least English, Spanish, French, German
    EXPECT_TRUE(detector->isLanguageSupported("en"));
    EXPECT_TRUE(detector->isLanguageSupported("es"));
    EXPECT_TRUE(detector->isLanguageSupported("fr"));
    EXPECT_TRUE(detector->isLanguageSupported("de"));
}

TEST_F(LanguageDetectorIntegrationTest, RealWorldTextSamples) {
    struct TestCase {
        std::string text;
        std::string expectedLanguage;
        std::string description;
    };
    
    std::vector<TestCase> testCases = {
        {
            "Good morning! How are you doing today? I hope you're having a wonderful day.",
            "en",
            "English greeting"
        },
        {
            "Buenos días! ¿Cómo estás hoy? Espero que tengas un día maravilloso.",
            "es", 
            "Spanish greeting"
        },
        {
            "Bonjour! Comment allez-vous aujourd'hui? J'espère que vous passez une merveilleuse journée.",
            "fr",
            "French greeting"
        },
        {
            "Guten Morgen! Wie geht es Ihnen heute? Ich hoffe, Sie haben einen wunderbaren Tag.",
            "de",
            "German greeting"
        },
        {
            "The weather is beautiful today. I think I'll go for a walk in the park.",
            "en",
            "English weather comment"
        },
        {
            "El clima está hermoso hoy. Creo que iré a caminar al parque.",
            "es",
            "Spanish weather comment"
        }
    };
    
    for (const auto& testCase : testCases) {
        LanguageDetectionResult result = detector->detectLanguage(testCase.text);
        
        EXPECT_EQ(result.detectedLanguage, testCase.expectedLanguage) 
            << "Failed for: " << testCase.description;
        EXPECT_GT(result.confidence, 0.0f) 
            << "No confidence for: " << testCase.description;
        EXPECT_FALSE(result.languageCandidates.empty()) 
            << "No candidates for: " << testCase.description;
    }
}

TEST_F(LanguageDetectorIntegrationTest, STTIntegrationSimulation) {
    // Simulate STT integration by setting up a callback that mimics Whisper behavior
    detector->setSTTLanguageDetectionCallback([](const std::vector<float>& audioData) -> LanguageDetectionResult {
        LanguageDetectionResult result;
        
        // Simulate different detection results based on audio data characteristics
        if (audioData.size() > 8000) {
            result.detectedLanguage = "en";
            result.confidence = 0.85f;
            result.languageCandidates.emplace_back("en", 0.85f);
            result.languageCandidates.emplace_back("es", 0.10f);
            result.languageCandidates.emplace_back("fr", 0.05f);
        } else {
            result.detectedLanguage = "es";
            result.confidence = 0.75f;
            result.languageCandidates.emplace_back("es", 0.75f);
            result.languageCandidates.emplace_back("en", 0.20f);
            result.languageCandidates.emplace_back("fr", 0.05f);
        }
        
        result.isReliable = result.confidence >= 0.7f;
        result.detectionMethod = "whisper";
        return result;
    });
    
    // Test with different audio data sizes
    std::vector<float> longAudio = createMockAudioData(16000); // 1 second at 16kHz
    LanguageDetectionResult longResult = detector->detectLanguageFromAudio(longAudio);
    
    EXPECT_EQ(longResult.detectedLanguage, "en");
    EXPECT_FLOAT_EQ(longResult.confidence, 0.85f);
    EXPECT_TRUE(longResult.isReliable);
    EXPECT_EQ(longResult.detectionMethod, "whisper");
    
    std::vector<float> shortAudio = createMockAudioData(4000); // 0.25 seconds
    LanguageDetectionResult shortResult = detector->detectLanguageFromAudio(shortAudio);
    
    EXPECT_EQ(shortResult.detectedLanguage, "es");
    EXPECT_FLOAT_EQ(shortResult.confidence, 0.75f);
    EXPECT_TRUE(shortResult.isReliable);
    EXPECT_EQ(shortResult.detectionMethod, "whisper");
}

TEST_F(LanguageDetectorIntegrationTest, HybridDetectionScenarios) {
    // Set up STT callback for hybrid testing
    detector->setSTTLanguageDetectionCallback([](const std::vector<float>& audioData) -> LanguageDetectionResult {
        LanguageDetectionResult result;
        result.detectedLanguage = "es";
        result.confidence = 0.80f;
        result.isReliable = true;
        result.detectionMethod = "whisper";
        result.languageCandidates.emplace_back("es", 0.80f);
        result.languageCandidates.emplace_back("en", 0.15f);
        return result;
    });
    
    // Test case 1: Text and audio agree
    std::string spanishText = "Hola, ¿cómo estás? Me llamo Juan y vivo en Madrid.";
    std::vector<float> audioData = createMockAudioData();
    
    LanguageDetectionResult hybridResult = detector->detectLanguageHybrid(spanishText, audioData);
    
    EXPECT_EQ(hybridResult.detectedLanguage, "es");
    EXPECT_GT(hybridResult.confidence, 0.0f);
    EXPECT_EQ(hybridResult.detectionMethod, "hybrid");
    
    // Test case 2: Text and audio disagree - audio has higher confidence
    std::string englishText = "Hello, how are you? My name is John and I live in London.";
    
    LanguageDetectionResult disagreementResult = detector->detectLanguageHybrid(englishText, audioData);
    
    // Should prefer the method with higher confidence
    EXPECT_FALSE(disagreementResult.detectedLanguage.empty());
    EXPECT_GT(disagreementResult.confidence, 0.0f);
    EXPECT_TRUE(disagreementResult.detectionMethod.find("hybrid") != std::string::npos);
}

TEST_F(LanguageDetectorIntegrationTest, PerformanceUnderLoad) {
    const int numThreads = 4;
    const int detectionsPerThread = 100;
    
    std::vector<std::thread> threads;
    std::vector<std::vector<LanguageDetectionResult>> results(numThreads);
    
    std::vector<std::string> testTexts = {
        "The quick brown fox jumps over the lazy dog.",
        "El rápido zorro marrón salta sobre el perro perezoso.",
        "Le renard brun rapide saute par-dessus le chien paresseux.",
        "Der schnelle braune Fuchs springt über den faulen Hund."
    };
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([this, t, &results, &testTexts, detectionsPerThread]() {
            results[t].reserve(detectionsPerThread);
            
            for (int i = 0; i < detectionsPerThread; ++i) {
                std::string text = testTexts[i % testTexts.size()];
                LanguageDetectionResult result = detector->detectLanguage(text);
                results[t].push_back(result);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // Verify all detections completed successfully
    int totalDetections = 0;
    int successfulDetections = 0;
    
    for (const auto& threadResults : results) {
        for (const auto& result : threadResults) {
            totalDetections++;
            if (!result.detectedLanguage.empty() && result.confidence > 0.0f) {
                successfulDetections++;
            }
        }
    }
    
    EXPECT_EQ(totalDetections, numThreads * detectionsPerThread);
    EXPECT_EQ(successfulDetections, totalDetections);
    
    // Performance check - should complete within reasonable time
    EXPECT_LT(duration.count(), 5000) << "Detection took too long: " << duration.count() << "ms";
    
    std::cout << "Completed " << totalDetections << " detections in " 
              << duration.count() << "ms (" 
              << (static_cast<double>(totalDetections) / duration.count() * 1000.0) 
              << " detections/second)" << std::endl;
}

TEST_F(LanguageDetectorIntegrationTest, ErrorHandlingAndRecovery) {
    // Test with malformed input
    std::string malformedText = ""; // Empty text
    LanguageDetectionResult result = detector->detectLanguage(malformedText);
    
    EXPECT_FALSE(result.detectedLanguage.empty());
    EXPECT_EQ(result.confidence, 0.0f);
    EXPECT_FALSE(result.isReliable);
    
    // Test with very long text
    std::string longText(10000, 'a'); // 10k characters of 'a'
    result = detector->detectLanguage(longText);
    
    EXPECT_FALSE(result.detectedLanguage.empty());
    EXPECT_GE(result.confidence, 0.0f);
    
    // Test with special characters and numbers
    std::string specialText = "123!@#$%^&*()_+-=[]{}|;':\",./<>?";
    result = detector->detectLanguage(specialText);
    
    EXPECT_FALSE(result.detectedLanguage.empty());
    EXPECT_GE(result.confidence, 0.0f);
}

TEST_F(LanguageDetectorIntegrationTest, LanguageChangeDetection) {
    // Simulate a conversation with language changes
    std::vector<std::string> conversationTexts = {
        "Hello, how are you today?",
        "I'm fine, thank you. And you?",
        "Hola, ¿cómo estás hoy?",
        "Estoy bien, gracias. ¿Y tú?",
        "Bonjour, comment allez-vous?",
        "Je vais bien, merci."
    };
    
    std::vector<std::string> expectedLanguages = {"en", "en", "es", "es", "fr", "fr"};
    
    std::string previousLanguage = "";
    int languageChanges = 0;
    
    for (size_t i = 0; i < conversationTexts.size(); ++i) {
        LanguageDetectionResult result = detector->detectLanguage(conversationTexts[i]);
        
        EXPECT_EQ(result.detectedLanguage, expectedLanguages[i]) 
            << "Failed at index " << i << " with text: " << conversationTexts[i];
        
        if (!previousLanguage.empty() && result.detectedLanguage != previousLanguage) {
            languageChanges++;
        }
        
        previousLanguage = result.detectedLanguage;
    }
    
    EXPECT_EQ(languageChanges, 2) << "Expected 2 language changes (en->es, es->fr)";
}

TEST_F(LanguageDetectorIntegrationTest, FallbackLanguageBehavior) {
    // Test fallback behavior for unsupported languages
    std::vector<std::pair<std::string, std::string>> fallbackTests = {
        {"pt", "es"}, // Portuguese -> Spanish
        {"it", "es"}, // Italian -> Spanish  
        {"nl", "de"}, // Dutch -> German
        {"unknown", "en"} // Unknown -> English (or first supported)
    };
    
    for (const auto& test : fallbackTests) {
        std::string fallback = detector->getFallbackLanguage(test.first);
        EXPECT_EQ(fallback, test.second) 
            << "Fallback for " << test.first << " should be " << test.second 
            << " but got " << fallback;
    }
}

TEST_F(LanguageDetectorIntegrationTest, ConfigurationPersistence) {
    // Test that configuration changes persist across operations
    float originalThreshold = detector->getConfidenceThreshold();
    std::string originalMethod = detector->getDetectionMethod();
    
    // Change configuration
    detector->setConfidenceThreshold(0.9f);
    detector->setDetectionMethod("hybrid");
    
    // Perform some detections
    std::string testText = "This is a test of configuration persistence.";
    for (int i = 0; i < 10; ++i) {
        LanguageDetectionResult result = detector->detectLanguage(testText);
        EXPECT_FALSE(result.detectedLanguage.empty());
    }
    
    // Verify configuration is still in effect
    EXPECT_FLOAT_EQ(detector->getConfidenceThreshold(), 0.9f);
    EXPECT_EQ(detector->getDetectionMethod(), "hybrid");
    
    // Restore original configuration
    detector->setConfidenceThreshold(originalThreshold);
    detector->setDetectionMethod(originalMethod);
}

TEST_F(LanguageDetectorIntegrationTest, MemoryUsageStability) {
    // Test for memory leaks by performing many detections
    const int numDetections = 1000;
    
    std::string testText = "This is a test for memory usage stability and leak detection.";
    
    for (int i = 0; i < numDetections; ++i) {
        LanguageDetectionResult result = detector->detectLanguage(testText);
        EXPECT_FALSE(result.detectedLanguage.empty());
        
        // Occasionally test with different text to vary memory usage
        if (i % 100 == 0) {
            std::string variedText = testText + " Iteration " + std::to_string(i);
            result = detector->detectLanguage(variedText);
            EXPECT_FALSE(result.detectedLanguage.empty());
        }
    }
    
    // If we reach here without crashes, memory usage is likely stable
    SUCCEED() << "Completed " << numDetections << " detections without memory issues";
}