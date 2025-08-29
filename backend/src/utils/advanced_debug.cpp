#include "utils/advanced_debug.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <random>
#include <iomanip>
#include <filesystem>
#include <cmath>
#include <numeric>

namespace speechrnt {
namespace utils {

// DebugSession Implementation
DebugSession::DebugSession(const std::string& sessionId, const std::string& operation)
    : sessionId_(sessionId), operation_(operation), completed_(false), success_(false) {
    startTime_ = std::chrono::steady_clock::now();
}

DebugSession::~DebugSession() {
    if (!completed_) {
        complete(false);
    }
}

void DebugSession::startStage(const std::string& stageName, const std::string& description) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    
    // Check if stage already exists
    auto* existingStage = findStage(stageName);
    if (existingStage) {
        logWarn("Stage '" + stageName + "' already exists, restarting", "DebugSession");
        existingStage->startTime = std::chrono::steady_clock::now();
        existingStage->completed = false;
        existingStage->success = false;
        existingStage->errorMessage.clear();
        return;
    }
    
    stages_.emplace_back(stageName, description);
    logTrace("Started stage: " + stageName + 
             (description.empty() ? "" : " (" + description + ")"), "DebugSession");
}

void DebugSession::completeStage(const std::string& stageName, bool success, const std::string& error) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    
    auto* stage = findStage(stageName);
    if (!stage) {
        logError("Attempted to complete non-existent stage: " + stageName, "DebugSession");
        return;
    }
    
    stage->complete(success, error);
    
    std::string logMessage = "Completed stage: " + stageName + 
                           " (duration: " + std::to_string(stage->getDurationMs()) + "ms, " +
                           "status: " + (success ? "SUCCESS" : "FAILED") + ")";
    if (!error.empty()) {
        logMessage += " - Error: " + error;
    }
    
    if (success) {
        logDebug(logMessage, "DebugSession");
    } else {
        logError(logMessage, "DebugSession");
    }
}

void DebugSession::addStageData(const std::string& stageName, const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    
    auto* stage = findStage(stageName);
    if (!stage) {
        logError("Attempted to add data to non-existent stage: " + stageName, "DebugSession");
        return;
    }
    
    stage->stageData[key] = value;
    logTrace("Added data to stage " + stageName + ": " + key + " = " + value, "DebugSession");
}

void DebugSession::addIntermediateResult(const std::string& stageName, const std::string& result) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    
    auto* stage = findStage(stageName);
    if (!stage) {
        logError("Attempted to add result to non-existent stage: " + stageName, "DebugSession");
        return;
    }
    
    stage->intermediateResults.push_back(result);
    logTrace("Added intermediate result to stage " + stageName + ": " + 
             result.substr(0, 100) + (result.length() > 100 ? "..." : ""), "DebugSession");
}

void DebugSession::setAudioCharacteristics(const AudioCharacteristics& characteristics) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    audioCharacteristics_ = characteristics;
    
    logDebug("Audio characteristics set - Duration: " + std::to_string(characteristics.durationSeconds) + 
             "s, Quality: " + std::to_string(characteristics.qualityScore) + 
             ", SNR: " + std::to_string(characteristics.signalToNoiseRatio) + "dB", "DebugSession");
}

void DebugSession::addAudioSample(const std::vector<float>& audioData, const std::string& label) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    
    // Store audio sample metadata (not the actual data to save memory)
    std::string key = "audio_sample_" + std::to_string(metadata_.size());
    if (!label.empty()) {
        key = "audio_sample_" + label;
    }
    
    metadata_[key + "_size"] = std::to_string(audioData.size());
    metadata_[key + "_timestamp"] = std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    
    logTrace("Added audio sample: " + key + " (size: " + std::to_string(audioData.size()) + ")", "DebugSession");
}

void DebugSession::logTrace(const std::string& message, const std::string& component) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    LogEntry entry;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.level = DebugLevel::TRACE;
    entry.component = component;
    entry.message = message;
    logEntries_.push_back(entry);
}

void DebugSession::logDebug(const std::string& message, const std::string& component) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    LogEntry entry;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.level = DebugLevel::DEBUG;
    entry.component = component;
    entry.message = message;
    logEntries_.push_back(entry);
}

void DebugSession::logInfo(const std::string& message, const std::string& component) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    LogEntry entry;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.level = DebugLevel::INFO;
    entry.component = component;
    entry.message = message;
    logEntries_.push_back(entry);
}

