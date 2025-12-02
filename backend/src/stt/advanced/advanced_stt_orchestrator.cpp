#include "stt/advanced/advanced_stt_orchestrator.hpp"
#include "stt/advanced/advanced_health_monitoring.hpp"
#include "stt/advanced/speaker_diarization_engine.hpp"
#include "stt/advanced/contextual_transcriber.hpp"
#include "stt/advanced/adaptive_quality_manager.hpp"
#include "stt/advanced/external_service_integrator.hpp"
#include "audio/audio_preprocessor.hpp"
#include "audio/realtime_audio_analyzer.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <sstream>

// Logging macros for convenience
#define LOG_INFO(msg) speechrnt::utils::Logger::info(msg)
#define LOG_WARNING(msg) speechrnt::utils::Logger::warn(msg)
#define LOG_ERROR(msg) speechrnt::utils::Logger::error(msg)
#define LOG_DEBUG(msg) speechrnt::utils::Logger::debug(msg)

namespace stt {
namespace advanced {

/**
 * Adapter class to bridge audio::AudioPreprocessor to AudioPreprocessorInterface
 */
class AudioPreprocessorAdapter : public AudioPreprocessorInterface {
private:
    std::unique_ptr<audio::AudioPreprocessor> impl_;
    
public:
    explicit AudioPreprocessorAdapter(std::unique_ptr<audio::AudioPreprocessor> impl)
        : impl_(std::move(impl)) {}
    
    bool initialize() override {
        return impl_ && impl_->isInitialized();
    }
    
    PreprocessingResult preprocessAudio(const std::vector<float>& audioData) override {
        PreprocessingResult result;
        if (!impl_) {
            result.processedAudio = audioData;
            result.processingLatencyMs = 0.0f;
            return result;
        }
        
        auto start = std::chrono::steady_clock::now();
        
        // Process audio through the implementation
        auto implResult = impl_->preprocessAudio(audioData);
        
        auto end = std::chrono::steady_clock::now();
        float latencyMs = std::chrono::duration<float, std::milli>(end - start).count();
        
        result.processedAudio = implResult.processedAudio;
        result.processingLatencyMs = latencyMs;
        
        // Convert quality metrics
        result.qualityMetrics.signalToNoiseRatio = implResult.qualityAfter.signalToNoiseRatio;
        result.qualityMetrics.spectralCentroid = implResult.qualityAfter.spectralCentroid;
        result.qualityMetrics.zeroCrossingRate = implResult.qualityAfter.zeroCrossingRate;
        result.qualityMetrics.spectralRolloff = implResult.qualityAfter.spectralRolloff;
        result.qualityMetrics.overallQuality = implResult.qualityAfter.overallQuality;
        result.qualityMetrics.hasEcho = implResult.qualityAfter.hasEcho;
        result.qualityMetrics.hasNoise = implResult.qualityAfter.hasNoise;
        
        // Convert applied filters from string names
        for (const auto& filterName : implResult.appliedFilters) {
            if (filterName == "noise_reduction") {
                result.appliedFilters.push_back(PreprocessingType::NOISE_REDUCTION);
            } else if (filterName == "volume_normalization") {
                result.appliedFilters.push_back(PreprocessingType::VOLUME_NORMALIZATION);
            } else if (filterName == "echo_cancellation") {
                result.appliedFilters.push_back(PreprocessingType::ECHO_CANCELLATION);
            }
        }
        
        return result;
    }
    
    AudioQualityMetrics analyzeAudioQuality(const std::vector<float>& audioData) override {
        AudioQualityMetrics metrics;
        if (!impl_) {
            return metrics;
        }
        
        auto qualityMetrics = impl_->analyzeAudioQuality(audioData);
        
        metrics.signalToNoiseRatio = qualityMetrics.signalToNoiseRatio;
        metrics.spectralCentroid = qualityMetrics.spectralCentroid;
        metrics.zeroCrossingRate = qualityMetrics.zeroCrossingRate;
        metrics.spectralRolloff = qualityMetrics.spectralRolloff;
        metrics.overallQuality = qualityMetrics.overallQuality;
        metrics.hasEcho = qualityMetrics.hasEcho;
        metrics.hasNoise = qualityMetrics.hasNoise;
        
        return metrics;
    }
    
