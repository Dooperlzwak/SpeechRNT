#include "audio/silero_vad_impl.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <stdexcept>

#ifdef SILERO_VAD_AVAILABLE
#include <onnxruntime_cxx_api.h>
#endif

namespace audio {

#ifdef SILERO_VAD_AVAILABLE
/**
 * ONNX Runtime session wrapper for silero-vad model
 */
class SileroVadImpl::OnnxSession {
public:
    OnnxSession() : session_(nullptr) {}
    
    ~OnnxSession() {
        if (session_) {
            delete session_;
        }
    }
    
    bool initialize(const std::string& modelPath) {
        try {
            // Create ONNX Runtime environment
            env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "SileroVAD");
            
            // Configure session options
            Ort::SessionOptions sessionOptions;
            sessionOptions.SetIntraOpNumThreads(1);
            sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
            
            // Create session
            session_ = new Ort::Session(*env_, modelPath.c_str(), sessionOptions);
            
            // Get input/output info
            Ort::AllocatorWithDefaultOptions allocator;
            
            // Input info
            inputName_ = session_->GetInputName(0, allocator);
            auto inputTypeInfo = session_->GetInputTypeInfo(0);
            auto inputTensorInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
            inputShape_ = inputTensorInfo.GetShape();
            
            // Output info  
            outputName_ = session_->GetOutputName(0, allocator);
            auto outputTypeInfo = session_->GetOutputTypeInfo(0);
            auto outputTensorInfo = outputTypeInfo.GetTensorTypeAndShapeInfo();
            outputShape_ = outputTensorInfo.GetShape();
            
            speechrnt::utils::Logger::info("Silero-VAD ONNX model loaded successfully");
            speechrnt::utils::Logger::info("Input shape: [" + std::to_string(inputShape_[0]) + ", " + std::to_string(inputShape_[1]) + "]");
            speechrnt::utils::Logger::info("Output shape: [" + std::to_string(outputShape_[0]) + ", " + std::to_string(outputShape_[1]) + "]");
            
            return true;
            
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Failed to load silero-vad model: " + std::string(e.what()));
            return false;
        }
    }
    
    float runInference(const std::vector<float>& audioData, uint32_t sampleRate) {
        if (!session_) {
            return 0.0f;
        }
        
        try {
            // Prepare input tensors
            Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            
            // Audio input tensor
            std::vector<int64_t> audioShape = {1, static_cast<int64_t>(audioData.size())};
            Ort::Value audioTensor = Ort::Value::CreateTensor<float>(
                memoryInfo, const_cast<float*>(audioData.data()), audioData.size(),
                audioShape.data(), audioShape.size()
            );
            
            // Sample rate input tensor
            std::vector<int64_t> srData = {static_cast<int64_t>(sampleRate)};
            std::vector<int64_t> srShape = {1};
            Ort::Value srTensor = Ort::Value::CreateTensor<int64_t>(
                memoryInfo, srData.data(), srData.size(),
                srShape.data(), srShape.size()
            );
            
            // Prepare input/output names
            const char* inputNames[] = {"input", "sr"};
            const char* outputNames[] = {"output"};
            
            // Run inference
            std::vector<Ort::Value> inputTensors;
            inputTensors.push_back(std::move(audioTensor));
            inputTensors.push_back(std::move(srTensor));
            
            auto outputTensors = session_->Run(Ort::RunOptions{nullptr}, 
                                             inputNames, inputTensors.data(), inputTensors.size(),
                                             outputNames, 1);
            
            // Extract result
            if (!outputTensors.empty()) {
                float* outputData = outputTensors[0].GetTensorMutableData<float>();
                return outputData[0]; // VAD probability
            }
            
            return 0.0f;
            
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Silero-VAD inference failed: " + std::string(e.what()));
            return 0.0f;
        }
    }
    
private:
    std::unique_ptr<Ort::Env> env_;
    Ort::Session* session_;
    std::string inputName_;
    std::string outputName_;
    std::vector<int64_t> inputShape_;
    std::vector<int64_t> outputShape_;
};
#else
// Dummy implementation when ONNX Runtime is not available
class SileroVadImpl::OnnxSession {
public:
    bool initialize(const std::string&) { return false; }
    float runInference(const std::vector<float>&, uint32_t) { return 0.0f; }
};
#endif

// SileroVadImpl implementation
SileroVadImpl::SileroVadImpl() 
    : initialized_(false), sampleRate_(16000), currentMode_(VadMode::HYBRID),
      sileroModelLoaded_(false), energyThreshold_(0.01f), energyHistorySize_(10) {
    
    onnxSession_ = std::make_unique<OnnxSession>();
    energyHistory_.reserve(energyHistorySize_);
    
    // Initialize statistics
    stats_ = {};
}

