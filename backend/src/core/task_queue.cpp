#include "core/task_queue.hpp"
#include <chrono>

namespace speechrnt {
namespace core {

TaskQueue::TaskQueue() : shutdown_(false) {
}

TaskQueue::~TaskQueue() {
    shutdown();
}

void TaskQueue::enqueue(std::shared_ptr<Task> task) {
    if (!task) {
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) {
            return; // Don't accept new tasks when shutting down
        }
        queue_.push(task);
    }
    condition_.notify_one();
}

void TaskQueue::enqueue(std::function<void()> func, TaskPriority priority) {
    auto task = std::make_shared<FunctionTask>(std::move(func), priority);
    enqueue(task);
}

std::shared_ptr<Task> TaskQueue::dequeue() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    // Wait until there's a task or we're shutting down
    condition_.wait(lock, [this] { return !queue_.empty() || shutdown_; });
    
    if (shutdown_ && queue_.empty()) {
        return nullptr;
    }
    
    auto task = queue_.top();
    queue_.pop();
    return task;
}

std::shared_ptr<Task> TaskQueue::tryDequeue() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (queue_.empty() || shutdown_) {
        return nullptr;
    }
    
    auto task = queue_.top();
    queue_.pop();
    return task;
}

size_t TaskQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool TaskQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

void TaskQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Clear the priority queue by creating a new empty one
    std::priority_queue<std::shared_ptr<Task>, std::vector<std::shared_ptr<Task>>, TaskComparator> empty_queue;
    queue_.swap(empty_queue);
}

void TaskQueue::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
    }
    condition_.notify_all();
}

bool TaskQueue::isShuttingDown() const {
    return shutdown_;
}

// ThreadPool implementation

ThreadPool::ThreadPool(size_t num_threads) 
    : num_threads_(num_threads), running_(false), active_threads_(0) {
    if (num_threads_ == 0) {
        num_threads_ = 1; // Ensure at least one thread
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::start(std::shared_ptr<TaskQueue> task_queue) {
    if (running_ || !task_queue) {
        return;
    }
    
    task_queue_ = task_queue;
    running_ = true;
    
    // Create worker threads
    workers_.reserve(num_threads_);
    for (size_t i = 0; i < num_threads_; ++i) {
        workers_.emplace_back(&ThreadPool::workerLoop, this);
    }
}

void ThreadPool::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // Shutdown the task queue to wake up all workers
    if (task_queue_) {
        task_queue_->shutdown();
    }
    
    // Wait for all worker threads to finish
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    workers_.clear();
    task_queue_.reset();
}

size_t ThreadPool::getActiveThreads() const {
    return active_threads_;
}

void ThreadPool::workerLoop() {
    while (running_) {
        auto task = task_queue_->dequeue();
        
        if (!task) {
            // Task queue is shutting down
            break;
        }
        
        try {
            // Increment active thread count
            active_threads_++;
            
            // Execute the task
            task->execute();
            
        } catch (const std::exception& e) {
            // Log error but continue processing
            // TODO: Add proper logging when logging system is available
            // For now, we silently handle exceptions to prevent thread termination
        } catch (...) {
            // Handle any other exceptions
        }
        
        // Decrement active thread count
        active_threads_--;
    }
}

} // namespace core
} // namespace speechrnt