#include "audio/load_balanced_pipeline.hpp"
#include "utils/logging.hpp"
#include "utils/performance_monitor.hpp"
#include <algorithm>
#include <numeric>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#else
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

namespace speechrnt {
namespace audio {

PriorityJobQueue::PriorityJobQueue()
    : maxQueueSize_(1000)
    , currentQueueSize_(0)
    , totalJobsSubmitted_(0)
    , totalJobsRetrieved_(0)
    , totalJobsCancelled_(0) {
}

PriorityJobQueue::~PriorityJobQueue() {
    clear();
}

bool PriorityJobQueue::initialize(size_t maxQueueSize) {
    maxQueueSize_ = maxQueueSize;
    
    // Initialize priority queues
    priorityQueues_[ProcessingPriority::CRITICAL] = std::queue<ProcessingJob>();
    priorityQueues_[ProcessingPriority::HIGH] = std::queue<ProcessingJob>();
    priorityQueues_[ProcessingPriority::NORMAL] = std::queue<ProcessingJob>();
    priorityQueues_[ProcessingPriority::LOW] = std::queue<ProcessingJob>();
    priorityQueues_[ProcessingPriority::BACKGROUND] = std::queue<ProcessingJob>();
    
    speechrnt::utils::Logger::info("PriorityJobQueue initialized with max size " + 
                       std::to_string(maxQueueSize));
    
    return true;
}

bool PriorityJobQueue::submitJob(const ProcessingJob& job) {
    std::unique_lock<std::mutex> lock(queueMutex_);
    
    // Check if queue is full
    if (currentQueueSize_ >= maxQueueSize_) {
        // Try to remove timed out jobs first
        removeTimedOutJobs();
        
        if (currentQueueSize_ >= maxQueueSize_) {
            speechrnt::utils::Logger::warn("Job queue full, rejecting job " + std::to_string(job.jobId));
            return false;
        }
    }
    
    // Add job to appropriate priority queue
    priorityQueues_[job.priority].push(job);
    currentQueueSize_++;
    totalJobsSubmitted_++;
    
    // Notify waiting workers
    queueCondition_.notify_one();
    
    speechrnt::utils::Logger::debug("Job " + std::to_string(job.jobId) + " submitted with priority " + 
                        std::to_string(static_cast<int>(job.priority)));
    
    return true;
}

bool PriorityJobQueue::getNextJob(ProcessingJob& job) {
    std::unique_lock<std::mutex> lock(queueMutex_);
    
    // Wait for jobs to be available
    queueCondition_.wait(lock, [this] { return currentQueueSize_ > 0; });
    
    // Get highest priority job
    ProcessingPriority highestPriority = getHighestPriorityWithJobs();
    if (priorityQueues_.find(highestPriority) != priorityQueues_.end() &&
        !priorityQueues_[highestPriority].empty()) {
        
        job = priorityQueues_[highestPriority].front();
        priorityQueues_[highestPriority].pop();
        currentQueueSize_--;
        totalJobsRetrieved_++;
        
        return true;
    }
    
    return false;
}

bool PriorityJobQueue::getNextJobWithPriority(ProcessingJob& job, ProcessingPriority minPriority) {
    std::unique_lock<std::mutex> lock(queueMutex_);
    
    // Check for jobs with required priority or higher
    for (int p = static_cast<int>(ProcessingPriority::CRITICAL); 
         p <= static_cast<int>(minPriority); ++p) {
        
        ProcessingPriority priority = static_cast<ProcessingPriority>(p);
        if (priorityQueues_.find(priority) != priorityQueues_.end() &&
            !priorityQueues_[priority].empty()) {
            
            job = priorityQueues_[priority].front();
            priorityQueues_[priority].pop();
            currentQueueSize_--;
            totalJobsRetrieved_++;
            
            return true;
        }
    }
    
    return false;
}

bool PriorityJobQueue::cancelJob(uint64_t jobId) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    // Search through all priority queues
    for (auto& [priority, queue] : priorityQueues_) {
        std::queue<ProcessingJob> tempQueue;
        bool found = false;
        
        while (!queue.empty()) {
            ProcessingJob job = queue.front();
            queue.pop();
            
            if (job.jobId == jobId) {
                found = true;
                currentQueueSize_--;
                totalJobsCancelled_++;
            } else {
                tempQueue.push(job);
            }
        }
        
        // Restore queue without cancelled job
        queue = tempQueue;
        
        if (found) {
            speechrnt::utils::Logger::debug("Job " + std::to_string(jobId) + " cancelled");
            return true;
        }
    }
    
