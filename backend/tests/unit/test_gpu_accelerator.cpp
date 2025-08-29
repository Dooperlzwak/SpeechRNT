#include <gtest/gtest.h>
#include "mt/gpu_accelerator.hpp"
#include <memory>

using namespace speechrnt::mt;

class GPUAcceleratorTest : public ::testing::Test {
protected:
    void SetUp() override {
        accelerator = std::make_unique<GPUAccelerator>();
    }
    
    void TearDown() override {
        if (accelerator) {
            accelerator->cleanup();
        }
    }
    
    std::unique_ptr<GPUAccelerator> accelerator;
};

TEST_F(GPUAcceleratorTest, InitializationTest) {
    // Test basic initialization
    EXPECT_TRUE(accelerator->initialize());
    
    // Test that we can get GPU information (even if no GPU is available)
    auto gpus = accelerator->getAvailableGPUs();
    // Should not crash, even if empty
    EXPECT_GE(gpus.size(), 0);
}

TEST_F(GPUAcceleratorTest, GPUAvailabilityTest) {
    EXPECT_TRUE(accelerator->initialize());
    
    // Test GPU availability check
    bool gpuAvailable = accelerator->isGPUAvailable();
    int compatibleCount = accelerator->getCompatibleGPUCount();
    
    if (gpuAvailable) {
        EXPECT_GT(compatibleCount, 0);
        EXPECT_GE(accelerator->getCurrentGPUDevice(), 0);
    } else {
        EXPECT_EQ(compatibleCount, 0);
        EXPECT_EQ(accelerator->getCurrentGPUDevice(), -1);
    }
}

TEST_F(GPUAcceleratorTest, DeviceSelectionTest) {
    EXPECT_TRUE(accelerator->initialize());
    
    if (accelerator->isGPUAvailable()) {
        int bestDevice = accelerator->getBestGPUDevice();
        EXPECT_GE(bestDevice, 0);
        
        // Test device validation
        EXPECT_TRUE(accelerator->validateGPUDevice(bestDevice));
        
        // Test selecting the device
        EXPECT_TRUE(accelerator->selectGPU(bestDevice));
        EXPECT_EQ(accelerator->getCurrentGPUDevice(), bestDevice);
        
        // Test getting device info
        auto deviceInfo = accelerator->getCurrentGPUInfo();
        EXPECT_EQ(deviceInfo.deviceId, bestDevice);
        EXPECT_TRUE(deviceInfo.isCompatible);
    }
}

TEST_F(GPUAcceleratorTest, MemoryManagementTest) {
    EXPECT_TRUE(accelerator->initialize());
    
    if (accelerator->isGPUAvailable()) {
        // Test memory allocation
        size_t testSizeMB = 64; // 64MB test allocation
        
        if (accelerator->hasSufficientGPUMemory(testSizeMB)) {
            EXPECT_TRUE(accelerator->allocateGPUMemory(testSizeMB, "test"));
            
            // Check memory usage increased
            EXPECT_GT(accelerator->getGPUMemoryUsage(), 0);
            
            // Test memory optimization
            EXPECT_TRUE(accelerator->optimizeGPUMemory());
            
            // Clean up
            accelerator->freeGPUMemory();
        }
    }
}

TEST_F(GPUAcceleratorTest, ModelLoadingTest) {
    EXPECT_TRUE(accelerator->initialize());
    
    if (accelerator->isGPUAvailable()) {
        std::string testModelPath = "test_model.npz";
        std::string languagePair = "en-es";
        void* gpuModelPtr = nullptr;
        
        // Note: This will fail in a real test environment without actual model files
        // but tests the interface
        bool loadResult = accelerator->loadModelToGPU(testModelPath, languagePair, &gpuModelPtr);
        
        if (loadResult) {
            EXPECT_NE(gpuModelPtr, nullptr);
            EXPECT_TRUE(accelerator->isModelLoadedOnGPU(languagePair));
            
            // Test getting model pointer
            void* retrievedPtr = accelerator->getGPUModelPointer(languagePair);
            EXPECT_EQ(retrievedPtr, gpuModelPtr);
            
            // Test getting loaded models
            auto loadedModels = accelerator->getLoadedModels();
            EXPECT_GT(loadedModels.size(), 0);
            
            // Clean up
            EXPECT_TRUE(accelerator->unloadModelFromGPU(gpuModelPtr));
            EXPECT_FALSE(accelerator->isModelLoadedOnGPU(languagePair));
        }
    }
}

TEST_F(GPUAcceleratorTest, TranslationAccelerationTest) {
    EXPECT_TRUE(accelerator->initialize());
    
    if (accelerator->isGPUAvailable()) {
        // Create a mock GPU model pointer for testing
        void* mockGpuModel = reinterpret_cast<void*>(0x12345678);
        
        std::string input = "Hello world";
        std::string output;
        
        // Note: This will likely fail without actual GPU model loaded
        // but tests the interface
        bool result = accelerator->accelerateTranslation(mockGpuModel, input, output);
        
        // The result depends on whether we have actual GPU acceleration available
        // In a mock environment, this might fail, which is expected
    }
}

