#include "stt/streaming_transcriber.hpp"
#include "core/translation_pipeline.hpp"
#include "stt/whisper_stt.hpp"
#include <cctype>
#include <algorithm>
#include "utils/logging.hpp"
#include <chrono>
#include <algorithm>

namespace stt {

StreamingTranscriber::StreamingTranscriber()
    : minUpdateIntervalMs_(100)  // Send updates at most every 100ms
    , minTextLength_(3)          // Only send updates for text longer than 3 characters
    , textSimilarityThreshold_(0.8f)  // 80% similarity threshold to avoid redundant updates
    , incrementalUpdatesEnabled_(true)  // Enable incremental updates by default
    , maxUpdateFrequency_(10)    // Maximum 10 updates per second
{
}

StreamingTranscriber::~StreamingTranscriber() {
    // Cancel all active transcriptions
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (auto& [utteranceId, state] : transcriptionStates_) {
        if (state.isActive && !state.isFinalized) {
            state.isActive = false;
        }
    }
}

bool StreamingTranscriber::initialize(std::shared_ptr<TranscriptionManager> transcriptionManager, MessageSender messageSender) {
    if (!transcriptionManager || !messageSender) {
        speechrnt::utils::Logger::error("StreamingTranscriber: Invalid parameters for initialization");
        return false;
    }
    
    transcriptionManager_ = transcriptionManager;
    messageSender_ = messageSender;
    
    speechrnt::utils::Logger::info("StreamingTranscriber initialized successfully");
    return true;
}

bool StreamingTranscriber::initializeWithTranslationPipeline(
    std::shared_ptr<TranscriptionManager> transcriptionManager, 
    MessageSender messageSender,
    std::shared_ptr<::speechrnt::core::TranslationPipeline> translationPipeline
) {
    if (!transcriptionManager || !messageSender || !translationPipeline) {
        speechrnt::utils::Logger::error("StreamingTranscriber: Invalid parameters for translation pipeline initialization");
        return false;
    }
    
    transcriptionManager_ = transcriptionManager;
    messageSender_ = messageSender;
    translationPipeline_ = translationPipeline;
    
    // Generate a session ID for this transcriber instance
    sessionId_ = "streaming_session_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    speechrnt::utils::Logger::info("StreamingTranscriber initialized with translation pipeline integration");
    return true;
}

void StreamingTranscriber::startTranscription(uint32_t utteranceId, const std::vector<float>& audioData, bool isLive) {
    if (!transcriptionManager_) {
        speechrnt::utils::Logger::error("StreamingTranscriber not initialized");
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        
        // Initialize transcription state
        TranscriptionState& state = transcriptionStates_[utteranceId];
        const auto nowMs = getCurrentTimeMs();
        state.currentText.clear();
        state.lastSentText.clear();
        state.textHistory.clear();
        state.confidence = 0.0;
        state.startTimeMs = nowMs;
        state.lastUpdateTimeMs = 0;
        state.updateCount = 0;
        state.isActive = true;
        state.isFinalized = false;
    }
    
    // If the underlying engine supports streaming via WhisperSTT, use it for partials
    if (auto engine = transcriptionManager_->getSTTEngine()) {
        if (auto whisper = dynamic_cast<::stt::WhisperSTT*>(engine)) {
            whisper->setStreamingCallback(utteranceId, [this, utteranceId](const TranscriptionResult& result) {
                auto updateStart = std::chrono::steady_clock::now();
                handleTranscriptionResult(utteranceId, result);
                // Record streaming update latency
                auto updateEnd = std::chrono::steady_clock::now();
                double latencyMs = std::chrono::duration_cast<std::chrono::microseconds>(updateEnd - updateStart).count() / 1000.0;
                if (performanceTracker_) {
                    // Track delta length
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    auto it = transcriptionStates_.find(utteranceId);
                    int textDelta = 0;
                    if (it != transcriptionStates_.end()) {
                        textDelta = static_cast<int>(it->second.currentText.size()) - static_cast<int>(it->second.lastSentText.size());
                    }
                    performanceTracker_->recordStreamingUpdate(0 /* sessionId unknown here */, latencyMs, true, textDelta);
                }
            });
            whisper->startStreamingTranscription(utteranceId);
            whisper->addAudioChunk(utteranceId, audioData);
            if (!isLive) {
                whisper->finalizeStreamingTranscription(utteranceId);
            }
        } else {
            // Fallback to non-streaming request path
            TranscriptionRequest request;
            request.utterance_id = utteranceId;
            request.audio_data = audioData;
            request.is_live = isLive;
            request.callback = [this](uint32_t id, const TranscriptionResult& result) {
                handleTranscriptionResult(id, result);
            };
            transcriptionManager_->submitTranscription(request);
        }
    }
    
    speechrnt::utils::Logger::debug("Started streaming transcription for utterance " + std::to_string(utteranceId));
}

void StreamingTranscriber::addAudioData(uint32_t utteranceId, const std::vector<float>& audioData) {
    // For now, we'll treat this as a new transcription request
    // In a more sophisticated implementation, we might accumulate audio data
    // and send it in chunks to the transcription engine
    
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto it = transcriptionStates_.find(utteranceId);
    if (it == transcriptionStates_.end() || !it->second.isActive) {
        speechrnt::utils::Logger::warn("Attempted to add audio data to inactive transcription: " + std::to_string(utteranceId));
        return;
    }
    
    // For simulation, we'll just trigger another transcription
    // In real implementation, this would be handled by the streaming STT engine
    speechrnt::utils::Logger::debug("Added audio data to utterance " + std::to_string(utteranceId) + 
                         " (" + std::to_string(audioData.size()) + " samples)");
}

void StreamingTranscriber::finalizeTranscription(uint32_t utteranceId) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    auto it = transcriptionStates_.find(utteranceId);
    if (it == transcriptionStates_.end()) {
        speechrnt::utils::Logger::warn("Attempted to finalize unknown transcription: " + std::to_string(utteranceId));
        return;
    }
    
