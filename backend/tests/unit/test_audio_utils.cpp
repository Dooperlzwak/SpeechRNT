// Simple test framework since GTest is not available
#include <iostream>
#include <cassert>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define EXPECT_TRUE(condition) assert(condition)
#define EXPECT_FALSE(condition) assert(!(condition))
#define EXPECT_EQ(a, b) assert((a) == (b))
#define EXPECT_NE(a, b) assert((a) != (b))
#define EXPECT_LT(a, b) assert((a) < (b))
#define EXPECT_GT(a, b) assert((a) > (b))
#define EXPECT_LE(a, b) assert((a) <= (b))
#define EXPECT_GE(a, b) assert((a) >= (b))
#define EXPECT_FLOAT_EQ(a, b) assert(std::abs((a) - (b)) < 1e-6f)
#define EXPECT_NEAR(a, b, tolerance) assert(std::abs((a) - (b)) <= (tolerance))

class TestBase {
protected:
    virtual void SetUp() {}
    virtual void TearDown() {}
};

#define TEST_F(test_fixture, test_name) \
    class test_fixture##_##test_name##_Test : public test_fixture { \
    public: \
        void TestBody(); \
    }; \
    void RunTest_##test_fixture##_##test_name() { \
        test_fixture##_##test_name##_Test test; \
        test.SetUp(); \
        test.TestBody(); \
        test.TearDown(); \
        std::cout << #test_fixture "." #test_name " - PASSED" << std::endl; \
    } \
    void test_fixture##_##test_name##_Test::TestBody()

#define TEST(test_case, test_name) \
    void RunTest_##test_case##_##test_name(); \
    void RunTest_##test_case##_##test_name() { \
        std::cout << #test_case "." #test_name " - PASSED" << std::endl; \
    } \
    void Test_##test_case##_##test_name()

namespace testing {
    class Test : public TestBase {};
}
#include "audio/audio_utils.hpp"
#include <vector>
#include <cmath>

using namespace audio;

class AudioUtilsTest : public TestBase {
protected:
    void SetUp() override {
        // Generate test audio data
        const size_t sampleCount = 1024;
        const float frequency = 440.0f; // A4 note
        const uint32_t sampleRate = 16000;
        
        testSamples_.reserve(sampleCount);
        for (size_t i = 0; i < sampleCount; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sampleRate);
            testSamples_.push_back(0.5f * std::sin(2.0f * M_PI * frequency * t));
        }
        
        // Add some noise
        noisySamples_ = testSamples_;
        for (size_t i = 0; i < noisySamples_.size(); ++i) {
            noisySamples_[i] += 0.1f * (static_cast<float>(rand()) / RAND_MAX - 0.5f);
        }
    }
    
    std::vector<float> testSamples_;
    std::vector<float> noisySamples_;
};

TEST_F(AudioUtilsTest, ExtendedAudioFormatValidation) {
    ExtendedAudioFormat validFormat(SampleRate::SR_16000, 1, AudioCodec::PCM_16, 1024);
    EXPECT_TRUE(validFormat.isValid());
    EXPECT_EQ(validFormat.getBytesPerSample(), 2);
    EXPECT_EQ(validFormat.getChunkSizeBytes(), 2048);
    
    ExtendedAudioFormat invalidFormat(SampleRate::SR_16000, 0, AudioCodec::UNKNOWN, 0);
    EXPECT_FALSE(invalidFormat.isValid());
}

