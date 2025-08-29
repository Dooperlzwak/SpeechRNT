#include "stt/advanced/speaker_diarization_engine.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <random>

namespace stt {
namespace advanced {

// SimpleSpeakerDetectionModel Implementation

SimpleSpeakerDetectionModel::SimpleSpeakerDetectionModel()
    : initialized_(false), changeThreshold_(0.7f), windowSize_(1024), hopSize_(512) {
}

bool SimpleSpeakerDetectionModel::initialize(const std::string& modelPath) {
    try {
        // For this simple implementation, we just validate the path exists
        if (!std::filesystem::exists(modelPath)) {
            // Create directory if it doesn't exist for future model storage
            std::filesystem::create_directories(modelPath);
        }
        
        // Initialize with default parameters
        changeThreshold_ = 0.7f;
        windowSize_ = 1024;
        hopSize_ = 512;
        
        initialized_ = true;
        return true;
    } catch (const std::exception& e) {
        initialized_ = false;
        return false;
    }
}

std::vector<int64_t> SimpleSpeakerDetectionModel::detectSpeakerChanges(
    const std::vector<float>& audioData, int sampleRate) {
    
    if (!initialized_ || audioData.empty()) {
        return {};
    }
    
    std::vector<int64_t> changePoints;
    
    // Extract features for overlapping windows
    std::vector<std::vector<float>> windowFeatures;
    
    for (size_t i = 0; i + windowSize_ < audioData.size(); i += hopSize_) {
        std::vector<float> window(audioData.begin() + i, audioData.begin() + i + windowSize_);
        
        // Extract energy and spectral features
        auto energyFeatures = extractEnergyFeatures(window, sampleRate);
        auto spectralFeatures = extractSpectralFeatures(window, sampleRate);
        
        // Combine features
        std::vector<float> combinedFeatures;
        combinedFeatures.insert(combinedFeatures.end(), energyFeatures.begin(), energyFeatures.end());
        combinedFeatures.insert(combinedFeatures.end(), spectralFeatures.begin(), spectralFeatures.end());
        
        windowFeatures.push_back(combinedFeatures);
    }
    
    // Detect changes by comparing adjacent windows
    for (size_t i = 1; i < windowFeatures.size(); ++i) {
        float distance = calculateFeatureDistance(windowFeatures[i-1], windowFeatures[i]);
        
        if (distance > changeThreshold_) {
            // Convert window index to time in milliseconds
            int64_t timeMs = static_cast<int64_t>((i * hopSize_ * 1000.0) / sampleRate);
            changePoints.push_back(timeMs);
        }
    }
    
    return changePoints;
}

std::string SimpleSpeakerDetectionModel::getModelInfo() const {
    return "SimpleSpeakerDetectionModel v1.0 - Energy and spectral feature based speaker change detection";
}

std::vector<float> SimpleSpeakerDetectionModel::extractEnergyFeatures(
    const std::vector<float>& audioData, int sampleRate) {
    
    std::vector<float> features;
    
    // RMS Energy
    float rmsEnergy = 0.0f;
    for (float sample : audioData) {
        rmsEnergy += sample * sample;
    }
    rmsEnergy = std::sqrt(rmsEnergy / audioData.size());
    features.push_back(rmsEnergy);
    
    // Zero Crossing Rate
    int zeroCrossings = 0;
    for (size_t i = 1; i < audioData.size(); ++i) {
        if ((audioData[i] >= 0) != (audioData[i-1] >= 0)) {
            zeroCrossings++;
        }
    }
    float zcr = static_cast<float>(zeroCrossings) / audioData.size();
    features.push_back(zcr);
    
    return features;
}

std::vector<float> SimpleSpeakerDetectionModel::extractSpectralFeatures(
    const std::vector<float>& audioData, int sampleRate) {
    
    std::vector<float> features;
    
    // Compute FFT
    auto spectrum = computeFFT(audioData);
    
    // Spectral Centroid
    float spectralCentroid = 0.0f;
    float totalMagnitude = 0.0f;
    
    for (size_t i = 0; i < spectrum.size() / 2; ++i) {
        float magnitude = std::sqrt(spectrum[i*2] * spectrum[i*2] + spectrum[i*2+1] * spectrum[i*2+1]);
        float frequency = static_cast<float>(i * sampleRate) / spectrum.size();
        
        spectralCentroid += frequency * magnitude;
        totalMagnitude += magnitude;
    }
    
    if (totalMagnitude > 0) {
        spectralCentroid /= totalMagnitude;
    }
    features.push_back(spectralCentroid);
    
    // Spectral Rolloff (85% of energy)
    float targetEnergy = totalMagnitude * 0.85f;
    float cumulativeEnergy = 0.0f;
    float spectralRolloff = 0.0f;
    
    for (size_t i = 0; i < spectrum.size() / 2; ++i) {
        float magnitude = std::sqrt(spectrum[i*2] * spectrum[i*2] + spectrum[i*2+1] * spectrum[i*2+1]);
        cumulativeEnergy += magnitude;
        
        if (cumulativeEnergy >= targetEnergy) {
            spectralRolloff = static_cast<float>(i * sampleRate) / spectrum.size();
            break;
        }
    }
    features.push_back(spectralRolloff);
    
    return features;
}

float SimpleSpeakerDetectionModel::calculateFeatureDistance(
    const std::vector<float>& features1, const std::vector<float>& features2) {
    
    if (features1.size() != features2.size()) {
        return 1.0f; // Maximum distance for mismatched features
    }
    
    float distance = 0.0f;
    for (size_t i = 0; i < features1.size(); ++i) {
        float diff = features1[i] - features2[i];
        distance += diff * diff;
    }
    
    return std::sqrt(distance);
}

std::vector<float> SimpleSpeakerDetectionModel::computeFFT(const std::vector<float>& window) {
    // Simple DFT implementation (not optimized, but functional)
    size_t N = window.size();
    std::vector<float> result(N * 2, 0.0f); // Real and imaginary parts
    
    for (size_t k = 0; k < N; ++k) {
        float realPart = 0.0f;
        float imagPart = 0.0f;
        
        for (size_t n = 0; n < N; ++n) {
            float angle = -2.0f * M_PI * k * n / N;
            realPart += window[n] * std::cos(angle);
            imagPart += window[n] * std::sin(angle);
        }
        
        result[k * 2] = realPart;
        result[k * 2 + 1] = imagPart;
    }
    
    return result;
}

// SimpleSpeakerEmbeddingModel Implementation

SimpleSpeakerEmbeddingModel::SimpleSpeakerEmbeddingModel()
    : initialized_(false), embeddingDimension_(128), numMfccCoeffs_(13)
    , windowSize_(1024), hopSize_(512) {
}

bool SimpleSpeakerEmbeddingModel::initialize(const std::string& modelPath) {
    try {
        // For this simple implementation, we just validate the path exists
        if (!std::filesystem::exists(modelPath)) {
            std::filesystem::create_directories(modelPath);
        }
        
        // Initialize with default parameters
        embeddingDimension_ = 128;
        numMfccCoeffs_ = 13;
        windowSize_ = 1024;
        hopSize_ = 512;
        
        initialized_ = true;
        return true;
    } catch (const std::exception& e) {
        initialized_ = false;
        return false;
    }
}

std::vector<float> SimpleSpeakerEmbeddingModel::generateEmbedding(
    const std::vector<float>& audioData, int sampleRate) {
    
    if (!initialized_ || audioData.empty()) {
        return std::vector<float>(embeddingDimension_, 0.0f);
    }
    
    // Extract MFCC features from multiple windows
    std::vector<std::vector<float>> mfccFrames;
    
    for (size_t i = 0; i + windowSize_ < audioData.size(); i += hopSize_) {
        std::vector<float> window(audioData.begin() + i, audioData.begin() + i + windowSize_);
        auto mfccFeatures = extractMFCCFeatures(window, sampleRate);
        mfccFrames.push_back(mfccFeatures);
    }
    
    if (mfccFrames.empty()) {
        return std::vector<float>(embeddingDimension_, 0.0f);
    }
    
    // Create embedding by statistical aggregation of MFCC features
    std::vector<float> embedding;
    
    // Mean of each MFCC coefficient
    for (size_t coeff = 0; coeff < numMfccCoeffs_; ++coeff) {
        float mean = 0.0f;
        for (const auto& frame : mfccFrames) {
            if (coeff < frame.size()) {
                mean += frame[coeff];
            }
        }
        mean /= mfccFrames.size();
        embedding.push_back(mean);
    }
    
    // Standard deviation of each MFCC coefficient
    for (size_t coeff = 0; coeff < numMfccCoeffs_; ++coeff) {
        float variance = 0.0f;
        float mean = embedding[coeff];
        
        for (const auto& frame : mfccFrames) {
            if (coeff < frame.size()) {
                float diff = frame[coeff] - mean;
                variance += diff * diff;
            }
        }
        variance /= mfccFrames.size();
        embedding.push_back(std::sqrt(variance));
    }
    
    // Delta features (first derivatives)
    std::vector<float> deltaFeatures(numMfccCoeffs_, 0.0f);
    for (size_t i = 1; i < mfccFrames.size() - 1; ++i) {
        for (size_t coeff = 0; coeff < numMfccCoeffs_ && coeff < mfccFrames[i].size(); ++coeff) {
            if (coeff < mfccFrames[i-1].size() && coeff < mfccFrames[i+1].size()) {
                deltaFeatures[coeff] += (mfccFrames[i+1][coeff] - mfccFrames[i-1][coeff]) / 2.0f;
            }
        }
    }
    
    // Average delta features
    for (float& delta : deltaFeatures) {
        delta /= std::max(1.0f, static_cast<float>(mfccFrames.size() - 2));
    }
    
    embedding.insert(embedding.end(), deltaFeatures.begin(), deltaFeatures.end());
    
    // Pad or truncate to desired embedding dimension
    embedding.resize(embeddingDimension_, 0.0f);
    
    // Normalize embedding
    float norm = 0.0f;
    for (float val : embedding) {
        norm += val * val;
    }
    norm = std::sqrt(norm);
    
    if (norm > 0.0f) {
        for (float& val : embedding) {
            val /= norm;
        }
    }
    
    return embedding;
}

float SimpleSpeakerEmbeddingModel::calculateSimilarity(
    const std::vector<float>& embedding1, const std::vector<float>& embedding2) {
    
    if (embedding1.size() != embedding2.size()) {
        return 0.0f;
    }
    
    // Use cosine similarity
    return cosineSimilarity(embedding1, embedding2);
}

std::vector<float> SimpleSpeakerEmbeddingModel::extractMFCCFeatures(
    const std::vector<float>& audioData, int sampleRate) {
    
    // Simplified MFCC extraction
    // 1. Apply window function (Hamming)
    std::vector<float> windowed = audioData;
    for (size_t i = 0; i < windowed.size(); ++i) {
        float hamming = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (windowed.size() - 1));
        windowed[i] *= hamming;
    }
    
