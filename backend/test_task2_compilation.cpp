#include "models/model_manager.hpp"
#include "models/model_quantization.hpp"
#include "utils/gpu_manager.hpp"
#include "utils/performance_monitor.hpp"
#include "utils/logging.hpp"
#include <iostream>

using namespace speechrnt::models;
using namespace speechrnt::utils;

int main() {
    std::cout << "Testing Task 2 compilation..." << std::endl;
    
    try {
        // Test ModelManager
        ModelManager modelManager(1024, 5);
        std::cout << "✓ ModelManager created successfully" << std::endl;
        
        // Test QuantizationManager
        auto& quantManager = QuantizationManager::getInstance();
        quantManager.initialize();
        std::cout << "✓ QuantizationManager initialized successfully" << std::endl;
        
        // Test GPUManager
        auto& gpuManager = GPUManager::getInstance();
        gpuManager.initialize();
        std::cout << "✓ GPUManager initialized successfully" << std::endl;
        
        // Test PerformanceMonitor
        auto& perfMonitor = PerformanceMonitor::getInstance();
        perfMonitor.initialize(true, 1000);
        std::cout << "✓ PerformanceMonitor initialized successfully" << std::endl;
        
        // Test basic functionality
        bool cudaAvailable = gpuManager.isCudaAvailable();
        std::cout << "✓ CUDA availability check: " << (cudaAvailable ? "available" : "not available") << std::endl;
        
        bool nvmlAvailable = gpuManager.isNVMLAvailable();
        std::cout << "✓ NVML availability check: " << (nvmlAvailable ? "available" : "not available") << std::endl;
        
        auto metrics = gpuManager.getDetailedGPUMetrics();
        std::cout << "✓ GPU metrics collection: " << metrics.size() << " metrics" << std::endl;
        
        // Test quantization types
        std::string fp16Str = QuantizationManager::precisionToString(QuantizationPrecision::FP16);
        std::cout << "✓ Quantization string conversion: " << fp16Str << std::endl;
        
        // Test model manager methods
        std::vector<std::string> loadedModels = modelManager.getLoadedModels();
        std::cout << "✓ Model manager query: " << loadedModels.size() << " loaded models" << std::endl;
        
        // Test performance monitoring
        perfMonitor.recordMetric("test.compilation", 1.0, "count");
        auto summary = perfMonitor.getSystemSummary();
        std::cout << "✓ Performance monitoring: " << summary.size() << " summary metrics" << std::endl;
        
        std::cout << "\n🎉 All Task 2 components compiled and initialized successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error during Task 2 testing: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}