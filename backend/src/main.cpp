#include <iostream>
#include <string>
#include <memory>
#include "core/websocket_server.hpp"
#include "utils/config.hpp"
#include "utils/logging.hpp"
#include "utils/gpu_manager.hpp"
#include "utils/gpu_config.hpp"
#include "utils/performance_monitor.hpp"

int main(int argc, char* argv[]) {
    try {
        // Initialize logging
        speechrnt::utils::Logger::initialize();
        
        // Initialize GPU manager and configuration
        auto& gpuManager = utils::GPUManager::getInstance();
        auto& gpuConfig = utils::GPUConfigManager::getInstance();
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        
        std::cout << "Initializing GPU acceleration..." << std::endl;
        if (gpuManager.initialize()) {
            if (gpuManager.isCudaAvailable()) {
                std::cout << "CUDA available with " << gpuManager.getDeviceCount() << " device(s)" << std::endl;
                
                // Load GPU configuration
                if (!gpuConfig.loadConfig("config/gpu.json")) {
                    std::cout << "Auto-detecting optimal GPU configuration..." << std::endl;
                    gpuConfig.autoDetectOptimalConfig();
                    gpuConfig.saveConfig("config/gpu.json");
                }
                
                // Display GPU configuration
                auto globalConfig = gpuConfig.getGlobalConfig();
                if (globalConfig.enabled) {
                    auto deviceInfo = gpuManager.getDeviceInfo(globalConfig.deviceId);
                    std::cout << "GPU acceleration enabled on: " << deviceInfo.name 
                              << " (Device " << globalConfig.deviceId << ")" << std::endl;
                    std::cout << "Memory limit: " << globalConfig.memoryLimitMB << "MB" << std::endl;
                }
            } else {
                std::cout << "CUDA not available, running in CPU-only mode" << std::endl;
            }
        } else {
            std::cout << "Failed to initialize GPU manager" << std::endl;
        }
        
        // Initialize performance monitoring
        std::cout << "Initializing performance monitoring..." << std::endl;
        perfMonitor.initialize(true, 1000); // Enable system metrics, 1s interval
        
        // Load configuration
        auto config = utils::Config::load("config/server.json");
        
        // Parse command line arguments
        int port = config.getPort();
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--port" && i + 1 < argc) {
                port = std::stoi(argv[++i]);
            } else if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: " << argv[0] << " [options]\n"
                          << "Options:\n"
                          << "  --port <port>    Set server port (default: 8080)\n"
                          << "  --help, -h       Show this help message\n";
                return 0;
            }
        }
        
        // Create and start WebSocket server
        auto server = std::make_unique<core::WebSocketServer>(port);
        
        std::cout << "Starting SpeechRNT server on port " << port << std::endl;
        server->start();
        
        // Keep server running
        std::cout << "Press Ctrl+C to stop the server" << std::endl;
        server->run();
        
        // Cleanup
        std::cout << "Shutting down..." << std::endl;
        perfMonitor.cleanup();
        gpuManager.cleanup();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        
        // Cleanup on error
        auto& perfMonitor = utils::PerformanceMonitor::getInstance();
        auto& gpuManager = utils::GPUManager::getInstance();
        perfMonitor.cleanup();
        gpuManager.cleanup();
        
        return 1;
    }
    
    return 0;
}