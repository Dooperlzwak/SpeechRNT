@echo off
setlocal enabledelayedexpansion

echo Setting up whisper.cpp...

set WHISPER_DIR=third_party\whisper.cpp
set WHISPER_REPO=https://github.com/ggerganov/whisper.cpp.git

REM Create third_party directory if it doesn't exist
if not exist "third_party" mkdir third_party

REM Clone whisper.cpp if not already present
if not exist "%WHISPER_DIR%" (
    echo Cloning whisper.cpp repository...
    git clone %WHISPER_REPO% %WHISPER_DIR%
) else (
    echo whisper.cpp already exists, updating...
    cd %WHISPER_DIR%
    git pull
    cd ..\..
)

echo whisper.cpp setup complete!
echo To build whisper.cpp, navigate to %WHISPER_DIR% and run the appropriate build commands for your platform.
echo To download models, run the model download script when available.