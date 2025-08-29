cmake_minimum_required(VERSION 3.16)
project(SpeechRNT VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable CUDA support if available
option(ENABLE_CUDA "Enable CUDA support for GPU acceleration" ON)
if(ENABLE_CUDA)
    enable_language(CUDA)
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        set(CUDA_AVAILABLE TRUE)
        add_definitions(-DCUDA_AVAILABLE)
        message(STATUS "CUDA found: ${CUDAToolkit_VERSION}")
        
        # Set CUDA architectures (adjust based on target GPUs)
        set(CMAKE_CUDA_ARCHITECTURES "75;80;86")
        
        # Find cuBLAS and cuDNN
        find_library(CUBLAS_LIBRARY cublas HINTS ${CUDAToolkit_LIBRARY_DIR})
        find_library(CUDNN_LIBRARY cudnn HINTS ${CUDAToolkit_LIBRARY_DIR})
        
        if(CUBLAS_LIBRARY)
            message(STATUS "cuBLAS found: ${CUBLAS_LIBRARY}")
        endif()
        
        if(CUDNN_LIBRARY)
            message(STATUS "cuDNN found: ${CUDNN_LIBRARY}")
        endif()
    else()
        set(CUDA_AVAILABLE FALSE)
        message(WARNING "CUDA requested but not found. Building without GPU acceleration.")
    endif()
else()
    set(CUDA_AVAILABLE FALSE)
    message(STATUS "CUDA support disabled")
endif()

# Build configuration
set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type")
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Define WHISPER_VERSION for whisper.cpp
add_definitions(-DWHISPER_VERSION="1.5.4")
include_directories(${CMAKE_SOURCE_DIR}/third_party)

# Find required packages
find_package(Threads REQUIRED)

# Find nlohmann/json for configuration management
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    # Try to find it as a header-only library
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
        PATHS 
            /usr/include
            /usr/local/include
            ${CMAKE_SOURCE_DIR}/third_party/nlohmann
    )
    if(NLOHMANN_JSON_INCLUDE_DIR)
        include_directories(${NLOHMANN_JSON_INCLUDE_DIR})
        set(NLOHMANN_JSON_AVAILABLE TRUE)
        message(STATUS "nlohmann/json found: ${NLOHMANN_JSON_INCLUDE_DIR}")
    else()
        set(NLOHMANN_JSON_AVAILABLE FALSE)
        message(WARNING "nlohmann/json not found. Advanced STT configuration will use basic parsing.")
    endif()
else()
    set(NLOHMANN_JSON_AVAILABLE TRUE)
    message(STATUS "nlohmann/json found via package manager")
endif()

# Try to find optional dependencies
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ZLIB zlib)
    pkg_check_modules(LIBUV libuv)
endif()

# Add uWebSockets (header-only for now)
set(UWEBSOCKETS_DIR ${CMAKE_SOURCE_DIR}/third_party/uWebSockets)
include_directories(${UWEBSOCKETS_DIR}/src)

# Add whisper.cpp
set(WHISPER_DIR ${CMAKE_SOURCE_DIR}/third_party/whisper.cpp)
if(EXISTS ${WHISPER_DIR})
    # Include whisper.cpp headers
    include_directories(${WHISPER_DIR}/include)
    include_directories(${WHISPER_DIR}/ggml/include)
    
    # Add whisper.cpp source files
    set(WHISPER_SOURCES 
        "${WHISPER_DIR}/src/whisper.cpp"
    )
    
    # Add ggml sources
    set(GGML_SOURCES 
        "${WHISPER_DIR}/ggml/src/ggml.c"
        "${WHISPER_DIR}/ggml/src/ggml-alloc.c"
        "${WHISPER_DIR}/ggml/src/ggml-backend.cpp"
        "${WHISPER_DIR}/ggml/src/ggml-quants.c"
        "${WHISPER_DIR}/ggml/src/ggml-backend-reg.cpp"
        "${WHISPER_DIR}/ggml/src/ggml-opt.cpp"
        "${WHISPER_DIR}/ggml/src/ggml-threading.cpp"
        "${WHISPER_DIR}/ggml/src/gguf.cpp"
    )
    
    # Add CUDA sources if available
    if(CUDA_AVAILABLE)
        file(GLOB GGML_CUDA_SOURCES "${WHISPER_DIR}/ggml/src/ggml-cuda.cu")
        if(GGML_CUDA_SOURCES)
            list(APPEND GGML_SOURCES ${GGML_CUDA_SOURCES})
            add_definitions(-DGGML_USE_CUDA)
        endif()
    endif()
    
    list(APPEND WHISPER_SOURCES ${GGML_SOURCES})
    set(WHISPER_AVAILABLE TRUE)
    add_definitions(-DWHISPER_AVAILABLE)
    message(STATUS "whisper.cpp found and configured")