    void setAdaptiveMode(bool enabled) override {
        if (impl_) {
            impl_->enableAdaptiveMode(enabled);
        }
    }
    
    void updatePreprocessingParameters(const AudioQualityMetrics& metrics) override {
        if (impl_) {
            audio::AudioQualityMetrics implMetrics;
            implMetrics.signalToNoiseRatio = metrics.signalToNoiseRatio;
            implMetrics.spectralCentroid = metrics.spectralCentroid;
            implMetrics.zeroCrossingRate = metrics.zeroCrossingRate;
            implMetrics.spectralRolloff = metrics.spectralRolloff;
            implMetrics.overallQuality = metrics.overallQuality;
            implMetrics.hasEcho = metrics.hasEcho;
            implMetrics.hasNoise = metrics.hasNoise;
            
            impl_->adaptParametersForQuality(implMetrics);
        }
    }
    
    std::vector<float> processAudioChunk(const std::vector<float>& chunk) override {
        if (!impl_) {
            return chunk;
        }
        return impl_->preprocessAudioSimple(chunk);
    }
    
    void resetProcessingState() override {
        if (impl_) {
            impl_->resetRealTimeState();
        }
    }
    
    bool isInitialized() const override {
        return impl_ && impl_->isInitialized();
    }
    
    std::string getLastError() const override {
        return impl_ ? impl_->getLastError() : "AudioPreprocessor not initialized";
    }
};

/**
 * Adapter class to bridge audio::RealTimeAudioAnalyzer to RealTimeAudioAnalyzerInterface
 */
class RealTimeAudioAnalyzerAdapter : public RealTimeAudioAnalyzerInterface {
private:
    std::unique_ptr<audio::RealTimeAudioAnalyzer> impl_;
    
public:
    explicit RealTimeAudioAnalyzerAdapter(std::unique_ptr<audio::RealTimeAudioAnalyzer> impl)
        : impl_(std::move(impl)) {}
    
    bool initialize(int sampleRate, size_t bufferSize) override {
        return impl_ && impl_->isInitialized();
    }
    
    void processAudioSample(float sample) override {
        if (impl_) {
            impl_->processAudioSample(sample);
        }
    }
    
    void processAudioChunk(const std::vector<float>& chunk) override {
        if (impl_) {
            impl_->processAudioChunk(chunk);
        }
    }
    
    RealTimeMetrics getCurrentMetrics() const override {
        RealTimeMetrics metrics;
        if (!impl_) {
            return metrics;
        }
        
        auto implMetrics = impl_->getCurrentMetrics();
        
        // Convert metrics
        metrics.levels.currentLevel = implMetrics.currentLevel;
        metrics.levels.peakLevel = implMetrics.peakLevel;
        metrics.levels.averageLevel = implMetrics.averageLevel;
        metrics.levels.clipping = implMetrics.clipping;
        metrics.levels.silence = implMetrics.silence;
        
        metrics.spectral.dominantFrequency = implMetrics.dominantFrequency;
        metrics.spectral.spectralCentroid = implMetrics.spectralCentroid;
        metrics.spectral.spectralBandwidth = implMetrics.spectralBandwidth;
        metrics.spectral.spectralRolloff = implMetrics.spectralRolloff;
        
        metrics.noiseLevel = implMetrics.noiseLevel;
        metrics.speechProbability = implMetrics.speechProbability;
        metrics.timestampMs = implMetrics.timestampMs;
        
        return metrics;
    }
    
