#pragma once

#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <cstdint>

namespace stt {
namespace advanced {

/**
 * Audio level metrics
 */
struct AudioLevelMetrics {
    float currentLevel;      // Current RMS level
    float peakLevel;        // Peak level in current window
    float averageLevel;     // Running average level
    bool clipping;          // Audio clipping detected
    bool silence;           // Silence detected
    float dynamicRange;     // Dynamic range in current window
    
    AudioLevelMetrics() 
        : currentLevel(0.0f)
        , peakLevel(0.0f)
        , averageLevel(0.0f)
        , clipping(false)
        , silence(false)
        , dynamicRange(0.0f) {}
};

/**
 * Spectral analysis data
 */
struct SpectralAnalysis {
    std::vector<float> frequencySpectrum;
    float dominantFrequency;
    float spectralCentroid;
    float spectralBandwidth;
    float spectralRolloff;
    std::vector<float> mfccCoefficients;
    float spectralFlatness;
    float spectralFlux;
    
    SpectralAnalysis() 
        : dominantFrequency(0.0f)
        , spectralCentroid(0.0f)
        , spectralBandwidth(0.0f)
        , spectralRolloff(0.0f)
        , spectralFlatness(0.0f)
        , spectralFlux(0.0f) {}
};

/**
 * Real-time audio metrics
 */
struct RealTimeMetrics {
    AudioLevelMetrics levels;
    SpectralAnalysis spectral;
    float noiseLevel;
    float speechProbability;
    int64_t timestampMs;
    bool vadActive; // Voice activity detection
    float qualityScore; // Overall audio quality (0.0 to 1.0)
    
    RealTimeMetrics() 
        : noiseLevel(0.0f)
        , speechProbability(0.0f)
        , timestampMs(0)
        , vadActive(false)
        , qualityScore(0.0f) {}
};

/**
 * Circular buffer interface for real-time processing
 */
template<typename T>
class CircularBuffer {
public:
    virtual ~CircularBuffer() = default;
    
    /**
     * Initialize buffer with specified size
     * @param size Buffer size
     * @return true if initialization successful
     */
    virtual bool initialize(size_t size) = 0;
    
    /**
     * Write data to buffer
     * @param data Data to write
     * @param count Number of elements to write
     * @return Number of elements actually written
     */
    virtual size_t write(const T* data, size_t count) = 0;
    
    /**
     * Read data from buffer
     * @param data Buffer to read into
     * @param count Number of elements to read
     * @return Number of elements actually read
     */
    virtual size_t read(T* data, size_t count) = 0;
    
    /**
     * Get available data count
     * @return Number of elements available for reading
     */
    virtual size_t available() const = 0;
    
    /**
     * Get free space count
     * @return Number of elements that can be written
     */
    virtual size_t freeSpace() const = 0;
    
    /**
     * Clear buffer
     */
    virtual void clear() = 0;
    
    /**
     * Check if buffer is full
     * @return true if buffer is full
     */
    virtual bool isFull() const = 0;
    
    /**
     * Check if buffer is empty
     * @return true if buffer is empty
     */
    virtual bool isEmpty() const = 0;
    
    /**
     * Get buffer capacity
     * @return Buffer capacity
     */
    virtual size_t capacity() const = 0;
};

/**
 * Real-time FFT processor interface
 */
class RealTimeFFT {
public:
    virtual ~RealTimeFFT() = default;
    
    /**
     * Initialize FFT processor
     * @param fftSize FFT size (must be power of 2)
     * @param sampleRate Audio sample rate
     * @return true if initialization successful
     */
    virtual bool initialize(size_t fftSize, int sampleRate) = 0;
    
    /**
     * Process audio samples and compute FFT
     * @param audioSamples Input audio samples
     * @return Frequency spectrum (magnitude)
     */
    virtual std::vector<float> processFFT(const std::vector<float>& audioSamples) = 0;
    
