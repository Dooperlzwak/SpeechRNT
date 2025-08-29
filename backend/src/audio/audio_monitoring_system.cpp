#include "audio/audio_monitoring_system.hpp"
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace audio {

// MetricsFilter implementation
MetricsFilter::MetricsFilter(const FilterConfig& config) 
    : config_(config), filterState_(0.0f) {
    reset();
}

bool MetricsFilter::shouldPassMetrics(const RealTimeMetrics& current, const RealTimeMetrics& previous) {
    switch (config_.type) {
        case FilterType::NONE:
            return true;
        case FilterType::LOW_PASS:
            return applyLowPassFilter(current, previous);
        case FilterType::THRESHOLD:
            return applyThresholdFilter(current, previous);
        case FilterType::RATE_LIMIT:
            return applyRateLimitFilter();
        case FilterType::CHANGE_DETECTION:
            return applyChangeDetectionFilter(current, previous);
        default:
            return true;
    }
}

RealTimeMetrics MetricsFilter::filterMetrics(const RealTimeMetrics& metrics) {
    if (config_.type == FilterType::LOW_PASS) {
        // Apply low-pass filtering to smooth metrics
        RealTimeMetrics filtered = metrics;
        float alpha = config_.parameter1; // Smoothing factor
        
        // Smooth level metrics
        filtered.levels.currentLevel = alpha * lastPassedMetrics_.levels.currentLevel + 
                                      (1.0f - alpha) * metrics.levels.currentLevel;
        filtered.levels.peakLevel = std::max(filtered.levels.peakLevel, lastPassedMetrics_.levels.peakLevel * 0.95f);
        filtered.levels.averageLevel = alpha * lastPassedMetrics_.levels.averageLevel + 
                                      (1.0f - alpha) * metrics.levels.averageLevel;
        
        // Smooth spectral metrics
        filtered.spectral.spectralCentroid = alpha * lastPassedMetrics_.spectral.spectralCentroid + 
                                            (1.0f - alpha) * metrics.spectral.spectralCentroid;
        filtered.spectral.spectralBandwidth = alpha * lastPassedMetrics_.spectral.spectralBandwidth + 
                                             (1.0f - alpha) * metrics.spectral.spectralBandwidth;
        
        return filtered;
    }
    
    return metrics;
}

void MetricsFilter::updateConfig(const FilterConfig& config) {
    config_ = config;
    reset();
}

void MetricsFilter::reset() {
    lastPassedMetrics_ = RealTimeMetrics{};
    lastPassTime_ = std::chrono::steady_clock::now();
    filterState_ = 0.0f;
}

bool MetricsFilter::applyLowPassFilter(const RealTimeMetrics& current, const RealTimeMetrics& previous) {
    // Always pass for low-pass filter, but the filtering happens in filterMetrics()
    return true;
}

bool MetricsFilter::applyThresholdFilter(const RealTimeMetrics& current, const RealTimeMetrics& previous) {
    float distance = calculateMetricsDistance(current, previous);
    return distance >= config_.parameter1;
}

bool MetricsFilter::applyRateLimitFilter() {
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastPass = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPassTime_);
    
    if (timeSinceLastPass.count() >= config_.parameter1) {
        lastPassTime_ = now;
        return true;
    }
    return false;
}

bool MetricsFilter::applyChangeDetectionFilter(const RealTimeMetrics& current, const RealTimeMetrics& previous) {
    float distance = calculateMetricsDistance(current, previous);
    float threshold = config_.parameter1;
    
    // Use hysteresis to prevent oscillation
    if (filterState_ == 0.0f) {
        // Currently not passing, need higher threshold to start passing
        if (distance >= threshold) {
            filterState_ = 1.0f;
            return true;
        }
    } else {
        // Currently passing, need lower threshold to stop passing
        if (distance >= threshold * 0.7f) {
            return true;
        } else {
            filterState_ = 0.0f;
        }
    }
    
    return false;
}

float MetricsFilter::calculateMetricsDistance(const RealTimeMetrics& a, const RealTimeMetrics& b) {
    // Calculate normalized distance between metrics
    float levelDist = std::abs(a.levels.currentLevel - b.levels.currentLevel);
    float spectralDist = std::abs(a.spectral.spectralCentroid - b.spectral.spectralCentroid) / 4000.0f; // Normalize by typical range
    float noiseDist = std::abs(a.noiseLevel - b.noiseLevel) / 60.0f; // Normalize by dB range
    float speechDist = std::abs(a.speechProbability - b.speechProbability);
    
    return std::sqrt(levelDist * levelDist + spectralDist * spectralDist + 
                    noiseDist * noiseDist + speechDist * speechDist);
}

