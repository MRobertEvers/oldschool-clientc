#!/bin/sh
#
# Npc opcode 42, the rev-241+ recolour-all, through cachepack's text: the key
# `recolall` (tools/cachepack/config/cp_npc.c, CP_KEY_OPTIONAL).
#
#   binary -> text -> binary
#       A record stating opcode 42 is packed into a scratch cache, then
#       `cachepack verify` decodes every record, writes it as text, packs the
#       text and compares the bytes: both records must come back exact. Before
#       the key existed the text had no line for 42 and the record came back
#       three bytes short.
#
#   text unchanged without it
#       A record that does not state 42 unpacks with no `recolall` line at all:
#       the content's exported text (every npc in cache.osrs242, none of which
#       states 42) does not change.
#
# No cache is needed: the cache is made here from text, under osrs242.
#
# Usage: sh test/test_cachepack_npc_recolall.sh
#        CACHEPACK=/path/to/cachepack overrides the in-tree binary.
set -u
CP=${CACHEPACK:-tools/cachepack/cachepack}
TMP=${TMPDIR:-/tmp}/cachepack_npc_recolall.$$
REV=osrs242
fail=0

if [ -z "${CACHEPACK:-}" ] && ! make -s -C tools cachepack >/dev/null; then
    echo "cachepack-npc-recolall: FAILED — cachepack did not build"
    exit 1
fi
rm -rf "$TMP"
mkdir -p "$TMP/tree/configs" "$TMP/tree/pack" "$TMP/tree/server/scripts" "$TMP/un"

# Every key of the type, `default` unless the block states it (the packer
# refuses a record missing one; `recolall` is the one key it does not ask for).
block() {
    name=$1
    shift
    echo "[$name]"
    for line in "$@"; do
        echo "$line"
    done
    "$CP" keys --rev "$REV" --types npc | while read -r type key kind rest; do
        stated=0
        for line in "$@"; do
            case "$line" in
            "$key="* | "$key"[0-9]*) stated=1 ;;
            esac
        done
        [ "$stated" = 1 ] || echo "$key=default"
    done
    echo
}

printf '0=fairy_recolall\n1=fairy_plain\n' >"$TMP/tree/configs/all.npc.compack"
{
    block fairy_recolall "name=Treasure fairy" "model1=2857" "size=1" "vislevel=0" \
        "recol1s=22753" "recol1d=43239" "recol2s=9447" "recol2d=8" "recolall=4660"
    block fairy_plain "name=Treasure fairy" "model1=2857" "size=1" "vislevel=0" \
        "recol1s=22753" "recol1d=43239" "recol2s=9447" "recol2d=8"
} >"$TMP/tree/configs/all.npc"

if ! "$CP" pack --src "$TMP/tree" --out "$TMP/cache" --rev "$REV" --types npc \
    >"$TMP/pack.log" 2>&1; then
    echo "FAIL: the tree did not pack"
    sed 's/^/    /' "$TMP/pack.log"
    fail=1
fi

if [ "$fail" = 0 ]; then
    "$CP" verify --cache "$TMP/cache" --rev "$REV" --src "$TMP/tree" --types npc \
        >"$TMP/verify.log" 2>&1
    status=$?
    # The table row: type, records, exact, same-len, differ, codec-ex, lost-here.
    line=$(awk '/^ *type +records/{t=1; next} t && $1=="npc"{print; exit}' "$TMP/verify.log")
    echo "verify: $line"
    if [ "$status" != 0 ] || [ "$(echo "$line" | awk '{print $2, $3, $7}')" != "2 2 0" ]; then
        echo "FAIL: binary -> text -> binary is not exact for both records"
        sed 's/^/    /' "$TMP/verify.log"
        fail=1
    else
        echo "ok: binary -> text -> binary, both records byte-identical"
    fi

    mkdir -p "$TMP/un/configs"
    cp "$TMP/tree/configs/all.npc.compack" "$TMP/un/configs/"
    "$CP" unpack --cache "$TMP/cache" --rev "$REV" --src "$TMP/un" --types npc \
        >"$TMP/unpack.log" 2>&1
    text="$TMP/un/configs/all.npc"
    with=$(awk '/^\[/{b=$0} b=="[fairy_recolall]" && /^recolall=/' "$text")
    without=$(awk '/^\[/{b=$0} b=="[fairy_plain]" && /^recolall/' "$text")
    if [ "$with" = "recolall=4660" ]; then
        echo "ok: the record stating 42 unpacks to recolall=4660"
    else
        echo "FAIL: the record stating 42 unpacks to '$with', not recolall=4660"
        fail=1
    fi
    if [ -z "$without" ]; then
        echo "ok: a record without 42 has no recolall line"
    else
        echo "FAIL: a record without 42 gained '$without'"
        fail=1
    fi
fi

rm -rf "$TMP"
if [ "$fail" = 0 ]; then
    echo "cachepack-npc-recolall: ok"
else
    echo "cachepack-npc-recolall: FAILED"
fi
exit $fail
