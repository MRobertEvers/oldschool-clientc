#!/bin/sh
#
# The fidelity bars, run against a real cache.
#
# The content build rests on a rule nothing was checking: re-encode only what the
# tree states, and let everything else fall through from the base cache. This is
# the check on that rule's two halves —
#
#   configs   `cachepack verify` round-trips every record through the text and
#             fails on a non-zero `lost-here`, i.e. a record the library's own
#             codec reproduced byte-exactly and this tool's text layer did not.
#             The other columns are the library's documented losses
#             (EXCEPTIONS.md B2/B3) and are reported, not judged.
#
#   assets    `--assets` round-trips each table through the friendly form it
#             would be edited in and fails on a *length* change. A same-length
#             mismatch is byte ordering and loses nothing — sprites measure 100%
#             same-length and 24-29% exact for exactly that reason.
#
# **Whole-cache byte-identity is deliberately not the bar.** Jagex's packer does
# not write config opcodes in ascending order and the encoders do, so records come
# back at identical length with different bytes. Matching that ordering would
# raise one column and change nothing observable.
#
# No cache is committed (`.gitignore` excludes `cache.*/`), so this skips when it
# cannot find one — loudly, because a fidelity check that silently passes on an
# absent corpus is worse than no check.
#
# Usage: sh test/test_cachepack_fidelity.sh [CACHE_ROOT]

set -u

ROOT=${1:-../..}
CACHE="$ROOT/cache.osrs239"
SRC="$ROOT/OSRS-Content/osrs239-content"
REV=osrs239
TMP=build/cachepack_fidelity

if [ ! -f "$CACHE/main_file_cache.dat2" ]; then
    echo "cachepack-fidelity: SKIPPED — no cache at $CACHE"
    echo "   (caches are not committed; point CACHE_ROOT at a tree that has one)"
    exit 0
fi
if [ ! -f "$SRC/meta.ini" ]; then
    echo "cachepack-fidelity: SKIPPED — no content tree at $SRC"
    exit 0
fi

if ! make -s -C tools cachepack; then
    echo "cachepack-fidelity: FAILED — cachepack did not build"
    exit 1
fi

# Every config type, and the asset tables whose codec is held to the byte bar.
#
# Maps and scripts are left out of the default run and that is stated rather than
# hidden: `maps` explodes 2,934 squares into five files each and `scripts`
# decompiles 9,725 clientscripts, which together make the scratch tree larger and
# slower than a routine `make test` should be.
#
# Everything with a *codec* is covered, because a codec is where a round trip can
# silently stop being exact. `fonts` costs 21 files and `worldmap/geography` 2,101,
# and both were unguarded until a font codec shipped whose text form did not parse
# back — caught by running the bar by hand, which is not a system. Scripts are also on the *semantic*
# bar rather than the byte one — compiling decompiled source back gives this
# compiler's bytes, not Jagex's — and that bar is measured by test_cs2.
ASSETS=interfaces,sprites,textures,dbindex,worldmap/areas,worldmap/geography,fonts,defaults

rm -rf "$TMP"
echo "cachepack-fidelity: configs + $ASSETS against $CACHE"
echo "   not covered here: maps (scratch size), scripts (semantic bar — see test_cs2)"

# `--digests` rides along on the verify that was going to run anyway — measured at
# 20.42s with and without, so it is free.
mkdir -p "$TMP.digests"
if tools/cachepack/cachepack verify \
        --cache "$CACHE" --rev "$REV" --src "$SRC" \
        --assets="$ASSETS" --tmp "$TMP" --digests "$TMP.digests" >"$TMP.log" 2>&1; then
    status=0
else
    status=1
fi

# The tables are the point of the run, so they are shown either way. The world-map
# decoder's per-section chatter is filtered out of the *summary* only — it is still
# in the log, and it is why 103 of that table's 104 members decline the codec.
sed -n '/^type /,/^$/p;/^Asset fidelity/,/^$/p' "$TMP.log" |
    grep -v 'unknown section type'

if [ "$status" -ne 0 ]; then
    echo "cachepack-fidelity: FAILED — a bar was missed; full log at $TMP.log"
    grep -E "lost-here|length changed|content.ini" "$TMP.log" | head -20
    exit 1
fi

