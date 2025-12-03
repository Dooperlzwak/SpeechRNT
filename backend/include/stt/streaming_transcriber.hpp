#pragma once

#include "core/message_protocol.hpp"
#include "stt/stt_config.hpp"
#include "stt/stt_performance_tracker.hpp"
#include "stt/transcription_manager.hpp"
#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace stt {

// Callback for sending messages to clients
using MessageSender = std::function<void(const std::string &message)>;

} // namespace stt

// Forward declaration for translation pipeline integration
namespace core {
class TranslationPipeline;
}

namespace stt {

// Manages streaming transcription with incremental updates
class StreamingTranscriber {
public:
  StreamingTranscriber();
  ~StreamingTranscriber();

  // Initialize with transcription manager and message sender
  bool initialize(std::shared_ptr<TranscriptionManager> transcriptionManager,
                  MessageSender messageSender);

  /**
   * Set translation pipeline for real-time translation
   * @param pipeline Translation pipeline
   */
  void
  setTranslationPipeline(std::shared_ptr<::core::TranslationPipeline> pipeline);

  // Start streaming transcription for an utterance
  void startTranscription(uint32_t utteranceId,
                          const std::vector<float> &audioData,
                          bool isLive = true);

  // Add more audio data to an ongoing transcription
  void addAudioData(uint32_t utteranceId, const std::vector<float> &audioData);

  // Finalize transcription for an utterance
  void finalizeTranscription(uint32_t utteranceId);

  // Cancel transcription for an utterance
  void cancelTranscription(uint32_t utteranceId);

  // Configuration
  void setMinUpdateInterval(int intervalMs) {
    minUpdateIntervalMs_ = intervalMs;
  }
  void setMinTextLength(size_t length) { minTextLength_ = length; }
  void setTextSimilarityThreshold(float threshold) {
    textSimilarityThreshold_ = threshold;
  }
  void setIncrementalUpdatesEnabled(bool enabled) {
    incrementalUpdatesEnabled_ = enabled;
  }
  void setMaxUpdateFrequency(int maxUpdatesPerSecond) {
    maxUpdateFrequency_ = maxUpdatesPerSecond;
  }

  // Status
  bool isTranscribing(uint32_t utteranceId) const;
  size_t getActiveTranscriptions() const;

  // Normalization configuration
  void setNormalizationConfig(
      const stt::STTConfig::TextNormalizationConfig &config) {
    normalizationConfig_ = config;
  }

private:
  struct TranscriptionState {
    std::string currentText;
    std::string lastSentText;
    std::vector<std::string>
        textHistory; // For incremental difference detection
    double confidence;
    int64_t startTimeMs;
    int64_t lastUpdateTimeMs;
    int updateCount; // Track number of updates sent
    bool isActive;
    bool isFinalized;
  };

  std::shared_ptr<TranscriptionManager> transcriptionManager_;
  MessageSender messageSender_;

  // Translation pipeline integration
  std::shared_ptr<::core::TranslationPipeline> translationPipeline_;
  std::string sessionId_; // Session ID for translation pipeline

  // State management
  mutable std::mutex stateMutex_;
  std::unordered_map<uint32_t, TranscriptionState> transcriptionStates_;

  // Configuration
  int minUpdateIntervalMs_;
  size_t minTextLength_;
  float textSimilarityThreshold_;  // Threshold for text similarity (0.0-1.0)
  bool incrementalUpdatesEnabled_; // Enable/disable incremental updates
  int maxUpdateFrequency_;         // Maximum updates per second

  // Performance tracking
  std::shared_ptr<STTPerformanceTracker> performanceTracker_;

  // Normalization
  stt::STTConfig::TextNormalizationConfig normalizationConfig_;

  // Internal methods
  void handleTranscriptionResult(uint32_t utteranceId,
                                 const TranscriptionResult &result);
  void sendTranscriptionUpdate(uint32_t utteranceId,
                               const TranscriptionState &state, bool isPartial);
  bool shouldSendUpdate(const TranscriptionState &state, bool isPartial) const;
  int64_t getCurrentTimeMs() const;

  // Text analysis methods
  float calculateTextSimilarity(const std::string &text1,
                                const std::string &text2) const;
  std::string detectIncrementalChanges(const std::string &oldText,
                                       const std::string &newText) const;
  bool isSignificantTextChange(const std::string &oldText,
                               const std::string &newText) const;
  void updateTextHistory(TranscriptionState &state, const std::string &newText);
};

} // namespace stt