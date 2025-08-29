# Comprehensive test runner for SpeechRNT project (PowerShell version)
# This script runs all unit tests, integration tests, and performance tests

param(
    [string]$Target = "all"
)

# Configuration
$BackendDir = "backend"
$FrontendDir = "frontend"
$TestResultsDir = "test_results"
$CoverageDir = "coverage"
$PerformanceLog = "performance_results.log"

# Create results directory
New-Item -ItemType Directory -Force -Path $TestResultsDir | Out-Null
New-Item -ItemType Directory -Force -Path $CoverageDir | Out-Null

Write-Host "========================================" -ForegroundColor Blue
Write-Host "  SpeechRNT Comprehensive Test Suite   " -ForegroundColor Blue
Write-Host "========================================" -ForegroundColor Blue
Write-Host ""

# Function to print section headers
function Write-Section {
    param([string]$Title)
    Write-Host $Title -ForegroundColor Yellow
    Write-Host "----------------------------------------"
}

# Function to print success/failure
function Write-Result {
    param([bool]$Success, [string]$Message)
    if ($Success) {
        Write-Host "✓ $Message" -ForegroundColor Green
    } else {
        Write-Host "✗ $Message" -ForegroundColor Red
    }
}

# Function to run backend tests
function Run-BackendTests {
    Write-Section "Running Backend Tests"
    
    Push-Location $BackendDir
    
    try {
        # Create build directory if it doesn't exist
        if (-not (Test-Path "build")) {
            New-Item -ItemType Directory -Name "build" | Out-Null
        }
        
        Push-Location "build"
        
        # Configure with CMake
        Write-Host "Configuring backend build..."
        $cmakeResult = & cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTING=ON 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host "ERROR: CMake configuration failed" -ForegroundColor Red
            Write-Host $cmakeResult
            return $false
        }
        
        # Build the project
        Write-Host "Building backend..."
        $buildResult = & cmake --build . --config Debug 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host "ERROR: Build failed" -ForegroundColor Red
            Write-Host $buildResult
            return $false
        }
        
        # Initialize test results
        $script:BackendTestsPassed = 0
        $script:BackendTestsFailed = 0
        
        Write-Host ""
        Write-Host "Running backend unit tests..."
        
        # Run individual test executables
        $Tests = @(
            "simple_tests",
            "vad_integration_test",
            "vad_edge_cases_test", 
            "whisper_stt_test",
            "stt_integration_test",
            "streaming_transcriber_test",
            "marian_translator_test",
            "model_manager_test",
            "coqui_tts_test",
            "voice_manager_test",
            "task_queue_test",
            "utterance_manager_test",
            "websocket_message_protocol_test",
            "audio_buffer_test"
        )
        
        foreach ($test in $Tests) {
            $testExe = "Debug\$test.exe"
            if (-not (Test-Path $testExe)) {
                $testExe = "$test.exe"
            }
            
            if (Test-Path $testExe) {
                Write-Host "  Running $test... " -NoNewline
                
                $outputFile = "..\$TestResultsDir\${test}_output.log"
                $testResult = & $testExe > $outputFile 2>&1
                
                if ($LASTEXITCODE -eq 0) {
                    Write-Host "PASSED" -ForegroundColor Green
                    $script:BackendTestsPassed++
                } else {
                    Write-Host "FAILED" -ForegroundColor Red
                    $script:BackendTestsFailed++
                    Write-Host "    See $TestResultsDir\${test}_output.log for details"
                }
            } else {
                Write-Host "  Skipping $test (executable not found)" -ForegroundColor Yellow
            }
        }
        
        Write-Host ""
        Write-Host "Running backend integration tests..."
        
        $IntegrationTests = @("end_to_end_conversation_test")
        
        foreach ($test in $IntegrationTests) {
            $testExe = "Debug\$test.exe"
            if (-not (Test-Path $testExe)) {
                $testExe = "$test.exe"
            }
            
            if (Test-Path $testExe) {
                Write-Host "  Running $test... " -NoNewline
                
                $outputFile = "..\$TestResultsDir\${test}_output.log"
                $testResult = & $testExe > $outputFile 2>&1
                
                if ($LASTEXITCODE -eq 0) {
                    Write-Host "PASSED" -ForegroundColor Green
                    $script:BackendTestsPassed++
                } else {
                    Write-Host "FAILED" -ForegroundColor Red
                    $script:BackendTestsFailed++
                    Write-Host "    See $TestResultsDir\${test}_output.log for details"
                }
            } else {
                Write-Host "  Skipping $test (executable not found)" -ForegroundColor Yellow
            }
        }
        
        Write-Host ""
        Write-Host "Running backend performance tests..."
        
        $PerformanceTests = @(
            "gpu_acceleration_benchmark",
            "latency_benchmark", 
            "load_testing_test"
        )
        
        foreach ($test in $PerformanceTests) {
            $testExe = "Debug\$test.exe"
            if (-not (Test-Path $testExe)) {
                $testExe = "$test.exe"
            }
            
            if (Test-Path $testExe) {
                Write-Host "  Running $test... " -NoNewline
                
                $outputFile = "..\$TestResultsDir\${test}_output.log"
                $testResult = & $testExe > $outputFile 2>&1
                
                if ($LASTEXITCODE -eq 0) {
                    Write-Host "PASSED" -ForegroundColor Green
                    $script:BackendTestsPassed++
                    
                    # Extract performance metrics
                    $logContent = Get-Content $outputFile -ErrorAction SilentlyContinue
                    if ($logContent -match "Results:") {
                        "Performance Results for $test:" | Add-Content "..\$TestResultsDir\$PerformanceLog"
                        $logContent | Where-Object { $_ -match "Results:" } | Add-Content "..\$TestResultsDir\$PerformanceLog"
                        "" | Add-Content "..\$TestResultsDir\$PerformanceLog"
                    }
                } else {
                    Write-Host "FAILED" -ForegroundColor Red
                    $script:BackendTestsFailed++
                    Write-Host "    See $TestResultsDir\${test}_output.log for details"
                }
            } else {
                Write-Host "  Skipping $test (executable not found)" -ForegroundColor Yellow
            }
        }
        
        # Run CTest if available
        if (Get-Command ctest -ErrorAction SilentlyContinue) {
            Write-Host ""
            Write-Host "Running CTest suite..."
            $ctestResult = & ctest --output-on-failure > "..\$TestResultsDir\ctest_output.log" 2>&1
            if ($LASTEXITCODE -eq 0) {
                Write-Host "  CTest suite PASSED" -ForegroundColor Green
            } else {
                Write-Host "  CTest suite FAILED" -ForegroundColor Red
                Write-Host "    See $TestResultsDir\ctest_output.log for details"
            }
        }
        
        Pop-Location
        
        Write-Host ""
        Write-Host "Backend Test Summary:"
        Write-Host "  Passed: $script:BackendTestsPassed"
        Write-Host "  Failed: $script:BackendTestsFailed"
        
        return ($script:BackendTestsFailed -eq 0)
    }
    finally {
        Pop-Location
    }
}

