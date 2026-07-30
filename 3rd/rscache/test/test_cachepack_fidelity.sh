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
ASSETS=interfaces,sprites,textures,dbindex,worldmap/areas,worldmap/geography,fonts

rm -rf "$TMP"
echo "cachepack-fidelity: configs + $ASSETS against $CACHE"
echo "   not covered here: maps (scratch size), scripts (semantic bar — see test_cs2)"

if tools/cachepack/cachepack verify \
        --cache "$CACHE" --rev "$REV" --src "$SRC" \
        --assets="$ASSETS" --tmp "$TMP" >"$TMP.log" 2>&1; then
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

rm -rf "$TMP"
echo "cachepack-fidelity: all bars met"
exit 0
