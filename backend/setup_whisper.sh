#!/bin/bash

# Setup script for whisper.cpp integration
set -e

WHISPER_DIR="third_party/whisper.cpp"
WHISPER_REPO="https://github.com/ggerganov/whisper.cpp.git"

echo "Setting up whisper.cpp..."

# Create third_party directory if it doesn't exist
mkdir -p third_party

# Clone whisper.cpp if not already present
if [ ! -d "$WHISPER_DIR" ]; then
    echo "Cloning whisper.cpp repository..."
    git clone "$WHISPER_REPO" "$WHISPER_DIR"
else
    echo "whisper.cpp already exists, updating..."
    cd "$WHISPER_DIR"
    git pull
    cd ../..
fi

# Build whisper.cpp
echo "Building whisper.cpp..."
cd "$WHISPER_DIR"
make clean || true
make -j$(nproc)

echo "whisper.cpp setup complete!"
echo "To download models, run: bash scripts/download-models.sh"