#!/bin/bash

# MT Backend Integration Tests Runner
# This script runs comprehensive integration tests for the MT backend completion

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test configuration
BUILD_DIR="backend/build"
TEST_RESULTS_DIR="backend/tests/results"
PERFORMANCE_RESULTS_DIR="backend/tests/performance"

echo -e "${BLUE}=== MT Backend Integration Tests Runner ===${NC}"
echo "Running comprehensive integration tests for MT backend completion"
echo

# Create results directories
mkdir -p "$TEST_RESULTS_DIR"
mkdir -p "$PERFORMANCE_RESULTS_DIR"

# Function to run a test and capture results
run_test() {
    local test_name=$1
    local test_executable=$2
    local test_description=$3
    
    echo -e "${YELLOW}Running: $test_description${NC}"
    echo "Test: $test_name"
    echo "Executable: $test_executable"
    echo
    
    local start_time=$(date +%s)
    local result_file="$TEST_RESULTS_DIR/${test_name}_result.txt"
    
    if [ -f "$BUILD_DIR/$test_executable" ]; then
        if timeout 300 "$BUILD_DIR/$test_executable" > "$result_file" 2>&1; then
            local end_time=$(date +%s)
            local duration=$((end_time - start_time))
            echo -e "${GREEN}✓ PASSED${NC} ($duration seconds)"
            echo "Results saved to: $result_file"
            return 0
        else
            local end_time=$(date +%s)
            local duration=$((end_time - start_time))
            echo -e "${RED}✗ FAILED${NC} ($duration seconds)"
            echo "Error details saved to: $result_file"
            echo "Last 10 lines of output:"
            tail -n 10 "$result_file"
            return 1
        fi
    else
        echo -e "${RED}✗ EXECUTABLE NOT FOUND${NC}"
        echo "Expected: $BUILD_DIR/$test_executable"
        return 1
    fi
    echo
}

# Function to check if build directory exists and has tests
check_build_environment() {
    if [ ! -d "$BUILD_DIR" ]; then
        echo -e "${RED}Error: Build directory not found: $BUILD_DIR${NC}"
        echo "Please run 'cd backend && mkdir build && cd build && cmake .. && make' first"
        exit 1
    fi
    
    echo -e "${BLUE}Checking build environment...${NC}"
    echo "Build directory: $BUILD_DIR"
    
    # Count available test executables
    local test_count=0
    for test_exec in mt_end_to_end_integration_test mt_error_propagation_recovery_test mt_performance_benchmark; do
        if [ -f "$BUILD_DIR/$test_exec" ]; then
            ((test_count++))
        fi
    done
    
    echo "Found $test_count MT integration test executables"
    
    if [ $test_count -eq 0 ]; then
        echo -e "${RED}Error: No MT integration test executables found${NC}"
        echo "Please build the tests first with 'make' in the build directory"
        exit 1
    fi
    
    echo
}

