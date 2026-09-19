#!/bin/sh
# Compile the server scripts with ONLY the Chambers of Xeric work applied, so a
# failure is attributable.
#
# Why this exists
# ---------------
# This content tree is edited by more than one session at a time. A plain
# `make -C src torirsserver-scripts` therefore fails for reasons that have nothing to
# do with your change: during one CoX session the first error moved between
# `quest_troll.rs2:177`, `quest_troll.rs2:268`, `combat.rs2:354` and
# `flamtaer_temple.rs2:2` on identical CoX input, every one of them in a file
# with a modification timestamp inside that session's own window.
#
# `sscompile` stops at the FIRST error, so somebody else's in-flight edit hides
# whether your files are clean. This script builds a throwaway copy of the tree
# with every other session's script edits reverted to HEAD, leaving the CoX
# package as the only working-tree change.
#
# What it does NOT do
# -------------------
# It does not revert `pack/*.alloc`. Those are shared id ledgers: reverting them
# to HEAD desynchronises them from scripts that were already committed against
# newer ids, which breaks unrelated content (agilityarena, tbwt). The copy keeps
# the working-tree allocs, so a residual error in someone else's *committed*
# script is expected and is not yours.
#
# How to read the result
# ----------------------
# Traversal is alphabetical. `minigames/minigame_cox` therefore compiles before
# `quests/`, and an error first reported outside minigame_cox means the CoX
# files passed. Do not take that on faith — run with `--selftest`, which appends
# a deliberately broken proc to a CoX file and checks the compiler reports it.
# A gate that cannot fail is not a gate.
#
# Usage:  tools/cox_compile_check.sh [--selftest]

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CONTENT="$ROOT/OSRS-Content/osrs239-content"
WORK="${TMPDIR:-/tmp}/cox-compile-check"
ISO="$WORK/isotree"
OUT="$WORK/out"
PKG="minigames/minigame_cox"

if [ ! -x "$ROOT/src/build_opt/sscompile" ] && [ ! -x "$ROOT/src/build/sscompile" ]; then
    make -C "$ROOT/src" sscompile >/dev/null
fi
SSC="$ROOT/src/build_opt/sscompile"
[ -x "$SSC" ] || SSC="$ROOT/src/build/sscompile"

echo "staging an isolated tree in $WORK"
rm -rf "$ISO"
mkdir -p "$ISO" "$OUT"
# Only `server` and `interfaces` are needed. Copying the whole content dir would
# be 1.9G of maps and models for no benefit; components come from `interfaces`,
# which is why pointing --src at a bare scripts copy drops 26,951 components to
# 351 and produces a cascade of bogus "not a symbol" errors.
cp -R "$CONTENT/server" "$ISO/"
cp -R "$CONTENT/interfaces" "$ISO/"
cp -R "$CONTENT/pack" "$ISO/"
# `ported` is a symlink, not a copy. It is where the lane descriptors live, and
# without it this tree declares no lanes at all -- which would leave every lane's
# server scripts (copied above, inside `server/`) in the --src walk with none of
# their symbols, and the run would fail in content the CoX work never touched.
ln -s "$CONTENT/ported" "$ISO/ported"

# Files the CoX work owns but which live OUTSIDE the package. Reverting these
# would silently drop the change under test: the points hook lives in the shared
# combat path, and an early version of this script reverted it and then reported
# "compiled clean" without ever having compiled the hook. Keep this list in sync
# with any CoX edit made outside minigame_cox/.
KEEP='minigame_cox|skill_combat/npc_combat\.rs2'

reverted=0
for f in $(git -C "$CONTENT" diff --name-only HEAD | grep 'server/scripts/' || true); do
    rel=${f#osrs239-content/}
    echo "$rel" | grep -Eq "$KEEP" && continue
    [ -e "$ISO/$rel" ] || continue
    git -C "$CONTENT" show "HEAD:$f" > "$ISO/$rel" 2>/dev/null && reverted=$((reverted + 1))
done
removed=0
for f in $(git -C "$CONTENT" ls-files --others --exclude-standard | grep 'server/scripts/' || true); do
    rel=${f#osrs239-content/}
    echo "$rel" | grep -Eq "$KEEP" && continue
    [ -e "$ISO/$rel" ] || continue
    rm -f "$ISO/$rel" && removed=$((removed + 1))
done
echo "reverted $reverted in-flight script(s), removed $removed untracked"

# --content-root is the isolated tree, which reaches every lane through the
# `ported` symlink staged above; --pack is stated because this tree borrows the
# shared `configs/` while keeping its own `pack/`, which is not the default
# pair. Nothing here names a lane: `ported/<lane>/lane.ini` does.
compile() {
    "$SSC" --src "$ISO/server/scripts" --out "$OUT" \
        --content-root "$ISO" \
        --pack "$ISO/pack" \
        --pack "$CONTENT/configs" 2>&1 | sed "s|$ISO/server/scripts/||"
}

if [ "$1" = "--selftest" ]; then
    target="$ISO/server/scripts/$PKG/scripts/cox_tekton.rs2"
    cp "$target" "$WORK/selftest.bak"
    printf '\n[proc,cox_selftest_deliberate_break]\n~this_proc_does_not_exist;\n' >> "$target"
    echo "--- selftest: with a deliberate error in $PKG ---"
    if compile | grep -q "$PKG"; then
        echo "OK: the compiler reports errors in $PKG, so a clean run means something"
    else
        echo "FAIL: a broken $PKG file produced no $PKG error - this gate proves nothing"
        cp "$WORK/selftest.bak" "$target"
        exit 1
    fi
    cp "$WORK/selftest.bak" "$target"
    echo
fi

echo "--- compile ---"
result=$(compile)
echo "$result" | grep -v '^symbols:' || true
echo "$result" | grep '^symbols:' || true
echo
if echo "$result" | grep -q "$PKG"; then
    echo "RESULT: FAILED in $PKG - this one is yours."
    exit 1
fi
echo "RESULT: $PKG compiled clean."
echo "(Any error above is in another session's committed content, not CoX.)"
