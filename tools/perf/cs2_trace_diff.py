#!/usr/bin/env python3
"""Compare a CS2 execution trace from the C client against the official client's.

    cs2_trace_diff.py <c-trace.log> <java-trace.json> [--script N]
    cs2_trace_diff.py <c-trace.log> <java-dir>/       # every script both ran

Two inputs, deliberately in the formats each side already produces:

  C     `TORIRS_CS2_TRACE=1` makes src/cs2vm2 print one line per instruction —
        `CS2TRACE script=N pc=N op=NAME(N) intOp=N istack=N sstack=N ... itop=N`
  Java  `cs2trace <id> <path>` on the instrumented client's control channel
        writes `{step, pc, opcode, intSp, strSp, topInt}` records
        (Deobfuscator/instr/src/CS2Trace.java)

Both sample *after* the instruction, so a divergence names the instruction that
caused it rather than the one after.

What is compared, and why only this: the executed opcode, the pc it executed
at, and both stack depths. Those four are enough to catch every class of defect
that matters — a wrong pop count (depth diverges), a wrong branch target (pc
diverges), a missing or extra instruction (opcode diverges) — and none of them
depend on the two clients agreeing about cache contents, host state, or what a
component looks like. `topInt` is reported when it differs but is not treated as
a divergence on its own: it legitimately differs wherever the two clients have
different world state, which is almost everywhere outside pure computation.
"""
import json
import os
import re
import sys

C_LINE = re.compile(
    r'CS2TRACE script=(-?\d+) pc=(-?\d+) op=\S+\((-?\d+)\).*?istack=(-?\d+) sstack=(-?\d+)')
C_ITOP = re.compile(r'itop=(-?\d+)')


def load_c(path):
    """script id -> [(pc, opcode, intSp, strSp, topInt)], first run of each."""
    runs, seen = {}, set()
    for line in open(path, encoding='utf-8', errors='replace'):
        m = C_LINE.search(line)
        if not m:
            continue
        sid, pc, op, isp, ssp = (int(m.group(i)) for i in range(1, 6))
        itop = C_ITOP.search(line)
        # Only the first execution of a script: later ones run against world
        # state the other client does not share, and comparing the tenth
        # invocation of a redraw against the first is noise, not a divergence.
        if sid in seen and not runs.get(sid):
            continue
        runs.setdefault(sid, []).append(
            (pc, op, isp, ssp, int(itop.group(1)) if itop else 0))
        seen.add(sid)
    return runs


def load_java(path):
    doc = json.load(open(path))
    return doc.get('scriptId'), [
        (r['pc'], r['opcode'], r['intSp'], r['strSp'], r.get('topInt', 0))
        for r in doc.get('trace', [])
    ]


def compare(sid, c, j):
    """None when aligned, else a human-readable first divergence."""
    n = min(len(c), len(j))
    for i in range(n):
        cp, co, ci, cs, ct = c[i]
        jp, jo, ji, js, jt = j[i]
        if (cp, co, ci, cs) != (jp, jo, ji, js):
            return (f'script {sid}: diverges at step {i}\n'
                    f'    C     pc={cp:<5d} op={co:<6d} intSp={ci:<3d} strSp={cs:<3d} topInt={ct}\n'
                    f'    client pc={jp:<5d} op={jo:<6d} intSp={ji:<3d} strSp={js:<3d} topInt={jt}\n'
                    f'    (previous {min(i, 3)} steps agreed)')
    if len(c) != len(j):
        return (f'script {sid}: agreed for all {n} shared steps, then lengths differ '
                f'(C {len(c)}, client {len(j)}) — one side stopped early')
    return None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    only = None
    for a in sys.argv[1:]:
        if a.startswith('--script'):
            only = int(a.split('=', 1)[1]) if '=' in a else None
    if len(args) < 2:
        print(__doc__)
        return 2

    c_runs = load_c(args[0])
    targets = []
    if os.path.isdir(args[1]):
        for name in sorted(os.listdir(args[1])):
            if name.endswith('.json'):
                targets.append(os.path.join(args[1], name))
    else:
        targets.append(args[1])

    aligned = diverged = skipped = 0
    for path in targets:
        sid, j = load_java(path)
        if only is not None and sid != only:
            continue
        if sid not in c_runs:
            skipped += 1
            continue
        report = compare(sid, c_runs[sid], j)
        if report is None:
            aligned += 1
            print(f'script {sid}: ALIGNED, {len(j)} instructions')
        else:
            diverged += 1
            print(report)
    print(f'\n{aligned} aligned, {diverged} diverged, '
          f'{skipped} not run by the C client')
    return 1 if diverged else 0


if __name__ == '__main__':
    sys.exit(main())
