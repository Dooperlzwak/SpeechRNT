#include "audio/voice_activity_detector.hpp"
#include "audio/silero_vad_impl.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace audio {

// VoiceActivityDetector implementation
VoiceActivityDetector::VoiceActivityDetector(const VadConfig& config)
    : config_(config), initialized_(false), currentState_(VadState::IDLE),
      currentUtteranceId_(0), nextUtteranceId_(1), lastError_(ErrorCode::NONE) {
    
    if (!config_.isValid()) {
        setError(ErrorCode::INVALID_CONFIG);
        throw std::invalid_argument("Invalid VAD configuration");
    }
    
    // Create enhanced silero-vad implementation
    sileroVad_ = std::make_unique<SileroVadImpl>();
    
    // Create energy-based fallback
    EnergyBasedVAD::Config energyConfig;
    energyConfig.energyThreshold = config_.speechThreshold * 0.5f; // More sensitive for fallback
    energyConfig.useAdaptiveThreshold = true;
    energyConfig.useSpectralFeatures = true;
    energyBasedVad_ = std::make_unique<EnergyBasedVAD>(energyConfig);
    
    // Initialize statistics
    stats_ = {};
    stats_.lastActivity = std::chrono::steady_clock::now();
    
    utils::Logger::info("VoiceActivityDetector created with speech threshold: " + 
                       std::to_string(config_.speechThreshold));
}

VoiceActivityDetector::~VoiceActivityDetector() {
    shutdown();
}

bool VoiceActivityDetector::initialize() {
    if (initialized_) {
        utils::Logger::warn("VoiceActivityDetector already initialized");
        return true;
    }
    
    try {
        // Initialize enhanced silero-vad implementation
        std::string modelPath = "";
#ifdef SILERO_MODEL_PATH
        modelPath = SILERO_MODEL_PATH;
#endif
        
        if (!sileroVad_->initialize(config_.sampleRate, modelPath)) {
            utils::Logger::warn("Silero-VAD initialization failed, using energy-based fallback");
            // Continue with energy-based fallback - this is not a fatal error
        }
        
        // Set VAD mode based on what's available
        if (sileroVad_->isSileroModelLoaded()) {
            sileroVad_->setVadMode(SileroVadImpl::VadMode::HYBRID);
            utils::Logger::info("VAD initialized with silero-vad ML model and energy-based fallback");
        } else {
            sileroVad_->setVadMode(SileroVadImpl::VadMode::ENERGY_BASED);
            utils::Logger::info("VAD initialized with energy-based detection only");
        }
        
        // Reset state
        currentState_ = VadState::IDLE;
        currentUtteranceId_ = 0;
        stateChangeTime_ = std::chrono::steady_clock::now();
        lastAudioTime_ = stateChangeTime_;
        
        initialized_ = true;
        setError(ErrorCode::NONE);
        
        utils::Logger::info("VoiceActivityDetector initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("VoiceActivityDetector initialization failed: " + std::string(e.what()));
        setError(ErrorCode::MODEL_LOAD_ERROR);
        return false;
    }
}

void VoiceActivityDetector::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Finalize any ongoing utterance
    if (currentState_ == VadState::SPEAKING) {
        finalizeUtterance();
    }
    
    sileroVad_->shutdown();
    initialized_ = false;
    
    utils::Logger::info("VoiceActivityDetector shutdown complete");
}

void VoiceActivityDetector::setConfig(const VadConfig& config) {
    if (!config.isValid()) {
        setError(ErrorCode::INVALID_CONFIG);
        throw std::invalid_argument("Invalid VAD configuration");
    }
    
    config_ = config;
    
    // If sample rate changed, reinitialize silero-vad
    if (initialized_ && sileroVad_) {
        std::string modelPath = "";
#ifdef SILERO_MODEL_PATH
        modelPath = SILERO_MODEL_PATH;
#endif
        sileroVad_->shutdown();
        sileroVad_->initialize(config_.sampleRate, modelPath);
    }
    
    // Update energy-based VAD configuration
    if (energyBasedVad_) {
        EnergyBasedVAD::Config energyConfig;
        energyConfig.energyThreshold = config_.speechThreshold * 0.5f;
        energyConfig.useAdaptiveThreshold = true;
        energyConfig.useSpectralFeatures = true;
        energyBasedVad_->configure(energyConfig);
    }
    
    utils::Logger::info("VoiceActivityDetector configuration updated");
}

