#!/bin/bash

# Setup uWebSockets for SpeechRNT backend

set -e

THIRD_PARTY_DIR="third_party"
UWEBSOCKETS_DIR="$THIRD_PARTY_DIR/uWebSockets"

echo "Setting up uWebSockets..."

# Create third_party directory if it doesn't exist
mkdir -p "$THIRD_PARTY_DIR"

# Clone uWebSockets if not already present
if [ ! -d "$UWEBSOCKETS_DIR" ]; then
    echo "Cloning uWebSockets..."
    git clone https://github.com/uNetworking/uWebSockets.git "$UWEBSOCKETS_DIR"
    cd "$UWEBSOCKETS_DIR"
    git checkout v20.48.0  # Use a stable version
    cd ../..
else
    echo "uWebSockets already exists"
fi

echo "uWebSockets setup complete!"