    return false;
}

std::map<std::string, double> PriorityJobQueue::getQueueStats() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    std::map<std::string, double> stats;
    stats["total_jobs_submitted"] = static_cast<double>(totalJobsSubmitted_.load());
    stats["total_jobs_retrieved"] = static_cast<double>(totalJobsRetrieved_.load());
    stats["total_jobs_cancelled"] = static_cast<double>(totalJobsCancelled_.load());
    stats["current_queue_size"] = static_cast<double>(currentQueueSize_);
    stats["max_queue_size"] = static_cast<double>(maxQueueSize_);
    stats["queue_utilization"] = static_cast<double>(currentQueueSize_) / maxQueueSize_;
    
    // Per-priority statistics
    stats["critical_jobs"] = static_cast<double>(getJobCountByPriority(ProcessingPriority::CRITICAL));
    stats["high_jobs"] = static_cast<double>(getJobCountByPriority(ProcessingPriority::HIGH));
    stats["normal_jobs"] = static_cast<double>(getJobCountByPriority(ProcessingPriority::NORMAL));
    stats["low_jobs"] = static_cast<double>(getJobCountByPriority(ProcessingPriority::LOW));
    stats["background_jobs"] = static_cast<double>(getJobCountByPriority(ProcessingPriority::BACKGROUND));
    
    return stats;
}

size_t PriorityJobQueue::getQueueSize() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return currentQueueSize_;
}

size_t PriorityJobQueue::getJobCountByPriority(ProcessingPriority priority) const {
    auto it = priorityQueues_.find(priority);
    if (it != priorityQueues_.end()) {
        return it->second.size();
    }
    return 0;
}

void PriorityJobQueue::clear() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    for (auto& [priority, queue] : priorityQueues_) {
        while (!queue.empty()) {
            queue.pop();
        }
    }
    
    currentQueueSize_ = 0;
    speechrnt::utils::Logger::info("Job queue cleared");
}

void PriorityJobQueue::setMaxQueueSize(size_t maxSize) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    maxQueueSize_ = maxSize;
    speechrnt::utils::Logger::info("Max queue size set to " + std::to_string(maxSize));
}

ProcessingPriority PriorityJobQueue::getHighestPriorityWithJobs() const {
    // Check priorities from highest to lowest
    for (int p = static_cast<int>(ProcessingPriority::CRITICAL); 
         p <= static_cast<int>(ProcessingPriority::BACKGROUND); ++p) {
        
        ProcessingPriority priority = static_cast<ProcessingPriority>(p);
        auto it = priorityQueues_.find(priority);
        if (it != priorityQueues_.end() && !it->second.empty()) {
            return priority;
        }
    }
    
    return ProcessingPriority::NORMAL; // Default
}

void PriorityJobQueue::removeTimedOutJobs() {
    for (auto& [priority, queue] : priorityQueues_) {
        std::queue<ProcessingJob> tempQueue;
        
        while (!queue.empty()) {
            ProcessingJob job = queue.front();
            queue.pop();
            
            if (!isJobTimedOut(job)) {
                tempQueue.push(job);
            } else {
                currentQueueSize_--;
                totalJobsCancelled_++;
            }
        }
        
        queue = tempQueue;
    }
}

bool PriorityJobQueue::isJobTimedOut(const ProcessingJob& job) const {
    return std::chrono::steady_clock::now() > job.deadline;
}

ResourceMonitor::ResourceMonitor()
    : updateIntervalMs_(1000)
    , monitoring_(false) {
}

ResourceMonitor::~ResourceMonitor() {
    stopMonitoring();
}

bool ResourceMonitor::initialize(int updateIntervalMs) {
    updateIntervalMs_ = updateIntervalMs;
    
    speechrnt::utils::Logger::info("ResourceMonitor initialized with " + 
                       std::to_string(updateIntervalMs) + "ms update interval");
    
    return true;
}

bool ResourceMonitor::startMonitoring() {
    if (monitoring_.load()) {
        speechrnt::utils::Logger::warn("Resource monitoring already started");
        return true;
    }
    
    monitoring_ = true;
    monitoringThread_ = std::make_unique<std::thread>(&ResourceMonitor::monitoringLoop, this);
    
    speechrnt::utils::Logger::info("Resource monitoring started");
    return true;
}

