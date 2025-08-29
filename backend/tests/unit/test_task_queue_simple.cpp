#include "core/task_queue.hpp"
#include <iostream>
#include <chrono>
#include <atomic>
#include <vector>
#include <thread>
#include <cassert>

using namespace speechrnt::core;

// Simple test framework
#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::cerr << "ASSERTION FAILED: " << #a << " != " << #b \
                  << " (" << (a) << " != " << (b) << ")" << std::endl; \
        return false; \
    } \
} while(0)

#define ASSERT_NE(a, b) do { \
    if ((a) == (b)) { \
        std::cerr << "ASSERTION FAILED: " << #a << " == " << #b \
                  << " (" << (a) << " == " << (b) << ")" << std::endl; \
        return false; \
    } \
} while(0)

#define ASSERT_TRUE(a) do { \
    if (!(a)) { \
        std::cerr << "ASSERTION FAILED: " << #a << " is false" << std::endl; \
        return false; \
    } \
} while(0)

#define ASSERT_FALSE(a) do { \
    if (a) { \
        std::cerr << "ASSERTION FAILED: " << #a << " is true" << std::endl; \
        return false; \
    } \
} while(0)

#define ASSERT_GE(a, b) do { \
    if ((a) < (b)) { \
        std::cerr << "ASSERTION FAILED: " << #a << " < " << #b \
                  << " (" << (a) << " < " << (b) << ")" << std::endl; \
        return false; \
    } \
} while(0)

#define ASSERT_LE(a, b) do { \
    if ((a) > (b)) { \
        std::cerr << "ASSERTION FAILED: " << #a << " > " << #b \
                  << " (" << (a) << " > " << (b) << ")" << std::endl; \
        return false; \
    } \
} while(0)

// Test basic task enqueueing and dequeueing
bool testBasicEnqueueDequeue() {
    std::cout << "Testing basic enqueue/dequeue..." << std::endl;
    
    auto task_queue = std::make_shared<TaskQueue>();
    std::atomic<int> counter{0};
    
    // Enqueue a simple task
    task_queue->enqueue([&counter]() {
        counter++;
    });
    
    ASSERT_EQ(task_queue->size(), 1);
    ASSERT_FALSE(task_queue->empty());
    
    // Dequeue and execute the task
    auto task = task_queue->tryDequeue();
    ASSERT_NE(task, nullptr);
    task->execute();
    
    ASSERT_EQ(counter.load(), 1);
    ASSERT_EQ(task_queue->size(), 0);
    ASSERT_TRUE(task_queue->empty());
    
    task_queue->shutdown();
    return true;
}

// Test priority ordering
bool testPriorityOrdering() {
    std::cout << "Testing priority ordering..." << std::endl;
    
    auto task_queue = std::make_shared<TaskQueue>();
    std::vector<int> execution_order;
    std::mutex order_mutex;
    
    // Enqueue tasks with different priorities
    task_queue->enqueue([&]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(1);
    }, TaskPriority::LOW);
    
    task_queue->enqueue([&]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(2);
    }, TaskPriority::HIGH);
    
    task_queue->enqueue([&]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(3);
    }, TaskPriority::CRITICAL);
    
    task_queue->enqueue([&]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(4);
    }, TaskPriority::NORMAL);
    
    // Execute tasks in priority order
    while (!task_queue->empty()) {
        auto task = task_queue->tryDequeue();
        if (task) {
            task->execute();
        }
    }
    
    // Should execute in order: CRITICAL(3), HIGH(2), NORMAL(4), LOW(1)
    ASSERT_EQ(execution_order.size(), 4);
    ASSERT_EQ(execution_order[0], 3); // CRITICAL
    ASSERT_EQ(execution_order[1], 2); // HIGH
    ASSERT_EQ(execution_order[2], 4); // NORMAL
    ASSERT_EQ(execution_order[3], 1); // LOW
    
    task_queue->shutdown();
    return true;
}

// Test future-based task execution
bool testFutureBasedTasks() {
    std::cout << "Testing future-based tasks..." << std::endl;
    
    auto task_queue = std::make_shared<TaskQueue>();
    
    // Test with return value
    auto future1 = task_queue->enqueueWithFuture(TaskPriority::NORMAL, []() -> int {
        return 42;
    });
    
    // Test with parameters
    auto future2 = task_queue->enqueueWithFuture(TaskPriority::HIGH, [](int a, int b) -> int {
        return a + b;
    }, 10, 20);
    
    // Execute tasks
    auto task1 = task_queue->tryDequeue(); // Should be HIGH priority task first
    auto task2 = task_queue->tryDequeue(); // Then NORMAL priority task
    
    ASSERT_NE(task1, nullptr);
    ASSERT_NE(task2, nullptr);
    
    task1->execute();
    task2->execute();
    
    // Check results
    ASSERT_EQ(future2.get(), 30); // HIGH priority task (10 + 20)
    ASSERT_EQ(future1.get(), 42); // NORMAL priority task
    
    task_queue->shutdown();
    return true;
}

