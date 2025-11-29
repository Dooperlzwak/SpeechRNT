#!/bin/bash

# Comprehensive test runner for SpeechRNT project
# This script runs all unit tests, integration tests, and performance tests

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BACKEND_DIR="backend"
FRONTEND_DIR="frontend"
TEST_RESULTS_DIR="test_results"
COVERAGE_DIR="coverage"
PERFORMANCE_LOG="performance_results.log"

# Create results directory
mkdir -p "$TEST_RESULTS_DIR"
mkdir -p "$COVERAGE_DIR"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  SpeechRNT Comprehensive Test Suite   ${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Function to print section headers
print_section() {
    echo -e "${YELLOW}$1${NC}"
    echo "----------------------------------------"
}

# Function to print success/failure
print_result() {
    if [ $1 -eq 0 ]; then
        echo -e "${GREEN}✓ $2${NC}"
    else
        echo -e "${RED}✗ $2${NC}"
        return 1
    fi
}

# Function to run backend tests
run_backend_tests() {
    print_section "Running Backend Tests"
    
    cd "$BACKEND_DIR"
    
    # Create build directory if it doesn't exist
    if [ ! -d "build" ]; then
        mkdir build
    fi
    
    cd build
    
    # Configure with CMake
    echo "Configuring backend build..."
    cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTING=ON
    
    # Build the project
    echo "Building backend..."
    make -j$(nproc)
    
    # Initialize test results
    BACKEND_TESTS_PASSED=0
    BACKEND_TESTS_FAILED=0
    
    echo ""
    echo "Running backend unit tests..."
    
    # Run individual test executables
    TESTS=(
        "simple_tests:Simple Tests"
        "vad_integration_test:VAD Integration"
        "vad_edge_cases_test:VAD Edge Cases"
        "whisper_stt_test:Whisper STT"
        "stt_integration_test:STT Integration"
        "streaming_transcriber_test:Streaming Transcriber"
        "marian_translator_test:Marian Translator"
        "model_manager_test:Model Manager"
        "piper_tts_test:Coqui TTS"
        "voice_manager_test:Voice Manager"
        "task_queue_test:Task Queue"
        "utterance_manager_test:Utterance Manager"
        "websocket_message_protocol_test:WebSocket Message Protocol"
        "audio_buffer_test:Audio Buffer"
    )
    
    for test_info in "${TESTS[@]}"; do
        IFS=':' read -r test_name test_description <<< "$test_info"
        
        if [ -f "./$test_name" ]; then
            echo -n "  Running $test_description... "
            if ./"$test_name" > "../$TEST_RESULTS_DIR/${test_name}_output.log" 2>&1; then
                echo -e "${GREEN}PASSED${NC}"
                ((BACKEND_TESTS_PASSED++))
            else
                echo -e "${RED}FAILED${NC}"
                ((BACKEND_TESTS_FAILED++))
                echo "    See $TEST_RESULTS_DIR/${test_name}_output.log for details"
            fi
        else
            echo -e "${YELLOW}  Skipping $test_description (executable not found)${NC}"
        fi
    done
    
    echo ""
    echo "Running backend integration tests..."
    
    INTEGRATION_TESTS=(
        "end_to_end_conversation_test:End-to-End Conversation"
    )
    
    for test_info in "${INTEGRATION_TESTS[@]}"; do
        IFS=':' read -r test_name test_description <<< "$test_info"
        
        if [ -f "./$test_name" ]; then
            echo -n "  Running $test_description... "
            if ./"$test_name" > "../$TEST_RESULTS_DIR/${test_name}_output.log" 2>&1; then
                echo -e "${GREEN}PASSED${NC}"
                ((BACKEND_TESTS_PASSED++))
            else
                echo -e "${RED}FAILED${NC}"
                ((BACKEND_TESTS_FAILED++))
                echo "    See $TEST_RESULTS_DIR/${test_name}_output.log for details"
            fi
        else
            echo -e "${YELLOW}  Skipping $test_description (executable not found)${NC}"
        fi
    done
    
    echo ""
    echo "Running backend performance tests..."
    
    PERFORMANCE_TESTS=(
        "gpu_acceleration_benchmark:GPU Acceleration Benchmark"
        "latency_benchmark:Latency Benchmark"
        "load_testing_test:Load Testing"
    )
    
    for test_info in "${PERFORMANCE_TESTS[@]}"; do
        IFS=':' read -r test_name test_description <<< "$test_info"
        
        if [ -f "./$test_name" ]; then
            echo -n "  Running $test_description... "
            if ./"$test_name" > "../$TEST_RESULTS_DIR/${test_name}_output.log" 2>&1; then
                echo -e "${GREEN}PASSED${NC}"
                ((BACKEND_TESTS_PASSED++))
                
                # Extract performance metrics
                if grep -q "Results:" "../$TEST_RESULTS_DIR/${test_name}_output.log"; then
                    echo "Performance Results for $test_description:" >> "../$TEST_RESULTS_DIR/$PERFORMANCE_LOG"
                    grep -A 10 "Results:" "../$TEST_RESULTS_DIR/${test_name}_output.log" >> "../$TEST_RESULTS_DIR/$PERFORMANCE_LOG"
                    echo "" >> "../$TEST_RESULTS_DIR/$PERFORMANCE_LOG"
                fi
            else
                echo -e "${RED}FAILED${NC}"
                ((BACKEND_TESTS_FAILED++))
                echo "    See $TEST_RESULTS_DIR/${test_name}_output.log for details"
            fi
        else
            echo -e "${YELLOW}  Skipping $test_description (executable not found)${NC}"
        fi
    done
    
    # Run CTest if available
    if command -v ctest &> /dev/null; then
        echo ""
        echo "Running CTest suite..."
        if ctest --output-on-failure > "../$TEST_RESULTS_DIR/ctest_output.log" 2>&1; then
            echo -e "${GREEN}  CTest suite PASSED${NC}"
        else
            echo -e "${RED}  CTest suite FAILED${NC}"
            echo "    See $TEST_RESULTS_DIR/ctest_output.log for details"
        fi
    fi
    
    cd ../..
    
    echo ""
    echo "Backend Test Summary:"
    echo "  Passed: $BACKEND_TESTS_PASSED"
    echo "  Failed: $BACKEND_TESTS_FAILED"
    
    return $BACKEND_TESTS_FAILED
}

# Function to run frontend tests
run_frontend_tests() {
    print_section "Running Frontend Tests"
    
    cd "$FRONTEND_DIR"
    
    # Install dependencies if needed
    if [ ! -d "node_modules" ]; then
        echo "Installing frontend dependencies..."
        npm install
    fi
    
    # Initialize test results
    FRONTEND_TESTS_PASSED=0
    FRONTEND_TESTS_FAILED=0
    
    echo ""
    echo "Running frontend unit tests..."
    
    # Run unit tests
    if npm run test:run > "../$TEST_RESULTS_DIR/frontend_unit_tests.log" 2>&1; then
        echo -e "${GREEN}  Unit tests PASSED${NC}"
        ((FRONTEND_TESTS_PASSED++))
    else
        echo -e "${RED}  Unit tests FAILED${NC}"
        ((FRONTEND_TESTS_FAILED++))
        echo "    See $TEST_RESULTS_DIR/frontend_unit_tests.log for details"
    fi
    
    echo ""
    echo "Running frontend integration tests..."
    
    # Run specific integration test files
    INTEGRATION_TEST_FILES=(
        "src/test/conversationFlowIntegration.test.tsx"
        "src/test/errorHandlingIntegration.test.tsx"
        "src/test/sessionStateIntegration.test.tsx"
        "src/test/audioPlaybackIntegration.test.tsx"
    )
    
    for test_file in "${INTEGRATION_TEST_FILES[@]}"; do
        if [ -f "$test_file" ]; then
            test_name=$(basename "$test_file" .test.tsx)
            echo -n "  Running $test_name... "
            
            if npx vitest run "$test_file" > "../$TEST_RESULTS_DIR/frontend_${test_name}.log" 2>&1; then
                echo -e "${GREEN}PASSED${NC}"
                ((FRONTEND_TESTS_PASSED++))
            else
                echo -e "${RED}FAILED${NC}"
                ((FRONTEND_TESTS_FAILED++))
                echo "    See $TEST_RESULTS_DIR/frontend_${test_name}.log for details"
            fi
        else
            echo -e "${YELLOW}  Skipping $test_name (file not found)${NC}"
        fi
    done
    
    echo ""
    echo "Running frontend performance tests..."
    
    # Run performance tests
    if [ -f "src/test/performanceTests.test.tsx" ]; then
        echo -n "  Running performance tests... "
        if npx vitest run "src/test/performanceTests.test.tsx" > "../$TEST_RESULTS_DIR/frontend_performance.log" 2>&1; then
            echo -e "${GREEN}PASSED${NC}"
            ((FRONTEND_TESTS_PASSED++))
            
            # Extract performance metrics
            if grep -q "benchmark results:" "../$TEST_RESULTS_DIR/frontend_performance.log"; then
                echo "Frontend Performance Results:" >> "../$TEST_RESULTS_DIR/$PERFORMANCE_LOG"
                grep -A 5 "benchmark results:" "../$TEST_RESULTS_DIR/frontend_performance.log" >> "../$TEST_RESULTS_DIR/$PERFORMANCE_LOG"
                echo "" >> "../$TEST_RESULTS_DIR/$PERFORMANCE_LOG"
            fi
        else
            echo -e "${RED}FAILED${NC}"
            ((FRONTEND_TESTS_FAILED++))
            echo "    See $TEST_RESULTS_DIR/frontend_performance.log for details"
        fi
    else
        echo -e "${YELLOW}  Skipping performance tests (file not found)${NC}"
    fi
    
    # Generate test coverage if possible
    echo ""
    echo "Generating test coverage..."
    if npm run test:run -- --coverage > "../$TEST_RESULTS_DIR/frontend_coverage.log" 2>&1; then
        echo -e "${GREEN}  Coverage report generated${NC}"
        if [ -d "coverage" ]; then
            cp -r coverage/* "../$COVERAGE_DIR/"
        fi
    else
        echo -e "${YELLOW}  Coverage generation failed or not configured${NC}"
    fi
    
    cd ..
    
    echo ""
    echo "Frontend Test Summary:"
    echo "  Passed: $FRONTEND_TESTS_PASSED"
    echo "  Failed: $FRONTEND_TESTS_FAILED"
    
    return $FRONTEND_TESTS_FAILED
}

# Function to generate test data
generate_test_data() {
    print_section "Generating Test Data"
    
    cd "$BACKEND_DIR/build"
    
    if [ -f "./test_data_generator_test" ]; then
        echo "Generating test audio data..."
        mkdir -p "../$TEST_RESULTS_DIR/test_data"
        
        if ./test_data_generator_test > "../$TEST_RESULTS_DIR/test_data_generation.log" 2>&1; then
            echo -e "${GREEN}  Test data generated successfully${NC}"
        else
            echo -e "${YELLOW}  Test data generation failed (non-critical)${NC}"
        fi
    else
        echo -e "${YELLOW}  Test data generator not available${NC}"
    fi
    
    cd ../..
}

# Function to run linting and code quality checks
run_code_quality_checks() {
    print_section "Running Code Quality Checks"
    
    # Frontend linting
    cd "$FRONTEND_DIR"
    
    echo "Running frontend linting..."
    if npm run lint > "../$TEST_RESULTS_DIR/frontend_lint.log" 2>&1; then
        echo -e "${GREEN}  Frontend linting PASSED${NC}"
    else
        echo -e "${YELLOW}  Frontend linting found issues${NC}"
        echo "    See $TEST_RESULTS_DIR/frontend_lint.log for details"
    fi
    
    cd ..
    
    # Backend code analysis (if tools are available)
    if command -v cppcheck &> /dev/null; then
        echo "Running backend static analysis..."
        cppcheck --enable=all --xml --xml-version=2 "$BACKEND_DIR/src" 2> "$TEST_RESULTS_DIR/backend_cppcheck.xml"
        echo -e "${GREEN}  Backend static analysis completed${NC}"
    else
        echo -e "${YELLOW}  cppcheck not available, skipping backend static analysis${NC}"
    fi
}

# Function to generate final report
generate_final_report() {
    print_section "Generating Final Report"
    
    REPORT_FILE="$TEST_RESULTS_DIR/test_report.md"
    
    cat > "$REPORT_FILE" << EOF
# SpeechRNT Test Report

Generated on: $(date)

## Test Summary

### Backend Tests
- Passed: $BACKEND_TESTS_PASSED
- Failed: $BACKEND_TESTS_FAILED

### Frontend Tests  
- Passed: $FRONTEND_TESTS_PASSED
- Failed: $FRONTEND_TESTS_FAILED

### Total
- Passed: $((BACKEND_TESTS_PASSED + FRONTEND_TESTS_PASSED))
- Failed: $((BACKEND_TESTS_FAILED + FRONTEND_TESTS_FAILED))

## Performance Results

EOF

    if [ -f "$TEST_RESULTS_DIR/$PERFORMANCE_LOG" ]; then
        cat "$TEST_RESULTS_DIR/$PERFORMANCE_LOG" >> "$REPORT_FILE"
    else
        echo "No performance results available." >> "$REPORT_FILE"
    fi
    
    cat >> "$REPORT_FILE" << EOF

## Test Files

The following test output files are available:

EOF
    
    for file in "$TEST_RESULTS_DIR"/*.log; do
        if [ -f "$file" ]; then
            echo "- $(basename "$file")" >> "$REPORT_FILE"
        fi
    done
    
    echo ""
    echo -e "${GREEN}Final test report generated: $REPORT_FILE${NC}"
}

# Main execution
main() {
    local start_time=$(date +%s)
    local backend_result=0
    local frontend_result=0
    
    # Generate test data first
    generate_test_data
    
    # Run backend tests
    if ! run_backend_tests; then
        backend_result=1
    fi
    
    echo ""
    
    # Run frontend tests
    if ! run_frontend_tests; then
        frontend_result=1
    fi
    
    echo ""
    
    # Run code quality checks
    run_code_quality_checks
    
    echo ""
    
    # Generate final report
    generate_final_report
    
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  Test Suite Complete                   ${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo "Total execution time: ${duration}s"
    echo "Results directory: $TEST_RESULTS_DIR"
    
    if [ $backend_result -eq 0 ] && [ $frontend_result -eq 0 ]; then
        echo -e "${GREEN}All tests passed successfully!${NC}"
        exit 0
    else
        echo -e "${RED}Some tests failed. Check the logs for details.${NC}"
        exit 1
    fi
}

# Handle command line arguments
case "${1:-}" in
    "backend")
        run_backend_tests
        ;;
    "frontend")
        run_frontend_tests
        ;;
    "performance")
        run_backend_tests
        run_frontend_tests
        echo "Performance results available in $TEST_RESULTS_DIR/$PERFORMANCE_LOG"
        ;;
    "help"|"-h"|"--help")
        echo "Usage: $0 [backend|frontend|performance|help]"
        echo ""
        echo "Options:"
        echo "  backend     Run only backend tests"
        echo "  frontend    Run only frontend tests"
        echo "  performance Run performance tests only"
        echo "  help        Show this help message"
        echo ""
        echo "Default: Run all tests"
        ;;
    *)
        main
        ;;
esac