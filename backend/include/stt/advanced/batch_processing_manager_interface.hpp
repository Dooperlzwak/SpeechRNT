#pragma once

#include "stt/stt_interface.hpp"
#include "stt/advanced/advanced_stt_config.hpp"
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <functional>
#include <chrono>
#include <atomic>

namespace stt {
namespace advanced {

/**
 * Batch job status enumeration
 */
enum class BatchJobStatus {
    PENDING,
    RUNNING,
    PAUSED,
    COMPLETED,
    FAILED,
    CANCELLED
};

/**
 * Batch job priority levels
 */
enum class BatchJobPriority {
    LOW,
    NORMAL,
    HIGH,
    URGENT
};

/**
 * Audio file information
 */
struct AudioFileInfo {
    std::string filePath;
    std::string fileName;
    size_t fileSizeBytes;
    float durationSeconds;
    int sampleRate;
    int channels;
    std::string format; // "wav", "mp3", "flac", etc.
    std::map<std::string, std::string> metadata;
    
    AudioFileInfo() 
        : fileSizeBytes(0)
        , durationSeconds(0.0f)
        , sampleRate(0)
        , channels(0) {}
};

/**
 * Batch job configuration
 */
struct BatchJobConfig {
    std::string outputFormat; // "json", "txt", "srt", "vtt"
    std::string outputDirectory;
    std::string language;
    bool enableSpeakerDiarization;
    bool enableContextualTranscription;
    bool enableAudioPreprocessing;
    QualityLevel qualityLevel;
    size_t chunkSizeSeconds;
    bool enableParallelProcessing;
    size_t maxConcurrentFiles;
    bool preserveTimestamps;
    bool generateWordTimings;
    std::map<std::string, std::string> customParameters;
    
    BatchJobConfig() 
        : outputFormat("json")
        , enableSpeakerDiarization(false)
        , enableContextualTranscription(false)
        , enableAudioPreprocessing(true)
        , qualityLevel(QualityLevel::MEDIUM)
        , chunkSizeSeconds(30)
        , enableParallelProcessing(true)
        , maxConcurrentFiles(4)
        , preserveTimestamps(true)
        , generateWordTimings(false) {}
};

/**
 * Batch job progress information
 */
struct BatchJobProgress {
    uint32_t jobId;
    BatchJobStatus status;
    size_t totalFiles;
    size_t processedFiles;
    size_t failedFiles;
    float overallProgress; // 0.0 to 1.0
    std::string currentFile;
    float currentFileProgress; // 0.0 to 1.0
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point estimatedCompletionTime;
    float averageProcessingSpeed; // files per minute
    std::vector<std::string> errorMessages;
    
    BatchJobProgress() 
        : jobId(0)
        , status(BatchJobStatus::PENDING)
        , totalFiles(0)
        , processedFiles(0)
        , failedFiles(0)
        , overallProgress(0.0f)
        , currentFileProgress(0.0f)
        , startTime(std::chrono::steady_clock::now())
        , estimatedCompletionTime(std::chrono::steady_clock::now())
        , averageProcessingSpeed(0.0f) {}
};

/**
 * Batch job result
 */
struct BatchJobResult {
    uint32_t jobId;
    BatchJobStatus finalStatus;
    size_t totalFiles;
    size_t successfulFiles;
    size_t failedFiles;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
    float totalProcessingTime; // seconds
    std::vector<std::string> outputFiles;
    std::vector<std::string> errorMessages;
    std::map<std::string, std::string> statistics;
    
    BatchJobResult() 
        : jobId(0)
        , finalStatus(BatchJobStatus::PENDING)
        , totalFiles(0)
        , successfulFiles(0)
        , failedFiles(0)
        , startTime(std::chrono::steady_clock::now())
        , endTime(std::chrono::steady_clock::now())
        , totalProcessingTime(0.0f) {}
};

/**
 * File processing result
 */
struct FileProcessingResult {
    std::string inputFile;
    std::string outputFile;
    bool success;
    TranscriptionResult transcriptionResult;
    float processingTimeSeconds;
    std::string errorMessage;
    std::map<std::string, std::string> metadata;
    
    FileProcessingResult() 
        : success(false)
        , processingTimeSeconds(0.0f) {}
};

/**
 * Batch job request
 */
struct BatchJobRequest {
    std::vector<std::string> inputFiles;
    BatchJobConfig config;
    BatchJobPriority priority;
    std::string jobName;
    std::string description;
    std::function<void(const BatchJobProgress&)> progressCallback;
    std::function<void(const BatchJobResult&)> completionCallback;
    std::function<void(const FileProcessingResult&)> fileCompletionCallback;
    
