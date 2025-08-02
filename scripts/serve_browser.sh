#!/bin/bash

# Simple server script to serve the browser version
echo "Starting local server for 3D Raster browser version..."
echo "Serving files from public/ directory"
echo ""
echo "Open your browser and go to: http://localhost:8000"
echo "Press Ctrl+C to stop the server"
echo ""

cd public

# Try different Python versions
if command -v python3 &> /dev/null; then
    python3 -m http.server 8000
elif command -v python &> /dev/null; then
    python -m http.server 8000
else
    echo "Error: Python not found. Please install Python or use another web server."
    echo "You can also use:"
    echo "  npx serve public"
    echo "  or"
    echo "  php -S localhost:8000 -t public"
    exit 1
fi 