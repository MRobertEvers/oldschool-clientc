#!/usr/bin/env bash
# Profile the torirs client with DTrace and render a flamegraph.
# Usage: sudo scripts/flamegraph_torirs.sh   (DTrace requires root)
set -euo pipefail

REPO="/Users/matthewevers/Documents/git_repos/3draster"
FG="/Users/matthewevers/Documents/git_repos/FlameGraph"
OUT="$REPO/src/out.stacks"
FOLDED="$REPO/src/out.folded"
SVG="$REPO/src/flamegraph.svg"

cd "$REPO"

echo "[*] Sampling ./src/torirs at 4000Hz for 30s (close the window early to stop)..."
"$REPO/profile.d" -c "./src/torirs /Users/matthewevers/Documents/git_repos/3draster/cache 12" > "$OUT"

echo "[*] Collapsing stacks..."
"$FG/stackcollapse.pl" "$OUT" > "$FOLDED"

echo "[*] Rendering flamegraph..."
"$FG/flamegraph.pl" "$FOLDED" > "$SVG"

# Make outputs user-owned if run under sudo
if [ -n "${SUDO_USER:-}" ]; then
  chown "$SUDO_USER" "$OUT" "$FOLDED" "$SVG" 2>/dev/null || true
fi

echo "[*] Done: $SVG"