void VoiceActivityDetector::setVadCallback(VadCallback callback) {
    vadCallback_ = callback;
}

void VoiceActivityDetector::setUtteranceCallback(UtteranceCallback callback) {
    utteranceCallback_ = callback;
}

void VoiceActivityDetector::processAudio(const std::vector<float>& audioData) {
    processAudioChunk(audioData, std::chrono::steady_clock::now());
}

void VoiceActivityDetector::processAudioChunk(const std::vector<float>& audioData, 
                                            std::chrono::steady_clock::time_point timestamp) {
    if (!initialized_) {
        setError(ErrorCode::NOT_INITIALIZED);
        return;
    }
    
    if (audioData.empty()) {
        return;
    }
    
    try {
        lastAudioTime_ = timestamp;
        
        // Analyze speech probability using silero-vad
        float speechProbability = analyzeSpeechProbability(audioData);
        
        // Update statistics
        updateStatistics(speechProbability > config_.speechThreshold, speechProbability);
        
        // Store audio data for current utterance if we're in speech state
        if (currentState_ == VadState::SPEAKING || currentState_ == VadState::SPEECH_DETECTED) {
            std::lock_guard<std::mutex> lock(audioMutex_);
            currentUtteranceAudio_.insert(currentUtteranceAudio_.end(), 
                                        audioData.begin(), audioData.end());
        }
        
        // Process state transitions based on speech probability
        VadState newState = currentState_;
        
        switch (currentState_) {
            case VadState::IDLE:
                if (speechProbability > config_.speechThreshold) {
                    newState = VadState::SPEECH_DETECTED;
                }
                break;
                
            case VadState::SPEECH_DETECTED:
                if (speechProbability > config_.speechThreshold) {
                    if (shouldTransitionToSpeaking()) {
                        newState = VadState::SPEAKING;
                    }
                } else if (speechProbability < config_.silenceThreshold) {
                    newState = VadState::IDLE;
                }
                break;
                
            case VadState::SPEAKING:
                if (getTimeSinceUtteranceStart().count() > config_.maxUtteranceDurationMs) {
                    // Force utterance end if too long - go directly to IDLE to finalize
                    newState = VadState::IDLE;
                } else if (speechProbability < config_.silenceThreshold) {
                    newState = VadState::PAUSE_DETECTED;
                }
                break;
                
            case VadState::PAUSE_DETECTED:
                if (getTimeSinceUtteranceStart().count() > config_.maxUtteranceDurationMs) {
                    // Force utterance end if too long
                    newState = VadState::IDLE;
                } else if (speechProbability > config_.speechThreshold) {
                    newState = VadState::SPEAKING;
                } else if (shouldTransitionToIdle()) {
                    newState = VadState::IDLE;
                }
                break;
        }
        
        // For continuous audio processing, check if we should transition to SPEAKING
        // only after we've accumulated enough speech duration
        if (currentState_ == VadState::SPEECH_DETECTED && newState == VadState::SPEECH_DETECTED) {
            if (shouldTransitionToSpeaking()) {
                newState = VadState::SPEAKING;
            }
        }
        
        // Apply state transition if needed
        if (newState != currentState_) {
            processStateTransition(newState, speechProbability);
        }
        
        setError(ErrorCode::NONE);
        
    } catch (const std::exception& e) {
        utils::Logger::error("VoiceActivityDetector processing error: " + std::string(e.what()));
        setError(ErrorCode::PROCESSING_ERROR);
    }
}

std::vector<float> VoiceActivityDetector::getCurrentUtteranceAudio() const {
    std::lock_guard<std::mutex> lock(audioMutex_);
    return currentUtteranceAudio_;
}

void VoiceActivityDetector::forceUtteranceEnd() {
    if (currentState_ == VadState::SPEAKING || currentState_ == VadState::PAUSE_DETECTED) {
        processStateTransition(VadState::IDLE, 0.0f);
    }
}

