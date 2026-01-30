# PowerShell script to download and install SDL2 for MinGW i686
# This script downloads SDL2 development files and installs them to the MinGW toolchain

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "SDL2 Installation for MinGW i686" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Configuration
$MINGW_ROOT = "C:\Users\mrobe\Downloads\winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2"
$MINGW_PREFIX = "$MINGW_ROOT\mingw32"
$SDL2_VERSION = "2.30.9"
$SDL2_URL = "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-devel-$SDL2_VERSION-mingw.tar.gz"
$TEMP_DIR = "$env:TEMP\sdl2_install"
$DOWNLOAD_FILE = "$TEMP_DIR\SDL2-devel-$SDL2_VERSION-mingw.tar.gz"

Write-Host "MinGW Installation: $MINGW_ROOT" -ForegroundColor Yellow
Write-Host "SDL2 Version: $SDL2_VERSION" -ForegroundColor Yellow
Write-Host ""

# Check if MinGW exists
if (-not (Test-Path $MINGW_PREFIX)) {
    Write-Host "ERROR: MinGW installation not found at: $MINGW_PREFIX" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please update MINGW_ROOT in this script to match your installation." -ForegroundColor Red
    exit 1
}

# Check if SDL2 is already installed
$sdl2HeaderExists = Test-Path "$MINGW_PREFIX\include\SDL2\SDL.h"
$sdl2LibExists = Test-Path "$MINGW_PREFIX\lib\libSDL2.a"
$sdl2DllExists = Test-Path "$MINGW_PREFIX\bin\SDL2.dll"

if ($sdl2HeaderExists -and $sdl2LibExists -and $sdl2DllExists) {
    Write-Host "[OK] SDL2 is already installed!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Headers: $MINGW_PREFIX\include\SDL2\" -ForegroundColor Green
    Write-Host "Library: $MINGW_PREFIX\lib\libSDL2.a" -ForegroundColor Green
    Write-Host "DLL: $MINGW_PREFIX\bin\SDL2.dll" -ForegroundColor Green
    Write-Host ""
    Write-Host "You can now run build_winxp.bat" -ForegroundColor Green
    exit 0
}

Write-Host "SDL2 installation required. Starting download..." -ForegroundColor Yellow
Write-Host ""

# Create temp directory
if (Test-Path $TEMP_DIR) {
    Remove-Item -Recurse -Force $TEMP_DIR
}
New-Item -ItemType Directory -Path $TEMP_DIR | Out-Null

# Download SDL2
Write-Host "Downloading SDL2 from: $SDL2_URL" -ForegroundColor Cyan
try {
    Invoke-WebRequest -Uri $SDL2_URL -OutFile $DOWNLOAD_FILE -UseBasicParsing
    Write-Host "[OK] Download complete: $DOWNLOAD_FILE" -ForegroundColor Green
} catch {
    Write-Host "ERROR: Failed to download SDL2" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}

# Extract tar.gz (requires tar on Windows 10+)
Write-Host ""
Write-Host "Extracting SDL2 archive..." -ForegroundColor Cyan

# First extract .gz to get .tar
$TAR_FILE = "$TEMP_DIR\SDL2-devel-$SDL2_VERSION-mingw.tar"
try {
    # Use PowerShell's built-in extraction for .gz
    & tar -xzf $DOWNLOAD_FILE -C $TEMP_DIR
    Write-Host "[OK] Extraction complete" -ForegroundColor Green
} catch {
    Write-Host "ERROR: Failed to extract SDL2 archive" -ForegroundColor Red
    Write-Host "Make sure you have tar available (Windows 10 1803+)" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}

# Find the SDL2 directory
$SDL2_DIR = Get-ChildItem -Path $TEMP_DIR -Filter "SDL2-*" -Directory | Select-Object -First 1
if (-not $SDL2_DIR) {
    Write-Host "ERROR: Could not find SDL2 directory after extraction" -ForegroundColor Red
    exit 1
}

$SDL2_MINGW_DIR = "$($SDL2_DIR.FullName)\i686-w64-mingw32"

if (-not (Test-Path $SDL2_MINGW_DIR)) {
    Write-Host "ERROR: i686-w64-mingw32 directory not found in SDL2 package" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Installing SDL2 to MinGW..." -ForegroundColor Cyan

# Copy include files
Write-Host "Copying headers..." -ForegroundColor Yellow
$SDL2_SRC_INCLUDE = "$SDL2_MINGW_DIR\include\SDL2"
$SDL2_DST_INCLUDE = "$MINGW_PREFIX\include\SDL2"
if (Test-Path $SDL2_SRC_INCLUDE) {
    if (-not (Test-Path "$MINGW_PREFIX\include")) {
        New-Item -ItemType Directory -Path "$MINGW_PREFIX\include" | Out-Null
    }
    Copy-Item -Path $SDL2_SRC_INCLUDE -Destination "$MINGW_PREFIX\include\" -Recurse -Force
    Write-Host "[OK] Headers installed to: $SDL2_DST_INCLUDE" -ForegroundColor Green
} else {
    Write-Host "WARNING: SDL2 headers not found at: $SDL2_SRC_INCLUDE" -ForegroundColor Yellow
}

# Copy library files
Write-Host "Copying libraries..." -ForegroundColor Yellow
$SDL2_SRC_LIB = "$SDL2_MINGW_DIR\lib"
$SDL2_DST_LIB = "$MINGW_PREFIX\lib"
if (Test-Path $SDL2_SRC_LIB) {
    if (-not (Test-Path $SDL2_DST_LIB)) {
        New-Item -ItemType Directory -Path $SDL2_DST_LIB | Out-Null
    }
    Copy-Item -Path "$SDL2_SRC_LIB\*" -Destination $SDL2_DST_LIB -Force
    Write-Host "[OK] Libraries installed to: $SDL2_DST_LIB" -ForegroundColor Green
} else {
    Write-Host "WARNING: SDL2 libraries not found at: $SDL2_SRC_LIB" -ForegroundColor Yellow
}

# Copy DLL files
Write-Host "Copying DLLs..." -ForegroundColor Yellow
$SDL2_SRC_BIN = "$SDL2_MINGW_DIR\bin"
$SDL2_DST_BIN = "$MINGW_PREFIX\bin"
if (Test-Path $SDL2_SRC_BIN) {
    if (-not (Test-Path $SDL2_DST_BIN)) {
        New-Item -ItemType Directory -Path $SDL2_DST_BIN | Out-Null
    }
    Copy-Item -Path "$SDL2_SRC_BIN\*.dll" -Destination $SDL2_DST_BIN -Force
    Write-Host "[OK] DLLs installed to: $SDL2_DST_BIN" -ForegroundColor Green
} else {
    Write-Host "WARNING: SDL2 DLLs not found at: $SDL2_SRC_BIN" -ForegroundColor Yellow
}

# Cleanup
Write-Host ""
Write-Host "Cleaning up temporary files..." -ForegroundColor Cyan
Remove-Item -Recurse -Force $TEMP_DIR
Write-Host "[OK] Cleanup complete" -ForegroundColor Green

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "SDL2 Installation Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Installed files:" -ForegroundColor Cyan
Write-Host "  Headers: $MINGW_PREFIX\include\SDL2\" -ForegroundColor White
Write-Host "  Library: $MINGW_PREFIX\lib\libSDL2.a" -ForegroundColor White
Write-Host "  DLL: $MINGW_PREFIX\bin\SDL2.dll" -ForegroundColor White
Write-Host ""
Write-Host "You can now run build_winxp.bat to build for Windows XP!" -ForegroundColor Green
Write-Host ""