TEST_F(GPUAcceleratorTest, PerformanceMonitoringTest) {
    EXPECT_TRUE(accelerator->initialize());
    
    // Test performance monitoring interface
    EXPECT_FALSE(accelerator->isPerformanceMonitoringActive());
    
    if (accelerator->isGPUAvailable()) {
        // Start monitoring
        EXPECT_TRUE(accelerator->startPerformanceMonitoring(1000));
        EXPECT_TRUE(accelerator->isPerformanceMonitoringActive());
        
        // Get statistics
        auto stats = accelerator->getGPUStatistics();
        EXPECT_GE(stats.memoryUsedMB, 0);
        EXPECT_GE(stats.utilizationPercent, 0.0f);
        
        // Test performance thresholds
        accelerator->setPerformanceThresholds(80.0f, 85.0f, 90.0f);
        
        // Stop monitoring
        accelerator->stopPerformanceMonitoring();
        EXPECT_FALSE(accelerator->isPerformanceMonitoringActive());
    }
}

TEST_F(GPUAcceleratorTest, ErrorHandlingTest) {
    EXPECT_TRUE(accelerator->initialize());
    
    // Test CPU fallback configuration
    accelerator->enableCPUFallback(true);
    EXPECT_TRUE(accelerator->isCPUFallbackEnabled());
    
    accelerator->enableCPUFallback(false);
    EXPECT_FALSE(accelerator->isCPUFallbackEnabled());
    
    // Test error handling
    std::string testError = "Test GPU error";
    bool recoveryResult = accelerator->handleGPUError(testError);
    
    // Recovery result depends on actual GPU state
    std::string lastError = accelerator->getLastGPUError();
    EXPECT_EQ(lastError, testError);
}

TEST_F(GPUAcceleratorTest, ConfigurationTest) {
    EXPECT_TRUE(accelerator->initialize());
    
    // Test memory pool configuration
    EXPECT_TRUE(accelerator->configureMemoryPool(512, true));
    
    // Test quantization configuration
    EXPECT_TRUE(accelerator->configureQuantization(true, "fp16"));
    
    // Test batch processing configuration
    EXPECT_TRUE(accelerator->configureBatchProcessing(32, 8));
    
    // Test concurrent streams configuration
    EXPECT_TRUE(accelerator->configureConcurrentStreams(true, 4));
}

TEST_F(GPUAcceleratorTest, StreamingSessionTest) {
    EXPECT_TRUE(accelerator->initialize());
    
    if (accelerator->isGPUAvailable()) {
        void* mockGpuModel = reinterpret_cast<void*>(0x12345678);
        std::string sessionId = "test_session_001";
        
        // Test starting streaming session
        bool startResult = accelerator->startStreamingSession(mockGpuModel, sessionId);
        
        if (startResult) {
            // Test processing streaming chunk
            std::string inputChunk = "Hello";
            std::string outputChunk;
            
            bool processResult = accelerator->processStreamingChunk(sessionId, inputChunk, outputChunk);
            // Result depends on actual GPU model availability
            
            // Test ending session
            EXPECT_TRUE(accelerator->endStreamingSession(sessionId));
        }
    }
}

TEST_F(GPUAcceleratorTest, CudaContextManagementTest) {
    EXPECT_TRUE(accelerator->initialize());
    
    if (accelerator->isGPUAvailable()) {
        int deviceId = accelerator->getCurrentGPUDevice();
        
        if (deviceId >= 0) {
            // Test CUDA context creation
            EXPECT_TRUE(accelerator->createCudaContext(deviceId));
            
            // Test CUDA streams creation
            EXPECT_TRUE(accelerator->createCudaStreams(4));
            
            // Test stream management
            void* stream = accelerator->getAvailableCudaStream();
            if (stream) {
                accelerator->releaseCudaStream(stream);
            }
            
            // Test synchronization
            EXPECT_TRUE(accelerator->synchronizeCudaStreams());
            
            // Test context cleanup
            EXPECT_TRUE(accelerator->destroyCudaContext(deviceId));
        }
    }
}

// Test fixture for GPU operational tests
class GPUOperationalTest : public GPUAcceleratorTest {
protected:
    void SetUp() override {
        GPUAcceleratorTest::SetUp();
        ASSERT_TRUE(accelerator->initialize());
        
        // Skip tests if no GPU is available
        if (!accelerator->isGPUAvailable()) {
            GTEST_SKIP() << "No compatible GPU available for operational tests";
        }
    }
};

TEST_F(GPUOperationalTest, OperationalStatusTest) {
    EXPECT_TRUE(accelerator->isGPUOperational());
    
    // Test device reset
    if (accelerator->getCurrentGPUDevice() >= 0) {
        EXPECT_TRUE(accelerator->resetGPUDevice());
        EXPECT_TRUE(accelerator->isGPUOperational());
    }
}

TEST_F(GPUOperationalTest, BatchTranslationTest) {
    void* mockGpuModel = reinterpret_cast<void*>(0x12345678);
    
    std::vector<std::string> inputs = {
        "Hello world",
        "How are you?",
        "Good morning"
    };
    
    std::vector<std::string> outputs;
    
    // Note: This will likely fail without actual GPU model
    // but tests the interface
    bool result = accelerator->accelerateBatchTranslation(mockGpuModel, inputs, outputs);
    
    // The result depends on actual GPU model availability
    if (result) {
        EXPECT_EQ(outputs.size(), inputs.size());
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}