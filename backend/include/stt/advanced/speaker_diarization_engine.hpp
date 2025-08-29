#pragma once

#include "stt/advanced/speaker_diarization_interface.hpp"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>
#include <chrono>

namespace stt {
namespace advanced {

/**
 * Simple speaker detection model implementation
 * Uses energy-based and spectral features for speaker change detection
 */
class SimpleSpeakerDetectionModel : public SpeakerDetectionModel {
public:
    SimpleSpeakerDetectionModel();
    ~SimpleSpeakerDetectionModel() override = default;
    
    bool initialize(const std::string& modelPath) override;
    std::vector<int64_t> detectSpeakerChanges(const std::vector<float>& audioData, 
                                              int sampleRate) override;
    bool isInitialized() const override { return initialized_; }
    std::string getModelInfo() const override;

private:
    std::atomic<bool> initialized_;
    float changeThreshold_;
    size_t windowSize_;
    size_t hopSize_;
    
    // Helper methods
    std::vector<float> extractEnergyFeatures(const std::vector<float>& audioData, int sampleRate);
    std::vector<float> extractSpectralFeatures(const std::vector<float>& audioData, int sampleRate);
    float calculateFeatureDistance(const std::vector<float>& features1, 
                                  const std::vector<float>& features2);
    std::vector<float> computeFFT(const std::vector<float>& window);
};

/**
 * Simple speaker embedding model implementation
 * Uses MFCC-based features for speaker embeddings
 */
class SimpleSpeakerEmbeddingModel : public SpeakerEmbeddingModel {
public:
    SimpleSpeakerEmbeddingModel();
    ~SimpleSpeakerEmbeddingModel() override = default;
    
    bool initialize(const std::string& modelPath) override;
    std::vector<float> generateEmbedding(const std::vector<float>& audioData, 
                                        int sampleRate) override;
    float calculateSimilarity(const std::vector<float>& embedding1,
                             const std::vector<float>& embedding2) override;
    size_t getEmbeddingDimension() const override { return embeddingDimension_; }
    bool isInitialized() const override { return initialized_; }

private:
    std::atomic<bool> initialized_;
    size_t embeddingDimension_;
    size_t numMfccCoeffs_;
    size_t windowSize_;
    size_t hopSize_;
    
    // Helper methods
    std::vector<float> extractMFCCFeatures(const std::vector<float>& audioData, int sampleRate);
    std::vector<float> computeMelFilterBank(const std::vector<float>& spectrum, int sampleRate);
    std::vector<float> computeDCT(const std::vector<float>& melFeatures);
    float cosineSimilarity(const std::vector<float>& vec1, const std::vector<float>& vec2);
    float euclideanDistance(const std::vector<float>& vec1, const std::vector<float>& vec2);
};

/**
 * K-means based speaker clustering implementation
 */
class KMeansSpeakerClustering : public SpeakerClustering {
public:
    KMeansSpeakerClustering();
    ~KMeansSpeakerClustering() override = default;
    
    std::map<size_t, uint32_t> clusterSpeakers(const std::vector<std::vector<float>>& embeddings,
                                              float threshold) override;
    uint32_t addEmbedding(const std::vector<float>& embedding, float threshold) override;
    size_t getClusterCount() const override { return clusterCentroids_.size(); }
    void reset() override;

private:
    std::vector<std::vector<float>> clusterCentroids_;
    std::vector<std::vector<size_t>> clusterMembers_;
    uint32_t nextClusterId_;
    mutable std::mutex clusteringMutex_;
    
    // Helper methods
    uint32_t findNearestCluster(const std::vector<float>& embedding, float threshold);
    void updateClusterCentroid(uint32_t clusterId, const std::vector<float>& newEmbedding);
    float calculateDistance(const std::vector<float>& vec1, const std::vector<float>& vec2);
    std::vector<float> calculateCentroid(const std::vector<std::vector<float>>& embeddings);
};

/**
 * Streaming diarization state for real-time processing
 */
struct StreamingDiarizationState {
    uint32_t utteranceId;
    std::vector<float> audioBuffer;
    std::vector<SpeakerSegment> segments;
    uint32_t currentSpeakerId;
    int64_t lastChangeTimeMs;
    float currentConfidence;
    bool isActive;
    std::chrono::steady_clock::time_point startTime;
    
    StreamingDiarizationState(uint32_t id) 
        : utteranceId(id), currentSpeakerId(0), lastChangeTimeMs(0)
        , currentConfidence(0.0f), isActive(false)
        , startTime(std::chrono::steady_clock::now()) {}
};

/**
 * Concrete implementation of Speaker Diarization Engine
 */
class SpeakerDiarizationEngine : public SpeakerDiarizationInterface {
public:
    SpeakerDiarizationEngine();
    ~SpeakerDiarizationEngine() override;
    
