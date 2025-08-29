#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <mutex>
#include <atomic>
#include <functional>
#include <fstream>
#include <sstream>

namespace speechrnt {
namespace utils {

/**
 * Debug level enumeration for controlling verbosity
 */
enum class DebugLevel {
    OFF = 0,
    ERROR = 1,
    WARN = 2,
    INFO = 3,
    DEBUG = 4,
    TRACE = 5
};

/**
 * Processing stage information for step-by-step debugging
 */
struct ProcessingStage {
    std::string stageName;
    std::string stageDescription;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
    bool completed;
    bool success;
    std::string errorMessage;
    std::map<std::string, std::string> stageData;
    std::vector<std::string> intermediateResults;
    
    ProcessingStage(const std::string& name, const std::string& description = "")
        : stageName(name), stageDescription(description), completed(false), success(false) {
        startTime = std::chrono::steady_clock::now();
    }
    
    void complete(bool wasSuccessful = true, const std::string& error = "") {
        endTime = std::chrono::steady_clock::now();
        completed = true;
        success = wasSuccessful;
        errorMessage = error;
    }
    
    double getDurationMs() const {
        if (!completed) {
            auto now = std::chrono::steady_clock::now();
            return std::chrono::duration_cast<std::chrono::microseconds>(now - startTime).count() / 1000.0;
        }
        return std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count() / 1000.0;
    }
};

/**
 * Audio characteristics captured for debugging failed transcriptions
 */
struct AudioCharacteristics {
    // Basic audio properties
    size_t sampleCount;
    int sampleRate;
    int channels;
    double durationSeconds;
    
    // Signal analysis
    double rmsLevel;
    double peakLevel;
    double signalToNoiseRatio;
    double zeroCrossingRate;
    double spectralCentroid;
    double spectralRolloff;
    
    // Quality indicators
    bool hasClipping;
    bool hasSilence;
    bool hasNoise;
    double speechProbability;
    double qualityScore; // 0.0 to 1.0
    
    // Frequency analysis
    std::vector<double> frequencySpectrum;
    std::vector<double> mfccCoefficients;
    
    // Metadata
    std::chrono::steady_clock::time_point captureTime;
    std::string sourceInfo;
    std::map<std::string, std::string> additionalMetrics;
    
    AudioCharacteristics() 
        : sampleCount(0), sampleRate(0), channels(0), durationSeconds(0.0),
          rmsLevel(0.0), peakLevel(0.0), signalToNoiseRatio(0.0), zeroCrossingRate(0.0),
          spectralCentroid(0.0), spectralRolloff(0.0), hasClipping(false), 
          hasSilence(false), hasNoise(false), speechProbability(0.0), qualityScore(0.0) {
        captureTime = std::chrono::steady_clock::now();
    }
};

/**
 * Debug session for tracking a complete processing pipeline
 */
class DebugSession {
public:
    explicit DebugSession(const std::string& sessionId, const std::string& operation = "");
    ~DebugSession();
    
    // Stage management
    void startStage(const std::string& stageName, const std::string& description = "");
    void completeStage(const std::string& stageName, bool success = true, const std::string& error = "");
    void addStageData(const std::string& stageName, const std::string& key, const std::string& value);
    void addIntermediateResult(const std::string& stageName, const std::string& result);
    
    // Audio characteristics
    void setAudioCharacteristics(const AudioCharacteristics& characteristics);
    void addAudioSample(const std::vector<float>& audioData, const std::string& label = "");
    
    // Debug logging
    void logTrace(const std::string& message, const std::string& component = "");
    void logDebug(const std::string& message, const std::string& component = "");
    void logInfo(const std::string& message, const std::string& component = "");
    void logWarn(const std::string& message, const std::string& component = "");
    void logError(const std::string& message, const std::string& component = "");
    
    // Data export
    std::string exportToJSON() const;
    std::string exportToText() const;
    bool saveToFile(const std::string& filePath, const std::string& format = "json") const;
    
    // Getters
    const std::string& getSessionId() const { return sessionId_; }
    const std::string& getOperation() const { return operation_; }
    const std::vector<ProcessingStage>& getStages() const { return stages_; }
    const AudioCharacteristics& getAudioCharacteristics() const { return audioCharacteristics_; }
    bool isCompleted() const { return completed_; }
    bool wasSuccessful() const { return success_; }
    double getTotalDurationMs() const;
    