// MetricsAggregator implementation
MetricsAggregator::MetricsAggregator(const AggregationConfig& config) 
    : config_(config) {
    reset();
}

void MetricsAggregator::addMetrics(const RealTimeMetrics& metrics) {
    std::lock_guard<std::mutex> lock(historyMutex_);
    
    metricsHistory_.push_back(metrics);
    
    // Clean up old metrics
    cleanupOldMetrics();
    
    // Update aggregation
    updateAggregation();
}

MetricsAggregator::AggregatedMetrics MetricsAggregator::getAggregatedMetrics() const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    return aggregatedMetrics_;
}

void MetricsAggregator::updateConfig(const AggregationConfig& config) {
    std::lock_guard<std::mutex> lock(historyMutex_);
    config_ = config;
    updateAggregation();
}

void MetricsAggregator::reset() {
    std::lock_guard<std::mutex> lock(historyMutex_);
    metricsHistory_.clear();
    aggregatedMetrics_ = AggregatedMetrics{};
}

std::vector<RealTimeMetrics> MetricsAggregator::getHistory(size_t count) const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    
    size_t startIndex = metricsHistory_.size() > count ? metricsHistory_.size() - count : 0;
    return std::vector<RealTimeMetrics>(metricsHistory_.begin() + startIndex, metricsHistory_.end());
}

std::vector<RealTimeMetrics> MetricsAggregator::getHistoryInTimeRange(std::chrono::milliseconds timeRange) const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    
    if (metricsHistory_.empty()) {
        return {};
    }
    
    auto cutoffTime = metricsHistory_.back().timestampMs - timeRange.count();
    std::vector<RealTimeMetrics> result;
    
    for (const auto& metrics : metricsHistory_) {
        if (metrics.timestampMs >= cutoffTime) {
            result.push_back(metrics);
        }
    }
    
    return result;
}

void MetricsAggregator::updateAggregation() {
    if (metricsHistory_.empty()) {
        return;
    }
    
    aggregatedMetrics_.current = metricsHistory_.back();
    aggregatedMetrics_.sampleCount = metricsHistory_.size();
    
    if (metricsHistory_.size() > 1) {
        auto timeSpan = metricsHistory_.back().timestampMs - metricsHistory_.front().timestampMs;
        aggregatedMetrics_.timeSpan = std::chrono::milliseconds(timeSpan);
    }
    
    switch (config_.type) {
        case AggregationType::AVERAGE:
            aggregatedMetrics_.average = calculateAverage(metricsHistory_);
            break;
        case AggregationType::MIN_MAX:
            aggregatedMetrics_.minimum = calculateMinimum(metricsHistory_);
            aggregatedMetrics_.maximum = calculateMaximum(metricsHistory_);
            break;
        case AggregationType::PEAK_HOLD:
            aggregatedMetrics_.maximum = calculateMaximum(metricsHistory_);
            // Apply decay to peak hold
            if (!metricsHistory_.empty()) {
                auto& peak = aggregatedMetrics_.maximum;
                peak.levels.peakLevel *= config_.decayRate;
                peak.spectral.dominantFrequency *= config_.decayRate;
            }
            break;
        case AggregationType::TREND_ANALYSIS:
            aggregatedMetrics_.trend = calculateTrend(metricsHistory_);
            aggregatedMetrics_.stability = calculateStability(metricsHistory_);
            break;
        default:
            break;
    }
}

void MetricsAggregator::cleanupOldMetrics() {
    if (config_.timeWindow.count() <= 0) {
        return;
    }
    
    if (metricsHistory_.empty()) {
        return;
    }
    
    auto cutoffTime = metricsHistory_.back().timestampMs - config_.timeWindow.count();
    
    while (!metricsHistory_.empty() && metricsHistory_.front().timestampMs < cutoffTime) {
        metricsHistory_.pop_front();
    }
    
    // Also limit by sample count
    while (metricsHistory_.size() > config_.sampleCount) {
        metricsHistory_.pop_front();
    }
}

