#include "core/utterance_manager.hpp"
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
        std::cerr << "ASSERTION FAILED: " << #a << " != " << #b << std::endl; \
        return false; \
    } \
} while(0)

#define ASSERT_NE(a, b) do { \
    if ((a) == (b)) { \
        std::cerr << "ASSERTION FAILED: " << #a << " == " << #b << std::endl; \
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
        std::cerr << "ASSERTION FAILED: " << #a << " < " << #b << std::endl; \
        return false; \
    } \
} while(0)

#define ASSERT_LE(a, b) do { \
    if ((a) > (b)) { \
        std::cerr << "ASSERTION FAILED: " << #a << " > " << #b << std::endl; \
        return false; \
    } \
} while(0)

// Test basic utterance creation and state management
bool testBasicUtteranceCreation() {
    std::cout << "Testing basic utterance creation..." << std::endl;
    
    auto task_queue = std::make_shared<TaskQueue>();
    UtteranceManager manager;
    manager.initialize(task_queue);
    
    // Create an utterance
    uint32_t utterance_id = manager.createUtterance("session1");
    ASSERT_NE(utterance_id, 0);
    
    // Check initial state
    ASSERT_EQ(manager.getUtteranceState(utterance_id), UtteranceState::LISTENING);
    
    // Get utterance data
    auto utterance = manager.getUtterance(utterance_id);
    ASSERT_NE(utterance, nullptr);
    ASSERT_EQ(utterance->id, utterance_id);
    ASSERT_EQ(utterance->session_id, "session1");
    ASSERT_EQ(utterance->state, UtteranceState::LISTENING);
    
    manager.shutdown();
    task_queue->shutdown();
    return true;
}

// Test state transitions
bool testStateTransitions() {
    std::cout << "Testing state transitions..." << std::endl;
    
    auto task_queue = std::make_shared<TaskQueue>();
    UtteranceManager manager;
    manager.initialize(task_queue);
    
    uint32_t utterance_id = manager.createUtterance("session1");
    
    // Test state transitions
    ASSERT_TRUE(manager.updateUtteranceState(utterance_id, UtteranceState::TRANSCRIBING));
    ASSERT_EQ(manager.getUtteranceState(utterance_id), UtteranceState::TRANSCRIBING);
    
    ASSERT_TRUE(manager.updateUtteranceState(utterance_id, UtteranceState::TRANSLATING));
    ASSERT_EQ(manager.getUtteranceState(utterance_id), UtteranceState::TRANSLATING);
    
    ASSERT_TRUE(manager.updateUtteranceState(utterance_id, UtteranceState::SYNTHESIZING));
    ASSERT_EQ(manager.getUtteranceState(utterance_id), UtteranceState::SYNTHESIZING);
    
    ASSERT_TRUE(manager.updateUtteranceState(utterance_id, UtteranceState::COMPLETE));
    ASSERT_EQ(manager.getUtteranceState(utterance_id), UtteranceState::COMPLETE);
    
    // Test invalid utterance ID
    ASSERT_FALSE(manager.updateUtteranceState(99999, UtteranceState::COMPLETE));
    
    manager.shutdown();
    task_queue->shutdown();
    return true;
}

// Test data setting methods
bool testDataSetting() {
    std::cout << "Testing data setting methods..." << std::endl;
    
    auto task_queue = std::make_shared<TaskQueue>();
    UtteranceManager manager;
    manager.initialize(task_queue);
    
    uint32_t utterance_id = manager.createUtterance("session1");
    
    // Test audio data
    std::vector<float> audio_data = {1.0f, 2.0f, 3.0f, 4.0f};
    ASSERT_TRUE(manager.addAudioData(utterance_id, audio_data));
    
    auto utterance = manager.getUtterance(utterance_id);
    ASSERT_EQ(utterance->audio_buffer.size(), 4);
    ASSERT_EQ(utterance->audio_buffer[0], 1.0f);
    ASSERT_EQ(utterance->audio_buffer[3], 4.0f);
    
    // Test transcription
    ASSERT_TRUE(manager.setTranscription(utterance_id, "Hello world", 0.95f));
    utterance = manager.getUtterance(utterance_id);
    ASSERT_EQ(utterance->transcript, "Hello world");
    ASSERT_EQ(utterance->transcription_confidence, 0.95f);
    
    // Test translation
    ASSERT_TRUE(manager.setTranslation(utterance_id, "Hola mundo"));
    utterance = manager.getUtterance(utterance_id);
    ASSERT_EQ(utterance->translation, "Hola mundo");
    
    // Test synthesized audio
    std::vector<uint8_t> synth_audio = {0x01, 0x02, 0x03};
    ASSERT_TRUE(manager.setSynthesizedAudio(utterance_id, synth_audio));
    utterance = manager.getUtterance(utterance_id);
    ASSERT_EQ(utterance->synthesized_audio.size(), 3);
    ASSERT_EQ(utterance->synthesized_audio[0], 0x01);
    
    // Test language config
    ASSERT_TRUE(manager.setLanguageConfig(utterance_id, "en", "es", "voice1"));
    utterance = manager.getUtterance(utterance_id);
    ASSERT_EQ(utterance->source_language, "en");
    ASSERT_EQ(utterance->target_language, "es");
    ASSERT_EQ(utterance->voice_id, "voice1");
    
    // Test error setting
    ASSERT_TRUE(manager.setUtteranceError(utterance_id, "Test error"));
    utterance = manager.getUtterance(utterance_id);
    ASSERT_EQ(utterance->error_message, "Test error");
    ASSERT_EQ(utterance->state, UtteranceState::ERROR);
    
    manager.shutdown();
    task_queue->shutdown();
    return true;
}

