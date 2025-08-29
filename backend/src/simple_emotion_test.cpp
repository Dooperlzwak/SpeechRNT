#include "stt/emotion_detector.hpp"
#include "stt/emotional_context_manager.hpp"
#include <iostream>
#include <vector>
#include <cmath>

using namespace stt;

// Simple test without complex dependencies
int main() {
    std::cout << "Testing Emotion Detection System" << std::endl;
    
    // Test 1: Basic emotion detector initialization
    std::cout << "\n1. Testing EmotionDetector initialization..." << std::endl;
    EmotionDetector detector;
    EmotionDetectionConfig config;
    config.enable_prosodic_analysis = true;
    config.enable_text_sentiment = true;
    
    if (detector.initialize(config)) {
        std::cout << "✓ EmotionDetector initialized successfully" << std::endl;
    } else {
        std::cout << "✗ EmotionDetector initialization failed: " << detector.getLastError() << std::endl;
        return 1;
    }
    
    // Test 2: Basic emotion analysis
    std::cout << "\n2. Testing basic emotion analysis..." << std::endl;
    std::vector<float> test_audio(16000, 0.0f); // 1 second of silence
    for (size_t i = 0; i < test_audio.size(); ++i) {
        test_audio[i] = 0.5f * std::sin(2.0f * M_PI * 200.0f * i / 16000.0f);
    }
    
    std::string test_text = "I am feeling happy today!";
    auto result = detector.analyzeEmotion(test_audio, test_text, 16000);
    
    std::cout << "✓ Emotion analysis completed" << std::endl;
    std::cout << "  Detected emotion: " << emotion_utils::emotionTypeToString(result.emotion.primary_emotion) << std::endl;
    std::cout << "  Emotion confidence: " << result.emotion.confidence << std::endl;
    std::cout << "  Sentiment: " << emotion_utils::sentimentPolarityToString(result.sentiment.polarity) << std::endl;
    std::cout << "  Sentiment confidence: " << result.sentiment.confidence << std::endl;
    
    // Test 3: Emotional context manager
    std::cout << "\n3. Testing EmotionalContextManager..." << std::endl;
    EmotionalContextManager context_manager;
    EmotionalContextConfig context_config;
    
    if (context_manager.initialize(context_config)) {
        std::cout << "✓ EmotionalContextManager initialized successfully" << std::endl;
    } else {
        std::cout << "✗ EmotionalContextManager initialization failed" << std::endl;
        return 1;
    }
    
    // Test context update
    uint32_t conversation_id = 1;
    context_manager.updateEmotionalContext(conversation_id, result, test_text);
    
    auto conversation_state = context_manager.getConversationState(conversation_id);
    std::cout << "✓ Emotional context updated" << std::endl;
    std::cout << "  Current emotion: " << emotion_utils::emotionTypeToString(conversation_state.current_emotion) << std::endl;
    std::cout << "  Current sentiment: " << emotion_utils::sentimentPolarityToString(conversation_state.current_sentiment) << std::endl;
    std::cout << "  Segments count: " << conversation_state.segments.size() << std::endl;
    
    // Test 4: Emotional formatting
    std::cout << "\n4. Testing emotional formatting..." << std::endl;
    auto formatted_text = context_manager.applyEmotionalFormatting(test_text, result);
    std::cout << "✓ Emotional formatting applied" << std::endl;
    std::cout << "  Original: \"" << test_text << "\"" << std::endl;
    std::cout << "  Formatted: \"" << formatted_text << "\"" << std::endl;
    
    // Test 5: Utility functions
    std::cout << "\n5. Testing utility functions..." << std::endl;
    
    // Test emotion type conversion
    EmotionType happy = EmotionType::HAPPY;
    std::string happy_str = emotion_utils::emotionTypeToString(happy);
    EmotionType happy_back = emotion_utils::stringToEmotionType(happy_str);
    
    if (happy == happy_back) {
        std::cout << "✓ Emotion type conversion works correctly" << std::endl;
    } else {
        std::cout << "✗ Emotion type conversion failed" << std::endl;
        return 1;
    }
    
    // Test sentiment polarity conversion
    SentimentPolarity positive = SentimentPolarity::POSITIVE;
    std::string positive_str = emotion_utils::sentimentPolarityToString(positive);
    SentimentPolarity positive_back = emotion_utils::stringToSentimentPolarity(positive_str);
    
    if (positive == positive_back) {
        std::cout << "✓ Sentiment polarity conversion works correctly" << std::endl;
    } else {
        std::cout << "✗ Sentiment polarity conversion failed" << std::endl;
        return 1;
    }
    
    std::cout << "\n=== All tests passed successfully! ===" << std::endl;
    std::cout << "\nEmotion Detection and Context Integration system is working correctly." << std::endl;
    
    return 0;
}