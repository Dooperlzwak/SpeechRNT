#include "core/pipeline_websocket_integration.hpp"
#include "utils/logging.hpp"
#include <sstream>
#include <iomanip>

namespace speechrnt {
namespace core {

PipelineWebSocketIntegration::PipelineWebSocketIntegration(
    std::shared_ptr<TranslationPipeline> pipeline,
    std::shared_ptr<WebSocketServer> websocket_server
) : pipeline_(pipeline)
  , websocket_server_(websocket_server)
  , active_(false) {
}

PipelineWebSocketIntegration::~PipelineWebSocketIntegration() {
    shutdown();
}

bool PipelineWebSocketIntegration::initialize() {
    if (active_) {
        speechrnt::utils::Logger::warn("PipelineWebSocketIntegration already initialized");
        return true;
    }
    
    if (!pipeline_ || !websocket_server_) {
        speechrnt::utils::Logger::error("PipelineWebSocketIntegration initialization failed: null pipeline or websocket server");
        return false;
    }
    
    // Set up pipeline callbacks
    pipeline_->setLanguageChangeCallback(
        [this](const std::string& session_id, const std::string& old_lang, const std::string& new_lang, float confidence) {
            handleLanguageChange(session_id, old_lang, new_lang, confidence);
        }
    );
    
    pipeline_->setLanguageDetectionCompleteCallback(
        [this](const PipelineResult& result) {
            handleLanguageDetectionComplete(result);
        }
    );
    
    pipeline_->setTranslationCompleteCallback(
        [this](const PipelineResult& result) {
            handleTranslationComplete(result);
        }
    );
    
    pipeline_->setPipelineErrorCallback(
        [this](const PipelineResult& result, const std::string& error) {
            handlePipelineError(result, error);
        }
    );
    
    active_ = true;
    speechrnt::utils::Logger::info("PipelineWebSocketIntegration initialized successfully");
    return true;
}

void PipelineWebSocketIntegration::shutdown() {
    if (!active_) {
        return;
    }
    
    // Clear callbacks to prevent further notifications
    if (pipeline_) {
        pipeline_->setLanguageChangeCallback(nullptr);
        pipeline_->setLanguageDetectionCompleteCallback(nullptr);
        pipeline_->setTranslationCompleteCallback(nullptr);
        pipeline_->setPipelineErrorCallback(nullptr);
    }
    
    active_ = false;
    speechrnt::utils::Logger::info("PipelineWebSocketIntegration shutdown completed");
}

void PipelineWebSocketIntegration::handleLanguageChange(
    const std::string& session_id,
    const std::string& old_lang,
    const std::string& new_lang,
    float confidence
) {
    if (!active_ || !websocket_server_) {
        return;
    }
    
    try {
        sendLanguageChangeNotification(session_id, old_lang, new_lang, confidence);
        speechrnt::utils::Logger::info("Language change notification sent for session " + session_id + 
                           ": " + old_lang + " -> " + new_lang + 
                           " (confidence: " + std::to_string(confidence) + ")");
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to send language change notification: " + std::string(e.what()));
    }
}

void PipelineWebSocketIntegration::handleLanguageDetectionComplete(const PipelineResult& result) {
    if (!active_ || !websocket_server_) {
        return;
    }
    
    try {
        sendLanguageDetectionResult(result.session_id, result.language_detection);
        speechrnt::utils::Logger::debug("Language detection result sent for session " + result.session_id);
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to send language detection result: " + std::string(e.what()));
    }
}

void PipelineWebSocketIntegration::handleTranslationComplete(const PipelineResult& result) {
    if (!active_ || !websocket_server_) {
        return;
    }
    
    // Send translation result with language information if language changed
    if (result.language_changed) {
        try {
            std::ostringstream json;
            json << "{\n";
            json << "  \"type\": \"translation_complete\",\n";
            json << "  \"utterance_id\": " << result.utterance_id << ",\n";
            json << "  \"session_id\": \"" << result.session_id << "\",\n";
            json << "  \"translated_text\": \"" << result.translation.translatedText << "\",\n";
            json << "  \"confidence\": " << std::fixed << std::setprecision(3) << result.translation.confidence << ",\n";
            json << "  \"source_language\": \"" << result.translation.sourceLang << "\",\n";
            json << "  \"target_language\": \"" << result.translation.targetLang << "\",\n";
            json << "  \"language_changed\": true,\n";
            json << "  \"previous_language\": \"" << result.previous_language << "\",\n";
            json << "  \"detected_language\": \"" << result.language_detection.detectedLanguage << "\",\n";
            json << "  \"language_confidence\": " << std::fixed << std::setprecision(3) << result.language_detection.confidence << "\n";
            json << "}";
            
            websocket_server_->sendMessage(result.session_id, json.str());
            speechrnt::utils::Logger::debug("Translation complete with language change sent for session " + result.session_id);
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Failed to send translation complete with language change: " + std::string(e.what()));
        }
    }
}

void PipelineWebSocketIntegration::handlePipelineError(const PipelineResult& result, const std::string& error) {
    if (!active_ || !websocket_server_) {
        return;
    }
    
    try {
        std::ostringstream json;
        json << "{\n";
        json << "  \"type\": \"pipeline_error\",\n";
        json << "  \"utterance_id\": " << result.utterance_id << ",\n";
        json << "  \"session_id\": \"" << result.session_id << "\",\n";
        json << "  \"stage\": \"" << result.pipeline_stage << "\",\n";
        json << "  \"error_message\": \"" << error << "\",\n";
        json << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count() << "\n";
        json << "}";
        
        websocket_server_->sendMessage(result.session_id, json.str());
        speechrnt::utils::Logger::debug("Pipeline error notification sent for session " + result.session_id);
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to send pipeline error notification: " + std::string(e.what()));
    }
}

void PipelineWebSocketIntegration::sendLanguageChangeNotification(
    const std::string& session_id,
    const std::string& old_lang,
    const std::string& new_lang,
    float confidence
) {
    LanguageChangeMessage message(session_id, old_lang, new_lang, confidence);
    std::string json = message.toJson();
    websocket_server_->sendMessage(session_id, json);
}

void PipelineWebSocketIntegration::sendLanguageDetectionResult(
    const std::string& session_id,
    const mt::LanguageDetectionResult& result
) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"type\": \"language_detection_result\",\n";
    json << "  \"session_id\": \"" << session_id << "\",\n";
    json << "  \"detected_language\": \"" << result.detectedLanguage << "\",\n";
    json << "  \"confidence\": " << std::fixed << std::setprecision(3) << result.confidence << ",\n";
    json << "  \"is_reliable\": " << (result.isReliable ? "true" : "false") << ",\n";
    json << "  \"detection_method\": \"" << result.detectionMethod << "\",\n";
    json << "  \"candidates\": [\n";
    
    for (size_t i = 0; i < result.languageCandidates.size(); ++i) {
        const auto& candidate = result.languageCandidates[i];
        json << "    {\n";
        json << "      \"language\": \"" << candidate.first << "\",\n";
        json << "      \"confidence\": " << std::fixed << std::setprecision(3) << candidate.second << "\n";
        json << "    }";
        if (i < result.languageCandidates.size() - 1) {
            json << ",";
        }
        json << "\n";
    }
    
    json << "  ],\n";
    json << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count() << "\n";
    json << "}";
    
    websocket_server_->sendMessage(session_id, json.str());
}

} // namespace core
} // namespace speechrnt