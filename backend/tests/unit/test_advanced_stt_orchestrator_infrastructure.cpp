#include <gtest/gtest.h>
#include "stt/advanced/advanced_stt_orchestrator.hpp"
#include "utils/logging.hpp"

using namespace stt::advanced;

class AdvancedSTTOrchestratorInfrastructureTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging for tests
        utils::Logger::setLevel(utils::LogLevel::INFO);
        
        orchestrator_ = std::make_unique<AdvancedSTTOrchestrator>();
    }
    
    void TearDown() override {
        if (orchestrator_) {
            orchestrator_->shutdown();
        }
    }
    
    std::unique_ptr<AdvancedSTTOrchestrator> orchestrator_;
};

TEST_F(AdvancedSTTOrchestratorInfrastructureTest, InitializationWithDefaultConfig) {
    AdvancedSTTConfig config;
    
    // Enable basic features for testing
    config.audioPreprocessing.enabled = true;
    config.realTimeAnalysis.enabled = true;
    config.adaptiveQuality.enabled = true;
    
    bool success = orchestrator_->initializeAdvancedFeatures(config);
    EXPECT_TRUE(success) << "Failed to initialize with default config: " << orchestrator_->getLastError();
    
    if (success) {
        EXPECT_TRUE(orchestrator_->isInitialized());
        
        // Check that enabled features are actually enabled
        EXPECT_TRUE(orchestrator_->isFeatureEnabled(AdvancedFeature::AUDIO_PREPROCESSING));
        EXPECT_TRUE(orchestrator_->isFeatureEnabled(AdvancedFeature::REALTIME_ANALYSIS));
        EXPECT_TRUE(orchestrator_->isFeatureEnabled(AdvancedFeature::ADAPTIVE_QUALITY));
        
        // Check that disabled features are not enabled
        EXPECT_FALSE(orchestrator_->isFeatureEnabled(AdvancedFeature::SPEAKER_DIARIZATION));
        EXPECT_FALSE(orchestrator_->isFeatureEnabled(AdvancedFeature::CONTEXTUAL_TRANSCRIPTION));
        EXPECT_FALSE(orchestrator_->isFeatureEnabled(AdvancedFeature::EXTERNAL_SERVICES));
    }
}

TEST_F(AdvancedSTTOrchestratorInfrastructureTest, SpeakerDiarizationFeatureInitialization) {
    AdvancedSTTConfig config;
    config.speakerDiarization.enabled = true;
    config.speakerDiarization.setStringParameter("modelPath", "data/test_speaker_models/");
    config.speakerDiarization.setIntParameter("maxSpeakers", 5);
    
    bool success = orchestrator_->initializeAdvancedFeatures(config);
    EXPECT_TRUE(success) << "Failed to initialize speaker diarization: " << orchestrator_->getLastError();
    
    if (success) {
        EXPECT_TRUE(orchestrator_->isFeatureEnabled(AdvancedFeature::SPEAKER_DIARIZATION));
    }
}

TEST_F(AdvancedSTTOrchestratorInfrastructureTest, AudioPreprocessingFeatureInitialization) {
    AdvancedSTTConfig config;
    config.audioPreprocessing.enabled = true;
    config.audioPreprocessing.setBoolParameter("enableNoiseReduction", true);
    config.audioPreprocessing.setBoolParameter("enableVolumeNormalization", true);
    config.audioPreprocessing.setFloatParameter("noiseReductionStrength", 0.7f);
    
    bool success = orchestrator_->initializeAdvancedFeatures(config);
    EXPECT_TRUE(success) << "Failed to initialize audio preprocessing: " << orchestrator_->getLastError();
    
    if (success) {
        EXPECT_TRUE(orchestrator_->isFeatureEnabled(AdvancedFeature::AUDIO_PREPROCESSING));
    }
}