# Function to run frontend tests
function Run-FrontendTests {
    Write-Section "Running Frontend Tests"
    
    Push-Location $FrontendDir
    
    try {
        # Install dependencies if needed
        if (-not (Test-Path "node_modules")) {
            Write-Host "Installing frontend dependencies..."
            $npmResult = & npm install 2>&1
            if ($LASTEXITCODE -ne 0) {
                Write-Host "ERROR: npm install failed" -ForegroundColor Red
                Write-Host $npmResult
                return $false
            }
        }
        
        # Initialize test results
        $script:FrontendTestsPassed = 0
        $script:FrontendTestsFailed = 0
        
        Write-Host ""
        Write-Host "Running frontend unit tests..."
        
        # Run unit tests
        $unitTestResult = & npm run test:run > "..\$TestResultsDir\frontend_unit_tests.log" 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  Unit tests PASSED" -ForegroundColor Green
            $script:FrontendTestsPassed++
        } else {
            Write-Host "  Unit tests FAILED" -ForegroundColor Red
            $script:FrontendTestsFailed++
            Write-Host "    See $TestResultsDir\frontend_unit_tests.log for details"
        }
        
        Write-Host ""
        Write-Host "Running frontend integration tests..."
        
        # Run specific integration test files
        $IntegrationTestFiles = @(
            "src/test/conversationFlowIntegration.test.tsx",
            "src/test/errorHandlingIntegration.test.tsx",
            "src/test/sessionStateIntegration.test.tsx",
            "src/test/audioPlaybackIntegration.test.tsx"
        )
        
        foreach ($testFile in $IntegrationTestFiles) {
            if (Test-Path $testFile) {
                $testName = [System.IO.Path]::GetFileNameWithoutExtension($testFile)
                Write-Host "  Running $testName... " -NoNewline
                
                $outputFile = "..\$TestResultsDir\frontend_$testName.log"
                $testResult = & npx vitest run $testFile > $outputFile 2>&1
                
                if ($LASTEXITCODE -eq 0) {
                    Write-Host "PASSED" -ForegroundColor Green
                    $script:FrontendTestsPassed++
                } else {
                    Write-Host "FAILED" -ForegroundColor Red
                    $script:FrontendTestsFailed++
                    Write-Host "    See $TestResultsDir\frontend_$testName.log for details"
                }
            } else {
                $testName = [System.IO.Path]::GetFileNameWithoutExtension($testFile)
                Write-Host "  Skipping $testName (file not found)" -ForegroundColor Yellow
            }
        }
        
        Write-Host ""
        Write-Host "Running frontend performance tests..."
        
        # Run performance tests
        if (Test-Path "src\test\performanceTests.test.tsx") {
            Write-Host "  Running performance tests... " -NoNewline
            
            $outputFile = "..\$TestResultsDir\frontend_performance.log"
            $perfResult = & npx vitest run "src\test\performanceTests.test.tsx" > $outputFile 2>&1
            
            if ($LASTEXITCODE -eq 0) {
                Write-Host "PASSED" -ForegroundColor Green
                $script:FrontendTestsPassed++
                
                # Extract performance metrics
                $logContent = Get-Content $outputFile -ErrorAction SilentlyContinue
                if ($logContent -match "benchmark results:") {
                    "Frontend Performance Results:" | Add-Content "..\$TestResultsDir\$PerformanceLog"
                    $logContent | Where-Object { $_ -match "benchmark results:" } | Add-Content "..\$TestResultsDir\$PerformanceLog"
                    "" | Add-Content "..\$TestResultsDir\$PerformanceLog"
                }
            } else {
                Write-Host "FAILED" -ForegroundColor Red
                $script:FrontendTestsFailed++
                Write-Host "    See $TestResultsDir\frontend_performance.log for details"
            }
        } else {
            Write-Host "  Skipping performance tests (file not found)" -ForegroundColor Yellow
        }
        
        # Generate test coverage if possible
        Write-Host ""
        Write-Host "Generating test coverage..."
        $coverageResult = & npm run test:run -- --coverage > "..\$TestResultsDir\frontend_coverage.log" 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  Coverage report generated" -ForegroundColor Green
            if (Test-Path "coverage") {
                Copy-Item -Recurse -Force "coverage\*" "..\$CoverageDir\"
            }
        } else {
            Write-Host "  Coverage generation failed or not configured" -ForegroundColor Yellow
        }
        
        Write-Host ""
        Write-Host "Frontend Test Summary:"
        Write-Host "  Passed: $script:FrontendTestsPassed"
        Write-Host "  Failed: $script:FrontendTestsFailed"
        
        return ($script:FrontendTestsFailed -eq 0)
    }
    finally {
        Pop-Location
    }
}