    std::vector<RealTimeMetrics> getMetricsHistory(size_t samples) const override {
        std::vector<RealTimeMetrics> history;
        if (!impl_) {
            return history;
        }
        
        auto implHistory = impl_->getMetricsHistory(samples);
        history.reserve(implHistory.size());
        
        for (const auto& implMetrics : implHistory) {
            RealTimeMetrics metrics;
            metrics.levels.currentLevel = implMetrics.currentLevel;
            metrics.levels.peakLevel = implMetrics.peakLevel;
            metrics.levels.averageLevel = implMetrics.averageLevel;
            metrics.levels.clipping = implMetrics.clipping;
            metrics.levels.silence = implMetrics.silence;
            
            metrics.spectral.dominantFrequency = implMetrics.dominantFrequency;
            metrics.spectral.spectralCentroid = implMetrics.spectralCentroid;
            metrics.spectral.spectralBandwidth = implMetrics.spectralBandwidth;
            metrics.spectral.spectralRolloff = implMetrics.spectralRolloff;
            
            metrics.noiseLevel = implMetrics.noiseLevel;
            metrics.speechProbability = implMetrics.speechProbability;
            metrics.timestampMs = implMetrics.timestampMs;
            
            history.push_back(metrics);
        }
        
        return history;
    }
    
    void registerMetricsCallback(std::function<void(const RealTimeMetrics&)> callback) override {
        if (impl_) {
            // Create adapter callback that converts metrics
            auto adapterCallback = [callback](const audio::RealTimeMetrics& implMetrics) {
                RealTimeMetrics metrics;
                metrics.levels.currentLevel = implMetrics.currentLevel;
                metrics.levels.peakLevel = implMetrics.peakLevel;
                metrics.levels.averageLevel = implMetrics.averageLevel;
                metrics.levels.clipping = implMetrics.clipping;
                metrics.levels.silence = implMetrics.silence;
                
                metrics.spectral.dominantFrequency = implMetrics.dominantFrequency;
                metrics.spectral.spectralCentroid = implMetrics.spectralCentroid;
                metrics.spectral.spectralBandwidth = implMetrics.spectralBandwidth;
                metrics.spectral.spectralRolloff = implMetrics.spectralRolloff;
                
                metrics.noiseLevel = implMetrics.noiseLevel;
                metrics.speechProbability = implMetrics.speechProbability;
                metrics.timestampMs = implMetrics.timestampMs;
                
                callback(metrics);
            };
            
            impl_->registerMetricsCallback(adapterCallback);
        }
    }
    
    void enableRealTimeEffects(bool enabled) override {
        if (impl_) {
            impl_->enableRealTimeEffects(enabled);
        }
    }
    
    std::vector<float> applyRealTimeEffects(const std::vector<float>& audio) override {
        if (!impl_) {
            return audio;
        }
        return impl_->applyRealTimeEffects(audio);
    }
    
    bool isInitialized() const override {
        return impl_ && impl_->isInitialized();
    }
    
    std::string getLastError() const override {
        return impl_ ? impl_->getLastError() : "RealTimeAudioAnalyzer not initialized";
    }
};

} // namespace advanced
} // namespace stt

