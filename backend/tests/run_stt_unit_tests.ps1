#!/usr/bin/env pwsh

# STT Unit Test Runner for PowerShell
# This script runs all comprehensive STT unit tests

param(
    [switch]$Verbose = $false,
    [int]$Timeout = 300,
    [string]$BuildDir = "build",
    [switch]$Help = $false
)

# Show help if requested
if ($Help) {
    Write-Host "Usage: .\run_stt_unit_tests.ps1 [OPTIONS]"
    Write-Host "Options:"
    Write-Host "  -Verbose          Enable verbose output"
    Write-Host "  -Timeout <int>    Set test timeout in seconds (default: 300)"
    Write-Host "  -BuildDir <path>  Set build directory (default: build)"
    Write-Host "  -Help             Show this help message"
    exit 0
}

# Function to write colored output
function Write-ColorOutput {
    param(
        [string]$Message,
        [string]$Color = "White"
    )
    Write-Host $Message -ForegroundColor $Color
}

# Function to run a single test
function Invoke-Test {
    param(
        [string]$TestName,
        [string]$TestExecutable
    )
    
    Write-ColorOutput "Running $TestName..." -Color Blue
    
    # Check if test executable exists
    if (-not (Test-Path $TestExecutable)) {
        Write-ColorOutput "⚠ Skipping $TestName - executable not found: $TestExecutable" -Color Yellow
        return $null
    }
    
    try {
        $outputFile = "${TestName}_output.log"
        $xmlFile = "${TestName}_results.xml"
        
        if ($Verbose) {
            $process = Start-Process -FilePath $TestExecutable -ArgumentList "--gtest_output=xml:$xmlFile" -Wait -PassThru -NoNewWindow
        } else {
            $process = Start-Process -FilePath $TestExecutable -ArgumentList "--gtest_output=xml:$xmlFile" -Wait -PassThru -NoNewWindow -RedirectStandardOutput $outputFile -RedirectStandardError $outputFile
        }
        
        # Check for timeout (simplified - PowerShell doesn't have built-in timeout like bash)
        if ($process.ExitCode -eq 0) {
            Write-ColorOutput "✓ $TestName PASSED" -Color Green
            return $true
        } else {
            Write-ColorOutput "✗ $TestName FAILED (exit code: $($process.ExitCode))" -Color Red
            if (-not $Verbose -and (Test-Path $outputFile)) {
                Write-Host "Last 10 lines of output:"
                Get-Content $outputFile -Tail 10
            }
            return $false
        }
    }
    catch {
        Write-ColorOutput "✗ $TestName ERROR: $($_.Exception.Message)" -Color Red
        return $false
    }
}

# Main execution
Write-ColorOutput "Starting STT Unit Test Suite" -Color Yellow
Write-ColorOutput "Build directory: $BuildDir" -Color Yellow
Write-ColorOutput "Test timeout: ${Timeout}s" -Color Yellow
Write-ColorOutput "Verbose mode: $Verbose" -Color Yellow
Write-Host ""

# Check if build directory exists
if (-not (Test-Path $BuildDir)) {
    Write-ColorOutput "Build directory '$BuildDir' does not exist!" -Color Red
    Write-ColorOutput "Please build the project first:" -Color Yellow
    Write-ColorOutput "  mkdir $BuildDir; cd $BuildDir" -Color Yellow
    Write-ColorOutput "  cmake ..; cmake --build . --config Release" -Color Yellow
    exit 1
}

# Change to build directory
Push-Location $BuildDir

try {
    # Define test cases
    $tests = @{
        "WhisperSTT Core Tests" = "whisper_stt_test.exe"
        "WhisperSTT VAD Integration Tests" = "whisper_stt_vad_integration_test.exe"
        "WhisperSTT Error Recovery Tests" = "whisper_stt_error_recovery_test.exe"
        "WhisperSTT Streaming Tests" = "whisper_stt_streaming_test.exe"
    }
    
    # Track results
    $totalTests = 0
    $passedTests = 0
    $failedTests = 0
    $failedTestNames = @()
    
    # Run each test
    foreach ($testName in $tests.Keys) {
        $testExecutable = $tests[$testName]
        
        $result = Invoke-Test -TestName $testName -TestExecutable $testExecutable
        
        if ($result -ne $null) {
            $totalTests++
            if ($result) {
                $passedTests++
            } else {
                $failedTests++
                $failedTestNames += $testName
            }
        }
        
        Write-Host ""
    }
    
    # Print summary
    Write-ColorOutput "=========================================" -Color Yellow
    Write-ColorOutput "STT Unit Test Suite Summary" -Color Yellow
    Write-ColorOutput "=========================================" -Color Yellow
    Write-ColorOutput "Total tests: $totalTests" -Color Blue
    Write-ColorOutput "Passed: $passedTests" -Color Green
    Write-ColorOutput "Failed: $failedTests" -Color Red
    
    if ($failedTests -gt 0) {
        Write-ColorOutput "Failed tests:" -Color Red
        foreach ($failedTest in $failedTestNames) {
            Write-ColorOutput "  - $failedTest" -Color Red
        }
    }
    
    # Cleanup log files if not verbose
    if (-not $Verbose) {
        Remove-Item "*_output.log" -ErrorAction SilentlyContinue
    }
    
    # Exit with appropriate code
    if ($failedTests -eq 0) {
        Write-ColorOutput "🎉 All STT unit tests passed!" -Color Green
        exit 0
    } else {
        Write-ColorOutput "❌ Some STT unit tests failed!" -Color Red
        exit 1
    }
}
finally {
    Pop-Location
}