void ResourceMonitor::stopMonitoring() {
    if (!monitoring_.load()) {
        return;
    }
    
    monitoring_ = false;
    
    if (monitoringThread_ && monitoringThread_->joinable()) {
        monitoringThread_->join();
    }
    
    speechrnt::utils::Logger::info("Resource monitoring stopped");
}

SystemResources ResourceMonitor::getCurrentResources() const {
    std::lock_guard<std::mutex> lock(resourcesMutex_);
    return currentResources_;
}

bool ResourceMonitor::isResourceConstrained(float cpuThreshold, float memoryThreshold) const {
    std::lock_guard<std::mutex> lock(resourcesMutex_);
    return currentResources_.cpuUsage > cpuThreshold || 
           currentResources_.memoryUsage > memoryThreshold;
}

float ResourceMonitor::getAvailableCapacity() const {
    std::lock_guard<std::mutex> lock(resourcesMutex_);
    
    // Calculate available capacity based on CPU and memory
    float cpuCapacity = 1.0f - currentResources_.cpuUsage;
    float memoryCapacity = 1.0f - currentResources_.memoryUsage;
    
    // Return the more constraining factor
    return std::min(cpuCapacity, memoryCapacity);
}

void ResourceMonitor::registerResourceCallback(std::function<void(const SystemResources&)> callback) {
    std::lock_guard<std::mutex> lock(resourcesMutex_);
    resourceCallbacks_.push_back(callback);
}

void ResourceMonitor::updateActiveThreadCount(size_t count) {
    std::lock_guard<std::mutex> lock(resourcesMutex_);
    currentResources_.activeThreads = count;
}

void ResourceMonitor::updateQueueSize(size_t size) {
    std::lock_guard<std::mutex> lock(resourcesMutex_);
    currentResources_.queuedJobs = size;
}

void ResourceMonitor::updateAverageLatency(float latencyMs) {
    std::lock_guard<std::mutex> lock(resourcesMutex_);
    currentResources_.averageLatency = latencyMs;
}

void ResourceMonitor::monitoringLoop() {
    speechrnt::utils::Logger::info("Resource monitoring loop started");
    
    while (monitoring_.load()) {
        try {
            SystemResources resources = measureSystemResources();
            
            {
                std::lock_guard<std::mutex> lock(resourcesMutex_);
                
                // Update resources but preserve manually set values
                resources.activeThreads = currentResources_.activeThreads;
                resources.queuedJobs = currentResources_.queuedJobs;
                resources.averageLatency = currentResources_.averageLatency;
                
                bool significantChange = 
                    std::abs(resources.cpuUsage - currentResources_.cpuUsage) > 0.1f ||
                    std::abs(resources.memoryUsage - currentResources_.memoryUsage) > 0.1f;
                
                currentResources_ = resources;
                
                if (significantChange) {
                    notifyResourceChange(resources);
                }
            }
            
            // Record performance metrics
            speechrnt::utils::PerformanceMonitor::getInstance().recordValue(
                "system.cpu_usage", resources.cpuUsage);
            speechrnt::utils::PerformanceMonitor::getInstance().recordValue(
                "system.memory_usage", resources.memoryUsage);
            
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Resource monitoring error: " + std::string(e.what()));
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(updateIntervalMs_));
    }
    
    speechrnt::utils::Logger::info("Resource monitoring loop stopped");
}

SystemResources ResourceMonitor::measureSystemResources() {
    SystemResources resources;
    
    resources.cpuUsage = getCpuUsage();
    resources.memoryUsage = getMemoryUsage();
    resources.gpuUsage = getGpuUsage();
    
    // Determine if system is resource constrained
    resources.resourceConstrained = resources.cpuUsage > 0.8f || 
                                   resources.memoryUsage > 0.8f;
    
    return resources;
}

void ResourceMonitor::notifyResourceChange(const SystemResources& resources) {
    for (const auto& callback : resourceCallbacks_) {
        try {
            callback(resources);
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Resource callback error: " + std::string(e.what()));
        }
    }
}