    // 2. Compute FFT
    auto spectrum = computeFFT(windowed);
    
    // 3. Compute power spectrum
    std::vector<float> powerSpectrum;
    for (size_t i = 0; i < spectrum.size() / 2; ++i) {
        float magnitude = spectrum[i*2] * spectrum[i*2] + spectrum[i*2+1] * spectrum[i*2+1];
        powerSpectrum.push_back(magnitude);
    }
    
    // 4. Apply mel filter bank
    auto melFeatures = computeMelFilterBank(powerSpectrum, sampleRate);
    
    // 5. Apply DCT
    auto mfccFeatures = computeDCT(melFeatures);
    
    return mfccFeatures;
}

std::vector<float> SimpleSpeakerEmbeddingModel::computeMelFilterBank(
    const std::vector<float>& spectrum, int sampleRate) {
    
    const size_t numFilters = 26;
    const float minFreq = 0.0f;
    const float maxFreq = sampleRate / 2.0f;
    
    // Convert to mel scale
    auto hzToMel = [](float hz) { return 2595.0f * std::log10(1.0f + hz / 700.0f); };
    auto melToHz = [](float mel) { return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f); };
    
    float minMel = hzToMel(minFreq);
    float maxMel = hzToMel(maxFreq);
    
    // Create mel filter bank
    std::vector<float> melFilters(numFilters, 0.0f);
    
