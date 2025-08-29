#pragma once

#include "stt/stt_interface.hpp"
#include "stt/quantization_config.hpp"
#include "stt/stt_performance_tracker.hpp"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <queue>

#ifdef WHISPER_AVAILABLE
#include "whisper.h"
#else
// Forward declare whisper.cpp types when not available
struct whisper_context;
struct whisper_full_params;
#endif

namespace stt {

// Forward declaration for AudioBufferManager
namespace audio {
    class AudioBufferManager;
}

class WhisperSTT : public STTInterface {
public:
    // Type definitions
    using LanguageChangeCallback = std::function<void(const std::string& oldLang, const std::string& newLang, float confidence)>;
    using TranscriptionCompleteCallback = std::function<void(uint32_t utteranceId, const TranscriptionResult& result, const std::vector<TranscriptionResult>& candidates)>;
    
    WhisperSTT();
    ~WhisperSTT() override;
    
    // STTInterface implementation
    bool initialize(const std::string& modelPath, int n_threads = 4) override;
    bool initializeWithGPU(const std::string& modelPath, int gpuDeviceId = 0, int n_threads = 4);
    void transcribe(const std::vector<float>& audioData, TranscriptionCallback callback) override;
    void transcribeLive(const std::vector<float>& audioData, TranscriptionCallback callback) override;
    void setLanguage(const std::string& language) override;
    void setTranslateToEnglish(bool translate) override;
    void setTemperature(float temperature) override;
    void setMaxTokens(int max_tokens) override;
    bool isInitialized() const override { return initialized_; }
    std::string getLastError() const override { return last_error_; }
    
    // Language detection configuration
    void setLanguageDetectionEnabled(bool enabled) override;
    void setLanguageDetectionThreshold(float threshold) override;
    void setAutoLanguageSwitching(bool enabled) override;
    
    // Streaming transcription capabilities
    void startStreamingTranscription(uint32_t utteranceId);
    void addAudioChunk(uint32_t utteranceId, const std::vector<float>& audio);
    void finalizeStreamingTranscription(uint32_t utteranceId);
    
    // Streaming configuration
    void setStreamingCallback(uint32_t utteranceId, TranscriptionCallback callback);
    void setPartialResultsEnabled(bool enabled) { partialResultsEnabled_ = enabled; }
    void setMinChunkSizeMs(int chunkSizeMs) { minChunkSizeMs_ = chunkSizeMs; }
    void setConfidenceThreshold(float threshold) { confidenceThreshold_ = threshold; }
    
    // Confidence score configuration
    void setWordLevelConfidenceEnabled(bool enabled) { wordLevelConfidenceEnabled_ = enabled; }
    void setQualityIndicatorsEnabled(bool enabled) { qualityIndicatorsEnabled_ = enabled; }
    void setConfidenceFilteringEnabled(bool enabled) { confidenceFilteringEnabled_ = enabled; }
    float getConfidenceThreshold() const { return confidenceThreshold_; }
    bool isWordLevelConfidenceEnabled() const { return wordLevelConfidenceEnabled_; }
    bool isQualityIndicatorsEnabled() const { return qualityIndicatorsEnabled_; }
    bool isConfidenceFilteringEnabled() const { return confidenceFilteringEnabled_; }
    
    // Streaming status
    bool isStreamingActive(uint32_t utteranceId) const;
    size_t getActiveStreamingCount() const;
    
    // Language detection
    void setLanguageChangeCallback(LanguageChangeCallback callback);
    std::string getCurrentDetectedLanguage() const { return currentDetectedLanguage_; }
    bool isLanguageDetectionEnabled() const { return languageDetectionEnabled_; }
    bool isAutoLanguageSwitchingEnabled() const { return autoLanguageSwitching_; }
    
    // Translation pipeline integration
    void setTranscriptionCompleteCallback(TranscriptionCompleteCallback callback);
    void generateTranscriptionCandidates(const std::vector<float>& audioData, std::vector<TranscriptionResult>& candidates, int maxCandidates = 3);
    
    // Quantization support
    void setQuantizationLevel(QuantizationLevel level);
    QuantizationLevel getQuantizationLevel() const { return currentQuantizationLevel_; }
    bool initializeWithQuantization(const std::string& modelPath, QuantizationLevel level, int n_threads = 4);
    bool initializeWithQuantizationGPU(const std::string& modelPath, QuantizationLevel level, int gpuDeviceId = 0, int n_threads = 4);
    std::vector<QuantizationLevel> getSupportedQuantizationLevels() const;
    AccuracyValidationResult validateQuantizedModel(const std::vector<std::string>& validationAudioPaths, const std::vector<std::string>& expectedTranscriptions) const;
    
private:
    // Streaming state for each utterance
    struct StreamingState {
        uint32_t utteranceId;
        TranscriptionCallback callback;
        std::vector<float> accumulatedAudio;
        std::string lastTranscriptionText;
        bool isActive;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point lastProcessTime;
        size_t totalAudioSamples;
        size_t processedAudioSamples;
        