float ResourceMonitor::getCpuUsage() const {
#ifdef _WIN32
    // Windows implementation
    static ULARGE_INTEGER lastCPU, lastSysCPU, lastUserCPU;
    static int numProcessors = -1;
    static HANDLE self = GetCurrentProcess();
    
    if (numProcessors == -1) {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        numProcessors = sysInfo.dwNumberOfProcessors;
    }
    
    FILETIME ftime, fsys, fuser;
    ULARGE_INTEGER now, sys, user;
    
    GetSystemTimeAsFileTime(&ftime);
    memcpy(&now, &ftime, sizeof(FILETIME));
    
    GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
    memcpy(&sys, &fsys, sizeof(FILETIME));
    memcpy(&user, &fuser, sizeof(FILETIME));
    
    float percent = 0.0f;
    if (lastCPU.QuadPart != 0) {
        percent = static_cast<float>(sys.QuadPart - lastSysCPU.QuadPart) +
                 static_cast<float>(user.QuadPart - lastUserCPU.QuadPart);
        percent /= static_cast<float>(now.QuadPart - lastCPU.QuadPart);
        percent /= numProcessors;
    }
    
    lastCPU = now;
    lastUserCPU = user;
    lastSysCPU = sys;
    
    return std::min(1.0f, std::max(0.0f, percent));
#else
    // Linux implementation (simplified)
    static long lastTotalUser = 0, lastTotalUserLow = 0, lastTotalSys = 0, lastTotalIdle = 0;
    
    FILE* file = fopen("/proc/stat", "r");
    if (!file) {
        return 0.0f;
    }
    
    long totalUser, totalUserLow, totalSys, totalIdle;
    fscanf(file, "cpu %ld %ld %ld %ld", &totalUser, &totalUserLow, &totalSys, &totalIdle);
    fclose(file);
    
    if (lastTotalUser == 0) {
        lastTotalUser = totalUser;
        lastTotalUserLow = totalUserLow;
        lastTotalSys = totalSys;
        lastTotalIdle = totalIdle;
        return 0.0f;
    }
    
    long total = (totalUser - lastTotalUser) + (totalUserLow - lastTotalUserLow) +
                (totalSys - lastTotalSys);
    long totalTime = total + (totalIdle - lastTotalIdle);
    
    float percent = totalTime > 0 ? static_cast<float>(total) / totalTime : 0.0f;
    
    lastTotalUser = totalUser;
    lastTotalUserLow = totalUserLow;
    lastTotalSys = totalSys;
    lastTotalIdle = totalIdle;
    
    return std::min(1.0f, std::max(0.0f, percent));
#endif
}

float ResourceMonitor::getMemoryUsage() const {
#ifdef _WIN32
    // Windows implementation
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    
    DWORDLONG totalPhysMem = memInfo.ullTotalPhys;
    DWORDLONG physMemUsed = memInfo.ullTotalPhys - memInfo.ullAvailPhys;
    
    return static_cast<float>(physMemUsed) / static_cast<float>(totalPhysMem);
#else
    // Linux implementation
    struct sysinfo memInfo;
    sysinfo(&memInfo);
    
    long long totalPhysMem = memInfo.totalram;
    totalPhysMem *= memInfo.mem_unit;
    
    long long physMemUsed = memInfo.totalram - memInfo.freeram;
    physMemUsed *= memInfo.mem_unit;
    
    return static_cast<float>(physMemUsed) / static_cast<float>(totalPhysMem);
#endif
}

float ResourceMonitor::getGpuUsage() const {
    // Placeholder - would need GPU-specific libraries (CUDA, OpenCL, etc.)
    return 0.0f;
}

LoadBalancedProcessingPipeline::LoadBalancedProcessingPipeline()
    : pipelineActive_(false)
    , numWorkerThreads_(4)
    , nextJobId_(1)
    , gracefulDegradation_(true)
    , cpuThreshold_(0.8f)
    , memoryThreshold_(0.8f) {
    
    // Set default job type priorities
    jobTypePriorities_[ProcessingJobType::REAL_TIME_STREAM] = ProcessingPriority::CRITICAL;
    jobTypePriorities_[ProcessingJobType::INTERACTIVE_REQUEST] = ProcessingPriority::HIGH;
    jobTypePriorities_[ProcessingJobType::BATCH_TRANSCRIPTION] = ProcessingPriority::LOW;
    jobTypePriorities_[ProcessingJobType::BACKGROUND_TASK] = ProcessingPriority::BACKGROUND;
    jobTypePriorities_[ProcessingJobType::SYSTEM_TASK] = ProcessingPriority::NORMAL;
}

