#!/usr/bin/env python3
"""
Aggregate a memtrace.bin into a per-call-site report.

decode_memtrace.py answers "what happened"; this answers "who is holding the
heap". It streams the record stream twice — once to find the peak-live event,
once to replay the live set up to it — and groups by allocation stack, so the
output is a ranked list of sites rather than millions of events.

  python3 tools/memtrace/summarize.py memtrace.bin --exe src/torirs

Sections:
  PEAK        live bytes per site at the moment the heap was largest
  STILL LIVE  live bytes per site at the end of the trace (leaks / caches)
  CHURN       total bytes ever allocated per site (reallocation pressure)

Symbolization runs once per distinct address, batched into a single atos call
(macOS) or addr2line call (Linux), so a trace with millions of events costs a
few seconds rather than a few hours.
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Optional, Tuple

MAGIC = b"TRMT"
FOOTER_MAGIC = b"TRMS"
HEADER_FMT = struct.Struct("<4s4I")
MOD_FMT = struct.Struct("<QQI")
RECORD = struct.Struct("<2BH8Q32Q")
RECORD_SIZE = RECORD.size

KIND_MALLOC, KIND_CALLOC, KIND_REALLOC, KIND_FREE = 1, 2, 3, 4
KIND_MEMALIGN, KIND_STRDUP = 5, 6
ALLOC_KINDS = {KIND_MALLOC, KIND_CALLOC, KIND_MEMALIGN, KIND_STRDUP}


def parse_header(blob: bytes):
    magic, version, platform, mod_count, _wasm = HEADER_FMT.unpack_from(blob, 0)
    if magic != MAGIC:
        raise SystemExit(f"bad magic {magic!r} — not a memtrace file")
    off = HEADER_FMT.size
    mods = []
    for _ in range(mod_count):
        base, size, path_len = MOD_FMT.unpack_from(blob, off)
        off += MOD_FMT.size
        path = blob[off : off + path_len].decode("utf-8", "replace")
        off += path_len
        mods.append((base, size, path))
    return platform, mods, off


def records(blob: bytes, start: int, end: int):
    """Yield (kind, stack_key, ptr, old_ptr, usable, req) for each record."""
    off = start
    while off + RECORD_SIZE <= end:
        f = RECORD.unpack_from(blob, off)
        off += RECORD_SIZE
        kind, stack_count = f[0], f[1]
        req, usable, ptr, old_ptr = f[5], f[6], f[7], f[8]
        yield kind, f[11 : 11 + stack_count], ptr, old_ptr, usable, req


def find_mod(addr: int, mods) -> Optional[Tuple[int, int, str]]:
    best = None
    for m in mods:
        if m[0] <= addr and (best is None or m[0] > best[0]):
            best = m
    return best


def symbolize(addrs: List[int], mods, platform: int, exe: Optional[Path]) -> Dict[int, str]:
    """One subprocess per module, every address for that module at once."""
    out: Dict[int, str] = {}
    by_mod: Dict[Tuple[int, str], List[int]] = defaultdict(list)
    for a in addrs:
        m = find_mod(a, mods)
        if m:
            by_mod[(m[0], m[2])].append(a)
        else:
            out[a] = hex(a)

    for (base, path), group in by_mod.items():
        if platform == 1:  # macOS
            cmd = ["atos", "-o", path, "-l", hex(base)] + [hex(a) for a in group]
        else:
            target = path if path and path != "[main]" else (str(exe) if exe else path)
            cmd = ["addr2line", "-f", "-C", "-e", target] + [hex(a - base) for a in group]
        try:
            res = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
            lines = [l for l in res.stdout.splitlines() if l.strip()]
        except (FileNotFoundError, subprocess.TimeoutExpired):
            lines = []
        if platform != 1:
            lines = [
                f"{lines[i]} at {lines[i + 1]}" for i in range(0, len(lines) - 1, 2)
            ]
        for a, sym in zip(group, lines):
            out[a] = sym
        for a in group:
            out.setdefault(a, hex(a))
    return out


def clean(sym: str) -> str:
    """Trim atos's '(in torirs) (file.c:123)' tail down to 'fn  file.c:123'."""
    sym = sym.replace(" (in torirs)", "")
    if sym.endswith(")") and " (" in sym:
        head, _, tail = sym.rpartition(" (")
        return f"{head}  {tail[:-1]}"
    return sym


def mib(n: int) -> str:
    return f"{n / (1024 * 1024):8.2f} MB"