    TranscriptionState& state = it->second;
    if (!state.isActive) {
        speechrnt::utils::Logger::warn("Attempted to finalize inactive transcription: " + std::to_string(utteranceId));
        return;
    }
    
    state.isFinalized = true;
    state.isActive = false;
    
    // Send final transcription update if we have text
    if (!state.currentText.empty()) {
        sendTranscriptionUpdate(utteranceId, state, false);
        
        // Trigger translation pipeline if available
        if (translationPipeline_ && translationPipeline_->isReady()) {
            // Create transcription result for translation pipeline
            TranscriptionResult finalResult;
            finalResult.text = state.currentText;
            finalResult.confidence = static_cast<float>(state.confidence);
            finalResult.is_partial = false;
            finalResult.start_time_ms = state.startTimeMs;
            finalResult.end_time_ms = getCurrentTimeMs();
            
            // For now, we don't have multiple candidates in streaming mode
            // In a full implementation, we would generate them here
            std::vector<TranscriptionResult> candidates;
            
            // Trigger translation
            translationPipeline_->processTranscriptionResult(utteranceId, sessionId_, finalResult, candidates);
            
            speechrnt::utils::Logger::debug("Triggered translation pipeline for utterance " + std::to_string(utteranceId));
        }
    }
    
    speechrnt::utils::Logger::debug("Finalized transcription for utterance " + std::to_string(utteranceId));
}

void StreamingTranscriber::cancelTranscription(uint32_t utteranceId) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    auto it = transcriptionStates_.find(utteranceId);
    if (it != transcriptionStates_.end()) {
        it->second.isActive = false;
        it->second.isFinalized = true;
        transcriptionStates_.erase(it);
    }
    
    speechrnt::utils::Logger::debug("Cancelled transcription for utterance " + std::to_string(utteranceId));
}

bool StreamingTranscriber::isTranscribing(uint32_t utteranceId) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    auto it = transcriptionStates_.find(utteranceId);
    return it != transcriptionStates_.end() && it->second.isActive;
}

size_t StreamingTranscriber::getActiveTranscriptions() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    return std::count_if(transcriptionStates_.begin(), transcriptionStates_.end(),
                        [](const auto& pair) { return pair.second.isActive; });
}

