#!/bin/bash

set -e

MODELS_DIR=${MODELS_DIR:-/models}

echo "Starting AI model downloads to $MODELS_DIR..."

# Create model directories
mkdir -p "$MODELS_DIR/whisper"
mkdir -p "$MODELS_DIR/marian"
mkdir -p "$MODELS_DIR/coqui"

# Download Whisper models
echo "Downloading Whisper models..."
cd "$MODELS_DIR/whisper"

# Download different Whisper model sizes
wget -nc https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin || true
wget -nc https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin || true

# Download Marian translation models (common language pairs)
echo "Downloading Marian translation models..."
cd "$MODELS_DIR/marian"

# English <-> Spanish
python3 /scripts/download-models.py --model "Helsinki-NLP/opus-mt-en-es" --output "en-es"
python3 /scripts/download-models.py --model "Helsinki-NLP/opus-mt-es-en" --output "es-en"

# English <-> French
python3 /scripts/download-models.py --model "Helsinki-NLP/opus-mt-en-fr" --output "en-fr"
python3 /scripts/download-models.py --model "Helsinki-NLP/opus-mt-fr-en" --output "fr-en"

# English <-> German
python3 /scripts/download-models.py --model "Helsinki-NLP/opus-mt-en-de" --output "en-de"
python3 /scripts/download-models.py --model "Helsinki-NLP/opus-mt-de-en" --output "de-en"

# Download Coqui TTS models
echo "Downloading Coqui TTS models..."
cd "$MODELS_DIR/coqui"

# Download common TTS models
python3 /scripts/download-models.py --model "coqui/XTTS-v2" --output "xtts-v2"

echo "Model downloads completed successfully!"

# Set permissions
chmod -R 755 "$MODELS_DIR"

echo "All models downloaded to $MODELS_DIR"