else()
    set(WHISPER_AVAILABLE FALSE)
    message(WARNING "whisper.cpp not found. Run setup_whisper.bat to install it.")
endif()

# Add silero-vad
set(SILERO_DIR ${CMAKE_SOURCE_DIR}/third_party/silero-vad)
set(SILERO_MODEL_PATH ${CMAKE_SOURCE_DIR}/data/silero/silero_vad.onnx)

# Try to find ONNX Runtime
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    PATHS 
        /usr/include/onnxruntime
        /usr/local/include/onnxruntime
        ${CMAKE_SOURCE_DIR}/third_party/onnxruntime/include
    PATH_SUFFIXES core/session
)

find_library(ONNXRUNTIME_LIB onnxruntime
    PATHS 
        /usr/lib
        /usr/local/lib
        ${CMAKE_SOURCE_DIR}/third_party/onnxruntime/lib
)

if(EXISTS ${SILERO_MODEL_PATH} AND ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB)
    set(SILERO_VAD_AVAILABLE TRUE)
    include_directories(${ONNXRUNTIME_INCLUDE_DIR})
    add_definitions(-DSILERO_VAD_AVAILABLE)
    add_definitions(-DSILERO_MODEL_PATH="${SILERO_MODEL_PATH}")
    message(STATUS "silero-vad found and configured")
    message(STATUS "ONNX Runtime include: ${ONNXRUNTIME_INCLUDE_DIR}")
    message(STATUS "ONNX Runtime library: ${ONNXRUNTIME_LIB}")
else()
    set(SILERO_VAD_AVAILABLE FALSE)
    if(NOT EXISTS ${SILERO_MODEL_PATH})
        message(WARNING "silero-vad model not found. Run setup_silero_vad.sh/bat to install it.")
    endif()
    if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIB)
        message(WARNING "ONNX Runtime not found. VAD will use energy-based fallback.")
    endif()
endif()

# Add Marian NMT
set(MARIAN_DIR ${CMAKE_SOURCE_DIR}/third_party/marian-nmt)
if(EXISTS ${MARIAN_DIR})
    include_directories(${MARIAN_DIR}/src)
    include_directories(${MARIAN_DIR}/build/local/include)
    
    # Find Marian libraries
    find_library(MARIAN_LIB marian PATHS ${MARIAN_DIR}/build/src NO_DEFAULT_PATH)
    find_library(MARIAN_COMMON_LIB marian_common PATHS ${MARIAN_DIR}/build/src/common NO_DEFAULT_PATH)
    
    if(MARIAN_LIB AND MARIAN_COMMON_LIB)
        set(MARIAN_AVAILABLE TRUE)
        set(MARIAN_LIBRARIES ${MARIAN_LIB} ${MARIAN_COMMON_LIB})
        add_definitions(-DMARIAN_AVAILABLE)
    else()
        set(MARIAN_AVAILABLE FALSE)
        message(WARNING "Marian libraries not found. Translation will use mock implementation.")
    endif()
else()
    set(MARIAN_AVAILABLE FALSE)
    message(WARNING "Marian NMT not found. Run setup_marian.sh to install it.")
endif()