void StreamingTranscriber::handleTranscriptionResult(uint32_t utteranceId, const TranscriptionResult& result) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    auto it = transcriptionStates_.find(utteranceId);
    if (it == transcriptionStates_.end() || !it->second.isActive) {
        speechrnt::utils::Logger::warn("Received transcription result for inactive utterance: " + std::to_string(utteranceId));
        return;
    }
    
    TranscriptionState& state = it->second;
    
    // Store previous text for comparison
    std::string previousText = state.currentText;
    
    // Normalize text per simple policy (configurable hook point)
    // Fetch normalization config if available from the manager
    auto normalizationSource = normalizationConfig_;

    auto normalize = [normalizationSource](const std::string& in) {
        std::string out = in;
        // Trim leading/trailing spaces
        if (normalizationSource.normalization.trimWhitespace) {
            auto notSpace = [](int ch){ return !std::isspace(ch); };
            out.erase(out.begin(), std::find_if(out.begin(), out.end(), notSpace));
            out.erase(std::find_if(out.rbegin(), out.rend(), notSpace).base(), out.end());
        }
        // Collapse multiple spaces
        std::string collapsed;
        collapsed.reserve(out.size());
        bool prevSpace = false;
        for (char c : out) {
            if (normalizationSource.normalization.collapseWhitespace && std::isspace(static_cast<unsigned char>(c))) {
                if (!prevSpace) collapsed.push_back(' ');
                prevSpace = true;
            } else {
                collapsed.push_back(c);
                prevSpace = false;
            }
        }
        out.swap(collapsed);
        // Lowercase
        if (normalizationSource.normalization.lowercase) {
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch){ return static_cast<char>(std::tolower(ch)); });
        }
        // Remove punctuation
        if (normalizationSource.normalization.removePunctuation) {
            out.erase(std::remove_if(out.begin(), out.end(), [](unsigned char ch){ return std::ispunct(ch); }), out.end());
        }
        // Optionally ensure ending punctuation for finals will be handled on finalize path
        return out;
    };

    // Update state with normalized transcription text
    state.currentText = normalize(result.text);
    state.confidence = result.confidence;
    
    // Update text history for incremental analysis
    updateTextHistory(state, result.text);
    
    // Determine if this is a partial or final result
    bool isPartial = result.is_partial && !state.isFinalized;
    
    // Check if this is a significant change worth sending
    bool isSignificantChange = isSignificantTextChange(previousText, state.currentText);
    
    // Send update if conditions are met
    if (shouldSendUpdate(state, isPartial) && (isSignificantChange || !isPartial)) {
        sendTranscriptionUpdate(utteranceId, state, isPartial);
        state.lastSentText = state.currentText;
        state.lastUpdateTimeMs = getCurrentTimeMs();
        state.updateCount++;
    }
    
    speechrnt::utils::Logger::debug("Handled transcription result for utterance " + std::to_string(utteranceId) + 
                         ": '" + result.text + "' (confidence: " + std::to_string(result.confidence) + 
                         ", partial: " + (isPartial ? "true" : "false") + ")");
}

void StreamingTranscriber::sendTranscriptionUpdate(uint32_t utteranceId, const TranscriptionState& state, bool isPartial) {
    if (!messageSender_) {
        speechrnt::utils::Logger::error("No message sender available");
        return;
    }
    
    // Detect incremental changes if enabled
    std::string incrementalText = state.currentText;
    if (incrementalUpdatesEnabled_ && isPartial && !state.lastSentText.empty()) {
        incrementalText = detectIncrementalChanges(state.lastSentText, state.currentText);
    }
    
    // Attach timestamps from state and now; send update
    core::TranscriptionUpdateMessage message(
        state.currentText,
        utteranceId,
        state.confidence,
        isPartial,
        state.startTimeMs,
        getCurrentTimeMs()
    );
    
    // Send the message
    std::string serialized = message.serialize();
    messageSender_(serialized);
    
    // Log with additional information about the update
    speechrnt::utils::Logger::debug("Sent transcription update for utterance " + std::to_string(utteranceId) + 
                         ": '" + state.currentText + "' (partial: " + (isPartial ? "true" : "false") + 
                         ", update #" + std::to_string(state.updateCount + 1) + 
                         ", similarity: " + std::to_string(calculateTextSimilarity(state.lastSentText, state.currentText)) + ")");
}