SileroVadImpl::~SileroVadImpl() {
    shutdown();
}

bool SileroVadImpl::initialize(uint32_t sampleRate, const std::string& modelPath) {
    if (initialized_) {
        speechrnt::utils::Logger::warn("SileroVadImpl already initialized");
        return true;
    }
    
    sampleRate_ = sampleRate;
    modelPath_ = modelPath.empty() ? SILERO_MODEL_PATH : modelPath;
    
    // Try to load silero-vad model
    sileroModelLoaded_ = loadSileroModel(modelPath_);
    
    if (sileroModelLoaded_) {
        speechrnt::utils::Logger::info("SileroVadImpl initialized with ML model");
        if (currentMode_ == VadMode::ENERGY_BASED) {
            currentMode_ = VadMode::HYBRID; // Upgrade to hybrid if model is available
        }
    } else {
        speechrnt::utils::Logger::warn("SileroVadImpl initialized with energy-based fallback only");
        if (currentMode_ == VadMode::SILERO) {
            currentMode_ = VadMode::ENERGY_BASED; // Downgrade to energy-based
        }
    }
    
    initialized_ = true;
    return true;
}

void SileroVadImpl::shutdown() {
    if (!initialized_) {
        return;
    }
    
    unloadSileroModel();
    energyHistory_.clear();
    
    initialized_ = false;
    speechrnt::utils::Logger::info("SileroVadImpl shutdown complete");
}

void SileroVadImpl::setVadMode(VadMode mode) {
    if (mode == VadMode::SILERO && !sileroModelLoaded_) {
        speechrnt::utils::Logger::warn("Cannot set SILERO mode: model not loaded, using HYBRID instead");
        currentMode_ = VadMode::HYBRID;
    } else {
        currentMode_ = mode;
    }
    
    speechrnt::utils::Logger::info("VAD mode set to: " + std::to_string(static_cast<int>(currentMode_)));
}

float SileroVadImpl::processSamples(const std::vector<float>& samples) {
    if (!initialized_ || samples.empty()) {
        return 0.0f;
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    float result = 0.0f;
    bool usedSilero = false;
    
    try {
        switch (currentMode_) {
            case VadMode::SILERO:
                if (sileroModelLoaded_) {
                    result = processSileroVad(samples);
                    usedSilero = true;
                } else {
                    result = processEnergyBasedVad(samples);
                }
                break;
                
            case VadMode::ENERGY_BASED:
                result = processEnergyBasedVad(samples);
                break;
                
            case VadMode::HYBRID:
                if (sileroModelLoaded_) {
                    result = processSileroVad(samples);
                    usedSilero = true;
                    
                    // If silero-vad fails, fallback to energy-based
                    if (result < 0.0f) {
                        result = processEnergyBasedVad(samples);
                        usedSilero = false;
                    }
                } else {
                    result = processEnergyBasedVad(samples);
                }
                break;
        }
        
        // Ensure result is in valid range
        result = std::max(0.0f, std::min(1.0f, result));
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("VAD processing error: " + std::string(e.what()));
        result = processEnergyBasedVad(samples); // Emergency fallback
        usedSilero = false;
    }
    
    // Update statistics
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    double processingTimeMs = duration.count() / 1000.0;
    
    updateStatistics(usedSilero, result, processingTimeMs);
    
    return result;
}

void SileroVadImpl::reset() {
    energyHistory_.clear();
    
    // Reset statistics
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = {};
}

SileroVadImpl::Statistics SileroVadImpl::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void SileroVadImpl::resetStatistics() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = {};
}

// Private methods
bool SileroVadImpl::loadSileroModel(const std::string& modelPath) {
#ifdef SILERO_VAD_AVAILABLE
    try {
        if (onnxSession_->initialize(modelPath)) {
            speechrnt::utils::Logger::info("Silero-VAD model loaded from: " + modelPath);
            return true;
        }
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to load silero-vad model: " + std::string(e.what()));
    }
#else
    speechrnt::utils::Logger::warn("ONNX Runtime not available, cannot load silero-vad model");
#endif
    return false;
}

void SileroVadImpl::unloadSileroModel() {
    if (sileroModelLoaded_) {
        onnxSession_.reset();
        onnxSession_ = std::make_unique<OnnxSession>();
        sileroModelLoaded_ = false;
        speechrnt::utils::Logger::info("Silero-VAD model unloaded");
    }
}