// Test session management
bool testSessionManagement() {
    std::cout << "Testing session management..." << std::endl;
    
    auto task_queue = std::make_shared<TaskQueue>();
    UtteranceManager manager;
    manager.initialize(task_queue);
    
    // Create utterances for different sessions
    uint32_t utterance1 = manager.createUtterance("session1");
    uint32_t utterance2 = manager.createUtterance("session1");
    uint32_t utterance3 = manager.createUtterance("session2");
    
    // Get session utterances
    auto session1_utterances = manager.getSessionUtterances("session1");
    ASSERT_EQ(session1_utterances.size(), 2);
    
    auto session2_utterances = manager.getSessionUtterances("session2");
    ASSERT_EQ(session2_utterances.size(), 1);
    
    auto nonexistent_utterances = manager.getSessionUtterances("session3");
    ASSERT_EQ(nonexistent_utterances.size(), 0);
    
    // Test active utterances
    auto active_utterances = manager.getActiveUtterances();
    ASSERT_EQ(active_utterances.size(), 3); // All are active initially
    
    // Complete one utterance
    manager.updateUtteranceState(utterance1, UtteranceState::COMPLETE);
    active_utterances = manager.getActiveUtterances();
    ASSERT_EQ(active_utterances.size(), 2); // One less active
    
    // Remove session utterances
    size_t removed = manager.removeSessionUtterances("session1");
    ASSERT_EQ(removed, 2);
    
    active_utterances = manager.getActiveUtterances();
    ASSERT_EQ(active_utterances.size(), 1); // Only session2 utterance remains
    
    manager.shutdown();
    task_queue->shutdown();
    return true;
}

// Test statistics
bool testStatistics() {
    std::cout << "Testing statistics..." << std::endl;
    
    auto task_queue = std::make_shared<TaskQueue>();
    UtteranceManager manager;
    manager.initialize(task_queue);
    
    // Initial statistics
    auto stats = manager.getStatistics();
    ASSERT_EQ(stats.total_utterances, 0);
    ASSERT_EQ(stats.active_utterances, 0);
    ASSERT_EQ(stats.completed_utterances, 0);
    ASSERT_EQ(stats.error_utterances, 0);
    
    // Create some utterances
    uint32_t utterance1 = manager.createUtterance("session1");
    uint32_t utterance2 = manager.createUtterance("session1");
    uint32_t utterance3 = manager.createUtterance("session1");
    
    stats = manager.getStatistics();
    ASSERT_EQ(stats.total_utterances, 3);
    ASSERT_EQ(stats.active_utterances, 3);
    ASSERT_EQ(stats.completed_utterances, 0);
    ASSERT_EQ(stats.error_utterances, 0);
    
    // Complete and error some utterances
    manager.updateUtteranceState(utterance1, UtteranceState::COMPLETE);
    manager.updateUtteranceState(utterance2, UtteranceState::ERROR);
    
    stats = manager.getStatistics();
    ASSERT_EQ(stats.total_utterances, 3);
    ASSERT_EQ(stats.active_utterances, 1);
    ASSERT_EQ(stats.completed_utterances, 1);
    ASSERT_EQ(stats.error_utterances, 1);
    
    manager.shutdown();
    task_queue->shutdown();
    return true;
}