void DebugSession::logWarn(const std::string& message, const std::string& component) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    LogEntry entry;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.level = DebugLevel::WARN;
    entry.component = component;
    entry.message = message;
    logEntries_.push_back(entry);
}

void DebugSession::logError(const std::string& message, const std::string& component) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    LogEntry entry;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.level = DebugLevel::ERROR;
    entry.component = component;
    entry.message = message;
    logEntries_.push_back(entry);
}

std::string DebugSession::exportToJSON() const {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    
    std::ostringstream json;
    json << "{\n";
    json << "  \"sessionId\": \"" << sessionId_ << "\",\n";
    json << "  \"operation\": \"" << operation_ << "\",\n";
    json << "  \"startTime\": \"" << formatTimestamp(startTime_) << "\",\n";
    json << "  \"endTime\": \"" << (completed_ ? formatTimestamp(endTime_) : "null") << "\",\n";
    json << "  \"completed\": " << (completed_ ? "true" : "false") << ",\n";
    json << "  \"success\": " << (success_ ? "true" : "false") << ",\n";
    json << "  \"totalDurationMs\": " << getTotalDurationMs() << ",\n";
    
    // Stages
    json << "  \"stages\": [\n";
    for (size_t i = 0; i < stages_.size(); ++i) {
        const auto& stage = stages_[i];
        json << "    {\n";
        json << "      \"name\": \"" << stage.stageName << "\",\n";
        json << "      \"description\": \"" << stage.stageDescription << "\",\n";
        json << "      \"durationMs\": " << stage.getDurationMs() << ",\n";
        json << "      \"completed\": " << (stage.completed ? "true" : "false") << ",\n";
        json << "      \"success\": " << (stage.success ? "true" : "false") << ",\n";
        json << "      \"errorMessage\": \"" << stage.errorMessage << "\",\n";
        
        // Stage data
        json << "      \"data\": {\n";
        size_t dataIndex = 0;
        for (const auto& data : stage.stageData) {
            json << "        \"" << data.first << "\": \"" << data.second << "\"";
            if (++dataIndex < stage.stageData.size()) json << ",";
            json << "\n";
        }
        json << "      },\n";
        
        // Intermediate results
        json << "      \"intermediateResults\": [\n";
        for (size_t j = 0; j < stage.intermediateResults.size(); ++j) {
            json << "        \"" << stage.intermediateResults[j] << "\"";
            if (j < stage.intermediateResults.size() - 1) json << ",";
            json << "\n";
        }
        json << "      ]\n";
        
        json << "    }";
        if (i < stages_.size() - 1) json << ",";
        json << "\n";
    }
    json << "  ],\n";
    
    // Audio characteristics
    json << "  \"audioCharacteristics\": {\n";
    json << "    \"sampleCount\": " << audioCharacteristics_.sampleCount << ",\n";
    json << "    \"sampleRate\": " << audioCharacteristics_.sampleRate << ",\n";
    json << "    \"channels\": " << audioCharacteristics_.channels << ",\n";
    json << "    \"durationSeconds\": " << audioCharacteristics_.durationSeconds << ",\n";
    json << "    \"rmsLevel\": " << audioCharacteristics_.rmsLevel << ",\n";
    json << "    \"peakLevel\": " << audioCharacteristics_.peakLevel << ",\n";
    json << "    \"signalToNoiseRatio\": " << audioCharacteristics_.signalToNoiseRatio << ",\n";
    json << "    \"zeroCrossingRate\": " << audioCharacteristics_.zeroCrossingRate << ",\n";
    json << "    \"spectralCentroid\": " << audioCharacteristics_.spectralCentroid << ",\n";
    json << "    \"spectralRolloff\": " << audioCharacteristics_.spectralRolloff << ",\n";
    json << "    \"hasClipping\": " << (audioCharacteristics_.hasClipping ? "true" : "false") << ",\n";
    json << "    \"hasSilence\": " << (audioCharacteristics_.hasSilence ? "true" : "false") << ",\n";
    json << "    \"hasNoise\": " << (audioCharacteristics_.hasNoise ? "true" : "false") << ",\n";
    json << "    \"speechProbability\": " << audioCharacteristics_.speechProbability << ",\n";
    json << "    \"qualityScore\": " << audioCharacteristics_.qualityScore << ",\n";
    json << "    \"sourceInfo\": \"" << audioCharacteristics_.sourceInfo << "\"\n";
    json << "  },\n";
    
    // Metadata
    json << "  \"metadata\": {\n";
    size_t metaIndex = 0;
    for (const auto& meta : metadata_) {
        json << "    \"" << meta.first << "\": \"" << meta.second << "\"";
        if (++metaIndex < metadata_.size()) json << ",";
        json << "\n";
    }
    json << "  },\n";
    
    // Log entries
    json << "  \"logEntries\": [\n";
    for (size_t i = 0; i < logEntries_.size(); ++i) {
        const auto& entry = logEntries_[i];
        json << "    {\n";
        json << "      \"timestamp\": \"" << formatTimestamp(entry.timestamp) << "\",\n";
        json << "      \"level\": " << static_cast<int>(entry.level) << ",\n";
        json << "      \"component\": \"" << entry.component << "\",\n";
        json << "      \"message\": \"" << entry.message << "\"\n";
        json << "    }";
        if (i < logEntries_.size() - 1) json << ",";
        json << "\n";
    }
    json << "  ]\n";
    
    json << "}\n";
    return json.str();
}

