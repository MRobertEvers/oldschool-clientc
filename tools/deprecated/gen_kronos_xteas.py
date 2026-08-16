#!/usr/bin/env python3
"""Convert Kronos region_keys.json to client xteas.json for cache.kronos."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DUMP_BIN = REPO_ROOT / "build/dump_map_index"
MAPS_TABLE = 5


def djb2(name: str) -> int:
    h = 0
    for c in name:
        h = ((h << 5) - h + ord(c)) & 0xFFFFFFFF
    if h >= 0x80000000:
        h -= 0x100000000
    return h


def load_identifier_to_group(cache_dir: Path, dump_bin: Path) -> dict[int, int]:
    if not dump_bin.is_file():
        raise FileNotFoundError(
            f"{dump_bin} not found; build with: cmake --build build --target dump_map_index"
        )

    proc = subprocess.run(
        [str(dump_bin), str(cache_dir)],
        check=True,
        capture_output=True,
        text=True,
    )

    id_map: dict[int, int] = {}
    for line in proc.stdout.splitlines():
        if not line.strip():
            continue
        group_s, ident_s = line.split("\t", 1)
        group = int(group_s)
        ident = int(ident_s)
        id_map[ident] = group
    return id_map


def convert_region_keys(
    region_keys: dict[str, list[int]],
    id_map: dict[int, int],
) -> tuple[list[dict], int, int]:
    entries: list[dict] = []
    skipped_zero = 0
    skipped_no_archive = 0

    for region_id_s, key in region_keys.items():
        if not any(k != 0 for k in key):
            skipped_zero += 1
            continue

        mapsquare = int(region_id_s)
        map_x = mapsquare >> 8
        map_z = mapsquare & 0xFF
        name = f"l{map_x}_{map_z}"
        name_hash = djb2(name)
        group = id_map.get(name_hash)
        if group is None:
            print(
                f"warning: no map archive for region {mapsquare} ({name})",
                file=sys.stderr,
            )
            skipped_no_archive += 1
            continue

        entries.append(
            {
                "archive": MAPS_TABLE,
                "group": group,
                "name_hash": name_hash,
                "name": name,
                "mapsquare": mapsquare,
                "key": key,
            }
        )

    entries.sort(key=lambda e: e["group"])
    return entries, skipped_zero, skipped_no_archive


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--region-keys",
        type=Path,
        default=None,
        help="Path to Kronos kronos-server/data/region_keys.json",
    )
    parser.add_argument(
        "--cache",
        type=Path,
        default=REPO_ROOT / "cache.kronos",
        help="Dat2 cache directory (default: cache.kronos)",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Output xteas.json path (default: <cache>/xteas.json)",
    )
    parser.add_argument(
        "--dump-bin",
        type=Path,
        default=DUMP_BIN,
        help="Path to dump_map_index binary",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify dump_map_index exists and exit",
    )
    args = parser.parse_args()

    if args.check:
        if args.dump_bin.is_file():
            print(f"ok: {args.dump_bin}")
            return 0
        print(
            f"missing: {args.dump_bin}\n"
            "build with: cmake --build build --target dump_map_index",
            file=sys.stderr,
        )
        return 1

    if args.region_keys is None:
        parser.error("--region-keys is required unless --check is set")

    out_path = args.out if args.out is not None else args.cache / "xteas.json"

    region_keys = json.loads(args.region_keys.read_text())
    id_map = load_identifier_to_group(args.cache, args.dump_bin)
    entries, skipped_zero, skipped_no_archive = convert_region_keys(region_keys, id_map)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(entries, indent=2) + "\n")

    print(
        f"wrote {out_path}: {len(entries)} entries "
        f"(skipped_zero={skipped_zero}, skipped_no_archive={skipped_no_archive})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
