@echo off
setlocal enabledelayedexpansion

REM STT Unit Test Runner for Windows
REM This script runs all comprehensive STT unit tests

set BUILD_DIR=build
set TEST_TIMEOUT=300
set VERBOSE=false

REM Parse command line arguments
:parse_args
if "%~1"=="" goto start_tests
if "%~1"=="-v" set VERBOSE=true & shift & goto parse_args
if "%~1"=="--verbose" set VERBOSE=true & shift & goto parse_args
if "%~1"=="-t" set TEST_TIMEOUT=%~2 & shift & shift & goto parse_args
if "%~1"=="--timeout" set TEST_TIMEOUT=%~2 & shift & shift & goto parse_args
if "%~1"=="-b" set BUILD_DIR=%~2 & shift & shift & goto parse_args
if "%~1"=="--build-dir" set BUILD_DIR=%~2 & shift & shift & goto parse_args
if "%~1"=="-h" goto show_help
if "%~1"=="--help" goto show_help
echo Unknown option: %~1
exit /b 1

:show_help
echo Usage: %0 [OPTIONS]
echo Options:
echo   -v, --verbose     Enable verbose output
echo   -t, --timeout     Set test timeout in seconds (default: 300)
echo   -b, --build-dir   Set build directory (default: build)
echo   -h, --help        Show this help message
exit /b 0

:start_tests
echo Starting STT Unit Test Suite
echo Build directory: %BUILD_DIR%
echo Test timeout: %TEST_TIMEOUT%s
echo Verbose mode: %VERBOSE%
echo.

REM Check if build directory exists
if not exist "%BUILD_DIR%" (
    echo Build directory '%BUILD_DIR%' does not exist!
    echo Please build the project first:
    echo   mkdir %BUILD_DIR% ^&^& cd %BUILD_DIR%
    echo   cmake .. ^&^& cmake --build . --config Release
    exit /b 1
)

REM Change to build directory
cd "%BUILD_DIR%"

REM Initialize counters
set total_tests=0
set passed_tests=0
set failed_tests=0
set failed_test_names=

REM Define and run tests
call :run_test "WhisperSTT Core Tests" "whisper_stt_test.exe"
call :run_test "WhisperSTT VAD Integration Tests" "whisper_stt_vad_integration_test.exe"
call :run_test "WhisperSTT Error Recovery Tests" "whisper_stt_error_recovery_test.exe"
call :run_test "WhisperSTT Streaming Tests" "whisper_stt_streaming_test.exe"

REM Print summary
echo =========================================
echo STT Unit Test Suite Summary
echo =========================================
echo Total tests: !total_tests!
echo Passed: !passed_tests!
echo Failed: !failed_tests!

if !failed_tests! gtr 0 (
    echo Failed tests:
    echo !failed_test_names!
)

REM Cleanup log files if not verbose
if "%VERBOSE%"=="false" (
    del /q *_output.log 2>nul
)

REM Exit with appropriate code
if !failed_tests! equ 0 (
    echo All STT unit tests passed!
    exit /b 0
) else (
    echo Some STT unit tests failed!
    exit /b 1
)

:run_test
set test_name=%~1
set test_executable=%~2

echo Running %test_name%...

REM Check if test executable exists
if not exist "%test_executable%" (
    echo Skipping %test_name% - executable not found: %test_executable%
    goto :eof
)

set /a total_tests+=1

REM Run the test with timeout
if "%VERBOSE%"=="true" (
    timeout /t %TEST_TIMEOUT% /nobreak >nul 2>&1 & "%test_executable%" --gtest_output=xml:%test_name%_results.xml
) else (
    timeout /t %TEST_TIMEOUT% /nobreak >nul 2>&1 & "%test_executable%" --gtest_output=xml:%test_name%_results.xml > "%test_name%_output.log" 2>&1
)

if !errorlevel! equ 0 (
    echo [32m✓ %test_name% PASSED[0m
    set /a passed_tests+=1
) else (
    echo [31m✗ %test_name% FAILED (exit code: !errorlevel!)[0m
    set /a failed_tests+=1
    set failed_test_names=!failed_test_names! - %test_name%
    if "%VERBOSE%"=="false" (
        echo Last lines of output:
        if exist "%test_name%_output.log" type "%test_name%_output.log" | more +10
    )
)

echo.
goto :eof