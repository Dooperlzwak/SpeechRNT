# Test configuration
find_package(GTest QUIET)

# Include directories for tests
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/third_party)

# Helper function to link common libraries to test targets
function(link_test_libraries target_name)
    target_link_libraries(${target_name} Threads::Threads)
    
    # Link optional libraries if available
    if(ZLIB_FOUND)
        target_link_libraries(${target_name} ${ZLIB_LIBRARIES})
    endif()
    if(LIBUV_FOUND)
        target_link_libraries(${target_name} ${LIBUV_LIBRARIES})
    endif()
    if(SILERO_VAD_AVAILABLE)
        target_link_libraries(${target_name} ${ONNXRUNTIME_LIB})
    endif()
    if(MARIAN_AVAILABLE)
        target_link_libraries(${target_name} ${MARIAN_LIBRARIES})
    endif()
    if(CUDA_AVAILABLE)
        target_link_libraries(${target_name} 
            CUDA::cudart
            CUDA::cuda_driver
        )
        if(CUBLAS_LIBRARY)
            target_link_libraries(${target_name} CUDA::cublas)
        endif()
        if(CUDNN_LIBRARY)
            target_link_libraries(${target_name} ${CUDNN_LIBRARY})
        endif()
    endif()
endfunction()

# Source files needed for testing (excluding main.cpp)
file(GLOB_RECURSE TEST_SOURCES 
    "${CMAKE_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/audio/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/stt/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/mt/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/tts/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/models/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/*.cpp"
)

# Add whisper.cpp sources if available
if(WHISPER_AVAILABLE)
    list(APPEND TEST_SOURCES ${WHISPER_SOURCES})
endif()

# VAD Integration test (standalone)
add_executable(vad_integration_test integration/test_vad_integration.cpp ${TEST_SOURCES})
link_test_libraries(vad_integration_test)
add_test(NAME VadIntegrationTest COMMAND vad_integration_test)

# VAD Edge Cases test (standalone)
add_executable(vad_edge_cases_test integration/test_vad_edge_cases.cpp ${TEST_SOURCES})
link_test_libraries(vad_edge_cases_test)
add_test(NAME VadEdgeCasesTest COMMAND vad_edge_cases_test)

# Silero VAD test (standalone)
add_executable(silero_vad_test unit/test_silero_vad.cpp ${TEST_SOURCES})
link_test_libraries(silero_vad_test)
add_test(NAME SileroVadTest COMMAND silero_vad_test)

# Silero VAD Integration test (standalone)
add_executable(silero_vad_integration_test integration/test_silero_vad_integration.cpp ${TEST_SOURCES})
link_test_libraries(silero_vad_integration_test)
add_test(NAME SileroVadIntegrationTest COMMAND silero_vad_integration_test)

# WhisperSTT comprehensive test suite
add_executable(whisper_stt_test unit/test_whisper_stt.cpp ${TEST_SOURCES})
link_test_libraries(whisper_stt_test)
add_test(NAME WhisperSTTTest COMMAND whisper_stt_test)

# WhisperSTT VAD Integration test
add_executable(whisper_stt_vad_integration_test unit/test_whisper_stt_vad_integration.cpp ${TEST_SOURCES})
link_test_libraries(whisper_stt_vad_integration_test)
add_test(NAME WhisperSTTVADIntegrationTest COMMAND whisper_stt_vad_integration_test)

# WhisperSTT Error Recovery test
add_executable(whisper_stt_error_recovery_test unit/test_whisper_stt_error_recovery.cpp ${TEST_SOURCES})
link_test_libraries(whisper_stt_error_recovery_test)
add_test(NAME WhisperSTTErrorRecoveryTest COMMAND whisper_stt_error_recovery_test)

# WhisperSTT Streaming test
add_executable(whisper_stt_streaming_test unit/test_whisper_stt_streaming.cpp ${TEST_SOURCES})
link_test_libraries(whisper_stt_streaming_test)
add_test(NAME WhisperSTTStreamingTest COMMAND whisper_stt_streaming_test)

