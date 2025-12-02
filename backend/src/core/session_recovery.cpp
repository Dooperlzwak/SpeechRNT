#include "core/session_recovery.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <random>
#include <fstream>
#include <sstream>

namespace speechrnt {
namespace core {

SessionRecoveryManager::SessionRecoveryManager(const SessionRecoveryConfig& config)
    : config_(config) {
}

SessionRecoveryManager::~SessionRecoveryManager() {
    shutdown();
}

void SessionRecoveryManager::initialize() {
    running_ = true;
    
    // Load existing session data if persistent storage is enabled
    if (config_.enable_persistent_storage) {
        loadFromStorage();
    }
    
    // Start cleanup worker thread
    cleanup_thread_ = std::thread(&SessionRecoveryManager::cleanupWorker, this);
    
    speechrnt::utils::Logger::info("SessionRecoveryManager initialized");
}

void SessionRecoveryManager::shutdown() {
    if (running_) {
        running_ = false;
        
        if (cleanup_thread_.joinable()) {
            cleanup_thread_.join();
        }
        
        // Save session data if persistent storage is enabled
        if (config_.enable_persistent_storage) {
            saveToStorage();
        }
        
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        stored_sessions_.clear();
        
        speechrnt::utils::Logger::info("SessionRecoveryManager shutdown complete");
    }
}

bool SessionRecoveryManager::storeSessionData(const std::string& session_id, const SessionRecoveryData& data) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    // Check if we've reached the maximum number of stored sessions
    if (stored_sessions_.size() >= config_.max_stored_sessions) {
        // Remove oldest session
        auto oldest_it = std::min_element(stored_sessions_.begin(), stored_sessions_.end(),
            [](const auto& a, const auto& b) {
                return a.second->last_activity < b.second->last_activity;
            });
        
        if (oldest_it != stored_sessions_.end()) {
            speechrnt::utils::Logger::info("Removing oldest session to make room: " + oldest_it->first);
            stored_sessions_.erase(oldest_it);
        }
    }
    
    // Validate the recovery data
    if (!SessionRecoveryHelper::validateRecoveryData(data)) {
        speechrnt::utils::Logger::error("Invalid session recovery data for session: " + session_id);
        return false;
    }
    
    // Store the session data
    auto session_data = std::make_shared<SessionRecoveryData>(data);
    session_data->created_at = std::chrono::steady_clock::now();
    stored_sessions_[session_id] = session_data;
    
    // Update statistics
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.total_sessions_stored++;
    stats_.current_stored_sessions = stored_sessions_.size();
    
    speechrnt::utils::Logger::info("Stored session recovery data for session: " + session_id);
    return true;
}

bool SessionRecoveryManager::recoverSession(const std::string& session_id, std::shared_ptr<ClientSession> new_session) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = stored_sessions_.find(session_id);
    if (it == stored_sessions_.end()) {
        speechrnt::utils::Logger::warn("No recovery data found for session: " + session_id);
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.failed_recoveries++;
        return false;
    }
    
    auto& recovery_data = *it->second;
    
    // Check if session has expired
    if (isSessionExpired(recovery_data)) {
        speechrnt::utils::Logger::warn("Session recovery data expired for session: " + session_id);
        stored_sessions_.erase(it);
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.failed_recoveries++;
        stats_.expired_sessions++;
        stats_.current_stored_sessions = stored_sessions_.size();
        return false;
    }
    
    // Check recovery attempts
    if (recovery_data.recovery_attempts >= config_.max_recovery_attempts) {
        speechrnt::utils::Logger::error("Max recovery attempts exceeded for session: " + session_id);
        recovery_data.is_recoverable = false;
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.failed_recoveries++;
        return false;
    }
    
    // Update recovery attempt info
    recovery_data.recovery_attempts++;
    recovery_data.last_recovery_attempt = std::chrono::steady_clock::now();
    
    // Attempt recovery using callback if set
    bool recovery_success = false;
    if (recovery_callback_) {
        try {
            recovery_success = recovery_callback_(recovery_data, new_session);
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Exception during session recovery callback: " + std::string(e.what()));
            recovery_success = false;
        }
    } else {
        // Default recovery: apply recovery data to new session
        recovery_success = SessionRecoveryHelper::applyToClientSession(recovery_data, new_session);
    }
    
    if (recovery_success) {
        speechrnt::utils::Logger::info("Successfully recovered session: " + session_id);
        
        // Update session activity and remove from storage (session is now active)
        stored_sessions_.erase(it);
        
        // Update statistics
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.successful_recoveries++;
        stats_.current_stored_sessions = stored_sessions_.size();
        
        return true;
    } else {
        speechrnt::utils::Logger::error("Failed to recover session: " + session_id);
        
        // Update statistics
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.failed_recoveries++;
        
        return false;
    }
}

