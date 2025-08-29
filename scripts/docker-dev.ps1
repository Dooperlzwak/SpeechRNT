# SpeechRNT Development Docker Manager (PowerShell)

param(
    [Parameter(Position=0)]
    [string]$Command = "help",
    
    [Parameter(Position=1)]
    [string]$Service = ""
)

$ProjectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $ProjectRoot

switch ($Command.ToLower()) {
    "start" {
        Write-Host "Starting development environment..." -ForegroundColor Green
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml up -d
        Write-Host "Development environment started!" -ForegroundColor Green
        Write-Host "Frontend: http://localhost:3000" -ForegroundColor Cyan
        Write-Host "Backend WebSocket: ws://localhost:8080" -ForegroundColor Cyan
        Write-Host "Adminer: http://localhost:8081" -ForegroundColor Cyan
    }
    
    "stop" {
        Write-Host "Stopping development environment..." -ForegroundColor Yellow
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml down
    }
    
    "restart" {
        Write-Host "Restarting development environment..." -ForegroundColor Yellow
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml restart
    }
    
    "build" {
        Write-Host "Building development containers..." -ForegroundColor Blue
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml build
    }
    
    "logs" {
        if ($Service) {
            docker-compose -f docker-compose.yml -f docker-compose.dev.yml logs -f $Service
        } else {
            docker-compose -f docker-compose.yml -f docker-compose.dev.yml logs -f
        }
    }
    
    "shell" {
        $TargetService = if ($Service) { $Service } else { "backend" }
        Write-Host "Opening shell in $TargetService container..." -ForegroundColor Cyan
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec $TargetService /bin/bash
    }
    
    "clean" {
        Write-Host "Cleaning up Docker resources..." -ForegroundColor Red
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml down -v
        docker system prune -f
    }
    
    "models" {
        Write-Host "Downloading AI models..." -ForegroundColor Blue
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml run --rm model-downloader
    }
    
    default {
        Write-Host "SpeechRNT Development Docker Manager" -ForegroundColor Magenta
        Write-Host ""
        Write-Host "Usage: .\scripts\docker-dev.ps1 <command> [service]" -ForegroundColor White
        Write-Host ""
        Write-Host "Commands:" -ForegroundColor White
        Write-Host "  start    - Start development environment" -ForegroundColor Gray
        Write-Host "  stop     - Stop development environment" -ForegroundColor Gray
        Write-Host "  restart  - Restart development environment" -ForegroundColor Gray
        Write-Host "  build    - Build development containers" -ForegroundColor Gray
        Write-Host "  logs     - Show logs (optionally for specific service)" -ForegroundColor Gray
        Write-Host "  shell    - Open shell in container (default: backend)" -ForegroundColor Gray
        Write-Host "  clean    - Clean up Docker resources" -ForegroundColor Gray
        Write-Host "  models   - Download AI models" -ForegroundColor Gray
        Write-Host "  help     - Show this help message" -ForegroundColor Gray
    }
}