LoadBalancedProcessingPipeline::~LoadBalancedProcessingPipeline() {
    stop();
}

bool LoadBalancedProcessingPipeline::initialize(size_t numWorkerThreads, size_t maxQueueSize) {
    numWorkerThreads_ = numWorkerThreads;
    
    // Initialize components
    jobQueue_ = std::make_unique<PriorityJobQueue>();
    resourceMonitor_ = std::make_unique<ResourceMonitor>();
    
    bool queueOk = jobQueue_->initialize(maxQueueSize);
    bool monitorOk = resourceMonitor_->initialize(1000); // 1 second update interval
    
    if (queueOk && monitorOk) {
        // Register for resource change notifications
        resourceMonitor_->registerResourceCallback(
            [this](const SystemResources& resources) {
                onResourceChange(resources);
            });
        
        speechrnt::utils::Logger::info("LoadBalancedProcessingPipeline initialized with " + 
                           std::to_string(numWorkerThreads) + " worker threads");
        return true;
    } else {
        speechrnt::utils::Logger::error("LoadBalancedProcessingPipeline initialization failed");
        return false;
    }
}

bool LoadBalancedProcessingPipeline::start() {
    if (pipelineActive_.load()) {
        speechrnt::utils::Logger::warn("Processing pipeline already active");
        return true;
    }
    
    pipelineActive_ = true;
    
    // Start resource monitoring
    resourceMonitor_->startMonitoring();
    
    // Create worker threads
    workerThreads_.reserve(numWorkerThreads_);
    for (size_t i = 0; i < numWorkerThreads_; ++i) {
        workerThreads_.push_back(
            std::make_unique<std::thread>(&LoadBalancedProcessingPipeline::workerLoop, this, i));
    }
    
    speechrnt::utils::Logger::info("Processing pipeline started with " + 
                       std::to_string(numWorkerThreads_) + " workers");
    return true;
}

void LoadBalancedProcessingPipeline::stop() {
    if (!pipelineActive_.load()) {
        return;
    }
    
    pipelineActive_ = false;
    
    // Stop resource monitoring
    resourceMonitor_->stopMonitoring();
    
    // Wait for worker threads to finish
    for (auto& thread : workerThreads_) {
        if (thread && thread->joinable()) {
            thread->join();
        }
    }
    workerThreads_.clear();
    
    speechrnt::utils::Logger::info("Processing pipeline stopped");
}

uint64_t LoadBalancedProcessingPipeline::submitJob(const ProcessingJob& job) {
    if (!pipelineActive_.load()) {
        speechrnt::utils::Logger::error("Cannot submit job: pipeline not active");
        return 0;
    }
    
    ProcessingJob jobCopy = job;
    if (jobCopy.jobId == 0) {
        jobCopy.jobId = nextJobId_++;
    }
    
    // Set priority based on job type if not explicitly set
    if (jobCopy.priority == ProcessingPriority::NORMAL) {
        jobCopy.priority = getJobPriority(jobCopy.type);
    }
    
    if (jobQueue_->submitJob(jobCopy)) {
        // Update resource monitor
        resourceMonitor_->updateQueueSize(jobQueue_->getQueueSize());
        
        speechrnt::utils::Logger::debug("Job " + std::to_string(jobCopy.jobId) + " submitted successfully");
        return jobCopy.jobId;
    } else {
        speechrnt::utils::Logger::error("Failed to submit job " + std::to_string(jobCopy.jobId));
        return 0;
    }
}

uint64_t LoadBalancedProcessingPipeline::submitRealTimeJob(std::function<void()> task, 
                                                          const std::string& description) {
    ProcessingJob job;
    job.jobId = nextJobId_++;
    job.type = ProcessingJobType::REAL_TIME_STREAM;
    job.priority = ProcessingPriority::CRITICAL;
    job.task = task;
    job.description = description;
    job.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200); // 200ms deadline
    job.estimatedProcessingTimeMs = 50; // Estimate 50ms processing time
    job.resourceRequirement = 8; // High resource requirement
    
    return submitJob(job);
}