void VoiceActivityDetector::reset() {
    if (currentState_ == VadState::SPEAKING) {
        finalizeUtterance();
    }
    
    transitionToState(VadState::IDLE, 0.0f);
    
    std::lock_guard<std::mutex> lock(audioMutex_);
    currentUtteranceAudio_.clear();
    
    utils::Logger::info("VoiceActivityDetector reset to IDLE state");
}

VoiceActivityDetector::Statistics VoiceActivityDetector::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    Statistics result = stats_;
    if (stats_.totalUtterances > 0) {
        result.averageUtteranceDuration = static_cast<double>(stats_.totalSpeechTime) / stats_.totalUtterances;
    }
    
    return result;
}

void VoiceActivityDetector::resetStatistics() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    stats_ = {};
    stats_.lastActivity = std::chrono::steady_clock::now();
    
    utils::Logger::info("VoiceActivityDetector statistics reset");
}

std::string VoiceActivityDetector::getErrorMessage() const {
    switch (lastError_) {
        case ErrorCode::NONE:
            return "No error";
        case ErrorCode::NOT_INITIALIZED:
            return "VAD not initialized";
        case ErrorCode::INVALID_CONFIG:
            return "Invalid VAD configuration";
        case ErrorCode::PROCESSING_ERROR:
            return "Audio processing error";
        case ErrorCode::MODEL_LOAD_ERROR:
            return "Failed to load VAD model";
        default:
            return "Unknown error";
    }
}

bool VoiceActivityDetector::isSileroModelLoaded() const {
    return sileroVad_ && sileroVad_->isSileroModelLoaded();
}

void VoiceActivityDetector::setVadMode(int mode) {
    if (!sileroVad_) {
        return;
    }
    
    SileroVadImpl::VadMode vadMode;
    switch (mode) {
        case 0:
            vadMode = SileroVadImpl::VadMode::SILERO;
            break;
        case 1:
            vadMode = SileroVadImpl::VadMode::ENERGY_BASED;
            break;
        case 2:
        default:
            vadMode = SileroVadImpl::VadMode::HYBRID;
            break;
    }
    
    sileroVad_->setVadMode(vadMode);
    utils::Logger::info("VAD mode set to: " + std::to_string(mode));
}

int VoiceActivityDetector::getCurrentVadMode() const {
    if (!sileroVad_) {
        return 1; // Energy-based fallback
    }
    
    switch (sileroVad_->getCurrentMode()) {
        case SileroVadImpl::VadMode::SILERO:
            return 0;
        case SileroVadImpl::VadMode::ENERGY_BASED:
            return 1;
        case SileroVadImpl::VadMode::HYBRID:
        default:
            return 2;
    }
}

// Private methods
void VoiceActivityDetector::transitionToState(VadState newState, float confidence) {
    VadState previousState = currentState_;
    currentState_ = newState;
    stateChangeTime_ = std::chrono::steady_clock::now();
    
    // Handle state-specific actions
    switch (newState) {
        case VadState::SPEECH_DETECTED:
            // Start collecting audio for potential utterance
            {
                std::lock_guard<std::mutex> lock(audioMutex_);
                currentUtteranceAudio_.clear();
            }
            break;
            
        case VadState::SPEAKING:
            // Start new utterance only if we don't already have one active
            if (currentUtteranceId_ == 0) {
                currentUtteranceId_ = nextUtteranceId_++;
                utteranceStartTime_ = stateChangeTime_;
                utils::Logger::debug("Started utterance " + std::to_string(currentUtteranceId_));
            } else {
                utils::Logger::debug("Continuing utterance " + std::to_string(currentUtteranceId_));
            }
            break;
            
        case VadState::IDLE:
            // End utterance if we were speaking
            if (previousState == VadState::SPEAKING || previousState == VadState::PAUSE_DETECTED) {
                finalizeUtterance();
            }
            break;
            
        case VadState::PAUSE_DETECTED:
            // Continue collecting audio but prepare for potential utterance end
            break;
    }
    
    // Notify callback if registered
    if (vadCallback_) {
        VadEvent event(previousState, newState, confidence, currentUtteranceId_);
        vadCallback_(event);
    }
    
    utils::Logger::debug("VAD state transition: " + std::to_string(static_cast<int>(previousState)) + 
                        " -> " + std::to_string(static_cast<int>(newState)) + 
                        " (confidence: " + std::to_string(confidence) + ")");
}

