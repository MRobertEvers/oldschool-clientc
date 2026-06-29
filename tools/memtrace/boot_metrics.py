#!/usr/bin/env python3
"""Print boot memtrace summary metrics from a WASM memtrace.bin file."""

from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import defaultdict
from pathlib import Path

MAGIC = b"TRMT"
FOOTER_MAGIC = b"TRMS"
VERSION = 1

KIND_NAMES = {
    1: "malloc",
    2: "calloc",
    3: "realloc",
    4: "free",
    5: "posix_memalign",
    6: "strdup",
}

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
        text = blob[off : off + slen].decode("utf-8", errors="replace")
        off += slen
        wasm_stacks.append(text)


def analyze(path: Path) -> dict:
    wasm_stacks: list[str] = [""]

    with path.open("rb") as f:
        magic, version, platform, _mod_count, _wasm_hint = struct.unpack(
            HEADER_FMT, read_exact(f, struct.calcsize(HEADER_FMT))
        )
        if magic != MAGIC:
            raise ValueError(f"bad magic {magic!r}")
        if version != VERSION:
            raise ValueError(f"unsupported version {version}")

        blob = f.read()
        footer_off = blob.rfind(FOOTER_MAGIC)
        if footer_off >= 0:
            events_blob = blob[:footer_off]
            parse_footer(blob[footer_off:], wasm_stacks)
        else:
            events_blob = blob

    live: dict[int, int] = {}
    ptr_site: dict[int, int] = {}
    sites: dict[int, dict[str, float]] = defaultdict(
        lambda: {"alloc_bytes": 0.0, "alloc_count": 0, "free_count": 0, "live_bytes": 0.0}
    )
    peak_live = 0
    event_count = 0

    for off in range(0, len(events_blob) - RECORD_SIZE + 1, RECORD_SIZE):
        chunk = events_blob[off : off + RECORD_SIZE]
        if len(chunk) < RECORD_SIZE:
            break
        (
            kind,
            stack_count,
            _reserved,
            _ts_ns,
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
                live[ptr_i] = size_i
                ptr_site[ptr_i] = stack_id
                st = sites[stack_id]
                st["alloc_bytes"] += size_i
                st["alloc_count"] += 1
                st["live_bytes"] += size_i
        elif kind_name == "free" and ptr_i:
            sid = ptr_site.pop(ptr_i, None)
            freed = live.pop(ptr_i, 0)
            if sid is not None:
                sites[sid]["free_count"] += 1
                sites[sid]["live_bytes"] -= freed

        peak_live = max(peak_live, int(live_bytes))
        event_count += 1

    stack_text: dict[int, str] = {}
    for sid, st in sites.items():
        if 0 < sid < len(wasm_stacks):
            stack_text[sid] = wasm_stacks[sid]

    def site_frames(match: str) -> list[tuple[int, float, int]]:
        out = []
        for sid, st in sites.items():
            frames = stack_text.get(sid, "")
            if match in frames:
                out.append((sid, st["live_bytes"], int(st["alloc_count"])))
        out.sort(key=lambda x: -x[1])
        return out

    return {
        "platform": platform,
        "event_count": event_count,
        "peak_live_bytes": peak_live,
        "still_live_count": len(live),
        "still_live_bytes": sum(live.values()),
        "sites": {
            "ToriDraw_ModelCopy": site_frames("ToriDraw_ModelCopy"),
            "ToriAuxLibTD_ModelNewFromCore": site_frames("ToriAuxLibTD_ModelNewFromCore"),
            "ToriAuxLibTD_ModelNewInstanceFromCore": site_frames(
                "ToriAuxLibTD_ModelNewInstanceFromCore"
            ),
            "decode_tile": site_frames("decode_tile"),
        },
    }


def fmt_bytes(n: float) -> str:
    if n >= 1e6:
        return f"{n / 1e6:.2f} MB"
    if n >= 1e3:
        return f"{n / 1e3:.1f} KB"
    return f"{int(n)} B"


def default_input() -> Path:
    return Path(__file__).resolve().parent / "bins" / "boot_memtrace.bin"


def main() -> int:
    ap = argparse.ArgumentParser(description="Summarize WASM memtrace.bin boot metrics")
    ap.add_argument("input", type=Path, nargs="?", default=None, help="memtrace.bin (default: bins/boot_memtrace.bin)")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    input_path = args.input if args.input is not None else default_input()
    if not input_path.is_file():
        print(f"not found: {input_path}", file=sys.stderr)
        return 1

    metrics = analyze(input_path)
    if args.json:
        print(json.dumps(metrics, indent=2))
        return 0

    print(f"file: {input_path}")
    print(f"events: {metrics['event_count']:,}")
    print(f"peak_live_bytes: {metrics['peak_live_bytes']:,} ({fmt_bytes(metrics['peak_live_bytes'])})")
    print(
        f"still_live: {metrics['still_live_count']:,} pointers, "
        f"{metrics['still_live_bytes']:,} bytes ({fmt_bytes(metrics['still_live_bytes'])})"
    )
    for label, rows in metrics["sites"].items():
        live_bytes = sum(r[1] for r in rows)
        alloc_count = sum(r[2] for r in rows)
        print(f"{label}: alloc_count={alloc_count:,}, live_bytes={int(live_bytes):,} ({fmt_bytes(live_bytes)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