// Test thread safety
bool testThreadSafety() {
    std::cout << "Testing thread safety..." << std::endl;
    
    auto task_queue = std::make_shared<TaskQueue>();
    const int num_producers = 2;
    const int num_consumers = 2;
    const int tasks_per_producer = 50;
    
    std::atomic<int> total_executed{0};
    std::atomic<int> total_enqueued{0};
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    
    // Start consumer threads
    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([&]() {
            while (total_executed.load() < num_producers * tasks_per_producer) {
                auto task = task_queue->tryDequeue();
                if (task) {
                    task->execute();
                } else {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }
        });
    }
    
    // Start producer threads
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([&]() {
            for (int j = 0; j < tasks_per_producer; ++j) {
                task_queue->enqueue([&]() {
                    total_executed++;
                });
                total_enqueued++;
            }
        });
    }
    
    // Wait for all producers to finish
    for (auto& producer : producers) {
        producer.join();
    }
    
    // Wait for all tasks to be executed
    while (total_executed.load() < total_enqueued.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Stop consumers
    task_queue->shutdown();
    for (auto& consumer : consumers) {
        consumer.join();
    }
    
    ASSERT_EQ(total_executed.load(), num_producers * tasks_per_producer);
    ASSERT_EQ(total_enqueued.load(), num_producers * tasks_per_producer);
    
    return true;
}

// Test ThreadPool basic execution
bool testThreadPoolBasicExecution() {
    std::cout << "Testing ThreadPool basic execution..." << std::endl;
    
    auto task_queue = std::make_shared<TaskQueue>();
    auto thread_pool = std::make_unique<ThreadPool>(2);
    
    std::atomic<int> counter{0};
    
    // Start the thread pool
    thread_pool->start(task_queue);
    ASSERT_TRUE(thread_pool->isRunning());
    ASSERT_EQ(thread_pool->getNumThreads(), 2);
    
    // Enqueue some tasks
    const int num_tasks = 5;
    for (int i = 0; i < num_tasks; ++i) {
        task_queue->enqueue([&counter]() {
            counter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });
    }
    
    // Wait for all tasks to complete
    while (counter.load() < num_tasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    ASSERT_EQ(counter.load(), num_tasks);
    
    thread_pool->stop();
    task_queue->shutdown();
    return true;
}

// Test ThreadPool concurrent execution
bool testThreadPoolConcurrentExecution() {
    std::cout << "Testing ThreadPool concurrent execution..." << std::endl;
    
    auto task_queue = std::make_shared<TaskQueue>();
    auto thread_pool = std::make_unique<ThreadPool>(3);
    
    std::atomic<int> max_concurrent{0};
    std::atomic<int> current_concurrent{0};
    
    thread_pool->start(task_queue);
    
    // Enqueue tasks that track concurrency
    const int num_tasks = 10;
    for (int i = 0; i < num_tasks; ++i) {
        task_queue->enqueue([&]() {
            int current = current_concurrent.fetch_add(1) + 1;
            
            // Update max concurrent if needed
            int expected_max = max_concurrent.load();
            while (current > expected_max && 
                   !max_concurrent.compare_exchange_weak(expected_max, current)) {
                // Retry if another thread updated max_concurrent
            }
            
            // Simulate work
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            
            current_concurrent--;
        });
    }
    
    // Wait for all tasks to complete
    while (current_concurrent.load() > 0 || !task_queue->empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Should have achieved some level of concurrency (at least 2 threads working)
    ASSERT_GE(max_concurrent.load(), 2);
    ASSERT_LE(max_concurrent.load(), 3); // Should not exceed thread pool size
    
    thread_pool->stop();
    task_queue->shutdown();
    return true;
}

int main() {
    std::cout << "Running TaskQueue and ThreadPool tests..." << std::endl;
    
    bool all_passed = true;
    
    // Run all tests
    all_passed &= testBasicEnqueueDequeue();
    all_passed &= testPriorityOrdering();
    all_passed &= testFutureBasedTasks();
    all_passed &= testThreadSafety();
    all_passed &= testThreadPoolBasicExecution();
    all_passed &= testThreadPoolConcurrentExecution();
    
    if (all_passed) {
        std::cout << "All tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "Some tests failed!" << std::endl;
        return 1;
    }
}