TEST_F(AudioUtilsTest, AudioFormatValidatorSupport) {
    ExtendedAudioFormat format(SampleRate::SR_16000, 1, AudioCodec::PCM_16, 1024);
    EXPECT_TRUE(AudioFormatValidator::isFormatSupported(format));
    
    ExtendedAudioFormat unsupportedFormat(static_cast<SampleRate>(96000), 8, AudioCodec::UNKNOWN, 1024);
    EXPECT_FALSE(AudioFormatValidator::isFormatSupported(unsupportedFormat));
    
    EXPECT_TRUE(AudioFormatValidator::validateSampleRate(16000));
    EXPECT_FALSE(AudioFormatValidator::validateSampleRate(96000));
    
    EXPECT_TRUE(AudioFormatValidator::validateChannelCount(1));
    EXPECT_TRUE(AudioFormatValidator::validateChannelCount(2));
    EXPECT_FALSE(AudioFormatValidator::validateChannelCount(8));
}

TEST_F(AudioUtilsTest, AudioFormatConversion) {
    // Test stereo to mono conversion
    std::vector<float> stereoData = {0.5f, -0.5f, 0.3f, -0.3f, 0.1f, -0.1f};
    auto monoData = AudioFormatConverter::stereoToMono(stereoData);
    
    EXPECT_EQ(monoData.size(), 3);
    EXPECT_FLOAT_EQ(monoData[0], 0.0f);
    EXPECT_FLOAT_EQ(monoData[1], 0.0f);
    EXPECT_FLOAT_EQ(monoData[2], 0.0f);
    
    // Test mono to stereo conversion
    std::vector<float> monoInput = {0.5f, 0.3f, 0.1f};
    auto stereoOutput = AudioFormatConverter::monoToStereo(monoInput);
    
    EXPECT_EQ(stereoOutput.size(), 6);
    EXPECT_FLOAT_EQ(stereoOutput[0], 0.5f);
    EXPECT_FLOAT_EQ(stereoOutput[1], 0.5f);
    EXPECT_FLOAT_EQ(stereoOutput[2], 0.3f);
    EXPECT_FLOAT_EQ(stereoOutput[3], 0.3f);
    
    // Test PCM16 conversion
    auto pcm16Data = AudioFormatConverter::convertToPCM16(testSamples_);
    EXPECT_EQ(pcm16Data.size(), testSamples_.size());
    
    for (size_t i = 0; i < std::min(pcm16Data.size(), testSamples_.size()); ++i) {
        float expected = testSamples_[i] * 32767.0f;
        EXPECT_NEAR(static_cast<float>(pcm16Data[i]), expected, 1.0f);
    }
}

TEST_F(AudioUtilsTest, AudioQualityAssessment) {
    AudioQualityMetrics metrics = AudioQualityAssessor::assessQuality(testSamples_, 16000);
    
    // Clean sine wave should have good quality metrics
    EXPECT_GT(metrics.signalToNoiseRatio, 20.0f);
    EXPECT_LT(metrics.totalHarmonicDistortion, 5.0f);
    EXPECT_GT(metrics.dynamicRange, 30.0f);
    EXPECT_FALSE(metrics.hasClipping);
    EXPECT_FALSE(metrics.hasSilence);
    EXPECT_GT(metrics.rmsLevel, 0.1f);
    EXPECT_TRUE(metrics.isGoodQuality());
    
    // Test individual quality functions
    float snr = AudioQualityAssessor::calculateSNR(testSamples_);
    EXPECT_GT(snr, 20.0f);
    
    float rms = AudioQualityAssessor::calculateRMSLevel(testSamples_);
    EXPECT_GT(rms, 0.1f);
    EXPECT_LT(rms, 1.0f);
    
    float zcr = AudioQualityAssessor::calculateZeroCrossingRate(testSamples_, 16000);
    EXPECT_GT(zcr, 400.0f); // Should be close to 440 Hz * 2
    EXPECT_LT(zcr, 1000.0f);
    
    EXPECT_FALSE(AudioQualityAssessor::hasClipping(testSamples_));
    EXPECT_FALSE(AudioQualityAssessor::hasSilence(testSamples_));
}

