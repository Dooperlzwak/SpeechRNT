#pragma once

#include "audio/streaming_optimizer.hpp"
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <mutex>
#include <memory>
#include <functional>
#include <chrono>
#include <vector>
#include <map>

namespace speechrnt {
namespace audio {

/**
 * Processing priority levels
 */
enum class ProcessingPriority {
    CRITICAL,    // Real-time streams that must be processed immediately
    HIGH,        // Interactive streams with low latency requirements
    NORMAL,      // Standard processing requests
    LOW,         // Batch processing that can be delayed
    BACKGROUND   // Background tasks that run when resources are available
};

/**
 * Processing job types
 */
enum class ProcessingJobType {
    REAL_TIME_STREAM,     // Live audio streaming
    INTERACTIVE_REQUEST,  // Interactive transcription request
    BATCH_TRANSCRIPTION,  // Batch file processing
    BACKGROUND_TASK,      // Background maintenance tasks
    SYSTEM_TASK          // System-level tasks
};

/**
 * Processing job information
 */
struct ProcessingJob {
    uint64_t jobId;
    ProcessingJobType type;
    ProcessingPriority priority;
    std::chrono::steady_clock::time_point submissionTime;
    std::chrono::steady_clock::time_point deadline;
    std::function<void()> task;
    std::string description;
    size_t estimatedProcessingTimeMs;
    size_t resourceRequirement; // Relative resource requirement (1-10)
    
    ProcessingJob() 
        : jobId(0), type(ProcessingJobType::INTERACTIVE_REQUEST)
        , priority(ProcessingPriority::NORMAL)
        , submissionTime(std::chrono::steady_clock::now())
        , deadline(std::chrono::steady_clock::now() + std::chrono::seconds(30))
        , estimatedProcessingTimeMs(100), resourceRequirement(5) {}
        
    ProcessingJob(uint64_t id, ProcessingJobType jobType, ProcessingPriority prio,
                 std::function<void()> taskFunc, const std::string& desc = "")
        : jobId(id), type(jobType), priority(prio), task(taskFunc), description(desc)
        , submissionTime(std::chrono::steady_clock::now())
        , deadline(std::chrono::steady_clock::now() + std::chrono::seconds(30))
        , estimatedProcessingTimeMs(100), resourceRequirement(5) {}
};

/**
 * System resource monitor
 */
struct SystemResources {
    float cpuUsage;          // CPU usage percentage (0.0-1.0)
    float memoryUsage;       // Memory usage percentage (0.0-1.0)
    float gpuUsage;          // GPU usage percentage (0.0-1.0)
    size_t activeThreads;    // Number of active processing threads
    size_t queuedJobs;       // Number of jobs in queue
    float averageLatency;    // Average processing latency in ms
    bool resourceConstrained; // True if resources are constrained
    
    SystemResources()
        : cpuUsage(0.0f), memoryUsage(0.0f), gpuUsage(0.0f)
        , activeThreads(0), queuedJobs(0), averageLatency(0.0f)
        , resourceConstrained(false) {}
};

/**
 * Processing statistics
 */
struct ProcessingStats {
    uint64_t totalJobsProcessed;
    uint64_t totalJobsQueued;
    uint64_t totalJobsCompleted;
    uint64_t totalJobsFailed;
    uint64_t totalJobsTimedOut;
    float averageProcessingTime;
    float averageQueueTime;
    float throughputJobsPerSecond;
    std::chrono::steady_clock::time_point lastUpdate;
    
    ProcessingStats()
        : totalJobsProcessed(0), totalJobsQueued(0), totalJobsCompleted(0)
        , totalJobsFailed(0), totalJobsTimedOut(0), averageProcessingTime(0.0f)
        , averageQueueTime(0.0f), throughputJobsPerSecond(0.0f)
        , lastUpdate(std::chrono::steady_clock::now()) {}
};

/**
 * Priority-based job queue with load balancing
 */
class PriorityJobQueue {
public:
    PriorityJobQueue();
    ~PriorityJobQueue();
    
    /**
     * Initialize the job queue
     * @param maxQueueSize Maximum number of jobs in queue
     * @return true if initialization successful
     */
    bool initialize(size_t maxQueueSize = 1000);
    
    /**
     * Submit a job to the queue
     * @param job Processing job to submit
     * @return true if job was queued successfully
     */
    bool submitJob(const ProcessingJob& job);
    
    /**
     * Get next job for processing
     * @param job Output job
     * @return true if job is available
     */
    bool getNextJob(ProcessingJob& job);
    