std::string DebugSession::exportToText() const {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    
    std::ostringstream text;
    text << "=== Debug Session Report ===\n";
    text << "Session ID: " << sessionId_ << "\n";
    text << "Operation: " << operation_ << "\n";
    text << "Start Time: " << formatTimestamp(startTime_) << "\n";
    text << "End Time: " << (completed_ ? formatTimestamp(endTime_) : "In Progress") << "\n";
    text << "Status: " << (completed_ ? (success_ ? "SUCCESS" : "FAILED") : "IN PROGRESS") << "\n";
    text << "Total Duration: " << getTotalDurationMs() << " ms\n\n";
    
    // Processing stages
    text << "=== Processing Stages ===\n";
    for (const auto& stage : stages_) {
        text << "Stage: " << stage.stageName << "\n";
        if (!stage.stageDescription.empty()) {
            text << "  Description: " << stage.stageDescription << "\n";
        }
        text << "  Duration: " << stage.getDurationMs() << " ms\n";
        text << "  Status: " << (stage.completed ? (stage.success ? "SUCCESS" : "FAILED") : "IN PROGRESS") << "\n";
        if (!stage.errorMessage.empty()) {
            text << "  Error: " << stage.errorMessage << "\n";
        }
        
        if (!stage.stageData.empty()) {
            text << "  Data:\n";
            for (const auto& data : stage.stageData) {
                text << "    " << data.first << ": " << data.second << "\n";
            }
        }
        
        if (!stage.intermediateResults.empty()) {
            text << "  Intermediate Results:\n";
            for (size_t i = 0; i < stage.intermediateResults.size(); ++i) {
                text << "    " << (i + 1) << ". " << stage.intermediateResults[i] << "\n";
            }
        }
        text << "\n";
    }
    
    // Audio characteristics
    text << "=== Audio Characteristics ===\n";
    text << "Sample Count: " << audioCharacteristics_.sampleCount << "\n";
    text << "Sample Rate: " << audioCharacteristics_.sampleRate << " Hz\n";
    text << "Channels: " << audioCharacteristics_.channels << "\n";
    text << "Duration: " << audioCharacteristics_.durationSeconds << " seconds\n";
    text << "RMS Level: " << audioCharacteristics_.rmsLevel << "\n";
    text << "Peak Level: " << audioCharacteristics_.peakLevel << "\n";
    text << "Signal-to-Noise Ratio: " << audioCharacteristics_.signalToNoiseRatio << " dB\n";
    text << "Zero Crossing Rate: " << audioCharacteristics_.zeroCrossingRate << "\n";
    text << "Spectral Centroid: " << audioCharacteristics_.spectralCentroid << " Hz\n";
    text << "Spectral Rolloff: " << audioCharacteristics_.spectralRolloff << " Hz\n";
    text << "Has Clipping: " << (audioCharacteristics_.hasClipping ? "Yes" : "No") << "\n";
    text << "Has Silence: " << (audioCharacteristics_.hasSilence ? "Yes" : "No") << "\n";
    text << "Has Noise: " << (audioCharacteristics_.hasNoise ? "Yes" : "No") << "\n";
    text << "Speech Probability: " << audioCharacteristics_.speechProbability << "\n";
    text << "Quality Score: " << audioCharacteristics_.qualityScore << "\n";
    if (!audioCharacteristics_.sourceInfo.empty()) {
        text << "Source Info: " << audioCharacteristics_.sourceInfo << "\n";
    }
    text << "\n";
    
    // Log entries
    text << "=== Log Entries ===\n";
    for (const auto& entry : logEntries_) {
        std::string levelStr;
        switch (entry.level) {
            case DebugLevel::TRACE: levelStr = "TRACE"; break;
            case DebugLevel::DEBUG: levelStr = "DEBUG"; break;
            case DebugLevel::INFO: levelStr = "INFO"; break;
            case DebugLevel::WARN: levelStr = "WARN"; break;
            case DebugLevel::ERROR: levelStr = "ERROR"; break;
            default: levelStr = "UNKNOWN"; break;
        }
        
        text << "[" << formatTimestamp(entry.timestamp) << "] " 
             << levelStr << " [" << entry.component << "] " << entry.message << "\n";
    }
    
    return text.str();
}