    // Session control
    void complete(bool wasSuccessful = true);
    void setMetadata(const std::string& key, const std::string& value);
    std::string getMetadata(const std::string& key) const;

private:
    std::string sessionId_;
    std::string operation_;
    std::chrono::steady_clock::time_point startTime_;
    std::chrono::steady_clock::time_point endTime_;
    bool completed_;
    bool success_;
    
    std::vector<ProcessingStage> stages_;
    AudioCharacteristics audioCharacteristics_;
    std::map<std::string, std::string> metadata_;
    
    struct LogEntry {
        std::chrono::steady_clock::time_point timestamp;
        DebugLevel level;
        std::string component;
        std::string message;
    };
    std::vector<LogEntry> logEntries_;
    
    mutable std::mutex sessionMutex_;
    
    ProcessingStage* findStage(const std::string& stageName);
    std::string formatTimestamp(const std::chrono::steady_clock::time_point& timePoint) const;
};

/**
 * Advanced debugging manager for STT features
 */
class AdvancedDebugManager {
public:
    static AdvancedDebugManager& getInstance();
    
    /**
     * Initialize the debug manager
     * @param debugLevel Global debug level
     * @param enableFileLogging Enable logging to files
     * @param logDirectory Directory for debug log files
     * @return true if initialization successful
     */
    bool initialize(DebugLevel debugLevel = DebugLevel::INFO, 
                   bool enableFileLogging = true,
                   const std::string& logDirectory = "debug_logs");
    
    /**
     * Set global debug level
     * @param level New debug level
     */
    void setDebugLevel(DebugLevel level);
    
    /**
     * Get current debug level
     * @return current debug level
     */
    DebugLevel getDebugLevel() const { return debugLevel_.load(); }
    
    /**
     * Enable/disable debug mode
     * @param enabled true to enable debug mode
     */
    void setDebugMode(bool enabled);
    
    /**
     * Check if debug mode is enabled
     * @return true if debug mode is enabled
     */
    bool isDebugMode() const { return debugMode_.load(); }
    
    /**
     * Create a new debug session
     * @param operation Operation being debugged
     * @param sessionId Optional custom session ID
     * @return shared pointer to debug session
     */
    std::shared_ptr<DebugSession> createSession(const std::string& operation, 
                                               const std::string& sessionId = "");
    
    /**
     * Get an existing debug session
     * @param sessionId Session ID
     * @return shared pointer to debug session or nullptr if not found
     */
    std::shared_ptr<DebugSession> getSession(const std::string& sessionId);
    
    /**
     * Complete and archive a debug session
     * @param sessionId Session ID
     * @param success Whether the operation was successful
     */
    void completeSession(const std::string& sessionId, bool success = true);
    
    /**
     * Analyze audio characteristics for debugging
     * @param audioData Audio samples
     * @param sampleRate Sample rate in Hz
     * @param channels Number of channels
     * @param sourceInfo Optional source information
     * @return audio characteristics
     */
    AudioCharacteristics analyzeAudioCharacteristics(const std::vector<float>& audioData,
                                                    int sampleRate,
                                                    int channels = 1,
                                                    const std::string& sourceInfo = "");
    
    /**
     * Register a callback for debug events
     * @param callback Function to call on debug events
     */
    void registerDebugCallback(std::function<void(const std::string&, DebugLevel, const std::string&)> callback);
    
    /**
     * Log a debug message
     * @param level Debug level
     * @param component Component name
     * @param message Debug message
     * @param sessionId Optional session ID
     */
    void log(DebugLevel level, const std::string& component, const std::string& message, 
             const std::string& sessionId = "");
    
    /**
     * Export debug data for analysis
     * @param sessionIds List of session IDs to export (empty for all)
     * @param format Export format ("json", "csv", "text")
     * @return exported data as string
     */
    std::string exportDebugData(const std::vector<std::string>& sessionIds = {},
                               const std::string& format = "json");
    
    /**
     * Get debug statistics
     * @return map of debug statistics
     */
    std::map<std::string, double> getDebugStatistics() const;
    
