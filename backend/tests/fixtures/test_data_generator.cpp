#include "test_data_generator.hpp"
#include <random>
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace fixtures {

TestDataGenerator::TestDataGenerator() {
    initializeLanguageData();
    initializeAudioPatterns();
}

void TestDataGenerator::initializeLanguageData() {
    // English test phrases
    languageData_["en"] = {
        {"Hello, how are you?", "greeting"},
        {"What time is it?", "question"},
        {"I need help with directions.", "request"},
        {"The weather is beautiful today.", "statement"},
        {"Can you recommend a good restaurant?", "question"},
        {"Thank you very much for your assistance.", "gratitude"},
        {"I'm sorry, I don't understand.", "apology"},
        {"Where is the nearest hospital?", "emergency"},
        {"How much does this cost?", "shopping"},
        {"I would like to make a reservation.", "booking"}
    };
    
    // Spanish test phrases
    languageData_["es"] = {
        {"Hola, ¿cómo estás?", "greeting"},
        {"¿Qué hora es?", "question"},
        {"Necesito ayuda con las direcciones.", "request"},
        {"El clima está hermoso hoy.", "statement"},
        {"¿Puedes recomendar un buen restaurante?", "question"},
        {"Muchas gracias por tu ayuda.", "gratitude"},
        {"Lo siento, no entiendo.", "apology"},
        {"¿Dónde está el hospital más cercano?", "emergency"},
        {"¿Cuánto cuesta esto?", "shopping"},
        {"Me gustaría hacer una reserva.", "booking"}
    };
    
    // French test phrases
    languageData_["fr"] = {
        {"Bonjour, comment allez-vous?", "greeting"},
        {"Quelle heure est-il?", "question"},
        {"J'ai besoin d'aide pour les directions.", "request"},
        {"Le temps est magnifique aujourd'hui.", "statement"},
        {"Pouvez-vous recommander un bon restaurant?", "question"},
        {"Merci beaucoup pour votre aide.", "gratitude"},
        {"Je suis désolé, je ne comprends pas.", "apology"},
        {"Où est l'hôpital le plus proche?", "emergency"},
        {"Combien cela coûte-t-il?", "shopping"},
        {"J'aimerais faire une réservation.", "booking"}
    };
    
    // German test phrases
    languageData_["de"] = {
        {"Hallo, wie geht es Ihnen?", "greeting"},
        {"Wie spät ist es?", "question"},
        {"Ich brauche Hilfe bei der Wegbeschreibung.", "request"},
        {"Das Wetter ist heute wunderschön.", "statement"},
        {"Können Sie ein gutes Restaurant empfehlen?", "question"},
        {"Vielen Dank für Ihre Hilfe.", "gratitude"},
        {"Es tut mir leid, ich verstehe nicht.", "apology"},
        {"Wo ist das nächste Krankenhaus?", "emergency"},
        {"Wie viel kostet das?", "shopping"},
        {"Ich möchte gerne eine Reservierung machen.", "booking"}
    };
    
    // Japanese test phrases
    languageData_["ja"] = {
        {"こんにちは、元気ですか？", "greeting"},
        {"今何時ですか？", "question"},
        {"道案内を手伝ってください。", "request"},
        {"今日は天気がとても良いです。", "statement"},
        {"良いレストランを教えてください。", "question"},
        {"ご協力ありがとうございます。", "gratitude"},
        {"すみません、わかりません。", "apology"},
        {"一番近い病院はどこですか？", "emergency"},
        {"これはいくらですか？", "shopping"},
        {"予約を取りたいのですが。", "booking"}
    };
}

void TestDataGenerator::initializeAudioPatterns() {
    // Define different audio pattern types
    audioPatterns_["speech"] = {200.0f, 400.0f, 800.0f, 1600.0f}; // Formant frequencies
    audioPatterns_["noise"] = {100.0f, 300.0f, 500.0f, 700.0f};   // Background noise
    audioPatterns_["silence"] = {};                                 // No frequencies
    audioPatterns_["music"] = {440.0f, 880.0f, 1320.0f};          // Musical tones
}

std::vector<TestPhrase> TestDataGenerator::getPhrasesForLanguage(const std::string& language) const {
    auto it = languageData_.find(language);
    if (it != languageData_.end()) {
        return it->second;
    }
    return {};
}

std::vector<TestPhrase> TestDataGenerator::getPhrasesForCategory(const std::string& category) const {
    std::vector<TestPhrase> result;
    
    for (const auto& langPair : languageData_) {
        for (const auto& phrase : langPair.second) {
            if (phrase.category == category) {
                result.push_back(phrase);
            }
        }
    }
    
    return result;
}

std::vector<float> TestDataGenerator::generateSpeechAudio(
    const std::string& text, 
    float duration, 
    int sampleRate,
    const AudioCharacteristics& characteristics
) const {
    std::vector<float> audio;
    int numSamples = static_cast<int>(duration * sampleRate);
    audio.reserve(numSamples);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<float> noise(0.0f, characteristics.noiseLevel);
    
    // Get formant frequencies for speech
    auto formants = audioPatterns_.at("speech");
    
    for (int i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float sample = 0.0f;
        
        // Generate speech-like signal with formants
        for (size_t f = 0; f < formants.size(); ++f) {
            float amplitude = characteristics.amplitude * (1.0f / (f + 1)); // Decreasing amplitude
            sample += amplitude * std::sin(2.0f * M_PI * formants[f] * t);
        }
        
        // Apply envelope based on text characteristics
        float envelope = generateEnvelope(t, duration, text.length());
        sample *= envelope;
        
        // Add noise
        sample += noise(gen);
        
        // Apply pitch variation
        if (characteristics.pitchVariation > 0.0f) {
            float pitchMod = characteristics.pitchVariation * std::sin(2.0f * M_PI * 3.0f * t);
            sample *= (1.0f + pitchMod);
        }
        
        // Clamp to valid range
        audio.push_back(std::clamp(sample, -1.0f, 1.0f));
    }
    
    return audio;
}

std::vector<float> TestDataGenerator::generateNoiseAudio(
    float duration, 
    int sampleRate,
    NoiseType noiseType
) const {
    std::vector<float> audio;
    int numSamples = static_cast<int>(duration * sampleRate);
    audio.reserve(numSamples);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    switch (noiseType) {
        case NoiseType::WHITE: {
            std::normal_distribution<float> dist(0.0f, 0.1f);
            for (int i = 0; i < numSamples; ++i) {
                audio.push_back(dist(gen));
            }
            break;
        }
        
        case NoiseType::PINK: {
            // Simplified pink noise generation
            std::normal_distribution<float> dist(0.0f, 0.1f);
            float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
            
            for (int i = 0; i < numSamples; ++i) {
                float white = dist(gen);
                b0 = 0.99886f * b0 + white * 0.0555179f;
                b1 = 0.99332f * b1 + white * 0.0750759f;
                b2 = 0.96900f * b2 + white * 0.1538520f;
                float pink = b0 + b1 + b2 + white * 0.3104856f;
                audio.push_back(pink * 0.11f);
            }
            break;
        }
        
        case NoiseType::BROWN: {
            // Brown noise (Brownian motion)
            std::normal_distribution<float> dist(0.0f, 0.02f);
            float accumulator = 0.0f;
            
            for (int i = 0; i < numSamples; ++i) {
                accumulator += dist(gen);
                accumulator = std::clamp(accumulator, -1.0f, 1.0f);
                audio.push_back(accumulator);
            }
            break;
        }
        
        case NoiseType::ENVIRONMENTAL: {
            // Simulate environmental noise (traffic, crowd, etc.)
            auto noiseFreqs = audioPatterns_.at("noise");
            std::uniform_real_distribution<float> phaseDist(0.0f, 2.0f * M_PI);
            
            for (int i = 0; i < numSamples; ++i) {
                float t = static_cast<float>(i) / sampleRate;
                float sample = 0.0f;
                
                for (float freq : noiseFreqs) {
                    float phase = phaseDist(gen);
                    sample += 0.05f * std::sin(2.0f * M_PI * freq * t + phase);
                }
                
                audio.push_back(sample);
            }
            break;
        }
    }
    
    return audio;
}

std::vector<float> TestDataGenerator::generateSilence(float duration, int sampleRate) const {
    int numSamples = static_cast<int>(duration * sampleRate);
    return std::vector<float>(numSamples, 0.0f);
}

std::vector<float> TestDataGenerator::generateConversationScenario(
    const ConversationScenario& scenario,
    int sampleRate
) const {
    std::vector<float> fullAudio;
    
    for (const auto& segment : scenario.segments) {
        std::vector<float> segmentAudio;
        
        switch (segment.type) {
            case SegmentType::SPEECH: {
                AudioCharacteristics chars;
                chars.amplitude = 0.5f;
                chars.noiseLevel = 0.05f;
                chars.pitchVariation = 0.1f;
                
                segmentAudio = generateSpeechAudio(
                    segment.content, 
                    segment.duration, 
                    sampleRate, 
                    chars
                );
                break;
            }
            
            case SegmentType::SILENCE:
                segmentAudio = generateSilence(segment.duration, sampleRate);
                break;
                
            case SegmentType::NOISE:
                segmentAudio = generateNoiseAudio(
                    segment.duration, 
                    sampleRate, 
                    NoiseType::ENVIRONMENTAL
                );
                break;
                
            case SegmentType::OVERLAP: {
                // Generate overlapping speech (simplified)
                AudioCharacteristics chars1, chars2;
                chars1.amplitude = 0.3f;
                chars2.amplitude = 0.3f;
                chars1.pitchVariation = 0.1f;
                chars2.pitchVariation = 0.15f;
                
                auto speech1 = generateSpeechAudio("Speaker 1", segment.duration, sampleRate, chars1);
                auto speech2 = generateSpeechAudio("Speaker 2", segment.duration, sampleRate, chars2);
                
                segmentAudio.resize(speech1.size());
                for (size_t i = 0; i < speech1.size(); ++i) {
                    segmentAudio[i] = speech1[i] + speech2[i];
                }
                break;
            }
        }
        
        // Append to full audio
        fullAudio.insert(fullAudio.end(), segmentAudio.begin(), segmentAudio.end());
    }
    
    return fullAudio;
}

ConversationScenario TestDataGenerator::createScenario(const std::string& scenarioType) const {
    ConversationScenario scenario;
    scenario.name = scenarioType;
    
    if (scenarioType == "simple_greeting") {
        scenario.segments = {
            {SegmentType::SILENCE, "", 0.5f},
            {SegmentType::SPEECH, "Hello, how are you?", 2.0f},
            {SegmentType::SILENCE, "", 1.0f},
            {SegmentType::SPEECH, "I'm fine, thank you.", 1.5f},
            {SegmentType::SILENCE, "", 0.5f}
        };
    }
    else if (scenarioType == "noisy_environment") {
        scenario.segments = {
            {SegmentType::NOISE, "", 1.0f},
            {SegmentType::SPEECH, "Can you hear me?", 2.0f},
            {SegmentType::NOISE, "", 0.5f},
            {SegmentType::SPEECH, "Yes, but it's very noisy.", 2.5f},
            {SegmentType::NOISE, "", 1.0f}
        };
    }
    else if (scenarioType == "rapid_exchange") {
        scenario.segments = {
            {SegmentType::SPEECH, "Quick question", 1.0f},
            {SegmentType::SILENCE, "", 0.2f},
            {SegmentType::SPEECH, "Yes?", 0.5f},
            {SegmentType::SILENCE, "", 0.2f},
            {SegmentType::SPEECH, "What time?", 0.8f},
            {SegmentType::SILENCE, "", 0.2f},
            {SegmentType::SPEECH, "Three o'clock", 1.0f}
        };
    }
    else if (scenarioType == "overlapping_speech") {
        scenario.segments = {
            {SegmentType::SPEECH, "I was thinking that we should", 2.0f},
            {SegmentType::OVERLAP, "", 1.5f},
            {SegmentType::SPEECH, "Sorry, you go first", 1.5f},
            {SegmentType::SILENCE, "", 0.5f}
        };
    }
    else if (scenarioType == "long_monologue") {
        scenario.segments = {
            {SegmentType::SILENCE, "", 0.5f},
            {SegmentType::SPEECH, "Let me tell you about my experience traveling through Europe last summer. It was an incredible journey that took me through many different countries and cultures.", 8.0f},
            {SegmentType::SILENCE, "", 1.0f}
        };
    }
    
    return scenario;
}

std::vector<ConversationScenario> TestDataGenerator::getAllScenarios() const {
    return {
        createScenario("simple_greeting"),
        createScenario("noisy_environment"),
        createScenario("rapid_exchange"),
        createScenario("overlapping_speech"),
        createScenario("long_monologue")
    };
}

void TestDataGenerator::saveAudioToFile(
    const std::vector<float>& audio, 
    const std::string& filename,
    int sampleRate
) const {
    // Save as simple WAV file
    std::ofstream file(filename, std::ios::binary);
    
    // WAV header
    file.write("RIFF", 4);
    uint32_t fileSize = 36 + audio.size() * sizeof(int16_t);
    file.write(reinterpret_cast<const char*>(&fileSize), 4);
    file.write("WAVE", 4);
    
    // Format chunk
    file.write("fmt ", 4);
    uint32_t fmtSize = 16;
    file.write(reinterpret_cast<const char*>(&fmtSize), 4);
    uint16_t audioFormat = 1; // PCM
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);
    uint16_t numChannels = 1;
    file.write(reinterpret_cast<const char*>(&numChannels), 2);
    uint32_t sampleRateValue = sampleRate;
    file.write(reinterpret_cast<const char*>(&sampleRateValue), 4);
    uint32_t byteRate = sampleRate * sizeof(int16_t);
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    uint16_t blockAlign = sizeof(int16_t);
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    uint16_t bitsPerSample = 16;
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
    
    // Data chunk
    file.write("data", 4);
    uint32_t dataSize = audio.size() * sizeof(int16_t);
    file.write(reinterpret_cast<const char*>(&dataSize), 4);
    
    // Audio data
    for (float sample : audio) {
        int16_t pcmSample = static_cast<int16_t>(sample * 32767.0f);
        file.write(reinterpret_cast<const char*>(&pcmSample), sizeof(int16_t));
    }
}