uint64_t LoadBalancedProcessingPipeline::submitBatchJob(std::function<void()> task, 
                                                       const std::string& description) {
    ProcessingJob job;
    job.jobId = nextJobId_++;
    job.type = ProcessingJobType::BATCH_TRANSCRIPTION;
    job.priority = ProcessingPriority::LOW;
    job.task = task;
    job.description = description;
    job.deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5); // 5 minute deadline
    job.estimatedProcessingTimeMs = 1000; // Estimate 1 second processing time
    job.resourceRequirement = 3; // Lower resource requirement
    
    return submitJob(job);
}

bool LoadBalancedProcessingPipeline::cancelJob(uint64_t jobId) {
    return jobQueue_->cancelJob(jobId);
}

ProcessingStats LoadBalancedProcessingPipeline::getProcessingStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

SystemResources LoadBalancedProcessingPipeline::getCurrentResources() const {
    return resourceMonitor_->getCurrentResources();
}

void LoadBalancedProcessingPipeline::setJobTypePriority(ProcessingJobType jobType, 
                                                       ProcessingPriority priority) {
    jobTypePriorities_[jobType] = priority;
    speechrnt::utils::Logger::info("Job type " + std::to_string(static_cast<int>(jobType)) + 
                       " priority set to " + std::to_string(static_cast<int>(priority)));
}

void LoadBalancedProcessingPipeline::setGracefulDegradation(bool enabled) {
    gracefulDegradation_ = enabled;
    speechrnt::utils::Logger::info("Graceful degradation " + std::string(enabled ? "enabled" : "disabled"));
}

void LoadBalancedProcessingPipeline::setResourceThresholds(float cpuThreshold, float memoryThreshold) {
    cpuThreshold_ = cpuThreshold;
    memoryThreshold_ = memoryThreshold;
    speechrnt::utils::Logger::info("Resource thresholds set: CPU=" + std::to_string(cpuThreshold) + 
                       ", Memory=" + std::to_string(memoryThreshold));
}

bool LoadBalancedProcessingPipeline::isHealthy() const {
    SystemResources resources = resourceMonitor_->getCurrentResources();
    ProcessingStats stats = getProcessingStats();
    
    // Check various health indicators
    bool resourcesHealthy = !resources.resourceConstrained;
    bool queueHealthy = resources.queuedJobs < 100; // Arbitrary threshold
    bool latencyHealthy = resources.averageLatency < 1000.0f; // 1 second threshold
    
    return resourcesHealthy && queueHealthy && latencyHealthy;
}

std::map<std::string, double> LoadBalancedProcessingPipeline::getPipelineStats() const {
    std::map<std::string, double> stats;
    
    // Combine statistics from components
    auto queueStats = jobQueue_->getQueueStats();
    auto processingStats = getProcessingStats();
    auto resources = resourceMonitor_->getCurrentResources();
    
    // Queue statistics
    for (const auto& [key, value] : queueStats) {
        stats["queue." + key] = value;
    }
    
    // Processing statistics
    stats["processing.total_jobs_processed"] = static_cast<double>(processingStats.totalJobsProcessed);
    stats["processing.total_jobs_completed"] = static_cast<double>(processingStats.totalJobsCompleted);
    stats["processing.total_jobs_failed"] = static_cast<double>(processingStats.totalJobsFailed);
    stats["processing.average_processing_time"] = static_cast<double>(processingStats.averageProcessingTime);
    stats["processing.average_queue_time"] = static_cast<double>(processingStats.averageQueueTime);
    stats["processing.throughput"] = static_cast<double>(processingStats.throughputJobsPerSecond);
    
    // Resource statistics
    stats["resources.cpu_usage"] = static_cast<double>(resources.cpuUsage);
    stats["resources.memory_usage"] = static_cast<double>(resources.memoryUsage);
    stats["resources.active_threads"] = static_cast<double>(resources.activeThreads);
    stats["resources.average_latency"] = static_cast<double>(resources.averageLatency);
    stats["resources.resource_constrained"] = resources.resourceConstrained ? 1.0 : 0.0;
    
    // Pipeline configuration
    stats["config.num_worker_threads"] = static_cast<double>(numWorkerThreads_);
    stats["config.graceful_degradation"] = gracefulDegradation_ ? 1.0 : 0.0;
    stats["config.cpu_threshold"] = static_cast<double>(cpuThreshold_);
    stats["config.memory_threshold"] = static_cast<double>(memoryThreshold_);
    stats["config.pipeline_active"] = pipelineActive_.load() ? 1.0 : 0.0;
    
    return stats;
}

