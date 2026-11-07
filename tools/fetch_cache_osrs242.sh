#!/usr/bin/env bash
# Put Jagex OSRS 242 dat2/idx at $REPO/cache.osrs242.
# OpenRS2 #2757 is the 2026-11-06 b242 dump.
set -euo pipefail
ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
DEST="$ROOT/cache.osrs242"
DAT2="$DEST/main_file_cache.dat2"
if [[ -f "$DAT2" ]]; then
	sz=$(wc -c <"$DAT2")
	if [[ "$sz" -gt 100000000 ]]; then
		echo "cache.osrs242 already present ($sz bytes)"
		exit 0
	fi
fi
URL="${CACHE_OSRS242_URL:-https://archive.openrs2.org/caches/runescape/2757/disk.zip}"
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
for p in src.glob("main_file_cache.*"):
    shutil.copy2(p, dst / p.name)
print("installed", dst, "dat2", (dst / "main_file_cache.dat2").stat().st_size)
PY
