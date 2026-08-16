#!/usr/bin/env python3
"""Scan all loc configs for multi-shape entries missing shape 22."""

import subprocess
import sys

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <cache_dir>", file=sys.stderr)
        return 1
    cache = sys.argv[1]
    shapes_bin = "./build/dump_loc_shapes"
    # get max loc id from reference - use loc 50000 scan with binary loop
    missing = []
    for loc_id in range(1, 50000):
        try:
            out = subprocess.check_output(
                [shapes_bin, cache, str(loc_id)],
                stderr=subprocess.DEVNULL,
                timeout=2,
            ).decode()
        except subprocess.CalledProcessError:
            continue
        if "not found" in out:
            continue
        if "shapes=yes" not in out:
            continue
        if "shape=22" in out:
            continue
        if "count=0" in out.split("\n")[0]:
            continue
        missing.append((loc_id, out.strip().split("\n")[0]))
        if len(missing) >= 30:
            break
    print(f"multi-shape locs missing shape 22 (first 30): {len(missing)}")
    for loc_id, line in missing:
        print(loc_id, line)
    return 0

if __name__ == "__main__":
    sys.exit(main())