namespace stt {
namespace advanced {

AdvancedSTTOrchestrator::AdvancedSTTOrchestrator()
    : initialized_(false) {
    // Initialize processing metrics
    processingMetrics_ = ProcessingMetrics{};
}

AdvancedSTTOrchestrator::~AdvancedSTTOrchestrator() {
    shutdown();
}

bool AdvancedSTTOrchestrator::initializeAdvancedFeatures(const AdvancedSTTConfig& config) {
    std::lock_guard<std::mutex> lock(orchestratorMutex_);
    
    if (initialized_) {
        LOG_WARNING("Advanced STT Orchestrator already initialized");
        return true;
    }
    
    // Validate configuration
    if (!validateConfiguration(config)) {
        lastError_ = "Invalid advanced STT configuration";
        LOG_ERROR(lastError_);
        return false;
    }
    
    config_ = config;
    
    try {
        // Initialize core STT components if not already done
        if (!whisperSTT_) {
            whisperSTT_ = std::make_shared<WhisperSTT>();
            if (!whisperSTT_->initialize("data/whisper/base.bin")) {
                lastError_ = "Failed to initialize Whisper STT";
                LOG_ERROR(lastError_);
                return false;
            }
        }
        
        if (!transcriptionManager_) {
            transcriptionManager_ = std::make_shared<TranscriptionManager>();
            if (!transcriptionManager_->initialize("data/whisper/base.bin")) {
                lastError_ = "Failed to initialize Transcription Manager";
                LOG_ERROR(lastError_);
                return false;
            }
        }
        
        // Initialize processing pipeline
        pipeline_ = std::make_unique<AdvancedProcessingPipeline>();
        PipelineConfig pipelineConfig;
        pipelineConfig.enableParallelProcessing = true;
        pipelineConfig.enableStageSkipping = true;
        pipelineConfig.enableProfiling = config_.enableDebugMode;
        
        if (!pipeline_->initialize(pipelineConfig)) {
            lastError_ = "Failed to initialize processing pipeline";
            LOG_ERROR(lastError_);
            return false;
        }
        
        // Initialize advanced features based on configuration
        bool allFeaturesInitialized = true;
        
        // Initialize enabled features
        if (config_.speakerDiarization.enabled) {
            FeatureConfig featureConfig = config_.speakerDiarization;
            if (!enableFeature(AdvancedFeature::SPEAKER_DIARIZATION, featureConfig)) {
                LOG_WARNING("Failed to initialize speaker diarization feature");
                allFeaturesInitialized = false;
            }
        }
        
        if (config_.audioPreprocessing.enabled) {
            FeatureConfig featureConfig = config_.audioPreprocessing;
            if (!enableFeature(AdvancedFeature::AUDIO_PREPROCESSING, featureConfig)) {
                LOG_WARNING("Failed to initialize audio preprocessing feature");
                allFeaturesInitialized = false;
            }
        }
        
        if (config_.contextualTranscription.enabled) {
            FeatureConfig featureConfig = config_.contextualTranscription;
            if (!enableFeature(AdvancedFeature::CONTEXTUAL_TRANSCRIPTION, featureConfig)) {
                LOG_WARNING("Failed to initialize contextual transcription feature");
                allFeaturesInitialized = false;
            }
        }
        
        if (config_.realTimeAnalysis.enabled) {
            FeatureConfig featureConfig = config_.realTimeAnalysis;
            if (!enableFeature(AdvancedFeature::REALTIME_ANALYSIS, featureConfig)) {
                LOG_WARNING("Failed to initialize real-time analysis feature");
                allFeaturesInitialized = false;
            }
        }
        
        if (config_.adaptiveQuality.enabled) {
            FeatureConfig featureConfig = config_.adaptiveQuality;
            if (!enableFeature(AdvancedFeature::ADAPTIVE_QUALITY, featureConfig)) {
                LOG_WARNING("Failed to initialize adaptive quality feature");
                allFeaturesInitialized = false;
            }
        }
        
        if (config_.externalServices.enabled) {
            FeatureConfig featureConfig = config_.externalServices;
            if (!enableFeature(AdvancedFeature::EXTERNAL_SERVICES, featureConfig)) {
                LOG_WARNING("Failed to initialize external services feature");
                allFeaturesInitialized = false;
            }
        }
        
        if (config_.batchProcessing.enabled) {
            FeatureConfig featureConfig = config_.batchProcessing;
            if (!enableFeature(AdvancedFeature::BATCH_PROCESSING, featureConfig)) {
                LOG_WARNING("Failed to initialize batch processing feature");
                allFeaturesInitialized = false;
            }
        }
        
        // Set component references in pipeline
        pipeline_->setComponents(
            speakerEngine_,
            audioPreprocessor_,
            contextualTranscriber_,
            audioAnalyzer_,
            qualityManager_,
            externalServices_,
            whisperSTT_
        );
        
        initialized_ = true;
        
        LOG_INFO("Advanced STT Orchestrator initialized successfully");
        if (!allFeaturesInitialized) {
            LOG_WARNING("Some advanced features failed to initialize but orchestrator is functional");
        }
        
        return true;
        
    } catch (const std::exception& e) {
        lastError_ = "Exception during initialization: " + std::string(e.what());
        LOG_ERROR(lastError_);
        return false;
    }
}

AdvancedTranscriptionResult AdvancedSTTOrchestrator::processAudioWithAdvancedFeatures(
    const AudioProcessingRequest& request) {
    
    if (!initialized_) {
        AdvancedTranscriptionResult result;
        result.text = "";
        result.confidence = 0.0f;
        lastError_ = "Orchestrator not initialized";
        return result;
    }
    
    try {
        // Process through pipeline
        auto result = processWithPipeline(request);
        
        // Update processing metrics
        updateProcessingMetrics(result);
        
        return result;
        
    } catch (const std::exception& e) {
        lastError_ = "Exception during processing: " + std::string(e.what());
        LOG_ERROR(lastError_);
        
        AdvancedTranscriptionResult result;
        result.text = "";
        result.confidence = 0.0f;
        return result;
    }
}

void AdvancedSTTOrchestrator::processAudioAsync(const AudioProcessingRequest& request) {
    if (!initialized_) {
        if (request.callback) {
            AdvancedTranscriptionResult result;
            result.text = "";
            result.confidence = 0.0f;
            request.callback(result);
        }
        return;
    }
    
    // Process asynchronously using pipeline
    pipeline_->processAudioAsync(request, request.callback);
}

bool AdvancedSTTOrchestrator::enableFeature(AdvancedFeature feature, const FeatureConfig& config) {
    std::lock_guard<std::mutex> lock(orchestratorMutex_);
    
    try {
        bool success = initializeFeature(feature, config);
        if (success) {
            featureStates_[feature] = true;
            featureConfigs_[feature] = config;
            LOG_INFO("Advanced feature enabled: " + std::to_string(static_cast<int>(feature)));
        } else {
            featureStates_[feature] = false;
            LOG_ERROR("Failed to enable advanced feature: " + std::to_string(static_cast<int>(feature)));
        }
        return success;
        
    } catch (const std::exception& e) {
        lastError_ = "Exception enabling feature: " + std::string(e.what());
        LOG_ERROR(lastError_);
        return false;
    }
}

bool AdvancedSTTOrchestrator::disableFeature(AdvancedFeature feature) {
    std::lock_guard<std::mutex> lock(orchestratorMutex_);
    
    try {
        shutdownFeature(feature);
        featureStates_[feature] = false;
        LOG_INFO("Advanced feature disabled: " + std::to_string(static_cast<int>(feature)));
        return true;
        
    } catch (const std::exception& e) {
        lastError_ = "Exception disabling feature: " + std::string(e.what());
        LOG_ERROR(lastError_);
        return false;
    }
}

bool AdvancedSTTOrchestrator::isFeatureEnabled(AdvancedFeature feature) const {
    std::lock_guard<std::mutex> lock(orchestratorMutex_);
    
    auto it = featureStates_.find(feature);
    if (it != featureStates_.end()) {
        return it->second && isFeatureHealthy(feature);
    }
    return false;
}

AdvancedSTTConfig AdvancedSTTOrchestrator::getCurrentConfig() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return config_;
}

bool AdvancedSTTOrchestrator::updateConfiguration(const AdvancedSTTConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    if (!validateConfiguration(config)) {
        lastError_ = "Invalid configuration update";
        return false;
    }
    
    config_ = config;
    updateFeatureStates();
    
    LOG_INFO("Advanced STT configuration updated");
    return true;
}

AdvancedHealthStatus AdvancedSTTOrchestrator::getHealthStatus() const {
    AdvancedHealthStatus status;
    
    // Check each feature health
    status.speakerDiarizationHealthy = isFeatureEnabled(AdvancedFeature::SPEAKER_DIARIZATION);
    status.audioPreprocessingHealthy = isFeatureEnabled(AdvancedFeature::AUDIO_PREPROCESSING);
    status.contextualTranscriptionHealthy = isFeatureEnabled(AdvancedFeature::CONTEXTUAL_TRANSCRIPTION);
    status.realTimeAnalysisHealthy = isFeatureEnabled(AdvancedFeature::REALTIME_ANALYSIS);
    status.adaptiveQualityHealthy = isFeatureEnabled(AdvancedFeature::ADAPTIVE_QUALITY);
    status.externalServicesHealthy = isFeatureEnabled(AdvancedFeature::EXTERNAL_SERVICES);
    status.batchProcessingHealthy = isFeatureEnabled(AdvancedFeature::BATCH_PROCESSING);
    
    // Calculate overall health
    int healthyFeatures = 0;
    int totalFeatures = 7;
    
    if (status.speakerDiarizationHealthy) healthyFeatures++;
    if (status.audioPreprocessingHealthy) healthyFeatures++;
    if (status.contextualTranscriptionHealthy) healthyFeatures++;
    if (status.realTimeAnalysisHealthy) healthyFeatures++;
    if (status.adaptiveQualityHealthy) healthyFeatures++;
    if (status.externalServicesHealthy) healthyFeatures++;
    if (status.batchProcessingHealthy) healthyFeatures++;
    
    status.overallAdvancedHealth = static_cast<float>(healthyFeatures) / totalFeatures;
    
    return status;
}

ProcessingMetrics AdvancedSTTOrchestrator::getProcessingMetrics() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return processingMetrics_;
}