void TestDataGenerator::generateTestDataSet(const std::string& outputDir) const {
    // Create test data for each language
    for (const auto& langPair : languageData_) {
        const std::string& language = langPair.first;
        const auto& phrases = langPair.second;
        
        for (size_t i = 0; i < phrases.size(); ++i) {
            const auto& phrase = phrases[i];
            
            // Generate audio for this phrase
            AudioCharacteristics chars;
            chars.amplitude = 0.6f;
            chars.noiseLevel = 0.02f;
            chars.pitchVariation = 0.05f;
            
            auto audio = generateSpeechAudio(phrase.text, 3.0f, 16000, chars);
            
            // Save to file
            std::string filename = outputDir + "/" + language + "_" + 
                                 phrase.category + "_" + std::to_string(i) + ".wav";
            saveAudioToFile(audio, filename, 16000);
        }
    }
    
    // Generate scenario-based test data
    auto scenarios = getAllScenarios();
    for (const auto& scenario : scenarios) {
        auto audio = generateConversationScenario(scenario, 16000);
        std::string filename = outputDir + "/scenario_" + scenario.name + ".wav";
        saveAudioToFile(audio, filename, 16000);
    }
    
    // Generate noise samples
    std::vector<std::pair<NoiseType, std::string>> noiseTypes = {
        {NoiseType::WHITE, "white"},
        {NoiseType::PINK, "pink"},
        {NoiseType::BROWN, "brown"},
        {NoiseType::ENVIRONMENTAL, "environmental"}
    };
    
    for (const auto& noisePair : noiseTypes) {
        auto noise = generateNoiseAudio(5.0f, 16000, noisePair.first);
        std::string filename = outputDir + "/noise_" + noisePair.second + ".wav";
        saveAudioToFile(noise, filename, 16000);
    }
}

float TestDataGenerator::generateEnvelope(float t, float duration, size_t textLength) const {
    // Create speech-like envelope based on text characteristics
    float normalizedTime = t / duration;
    
    // Basic envelope shape
    float envelope = 1.0f;
    
    // Attack phase (beginning)
    if (normalizedTime < 0.1f) {
        envelope = normalizedTime / 0.1f;
    }
    // Release phase (ending)
    else if (normalizedTime > 0.9f) {
        envelope = (1.0f - normalizedTime) / 0.1f;
    }
    
    // Add variations based on text length (simulate pauses, emphasis)
    float textFactor = static_cast<float>(textLength) / 50.0f; // Normalize to typical sentence length
    float variation = 0.8f + 0.4f * std::sin(2.0f * M_PI * normalizedTime * textFactor);
    
    return envelope * variation;
}

} // namespace fixtures