# STT Integration test (standalone)
add_executable(stt_integration_test integration/test_stt_integration.cpp ${TEST_SOURCES})
target_link_libraries(stt_integration_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(stt_integration_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(stt_integration_test ${LIBUV_LIBRARIES})
endif()

add_test(NAME STTIntegrationTest COMMAND stt_integration_test)

# StreamingTranscriber test (standalone)
add_executable(streaming_transcriber_test unit/test_streaming_transcriber.cpp ${TEST_SOURCES})
target_link_libraries(streaming_transcriber_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(streaming_transcriber_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(streaming_transcriber_test ${LIBUV_LIBRARIES})
endif()

add_test(NAME StreamingTranscriberTest COMMAND streaming_transcriber_test)

# Simple test runner (fallback when GTest is not available)
add_executable(simple_tests simple_test_runner.cpp ${TEST_SOURCES})
target_link_libraries(simple_tests Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(simple_tests ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(simple_tests ${LIBUV_LIBRARIES})
endif()

add_test(NAME SimpleTests COMMAND simple_tests)

# MarianTranslator test (standalone)
add_executable(marian_translator_test unit/test_marian_translator.cpp ${TEST_SOURCES})
target_link_libraries(marian_translator_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(marian_translator_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(marian_translator_test ${LIBUV_LIBRARIES})
endif()
if(MARIAN_AVAILABLE)
    target_link_libraries(marian_translator_test ${MARIAN_LIBRARIES})
endif()

add_test(NAME MarianTranslatorTest COMMAND marian_translator_test)

# MarianTranslator Multi-Language Pairs test (standalone)
add_executable(marian_multi_language_pairs_test unit/test_marian_multi_language_pairs.cpp ${TEST_SOURCES})
target_link_libraries(marian_multi_language_pairs_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(marian_multi_language_pairs_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(marian_multi_language_pairs_test ${LIBUV_LIBRARIES})
endif()
if(MARIAN_AVAILABLE)
    target_link_libraries(marian_multi_language_pairs_test ${MARIAN_LIBRARIES})
endif()

add_test(NAME MarianMultiLanguagePairsTest COMMAND marian_multi_language_pairs_test)

# Multi-Language Pipeline Integration test (standalone)
add_executable(multi_language_pipeline_test integration/test_multi_language_pipeline.cpp ${TEST_SOURCES})
target_link_libraries(multi_language_pipeline_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(multi_language_pipeline_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(multi_language_pipeline_test ${LIBUV_LIBRARIES})
endif()
if(MARIAN_AVAILABLE)
    target_link_libraries(multi_language_pipeline_test ${MARIAN_LIBRARIES})
endif()

add_test(NAME MultiLanguagePipelineTest COMMAND multi_language_pipeline_test)

# ModelManager test (standalone)
add_executable(model_manager_test unit/test_model_manager.cpp ${TEST_SOURCES})
target_link_libraries(model_manager_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(model_manager_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(model_manager_test ${LIBUV_LIBRARIES})
endif()
if(MARIAN_AVAILABLE)
    target_link_libraries(model_manager_test ${MARIAN_LIBRARIES})
endif()

add_test(NAME ModelManagerTest COMMAND model_manager_test)

# CoquiTTS test (standalone)
add_executable(coqui_tts_test unit/test_coqui_tts.cpp ${TEST_SOURCES})
target_link_libraries(coqui_tts_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(coqui_tts_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(coqui_tts_test ${LIBUV_LIBRARIES})
endif()

add_test(NAME CoquiTTSTest COMMAND coqui_tts_test)

# VoiceManager test (standalone)
add_executable(voice_manager_test unit/test_voice_manager.cpp ${TEST_SOURCES})
target_link_libraries(voice_manager_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(voice_manager_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(voice_manager_test ${LIBUV_LIBRARIES})
endif()

add_test(NAME VoiceManagerTest COMMAND voice_manager_test)

if(GTest_FOUND)
    # Unit tests
    file(GLOB_RECURSE UNIT_TEST_SOURCES "unit/*.cpp")
    if(UNIT_TEST_SOURCES)
        add_executable(unit_tests ${UNIT_TEST_SOURCES} ${TEST_SOURCES})
        target_link_libraries(unit_tests 
            GTest::gtest 
            GTest::gtest_main
            Threads::Threads
        )
        
        # Link optional libraries if available
        if(ZLIB_FOUND)
            target_link_libraries(unit_tests ${ZLIB_LIBRARIES})
        endif()
        if(LIBUV_FOUND)
            target_link_libraries(unit_tests ${LIBUV_LIBRARIES})
        endif()
        if(MARIAN_AVAILABLE)
            target_link_libraries(unit_tests ${MARIAN_LIBRARIES})
        endif()
        add_test(NAME UnitTests COMMAND unit_tests)
    endif()
    
    # Integration tests
    file(GLOB_RECURSE INTEGRATION_TEST_SOURCES "integration/*.cpp")
    if(INTEGRATION_TEST_SOURCES)
        add_executable(integration_tests ${INTEGRATION_TEST_SOURCES} ${TEST_SOURCES})
        target_link_libraries(integration_tests 
            GTest::gtest 
            GTest::gtest_main
            Threads::Threads
        )
        
        # Link optional libraries if available
        if(ZLIB_FOUND)
            target_link_libraries(integration_tests ${ZLIB_LIBRARIES})
        endif()
        if(LIBUV_FOUND)
            target_link_libraries(integration_tests ${LIBUV_LIBRARIES})
        endif()
        if(MARIAN_AVAILABLE)
            target_link_libraries(integration_tests ${MARIAN_LIBRARIES})
        endif()
        add_test(NAME IntegrationTests COMMAND integration_tests)
    endif()
else()
    message(WARNING "GTest not found. Using simple test runner instead.")
endif()

# TaskQueue test (standalone)
add_executable(task_queue_test unit/test_task_queue_simple.cpp ${TEST_SOURCES})
target_link_libraries(task_queue_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(task_queue_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(task_queue_test ${LIBUV_LIBRARIES})
endif()
if(MARIAN_AVAILABLE)
    target_link_libraries(task_queue_test ${MARIAN_LIBRARIES})
endif()

add_test(NAME TaskQueueTest COMMAND task_queue_test)

# WebSocket Message Protocol test (standalone)
add_executable(websocket_message_protocol_test unit/test_websocket_message_protocol.cpp ${TEST_SOURCES})
target_link_libraries(websocket_message_protocol_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(websocket_message_protocol_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(websocket_message_protocol_test ${LIBUV_LIBRARIES})
endif()

add_test(NAME WebSocketMessageProtocolTest COMMAND websocket_message_protocol_test)

# Audio Buffer test (standalone)
add_executable(audio_buffer_test unit/test_audio_buffer.cpp ${TEST_SOURCES})
target_link_libraries(audio_buffer_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(audio_buffer_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(audio_buffer_test ${LIBUV_LIBRARIES})
endif()

add_test(NAME AudioBufferTest COMMAND audio_buffer_test)

# End-to-End Conversation test (standalone)
add_executable(end_to_end_conversation_test integration/test_end_to_end_conversation.cpp ${TEST_SOURCES})
target_link_libraries(end_to_end_conversation_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(end_to_end_conversation_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(end_to_end_conversation_test ${LIBUV_LIBRARIES})
endif()
if(MARIAN_AVAILABLE)
    target_link_libraries(end_to_end_conversation_test ${MARIAN_LIBRARIES})
endif()

add_test(NAME EndToEndConversationTest COMMAND end_to_end_conversation_test)

# MT End-to-End Integration test (standalone)
add_executable(mt_end_to_end_integration_test integration/test_mt_end_to_end_integration.cpp ${TEST_SOURCES})
link_test_libraries(mt_end_to_end_integration_test)
add_test(NAME MTEndToEndIntegrationTest COMMAND mt_end_to_end_integration_test)

# MT Error Propagation and Recovery test (standalone)
add_executable(mt_error_propagation_recovery_test integration/test_mt_error_propagation_recovery.cpp ${TEST_SOURCES})
link_test_libraries(mt_error_propagation_recovery_test)
add_test(NAME MTErrorPropagationRecoveryTest COMMAND mt_error_propagation_recovery_test)

# Load Testing (standalone)
add_executable(load_testing_test performance/load_testing.cpp ${TEST_SOURCES})
target_link_libraries(load_testing_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(load_testing_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(load_testing_test ${LIBUV_LIBRARIES})
endif()
if(MARIAN_AVAILABLE)
    target_link_libraries(load_testing_test ${MARIAN_LIBRARIES})
endif()

add_test(NAME LoadTestingTest COMMAND load_testing_test)

# STT Integration Tests (comprehensive)
add_executable(stt_integration_comprehensive_test integration/test_stt_integration_comprehensive.cpp ${TEST_SOURCES})
link_test_libraries(stt_integration_comprehensive_test)
add_test(NAME STTIntegrationComprehensiveTest COMMAND stt_integration_comprehensive_test)

# STT Performance Benchmark
add_executable(stt_performance_benchmark_test performance/stt_performance_benchmark.cpp ${TEST_SOURCES})
link_test_libraries(stt_performance_benchmark_test)
add_test(NAME STTPerformanceBenchmarkTest COMMAND stt_performance_benchmark_test)

# STT Load Testing
add_executable(stt_load_testing_test performance/stt_load_testing.cpp ${TEST_SOURCES})
link_test_libraries(stt_load_testing_test)
add_test(NAME STTLoadTestingTest COMMAND stt_load_testing_test)

# STT WebSocket Integration
add_executable(stt_websocket_integration_test integration/test_stt_websocket_integration.cpp ${TEST_SOURCES})
link_test_libraries(stt_websocket_integration_test)
add_test(NAME STTWebSocketIntegrationTest COMMAND stt_websocket_integration_test)

# Test Data Generator (standalone)
add_executable(test_data_generator_test fixtures/test_data_generator.cpp ${TEST_SOURCES})
target_link_libraries(test_data_generator_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(test_data_generator_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(test_data_generator_test ${LIBUV_LIBRARIES})
endif()

add_test(NAME TestDataGeneratorTest COMMAND test_data_generator_test)

# UtteranceManager test (standalone)
add_executable(utterance_manager_test unit/test_utterance_manager_simple.cpp ${TEST_SOURCES})
target_link_libraries(utterance_manager_test Threads::Threads)

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(utterance_manager_test ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(utterance_manager_test ${LIBUV_LIBRARIES})
endif()
if(MARIAN_AVAILABLE)
    target_link_libraries(utterance_manager_test ${MARIAN_LIBRARIES})
endif()

add_test(NAME UtteranceManagerTest COMMAND utterance_manager_test)

# GPU Acceleration Benchmark (if GTest is available)
if(GTest_FOUND)
    add_executable(gpu_acceleration_benchmark performance/gpu_acceleration_benchmark.cpp ${TEST_SOURCES})
    target_link_libraries(gpu_acceleration_benchmark 
        GTest::gtest 
        GTest::gtest_main
        Threads::Threads
    )
    
    # Link CUDA libraries if available
    if(CUDA_AVAILABLE)
        target_link_libraries(gpu_acceleration_benchmark 
            CUDA::cudart
            CUDA::cuda_driver
        )
        
        if(CUBLAS_LIBRARY)
            target_link_libraries(gpu_acceleration_benchmark CUDA::cublas)
        endif()
        
        if(CUDNN_LIBRARY)
            target_link_libraries(gpu_acceleration_benchmark ${CUDNN_LIBRARY})
        endif()
    endif()
    
    # Link optional libraries if available
    if(ZLIB_FOUND)
        target_link_libraries(gpu_acceleration_benchmark ${ZLIB_LIBRARIES})
    endif()
    if(LIBUV_FOUND)
        target_link_libraries(gpu_acceleration_benchmark ${LIBUV_LIBRARIES})
    endif()
    if(MARIAN_AVAILABLE)
        target_link_libraries(gpu_acceleration_benchmark ${MARIAN_LIBRARIES})
    endif()
    
    add_test(NAME GPUAccelerationBenchmark COMMAND gpu_acceleration_benchmark)
    
    # Latency Benchmark
    add_executable(latency_benchmark performance/latency_benchmark.cpp ${TEST_SOURCES})
    target_link_libraries(latency_benchmark 
        GTest::gtest 
        GTest::gtest_main
        Threads::Threads
    )
    
    # Link CUDA libraries if available
    if(CUDA_AVAILABLE)
        target_link_libraries(latency_benchmark 
            CUDA::cudart
            CUDA::cuda_driver
        )
        
        if(CUBLAS_LIBRARY)
            target_link_libraries(latency_benchmark CUDA::cublas)
        endif()
        
        if(CUDNN_LIBRARY)
            target_link_libraries(latency_benchmark ${CUDNN_LIBRARY})
        endif()
    endif()
    
    # Link optional libraries if available
    if(ZLIB_FOUND)
        target_link_libraries(latency_benchmark ${ZLIB_LIBRARIES})
    endif()
    if(LIBUV_FOUND)
        target_link_libraries(latency_benchmark ${LIBUV_LIBRARIES})
    endif()
    if(MARIAN_AVAILABLE)
        target_link_libraries(latency_benchmark ${MARIAN_LIBRARIES})
    endif()
    
    add_test(NAME LatencyBenchmark COMMAND latency_benchmark)
    
    # MT Performance Benchmark
    add_executable(mt_performance_benchmark performance/test_mt_performance_benchmark.cpp ${TEST_SOURCES})
    target_link_libraries(mt_performance_benchmark 
        GTest::gtest 
        GTest::gtest_main
        Threads::Threads
    )
    
    # Link CUDA libraries if available
    if(CUDA_AVAILABLE)
        target_link_libraries(mt_performance_benchmark 
            CUDA::cudart
            CUDA::cuda_driver
        )
        
        if(CUBLAS_LIBRARY)
            target_link_libraries(mt_performance_benchmark CUDA::cublas)
        endif()
        
        if(CUDNN_LIBRARY)
            target_link_libraries(mt_performance_benchmark ${CUDNN_LIBRARY})
        endif()
    endif()
    
    # Link optional libraries if available
    if(ZLIB_FOUND)
        target_link_libraries(mt_performance_benchmark ${ZLIB_LIBRARIES})
    endif()
    if(LIBUV_FOUND)
        target_link_libraries(mt_performance_benchmark ${LIBUV_LIBRARIES})
    endif()
    if(MARIAN_AVAILABLE)
        target_link_libraries(mt_performance_benchmark ${MARIAN_LIBRARIES})
    endif()
    
    add_test(NAME MTPerformanceBenchmark COMMAND mt_performance_benchmark)
endif()