    BatchJobRequest() 
        : priority(BatchJobPriority::NORMAL) {}
};

/**
 * Audio file processor interface
 */
class AudioFileProcessor {
public:
    virtual ~AudioFileProcessor() = default;
    
    /**
     * Initialize file processor
     * @return true if initialization successful
     */
    virtual bool initialize() = 0;
    
    /**
     * Load and analyze audio file
     * @param filePath Path to audio file
     * @return Audio file information
     */
    virtual AudioFileInfo analyzeAudioFile(const std::string& filePath) = 0;
    
    /**
     * Load audio data from file
     * @param filePath Path to audio file
     * @return Audio samples
     */
    virtual std::vector<float> loadAudioFile(const std::string& filePath) = 0;
    
    /**
     * Process audio file in chunks
     * @param filePath Path to audio file
     * @param chunkSizeSeconds Chunk size in seconds
     * @param callback Callback for each chunk
     * @return true if processing successful
     */
    virtual bool processAudioFileInChunks(const std::string& filePath,
                                         size_t chunkSizeSeconds,
                                         std::function<void(const std::vector<float>&, size_t)> callback) = 0;
    
    /**
     * Get supported audio formats
     * @return Vector of supported format extensions
     */
    virtual std::vector<std::string> getSupportedFormats() const = 0;
    
    /**
     * Check if file format is supported
     * @param filePath Path to audio file
     * @return true if format is supported
     */
    virtual bool isFormatSupported(const std::string& filePath) const = 0;
    
    /**
     * Check if processor is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Batch job queue interface
 */
class BatchJobQueue {
public:
    virtual ~BatchJobQueue() = default;
    
    /**
     * Initialize job queue
     * @param maxConcurrentJobs Maximum concurrent jobs
     * @return true if initialization successful
     */
    virtual bool initialize(size_t maxConcurrentJobs = 4) = 0;
    
    /**
     * Add job to queue
     * @param request Batch job request
     * @return Job ID
     */
    virtual uint32_t addJob(const BatchJobRequest& request) = 0;
    
    /**
     * Remove job from queue
     * @param jobId Job ID to remove
     * @return true if removed successfully
     */
    virtual bool removeJob(uint32_t jobId) = 0;
    
    /**
     * Start job processing
     * @return true if started successfully
     */
    virtual bool startProcessing() = 0;
    
    /**
     * Stop job processing
     */
    virtual void stopProcessing() = 0;
    
    /**
     * Pause job processing
     */
    virtual void pauseProcessing() = 0;
    
    /**
     * Resume job processing
     */
    virtual void resumeProcessing() = 0;
    
    /**
     * Get job progress
     * @param jobId Job ID
     * @return Job progress information
     */
    virtual BatchJobProgress getJobProgress(uint32_t jobId) const = 0;
    
    /**
     * Get all job statuses
     * @return Map of job ID to progress
     */
    virtual std::map<uint32_t, BatchJobProgress> getAllJobProgress() const = 0;
    
    /**
     * Cancel job
     * @param jobId Job ID to cancel
     * @return true if cancelled successfully
     */
    virtual bool cancelJob(uint32_t jobId) = 0;
    
    /**
     * Set job priority
     * @param jobId Job ID
     * @param priority New priority
     * @return true if priority set successfully
     */
    virtual bool setJobPriority(uint32_t jobId, BatchJobPriority priority) = 0;
    
    /**
     * Get queue statistics
     * @return Statistics as JSON string
     */
    virtual std::string getQueueStats() const = 0;
    
    /**
     * Check if queue is processing
     * @return true if processing is active
     */
    virtual bool isProcessing() const = 0;
    
    /**
     * Check if queue is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Output formatter interface
 */
class OutputFormatter {
public:
    virtual ~OutputFormatter() = default;
    
    /**
     * Initialize formatter
     * @return true if initialization successful
     */
    virtual bool initialize() = 0;
    
    /**
     * Format transcription result
     * @param result Transcription result
     * @param format Output format ("json", "txt", "srt", "vtt")
     * @param includeTimestamps Include timestamp information
     * @param includeWordTimings Include word-level timings
     * @return Formatted output string
     */
    virtual std::string formatResult(const TranscriptionResult& result,
                                    const std::string& format,
                                    bool includeTimestamps = true,
                                    bool includeWordTimings = false) = 0;
    
    /**
     * Format batch job result
     * @param jobResult Batch job result
     * @param format Output format
     * @return Formatted output string
     */
    virtual std::string formatBatchResult(const BatchJobResult& jobResult,
                                         const std::string& format) = 0;
    