bool DebugSession::saveToFile(const std::string& filePath, const std::string& format) const {
    try {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            return false;
        }
        
        if (format == "json") {
            file << exportToJSON();
        } else {
            file << exportToText();
        }
        
        file.close();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

double DebugSession::getTotalDurationMs() const {
    if (!completed_) {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(now - startTime_).count() / 1000.0;
    }
    return std::chrono::duration_cast<std::chrono::microseconds>(endTime_ - startTime_).count() / 1000.0;
}

void DebugSession::complete(bool wasSuccessful) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    if (!completed_) {
        endTime_ = std::chrono::steady_clock::now();
        completed_ = true;
        success_ = wasSuccessful;
        
        logInfo("Session completed - Status: " + std::string(wasSuccessful ? "SUCCESS" : "FAILED") + 
                ", Duration: " + std::to_string(getTotalDurationMs()) + "ms", "DebugSession");
    }
}

void DebugSession::setMetadata(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    metadata_[key] = value;
}

std::string DebugSession::getMetadata(const std::string& key) const {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    auto it = metadata_.find(key);
    return (it != metadata_.end()) ? it->second : "";
}

ProcessingStage* DebugSession::findStage(const std::string& stageName) {
    for (auto& stage : stages_) {
        if (stage.stageName == stageName) {
            return &stage;
        }
    }
    return nullptr;
}

std::string DebugSession::formatTimestamp(const std::chrono::steady_clock::time_point& timePoint) const {
    auto time_t = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now() + 
        (timePoint - std::chrono::steady_clock::now()));
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// AdvancedDebugManager Implementation
AdvancedDebugManager& AdvancedDebugManager::getInstance() {
    static AdvancedDebugManager instance;
    return instance;
}

AdvancedDebugManager::~AdvancedDebugManager() {
    cleanup();
}

bool AdvancedDebugManager::initialize(DebugLevel debugLevel, bool enableFileLogging, const std::string& logDirectory) {
    if (initialized_.load()) {
        return true;
    }
    
    debugLevel_ = debugLevel;
    fileLoggingEnabled_ = enableFileLogging;
    logDirectory_ = logDirectory;
    
    if (enableFileLogging) {
        try {
            std::filesystem::create_directories(logDirectory);
            
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::ostringstream filename;
            filename << logDirectory << "/debug_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << ".log";
            
            debugOutputFile_ = filename.str();
            debugFileStream_ = std::make_unique<std::ofstream>(debugOutputFile_, std::ios::app);
            
            if (!debugFileStream_->is_open()) {
                Logger::warn("Failed to open debug output file: " + debugOutputFile_);
                fileLoggingEnabled_ = false;
            }
        } catch (const std::exception& e) {
            Logger::warn("Failed to initialize debug file logging: " + std::string(e.what()));
            fileLoggingEnabled_ = false;
        }
    }
    
    initialized_ = true;
    Logger::info("Advanced debug manager initialized (level: " + std::to_string(static_cast<int>(debugLevel)) + 
                ", file logging: " + (fileLoggingEnabled_.load() ? "enabled" : "disabled") + ")");
    
    return true;
}

void AdvancedDebugManager::setDebugLevel(DebugLevel level) {
    debugLevel_ = level;
    log(DebugLevel::INFO, "AdvancedDebugManager", "Debug level changed to " + std::to_string(static_cast<int>(level)));
}

void AdvancedDebugManager::setDebugMode(bool enabled) {
    debugMode_ = enabled;
    log(DebugLevel::INFO, "AdvancedDebugManager", "Debug mode " + std::string(enabled ? "enabled" : "disabled"));
}

