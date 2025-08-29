#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <chrono>

namespace speechrnt {
namespace mt {

// Forward declaration
struct QualityMetrics;

/**
 * Translation result containing the translated text and metadata
 */
struct TranslationResult {
    std::string translatedText;
    float confidence;
    std::string sourceLang;
    std::string targetLang;
    bool success;
    std::string errorMessage;
    
    // Enhanced quality assessment fields
    std::unique_ptr<QualityMetrics> qualityMetrics;
    std::vector<std::string> alternativeTranslations;
    std::chrono::milliseconds processingTime;
    bool usedGPUAcceleration;
    std::string modelVersion;
    std::vector<float> wordLevelConfidences;
    
    // Batch processing support
    int batchIndex;
    std::string sessionId;
    
    // Streaming support
    bool isPartialResult;
    bool isStreamingComplete;
    
    TranslationResult() 
        : confidence(0.0f)
        , success(false)
        , processingTime(0)
        , usedGPUAcceleration(false)
        , batchIndex(-1)
        , isPartialResult(false)
        , isStreamingComplete(false) {}
};

/**
 * Abstract interface for translation engines
 */
class TranslationInterface {
public:
    virtual ~TranslationInterface() = default;
    
    /**
     * Initialize the translation engine with language pair
     * @param sourceLang Source language code (e.g., "en")
     * @param targetLang Target language code (e.g., "es")
     * @return true if initialization successful
     */
    virtual bool initialize(const std::string& sourceLang, const std::string& targetLang) = 0;
    
    /**
     * Translate text synchronously
     * @param text Text to translate
     * @return Translation result
     */
    virtual TranslationResult translate(const std::string& text) = 0;
    
    /**
     * Translate text asynchronously
     * @param text Text to translate
     * @return Future containing translation result
     */
    virtual std::future<TranslationResult> translateAsync(const std::string& text) = 0;
    
    /**
     * Check if the engine supports the given language pair
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return true if supported
     */
    virtual bool supportsLanguagePair(const std::string& sourceLang, const std::string& targetLang) const = 0;
    
    /**
     * Get list of supported source languages
     * @return Vector of language codes
     */
    virtual std::vector<std::string> getSupportedSourceLanguages() const = 0;
    
    /**
     * Get list of supported target languages for a given source language
     * @param sourceLang Source language code
     * @return Vector of target language codes
     */
    virtual std::vector<std::string> getSupportedTargetLanguages(const std::string& sourceLang) const = 0;
    
    /**
     * Check if the engine is ready for translation
     * @return true if ready
     */
    virtual bool isReady() const = 0;
    
    /**
     * Clean up resources
     */
    virtual void cleanup() = 0;
    
    // Batch translation methods
    
    /**
     * Translate multiple texts in batch mode
     * @param texts Vector of texts to translate
     * @return Vector of translation results
     */
    virtual std::vector<TranslationResult> translateBatch(const std::vector<std::string>& texts) = 0;
    
    /**
     * Translate multiple texts asynchronously in batch mode
     * @param texts Vector of texts to translate
     * @return Future containing vector of translation results
     */
    virtual std::future<std::vector<TranslationResult>> translateBatchAsync(const std::vector<std::string>& texts) = 0;
    
    // Streaming translation methods
    
    /**
     * Start a streaming translation session
     * @param sessionId Unique session identifier
     * @param sourceLang Source language code
     * @param targetLang Target language code
     * @return true if session started successfully
     */
    virtual bool startStreamingTranslation(const std::string& sessionId, const std::string& sourceLang, const std::string& targetLang) = 0;
    
    /**
     * Add incremental text to streaming translation session
     * @param sessionId Session identifier
     * @param text Text chunk to add
     * @param isComplete Whether this is the final chunk
     * @return Translation result (may be partial)
     */
    virtual TranslationResult addStreamingText(const std::string& sessionId, const std::string& text, bool isComplete = false) = 0;
    
    /**
     * Finalize streaming translation session and get final result
     * @param sessionId Session identifier
     * @return Final translation result
     */
    virtual TranslationResult finalizeStreamingTranslation(const std::string& sessionId) = 0;
    
    /**
     * Cancel streaming translation session
     * @param sessionId Session identifier
     */
    virtual void cancelStreamingTranslation(const std::string& sessionId) = 0;
    
    /**
     * Check if streaming session exists
     * @param sessionId Session identifier
     * @return true if session exists
     */
    virtual bool hasStreamingSession(const std::string& sessionId) const = 0;
};

} // namespace mt
} // namespace speechrnt