RealTimeMetrics MetricsAggregator::calculateAverage(const std::deque<RealTimeMetrics>& metrics) const {
    if (metrics.empty()) {
        return RealTimeMetrics{};
    }
    
    RealTimeMetrics average{};
    
    for (const auto& m : metrics) {
        average.levels.currentLevel += m.levels.currentLevel;
        average.levels.peakLevel += m.levels.peakLevel;
        average.levels.averageLevel += m.levels.averageLevel;
        average.spectral.spectralCentroid += m.spectral.spectralCentroid;
        average.spectral.spectralBandwidth += m.spectral.spectralBandwidth;
        average.spectral.spectralRolloff += m.spectral.spectralRolloff;
        average.noiseLevel += m.noiseLevel;
        average.speechProbability += m.speechProbability;
        average.voiceActivityScore += m.voiceActivityScore;
    }
    
    float count = static_cast<float>(metrics.size());
    average.levels.currentLevel /= count;
    average.levels.peakLevel /= count;
    average.levels.averageLevel /= count;
    average.spectral.spectralCentroid /= count;
    average.spectral.spectralBandwidth /= count;
    average.spectral.spectralRolloff /= count;
    average.noiseLevel /= count;
    average.speechProbability /= count;
    average.voiceActivityScore /= count;
    
    return average;
}

RealTimeMetrics MetricsAggregator::calculateMinimum(const std::deque<RealTimeMetrics>& metrics) const {
    if (metrics.empty()) {
        return RealTimeMetrics{};
    }
    
    RealTimeMetrics minimum = metrics.front();
    
    for (const auto& m : metrics) {
        minimum.levels.currentLevel = std::min(minimum.levels.currentLevel, m.levels.currentLevel);
        minimum.levels.peakLevel = std::min(minimum.levels.peakLevel, m.levels.peakLevel);
        minimum.levels.averageLevel = std::min(minimum.levels.averageLevel, m.levels.averageLevel);
        minimum.spectral.spectralCentroid = std::min(minimum.spectral.spectralCentroid, m.spectral.spectralCentroid);
        minimum.spectral.spectralBandwidth = std::min(minimum.spectral.spectralBandwidth, m.spectral.spectralBandwidth);
        minimum.noiseLevel = std::min(minimum.noiseLevel, m.noiseLevel);
        minimum.speechProbability = std::min(minimum.speechProbability, m.speechProbability);
    }
    
    return minimum;
}

RealTimeMetrics MetricsAggregator::calculateMaximum(const std::deque<RealTimeMetrics>& metrics) const {
    if (metrics.empty()) {
        return RealTimeMetrics{};
    }
    
    RealTimeMetrics maximum = metrics.front();
    
    for (const auto& m : metrics) {
        maximum.levels.currentLevel = std::max(maximum.levels.currentLevel, m.levels.currentLevel);
        maximum.levels.peakLevel = std::max(maximum.levels.peakLevel, m.levels.peakLevel);
        maximum.levels.averageLevel = std::max(maximum.levels.averageLevel, m.levels.averageLevel);
        maximum.spectral.spectralCentroid = std::max(maximum.spectral.spectralCentroid, m.spectral.spectralCentroid);
        maximum.spectral.spectralBandwidth = std::max(maximum.spectral.spectralBandwidth, m.spectral.spectralBandwidth);
        maximum.noiseLevel = std::max(maximum.noiseLevel, m.noiseLevel);
        maximum.speechProbability = std::max(maximum.speechProbability, m.speechProbability);
    }
    
    return maximum;
}

RealTimeMetrics MetricsAggregator::calculateTrend(const std::deque<RealTimeMetrics>& metrics) const {
    if (metrics.size() < 2) {
        return RealTimeMetrics{};
    }
    
    // Simple linear trend calculation
    RealTimeMetrics trend{};
    const auto& first = metrics.front();
    const auto& last = metrics.back();
    
    float timeDiff = static_cast<float>(last.timestampMs - first.timestampMs);
    if (timeDiff > 0) {
        trend.levels.currentLevel = (last.levels.currentLevel - first.levels.currentLevel) / timeDiff * 1000.0f; // per second
        trend.spectral.spectralCentroid = (last.spectral.spectralCentroid - first.spectral.spectralCentroid) / timeDiff * 1000.0f;
        trend.noiseLevel = (last.noiseLevel - first.noiseLevel) / timeDiff * 1000.0f;
        trend.speechProbability = (last.speechProbability - first.speechProbability) / timeDiff * 1000.0f;
    }
    
    return trend;
}

