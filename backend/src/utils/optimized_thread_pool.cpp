#include "utils/optimized_thread_pool.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <sstream>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace utils {

OptimizedThreadPool::OptimizedThreadPool(const PoolConfig& config)
    : config_(config)
    , initialized_(false)
    , shutdownRequested_(false)
    , nextQueue_(0)
    , activeTasks_(0)
    , completedTasks_(0)
    , failedTasks_(0)
    , totalTaskTime_(0.0)
    , totalQueueTime_(0.0)
    , workStealingEvents_(0) {
}

OptimizedThreadPool::~OptimizedThreadPool() {
    shutdown();
}

bool OptimizedThreadPool::initialize() {
    if (initialized_) {
        return true;
    }
    
    // Determine optimal thread count
    size_t numThreads = config_.numThreads;
    if (numThreads == 0) {
        numThreads = getOptimalThreadCount();
    }
    
    if (numThreads == 0) {
        Logger::error("Failed to determine thread count for thread pool");
        return false;
    }
    
    // Create work queues
    workQueues_.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        auto queue = std::make_unique<WorkQueue>();
        queue->setEnablePriority(config_.enablePriority);
        workQueues_.push_back(std::move(queue));
    }
    
    // Create global overflow queue
    globalQueue_ = std::make_unique<WorkQueue>();
    globalQueue_->setEnablePriority(config_.enablePriority);
    
    // Start worker threads
    shutdownRequested_ = false;
    workers_.reserve(numThreads);
    
    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back(&OptimizedThreadPool::workerThread, this, i);
    }
    
    // Set thread affinity if enabled
    if (config_.enableThreadAffinity) {
        for (size_t i = 0; i < numThreads; ++i) {
            setThreadAffinity(i);
        }
    }
    
    initialized_ = true;
    
    Logger::info("OptimizedThreadPool initialized with " + std::to_string(numThreads) + 
                " threads (work stealing: " + (config_.enableWorkStealing ? "enabled" : "disabled") +
                ", priority: " + (config_.enablePriority ? "enabled" : "disabled") + ")");
    
    return true;
}

void OptimizedThreadPool::shutdown() {
    if (!initialized_) {
        return;
    }
    
    shutdownRequested_ = true;
    
    // Wake up all worker threads
    for (auto& queue : workQueues_) {
        // Push dummy tasks to wake up threads
        queue->push(Task([](){}, Priority::NORMAL));
    }
    
    if (globalQueue_) {
        globalQueue_->push(Task([](){}, Priority::NORMAL));
    }
    
    // Wait for all worker threads to finish
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    workers_.clear();
    workQueues_.clear();
    globalQueue_.reset();
    
    initialized_ = false;
    
    Logger::info("OptimizedThreadPool shutdown completed");
}

bool OptimizedThreadPool::submitTask(std::function<void()> task, Priority priority) {
    if (!initialized_ || shutdownRequested_) {
        return false;
    }
    
    // Check queue size limit
    if (getQueueSize() >= config_.maxQueueSize) {
        Logger::warn("Thread pool queue is full, rejecting task");
        return false;
    }
    
    Task wrappedTask(std::move(task), priority);
    
    // Try to submit to least loaded worker queue
    size_t bestQueue = nextQueue_.fetch_add(1) % workQueues_.size();
    size_t minQueueSize = SIZE_MAX;
    
    for (size_t i = 0; i < workQueues_.size(); ++i) {
        size_t queueSize = workQueues_[i]->size();
        if (queueSize < minQueueSize) {
            minQueueSize = queueSize;
            bestQueue = i;
        }
        
        // If we find an empty queue, use it immediately
        if (queueSize == 0) {
            break;
        }
    }
    
    // Submit to the best queue
    workQueues_[bestQueue]->push(std::move(wrappedTask));
    
    return true;
}

void OptimizedThreadPool::waitForAll() {
    if (!initialized_) {
        return;
    }
    
    std::unique_lock<std::mutex> lock(waitMutex_);
    waitCondition_.wait(lock, [this] {
        return activeTasks_.load() == 0 && getQueueSize() == 0;
    });
}

OptimizedThreadPool::PoolStatistics OptimizedThreadPool::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    PoolStatistics stats = stats_;
    stats.numThreads = workers_.size();
    stats.activeThreads = activeTasks_.load();
    stats.queuedTasks = getQueueSize();
    stats.completedTasks = completedTasks_.load();
    stats.failedTasks = failedTasks_.load();
    stats.workStealingEvents = workStealingEvents_.load();
    
    // Calculate averages
    if (stats.completedTasks > 0) {
        stats.averageTaskTime = totalTaskTime_.load() / stats.completedTasks;
        stats.averageQueueTime = totalQueueTime_.load() / stats.completedTasks;
    }
    
    return stats;
}

bool OptimizedThreadPool::isHealthy() const {
    if (!initialized_) {
        return false;
    }
    
    auto stats = getStatistics();
    
    // Consider healthy if:
    // 1. Queue is not overloaded
    // 2. Task failure rate is low
    // 3. Average task time is reasonable
    
    bool queueHealthy = stats.queuedTasks < (config_.maxQueueSize * 8 / 10);
    bool failureHealthy = stats.completedTasks == 0 || 
                         (stats.failedTasks * 100 / stats.completedTasks) < 5; // Less than 5% failure rate
    bool latencyHealthy = stats.averageTaskTime < 1000.0; // Less than 1 second average
    
    return queueHealthy && failureHealthy && latencyHealthy;
}

