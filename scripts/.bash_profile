# MSYS2 MinGW64 Bash Profile
# This file is sourced for login shells

# Source the project's .bashrc if it exists
if [ -f "${BASH_SOURCE%/*}/.bashrc" ]; then
    source "${BASH_SOURCE%/*}/.bashrc"
fi

# Source system .bashrc if it exists
if [ -f ~/.bashrc ]; then
    source ~/.bashrc
fi

# Additional login-specific configurations can go here