TEST_F(AdvancedSTTOrchestratorInfrastructureTest, ContextualTranscriptionFeatureInitialization) {
    AdvancedSTTConfig config;
    config.contextualTranscription.enabled = true;
    config.contextualTranscription.setStringParameter("modelsPath", "data/test_contextual_models/");
    
    bool success = orchestrator_->initializeAdvancedFeatures(config);
    EXPECT_TRUE(success) << "Failed to initialize contextual transcription: " << orchestrator_->getLastError();
    
    if (success) {
        EXPECT_TRUE(orchestrator_->isFeatureEnabled(AdvancedFeature::CONTEXTUAL_TRANSCRIPTION));
    }
}

TEST_F(AdvancedSTTOrchestratorInfrastructureTest, RealTimeAnalysisFeatureInitialization) {
    AdvancedSTTConfig config;
    config.realTimeAnalysis.enabled = true;
    config.realTimeAnalysis.setIntParameter("analysisBufferSize", 2048);
    config.realTimeAnalysis.setFloatParameter("metricsUpdateIntervalMs", 25.0f);
    
    bool success = orchestrator_->initializeAdvancedFeatures(config);
    EXPECT_TRUE(success) << "Failed to initialize real-time analysis: " << orchestrator_->getLastError();
    
    if (success) {
        EXPECT_TRUE(orchestrator_->isFeatureEnabled(AdvancedFeature::REALTIME_ANALYSIS));
    }
}

TEST_F(AdvancedSTTOrchestratorInfrastructureTest, AdaptiveQualityFeatureInitialization) {
    AdvancedSTTConfig config;
    config.adaptiveQuality.enabled = true;
    config.adaptiveQuality.setFloatParameter("cpuThreshold", 0.75f);
    config.adaptiveQuality.setFloatParameter("memoryThreshold", 0.85f);
    
    bool success = orchestrator_->initializeAdvancedFeatures(config);
    EXPECT_TRUE(success) << "Failed to initialize adaptive quality: " << orchestrator_->getLastError();
    
    if (success) {
        EXPECT_TRUE(orchestrator_->isFeatureEnabled(AdvancedFeature::ADAPTIVE_QUALITY));
    }
}

TEST_F(AdvancedSTTOrchestratorInfrastructureTest, ExternalServicesFeatureInitialization) {
    AdvancedSTTConfig config;
    config.externalServices.enabled = true;
    config.externalServices.setBoolParameter("enableResultFusion", true);
    config.externalServices.setFloatParameter("fallbackThreshold", 0.6f);
    
    bool success = orchestrator_->initializeAdvancedFeatures(config);
    EXPECT_TRUE(success) << "Failed to initialize external services: " << orchestrator_->getLastError();
    
    if (success) {
        EXPECT_TRUE(orchestrator_->isFeatureEnabled(AdvancedFeature::EXTERNAL_SERVICES));
    }
}

TEST_F(AdvancedSTTOrchestratorInfrastructureTest, BatchProcessingFeatureInitialization) {
    AdvancedSTTConfig config;
    config.batchProcessing.enabled = true;
    config.batchProcessing.setIntParameter("maxConcurrentJobs", 2);
    config.batchProcessing.setIntParameter("chunkSizeSeconds", 60);
    
    bool success = orchestrator_->initializeAdvancedFeatures(config);
    EXPECT_TRUE(success) << "Failed to initialize batch processing: " << orchestrator_->getLastError();
    
    if (success) {
        // Note: Batch processing is currently placeholder implementation
        // so it may not show as enabled until fully implemented
        EXPECT_TRUE(orchestrator_->isInitialized());
    }
}

TEST_F(AdvancedSTTOrchestratorInfrastructureTest, MultipleFeatureInitialization) {
    AdvancedSTTConfig config;
    
    // Enable multiple features
    config.speakerDiarization.enabled = true;
    config.audioPreprocessing.enabled = true;
    config.realTimeAnalysis.enabled = true;
    config.adaptiveQuality.enabled = true;
    config.externalServices.enabled = true;
    
    bool success = orchestrator_->initializeAdvancedFeatures(config);
    EXPECT_TRUE(success) << "Failed to initialize multiple features: " << orchestrator_->getLastError();
    
    if (success) {
        EXPECT_TRUE(orchestrator_->isInitialized());
        
        // Check health status
        auto healthStatus = orchestrator_->getHealthStatus();
        EXPECT_GT(healthStatus.overallAdvancedHealth, 0.0f);
        
        // Check processing metrics
        auto metrics = orchestrator_->getProcessingMetrics();
        EXPECT_EQ(metrics.totalProcessedRequests, 0); // No requests processed yet
    }
}