    /**
     * Get next job with specific priority or higher
     * @param job Output job
     * @param minPriority Minimum priority level
     * @return true if job is available
     */
    bool getNextJobWithPriority(ProcessingJob& job, ProcessingPriority minPriority);
    
    /**
     * Cancel job by ID
     * @param jobId Job ID to cancel
     * @return true if job was cancelled
     */
    bool cancelJob(uint64_t jobId);
    
    /**
     * Get queue statistics
     * @return map of queue statistics
     */
    std::map<std::string, double> getQueueStats() const;
    
    /**
     * Get number of jobs in queue
     * @return number of queued jobs
     */
    size_t getQueueSize() const;
    
    /**
     * Get number of jobs with specific priority
     * @param priority Priority level
     * @return number of jobs with that priority
     */
    size_t getJobCountByPriority(ProcessingPriority priority) const;
    
    /**
     * Clear all jobs from queue
     */
    void clear();
    
    /**
     * Set maximum queue size
     * @param maxSize Maximum number of jobs
     */
    void setMaxQueueSize(size_t maxSize);

private:
    mutable std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    
    // Priority queues for different job types
    std::map<ProcessingPriority, std::queue<ProcessingJob>> priorityQueues_;
    
    size_t maxQueueSize_;
    size_t currentQueueSize_;
    
    // Statistics
    std::atomic<uint64_t> totalJobsSubmitted_;
    std::atomic<uint64_t> totalJobsRetrieved_;
    std::atomic<uint64_t> totalJobsCancelled_;
    
    // Private methods
    ProcessingPriority getHighestPriorityWithJobs() const;
    void removeTimedOutJobs();
    bool isJobTimedOut(const ProcessingJob& job) const;
};

/**
 * Resource monitor for system load tracking
 */
class ResourceMonitor {
public:
    ResourceMonitor();
    ~ResourceMonitor();
    
    /**
     * Initialize resource monitoring
     * @param updateIntervalMs Monitoring update interval
     * @return true if initialization successful
     */
    bool initialize(int updateIntervalMs = 1000);
    
    /**
     * Start resource monitoring
     * @return true if monitoring started
     */
    bool startMonitoring();
    
    /**
     * Stop resource monitoring
     */
    void stopMonitoring();
    
    /**
     * Get current system resources
     * @return current resource usage
     */
    SystemResources getCurrentResources() const;
    
    /**
     * Check if system is resource constrained
     * @param cpuThreshold CPU usage threshold (0.0-1.0)
     * @param memoryThreshold Memory usage threshold (0.0-1.0)
     * @return true if system is constrained
     */
    bool isResourceConstrained(float cpuThreshold = 0.8f, 
                              float memoryThreshold = 0.8f) const;
    
    /**
     * Get available processing capacity
     * @return available capacity (0.0-1.0)
     */
    float getAvailableCapacity() const;
    
    /**
     * Register callback for resource changes
     * @param callback Function to call when resources change significantly
     */
    void registerResourceCallback(std::function<void(const SystemResources&)> callback);
    
    /**
     * Update active thread count
     * @param count Number of active threads
     */
    void updateActiveThreadCount(size_t count);
    
    /**
     * Update queue size
     * @param size Current queue size
     */
    void updateQueueSize(size_t size);
    
    /**
     * Update average latency
     * @param latencyMs Average processing latency
     */
    void updateAverageLatency(float latencyMs);

private:
    // Monitoring configuration
    int updateIntervalMs_;
    std::atomic<bool> monitoring_;
    std::unique_ptr<std::thread> monitoringThread_;
    
    // Current resources
    mutable std::mutex resourcesMutex_;
    SystemResources currentResources_;
    
    // Callbacks
    std::vector<std::function<void(const SystemResources&)>> resourceCallbacks_;
    
    // Private methods
    void monitoringLoop();
    SystemResources measureSystemResources();
    void notifyResourceChange(const SystemResources& resources);
    float getCpuUsage() const;
    float getMemoryUsage() const;
    float getGpuUsage() const;
};

/**
 * Load-balanced processing pipeline
 */
class LoadBalancedProcessingPipeline {
public:
    LoadBalancedProcessingPipeline();
    ~LoadBalancedProcessingPipeline();
    
    /**
     * Initialize the processing pipeline
     * @param numWorkerThreads Number of worker threads
     * @param maxQueueSize Maximum queue size
     * @return true if initialization successful
     */
    bool initialize(size_t numWorkerThreads = 4, size_t maxQueueSize = 1000);
    
    /**
     * Start the processing pipeline
     * @return true if pipeline started successfully
     */
    bool start();
    
