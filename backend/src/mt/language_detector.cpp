#include "mt/language_detector.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <cmath>

namespace speechrnt {
namespace mt {

LanguageDetector::LanguageDetector()
    : confidenceThreshold_(0.7f)
    , detectionMethod_("text_analysis")
    , initialized_(false) {
}

LanguageDetector::~LanguageDetector() {
    cleanup();
}

bool LanguageDetector::initialize(const std::string& configPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true;
    }
    
    try {
        // Load configuration
        if (!loadConfiguration(configPath)) {
            std::cerr << "Failed to load language detection configuration from: " << configPath << std::endl;
            // Continue with default configuration
        }
        
        // Initialize language models and data
        initializeLanguageModels();
        loadCharacterFrequencies();
        loadCommonWords();
        loadNgramFrequencies();
        
        initialized_ = true;
        std::cout << "LanguageDetector initialized successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error initializing LanguageDetector: " << e.what() << std::endl;
        return false;
    }
}

void LanguageDetector::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Clean up language models
    for (auto& pair : languageModels_) {
        if (pair.second.modelData) {
            // In a real implementation, this would free model-specific resources
            pair.second.modelData = nullptr;
            pair.second.loaded = false;
        }
    }
    
    languageModels_.clear();
    characterFrequencies_.clear();
    commonWords_.clear();
    ngramFrequencies_.clear();
    
    initialized_ = false;
}

LanguageDetectionResult LanguageDetector::detectLanguage(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        LanguageDetectionResult result;
        result.detectedLanguage = "en"; // Default fallback
        result.confidence = 0.0f;
        result.isReliable = false;
        result.detectionMethod = "fallback";
        return result;
    }
    
    if (text.empty()) {
        LanguageDetectionResult result;
        result.detectedLanguage = supportedLanguages_.empty() ? "en" : supportedLanguages_[0];
        result.confidence = 0.0f;
        result.isReliable = false;
        result.detectionMethod = "empty_input";
        return result;
    }
    
    return detectFromTextFeatures(text);
}

LanguageDetectionResult LanguageDetector::detectLanguageFromAudio(const std::vector<float>& audioData) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LanguageDetectionResult result;
    
    if (!initialized_ || !sttDetectionCallback_) {
        result.detectedLanguage = "en"; // Default fallback
        result.confidence = 0.0f;
        result.isReliable = false;
        result.detectionMethod = "no_stt_callback";
        return result;
    }
    
    if (audioData.empty()) {
        result.detectedLanguage = supportedLanguages_.empty() ? "en" : supportedLanguages_[0];
        result.confidence = 0.0f;
        result.isReliable = false;
        result.detectionMethod = "empty_audio";
        return result;
    }
    
    try {
        result = sttDetectionCallback_(audioData);
        result.detectionMethod = "whisper";
        return result;
    } catch (const std::exception& e) {
        std::cerr << "Error in STT language detection: " << e.what() << std::endl;
        result.detectedLanguage = "en";
        result.confidence = 0.0f;
        result.isReliable = false;
        result.detectionMethod = "stt_error";
        return result;
    }
}

LanguageDetectionResult LanguageDetector::detectLanguageHybrid(const std::string& text, const std::vector<float>& audioData) {
    LanguageDetectionResult textResult = detectLanguage(text);
    LanguageDetectionResult audioResult = detectLanguageFromAudio(audioData);
    
    return combineDetectionResults(textResult, audioResult);
}

void LanguageDetector::setConfidenceThreshold(float threshold) {
    std::lock_guard<std::mutex> lock(mutex_);
    confidenceThreshold_ = std::max(0.0f, std::min(1.0f, threshold));
}

void LanguageDetector::setDetectionMethod(const std::string& method) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (method == "text_analysis" || method == "whisper" || method == "hybrid") {
        detectionMethod_ = method;
    }
}

void LanguageDetector::setSupportedLanguages(const std::vector<std::string>& languages) {
    std::lock_guard<std::mutex> lock(mutex_);
    supportedLanguages_ = languages;
    
    // Reinitialize language models for new languages
    if (initialized_) {
        initializeLanguageModels();
    }
}