std::string OptimizedThreadPool::getHealthStatus() const {
    auto stats = getStatistics();
    
    std::ostringstream oss;
    oss << "OptimizedThreadPool Health Status:\n";
    oss << "  Threads: " << stats.numThreads << " (Active: " << stats.activeThreads << ")\n";
    oss << "  Queued Tasks: " << stats.queuedTasks << "/" << config_.maxQueueSize << "\n";
    oss << "  Completed Tasks: " << stats.completedTasks << "\n";
    oss << "  Failed Tasks: " << stats.failedTasks << "\n";
    oss << "  Average Task Time: " << stats.averageTaskTime << "ms\n";
    oss << "  Average Queue Time: " << stats.averageQueueTime << "ms\n";
    oss << "  Work Stealing Events: " << stats.workStealingEvents << "\n";
    oss << "  Status: " << (isHealthy() ? "HEALTHY" : "UNHEALTHY");
    
    return oss.str();
}

void OptimizedThreadPool::updateConfig(const PoolConfig& config) {
    // Only allow limited runtime configuration changes
    config_.maxQueueSize = config.maxQueueSize;
    config_.threadIdleTimeout = config.threadIdleTimeout;
    
    // Update queue priority settings
    for (auto& queue : workQueues_) {
        queue->setEnablePriority(config.enablePriority);
    }
    
    if (globalQueue_) {
        globalQueue_->setEnablePriority(config.enablePriority);
    }
    
    Logger::info("OptimizedThreadPool configuration updated");
}

size_t OptimizedThreadPool::getQueueSize() const {
    size_t totalSize = 0;
    
    for (const auto& queue : workQueues_) {
        totalSize += queue->size();
    }
    
    if (globalQueue_) {
        totalSize += globalQueue_->size();
    }
    
    return totalSize;
}

size_t OptimizedThreadPool::getActiveThreadCount() const {
    return activeTasks_.load();
}

// Private helper methods
void OptimizedThreadPool::workerThread(size_t threadId) {
    Logger::debug("Worker thread " + std::to_string(threadId) + " started");
    
    WorkQueue* myQueue = workQueues_[threadId].get();
    Task task([](){}, Priority::NORMAL);
    
    while (!shutdownRequested_) {
        bool hasTask = false;
        
        // Try to get task from own queue first
        if (myQueue->waitAndPop(task, config_.threadIdleTimeout)) {
            hasTask = true;
        }
        // Try work stealing if enabled
        else if (config_.enableWorkStealing && tryStealWork(threadId, task)) {
            hasTask = true;
            workStealingEvents_++;
        }
        // Try global queue as last resort
        else if (globalQueue_ && globalQueue_->tryPop(task)) {
            hasTask = true;
        }
        
        if (hasTask && !shutdownRequested_) {
            // Execute task
            activeTasks_++;
            
            task.startTime = std::chrono::steady_clock::now();
            
            try {
                task.function();
                updateStatistics(task, true);
            } catch (const std::exception& e) {
                Logger::error("Task execution failed in thread " + 
                             std::to_string(threadId) + ": " + e.what());
                updateStatistics(task, false);
            } catch (...) {
                Logger::error("Unknown task execution error in thread " + 
                             std::to_string(threadId));
                updateStatistics(task, false);
            }
            
            activeTasks_--;
            
            // Notify waiters if no more active tasks
            if (activeTasks_.load() == 0) {
                waitCondition_.notify_all();
            }
        }
    }
    
    Logger::debug("Worker thread " + std::to_string(threadId) + " stopped");
}

bool OptimizedThreadPool::tryStealWork(size_t threadId, Task& task) {
    // Try to steal from other worker queues
    for (size_t i = 1; i < workQueues_.size(); ++i) {
        size_t targetQueue = (threadId + i) % workQueues_.size();
        
        if (workQueues_[targetQueue]->trySteal(task)) {
            return true;
        }
    }
    
    return false;
}

void OptimizedThreadPool::updateStatistics(const Task& task, bool success) {
    auto endTime = std::chrono::steady_clock::now();
    
    // Calculate task execution time
    auto taskDuration = std::chrono::duration_cast<std::chrono::microseconds>(
        endTime - task.startTime).count();
    
    // Calculate queue wait time
    auto queueDuration = std::chrono::duration_cast<std::chrono::microseconds>(
        task.startTime - task.queueTime).count();
    
    // Update atomic counters
    if (success) {
        completedTasks_++;
    } else {
        failedTasks_++;
    }
    
    // Update timing statistics
    totalTaskTime_ = totalTaskTime_.load() + taskDuration;
    totalQueueTime_ = totalQueueTime_.load() + queueDuration;
}

size_t OptimizedThreadPool::getOptimalThreadCount() const {
    size_t hardwareConcurrency = std::thread::hardware_concurrency();
    
    if (hardwareConcurrency == 0) {
        // Fallback if hardware_concurrency is not available
        return 4;
    }
    
    // For CPU-intensive tasks, use hardware concurrency
    // For I/O-intensive tasks, we might use more threads
    // For STT processing, we typically want hardware concurrency
    return hardwareConcurrency;
}

void OptimizedThreadPool::setThreadAffinity(size_t threadId) {
#ifdef __linux__
    if (threadId < workers_.size()) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        
        // Bind thread to specific CPU core
        size_t coreId = threadId % std::thread::hardware_concurrency();
        CPU_SET(coreId, &cpuset);
        
        pthread_t thread = workers_[threadId].native_handle();
        int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
        
        if (result == 0) {
            Logger::debug("Set thread " + std::to_string(threadId) + 
                         " affinity to CPU " + std::to_string(coreId));
        } else {
            Logger::warn("Failed to set thread affinity for thread " + 
                        std::to_string(threadId));
        }
    }
#else
    // Thread affinity not supported on this platform
    Logger::debug("Thread affinity not supported on this platform");
#endif
}

} // namespace utils