#include "core/pipeline_recovery.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <random>

namespace speechrnt {
namespace core {

PipelineRecovery::PipelineRecovery(
    std::shared_ptr<UtteranceManager> utterance_manager)
    : utterance_manager_(utterance_manager) {

  // Set up default recovery configurations
  RecoveryConfig default_config;
  default_config.strategy = RecoveryStrategy::RETRY_WITH_DELAY;
  default_config.max_retry_attempts = 3;
  default_config.retry_delay = std::chrono::milliseconds(1000);
  default_config.exponential_backoff = true;

  // Configure specific recovery strategies for different error types
  recovery_configs_[utils::ErrorCategory::STT] = default_config;
  recovery_configs_[utils::ErrorCategory::TRANSLATION] = default_config;
  recovery_configs_[utils::ErrorCategory::TTS] = default_config;

  // Audio processing errors - retry immediately with shorter delays
  RecoveryConfig audio_config = default_config;
  audio_config.strategy = RecoveryStrategy::RETRY_IMMEDIATE;
  audio_config.max_retry_attempts = 2;
  audio_config.retry_delay = std::chrono::milliseconds(100);
  recovery_configs_[utils::ErrorCategory::AUDIO_PROCESSING] = audio_config;

  // Model loading errors - try fallback models
  RecoveryConfig model_config;
  model_config.strategy = RecoveryStrategy::FALLBACK_MODEL;
  model_config.max_retry_attempts = 1;
  recovery_configs_[utils::ErrorCategory::MODEL_LOADING] = model_config;

  // WebSocket errors - notify client only
  RecoveryConfig websocket_config;
  websocket_config.strategy = RecoveryStrategy::NOTIFY_CLIENT_ONLY;
  recovery_configs_[utils::ErrorCategory::WEBSOCKET] = websocket_config;

  // Pipeline errors - restart pipeline
  RecoveryConfig pipeline_config;
  pipeline_config.strategy = RecoveryStrategy::RESTART_PIPELINE;
  pipeline_config.max_retry_attempts = 2;
  recovery_configs_[utils::ErrorCategory::PIPELINE] = pipeline_config;
}

PipelineRecovery::~PipelineRecovery() { shutdown(); }

void PipelineRecovery::initialize() {
  running_ = true;
  recovery_thread_ = std::thread(&PipelineRecovery::recoveryWorker, this);
  speechrnt::utils::Logger::info("PipelineRecovery initialized");
}

void PipelineRecovery::shutdown() {
  if (running_) {
    running_ = false;
    recovery_cv_.notify_all();

    if (recovery_thread_.joinable()) {
      recovery_thread_.join();
    }

    std::lock_guard<std::mutex> lock(recovery_mutex_);
    active_recoveries_.clear();
    while (!delayed_recovery_queue_.empty()) {
      delayed_recovery_queue_.pop();
    }

    speechrnt::utils::Logger::info("PipelineRecovery shutdown complete");
  }
}

void PipelineRecovery::configureRecovery(utils::ErrorCategory category,
                                         const RecoveryConfig &config) {
  std::lock_guard<std::mutex> lock(recovery_mutex_);
  recovery_configs_[category] = config;
  speechrnt::utils::Logger::info(
      "Recovery configuration updated for category: " +
      std::to_string(static_cast<int>(category)));
}

bool PipelineRecovery::attemptRecovery(const utils::ErrorInfo &error,
                                       uint32_t utterance_id) {
  std::lock_guard<std::mutex> lock(recovery_mutex_);

  // Check if we have a recovery configuration for this error category
  auto config_it = recovery_configs_.find(error.category);
  if (config_it == recovery_configs_.end()) {
    speechrnt::utils::Logger::warn(
        "No recovery configuration for error category: " +
        std::to_string(static_cast<int>(error.category)));
    return false;
  }

  const RecoveryConfig &config = config_it->second;

  // Check if we're already recovering this utterance
  auto recovery_it = active_recoveries_.find(utterance_id);
  if (recovery_it != active_recoveries_.end()) {
    RecoveryAttempt &attempt = recovery_it->second;

    // Check if we've exceeded max retry attempts
    if (attempt.attempt_count >= config.max_retry_attempts) {
      speechrnt::utils::Logger::error(
          "Max recovery attempts exceeded for utterance " +
          std::to_string(utterance_id));
      active_recoveries_.erase(recovery_it);

      // Update statistics
      std::lock_guard<std::mutex> stats_lock(stats_mutex_);
      stats_.failed_recoveries++;

      // Notify client of final failure
      notifyClientRecoveryStatus(utterance_id,
                                 "Recovery failed after " +
                                     std::to_string(config.max_retry_attempts) +
                                     " attempts",
                                 true);
      return false;
    }

    attempt.attempt_count++;
    attempt.last_attempt = std::chrono::steady_clock::now();
  } else {
    // Create new recovery attempt
    RecoveryAttempt attempt;
    attempt.utterance_id = utterance_id;
    attempt.error_category = error.category;
    attempt.attempt_count = 1;
    attempt.last_attempt = std::chrono::steady_clock::now();
    attempt.config = config;

    active_recoveries_[utterance_id] = attempt;

    // Update statistics
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.total_recovery_attempts++;
    stats_.recovery_attempts_by_category[error.category]++;
  }

  RecoveryAttempt &attempt = active_recoveries_[utterance_id];

  speechrnt::utils::Logger::info(
      "Attempting recovery for utterance " + std::to_string(utterance_id) +
      " (attempt " + std::to_string(attempt.attempt_count) + "/" +
      std::to_string(config.max_retry_attempts) + ")");

  // Execute recovery strategy
  bool recovery_success = false;

  switch (config.strategy) {
  case RecoveryStrategy::RETRY_IMMEDIATE:
    recovery_success = executeRetryRecovery(attempt);
    break;

  case RecoveryStrategy::RETRY_WITH_DELAY:
    scheduleDelayedRecovery(attempt);
    return true; // Recovery scheduled, not completed yet

  case RecoveryStrategy::FALLBACK_MODEL:
    recovery_success = executeFallbackModelRecovery(attempt);
    break;

  case RecoveryStrategy::SKIP_STAGE:
    recovery_success = executeSkipStageRecovery(attempt);
    break;

  case RecoveryStrategy::RESTART_PIPELINE:
    recovery_success = executeRestartPipelineRecovery(attempt);
    break;

  case RecoveryStrategy::NOTIFY_CLIENT_ONLY:
    notifyClientRecoveryStatus(utterance_id, "Error occurred: " + error.message,
                               false);
    recovery_success = true; // Consider notification as successful recovery
    break;

  case RecoveryStrategy::NONE:
  default:
    speechrnt::utils::Logger::info(
        "No recovery strategy configured for error category");
    active_recoveries_.erase(utterance_id);
    return false;
  }

  if (recovery_success) {
    speechrnt::utils::Logger::info("Recovery successful for utterance " +
                                   std::to_string(utterance_id));
    active_recoveries_.erase(utterance_id);

    // Update statistics
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.successful_recoveries++;

    notifyClientRecoveryStatus(utterance_id, "Recovery successful", true);
  } else {
    speechrnt::utils::Logger::warn("Recovery attempt failed for utterance " +
                                   std::to_string(utterance_id));

    // If this was the last attempt, clean up
    if (attempt.attempt_count >= config.max_retry_attempts) {
      active_recoveries_.erase(utterance_id);

      // Update statistics
      std::lock_guard<std::mutex> stats_lock(stats_mutex_);
      stats_.failed_recoveries++;

      notifyClientRecoveryStatus(utterance_id, "Recovery failed", true);
    }
  }

  return recovery_success;
}

bool PipelineRecovery::isRecovering(uint32_t utterance_id) const {
  std::lock_guard<std::mutex> lock(recovery_mutex_);
  return active_recoveries_.find(utterance_id) != active_recoveries_.end();
}

PipelineRecovery::RecoveryStats PipelineRecovery::getRecoveryStats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return stats_;
}

void PipelineRecovery::cleanupCompletedRecoveries() {
  std::lock_guard<std::mutex> lock(recovery_mutex_);

  auto it = active_recoveries_.begin();
  while (it != active_recoveries_.end()) {
    // Check if utterance is completed or no longer exists
    if (!utterance_manager_ || !utterance_manager_->getUtterance(it->first)) {
      it = active_recoveries_.erase(it);
    } else {
      ++it;
    }
  }
}

bool PipelineRecovery::executeRetryRecovery(const RecoveryAttempt &attempt) {
  if (!utterance_manager_) {
    return false;
  }

  // Get the utterance and retry the failed stage
  auto utterance = utterance_manager_->getUtterance(attempt.utterance_id);
  if (!utterance) {
    return false;
  }

  // Reset utterance state to retry the failed stage
  switch (attempt.error_category) {
  case utils::ErrorCategory::STT:
    utterance->state = UtteranceState::TRANSCRIBING;
    break;
  case utils::ErrorCategory::TRANSLATION:
    utterance->state = UtteranceState::TRANSLATING;
    break;
  case utils::ErrorCategory::TTS:
    utterance->state = UtteranceState::SYNTHESIZING;
    break;
  default:
    return false;
  }

  // Clear any error state
  utterance->error_message.clear();

  // Re-queue the utterance for processing
  // This would typically involve re-submitting to the task queue
  speechrnt::utils::Logger::info("Retrying processing for utterance " +
                                 std::to_string(attempt.utterance_id));

  return true;
}

bool PipelineRecovery::executeFallbackModelRecovery(
    const RecoveryAttempt &attempt) {
  // Implementation would depend on model management system
  // For now, just log the attempt
  speechrnt::utils::Logger::info(
      "Attempting fallback model recovery for utterance " +
      std::to_string(attempt.utterance_id));

  if (!attempt.config.fallback_model_path.empty()) {
    speechrnt::utils::Logger::info("Using fallback model: " +
                                   attempt.config.fallback_model_path);
    // Load fallback model and retry
    return executeRetryRecovery(attempt);
  }

  return false;
}

bool PipelineRecovery::executeSkipStageRecovery(
    const RecoveryAttempt &attempt) {
  if (!utterance_manager_) {
    return false;
  }

  auto utterance = utterance_manager_->getUtterance(attempt.utterance_id);
  if (!utterance) {
    return false;
  }

  // Skip the failed stage and move to the next one
  switch (attempt.error_category) {
  case utils::ErrorCategory::STT:
    // Skip transcription, use placeholder text
    utterance->transcript = "[Transcription unavailable]";
    utterance->state = UtteranceState::TRANSLATING;
    break;
  case utils::ErrorCategory::TRANSLATION:
    // Skip translation, use original text
    utterance->translation = utterance->transcript;
    utterance->state = UtteranceState::SYNTHESIZING;
    break;
  case utils::ErrorCategory::TTS:
    // Skip synthesis, mark as complete
    utterance->state = UtteranceState::COMPLETE;
    break;
  default:
    return false;
  }

  speechrnt::utils::Logger::info("Skipped failed stage for utterance " +
                                 std::to_string(attempt.utterance_id));
  return true;
}

bool PipelineRecovery::executeRestartPipelineRecovery(
    const RecoveryAttempt &attempt) {
  if (!utterance_manager_) {
    return false;
  }

  auto utterance = utterance_manager_->getUtterance(attempt.utterance_id);
  if (!utterance) {
    return false;
  }

  // Reset utterance to initial state and restart entire pipeline
  utterance->state = UtteranceState::TRANSCRIBING;
  utterance->transcript.clear();
  utterance->translation.clear();
  utterance->synthesized_audio.clear();
  utterance->error_message.clear();

  speechrnt::utils::Logger::info("Restarting pipeline for utterance " +
                                 std::to_string(attempt.utterance_id));

  // Re-queue for complete processing
  return true;
}

bool PipelineRecovery::executeCustomRecovery(const RecoveryAttempt &attempt) {
  if (attempt.config.custom_recovery_action) {
    try {
      return attempt.config.custom_recovery_action();
    } catch (const std::exception &e) {
      speechrnt::utils::Logger::error("Custom recovery action failed: " +
                                      std::string(e.what()));
      return false;
    }
  }
  return false;
}

std::chrono::milliseconds
PipelineRecovery::calculateRetryDelay(const RecoveryAttempt &attempt) const {
  if (!attempt.config.exponential_backoff) {
    return attempt.config.retry_delay;
  }

  // Exponential backoff with jitter
  auto base_delay = attempt.config.retry_delay.count();
  auto exponential_delay = base_delay * std::pow(2, attempt.attempt_count - 1);

  // Add random jitter (±25%)
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<> jitter(0.75, 1.25);

  auto final_delay = static_cast<long long>(exponential_delay * jitter(gen));

  // Cap at maximum delay
  final_delay =
      std::min(final_delay,
               static_cast<long long>(attempt.config.max_retry_delay.count()));

  return std::chrono::milliseconds(final_delay);
}

void PipelineRecovery::scheduleDelayedRecovery(const RecoveryAttempt &attempt) {
  auto delay = calculateRetryDelay(attempt);

  speechrnt::utils::Logger::info("Scheduling delayed recovery for utterance " +
                                 std::to_string(attempt.utterance_id) + " in " +
                                 std::to_string(delay.count()) + "ms");

  // Add to delayed recovery queue
  delayed_recovery_queue_.push(attempt);
  recovery_cv_.notify_one();
}

void PipelineRecovery::recoveryWorker() {
  speechrnt::utils::Logger::info("Recovery worker thread started");

  while (running_) {
    std::unique_lock<std::mutex> lock(recovery_mutex_);

    // Wait for delayed recoveries or shutdown signal
    recovery_cv_.wait(
        lock, [this] { return !running_ || !delayed_recovery_queue_.empty(); });

    if (!running_) {
      break;
    }

    // Process delayed recoveries
    while (!delayed_recovery_queue_.empty()) {
      RecoveryAttempt attempt = delayed_recovery_queue_.front();
      delayed_recovery_queue_.pop();

      // Check if delay has elapsed
      auto now = std::chrono::steady_clock::now();
      auto delay = calculateRetryDelay(attempt);
      auto target_time = attempt.last_attempt + delay;

      if (now >= target_time) {
        // Execute the recovery
        lock.unlock();

        bool success = false;
        switch (attempt.config.strategy) {
        case RecoveryStrategy::RETRY_WITH_DELAY:
          success = executeRetryRecovery(attempt);
          break;
        default:
          success = executeCustomRecovery(attempt);
          break;
        }

        lock.lock();

        if (success) {
          // Remove from active recoveries
          active_recoveries_.erase(attempt.utterance_id);

          // Update statistics
          std::lock_guard<std::mutex> stats_lock(stats_mutex_);
          stats_.successful_recoveries++;

          notifyClientRecoveryStatus(attempt.utterance_id,
                                     "Delayed recovery successful", true);
        } else {
          // Check if we should retry again
          if (attempt.attempt_count < attempt.config.max_retry_attempts) {
            attempt.attempt_count++;
            attempt.last_attempt = now;
            delayed_recovery_queue_.push(attempt);
          } else {
            // Max attempts reached
            active_recoveries_.erase(attempt.utterance_id);

            // Update statistics
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.failed_recoveries++;

            notifyClientRecoveryStatus(attempt.utterance_id,
                                       "Delayed recovery failed", true);
          }
        }
      } else {
        // Put back in queue for later
        delayed_recovery_queue_.push(attempt);
        break;
      }
    }
  }

  speechrnt::utils::Logger::info("Recovery worker thread stopped");
}

void PipelineRecovery::notifyClientRecoveryStatus(uint32_t utterance_id,
                                                  const std::string &status,
                                                  bool is_final) {
  // This would typically send a WebSocket message to the client
  // For now, just log the notification
  speechrnt::utils::Logger::info("Recovery status for utterance " +
                                 std::to_string(utterance_id) + ": " + status +
                                 (is_final ? " (final)" : ""));
}

// RecoveryActionFactory implementations
std::function<bool()>
RecoveryActionFactory::createModelReloadAction(const std::string &model_path) {
  return [model_path]() -> bool {
    speechrnt::utils::Logger::info("Reloading model: " + model_path);
    // Implementation would reload the specified model
    return true;
  };
}

std::function<bool()> RecoveryActionFactory::createServiceRestartAction(
    const std::string &service_name) {
  return [service_name]() -> bool {
    speechrnt::utils::Logger::info("Restarting service: " + service_name);
    // Implementation would restart the specified service
    return true;
  };
}

std::function<bool()> RecoveryActionFactory::createCacheClearAction() {
  return []() -> bool {
    speechrnt::utils::Logger::info("Clearing caches");
    // Implementation would clear various caches
    return true;
  };
}

std::function<bool()> RecoveryActionFactory::createMemoryCleanupAction() {
  return []() -> bool {
    speechrnt::utils::Logger::info("Performing memory cleanup");
    // Implementation would perform memory cleanup
    return true;
  };
}

std::function<bool()> RecoveryActionFactory::createGPUResetAction() {
  return []() -> bool {
    speechrnt::utils::Logger::info("Resetting GPU state");
    // Implementation would reset GPU state
    return true;
  };
}

} // namespace core
} // namespace speechrnt