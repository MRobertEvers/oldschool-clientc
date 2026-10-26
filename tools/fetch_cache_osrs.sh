#!/usr/bin/env bash
# Put Jagex OSRS rev-<N> dat2/idx at $REPO/cache.osrs<N>, from OpenRS2.
#
#   tools/fetch_cache_osrs.sh 223            one revision
#   tools/fetch_cache_osrs.sh 222 241        every revision in a range
#
# The cache is the NEWEST live dump OpenRS2 holds for that build: a recording
# made anywhere in the build's lifetime names ids that exist in it. Below 237
# the map loc groups are XTEA-encrypted, so the dump's keys are saved beside it
# as xteas.json (the file app.c and the server read). A server that sends keys
# (REBUILD_* below 237) overrides them square by square.
#
# CACHE_OSRS_ID=<openrs2 id> pins one dump instead of the newest.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LO="${1:?usage: $0 <revision> [<last revision>]}"
HI="${2:-$LO}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
curl -fsSL --retry 5 --retry-delay 4 -o "$TMP/caches.json" https://archive.openrs2.org/caches.json

for (( REV = LO; REV <= HI; REV++ )); do
	DEST="$ROOT/cache.osrs$REV"
	DAT2="$DEST/main_file_cache.dat2"
	if [[ -f "$DAT2" && $(wc -c <"$DAT2") -gt 100000000 ]]; then
		echo "cache.osrs$REV already present ($(wc -c <"$DAT2") bytes)"
		continue
	fi
	ID="${CACHE_OSRS_ID:-$(python3 -I - "$TMP/caches.json" "$REV" <<'PY'
import json, sys
caches, rev = json.load(open(sys.argv[1])), int(sys.argv[2])
live = [c for c in caches if c["game"] == "oldschool" and c["environment"] == "live"
        and c["disk_store_valid"] and any(b["major"] == rev for b in c["builds"] or [])]
if not live:
    sys.exit("OpenRS2 has no live disk cache for build %d" % rev)
print(max(live, key=lambda c: c["timestamp"] or "")["id"])
PY
)}"
	echo "cache.osrs$REV <- OpenRS2 #$ID"
	rm -rf "$TMP/out" "$TMP/disk.zip"
	curl -fL --retry 5 --retry-delay 4 -o "$TMP/disk.zip" "https://archive.openrs2.org/caches/runescape/$ID/disk.zip"
	mkdir -p "$DEST"
	python3 -I - "$TMP" "$DEST" <<'PY'
import pathlib, shutil, sys, zipfile
tmp, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
out = tmp / "out"
out.mkdir()
with zipfile.ZipFile(tmp / "disk.zip") as z:
    z.extractall(out)
hits = list(out.rglob("main_file_cache.dat2"))
if not hits:
    sys.exit("disk.zip has no main_file_cache.dat2")
for p in hits[0].parent.glob("main_file_cache.*"):
    shutil.copy2(p, dst / p.name)
print("installed", dst, "dat2", (dst / "main_file_cache.dat2").stat().st_size)
PY
	if (( REV < 237 )); then
		curl -fsSL --retry 5 --retry-delay 4 -o "$DEST/xteas.json" "https://archive.openrs2.org/caches/runescape/$ID/keys.json"
		echo "  xteas.json: $(python3 -I -c 'import json,sys; print(len(json.load(open(sys.argv[1]))))' "$DEST/xteas.json") keys"
	fi
done
