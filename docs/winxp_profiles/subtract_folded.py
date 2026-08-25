#!/usr/bin/env python3
"""Subtract a boot-phase profile from a full-run profile, stack by stack.

    subtract_folded.py FULL.folded BOOT.folded OUT.folded

Very Sleepy cannot start sampling late -- its CLI is /h /r /i /o /t /q, and /i
loads an existing profile rather than attaching to a pid -- and its saved trace
cannot be sliced afterwards either: Callstacks.txt is aggregated and sorted by
stack depth, so it carries a weight per stack and no timestamps at all. The
only way to get a steady-state-only profile is to capture the boot phase
separately and take it out.

That works because a folded stack is keyed by `module!function;...` rather than
by Very Sleepy's per-capture symNNN ids, so two captures of the same binary
share keys. It relies on the two runs seeing the same warm cache, which is why
run_steady_captures.py takes them back to back.

A stack whose boot weight exceeds its full weight clamps to zero rather than
going negative -- sampling noise, and for the pure-boot stacks (JS5 priming,
login) that is the right answer anyway.
"""

from __future__ import annotations

import collections
import sys
from pathlib import Path


def load(path: Path) -> dict[str, int]:
    weights: dict[str, int] = collections.defaultdict(int)
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        stack, count = line.rsplit(" ", 1)
        weights[stack] += int(count)
    return weights


def main() -> None:
    if len(sys.argv) != 4:
        print(__doc__)
        raise SystemExit(2)

    full = load(Path(sys.argv[1]))
    boot = load(Path(sys.argv[2]))
    out_path = Path(sys.argv[3])

    steady: dict[str, int] = {}
    clamped = 0
    removed_us = 0
    for stack, weight in full.items():
        taken = boot.get(stack, 0)
        left = weight - taken
        if left <= 0:
            clamped += 1
            removed_us += weight
            continue
        removed_us += taken
        steady[stack] = left

    boot_only = sum(w for s, w in boot.items() if s not in full)

    with out_path.open("w", encoding="utf-8", newline="\n") as out:
        for stack, weight in sorted(steady.items(), key=lambda kv: kv[1], reverse=True):
            out.write("%s %d\n" % (stack, weight))

    full_s = sum(full.values()) / 1e6
    boot_s = sum(boot.values()) / 1e6
    steady_s = sum(steady.values()) / 1e6
    print("full   %8.3fs  %6d stacks" % (full_s, len(full)))
    print("boot   %8.3fs  %6d stacks (%.3fs of it on stacks the full run never hit)"
          % (boot_s, len(boot), boot_only / 1e6))
    print("steady %8.3fs  %6d stacks  (%d stacks fully consumed, %.3fs removed)"
          % (steady_s, len(steady), clamped, removed_us / 1e6))
    print(out_path)


if __name__ == "__main__":
    main()
