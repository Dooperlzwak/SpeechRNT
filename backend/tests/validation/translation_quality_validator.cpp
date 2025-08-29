#include "translation_quality_validator.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <regex>
#include <unordered_set>

namespace validation {

TranslationQualityValidator::TranslationQualityValidator() {
    initializeMetrics();
    loadReferenceTranslations();
}

void TranslationQualityValidator::initializeMetrics() {
    // Initialize BLEU score calculator
    bleuCalculator_ = std::make_unique<BLEUCalculator>();
    
    // Initialize semantic similarity calculator
    semanticCalculator_ = std::make_unique<SemanticSimilarityCalculator>();
    
    // Initialize fluency evaluator
    fluencyEvaluator_ = std::make_unique<FluencyEvaluator>();
}

void TranslationQualityValidator::loadReferenceTranslations() {
    // Load reference translations for common phrases
    referenceTranslations_["en"]["es"] = {
        {"Hello, how are you?", "Hola, ¿cómo estás?"},
        {"What time is it?", "¿Qué hora es?"},
        {"I need help with directions.", "Necesito ayuda con las direcciones."},
        {"The weather is beautiful today.", "El clima está hermoso hoy."},
        {"Can you recommend a good restaurant?", "¿Puedes recomendar un buen restaurante?"},
        {"Thank you very much for your assistance.", "Muchas gracias por tu ayuda."},
        {"I'm sorry, I don't understand.", "Lo siento, no entiendo."},
        {"Where is the nearest hospital?", "¿Dónde está el hospital más cercano?"},
        {"How much does this cost?", "¿Cuánto cuesta esto?"},
        {"I would like to make a reservation.", "Me gustaría hacer una reserva."}
    };
    
    referenceTranslations_["en"]["fr"] = {
        {"Hello, how are you?", "Bonjour, comment allez-vous?"},
        {"What time is it?", "Quelle heure est-il?"},
        {"I need help with directions.", "J'ai besoin d'aide pour les directions."},
        {"The weather is beautiful today.", "Le temps est magnifique aujourd'hui."},
        {"Can you recommend a good restaurant?", "Pouvez-vous recommander un bon restaurant?"},
        {"Thank you very much for your assistance.", "Merci beaucoup pour votre aide."},
        {"I'm sorry, I don't understand.", "Je suis désolé, je ne comprends pas."},
        {"Where is the nearest hospital?", "Où est l'hôpital le plus proche?"},
        {"How much does this cost?", "Combien cela coûte-t-il?"},
        {"I would like to make a reservation.", "J'aimerais faire une réservation."}
    };
    
    referenceTranslations_["en"]["de"] = {
        {"Hello, how are you?", "Hallo, wie geht es Ihnen?"},
        {"What time is it?", "Wie spät ist es?"},
        {"I need help with directions.", "Ich brauche Hilfe bei der Wegbeschreibung."},
        {"The weather is beautiful today.", "Das Wetter ist heute wunderschön."},
        {"Can you recommend a good restaurant?", "Können Sie ein gutes Restaurant empfehlen?"},
        {"Thank you very much for your assistance.", "Vielen Dank für Ihre Hilfe."},
        {"I'm sorry, I don't understand.", "Es tut mir leid, ich verstehe nicht."},
        {"Where is the nearest hospital?", "Wo ist das nächste Krankenhaus?"},
        {"How much does this cost?", "Wie viel kostet das?"},
        {"I would like to make a reservation.", "Ich möchte gerne eine Reservierung machen."}
    };
}

TranslationQualityMetrics TranslationQualityValidator::evaluateTranslation(
    const std::string& sourceText,
    const std::string& translatedText,
    const std::string& sourceLang,
    const std::string& targetLang,
    const std::string& referenceTranslation
) {
    TranslationQualityMetrics metrics;
    
    // Calculate BLEU score if reference is available
    if (!referenceTranslation.empty()) {
        metrics.bleuScore = bleuCalculator_->calculateBLEU(translatedText, referenceTranslation);
    } else {
        // Try to find reference translation
        auto langIt = referenceTranslations_.find(sourceLang);
        if (langIt != referenceTranslations_.end()) {
            auto targetIt = langIt->second.find(targetLang);
            if (targetIt != langIt->second.end()) {
                auto refIt = targetIt->second.find(sourceText);
                if (refIt != targetIt->second.end()) {
                    metrics.bleuScore = bleuCalculator_->calculateBLEU(translatedText, refIt->second);
                }
            }
        }
    }
    
    // Calculate semantic similarity
    metrics.semanticSimilarity = semanticCalculator_->calculateSimilarity(
        sourceText, translatedText, sourceLang, targetLang
    );
    
    // Evaluate fluency
    metrics.fluencyScore = fluencyEvaluator_->evaluateFluency(translatedText, targetLang);
    
    // Calculate adequacy (content preservation)
    metrics.adequacyScore = calculateAdequacy(sourceText, translatedText, sourceLang, targetLang);
    
    // Detect potential errors
    metrics.errorTypes = detectTranslationErrors(sourceText, translatedText, sourceLang, targetLang);
    
    // Calculate overall quality score
    metrics.overallQuality = calculateOverallQuality(metrics);
    
    // Add metadata
    metrics.sourceLength = sourceText.length();
    metrics.targetLength = translatedText.length();
    metrics.lengthRatio = static_cast<double>(metrics.targetLength) / metrics.sourceLength;
    metrics.evaluationTimestamp = std::chrono::system_clock::now();
    
    return metrics;
}

double TranslationQualityValidator::calculateAdequacy(
    const std::string& sourceText,
    const std::string& translatedText,
    const std::string& sourceLang,
    const std::string& targetLang
) {
    // Simple adequacy calculation based on content preservation
    // In a real implementation, this would use more sophisticated NLP techniques
    
    // Extract key content words (nouns, verbs, adjectives)
    auto sourceWords = extractContentWords(sourceText, sourceLang);
    auto targetWords = extractContentWords(translatedText, targetLang);
    
    // Calculate content preservation ratio
    int preservedConcepts = 0;
    int totalConcepts = sourceWords.size();
    
    // This is a simplified approach - in practice, you'd use bilingual dictionaries
    // or cross-lingual embeddings to match concepts across languages
    for (const auto& sourceWord : sourceWords) {
        // Check if concept is preserved (simplified heuristic)
        if (isConceptPreserved(sourceWord, targetWords, sourceLang, targetLang)) {
            preservedConcepts++;
        }
    }
    
    return totalConcepts > 0 ? static_cast<double>(preservedConcepts) / totalConcepts : 0.0;
}

std::vector<std::string> TranslationQualityValidator::extractContentWords(
    const std::string& text,
    const std::string& language
) {
    std::vector<std::string> contentWords;
    
    // Simple tokenization and filtering
    std::regex wordRegex(R"(\b\w+\b)");
    std::sregex_iterator iter(text.begin(), text.end(), wordRegex);
    std::sregex_iterator end;
    
    // Common stop words for different languages
    std::unordered_set<std::string> stopWords;
    if (language == "en") {
        stopWords = {"the", "a", "an", "and", "or", "but", "in", "on", "at", "to", "for", "of", "with", "by", "is", "are", "was", "were", "be", "been", "have", "has", "had", "do", "does", "did", "will", "would", "could", "should", "may", "might", "can", "this", "that", "these", "those", "i", "you", "he", "she", "it", "we", "they", "me", "him", "her", "us", "them"};
    } else if (language == "es") {
        stopWords = {"el", "la", "los", "las", "un", "una", "y", "o", "pero", "en", "con", "por", "para", "de", "del", "al", "es", "son", "está", "están", "ser", "estar", "tener", "hacer", "ir", "venir", "ver", "dar", "saber", "poder", "querer", "decir", "este", "esta", "estos", "estas", "yo", "tú", "él", "ella", "nosotros", "vosotros", "ellos", "ellas", "me", "te", "le", "nos", "os", "les"};
    } else if (language == "fr") {
        stopWords = {"le", "la", "les", "un", "une", "et", "ou", "mais", "dans", "sur", "avec", "par", "pour", "de", "du", "des", "au", "aux", "est", "sont", "être", "avoir", "faire", "aller", "venir", "voir", "donner", "savoir", "pouvoir", "vouloir", "dire", "ce", "cette", "ces", "je", "tu", "il", "elle", "nous", "vous", "ils", "elles", "me", "te", "lui", "nous", "vous", "leur"};
    } else if (language == "de") {
        stopWords = {"der", "die", "das", "ein", "eine", "und", "oder", "aber", "in", "auf", "mit", "von", "zu", "für", "ist", "sind", "war", "waren", "sein", "haben", "werden", "können", "sollen", "wollen", "müssen", "dieser", "diese", "dieses", "ich", "du", "er", "sie", "es", "wir", "ihr", "sie", "mich", "dich", "ihn", "sie", "uns", "euch", "sie"};
    }
    
    for (; iter != end; ++iter) {
        std::string word = iter->str();
        std::transform(word.begin(), word.end(), word.begin(), ::tolower);
        
        if (word.length() > 2 && stopWords.find(word) == stopWords.end()) {
            contentWords.push_back(word);
        }
    }
    
    return contentWords;
}

bool TranslationQualityValidator::isConceptPreserved(
    const std::string& sourceWord,
    const std::vector<std::string>& targetWords,
    const std::string& sourceLang,
    const std::string& targetLang
) {
    // Simplified concept preservation check
    // In practice, this would use bilingual dictionaries or embeddings
    
    // Basic heuristics for common words
    if (sourceLang == "en" && targetLang == "es") {
        std::unordered_map<std::string, std::string> translations = {
            {"hello", "hola"}, {"time", "tiempo"}, {"help", "ayuda"},
            {"weather", "clima"}, {"restaurant", "restaurante"},
            {"thank", "gracias"}, {"sorry", "siento"}, {"hospital", "hospital"},
            {"cost", "cuesta"}, {"reservation", "reserva"}
        };
        
        auto it = translations.find(sourceWord);
        if (it != translations.end()) {
            return std::find(targetWords.begin(), targetWords.end(), it->second) != targetWords.end();
        }
    }
    
    // Fallback: check for similar words (cognates)
    for (const auto& targetWord : targetWords) {
        if (calculateStringSimilarity(sourceWord, targetWord) > 0.7) {
            return true;
        }
    }
    
    return false;
}

double TranslationQualityValidator::calculateStringSimilarity(
    const std::string& str1,
    const std::string& str2
) {
    // Levenshtein distance-based similarity
    int len1 = str1.length();
    int len2 = str2.length();
    
    if (len1 == 0) return len2 == 0 ? 1.0 : 0.0;
    if (len2 == 0) return 0.0;
    
    std::vector<std::vector<int>> dp(len1 + 1, std::vector<int>(len2 + 1));
    
    for (int i = 0; i <= len1; i++) dp[i][0] = i;
    for (int j = 0; j <= len2; j++) dp[0][j] = j;
    
    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (str1[i-1] == str2[j-1]) ? 0 : 1;
            dp[i][j] = std::min({
                dp[i-1][j] + 1,      // deletion
                dp[i][j-1] + 1,      // insertion
                dp[i-1][j-1] + cost  // substitution
            });
        }
    }
    
    int maxLen = std::max(len1, len2);
    return 1.0 - static_cast<double>(dp[len1][len2]) / maxLen;
}

