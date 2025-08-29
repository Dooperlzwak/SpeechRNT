#include "stt/stt_performance_tracker.hpp"
#include "utils/performance_monitor.hpp"
#include "utils/logging.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <vector>

using namespace stt;
using namespace speechrnt::utils;
using utils::Logger;

/**
 * STT Performance Monitoring Integration Demo
 * 
 * This demo shows how the enhanced STT performance monitoring system works
 * with realistic transcription scenarios including:
 * - Pipeline stage latency tracking
 * - Confidence score monitoring
 * - Throughput measurement
 * - Concurrent transcription tracking
 * - VAD performance metrics
 * - Streaming transcription updates
 */

class STTPerformanceDemo {
public:
    STTPerformanceDemo() {
        // Initialize performance monitor
        PerformanceMonitor::getInstance().initialize(true, 1000);
        
        // Create STT performance tracker
        sttTracker_ = std::make_unique<STTPerformanceTracker>();
        sttTracker_->initialize(true);
        
        Logger::info("STT Performance Demo initialized");
    }
    
    void runDemo() {
        std::cout << "\n=== STT Performance Monitoring Integration Demo ===\n" << std::endl;
        
        // Demo 1: Basic transcription with full pipeline tracking
        std::cout << "1. Running basic transcription pipeline demo..." << std::endl;
        runBasicTranscriptionDemo();
        
        // Demo 2: Streaming transcription with incremental updates
        std::cout << "\n2. Running streaming transcription demo..." << std::endl;
        runStreamingTranscriptionDemo();
        
        // Demo 3: Concurrent transcriptions
        std::cout << "\n3. Running concurrent transcriptions demo..." << std::endl;
        runConcurrentTranscriptionsDemo();
        
        // Demo 4: VAD performance tracking
        std::cout << "\n4. Running VAD performance demo..." << std::endl;
        runVADPerformanceDemo();
        
        // Demo 5: Language detection tracking
        std::cout << "\n5. Running language detection demo..." << std::endl;
        runLanguageDetectionDemo();
        
        // Demo 6: Model loading and buffer usage
        std::cout << "\n6. Running resource usage demo..." << std::endl;
        runResourceUsageDemo();
        
        // Show final performance summary
        std::cout << "\n=== Final Performance Summary ===" << std::endl;
        showPerformanceSummary();
        
        std::cout << "\nDemo completed successfully!" << std::endl;
    }

private:
    std::unique_ptr<STTPerformanceTracker> sttTracker_;
    std::mt19937 rng_{std::random_device{}()};
    