std::shared_ptr<DebugSession> AdvancedDebugManager::createSession(const std::string& operation, const std::string& sessionId) {
    std::string actualSessionId = sessionId.empty() ? generateSessionId() : sessionId;
    
    auto session = std::make_shared<DebugSession>(actualSessionId, operation);
    
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        activeSessions_[actualSessionId] = session;
    }
    
    totalSessions_++;
    log(DebugLevel::DEBUG, "AdvancedDebugManager", "Created debug session: " + actualSessionId + " for operation: " + operation);
    
    return session;
}

std::shared_ptr<DebugSession> AdvancedDebugManager::getSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    
    auto it = activeSessions_.find(sessionId);
    if (it != activeSessions_.end()) {
        return it->second;
    }
    
    auto completedIt = completedSessions_.find(sessionId);
    if (completedIt != completedSessions_.end()) {
        return completedIt->second;
    }
    
    return nullptr;
}

void AdvancedDebugManager::completeSession(const std::string& sessionId, bool success) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    
    auto it = activeSessions_.find(sessionId);
    if (it != activeSessions_.end()) {
        it->second->complete(success);
        completedSessions_[sessionId] = it->second;
        activeSessions_.erase(it);
        
        if (success) {
            successfulSessions_++;
        } else {
            failedSessions_++;
        }
        
        log(DebugLevel::DEBUG, "AdvancedDebugManager", "Completed debug session: " + sessionId + 
            " (status: " + (success ? "SUCCESS" : "FAILED") + ")");
    }
}

AudioCharacteristics AdvancedDebugManager::analyzeAudioCharacteristics(const std::vector<float>& audioData,
                                                                      int sampleRate,
                                                                      int channels,
                                                                      const std::string& sourceInfo) {
    AudioCharacteristics characteristics;
    
    if (audioData.empty()) {
        return characteristics;
    }
    
    // Basic properties
    characteristics.sampleCount = audioData.size();
    characteristics.sampleRate = sampleRate;
    characteristics.channels = channels;
    characteristics.durationSeconds = static_cast<double>(audioData.size()) / (sampleRate * channels);
    characteristics.sourceInfo = sourceInfo;
    
    // Signal analysis
    characteristics.rmsLevel = calculateRMS(audioData);
    characteristics.peakLevel = calculatePeak(audioData);
    characteristics.zeroCrossingRate = calculateZeroCrossingRate(audioData);
    
    // Quality indicators
    characteristics.hasClipping = characteristics.peakLevel >= 0.95;
    characteristics.hasSilence = characteristics.rmsLevel < 0.01;
    characteristics.hasNoise = characteristics.rmsLevel > 0.1 && characteristics.zeroCrossingRate > 0.1;
    
    // Simple speech probability estimation based on energy and zero crossing rate
    if (characteristics.rmsLevel > 0.02 && characteristics.rmsLevel < 0.8 && 
        characteristics.zeroCrossingRate > 0.01 && characteristics.zeroCrossingRate < 0.3) {
        characteristics.speechProbability = std::min(1.0, characteristics.rmsLevel * 2.0);
    } else {
        characteristics.speechProbability = 0.0;
    }
    
    // Overall quality score (0.0 to 1.0)
    double qualityScore = 1.0;
    if (characteristics.hasClipping) qualityScore -= 0.3;
    if (characteristics.hasSilence) qualityScore -= 0.2;
    if (characteristics.hasNoise) qualityScore -= 0.1;
    if (characteristics.rmsLevel < 0.005) qualityScore -= 0.2; // Too quiet
    characteristics.qualityScore = std::max(0.0, qualityScore);
    
    // Estimate SNR (simplified)
    if (characteristics.rmsLevel > 0.01) {
        double noiseFloor = 0.01; // Assumed noise floor
        characteristics.signalToNoiseRatio = 20.0 * std::log10(characteristics.rmsLevel / noiseFloor);
    } else {
        characteristics.signalToNoiseRatio = 0.0;
    }
    
    // Calculate MFCC coefficients (simplified version)
    try {
        characteristics.mfccCoefficients = calculateMFCC(audioData, sampleRate);
    } catch (const std::exception&) {
        // If MFCC calculation fails, continue without it
        characteristics.mfccCoefficients.clear();
    }
    
    // Calculate frequency spectrum
    try {
        characteristics.frequencySpectrum = calculateFFT(audioData);
        
        // Calculate spectral centroid and rolloff from spectrum
        if (!characteristics.frequencySpectrum.empty()) {
            double totalEnergy = 0.0;
            double weightedSum = 0.0;
            
            for (size_t i = 0; i < characteristics.frequencySpectrum.size(); ++i) {
                double freq = static_cast<double>(i * sampleRate) / (2.0 * characteristics.frequencySpectrum.size());
                double energy = characteristics.frequencySpectrum[i];
                totalEnergy += energy;
                weightedSum += freq * energy;
            }
            
            if (totalEnergy > 0) {
                characteristics.spectralCentroid = weightedSum / totalEnergy;
                
                // Calculate spectral rolloff (frequency below which 85% of energy is contained)
                double cumulativeEnergy = 0.0;
                double rolloffThreshold = 0.85 * totalEnergy;
                
                for (size_t i = 0; i < characteristics.frequencySpectrum.size(); ++i) {
                    cumulativeEnergy += characteristics.frequencySpectrum[i];
                    if (cumulativeEnergy >= rolloffThreshold) {
                        characteristics.spectralRolloff = static_cast<double>(i * sampleRate) / 
                                                         (2.0 * characteristics.frequencySpectrum.size());
                        break;
                    }
                }
            }
        }
    } catch (const std::exception&) {
        // If FFT calculation fails, continue without spectral features
        characteristics.frequencySpectrum.clear();
        characteristics.spectralCentroid = 0.0;
        characteristics.spectralRolloff = 0.0;
    }
    
    return characteristics;
}