# ---------------------------------------------------------------------------
# The import bar: every archive the index lists has a file behind it.
#
# `verify` never calls `cp_assets_import`, so until this ran, the whole import
# path — the candidate-extension list and its fallthrough — was exercised by no
# test at all. That fallthrough is silent by construction: `pack --base` copies
# the base cache first, so an archive whose file was not found keeps the *base*
# bytes and looks identical to one that was found and unchanged. A renamed
# extension would stop shipping the tree's copy with nothing said.
#
# Only `missing` fails. Two other causes are counted separately because neither
# is a defect:
#   codec-declined  the friendly form is on disk and the codec would not turn it
#                   back into bytes. 219 clientscripts decompile and do not
#                   recompile — that is the documented semantic bar, and keeping
#                   the cache's own bytes is the right answer.
#   not in cache    the index names an archive the cache never had.
#                   `3_interfaces.pack` has 969 names for 968 archives, because
#                   gameval archive 14 names one this revision does not ship.
echo "cachepack-fidelity: import — every indexed archive has a file"
if tools/cachepack/cachepack pack \
        --src "$SRC" --base "$CACHE" --rev "$REV" \
        --assets --check-only >"$TMP.import.log" 2>&1; then
    grep -E "missing, .* codec-declined" "$TMP.import.log" | sed 's/^/   /'
    grep -E "^Imported" "$TMP.import.log" | sed 's/^/   /'
else
    echo "cachepack-fidelity: FAILED — an indexed archive has no file on disk"
    grep -E "no file is on disk|missing," "$TMP.import.log" | head -20
    exit 1
fi
rm -f "$TMP.import.log"

# ---------------------------------------------------------------------------
# The grow bar: pack with no --base, so every reference table is created from
# nothing and every archive written takes cp_reference_sync's grow path.
#
# --check-only can never reach that path (it writes nothing), and pack --base
# reaches it only for archives past the base table's end — which is why the
# 2026-08-24 present-bitmap rework could abort every from-scratch pack on its
# first asset archive and no bar noticed. Scoped to one config type and the
# three smallest codec tables (24 archives all told) so the whole bar costs
# seconds; growth is growth regardless of how many archives follow.
echo "cachepack-fidelity: grow — a tree-only pack creates its reference tables"
rm -rf "$TMP.grow"
if tools/cachepack/cachepack pack \
        --src "$SRC" --rev "$REV" --out "$TMP.grow" \
        --types hitsplat --assets=defaults,textures,fonts >"$TMP.grow.log" 2>&1; then
    if [ -f "$TMP.grow/main_file_cache.idx17" ]; then
        echo "   idx17 present; $(grep -c 'reference table updated' "$TMP.grow.log") table(s) grown"
    else
        echo "cachepack-fidelity: FAILED — the tree-only pack wrote no idx17"
        exit 1
    fi
else
    echo "cachepack-fidelity: FAILED — the tree-only pack did not complete"
    tail -5 "$TMP.grow.log"
    exit 1
fi
rm -rf "$TMP.grow" "$TMP.grow.log"

# ---------------------------------------------------------------------------
# Per-record digests against the committed golden.
#
# `test/digests/<type>.digests` is `<id>=<crc32>` over the bytes `pack` would
# write, snapshotted while the packer's output is still a pure function of
# `configs/all.<type>`. Once a server overlay is merged in before encoding, the
# only way to show an *unoverlaid* record did not move is a snapshot taken before
# the merge existed — one captured afterwards proves nothing.
#
# Per record rather than a whole-cache hash, because the assertion this feeds is
# set containment over record ids: a whole-cache hash says a byte moved and not
# which record moved it.
GOLDEN="test/digests"
if [ -d "$GOLDEN" ]; then
    if diff -rq "$GOLDEN" "$TMP.digests" >"$TMP.digests.diff" 2>&1; then
        echo "cachepack-fidelity: digests — $(cat "$GOLDEN"/*.digests | wc -l | tr -d ' ') records unchanged"
    else
        echo "cachepack-fidelity: FAILED — packed records changed against the golden"
        echo "   (if the change is intended, regenerate with"
        echo "    cachepack verify ... --digests $GOLDEN)"
        head -20 "$TMP.digests.diff"
        exit 1
    fi
else
    echo "cachepack-fidelity: no digest golden at $GOLDEN — skipping"
fi
rm -rf "$TMP.digests" "$TMP.digests.diff"

rm -rf "$TMP"
echo "cachepack-fidelity: all bars met"
exit 0
