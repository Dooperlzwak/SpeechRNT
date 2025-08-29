#!/bin/bash

# STT Unit Test Runner
# This script runs all comprehensive STT unit tests

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test configuration
BUILD_DIR="build"
TEST_TIMEOUT=300  # 5 minutes per test
VERBOSE=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -t|--timeout)
            TEST_TIMEOUT="$2"
            shift 2
            ;;
        -b|--build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  -v, --verbose     Enable verbose output"
            echo "  -t, --timeout     Set test timeout in seconds (default: 300)"
            echo "  -b, --build-dir   Set build directory (default: build)"
            echo "  -h, --help        Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Function to print colored output
print_status() {
    local color=$1
    local message=$2
    echo -e "${color}${message}${NC}"
}

# Function to run a single test
run_test() {
    local test_name=$1
    local test_executable=$2
    
    print_status $BLUE "Running $test_name..."
    
    if [[ $VERBOSE == true ]]; then
        timeout $TEST_TIMEOUT $test_executable --gtest_output=xml:${test_name}_results.xml
    else
        timeout $TEST_TIMEOUT $test_executable --gtest_output=xml:${test_name}_results.xml > ${test_name}_output.log 2>&1
    fi
    
    local exit_code=$?
    
    if [[ $exit_code -eq 0 ]]; then
        print_status $GREEN "✓ $test_name PASSED"
        return 0
    elif [[ $exit_code -eq 124 ]]; then
        print_status $RED "✗ $test_name TIMEOUT (${TEST_TIMEOUT}s)"
        return 1
    else
        print_status $RED "✗ $test_name FAILED (exit code: $exit_code)"
        if [[ $VERBOSE == false ]]; then
            echo "Last 20 lines of output:"
            tail -20 ${test_name}_output.log
        fi
        return 1
    fi
}

# Main execution
main() {
    print_status $YELLOW "Starting STT Unit Test Suite"
    print_status $YELLOW "Build directory: $BUILD_DIR"
    print_status $YELLOW "Test timeout: ${TEST_TIMEOUT}s"
    print_status $YELLOW "Verbose mode: $VERBOSE"
    echo ""
    
    # Check if build directory exists
    if [[ ! -d "$BUILD_DIR" ]]; then
        print_status $RED "Build directory '$BUILD_DIR' does not exist!"
        print_status $YELLOW "Please build the project first:"
        print_status $YELLOW "  mkdir -p $BUILD_DIR && cd $BUILD_DIR"
        print_status $YELLOW "  cmake .. && make -j\$(nproc)"
        exit 1
    fi
    
    # Change to build directory
    cd "$BUILD_DIR"
    
    # Define test cases
    declare -A tests=(
        ["WhisperSTT Core Tests"]="whisper_stt_test"
        ["WhisperSTT VAD Integration Tests"]="whisper_stt_vad_integration_test"
        ["WhisperSTT Error Recovery Tests"]="whisper_stt_error_recovery_test"
        ["WhisperSTT Streaming Tests"]="whisper_stt_streaming_test"
    )
    
    # Track results
    local total_tests=0
    local passed_tests=0
    local failed_tests=0
    local failed_test_names=()
    
    # Run each test
    for test_name in "${!tests[@]}"; do
        test_executable="${tests[$test_name]}"
        
        # Check if test executable exists
        if [[ ! -f "$test_executable" ]]; then
            print_status $YELLOW "⚠ Skipping $test_name - executable not found: $test_executable"
            continue
        fi
        
        total_tests=$((total_tests + 1))
        
        if run_test "$test_name" "$test_executable"; then
            passed_tests=$((passed_tests + 1))
        else
            failed_tests=$((failed_tests + 1))
            failed_test_names+=("$test_name")
        fi
        
        echo ""
    done
    
    # Print summary
    print_status $YELLOW "========================================="
    print_status $YELLOW "STT Unit Test Suite Summary"
    print_status $YELLOW "========================================="
    print_status $BLUE "Total tests: $total_tests"
    print_status $GREEN "Passed: $passed_tests"
    print_status $RED "Failed: $failed_tests"
    
    if [[ $failed_tests -gt 0 ]]; then
        print_status $RED "Failed tests:"
        for failed_test in "${failed_test_names[@]}"; do
            print_status $RED "  - $failed_test"
        done
    fi
    
    # Generate combined test report
    if command -v xmllint >/dev/null 2>&1; then
        print_status $BLUE "Generating combined test report..."
        echo '<?xml version="1.0" encoding="UTF-8"?>' > stt_test_results.xml
        echo '<testsuites>' >> stt_test_results.xml
        
        for xml_file in *_results.xml; do
            if [[ -f "$xml_file" ]]; then
                xmllint --format "$xml_file" | sed '1d' | sed '$d' >> stt_test_results.xml
            fi
        done
        
        echo '</testsuites>' >> stt_test_results.xml
        print_status $GREEN "Combined test report saved to: stt_test_results.xml"
    fi
    
    # Cleanup log files if not verbose
    if [[ $VERBOSE == false ]]; then
        rm -f *_output.log
    fi
    
    # Exit with appropriate code
    if [[ $failed_tests -eq 0 ]]; then
        print_status $GREEN "🎉 All STT unit tests passed!"
        exit 0
    else
        print_status $RED "❌ Some STT unit tests failed!"
        exit 1
    fi
}

# Run main function
main "$@"