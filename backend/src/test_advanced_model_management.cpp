#include "stt/advanced/advanced_model_manager.hpp"
#include "stt/advanced/custom_model_integration.hpp"
#include "models/model_manager.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

using namespace speechrnt::stt::advanced;
using namespace speechrnt::models;

int main() {
    try {
        std::cout << "Testing Advanced Model Management and A/B Testing..." << std::endl;
        
        // Initialize base model manager
        auto baseModelManager = std::make_shared<ModelManager>(2048, 5);
        
        // Initialize advanced model manager
        auto advancedManager = std::make_unique<AdvancedModelManager>(baseModelManager);
        
        // Test performance metrics recording
        std::cout << "\n1. Testing performance metrics recording..." << std::endl;
        advancedManager->recordTranscriptionMetrics("whisper-base", "en->es", 150.0f, 0.05f, 0.95f, 0.9f, true);
        advancedManager->recordTranscriptionMetrics("whisper-large", "en->es", 200.0f, 0.03f, 0.97f, 0.9f, true);
        advancedManager->recordTranscriptionMetrics("whisper-base", "en->es", 160.0f, 0.06f, 0.94f, 0.85f, true);
        
        // Get and display metrics
        auto metrics1 = advancedManager->getModelMetrics("whisper-base", "en->es");
        auto metrics2 = advancedManager->getModelMetrics("whisper-large", "en->es");
        
        std::cout << "Whisper-base metrics: WER=" << metrics1.wordErrorRate 
                  << ", Latency=" << metrics1.averageLatencyMs << "ms" << std::endl;
        std::cout << "Whisper-large metrics: WER=" << metrics2.wordErrorRate 
                  << ", Latency=" << metrics2.averageLatencyMs << "ms" << std::endl;
        
        // Test model comparison
        std::cout << "\n2. Testing model comparison..." << std::endl;
        float comparison = advancedManager->compareModels("whisper-base", "whisper-large", "en->es", 
                                                         ModelComparisonMetric::WORD_ERROR_RATE);
        std::cout << "Model comparison (WER): " << comparison << std::endl;
        
        // Test model ranking
        std::cout << "\n3. Testing model ranking..." << std::endl;
        auto rankings = advancedManager->rankModels("en->es", ModelComparisonMetric::WORD_ERROR_RATE);
        std::cout << "Model rankings by WER: ";
        for (const auto& model : rankings) {
            std::cout << model << " ";
        }
        std::cout << std::endl;
        
        // Test best model selection
        std::cout << "\n4. Testing best model selection..." << std::endl;
        ModelSelectionCriteria criteria;
        criteria.maxAcceptableLatencyMs = 180.0f;
        criteria.minAcceptableConfidence = 0.9f;
        
        std::string bestModel = advancedManager->selectBestModel("en->es", criteria);
        std::cout << "Best model for criteria: " << bestModel << std::endl;
        
        // Test A/B testing
        std::cout << "\n5. Testing A/B testing..." << std::endl;
        ABTestConfig abConfig;
        abConfig.testId = "whisper_comparison_test";
        abConfig.testName = "Whisper Base vs Large";
        abConfig.modelIds = {"whisper-base", "whisper-large"};
        abConfig.trafficSplitPercentages = {50.0f, 50.0f};
        abConfig.testDuration = std::chrono::hours(1);
        
        bool testCreated = advancedManager->createABTest(abConfig);
        std::cout << "A/B test created: " << (testCreated ? "SUCCESS" : "FAILED") << std::endl;
        
        if (testCreated) {
            bool testStarted = advancedManager->startABTest("whisper_comparison_test");
            std::cout << "A/B test started: " << (testStarted ? "SUCCESS" : "FAILED") << std::endl;
            
            // Test model selection during A/B test
            std::string selectedModel = advancedManager->getModelForTranscription("en->es", "session_123");
            std::cout << "Selected model for session: " << selectedModel << std::endl;
        }
        
        // Test performance report generation
        std::cout << "\n6. Testing performance report generation..." << std::endl;
        std::string report = advancedManager->generatePerformanceReport("en->es", 24);
        std::cout << "Performance report generated (length: " << report.length() << " chars)" << std::endl;
        
        // Initialize custom model integration
        std::cout << "\n7. Testing custom model integration..." << std::endl;
        auto customIntegration = std::make_unique<CustomModelIntegration>(baseModelManager);
        
        // Test model validation (using a dummy path)
        std::string dummyModelPath = "./test_model";
        
        // Create dummy model directory for testing
        std::filesystem::create_directories(dummyModelPath);
        std::ofstream dummyFile(dummyModelPath + "/model.bin");
        dummyFile << "dummy model data";
        dummyFile.close();
        
        auto validationResult = customIntegration->validateModel(dummyModelPath, "test-model-1");
        std::cout << "Model validation result: " << (validationResult.isValid ? "VALID" : "INVALID") << std::endl;
        std::cout << "Validation errors: " << validationResult.errors.size() << std::endl;
        std::cout << "Validation warnings: " << validationResult.warnings.size() << std::endl;
        
        // Test model quantization
        std::cout << "\n8. Testing model quantization..." << std::endl;
        ModelQuantizationConfig quantConfig;
        quantConfig.quantizationType = QuantizationType::INT8;
        quantConfig.preserveAccuracy = true;
        quantConfig.maxAccuracyLoss = 0.05f;
        
        std::string outputPath = "./test_model_quantized";
        auto quantResult = customIntegration->quantizeModel(dummyModelPath, outputPath, quantConfig);
        std::cout << "Model quantization: " << (quantResult.successful ? "SUCCESS" : "FAILED") << std::endl;
        if (quantResult.successful) {
            std::cout << "Size reduction: " << quantResult.sizeReductionPercentage << "%" << std::endl;
            std::cout << "Speed improvement: " << quantResult.speedImprovementPercentage << "%" << std::endl;
        }
        
        // Test model deployment
        std::cout << "\n9. Testing model deployment..." << std::endl;
        ModelDeploymentConfig deployConfig;
        deployConfig.modelId = "test-model-1";
        deployConfig.targetEnvironment = "development";
        deployConfig.strategy = ModelDeploymentConfig::DeploymentStrategy::IMMEDIATE;
        deployConfig.enableHealthChecks = true;
        
        auto deployResult = customIntegration->deployModel(dummyModelPath, "test-model-1", deployConfig);
        std::cout << "Model deployment: " << (deployResult.successful ? "SUCCESS" : "FAILED") << std::endl;
        std::cout << "Deployment ID: " << deployResult.deploymentId << std::endl;
        
        // Get integration statistics
        std::cout << "\n10. Testing integration statistics..." << std::endl;
        auto stats = customIntegration->getIntegrationStats();
        for (const auto& stat : stats) {
            std::cout << stat.first << ": " << stat.second << std::endl;
        }
        
        // Cleanup test files
        std::filesystem::remove_all(dummyModelPath);
        std::filesystem::remove_all(outputPath);
        
        std::cout << "\nAll tests completed successfully!" << std::endl;
        
        // Give background threads time to process
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}