void LanguageDetector::setSTTLanguageDetectionCallback(std::function<LanguageDetectionResult(const std::vector<float>&)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    sttDetectionCallback_ = callback;
}

bool LanguageDetector::isLanguageSupported(const std::string& languageCode) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::find(supportedLanguages_.begin(), supportedLanguages_.end(), languageCode) != supportedLanguages_.end();
}

std::string LanguageDetector::getFallbackLanguage(const std::string& unsupportedLanguage) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = fallbackLanguages_.find(unsupportedLanguage);
    if (it != fallbackLanguages_.end()) {
        return it->second;
    }
    
    // Default fallback to English or first supported language
    if (!supportedLanguages_.empty()) {
        auto enIt = std::find(supportedLanguages_.begin(), supportedLanguages_.end(), "en");
        if (enIt != supportedLanguages_.end()) {
            return "en";
        }
        return supportedLanguages_[0];
    }
    
    return "en";
}

LanguageDetectionResult LanguageDetector::detectFromTextFeatures(const std::string& text) {
    LanguageDetectionResult result;
    result.detectionMethod = "text_analysis";
    
    std::string normalizedText = normalizeText(text);
    if (normalizedText.length() < 10) {
        // Text too short for reliable detection
        result.detectedLanguage = supportedLanguages_.empty() ? "en" : supportedLanguages_[0];
        result.confidence = 0.1f;
        result.isReliable = false;
        return result;
    }
    
    std::vector<std::pair<std::string, float>> scores;
    
    // Calculate scores for each supported language
    for (const std::string& lang : supportedLanguages_) {
        float charScore = calculateCharacterFrequencyScore(normalizedText, lang);
        float wordScore = calculateCommonWordScore(normalizedText, lang);
        float ngramScore = calculateNgramScore(normalizedText, lang);
        
        // Weighted combination of scores
        float combinedScore = (charScore * 0.3f) + (wordScore * 0.4f) + (ngramScore * 0.3f);
        scores.emplace_back(lang, combinedScore);
    }
    
    // Sort by score (highest first)
    std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    
    if (!scores.empty()) {
        result.detectedLanguage = scores[0].first;
        result.confidence = scores[0].second;
        result.languageCandidates = scores;
        result.isReliable = result.confidence >= confidenceThreshold_;
    } else {
        result.detectedLanguage = "en";
        result.confidence = 0.0f;
        result.isReliable = false;
    }
    
    return result;
}