float MetricsAggregator::calculateStability(const std::deque<RealTimeMetrics>& metrics) const {
    if (metrics.size() < 2) {
        return 1.0f;
    }
    
    // Calculate coefficient of variation for key metrics
    auto average = calculateAverage(metrics);
    
    float levelVariance = 0.0f;
    float spectralVariance = 0.0f;
    
    for (const auto& m : metrics) {
        float levelDiff = m.levels.currentLevel - average.levels.currentLevel;
        float spectralDiff = m.spectral.spectralCentroid - average.spectral.spectralCentroid;
        levelVariance += levelDiff * levelDiff;
        spectralVariance += spectralDiff * spectralDiff;
    }
    
    levelVariance /= metrics.size();
    spectralVariance /= metrics.size();
    
    float levelCV = average.levels.currentLevel > 0 ? std::sqrt(levelVariance) / average.levels.currentLevel : 0.0f;
    float spectralCV = average.spectral.spectralCentroid > 0 ? std::sqrt(spectralVariance) / average.spectral.spectralCentroid : 0.0f;
    
    // Convert to stability score (lower CV = higher stability)
    float stability = 1.0f / (1.0f + levelCV + spectralCV * 0.001f); // Scale spectral CV
    return std::clamp(stability, 0.0f, 1.0f);
}

// CallbackSubscription implementation
CallbackSubscription::CallbackSubscription(const std::string& id, const CallbackConfig& config)
    : id_(id), config_(config), active_(true) {
    lastCallbackTime_ = std::chrono::steady_clock::now();
    resetStats();
    
    // Initialize filter if enabled
    if (config_.enableFiltering) {
        MetricsFilter::FilterConfig filterConfig(MetricsFilter::FilterType::THRESHOLD, config_.minChangeThreshold);
        filter_ = std::make_unique<MetricsFilter>(filterConfig);
    }
    
    // Initialize aggregator if enabled
    if (config_.enableAggregation) {
        MetricsAggregator::AggregationConfig aggConfig(MetricsAggregator::AggregationType::AVERAGE, config_.updateInterval);
        aggregator_ = std::make_unique<MetricsAggregator>(aggConfig);
    }
}

void CallbackSubscription::setMetricsCallback(MetricsCallback callback) {
    metricsCallback_ = callback;
}

void CallbackSubscription::setAggregatedMetricsCallback(AggregatedMetricsCallback callback) {
    aggregatedMetricsCallback_ = callback;
}

void CallbackSubscription::setLevelsCallback(LevelsCallback callback) {
    levelsCallback_ = callback;
}

void CallbackSubscription::setSpectralCallback(SpectralCallback callback) {
    spectralCallback_ = callback;
}

void CallbackSubscription::updateConfig(const CallbackConfig& config) {
    config_ = config;
    
    // Update filter if needed
    if (config_.enableFiltering && !filter_) {
        MetricsFilter::FilterConfig filterConfig(MetricsFilter::FilterType::THRESHOLD, config_.minChangeThreshold);
        filter_ = std::make_unique<MetricsFilter>(filterConfig);
    } else if (!config_.enableFiltering) {
        filter_.reset();
    }
    
    // Update aggregator if needed
    if (config_.enableAggregation && !aggregator_) {
        MetricsAggregator::AggregationConfig aggConfig(MetricsAggregator::AggregationType::AVERAGE, config_.updateInterval);
        aggregator_ = std::make_unique<MetricsAggregator>(aggConfig);
    } else if (!config_.enableAggregation) {
        aggregator_.reset();
    }
}

void CallbackSubscription::setFilter(const MetricsFilter::FilterConfig& filterConfig) {
    if (!filter_) {
        filter_ = std::make_unique<MetricsFilter>(filterConfig);
    } else {
        filter_->updateConfig(filterConfig);
    }
}

void CallbackSubscription::setAggregator(const MetricsAggregator::AggregationConfig& aggregationConfig) {
    if (!aggregator_) {
        aggregator_ = std::make_unique<MetricsAggregator>(aggregationConfig);
    } else {
        aggregator_->updateConfig(aggregationConfig);
    }
}

