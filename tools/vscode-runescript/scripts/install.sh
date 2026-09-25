#!/usr/bin/env bash
#
# Install the extension into VS Code.
#
#   tools/vscode-runescript/scripts/install.sh              build, then install
#   tools/vscode-runescript/scripts/install.sh --link       install a symlink
#   tools/vscode-runescript/scripts/install.sh --uninstall
#
# --link puts a symlink into ~/.vscode/extensions instead of a packaged copy,
# so an edit to extension.js is live after one window reload. That is the mode
# to develop in; the packaged one is what to hand to someone else.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXT_DIR="$(dirname "$HERE")"
EXT_ID="toridraw.runescript"

MODE=package
for arg in "$@"; do
    case "$arg" in
        --link) MODE=link ;;
        --uninstall) MODE=uninstall ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

# VS Code's extension directory, per platform. The CLI is preferred when it is
# on PATH; the directory is the fallback for a machine where `code` is not
# installed as a command.
case "$(uname -s)" in
    Darwin|Linux) EXTENSIONS_DIR="$HOME/.vscode/extensions" ;;
    MINGW*|MSYS*|CYGWIN*) EXTENSIONS_DIR="$USERPROFILE/.vscode/extensions" ;;
    *) EXTENSIONS_DIR="$HOME/.vscode/extensions" ;;
esac
LINK_TARGET="$EXTENSIONS_DIR/$EXT_ID-dev"

if [ "$MODE" = uninstall ]; then
    if command -v code >/dev/null 2>&1; then
        code --uninstall-extension "$EXT_ID" || true
    fi
    rm -rf "$LINK_TARGET"
    echo "uninstalled $EXT_ID"
    exit 0
fi

if [ "$MODE" = link ]; then
    "$HERE/build.sh"
    mkdir -p "$EXTENSIONS_DIR"
    rm -rf "$LINK_TARGET"
    ln -s "$EXT_DIR" "$LINK_TARGET"
    echo "linked $LINK_TARGET -> $EXT_DIR"
    echo "reload the VS Code window to pick it up (Developer: Reload Window)"
    exit 0
fi

"$HERE/build.sh"

VSIX="$(ls -t "$EXT_DIR"/dist/*.vsix 2>/dev/null | head -1 || true)"
if [ -z "$VSIX" ]; then
    echo "no .vsix in $EXT_DIR/dist — did build.sh fail?" >&2
    exit 1
fi

if command -v code >/dev/null 2>&1; then
    code --install-extension "$VSIX" --force
    echo "installed $VSIX"
    echo "reload the VS Code window to pick it up (Developer: Reload Window)"
else
    echo "the 'code' command is not on PATH." >&2
    echo "In VS Code: Extensions -> ... -> Install from VSIX..., and choose" >&2
    echo "  $VSIX" >&2
    echo "(the command can be added from the palette: Shell Command: Install 'code' command in PATH)" >&2
    exit 1
fi
