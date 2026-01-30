@echo off
REM Inspect debug paths in a MinGW-built executable
REM Usage: inspect_debug_paths.bat [path_to_exe]

setlocal

if "%1"=="" (
    set "EXE=build-mingw\osx.exe"
) else (
    set "EXE=%~1"
)

if not exist "%EXE%" (
    echo Error: Executable not found: %EXE%
    exit /b 1
)

echo ========================================
echo Inspecting debug paths in: %EXE%
echo ========================================
echo.

REM Check if objdump is available
where objdump >nul 2>&1
if errorlevel 1 (
    echo objdump not found in PATH. Trying common locations...
    set "OBJDUMP="
    if exist "C:\msys64\mingw64\bin\objdump.exe" set "OBJDUMP=C:\msys64\mingw64\bin\objdump.exe"
    if exist "C:\msys64\usr\bin\objdump.exe" set "OBJDUMP=C:\msys64\usr\bin\objdump.exe"
    if "%OBJDUMP%"=="" (
        echo Error: objdump not found. Please add MinGW bin directory to PATH.
        echo Example: set PATH=C:\msys64\mingw64\bin;%%PATH%%
        exit /b 1
    )
) else (
    set "OBJDUMP=objdump"
)

echo Method 1: Using objdump to extract .debug_line section (DWARF debug info)
echo ----------------------------------------------------------------------------
%OBJDUMP% -W "%EXE%" 2>nul | findstr /i "directory\|file\|path" | head -n 50
echo.

echo Method 2: Using objdump to show all debug sections
echo ----------------------------------------------------------------------------
%OBJDUMP% -h "%EXE%" | findstr /i "debug"
echo.

echo Method 3: Using strings to find path-like strings
echo ----------------------------------------------------------------------------
where strings >nul 2>&1
if not errorlevel 1 (
    strings "%EXE%" | findstr /i "\.c\|\.cpp\|\.h\|Users\|Documents\|git_repos\|3d-raster" | head -n 30
) else (
    echo strings command not found. Skipping...
)
echo.

echo Method 4: Using objdump to dump .debug_info section (detailed)
echo ----------------------------------------------------------------------------
%OBJDUMP% -g "%EXE%" 2>nul | findstr /i "DW_AT_name\|DW_AT_comp_dir" | head -n 30
echo.

echo ========================================
echo Done! Look for paths in the output above.
echo ========================================
echo.
echo Tip: To see ALL paths, redirect to a file:
echo   %OBJDUMP% -W "%EXE%" > debug_info.txt
echo   %OBJDUMP% -g "%EXE%" >> debug_info.txt

pause
