#!/bin/sh
# Interoperability check for our bzip2 encoder against the reference binary.
#
# Our encoder and our decoder could share a misreading of the format and still
# round-trip cleanly with each other. bzip2 1.0.8 is the independent authority,
# so every stream we produce is fed to `bzip2 -t` (integrity, which verifies the
# per-block and stream CRCs) and `bzip2 -dc` (content must match byte for byte).
#
# Skips cleanly when no bzip2 binary is installed.

set -e

HELPER="$1"
if [ -z "$HELPER" ] || [ ! -x "$HELPER" ]; then
	echo "usage: $0 <path to bzip_interop_helper>" >&2
	exit 2
fi

if ! command -v bzip2 >/dev/null 2>&1; then
	echo "bzip2-interop: SKIP (no bzip2 binary)"
	exit 0
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

fail=0
checks=0

check() {
	name="$1"
	src="$2"
	checks=$((checks + 1))

	if ! "$HELPER" "$src" "$WORK/out.bz2" 2>"$WORK/err"; then
		echo "   FAIL $name: encoder refused the input"
		cat "$WORK/err" >&2
		fail=$((fail + 1))
		return
	fi

	if ! bzip2 -t "$WORK/out.bz2" 2>"$WORK/err"; then
		echo "   FAIL $name: bzip2 -t rejected the stream"
		cat "$WORK/err" >&2
		fail=$((fail + 1))
		return
	fi

	bzip2 -dc "$WORK/out.bz2" >"$WORK/restored" 2>"$WORK/err" || {
		echo "   FAIL $name: bzip2 -dc failed"
		cat "$WORK/err" >&2
		fail=$((fail + 1))
		return
	}

	if ! cmp -s "$src" "$WORK/restored"; then
		echo "   FAIL $name: content differs after bzip2 -dc"
		fail=$((fail + 1))
		return
	fi
}

# Short text.
printf 'The quick brown fox jumps over the lazy dog.' >"$WORK/text"
check "short text" "$WORK/text"

# Single byte.
printf 'x' >"$WORK/one"
check "one byte" "$WORK/one"

# RLE1 boundaries: 3, 4 and 5 identical bytes.
printf 'aaa' >"$WORK/three"
check "three identical" "$WORK/three"
printf 'aaaa' >"$WORK/four"
check "four identical" "$WORK/four"
printf 'aaaaa' >"$WORK/five"
check "five identical" "$WORK/five"

# Long single-symbol run: crosses RLE1's 255+4 cap repeatedly.
dd if=/dev/zero bs=1024 count=64 2>/dev/null | tr '\0' 'Z' >"$WORK/run"
check "65536-byte run" "$WORK/run"

# Full alphabet, incompressible: worst case for the sort, and forces the
# multi-table Huffman path.
dd if=/dev/urandom bs=1024 count=64 2>/dev/null >"$WORK/noise"
check "65536 random bytes" "$WORK/noise"

# Structured, repetitive data resembling a config archive.
i=0
: >"$WORK/repetitive"
while [ $i -lt 400 ]; do
	printf 'item %d: name=Ring of stone, cost=1500, model=2314\n' "$i" >>"$WORK/repetitive"
	i=$((i + 1))
done
check "repetitive records" "$WORK/repetitive"

# Real cache data, if a jagfile-era cache is present: the actual target input.
for candidate in ../../cache254/main_file_cache.idx0 ../../cache254.lostcity/main_file_cache.dat; do
	if [ -f "$candidate" ]; then
		dd if="$candidate" of="$WORK/real" bs=1024 count=48 2>/dev/null
		check "real cache bytes ($candidate)" "$WORK/real"
		break
	fi
done

if [ "$fail" -eq 0 ]; then
	echo "bzip2-interop: $checks checks passed (verified by bzip2 -t and -dc)"
	exit 0
fi

echo "bzip2-interop: $fail/$checks checks FAILED"
exit 1