bool CallbackSubscription::processMetrics(const RealTimeMetrics& metrics) {
    if (!active_ || !shouldTriggerCallback(metrics)) {
        updateStats(false);
        return false;
    }
    
    // Apply filtering if enabled
    RealTimeMetrics processedMetrics = metrics;
    if (filter_) {
        if (!filter_->shouldPassMetrics(metrics, lastMetrics_)) {
            updateStats(false);
            return false;
        }
        processedMetrics = filter_->filterMetrics(metrics);
    }
    
    // Add to aggregator if enabled
    if (aggregator_) {
        aggregator_->addMetrics(processedMetrics);
        
        if (aggregatedMetricsCallback_) {
            try {
                aggregatedMetricsCallback_(aggregator_->getAggregatedMetrics());
            } catch (...) {
                // Ignore callback exceptions
            }
        }
    }
    
    // Call metrics callback
    if (metricsCallback_) {
        try {
            metricsCallback_(processedMetrics);
        } catch (...) {
            // Ignore callback exceptions
        }
    }
    
    lastMetrics_ = processedMetrics;
    lastCallbackTime_ = std::chrono::steady_clock::now();
    updateStats(true);
    
    return true;
}

void CallbackSubscription::processLevels(const AudioLevelMetrics& levels) {
    if (!active_ || !levelsCallback_) {
        return;
    }
    
    try {
        levelsCallback_(levels);
    } catch (...) {
        // Ignore callback exceptions
    }
}

void CallbackSubscription::processSpectral(const SpectralAnalysis& spectral) {
    if (!active_ || !spectralCallback_) {
        return;
    }
    
    try {
        spectralCallback_(spectral);
    } catch (...) {
        // Ignore callback exceptions
    }
}

bool CallbackSubscription::shouldTriggerCallback(const RealTimeMetrics& metrics) {
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastCallback = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCallbackTime_);
    
    return timeSinceLastCallback >= config_.updateInterval;
}

void CallbackSubscription::updateStats(bool callbackTriggered) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    if (callbackTriggered) {
        stats_.totalCallbacks++;
        stats_.lastCallback = std::chrono::steady_clock::now();
        
        // Update average callback interval
        if (stats_.totalCallbacks > 1) {
            auto interval = std::chrono::duration_cast<std::chrono::milliseconds>(
                stats_.lastCallback - lastCallbackTime_).count();
            stats_.averageCallbackInterval = 0.9f * stats_.averageCallbackInterval + 0.1f * interval;
        }
    } else {
        stats_.filteredCallbacks++;
    }
}

CallbackSubscription::SubscriptionStats CallbackSubscription::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void CallbackSubscription::resetStats() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = SubscriptionStats{};
    stats_.averageCallbackInterval = config_.updateInterval.count();
}

// AudioMonitoringSystem implementation
AudioMonitoringSystem::AudioMonitoringSystem(std::shared_ptr<RealTimeAudioAnalyzer> analyzer)
    : analyzer_(analyzer), running_(false), debugMode_(false),
      globalUpdateInterval_(std::chrono::milliseconds(50)), maxSubscriptions_(100), threadPoolSize_(2),
      nextSubscriptionId_(1) {
    
    // Initialize global aggregator
    MetricsAggregator::AggregationConfig globalConfig(MetricsAggregator::AggregationType::AVERAGE, 
                                                     std::chrono::milliseconds(1000));
    globalAggregator_ = std::make_unique<MetricsAggregator>(globalConfig);
    
    // Initialize performance tracking
    performance_ = SystemPerformance{};
    lastPerformanceUpdate_ = std::chrono::steady_clock::now();
}

AudioMonitoringSystem::~AudioMonitoringSystem() {
    shutdown();
}

