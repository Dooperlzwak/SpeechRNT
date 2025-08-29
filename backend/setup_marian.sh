#!/bin/bash

# Setup script for Marian NMT integration
# This script downloads and sets up Marian NMT for the SpeechRNT project

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_PARTY_DIR="$SCRIPT_DIR/third_party"
MARIAN_DIR="$THIRD_PARTY_DIR/marian-nmt"
DATA_DIR="$SCRIPT_DIR/data/marian"

echo "Setting up Marian NMT for SpeechRNT..."

# Create directories
mkdir -p "$THIRD_PARTY_DIR"
mkdir -p "$DATA_DIR"

# Check if Marian is already installed
if [ -d "$MARIAN_DIR" ]; then
    echo "Marian NMT already exists in $MARIAN_DIR"
    echo "To reinstall, remove the directory and run this script again"
else
    echo "Cloning Marian NMT..."
    cd "$THIRD_PARTY_DIR"
    git clone https://github.com/marian-nmt/marian-dev.git marian-nmt
    cd marian-nmt
    
    echo "Building Marian NMT..."
    mkdir -p build
    cd build
    
    # Configure with CMake
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCOMPILE_CPU=ON \
        -DCOMPILE_CUDA=OFF \
        -DUSE_SENTENCEPIECE=ON \
        -DUSE_FBGEMM=OFF
    
    # Build
    make -j$(nproc)
    
    echo "Marian NMT built successfully"
fi

# Create sample model directory structure
echo "Creating sample model directory structure..."

# English-Spanish model directory
EN_ES_DIR="$DATA_DIR/en-es"
mkdir -p "$EN_ES_DIR"

# Create placeholder model files (these would be replaced with actual models)
cat > "$EN_ES_DIR/config.yml" << EOF
# Marian NMT model configuration for English -> Spanish
# This is a placeholder configuration

# Model architecture
type: transformer
dim-vocabs: [32000, 32000]
dim-emb: 512
dim-rnn: 512
enc-depth: 6
dec-depth: 6
transformer-heads: 8
transformer-postprocess-emb: d
transformer-postprocess: dan
transformer-dropout: 0.1
transformer-dropout-attention: 0.1
transformer-dropout-ffn: 0.1
transformer-ffn-depth: 2
transformer-ffn-activation: relu

# Training parameters (not used for inference)
learn-rate: 0.0003
lr-warmup: 16000
lr-decay-inv-sqrt: 16000
lr-report: true
optimizer-params: []

# Inference parameters
beam-size: 5
normalize: 1.0
word-penalty: 0.0

# Vocabulary
vocabs: ["vocab.yml", "vocab.yml"]
EOF

cat > "$EN_ES_DIR/vocab.yml" << EOF
# Placeholder vocabulary file for English-Spanish model
# In a real implementation, this would contain the actual vocabulary
# Format: word: id

<unk>: 0
<s>: 1
</s>: 2
hello: 3
hola: 4
how: 5
como: 6
are: 7
estas: 8
you: 9
tu: 10
EOF

# Create a placeholder model file
echo "# Placeholder Marian model file" > "$EN_ES_DIR/model.npz"
echo "# This would be replaced with an actual trained model" >> "$EN_ES_DIR/model.npz"

# Spanish-English model directory
ES_EN_DIR="$DATA_DIR/es-en"
mkdir -p "$ES_EN_DIR"
cp "$EN_ES_DIR/config.yml" "$ES_EN_DIR/"
cp "$EN_ES_DIR/vocab.yml" "$ES_EN_DIR/"
cp "$EN_ES_DIR/model.npz" "$ES_EN_DIR/"

echo "Sample model structure created in $DATA_DIR"

# Create a download script for actual models
cat > "$DATA_DIR/download_models.sh" << 'EOF'
#!/bin/bash

# Script to download actual Marian NMT models
# This is a placeholder - actual model URLs would be added here

echo "Downloading Marian NMT models..."
echo "Note: This is a placeholder script."
echo "In a production setup, you would download actual trained models from:"
echo "- Opus-MT models: https://github.com/Helsinki-NLP/Opus-MT"
echo "- Hugging Face models: https://huggingface.co/models?library=marian"
echo ""
echo "Example commands:"
echo "wget https://object.pouta.csc.fi/Opus-MT-models/en-es/opus-mt-en-es.zip"
echo "unzip opus-mt-en-es.zip -d en-es/"
echo ""
echo "For now, using placeholder models for development and testing."

EOF

chmod +x "$DATA_DIR/download_models.sh"

echo ""
echo "Marian NMT setup completed!"
echo ""
echo "Directory structure:"
echo "  - Marian source: $MARIAN_DIR"
echo "  - Model data: $DATA_DIR"
echo ""
echo "Next steps:"
echo "1. Download actual trained models using $DATA_DIR/download_models.sh"
echo "2. Update model paths in backend/config/models.json if needed"
echo "3. Build the backend with 'cd backend/build && make'"
echo ""
echo "Note: Currently using placeholder models for development."
echo "For production use, download actual Opus-MT or other trained models."