    /**
     * Get frequency bins
     * @return Vector of frequency values for each bin
     */
    virtual std::vector<float> getFrequencyBins() const = 0;
    
    /**
     * Get FFT size
     * @return FFT size
     */
    virtual size_t getFFTSize() const = 0;
    
    /**
     * Check if processor is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Level meter interface
 */
class LevelMeter {
public:
    virtual ~LevelMeter() = default;
    
    /**
     * Initialize level meter
     * @param sampleRate Audio sample rate
     * @param windowSizeMs Window size for level calculation (ms)
     * @return true if initialization successful
     */
    virtual bool initialize(int sampleRate, float windowSizeMs = 100.0f) = 0;
    
    /**
     * Process audio samples and update levels
     * @param audioSamples Input audio samples
     * @return Current level metrics
     */
    virtual AudioLevelMetrics processLevels(const std::vector<float>& audioSamples) = 0;
    
    /**
     * Get current level metrics
     * @return Current level metrics
     */
    virtual AudioLevelMetrics getCurrentLevels() const = 0;
    
    /**
     * Reset level meter state
     */
    virtual void reset() = 0;
    
    /**
     * Set clipping threshold
     * @param threshold Clipping threshold (0.0 to 1.0)
     */
    virtual void setClippingThreshold(float threshold) = 0;
    
    /**
     * Set silence threshold
     * @param threshold Silence threshold (dB)
     */
    virtual void setSilenceThreshold(float threshold) = 0;
    
    /**
     * Check if meter is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Noise estimator interface
 */
class NoiseEstimator {
public:
    virtual ~NoiseEstimator() = default;
    
    /**
     * Initialize noise estimator
     * @param sampleRate Audio sample rate
     * @return true if initialization successful
     */
    virtual bool initialize(int sampleRate) = 0;
    
    /**
     * Estimate noise level from audio
     * @param audioSamples Input audio samples
     * @return Estimated noise level (dB)
     */
    virtual float estimateNoiseLevel(const std::vector<float>& audioSamples) = 0;
    
    /**
     * Update noise profile
     * @param audioSamples Audio samples for noise profile update
     */
    virtual void updateNoiseProfile(const std::vector<float>& audioSamples) = 0;
    
    /**
     * Get current noise level
     * @return Current noise level estimate (dB)
     */
    virtual float getCurrentNoiseLevel() const = 0;
    
    /**
     * Reset noise estimator
     */
    virtual void reset() = 0;
    
    /**
     * Check if estimator is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Speech detector interface
 */
class SpeechDetector {
public:
    virtual ~SpeechDetector() = default;
    
    /**
     * Initialize speech detector
     * @param sampleRate Audio sample rate
     * @return true if initialization successful
     */
    virtual bool initialize(int sampleRate) = 0;
    
    /**
     * Detect speech probability in audio
     * @param audioSamples Input audio samples
     * @return Speech probability (0.0 to 1.0)
     */
    virtual float detectSpeechProbability(const std::vector<float>& audioSamples) = 0;
    
    /**
     * Check if speech is detected
     * @param audioSamples Input audio samples
     * @param threshold Detection threshold (0.0 to 1.0)
     * @return true if speech is detected
     */
    virtual bool isSpeechDetected(const std::vector<float>& audioSamples, 
                                 float threshold = 0.5f) = 0;
    
    /**
     * Set speech detection threshold
     * @param threshold Detection threshold (0.0 to 1.0)
     */
    virtual void setSpeechThreshold(float threshold) = 0;
    
    /**
     * Get current speech probability
     * @return Current speech probability
     */
    virtual float getCurrentSpeechProbability() const = 0;
    
    /**
     * Reset speech detector
     */
    virtual void reset() = 0;
    
    /**
     * Check if detector is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
};

/**
 * Real-time audio analyzer interface
 */
class RealTimeAudioAnalyzerInterface {
public:
    virtual ~RealTimeAudioAnalyzerInterface() = default;
    
