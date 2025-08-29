#pragma once

#include <vector>
#include <string>
#include <map>
#include <memory>

namespace fixtures {

struct TestPhrase {
    std::string text;
    std::string category;
};

struct AudioCharacteristics {
    float amplitude = 0.5f;
    float noiseLevel = 0.05f;
    float pitchVariation = 0.1f;
};

enum class NoiseType {
    WHITE,
    PINK,
    BROWN,
    ENVIRONMENTAL
};

enum class SegmentType {
    SPEECH,
    SILENCE,
    NOISE,
    OVERLAP
};

struct ConversationSegment {
    SegmentType type;
    std::string content;
    float duration;
};

struct ConversationScenario {
    std::string name;
    std::vector<ConversationSegment> segments;
};

class TestDataGenerator {
public:
    TestDataGenerator();
    
    // Language-specific test data
    std::vector<TestPhrase> getPhrasesForLanguage(const std::string& language) const;
    std::vector<TestPhrase> getPhrasesForCategory(const std::string& category) const;
    
    // Audio generation
    std::vector<float> generateSpeechAudio(
        const std::string& text, 
        float duration, 
        int sampleRate = 16000,
        const AudioCharacteristics& characteristics = {}
    ) const;
    
    std::vector<float> generateNoiseAudio(
        float duration, 
        int sampleRate = 16000,
        NoiseType noiseType = NoiseType::WHITE
    ) const;
    
    std::vector<float> generateSilence(float duration, int sampleRate = 16000) const;
    
    // Conversation scenarios
    std::vector<float> generateConversationScenario(
        const ConversationScenario& scenario,
        int sampleRate = 16000
    ) const;
    
    ConversationScenario createScenario(const std::string& scenarioType) const;
    std::vector<ConversationScenario> getAllScenarios() const;
    
    // File I/O
    void saveAudioToFile(
        const std::vector<float>& audio, 
        const std::string& filename,
        int sampleRate = 16000
    ) const;
    
    void generateTestDataSet(const std::string& outputDir) const;
    
private:
    void initializeLanguageData();
    void initializeAudioPatterns();
    float generateEnvelope(float t, float duration, size_t textLength) const;
    
    std::map<std::string, std::vector<TestPhrase>> languageData_;
    std::map<std::string, std::vector<float>> audioPatterns_;
};

} // namespace fixtures