bool AudioMonitoringSystem::initialize() {
    if (running_) {
        return true;
    }
    
    if (!analyzer_ || !analyzer_->isInitialized()) {
        return false;
    }
    
    try {
        // Initialize thread pool
        initializeThreadPool();
        
        // Register with analyzer for callbacks
        analyzer_->registerMetricsCallback([this](const RealTimeMetrics& metrics) {
            this->processMetricsForSubscriptions(metrics);
        });
        
        analyzer_->registerLevelsCallback([this](const AudioLevelMetrics& levels) {
            this->processLevelsForSubscriptions(levels);
        });
        
        analyzer_->registerSpectralCallback([this](const SpectralAnalysis& spectral) {
            this->processSpectralForSubscriptions(spectral);
        });
        
        running_ = true;
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

void AudioMonitoringSystem::shutdown() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // Clear analyzer callbacks
    if (analyzer_) {
        analyzer_->clearCallbacks();
    }
    
    // Shutdown thread pool
    shutdownThreadPool();
    
    // Clear all subscriptions
    unsubscribeAll();
}

std::string AudioMonitoringSystem::subscribe(const CallbackConfig& config, 
                                           CallbackSubscription::MetricsCallback callback) {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    
    if (subscriptions_.size() >= maxSubscriptions_) {
        return ""; // Max subscriptions reached
    }
    
    std::string id = generateSubscriptionId();
    auto subscription = std::make_unique<CallbackSubscription>(id, config);
    subscription->setMetricsCallback(callback);
    
    subscriptions_[id] = std::move(subscription);
    
    if (debugMode_) {
        logDebugInfo("Created metrics subscription: " + id);
    }
    
    return id;
}

std::string AudioMonitoringSystem::subscribeAggregated(const CallbackConfig& config,
                                                     CallbackSubscription::AggregatedMetricsCallback callback) {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    
    if (subscriptions_.size() >= maxSubscriptions_) {
        return ""; // Max subscriptions reached
    }
    
    std::string id = generateSubscriptionId();
    CallbackConfig aggConfig = config;
    aggConfig.enableAggregation = true; // Force aggregation for this type
    
    auto subscription = std::make_unique<CallbackSubscription>(id, aggConfig);
    subscription->setAggregatedMetricsCallback(callback);
    
    subscriptions_[id] = std::move(subscription);
    
    if (debugMode_) {
        logDebugInfo("Created aggregated metrics subscription: " + id);
    }
    
    return id;
}

std::string AudioMonitoringSystem::subscribeLevels(const CallbackConfig& config,
                                                  CallbackSubscription::LevelsCallback callback) {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    
    if (subscriptions_.size() >= maxSubscriptions_) {
        return ""; // Max subscriptions reached
    }
    
    std::string id = generateSubscriptionId();
    auto subscription = std::make_unique<CallbackSubscription>(id, config);
    subscription->setLevelsCallback(callback);
    
    subscriptions_[id] = std::move(subscription);
    
    if (debugMode_) {
        logDebugInfo("Created levels subscription: " + id);
    }
    
    return id;
}

std::string AudioMonitoringSystem::subscribeSpectral(const CallbackConfig& config,
                                                   CallbackSubscription::SpectralCallback callback) {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    
    if (subscriptions_.size() >= maxSubscriptions_) {
        return ""; // Max subscriptions reached
    }
    
    std::string id = generateSubscriptionId();
    auto subscription = std::make_unique<CallbackSubscription>(id, config);
    subscription->setSpectralCallback(callback);
    
    subscriptions_[id] = std::move(subscription);
    
    if (debugMode_) {
        logDebugInfo("Created spectral subscription: " + id);
    }
    
    return id;
}

bool AudioMonitoringSystem::unsubscribe(const std::string& subscriptionId) {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    
    auto it = subscriptions_.find(subscriptionId);
    if (it != subscriptions_.end()) {
        subscriptions_.erase(it);
        
        if (debugMode_) {
            logDebugInfo("Removed subscription: " + subscriptionId);
        }
        
        return true;
    }
    
    return false;
}

void AudioMonitoringSystem::unsubscribeAll() {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    subscriptions_.clear();
    
    if (debugMode_) {
        logDebugInfo("Removed all subscriptions");
    }
}

void AudioMonitoringSystem::processMetricsForSubscriptions(const RealTimeMetrics& metrics) {
    if (!running_) {
        return;
    }
    
    // Update global metrics
    updateGlobalMetrics(metrics);
    
    // Process for all subscriptions
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    
    for (auto& [id, subscription] : subscriptions_) {
        if (subscription && subscription->isActive()) {
            enqueueTask([subscription = subscription.get(), metrics]() {
                subscription->processMetrics(metrics);
            });
        }
    }
    
    // Update performance metrics
    updatePerformanceMetrics();
}

void AudioMonitoringSystem::processLevelsForSubscriptions(const AudioLevelMetrics& levels) {
    if (!running_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    
    for (auto& [id, subscription] : subscriptions_) {
        if (subscription && subscription->isActive()) {
            enqueueTask([subscription = subscription.get(), levels]() {
                subscription->processLevels(levels);
            });
        }
    }
}

void AudioMonitoringSystem::processSpectralForSubscriptions(const SpectralAnalysis& spectral) {
    if (!running_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    
    for (auto& [id, subscription] : subscriptions_) {
        if (subscription && subscription->isActive()) {
            enqueueTask([subscription = subscription.get(), spectral]() {
                subscription->processSpectral(spectral);
            });
        }
    }
}

void AudioMonitoringSystem::initializeThreadPool() {
    processingThreads_.clear();
    
    for (size_t i = 0; i < threadPoolSize_; ++i) {
        processingThreads_.emplace_back(std::make_unique<std::thread>(&AudioMonitoringSystem::workerThread, this));
    }
}

void AudioMonitoringSystem::shutdownThreadPool() {
    // Signal shutdown
    {
        std::lock_guard<std::mutex> lock(taskQueueMutex_);
        // Add shutdown tasks
        for (size_t i = 0; i < threadPoolSize_; ++i) {
            taskQueue_.push([]() {}); // Empty task to wake up threads
        }
    }
    taskCondition_.notify_all();
    
    // Wait for threads to finish
    for (auto& thread : processingThreads_) {
        if (thread && thread->joinable()) {
            thread->join();
        }
    }
    
    processingThreads_.clear();
}

void AudioMonitoringSystem::workerThread() {
    while (running_) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(taskQueueMutex_);
            taskCondition_.wait(lock, [this] { return !taskQueue_.empty() || !running_; });
            
            if (!running_) {
                break;
            }
            
            if (!taskQueue_.empty()) {
                task = taskQueue_.front();
                taskQueue_.pop();
            }
        }
        
        if (task) {
            try {
                task();
            } catch (...) {
                // Ignore task exceptions
            }
        }
    }
}

void AudioMonitoringSystem::enqueueTask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(taskQueueMutex_);
        taskQueue_.push(task);
    }
    taskCondition_.notify_one();
}