# Source files
file(GLOB_RECURSE SOURCES 
    "src/core/*.cpp"
    "src/audio/*.cpp"
    "src/stt/*.cpp"
    "src/stt/advanced/*.cpp"
    "src/mt/*.cpp"
    "src/tts/*.cpp"
    "src/models/*.cpp"
    "src/utils/*.cpp"
)

# Add CUDA sources if available
if(CUDA_AVAILABLE)
    file(GLOB_RECURSE CUDA_SOURCES "src/**/*.cu")
    if(CUDA_SOURCES)
        list(APPEND SOURCES ${CUDA_SOURCES})
    endif()
endif()

# Add whisper.cpp sources if available
if(WHISPER_AVAILABLE)
    list(APPEND SOURCES ${WHISPER_SOURCES})
    add_definitions(-DWHISPER_AVAILABLE)
endif()

# Main executable
add_executable(SpeechRNT src/main.cpp ${SOURCES})

# Audio demo executable
add_executable(AudioDemo src/audio_demo.cpp ${SOURCES})

# Audio buffer demo executable
add_executable(AudioBufferDemo src/audio_buffer_demo.cpp ${SOURCES})

# GPU test executable
add_executable(GPUTest src/gpu_test.cpp ${SOURCES})

# Streaming test executable
add_executable(StreamingTest test_streaming.cpp ${SOURCES})

# STT Performance Demo executable
add_executable(STTPerformanceDemo src/stt_performance_demo.cpp ${SOURCES})

# Audio Utils Demo executable
add_executable(AudioUtilsDemo src/audio_utils_demo.cpp ${SOURCES})

# STT Translation Integration Demo executable
add_executable(STTTranslationDemo src/stt_translation_integration_demo.cpp ${SOURCES})

# STT Translation Integration Test executable
add_executable(TranslationIntegrationTest src/test_translation_integration.cpp ${SOURCES})

# Performance Optimization Demo executable
add_executable(PerformanceOptimizationDemo src/performance_optimization_demo.cpp ${SOURCES})

# Advanced STT Test executable
add_executable(AdvancedSTTTest src/advanced_stt_test.cpp ${SOURCES})

# Real-time Audio Analyzer Test executable
add_executable(RealTimeAudioAnalyzerTest src/test_realtime_audio_analyzer.cpp ${SOURCES})

# Advanced Streaming Test executable
add_executable(AdvancedStreamingTest src/test_advanced_streaming.cpp ${SOURCES})

# Emotion Detection Test executable
add_executable(EmotionDetectionTest src/test_emotion_detection.cpp ${SOURCES})

# Simple Emotion Test executable
add_executable(SimpleEmotionTest src/simple_emotion_test.cpp ${SOURCES})

# Advanced Model Management Test executable
add_executable(AdvancedModelManagementTest src/test_advanced_model_management.cpp ${SOURCES})

# External Service Integration Test executable
add_executable(ExternalServiceIntegrationTest src/test_external_service_integration.cpp ${SOURCES})

# Real STT Integration Test executable
add_executable(RealSTTIntegrationTest src/test_real_stt_integration.cpp ${SOURCES})

# Real MT Integration Test executable
add_executable(RealMTIntegrationTest src/test_real_mt_integration.cpp ${SOURCES})

# MT Configuration Example executable
add_executable(MTConfigExample examples/mt_config_example.cpp ${SOURCES})

# GPU Accelerator Example executable
add_executable(GPUAcceleratorExample examples/gpu_accelerator_example.cpp ${SOURCES})

# Advanced Debugging Example executable
add_executable(AdvancedDebuggingExample examples/advanced_debugging_example.cpp ${SOURCES})

# STT Integration Example executable
add_executable(STTIntegrationExample examples/stt_integration_example.cpp ${SOURCES})

# MT Integration Example executable
add_executable(MTIntegrationExample examples/mt_integration_example.cpp ${SOURCES})

# Real TTS Integration Test executable
add_executable(RealTTSIntegrationTest src/test_real_tts_integration.cpp ${SOURCES})