std::vector<TranslationError> TranslationQualityValidator::detectTranslationErrors(
    const std::string& sourceText,
    const std::string& translatedText,
    const std::string& sourceLang,
    const std::string& targetLang
) {
    std::vector<TranslationError> errors;
    
    // Check for untranslated text
    if (containsUntranslatedText(translatedText, sourceLang, targetLang)) {
        errors.push_back({
            TranslationErrorType::UNTRANSLATED_TEXT,
            "Text contains untranslated segments",
            0.8
        });
    }
    
    // Check for over-translation (excessive length)
    double lengthRatio = static_cast<double>(translatedText.length()) / sourceText.length();
    if (lengthRatio > 2.5) {
        errors.push_back({
            TranslationErrorType::OVER_TRANSLATION,
            "Translation is excessively long compared to source",
            0.6
        });
    }
    
    // Check for under-translation (too short)
    if (lengthRatio < 0.3) {
        errors.push_back({
            TranslationErrorType::UNDER_TRANSLATION,
            "Translation is too short, likely missing content",
            0.7
        });
    }
    
    // Check for repetition
    if (containsRepetition(translatedText)) {
        errors.push_back({
            TranslationErrorType::REPETITION,
            "Translation contains repetitive text",
            0.5
        });
    }
    
    // Check for grammatical issues (simplified)
    auto grammarErrors = detectGrammarIssues(translatedText, targetLang);
    errors.insert(errors.end(), grammarErrors.begin(), grammarErrors.end());
    
    return errors;
}

