"""Symbolise an EIP stack capture into folded stacks.

Addresses are absolute in the running image; addr2line wants them relative to
the link base. The capture's header carries the base it observed, and the PE
header carries the base it was linked at -- on Windows these normally agree
(0x400000) because the exe is not relocated, but the subtraction is done from
the recorded value rather than assumed.

Every address is resolved in ONE addr2line invocation. Spawning one process per
address would take longer than the run being profiled.

-1 on the return address: a return address points at the instruction AFTER the
call, which for a call in the last statement of a block can land in the next
line or even the next function. Subtracting one lands inside the call itself.
The leaf (frame 0) is a real EIP and is used as-is.
"""
import subprocess
import sys

STACKS = sys.argv[1]
BINARY = sys.argv[2]
ADDR2LINE = sys.argv[3]
OUT = sys.argv[4]

rows = []
base = 0x400000
with open(STACKS) as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        if line.startswith("#"):
            for tok in line.split():
                if tok.startswith("base="):
                    base = int(tok.split("=")[1], 16)
            continue
        rows.append([int(a, 16) for a in line.split()])

# Unique addresses, with the return-address adjustment already applied.
wanted = set()
for r in rows:
    for i, a in enumerate(r):
        wanted.add(a if i == 0 else a - 1)
wanted = sorted(wanted)
print("%d samples, %d unique addresses" % (len(rows), len(wanted)))

# Addresses go in on stdin, not argv: 20k of them blow past the Windows
# command-line limit long before addr2line sees them.
names = {}
stdin = "\n".join("0x%x" % (a - base + 0x400000) for a in wanted) + "\n"
proc = subprocess.run([ADDR2LINE, "-f", "-C", "-e", BINARY],
                      input=stdin, capture_output=True, text=True)
out = proc.stdout.splitlines()
# addr2line -f emits two lines per address: function, then file:line.
for j, a in enumerate(wanted):
    fn = out[2 * j].strip() if 2 * j < len(out) else "??"
    if fn == "??" or not fn:
        fn = None
    names[a] = fn

# addr2line only knows DWARF, and the hand-written .S kernels carry none -- which
# is exactly the code the profile is trying to name. Fall back to the nearest
# preceding symbol from the symbol table, which does have their labels.
NM = ADDR2LINE.replace("addr2line", "nm")
syms = []
try:
    nm = subprocess.run([NM, "--numeric-sort", "--defined-only", BINARY],
                        capture_output=True, text=True).stdout
    for line in nm.splitlines():
        p = line.split()
        if len(p) >= 3 and len(p[0]) >= 6:
            try:
                syms.append((int(p[0], 16), p[2].lstrip("_")))
            except ValueError:
                pass
    syms.sort()
except Exception:
    syms = []

import bisect
addrs = [s[0] for s in syms]
for a, fn in list(names.items()):
    if fn:
        continue
    i = bisect.bisect_right(addrs, a) - 1
    # Only trust it within a sane function span; beyond that it is a wrong name,
    # which is worse than an honest address.
    if i >= 0 and a - addrs[i] < 0x4000:
        names[a] = syms[i][1]
    else:
        names[a] = "[0x%08x]" % a

folded = {}
for r in rows:
    frames = []
    for i, a in enumerate(r):
        key = a if i == 0 else a - 1
        fn = names.get(key, "[0x%08x]" % key)
        # Collapse immediate recursion so a deep self-call does not dominate.
        if frames and frames[-1] == fn:
            continue
        frames.append(fn)
    stack = ";".join(reversed(frames))
    folded[stack] = folded.get(stack, 0) + 1

with open(OUT, "w", encoding="utf-8") as f:
    for k, v in sorted(folded.items(), key=lambda kv: -kv[1]):
        f.write("%s %d\n" % (k, v))
print("%d unique stacks -> %s" % (len(folded), OUT))
