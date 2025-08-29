#!/bin/bash

# STT Integration Tests Runner
# This script runs all STT integration tests and generates a comprehensive report

set -e

echo "=== STT Integration Tests Runner ==="
echo "Starting comprehensive STT integration testing..."
echo

# Configuration
BUILD_DIR="build"
TEST_RESULTS_DIR="test_results/stt_integration"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
REPORT_FILE="$TEST_RESULTS_DIR/stt_integration_report_$TIMESTAMP.txt"

# Create results directory
mkdir -p "$TEST_RESULTS_DIR"

# Initialize report
echo "STT Integration Test Report" > "$REPORT_FILE"
echo "Generated: $(date)" >> "$REPORT_FILE"
echo "========================================" >> "$REPORT_FILE"
echo >> "$REPORT_FILE"

# Function to run a test and capture results
run_test() {
    local test_name="$1"
    local test_executable="$2"
    local description="$3"
    
    echo "Running $test_name..."
    echo "Description: $description"
    
    # Add test header to report
    echo "Test: $test_name" >> "$REPORT_FILE"
    echo "Description: $description" >> "$REPORT_FILE"
    echo "Started: $(date)" >> "$REPORT_FILE"
    echo "----------------------------------------" >> "$REPORT_FILE"
    
    # Run the test and capture output
    local test_output_file="$TEST_RESULTS_DIR/${test_name}_output_$TIMESTAMP.txt"
    local test_start_time=$(date +%s)
    
    if ./"$BUILD_DIR"/"$test_executable" > "$test_output_file" 2>&1; then
        local test_result="PASSED"
        local exit_code=0
    else
        local test_result="FAILED"
        local exit_code=$?
    fi
    
    local test_end_time=$(date +%s)
    local test_duration=$((test_end_time - test_start_time))
    
    # Add results to report
    echo "Result: $test_result" >> "$REPORT_FILE"
    echo "Duration: ${test_duration}s" >> "$REPORT_FILE"
    echo "Exit Code: $exit_code" >> "$REPORT_FILE"
    echo "Output File: $test_output_file" >> "$REPORT_FILE"
    echo >> "$REPORT_FILE"
    
    # Display summary
    echo "  Result: $test_result"
    echo "  Duration: ${test_duration}s"
    echo "  Output saved to: $test_output_file"
    echo
    
    return $exit_code
}

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory '$BUILD_DIR' not found."
    echo "Please run 'mkdir build && cd build && cmake .. && make' first."
    exit 1
fi

# Change to project root directory
cd "$(dirname "$0")/../.."

# Track test results
total_tests=0
passed_tests=0
failed_tests=0

echo "Starting STT Integration Test Suite..."
echo "Results will be saved to: $TEST_RESULTS_DIR"
echo

# Test 1: Comprehensive STT Integration
total_tests=$((total_tests + 1))
if run_test "STT_Integration_Comprehensive" "stt_integration_comprehensive_test" "End-to-end STT pipeline testing with real components"; then
    passed_tests=$((passed_tests + 1))
else
    failed_tests=$((failed_tests + 1))
fi

# Test 2: STT Performance Benchmark
total_tests=$((total_tests + 1))
if run_test "STT_Performance_Benchmark" "stt_performance_benchmark_test" "Performance benchmarking for latency requirements"; then
    passed_tests=$((passed_tests + 1))
else
    failed_tests=$((failed_tests + 1))
fi

# Test 3: STT Load Testing
total_tests=$((total_tests + 1))
if run_test "STT_Load_Testing" "stt_load_testing_test" "Load testing for concurrent transcription scenarios"; then
    passed_tests=$((passed_tests + 1))
else
    failed_tests=$((failed_tests + 1))
fi

# Test 4: STT WebSocket Integration
total_tests=$((total_tests + 1))
if run_test "STT_WebSocket_Integration" "stt_websocket_integration_test" "Integration testing with WebSocket communication layer"; then
    passed_tests=$((passed_tests + 1))
else
    failed_tests=$((failed_tests + 1))
fi

# Test 5: Original STT Integration (for comparison)
total_tests=$((total_tests + 1))
if run_test "STT_Integration_Original" "stt_integration_test" "Original STT integration test for baseline comparison"; then
    passed_tests=$((passed_tests + 1))
else
    failed_tests=$((failed_tests + 1))
fi

# Generate final summary
echo "========================================" >> "$REPORT_FILE"
echo "FINAL SUMMARY" >> "$REPORT_FILE"
echo "========================================" >> "$REPORT_FILE"
echo "Total Tests: $total_tests" >> "$REPORT_FILE"
echo "Passed: $passed_tests" >> "$REPORT_FILE"
echo "Failed: $failed_tests" >> "$REPORT_FILE"
echo "Success Rate: $(( (passed_tests * 100) / total_tests ))%" >> "$REPORT_FILE"
echo "Completed: $(date)" >> "$REPORT_FILE"

# Display final summary
echo "========================================="
echo "STT Integration Test Summary"
echo "========================================="
echo "Total Tests: $total_tests"
echo "Passed: $passed_tests"
echo "Failed: $failed_tests"
echo "Success Rate: $(( (passed_tests * 100) / total_tests ))%"
echo
echo "Detailed report saved to: $REPORT_FILE"
echo

# Check if performance report was generated
if [ -f "stt_performance_report.txt" ]; then
    echo "Performance report generated: stt_performance_report.txt"
    mv "stt_performance_report.txt" "$TEST_RESULTS_DIR/stt_performance_report_$TIMESTAMP.txt"
fi

# Exit with appropriate code
if [ $failed_tests -eq 0 ]; then
    echo "✅ All STT integration tests passed!"
    exit 0
else
    echo "❌ $failed_tests test(s) failed. Check the detailed reports for more information."
    exit 1
fi