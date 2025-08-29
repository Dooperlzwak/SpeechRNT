#pragma once

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <cstdint>

namespace stt {
namespace advanced {

/**
 * Speaker profile information
 */
struct SpeakerProfile {
    uint32_t speakerId;
    std::string speakerLabel;
    std::vector<float> referenceEmbedding;
    float confidence;
    size_t utteranceCount;
    std::string metadata; // JSON string for additional info
    
    SpeakerProfile() : speakerId(0), confidence(0.0f), utteranceCount(0) {}
    
    SpeakerProfile(uint32_t id, const std::string& label, const std::vector<float>& embedding)
        : speakerId(id), speakerLabel(label), referenceEmbedding(embedding)
        , confidence(0.0f), utteranceCount(0) {}
};

/**
 * Speaker segment information
 */
struct SpeakerSegment {
    uint32_t speakerId;
    std::string speakerLabel;
    int64_t startTimeMs;
    int64_t endTimeMs;
    float confidence;
    std::vector<float> speakerEmbedding;
    
    SpeakerSegment() : speakerId(0), startTimeMs(0), endTimeMs(0), confidence(0.0f) {}
    
    SpeakerSegment(uint32_t id, const std::string& label, int64_t start, int64_t end, float conf)
        : speakerId(id), speakerLabel(label), startTimeMs(start), endTimeMs(end), confidence(conf) {}
};

/**
 * Speaker diarization result
 */
struct DiarizationResult {
    std::vector<SpeakerSegment> segments;
    size_t totalSpeakers;
    std::map<uint32_t, SpeakerProfile> detectedSpeakers;
    float overallConfidence;
    bool hasNewSpeakers;
    std::string processingInfo; // Debug/diagnostic information
    
    DiarizationResult() : totalSpeakers(0), overallConfidence(0.0f), hasNewSpeakers(false) {}
};

/**
 * Speaker detection model interface
 */
class SpeakerDetectionModel {
public:
    virtual ~SpeakerDetectionModel() = default;
    
    /**
     * Initialize the speaker detection model
     * @param modelPath Path to the model file
     * @return true if initialization successful
     */
    virtual bool initialize(const std::string& modelPath) = 0;
    
    /**
     * Detect speaker changes in audio
     * @param audioData Audio samples
     * @param sampleRate Sample rate of audio
     * @return Vector of change points in milliseconds
     */
    virtual std::vector<int64_t> detectSpeakerChanges(const std::vector<float>& audioData, 
                                                      int sampleRate) = 0;
    
    /**
     * Check if model is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
    
    /**
     * Get model information
     * @return Model information string
     */
    virtual std::string getModelInfo() const = 0;
};

/**
 * Speaker embedding model interface
 */
class SpeakerEmbeddingModel {
public:
    virtual ~SpeakerEmbeddingModel() = default;
    
    /**
     * Initialize the speaker embedding model
     * @param modelPath Path to the model file
     * @return true if initialization successful
     */
    virtual bool initialize(const std::string& modelPath) = 0;
    
    /**
     * Generate speaker embedding from audio
     * @param audioData Audio samples
     * @param sampleRate Sample rate of audio
     * @return Speaker embedding vector
     */
    virtual std::vector<float> generateEmbedding(const std::vector<float>& audioData, 
                                                int sampleRate) = 0;
    
    /**
     * Calculate similarity between two embeddings
     * @param embedding1 First embedding
     * @param embedding2 Second embedding
     * @return Similarity score (0.0 to 1.0)
     */
    virtual float calculateSimilarity(const std::vector<float>& embedding1,
                                    const std::vector<float>& embedding2) = 0;
    
    /**
     * Get embedding dimension
     * @return Embedding vector size
     */
    virtual size_t getEmbeddingDimension() const = 0;
    
    /**
     * Check if model is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Speaker clustering interface
 */
class SpeakerClustering {
public:
    virtual ~SpeakerClustering() = default;
    
    /**
     * Cluster speaker embeddings
     * @param embeddings Vector of speaker embeddings
     * @param threshold Clustering threshold
     * @return Map of embedding index to cluster ID
     */
    virtual std::map<size_t, uint32_t> clusterSpeakers(const std::vector<std::vector<float>>& embeddings,
                                                       float threshold) = 0;
    
    /**
     * Update clustering with new embedding
     * @param embedding New speaker embedding
     * @param threshold Clustering threshold
     * @return Cluster ID for the new embedding
     */
    virtual uint32_t addEmbedding(const std::vector<float>& embedding, float threshold) = 0;
    
