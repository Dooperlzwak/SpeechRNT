#include "stt/emotion_detector.hpp"
#include "stt/emotional_context_manager.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <random>

using namespace stt;

// Generate synthetic audio data for testing
std::vector<float> generateTestAudio(float frequency, float duration, int sample_rate, float amplitude = 0.5f) {
    size_t num_samples = static_cast<size_t>(duration * sample_rate);
    std::vector<float> audio_data(num_samples);
    
    for (size_t i = 0; i < num_samples; ++i) {
        float t = static_cast<float>(i) / sample_rate;
        audio_data[i] = amplitude * std::sin(2.0f * M_PI * frequency * t);
    }
    
    return audio_data;
}

// Add noise to audio data
std::vector<float> addNoise(const std::vector<float>& audio_data, float noise_level) {
    std::vector<float> noisy_data = audio_data;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<float> noise_dist(0.0f, noise_level);
    
    for (auto& sample : noisy_data) {
        sample += noise_dist(gen);
    }
    
    return noisy_data;
}

void testEmotionDetector() {
    std::cout << "\n=== Testing EmotionDetector ===" << std::endl;
    
    // Initialize emotion detector
    EmotionDetector detector;
    EmotionDetectionConfig config;
    config.enable_prosodic_analysis = true;
    config.enable_text_sentiment = true;
    config.enable_emotion_tracking = true;
    config.emotion_confidence_threshold = 0.5f;
    config.sentiment_confidence_threshold = 0.6f;
    
    if (!detector.initialize(config)) {
        std::cerr << "Failed to initialize EmotionDetector: " << detector.getLastError() << std::endl;
        return;
    }
    
    std::cout << "EmotionDetector initialized successfully" << std::endl;
    
    // Test 1: Happy/excited audio (high frequency, high energy)
    std::cout << "\nTest 1: Happy/excited audio" << std::endl;
    auto happy_audio = generateTestAudio(300.0f, 2.0f, 16000, 0.8f);
    std::string happy_text = "This is amazing! I'm so excited about this project!";
    
    auto happy_result = detector.analyzeEmotion(happy_audio, happy_text, 16000);
    std::cout << "Detected emotion: " << emotion_utils::emotionTypeToString(happy_result.emotion.primary_emotion) << std::endl;
    std::cout << "Emotion confidence: " << happy_result.emotion.confidence << std::endl;
    std::cout << "Sentiment: " << emotion_utils::sentimentPolarityToString(happy_result.sentiment.polarity) << std::endl;
    std::cout << "Sentiment confidence: " << happy_result.sentiment.confidence << std::endl;
    std::cout << "Arousal: " << happy_result.emotion.arousal << std::endl;
    std::cout << "Valence: " << happy_result.emotion.valence << std::endl;
    
    // Test 2: Sad audio (low frequency, low energy)
    std::cout << "\nTest 2: Sad audio" << std::endl;
    auto sad_audio = generateTestAudio(120.0f, 2.0f, 16000, 0.3f);
    std::string sad_text = "I'm really disappointed and upset about this situation.";
    
    auto sad_result = detector.analyzeEmotion(sad_audio, sad_text, 16000);
    std::cout << "Detected emotion: " << emotion_utils::emotionTypeToString(sad_result.emotion.primary_emotion) << std::endl;
    std::cout << "Emotion confidence: " << sad_result.emotion.confidence << std::endl;
    std::cout << "Sentiment: " << emotion_utils::sentimentPolarityToString(sad_result.sentiment.polarity) << std::endl;
    std::cout << "Sentiment confidence: " << sad_result.sentiment.confidence << std::endl;
    std::cout << "Arousal: " << sad_result.emotion.arousal << std::endl;
    std::cout << "Valence: " << sad_result.emotion.valence << std::endl;
    
    // Test 3: Angry audio (high frequency variation, high energy)
    std::cout << "\nTest 3: Angry audio" << std::endl;
    auto angry_audio = generateTestAudio(250.0f, 2.0f, 16000, 0.9f);
    // Add frequency variation to simulate anger
    for (size_t i = 0; i < angry_audio.size(); i += 100) {
        float variation = 0.3f * std::sin(static_cast<float>(i) / 1000.0f);
        for (size_t j = i; j < std::min(i + 100, angry_audio.size()); ++j) {
            angry_audio[j] *= (1.0f + variation);
        }
    }
    std::string angry_text = "This is absolutely terrible! I hate this situation!";
    
    auto angry_result = detector.analyzeEmotion(angry_audio, angry_text, 16000);
    std::cout << "Detected emotion: " << emotion_utils::emotionTypeToString(angry_result.emotion.primary_emotion) << std::endl;
    std::cout << "Emotion confidence: " << angry_result.emotion.confidence << std::endl;
    std::cout << "Sentiment: " << emotion_utils::sentimentPolarityToString(angry_result.sentiment.polarity) << std::endl;
    std::cout << "Sentiment confidence: " << angry_result.sentiment.confidence << std::endl;
    std::cout << "Arousal: " << angry_result.emotion.arousal << std::endl;
    std::cout << "Valence: " << angry_result.emotion.valence << std::endl;
    
    // Test 4: Neutral audio and text
    std::cout << "\nTest 4: Neutral audio" << std::endl;
    auto neutral_audio = generateTestAudio(180.0f, 2.0f, 16000, 0.5f);
    std::string neutral_text = "The weather is okay today. Nothing special happening.";
    
    auto neutral_result = detector.analyzeEmotion(neutral_audio, neutral_text, 16000);
    std::cout << "Detected emotion: " << emotion_utils::emotionTypeToString(neutral_result.emotion.primary_emotion) << std::endl;
    std::cout << "Emotion confidence: " << neutral_result.emotion.confidence << std::endl;
    std::cout << "Sentiment: " << emotion_utils::sentimentPolarityToString(neutral_result.sentiment.polarity) << std::endl;
    std::cout << "Sentiment confidence: " << neutral_result.sentiment.confidence << std::endl;
    std::cout << "Arousal: " << neutral_result.emotion.arousal << std::endl;
    std::cout << "Valence: " << neutral_result.emotion.valence << std::endl;
    
    // Test emotion history
    std::cout << "\nEmotion history:" << std::endl;
    auto history = detector.getEmotionHistory(4);
    for (size_t i = 0; i < history.size(); ++i) {
        std::cout << "  " << i+1 << ". " << emotion_utils::emotionTypeToString(history[i].primary_emotion) 
                  << " (confidence: " << history[i].confidence << ")" << std::endl;
    }
}