    for (size_t filter = 0; filter < numFilters; ++filter) {
        float centerMel = minMel + (maxMel - minMel) * filter / (numFilters - 1);
        float centerHz = melToHz(centerMel);
        
        // Simple triangular filter approximation
        float filterResponse = 0.0f;
        for (size_t bin = 0; bin < spectrum.size(); ++bin) {
            float binHz = static_cast<float>(bin * sampleRate) / (2 * spectrum.size());
            
            if (std::abs(binHz - centerHz) < 100.0f) { // Simple bandwidth
                float weight = 1.0f - std::abs(binHz - centerHz) / 100.0f;
                filterResponse += spectrum[bin] * weight;
            }
        }
        
        melFilters[filter] = std::log(std::max(filterResponse, 1e-10f));
    }
    
    return melFilters;
}

std::vector<float> SimpleSpeakerEmbeddingModel::computeDCT(const std::vector<float>& melFeatures) {
    std::vector<float> dctFeatures(numMfccCoeffs_, 0.0f);
    
    for (size_t k = 0; k < numMfccCoeffs_; ++k) {
        for (size_t n = 0; n < melFeatures.size(); ++n) {
            dctFeatures[k] += melFeatures[n] * std::cos(M_PI * k * (n + 0.5f) / melFeatures.size());
        }
    }
    
    return dctFeatures;
}

std::vector<float> SimpleSpeakerEmbeddingModel::computeFFT(const std::vector<float>& window) {
    // Reuse the FFT implementation from SimpleSpeakerDetectionModel
    size_t N = window.size();
    std::vector<float> result(N * 2, 0.0f);
    
    for (size_t k = 0; k < N; ++k) {
        float realPart = 0.0f;
        float imagPart = 0.0f;
        
        for (size_t n = 0; n < N; ++n) {
            float angle = -2.0f * M_PI * k * n / N;
            realPart += window[n] * std::cos(angle);
            imagPart += window[n] * std::sin(angle);
        }
        
        result[k * 2] = realPart;
        result[k * 2 + 1] = imagPart;
    }
    
    return result;
}

float SimpleSpeakerEmbeddingModel::cosineSimilarity(
    const std::vector<float>& vec1, const std::vector<float>& vec2) {
    
    float dotProduct = 0.0f;
    float norm1 = 0.0f;
    float norm2 = 0.0f;
    
    for (size_t i = 0; i < vec1.size(); ++i) {
        dotProduct += vec1[i] * vec2[i];
        norm1 += vec1[i] * vec1[i];
        norm2 += vec2[i] * vec2[i];
    }
    
    norm1 = std::sqrt(norm1);
    norm2 = std::sqrt(norm2);
    
    if (norm1 == 0.0f || norm2 == 0.0f) {
        return 0.0f;
    }
    
    return dotProduct / (norm1 * norm2);
}

