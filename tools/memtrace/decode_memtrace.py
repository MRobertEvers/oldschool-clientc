#!/usr/bin/env python3
"""
Decode torirs memtrace.bin → events.jsonl + meta.json

Usage:
  python3 decode_memtrace.py memtrace.bin -o out_dir
  python3 decode_memtrace.py memtrace.bin --exe path/to/sdl2
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple

MAGIC = b"TRMT"
FOOTER_MAGIC = b"TRMS"
VERSION = 1

PLATFORM_NAMES = {0: "linux", 1: "macos", 2: "wasm"}

KIND_NAMES = {
    1: "malloc",
    2: "calloc",
    3: "realloc",
    4: "free",
    5: "posix_memalign",
    6: "strdup",
}

HEADER_FMT = "<4s4I"
MOD_FMT = "<QQI"
RECORD_FMT = "<2BH8Q32Q"
FOOTER_FMT = "<4sI"

RECORD_SIZE = struct.calcsize(RECORD_FMT)


@dataclass
class ModEntry:
    base: int
    size: int
    path: str


def read_exact(f, n: int) -> bytes:
    data = f.read(n)
    if len(data) != n:
        raise EOFError("unexpected EOF")
    return data


def parse_header(f) -> Tuple[int, int, List[ModEntry]]:
    magic, version, platform, mod_count, wasm_stack_count = struct.unpack(
        HEADER_FMT, read_exact(f, struct.calcsize(HEADER_FMT))
    )
    if magic != MAGIC:
        raise ValueError(f"bad magic {magic!r}")
    if version != VERSION:
        raise ValueError(f"unsupported version {version}")

    mods: List[ModEntry] = []
    for _ in range(mod_count):
        base, size, path_len = struct.unpack(MOD_FMT, read_exact(f, struct.calcsize(MOD_FMT)))
        path = read_exact(f, path_len).decode("utf-8", errors="replace") if path_len else ""
        mods.append(ModEntry(base=base, size=size, path=path))

    return platform, wasm_stack_count, mods


def find_mod(addr: int, mods: List[ModEntry]) -> Optional[ModEntry]:
    best = None
    for m in mods:
        if m.base <= addr and (best is None or m.base > best.base):
            best = m
    return best


def symbolize_atos(addr: int, mod: ModEntry) -> str:
    try:
        out = subprocess.check_output(
            ["atos", "-o", mod.path, "-l", hex(mod.base), hex(addr)],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
        return out or hex(addr)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return hex(addr)


def symbolize_addr2line(addr: int, mod: ModEntry, exe: Optional[Path]) -> str:
  path = mod.path if mod.path and mod.path != "[main]" else (str(exe) if exe else mod.path)
  if not path or not os.path.exists(path):
      return hex(addr)
  try:
      out = subprocess.check_output(
          ["addr2line", "-f", "-C", "-e", path, hex(addr - mod.base)],
          stderr=subprocess.DEVNULL,
          text=True,
      ).strip().splitlines()
      if len(out) >= 2 and out[0] != "??":
          return f"{out[0]} in {out[1]}"
      return hex(addr)
  except (subprocess.CalledProcessError, FileNotFoundError):
      return hex(addr)


def symbolize_stack(
    addrs: List[int],
    mods: List[ModEntry],
    platform: int,
    exe: Optional[Path],
    cache: Dict[int, str],
) -> List[str]:
    frames: List[str] = []
    for addr in addrs:
        if addr == 0:
            continue
        if addr in cache:
            frames.append(cache[addr])
            continue
        mod = find_mod(addr, mods)
        if not mod:
            sym = hex(addr)
        elif platform == 1:
            sym = symbolize_atos(addr, mod)
        else:
            sym = symbolize_addr2line(addr, mod, exe)
        cache[addr] = sym
        frames.append(sym)
    return frames


def parse_footer(f, wasm_stacks: List[str]) -> None:
    pos = f.tell()
    rest = f.read()
    if len(rest) < struct.calcsize(FOOTER_FMT):
        f.seek(pos)
        return
    magic, count = struct.unpack(FOOTER_FMT, rest[: struct.calcsize(FOOTER_FMT)])
    if magic != FOOTER_MAGIC:
        f.seek(pos)
        return
    off = struct.calcsize(FOOTER_FMT)
    for _ in range(count):
        if off + 4 > len(rest):
            break
        (slen,) = struct.unpack("<I", rest[off : off + 4])
        off += 4
        text = rest[off : off + slen].decode("utf-8", errors="replace")
        off += slen
        wasm_stacks.append(text)


def decode(path: Path, out_dir: Path, exe: Optional[Path], max_events: Optional[int]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    wasm_stacks: List[str] = [""]  # 1-based ids

    with path.open("rb") as f:
        platform, _wasm_hint, mods = parse_header(f)
        header_end = f.tell()

        # Read events until footer or EOF
        f.seek(header_end)
        blob = f.read()
        footer_off = blob.rfind(FOOTER_MAGIC)
        if footer_off >= 0:
            events_blob = blob[:footer_off]
            footer_blob = blob[footer_off:]
            import io

            parse_footer(io.BytesIO(footer_blob), wasm_stacks)
        else:
            events_blob = blob

    sym_cache: Dict[int, str] = {}
    live: Dict[int, int] = {}
    peak_live = 0
    event_count = 0

    events_path = out_dir / "events.jsonl"
    with events_path.open("w", encoding="utf-8") as out:
        for off in range(0, len(events_blob) - RECORD_SIZE + 1, RECORD_SIZE):
            chunk = events_blob[off : off + RECORD_SIZE]
            if len(chunk) < RECORD_SIZE:
                break
            (
                kind,
                stack_count,
                _reserved,
                ts_ns,
                thread_id,
                req_size,
                usable_size,
                ptr,
                old_ptr,
                live_bytes,
                heap_total,
                *stack_raw,
            ) = struct.unpack(RECORD_FMT, chunk)

            kind_name = KIND_NAMES.get(kind, f"kind_{kind}")
            ptr_i = int(ptr)
            usable_i = int(usable_size)

            if platform == 2 and stack_count >= 1:
                stack_id = int(stack_raw[0])
                stack = (
                    wasm_stacks[stack_id].splitlines()
                    if 0 < stack_id < len(wasm_stacks)
                    else []
                )
            else:
                addrs = [int(stack_raw[i]) for i in range(stack_count)]
                stack = symbolize_stack(addrs, mods, platform, exe, sym_cache)

            if kind_name in ("malloc", "calloc", "posix_memalign", "strdup", "realloc"):
                if ptr_i:
                    live[ptr_i] = usable_i or int(req_size)
            elif kind_name == "free" and ptr_i:
                live.pop(ptr_i, None)

            peak_live = max(peak_live, int(live_bytes))

            rec = {
                "t": int(ts_ns),
                "thread": int(thread_id),
                "kind": kind_name,
                "req": int(req_size),
                "usable": usable_i,
                "ptr": hex(ptr_i) if ptr_i else None,
                "old_ptr": hex(int(old_ptr)) if old_ptr else None,
                "live": int(live_bytes),
                "heap": int(heap_total),
                "stack": stack,
            }
            out.write(json.dumps(rec, separators=(",", ":")) + "\n")
            event_count += 1
            if max_events and event_count >= max_events:
                break

    leaks = [
        {"ptr": hex(ptr), "bytes": size}
        for ptr, size in sorted(live.items(), key=lambda x: -x[1])[:256]
    ]

    meta = {
        "source": str(path),
        "platform": PLATFORM_NAMES.get(platform, str(platform)),
        "version": VERSION,
        "event_count": event_count,
        "peak_live_bytes": peak_live,
        "modules": [{"base": hex(m.base), "size": m.size, "path": m.path} for m in mods],
        "still_live_count": len(live),
        "still_live_bytes": sum(live.values()),
        "top_leaks": leaks[:64],
    }
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    print(f"wrote {events_path} ({event_count} events)")
    print(f"wrote {out_dir / 'meta.json'}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Decode torirs memtrace.bin")
    ap.add_argument("input", type=Path, help="memtrace.bin path")
    ap.add_argument("-o", "--out", type=Path, default=Path("memtrace_out"))
    ap.add_argument("--exe", type=Path, default=None, help="binary for addr2line (linux)")
    ap.add_argument("--max-events", type=int, default=None)
    args = ap.parse_args()
    if not args.input.is_file():
        print(f"not found: {args.input}", file=sys.stderr)
        return 1
    decode(args.input, args.out, args.exe, args.max_events)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
