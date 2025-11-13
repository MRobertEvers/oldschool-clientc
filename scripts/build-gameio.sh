#!/bin/bash

# Convenience script to build GameIO.ts from project root with debug info
# This script can be run from anywhere in the project

set -e  # Exit on error

# Get project root
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

# Change to browser platform directory and run build
cd "$PROJECT_ROOT/src/platforms/browser"

echo "Building GameIO with debug info from: $PWD"

# Set debug environment variable for build script
export BUILD_MODE="debug"
./build.sh