float SimpleSpeakerEmbeddingModel::euclideanDistance(
    const std::vector<float>& vec1, const std::vector<float>& vec2) {
    
    float distance = 0.0f;
    for (size_t i = 0; i < vec1.size(); ++i) {
        float diff = vec1[i] - vec2[i];
        distance += diff * diff;
    }
    
    return std::sqrt(distance);
}

// KMeansSpeakerClustering Implementation

KMeansSpeakerClustering::KMeansSpeakerClustering() : nextClusterId_(1) {
}

std::map<size_t, uint32_t> KMeansSpeakerClustering::clusterSpeakers(
    const std::vector<std::vector<float>>& embeddings, float threshold) {
    
    std::lock_guard<std::mutex> lock(clusteringMutex_);
    
    std::map<size_t, uint32_t> assignments;
    
    if (embeddings.empty()) {
        return assignments;
    }
    
    // Reset clustering state
    clusterCentroids_.clear();
    clusterMembers_.clear();
    nextClusterId_ = 1;
    
    // Assign each embedding to a cluster
    for (size_t i = 0; i < embeddings.size(); ++i) {
        uint32_t clusterId = addEmbedding(embeddings[i], threshold);
        assignments[i] = clusterId;
    }
    
    return assignments;
}

uint32_t KMeansSpeakerClustering::addEmbedding(const std::vector<float>& embedding, float threshold) {
    std::lock_guard<std::mutex> lock(clusteringMutex_);
    
    // Find nearest existing cluster
    uint32_t nearestCluster = findNearestCluster(embedding, threshold);
    
    if (nearestCluster == 0) {
        // Create new cluster
        nearestCluster = nextClusterId_++;
        clusterCentroids_.push_back(embedding);
        clusterMembers_.push_back({});
    } else {
        // Update existing cluster centroid
        updateClusterCentroid(nearestCluster - 1, embedding);
    }
    
    // Add to cluster members
    if (nearestCluster - 1 < clusterMembers_.size()) {
        clusterMembers_[nearestCluster - 1].push_back(clusterMembers_[nearestCluster - 1].size());
    }
    
    return nearestCluster;
}

void KMeansSpeakerClustering::reset() {
    std::lock_guard<std::mutex> lock(clusteringMutex_);
    
    clusterCentroids_.clear();
    clusterMembers_.clear();
    nextClusterId_ = 1;
}

uint32_t KMeansSpeakerClustering::findNearestCluster(const std::vector<float>& embedding, float threshold) {
    if (clusterCentroids_.empty()) {
        return 0; // No clusters exist
    }
    
    uint32_t nearestCluster = 0;
    float minDistance = std::numeric_limits<float>::max();
    
    for (size_t i = 0; i < clusterCentroids_.size(); ++i) {
        float distance = calculateDistance(embedding, clusterCentroids_[i]);
        
        if (distance < minDistance) {
            minDistance = distance;
            nearestCluster = i + 1; // Cluster IDs start from 1
        }
    }
    
    // Check if nearest cluster is within threshold
    if (minDistance > threshold) {
        return 0; // Create new cluster
    }
    
    return nearestCluster;
}

void KMeansSpeakerClustering::updateClusterCentroid(uint32_t clusterId, const std::vector<float>& newEmbedding) {
    if (clusterId >= clusterCentroids_.size()) {
        return;
    }
    
    // Simple moving average update
    const float learningRate = 0.1f;
    
    for (size_t i = 0; i < clusterCentroids_[clusterId].size() && i < newEmbedding.size(); ++i) {
        clusterCentroids_[clusterId][i] = 
            (1.0f - learningRate) * clusterCentroids_[clusterId][i] + 
            learningRate * newEmbedding[i];
    }
}

float KMeansSpeakerClustering::calculateDistance(const std::vector<float>& vec1, const std::vector<float>& vec2) {
    if (vec1.size() != vec2.size()) {
        return std::numeric_limits<float>::max();
    }
    
    float distance = 0.0f;
    for (size_t i = 0; i < vec1.size(); ++i) {
        float diff = vec1[i] - vec2[i];
        distance += diff * diff;
    }
    
    return std::sqrt(distance);
}

std::vector<float> KMeansSpeakerClustering::calculateCentroid(const std::vector<std::vector<float>>& embeddings) {
    if (embeddings.empty()) {
        return {};
    }
    
    std::vector<float> centroid(embeddings[0].size(), 0.0f);
    
    for (const auto& embedding : embeddings) {
        for (size_t i = 0; i < centroid.size() && i < embedding.size(); ++i) {
            centroid[i] += embedding[i];
        }
    }
    
    for (float& val : centroid) {
        val /= embeddings.size();
    }
    
    return centroid;
}

// SpeakerDiarizationEngine Implementation

SpeakerDiarizationEngine::SpeakerDiarizationEngine()
    : maxSpeakers_(DEFAULT_MAX_SPEAKERS)
    , speakerChangeThreshold_(DEFAULT_SPEAKER_CHANGE_THRESHOLD)
    , speakerIdentificationThreshold_(DEFAULT_SPEAKER_IDENTIFICATION_THRESHOLD)
    , profileLearningEnabled_(true)
    , initialized_(false) {
    
    // Initialize components
    detectionModel_ = std::make_unique<SimpleSpeakerDetectionModel>();
    embeddingModel_ = std::make_unique<SimpleSpeakerEmbeddingModel>();
    clustering_ = std::make_unique<KMeansSpeakerClustering>();
}