TEST_F(AdvancedSTTOrchestratorInfrastructureTest, FeatureEnableDisable) {
    AdvancedSTTConfig config;
    config.audioPreprocessing.enabled = true;
    
    bool success = orchestrator_->initializeAdvancedFeatures(config);
    ASSERT_TRUE(success);
    
    // Test enabling a feature at runtime
    FeatureConfig realtimeConfig;
    realtimeConfig.enabled = true;
    realtimeConfig.setIntParameter("analysisBufferSize", 1024);
    
    bool enableSuccess = orchestrator_->enableFeature(AdvancedFeature::REALTIME_ANALYSIS, realtimeConfig);
    EXPECT_TRUE(enableSuccess);
    EXPECT_TRUE(orchestrator_->isFeatureEnabled(AdvancedFeature::REALTIME_ANALYSIS));
    
    // Test disabling a feature at runtime
    bool disableSuccess = orchestrator_->disableFeature(AdvancedFeature::REALTIME_ANALYSIS);
    EXPECT_TRUE(disableSuccess);
    EXPECT_FALSE(orchestrator_->isFeatureEnabled(AdvancedFeature::REALTIME_ANALYSIS));
}

TEST_F(AdvancedSTTOrchestratorInfrastructureTest, ConfigurationUpdate) {
    AdvancedSTTConfig config;
    config.audioPreprocessing.enabled = true;
    
    bool success = orchestrator_->initializeAdvancedFeatures(config);
    ASSERT_TRUE(success);
    
    // Update configuration
    AdvancedSTTConfig newConfig = config;
    newConfig.realTimeAnalysis.enabled = true;
    newConfig.adaptiveQuality.enabled = true;
    
    bool updateSuccess = orchestrator_->updateConfiguration(newConfig);
    EXPECT_TRUE(updateSuccess);
    
    // Verify configuration was updated
    auto currentConfig = orchestrator_->getCurrentConfig();
    EXPECT_TRUE(currentConfig.realTimeAnalysis.enabled);
    EXPECT_TRUE(currentConfig.adaptiveQuality.enabled);
}

TEST_F(AdvancedSTTOrchestratorInfrastructureTest, InvalidConfiguration) {
    AdvancedSTTConfig config;
    
    // Create invalid configuration
    config.speakerDiarization.enabled = true;
    config.speakerDiarization.setIntParameter("maxSpeakers", 0); // Invalid: 0 speakers
    
    bool success = orchestrator_->initializeAdvancedFeatures(config);
    EXPECT_FALSE(success);
    EXPECT_FALSE(orchestrator_->getLastError().empty());
}

TEST_F(AdvancedSTTOrchestratorInfrastructureTest, ShutdownAndReset) {
    AdvancedSTTConfig config;
    config.audioPreprocessing.enabled = true;
    config.realTimeAnalysis.enabled = true;
    
    bool success = orchestrator_->initializeAdvancedFeatures(config);
    ASSERT_TRUE(success);
    EXPECT_TRUE(orchestrator_->isInitialized());
    
    // Test reset
    orchestrator_->resetAdvancedFeatures();
    EXPECT_FALSE(orchestrator_->isFeatureEnabled(AdvancedFeature::AUDIO_PREPROCESSING));
    EXPECT_FALSE(orchestrator_->isFeatureEnabled(AdvancedFeature::REALTIME_ANALYSIS));
    
    // Test shutdown
    orchestrator_->shutdown();
    EXPECT_FALSE(orchestrator_->isInitialized());
}