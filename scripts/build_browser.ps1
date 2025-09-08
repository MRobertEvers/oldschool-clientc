# Build script for browser version using Emscripten (PowerShell)
param(
    [switch]$Clean,
    [switch]$Help
)

if ($Help) {
    Write-Host "Build script for 3D Raster browser version with Emscripten"
    Write-Host ""
    Write-Host "Usage: .\build_browser.ps1 [-Clean] [-Help]"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  -Clean    Clean build directory before building"
    Write-Host "  -Help     Show this help message"
    Write-Host ""
    Write-Host "Requirements:"
    Write-Host "  - Emscripten SDK installed and activated"
    Write-Host "  - CMake"
    Write-Host ""
    exit 0
}

Write-Host "Building 3D Raster for browser with Emscripten..." -ForegroundColor Green

# Check if emcmake is available
try {
    $null = Get-Command emcmake -ErrorAction Stop
} catch {
    Write-Host "Error: emcmake not found. Please install Emscripten SDK." -ForegroundColor Red
    Write-Host "Visit: https://emscripten.org/docs/getting_started/downloads.html" -ForegroundColor Yellow
    exit 1
}

# Clean build directory if requested
if ($Clean) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    if (Test-Path "build.em") {
        Remove-Item -Recurse -Force "build.em"
    }
}

# Create build directory if it doesn't exist
if (-not (Test-Path "build.em")) {
    New-Item -ItemType Directory -Path "build.em" | Out-Null
}

# Navigate to build directory
Push-Location "build.em"

try {
    # Configure with CMake
    Write-Host "Configuring with CMake..." -ForegroundColor Cyan
    emcmake cmake ..

    # Build the browser targets
    Write-Host "Building scene_tile_test_browser..." -ForegroundColor Cyan
    emmake ninja scene_tile_test_browser

    Write-Host "Building scene_tile_test_imgui_browser..." -ForegroundColor Cyan
    emmake ninja scene_tile_test_imgui_browser

    # Copy the cache to the public/cache directory
    Write-Host "Copying cache to public/cache directory..." -ForegroundColor Cyan
    if (-not (Test-Path "../public/cache")) {
        New-Item -ItemType Directory -Path "../public/cache" | Out-Null
    }
    Copy-Item -Path "../cache/*" -Destination "../public/cache/" -Recurse -Force

    # Copy files to public/build directory
    Write-Host "Copying files to public/build directory..." -ForegroundColor Cyan
    if (-not (Test-Path "../public/build")) {
        New-Item -ItemType Directory -Path "../public/build" | Out-Null
    }

    # Copy simple browser version
    Copy-Item -Path "scene_tile_test_browser.js" -Destination "../public/build/" -Force
    Copy-Item -Path "scene_tile_test_browser.wasm" -Destination "../public/build/" -Force
    Copy-Item -Path "scene_tile_test_browser.data" -Destination "../public/build/" -Force
    Copy-Item -Path "scene_tile_test_browser.html" -Destination "../public/build/" -Force

    # Copy ImGui browser version
    Copy-Item -Path "scene_tile_test_imgui_browser.js" -Destination "../public/build/" -Force
    Copy-Item -Path "scene_tile_test_imgui_browser.wasm" -Destination "../public/build/" -Force
    Copy-Item -Path "scene_tile_test_imgui_browser.data" -Destination "../public/build/" -Force
    Copy-Item -Path "scene_tile_test_imgui_browser.html" -Destination "../public/build/" -Force

    Write-Host ""
    Write-Host "Build completed successfully!" -ForegroundColor Green
    Write-Host "Files copied to public/build/ directory" -ForegroundColor Green
    Write-Host ""
    Write-Host "Available applications:" -ForegroundColor Yellow
    Write-Host "  - Simple Browser Version: scene_tile_test_browser.html" -ForegroundColor White
    Write-Host "  - ImGui Browser Version: scene_tile_test_imgui_browser.html" -ForegroundColor White
    Write-Host ""
    Write-Host "To test locally, run:" -ForegroundColor Yellow
    Write-Host "  python -m http.server -d public 8080" -ForegroundColor White
    Write-Host "  Then open http://localhost:8080/build/scene_tile_test_imgui_browser.html" -ForegroundColor White

} catch {
    Write-Host "Build failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    Pop-Location
}
