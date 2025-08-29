@echo off
setlocal enabledelayedexpansion

REM Comprehensive test runner for SpeechRNT project (Windows version)
REM This script runs all unit tests, integration tests, and performance tests

REM Configuration
set BACKEND_DIR=backend
set FRONTEND_DIR=frontend
set TEST_RESULTS_DIR=test_results
set COVERAGE_DIR=coverage
set PERFORMANCE_LOG=performance_results.log

REM Create results directory
if not exist "%TEST_RESULTS_DIR%" mkdir "%TEST_RESULTS_DIR%"
if not exist "%COVERAGE_DIR%" mkdir "%COVERAGE_DIR%"

echo ========================================
echo   SpeechRNT Comprehensive Test Suite   
echo ========================================
echo.

REM Function to run backend tests
:run_backend_tests
echo Running Backend Tests
echo ----------------------------------------

cd "%BACKEND_DIR%"

REM Create build directory if it doesn't exist
if not exist "build" mkdir build

cd build

REM Configure with CMake
echo Configuring backend build...
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTING=ON
if errorlevel 1 (
    echo ERROR: CMake configuration failed
    cd ..\..
    goto :error
)

REM Build the project
echo Building backend...
cmake --build . --config Debug
if errorlevel 1 (
    echo ERROR: Build failed
    cd ..\..
    goto :error
)

set BACKEND_TESTS_PASSED=0
set BACKEND_TESTS_FAILED=0

echo.
echo Running backend unit tests...

REM List of tests to run
set TESTS=simple_tests vad_integration_test vad_edge_cases_test whisper_stt_test stt_integration_test streaming_transcriber_test marian_translator_test model_manager_test coqui_tts_test voice_manager_test task_queue_test utterance_manager_test websocket_message_protocol_test audio_buffer_test

for %%t in (%TESTS%) do (
    if exist "Debug\%%t.exe" (
        echo   Running %%t...
        "Debug\%%t.exe" > "..\%TEST_RESULTS_DIR%\%%t_output.log" 2>&1
        if errorlevel 1 (
            echo     FAILED
            set /a BACKEND_TESTS_FAILED+=1
            echo     See %TEST_RESULTS_DIR%\%%t_output.log for details
        ) else (
            echo     PASSED
            set /a BACKEND_TESTS_PASSED+=1
        )
    ) else if exist "%%t.exe" (
        echo   Running %%t...
        "%%t.exe" > "..\%TEST_RESULTS_DIR%\%%t_output.log" 2>&1
        if errorlevel 1 (
            echo     FAILED
            set /a BACKEND_TESTS_FAILED+=1
            echo     See %TEST_RESULTS_DIR%\%%t_output.log for details
        ) else (
            echo     PASSED
            set /a BACKEND_TESTS_PASSED+=1
        )
    ) else (
        echo   Skipping %%t ^(executable not found^)
    )
)

echo.
echo Running backend integration tests...

set INTEGRATION_TESTS=end_to_end_conversation_test

for %%t in (%INTEGRATION_TESTS%) do (
    if exist "Debug\%%t.exe" (
        echo   Running %%t...
        "Debug\%%t.exe" > "..\%TEST_RESULTS_DIR%\%%t_output.log" 2>&1
        if errorlevel 1 (
            echo     FAILED
            set /a BACKEND_TESTS_FAILED+=1
            echo     See %TEST_RESULTS_DIR%\%%t_output.log for details
        ) else (
            echo     PASSED
            set /a BACKEND_TESTS_PASSED+=1
        )
    ) else if exist "%%t.exe" (
        echo   Running %%t...
        "%%t.exe" > "..\%TEST_RESULTS_DIR%\%%t_output.log" 2>&1
        if errorlevel 1 (
            echo     FAILED
            set /a BACKEND_TESTS_FAILED+=1
            echo     See %TEST_RESULTS_DIR%\%%t_output.log for details
        ) else (
            echo     PASSED
            set /a BACKEND_TESTS_PASSED+=1
        )
    ) else (
        echo   Skipping %%t ^(executable not found^)
    )
)