SpeakerDiarizationEngine::~SpeakerDiarizationEngine() {
    // Cancel all streaming sessions
    std::lock_guard<std::mutex> lock(streamingStatesMutex_);
    streamingStates_.clear();
}

bool SpeakerDiarizationEngine::initialize(const std::string& modelPath) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    try {
        modelPath_ = modelPath;
        
        // Initialize detection model
        if (!detectionModel_->initialize(modelPath + "/detection")) {
            setLastError("Failed to initialize speaker detection model");
            return false;
        }
        
        // Initialize embedding model
        if (!embeddingModel_->initialize(modelPath + "/embedding")) {
            setLastError("Failed to initialize speaker embedding model");
            return false;
        }
        
        // Validate configuration
        if (!validateConfiguration()) {
            setLastError("Invalid configuration");
            return false;
        }
        
        initialized_ = true;
        setLastError("");
        
        return true;
    } catch (const std::exception& e) {
        setLastError("Exception during initialization: " + std::string(e.what()));
        initialized_ = false;
        return false;
    }
}

DiarizationResult SpeakerDiarizationEngine::processSpeakerDiarization(
    const std::vector<float>& audioData, int sampleRate) {
    
    auto startTime = std::chrono::steady_clock::now();
    
    if (!initialized_) {
        setLastError("Engine not initialized");
        return DiarizationResult{};
    }
    
    if (!validateAudioData(audioData, sampleRate)) {
        setLastError("Invalid audio data");
        return DiarizationResult{};
    }
    
    try {
        auto result = processAudioSegment(audioData, sampleRate, false);
        
        auto endTime = std::chrono::steady_clock::now();
        auto processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        updateProcessingStats(result, processingTime);
        
        return result;
    } catch (const std::exception& e) {
        setLastError("Exception during processing: " + std::string(e.what()));
        return DiarizationResult{};
    }
}

bool SpeakerDiarizationEngine::addSpeakerProfile(const SpeakerProfile& profile) {
    std::lock_guard<std::mutex> lock(speakerProfilesMutex_);
    
    if (profile.speakerId == 0 || profile.referenceEmbedding.empty()) {
        setLastError("Invalid speaker profile");
        return false;
    }
    
    knownSpeakers_[profile.speakerId] = profile;
    return true;
}

void SpeakerDiarizationEngine::updateSpeakerProfiles(const DiarizationResult& result) {
    if (!profileLearningEnabled_) {
        return;
    }
    
    learnFromDiarizationResult(result);
}

std::map<uint32_t, SpeakerProfile> SpeakerDiarizationEngine::getSpeakerProfiles() const {
    std::lock_guard<std::mutex> lock(speakerProfilesMutex_);
    return knownSpeakers_;
}

bool SpeakerDiarizationEngine::removeSpeakerProfile(uint32_t speakerId) {
    std::lock_guard<std::mutex> lock(speakerProfilesMutex_);
    
    auto it = knownSpeakers_.find(speakerId);
    if (it != knownSpeakers_.end()) {
        knownSpeakers_.erase(it);
        return true;
    }
    
    return false;
}

void SpeakerDiarizationEngine::clearSpeakerProfiles() {
    std::lock_guard<std::mutex> lock(speakerProfilesMutex_);
    knownSpeakers_.clear();
}

bool SpeakerDiarizationEngine::startStreamingDiarization(uint32_t utteranceId) {
    std::lock_guard<std::mutex> lock(streamingStatesMutex_);
    
    if (streamingStates_.find(utteranceId) != streamingStates_.end()) {
        setLastError("Streaming session already exists for utterance ID");
        return false;
    }
    
    auto state = std::make_unique<StreamingDiarizationState>(utteranceId);
    state->isActive = true;
    
    streamingStates_[utteranceId] = std::move(state);
    
    return true;
}

bool SpeakerDiarizationEngine::addAudioForDiarization(uint32_t utteranceId, 
                                                     const std::vector<float>& audioChunk,
                                                     int sampleRate) {
    
    std::lock_guard<std::mutex> lock(streamingStatesMutex_);
    
    auto it = streamingStates_.find(utteranceId);
    if (it == streamingStates_.end() || !it->second->isActive) {
        setLastError("No active streaming session for utterance ID");
        return false;
    }
    
    if (!validateAudioData(audioChunk, sampleRate)) {
        setLastError("Invalid audio chunk");
        return false;
    }
    
    return processStreamingAudio(*it->second, audioChunk, sampleRate);
}

SpeakerSegment SpeakerDiarizationEngine::getCurrentSpeaker(uint32_t utteranceId) {
    std::lock_guard<std::mutex> lock(streamingStatesMutex_);
    
    auto it = streamingStates_.find(utteranceId);
    if (it == streamingStates_.end() || !it->second->isActive) {
        return SpeakerSegment{};
    }
    
    auto& state = *it->second;
    auto currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - state.startTime).count();
    
    return createSpeakerSegment(state.currentSpeakerId, state.lastChangeTimeMs, 
                               currentTime, state.currentConfidence);
}