void AdvancedDebugManager::registerDebugCallback(std::function<void(const std::string&, DebugLevel, const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    debugCallbacks_.push_back(callback);
}

void AdvancedDebugManager::log(DebugLevel level, const std::string& component, const std::string& message, const std::string& sessionId) {
    if (!initialized_.load() || level > debugLevel_.load()) {
        return;
    }
    
    totalLogEntries_++;
    
    // Format log message
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream logMessage;
    logMessage << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << "] ";
    
    switch (level) {
        case DebugLevel::TRACE: logMessage << "TRACE"; break;
        case DebugLevel::DEBUG: logMessage << "DEBUG"; break;
        case DebugLevel::INFO: logMessage << "INFO"; break;
        case DebugLevel::WARN: logMessage << "WARN"; break;
        case DebugLevel::ERROR: logMessage << "ERROR"; break;
        default: logMessage << "UNKNOWN"; break;
    }
    
    logMessage << " [" << component << "]";
    if (!sessionId.empty()) {
        logMessage << " [" << sessionId << "]";
    }
    logMessage << " " << message;
    
    // Write to file if enabled
    if (fileLoggingEnabled_.load()) {
        writeToFile(logMessage.str());
    }
    
    // Notify callbacks
    notifyCallbacks(component, level, message);
    
    // Also log to standard logger for important messages
    if (level <= DebugLevel::WARN) {
        switch (level) {
            case DebugLevel::ERROR:
                Logger::error(logMessage.str());
                break;
            case DebugLevel::WARN:
                Logger::warn(logMessage.str());
                break;
            default:
                Logger::info(logMessage.str());
                break;
        }
    }
}

std::string AdvancedDebugManager::exportDebugData(const std::vector<std::string>& sessionIds, const std::string& format) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    
    std::vector<std::shared_ptr<DebugSession>> sessionsToExport;
    
    if (sessionIds.empty()) {
        // Export all sessions
        for (const auto& pair : activeSessions_) {
            sessionsToExport.push_back(pair.second);
        }
        for (const auto& pair : completedSessions_) {
            sessionsToExport.push_back(pair.second);
        }
    } else {
        // Export specific sessions
        for (const auto& sessionId : sessionIds) {
            auto session = getSession(sessionId);
            if (session) {
                sessionsToExport.push_back(session);
            }
        }
    }
    
    if (format == "json") {
        std::ostringstream json;
        json << "{\n";
        json << "  \"exportTimestamp\": \"" << std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << "\",\n";
        json << "  \"totalSessions\": " << sessionsToExport.size() << ",\n";
        json << "  \"sessions\": [\n";
        
        for (size_t i = 0; i < sessionsToExport.size(); ++i) {
            json << sessionsToExport[i]->exportToJSON();
            if (i < sessionsToExport.size() - 1) {
                json << ",";
            }
            json << "\n";
        }
        
        json << "  ]\n";
        json << "}\n";
        return json.str();
    } else {
        // Text format
        std::ostringstream text;
        text << "=== Debug Data Export ===\n";
        text << "Export Time: " << std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << "\n";
        text << "Total Sessions: " << sessionsToExport.size() << "\n\n";
        
        for (const auto& session : sessionsToExport) {
            text << session->exportToText() << "\n";
            text << "=====================================\n\n";
        }
        
        return text.str();
    }
}