def report(
    title: str,
    sites: Dict[Tuple[int, ...], int],
    counts: Dict[Tuple[int, ...], int],
    syms: Dict[int, str],
    total: int,
    top: int,
    depth: int,
) -> None:
    print()
    print("=" * 100)
    print(f"{title}   (total {mib(total)} over {len(sites)} sites)")
    print("=" * 100)
    ranked = sorted(sites.items(), key=lambda kv: -kv[1])[:top]
    for rank, (stack, size) in enumerate(ranked, 1):
        share = 100.0 * size / total if total else 0.0
        print(f"\n[{rank:2d}] {mib(size)}  {share:5.1f}%   blocks={counts.get(stack, 0)}")
        for frame in stack[:depth]:
            print(f"        {clean(syms.get(frame, hex(frame)))}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", type=Path)
    ap.add_argument("--exe", type=Path, default=None, help="binary for addr2line (linux)")
    ap.add_argument("--top", type=int, default=20, help="sites per section")
    ap.add_argument("--depth", type=int, default=8, help="frames printed per site")
    ap.add_argument(
        "--churn-min",
        type=int,
        default=1 << 20,
        help="ignore sites whose total allocation is under this many bytes",
    )
    args = ap.parse_args()

    blob = args.input.read_bytes()
    platform, mods, data_start = parse_header(blob)
    footer = blob.rfind(FOOTER_MAGIC)
    data_end = footer if footer > data_start else len(blob)
    count = (data_end - data_start) // RECORD_SIZE
    print(f"{args.input}: {count} records, platform={platform}, {len(mods)} modules")

    # Pass 1 — running live total, peak record index, and lifetime churn.
    live: Dict[int, int] = {}
    total = 0
    peak = 0
    peak_at = 0
    churn: Dict[Tuple[int, ...], int] = defaultdict(int)
    churn_blocks: Dict[Tuple[int, ...], int] = defaultdict(int)
    for i, (kind, stack, ptr, old_ptr, usable, req) in enumerate(
        records(blob, data_start, data_end)
    ):
        if kind == KIND_FREE:
            total -= live.pop(ptr, 0)
        elif kind == KIND_REALLOC:
            total -= live.pop(old_ptr, 0)
            if ptr:
                size = usable or req
                live[ptr] = size
                total += size
                churn[stack] += size
                churn_blocks[stack] += 1
        elif kind in ALLOC_KINDS and ptr:
            size = usable or req
            live[ptr] = size
            total += size
            churn[stack] += size
            churn_blocks[stack] += 1
        if total > peak:
            peak, peak_at = total, i

    end_total = total
    end_live = dict(live)

    # Pass 2 — replay to the peak record and keep the live set there.
    live = {}
    owner: Dict[int, Tuple[int, ...]] = {}
    peak_sites: Dict[Tuple[int, ...], int] = defaultdict(int)
    peak_blocks: Dict[Tuple[int, ...], int] = defaultdict(int)
    peak_owner: Dict[int, Tuple[int, ...]] = {}
    for i, (kind, stack, ptr, old_ptr, usable, req) in enumerate(
        records(blob, data_start, data_end)
    ):
        if kind == KIND_FREE:
            live.pop(ptr, None)
            owner.pop(ptr, None)
        elif kind == KIND_REALLOC:
            live.pop(old_ptr, None)
            owner.pop(old_ptr, None)
            if ptr:
                live[ptr] = usable or req
                owner[ptr] = stack
        elif kind in ALLOC_KINDS and ptr:
            live[ptr] = usable or req
            owner[ptr] = stack
        if i == peak_at:
            for p, size in live.items():
                st = owner.get(p, ())
                peak_sites[st] += size
                peak_blocks[st] += 1
            peak_owner = dict(owner)
            break

    # The end-of-trace live set needs owners too; replay the tail from the peak.
    end_sites: Dict[Tuple[int, ...], int] = defaultdict(int)
    end_blocks: Dict[Tuple[int, ...], int] = defaultdict(int)
    owner = peak_owner
    for i, (kind, stack, ptr, old_ptr, usable, req) in enumerate(
        records(blob, data_start, data_end)
    ):
        if i <= peak_at:
            continue
        if kind == KIND_FREE:
            owner.pop(ptr, None)
        elif kind == KIND_REALLOC:
            owner.pop(old_ptr, None)
            if ptr:
                owner[ptr] = stack
        elif kind in ALLOC_KINDS and ptr:
            owner[ptr] = stack
    for p, size in end_live.items():
        st = owner.get(p, ())
        end_sites[st] += size
        end_blocks[st] += 1

    churn = {k: v for k, v in churn.items() if v >= args.churn_min}

    wanted = set()
    for table in (peak_sites, end_sites, churn):
        for stack, _ in sorted(table.items(), key=lambda kv: -kv[1])[: args.top]:
            wanted.update(stack[: args.depth])
    syms = symbolize(sorted(wanted), mods, platform, args.exe)

    print(f"peak live {mib(peak)} at record {peak_at}; end live {mib(end_total)}")
    report("PEAK — live bytes by allocation site", peak_sites, peak_blocks, syms, peak, args.top, args.depth)
    report("STILL LIVE AT EXIT — by allocation site", end_sites, end_blocks, syms, end_total, args.top, args.depth)
    report(
        "CHURN — total bytes ever allocated by site",
        churn,
        churn_blocks,
        syms,
        sum(churn.values()),
        args.top,
        args.depth,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
