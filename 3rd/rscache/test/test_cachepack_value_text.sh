#!/bin/sh
#
# Typed values: the one ScriptVarType table (tools/cachepack/cp_value.c), both
# directions, through enums, param records and `param=` lines.
#
# A value is spelled by its declared type, in LostCity's authored grammar, the
# same way wherever it appears: a reference by name (`null` for -1), a coord as
# `level_mx_mz_lx_lz`, a boolean `yes`/`no`, a synth by its sound name, a stat by
# its name, a `^constant` as its text read through the type, a string raw. An
# enum is `inputtype=` / `outputtype=` words, `val=<key>,<value>` and `default=`,
# whatever its output type -- there is no `valstr=` / `defaultstr=`: the output
# type says which opcode the map and the default are.
#
#   synthetic  a hand-written tree exercising every spelling. It must pack,
#              unpack to the values it stated (spelled the one way), and pack
#              again to the same text: the grammar's fixed point. No cache needed.
#
#   cache      `cachepack verify --types enum,param,struct` against a real cache:
#              every record exact. Skipped, loudly, without one.
#
# Usage: sh test/test_cachepack_value_text.sh [CACHE_ROOT]
#        CACHEPACK=/path/to/cachepack overrides the in-tree binary.
set -u
ROOT=${1:-../..}
CP=${CACHEPACK:-tools/cachepack/cachepack}
TMP=${TMPDIR:-/tmp}/cachepack_value_text.$$
fail=0

if [ -z "${CACHEPACK:-}" ] && ! make -s -C tools cachepack >/dev/null; then
    echo "cachepack-value-text: FAILED — cachepack did not build"
    exit 1
fi
if [ ! -x "$CP" ]; then
    echo "cachepack-value-text: FAILED — no cachepack at $CP (make -C tools cachepack)"
    exit 1
fi
rm -rf "$TMP"
mkdir -p "$TMP/tree/configs" "$TMP/tree/pack" "$TMP/tree/interfaces" \
    "$TMP/tree/server/scripts/t" "$TMP/un/configs" "$TMP/re/configs"

# ---- synthetic -------------------------------------------------------------
T="$TMP/tree"
printf '0=e_coord\n1=e_obj\n2=e_str\n3=e_bool\n4=e_comp\n5=e_empty\n6=e_varbit\n7=e_synth\n' \
    >"$T/configs/all.enum.compack"
printf '0=p_coord\n1=p_bool\n2=p_obj\n3=p_synth\n4=p_str\n5=p_dbrow\n' >"$T/configs/all.param.compack"
printf '0=s_one\n' >"$T/configs/all.struct.compack"
printf '0=coins\n1=bones\n' >"$T/configs/all.obj.compack"
printf '0=bank_tab_1\n' >"$T/configs/all.varbit.compack"
printf '0=row_one\n' >"$T/configs/all.dbrow.compack"
printf '7=tone\n' >"$T/pack/4_soundeffects.pack"
printf '0=attack\n2=strength\n' >"$T/pack/stat.pack"
printf '5=toplevel\n' >"$T/pack/3_interfaces.pack"
printf '3=chat\n' >"$T/interfaces/toplevel.compack"
cat >"$T/server/scripts/t/t.constant" <<'EOF'
^home = 0_50_50_1_1
^greeting = Hello there
EOF
cat >"$T/configs/all.enum" <<'EOF'
[e_coord]
inputtype=int
outputtype=coord
val=0,0_51_48_41_35  // a trailing comment the line cleaner drops
val=1,^home
default=null

[e_obj]
inputtype=obj
outputtype=namedobj
val=coins,bones
val=bones,null
default=coins

[e_bool]
inputtype=stat
outputtype=boolean
val=attack,yes
val=strength,no
default=no

[e_comp]
inputtype=int
outputtype=component
val=0,toplevel:chat
default=default

[e_empty]
inputtype=int
outputtype=string
val=empty
default=default