echo.
echo Running backend performance tests...

set PERFORMANCE_TESTS=gpu_acceleration_benchmark latency_benchmark load_testing_test

for %%t in (%PERFORMANCE_TESTS%) do (
    if exist "Debug\%%t.exe" (
        echo   Running %%t...
        "Debug\%%t.exe" > "..\%TEST_RESULTS_DIR%\%%t_output.log" 2>&1
        if errorlevel 1 (
            echo     FAILED
            set /a BACKEND_TESTS_FAILED+=1
            echo     See %TEST_RESULTS_DIR%\%%t_output.log for details
        ) else (
            echo     PASSED
            set /a BACKEND_TESTS_PASSED+=1
            
            REM Extract performance metrics
            findstr /C:"Results:" "..\%TEST_RESULTS_DIR%\%%t_output.log" > nul
            if not errorlevel 1 (
                echo Performance Results for %%t: >> "..\%TEST_RESULTS_DIR%\%PERFORMANCE_LOG%"
                findstr /A:"Results:" "..\%TEST_RESULTS_DIR%\%%t_output.log" >> "..\%TEST_RESULTS_DIR%\%PERFORMANCE_LOG%"
                echo. >> "..\%TEST_RESULTS_DIR%\%PERFORMANCE_LOG%"
            )
        )
    ) else if exist "%%t.exe" (
        echo   Running %%t...
        "%%t.exe" > "..\%TEST_RESULTS_DIR%\%%t_output.log" 2>&1
        if errorlevel 1 (
            echo     FAILED
            set /a BACKEND_TESTS_FAILED+=1
            echo     See %TEST_RESULTS_DIR%\%%t_output.log for details
        ) else (
            echo     PASSED
            set /a BACKEND_TESTS_PASSED+=1
        )
    ) else (
        echo   Skipping %%t ^(executable not found^)
    )
)

REM Run CTest if available
where ctest >nul 2>&1
if not errorlevel 1 (
    echo.
    echo Running CTest suite...
    ctest --output-on-failure > "..\%TEST_RESULTS_DIR%\ctest_output.log" 2>&1
    if errorlevel 1 (
        echo   CTest suite FAILED
        echo   See %TEST_RESULTS_DIR%\ctest_output.log for details
    ) else (
        echo   CTest suite PASSED
    )
)

cd ..\..

echo.
echo Backend Test Summary:
echo   Passed: %BACKEND_TESTS_PASSED%
echo   Failed: %BACKEND_TESTS_FAILED%

goto :run_frontend_tests

:run_frontend_tests
echo.
echo Running Frontend Tests
echo ----------------------------------------

cd "%FRONTEND_DIR%"

REM Install dependencies if needed
if not exist "node_modules" (
    echo Installing frontend dependencies...
    call npm install
    if errorlevel 1 (
        echo ERROR: npm install failed
        cd ..
        goto :error
    )
)

set FRONTEND_TESTS_PASSED=0
set FRONTEND_TESTS_FAILED=0

echo.
echo Running frontend unit tests...

REM Run unit tests
call npm run test:run > "..\%TEST_RESULTS_DIR%\frontend_unit_tests.log" 2>&1
if errorlevel 1 (
    echo   Unit tests FAILED
    set /a FRONTEND_TESTS_FAILED+=1
    echo   See %TEST_RESULTS_DIR%\frontend_unit_tests.log for details
) else (
    echo   Unit tests PASSED
    set /a FRONTEND_TESTS_PASSED+=1
)

echo.
echo Running frontend integration tests...

REM Run specific integration test files
set INTEGRATION_TEST_FILES=src/test/conversationFlowIntegration.test.tsx src/test/errorHandlingIntegration.test.tsx src/test/sessionStateIntegration.test.tsx src/test/audioPlaybackIntegration.test.tsx