    /**
     * Save formatted result to file
     * @param content Formatted content
     * @param outputPath Output file path
     * @return true if saved successfully
     */
    virtual bool saveToFile(const std::string& content, const std::string& outputPath) = 0;
    
    /**
     * Get supported output formats
     * @return Vector of supported format names
     */
    virtual std::vector<std::string> getSupportedFormats() const = 0;
    
    /**
     * Check if formatter is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Batch processing manager interface
 */
class BatchProcessingManagerInterface {
public:
    virtual ~BatchProcessingManagerInterface() = default;
    
    /**
     * Initialize the batch processing manager
     * @param config Batch processing configuration
     * @return true if initialization successful
     */
    virtual bool initialize(const BatchProcessingConfig& config) = 0;
    
    /**
     * Submit batch job
     * @param request Batch job request
     * @return Job ID
     */
    virtual uint32_t submitBatchJob(const BatchJobRequest& request) = 0;
    
    /**
     * Cancel batch job
     * @param jobId Job ID to cancel
     * @return true if cancelled successfully
     */
    virtual bool cancelBatchJob(uint32_t jobId) = 0;
    
    /**
     * Pause batch job
     * @param jobId Job ID to pause
     * @return true if paused successfully
     */
    virtual bool pauseBatchJob(uint32_t jobId) = 0;
    
    /**
     * Resume batch job
     * @param jobId Job ID to resume
     * @return true if resumed successfully
     */
    virtual bool resumeBatchJob(uint32_t jobId) = 0;
    
    /**
     * Get job progress
     * @param jobId Job ID
     * @return Job progress information
     */
    virtual BatchJobProgress getJobProgress(uint32_t jobId) const = 0;
    
    /**
     * Get job result
     * @param jobId Job ID
     * @return Job result (only available after completion)
     */
    virtual BatchJobResult getJobResult(uint32_t jobId) const = 0;
    
    /**
     * Get all active jobs
     * @return Map of job ID to progress
     */
    virtual std::map<uint32_t, BatchJobProgress> getActiveJobs() const = 0;
    
    /**
     * Get job history
     * @param maxJobs Maximum number of historical jobs to return
     * @return Vector of completed job results
     */
    virtual std::vector<BatchJobResult> getJobHistory(size_t maxJobs = 100) const = 0;
    
    /**
     * Set maximum concurrent jobs
     * @param maxJobs Maximum concurrent jobs
     */
    virtual void setMaxConcurrentJobs(size_t maxJobs) = 0;
    
    /**
     * Set default chunk size
     * @param chunkSizeSeconds Default chunk size in seconds
     */
    virtual void setDefaultChunkSize(size_t chunkSizeSeconds) = 0;
    
    /**
     * Enable or disable parallel processing
     * @param enabled true to enable parallel processing
     */
    virtual void setParallelProcessingEnabled(bool enabled) = 0;
    
    /**
     * Set output directory
     * @param directory Default output directory
     */
    virtual void setOutputDirectory(const std::string& directory) = 0;
    
    /**
     * Get processing statistics
     * @return Statistics as JSON string
     */
    virtual std::string getProcessingStats() const = 0;
    
    /**
     * Get supported audio formats
     * @return Vector of supported format extensions
     */
    virtual std::vector<std::string> getSupportedAudioFormats() const = 0;
    
    /**
     * Get supported output formats
     * @return Vector of supported output format names
     */
    virtual std::vector<std::string> getSupportedOutputFormats() const = 0;
    
    /**
     * Validate audio files
     * @param filePaths Vector of file paths to validate
     * @return Map of file path to validation result (true if valid)
     */
    virtual std::map<std::string, bool> validateAudioFiles(const std::vector<std::string>& filePaths) const = 0;
    
    /**
     * Estimate processing time
     * @param filePaths Vector of file paths
     * @param config Job configuration
     * @return Estimated processing time in seconds
     */
    virtual float estimateProcessingTime(const std::vector<std::string>& filePaths,
                                        const BatchJobConfig& config) const = 0;
    
    /**
     * Update configuration
     * @param config New batch processing configuration
     * @return true if update successful
     */
    virtual bool updateConfiguration(const BatchProcessingConfig& config) = 0;
    
    /**
     * Get current configuration
     * @return Current batch processing configuration
     */
    virtual BatchProcessingConfig getCurrentConfiguration() const = 0;
    
    /**
     * Check if manager is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
    
    /**
     * Get last error message
     * @return Last error message
     */
    virtual std::string getLastError() const = 0;
    
    /**
     * Shutdown manager gracefully
     */
    virtual void shutdown() = 0;
};

} // namespace advanced
} // namespace stt