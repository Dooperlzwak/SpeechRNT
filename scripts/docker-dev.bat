@echo off
setlocal

set "PROJECT_ROOT=%~dp0.."
cd /d "%PROJECT_ROOT%"

if "%1"=="start" (
    echo Starting development environment...
    docker-compose -f docker-compose.yml -f docker-compose.dev.yml up -d
    echo Development environment started!
    echo Frontend: http://localhost:3000
    echo Backend WebSocket: ws://localhost:8080
    echo Adminer: http://localhost:8081
    goto :eof
)

if "%1"=="stop" (
    echo Stopping development environment...
    docker-compose -f docker-compose.yml -f docker-compose.dev.yml down
    goto :eof
)

if "%1"=="restart" (
    echo Restarting development environment...
    docker-compose -f docker-compose.yml -f docker-compose.dev.yml restart
    goto :eof
)

if "%1"=="build" (
    echo Building development containers...
    docker-compose -f docker-compose.yml -f docker-compose.dev.yml build
    goto :eof
)

if "%1"=="logs" (
    if "%2"=="" (
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml logs -f
    ) else (
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml logs -f %2
    )
    goto :eof
)

if "%1"=="shell" (
    set "service=%2"
    if "%service%"=="" set "service=backend"
    echo Opening shell in %service% container...
    docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec %service% /bin/bash
    goto :eof
)

if "%1"=="clean" (
    echo Cleaning up Docker resources...
    docker-compose -f docker-compose.yml -f docker-compose.dev.yml down -v
    docker system prune -f
    goto :eof
)

if "%1"=="models" (
    echo Downloading AI models...
    docker-compose -f docker-compose.yml -f docker-compose.dev.yml run --rm model-downloader
    goto :eof
)

echo SpeechRNT Development Docker Manager
echo.
echo Usage: %0 ^<command^> [service]
echo.
echo Commands:
echo   start    - Start development environment
echo   stop     - Stop development environment
echo   restart  - Restart development environment
echo   build    - Build development containers
echo   logs     - Show logs (optionally for specific service)
echo   shell    - Open shell in container (default: backend)
echo   clean    - Clean up Docker resources
echo   models   - Download AI models
echo   help     - Show this help message