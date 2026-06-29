#!/usr/bin/env python3
"""Export WASM memtrace.bin to SQLite (same schema as viewer Export SQLite)."""

from __future__ import annotations

import argparse
import sqlite3
import struct
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

MAGIC = b"TRMT"
FOOTER_MAGIC = b"TRMS"
VERSION = 1
KIND_NAMES = {1: "malloc", 2: "calloc", 3: "realloc", 4: "free", 5: "posix_memalign", 6: "strdup"}
HEADER_FMT = "<4s4I"
RECORD_FMT = "<2BH8Q32Q"
FOOTER_FMT = "<4sI"
RECORD_SIZE = struct.calcsize(RECORD_FMT)


def read_exact(f, n: int) -> bytes:
    data = f.read(n)
    if len(data) != n:
        raise EOFError("unexpected EOF")
    return data


def parse_footer(blob: bytes, wasm_stacks: list[str]) -> None:
    if len(blob) < struct.calcsize(FOOTER_FMT):
        return
    magic, count = struct.unpack(FOOTER_FMT, blob[: struct.calcsize(FOOTER_FMT)])
    if magic != FOOTER_MAGIC:
        return
    off = struct.calcsize(FOOTER_FMT)
    for _ in range(count):
        if off + 4 > len(blob):
            break
        (slen,) = struct.unpack("<I", blob[off : off + 4])
        off += 4
        wasm_stacks.append(blob[off : off + slen].decode("utf-8", errors="replace"))
        off += slen


def export_sqlite(src: Path, dst: Path) -> None:
    wasm_stacks: list[str] = [""]
    with src.open("rb") as f:
        magic, version, platform, _, _ = struct.unpack(
            HEADER_FMT, read_exact(f, struct.calcsize(HEADER_FMT))
        )
        if magic != MAGIC:
            raise ValueError(f"bad magic {magic!r}")

        blob = f.read()
        footer_off = blob.rfind(FOOTER_MAGIC)
        if footer_off >= 0:
            events_blob = blob[:footer_off]
            parse_footer(blob[footer_off:], wasm_stacks)
        else:
            events_blob = blob

    live: dict[int, tuple[int, int]] = {}
    ptr_site: dict[int, int] = {}
    sites: dict[int, dict[str, float]] = defaultdict(
        lambda: {"alloc_bytes": 0.0, "alloc_count": 0, "free_count": 0, "live_bytes": 0.0}
    )
    peak_live = 0
    events: list[tuple] = []

    for off in range(0, len(events_blob) - RECORD_SIZE + 1, RECORD_SIZE):
        chunk = events_blob[off : off + RECORD_SIZE]
        if len(chunk) < RECORD_SIZE:
            break
        (
            kind,
            stack_count,
            _reserved,
            ts_ns,
            _thread_id,
            req_size,
            usable_size,
            ptr,
            _old_ptr,
            live_bytes,
            _heap_total,
            *stack_raw,
        ) = struct.unpack(RECORD_FMT, chunk)

        kind_name = KIND_NAMES.get(kind, f"kind_{kind}")
        ptr_i = int(ptr)
        size_i = int(usable_size) or int(req_size)
        stack_id = int(stack_raw[0]) if platform == 2 and stack_count >= 1 else 0

        if kind_name in ("malloc", "calloc", "posix_memalign", "strdup", "realloc"):
            if ptr_i:
                live[ptr_i] = (size_i, stack_id)
                ptr_site[ptr_i] = stack_id
                st = sites[stack_id]
                st["alloc_bytes"] += size_i
                st["alloc_count"] += 1
                st["live_bytes"] += size_i
        elif kind_name == "free" and ptr_i:
            sid = ptr_site.pop(ptr_i, None)
            entry = live.pop(ptr_i, None)
            if sid is not None and entry is not None:
                sites[sid]["free_count"] += 1
                sites[sid]["live_bytes"] -= entry[0]

        peak_live = max(peak_live, int(live_bytes))
        events.append(
            (
                int(ts_ns),
                kind,
                kind_name,
                hex(ptr_i) if ptr_i else None,
                size_i,
                int(live_bytes),
                stack_id,
            )
        )

    still_live_bytes = sum(v[0] for v in live.values())
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists():
        dst.unlink()

    db = sqlite3.connect(dst)
    cur = db.cursor()
    cur.executescript(
        """
        CREATE TABLE meta(key TEXT PRIMARY KEY, value);
        CREATE TABLE stacks(sid INTEGER PRIMARY KEY, depth INTEGER, frames TEXT);
        CREATE TABLE frames(sid INTEGER, idx INTEGER, frame TEXT, PRIMARY KEY(sid, idx));
        CREATE TABLE events(
            seq INTEGER PRIMARY KEY, t_ns REAL, kind_id INTEGER, kind TEXT,
            ptr TEXT, size REAL, live_bytes REAL, stack_id INTEGER);
        CREATE TABLE sites(
            sid INTEGER PRIMARY KEY, alloc_bytes REAL, alloc_count INTEGER,
            free_count INTEGER, live_bytes REAL);
        CREATE TABLE live_pointers(ptr TEXT PRIMARY KEY, size REAL, stack_id INTEGER);
        """
    )

    meta = [
        ("platform", "wasm"),
        ("version", str(version)),
        ("event_count", str(len(events))),
        ("peak_live_bytes", str(peak_live)),
        ("still_live_count", str(len(live))),
        ("still_live_bytes", str(still_live_bytes)),
        ("exported_at", datetime.now(timezone.utc).isoformat()),
    ]
    cur.executemany("INSERT INTO meta(key, value) VALUES (?, ?)", meta)

    for sid, frames_text in enumerate(wasm_stacks):
        if sid == 0 or not frames_text:
            continue
        frames = frames_text.splitlines()
        cur.execute(
            "INSERT INTO stacks(sid, depth, frames) VALUES (?, ?, ?)",
            (sid, len(frames), frames_text),
        )
        for idx, frame in enumerate(frames):
            cur.execute(
                "INSERT INTO frames(sid, idx, frame) VALUES (?, ?, ?)",
                (sid, idx, frame),
            )

    for sid, st in sites.items():
        if sid == 0 and st["alloc_count"] == 0:
            continue
        cur.execute(
            "INSERT INTO sites(sid, alloc_bytes, alloc_count, free_count, live_bytes) VALUES (?, ?, ?, ?, ?)",
            (sid, st["alloc_bytes"], st["alloc_count"], st["free_count"], st["live_bytes"]),
        )

    cur.executemany(
        "INSERT INTO events(seq, t_ns, kind_id, kind, ptr, size, live_bytes, stack_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        ((i + 1, *ev) for i, ev in enumerate(events)),
    )

    cur.executemany(
        "INSERT INTO live_pointers(ptr, size, stack_id) VALUES (?, ?, ?)",
        ((hex(ptr), size, sid) for ptr, (size, sid) in live.items()),
    )

    db.commit()
    db.close()


def default_sqlite_out(input_path: Path) -> Path:
    return Path(__file__).resolve().parent / "bins" / f"{input_path.stem}.db"


def main() -> int:
    ap = argparse.ArgumentParser(description="Export memtrace.bin to SQLite")
    ap.add_argument("input", type=Path)
    ap.add_argument("-o", "--out", type=Path, default=None, help="output .db (default: bins/<input-stem>.db)")
    args = ap.parse_args()
    if not args.input.is_file():
        print(f"not found: {args.input}", file=sys.stderr)
        return 1
    out_path = args.out if args.out is not None else default_sqlite_out(args.input)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    export_sqlite(args.input, out_path)
    print(f"wrote {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