# Function to generate test data
function Generate-TestData {
    Write-Section "Generating Test Data"
    
    Push-Location "$BackendDir\build"
    
    try {
        $testDataExe = "Debug\test_data_generator_test.exe"
        if (-not (Test-Path $testDataExe)) {
            $testDataExe = "test_data_generator_test.exe"
        }
        
        if (Test-Path $testDataExe) {
            Write-Host "Generating test audio data..."
            New-Item -ItemType Directory -Force -Path "..\$TestResultsDir\test_data" | Out-Null
            
            $dataGenResult = & $testDataExe > "..\$TestResultsDir\test_data_generation.log" 2>&1
            if ($LASTEXITCODE -eq 0) {
                Write-Host "  Test data generated successfully" -ForegroundColor Green
            } else {
                Write-Host "  Test data generation failed (non-critical)" -ForegroundColor Yellow
            }
        } else {
            Write-Host "  Test data generator not available" -ForegroundColor Yellow
        }
    }
    finally {
        Pop-Location
    }
}

# Function to run code quality checks
function Run-CodeQualityChecks {
    Write-Section "Running Code Quality Checks"
    
    # Frontend linting
    Push-Location $FrontendDir
    
    try {
        Write-Host "Running frontend linting..."
        $lintResult = & npm run lint > "..\$TestResultsDir\frontend_lint.log" 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  Frontend linting PASSED" -ForegroundColor Green
        } else {
            Write-Host "  Frontend linting found issues" -ForegroundColor Yellow
            Write-Host "    See $TestResultsDir\frontend_lint.log for details"
        }
    }
    finally {
        Pop-Location
    }
    
    # Backend code analysis (if tools are available)
    if (Get-Command cppcheck -ErrorAction SilentlyContinue) {
        Write-Host "Running backend static analysis..."
        $cppcheckResult = & cppcheck --enable=all --xml --xml-version=2 "$BackendDir\src" 2> "$TestResultsDir\backend_cppcheck.xml"
        Write-Host "  Backend static analysis completed" -ForegroundColor Green
    } else {
        Write-Host "  cppcheck not available, skipping backend static analysis" -ForegroundColor Yellow
    }
}

