#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <vector>
#include <atomic>
#include <memory>
#include <future>

namespace speechrnt {
namespace core {

/**
 * Priority levels for tasks in the queue
 */
enum class TaskPriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};

/**
 * Base task interface
 */
class Task {
public:
    Task(TaskPriority priority = TaskPriority::NORMAL) 
        : priority_(priority), created_at_(std::chrono::steady_clock::now()) {}
    
    virtual ~Task() = default;
    virtual void execute() = 0;
    
    TaskPriority getPriority() const { return priority_; }
    std::chrono::steady_clock::time_point getCreatedAt() const { return created_at_; }

private:
    TaskPriority priority_;
    std::chrono::steady_clock::time_point created_at_;
};

/**
 * Function-based task implementation
 */
class FunctionTask : public Task {
public:
    FunctionTask(std::function<void()> func, TaskPriority priority = TaskPriority::NORMAL)
        : Task(priority), func_(std::move(func)) {}
    
    void execute() override {
        if (func_) {
            func_();
        }
    }

private:
    std::function<void()> func_;
};

/**
 * Task comparator for priority queue (higher priority first)
 */
struct TaskComparator {
    bool operator()(const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) const {
        if (a->getPriority() != b->getPriority()) {
            return static_cast<int>(a->getPriority()) < static_cast<int>(b->getPriority());
        }
        // If same priority, older tasks first (FIFO)
        return a->getCreatedAt() > b->getCreatedAt();
    }
};

/**
 * Thread-safe task queue with priority support
 */
class TaskQueue {
public:
    TaskQueue();
    ~TaskQueue();
    
    // Non-copyable, non-movable
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;
    TaskQueue(TaskQueue&&) = delete;
    TaskQueue& operator=(TaskQueue&&) = delete;
    
    /**
     * Add a task to the queue
     */
    void enqueue(std::shared_ptr<Task> task);
    
    /**
     * Add a function-based task to the queue
     */
    void enqueue(std::function<void()> func, TaskPriority priority = TaskPriority::NORMAL);
    
    /**
     * Add a task with future support for result retrieval
     */
    template<typename F, typename... Args>
    auto enqueueWithFuture(TaskPriority priority, F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;
    
    /**
     * Get the next task from the queue (blocks if empty)
     * Returns nullptr if queue is shutting down
     */
    std::shared_ptr<Task> dequeue();
    
    /**
     * Try to get the next task without blocking
     * Returns nullptr if queue is empty
     */
    std::shared_ptr<Task> tryDequeue();
    
    /**
     * Get current queue size
     */
    size_t size() const;
    
    /**
     * Check if queue is empty
     */
    bool empty() const;
    
    /**
     * Clear all pending tasks
     */
    void clear();
    
    /**
     * Shutdown the queue (wake up all waiting threads)
     */
    void shutdown();
    
    /**
     * Check if queue is shutting down
     */
    bool isShuttingDown() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::priority_queue<std::shared_ptr<Task>, std::vector<std::shared_ptr<Task>>, TaskComparator> queue_;
    std::atomic<bool> shutdown_;
};

/**
 * Thread pool for executing tasks from TaskQueue
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency());
    ~ThreadPool();
    
    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;
    
    /**
     * Start the thread pool with the given task queue
     */
    void start(std::shared_ptr<TaskQueue> task_queue);
    
    /**
     * Stop the thread pool and wait for all threads to finish
     */
    void stop();
    
    /**
     * Get the number of worker threads
     */
    size_t getNumThreads() const { return num_threads_; }
    
    /**
     * Get the number of active (busy) threads
     */
    size_t getActiveThreads() const;
    
    /**
     * Check if the thread pool is running
     */
    bool isRunning() const { return running_; }

private:
    void workerLoop();
    
    size_t num_threads_;
    std::vector<std::thread> workers_;
    std::shared_ptr<TaskQueue> task_queue_;
    std::atomic<bool> running_;
    std::atomic<size_t> active_threads_;
};

// Template implementation
template<typename F, typename... Args>
auto TaskQueue::enqueueWithFuture(TaskPriority priority, F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type> {
    
    using return_type = typename std::result_of<F(Args...)>::type;
    
    auto task_ptr = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> result = task_ptr->get_future();
    
    auto wrapper_task = std::make_shared<FunctionTask>(
        [task_ptr]() { (*task_ptr)(); },
        priority
    );
    
    enqueue(wrapper_task);
    
    return result;
}

} // namespace core
} // namespace speechrnt