void SessionRecoveryManager::removeSessionData(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = stored_sessions_.find(session_id);
    if (it != stored_sessions_.end()) {
        stored_sessions_.erase(it);
        
        // Update statistics
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.current_stored_sessions = stored_sessions_.size();
        
        speechrnt::utils::Logger::info("Removed session recovery data for session: " + session_id);
    }
}

void SessionRecoveryManager::updateSessionActivity(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = stored_sessions_.find(session_id);
    if (it != stored_sessions_.end()) {
        it->second->last_activity = std::chrono::steady_clock::now();
    }
}

bool SessionRecoveryManager::canRecoverSession(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = stored_sessions_.find(session_id);
    if (it == stored_sessions_.end()) {
        return false;
    }
    
    const auto& recovery_data = *it->second;
    return recovery_data.is_recoverable && 
           !isSessionExpired(recovery_data) &&
           recovery_data.recovery_attempts < config_.max_recovery_attempts;
}

std::shared_ptr<SessionRecoveryData> SessionRecoveryManager::getSessionData(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = stored_sessions_.find(session_id);
    if (it != stored_sessions_.end()) {
        return it->second;
    }
    return nullptr;
}

void SessionRecoveryManager::setRecoveryCallback(SessionRecoveryCallback callback) {
    recovery_callback_ = callback;
}

void SessionRecoveryManager::setCleanupCallback(SessionCleanupCallback callback) {
    cleanup_callback_ = callback;
}

SessionRecoveryManager::RecoveryStats SessionRecoveryManager::getRecoveryStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void SessionRecoveryManager::cleanupExpiredSessions() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = stored_sessions_.begin();
    size_t cleaned_count = 0;
    
    while (it != stored_sessions_.end()) {
        if (isSessionExpired(*it->second)) {
            // Call cleanup callback if set
            if (cleanup_callback_) {
                try {
                    cleanup_callback_(it->first);
                } catch (const std::exception& e) {
                    speechrnt::utils::Logger::error("Exception during session cleanup callback: " + std::string(e.what()));
                }
            }
            
            speechrnt::utils::Logger::info("Cleaning up expired session: " + it->first);
            it = stored_sessions_.erase(it);
            cleaned_count++;
        } else {
            ++it;
        }
    }
    
    if (cleaned_count > 0) {
        // Update statistics
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.expired_sessions += cleaned_count;
        stats_.current_stored_sessions = stored_sessions_.size();
        
        speechrnt::utils::Logger::info("Cleaned up " + std::to_string(cleaned_count) + " expired sessions");
    }
}

std::vector<SessionRecoveryData> SessionRecoveryManager::exportSessionData() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    std::vector<SessionRecoveryData> result;
    result.reserve(stored_sessions_.size());
    
    for (const auto& pair : stored_sessions_) {
        result.push_back(*pair.second);
    }
    
    return result;
}

void SessionRecoveryManager::cleanupWorker() {
    speechrnt::utils::Logger::info("Session recovery cleanup worker started");
    
    while (running_) {
        try {
            cleanupExpiredSessions();
            
            // Sleep for cleanup interval
            std::this_thread::sleep_for(config_.cleanup_interval);
        } catch (const std::exception& e) {
            speechrnt::utils::Logger::error("Exception in session recovery cleanup worker: " + std::string(e.what()));
        }
    }
    
    speechrnt::utils::Logger::info("Session recovery cleanup worker stopped");
}

bool SessionRecoveryManager::isSessionExpired(const SessionRecoveryData& data) const {
    auto now = std::chrono::steady_clock::now();
    auto time_since_activity = now - data.last_activity;
    return time_since_activity > config_.session_timeout;
}

bool SessionRecoveryManager::loadFromStorage() {
    // Simple file-based storage implementation
    // In production, this could be replaced with a database
    
    std::ifstream file(config_.storage_path);
    if (!file.is_open()) {
        speechrnt::utils::Logger::info("No existing session recovery storage found");
        return true; // Not an error, just no existing data
    }
    
    try {
        // Simple JSON-like format for demonstration
        // In production, use a proper serialization library
        std::string line;
        while (std::getline(file, line)) {
            // Parse session data from line
            // This is a simplified implementation
            speechrnt::utils::Logger::info("Loaded session recovery data from storage");
        }
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to load session recovery data: " + std::string(e.what()));
        return false;
    }
}

