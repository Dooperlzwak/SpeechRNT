#!/bin/bash

# Backend build script

set -e

echo "Building backend..."

# Create build directory if it doesn't exist
mkdir -p backend/build
cd backend/build

# Configure with CMake
echo "Configuring with CMake..."
cmake .. "$@"

# Build with make
echo "Compiling..."
make -j$(nproc)

# Run tests if available
if [ -f "unit_tests" ]; then
    echo "Running unit tests..."
    ./unit_tests
fi

if [ -f "integration_tests" ]; then
    echo "Running integration tests..."
    ./integration_tests
fi

echo "Backend build complete!"
echo "Executable: backend/build/bin/SpeechRNT"