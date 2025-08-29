@echo off
echo Building and testing TTS integration...

REM Create build directory if it doesn't exist
if not exist build mkdir build
cd build

REM Configure with CMake
echo Configuring with CMake...
cmake .. -DCMAKE_BUILD_TYPE=Debug
if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed!
    exit /b 1
)

REM Build the project
echo Building project...
cmake --build . --config Debug --target RealTTSIntegrationTest
if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b 1
)

REM Run the integration test
echo Running TTS integration test...
.\bin\Debug\RealTTSIntegrationTest.exe
if %ERRORLEVEL% neq 0 (
    echo TTS integration test failed!
    exit /b 1
)

echo TTS integration test completed successfully!
cd ..