std::map<std::string, double> AdvancedDebugManager::getDebugStatistics() const {
    std::map<std::string, double> stats;
    
    stats["total_sessions"] = static_cast<double>(totalSessions_.load());
    stats["successful_sessions"] = static_cast<double>(successfulSessions_.load());
    stats["failed_sessions"] = static_cast<double>(failedSessions_.load());
    stats["active_sessions"] = static_cast<double>(getActiveSessionCount());
    stats["total_log_entries"] = static_cast<double>(totalLogEntries_.load());
    
    if (totalSessions_.load() > 0) {
        stats["success_rate"] = static_cast<double>(successfulSessions_.load()) / totalSessions_.load();
    } else {
        stats["success_rate"] = 0.0;
    }
    
    {
        std::lock_guard<std::mutex> lock(audioSamplesMutex_);
        stats["failed_audio_samples"] = static_cast<double>(failedAudioSamples_.size());
    }
    
    return stats;
}

void AdvancedDebugManager::clearOldSessions(int olderThanHours) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    
    auto cutoffTime = std::chrono::steady_clock::now() - std::chrono::hours(olderThanHours);
    
    // Clear old completed sessions
    auto it = completedSessions_.begin();
    while (it != completedSessions_.end()) {
        // Assuming session start time as proxy for age
        if (it->second->getTotalDurationMs() > 0) { // Session has been completed
            it = completedSessions_.erase(it);
        } else {
            ++it;
        }
    }
    
    log(DebugLevel::INFO, "AdvancedDebugManager", "Cleared old debug sessions (older than " + 
        std::to_string(olderThanHours) + " hours)");
}

size_t AdvancedDebugManager::getActiveSessionCount() const {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    return activeSessions_.size();
}

std::vector<std::string> AdvancedDebugManager::getSessionIds(bool activeOnly) const {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    
    std::vector<std::string> sessionIds;
    
    for (const auto& pair : activeSessions_) {
        sessionIds.push_back(pair.first);
    }
    
    if (!activeOnly) {
        for (const auto& pair : completedSessions_) {
            sessionIds.push_back(pair.first);
        }
    }
    
    return sessionIds;
}

void AdvancedDebugManager::setAutoAudioCapture(bool enabled, size_t maxSamples) {
    autoAudioCapture_ = enabled;
    maxAudioSamples_ = maxSamples;
    
    log(DebugLevel::INFO, "AdvancedDebugManager", "Auto audio capture " + 
        std::string(enabled ? "enabled" : "disabled") + " (max samples: " + std::to_string(maxSamples) + ")");
}

void AdvancedDebugManager::setDebugOutputFile(const std::string& filePath, bool append) {
    if (debugFileStream_) {
        debugFileStream_->close();
    }
    
    debugOutputFile_ = filePath;
    
    try {
        debugFileStream_ = std::make_unique<std::ofstream>(filePath, append ? std::ios::app : std::ios::trunc);
        if (debugFileStream_->is_open()) {
            fileLoggingEnabled_ = true;
            log(DebugLevel::INFO, "AdvancedDebugManager", "Debug output file set to: " + filePath);
        } else {
            fileLoggingEnabled_ = false;
            Logger::warn("Failed to open debug output file: " + filePath);
        }
    } catch (const std::exception& e) {
        fileLoggingEnabled_ = false;
        Logger::warn("Failed to set debug output file: " + std::string(e.what()));
    }
}

void AdvancedDebugManager::cleanup() {
    if (!initialized_.load()) {
        return;
    }
    
    log(DebugLevel::INFO, "AdvancedDebugManager", "Cleaning up debug manager");
    
    // Complete all active sessions
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        for (auto& pair : activeSessions_) {
            pair.second->complete(false);
        }
        activeSessions_.clear();
        completedSessions_.clear();
    }
    
    // Close debug file
    if (debugFileStream_) {
        debugFileStream_->close();
        debugFileStream_.reset();
    }
    
    // Clear callbacks
    {
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        debugCallbacks_.clear();
    }
    
    // Clear audio samples
    {
        std::lock_guard<std::mutex> lock(audioSamplesMutex_);
        failedAudioSamples_.clear();
    }
    
    initialized_ = false;
}