void LoadBalancedProcessingPipeline::workerLoop(size_t workerId) {
    speechrnt::utils::Logger::info("Worker thread " + std::to_string(workerId) + " started");
    
    while (pipelineActive_.load()) {
        try {
            ProcessingJob job;
            
            // Try to get a job from the queue
            if (jobQueue_->getNextJob(job)) {
                auto startTime = std::chrono::high_resolution_clock::now();
                
                // Check if we should process this job based on current conditions
                if (shouldProcessJob(job)) {
                    processJob(job);
                    
                    auto endTime = std::chrono::high_resolution_clock::now();
                    float processingTime = std::chrono::duration_cast<std::chrono::microseconds>(
                        endTime - startTime).count() / 1000.0f; // Convert to milliseconds
                    
                    float queueTime = std::chrono::duration_cast<std::chrono::microseconds>(
                        startTime - job.submissionTime).count() / 1000.0f;
                    
                    updateStatistics(job, processingTime, queueTime);
                    
                    speechrnt::utils::Logger::debug("Worker " + std::to_string(workerId) + 
                                        " completed job " + std::to_string(job.jobId) + 
                                        " in " + std::to_string(processingTime) + "ms");
                } else {
                    // Job was skipped due to resource constraints
                    speechrnt::utils::Logger::debug("Worker " + std::to_string(workerId) + 
                                        " skipped job " + std::to_string(job.jobId) + 
                                        " due to resource constraints");
                }
                
                // Update active thread count
                resourceMonitor_->updateActiveThreadCount(numWorkerThreads_);
                resourceMonitor_->updateQueueSize(jobQueue_->getQueueSize());
            } else {
                // No jobs available, sleep briefly
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Worker " + std::to_string(workerId) + 
                               " error: " + std::string(e.what()));
        }
    }
    
    speechrnt::utils::Logger::info("Worker thread " + std::to_string(workerId) + " stopped");
}

void LoadBalancedProcessingPipeline::processJob(const ProcessingJob& job) {
    try {
        if (job.task) {
            job.task();
            
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.totalJobsCompleted++;
        }
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Job " + std::to_string(job.jobId) + 
                           " failed: " + std::string(e.what()));
        
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.totalJobsFailed++;
    }
}

void LoadBalancedProcessingPipeline::updateStatistics(const ProcessingJob& job, 
                                                     float processingTime, float queueTime) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    stats_.totalJobsProcessed++;
    
    // Update running averages
    const float alpha = 0.1f; // Smoothing factor
    stats_.averageProcessingTime = alpha * processingTime + 
                                  (1.0f - alpha) * stats_.averageProcessingTime;
    stats_.averageQueueTime = alpha * queueTime + 
                             (1.0f - alpha) * stats_.averageQueueTime;
    
    // Update recent processing times for throughput calculation
    recentProcessingTimes_.push_back(processingTime);
    if (recentProcessingTimes_.size() > 100) {
        recentProcessingTimes_.erase(recentProcessingTimes_.begin());
    }
    
    // Calculate throughput
    if (!recentProcessingTimes_.empty()) {
        float avgTime = std::accumulate(recentProcessingTimes_.begin(), 
                                       recentProcessingTimes_.end(), 0.0f) / 
                       recentProcessingTimes_.size();
        stats_.throughputJobsPerSecond = avgTime > 0 ? (1000.0f / avgTime) : 0.0f;
    }
    
    stats_.lastUpdate = std::chrono::steady_clock::now();
    
    // Update resource monitor with average latency
    resourceMonitor_->updateAverageLatency(stats_.averageProcessingTime);
}

void LoadBalancedProcessingPipeline::handleResourceConstraints() {
    if (gracefulDegradation_) {
        performGracefulDegradation();
    }
}

ProcessingPriority LoadBalancedProcessingPipeline::getJobPriority(ProcessingJobType jobType) const {
    auto it = jobTypePriorities_.find(jobType);
    if (it != jobTypePriorities_.end()) {
        return it->second;
    }
    return ProcessingPriority::NORMAL;
}

bool LoadBalancedProcessingPipeline::shouldProcessJob(const ProcessingJob& job) const {
    SystemResources resources = resourceMonitor_->getCurrentResources();
    
    // Always process critical jobs
    if (job.priority == ProcessingPriority::CRITICAL) {
        return true;
    }
    
    // Skip low priority jobs if resources are constrained
    if (resources.resourceConstrained && 
        (job.priority == ProcessingPriority::LOW || job.priority == ProcessingPriority::BACKGROUND)) {
        return false;
    }
    
    // Check if job has timed out
    if (std::chrono::steady_clock::now() > job.deadline) {
        return false;
    }
    
    return true;
}