std::string AudioMonitoringSystem::generateSubscriptionId() {
    return "sub_" + std::to_string(nextSubscriptionId_++);
}

void AudioMonitoringSystem::updateGlobalMetrics(const RealTimeMetrics& metrics) {
    std::lock_guard<std::mutex> lock(globalMetricsMutex_);
    
    globalMetricsHistory_.push_back(metrics);
    
    // Keep only recent history (last 1000 samples)
    while (globalMetricsHistory_.size() > 1000) {
        globalMetricsHistory_.pop_front();
    }
    
    if (globalAggregator_) {
        globalAggregator_->addMetrics(metrics);
    }
}

void AudioMonitoringSystem::updatePerformanceMetrics() {
    std::lock_guard<std::mutex> lock(performanceMutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto timeSinceUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPerformanceUpdate_);
    
    if (timeSinceUpdate.count() >= 1000) { // Update every second
        performance_.activeSubscriptions = 0;
        performance_.totalCallbacks = 0;
        
        // Count active subscriptions and total callbacks
        std::lock_guard<std::mutex> subLock(subscriptionsMutex_);
        for (const auto& [id, subscription] : subscriptions_) {
            if (subscription && subscription->isActive()) {
                performance_.activeSubscriptions++;
                auto stats = subscription->getStats();
                performance_.totalCallbacks += stats.totalCallbacks;
            }
        }
        
        lastPerformanceUpdate_ = now;
    }
}

std::vector<RealTimeMetrics> AudioMonitoringSystem::getGlobalMetricsHistory(size_t count) const {
    std::lock_guard<std::mutex> lock(globalMetricsMutex_);
    
    size_t startIndex = globalMetricsHistory_.size() > count ? globalMetricsHistory_.size() - count : 0;
    return std::vector<RealTimeMetrics>(globalMetricsHistory_.begin() + startIndex, globalMetricsHistory_.end());
}

MetricsAggregator::AggregatedMetrics AudioMonitoringSystem::getGlobalAggregatedMetrics() const {
    if (globalAggregator_) {
        return globalAggregator_->getAggregatedMetrics();
    }
    return MetricsAggregator::AggregatedMetrics{};
}

AudioMonitoringSystem::SystemPerformance AudioMonitoringSystem::getPerformance() const {
    std::lock_guard<std::mutex> lock(performanceMutex_);
    return performance_;
}

void AudioMonitoringSystem::resetPerformanceCounters() {
    std::lock_guard<std::mutex> lock(performanceMutex_);
    performance_ = SystemPerformance{};
    lastPerformanceUpdate_ = std::chrono::steady_clock::now();
}