    /**
     * Get number of clusters
     * @return Number of speaker clusters
     */
    virtual size_t getClusterCount() const = 0;
    
    /**
     * Reset clustering state
     */
    virtual void reset() = 0;
};

/**
 * Speaker diarization engine interface
 */
class SpeakerDiarizationInterface {
public:
    virtual ~SpeakerDiarizationInterface() = default;
    
    /**
     * Initialize the speaker diarization engine
     * @param modelPath Path to speaker models
     * @return true if initialization successful
     */
    virtual bool initialize(const std::string& modelPath) = 0;
    
    /**
     * Process speaker diarization for audio
     * @param audioData Audio samples
     * @param sampleRate Sample rate of audio
     * @return Diarization result
     */
    virtual DiarizationResult processSpeakerDiarization(const std::vector<float>& audioData,
                                                       int sampleRate = 16000) = 0;
    
    /**
     * Add known speaker profile
     * @param profile Speaker profile to add
     * @return true if added successfully
     */
    virtual bool addSpeakerProfile(const SpeakerProfile& profile) = 0;
    
    /**
     * Update speaker profiles based on diarization result
     * @param result Diarization result to learn from
     */
    virtual void updateSpeakerProfiles(const DiarizationResult& result) = 0;
    
    /**
     * Get known speaker profiles
     * @return Map of speaker ID to profile
     */
    virtual std::map<uint32_t, SpeakerProfile> getSpeakerProfiles() const = 0;
    
    /**
     * Remove speaker profile
     * @param speakerId Speaker ID to remove
     * @return true if removed successfully
     */
    virtual bool removeSpeakerProfile(uint32_t speakerId) = 0;
    
    /**
     * Clear all speaker profiles
     */
    virtual void clearSpeakerProfiles() = 0;
    
    // Real-time streaming support
    
    /**
     * Start streaming diarization for an utterance
     * @param utteranceId Unique utterance identifier
     * @return true if started successfully
     */
    virtual bool startStreamingDiarization(uint32_t utteranceId) = 0;
    
    /**
     * Add audio chunk for streaming diarization
     * @param utteranceId Utterance identifier
     * @param audioChunk Audio chunk to process
     * @param sampleRate Sample rate of audio
     * @return true if processed successfully
     */
    virtual bool addAudioForDiarization(uint32_t utteranceId, 
                                       const std::vector<float>& audioChunk,
                                       int sampleRate = 16000) = 0;
    
    /**
     * Get current speaker for streaming utterance
     * @param utteranceId Utterance identifier
     * @return Current speaker segment
     */
    virtual SpeakerSegment getCurrentSpeaker(uint32_t utteranceId) = 0;
    
    /**
     * Finish streaming diarization for an utterance
     * @param utteranceId Utterance identifier
     * @return Final diarization result
     */
    virtual DiarizationResult finishStreamingDiarization(uint32_t utteranceId) = 0;
    
    /**
     * Cancel streaming diarization for an utterance
     * @param utteranceId Utterance identifier
     */
    virtual void cancelStreamingDiarization(uint32_t utteranceId) = 0;
    
    // Configuration and status
    
    /**
     * Set maximum number of speakers
     * @param maxSpeakers Maximum speakers to detect
     */
    virtual void setMaxSpeakers(size_t maxSpeakers) = 0;
    
    /**
     * Set speaker change detection threshold
     * @param threshold Threshold for speaker changes (0.0 to 1.0)
     */
    virtual void setSpeakerChangeThreshold(float threshold) = 0;
    
    /**
     * Set speaker identification threshold
     * @param threshold Threshold for speaker identification (0.0 to 1.0)
     */
    virtual void setSpeakerIdentificationThreshold(float threshold) = 0;
    
    /**
     * Enable or disable speaker profile learning
     * @param enabled true to enable learning
     */
    virtual void setProfileLearningEnabled(bool enabled) = 0;
    
    /**
     * Check if engine is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
    
    /**
     * Get last error message
     * @return Last error message
     */
    virtual std::string getLastError() const = 0;
    
    /**
     * Get processing statistics
     * @return Statistics as JSON string
     */
    virtual std::string getProcessingStats() const = 0;
    
    /**
     * Reset engine state
     */
    virtual void reset() = 0;
};

} // namespace advanced
} // namespace stt