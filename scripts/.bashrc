# MSYS2 MinGW64 Bash Configuration
# This file is sourced when starting a bash session in MSYS2 MinGW64 environment

# Add alias for mingw32-make to make
alias make='mingw32-make'

# Optional: Add other useful aliases for MinGW development
alias gcc='gcc.exe'
alias g++='g++.exe'
alias cmake='cmake.exe'

# Set default make to use mingw32-make
export MAKE=mingw32-make

# Add current directory to PATH if not already present
if [[ ":$PATH:" != *":$(pwd):"* ]]; then
    export PATH="$(pwd):$PATH"
fi

echo "MSYS2 MinGW64 environment loaded with mingw32-make alias"