DiarizationResult SpeakerDiarizationEngine::finishStreamingDiarization(uint32_t utteranceId) {
    std::lock_guard<std::mutex> lock(streamingStatesMutex_);
    
    auto it = streamingStates_.find(utteranceId);
    if (it == streamingStates_.end()) {
        setLastError("No streaming session found for utterance ID");
        return DiarizationResult{};
    }
    
    auto& state = *it->second;
    state.isActive = false;
    
    // Create final result from streaming state
    DiarizationResult result;
    result.segments = state.segments;
    result.totalSpeakers = std::set<uint32_t>(
        state.segments.begin(), state.segments.end(),
        [](const SpeakerSegment& a, const SpeakerSegment& b) {
            return a.speakerId < b.speakerId;
        }).size();
    
    // Calculate overall confidence
    if (!result.segments.empty()) {
        float totalConfidence = 0.0f;
        for (const auto& segment : result.segments) {
            totalConfidence += segment.confidence;
        }
        result.overallConfidence = totalConfidence / result.segments.size();
    }
    
    // Clean up streaming state
    streamingStates_.erase(it);
    
    return result;
}

void SpeakerDiarizationEngine::cancelStreamingDiarization(uint32_t utteranceId) {
    std::lock_guard<std::mutex> lock(streamingStatesMutex_);
    
    auto it = streamingStates_.find(utteranceId);
    if (it != streamingStates_.end()) {
        streamingStates_.erase(it);
    }
}

void SpeakerDiarizationEngine::setMaxSpeakers(size_t maxSpeakers) {
    std::lock_guard<std::mutex> lock(configMutex_);
    maxSpeakers_ = maxSpeakers;
}

void SpeakerDiarizationEngine::setSpeakerChangeThreshold(float threshold) {
    std::lock_guard<std::mutex> lock(configMutex_);
    speakerChangeThreshold_ = std::clamp(threshold, 0.0f, 1.0f);
}

void SpeakerDiarizationEngine::setSpeakerIdentificationThreshold(float threshold) {
    std::lock_guard<std::mutex> lock(configMutex_);
    speakerIdentificationThreshold_ = std::clamp(threshold, 0.0f, 1.0f);
}

void SpeakerDiarizationEngine::setProfileLearningEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(configMutex_);
    profileLearningEnabled_ = enabled;
}

std::string SpeakerDiarizationEngine::getLastError() const {
    return lastError_;
}

std::string SpeakerDiarizationEngine::getProcessingStats() const {
    std::lock_guard<std::mutex> lock(stats_.statsMutex);
    
    std::ostringstream oss;
    oss << "{"
        << "\"totalProcessedSegments\":" << stats_.totalProcessedSegments << ","
        << "\"totalDetectedSpeakers\":" << stats_.totalDetectedSpeakers << ","
        << "\"averageConfidence\":" << stats_.averageConfidence << ","
        << "\"totalProcessingTimeMs\":" << stats_.totalProcessingTime.count() << ","
        << "\"activeStreamingSessions\":" << stats_.activeStreamingSessions << ","
        << "\"profileLearningEvents\":" << stats_.profileLearningEvents
        << "}";
    
    return oss.str();
}

void SpeakerDiarizationEngine::reset() {
    std::lock_guard<std::mutex> configLock(configMutex_);
    std::lock_guard<std::mutex> profilesLock(speakerProfilesMutex_);
    std::lock_guard<std::mutex> streamingLock(streamingStatesMutex_);
    
    // Clear all state
    knownSpeakers_.clear();
    streamingStates_.clear();
    clustering_->reset();
    
    // Reset statistics
    {
        std::lock_guard<std::mutex> statsLock(stats_.statsMutex);
        stats_ = ProcessingStats{};
    }
    
    setLastError("");
}

// Private helper methods implementation continues in next part...// Private 
helper methods implementation

DiarizationResult SpeakerDiarizationEngine::processAudioSegment(
    const std::vector<float>& audioData, int sampleRate, bool isStreaming) {
    
    DiarizationResult result;
    
    // Detect speaker segments
    auto segments = detectSpeakerSegments(audioData, sampleRate);
    result.segments = segments;
    
    // Count unique speakers
    std::set<uint32_t> uniqueSpeakers;
    for (const auto& segment : segments) {
        uniqueSpeakers.insert(segment.speakerId);
    }
    result.totalSpeakers = uniqueSpeakers.size();
    
    // Calculate overall confidence
    if (!segments.empty()) {
        float totalConfidence = 0.0f;
        for (const auto& segment : segments) {
            totalConfidence += segment.confidence;
        }
        result.overallConfidence = totalConfidence / segments.size();
    }
    
    // Check for new speakers
    {
        std::lock_guard<std::mutex> lock(speakerProfilesMutex_);
        for (uint32_t speakerId : uniqueSpeakers) {
            if (knownSpeakers_.find(speakerId) == knownSpeakers_.end()) {
                result.hasNewSpeakers = true;
                break;
            }
        }
    }
    
    // Add detected speakers to result
    for (uint32_t speakerId : uniqueSpeakers) {
        std::lock_guard<std::mutex> lock(speakerProfilesMutex_);
        auto it = knownSpeakers_.find(speakerId);
        if (it != knownSpeakers_.end()) {
            result.detectedSpeakers[speakerId] = it->second;
        }
    }
    
    return result;
}