// Test concurrent processing
bool testConcurrentProcessing() {
    std::cout << "Testing concurrent processing..." << std::endl;
    
    auto task_queue = std::make_shared<TaskQueue>();
    auto thread_pool = std::make_unique<ThreadPool>(4);
    thread_pool->start(task_queue);
    
    UtteranceManager manager;
    manager.initialize(task_queue);
    
    // Test callback functionality
    std::atomic<int> state_changes{0};
    std::atomic<int> completions{0};
    std::atomic<int> errors{0};
    
    manager.setStateChangeCallback([&](const UtteranceData& utterance) {
        state_changes++;
    });
    
    manager.setCompleteCallback([&](const UtteranceData& utterance) {
        completions++;
    });
    
    manager.setErrorCallback([&](const UtteranceData& utterance, const std::string& error) {
        errors++;
    });
    
    // Create multiple utterances and process them
    std::vector<uint32_t> utterance_ids;
    for (int i = 0; i < 5; ++i) {
        uint32_t id = manager.createUtterance("session1");
        utterance_ids.push_back(id);
        
        // Add some mock audio data
        std::vector<float> audio_data(100, static_cast<float>(i));
        manager.addAudioData(id, audio_data);
        
        // Set language config
        manager.setLanguageConfig(id, "en", "es", "voice1");
        
        // Process the utterance
        ASSERT_TRUE(manager.processUtterance(id));
    }
    
    // Wait for processing to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Check that all utterances completed
    auto stats = manager.getStatistics();
    ASSERT_EQ(stats.completed_utterances, 5);
    
    // Check callbacks were called
    ASSERT_GE(state_changes.load(), 5); // At least one state change per utterance
    ASSERT_EQ(completions.load(), 5);   // One completion per utterance
    ASSERT_EQ(errors.load(), 0);        // No errors expected
    
    thread_pool->stop();
    manager.shutdown();
    task_queue->shutdown();
    return true;
}

// Test capacity limits
bool testCapacityLimits() {
    std::cout << "Testing capacity limits..." << std::endl;
    
    auto task_queue = std::make_shared<TaskQueue>();
    
    // Create manager with limited capacity
    UtteranceManagerConfig config;
    config.max_concurrent_utterances = 3;
    UtteranceManager manager(config);
    manager.initialize(task_queue);
    
    // Create utterances up to the limit
    std::vector<uint32_t> utterance_ids;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(manager.canAcceptNewUtterance());
        uint32_t id = manager.createUtterance("session1");
        ASSERT_NE(id, 0);
        utterance_ids.push_back(id);
    }
    
    // Should not be able to create more
    ASSERT_FALSE(manager.canAcceptNewUtterance());
    uint32_t overflow_id = manager.createUtterance("session1");
    ASSERT_EQ(overflow_id, 0); // Should fail
    
    // Complete one utterance to free up space
    manager.updateUtteranceState(utterance_ids[0], UtteranceState::COMPLETE);
    
    // Should be able to create new utterance now
    ASSERT_TRUE(manager.canAcceptNewUtterance());
    uint32_t new_id = manager.createUtterance("session1");
    ASSERT_NE(new_id, 0);
    
    manager.shutdown();
    task_queue->shutdown();
    return true;
}

// Test cleanup functionality
bool testCleanup() {
    std::cout << "Testing cleanup functionality..." << std::endl;
    
    auto task_queue = std::make_shared<TaskQueue>();
    
    // Create manager with automatic cleanup disabled for manual testing
    UtteranceManagerConfig config;
    config.enable_automatic_cleanup = false;
    UtteranceManager manager(config);
    manager.initialize(task_queue);
    
    // Create and complete some utterances
    uint32_t utterance1 = manager.createUtterance("session1");
    uint32_t utterance2 = manager.createUtterance("session1");
    uint32_t utterance3 = manager.createUtterance("session1");
    
    manager.updateUtteranceState(utterance1, UtteranceState::COMPLETE);
    manager.updateUtteranceState(utterance2, UtteranceState::ERROR);
    // Leave utterance3 active
    
    auto stats = manager.getStatistics();
    ASSERT_EQ(stats.total_utterances, 3);
    ASSERT_EQ(stats.active_utterances, 1);
    
    // Manual cleanup with very short age (should remove completed/errored)
    size_t cleaned = manager.cleanupOldUtterances(std::chrono::seconds(0));
    ASSERT_EQ(cleaned, 2); // Should remove utterance1 and utterance2
    
    stats = manager.getStatistics();
    ASSERT_EQ(stats.active_utterances, 1); // utterance3 should remain
    
    manager.shutdown();
    task_queue->shutdown();
    return true;
}

int main() {
    std::cout << "Running UtteranceManager tests..." << std::endl;
    
    bool all_passed = true;
    
    // Run all tests
    all_passed &= testBasicUtteranceCreation();
    all_passed &= testStateTransitions();
    all_passed &= testDataSetting();
    all_passed &= testSessionManagement();
    all_passed &= testStatistics();
    all_passed &= testConcurrentProcessing();
    all_passed &= testCapacityLimits();
    all_passed &= testCleanup();
    
    if (all_passed) {
        std::cout << "All tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "Some tests failed!" << std::endl;
        return 1;
    }
}