# TTS Integration Example executable
add_executable(TTSIntegrationExample examples/tts_integration_example.cpp ${SOURCES})

# Link libraries
target_link_libraries(SpeechRNT 
    Threads::Threads
)

target_link_libraries(AudioDemo 
    Threads::Threads
)

target_link_libraries(AudioBufferDemo 
    Threads::Threads
)

target_link_libraries(GPUTest 
    Threads::Threads
)

target_link_libraries(StreamingTest 
    Threads::Threads
)

target_link_libraries(STTPerformanceDemo 
    Threads::Threads
)

target_link_libraries(AudioUtilsDemo 
    Threads::Threads
)

target_link_libraries(STTTranslationDemo 
    Threads::Threads
)

target_link_libraries(TranslationIntegrationTest 
    Threads::Threads
)

target_link_libraries(PerformanceOptimizationDemo 
    Threads::Threads
)

target_link_libraries(MTConfigExample 
    Threads::Threads
)

target_link_libraries(GPUAcceleratorExample 
    Threads::Threads
)

target_link_libraries(AdvancedDebuggingExample 
    Threads::Threads
)

target_link_libraries(STTIntegrationExample 
    Threads::Threads
)

target_link_libraries(MTIntegrationExample 
    Threads::Threads
)

target_link_libraries(AdvancedSTTTest 
    Threads::Threads
)

target_link_libraries(RealTimeAudioAnalyzerTest 
    Threads::Threads
)

target_link_libraries(AdvancedStreamingTest 
    Threads::Threads
)

target_link_libraries(EmotionDetectionTest 
    Threads::Threads
)

target_link_libraries(SimpleEmotionTest 
    Threads::Threads
)

target_link_libraries(AdvancedModelManagementTest 
    Threads::Threads
)

target_link_libraries(ExternalServiceIntegrationTest 
    Threads::Threads
)

target_link_libraries(RealSTTIntegrationTest 
    Threads::Threads
)

target_link_libraries(RealMTIntegrationTest 
    Threads::Threads
)

target_link_libraries(RealTTSIntegrationTest 
    Threads::Threads
)

target_link_libraries(TTSIntegrationExample 
    Threads::Threads
)

