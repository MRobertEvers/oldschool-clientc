@echo off
REM Batch script to dump assembly of specific functions using Windows tools
REM This script tries multiple methods to extract assembly code

setlocal enabledelayedexpansion

REM Default parameters
set BUILD_DIR=build/Debug
set EXECUTABLE=model_viewer.exe
set OUTPUT_DIR=assembly_dumps

REM Parse command line arguments
if "%1" neq "" set BUILD_DIR=%1
if "%2" neq "" set EXECUTABLE=%2
if "%3" neq "" set OUTPUT_DIR=%3

echo === Assembly Dumper Script ===
echo Build Directory: %BUILD_DIR%
echo Executable: %EXECUTABLE%
echo Output Directory: %OUTPUT_DIR%
echo.

REM Check if executable exists
set EXE_PATH=%BUILD_DIR%\%EXECUTABLE%
if not exist "%EXE_PATH%" (
    echo ERROR: Executable not found: %EXE_PATH%
    echo Available executables in %BUILD_DIR%:
    dir /b "%BUILD_DIR%\*.exe" 2>nul
    exit /b 1
)

REM Create output directory
if not exist "%OUTPUT_DIR%" (
    mkdir "%OUTPUT_DIR%"
    echo Created output directory: %OUTPUT_DIR%
)

echo Looking for functions: raster_gouraud_s4, gouraud_deob_draw_triangle
echo.

REM Method 1: Try dumpbin (Visual Studio tool)
where dumpbin >nul 2>&1
if %errorlevel% equ 0 (
    echo dumpbin is available
    
    REM Dump symbols first
    echo Extracting symbol information...
    dumpbin /symbols "%EXE_PATH%" > "%OUTPUT_DIR%\symbols.txt"
    
    REM Search for our functions in symbols
    findstr /i "raster_gouraud_s4" "%OUTPUT_DIR%\symbols.txt" > "%OUTPUT_DIR%\raster_gouraud_s4.symbols.txt"
    findstr /i "gouraud_deob_draw_triangle" "%OUTPUT_DIR%\symbols.txt" > "%OUTPUT_DIR%\gouraud_deob_draw_triangle.symbols.txt"
    
    REM Dump disassembly
    echo Extracting disassembly...
    dumpbin /disasm "%EXE_PATH%" > "%OUTPUT_DIR%\full_disassembly.txt"
    
    echo Assembly information extracted using dumpbin
) else (
    echo dumpbin not available
)

REM Method 2: Try objdump (if available through MinGW/GCC)
where objdump >nul 2>&1
if %errorlevel% equ 0 (
    echo objdump is available
    
    REM Extract assembly for specific functions
    echo Extracting assembly for raster_gouraud_s4...
    objdump -d -M intel "%EXE_PATH%" | findstr /i "raster_gouraud_s4" /C:50 > "%OUTPUT_DIR%\raster_gouraud_s4.objdump.txt"
    
    echo Extracting assembly for gouraud_deob_draw_triangle...
    objdump -d -M intel "%EXE_PATH%" | findstr /i "gouraud_deob_draw_triangle" /C:50 > "%OUTPUT_DIR%\gouraud_deob_draw_triangle.objdump.txt"
    
    echo Assembly extracted using objdump
) else (
    echo objdump not available
)

REM Method 3: Try Visual Studio Developer Command Prompt tools
where cl >nul 2>&1
if %errorlevel% equ 0 (
    echo Visual Studio tools are available
    
    where link >nul 2>&1
    if %errorlevel% equ 0 (
        echo Using link.exe for additional symbol information...
        link /dump /symbols "%EXE_PATH%" > "%OUTPUT_DIR%\link_symbols.txt"
    )
)

REM Create executable information file
echo Creating executable information...
echo Executable: %EXE_PATH% > "%OUTPUT_DIR%\executable_info.txt"
for %%F in ("%EXE_PATH%") do (
    echo Size: %%~zF bytes >> "%OUTPUT_DIR%\executable_info.txt"
    echo Created: %%~tF >> "%OUTPUT_DIR%\executable_info.txt"
)
echo Build Type: %BUILD_DIR% >> "%OUTPUT_DIR%\executable_info.txt"

echo.
echo === Summary ===
echo Check the '%OUTPUT_DIR%' directory for output files.
echo.
echo Available tools:
where dumpbin >nul 2>&1 && echo   dumpbin: Available || echo   dumpbin: Not available
where objdump >nul 2>&1 && echo   objdump: Available || echo   objdump: Not available
where cl >nul 2>&1 && echo   cl (Visual Studio): Available || echo   cl (Visual Studio): Not available
where link >nul 2>&1 && echo   link: Available || echo   link: Not available

echo.
echo Script completed!
pause