    /**
     * Clear old debug sessions
     * @param olderThanHours Clear sessions older than specified hours
     */
    void clearOldSessions(int olderThanHours = 24);
    
    /**
     * Get active session count
     * @return number of active debug sessions
     */
    size_t getActiveSessionCount() const;
    
    /**
     * Get all session IDs
     * @param activeOnly If true, return only active sessions
     * @return vector of session IDs
     */
    std::vector<std::string> getSessionIds(bool activeOnly = false) const;
    
    /**
     * Enable/disable automatic audio capture for failed transcriptions
     * @param enabled true to enable automatic capture
     * @param maxSamples Maximum number of audio samples to keep
     */
    void setAutoAudioCapture(bool enabled, size_t maxSamples = 10);
    
    /**
     * Set debug output file
     * @param filePath Path to debug output file
     * @param append true to append to existing file
     */
    void setDebugOutputFile(const std::string& filePath, bool append = true);
    
    /**
     * Cleanup and shutdown debug manager
     */
    void cleanup();

private:
    AdvancedDebugManager() = default;
    ~AdvancedDebugManager();
    
    // Prevent copying
    AdvancedDebugManager(const AdvancedDebugManager&) = delete;
    AdvancedDebugManager& operator=(const AdvancedDebugManager&) = delete;
    
    // Private methods
    std::string generateSessionId();
    void writeToFile(const std::string& message);
    void notifyCallbacks(const std::string& component, DebugLevel level, const std::string& message);
    double calculateRMS(const std::vector<float>& audioData);
    double calculatePeak(const std::vector<float>& audioData);
    double calculateZeroCrossingRate(const std::vector<float>& audioData);
    std::vector<double> calculateMFCC(const std::vector<float>& audioData, int sampleRate);
    std::vector<double> calculateFFT(const std::vector<float>& audioData);
    
    // Member variables
    std::atomic<bool> initialized_{false};
    std::atomic<DebugLevel> debugLevel_{DebugLevel::INFO};
    std::atomic<bool> debugMode_{false};
    std::atomic<bool> fileLoggingEnabled_{false};
    std::atomic<bool> autoAudioCapture_{false};
    std::atomic<size_t> maxAudioSamples_{10};
    
    std::string logDirectory_;
    std::string debugOutputFile_;
    std::unique_ptr<std::ofstream> debugFileStream_;
    
    mutable std::mutex sessionsMutex_;
    std::map<std::string, std::shared_ptr<DebugSession>> activeSessions_;
    std::map<std::string, std::shared_ptr<DebugSession>> completedSessions_;
    
    mutable std::mutex callbacksMutex_;
    std::vector<std::function<void(const std::string&, DebugLevel, const std::string&)>> debugCallbacks_;
    
    // Statistics
    std::atomic<uint64_t> totalSessions_{0};
    std::atomic<uint64_t> successfulSessions_{0};
    std::atomic<uint64_t> failedSessions_{0};
    std::atomic<uint64_t> totalLogEntries_{0};
    
    // Audio samples for failed transcriptions
    mutable std::mutex audioSamplesMutex_;
    std::vector<std::pair<AudioCharacteristics, std::vector<float>>> failedAudioSamples_;
};

// Convenience macros for debug logging
#define DEBUG_SESSION(operation) AdvancedDebugManager::getInstance().createSession(operation)
#define DEBUG_STAGE_START(session, stage) if(session) session->startStage(stage)
#define DEBUG_STAGE_COMPLETE(session, stage, success) if(session) session->completeStage(stage, success)
#define DEBUG_LOG_TRACE(component, message) AdvancedDebugManager::getInstance().log(DebugLevel::TRACE, component, message)
#define DEBUG_LOG_DEBUG(component, message) AdvancedDebugManager::getInstance().log(DebugLevel::DEBUG, component, message)
#define DEBUG_LOG_INFO(component, message) AdvancedDebugManager::getInstance().log(DebugLevel::INFO, component, message)
#define DEBUG_LOG_WARN(component, message) AdvancedDebugManager::getInstance().log(DebugLevel::WARN, component, message)
#define DEBUG_LOG_ERROR(component, message) AdvancedDebugManager::getInstance().log(DebugLevel::ERROR, component, message)

} // namespace utils
} // namespace speechrnt