for %%f in (%INTEGRATION_TEST_FILES%) do (
    if exist "%%f" (
        for %%i in ("%%f") do set test_name=%%~ni
        echo   Running !test_name!...
        
        call npx vitest run "%%f" > "..\%TEST_RESULTS_DIR%\frontend_!test_name!.log" 2>&1
        if errorlevel 1 (
            echo     FAILED
            set /a FRONTEND_TESTS_FAILED+=1
            echo     See %TEST_RESULTS_DIR%\frontend_!test_name!.log for details
        ) else (
            echo     PASSED
            set /a FRONTEND_TESTS_PASSED+=1
        )
    ) else (
        for %%i in ("%%f") do set test_name=%%~ni
        echo   Skipping !test_name! ^(file not found^)
    )
)

echo.
echo Running frontend performance tests...

REM Run performance tests
if exist "src\test\performanceTests.test.tsx" (
    echo   Running performance tests...
    call npx vitest run "src\test\performanceTests.test.tsx" > "..\%TEST_RESULTS_DIR%\frontend_performance.log" 2>&1
    if errorlevel 1 (
        echo     FAILED
        set /a FRONTEND_TESTS_FAILED+=1
        echo     See %TEST_RESULTS_DIR%\frontend_performance.log for details
    ) else (
        echo     PASSED
        set /a FRONTEND_TESTS_PASSED+=1
        
        REM Extract performance metrics
        findstr /C:"benchmark results:" "..\%TEST_RESULTS_DIR%\frontend_performance.log" > nul
        if not errorlevel 1 (
            echo Frontend Performance Results: >> "..\%TEST_RESULTS_DIR%\%PERFORMANCE_LOG%"
            findstr /A:"benchmark results:" "..\%TEST_RESULTS_DIR%\frontend_performance.log" >> "..\%TEST_RESULTS_DIR%\%PERFORMANCE_LOG%"
            echo. >> "..\%TEST_RESULTS_DIR%\%PERFORMANCE_LOG%"
        )
    )
) else (
    echo   Skipping performance tests ^(file not found^)
)

REM Generate test coverage if possible
echo.
echo Generating test coverage...
call npm run test:run -- --coverage > "..\%TEST_RESULTS_DIR%\frontend_coverage.log" 2>&1
if errorlevel 1 (
    echo   Coverage generation failed or not configured
) else (
    echo   Coverage report generated
    if exist "coverage" (
        xcopy /E /I /Y "coverage\*" "..\%COVERAGE_DIR%\" > nul
    )
)

cd ..

echo.
echo Frontend Test Summary:
echo   Passed: %FRONTEND_TESTS_PASSED%
echo   Failed: %FRONTEND_TESTS_FAILED%

goto :generate_test_data

:generate_test_data
echo.
echo Generating Test Data
echo ----------------------------------------

cd "%BACKEND_DIR%\build"

if exist "Debug\test_data_generator_test.exe" (
    echo Generating test audio data...
    if not exist "..\%TEST_RESULTS_DIR%\test_data" mkdir "..\%TEST_RESULTS_DIR%\test_data"
    
    "Debug\test_data_generator_test.exe" > "..\%TEST_RESULTS_DIR%\test_data_generation.log" 2>&1
    if errorlevel 1 (
        echo   Test data generation failed ^(non-critical^)
    ) else (
        echo   Test data generated successfully
    )
) else if exist "test_data_generator_test.exe" (
    echo Generating test audio data...
    if not exist "..\%TEST_RESULTS_DIR%\test_data" mkdir "..\%TEST_RESULTS_DIR%\test_data"
    
    "test_data_generator_test.exe" > "..\%TEST_RESULTS_DIR%\test_data_generation.log" 2>&1
    if errorlevel 1 (
        echo   Test data generation failed ^(non-critical^)
    ) else (
        echo   Test data generated successfully
    )
) else (
    echo   Test data generator not available
)

cd ..\..

goto :run_code_quality_checks

