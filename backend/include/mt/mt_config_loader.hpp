#pragma once

#include "mt_config.hpp"
#include <string>
#include <memory>
#include <vector>

namespace speechrnt {
namespace mt {

/**
 * Utility class for loading and managing MT configurations
 */
class MTConfigLoader {
public:
    /**
     * Load MT configuration from file with environment overrides
     * @param configPath Path to the main configuration file
     * @param environment Environment name (e.g., "development", "production", "testing")
     * @return Loaded configuration or nullptr on failure
     */
    static std::shared_ptr<MTConfig> loadConfiguration(
        const std::string& configPath, 
        const std::string& environment = "development"
    );
    
    /**
     * Load MT configuration from JSON string
     * @param jsonContent JSON configuration content
     * @param environment Environment name for overrides
     * @return Loaded configuration or nullptr on failure
     */
    static std::shared_ptr<MTConfig> loadConfigurationFromJson(
        const std::string& jsonContent,
        const std::string& environment = "development"
    );
    
    /**
     * Create default MT configuration
     * @param environment Environment name
     * @return Default configuration
     */
    static std::shared_ptr<MTConfig> createDefaultConfiguration(
        const std::string& environment = "development"
    );
    
    /**
     * Validate configuration and return validation errors
     * @param config Configuration to validate
     * @return Vector of validation error messages (empty if valid)
     */
    static std::vector<std::string> validateConfiguration(const MTConfig& config);
    
    /**
     * Merge two configurations (overlay takes precedence)
     * @param base Base configuration
     * @param overlay Configuration to overlay on top
     * @return Merged configuration
     */
    static std::shared_ptr<MTConfig> mergeConfigurations(
        const MTConfig& base,
        const MTConfig& overlay
    );
    
    /**
     * Save configuration to file
     * @param config Configuration to save
     * @param configPath Path to save the configuration
     * @return true if saved successfully
     */
    static bool saveConfiguration(
        const MTConfig& config,
        const std::string& configPath
    );
    
    /**
     * Get available configuration templates
     * @return Map of template names to template configurations
     */
    static std::unordered_map<std::string, std::shared_ptr<MTConfig>> getConfigurationTemplates();
    
    /**
     * Apply configuration tuning parameters
     * @param config Configuration to tune
     * @param tuningParams Map of parameter names to values
     * @return true if tuning was applied successfully
     */
    static bool applyTuningParameters(
        MTConfig& config,
        const std::unordered_map<std::string, std::string>& tuningParams
    );
    
    /**
     * Get configuration parameter documentation
     * @return Map of parameter paths to documentation strings
     */
    static std::unordered_map<std::string, std::string> getParameterDocumentation();
    
private:
    // Helper methods for configuration processing
    static bool applyEnvironmentOverrides(MTConfig& config, const std::string& environment);
    static std::string getEnvironmentConfigPath(const std::string& environment);
    static void setDefaultLanguagePairs(MTConfig& config);
    static void optimizeForEnvironment(MTConfig& config, const std::string& environment);
    
    // Configuration templates
    static std::shared_ptr<MTConfig> createDevelopmentTemplate();
    static std::shared_ptr<MTConfig> createProductionTemplate();
    static std::shared_ptr<MTConfig> createTestingTemplate();
    static std::shared_ptr<MTConfig> createHighPerformanceTemplate();
    static std::shared_ptr<MTConfig> createLowResourceTemplate();
};

/**
 * Configuration parameter tuning utility
 */
class MTConfigTuner {
public:
    /**
     * Auto-tune configuration based on system resources
     * @param config Configuration to tune
     * @param availableGPUMemoryMB Available GPU memory in MB
     * @param availableRAMMB Available system RAM in MB
     * @param cpuCores Number of CPU cores
     * @return true if tuning was successful
     */
    static bool autoTuneForSystem(
        MTConfig& config,
        size_t availableGPUMemoryMB,
        size_t availableRAMMB,
        int cpuCores
    );
    
    /**
     * Tune configuration for specific performance targets
     * @param config Configuration to tune
     * @param targetLatencyMs Target translation latency in milliseconds
     * @param targetThroughputTPS Target throughput in translations per second
     * @param maxMemoryUsageMB Maximum memory usage in MB
     * @return true if tuning was successful
     */
    static bool tuneForPerformance(
        MTConfig& config,
        int targetLatencyMs,
        int targetThroughputTPS,
        size_t maxMemoryUsageMB
    );
    
    /**
     * Tune configuration for specific use case
     * @param config Configuration to tune
     * @param useCase Use case identifier ("realtime", "batch", "streaming", "quality")
     * @return true if tuning was successful
     */
    static bool tuneForUseCase(MTConfig& config, const std::string& useCase);
    
private:
    static void tuneGPUSettings(MTConfig& config, size_t availableGPUMemoryMB);
    static void tuneBatchSettings(MTConfig& config, int cpuCores, size_t availableRAMMB);
    static void tuneCachingSettings(MTConfig& config, size_t availableRAMMB);
    static void tuneQualitySettings(MTConfig& config, const std::string& useCase);
    static void tuneErrorHandlingSettings(MTConfig& config, const std::string& useCase);
};

} // namespace mt
} // namespace speechrnt