LanguageDetectionResult LanguageDetector::detectFromCharacteristics(const std::string& text) {
    // This method focuses on language-specific characteristics
    LanguageDetectionResult result;
    result.detectionMethod = "characteristics";
    
    std::string normalizedText = normalizeText(text);
    std::unordered_map<std::string, float> languageScores;
    
    // Initialize scores
    for (const std::string& lang : supportedLanguages_) {
        languageScores[lang] = 0.0f;
    }
    
    // Check for language-specific patterns
    // Spanish characteristics
    if (std::find(supportedLanguages_.begin(), supportedLanguages_.end(), "es") != supportedLanguages_.end()) {
        std::regex spanishPatterns(R"(\b(el|la|los|las|un|una|de|del|y|en|con|por|para|que|es|son|está|están)\b)", std::regex_constants::icase);
        std::sregex_iterator iter(normalizedText.begin(), normalizedText.end(), spanishPatterns);
        std::sregex_iterator end;
        int spanishMatches = std::distance(iter, end);
        languageScores["es"] += spanishMatches * 0.1f;
    }
    
    // French characteristics
    if (std::find(supportedLanguages_.begin(), supportedLanguages_.end(), "fr") != supportedLanguages_.end()) {
        std::regex frenchPatterns(R"(\b(le|la|les|un|une|de|du|des|et|en|avec|pour|que|est|sont|être|avoir)\b)", std::regex_constants::icase);
        std::sregex_iterator iter(normalizedText.begin(), normalizedText.end(), frenchPatterns);
        std::sregex_iterator end;
        int frenchMatches = std::distance(iter, end);
        languageScores["fr"] += frenchMatches * 0.1f;
    }
    
    // German characteristics
    if (std::find(supportedLanguages_.begin(), supportedLanguages_.end(), "de") != supportedLanguages_.end()) {
        std::regex germanPatterns(R"(\b(der|die|das|ein|eine|und|mit|für|von|zu|ist|sind|haben|sein)\b)", std::regex_constants::icase);
        std::sregex_iterator iter(normalizedText.begin(), normalizedText.end(), germanPatterns);
        std::sregex_iterator end;
        int germanMatches = std::distance(iter, end);
        languageScores["de"] += germanMatches * 0.1f;
    }
    
    // English characteristics
    if (std::find(supportedLanguages_.begin(), supportedLanguages_.end(), "en") != supportedLanguages_.end()) {
        std::regex englishPatterns(R"(\b(the|a|an|and|or|but|in|on|at|to|for|of|with|by|is|are|was|were|have|has|had)\b)", std::regex_constants::icase);
        std::sregex_iterator iter(normalizedText.begin(), normalizedText.end(), englishPatterns);
        std::sregex_iterator end;
        int englishMatches = std::distance(iter, end);
        languageScores["en"] += englishMatches * 0.1f;
    }
    
    // Find the language with the highest score
    auto maxElement = std::max_element(languageScores.begin(), languageScores.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });
    
    if (maxElement != languageScores.end() && maxElement->second > 0.0f) {
        result.detectedLanguage = maxElement->first;
        result.confidence = std::min(1.0f, maxElement->second);
        
        // Create candidates list
        for (const auto& pair : languageScores) {
            if (pair.second > 0.0f) {
                result.languageCandidates.emplace_back(pair.first, pair.second);
            }
        }
        
        // Sort candidates by score
        std::sort(result.languageCandidates.begin(), result.languageCandidates.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        
        result.isReliable = result.confidence >= confidenceThreshold_;
    } else {
        result.detectedLanguage = supportedLanguages_.empty() ? "en" : supportedLanguages_[0];
        result.confidence = 0.0f;
        result.isReliable = false;
    }
    
    return result;
}

float LanguageDetector::calculateCharacterFrequencyScore(const std::string& text, const std::string& language) {
    auto it = characterFrequencies_.find(language);
    if (it == characterFrequencies_.end()) {
        return 0.0f;
    }
    
    const auto& expectedFreq = it->second;
    std::unordered_map<char, int> actualCounts;
    int totalChars = 0;
    
    // Count character frequencies in the text
    for (char c : text) {
        if (std::isalpha(c)) {
            char lowerC = std::tolower(c);
            actualCounts[lowerC]++;
            totalChars++;
        }
    }
    
    if (totalChars == 0) {
        return 0.0f;
    }
    
    // Calculate chi-squared distance
    float chiSquared = 0.0f;
    for (const auto& pair : expectedFreq) {
        char c = pair.first;
        float expectedFreq = pair.second;
        float actualFreq = static_cast<float>(actualCounts[c]) / totalChars;
        
        if (expectedFreq > 0.0f) {
            float diff = actualFreq - expectedFreq;
            chiSquared += (diff * diff) / expectedFreq;
        }
    }
    
    // Convert chi-squared to a similarity score (lower chi-squared = higher similarity)
    return std::exp(-chiSquared / 10.0f);
}

float LanguageDetector::calculateCommonWordScore(const std::string& text, const std::string& language) {
    auto it = commonWords_.find(language);
    if (it == commonWords_.end()) {
        return 0.0f;
    }
    
    const auto& commonWordsList = it->second;
    std::vector<std::string> words = extractWords(text);
    
    if (words.empty()) {
        return 0.0f;
    }
    
    int matches = 0;
    for (const std::string& word : words) {
        std::string lowerWord = word;
        std::transform(lowerWord.begin(), lowerWord.end(), lowerWord.begin(), ::tolower);
        
        if (std::find(commonWordsList.begin(), commonWordsList.end(), lowerWord) != commonWordsList.end()) {
            matches++;
        }
    }
    
    return static_cast<float>(matches) / words.size();
}