    /**
     * Initialize the real-time audio analyzer
     * @param sampleRate Audio sample rate
     * @param bufferSize Buffer size for analysis
     * @return true if initialization successful
     */
    virtual bool initialize(int sampleRate, size_t bufferSize) = 0;
    
    /**
     * Process single audio sample
     * @param sample Audio sample to process
     */
    virtual void processAudioSample(float sample) = 0;
    
    /**
     * Process audio chunk
     * @param chunk Audio chunk to process
     */
    virtual void processAudioChunk(const std::vector<float>& chunk) = 0;
    
    /**
     * Get current real-time metrics
     * @return Current metrics
     */
    virtual RealTimeMetrics getCurrentMetrics() const = 0;
    
    /**
     * Get metrics history
     * @param samples Number of historical samples to retrieve
     * @return Vector of historical metrics
     */
    virtual std::vector<RealTimeMetrics> getMetricsHistory(size_t samples) const = 0;
    
    /**
     * Register callback for metrics updates
     * @param callback Callback function to register
     */
    virtual void registerMetricsCallback(std::function<void(const RealTimeMetrics&)> callback) = 0;
    
    /**
     * Unregister all metrics callbacks
     */
    virtual void clearMetricsCallbacks() = 0;
    
    /**
     * Enable or disable real-time effects processing
     * @param enabled true to enable effects
     */
    virtual void enableRealTimeEffects(bool enabled) = 0;
    
    /**
     * Apply real-time effects to audio
     * @param audio Input audio samples
     * @return Processed audio samples
     */
    virtual std::vector<float> applyRealTimeEffects(const std::vector<float>& audio) = 0;
    
    /**
     * Set analysis update interval
     * @param intervalMs Update interval in milliseconds
     */
    virtual void setUpdateInterval(float intervalMs) = 0;
    
    /**
     * Enable or disable spectral analysis
     * @param enabled true to enable spectral analysis
     */
    virtual void setSpectralAnalysisEnabled(bool enabled) = 0;
    
    /**
     * Enable or disable level metering
     * @param enabled true to enable level metering
     */
    virtual void setLevelMeteringEnabled(bool enabled) = 0;
    
    /**
     * Enable or disable noise estimation
     * @param enabled true to enable noise estimation
     */
    virtual void setNoiseEstimationEnabled(bool enabled) = 0;
    
    /**
     * Enable or disable speech detection
     * @param enabled true to enable speech detection
     */
    virtual void setSpeechDetectionEnabled(bool enabled) = 0;
    
    /**
     * Set VAD (Voice Activity Detection) threshold
     * @param threshold VAD threshold (0.0 to 1.0)
     */
    virtual void setVADThreshold(float threshold) = 0;
    
    /**
     * Get analysis buffer size
     * @return Current buffer size
     */
    virtual size_t getBufferSize() const = 0;
    
    /**
     * Get sample rate
     * @return Current sample rate
     */
    virtual int getSampleRate() const = 0;
    
    /**
     * Reset analyzer state
     */
    virtual void reset() = 0;
    
    /**
     * Update configuration
     * @param config New real-time analysis configuration
     * @return true if update successful
     */
    virtual bool updateConfiguration(const RealTimeAnalysisConfig& config) = 0;
    
    /**
     * Get current configuration
     * @return Current real-time analysis configuration
     */
    virtual RealTimeAnalysisConfig getCurrentConfiguration() const = 0;
    
    /**
     * Check if analyzer is initialized
     * @return true if initialized
     */
    virtual bool isInitialized() const = 0;
    
    /**
     * Get last error message
     * @return Last error message
     */
    virtual std::string getLastError() const = 0;
    
    /**
     * Get processing statistics
     * @return Statistics as JSON string
     */
    virtual std::string getProcessingStats() const = 0;
};

} // namespace advanced
} // namespace stt