void AdvancedSTTOrchestrator::resetAdvancedFeatures() {
    std::lock_guard<std::mutex> lock(orchestratorMutex_);
    
    // Reset all features
    for (auto& [feature, enabled] : featureStates_) {
        if (enabled) {
            shutdownFeature(feature);
        }
    }
    
    featureStates_.clear();
    featureConfigs_.clear();
    
    // Reset processing metrics
    {
        std::lock_guard<std::mutex> metricsLock(metricsMutex_);
        processingMetrics_ = ProcessingMetrics{};
    }
    
    LOG_INFO("Advanced STT features reset");
}

void AdvancedSTTOrchestrator::shutdown() {
    if (!initialized_) {
        return;
    }
    
    LOG_INFO("Shutting down Advanced STT Orchestrator");
    
    resetAdvancedFeatures();
    
    // Shutdown pipeline
    if (pipeline_) {
        pipeline_->shutdown();
        pipeline_.reset();
    }
    
    // Reset component references
    speakerEngine_.reset();
    audioPreprocessor_.reset();
    contextualTranscriber_.reset();
    audioAnalyzer_.reset();
    qualityManager_.reset();
    externalServices_.reset();
    batchProcessor_.reset();
    
    initialized_ = false;
    
    LOG_INFO("Advanced STT Orchestrator shutdown complete");
}

