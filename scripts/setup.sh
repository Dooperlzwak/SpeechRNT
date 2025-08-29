#!/bin/bash

# SpeechRNT Development Environment Setup Script

set -e

echo "Setting up SpeechRNT development environment..."

# Check system requirements
echo "Checking system requirements..."

# Check for Node.js
if ! command -v node &> /dev/null; then
    echo "Error: Node.js is not installed. Please install Node.js 18+ and npm."
    exit 1
fi

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo "Error: CMake is not installed. Please install CMake 3.16+."
    exit 1
fi

# Check for C++ compiler
if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
    echo "Error: No C++ compiler found. Please install GCC 9+ or Clang 10+."
    exit 1
fi

echo "System requirements satisfied."

# Setup frontend
echo "Setting up frontend..."
cd frontend
if [ ! -d "node_modules" ]; then
    npm install
fi
cd ..

# Setup backend build directory
echo "Setting up backend build directory..."
mkdir -p backend/build
mkdir -p backend/data/whisper
mkdir -p backend/data/marian  
mkdir -p backend/data/coqui

# Build backend
echo "Building backend..."
cd backend/build
cmake ..
make -j$(nproc)
cd ../..

echo "Development environment setup complete!"
echo ""
echo "To start development:"
echo "  Frontend: cd frontend && npm run dev"
echo "  Backend:  cd backend/build && ./SpeechRNT"