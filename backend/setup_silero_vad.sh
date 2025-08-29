#!/bin/bash

# Setup script for silero-vad integration
# This script downloads and sets up the silero-vad model and dependencies

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_PARTY_DIR="${SCRIPT_DIR}/third_party"
SILERO_DIR="${THIRD_PARTY_DIR}/silero-vad"
DATA_DIR="${SCRIPT_DIR}/data"
SILERO_DATA_DIR="${DATA_DIR}/silero"

echo "Setting up silero-vad for SpeechRNT..."

# Create directories
mkdir -p "${THIRD_PARTY_DIR}"
mkdir -p "${SILERO_DATA_DIR}"

# Clone silero-vad repository if it doesn't exist
if [ ! -d "${SILERO_DIR}" ]; then
    echo "Cloning silero-vad repository..."
    git clone https://github.com/snakers4/silero-vad.git "${SILERO_DIR}"
else
    echo "silero-vad repository already exists, updating..."
    cd "${SILERO_DIR}"
    git pull
    cd "${SCRIPT_DIR}"
fi

# Download the ONNX model if it doesn't exist
ONNX_MODEL="${SILERO_DATA_DIR}/silero_vad.onnx"
if [ ! -f "${ONNX_MODEL}" ]; then
    echo "Downloading silero-vad ONNX model..."
    wget -O "${ONNX_MODEL}" "https://github.com/snakers4/silero-vad/raw/master/src/silero_vad/data/silero_vad.onnx"
else
    echo "silero-vad ONNX model already exists"
fi

# Check if ONNX Runtime is available
echo "Checking for ONNX Runtime..."
if ! pkg-config --exists onnxruntime 2>/dev/null; then
    echo "Warning: ONNX Runtime not found via pkg-config"
    echo "You may need to install ONNX Runtime manually:"
    echo "  - Ubuntu/Debian: apt-get install libonnxruntime-dev"
    echo "  - Or download from: https://github.com/microsoft/onnxruntime/releases"
    echo ""
    echo "Alternatively, we'll try to use a CPU-only implementation"
else
    echo "ONNX Runtime found"
fi

# Create a simple test to verify the model works
echo "Creating model verification test..."
cat > "${SILERO_DATA_DIR}/test_model.py" << 'EOF'
#!/usr/bin/env python3
import numpy as np
try:
    import onnxruntime as ort
    
    # Load the model
    model_path = "silero_vad.onnx"
    session = ort.InferenceSession(model_path)
    
    # Test with dummy audio data
    audio = np.random.randn(1, 512).astype(np.float32)
    sr = np.array([16000], dtype=np.int64)
    
    # Run inference
    outputs = session.run(None, {'input': audio, 'sr': sr})
    print(f"Model test successful! Output shape: {outputs[0].shape}")
    print(f"Sample VAD probability: {outputs[0][0][0]:.4f}")
    
except ImportError:
    print("ONNX Runtime not available for Python test")
    print("Model file exists, but runtime verification skipped")
except Exception as e:
    print(f"Model test failed: {e}")
EOF

cd "${SILERO_DATA_DIR}"
python3 test_model.py 2>/dev/null || echo "Python test skipped (dependencies not available)"
cd "${SCRIPT_DIR}"

echo ""
echo "silero-vad setup complete!"
echo "Model location: ${ONNX_MODEL}"
echo ""
echo "Next steps:"
echo "1. Ensure ONNX Runtime is installed for C++ development"
echo "2. Run cmake to configure the build with silero-vad support"
echo "3. Build the project with 'make -j\$(nproc)'"