:run_code_quality_checks
echo.
echo Running Code Quality Checks
echo ----------------------------------------

REM Frontend linting
cd "%FRONTEND_DIR%"

echo Running frontend linting...
call npm run lint > "..\%TEST_RESULTS_DIR%\frontend_lint.log" 2>&1
if errorlevel 1 (
    echo   Frontend linting found issues
    echo   See %TEST_RESULTS_DIR%\frontend_lint.log for details
) else (
    echo   Frontend linting PASSED
)

cd ..

REM Backend code analysis (if tools are available)
where cppcheck >nul 2>&1
if not errorlevel 1 (
    echo Running backend static analysis...
    cppcheck --enable=all --xml --xml-version=2 "%BACKEND_DIR%\src" 2> "%TEST_RESULTS_DIR%\backend_cppcheck.xml"
    echo   Backend static analysis completed
) else (
    echo   cppcheck not available, skipping backend static analysis
)

goto :generate_final_report

:generate_final_report
echo.
echo Generating Final Report
echo ----------------------------------------

set REPORT_FILE=%TEST_RESULTS_DIR%\test_report.md

echo # SpeechRNT Test Report > "%REPORT_FILE%"
echo. >> "%REPORT_FILE%"
echo Generated on: %date% %time% >> "%REPORT_FILE%"
echo. >> "%REPORT_FILE%"
echo ## Test Summary >> "%REPORT_FILE%"
echo. >> "%REPORT_FILE%"
echo ### Backend Tests >> "%REPORT_FILE%"
echo - Passed: %BACKEND_TESTS_PASSED% >> "%REPORT_FILE%"
echo - Failed: %BACKEND_TESTS_FAILED% >> "%REPORT_FILE%"
echo. >> "%REPORT_FILE%"
echo ### Frontend Tests >> "%REPORT_FILE%"
echo - Passed: %FRONTEND_TESTS_PASSED% >> "%REPORT_FILE%"
echo - Failed: %FRONTEND_TESTS_FAILED% >> "%REPORT_FILE%"
echo. >> "%REPORT_FILE%"
set /a TOTAL_PASSED=%BACKEND_TESTS_PASSED%+%FRONTEND_TESTS_PASSED%
set /a TOTAL_FAILED=%BACKEND_TESTS_FAILED%+%FRONTEND_TESTS_FAILED%
echo ### Total >> "%REPORT_FILE%"
echo - Passed: %TOTAL_PASSED% >> "%REPORT_FILE%"
echo - Failed: %TOTAL_FAILED% >> "%REPORT_FILE%"
echo. >> "%REPORT_FILE%"
echo ## Performance Results >> "%REPORT_FILE%"
echo. >> "%REPORT_FILE%"

if exist "%TEST_RESULTS_DIR%\%PERFORMANCE_LOG%" (
    type "%TEST_RESULTS_DIR%\%PERFORMANCE_LOG%" >> "%REPORT_FILE%"
) else (
    echo No performance results available. >> "%REPORT_FILE%"
)

echo. >> "%REPORT_FILE%"
echo ## Test Files >> "%REPORT_FILE%"
echo. >> "%REPORT_FILE%"
echo The following test output files are available: >> "%REPORT_FILE%"
echo. >> "%REPORT_FILE%"

for %%f in ("%TEST_RESULTS_DIR%\*.log") do (
    echo - %%~nxf >> "%REPORT_FILE%"
)

echo.
echo Final test report generated: %REPORT_FILE%

goto :main_end

:main_end
echo.
echo ========================================
echo   Test Suite Complete                   
echo ========================================
echo Results directory: %TEST_RESULTS_DIR%

if %TOTAL_FAILED% equ 0 (
    echo All tests passed successfully!
    exit /b 0
) else (
    echo Some tests failed. Check the logs for details.
    exit /b 1
)

:error
echo.
echo ========================================
echo   Test Suite Failed                     
echo ========================================
exit /b 1