std::string AdvancedSTTOrchestrator::getLastError() const {
    return lastError_;
}

// Private helper methods

bool AdvancedSTTOrchestrator::initializeFeature(AdvancedFeature feature, const FeatureConfig& config) {
    try {
        switch (feature) {
            case AdvancedFeature::SPEAKER_DIARIZATION: {
                speakerEngine_ = std::make_unique<SpeakerDiarizationEngine>();
                std::string modelPath = config.getStringParameter("modelPath", "data/speaker_models/");
                bool success = speakerEngine_->initialize(modelPath);
                if (success) {
                    LOG_INFO("Speaker diarization feature initialized successfully");
                } else {
                    LOG_ERROR("Failed to initialize speaker diarization engine");
                    speakerEngine_.reset();
                }
                return success;
            }
            
            case AdvancedFeature::AUDIO_PREPROCESSING: {
                // Create audio preprocessor with configuration
                audio::AudioPreprocessingConfig audioConfig;
                audioConfig.enableNoiseReduction = config.getBoolParameter("enableNoiseReduction", true);
                audioConfig.enableVolumeNormalization = config.getBoolParameter("enableVolumeNormalization", true);
                audioConfig.enableEchoCancellation = config.getBoolParameter("enableEchoCancellation", false);
                audioConfig.enableAdaptiveProcessing = config.getBoolParameter("adaptivePreprocessing", true);
                audioConfig.enableQualityAnalysis = true;
                
                // Configure noise reduction
                audioConfig.noiseReduction.spectralSubtractionAlpha = config.getFloatParameter("noiseReductionStrength", 0.5f) * 2.0f;
                audioConfig.noiseReduction.enableSpectralSubtraction = audioConfig.enableNoiseReduction;
                audioConfig.noiseReduction.enableWienerFiltering = audioConfig.enableNoiseReduction;
                
                // Configure volume normalization
                audioConfig.volumeNormalization.enableAGC = audioConfig.enableVolumeNormalization;
                audioConfig.volumeNormalization.enableCompression = audioConfig.enableVolumeNormalization;
                
                // Configure echo cancellation
                audioConfig.echoCancellation.enableLMS = audioConfig.enableEchoCancellation;
                
                auto audioPreprocessorImpl = std::make_unique<audio::AudioPreprocessor>(audioConfig);
                if (audioPreprocessorImpl->initialize()) {
                    // Create adapter to interface
                    audioPreprocessor_ = std::make_unique<AudioPreprocessorAdapter>(std::move(audioPreprocessorImpl));
                    LOG_INFO("Audio preprocessing feature initialized successfully");
                    return true;
                } else {
                    LOG_ERROR("Failed to initialize audio preprocessor");
                    return false;
                }
            }
            
            case AdvancedFeature::CONTEXTUAL_TRANSCRIPTION: {
                contextualTranscriber_ = createContextualTranscriber();
                if (contextualTranscriber_) {
                    std::string modelsPath = config.getStringParameter("modelsPath", "data/contextual_models/");
                    bool success = contextualTranscriber_->initialize(modelsPath);
                    if (success) {
                        LOG_INFO("Contextual transcription feature initialized successfully");
                    } else {
                        LOG_ERROR("Failed to initialize contextual transcriber");
                        contextualTranscriber_.reset();
                    }
                    return success;
                } else {
                    LOG_ERROR("Failed to create contextual transcriber instance");
                    return false;
                }
            }
            
            case AdvancedFeature::REALTIME_ANALYSIS: {
                int sampleRate = 16000;
                size_t bufferSize = config.getIntParameter("analysisBufferSize", 1024);
                
                auto realtimeAnalyzerImpl = std::make_unique<audio::RealTimeAudioAnalyzer>(sampleRate, bufferSize);
                if (realtimeAnalyzerImpl->initialize()) {
                    // Create adapter to interface
                    audioAnalyzer_ = std::make_unique<RealTimeAudioAnalyzerAdapter>(std::move(realtimeAnalyzerImpl));
                    LOG_INFO("Real-time analysis feature initialized successfully");
                    return true;
                } else {
                    LOG_ERROR("Failed to initialize real-time audio analyzer");
                    return false;
                }
            }
            
            case AdvancedFeature::ADAPTIVE_QUALITY: {
                qualityManager_ = std::make_unique<AdaptiveQualityManager>();
                bool success = qualityManager_->initialize();
                if (success) {
                    LOG_INFO("Adaptive quality feature initialized successfully");
                } else {
                    LOG_ERROR("Failed to initialize adaptive quality manager");
                    qualityManager_.reset();
                }
                return success;
            }
            
            case AdvancedFeature::EXTERNAL_SERVICES: {
                externalServices_ = std::make_unique<ExternalServiceIntegrator>();
                bool success = externalServices_->initialize();
                if (success) {
                    LOG_INFO("External services feature initialized successfully");
                } else {
                    LOG_ERROR("Failed to initialize external service integrator");
                    externalServices_.reset();
                }
                return success;
            }
            
            case AdvancedFeature::BATCH_PROCESSING: {
                // For now, create a placeholder batch processor since the interface exists but no concrete implementation
                // This will be implemented in a future task
                LOG_INFO("Batch processing feature initialized (interface only - implementation pending)");
                return true;
            }
            
            default:
                LOG_ERROR("Unknown advanced feature type: " + std::to_string(static_cast<int>(feature)));
                return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during feature initialization: " + std::string(e.what()));
        return false;
    }
}

void AdvancedSTTOrchestrator::shutdownFeature(AdvancedFeature feature) {
    switch (feature) {
        case AdvancedFeature::SPEAKER_DIARIZATION:
            speakerEngine_.reset();
            break;
        case AdvancedFeature::AUDIO_PREPROCESSING:
            audioPreprocessor_.reset();
            break;
        case AdvancedFeature::CONTEXTUAL_TRANSCRIPTION:
            contextualTranscriber_.reset();
            break;
        case AdvancedFeature::REALTIME_ANALYSIS:
            audioAnalyzer_.reset();
            break;
        case AdvancedFeature::ADAPTIVE_QUALITY:
            qualityManager_.reset();
            break;
        case AdvancedFeature::EXTERNAL_SERVICES:
            externalServices_.reset();
            break;
        case AdvancedFeature::BATCH_PROCESSING:
            batchProcessor_.reset();
            break;
    }
}

bool AdvancedSTTOrchestrator::validateConfiguration(const AdvancedSTTConfig& config) const {
    // Basic validation
    if (!config.enableAdvancedFeatures) {
        return true; // Valid to have all features disabled
    }
    
    // Validate individual feature configurations
    if (config.speakerDiarization.enabled) {
        if (config.speakerDiarization.maxSpeakers == 0) {
            return false;
        }
    }
    
    if (config.realTimeAnalysis.enabled) {
        if (config.realTimeAnalysis.analysisBufferSize == 0) {
            return false;
        }
    }
    
    // Add more validation as needed
    return true;
}

void AdvancedSTTOrchestrator::updateFeatureStates() {
    // Update feature states based on current configuration
    // This would typically involve enabling/disabling features as needed
    LOG_INFO("Feature states updated based on new configuration");
}

void AdvancedSTTOrchestrator::updateProcessingMetrics(const AdvancedTranscriptionResult& result) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    processingMetrics_.totalProcessedRequests++;
    
    if (result.confidence > 0.0f) {
        processingMetrics_.successfulRequests++;
    } else {
        processingMetrics_.failedRequests++;
    }
    
    // Update latency metrics
    if (result.processingLatencyMs > 0.0f) {
        if (processingMetrics_.minLatency == 0.0f || result.processingLatencyMs < processingMetrics_.minLatency) {
            processingMetrics_.minLatency = result.processingLatencyMs;
        }
        if (result.processingLatencyMs > processingMetrics_.maxLatency) {
            processingMetrics_.maxLatency = result.processingLatencyMs;
        }
        
        // Update average processing time
        float totalTime = processingMetrics_.averageProcessingTime * (processingMetrics_.totalProcessedRequests - 1);
        processingMetrics_.averageProcessingTime = (totalTime + result.processingLatencyMs) / processingMetrics_.totalProcessedRequests;
    }
    
    // Update confidence metrics
    if (result.confidence > 0.0f) {
        float totalConfidence = processingMetrics_.averageConfidence * (processingMetrics_.successfulRequests - 1);
        processingMetrics_.averageConfidence = (totalConfidence + result.confidence) / processingMetrics_.successfulRequests;
        
        if (result.confidence < 0.5f) {
            processingMetrics_.lowConfidenceResults++;
        }
    }
}

