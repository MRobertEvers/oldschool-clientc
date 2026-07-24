#!/usr/bin/env python3
"""Scan cache/main_file_cache.dat2 idx12 and report clientscript opcode usage."""

from __future__ import annotations

import struct
import sys
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CACHE = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "cache"
DAT2 = CACHE / "main_file_cache.dat2"
IDX12 = CACHE / "main_file_cache.idx12"
META = REPO / "src" / "cs2vm2" / "cs2_opcode_meta.c"


def load_known_opcodes() -> set[int]:
    known: set[int] = set()
    if not META.is_file():
        return known
    for line in META.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("[") and "] = {" in line:
            idx = int(line[1 : line.index("]")])
            if '"_unknown"' not in line:
                known.add(idx)
    return known


def read_u16(data: bytes, off: int) -> int:
    return (data[off] << 8) | data[off + 1]


def decode_script(data: bytes) -> list[int]:
    if len(data) < 16 or data[0] != 0:
        return []
    trailer_len = read_u16(data, len(data) - 2)
    trailer_pos = len(data) - 14 - trailer_len
    if trailer_pos < 1:
        return []

    op_count = struct.unpack_from(">i", data, trailer_pos)[0]
    pos = 1
    while pos < len(data) and data[pos] != 0:
        pos += 1
    pos += 1

    opcodes: list[int] = []
    while pos + 1 < trailer_pos and len(opcodes) < op_count:
        opcode = read_u16(data, pos)
        pos += 2
        opcodes.append(opcode)
        if opcode == 3:
            while pos < trailer_pos and data[pos] != 0:
                pos += 1
            pos += 1
        elif opcode >= 100 or opcode in (21, 38, 39):
            pos += 1
        else:
            pos += 4
    return opcodes


def read_index_entry(idx: bytes, entry: int) -> tuple[int, int]:
    off = entry * 6
    if off + 6 > len(idx):
        return 0, 0
    length = (idx[off] << 16) | (idx[off + 1] << 8) | idx[off + 2]
    sector = (idx[off + 3] << 16) | (idx[off + 4] << 8) | idx[off + 5]
    return length, sector


def iter_archives() -> list[bytes]:
    if not DAT2.is_file() or not IDX12.is_file():
        return []
    idx = IDX12.read_bytes()
    entry_count = len(idx) // 6
    dat2 = open(DAT2, "rb")
    archives: list[bytes] = []
    for entry in range(entry_count):
        length, sector = read_index_entry(idx, entry)
        if length <= 0 or sector <= 0:
            continue
        chunks: list[bytes] = []
        remaining = length
        sec = sector
        while remaining > 0 and sec > 0:
            dat2.seek(sec * 520)
            header = dat2.read(8)
            if len(header) < 8:
                break
            chunk_len = min(remaining, 512)
            chunks.append(dat2.read(chunk_len))
            remaining -= chunk_len
            sec = struct.unpack_from(">i", header, 4)[0]
        archives.append(b"".join(chunks))
    dat2.close()
    return archives


def main() -> int:
    if not DAT2.is_file():
        print(f"skip: no dat2 at {DAT2}")
        return 0

    known = load_known_opcodes()
    freq: Counter[int] = Counter()
    for blob in iter_archives():
        freq.update(decode_script(blob))

    if not freq:
        print("skip: no scripts decoded (cache may be absent or idx12 unreadable)")
        return 0

    missing = sorted(op for op in freq if op not in known)
    archives = iter_archives()
    print(f"scripts scanned: {len(archives)} unique opcodes: {len(freq)}")
    if missing:
        print(f"WARNING: {len(missing)} opcode ids not in meta table (first 20): {missing[:20]}")
        return 1
    print("all cache opcodes covered by cs2_opcode_meta_table")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
