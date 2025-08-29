#pragma once

#include "utils/error_handler.hpp"
#include "core/utterance_manager.hpp"
#include <memory>
#include <functional>
#include <chrono>
#include <map>
#include <queue>

namespace speechrnt {
namespace core {

/**
 * Recovery strategies for different pipeline failures
 */
enum class RecoveryStrategy {
    NONE,
    RETRY_IMMEDIATE,
    RETRY_WITH_DELAY,
    FALLBACK_MODEL,
    SKIP_STAGE,
    RESTART_PIPELINE,
    NOTIFY_CLIENT_ONLY
};

/**
 * Recovery configuration for different error types
 */
struct RecoveryConfig {
    RecoveryStrategy strategy = RecoveryStrategy::RETRY_WITH_DELAY;
    int max_retry_attempts = 3;
    std::chrono::milliseconds retry_delay = std::chrono::milliseconds(1000);
    std::chrono::milliseconds max_retry_delay = std::chrono::milliseconds(10000);
    bool exponential_backoff = true;
    std::string fallback_model_path;
    std::function<bool()> custom_recovery_action;
};

/**
 * Recovery attempt tracking
 */
struct RecoveryAttempt {
    uint32_t utterance_id;
    utils::ErrorCategory error_category;
    int attempt_count = 0;
    std::chrono::steady_clock::time_point last_attempt;
    RecoveryConfig config;
};

/**
 * Pipeline recovery manager that handles graceful error recovery
 * for speech processing pipeline failures
 */
class PipelineRecovery {
public:
    explicit PipelineRecovery(std::shared_ptr<UtteranceManager> utterance_manager);
    ~PipelineRecovery();

    /**
     * Initialize the recovery system
     */
    void initialize();

    /**
     * Shutdown the recovery system
     */
    void shutdown();

    /**
     * Configure recovery strategy for specific error categories
     */
    void configureRecovery(utils::ErrorCategory category, const RecoveryConfig& config);

    /**
     * Attempt to recover from a pipeline error
     * @param error The error information
     * @param utterance_id The utterance that failed
     * @return true if recovery was attempted, false if no recovery possible
     */
    bool attemptRecovery(const utils::ErrorInfo& error, uint32_t utterance_id);

    /**
     * Check if an utterance is currently being recovered
     */
    bool isRecovering(uint32_t utterance_id) const;

    /**
     * Get recovery statistics
     */
    struct RecoveryStats {
        size_t total_recovery_attempts = 0;
        size_t successful_recoveries = 0;
        size_t failed_recoveries = 0;
        std::map<utils::ErrorCategory, size_t> recovery_attempts_by_category;
    };
    RecoveryStats getRecoveryStats() const;

    /**
     * Clear recovery history for completed utterances
     */
    void cleanupCompletedRecoveries();

private:
    /**
     * Execute specific recovery strategies
     */
    bool executeRetryRecovery(const RecoveryAttempt& attempt);
    bool executeFallbackModelRecovery(const RecoveryAttempt& attempt);
    bool executeSkipStageRecovery(const RecoveryAttempt& attempt);
    bool executeRestartPipelineRecovery(const RecoveryAttempt& attempt);
    bool executeCustomRecovery(const RecoveryAttempt& attempt);

    /**
     * Calculate retry delay with exponential backoff
     */
    std::chrono::milliseconds calculateRetryDelay(const RecoveryAttempt& attempt) const;

    /**
     * Schedule delayed recovery attempt
     */
    void scheduleDelayedRecovery(const RecoveryAttempt& attempt);

    /**
     * Recovery worker thread function
     */
    void recoveryWorker();

    /**
     * Notify client about recovery status
     */
    void notifyClientRecoveryStatus(uint32_t utterance_id, const std::string& status, 
                                   bool is_final = false);

    std::shared_ptr<UtteranceManager> utterance_manager_;
    std::map<utils::ErrorCategory, RecoveryConfig> recovery_configs_;
    std::map<uint32_t, RecoveryAttempt> active_recoveries_;
    std::queue<RecoveryAttempt> delayed_recovery_queue_;
    
    mutable std::mutex recovery_mutex_;
    std::condition_variable recovery_cv_;
    std::thread recovery_thread_;
    std::atomic<bool> running_{false};
    
    // Statistics
    mutable std::mutex stats_mutex_;
    RecoveryStats stats_;
};

/**
 * Recovery action factory for common recovery patterns
 */
class RecoveryActionFactory {
public:
    /**
     * Create a model reload recovery action
     */
    static std::function<bool()> createModelReloadAction(const std::string& model_path);

    /**
     * Create a service restart recovery action
     */
    static std::function<bool()> createServiceRestartAction(const std::string& service_name);

    /**
     * Create a cache clear recovery action
     */
    static std::function<bool()> createCacheClearAction();

    /**
     * Create a memory cleanup recovery action
     */
    static std::function<bool()> createMemoryCleanupAction();

    /**
     * Create a GPU reset recovery action
     */
    static std::function<bool()> createGPUResetAction();
};

} // namespace core
} // namespace speechrnt