float LanguageDetector::calculateNgramScore(const std::string& text, const std::string& language) {
    auto it = ngramFrequencies_.find(language);
    if (it == ngramFrequencies_.end()) {
        return 0.0f;
    }
    
    const auto& expectedNgrams = it->second;
    std::vector<std::string> bigrams = extractNgrams(text, 2);
    std::vector<std::string> trigrams = extractNgrams(text, 3);
    
    float score = 0.0f;
    int totalNgrams = 0;
    
    // Score bigrams
    for (const std::string& bigram : bigrams) {
        auto ngramIt = expectedNgrams.find(bigram);
        if (ngramIt != expectedNgrams.end()) {
            score += ngramIt->second;
        }
        totalNgrams++;
    }
    
    // Score trigrams (weighted more heavily)
    for (const std::string& trigram : trigrams) {
        auto ngramIt = expectedNgrams.find(trigram);
        if (ngramIt != expectedNgrams.end()) {
            score += ngramIt->second * 1.5f; // Weight trigrams more
        }
        totalNgrams++;
    }
    
    return totalNgrams > 0 ? score / totalNgrams : 0.0f;
}

LanguageDetectionResult LanguageDetector::combineDetectionResults(
    const LanguageDetectionResult& textResult,
    const LanguageDetectionResult& audioResult) {
    
    LanguageDetectionResult combined;
    combined.detectionMethod = "hybrid";
    
    // If both methods agree and have good confidence, use that
    if (textResult.detectedLanguage == audioResult.detectedLanguage) {
        combined.detectedLanguage = textResult.detectedLanguage;
        combined.confidence = (textResult.confidence + audioResult.confidence) / 2.0f;
        combined.isReliable = combined.confidence >= confidenceThreshold_;
        
        // Merge candidates
        std::unordered_map<std::string, float> candidateMap;
        for (const auto& candidate : textResult.languageCandidates) {
            candidateMap[candidate.first] += candidate.second * 0.5f;
        }
        for (const auto& candidate : audioResult.languageCandidates) {
            candidateMap[candidate.first] += candidate.second * 0.5f;
        }
        
        for (const auto& pair : candidateMap) {
            combined.languageCandidates.emplace_back(pair.first, pair.second);
        }
        
        std::sort(combined.languageCandidates.begin(), combined.languageCandidates.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        
        return combined;
    }
    
    // If methods disagree, prefer the one with higher confidence
    if (audioResult.confidence > textResult.confidence) {
        combined = audioResult;
        combined.detectionMethod = "hybrid_audio_preferred";
    } else {
        combined = textResult;
        combined.detectionMethod = "hybrid_text_preferred";
    }
    
    return combined;
}

std::string LanguageDetector::normalizeText(const std::string& text) {
    std::string normalized;
    normalized.reserve(text.length());
    
    for (char c : text) {
        if (std::isalnum(c) || std::isspace(c)) {
            normalized += std::tolower(c);
        } else if (std::ispunct(c)) {
            normalized += ' '; // Replace punctuation with space
        }
    }
    
    return normalized;
}

std::vector<std::string> LanguageDetector::extractWords(const std::string& text) {
    std::vector<std::string> words;
    std::istringstream iss(text);
    std::string word;
    
    while (iss >> word) {
        // Remove punctuation
        word.erase(std::remove_if(word.begin(), word.end(), [](char c) {
            return !std::isalnum(c);
        }), word.end());
        
        if (!word.empty()) {
            std::transform(word.begin(), word.end(), word.begin(), ::tolower);
            words.push_back(word);
        }
    }
    
    return words;
}

std::vector<std::string> LanguageDetector::extractNgrams(const std::string& text, int n) {
    std::vector<std::string> ngrams;
    std::string normalized = normalizeText(text);
    
    if (static_cast<int>(normalized.length()) < n) {
        return ngrams;
    }
    
    for (size_t i = 0; i <= normalized.length() - n; ++i) {
        std::string ngram = normalized.substr(i, n);
        if (ngram.find(' ') == std::string::npos) { // Skip ngrams with spaces
            ngrams.push_back(ngram);
        }
    }
    
    return ngrams;
}

bool LanguageDetector::loadConfiguration(const std::string& configPath) {
    // For now, use default configuration
    // In a real implementation, this would load from JSON file
    
    // Set default supported languages
    if (supportedLanguages_.empty()) {
        supportedLanguages_ = {"en", "es", "fr", "de"};
    }
    
    // Set default fallback mappings
    fallbackLanguages_["pt"] = "es"; // Portuguese -> Spanish
    fallbackLanguages_["it"] = "es"; // Italian -> Spanish
    fallbackLanguages_["ca"] = "es"; // Catalan -> Spanish
    fallbackLanguages_["nl"] = "de"; // Dutch -> German
    fallbackLanguages_["da"] = "de"; // Danish -> German
    fallbackLanguages_["sv"] = "de"; // Swedish -> German
    
    return true;
}

void LanguageDetector::initializeLanguageModels() {
    languageModels_.clear();
    
    for (const std::string& lang : supportedLanguages_) {
        LanguageModel model;
        model.languageCode = lang;
        model.accuracy = 0.85f; // Default accuracy
        model.loaded = true;
        languageModels_[lang] = model;
    }
}

void LanguageDetector::loadCharacterFrequencies() {
    // Load typical character frequencies for each language
    // These are approximate frequencies based on typical text
    
    // English
    characterFrequencies_["en"] = {
        {'e', 0.127}, {'t', 0.091}, {'a', 0.082}, {'o', 0.075}, {'i', 0.070},
        {'n', 0.067}, {'s', 0.063}, {'h', 0.061}, {'r', 0.060}, {'d', 0.043},
        {'l', 0.040}, {'c', 0.028}, {'u', 0.028}, {'m', 0.024}, {'w', 0.024},
        {'f', 0.022}, {'g', 0.020}, {'y', 0.020}, {'p', 0.019}, {'b', 0.013},
        {'v', 0.010}, {'k', 0.008}, {'j', 0.001}, {'x', 0.001}, {'q', 0.001}, {'z', 0.001}
    };
    
    // Spanish
    characterFrequencies_["es"] = {
        {'e', 0.137}, {'a', 0.125}, {'o', 0.086}, {'s', 0.080}, {'r', 0.069},
        {'n', 0.067}, {'i', 0.063}, {'d', 0.058}, {'l', 0.050}, {'c', 0.047},
        {'t', 0.046}, {'u', 0.039}, {'m', 0.032}, {'p', 0.025}, {'b', 0.022},
        {'g', 0.018}, {'v', 0.011}, {'y', 0.009}, {'q', 0.009}, {'h', 0.007},
        {'f', 0.007}, {'z', 0.005}, {'j', 0.004}, {'ñ', 0.003}, {'x', 0.002}, {'k', 0.001}
    };
    
    // French
    characterFrequencies_["fr"] = {
        {'e', 0.147}, {'s', 0.081}, {'a', 0.076}, {'r', 0.066}, {'i', 0.066},
        {'t', 0.059}, {'n', 0.059}, {'u', 0.049}, {'l', 0.049}, {'o', 0.049},
        {'d', 0.042}, {'c', 0.030}, {'p', 0.030}, {'m', 0.027}, {'é', 0.027},
        {'v', 0.016}, {'q', 0.016}, {'f', 0.011}, {'b', 0.011}, {'g', 0.011},
        {'h', 0.011}, {'j', 0.005}, {'x', 0.004}, {'y', 0.003}, {'z', 0.001}, {'w', 0.001}
    };
    
    // German
    characterFrequencies_["de"] = {
        {'e', 0.174}, {'n', 0.098}, {'i', 0.076}, {'s', 0.072}, {'r', 0.070},
        {'a', 0.065}, {'t', 0.061}, {'d', 0.051}, {'h', 0.048}, {'u', 0.044},
        {'l', 0.034}, {'c', 0.031}, {'g', 0.030}, {'m', 0.025}, {'o', 0.025},
        {'b', 0.019}, {'w', 0.017}, {'f', 0.017}, {'k', 0.012}, {'z', 0.011},
        {'p', 0.008}, {'v', 0.008}, {'ü', 0.007}, {'ä', 0.006}, {'ö', 0.003}, {'ß', 0.003}
    };
}

void LanguageDetector::loadCommonWords() {
    // Load common words for each language
    
    commonWords_["en"] = {
        "the", "be", "to", "of", "and", "a", "in", "that", "have", "i",
        "it", "for", "not", "on", "with", "he", "as", "you", "do", "at",
        "this", "but", "his", "by", "from", "they", "we", "say", "her", "she",
        "or", "an", "will", "my", "one", "all", "would", "there", "their"
    };
    
    commonWords_["es"] = {
        "el", "la", "de", "que", "y", "a", "en", "un", "ser", "se",
        "no", "te", "lo", "le", "da", "su", "por", "son", "con", "para",
        "al", "una", "su", "del", "las", "los", "me", "ya", "muy", "mi",
        "sin", "sobre", "este", "año", "cuando", "él", "más", "estado", "si"
    };
    
    commonWords_["fr"] = {
        "le", "de", "et", "à", "un", "il", "être", "et", "en", "avoir",
        "que", "pour", "dans", "ce", "son", "une", "sur", "avec", "ne", "se",
        "pas", "tout", "plus", "par", "grand", "en", "me", "bien", "te", "si",
        "tout", "mais", "y", "ou", "son", "lui", "nous", "comme", "mais"
    };
    
    commonWords_["de"] = {
        "der", "die", "und", "in", "den", "von", "zu", "das", "mit", "sich",
        "des", "auf", "für", "ist", "im", "dem", "nicht", "ein", "eine", "als",
        "auch", "es", "an", "werden", "aus", "er", "hat", "dass", "sie", "nach",
        "wird", "bei", "einer", "um", "am", "sind", "noch", "wie", "einem"
    };
}

void LanguageDetector::loadNgramFrequencies() {
    // Load common n-gram frequencies for each language
    // These are simplified examples - in practice, these would be loaded from trained models
    
    // English bigrams and trigrams
    ngramFrequencies_["en"] = {
        {"th", 0.031}, {"he", 0.028}, {"in", 0.022}, {"er", 0.020}, {"an", 0.020},
        {"ed", 0.017}, {"nd", 0.017}, {"to", 0.017}, {"en", 0.016}, {"ti", 0.016},
        {"the", 0.035}, {"and", 0.018}, {"ing", 0.016}, {"ion", 0.013}, {"tio", 0.012}
    };
    
    // Spanish bigrams and trigrams
    ngramFrequencies_["es"] = {
        {"de", 0.045}, {"la", 0.035}, {"el", 0.030}, {"en", 0.025}, {"es", 0.022},
        {"ar", 0.020}, {"er", 0.018}, {"al", 0.017}, {"or", 0.016}, {"an", 0.015},
        {"que", 0.025}, {"ent", 0.018}, {"ion", 0.015}, {"con", 0.014}, {"ado", 0.012}
    };
    
    // French bigrams and trigrams
    ngramFrequencies_["fr"] = {
        {"le", 0.040}, {"de", 0.035}, {"es", 0.030}, {"en", 0.025}, {"re", 0.022},
        {"er", 0.020}, {"nt", 0.018}, {"on", 0.017}, {"te", 0.016}, {"et", 0.015},
        {"ent", 0.020}, {"les", 0.018}, {"ion", 0.015}, {"que", 0.014}, {"tion", 0.012}
    };
    
    // German bigrams and trigrams
    ngramFrequencies_["de"] = {
        {"en", 0.040}, {"er", 0.035}, {"ch", 0.030}, {"te", 0.025}, {"nd", 0.022},
        {"in", 0.020}, {"ei", 0.018}, {"ie", 0.017}, {"st", 0.016}, {"an", 0.015},
        {"der", 0.025}, {"und", 0.020}, {"ich", 0.018}, {"ein", 0.015}, {"den", 0.014}
    };
}

} // namespace mt
} // namespace speechrnt