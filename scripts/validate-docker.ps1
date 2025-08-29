# Docker Setup Validation Script

Write-Host "🐳 Validating Docker setup for SpeechRNT..." -ForegroundColor Cyan

# Check Docker installation
try {
    $dockerVersion = docker --version
    Write-Host "✅ Docker installed: $dockerVersion" -ForegroundColor Green
} catch {
    Write-Host "❌ Docker not found. Please install Docker Desktop." -ForegroundColor Red
    exit 1
}

# Check Docker Compose
try {
    $composeVersion = docker-compose --version
    Write-Host "✅ Docker Compose available: $composeVersion" -ForegroundColor Green
} catch {
    Write-Host "❌ Docker Compose not found." -ForegroundColor Red
    exit 1
}

# Check if Docker daemon is running
try {
    docker info | Out-Null
    Write-Host "✅ Docker daemon is running" -ForegroundColor Green
} catch {
    Write-Host "❌ Docker daemon is not running. Please start Docker Desktop." -ForegroundColor Red
    exit 1
}

# Validate docker-compose files
$composeFiles = @("docker-compose.yml", "docker-compose.dev.yml")
foreach ($file in $composeFiles) {
    if (Test-Path $file) {
        Write-Host "✅ Found $file" -ForegroundColor Green
        
        # Validate compose file syntax
        try {
            docker-compose -f $file config | Out-Null
            Write-Host "✅ $file syntax is valid" -ForegroundColor Green
        } catch {
            Write-Host "❌ $file has syntax errors" -ForegroundColor Red
            Write-Host $_.Exception.Message -ForegroundColor Red
        }
    } else {
        Write-Host "❌ Missing $file" -ForegroundColor Red
    }
}

# Check required directories
$requiredDirs = @("frontend", "backend", "scripts")
foreach ($dir in $requiredDirs) {
    if (Test-Path $dir) {
        Write-Host "✅ Found $dir directory" -ForegroundColor Green
    } else {
        Write-Host "❌ Missing $dir directory" -ForegroundColor Red
    }
}

# Check Dockerfiles
$dockerfiles = @("backend/Dockerfile", "frontend/Dockerfile", "scripts/Dockerfile.models")
foreach ($dockerfile in $dockerfiles) {
    if (Test-Path $dockerfile) {
        Write-Host "✅ Found $dockerfile" -ForegroundColor Green
    } else {
        Write-Host "❌ Missing $dockerfile" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "🎉 Docker validation complete!" -ForegroundColor Magenta
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Run: .\scripts\docker-dev.ps1 build" -ForegroundColor Gray
Write-Host "2. Run: .\scripts\docker-dev.ps1 start" -ForegroundColor Gray
Write-Host "3. Access frontend at http://localhost:3000" -ForegroundColor Gray