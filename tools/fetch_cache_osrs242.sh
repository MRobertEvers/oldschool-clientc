#!/usr/bin/env bash
# Put Jagex OSRS 242 dat2/idx at $REPO/cache.osrs242.
# OpenRS2 #2772 is the 2026-11-18 14:45 UTC b242 cache (the content tree was
# patched to it that day; #2757, 2026-11-03, was the first 242 cache taken).
# The dat2's size names the cache: a cache.osrs242 of another size is an older
# 242 cache, and the tree's configs and scripts no longer describe it.
set -euo pipefail
ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
DEST="$ROOT/cache.osrs242"
DAT2="$DEST/main_file_cache.dat2"
DAT2_SIZE=252382774
if [[ -f "$DAT2" ]]; then
	sz=$(wc -c <"$DAT2" | tr -d ' ')
	if [[ "$sz" -eq "$DAT2_SIZE" ]]; then
		echo "cache.osrs242 already present ($sz bytes)"
		exit 0
	fi
	if [[ -z "${CACHE_OSRS242_URL:-}" ]]; then
		echo "cache.osrs242 is another 242 cache ($sz bytes, want $DAT2_SIZE); fetching #2772 over it"
	fi
fi
URL="${CACHE_OSRS242_URL:-https://archive.openrs2.org/caches/runescape/2772/disk.zip}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
echo "fetching $URL"
curl -fL --retry 5 --retry-delay 4 -o "$TMP/disk.zip" "$URL"
python3 - <<PY
import zipfile, pathlib, shutil, sys
tmp = pathlib.Path("$TMP")
out = tmp / "out"
out.mkdir()
with zipfile.ZipFile(tmp / "disk.zip") as z:
    z.extractall(out)
hits = list(out.rglob("main_file_cache.dat2"))
if not hits:
    sys.exit("disk.zip has no main_file_cache.dat2")
src = hits[0].parent
dst = pathlib.Path("$DEST")
dst.mkdir(parents=True, exist_ok=True)
for p in dst.glob("main_file_cache.*"):
    p.unlink()
for p in src.glob("main_file_cache.*"):
    shutil.copy2(p, dst / p.name)
print("installed", dst, "dat2", (dst / "main_file_cache.dat2").stat().st_size)
PY
