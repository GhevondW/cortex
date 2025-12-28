#!/bin/bash
# Development helper script for Cortex

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_usage() {
    echo "Cortex Development Helper"
    echo ""
    echo "Usage: ./dev.sh [command]"
    echo ""
    echo "Commands:"
    echo "  test-native       Run native tests"
    echo "  test-wasm         Run WASM tests"
    echo "  test-all          Run all tests"
    echo "  example-native    Build and run native example"
    echo "  example-wasm      Build and run WASM example"
    echo "  serve             Serve WASM example in browser"
    echo "  clean             Clean all build artifacts"
    echo "  format            Format all C++ code"
    echo "  shell             Open shell in development container"
    echo "  help              Show this help message"
}

run_command() {
    echo -e "${GREEN}Running: $1${NC}"
    docker compose up --build "$2"
}

case "${1:-help}" in
    test-native)
        run_command "Native Tests" "test-native"
        ;;
    test-wasm)
        run_command "WASM Tests" "test-wasm"
        ;;
    test-all)
        echo -e "${GREEN}Running all tests...${NC}"
        docker compose up --build test-native
        docker compose up --build test-wasm
        echo -e "${GREEN}✓ All tests passed!${NC}"
        ;;
    example-native)
        run_command "Native Example" "build-example-native"
        ;;
    example-wasm)
        run_command "WASM Example" "build-example-wasm"
        ;;
    serve)
        echo -e "${GREEN}Starting web server...${NC}"
        echo -e "${YELLOW}Open http://localhost:8080/examples/index.html in your browser${NC}"
        docker compose up serve-example
        ;;
    clean)
        echo -e "${YELLOW}Cleaning build artifacts...${NC}"
        rm -rf build/
        docker compose down
        echo -e "${GREEN}✓ Clean complete${NC}"
        ;;
    format)
        if [ -f "./format" ]; then
            echo -e "${GREEN}Formatting code...${NC}"
            ./format
            echo -e "${GREEN}✓ Format complete${NC}"
        else
            echo -e "${RED}Format script not found${NC}"
            exit 1
        fi
        ;;
    shell)
        echo -e "${GREEN}Opening development shell...${NC}"
        docker compose run --rm test-native bash
        ;;
    help)
        print_usage
        ;;
    *)
        echo -e "${RED}Unknown command: $1${NC}"
        echo ""
        print_usage
        exit 1
        ;;
esac

