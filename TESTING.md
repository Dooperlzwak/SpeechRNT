# Vocr Testing Suite

This document provides comprehensive information about the testing infrastructure for the Vocr project.

## Overview

The Vocr testing suite includes:

- **Unit Tests**: Test individual components in isolation
- **Integration Tests**: Test component interactions and workflows
- **End-to-End Tests**: Test complete user scenarios
- **Performance Tests**: Measure and validate system performance
- **User Acceptance Tests**: Validate user experience and requirements
- **Quality Validation**: Assess translation and audio quality
- **Load Testing**: Validate system behavior under stress

## Quick Start

### Running All Tests

```bash
# Cross-platform test runner (recommended)
./scripts/run-all-tests.sh        # Linux/macOS
scripts/run-all-tests.bat         # Windows (CMD)
scripts/run-all-tests.ps1         # Windows (PowerShell)

# Or run specific test suites
./scripts/run-all-tests.sh backend
./scripts/run-all-tests.sh frontend
./scripts/run-all-tests.sh performance
```

### Frontend Tests Only

```bash
cd frontend
npm test                    # Interactive mode
npm run test:run           # Single run
npm run test:run -- --coverage  # With coverage
npm run test:ui            # UI mode
```

### Backend Tests Only

```bash
cd backend
mkdir build && cd build
cmake .. -DENABLE_TESTING=ON
make -j$(nproc)
ctest --output-on-failure
```

## Test Structure

### Frontend Tests (`frontend/src/`)

```
src/
├── components/__tests__/          # Component unit tests
├── hooks/__tests__/              # Hook unit tests  
├── services/__tests__/           # Service unit tests
├── store/__tests__/              # State management tests
├── utils/__tests__/              # Utility function tests
└── test/                         # Integration & E2E tests
    ├── conversationFlowIntegration.test.tsx
    ├── performanceTests.test.tsx
    ├── userAcceptanceTests.test.tsx
    └── setup.ts                  # Test configuration
```

### Backend Tests (`backend/tests/`)

```
tests/
├── unit/                         # Unit tests
│   ├── test_websocket_message_protocol.cpp
│   ├── test_audio_buffer.cpp
│   ├── test_task_queue.cpp
│   └── ...
├── integration/                  # Integration tests
│   ├── test_end_to_end_conversation.cpp
│   ├── test_translation_pipeline.cpp
│   └── ...
├── performance/                  # Performance tests
│   ├── load_testing.cpp
│   ├── latency_benchmark.cpp
│   └── ...
├── validation/                   # Quality validation
│   ├── translation_quality_validator.cpp
│   ├── audio_quality_validator.cpp
│   └── ...
└── fixtures/                     # Test data and utilities
    ├── test_data_generator.cpp
    └── ...
```

## Test Categories

### 1. Unit Tests

Test individual components in isolation with mocked dependencies.

**Frontend Unit Tests:**
- Component rendering and behavior
- Hook functionality
- Service methods
- State management
- Utility functions

**Backend Unit Tests:**
- Class methods and functions
- Message protocol handling
- Audio processing algorithms
- Translation pipeline components
- Error handling

### 2. Integration Tests

Test component interactions and data flow.

**Frontend Integration Tests:**
- WebSocket communication
- Audio capture and playback
- State synchronization
- Error handling flows

**Backend Integration Tests:**
- End-to-end conversation flow
- Pipeline component integration
- WebSocket lifecycle
- VAD pipeline integration

### 3. Performance Tests

Measure and validate system performance characteristics.

**Metrics Tested:**
- Response latency (< 2 seconds end-to-end)
- Memory usage and leaks
- Concurrent user handling
- Audio processing efficiency
- WebSocket throughput

### 4. User Acceptance Tests (UAT)

Validate complete user scenarios and requirements.

**Test Scenarios:**
- First-time user experience
- Basic conversation flow
- Multi-turn conversations
- Language switching
- Error recovery
- Mobile responsiveness
- Accessibility compliance

### 5. Quality Validation

Assess translation and audio quality using automated metrics.

**Translation Quality:**
- BLEU scores
- Semantic similarity
- Fluency assessment
- Error detection
- Adequacy measurement

**Audio Quality:**
- Signal-to-noise ratio
- Harmonic distortion
- Spectral analysis
- Speech intelligibility
- Artifact detection

## Test Data and Fixtures

### Generated Test Data

The test suite includes a comprehensive test data generator:

```bash
cd backend/build
./test_data_generator_test
```

**Generated Data:**
- Multi-language phrase pairs
- Audio scenarios (greetings, questions, emergencies)
- Noise patterns (white, pink, environmental)
- Conversation scenarios
- Edge cases and error conditions

### Test Scenarios

Pre-defined conversation scenarios for consistent testing:

- **Simple Greeting**: Basic hello/response flow
- **Noisy Environment**: Testing with background noise
- **Rapid Exchange**: Quick back-and-forth conversation
- **Overlapping Speech**: Handling simultaneous speakers
- **Long Monologue**: Extended speech processing

## Continuous Integration

### GitHub Actions Pipeline

The CI/CD pipeline (`.github/workflows/ci-cd-pipeline.yml`) includes:

1. **Code Quality Checks**
   - Linting (ESLint, clang-format)
   - Static analysis (cppcheck)
   - Type checking

2. **Multi-Platform Testing**
   - Ubuntu, Windows, macOS
   - Multiple Node.js versions
   - Different compilers (GCC, Clang)

3. **Test Execution**
   - Unit tests with coverage
   - Integration tests
   - Performance benchmarks
   - User acceptance tests

4. **Quality Gates**
   - Minimum test coverage (80%)
   - Performance thresholds
   - Zero critical security issues

5. **Reporting**
   - Test results and coverage
   - Performance metrics
   - Quality assessments

### Local CI Simulation

```bash
# Validate test structure
node scripts/validate-tests.js

# Generate comprehensive report
node scripts/generate-test-report.js
```

## Test Configuration

### Frontend Configuration

**Vitest Config** (`frontend/vitest.config.ts`):
```typescript
export default defineConfig({
  test: {
    globals: true,
    environment: 'jsdom',
    setupFiles: ['./src/test/setup.ts'],
    coverage: {
      reporter: ['text', 'html', 'lcov'],
      threshold: {
        global: {
          branches: 80,
          functions: 80,
          lines: 80,
          statements: 80
        }
      }
    }
  }
})
```

**Test Setup** (`frontend/src/test/setup.ts`):
- Mock Web Audio API
- Mock MediaRecorder
- Mock WebSocket
- Mock localStorage
- Global test utilities

### Backend Configuration

**CMake Configuration**:
```cmake
# Enable testing
option(ENABLE_TESTING "Enable testing" ON)
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)

if(ENABLE_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
```

**Test Dependencies**:
- Google Test (GTest)
- FFTW3 (for audio analysis)
- Threading libraries
- Optional: CUDA for GPU tests

## Quality Metrics and Thresholds

### Coverage Targets
- **Frontend**: 85% line coverage
- **Backend**: 80% line coverage
- **Critical paths**: 95% coverage

### Performance Targets
- **End-to-end latency**: < 2 seconds
- **VAD response**: < 100ms
- **Memory usage**: < 500MB per session
- **Concurrent users**: 50+ simultaneous

### Quality Thresholds
- **Translation BLEU**: > 0.4
- **Audio SNR**: > 20dB
- **Speech intelligibility**: > 0.8
- **Test reliability**: > 95% pass rate

## Troubleshooting

### Common Issues

**Frontend Tests Failing:**
```bash
# Clear cache and reinstall
rm -rf node_modules package-lock.json
npm install

# Update snapshots
npm test -- --updateSnapshot
```

**Backend Build Issues:**
```bash
# Clean build
rm -rf build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
```

**Permission Issues (Audio Tests):**
```bash
# Linux: Add user to audio group
sudo usermod -a -G audio $USER

# macOS: Grant microphone permission in System Preferences
```

### Debug Mode

Enable verbose logging for debugging:

```bash
# Frontend
DEBUG=1 npm test

# Backend
CTEST_OUTPUT_ON_FAILURE=1 ctest --verbose
```

## Contributing to Tests

### Adding New Tests

1. **Frontend Component Test:**
```typescript
// components/__tests__/NewComponent.test.tsx
import { render, screen } from '@testing-library/react';
import NewComponent from '../NewComponent';

describe('NewComponent', () => {
  it('should render correctly', () => {
    render(<NewComponent />);
    expect(screen.getByText('Expected Text')).toBeInTheDocument();
  });
});
```

2. **Backend Unit Test:**
```cpp
// tests/unit/test_new_feature.cpp
#include <gtest/gtest.h>
#include "new_feature.hpp"

TEST(NewFeatureTest, BasicFunctionality) {
    NewFeature feature;
    EXPECT_TRUE(feature.isWorking());
}
```

### Test Guidelines

1. **Naming Convention:**
   - Frontend: `ComponentName.test.tsx`
   - Backend: `test_component_name.cpp`

2. **Test Structure:**
   - Arrange: Set up test data
   - Act: Execute the functionality
   - Assert: Verify the results

3. **Mocking:**
   - Mock external dependencies
   - Use realistic test data
   - Avoid testing implementation details

4. **Performance:**
   - Keep tests fast (< 100ms each)
   - Use parallel execution
   - Clean up resources

## Reporting and Metrics

### Test Reports

Generated reports include:
- **HTML Report**: Comprehensive visual report
- **JSON Metrics**: Machine-readable metrics
- **Coverage Reports**: Line and branch coverage
- **Performance Metrics**: Latency and throughput data

### Accessing Reports

```bash
# Generate comprehensive report
node scripts/generate-test-report.js

# View reports
open test_results/comprehensive_test_report.html
```

### Metrics Dashboard

Key metrics tracked:
- Test execution trends
- Coverage evolution
- Performance regression detection
- Quality score tracking
- Failure rate analysis

## Support

For testing-related questions:

1. Check this documentation
2. Review existing test examples
3. Run test validation: `node scripts/validate-tests.js`
4. Check CI/CD pipeline logs
5. Create an issue with test logs and environment details

---

**Happy Testing! 🧪**