    void runBasicTranscriptionDemo() {
        const int numTranscriptions = 5;
        
        for (int i = 0; i < numTranscriptions; ++i) {
            uint32_t utteranceId = 1000 + i;
            
            // Use RAII session tracker for automatic lifecycle management
            STT_TRACK_TRANSCRIPTION(*sttTracker_, utteranceId, false);
            auto sessionId = sttTracker_->startTranscription(utteranceId, false);
            
            // Simulate VAD processing
            double vadLatency = generateRandomLatency(20, 80);
            float vadAccuracy = generateRandomConfidence(0.85f, 0.98f);
            sttTracker_->recordVADProcessing(sessionId, vadLatency, vadAccuracy, i % 3 == 0);
            
            // Simulate preprocessing
            double prepLatency = generateRandomLatency(10, 40);
            double audioLength = generateRandomLatency(500, 3000);
            sttTracker_->recordPreprocessing(sessionId, prepLatency, audioLength);
            
            // Simulate inference (this is the main processing step)
            double inferenceLatency = generateRandomLatency(150, 400);
            std::string modelType = (i % 2 == 0) ? "whisper-base" : "whisper-small";
            bool useGPU = i % 3 != 0; // Use GPU for most transcriptions
            sttTracker_->recordInference(sessionId, inferenceLatency, modelType, useGPU);
            
            // Simulate postprocessing
            double postLatency = generateRandomLatency(5, 25);
            size_t textLength = 50 + (i * 20);
            sttTracker_->recordPostprocessing(sessionId, postLatency, textLength);
            
            // Record transcription result
            float confidence = generateRandomConfidence(0.75f, 0.95f);
            std::string language = (i % 4 == 0) ? "es" : "en";
            float langConfidence = generateRandomConfidence(0.80f, 0.98f);
            sttTracker_->recordTranscriptionResult(sessionId, confidence, false, textLength, language, langConfidence);
            
            // Complete transcription
            bool success = confidence > 0.7f; // Success based on confidence
            sttTracker_->completeTranscription(sessionId, success, confidence, textLength);
            
            // Small delay between transcriptions
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        std::cout << "   Completed " << numTranscriptions << " basic transcriptions" << std::endl;
    }
    
    void runStreamingTranscriptionDemo() {
        const int numStreamingTranscriptions = 3;
        
        for (int i = 0; i < numStreamingTranscriptions; ++i) {
            uint32_t utteranceId = 2000 + i;
            
            // Start streaming transcription
            uint64_t sessionId = sttTracker_->startTranscription(utteranceId, true);
            
            // Simulate multiple streaming updates
            int numUpdates = 5 + (i * 2);
            for (int update = 0; update < numUpdates; ++update) {
                double updateLatency = generateRandomLatency(25, 60);
                bool isIncremental = update > 0;
                int textDelta = 10 + (update * 5);
                
                sttTracker_->recordStreamingUpdate(sessionId, updateLatency, isIncremental, textDelta);
                
                // Record partial result
                float partialConfidence = generateRandomConfidence(0.60f, 0.85f);
                size_t currentTextLength = 20 + (update * 15);
                sttTracker_->recordTranscriptionResult(sessionId, partialConfidence, true, currentTextLength);
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            // Final result
            float finalConfidence = generateRandomConfidence(0.80f, 0.95f);
            size_t finalTextLength = 20 + (numUpdates * 15);
            sttTracker_->recordTranscriptionResult(sessionId, finalConfidence, false, finalTextLength);
            
            // Complete streaming transcription
            sttTracker_->completeTranscription(sessionId, true, finalConfidence, finalTextLength);
        }
        
        std::cout << "   Completed " << numStreamingTranscriptions << " streaming transcriptions" << std::endl;
    }
    
    void runConcurrentTranscriptionsDemo() {
        const int numConcurrent = 4;
        std::vector<std::thread> threads;
        std::vector<uint64_t> sessionIds;
        
        // Start concurrent transcriptions
        for (int i = 0; i < numConcurrent; ++i) {
            uint32_t utteranceId = 3000 + i;
            uint64_t sessionId = sttTracker_->startTranscription(utteranceId, false);
            sessionIds.push_back(sessionId);
            
            threads.emplace_back([this, sessionId, i]() {
                // Simulate concurrent processing with different latencies
                std::this_thread::sleep_for(std::chrono::milliseconds(100 + i * 50));
                
                // Record processing stages
                sttTracker_->recordVADProcessing(sessionId, generateRandomLatency(30, 70), 0.9f, false);
                sttTracker_->recordInference(sessionId, generateRandomLatency(200, 500), "whisper-base", true);
                
                float confidence = generateRandomConfidence(0.75f, 0.92f);
                sttTracker_->recordTranscriptionResult(sessionId, confidence, false, 80 + i * 20);
                sttTracker_->completeTranscription(sessionId, true, confidence, 80 + i * 20);
            });
        }
        
        // Wait for all to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        std::cout << "   Completed " << numConcurrent << " concurrent transcriptions" << std::endl;
    }
    
    void runVADPerformanceDemo() {
        const int numVADOperations = 20;
        
        for (int i = 0; i < numVADOperations; ++i) {
            double responseTime = generateRandomLatency(15, 85);
            float accuracy = generateRandomConfidence(0.82f, 0.96f);
            bool stateChanged = i % 4 == 0; // State changes occasionally
            
            sttTracker_->recordVADMetrics(responseTime, accuracy, stateChanged);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        
        std::cout << "   Recorded " << numVADOperations << " VAD operations" << std::endl;
    }
    
    void runLanguageDetectionDemo() {
        const std::vector<std::string> languages = {"en", "es", "fr", "de", "it"};
        std::string currentLang = "en";
        
        for (int i = 0; i < 10; ++i) {
            double detectionLatency = generateRandomLatency(60, 120);
            float confidence = generateRandomConfidence(0.85f, 0.98f);
            
            // Occasionally switch languages
            std::string newLang = currentLang;
            if (i % 3 == 0 && i > 0) {
                newLang = languages[i % languages.size()];
            }
            
            sttTracker_->recordLanguageDetection(detectionLatency, confidence, newLang, currentLang);
            currentLang = newLang;
            
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        
        std::cout << "   Recorded 10 language detection operations" << std::endl;
    }
    
    void runResourceUsageDemo() {
        // Simulate model loading
        sttTracker_->recordModelLoading("whisper-base", 1200.0, 142.5, true);
        sttTracker_->recordModelLoading("whisper-small", 800.0, 244.8, false);
        
        // Simulate buffer usage over time
        for (int i = 0; i < 10; ++i) {
            double bufferSize = 10.0 + (i * 2.5);
            float utilization = 45.0f + (i * 5.0f);
            int utteranceCount = 1 + (i / 3);
            
            sttTracker_->recordBufferUsage(bufferSize, utilization, utteranceCount);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "   Recorded model loading and buffer usage metrics" << std::endl;
    }
    
    void showPerformanceSummary() {
        // Update throughput metrics
        sttTracker_->updateThroughputMetrics();
        
        // Get performance summary
        auto summary = sttTracker_->getPerformanceSummary();
        
        std::cout << "\nKey Performance Indicators:" << std::endl;
        std::cout << "  Total Transcriptions: " << summary["total_transcriptions"] << std::endl;
        std::cout << "  Successful Transcriptions: " << summary["successful_transcriptions"] << std::endl;
        std::cout << "  Success Rate: " << (summary["success_rate"] * 100) << "%" << std::endl;
        std::cout << "  Streaming Transcriptions: " << summary["streaming_transcriptions"] << std::endl;
        std::cout << "  Streaming Ratio: " << (summary["streaming_ratio"] * 100) << "%" << std::endl;
        
        // Get detailed metrics
        auto detailedMetrics = sttTracker_->getDetailedMetrics(5);
        
        std::cout << "\nLatency Metrics (last 5 minutes):" << std::endl;
        if (detailedMetrics["overall_latency"].count > 0) {
            auto& latency = detailedMetrics["overall_latency"];
            std::cout << "  Overall STT Latency: " << latency.mean << "ms (p95: " << latency.p95 << "ms)" << std::endl;
        }
        
        if (detailedMetrics["vad_latency"].count > 0) {
            auto& vadLatency = detailedMetrics["vad_latency"];
            std::cout << "  VAD Latency: " << vadLatency.mean << "ms (p95: " << vadLatency.p95 << "ms)" << std::endl;
        }
        
        if (detailedMetrics["inference_latency"].count > 0) {
            auto& infLatency = detailedMetrics["inference_latency"];
            std::cout << "  Inference Latency: " << infLatency.mean << "ms (p95: " << infLatency.p95 << "ms)" << std::endl;
        }
        
        std::cout << "\nQuality Metrics:" << std::endl;
        if (detailedMetrics["confidence_score"].count > 0) {
            auto& confidence = detailedMetrics["confidence_score"];
            std::cout << "  Average Confidence: " << confidence.mean << " (min: " << confidence.min << ")" << std::endl;
        }
        
        if (detailedMetrics["vad_accuracy"].count > 0) {
            auto& vadAcc = detailedMetrics["vad_accuracy"];
            std::cout << "  VAD Accuracy: " << vadAcc.mean << std::endl;
        }
        
        std::cout << "\nResource Usage:" << std::endl;
        if (detailedMetrics["buffer_usage"].count > 0) {
            auto& bufferUsage = detailedMetrics["buffer_usage"];
            std::cout << "  Average Buffer Usage: " << bufferUsage.mean << "MB" << std::endl;
        }
        
        if (detailedMetrics["model_load_time"].count > 0) {
            auto& modelLoad = detailedMetrics["model_load_time"];
            std::cout << "  Model Load Time: " << modelLoad.mean << "ms" << std::endl;
        }
        
        // Show global performance monitor summary
        auto globalSummary = PerformanceMonitor::getInstance().getSTTPerformanceSummary();
        std::cout << "\nGlobal STT Performance Summary:" << std::endl;
        for (const auto& [key, value] : globalSummary) {
            std::cout << "  " << key << ": " << value << std::endl;
        }
    }
    
    double generateRandomLatency(double min, double max) {
        std::uniform_real_distribution<double> dist(min, max);
        return dist(rng_);
    }
    
    float generateRandomConfidence(float min, float max) {
        std::uniform_real_distribution<float> dist(min, max);
        return dist(rng_);
    }
};

int main() {
    try {
        // Initialize logging
        Logger::initialize(Logger::Level::INFO);
        
        // Run the demo
        STTPerformanceDemo demo;
        demo.runDemo();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        return 1;
    }
}