bool StreamingTranscriber::shouldSendUpdate(const TranscriptionState& state, bool isPartial) const {
    // Always send final results
    if (!isPartial) {
        return true;
    }
    
    // Don't send partial updates if incremental updates are disabled
    if (!incrementalUpdatesEnabled_ && isPartial) {
        return false;
    }
    
    // Don't send if text is too short
    if (state.currentText.length() < minTextLength_) {
        return false;
    }
    
    // Don't send if text hasn't changed significantly
    if (state.currentText == state.lastSentText) {
        return false;
    }
    
    // Check text similarity to avoid redundant updates
    float similarity = calculateTextSimilarity(state.currentText, state.lastSentText);
    if (similarity > textSimilarityThreshold_) {
        return false;
    }
    
    // Don't send if not enough time has passed since last update
    int64_t currentTime = getCurrentTimeMs();
    if (state.lastUpdateTimeMs > 0 && 
        (currentTime - state.lastUpdateTimeMs) < minUpdateIntervalMs_) {
        return false;
    }
    
    // Check maximum update frequency
    if (maxUpdateFrequency_ > 0) {
        int64_t minIntervalForFrequency = 1000 / maxUpdateFrequency_;  // Convert to ms
        if (state.lastUpdateTimeMs > 0 && 
            (currentTime - state.lastUpdateTimeMs) < minIntervalForFrequency) {
            return false;
        }
    }
    
    return true;
}

int64_t StreamingTranscriber::getCurrentTimeMs() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

float StreamingTranscriber::calculateTextSimilarity(const std::string& text1, const std::string& text2) const {
    if (text1.empty() && text2.empty()) {
        return 1.0f;
    }
    
    if (text1.empty() || text2.empty()) {
        return 0.0f;
    }
    
    // Simple Levenshtein distance-based similarity
    size_t len1 = text1.length();
    size_t len2 = text2.length();
    
    // Create matrix for dynamic programming
    std::vector<std::vector<size_t>> matrix(len1 + 1, std::vector<size_t>(len2 + 1));
    
    // Initialize first row and column
    for (size_t i = 0; i <= len1; ++i) {
        matrix[i][0] = i;
    }
    for (size_t j = 0; j <= len2; ++j) {
        matrix[0][j] = j;
    }
    
    // Fill the matrix
    for (size_t i = 1; i <= len1; ++i) {
        for (size_t j = 1; j <= len2; ++j) {
            size_t cost = (text1[i-1] == text2[j-1]) ? 0 : 1;
            matrix[i][j] = std::min({
                matrix[i-1][j] + 1,      // deletion
                matrix[i][j-1] + 1,      // insertion
                matrix[i-1][j-1] + cost  // substitution
            });
        }
    }
    
    // Calculate similarity as 1 - (distance / max_length)
    size_t distance = matrix[len1][len2];
    size_t maxLength = std::max(len1, len2);
    return 1.0f - (static_cast<float>(distance) / static_cast<float>(maxLength));
}

std::string StreamingTranscriber::detectIncrementalChanges(const std::string& oldText, const std::string& newText) const {
    // Simple implementation: return the new text if it's longer than old text
    // In a more sophisticated implementation, this could return just the diff
    if (newText.length() > oldText.length() && 
        newText.substr(0, oldText.length()) == oldText) {
        // New text is an extension of old text
        return newText.substr(oldText.length());
    }
    
    // Return full new text if it's not a simple extension
    return newText;
}

bool StreamingTranscriber::isSignificantTextChange(const std::string& oldText, const std::string& newText) const {
    // Check if the change is significant enough to warrant an update
    
    // Always consider it significant if one text is empty
    if (oldText.empty() || newText.empty()) {
        return true;
    }
    
    // Check length difference
    size_t lengthDiff = (newText.length() > oldText.length()) ? 
                       (newText.length() - oldText.length()) : 
                       (oldText.length() - newText.length());
    
    // Consider significant if length difference is substantial
    if (lengthDiff > 5) {  // More than 5 characters difference
        return true;
    }
    
    // Check similarity
    float similarity = calculateTextSimilarity(oldText, newText);
    return similarity < textSimilarityThreshold_;
}

void StreamingTranscriber::updateTextHistory(TranscriptionState& state, const std::string& newText) {
    // Add new text to history
    state.textHistory.push_back(newText);
    
    // Keep only recent history (last 10 entries)
    const size_t maxHistorySize = 10;
    if (state.textHistory.size() > maxHistorySize) {
        state.textHistory.erase(state.textHistory.begin());
    }
}

} // namespace stt