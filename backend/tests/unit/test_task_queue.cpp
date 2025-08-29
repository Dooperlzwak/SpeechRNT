#include "core/task_queue.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <atomic>
#include <vector>
#include <thread>

using namespace speechrnt::core;

class TaskQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        task_queue = std::make_shared<TaskQueue>();
    }
    
    void TearDown() override {
        task_queue->shutdown();
    }
    
    std::shared_ptr<TaskQueue> task_queue;
};

// Test basic task enqueueing and dequeueing
TEST_F(TaskQueueTest, BasicEnqueueDequeue) {
    std::atomic<int> counter{0};
    
    // Enqueue a simple task
    task_queue->enqueue([&counter]() {
        counter++;
    });
    
    EXPECT_EQ(task_queue->size(), 1);
    EXPECT_FALSE(task_queue->empty());
    
    // Dequeue and execute the task
    auto task = task_queue->tryDequeue();
    ASSERT_NE(task, nullptr);
    task->execute();
    
    EXPECT_EQ(counter.load(), 1);
    EXPECT_EQ(task_queue->size(), 0);
    EXPECT_TRUE(task_queue->empty());
}

// Test priority ordering
TEST_F(TaskQueueTest, PriorityOrdering) {
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
    EXPECT_EQ(execution_order[0], 3); // CRITICAL
    EXPECT_EQ(execution_order[1], 2); // HIGH
    EXPECT_EQ(execution_order[2], 4); // NORMAL
    EXPECT_EQ(execution_order[3], 1); // LOW
}

// Test FIFO ordering within same priority
TEST_F(TaskQueueTest, FIFOWithinSamePriority) {
    std::vector<int> execution_order;
    std::mutex order_mutex;
    
    // Enqueue multiple tasks with same priority
    for (int i = 1; i <= 5; ++i) {
        task_queue->enqueue([&, i]() {
            std::lock_guard<std::mutex> lock(order_mutex);
            execution_order.push_back(i);
        }, TaskPriority::NORMAL);
        
        // Small delay to ensure different timestamps
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
    
    // Execute all tasks
    while (!task_queue->empty()) {
        auto task = task_queue->tryDequeue();
        if (task) {
            task->execute();
        }
    }
    
    // Should execute in FIFO order: 1, 2, 3, 4, 5
    ASSERT_EQ(execution_order.size(), 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(execution_order[i], i + 1);
    }
}

// Test future-based task execution
TEST_F(TaskQueueTest, FutureBasedTasks) {
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
    EXPECT_EQ(future2.get(), 30); // HIGH priority task (10 + 20)
    EXPECT_EQ(future1.get(), 42); // NORMAL priority task
}

// Test thread safety
TEST_F(TaskQueueTest, ThreadSafety) {
    const int num_producers = 4;
    const int num_consumers = 2;
    const int tasks_per_producer = 100;
    
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
    
    EXPECT_EQ(total_executed.load(), num_producers * tasks_per_producer);
    EXPECT_EQ(total_enqueued.load(), num_producers * tasks_per_producer);
}

// Test queue shutdown
TEST_F(TaskQueueTest, Shutdown) {
    std::atomic<bool> task_executed{false};
    
    // Enqueue a task
    task_queue->enqueue([&]() {
        task_executed = true;
    });
    
    EXPECT_FALSE(task_queue->isShuttingDown());
    EXPECT_EQ(task_queue->size(), 1);
    
    // Shutdown the queue
    task_queue->shutdown();
    
    EXPECT_TRUE(task_queue->isShuttingDown());
    
    // Try to enqueue another task (should be ignored)
    task_queue->enqueue([&]() {
        // This should not execute
    });
    
    EXPECT_EQ(task_queue->size(), 1); // Only the original task
    
    // Dequeue should return nullptr after shutdown when queue is empty
    auto task = task_queue->tryDequeue();
    ASSERT_NE(task, nullptr);
    task->execute();
    EXPECT_TRUE(task_executed.load());
    
    // Now dequeue should return nullptr
    task = task_queue->tryDequeue();
    EXPECT_EQ(task, nullptr);
}

// Test clear functionality
TEST_F(TaskQueueTest, Clear) {
    // Enqueue multiple tasks
    for (int i = 0; i < 5; ++i) {
        task_queue->enqueue([]() {
            // Do nothing
        });
    }
    
    EXPECT_EQ(task_queue->size(), 5);
    
    // Clear the queue
    task_queue->clear();
    
    EXPECT_EQ(task_queue->size(), 0);
    EXPECT_TRUE(task_queue->empty());
}

// ThreadPool tests
class ThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        task_queue = std::make_shared<TaskQueue>();
        thread_pool = std::make_unique<ThreadPool>(4);
    }
    
    void TearDown() override {
        thread_pool->stop();
        task_queue->shutdown();
    }
    
    std::shared_ptr<TaskQueue> task_queue;
    std::unique_ptr<ThreadPool> thread_pool;
};

