#!/bin/bash
# Inspect debug paths in a MinGW-built executable
# Usage: ./inspect_debug_paths.sh [path_to_exe]

EXE="${1:-build-mingw/osx.exe}"

if [ ! -f "$EXE" ]; then
    echo "Error: Executable not found: $EXE"
    exit 1
fi

echo "========================================"
echo "Inspecting debug paths in: $EXE"
echo "========================================"
echo

# Check for objdump
if ! command -v objdump &> /dev/null; then
    echo "Error: objdump not found. Please install binutils or add to PATH."
    exit 1
fi

echo "Method 1: Using objdump to extract .debug_line section (DWARF debug info)"
echo "------------------------------------------------------------------------"
objdump -W "$EXE" 2>/dev/null | grep -iE "directory|file|path" | head -n 50
echo

echo "Method 2: Using objdump to show all debug sections"
echo "------------------------------------------------------------------------"
objdump -h "$EXE" | grep -i debug
echo

echo "Method 3: Using strings to find path-like strings"
echo "------------------------------------------------------------------------"
if command -v strings &> /dev/null; then
    strings "$EXE" | grep -iE "\.(c|cpp|h)$|Users|Documents|git_repos|3d-raster" | head -n 30
else
    echo "strings command not found. Skipping..."
fi
echo

echo "Method 4: Using objdump to dump .debug_info section (detailed)"
echo "------------------------------------------------------------------------"
objdump -g "$EXE" 2>/dev/null | grep -iE "DW_AT_name|DW_AT_comp_dir" | head -n 30
echo

echo "Method 5: Using readelf (if available) to show debug sections"
echo "------------------------------------------------------------------------"
if command -v readelf &> /dev/null; then
    readelf -w "$EXE" 2>/dev/null | grep -iE "comp_dir|name" | head -n 30
else
    echo "readelf not found. Skipping..."
fi
echo

echo "========================================"
echo "Done! Look for paths in the output above."
echo "========================================"
echo
echo "Tip: To see ALL paths, redirect to a file:"
echo "  objdump -W \"$EXE\" > debug_info.txt"
echo "  objdump -g \"$EXE\" >> debug_info.txt"
