# DEPRECATED — copied from build.em/ to public/build/, both of which belong to
# the retired v0 CMake web build. The live web build writes a complete servable
# directory itself: `make -C src web` links into build-web/ and copies the JS
# host harness in beside it. See docs/web_build.md.
Write-Error "scripts/copy_browser_files.ps1 is deprecated — 'make -C src web' already populates build-web/."
exit 1

# Simple script to copy browser files from build.em to public/build
# Run this after building with Emscripten

Write-Host "Copying browser files to public/build..." -ForegroundColor Green

# Check if build.em directory exists
if (-not (Test-Path "build.em")) {
    Write-Host "Error: build.em directory not found. Please build first." -ForegroundColor Red
    exit 1
}

# Create public/build directory if it doesn't exist
if (-not (Test-Path "public/build")) {
    New-Item -ItemType Directory -Path "public/build" | Out-Null
    Write-Host "Created public/build directory" -ForegroundColor Yellow
}

# Create public/cache directory if it doesn't exist
if (-not (Test-Path "public/cache")) {
    New-Item -ItemType Directory -Path "public/cache" | Out-Null
    Write-Host "Created public/cache directory" -ForegroundColor Yellow
}

# Copy cache files
Write-Host "Copying cache files..." -ForegroundColor Cyan
if (Test-Path "cache") {
    Copy-Item -Path "cache/*" -Destination "public/cache/" -Recurse -Force
    Write-Host "Cache files copied" -ForegroundColor Green
} else {
    Write-Host "Warning: cache directory not found" -ForegroundColor Yellow
}

# Copy simple browser version files
Write-Host "Copying simple browser version..." -ForegroundColor Cyan
$simpleFiles = @(
    "scene_tile_test_browser.js",
    "scene_tile_test_browser.wasm", 
    "scene_tile_test_browser.data",
    "scene_tile_test_browser.html"
)

foreach ($file in $simpleFiles) {
    $sourcePath = "build.em/$file"
    if (Test-Path $sourcePath) {
        Copy-Item -Path $sourcePath -Destination "public/build/" -Force
        Write-Host "  Copied $file" -ForegroundColor White
    } else {
        Write-Host "  Warning: $file not found" -ForegroundColor Yellow
    }
}

# Copy ImGui browser version files
Write-Host "Copying ImGui browser version..." -ForegroundColor Cyan
$imguiFiles = @(
    "scene_tile_test_imgui_browser.js",
    "scene_tile_test_imgui_browser.wasm",
    "scene_tile_test_imgui_browser.data", 
    "scene_tile_test_imgui_browser.html"
)

foreach ($file in $imguiFiles) {
    $sourcePath = "build.em/$file"
    if (Test-Path $sourcePath) {
        Copy-Item -Path $sourcePath -Destination "public/build/" -Force
        Write-Host "  Copied $file" -ForegroundColor White
    } else {
        Write-Host "  Warning: $file not found" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "Copy completed!" -ForegroundColor Green
Write-Host ""
Write-Host "Available applications:" -ForegroundColor Yellow
Write-Host "  - Simple Browser Version: public/build/scene_tile_test_browser.html" -ForegroundColor White
Write-Host "  - ImGui Browser Version: public/build/scene_tile_test_imgui_browser.html" -ForegroundColor White
Write-Host ""
Write-Host "To test locally, run:" -ForegroundColor Yellow
Write-Host "  python -m http.server -d public 8080" -ForegroundColor White
Write-Host "  Then open http://localhost:8080/build/scene_tile_test_imgui_browser.html" -ForegroundColor White