TEST_F(ThreadPoolTest, BasicExecution) {
    std::atomic<int> counter{0};
    
    // Start the thread pool
    thread_pool->start(task_queue);
    EXPECT_TRUE(thread_pool->isRunning());
    EXPECT_EQ(thread_pool->getNumThreads(), 4);
    
    // Enqueue some tasks
    const int num_tasks = 10;
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
    
    EXPECT_EQ(counter.load(), num_tasks);
}

TEST_F(ThreadPoolTest, ConcurrentExecution) {
    std::atomic<int> max_concurrent{0};
    std::atomic<int> current_concurrent{0};
    
    thread_pool->start(task_queue);
    
    // Enqueue tasks that track concurrency
    const int num_tasks = 20;
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
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            current_concurrent--;
        });
    }
    
    // Wait for all tasks to complete
    while (current_concurrent.load() > 0 || !task_queue->empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Should have achieved some level of concurrency (at least 2 threads working)
    EXPECT_GE(max_concurrent.load(), 2);
    EXPECT_LE(max_concurrent.load(), 4); // Should not exceed thread pool size
}

TEST_F(ThreadPoolTest, ExceptionHandling) {
    std::atomic<int> successful_tasks{0};
    std::atomic<int> total_tasks{0};
    
    thread_pool->start(task_queue);
    
    // Enqueue tasks, some of which throw exceptions
    for (int i = 0; i < 10; ++i) {
        task_queue->enqueue([&, i]() {
            total_tasks++;
            if (i % 3 == 0) {
                throw std::runtime_error("Test exception");
            }
            successful_tasks++;
        });
    }
    
    // Wait for all tasks to be processed
    while (total_tasks.load() < 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Thread pool should continue working despite exceptions
    EXPECT_EQ(total_tasks.load(), 10);
    EXPECT_EQ(successful_tasks.load(), 6); // 7 tasks should succeed (not divisible by 3)
    EXPECT_TRUE(thread_pool->isRunning());
}

TEST_F(ThreadPoolTest, StopAndRestart) {
    std::atomic<int> counter{0};
    
    // Start and verify
    thread_pool->start(task_queue);
    EXPECT_TRUE(thread_pool->isRunning());
    
    // Add a task
    task_queue->enqueue([&counter]() {
        counter++;
    });
    
    // Wait for task to complete
    while (counter.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    EXPECT_EQ(counter.load(), 1);
    
    // Stop the thread pool
    thread_pool->stop();
    EXPECT_FALSE(thread_pool->isRunning());
    
    // Create new task queue and restart
    task_queue = std::make_shared<TaskQueue>();
    thread_pool->start(task_queue);
    EXPECT_TRUE(thread_pool->isRunning());
    
    // Add another task
    task_queue->enqueue([&counter]() {
        counter++;
    });
    
    // Wait for task to complete
    while (counter.load() == 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    EXPECT_EQ(counter.load(), 2);
}