TEST_F(AudioUtilsTest, NoiseDetection) {
    NoiseProfile cleanProfile = NoiseDetector::analyzeNoise(testSamples_, 16000);
    EXPECT_GT(cleanProfile.getSNR(), 20.0f);
    EXPECT_FALSE(cleanProfile.requiresDenoising());
    
    NoiseProfile noisyProfile = NoiseDetector::analyzeNoise(noisySamples_, 16000);
    EXPECT_LT(noisyProfile.getSNR(), cleanProfile.getSNR());
    
    // Test individual noise detection functions
    float noiseLevel = NoiseDetector::detectNoiseLevel(noisySamples_);
    float speechLevel = NoiseDetector::detectSpeechLevel(noisySamples_);
    EXPECT_LT(noiseLevel, speechLevel);
    
    EXPECT_FALSE(NoiseDetector::hasBackgroundNoise(testSamples_));
    EXPECT_FALSE(NoiseDetector::hasImpulseNoise(testSamples_));
    
    auto noiseType = NoiseDetector::classifyNoise(cleanProfile);
    EXPECT_EQ(noiseType, NoiseDetector::NoiseType::NONE);
    
    std::string typeStr = NoiseDetector::noiseTypeToString(noiseType);
    EXPECT_EQ(typeStr, "None");
}

TEST_F(AudioUtilsTest, AudioPreprocessing) {
    // Test amplitude normalization
    auto normalized = AudioPreprocessor::normalizeAmplitude(testSamples_, 0.8f);
    EXPECT_EQ(normalized.size(), testSamples_.size());
    
    float maxAmplitude = 0.0f;
    for (float sample : normalized) {
        maxAmplitude = std::max(maxAmplitude, std::abs(sample));
    }
    EXPECT_NEAR(maxAmplitude, 0.8f, 0.01f);
    
    // Test gain control
    auto gained = AudioPreprocessor::applyGainControl(testSamples_, 2.0f);
    EXPECT_EQ(gained.size(), testSamples_.size());
    
    // Test clipping removal
    std::vector<float> clippedSamples = testSamples_;
    clippedSamples[0] = 1.0f;  // Add clipping
    clippedSamples[1] = -1.0f;
    
    auto declipped = AudioPreprocessor::removeClipping(clippedSamples);
    EXPECT_EQ(declipped.size(), clippedSamples.size());
    EXPECT_LT(std::abs(declipped[0]), 1.0f);
    EXPECT_LT(std::abs(declipped[1]), 1.0f);
    
    // Test noise reduction
    auto denoised = AudioPreprocessor::adaptiveNoiseReduction(noisySamples_, 16000);
    EXPECT_EQ(denoised.size(), noisySamples_.size());
    
    // Test speech enhancement
    auto enhanced = AudioPreprocessor::enhanceSpeech(noisySamples_, 16000);
    EXPECT_EQ(enhanced.size(), noisySamples_.size());
}

TEST_F(AudioUtilsTest, AudioStreamValidation) {
    ExtendedAudioFormat format(SampleRate::SR_16000, 1, AudioCodec::PCM_16, 1024);
    AudioStreamValidator validator(format);
    
    // Convert test samples to PCM16 data
    auto pcm16Data = AudioFormatConverter::convertToPCM16(testSamples_);
    std::string pcmString(reinterpret_cast<const char*>(pcm16Data.data()), 
                         pcm16Data.size() * sizeof(int16_t));
    
    // Test chunk validation
    EXPECT_TRUE(validator.validateChunk(pcmString));
    
    // Test continuity validation
    EXPECT_TRUE(validator.validateContinuity(testSamples_));
    
    // Test latency validation
    EXPECT_TRUE(validator.validateLatency(std::chrono::milliseconds(50)));
    EXPECT_FALSE(validator.validateLatency(std::chrono::milliseconds(200)));
    
    // Test stream health
    auto health = validator.getStreamHealth();
    EXPECT_TRUE(health.isHealthy);
    EXPECT_LT(health.dropoutRate, 0.05f);
    
    // Test configuration
    validator.setQualityThresholds(25.0f, 5.0f);
    validator.setLatencyThreshold(std::chrono::milliseconds(80));
    
    // Reset and test again
    validator.resetHealth();
    health = validator.getStreamHealth();
    EXPECT_FALSE(health.isHealthy); // Should be unhealthy with no data
}