bool SessionRecoveryManager::saveToStorage() {
    std::ofstream file(config_.storage_path);
    if (!file.is_open()) {
        speechrnt::utils::Logger::error("Failed to open session recovery storage for writing");
        return false;
    }
    
    try {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        
        // Simple JSON-like format for demonstration
        for (const auto& pair : stored_sessions_) {
            // Serialize session data to line
            // This is a simplified implementation
            file << pair.first << std::endl;
        }
        
        file.close();
        speechrnt::utils::Logger::info("Saved session recovery data to storage");
        return true;
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to save session recovery data: " + std::string(e.what()));
        return false;
    }
}

std::string SessionRecoveryManager::generateSessionId() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << "session_";
    for (int i = 0; i < 16; ++i) {
        ss << std::hex << dis(gen);
    }
    
    return ss.str();
}

// SessionRecoveryHelper implementations
SessionRecoveryData SessionRecoveryHelper::createFromClientSession(std::shared_ptr<ClientSession> session) {
    SessionRecoveryData data;
    
    if (!session) {
        return data;
    }
    
    data.session_id = session->getSessionId();
    data.client_id = session->getClientId();
    data.last_activity = std::chrono::steady_clock::now();
    data.source_lang = session->getSourceLanguage();
    data.target_lang = session->getTargetLanguage();
    data.voice_id = session->getVoiceId();
    data.is_active = session->isActive();
    
    // Get pending utterances
    auto utterances = session->getPendingUtterances();
    for (const auto& utterance : utterances) {
        data.pending_utterances.push_back(utterance->id);
    }
    
    data.last_known_state = session->getCurrentState();
    
    return data;
}

bool SessionRecoveryHelper::applyToClientSession(const SessionRecoveryData& data, std::shared_ptr<ClientSession> session) {
    if (!session || !validateRecoveryData(data)) {
        return false;
    }
    
    try {
        session->setSessionId(data.session_id);
        session->setSourceLanguage(data.source_lang);
        session->setTargetLanguage(data.target_lang);
        session->setVoiceId(data.voice_id);
        session->setActive(data.is_active);
        session->setCurrentState(data.last_known_state);
        
        // Restore custom data
        for (const auto& pair : data.custom_data) {
            session->setCustomData(pair.first, pair.second);
        }
        
        return true;
    } catch (const std::exception& e) {
        speechrnt::utils::Logger::error("Failed to apply recovery data to session: " + std::string(e.what()));
        return false;
    }
}

bool SessionRecoveryHelper::validateRecoveryData(const SessionRecoveryData& data) {
    // Basic validation
    if (data.session_id.empty() || data.client_id.empty()) {
        return false;
    }
    
    if (data.source_lang.empty() || data.target_lang.empty()) {
        return false;
    }
    
    // Check if data is not too old
    auto now = std::chrono::steady_clock::now();
    auto age = now - data.created_at;
    if (age > std::chrono::hours(24)) { // 24 hours max age
        return false;
    }
    
    return true;
}

SessionRecoveryData SessionRecoveryHelper::mergeRecoveryData(const SessionRecoveryData& existing, const SessionRecoveryData& update) {
    SessionRecoveryData merged = existing;
    
    // Update fields that have changed
    if (!update.source_lang.empty()) {
        merged.source_lang = update.source_lang;
    }
    if (!update.target_lang.empty()) {
        merged.target_lang = update.target_lang;
    }
    if (!update.voice_id.empty()) {
        merged.voice_id = update.voice_id;
    }
    if (!update.last_known_state.empty()) {
        merged.last_known_state = update.last_known_state;
    }
    
    merged.is_active = update.is_active;
    merged.last_activity = update.last_activity;
    merged.pending_utterances = update.pending_utterances;
    
    // Merge custom data
    for (const auto& pair : update.custom_data) {
        merged.custom_data[pair.first] = pair.second;
    }
    
    return merged;
}

double SessionRecoveryHelper::calculateRecoveryScore(const SessionRecoveryData& data) {
    double score = 1.0;
    
    // Reduce score based on age
    auto now = std::chrono::steady_clock::now();
    auto age = now - data.last_activity;
    auto age_minutes = std::chrono::duration_cast<std::chrono::minutes>(age).count();
    
    if (age_minutes > 60) { // More than 1 hour old
        score *= 0.5;
    } else if (age_minutes > 30) { // More than 30 minutes old
        score *= 0.7;
    } else if (age_minutes > 10) { // More than 10 minutes old
        score *= 0.9;
    }
    
    // Reduce score based on recovery attempts
    score *= std::pow(0.8, data.recovery_attempts);
    
    // Reduce score if not recoverable
    if (!data.is_recoverable) {
        score = 0.0;
    }
    
    return std::max(0.0, std::min(1.0, score));
}

} // namespace core
} // namespace speechrnt