        StreamingState() 
            : utteranceId(0), isActive(false), totalAudioSamples(0), processedAudioSamples(0) {
            startTime = std::chrono::steady_clock::now();
            lastProcessTime = startTime;
        }
    };

    bool initialized_;
    std::string modelPath_;
    std::string language_;
    std::string last_error_;
    
    // Whisper.cpp context and parameters
    whisper_context* ctx_;
    whisper_full_params* params_;
    
    // Thread safety
    mutable std::mutex mutex_;
    mutable std::mutex streamingMutex_;
    
    // Configuration
    bool translate_to_english_;
    float temperature_;
    int max_tokens_;
    int n_threads_;
    
    // GPU configuration
    bool gpu_enabled_;
    int gpu_device_id_;
    
    // Streaming configuration
    bool partialResultsEnabled_;
    int minChunkSizeMs_;
    float confidenceThreshold_;
    
    // Confidence score configuration
    bool wordLevelConfidenceEnabled_;
    bool qualityIndicatorsEnabled_;
    bool confidenceFilteringEnabled_;
    
    // Language detection configuration
    bool languageDetectionEnabled_;
    float languageDetectionThreshold_;
    bool autoLanguageSwitching_;
    std::string currentDetectedLanguage_;
    
    // Language change callback
    LanguageChangeCallback languageChangeCallback_;
    
    // Translation pipeline integration
    TranscriptionCompleteCallback transcriptionCompleteCallback_;
    
    // Quantization support
    QuantizationLevel currentQuantizationLevel_;
    std::unique_ptr<QuantizationManager> quantizationManager_;
    std::unordered_map<QuantizationLevel, whisper_context*> quantizedContexts_;
    mutable std::mutex quantizationMutex_;
    
    // Streaming state management
    std::unordered_map<uint32_t, std::unique_ptr<StreamingState>> streamingStates_;
    std::unique_ptr<audio::AudioBufferManager> audioBufferManager_;
    
    // Performance tracking
    std::unique_ptr<STTPerformanceTracker> performanceTracker_;
    
    // Helper methods
    bool setupWhisperParams();
    bool validateModel();
    void processTranscriptionResult(TranscriptionCallback callback, bool is_partial = false);
    
    // Streaming helper methods
    bool initializeAudioBufferManager();
    void processStreamingAudio(uint32_t utteranceId, StreamingState& state);
    void sendPartialResult(uint32_t utteranceId, const StreamingState& state, const TranscriptionResult& result);
    void sendFinalResult(uint32_t utteranceId, const StreamingState& state, const TranscriptionResult& result);
    bool shouldProcessStreamingChunk(const StreamingState& state) const;
    std::vector<float> getStreamingAudioChunk(uint32_t utteranceId, const StreamingState& state);
    void cleanupStreamingState(uint32_t utteranceId);
    
    // Language detection helper methods
    std::string detectLanguageFromResult() const;
    float getLanguageDetectionConfidence(const std::string& language) const;
    bool shouldSwitchLanguage(const std::string& detectedLang, float confidence) const;
    void handleLanguageChange(const std::string& newLanguage, float confidence);
    void updateTranscriptionResultWithLanguage(TranscriptionResult& result) const;
    
    // Quantization helper methods
    bool loadQuantizedModel(const std::string& modelPath, QuantizationLevel level, bool useGPU = false, int gpuDeviceId = 0);
    void unloadQuantizedModel(QuantizationLevel level);
    whisper_context* getQuantizedContext(QuantizationLevel level) const;
    QuantizationLevel selectOptimalQuantizationLevel(const std::string& modelPath) const;
    bool validateQuantizationSupport(QuantizationLevel level) const;
    void cleanupQuantizedModels();
    
    // Confidence calculation helper methods
    float calculateSegmentConfidence(int segmentIndex) const;
    std::vector<WordTiming> extractWordTimings(int segmentIndex) const;
    TranscriptionQuality calculateQualityMetrics(const std::vector<float>& audioData, float processingLatencyMs) const;
    std::string determineQualityLevel(float confidence, const TranscriptionQuality& quality) const;
    bool meetsConfidenceThreshold(float confidence) const;
    void enhanceTranscriptionResultWithConfidence(TranscriptionResult& result, const std::vector<float>& audioData, float processingLatencyMs) const;
    
    // Streaming word timing integration
    void enhanceStreamingResultWithWordTimings(TranscriptionResult& result, const StreamingState& state) const;
    void validateWordTimingsConsistency(TranscriptionResult& result) const;
    float adjustWordConfidence(const std::string& word, float baseConfidence, int tokenCount) const;
    
    // Translation pipeline integration helper methods
    void triggerTranslationPipeline(uint32_t utteranceId, const TranscriptionResult& result, const std::vector<TranscriptionResult>& candidates);
    void generateMultipleCandidates(const std::vector<float>& audioData, std::vector<TranscriptionResult>& candidates, int maxCandidates);
};

} // namespace stt