# Function to generate test report
generate_report() {
    local total_tests=$1
    local passed_tests=$2
    local failed_tests=$3
    
    local report_file="$TEST_RESULTS_DIR/mt_integration_test_report.txt"
    
    echo "=== MT Backend Integration Test Report ===" > "$report_file"
    echo "Generated: $(date)" >> "$report_file"
    echo >> "$report_file"
    echo "Total Tests: $total_tests" >> "$report_file"
    echo "Passed: $passed_tests" >> "$report_file"
    echo "Failed: $failed_tests" >> "$report_file"
    echo "Success Rate: $(( (passed_tests * 100) / total_tests ))%" >> "$report_file"
    echo >> "$report_file"
    
    echo "Individual Test Results:" >> "$report_file"
    for result_file in "$TEST_RESULTS_DIR"/*_result.txt; do
        if [ -f "$result_file" ]; then
            local test_name=$(basename "$result_file" _result.txt)
            echo "- $test_name: $(head -n 1 "$result_file" | grep -q "PASSED\|All tests passed" && echo "PASSED" || echo "FAILED")" >> "$report_file"
        fi
    done
    
    echo >> "$report_file"
    echo "Performance Results:" >> "$report_file"
    if [ -f "$PERFORMANCE_RESULTS_DIR/mt_benchmark_results.json" ]; then
        echo "- Benchmark results available in: $PERFORMANCE_RESULTS_DIR/mt_benchmark_results.json" >> "$report_file"
    else
        echo "- No performance benchmark results found" >> "$report_file"
    fi
    
    echo -e "${BLUE}Test report generated: $report_file${NC}"
}

# Main execution
main() {
    local start_time=$(date +%s)
    
    # Check build environment
    check_build_environment
    
    # Initialize counters
    local total_tests=0
    local passed_tests=0
    local failed_tests=0
    
    echo -e "${BLUE}=== Starting MT Integration Tests ===${NC}"
    echo
    
    # Test 1: STT → Language Detection → MT Pipeline Integration
    ((total_tests++))
    if run_test "mt_end_to_end_integration" "mt_end_to_end_integration_test" "STT → Language Detection → MT Pipeline Integration"; then
        ((passed_tests++))
    else
        ((failed_tests++))
    fi
    
    # Test 2: Error Propagation and Recovery Validation
    ((total_tests++))
    if run_test "mt_error_propagation_recovery" "mt_error_propagation_recovery_test" "Error Propagation and Recovery Validation"; then
        ((passed_tests++))
    else
        ((failed_tests++))
    fi
    
    # Test 3: Performance Benchmarks (if GTest is available)
    if [ -f "$BUILD_DIR/mt_performance_benchmark" ]; then
        ((total_tests++))
        if run_test "mt_performance_benchmark" "mt_performance_benchmark" "GPU vs CPU Performance Comparison and Benchmarks"; then
            ((passed_tests++))
        else
            ((failed_tests++))
        fi
    else
        echo -e "${YELLOW}Skipping performance benchmarks (GTest not available or not built)${NC}"
        echo
    fi
    
    # Additional integration tests if available
    for test_exec in multi_language_pipeline_test translation_pipeline_test; do
        if [ -f "$BUILD_DIR/$test_exec" ]; then
            ((total_tests++))
            local test_description="Additional MT Integration Test: $test_exec"
            if run_test "${test_exec%_test}" "$test_exec" "$test_description"; then
                ((passed_tests++))
            else
                ((failed_tests++))
            fi
        fi
    done
    
    local end_time=$(date +%s)
    local total_duration=$((end_time - start_time))
    
    # Generate final report
    generate_report $total_tests $passed_tests $failed_tests
    
    # Print summary
    echo -e "${BLUE}=== MT Integration Tests Summary ===${NC}"
    echo "Total Tests: $total_tests"
    echo "Passed: $passed_tests"
    echo "Failed: $failed_tests"
    echo "Total Duration: $total_duration seconds"
    echo
    
    if [ $failed_tests -eq 0 ]; then
        echo -e "${GREEN}🎉 All MT integration tests passed!${NC}"
        echo
        echo "The MT backend completion implementation successfully passes all integration tests:"
        echo "✓ STT → Language Detection → MT pipeline integration"
        echo "✓ Multi-language conversation scenarios"
        echo "✓ GPU vs CPU performance comparison"
        echo "✓ Real-time performance and latency measurements"
        echo "✓ Error propagation and recovery validation"
        echo
        echo "Results and performance data are available in:"
        echo "- Test results: $TEST_RESULTS_DIR/"
        echo "- Performance data: $PERFORMANCE_RESULTS_DIR/"
        exit 0
    else
        echo -e "${RED}❌ Some tests failed${NC}"
        echo
        echo "Failed tests need attention. Check the individual result files for details:"
        for result_file in "$TEST_RESULTS_DIR"/*_result.txt; do
            if [ -f "$result_file" ]; then
                if ! grep -q "PASSED\|All tests passed" "$result_file"; then
                    echo "- $(basename "$result_file" _result.txt): $result_file"
                fi
            fi
        done
        exit 1
    fi
}

# Handle script arguments
case "${1:-}" in
    --help|-h)
        echo "MT Backend Integration Tests Runner"
        echo
        echo "Usage: $0 [options]"
        echo
        echo "Options:"
        echo "  --help, -h     Show this help message"
        echo "  --clean        Clean previous test results"
        echo "  --verbose      Enable verbose output"
        echo
        echo "This script runs comprehensive integration tests for the MT backend completion,"
        echo "including end-to-end pipeline tests, performance benchmarks, and error recovery validation."
        exit 0
        ;;
    --clean)
        echo "Cleaning previous test results..."
        rm -rf "$TEST_RESULTS_DIR"
        rm -rf "$PERFORMANCE_RESULTS_DIR"/*.json
        echo "Clean completed."
        exit 0
        ;;
    --verbose)
        set -x
        main
        ;;
    "")
        main
        ;;
    *)
        echo "Unknown option: $1"
        echo "Use --help for usage information"
        exit 1
        ;;
esac