bool TranslationQualityValidator::containsUntranslatedText(
    const std::string& translatedText,
    const std::string& sourceLang,
    const std::string& targetLang
) {
    // Simple heuristic: check for English words in non-English translations
    if (targetLang != "en") {
        std::regex englishWordRegex(R"(\b[a-zA-Z]{3,}\b)");
        std::sregex_iterator iter(translatedText.begin(), translatedText.end(), englishWordRegex);
        std::sregex_iterator end;
        
        int englishWordCount = 0;
        int totalWords = 0;
        
        for (; iter != end; ++iter) {
            totalWords++;
            std::string word = iter->str();
            std::transform(word.begin(), word.end(), word.begin(), ::tolower);
            
            // Check if it's a common English word
            std::unordered_set<std::string> commonEnglishWords = {
                "the", "and", "for", "are", "but", "not", "you", "all", "can", "had", "her", "was", "one", "our", "out", "day", "get", "has", "him", "his", "how", "man", "new", "now", "old", "see", "two", "way", "who", "boy", "did", "its", "let", "put", "say", "she", "too", "use"
            };
            
            if (commonEnglishWords.find(word) != commonEnglishWords.end()) {
                englishWordCount++;
            }
        }
        
        return totalWords > 0 && (static_cast<double>(englishWordCount) / totalWords) > 0.3;
    }
    
    return false;
}

