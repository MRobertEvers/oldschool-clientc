# PowerShell script to download and install Freetype for MinGW i686
# This script downloads Freetype pre-built binaries from MSYS2 and installs them to the MinGW toolchain

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Freetype Installation for MinGW i686" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Configuration
$MINGW_ROOT = "C:\Users\mrobe\Downloads\winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2"
$MINGW_PREFIX = "$MINGW_ROOT\mingw32"
$TEMP_DIR = "$env:TEMP\freetype_install"

Write-Host "MinGW Installation: $MINGW_ROOT" -ForegroundColor Yellow
Write-Host ""

# Check if MinGW exists
if (-not (Test-Path "$MINGW_PREFIX\bin")) {
    Write-Host "ERROR: MinGW installation not found at: $MINGW_PREFIX" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please update MINGW_ROOT in this script to match your installation." -ForegroundColor Red
    exit 1
}

# Check if Freetype is already installed
$freetypeHeaderExists = Test-Path "$MINGW_PREFIX\include\freetype2\freetype\freetype.h"
$freetypeLibExists = Test-Path "$MINGW_PREFIX\lib\libfreetype.a"

if ($freetypeHeaderExists -and $freetypeLibExists) {
    Write-Host "[OK] Freetype is already installed!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Headers: $MINGW_PREFIX\include\freetype2\" -ForegroundColor Green
    Write-Host "Library: $MINGW_PREFIX\lib\libfreetype.a" -ForegroundColor Green
    Write-Host ""
    Write-Host "You can now run build_winxp.bat" -ForegroundColor Green
    exit 0
}

Write-Host "Freetype installation required. Downloading from MSYS2 repository..." -ForegroundColor Yellow
Write-Host ""

# Create temp directory
if (Test-Path $TEMP_DIR) {
    Remove-Item -Recurse -Force $TEMP_DIR
}
New-Item -ItemType Directory -Path $TEMP_DIR | Out-Null

# Download and install required packages
$packages = @(
    @{
        Name = "Freetype"
        URL = "https://repo.msys2.org/mingw/mingw32/mingw-w64-i686-freetype-2.13.2-1-any.pkg.tar.zst"
        File = "freetype.pkg.tar.zst"
    },
    @{
        Name = "Bzip2"
        URL = "https://repo.msys2.org/mingw/mingw32/mingw-w64-i686-bzip2-1.0.8-4-any.pkg.tar.zst"
        File = "bzip2.pkg.tar.zst"
    },
    @{
        Name = "Zlib"
        URL = "https://repo.msys2.org/mingw/mingw32/mingw-w64-i686-zlib-1.3.1-1-any.pkg.tar.zst"
        File = "zlib.pkg.tar.zst"
    },
    @{
        Name = "Libpng"
        URL = "https://repo.msys2.org/mingw/mingw32/mingw-w64-i686-libpng-1.6.43-1-any.pkg.tar.zst"
        File = "libpng.pkg.tar.zst"
    },
    @{
        Name = "Brotli"
        URL = "https://repo.msys2.org/mingw/mingw32/mingw-w64-i686-brotli-1.1.0-1-any.pkg.tar.zst"
        File = "brotli.pkg.tar.zst"
    }
)

# Check if zstd is available
$zstdAvailable = $null -ne (Get-Command zstd -ErrorAction SilentlyContinue)
if (-not $zstdAvailable) {
    Write-Host "ERROR: zstd is not available" -ForegroundColor Red
    Write-Host ""
    Write-Host "This script requires zstd to extract MSYS2 packages." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Please install zstd using one of these methods:" -ForegroundColor Yellow
    Write-Host "  1. winget install zstd" -ForegroundColor Cyan
    Write-Host "  2. choco install zstandard" -ForegroundColor Cyan
    Write-Host "  3. Download from: https://github.com/facebook/zstd/releases" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Or manually install Freetype development files." -ForegroundColor Yellow
    exit 1
}

