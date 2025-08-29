#pragma once

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <atomic>
#include <chrono>

namespace utils {

/**
 * High-performance thread pool with work stealing and priority queues
 * Optimized for STT pipeline processing with minimal synchronization overhead
 */
class OptimizedThreadPool {
public:
    /**
     * Task priority levels
     */
    enum class Priority {
        LOW = 0,
        NORMAL = 1,
        HIGH = 2,
        CRITICAL = 3
    };
    
    /**
     * Thread pool configuration
     */
    struct PoolConfig {
        size_t numThreads = 0;              // 0 = auto-detect based on hardware
        bool enableWorkStealing = true;     // Enable work stealing between threads
        bool enablePriority = true;         // Enable priority-based task scheduling
        size_t maxQueueSize = 10000;        // Maximum tasks in queue
        std::chrono::milliseconds threadIdleTimeout{5000}; // Thread idle timeout
        bool enableThreadAffinity = false;  // Enable CPU affinity (Linux only)
        
        PoolConfig() = default;
    };
    
    /**
     * Thread pool statistics
     */
    struct PoolStatistics {
        size_t numThreads;
        size_t activeThreads;
        size_t queuedTasks;
        size_t completedTasks;
        size_t failedTasks;
        double averageTaskTime;
        double averageQueueTime;
        size_t workStealingEvents;
        
        PoolStatistics() : numThreads(0), activeThreads(0), queuedTasks(0),
                          completedTasks(0), failedTasks(0), averageTaskTime(0.0),
                          averageQueueTime(0.0), workStealingEvents(0) {}
    };

private:
    /**
     * Task wrapper with priority and timing information
     */
    struct Task {
        std::function<void()> function;
        Priority priority;
        std::chrono::steady_clock::time_point queueTime;
        std::chrono::steady_clock::time_point startTime;
        
        Task(std::function<void()> f, Priority p)
            : function(std::move(f)), priority(p) {
            queueTime = std::chrono::steady_clock::now();
        }
        
        bool operator<(const Task& other) const {
            // Higher priority tasks come first
            return priority < other.priority;
        }
    };
    
    /**
     * Per-thread work queue with work stealing support
     */
    class WorkQueue {
    public:
        WorkQueue() = default;
        
        void push(Task task) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (enablePriority_) {
                priorityQueue_.push(std::move(task));
            } else {
                normalQueue_.push(std::move(task));
            }
            condition_.notify_one();
        }
        
        bool tryPop(Task& task) {
            std::lock_guard<std::mutex> lock(mutex_);
            
            if (enablePriority_ && !priorityQueue_.empty()) {
                task = std::move(const_cast<Task&>(priorityQueue_.top()));
                priorityQueue_.pop();
                return true;
            } else if (!normalQueue_.empty()) {
                task = std::move(normalQueue_.front());
                normalQueue_.pop();
                return true;
            }
            
            return false;
        }
        
        bool waitAndPop(Task& task, std::chrono::milliseconds timeout) {
            std::unique_lock<std::mutex> lock(mutex_);
            
            if (condition_.wait_for(lock, timeout, [this] { 
                return !isEmpty(); 
            })) {
                return tryPop(task);
            }
            
            return false;
        }
        
        bool trySteal(Task& task) {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Steal from normal queue first (lower priority)
            if (!normalQueue_.empty()) {
                task = std::move(normalQueue_.front());
                normalQueue_.pop();
                return true;
            }
            
            return false;
        }
        
        size_t size() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return priorityQueue_.size() + normalQueue_.size();
        }
        
        bool empty() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return isEmpty();
        }
        
        void setEnablePriority(bool enable) {
            enablePriority_ = enable;
        }

    private:
        mutable std::mutex mutex_;
        std::condition_variable condition_;
        std::priority_queue<Task> priorityQueue_;
        std::queue<Task> normalQueue_;
        bool enablePriority_ = true;
        
        bool isEmpty() const {
            return priorityQueue_.empty() && normalQueue_.empty();
        }
    };