void VoiceActivityDetector::processStateTransition(VadState newState, float confidence) {
    transitionToState(newState, confidence);
}

void VoiceActivityDetector::handleSpeechDetected(float confidence) {
    transitionToState(VadState::SPEECH_DETECTED, confidence);
}

void VoiceActivityDetector::handleSpeechEnded(float confidence) {
    transitionToState(VadState::IDLE, confidence);
}

void VoiceActivityDetector::finalizeUtterance() {
    if (currentUtteranceId_ == 0) {
        return; // No active utterance
    }
    
    std::vector<float> utteranceAudio;
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        utteranceAudio = std::move(currentUtteranceAudio_);
        currentUtteranceAudio_.clear();
    }
    
    // Update statistics
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.totalUtterances++;
        
        auto utteranceDuration = getTimeSinceUtteranceStart();
        stats_.totalSpeechTime += utteranceDuration.count();
    }
    
    // Notify utterance callback if registered
    if (utteranceCallback_ && !utteranceAudio.empty()) {
        utteranceCallback_(currentUtteranceId_, utteranceAudio);
    }
    
    utils::Logger::info("Finalized utterance " + std::to_string(currentUtteranceId_) + 
                       " with " + std::to_string(utteranceAudio.size()) + " samples");
    
    currentUtteranceId_ = 0;
}

void VoiceActivityDetector::updateStatistics(bool isSpeech, float confidence) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    stats_.totalAudioProcessed++;
    stats_.lastActivity = std::chrono::steady_clock::now();
    
    // Update confidence average
    if (stats_.totalAudioProcessed == 1) {
        stats_.averageConfidence = confidence;
    } else {
        stats_.averageConfidence = (stats_.averageConfidence * (stats_.totalAudioProcessed - 1) + confidence) / 
                                  stats_.totalAudioProcessed;
    }
    
    // Update speech/silence time (approximate based on window size)
    uint32_t windowDurationMs = config_.windowSizeMs;
    if (isSpeech) {
        stats_.totalSpeechTime += windowDurationMs;
    } else {
        stats_.totalSilenceTime += windowDurationMs;
    }
}

void VoiceActivityDetector::setError(ErrorCode error) {
    lastError_ = error;
    
    if (error != ErrorCode::NONE) {
        utils::Logger::warn("VoiceActivityDetector error: " + getErrorMessage());
    }
}

float VoiceActivityDetector::analyzeSpeechProbability(const std::vector<float>& audioData) {
    if (!sileroVad_ || !sileroVad_->isInitialized()) {
        return 0.0f;
    }
    
    return sileroVad_->processSamples(audioData);
}

bool VoiceActivityDetector::shouldTransitionToSpeaking() const {
    auto timeSinceStateChange = getTimeSinceStateChange();
    // Be more strict about minimum speech duration - only small tolerance for processing delays
    return timeSinceStateChange.count() >= (config_.minSpeechDurationMs - 10);
}

bool VoiceActivityDetector::shouldTransitionToPause() const {
    auto timeSinceStateChange = getTimeSinceStateChange();
    return timeSinceStateChange.count() >= config_.minSilenceDurationMs;
}

bool VoiceActivityDetector::shouldTransitionToIdle() const {
    auto timeSinceStateChange = getTimeSinceStateChange();
    // Add some tolerance for processing delays
    return timeSinceStateChange.count() >= (config_.minSilenceDurationMs - 50);
}

uint32_t VoiceActivityDetector::getWindowSizeSamples() const {
    return (config_.windowSizeMs * config_.sampleRate) / 1000;
}

std::chrono::milliseconds VoiceActivityDetector::getTimeSinceStateChange() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - stateChangeTime_);
}

std::chrono::milliseconds VoiceActivityDetector::getTimeSinceUtteranceStart() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - utteranceStartTime_);
}

} // namespace audio