bool TranslationQualityValidator::containsRepetition(const std::string& text) {
    // Check for repeated phrases
    std::vector<std::string> words;
    std::regex wordRegex(R"(\b\w+\b)");
    std::sregex_iterator iter(text.begin(), text.end(), wordRegex);
    std::sregex_iterator end;
    
    for (; iter != end; ++iter) {
        words.push_back(iter->str());
    }
    
    // Check for repeated sequences of 3+ words
    for (size_t i = 0; i < words.size() - 5; i++) {
        for (size_t j = i + 3; j < words.size() - 2; j++) {
            bool match = true;
            for (size_t k = 0; k < 3 && i + k < words.size() && j + k < words.size(); k++) {
                if (words[i + k] != words[j + k]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
    }
    
    return false;
}

std::vector<TranslationError> TranslationQualityValidator::detectGrammarIssues(
    const std::string& text,
    const std::string& language
) {
    std::vector<TranslationError> errors;
    
    // Simple grammar checks (in practice, you'd use a proper grammar checker)
    
    // Check for missing punctuation at the end
    if (!text.empty() && text.back() != '.' && text.back() != '!' && text.back() != '?') {
        errors.push_back({
            TranslationErrorType::GRAMMAR_ERROR,
            "Missing punctuation at the end of sentence",
            0.3
        });
    }
    
    // Check for double spaces
    if (text.find("  ") != std::string::npos) {
        errors.push_back({
            TranslationErrorType::FORMATTING_ERROR,
            "Multiple consecutive spaces found",
            0.2
        });
    }
    
    // Check for capitalization issues
    if (!text.empty() && std::islower(text[0])) {
        errors.push_back({
            TranslationErrorType::GRAMMAR_ERROR,
            "Sentence should start with capital letter",
            0.3
        });
    }
    
    return errors;
}

double TranslationQualityValidator::calculateOverallQuality(
    const TranslationQualityMetrics& metrics
) {
    double quality = 0.0;
    int components = 0;
    
    // Weight different metrics
    if (metrics.bleuScore >= 0) {
        quality += metrics.bleuScore * 0.3;
        components++;
    }
    
    if (metrics.semanticSimilarity >= 0) {
        quality += metrics.semanticSimilarity * 0.25;
        components++;
    }
    
    if (metrics.fluencyScore >= 0) {
        quality += metrics.fluencyScore * 0.25;
        components++;
    }
    
    if (metrics.adequacyScore >= 0) {
        quality += metrics.adequacyScore * 0.2;
        components++;
    }
    
    if (components > 0) {
        quality /= components;
    }
    
    // Apply error penalties
    for (const auto& error : metrics.errorTypes) {
        quality -= error.severity * 0.1;
    }
    
    return std::max(0.0, std::min(1.0, quality));
}

ValidationReport TranslationQualityValidator::generateValidationReport(
    const std::vector<TranslationQualityMetrics>& evaluations
) {
    ValidationReport report;
    report.totalEvaluations = evaluations.size();
    report.timestamp = std::chrono::system_clock::now();
    
    if (evaluations.empty()) {
        return report;
    }
    
    // Calculate aggregate statistics
    double totalQuality = 0.0;
    double totalBLEU = 0.0;
    double totalSemantic = 0.0;
    double totalFluency = 0.0;
    double totalAdequacy = 0.0;
    
    int bleuCount = 0;
    int semanticCount = 0;
    int fluencyCount = 0;
    int adequacyCount = 0;
    
    std::map<TranslationErrorType, int> errorCounts;
    
    for (const auto& eval : evaluations) {
        totalQuality += eval.overallQuality;
        
        if (eval.bleuScore >= 0) {
            totalBLEU += eval.bleuScore;
            bleuCount++;
        }
        
        if (eval.semanticSimilarity >= 0) {
            totalSemantic += eval.semanticSimilarity;
            semanticCount++;
        }
        
        if (eval.fluencyScore >= 0) {
            totalFluency += eval.fluencyScore;
            fluencyCount++;
        }
        
        if (eval.adequacyScore >= 0) {
            totalAdequacy += eval.adequacyScore;
            adequacyCount++;
        }
        
        for (const auto& error : eval.errorTypes) {
            errorCounts[error.type]++;
        }
    }
    
    report.averageQuality = totalQuality / evaluations.size();
    report.averageBLEU = bleuCount > 0 ? totalBLEU / bleuCount : -1.0;
    report.averageSemanticSimilarity = semanticCount > 0 ? totalSemantic / semanticCount : -1.0;
    report.averageFluency = fluencyCount > 0 ? totalFluency / fluencyCount : -1.0;
    report.averageAdequacy = adequacyCount > 0 ? totalAdequacy / adequacyCount : -1.0;
    
    // Quality distribution
    int excellent = 0, good = 0, fair = 0, poor = 0;
    for (const auto& eval : evaluations) {
        if (eval.overallQuality >= 0.8) excellent++;
        else if (eval.overallQuality >= 0.6) good++;
        else if (eval.overallQuality >= 0.4) fair++;
        else poor++;
    }
    
    report.qualityDistribution = {
        {"excellent", excellent},
        {"good", good},
        {"fair", fair},
        {"poor", poor}
    };
    
    // Error analysis
    for (const auto& errorPair : errorCounts) {
        report.errorAnalysis[errorPair.first] = {
            errorPair.second,
            static_cast<double>(errorPair.second) / evaluations.size()
        };
    }
    
    // Recommendations
    if (report.averageQuality < 0.6) {
        report.recommendations.push_back("Overall translation quality is below acceptable threshold. Consider model fine-tuning or alternative translation engines.");
    }
    
    if (report.averageFluency < 0.7) {
        report.recommendations.push_back("Fluency scores are low. Consider post-processing for grammar and style improvement.");
    }
    
    if (errorCounts[TranslationErrorType::UNTRANSLATED_TEXT] > evaluations.size() * 0.1) {
        report.recommendations.push_back("High rate of untranslated text detected. Check model coverage for input languages.");
    }
    
    return report;
}

} // namespace validation