# Function to generate final report
function Generate-FinalReport {
    Write-Section "Generating Final Report"
    
    $ReportFile = "$TestResultsDir\test_report.md"
    
    $reportContent = @"
# SpeechRNT Test Report

Generated on: $(Get-Date)

## Test Summary

### Backend Tests
- Passed: $script:BackendTestsPassed
- Failed: $script:BackendTestsFailed

### Frontend Tests  
- Passed: $script:FrontendTestsPassed
- Failed: $script:FrontendTestsFailed

### Total
- Passed: $($script:BackendTestsPassed + $script:FrontendTestsPassed)
- Failed: $($script:BackendTestsFailed + $script:FrontendTestsFailed)

## Performance Results

"@

    $reportContent | Out-File -FilePath $ReportFile -Encoding UTF8
    
    if (Test-Path "$TestResultsDir\$PerformanceLog") {
        Get-Content "$TestResultsDir\$PerformanceLog" | Add-Content $ReportFile
    } else {
        "No performance results available." | Add-Content $ReportFile
    }
    
    "" | Add-Content $ReportFile
    "## Test Files" | Add-Content $ReportFile
    "" | Add-Content $ReportFile
    "The following test output files are available:" | Add-Content $ReportFile
    "" | Add-Content $ReportFile
    
    Get-ChildItem "$TestResultsDir\*.log" | ForEach-Object {
        "- $($_.Name)" | Add-Content $ReportFile
    }
    
    Write-Host ""
    Write-Host "Final test report generated: $ReportFile" -ForegroundColor Green
}

# Main execution
function Main {
    $startTime = Get-Date
    $backendResult = $true
    $frontendResult = $true
    
    # Initialize counters
    $script:BackendTestsPassed = 0
    $script:BackendTestsFailed = 0
    $script:FrontendTestsPassed = 0
    $script:FrontendTestsFailed = 0
    
    # Generate test data first
    Generate-TestData
    
    # Run backend tests
    if ($Target -eq "all" -or $Target -eq "backend") {
        $backendResult = Run-BackendTests
        Write-Host ""
    }
    
    # Run frontend tests
    if ($Target -eq "all" -or $Target -eq "frontend") {
        $frontendResult = Run-FrontendTests
        Write-Host ""
    }
    
    # Run code quality checks
    if ($Target -eq "all") {
        Run-CodeQualityChecks
        Write-Host ""
    }
    
    # Generate final report
    Generate-FinalReport
    
    $endTime = Get-Date
    $duration = ($endTime - $startTime).TotalSeconds
    
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Blue
    Write-Host "  Test Suite Complete                   " -ForegroundColor Blue
    Write-Host "========================================" -ForegroundColor Blue
    Write-Host "Total execution time: $([math]::Round($duration, 2))s"
    Write-Host "Results directory: $TestResultsDir"
    
    if ($backendResult -and $frontendResult) {
        Write-Host "All tests passed successfully!" -ForegroundColor Green
        exit 0
    } else {
        Write-Host "Some tests failed. Check the logs for details." -ForegroundColor Red
        exit 1
    }
}

# Handle command line arguments
switch ($Target.ToLower()) {
    "backend" {
        $script:BackendTestsPassed = 0
        $script:BackendTestsFailed = 0
        Run-BackendTests
        break
    }
    "frontend" {
        $script:FrontendTestsPassed = 0
        $script:FrontendTestsFailed = 0
        Run-FrontendTests
        break
    }
    "performance" {
        $script:BackendTestsPassed = 0
        $script:BackendTestsFailed = 0
        $script:FrontendTestsPassed = 0
        $script:FrontendTestsFailed = 0
        Run-BackendTests
        Run-FrontendTests
        Write-Host "Performance results available in $TestResultsDir\$PerformanceLog"
        break
    }
    "help" {
        Write-Host "Usage: .\run-all-tests.ps1 [-Target <target>]"
        Write-Host ""
        Write-Host "Options:"
        Write-Host "  -Target backend     Run only backend tests"
        Write-Host "  -Target frontend    Run only frontend tests"
        Write-Host "  -Target performance Run performance tests only"
        Write-Host "  -Target help        Show this help message"
        Write-Host ""
        Write-Host "Default: Run all tests"
        break
    }
    default {
        Main
        break
    }
}