std::vector<SpeakerSegment> SpeakerDiarizationEngine::detectSpeakerSegments(
    const std::vector<float>& audioData, int sampleRate) {
    
    std::vector<SpeakerSegment> segments;
    
    // Detect speaker change points
    auto changePoints = detectionModel_->detectSpeakerChanges(audioData, sampleRate);
    
    // Add start and end points
    std::vector<int64_t> allPoints = {0};
    allPoints.insert(allPoints.end(), changePoints.begin(), changePoints.end());
    allPoints.push_back(static_cast<int64_t>(audioData.size() * 1000 / sampleRate));
    
    // Create segments between change points
    for (size_t i = 0; i < allPoints.size() - 1; ++i) {
        int64_t startMs = allPoints[i];
        int64_t endMs = allPoints[i + 1];
        
        // Extract audio for this segment
        size_t startSample = static_cast<size_t>(startMs * sampleRate / 1000);
        size_t endSample = static_cast<size_t>(endMs * sampleRate / 1000);
        
        if (startSample >= audioData.size() || endSample > audioData.size() || startSample >= endSample) {
            continue;
        }
        
        std::vector<float> segmentAudio(audioData.begin() + startSample, audioData.begin() + endSample);
        
        // Generate embedding for this segment
        auto embedding = embeddingModel_->generateEmbedding(segmentAudio, sampleRate);
        
        // Identify speaker
        uint32_t speakerId = identifySpeaker(embedding);
        float confidence = 0.8f; // Default confidence
        
        // Create segment
        SpeakerSegment segment;
        segment.speakerId = speakerId;
        segment.speakerLabel = "Speaker_" + std::to_string(speakerId);
        segment.startTimeMs = startMs;
        segment.endTimeMs = endMs;
        segment.confidence = confidence;
        segment.speakerEmbedding = embedding;
        
        segments.push_back(segment);
    }
    
    return segments;
}

uint32_t SpeakerDiarizationEngine::identifySpeaker(const std::vector<float>& embedding) {
    uint32_t speakerId = 0;
    float confidence = 0.0f;
    
    // First try to match with known speakers
    if (isKnownSpeaker(embedding, speakerId, confidence)) {
        if (confidence >= speakerIdentificationThreshold_) {
            return speakerId;
        }
    }
    
    // If no known speaker match, assign to cluster
    return assignSpeakerToCluster(embedding);
}

uint32_t SpeakerDiarizationEngine::assignSpeakerToCluster(const std::vector<float>& embedding) {
    // Use clustering to assign speaker ID
    uint32_t clusterId = clustering_->addEmbedding(embedding, speakerChangeThreshold_);
    
    // Map cluster ID to speaker ID (for now, they're the same)
    return clusterId;
}

void SpeakerDiarizationEngine::updateSpeakerProfile(uint32_t speakerId, const std::vector<float>& embedding) {
    std::lock_guard<std::mutex> lock(speakerProfilesMutex_);
    
    auto it = knownSpeakers_.find(speakerId);
    if (it != knownSpeakers_.end()) {
        // Update existing profile with moving average
        const float learningRate = 0.1f;
        
        for (size_t i = 0; i < it->second.referenceEmbedding.size() && i < embedding.size(); ++i) {
            it->second.referenceEmbedding[i] = 
                (1.0f - learningRate) * it->second.referenceEmbedding[i] + 
                learningRate * embedding[i];
        }
        
        it->second.utteranceCount++;
    } else {
        // Create new profile
        SpeakerProfile profile;
        profile.speakerId = speakerId;
        profile.speakerLabel = "Speaker_" + std::to_string(speakerId);
        profile.referenceEmbedding = embedding;
        profile.confidence = 0.8f;
        profile.utteranceCount = 1;
        
        knownSpeakers_[speakerId] = profile;
    }
}

bool SpeakerDiarizationEngine::validateAudioData(const std::vector<float>& audioData, int sampleRate) {
    if (audioData.empty()) {
        return false;
    }
    
    if (sampleRate <= 0 || sampleRate > 48000) {
        return false;
    }
    
    // Check minimum audio length
    size_t audioLengthMs = audioData.size() * 1000 / sampleRate;
    if (audioLengthMs < MIN_AUDIO_LENGTH_MS) {
        return false;
    }
    
    return true;
}

void SpeakerDiarizationEngine::setLastError(const std::string& error) {
    lastError_ = error;
}

void SpeakerDiarizationEngine::updateProcessingStats(const DiarizationResult& result, 
                                                     std::chrono::milliseconds processingTime) {
    std::lock_guard<std::mutex> lock(stats_.statsMutex);
    
    stats_.totalProcessedSegments += result.segments.size();
    stats_.totalDetectedSpeakers = std::max(stats_.totalDetectedSpeakers, result.totalSpeakers);
    
    // Update average confidence
    if (result.segments.size() > 0) {
        float totalConfidence = stats_.averageConfidence * stats_.totalProcessedSegments;
        totalConfidence += result.overallConfidence * result.segments.size();
        stats_.averageConfidence = totalConfidence / (stats_.totalProcessedSegments + result.segments.size());
    }
    
    stats_.totalProcessingTime += processingTime;
    stats_.activeStreamingSessions = streamingStates_.size();
}

