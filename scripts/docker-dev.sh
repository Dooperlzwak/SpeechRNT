#!/bin/bash

# Development Docker management script

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_ROOT"

case "${1:-help}" in
    "start")
        echo "Starting development environment..."
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml up -d
        echo "Development environment started!"
        echo "Frontend: http://localhost:3000"
        echo "Backend WebSocket: ws://localhost:8080"
        echo "Adminer: http://localhost:8081"
        ;;
    
    "stop")
        echo "Stopping development environment..."
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml down
        ;;
    
    "restart")
        echo "Restarting development environment..."
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml restart
        ;;
    
    "build")
        echo "Building development containers..."
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml build
        ;;
    
    "logs")
        service="${2:-}"
        if [ -n "$service" ]; then
            docker-compose -f docker-compose.yml -f docker-compose.dev.yml logs -f "$service"
        else
            docker-compose -f docker-compose.yml -f docker-compose.dev.yml logs -f
        fi
        ;;
    
    "shell")
        service="${2:-backend}"
        echo "Opening shell in $service container..."
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec "$service" /bin/bash
        ;;
    
    "clean")
        echo "Cleaning up Docker resources..."
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml down -v
        docker system prune -f
        ;;
    
    "models")
        echo "Downloading AI models..."
        docker-compose -f docker-compose.yml -f docker-compose.dev.yml run --rm model-downloader
        ;;
    
    "help"|*)
        echo "SpeechRNT Development Docker Manager"
        echo ""
        echo "Usage: $0 <command>"
        echo ""
        echo "Commands:"
        echo "  start    - Start development environment"
        echo "  stop     - Stop development environment"
        echo "  restart  - Restart development environment"
        echo "  build    - Build development containers"
        echo "  logs     - Show logs (optionally for specific service)"
        echo "  shell    - Open shell in container (default: backend)"
        echo "  clean    - Clean up Docker resources"
        echo "  models   - Download AI models"
        echo "  help     - Show this help message"
        ;;
esac