std::string AdvancedDebugManager::generateSessionId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::ostringstream sessionId;
    sessionId << "debug_";
    
    auto now = std::chrono::steady_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    sessionId << timestamp << "_";
    
    // Add random hex suffix
    for (int i = 0; i < 8; ++i) {
        sessionId << std::hex << dis(gen);
    }
    
    return sessionId.str();
}

void AdvancedDebugManager::writeToFile(const std::string& message) {
    if (debugFileStream_ && debugFileStream_->is_open()) {
        *debugFileStream_ << message << std::endl;
        debugFileStream_->flush();
    }
}

void AdvancedDebugManager::notifyCallbacks(const std::string& component, DebugLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    for (const auto& callback : debugCallbacks_) {
        try {
            callback(component, level, message);
        } catch (const std::exception&) {
            // Ignore callback exceptions to prevent cascading failures
        }
    }
}

double AdvancedDebugManager::calculateRMS(const std::vector<float>& audioData) {
    if (audioData.empty()) {
        return 0.0;
    }
    
    double sum = 0.0;
    for (float sample : audioData) {
        sum += sample * sample;
    }
    
    return std::sqrt(sum / audioData.size());
}

double AdvancedDebugManager::calculatePeak(const std::vector<float>& audioData) {
    if (audioData.empty()) {
        return 0.0;
    }
    
    float maxVal = 0.0f;
    for (float sample : audioData) {
        maxVal = std::max(maxVal, std::abs(sample));
    }
    
    return static_cast<double>(maxVal);
}

double AdvancedDebugManager::calculateZeroCrossingRate(const std::vector<float>& audioData) {
    if (audioData.size() < 2) {
        return 0.0;
    }
    
    int zeroCrossings = 0;
    for (size_t i = 1; i < audioData.size(); ++i) {
        if ((audioData[i] >= 0) != (audioData[i-1] >= 0)) {
            zeroCrossings++;
        }
    }
    
    return static_cast<double>(zeroCrossings) / (audioData.size() - 1);
}

std::vector<double> AdvancedDebugManager::calculateMFCC(const std::vector<float>& audioData, int sampleRate) {
    // Simplified MFCC calculation - in a real implementation, you would use a proper DSP library
    std::vector<double> mfcc(13, 0.0);
    
    if (audioData.empty()) {
        return mfcc;
    }
    
    // This is a placeholder implementation
    // Real MFCC calculation would involve:
    // 1. Pre-emphasis
    // 2. Windowing
    // 3. FFT
    // 4. Mel filter bank
    // 5. Log
    // 6. DCT
    
    // For now, just return some basic spectral features as approximation
    double energy = 0.0;
    for (float sample : audioData) {
        energy += sample * sample;
    }
    
    mfcc[0] = std::log(energy + 1e-10); // Log energy
    
    // Fill remaining coefficients with simplified calculations
    for (int i = 1; i < 13; ++i) {
        mfcc[i] = energy * std::cos(i * M_PI / 13.0) * 0.1;
    }
    
    return mfcc;
}

std::vector<double> AdvancedDebugManager::calculateFFT(const std::vector<float>& audioData) {
    // Simplified FFT calculation - in a real implementation, you would use FFTW or similar
    std::vector<double> spectrum;
    
    if (audioData.empty()) {
        return spectrum;
    }
    
    // This is a placeholder implementation
    // Real FFT would provide proper frequency domain representation
    
    size_t fftSize = std::min(audioData.size(), static_cast<size_t>(1024));
    spectrum.resize(fftSize / 2);
    
    // Simple magnitude spectrum approximation
    for (size_t i = 0; i < spectrum.size(); ++i) {
        double real = 0.0, imag = 0.0;
        
        for (size_t j = 0; j < fftSize; ++j) {
            double angle = -2.0 * M_PI * i * j / fftSize;
            real += audioData[j] * std::cos(angle);
            imag += audioData[j] * std::sin(angle);
        }
        
        spectrum[i] = std::sqrt(real * real + imag * imag);
    }
    
    return spectrum;
}

} // namespace utils
} // namespace speechrnt