    /**
     * Stop the processing pipeline
     */
    void stop();
    
    /**
     * Submit a processing job
     * @param job Job to process
     * @return job ID for tracking
     */
    uint64_t submitJob(const ProcessingJob& job);
    
    /**
     * Submit a real-time streaming job
     * @param task Task function to execute
     * @param description Job description
     * @return job ID for tracking
     */
    uint64_t submitRealTimeJob(std::function<void()> task, const std::string& description = "");
    
    /**
     * Submit a batch processing job
     * @param task Task function to execute
     * @param description Job description
     * @return job ID for tracking
     */
    uint64_t submitBatchJob(std::function<void()> task, const std::string& description = "");
    
    /**
     * Cancel a job by ID
     * @param jobId Job ID to cancel
     * @return true if job was cancelled
     */
    bool cancelJob(uint64_t jobId);
    
    /**
     * Get processing statistics
     * @return processing statistics
     */
    ProcessingStats getProcessingStats() const;
    
    /**
     * Get current system resources
     * @return current resource usage
     */
    SystemResources getCurrentResources() const;
    
    /**
     * Set processing priority for job types
     * @param jobType Job type
     * @param priority Priority level
     */
    void setJobTypePriority(ProcessingJobType jobType, ProcessingPriority priority);
    
    /**
     * Enable/disable graceful degradation
     * @param enabled true to enable degradation
     */
    void setGracefulDegradation(bool enabled);
    
    /**
     * Set resource thresholds for degradation
     * @param cpuThreshold CPU threshold (0.0-1.0)
     * @param memoryThreshold Memory threshold (0.0-1.0)
     */
    void setResourceThresholds(float cpuThreshold = 0.8f, float memoryThreshold = 0.8f);
    
    /**
     * Get pipeline health status
     * @return true if pipeline is healthy
     */
    bool isHealthy() const;
    
    /**
     * Get detailed pipeline statistics
     * @return map of pipeline statistics
     */
    std::map<std::string, double> getPipelineStats() const;

private:
    // Core components
    std::unique_ptr<PriorityJobQueue> jobQueue_;
    std::unique_ptr<ResourceMonitor> resourceMonitor_;
    
    // Worker threads
    std::vector<std::unique_ptr<std::thread>> workerThreads_;
    std::atomic<bool> pipelineActive_;
    size_t numWorkerThreads_;
    
    // Job management
    std::atomic<uint64_t> nextJobId_;
    std::map<ProcessingJobType, ProcessingPriority> jobTypePriorities_;
    
    // Configuration
    bool gracefulDegradation_;
    float cpuThreshold_;
    float memoryThreshold_;
    
    // Statistics
    mutable std::mutex statsMutex_;
    ProcessingStats stats_;
    std::vector<float> recentProcessingTimes_;
    std::vector<float> recentQueueTimes_;
    
    // Private methods
    void workerLoop(size_t workerId);
    void processJob(const ProcessingJob& job);
    void updateStatistics(const ProcessingJob& job, float processingTime, float queueTime);
    void handleResourceConstraints();
    ProcessingPriority getJobPriority(ProcessingJobType jobType) const;
    bool shouldProcessJob(const ProcessingJob& job) const;
    void performGracefulDegradation();
    void onResourceChange(const SystemResources& resources);
};

/**
 * Processing pipeline factory for creating optimized pipelines
 */
class ProcessingPipelineFactory {
public:
    /**
     * Create a pipeline optimized for real-time streaming
     * @param numThreads Number of worker threads
     * @return configured pipeline
     */
    static std::unique_ptr<LoadBalancedProcessingPipeline> createRealTimePipeline(
        size_t numThreads = 2);
    
    /**
     * Create a pipeline optimized for batch processing
     * @param numThreads Number of worker threads
     * @return configured pipeline
     */
    static std::unique_ptr<LoadBalancedProcessingPipeline> createBatchPipeline(
        size_t numThreads = 8);
    
    /**
     * Create a hybrid pipeline for mixed workloads
     * @param numThreads Number of worker threads
     * @return configured pipeline
     */
    static std::unique_ptr<LoadBalancedProcessingPipeline> createHybridPipeline(
        size_t numThreads = 4);
    
    /**
     * Create a pipeline with custom configuration
     * @param config Configuration parameters
     * @return configured pipeline
     */
    static std::unique_ptr<LoadBalancedProcessingPipeline> createCustomPipeline(
        const std::map<std::string, std::string>& config);
};

} // namespace audio
} // namespace speechrnt