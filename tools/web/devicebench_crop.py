#!/usr/bin/env python3
"""Crop a TORIRS_SCENE_EXPORT file to the tiles around one point, gzipped.

The web device bench (src/web/devicebench.html) draws a real scene in plain
JavaScript. The export (src/app/app_scene_export.c) holds every element of the
loaded squares; the bench draws only what the client would at the minimum
draw distance, so this keeps the elements within `--radius` tiles of the
camera's tile and writes the same format, gzipped (the page inflates it with
DecompressionStream).

    TORIRS_WORLD_MAP="49,49;49,50;50,49;50,50" TORIRS_SCENE_EXPORT=lumbridge.tscn \\
        ./src/torirs --manifest <osrs242 manifest> --soft3d --offline
    tools/web/devicebench_crop.py lumbridge.tscn --tile 3222,3218 --radius 26 \\
        -o src/web/devicebench_lumbridge.tscn.gz
"""

import argparse
import gzip
import struct
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("export")
    parser.add_argument("--tile", required=True, help="absolute tile x,z the camera looks at")
    parser.add_argument("--radius", type=int, default=26, help="tiles kept around it")
    parser.add_argument("-o", "--out", required=True)
    args = parser.parse_args()

    data = open(args.export, "rb").read()
    if data[:4] != b"TSCN":
        print(f"{args.export}: not a scene export", file=sys.stderr)
        return 1
    version, base_x, base_z, count = struct.unpack_from("<Iiii", data, 4)
    if version != 3:
        print(f"{args.export}: version {version}, this reads 3", file=sys.stderr)
        return 1
    tile_x, tile_z = (int(v) for v in args.tile.split(","))
    centre_x = (tile_x - base_x) * 128 + 64
    centre_z = (tile_z - base_z) * 128 + 64
    reach = args.radius * 128

    at = 20
    kept = []
    for _ in range(count):
        x, _y, z, _yaw, vc, fc = struct.unpack_from("<iiiiII", data, at)
        size = 24 + 6 * vc + 6 * fc + 6 * fc + 2 * fc + fc + 6 * fc
        if abs(x - centre_x) <= reach and abs(z - centre_z) <= reach:
            kept.append(data[at:at + size])
        at += size
    # The palette, then the texture averages: carried as they are.
    tail = data[at:]
    if len(tail) < 65536 * 4 + 4:
        print(f"{args.export}: palette or texture table missing", file=sys.stderr)
        return 1

    # The header's base becomes the camera's tile, so the page needs no
    # arguments: element coordinates stay as exported, and the camera looks
    # at (centre_x, centre_z) in them.
    header = b"TSCN" + struct.pack("<Iiii", 3, centre_x, centre_z, len(kept))
    with gzip.open(args.out, "wb", compresslevel=9) as out:
        out.write(header)
        for chunk in kept:
            out.write(chunk)
        out.write(tail)
    print(f"kept {len(kept)} of {count} elements around tile {tile_x},{tile_z} -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