void testEmotionalContextManager() {
    std::cout << "\n=== Testing EmotionalContextManager ===" << std::endl;
    
    // Initialize emotional context manager
    EmotionalContextManager context_manager;
    EmotionalContextConfig config;
    config.enable_transcription_influence = true;
    config.enable_emotional_formatting = true;
    config.enable_transition_detection = true;
    config.segment_min_duration_ms = 1000.0f;
    config.transition_threshold = 0.3f;
    
    if (!context_manager.initialize(config)) {
        std::cerr << "Failed to initialize EmotionalContextManager" << std::endl;
        return;
    }
    
    std::cout << "EmotionalContextManager initialized successfully" << std::endl;
    
    // Initialize emotion detector for generating analysis results
    EmotionDetector detector;
    EmotionDetectionConfig emotion_config;
    detector.initialize(emotion_config);
    
    uint32_t conversation_id = 1;
    
    // Simulate a conversation with emotional transitions
    std::vector<std::pair<std::vector<float>, std::string>> conversation_data = {
        {generateTestAudio(180.0f, 1.5f, 16000, 0.5f), "Hello, how are you today?"},
        {generateTestAudio(300.0f, 1.5f, 16000, 0.8f), "I'm fantastic! This is such great news!"},
        {generateTestAudio(320.0f, 1.5f, 16000, 0.9f), "I can't believe how amazing this is!"},
        {generateTestAudio(150.0f, 1.5f, 16000, 0.3f), "Actually, I'm feeling a bit sad now."},
        {generateTestAudio(120.0f, 1.5f, 16000, 0.2f), "This situation is really disappointing."},
        {generateTestAudio(200.0f, 1.5f, 16000, 0.6f), "Well, I guess things are getting better."}
    };
    
    std::cout << "\nProcessing conversation segments:" << std::endl;
    
    for (size_t i = 0; i < conversation_data.size(); ++i) {
        const auto& audio_data = conversation_data[i].first;
        const auto& text = conversation_data[i].second;
        
        // Analyze emotion
        auto analysis_result = detector.analyzeEmotion(audio_data, text, 16000);
        
        // Update emotional context
        context_manager.updateEmotionalContext(conversation_id, analysis_result, text);
        
        std::cout << "Segment " << (i+1) << ":" << std::endl;
        std::cout << "  Text: \"" << text << "\"" << std::endl;
        std::cout << "  Emotion: " << emotion_utils::emotionTypeToString(analysis_result.emotion.primary_emotion) << std::endl;
        std::cout << "  Sentiment: " << emotion_utils::sentimentPolarityToString(analysis_result.sentiment.polarity) << std::endl;
        std::cout << "  Transition: " << (analysis_result.is_emotional_transition ? "Yes" : "No") << std::endl;
        
        // Test emotional formatting
        auto formatted_text = context_manager.applyEmotionalFormatting(text, analysis_result);
        std::cout << "  Formatted: \"" << formatted_text << "\"" << std::endl;
        
        // Test transcription influence
        auto influence = context_manager.calculateTranscriptionInfluence(conversation_id, text, 0.8f);
        std::cout << "  Confidence adjustment: " << influence.confidence_adjustment << std::endl;
        std::cout << "  Formatting style: " << influence.formatting_style << std::endl;
        if (!influence.emotional_markers.empty()) {
            std::cout << "  Emotional markers: ";
            for (const auto& marker : influence.emotional_markers) {
                std::cout << marker << " ";
            }
            std::cout << std::endl;
        }
        
        std::cout << std::endl;
    }
    
    // Get conversation state
    auto conversation_state = context_manager.getConversationState(conversation_id);
    std::cout << "Final conversation state:" << std::endl;
    std::cout << "  Current emotion: " << emotion_utils::emotionTypeToString(conversation_state.current_emotion) << std::endl;
    std::cout << "  Current sentiment: " << emotion_utils::sentimentPolarityToString(conversation_state.current_sentiment) << std::endl;
    std::cout << "  Emotional stability: " << conversation_state.emotional_stability << std::endl;
    std::cout << "  Sentiment trend: " << conversation_state.overall_sentiment_trend << std::endl;
    std::cout << "  Total segments: " << conversation_state.segments.size() << std::endl;
    std::cout << "  Total transitions: " << conversation_state.transitions.size() << std::endl;
    
    // Show emotion distribution
    auto emotion_dist = context_manager.getEmotionDistribution(conversation_id);
    std::cout << "\nEmotion distribution:" << std::endl;
    for (const auto& pair : emotion_dist) {
        std::cout << "  " << emotion_utils::emotionTypeToString(pair.first) 
                  << ": " << (pair.second * 100.0f) << "%" << std::endl;
    }
    
    // Show sentiment distribution
    auto sentiment_dist = context_manager.getSentimentDistribution(conversation_id);
    std::cout << "\nSentiment distribution:" << std::endl;
    for (const auto& pair : sentiment_dist) {
        std::cout << "  " << emotion_utils::sentimentPolarityToString(pair.first) 
                  << ": " << (pair.second * 100.0f) << "%" << std::endl;
    }
    
    // Show transitions
    auto transitions = context_manager.detectEmotionalTransitions(conversation_id);
    std::cout << "\nEmotional transitions:" << std::endl;
    for (const auto& transition : transitions) {
        std::cout << "  " << emotion_utils::emotionTypeToString(transition.from_emotion) 
                  << " -> " << emotion_utils::emotionTypeToString(transition.to_emotion)
                  << " (strength: " << transition.transition_strength 
                  << ", type: " << transition.transition_type << ")" << std::endl;
    }
}

int main() {
    std::cout << "Starting Emotion Detection and Context Integration Tests" << std::endl;
    
    try {
        testEmotionDetector();
        testEmotionalContextManager();
        
        std::cout << "\n=== All tests completed successfully ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}