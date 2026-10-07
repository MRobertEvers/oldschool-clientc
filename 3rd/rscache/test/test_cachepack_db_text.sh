#!/bin/sh
#
# dbtable / dbrow text: the authored grammar, both directions.
#
# `configs/all.dbtable` and `all.dbrow` are written and read in LostCity's grammar
# (`column=` / `default=`, `table=` / `data=`), the one the authored tree and the
# game server already use. Two bars:
#
#   synthetic  a hand-written tree exercising what the cache needs and LostCity
#              never wrote -- an ABSENT hole in the column numbering, a comma in a
#              non-last string (`\,`), a last string holding bare commas, trailing
#              blanks (`\ `), a leading caret that is not a constant (`\^`), a
#              `^constant`, a `//` comment, `null`, coords and booleans. It must
#              pack, unpack to the values it stated, and pack again to the same
#              bytes. No cache needed.
#
#   cache      `cachepack verify --types dbtable,dbrow` against a real cache: every
#              record exact. Skipped, loudly, without one.
#
# Usage: sh test/test_cachepack_db_text.sh [CACHE_ROOT]
#        CACHEPACK=/path/to/cachepack overrides the in-tree binary.
set -u
ROOT=${1:-../..}
CP=${CACHEPACK:-tools/cachepack/cachepack}
TMP=${TMPDIR:-/tmp}/cachepack_db_text.$$
fail=0

if [ ! -x "$CP" ]; then
    echo "cachepack-db-text: FAILED — no cachepack at $CP (make -C tools cachepack)"
    exit 1
fi
rm -rf "$TMP"
mkdir -p "$TMP/tree/configs" "$TMP/tree/server/scripts/t" "$TMP/un/configs" "$TMP/re/configs"

# ---- synthetic -------------------------------------------------------------
T="$TMP/tree"
printf '0=t_test\n' >"$T/configs/all.dbtable.compack"
printf '0=r_one\n1=r_two\n' >"$T/configs/all.dbrow.compack"
printf '0=coins\n1=bones\n' >"$T/configs/all.obj.compack"
cat >"$T/configs/all.dbtable" <<'EOF'
[t_test]
column=id,int
column=hole,ABSENT
column=text,string,int
column=where,coord
column=thing,obj,string,LIST
column=flag,boolean
default=flag,true
EOF
cat >"$T/configs/all.dbrow" <<'EOF'
[r_one]
table=t_test
data=id,7  // a trailing comment the server's line cleaner drops
data=text,a\, b,3
data=where,0_50_50_10_20
data=thing,coins,last, with commas
data=thing,null,second tuple
data=flag,false

[r_two]
table=t_test
data=id,^answer
data=text,\^not a constant,-1
data=flag,true
EOF
# Built with printf: an editor would strip the trailing blank a heredoc line needs.
printf 'data=thing,bones,ends in blanks\\ \\ \n' >>"$T/configs/all.dbrow"
printf '^answer = 42 // the constant\n' >"$T/server/scripts/t/t.constant"

if ! "$CP" pack --src "$T" --out "$TMP/c1" --rev osrs239 --types dbtable,dbrow \
        >"$TMP/pack1.log" 2>&1; then
    echo "cachepack-db-text: FAILED — the synthetic tree did not pack"
    cat "$TMP/pack1.log"
    exit 1
fi

cp "$T"/configs/*.compack "$TMP/un/configs/"
"$CP" unpack --cache "$TMP/c1" --rev osrs239 --src "$TMP/un" --types dbtable,dbrow \
    >"$TMP/un.log" 2>&1 || { echo "cachepack-db-text: FAILED — unpack"; cat "$TMP/un.log"; exit 1; }

# The cache carries no column names (a gameval table does), so they come back
# as col<N>; the values are what is checked.
expect() {
    if ! grep -qxF -- "$2" "$TMP/un/configs/all.$1"; then
        echo "cachepack-db-text: FAILED — all.$1 lacks the line: $2"
        fail=1
    fi
}
expect dbtable 'column=col1,ABSENT'
expect dbtable 'column=col2,string,int'
expect dbtable 'default=col5,true'
expect dbrow 'data=col0,7'
expect dbrow 'data=col0,42'
expect dbrow 'data=col2,a\, b,3'
expect dbrow 'data=col2,\^not a constant,-1'
expect dbrow 'data=col3,0_50_50_10_20'
expect dbrow 'data=col4,coins,last, with commas'
expect dbrow 'data=col4,null,second tuple'
expect dbrow 'data=col4,bones,ends in blanks\ \ '
expect dbrow 'data=col5,false'

# And the unpacked text packs to the same bytes: the grammar's fixed point.
cp "$TMP/un/configs/"* "$TMP/re/configs/"
if ! "$CP" pack --src "$TMP/re" --out "$TMP/c2" --rev osrs239 --types dbtable,dbrow \
        >"$TMP/pack2.log" 2>&1; then
    echo "cachepack-db-text: FAILED — the unpacked text did not pack"
    cat "$TMP/pack2.log"
    fail=1
else
    rm -rf "$TMP/un2"
    mkdir -p "$TMP/un2/configs"
    cp "$TMP/un/configs/"*.compack "$TMP/un2/configs/"
    "$CP" unpack --cache "$TMP/c2" --rev osrs239 --src "$TMP/un2" --types dbtable,dbrow \
        >/dev/null 2>&1
    for t in dbtable dbrow; do
        if ! cmp -s "$TMP/un/configs/all.$t" "$TMP/un2/configs/all.$t"; then
            echo "cachepack-db-text: FAILED — all.$t is not a fixed point of unpack -> pack"
            diff "$TMP/un/configs/all.$t" "$TMP/un2/configs/all.$t" | head -10
            fail=1
        fi
    done
fi
[ "$fail" -eq 0 ] && echo "cachepack-db-text: synthetic tree packs, unpacks and repacks exactly"

# ---- a real cache ------------------------------------------------------------
for c in osrs239 osrs230; do
    CACHE="$ROOT/cache.$c"
    if [ ! -f "$CACHE/main_file_cache.dat2" ]; then
        echo "cachepack-db-text: cache.$c SKIPPED — no cache at $CACHE"
        continue
    fi
    "$CP" verify --cache "$CACHE" --rev "$c" --src "$TMP/un" --types dbtable,dbrow \
        --tmp "$TMP/v_$c" >"$TMP/v_$c.log" 2>&1
    rows=$(grep -E '^(dbtable|dbrow) +[0-9]' "$TMP/v_$c.log")
    echo "$rows" | sed "s/^/   cache.$c /"
    if [ -z "$rows" ] || echo "$rows" | awk '$2 != $3 || $7 != 0 { bad = 1 } END { exit !bad }'; then
        echo "cachepack-db-text: FAILED — cache.$c does not round-trip exactly"
        fail=1
    fi
done

rm -rf "$TMP"
if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "cachepack-db-text: all bars met"
exit 0
