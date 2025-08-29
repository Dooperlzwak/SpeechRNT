#include "stt/advanced/contextual_transcriber.hpp"
#include "stt/stt_interface.hpp"
#include <iostream>
#include <vector>
#include <string>

using namespace stt::advanced;

int main() {
    std::cout << "Contextual Transcriber Example\n";
    std::cout << "==============================\n\n";
    
    // Create and initialize contextual transcriber
    auto transcriber = createContextualTranscriber();
    if (!transcriber->initialize("models/contextual")) {
        std::cerr << "Failed to initialize contextual transcriber: " 
                  << transcriber->getLastError() << std::endl;
        return 1;
    }
    
    std::cout << "✓ Contextual transcriber initialized successfully\n\n";
    
    // Add custom medical vocabulary
    std::vector<std::string> medicalTerms = {
        "myocardial", "infarction", "cardiovascular", "hypertension",
        "diabetes", "pneumonia", "bronchitis", "arthritis"
    };
    
    if (transcriber->addCustomVocabulary(medicalTerms, "medical")) {
        std::cout << "✓ Added " << medicalTerms.size() << " medical terms to vocabulary\n";
    }
    
    // Add custom technical vocabulary
    std::vector<std::string> technicalTerms = {
        "JavaScript", "Python", "microservices", "containerization",
        "API", "REST", "JSON", "HTTP", "HTTPS"
    };
    
    if (transcriber->addCustomVocabulary(technicalTerms, "technical")) {
        std::cout << "✓ Added " << technicalTerms.size() << " technical terms to vocabulary\n";
    }
    
    std::cout << "\n";
    
    // Demonstrate conversation context management
    uint32_t utteranceId = 1;
    
    // First utterance - establish medical context
    std::string firstUtterance = "The patient is experiencing chest pain and shortness of breath";
    transcriber->updateConversationContext(utteranceId, firstUtterance, "doctor");
    transcriber->setDomainHint(utteranceId, "medical");
    
    std::cout << "Conversation Context:\n";
    std::cout << "Utterance " << utteranceId << ": \"" << firstUtterance << "\"\n";
    std::cout << "Speaker: doctor, Domain: medical\n\n";
    
    // Simulate transcription with errors
    TranscriptionResult baseResult;
    baseResult.text = "Patient has acute myocardial infraction and needs treatment";  // "infraction" should be "infarction"
    baseResult.confidence = 0.85f;
    baseResult.utteranceId = utteranceId;
    
    std::cout << "Base Transcription: \"" << baseResult.text << "\"\n";
    std::cout << "Confidence: " << baseResult.confidence << "\n\n";
    
    // Enhance transcription with contextual information
    auto context = transcriber->getConversationContext(utteranceId);
    auto enhancedResult = transcriber->enhanceTranscription(baseResult, context);
    
    std::cout << "Enhanced Transcription Results:\n";
    std::cout << "Enhanced Text: \"" << enhancedResult.enhancedText << "\"\n";
    std::cout << "Detected Domain: " << enhancedResult.detectedDomain << "\n";
    std::cout << "Context Used: " << (enhancedResult.contextUsed ? "Yes" : "No") << "\n";
    std::cout << "Contextual Confidence: " << enhancedResult.contextualConfidence << "\n";
    
    if (!enhancedResult.corrections.empty()) {
        std::cout << "\nCorrections Applied:\n";
        for (const auto& correction : enhancedResult.corrections) {
            std::cout << "  - \"" << correction.originalText << "\" → \"" 
                      << correction.correctedText << "\" (" << correction.correctionType 
                      << ", confidence: " << correction.confidence << ")\n";
            std::cout << "    Reasoning: " << correction.reasoning << "\n";
        }
    }
    
    if (!enhancedResult.alternativeTranscriptions.empty()) {
        std::cout << "\nAlternative Transcriptions:\n";
        for (size_t i = 0; i < enhancedResult.alternativeTranscriptions.size(); ++i) {
            std::cout << "  " << (i + 1) << ". \"" 
                      << enhancedResult.alternativeTranscriptions[i] << "\"\n";
        }
    }
    
    std::cout << "\n";
    
    // Demonstrate vocabulary search
    std::cout << "Vocabulary Search Example:\n";
    auto searchResults = transcriber->searchVocabulary("cardio", "medical", 5);
    if (!searchResults.empty()) {
        std::cout << "Search results for 'cardio' in medical domain:\n";
        for (const auto& entry : searchResults) {
            std::cout << "  - " << entry.term << " (confidence: " << entry.confidence 
                      << ", category: " << entry.category << ")\n";
        }
    } else {
        std::cout << "No results found for 'cardio' in medical domain\n";
    }
    
    std::cout << "\n";
    
    // Show vocabulary statistics
    auto stats = transcriber->getVocabularyStatistics();
    std::cout << "Vocabulary Statistics:\n";
    std::cout << "Total Entries: " << stats.totalEntries << "\n";
    std::cout << "Average Confidence: " << stats.averageConfidence << "\n";
    std::cout << "Total Usage Count: " << stats.totalUsageCount << "\n";
    
    // Show domain-specific statistics
    auto medicalStats = transcriber->getVocabularyStatistics("medical");
    std::cout << "Medical Domain Entries: " << medicalStats.totalEntries << "\n";
    
    auto technicalStats = transcriber->getVocabularyStatistics("technical");
    std::cout << "Technical Domain Entries: " << technicalStats.totalEntries << "\n";
    
    std::cout << "\n";
    
    // Demonstrate vocabulary export
    std::cout << "Vocabulary Export Example:\n";
    std::string exportedJson = transcriber->exportVocabulary("medical", "json");
    if (!exportedJson.empty()) {
        std::cout << "Exported medical vocabulary (JSON format):\n";
        std::cout << exportedJson.substr(0, 200) << "...\n";  // Show first 200 characters
    }
    
    std::cout << "\n";
    
    // Show processing statistics
    std::cout << "Processing Statistics:\n";
    std::string processingStats = transcriber->getProcessingStats();
    std::cout << processingStats << "\n";
    
    std::cout << "\n✓ Contextual transcriber example completed successfully!\n";
    
    return 0;
}