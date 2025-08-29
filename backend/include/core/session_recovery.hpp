#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <functional>
#include "core/client_session.hpp"
#include "utils/error_handler.hpp"

namespace speechrnt {
namespace core {

/**
 * Session recovery data structure
 */
struct SessionRecoveryData {
    std::string session_id;
    std::string client_id;
    std::chrono::steady_clock::time_point last_activity;
    std::chrono::steady_clock::time_point created_at;
    
    // Session configuration
    std::string source_lang;
    std::string target_lang;
    std::string voice_id;
    bool is_active;
    
    // Recovery state
    std::vector<uint32_t> pending_utterances;
    std::string last_known_state;
    std::unordered_map<std::string, std::string> custom_data;
    
    // Recovery metadata
    int recovery_attempts = 0;
    std::chrono::steady_clock::time_point last_recovery_attempt;
    bool is_recoverable = true;
};

/**
 * Session recovery configuration
 */
struct SessionRecoveryConfig {
    std::chrono::milliseconds session_timeout = std::chrono::milliseconds(300000); // 5 minutes
    std::chrono::milliseconds cleanup_interval = std::chrono::milliseconds(60000); // 1 minute
    int max_recovery_attempts = 3;
    size_t max_stored_sessions = 1000;
    bool enable_persistent_storage = false;
    std::string storage_path = "./session_recovery.db";
};

/**
 * Session recovery callback types
 */
using SessionRecoveryCallback = std::function<bool(const SessionRecoveryData&, std::shared_ptr<ClientSession>)>;
using SessionCleanupCallback = std::function<void(const std::string& session_id)>;

/**
 * Session recovery manager for handling client reconnections
 * and maintaining session state across connection drops
 */
class SessionRecoveryManager {
public:
    explicit SessionRecoveryManager(const SessionRecoveryConfig& config = SessionRecoveryConfig{});
    ~SessionRecoveryManager();

    /**
     * Initialize the session recovery manager
     */
    void initialize();

    /**
     * Shutdown the session recovery manager
     */
    void shutdown();

    /**
     * Store session data for recovery
     */
    bool storeSessionData(const std::string& session_id, const SessionRecoveryData& data);

    /**
     * Attempt to recover a session
     */
    bool recoverSession(const std::string& session_id, std::shared_ptr<ClientSession> new_session);

    /**
     * Remove session data (called when session ends normally)
     */
    void removeSessionData(const std::string& session_id);

    /**
     * Update session activity timestamp
     */
    void updateSessionActivity(const std::string& session_id);

    /**
     * Check if a session can be recovered
     */
    bool canRecoverSession(const std::string& session_id) const;

    /**
     * Get session recovery data
     */
    std::shared_ptr<SessionRecoveryData> getSessionData(const std::string& session_id) const;

    /**
     * Set recovery callback
     */
    void setRecoveryCallback(SessionRecoveryCallback callback);

    /**
     * Set cleanup callback
     */
    void setCleanupCallback(SessionCleanupCallback callback);

    /**
     * Get recovery statistics
     */
    struct RecoveryStats {
        size_t total_sessions_stored = 0;
        size_t successful_recoveries = 0;
        size_t failed_recoveries = 0;
        size_t expired_sessions = 0;
        size_t current_stored_sessions = 0;
    };
    RecoveryStats getRecoveryStats() const;

    /**
     * Manually trigger cleanup of expired sessions
     */
    void cleanupExpiredSessions();

    /**
     * Export session data for debugging
     */
    std::vector<SessionRecoveryData> exportSessionData() const;

private:
    /**
     * Cleanup worker thread function
     */
    void cleanupWorker();

    /**
     * Check if session has expired
     */
    bool isSessionExpired(const SessionRecoveryData& data) const;

    /**
     * Load session data from persistent storage
     */
    bool loadFromStorage();

    /**
     * Save session data to persistent storage
     */
    bool saveToStorage();

    /**
     * Generate unique session ID
     */
    std::string generateSessionId() const;

    SessionRecoveryConfig config_;
    std::unordered_map<std::string, std::shared_ptr<SessionRecoveryData>> stored_sessions_;
    
    mutable std::mutex sessions_mutex_;
    std::thread cleanup_thread_;
    std::atomic<bool> running_{false};
    
    SessionRecoveryCallback recovery_callback_;
    SessionCleanupCallback cleanup_callback_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    RecoveryStats stats_;
};

/**
 * Session recovery helper functions
 */
class SessionRecoveryHelper {
public:
    /**
     * Create session recovery data from client session
     */
    static SessionRecoveryData createFromClientSession(std::shared_ptr<ClientSession> session);

    /**
     * Apply recovery data to client session
     */
    static bool applyToClientSession(const SessionRecoveryData& data, std::shared_ptr<ClientSession> session);

    /**
     * Validate session recovery data
     */
    static bool validateRecoveryData(const SessionRecoveryData& data);

    /**
     * Merge recovery data (for partial updates)
     */
    static SessionRecoveryData mergeRecoveryData(const SessionRecoveryData& existing, const SessionRecoveryData& update);

    /**
     * Calculate session recovery score (0.0 to 1.0)
     */
    static double calculateRecoveryScore(const SessionRecoveryData& data);
};

} // namespace core
} // namespace speechrnt