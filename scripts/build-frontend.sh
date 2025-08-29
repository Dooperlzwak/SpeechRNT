#!/bin/bash

# Frontend build script

set -e

echo "Building frontend..."

cd frontend

# Install dependencies if needed
if [ ! -d "node_modules" ]; then
    echo "Installing dependencies..."
    npm install
fi

# Run linting
echo "Running ESLint..."
npm run lint

# Run tests
echo "Running tests..."
npm test -- --run

# Build for production
echo "Building for production..."
npm run build

echo "Frontend build complete!"
echo "Output: frontend/dist/"