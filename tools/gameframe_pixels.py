#!/usr/bin/env python3
"""Pixel assertions for gameframe_matrix.sh (standard library only)."""
import argparse
import re
import struct
from pathlib import Path


def read_bmp(path):
    data = Path(path).read_bytes()
    if data[:2] != b"BM":
        raise ValueError("capture is not a BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height, planes, bits, compression = struct.unpack_from("<iiHHI", data, 18)
    if width <= 0 or not height or planes != 1 or bits not in (24, 32) or compression:
        raise ValueError("unsupported BMP capture")
    stride = ((width * bits + 31) // 32) * 4
    if len(data) < offset + stride * abs(height):
        raise ValueError("truncated BMP capture")
    rows = []
    for y in range(abs(height)):
        sy = abs(height) - 1 - y if height > 0 else y
        start = offset + stride * sy
        rows.append([tuple(data[start + x * (bits // 8):start + x * (bits // 8) + 3])
                     for x in range(width)])
    return width, abs(height), rows


def check(path, frame, root, bounds_path=None):
    width, height, rows = read_bmp(path)
    failures = []
    if frame == "gameframe-layout/classic-fixed":
        # The approved plain-rock band spans x=0..495, y=467..498.
        # Its 29-column source repeats without any of the four old recesses.
        # Checking all repeats also catches partially covered/late old art.
        # Mobile puts its message area below the filters, covering part of
        # this strip. Test the exposed rock, not the chat painted over it.
        backing = None
        if bounds_path:
            match = re.search(r"BOUNDS[^\n]*\(162\|37\)[^\n]*abs=(-?\d+),(-?\d+) (\d+)x(\d+)",
                              Path(bounds_path).read_text())
            if match:
                backing = tuple(map(int, match.groups()))
        def exposed(x, y):
            if backing is None:
                return True
            bx, by, bw, bh = backing
            return not (bx <= x < bx + bw and by <= y < by + bh)
        valid = width >= 496 and height >= 499 and (root != 601 or backing is not None)
        if valid:
            valid = all(rows[y][x] == rows[y][x % 29]
                        for y in range(467, 499) for x in range(29, 496)
                        if exposed(x, y) and exposed(x % 29, y))
            valid = valid and len({p for row in rows[467:499] for p in row[:29]}) > 3
        print(f"PIXEL no_captionless_2004_hollows={'PASS' if valid else 'FAIL'} root={root}")
        if not valid:
            failures.append("no_captionless_2004_hollows")
    return failures


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture")
    parser.add_argument("--frame", required=True)
    parser.add_argument("--root", required=True, type=int)
    parser.add_argument("--bounds", help="matching TORIRS_DUMP_BOUNDS log")
    args = parser.parse_args()
    try:
        raise SystemExit(bool(check(args.capture, args.frame, args.root, args.bounds)))
    except (OSError, ValueError, struct.error) as error:
        print(f"PIXEL capture=FAIL: {error}")
        raise SystemExit(1)
