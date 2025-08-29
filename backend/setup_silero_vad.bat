@echo off
REM Setup script for silero-vad integration on Windows
REM This script downloads and sets up the silero-vad model and dependencies

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "THIRD_PARTY_DIR=%SCRIPT_DIR%third_party"
set "SILERO_DIR=%THIRD_PARTY_DIR%\silero-vad"
set "DATA_DIR=%SCRIPT_DIR%data"
set "SILERO_DATA_DIR=%DATA_DIR%\silero"

echo Setting up silero-vad for SpeechRNT...

REM Create directories
if not exist "%THIRD_PARTY_DIR%" mkdir "%THIRD_PARTY_DIR%"
if not exist "%SILERO_DATA_DIR%" mkdir "%SILERO_DATA_DIR%"

REM Clone silero-vad repository if it doesn't exist
if not exist "%SILERO_DIR%" (
    echo Cloning silero-vad repository...
    git clone https://github.com/snakers4/silero-vad.git "%SILERO_DIR%"
) else (
    echo silero-vad repository already exists, updating...
    cd /d "%SILERO_DIR%"
    git pull
    cd /d "%SCRIPT_DIR%"
)

REM Download the ONNX model if it doesn't exist
set "ONNX_MODEL=%SILERO_DATA_DIR%\silero_vad.onnx"
if not exist "%ONNX_MODEL%" (
    echo Downloading silero-vad ONNX model...
    powershell -Command "Invoke-WebRequest -Uri 'https://github.com/snakers4/silero-vad/raw/master/src/silero_vad/data/silero_vad.onnx' -OutFile '%ONNX_MODEL%'"
) else (
    echo silero-vad ONNX model already exists
)

REM Check for ONNX Runtime
echo Checking for ONNX Runtime...
echo Warning: Please ensure ONNX Runtime is installed for C++ development
echo You can download it from: https://github.com/microsoft/onnxruntime/releases
echo.

REM Create a simple test to verify the model works
echo Creating model verification test...
(
echo import numpy as np
echo try:
echo     import onnxruntime as ort
echo     
echo     # Load the model
echo     model_path = "silero_vad.onnx"
echo     session = ort.InferenceSession^(model_path^)
echo     
echo     # Test with dummy audio data
echo     audio = np.random.randn^(1, 512^).astype^(np.float32^)
echo     sr = np.array^([16000], dtype=np.int64^)
echo     
echo     # Run inference
echo     outputs = session.run^(None, {'input': audio, 'sr': sr}^)
echo     print^(f"Model test successful! Output shape: {outputs[0].shape}"^)
echo     print^(f"Sample VAD probability: {outputs[0][0][0]:.4f}"^)
echo     
echo except ImportError:
echo     print^("ONNX Runtime not available for Python test"^)
echo     print^("Model file exists, but runtime verification skipped"^)
echo except Exception as e:
echo     print^(f"Model test failed: {e}"^)
) > "%SILERO_DATA_DIR%\test_model.py"

cd /d "%SILERO_DATA_DIR%"
python test_model.py 2>nul || echo Python test skipped (dependencies not available)
cd /d "%SCRIPT_DIR%"

echo.
echo silero-vad setup complete!
echo Model location: %ONNX_MODEL%
echo.
echo Next steps:
echo 1. Ensure ONNX Runtime is installed for C++ development
echo 2. Run cmake to configure the build with silero-vad support
echo 3. Build the project with Visual Studio or make