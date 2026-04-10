#!/usr/bin/env bash
# Rename painters cullmap .bin files: replace the near-z prefix segment
# "nz<N>" -> "f<N>" and "nzm<N>" -> "fm<N>" in filenames.
set -euo pipefail

CULLMAPS_DIR="$(cd "$(dirname "$0")/../src/osrs/revconfig/configs/cullmaps" && pwd)"

renamed=0
skipped=0

for src in "$CULLMAPS_DIR"/*.bin; do
    base="$(basename "$src")"
    # Replace _nzm<digits> (negative near) then _nz<digits> (non-negative near).
    new="${base//_nzm/_fm}"
    new="${new//_nz/_f}"
    if [[ "$new" == "$base" ]]; then
        skipped=$((skipped + 1))
        continue
    fi
    mv "$src" "$CULLMAPS_DIR/$new"
    renamed=$((renamed + 1))
done

echo "rename_cullmaps_nz_to_f: renamed=$renamed skipped=$skipped"
