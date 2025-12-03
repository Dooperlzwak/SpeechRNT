#pragma once

#include "core/language_change_message.hpp"
#include "core/translation_pipeline.hpp"
#include "core/websocket_server.hpp"
#include <memory>
#include <string>

namespace speechrnt {
namespace core {

/**
 * Integration component that connects TranslationPipeline with WebSocket server
 * for language change notifications and other pipeline events
 */
class PipelineWebSocketIntegration {
public:
  PipelineWebSocketIntegration(
      std::shared_ptr<TranslationPipeline> pipeline,
      std::shared_ptr<::core::WebSocketServer> websocket_server);

  ~PipelineWebSocketIntegration();

  /**
   * Initialize the integration and set up callbacks
   */
  bool initialize();

  /**
   * Shutdown the integration
   */
  void shutdown();

  /**
   * Check if integration is active
   */
  bool isActive() const { return active_; }

private:
  std::shared_ptr<TranslationPipeline> pipeline_;
  std::shared_ptr<::core::WebSocketServer> websocket_server_;
  bool active_;

  // Pipeline event handlers
  void handleLanguageChange(const std::string &session_id,
                            const std::string &old_lang,
                            const std::string &new_lang, float confidence);

  void handleLanguageDetectionComplete(const PipelineResult &result);

  void handleTranslationComplete(const PipelineResult &result);

  void handlePipelineError(const PipelineResult &result,
                           const std::string &error);

  // Message sending helpers
  void sendLanguageChangeNotification(const std::string &session_id,
                                      const std::string &old_lang,
                                      const std::string &new_lang,
                                      float confidence);

  void sendLanguageDetectionResult(const std::string &session_id,
                                   const mt::LanguageDetectionResult &result);
};

} // namespace core
} // namespace speechrnt