AudioMonitoringSystem::SystemHealth AudioMonitoringSystem::getSystemHealth() const {
    SystemHealth health;
    health.isHealthy = true;
    health.overallScore = 1.0f;
    
    // Check if analyzer is healthy
    if (!analyzer_ || !analyzer_->isInitialized()) {
        health.isHealthy = false;
        health.issues.push_back("Audio analyzer not initialized");
        health.overallScore *= 0.5f;
    }
    
    // Check subscription health
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    size_t unhealthySubscriptions = 0;
    
    for (const auto& [id, subscription] : subscriptions_) {
        if (!isSubscriptionHealthy(*subscription)) {
            unhealthySubscriptions++;
        }
    }
    
    if (unhealthySubscriptions > 0) {
        health.warnings.push_back("Some subscriptions are unhealthy");
        health.overallScore *= (1.0f - static_cast<float>(unhealthySubscriptions) / subscriptions_.size() * 0.5f);
    }
    
    // Check performance
    auto perf = getPerformance();
    if (perf.averageProcessingTime > 100.0f) { // 100ms threshold
        health.warnings.push_back("High processing latency detected");
        health.overallScore *= 0.8f;
    }
    
    return health;
}

bool AudioMonitoringSystem::isSubscriptionHealthy(const CallbackSubscription& subscription) const {
    auto stats = subscription.getStats();
    
    // Check if subscription is receiving callbacks
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastCallback = std::chrono::duration_cast<std::chrono::milliseconds>(now - stats.lastCallback);
    
    // Consider unhealthy if no callbacks for more than 10x the expected interval
    auto expectedInterval = subscription.getConfig().updateInterval;
    return timeSinceLastCallback < expectedInterval * 10;
}

void AudioMonitoringSystem::enableDebugMode(bool enabled) {
    debugMode_ = enabled;
}

void AudioMonitoringSystem::logDebugInfo(const std::string& message) const {
    if (debugMode_) {
        auto now = std::chrono::steady_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        // In a real implementation, this would use a proper logging system
        // For now, we'll just store it (could be printed to console in debug builds)
    }
}

// Convenience factory functions
namespace monitoring {

std::unique_ptr<AudioMonitoringSystem> createBasicMonitoringSystem(
    std::shared_ptr<RealTimeAudioAnalyzer> analyzer) {
    auto system = std::make_unique<AudioMonitoringSystem>(analyzer);
    system->setGlobalUpdateInterval(std::chrono::milliseconds(100));
    system->setThreadPoolSize(1);
    return system;
}

std::unique_ptr<AudioMonitoringSystem> createHighPerformanceMonitoringSystem(
    std::shared_ptr<RealTimeAudioAnalyzer> analyzer) {
    auto system = std::make_unique<AudioMonitoringSystem>(analyzer);
    system->setGlobalUpdateInterval(std::chrono::milliseconds(20));
    system->setThreadPoolSize(4);
    system->setMaxSubscriptions(200);
    return system;
}

std::unique_ptr<AudioMonitoringSystem> createVisualizationMonitoringSystem(
    std::shared_ptr<RealTimeAudioAnalyzer> analyzer) {
    auto system = std::make_unique<AudioMonitoringSystem>(analyzer);
    system->setGlobalUpdateInterval(std::chrono::milliseconds(16)); // ~60 FPS
    system->setThreadPoolSize(2);
    return system;
}

CallbackConfig createLowLatencyConfig() {
    CallbackConfig config;
    config.updateInterval = std::chrono::milliseconds(20);
    config.enableFiltering = false;
    config.enableAggregation = false;
    config.historyBufferSize = 50;
    return config;
}

CallbackConfig createHighAccuracyConfig() {
    CallbackConfig config;
    config.updateInterval = std::chrono::milliseconds(100);
    config.enableFiltering = true;
    config.enableAggregation = true;
    config.historyBufferSize = 200;
    config.minChangeThreshold = 0.005f;
    return config;
}

CallbackConfig createVisualizationConfig() {
    CallbackConfig config;
    config.updateInterval = std::chrono::milliseconds(16); // ~60 FPS
    config.enableFiltering = true;
    config.enableAggregation = false;
    config.historyBufferSize = 100;
    config.minChangeThreshold = 0.01f;
    return config;
}

CallbackConfig createAnalyticsConfig() {
    CallbackConfig config;
    config.updateInterval = std::chrono::milliseconds(1000);
    config.enableFiltering = false;
    config.enableAggregation = true;
    config.historyBufferSize = 1000;
    return config;
}

} // namespace monitoring

} // namespace audio