#!/usr/bin/env python3
"""camera.txt -> camera.png.

The .txt is the art; this only bakes it. Kept next to the picture for the
reason the gameframe layout's SOURCES.sh is: a folder of pictures with no
provenance is one nobody can correct.
"""
import sys, zlib, struct


def read_art(path):
    palette, art, section = {}, [], None

    for raw in open(path):
        line = raw.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if line[0] not in " \t":
            section = line.strip()
            continue
        if section == "palette":
            ch, hexrgb = line.split()[0], line.split()[1]
            palette[ch] = tuple(int(hexrgb[i:i + 2], 16) for i in (0, 2, 4))
        elif section == "art":
            art.append(line[2:])
    assert art, "no art rows in " + path
    assert len({len(r) for r in art}) == 1, "art rows differ in width"
    return palette, art


def write_png(path, palette, art):
    w, h = len(art[0]), len(art)
    rows = bytearray()

    for row in art:
        rows.append(0)                                  # filter: none
        for ch in row:
            if ch == ".":
                rows += b"\0\0\0\0"
            else:
                r, g, b = palette[ch]
                rows += bytes((r, g, b, 255))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)
    return w, h


if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "camera.txt"
    dst = sys.argv[2] if len(sys.argv) > 2 else "camera.png"
    pal, art = read_art(src)
    print("%s -> %s (%dx%d, %d colours)" % ((src, dst) + write_png(dst, pal, art) + (len(pal),)))