[e_varbit]
inputtype=int
outputtype=varbit
val=0,bank_tab_1
default=null

[e_synth]
inputtype=int
outputtype=synth
val=0,tone
val=1,12
default=null

[e_str]
inputtype=int
outputtype=string
val=1,Chocolate Bomb, with commas
val=2,\^not a constant
val=3,^greeting
default=none
EOF
cat >"$T/configs/all.param" <<'EOF'
[p_coord]
type=coord
typechar=default
typeid=default
default=0_50_50_0_0
autodisable=default

[p_bool]
type=boolean
typechar=default
typeid=default
default=yes
autodisable=default

[p_obj]
type=namedobj
typechar=default
typeid=default
default=bones
autodisable=default

[p_synth]
type=synth
typechar=default
typeid=default
default=tone
autodisable=default

[p_str]
type=string
typechar=default
typeid=default
default=Hello, world
autodisable=default

[p_dbrow]
type=dbrow
typechar=208
typeid=74
default=row_one
autodisable=default
EOF
cat >"$T/configs/all.struct" <<'EOF'
[s_one]
param=p_coord,0_50_50_2_3
param=p_bool,no
param=p_obj,coins
param=p_synth,tone
param=p_str,a, b
param=p_dbrow,null
EOF
# Built with printf: an editor would strip the trailing blank a heredoc line needs.
printf 'val=4,ends in a blank\\ \n' >>"$T/configs/all.enum"

TYPES=enum,param,struct
if ! "$CP" pack --src "$T" --out "$TMP/c1" --rev osrs239 --types "$TYPES" \
        >"$TMP/pack1.log" 2>&1; then
    echo "cachepack-value-text: FAILED — the synthetic tree did not pack"
    cat "$TMP/pack1.log"
    exit 1
fi

