#!/bin/bash
set -e

# Get absolute path to repo root
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASE_MODELS_DIR="$REPO_ROOT/backend/data"

echo "Starting AI model downloads to $BASE_MODELS_DIR..."

# Create base directory
mkdir -p "$BASE_MODELS_DIR"

# Download Whisper models
echo "Downloading Whisper models..."
mkdir -p "$BASE_MODELS_DIR/whisper"
cd "$BASE_MODELS_DIR/whisper"
wget -nc https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin || true
wget -nc https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin || true

# Helper function to call python script
download_model() {
    local subdir=$1
    local model=$2
    local output=$3
    
    # We set MODELS_DIR to the specific subdirectory so the python script
    # puts the output directly inside it (e.g. backend/data/marian/en-es)
    export MODELS_DIR="$BASE_MODELS_DIR/$subdir"
    mkdir -p "$MODELS_DIR"
    
    echo "Downloading $model to $MODELS_DIR/$output..."
    "$REPO_ROOT/.venv/bin/python3" "$REPO_ROOT/scripts/download-models.py" --model "$model" --output "$output"
}

# Download Marian translation models
echo "Downloading Marian translation models..."
# English <-> Spanish
download_model "marian" "Helsinki-NLP/opus-mt-en-es" "en-es"
download_model "marian" "Helsinki-NLP/opus-mt-es-en" "es-en"

# English <-> French
download_model "marian" "Helsinki-NLP/opus-mt-en-fr" "en-fr"
download_model "marian" "Helsinki-NLP/opus-mt-fr-en" "fr-en"

# English <-> German
download_model "marian" "Helsinki-NLP/opus-mt-en-de" "en-de"
download_model "marian" "Helsinki-NLP/opus-mt-de-en" "de-en"

# Download Coqui TTS models
echo "Downloading Coqui TTS models..."
download_model "coqui" "coqui/XTTS-v2" "xtts-v2"

echo "Model downloads completed successfully!"
echo "All models downloaded to $BASE_MODELS_DIR"