float SileroVadImpl::processSileroVad(const std::vector<float>& samples) {
    if (!sileroModelLoaded_ || !onnxSession_) {
        return -1.0f; // Indicate failure
    }
    
    try {
        // Preprocess audio if needed
        std::vector<float> processedSamples = preprocessAudio(samples);
        
        // Run inference
        float probability = onnxSession_->runInference(processedSamples, sampleRate_);
        
        return probability;
        
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Silero-VAD processing failed: " + std::string(e.what()));
        return -1.0f; // Indicate failure
    }
}

float SileroVadImpl::processEnergyBasedVad(const std::vector<float>& samples) {
    if (samples.empty()) {
        return 0.0f;
    }
    
    // Calculate RMS energy
    float energy = calculateRmsEnergy(samples);
    
    // Update energy history for adaptive thresholding
    energyHistory_.push_back(energy);
    if (energyHistory_.size() > energyHistorySize_) {
        energyHistory_.erase(energyHistory_.begin());
    }
    
    // Calculate adaptive threshold
    float adaptiveThreshold = energyThreshold_;
    if (energyHistory_.size() >= 3) {
        float avgEnergy = 0.0f;
        for (float e : energyHistory_) {
            avgEnergy += e;
        }
        avgEnergy /= energyHistory_.size();
        
        // Adaptive threshold is slightly above average background energy
        adaptiveThreshold = std::max(energyThreshold_, avgEnergy * 1.5f);
    }
    
    // Calculate spectral centroid for additional features
    float spectralCentroid = calculateSpectralCentroid(samples);
    
    // Combine energy and spectral features
    float energyScore = (energy > adaptiveThreshold) ? 
                       std::min(1.0f, energy / adaptiveThreshold) : 0.0f;
    
    float spectralScore = std::min(1.0f, spectralCentroid / 2000.0f); // Normalize by typical speech centroid
    
    // Weighted combination
    float probability = 0.7f * energyScore + 0.3f * spectralScore;
    
    return std::max(0.0f, std::min(1.0f, probability));
}

float SileroVadImpl::calculateRmsEnergy(const std::vector<float>& samples) {
    if (samples.empty()) {
        return 0.0f;
    }
    
    double energy = 0.0;
    for (float sample : samples) {
        energy += sample * sample;
    }
    
    return static_cast<float>(std::sqrt(energy / samples.size()));
}

float SileroVadImpl::calculateSpectralCentroid(const std::vector<float>& samples) {
    if (samples.size() < 2) {
        return 0.0f;
    }
    
    // Simple spectral centroid calculation using magnitude spectrum
    size_t n = samples.size();
    double weightedSum = 0.0;
    double magnitudeSum = 0.0;
    
    for (size_t i = 1; i < n / 2; ++i) {
        // Simple magnitude approximation
        double magnitude = std::abs(samples[i]);
        double frequency = (static_cast<double>(i) * sampleRate_) / n;
        
        weightedSum += frequency * magnitude;
        magnitudeSum += magnitude;
    }
    
    return (magnitudeSum > 0.0) ? static_cast<float>(weightedSum / magnitudeSum) : 0.0f;
}

std::vector<float> SileroVadImpl::preprocessAudio(const std::vector<float>& samples) {
    // For silero-vad, we typically need 512 samples at 16kHz
    const size_t targetSize = 512;
    
    if (samples.size() == targetSize) {
        return samples;
    }
    
    std::vector<float> processed;
    processed.reserve(targetSize);
    
    if (samples.size() > targetSize) {
        // Downsample by taking every nth sample
        float step = static_cast<float>(samples.size()) / targetSize;
        for (size_t i = 0; i < targetSize; ++i) {
            size_t idx = static_cast<size_t>(i * step);
            processed.push_back(samples[idx]);
        }
    } else {
        // Upsample by repeating samples or zero-padding
        processed = samples;
        processed.resize(targetSize, 0.0f);
    }
    
    return processed;
}

std::vector<float> SileroVadImpl::resampleIfNeeded(const std::vector<float>& samples, uint32_t targetSampleRate) {
    if (sampleRate_ == targetSampleRate) {
        return samples;
    }
    
    // Simple resampling (for production, use a proper resampling library)
    float ratio = static_cast<float>(targetSampleRate) / sampleRate_;
    size_t newSize = static_cast<size_t>(samples.size() * ratio);
    
    std::vector<float> resampled;
    resampled.reserve(newSize);
    
    for (size_t i = 0; i < newSize; ++i) {
        float srcIdx = i / ratio;
        size_t idx = static_cast<size_t>(srcIdx);
        
        if (idx < samples.size()) {
            resampled.push_back(samples[idx]);
        } else {
            resampled.push_back(0.0f);
        }
    }
    
    return resampled;
}

