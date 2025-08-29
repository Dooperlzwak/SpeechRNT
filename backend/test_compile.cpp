#include "models/model_manager.hpp"
#include <iostream>

int main() {
    try {
        speechrnt::models::ModelManager manager(1024, 5);
        
        // Test basic functionality
        bool result = manager.loadModel("en", "es", "test_path");
        std::cout << "Basic load test: " << (result ? "PASS" : "FAIL") << std::endl;
        
        // Test enhanced functionality
        result = manager.loadModelWithGPU("en", "fr", "test_path", false, -1);
        std::cout << "GPU load test: " << (result ? "PASS" : "FAIL") << std::endl;
        
        // Test quantization
        result = manager.loadModelWithQuantization("en", "de", "test_path", 
                                                  speechrnt::models::QuantizationType::FP16);
        std::cout << "Quantization test: " << (result ? "PASS" : "FAIL") << std::endl;
        
        // Test integrity validation
        result = manager.validateModelIntegrity("test_path");
        std::cout << "Integrity test: " << (result ? "PASS" : "FAIL") << std::endl;
        
        std::cout << "Enhanced ModelManager compilation successful!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}