public:
    explicit OptimizedThreadPool(const PoolConfig& config = PoolConfig());
    ~OptimizedThreadPool();
    
    // Disable copy constructor and assignment
    OptimizedThreadPool(const OptimizedThreadPool&) = delete;
    OptimizedThreadPool& operator=(const OptimizedThreadPool&) = delete;
    
    /**
     * Initialize the thread pool
     */
    bool initialize();
    
    /**
     * Shutdown the thread pool
     */
    void shutdown();
    
    /**
     * Submit a task with default priority
     */
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
        return submit(Priority::NORMAL, std::forward<F>(f), std::forward<Args>(args)...);
    }
    
    /**
     * Submit a task with specified priority
     */
    template<typename F, typename... Args>
    auto submit(Priority priority, F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
        using ReturnType = typename std::result_of<F(Args...)>::type;
        
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<ReturnType> result = task->get_future();
        
        if (!submitTask([task]() { (*task)(); }, priority)) {
            // If submission failed, return a future with an exception
            std::promise<ReturnType> promise;
            promise.set_exception(std::make_exception_ptr(
                std::runtime_error("Thread pool queue is full")));
            return promise.get_future();
        }
        
        return result;
    }
    
    /**
     * Submit a simple function without return value
     */
    bool submitTask(std::function<void()> task, Priority priority = Priority::NORMAL);
    
    /**
     * Wait for all tasks to complete
     */
    void waitForAll();
    
    /**
     * Get thread pool statistics
     */
    PoolStatistics getStatistics() const;
    
    /**
     * Check if thread pool is healthy
     */
    bool isHealthy() const;
    
    /**
     * Get health status report
     */
    std::string getHealthStatus() const;
    
    /**
     * Update thread pool configuration (limited runtime changes)
     */
    void updateConfig(const PoolConfig& config);
    
    /**
     * Get current queue size
     */
    size_t getQueueSize() const;
    
    /**
     * Get number of active threads
     */
    size_t getActiveThreadCount() const;

private:
    // Configuration
    PoolConfig config_;
    
    // Thread pool state
    bool initialized_;
    std::atomic<bool> shutdownRequested_;
    
    // Worker threads and queues
    std::vector<std::thread> workers_;
    std::vector<std::unique_ptr<WorkQueue>> workQueues_;
    std::atomic<size_t> nextQueue_;
    
    // Global queue for overflow
    std::unique_ptr<WorkQueue> globalQueue_;
    
    // Statistics
    mutable std::mutex statsMutex_;
    PoolStatistics stats_;
    std::atomic<size_t> activeTasks_;
    std::atomic<size_t> completedTasks_;
    std::atomic<size_t> failedTasks_;
    std::atomic<double> totalTaskTime_;
    std::atomic<double> totalQueueTime_;
    std::atomic<size_t> workStealingEvents_;
    
    // Synchronization for waiting
    std::mutex waitMutex_;
    std::condition_variable waitCondition_;
    
    // Helper methods
    void workerThread(size_t threadId);
    bool tryStealWork(size_t threadId, Task& task);
    void updateStatistics(const Task& task, bool success);
    size_t getOptimalThreadCount() const;
    void setThreadAffinity(size_t threadId);
};

/**
 * RAII helper for batch task submission
 */
class TaskBatch {
public:
    explicit TaskBatch(OptimizedThreadPool& pool) : pool_(pool) {}
    
    template<typename F, typename... Args>
    void add(F&& f, Args&&... args) {
        add(OptimizedThreadPool::Priority::NORMAL, std::forward<F>(f), std::forward<Args>(args)...);
    }
    
    template<typename F, typename... Args>
    void add(OptimizedThreadPool::Priority priority, F&& f, Args&&... args) {
        futures_.push_back(pool_.submit(priority, std::forward<F>(f), std::forward<Args>(args)...));
    }
    
    void waitAll() {
        for (auto& future : futures_) {
            try {
                future.wait();
            } catch (...) {
                // Ignore exceptions in batch wait
            }
        }
        futures_.clear();
    }
    
    size_t size() const { return futures_.size(); }

private:
    OptimizedThreadPool& pool_;
    std::vector<std::future<void>> futures_;
};

} // namespace utils