void SileroVadImpl::updateStatistics(bool usedSilero, float confidence, double processingTimeMs) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    stats_.totalProcessedChunks++;
    
    if (usedSilero) {
        stats_.sileroSuccessCount++;
    } else {
        stats_.energyFallbackCount++;
    }
    
    // Update running averages
    if (stats_.totalProcessedChunks == 1) {
        stats_.averageProcessingTimeMs = processingTimeMs;
        stats_.averageConfidence = confidence;
    } else {
        double alpha = 0.1; // Smoothing factor
        stats_.averageProcessingTimeMs = (1.0 - alpha) * stats_.averageProcessingTimeMs + alpha * processingTimeMs;
        stats_.averageConfidence = (1.0 - alpha) * stats_.averageConfidence + alpha * confidence;
    }
}

// EnergyBasedVAD implementation
EnergyBasedVAD::EnergyBasedVAD(const Config& config) 
    : config_(config), adaptiveThreshold_(config.energyThreshold) {
    energyHistory_.reserve(100);
    spectralHistory_.reserve(100);
}

void EnergyBasedVAD::configure(const Config& config) {
    config_ = config;
    adaptiveThreshold_ = config.energyThreshold;
}

float EnergyBasedVAD::detectVoiceActivity(const std::vector<float>& samples) {
    if (samples.empty()) {
        return 0.0f;
    }
    
    float energy = calculateEnergy(samples);
    float spectralFeature = config_.useSpectralFeatures ? calculateSpectralFeatures(samples) : 0.0f;
    
    if (config_.useAdaptiveThreshold) {
        updateAdaptiveThreshold(energy);
    }
    
    float threshold = config_.useAdaptiveThreshold ? adaptiveThreshold_ : config_.energyThreshold;
    
    // Combine energy and spectral features
    float energyScore = (energy > threshold) ? std::min(1.0f, energy / threshold) : 0.0f;
    float spectralScore = config_.useSpectralFeatures ? 
                         std::min(1.0f, spectralFeature / config_.spectralThreshold) : 0.0f;
    
    // Weighted combination
    float weight = config_.useSpectralFeatures ? 0.7f : 1.0f;
    float probability = weight * energyScore + (1.0f - weight) * spectralScore;
    
    return std::max(0.0f, std::min(1.0f, probability));
}

void EnergyBasedVAD::reset() {
    energyHistory_.clear();
    spectralHistory_.clear();
    adaptiveThreshold_ = config_.energyThreshold;
}

float EnergyBasedVAD::calculateEnergy(const std::vector<float>& samples) {
    if (samples.empty()) {
        return 0.0f;
    }
    
    double energy = 0.0;
    for (float sample : samples) {
        energy += sample * sample;
    }
    
    return static_cast<float>(std::sqrt(energy / samples.size()));
}

float EnergyBasedVAD::calculateSpectralFeatures(const std::vector<float>& samples) {
    // Simple spectral feature calculation
    // In a full implementation, this would use FFT
    
    if (samples.size() < 2) {
        return 0.0f;
    }
    
    // Calculate zero-crossing rate as a simple spectral feature
    int zeroCrossings = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if ((samples[i] >= 0.0f) != (samples[i-1] >= 0.0f)) {
            zeroCrossings++;
        }
    }
    
    float zcr = static_cast<float>(zeroCrossings) / (samples.size() - 1);
    return zcr * 1000.0f; // Scale to reasonable range
}

void EnergyBasedVAD::updateAdaptiveThreshold(float currentEnergy) {
    energyHistory_.push_back(currentEnergy);
    
    if (energyHistory_.size() > 50) {
        energyHistory_.erase(energyHistory_.begin());
    }
    
    if (energyHistory_.size() >= 10) {
        // Calculate background noise level (lower percentile)
        std::vector<float> sortedEnergy = energyHistory_;
        std::sort(sortedEnergy.begin(), sortedEnergy.end());
        
        size_t percentileIdx = sortedEnergy.size() / 4; // 25th percentile
        float backgroundLevel = sortedEnergy[percentileIdx];
        
        // Adaptive threshold is above background level
        float targetThreshold = backgroundLevel * 2.0f;
        
        // Smooth adaptation
        adaptiveThreshold_ = (1.0f - config_.adaptationRate) * adaptiveThreshold_ + 
                           config_.adaptationRate * targetThreshold;
        
        // Ensure minimum threshold
        adaptiveThreshold_ = std::max(adaptiveThreshold_, config_.energyThreshold);
    }
}

} // namespace audio