foreach ($pkg in $packages) {
    Write-Host "Downloading $($pkg.Name)..." -ForegroundColor Cyan
    $downloadPath = Join-Path $TEMP_DIR $pkg.File
    
    try {
        Invoke-WebRequest -Uri $pkg.URL -OutFile $downloadPath -UseBasicParsing
        Write-Host "[OK] Downloaded $($pkg.Name)" -ForegroundColor Green
        
        # Extract with zstd and tar
        Write-Host "Extracting $($pkg.Name)..." -ForegroundColor Yellow
        $tarFile = $downloadPath -replace '\.zst$', ''
        
        # Decompress zst
        & zstd -d $downloadPath -o $tarFile --quiet
        
        # Extract tar to temporary extraction directory
        $extractDir = Join-Path $TEMP_DIR "extract_$($pkg.Name)"
        New-Item -ItemType Directory -Path $extractDir -Force | Out-Null
        & tar -xf $tarFile -C $extractDir
        
        # Copy files from mingw32 subdirectory to MinGW installation
        $mingw32Dir = Join-Path $extractDir "mingw32"
        if (Test-Path $mingw32Dir) {
            # Copy include files
            if (Test-Path "$mingw32Dir\include") {
                Copy-Item -Path "$mingw32Dir\include\*" -Destination "$MINGW_PREFIX\include\" -Recurse -Force
            }
            # Copy lib files
            if (Test-Path "$mingw32Dir\lib") {
                Copy-Item -Path "$mingw32Dir\lib\*" -Destination "$MINGW_PREFIX\lib\" -Recurse -Force
            }
            # Copy bin files (DLLs)
            if (Test-Path "$mingw32Dir\bin") {
                Copy-Item -Path "$mingw32Dir\bin\*" -Destination "$MINGW_PREFIX\bin\" -Recurse -Force
            }
            Write-Host "[OK] Installed $($pkg.Name)" -ForegroundColor Green
        } else {
            Write-Host "WARNING: No mingw32 directory found in $($pkg.Name) package" -ForegroundColor Yellow
        }
        
    } catch {
        Write-Host "WARNING: Failed to download/install $($pkg.Name)" -ForegroundColor Yellow
        Write-Host $_.Exception.Message -ForegroundColor Yellow
    }
    
    Write-Host ""
}

# Cleanup
Write-Host "Cleaning up temporary files..." -ForegroundColor Cyan
Remove-Item -Recurse -Force $TEMP_DIR -ErrorAction SilentlyContinue
Write-Host "[OK] Cleanup complete" -ForegroundColor Green

# Verify installation
$freetypeHeaderExists = Test-Path "$MINGW_PREFIX\include\freetype2\freetype\freetype.h"
$freetypeLibExists = Test-Path "$MINGW_PREFIX\lib\libfreetype.a"

if ($freetypeHeaderExists -and $freetypeLibExists) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "Freetype Installation Complete!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Installed files:" -ForegroundColor Cyan
    Write-Host "  Headers: $MINGW_PREFIX\include\freetype2\" -ForegroundColor White
    Write-Host "  Library: $MINGW_PREFIX\lib\libfreetype.a" -ForegroundColor White
    Write-Host ""
    Write-Host "You can now run build_winxp.bat to build for Windows XP!" -ForegroundColor Green
    Write-Host ""
} else {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Yellow
    Write-Host "Installation may be incomplete" -ForegroundColor Yellow
    Write-Host "========================================" -ForegroundColor Yellow
    Write-Host ""
    if (-not $freetypeHeaderExists) {
        Write-Host "WARNING: Freetype headers not found" -ForegroundColor Yellow
    }
    if (-not $freetypeLibExists) {
        Write-Host "WARNING: Freetype library not found" -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host "You may need to manually install Freetype development files." -ForegroundColor Yellow
    Write-Host ""
}