void LoadBalancedProcessingPipeline::performGracefulDegradation() {
    // Reduce processing of low-priority jobs
    // This could involve adjusting queue priorities, reducing worker threads, etc.
    speechrnt::utils::Logger::info("Performing graceful degradation due to resource constraints");
}

void LoadBalancedProcessingPipeline::onResourceChange(const SystemResources& resources) {
    if (resources.resourceConstrained) {
        handleResourceConstraints();
    }
}

// Factory implementations
std::unique_ptr<LoadBalancedProcessingPipeline> ProcessingPipelineFactory::createRealTimePipeline(
    size_t numThreads) {
    
    auto pipeline = std::make_unique<LoadBalancedProcessingPipeline>();
    pipeline->initialize(numThreads, 500); // Smaller queue for real-time
    
    // Configure for real-time processing
    pipeline->setJobTypePriority(ProcessingJobType::REAL_TIME_STREAM, ProcessingPriority::CRITICAL);
    pipeline->setJobTypePriority(ProcessingJobType::INTERACTIVE_REQUEST, ProcessingPriority::HIGH);
    pipeline->setJobTypePriority(ProcessingJobType::BATCH_TRANSCRIPTION, ProcessingPriority::BACKGROUND);
    
    pipeline->setResourceThresholds(0.7f, 0.7f); // Lower thresholds for real-time
    pipeline->setGracefulDegradation(true);
    
    return pipeline;
}

std::unique_ptr<LoadBalancedProcessingPipeline> ProcessingPipelineFactory::createBatchPipeline(
    size_t numThreads) {
    
    auto pipeline = std::make_unique<LoadBalancedProcessingPipeline>();
    pipeline->initialize(numThreads, 2000); // Larger queue for batch processing
    
    // Configure for batch processing
    pipeline->setJobTypePriority(ProcessingJobType::BATCH_TRANSCRIPTION, ProcessingPriority::HIGH);
    pipeline->setJobTypePriority(ProcessingJobType::BACKGROUND_TASK, ProcessingPriority::NORMAL);
    pipeline->setJobTypePriority(ProcessingJobType::REAL_TIME_STREAM, ProcessingPriority::CRITICAL);
    
    pipeline->setResourceThresholds(0.9f, 0.9f); // Higher thresholds for batch
    pipeline->setGracefulDegradation(true);
    
    return pipeline;
}

std::unique_ptr<LoadBalancedProcessingPipeline> ProcessingPipelineFactory::createHybridPipeline(
    size_t numThreads) {
    
    auto pipeline = std::make_unique<LoadBalancedProcessingPipeline>();
    pipeline->initialize(numThreads, 1000); // Balanced queue size
    
    // Balanced configuration
    pipeline->setResourceThresholds(0.8f, 0.8f);
    pipeline->setGracefulDegradation(true);
    
    return pipeline;
}

std::unique_ptr<LoadBalancedProcessingPipeline> ProcessingPipelineFactory::createCustomPipeline(
    const std::map<std::string, std::string>& config) {
    
    auto pipeline = std::make_unique<LoadBalancedProcessingPipeline>();
    
    // Parse configuration
    size_t numThreads = 4;
    size_t queueSize = 1000;
    float cpuThreshold = 0.8f;
    float memoryThreshold = 0.8f;
    
    auto threadsIt = config.find("num_threads");
    if (threadsIt != config.end()) {
        numThreads = std::stoul(threadsIt->second);
    }
    
    auto queueIt = config.find("queue_size");
    if (queueIt != config.end()) {
        queueSize = std::stoul(queueIt->second);
    }
    
    auto cpuIt = config.find("cpu_threshold");
    if (cpuIt != config.end()) {
        cpuThreshold = std::stof(cpuIt->second);
    }
    
    auto memoryIt = config.find("memory_threshold");
    if (memoryIt != config.end()) {
        memoryThreshold = std::stof(memoryIt->second);
    }
    
    pipeline->initialize(numThreads, queueSize);
    pipeline->setResourceThresholds(cpuThreshold, memoryThreshold);
    
    return pipeline;
}

} // namespace audio
} // namespace speechrnt