cp "$T"/configs/*.compack "$TMP/un/configs/"
cp -R "$T/pack" "$TMP/un/"
"$CP" unpack --cache "$TMP/c1" --rev osrs239 --src "$TMP/un" --types "$TYPES" \
    >"$TMP/un.log" 2>&1 || { echo "cachepack-value-text: FAILED — unpack"; cat "$TMP/un.log"; exit 1; }

expect() {
    if ! grep -qxF -- "$2" "$TMP/un/configs/all.$1"; then
        echo "cachepack-value-text: FAILED — all.$1 lacks the line: $2"
        fail=1
    fi
}
refuse() {
    if grep -q -- "$2" "$TMP/un/configs/all.$1"; then
        echo "cachepack-value-text: FAILED — all.$1 still has: $2"
        fail=1
    fi
}
expect enum 'outputtype=coord'
expect enum 'val=0,0_51_48_41_35'
expect enum 'val=1,0_50_50_1_1'
expect enum 'default=null'
expect enum 'inputtype=obj'
expect enum 'outputtype=namedobj'
expect enum 'val=coins,bones'
expect enum 'val=bones,null'
expect enum 'default=coins'
expect enum 'outputtype=string'
expect enum 'val=1,Chocolate Bomb, with commas'
expect enum 'val=2,\^not a constant'
expect enum 'val=3,Hello there'
expect enum 'val=4,ends in a blank\ '
expect enum 'default=none'
expect enum 'inputtype=stat'
expect enum 'outputtype=boolean'
expect enum 'val=attack,yes'
expect enum 'val=strength,no'
expect enum 'default=no'
expect enum 'val=0,327683'
expect enum 'val=empty'
expect enum 'outputtype=varbit'
expect enum 'val=0,bank_tab_1'
expect enum 'outputtype=synth'
expect enum 'val=0,tone'
expect enum 'val=1,12'
refuse enum '^valstr='
refuse enum '^defaultstr='
refuse enum '^vallong='
refuse enum '^defaultlong='
expect param 'type=coord'
expect param 'default=0_50_50_0_0'
expect param 'type=boolean'
expect param 'default=yes'
expect param 'type=namedobj'
expect param 'default=bones'
expect param 'type=synth'
expect param 'default=tone'
expect param 'type=string'
expect param 'default=Hello, world'
expect param 'type=dbrow'
expect param 'default=row_one'
refuse param '^defaultstr='
expect struct 'param=p_coord,0_50_50_2_3'
expect struct 'param=p_bool,no'
expect struct 'param=p_obj,coins'
expect struct 'param=p_synth,tone'
expect struct 'param=p_str,a, b'
expect struct 'param=p_dbrow,null'

# And the unpacked text packs to the same bytes: the grammar's fixed point.
cp "$TMP/un/configs/"* "$TMP/re/configs/"
cp -R "$TMP/un/pack" "$TMP/re/"
mkdir -p "$TMP/re/interfaces"
cp "$T/interfaces/"* "$TMP/re/interfaces/"
if ! "$CP" pack --src "$TMP/re" --out "$TMP/c2" --rev osrs239 --types "$TYPES" \
        >"$TMP/pack2.log" 2>&1; then
    echo "cachepack-value-text: FAILED — the unpacked text did not pack"
    cat "$TMP/pack2.log"
    fail=1
else
    rm -rf "$TMP/un2"
    mkdir -p "$TMP/un2/configs"
    cp "$TMP/un/configs/"*.compack "$TMP/un2/configs/"
    cp -R "$TMP/un/pack" "$TMP/un2/"
    "$CP" unpack --cache "$TMP/c2" --rev osrs239 --src "$TMP/un2" --types "$TYPES" \
        >/dev/null 2>&1
    for t in enum param struct; do
        if ! cmp -s "$TMP/un/configs/all.$t" "$TMP/un2/configs/all.$t"; then
            echo "cachepack-value-text: FAILED — all.$t is not a fixed point of unpack -> pack"
            diff "$TMP/un/configs/all.$t" "$TMP/un2/configs/all.$t" | head -10
            fail=1
        fi
    done
fi

# The retired spellings are refused, not read: one spelling per field.
mkdir -p "$TMP/old/configs"
cp "$T"/configs/*.compack "$TMP/old/configs/"
cat >"$TMP/old/configs/all.enum" <<'EOF'
[e_str]
inputtype=int
outputtype=string
val=default
default=default
valstr=1,hello
defaultstr=none
EOF
"$CP" pack --src "$TMP/old" --out "$TMP/c3" --rev osrs239 --types enum >"$TMP/old.log" 2>&1
if ! grep -q "unknown key valstr" "$TMP/old.log"; then
    echo "cachepack-value-text: FAILED — a retired \`valstr=\` was not reported"
    cat "$TMP/old.log"
    fail=1
fi
[ "$fail" -eq 0 ] && echo "cachepack-value-text: synthetic tree packs, unpacks and repacks exactly"

# ---- a real cache ------------------------------------------------------------
CACHE="$ROOT/cache.osrs239"
SRC="$ROOT/OSRS-Content/osrs239-content"
if [ ! -f "$CACHE/main_file_cache.dat2" ] || [ ! -f "$SRC/meta.ini" ]; then
    echo "cachepack-value-text: cache.osrs239 SKIPPED — no cache/content at $ROOT"
else
    "$CP" verify --cache "$CACHE" --rev osrs239 --src "$SRC" --types enum,param,struct \
        --tmp "$TMP/v" >"$TMP/v.log" 2>&1
    rows=$(grep -E '^(enum|param|struct) +[0-9]' "$TMP/v.log")
    echo "$rows" | sed "s/^/   cache.osrs239 /"
    if [ -z "$rows" ] || echo "$rows" | awk '$2 != $3 || $7 != 0 { bad = 1 } END { exit !bad }'; then
        echo "cachepack-value-text: FAILED — cache.osrs239 does not round-trip exactly"
        fail=1
    fi
fi

[ -n "${KEEP:-}" ] || rm -rf "$TMP"
if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "cachepack-value-text: all bars met"
exit 0