    // Core interface implementation
    bool initialize(const std::string& modelPath) override;
    DiarizationResult processSpeakerDiarization(const std::vector<float>& audioData,
                                               int sampleRate = 16000) override;
    
    // Speaker profile management
    bool addSpeakerProfile(const SpeakerProfile& profile) override;
    void updateSpeakerProfiles(const DiarizationResult& result) override;
    std::map<uint32_t, SpeakerProfile> getSpeakerProfiles() const override;
    bool removeSpeakerProfile(uint32_t speakerId) override;
    void clearSpeakerProfiles() override;
    
    // Real-time streaming support
    bool startStreamingDiarization(uint32_t utteranceId) override;
    bool addAudioForDiarization(uint32_t utteranceId, 
                               const std::vector<float>& audioChunk,
                               int sampleRate = 16000) override;
    SpeakerSegment getCurrentSpeaker(uint32_t utteranceId) override;
    DiarizationResult finishStreamingDiarization(uint32_t utteranceId) override;
    void cancelStreamingDiarization(uint32_t utteranceId) override;
    
    // Configuration and status
    void setMaxSpeakers(size_t maxSpeakers) override;
    void setSpeakerChangeThreshold(float threshold) override;
    void setSpeakerIdentificationThreshold(float threshold) override;
    void setProfileLearningEnabled(bool enabled) override;
    bool isInitialized() const override { return initialized_; }
    std::string getLastError() const override;
    std::string getProcessingStats() const override;
    void reset() override;

private:
    // Core components
    std::unique_ptr<SpeakerDetectionModel> detectionModel_;
    std::unique_ptr<SpeakerEmbeddingModel> embeddingModel_;
    std::unique_ptr<SpeakerClustering> clustering_;
    
    // Speaker profiles and state
    std::map<uint32_t, SpeakerProfile> knownSpeakers_;
    std::unordered_map<uint32_t, std::unique_ptr<StreamingDiarizationState>> streamingStates_;
    
    // Configuration
    size_t maxSpeakers_;
    float speakerChangeThreshold_;
    float speakerIdentificationThreshold_;
    bool profileLearningEnabled_;
    std::string modelPath_;
    
    // Thread safety
    mutable std::mutex speakerProfilesMutex_;
    mutable std::mutex streamingStatesMutex_;
    mutable std::mutex configMutex_;
    
    // Status and error tracking
    std::atomic<bool> initialized_;
    mutable std::string lastError_;
    
    // Processing statistics
    struct ProcessingStats {
        size_t totalProcessedSegments = 0;
        size_t totalDetectedSpeakers = 0;
        float averageConfidence = 0.0f;
        std::chrono::milliseconds totalProcessingTime{0};
        size_t activeStreamingSessions = 0;
        size_t profileLearningEvents = 0;
        
        std::mutex statsMutex;
    } stats_;
    
    // Helper methods
    DiarizationResult processAudioSegment(const std::vector<float>& audioData, 
                                         int sampleRate, bool isStreaming = false);
    std::vector<SpeakerSegment> detectSpeakerSegments(const std::vector<float>& audioData, 
                                                     int sampleRate);
    uint32_t identifySpeaker(const std::vector<float>& embedding);
    uint32_t assignSpeakerToCluster(const std::vector<float>& embedding);
    void updateSpeakerProfile(uint32_t speakerId, const std::vector<float>& embedding);
    bool validateAudioData(const std::vector<float>& audioData, int sampleRate);
    void setLastError(const std::string& error);
    void updateProcessingStats(const DiarizationResult& result, 
                              std::chrono::milliseconds processingTime);
    
    // Streaming helpers
    bool processStreamingAudio(StreamingDiarizationState& state, 
                              const std::vector<float>& audioChunk, int sampleRate);
    void detectSpeakerChangeInStream(StreamingDiarizationState& state, 
                                    const std::vector<float>& audioChunk, int sampleRate);
    SpeakerSegment createSpeakerSegment(uint32_t speakerId, int64_t startMs, 
                                       int64_t endMs, float confidence);
    
    // Profile management helpers
    uint32_t generateNewSpeakerId();
    bool isKnownSpeaker(const std::vector<float>& embedding, uint32_t& speakerId, float& confidence);
    void learnFromDiarizationResult(const DiarizationResult& result);
    
    // Validation helpers
    bool validateConfiguration() const;
    bool validateModelPath(const std::string& path) const;
    
    // Constants
    static constexpr float DEFAULT_SPEAKER_CHANGE_THRESHOLD = 0.7f;
    static constexpr float DEFAULT_SPEAKER_IDENTIFICATION_THRESHOLD = 0.8f;
    static constexpr size_t DEFAULT_MAX_SPEAKERS = 10;
    static constexpr size_t MIN_AUDIO_LENGTH_MS = 100;
    static constexpr size_t STREAMING_BUFFER_SIZE_MS = 1000;
};

} // namespace advanced
} // namespace stt