# Link CUDA libraries if available
if(CUDA_AVAILABLE)
    target_link_libraries(SpeechRNT 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(AudioDemo 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(AudioBufferDemo 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(GPUTest 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(StreamingTest 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(STTPerformanceDemo 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(AudioUtilsDemo 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(STTTranslationDemo 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(TranslationIntegrationTest 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(PerformanceOptimizationDemo 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(MTConfigExample 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(GPUAcceleratorExample 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(AdvancedDebuggingExample 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(STTIntegrationExample 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(MTIntegrationExample 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(AdvancedSTTTest 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(RealTimeAudioAnalyzerTest 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(AdvancedStreamingTest 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(EmotionDetectionTest 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(AdvancedModelManagementTest 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(ExternalServiceIntegrationTest 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(RealSTTIntegrationTest 
        CUDA::cudart
        CUDA::cuda_driver
    )
    target_link_libraries(RealMTIntegrationTest 
        CUDA::cudart
        CUDA::cuda_driver
    )
    
    # Link cuBLAS if found
    if(CUBLAS_LIBRARY)
        target_link_libraries(SpeechRNT CUDA::cublas)
        target_link_libraries(AudioDemo CUDA::cublas)
        target_link_libraries(AudioBufferDemo CUDA::cublas)
        target_link_libraries(GPUTest CUDA::cublas)
        target_link_libraries(StreamingTest CUDA::cublas)
        target_link_libraries(STTPerformanceDemo CUDA::cublas)
        target_link_libraries(AudioUtilsDemo CUDA::cublas)
        target_link_libraries(STTTranslationDemo CUDA::cublas)
        target_link_libraries(TranslationIntegrationTest CUDA::cublas)
        target_link_libraries(PerformanceOptimizationDemo CUDA::cublas)
        target_link_libraries(MTConfigExample CUDA::cublas)
        target_link_libraries(GPUAcceleratorExample CUDA::cublas)
        target_link_libraries(AdvancedDebuggingExample CUDA::cublas)
        target_link_libraries(STTIntegrationExample CUDA::cublas)
        target_link_libraries(MTIntegrationExample CUDA::cublas)
        target_link_libraries(AdvancedSTTTest CUDA::cublas)
        target_link_libraries(RealTimeAudioAnalyzerTest CUDA::cublas)
        target_link_libraries(AdvancedStreamingTest CUDA::cublas)
        target_link_libraries(EmotionDetectionTest CUDA::cublas)
        target_link_libraries(AdvancedModelManagementTest CUDA::cublas)
        target_link_libraries(ExternalServiceIntegrationTest CUDA::cublas)
        target_link_libraries(RealSTTIntegrationTest CUDA::cublas)
        target_link_libraries(RealMTIntegrationTest CUDA::cublas)
    endif()
    
    # Link cuDNN if found
    if(CUDNN_LIBRARY)
        target_link_libraries(SpeechRNT ${CUDNN_LIBRARY})
        target_link_libraries(AudioDemo ${CUDNN_LIBRARY})
        target_link_libraries(AudioBufferDemo ${CUDNN_LIBRARY})
        target_link_libraries(GPUTest ${CUDNN_LIBRARY})
        target_link_libraries(StreamingTest ${CUDNN_LIBRARY})
        target_link_libraries(STTPerformanceDemo ${CUDNN_LIBRARY})
        target_link_libraries(AudioUtilsDemo ${CUDNN_LIBRARY})
        target_link_libraries(STTTranslationDemo ${CUDNN_LIBRARY})
        target_link_libraries(TranslationIntegrationTest ${CUDNN_LIBRARY})
        target_link_libraries(PerformanceOptimizationDemo ${CUDNN_LIBRARY})
        target_link_libraries(MTConfigExample ${CUDNN_LIBRARY})
        target_link_libraries(GPUAcceleratorExample ${CUDNN_LIBRARY})
        target_link_libraries(AdvancedDebuggingExample ${CUDNN_LIBRARY})
        target_link_libraries(STTIntegrationExample ${CUDNN_LIBRARY})
        target_link_libraries(MTIntegrationExample ${CUDNN_LIBRARY})
        target_link_libraries(AdvancedSTTTest ${CUDNN_LIBRARY})
        target_link_libraries(RealTimeAudioAnalyzerTest ${CUDNN_LIBRARY})
        target_link_libraries(AdvancedStreamingTest ${CUDNN_LIBRARY})
        target_link_libraries(EmotionDetectionTest ${CUDNN_LIBRARY})
        target_link_libraries(AdvancedModelManagementTest ${CUDNN_LIBRARY})
        target_link_libraries(ExternalServiceIntegrationTest ${CUDNN_LIBRARY})
        target_link_libraries(RealSTTIntegrationTest ${CUDNN_LIBRARY})
        target_link_libraries(RealMTIntegrationTest ${CUDNN_LIBRARY})
    endif()
endif()

# Link silero-vad libraries if available
if(SILERO_VAD_AVAILABLE)
    target_link_libraries(SpeechRNT ${ONNXRUNTIME_LIB})
    target_link_libraries(AudioDemo ${ONNXRUNTIME_LIB})
    target_link_libraries(AudioBufferDemo ${ONNXRUNTIME_LIB})
    target_link_libraries(GPUTest ${ONNXRUNTIME_LIB})
    target_link_libraries(StreamingTest ${ONNXRUNTIME_LIB})
    target_link_libraries(STTPerformanceDemo ${ONNXRUNTIME_LIB})
    target_link_libraries(AudioUtilsDemo ${ONNXRUNTIME_LIB})
    target_link_libraries(STTTranslationDemo ${ONNXRUNTIME_LIB})
    target_link_libraries(TranslationIntegrationTest ${ONNXRUNTIME_LIB})
    target_link_libraries(PerformanceOptimizationDemo ${ONNXRUNTIME_LIB})
    target_link_libraries(MTConfigExample ${ONNXRUNTIME_LIB})
    target_link_libraries(GPUAcceleratorExample ${ONNXRUNTIME_LIB})
    target_link_libraries(AdvancedDebuggingExample ${ONNXRUNTIME_LIB})
    target_link_libraries(STTIntegrationExample ${ONNXRUNTIME_LIB})
    target_link_libraries(MTIntegrationExample ${ONNXRUNTIME_LIB})
    target_link_libraries(AdvancedSTTTest ${ONNXRUNTIME_LIB})
    target_link_libraries(RealTimeAudioAnalyzerTest ${ONNXRUNTIME_LIB})
    target_link_libraries(AdvancedStreamingTest ${ONNXRUNTIME_LIB})
    target_link_libraries(EmotionDetectionTest ${ONNXRUNTIME_LIB})
    target_link_libraries(AdvancedModelManagementTest ${ONNXRUNTIME_LIB})
    target_link_libraries(ExternalServiceIntegrationTest ${ONNXRUNTIME_LIB})
    target_link_libraries(RealSTTIntegrationTest ${ONNXRUNTIME_LIB})
    target_link_libraries(RealMTIntegrationTest ${ONNXRUNTIME_LIB})
endif()

# Link Marian libraries if available
if(MARIAN_AVAILABLE)
    target_link_libraries(SpeechRNT ${MARIAN_LIBRARIES})
    target_link_libraries(AudioDemo ${MARIAN_LIBRARIES})
    target_link_libraries(AudioBufferDemo ${MARIAN_LIBRARIES})
    target_link_libraries(GPUTest ${MARIAN_LIBRARIES})
    target_link_libraries(StreamingTest ${MARIAN_LIBRARIES})
    target_link_libraries(STTPerformanceDemo ${MARIAN_LIBRARIES})
    target_link_libraries(AudioUtilsDemo ${MARIAN_LIBRARIES})
    target_link_libraries(STTTranslationDemo ${MARIAN_LIBRARIES})
    target_link_libraries(TranslationIntegrationTest ${MARIAN_LIBRARIES})
    target_link_libraries(PerformanceOptimizationDemo ${MARIAN_LIBRARIES})
    target_link_libraries(MTConfigExample ${MARIAN_LIBRARIES})
    target_link_libraries(GPUAcceleratorExample ${MARIAN_LIBRARIES})
    target_link_libraries(AdvancedSTTTest ${MARIAN_LIBRARIES})
    target_link_libraries(RealTimeAudioAnalyzerTest ${MARIAN_LIBRARIES})
    target_link_libraries(AdvancedStreamingTest ${MARIAN_LIBRARIES})
    target_link_libraries(EmotionDetectionTest ${MARIAN_LIBRARIES})
    target_link_libraries(AdvancedModelManagementTest ${MARIAN_LIBRARIES})
    target_link_libraries(ExternalServiceIntegrationTest ${MARIAN_LIBRARIES})
    target_link_libraries(RealSTTIntegrationTest ${MARIAN_LIBRARIES})
    target_link_libraries(RealMTIntegrationTest ${MARIAN_LIBRARIES})
    target_link_libraries(AdvancedDebuggingExample ${MARIAN_LIBRARIES})
    target_link_libraries(STTIntegrationExample ${MARIAN_LIBRARIES})
    target_link_libraries(MTIntegrationExample ${MARIAN_LIBRARIES})
endif()

# Link optional libraries if available
if(ZLIB_FOUND)
    target_link_libraries(SpeechRNT ${ZLIB_LIBRARIES})
    target_link_libraries(AudioDemo ${ZLIB_LIBRARIES})
    target_link_libraries(AudioBufferDemo ${ZLIB_LIBRARIES})
    target_link_libraries(GPUTest ${ZLIB_LIBRARIES})
    target_link_libraries(StreamingTest ${ZLIB_LIBRARIES})
    target_link_libraries(STTPerformanceDemo ${ZLIB_LIBRARIES})
    target_link_libraries(AudioUtilsDemo ${ZLIB_LIBRARIES})
    target_link_libraries(STTTranslationDemo ${ZLIB_LIBRARIES})
    target_link_libraries(TranslationIntegrationTest ${ZLIB_LIBRARIES})
    target_link_libraries(PerformanceOptimizationDemo ${ZLIB_LIBRARIES})
    target_link_libraries(MTConfigExample ${ZLIB_LIBRARIES})
    target_link_libraries(GPUAcceleratorExample ${ZLIB_LIBRARIES})
    target_link_libraries(AdvancedSTTTest ${ZLIB_LIBRARIES})
    target_link_libraries(RealTimeAudioAnalyzerTest ${ZLIB_LIBRARIES})
    target_link_libraries(AdvancedStreamingTest ${ZLIB_LIBRARIES})
    target_link_libraries(EmotionDetectionTest ${ZLIB_LIBRARIES})
    target_link_libraries(AdvancedModelManagementTest ${ZLIB_LIBRARIES})
    target_link_libraries(ExternalServiceIntegrationTest ${ZLIB_LIBRARIES})
    target_link_libraries(RealSTTIntegrationTest ${ZLIB_LIBRARIES})
    target_link_libraries(RealMTIntegrationTest ${ZLIB_LIBRARIES})
    target_link_libraries(AdvancedDebuggingExample ${ZLIB_LIBRARIES})
    target_link_libraries(STTIntegrationExample ${ZLIB_LIBRARIES})
    target_link_libraries(MTIntegrationExample ${ZLIB_LIBRARIES})
endif()
if(LIBUV_FOUND)
    target_link_libraries(SpeechRNT ${LIBUV_LIBRARIES})
    target_link_libraries(AudioDemo ${LIBUV_LIBRARIES})
    target_link_libraries(AudioBufferDemo ${LIBUV_LIBRARIES})
    target_link_libraries(GPUTest ${LIBUV_LIBRARIES})
    target_link_libraries(StreamingTest ${LIBUV_LIBRARIES})
    target_link_libraries(STTPerformanceDemo ${LIBUV_LIBRARIES})
    target_link_libraries(AudioUtilsDemo ${LIBUV_LIBRARIES})
    target_link_libraries(STTTranslationDemo ${LIBUV_LIBRARIES})
    target_link_libraries(TranslationIntegrationTest ${LIBUV_LIBRARIES})
    target_link_libraries(PerformanceOptimizationDemo ${LIBUV_LIBRARIES})
    target_link_libraries(MTConfigExample ${LIBUV_LIBRARIES})
    target_link_libraries(GPUAcceleratorExample ${LIBUV_LIBRARIES})
    target_link_libraries(AdvancedSTTTest ${LIBUV_LIBRARIES})
    target_link_libraries(RealTimeAudioAnalyzerTest ${LIBUV_LIBRARIES})
    target_link_libraries(AdvancedStreamingTest ${LIBUV_LIBRARIES})
    target_link_libraries(EmotionDetectionTest ${LIBUV_LIBRARIES})
    target_link_libraries(AdvancedModelManagementTest ${LIBUV_LIBRARIES})
    target_link_libraries(ExternalServiceIntegrationTest ${LIBUV_LIBRARIES})
    target_link_libraries(RealSTTIntegrationTest ${LIBUV_LIBRARIES})
    target_link_libraries(RealMTIntegrationTest ${LIBUV_LIBRARIES})
    target_link_libraries(AdvancedDebuggingExample ${LIBUV_LIBRARIES})
    target_link_libraries(STTIntegrationExample ${LIBUV_LIBRARIES})
    target_link_libraries(RealTTSIntegrationTest ${LIBUV_LIBRARIES})
    target_link_libraries(TTSIntegrationExample ${LIBUV_LIBRARIES})
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(SpeechRNT PRIVATE -Wall -Wextra -O2)
endif()

# Enable testing
enable_testing()
add_subdirectory(tests)