bool SpeakerDiarizationEngine::processStreamingAudio(StreamingDiarizationState& state, 
                                                     const std::vector<float>& audioChunk, 
                                                     int sampleRate) {
    
    // Add audio to buffer
    state.audioBuffer.insert(state.audioBuffer.end(), audioChunk.begin(), audioChunk.end());
    
    // Check for speaker changes in the new audio
    detectSpeakerChangeInStream(state, audioChunk, sampleRate);
    
    // Maintain buffer size
    size_t maxBufferSamples = STREAMING_BUFFER_SIZE_MS * sampleRate / 1000;
    if (state.audioBuffer.size() > maxBufferSamples) {
        size_t excessSamples = state.audioBuffer.size() - maxBufferSamples;
        state.audioBuffer.erase(state.audioBuffer.begin(), state.audioBuffer.begin() + excessSamples);
    }
    
    return true;
}

void SpeakerDiarizationEngine::detectSpeakerChangeInStream(StreamingDiarizationState& state, 
                                                          const std::vector<float>& audioChunk, 
                                                          int sampleRate) {
    
    // Generate embedding for current chunk
    auto embedding = embeddingModel_->generateEmbedding(audioChunk, sampleRate);
    
    // Identify speaker for this chunk
    uint32_t detectedSpeakerId = identifySpeaker(embedding);
    
    // Check if speaker changed
    if (detectedSpeakerId != state.currentSpeakerId) {
        // Create segment for previous speaker
        if (state.currentSpeakerId != 0) {
            auto currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - state.startTime).count();
            
            auto segment = createSpeakerSegment(state.currentSpeakerId, state.lastChangeTimeMs, 
                                               currentTime, state.currentConfidence);
            state.segments.push_back(segment);
        }
        
        // Update state for new speaker
        state.currentSpeakerId = detectedSpeakerId;
        state.lastChangeTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - state.startTime).count();
        state.currentConfidence = 0.8f; // Default confidence
    }
}

SpeakerSegment SpeakerDiarizationEngine::createSpeakerSegment(uint32_t speakerId, int64_t startMs, 
                                                             int64_t endMs, float confidence) {
    SpeakerSegment segment;
    segment.speakerId = speakerId;
    segment.speakerLabel = "Speaker_" + std::to_string(speakerId);
    segment.startTimeMs = startMs;
    segment.endTimeMs = endMs;
    segment.confidence = confidence;
    
    return segment;
}

uint32_t SpeakerDiarizationEngine::generateNewSpeakerId() {
    static std::atomic<uint32_t> nextId{1};
    return nextId++;
}

bool SpeakerDiarizationEngine::isKnownSpeaker(const std::vector<float>& embedding, 
                                             uint32_t& speakerId, float& confidence) {
    std::lock_guard<std::mutex> lock(speakerProfilesMutex_);
    
    float bestSimilarity = 0.0f;
    uint32_t bestSpeakerId = 0;
    
    for (const auto& [id, profile] : knownSpeakers_) {
        float similarity = embeddingModel_->calculateSimilarity(embedding, profile.referenceEmbedding);
        
        if (similarity > bestSimilarity) {
            bestSimilarity = similarity;
            bestSpeakerId = id;
        }
    }
    
    if (bestSimilarity >= speakerIdentificationThreshold_) {
        speakerId = bestSpeakerId;
        confidence = bestSimilarity;
        return true;
    }
    
    return false;
}

void SpeakerDiarizationEngine::learnFromDiarizationResult(const DiarizationResult& result) {
    if (!profileLearningEnabled_) {
        return;
    }
    
    for (const auto& segment : result.segments) {
        if (!segment.speakerEmbedding.empty()) {
            updateSpeakerProfile(segment.speakerId, segment.speakerEmbedding);
            
            // Update statistics
            std::lock_guard<std::mutex> lock(stats_.statsMutex);
            stats_.profileLearningEvents++;
        }
    }
}

bool SpeakerDiarizationEngine::validateConfiguration() const {
    if (maxSpeakers_ == 0 || maxSpeakers_ > 100) {
        return false;
    }
    
    if (speakerChangeThreshold_ < 0.0f || speakerChangeThreshold_ > 1.0f) {
        return false;
    }
    
    if (speakerIdentificationThreshold_ < 0.0f || speakerIdentificationThreshold_ > 1.0f) {
        return false;
    }
    
    return validateModelPath(modelPath_);
}

bool SpeakerDiarizationEngine::validateModelPath(const std::string& path) const {
    if (path.empty()) {
        return false;
    }
    
    // Check if path exists or can be created
    try {
        if (!std::filesystem::exists(path)) {
            std::filesystem::create_directories(path);
        }
        return std::filesystem::is_directory(path);
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace advanced
} // namespace stt