AdvancedTranscriptionResult AdvancedSTTOrchestrator::processWithPipeline(const AudioProcessingRequest& request) {
    if (!pipeline_) {
        AdvancedTranscriptionResult result;
        result.text = "";
        result.confidence = 0.0f;
        lastError_ = "Processing pipeline not initialized";
        return result;
    }
    
    return pipeline_->processAudio(request);
}

void AdvancedSTTOrchestrator::handleFeatureError(AdvancedFeature feature, const std::string& error) {
    LOG_ERROR("Feature error - Feature: " + std::to_string(static_cast<int>(feature)) + ", Error: " + error);
    
    // Disable the problematic feature
    featureStates_[feature] = false;
    
    // Record error in metrics
    std::lock_guard<std::mutex> lock(metricsMutex_);
    processingMetrics_.errorCounts[error]++;
    processingMetrics_.recentErrors.push_back(error);
    
    // Keep only recent errors (last 100)
    if (processingMetrics_.recentErrors.size() > 100) {
        processingMetrics_.recentErrors.erase(processingMetrics_.recentErrors.begin());
    }
}

bool AdvancedSTTOrchestrator::isFeatureHealthy(AdvancedFeature feature) const {
    // In a real implementation, this would check the actual health of the feature
    // For now, we'll just check if it's enabled and initialized
    auto it = featureStates_.find(feature);
    return it != featureStates_.end() && it->second;
}

void AdvancedSTTOrchestrator::logFeatureStatus() const {
    std::ostringstream oss;
    oss << "Advanced STT Feature Status:\n";
    
    for (const auto& [feature, enabled] : featureStates_) {
        oss << "  Feature " << static_cast<int>(feature) << ": " 
            << (enabled ? "ENABLED" : "DISABLED") << "\n";
    }
    
    LOG_INFO(oss.str());
}

} // namespace advanced
} // namespace stt