TEST_F(AudioUtilsTest, FormatConversionIntegration) {
    ExtendedAudioFormat inputFormat(SampleRate::SR_16000, 1, AudioCodec::PCM_16, 1024);
    ExtendedAudioFormat outputFormat(SampleRate::SR_22050, 2, AudioCodec::FLOAT_32, 1024);
    
    // Convert test samples to PCM16 data
    auto pcm16Data = AudioFormatConverter::convertToPCM16(testSamples_);
    std::string inputData(reinterpret_cast<const char*>(pcm16Data.data()), 
                         pcm16Data.size() * sizeof(int16_t));
    
    // Test format conversion
    auto converted = AudioFormatConverter::convertFormat(inputData, inputFormat, outputFormat);
    
    // Should have more samples due to upsampling (16kHz -> 22.05kHz)
    EXPECT_GT(converted.size(), testSamples_.size());
    
    // Should be stereo (2x samples due to mono->stereo conversion)
    size_t expectedStereoSamples = static_cast<size_t>(testSamples_.size() * 22050.0f / 16000.0f) * 2;
    EXPECT_NEAR(converted.size(), expectedStereoSamples, expectedStereoSamples * 0.1f); // 10% tolerance
}

TEST_F(AudioUtilsTest, QualityMetricsIntegration) {
    // Test with different quality audio samples
    std::vector<float> silentSamples(1024, 0.0f);
    std::vector<float> clippedSamples(1024, 1.0f);
    
    auto silentMetrics = AudioQualityAssessor::assessQuality(silentSamples, 16000);
    EXPECT_TRUE(silentMetrics.hasSilence);
    EXPECT_FALSE(silentMetrics.isGoodQuality());
    
    auto clippedMetrics = AudioQualityAssessor::assessQuality(clippedSamples, 16000);
    EXPECT_TRUE(clippedMetrics.hasClipping);
    EXPECT_FALSE(clippedMetrics.isGoodQuality());
    
    // Test quality issues reporting
    auto issues = AudioQualityAssessor::getQualityIssues(silentMetrics);
    EXPECT_FALSE(issues.empty());
    
    auto clippedIssues = AudioQualityAssessor::getQualityIssues(clippedMetrics);
    EXPECT_FALSE(clippedIssues.empty());
    
    // Test preprocessing requirement
    EXPECT_TRUE(AudioQualityAssessor::requiresPreprocessing(silentMetrics));
    EXPECT_TRUE(AudioQualityAssessor::requiresPreprocessing(clippedMetrics));
    
    auto cleanMetrics = AudioQualityAssessor::assessQuality(testSamples_, 16000);
    EXPECT_FALSE(AudioQualityAssessor::requiresPreprocessing(cleanMetrics));
}
// T
est runner
int main() {
    std::cout << "Running Audio Utils Tests..." << std::endl;
    
    try {
        RunTest_AudioUtilsTest_ExtendedAudioFormatValidation();
        RunTest_AudioUtilsTest_AudioFormatValidatorSupport();
        RunTest_AudioUtilsTest_AudioFormatConversion();
        RunTest_AudioUtilsTest_AudioQualityAssessment();
        RunTest_AudioUtilsTest_NoiseDetection();
        RunTest_AudioUtilsTest_AudioPreprocessing();
        RunTest_AudioUtilsTest_AudioStreamValidation();
        RunTest_AudioUtilsTest_FormatConversionIntegration();
        RunTest_AudioUtilsTest_QualityMetricsIntegration();
        
        std::cout << "All tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cout << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cout << "Test failed with unknown exception" << std::endl;
        return 1;
    }
}