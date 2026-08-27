"""Compare two EIP arms in MILLISECONDS PER FRAME, not in share.

    python docs/d3d9/eipdiff.py <dir-of-fetched-dumps>

Reads every R*-off.eip.txt as the baseline arm and every R*-on.eip.txt as the
presorted one, resolves each against src/torirs.exe through
tools/bq/eipresolve.py, and averages within each arm.

Share is the wrong unit for an A/B whose whole point is that one arm's frame is
shorter. A symbol that costs exactly the same in both arms takes a LARGER share
of the faster arm, and would read as a regression. Every number below is
samples / sample_rate / frames, which is a duration and comparable across arms.

One caveat the symbol names cannot express, and it has to be stated rather than
worked around: the doors of a kernel family share one body, and the resolver
attributes a sample to the nearest preceding symbol. So every sample inside the
shared body lands on whichever door happens to sit last in address order --
which is why `..._presorted_run_asm` is the top symbol even in the arm where
the batcher is off and that door is never entered. The per-FAMILY totals are
right; the per-DOOR split is not readable here at all.
"""
import re
import subprocess
import sys
import collections

NM = r'toolchains\mingw32\bin\nm.exe'
EXE = r'src\torirs.exe'
FRAMES = 1600.0


def resolve(path, top=400):
    out = subprocess.run(
        [sys.executable, r'tools\bq\eipresolve.py', path, EXE,
         '--top', str(top)],
        capture_output=True, text=True).stdout
    hz = float(re.search(r'\((\d+) Hz\)', out).group(1))
    total = float(re.search(r'samples\s+: (\d+)', out).group(1))
    secs = float(re.search(r'over ([\d.]+) s', out).group(1))
    syms = {}
    inside = False
    for line in out.splitlines():
        if line.startswith('=== inside'):
            inside = True
            continue
        if line.startswith('=== outside'):
            inside = False
            continue
        m = re.match(r'\s+(\d+)\s+([\d.]+)%\s+(\S+)', line)
        if inside and m:
            syms[m.group(3)] = int(m.group(1))
    return {'hz': hz, 'total': total, 'secs': secs, 'syms': syms}


def mean(runs, key):
    return sum(r['syms'].get(key, 0) / r['hz'] / FRAMES * 1000.0
               for r in runs) / len(runs)


import glob
import os

E = sys.argv[1] if len(sys.argv) > 1 else '.'
off = [resolve(p) for p in sorted(glob.glob(os.path.join(E, 'R*-off.eip.txt')))]
on = [resolve(p) for p in sorted(glob.glob(os.path.join(E, 'R*-on.eip.txt')))]
if not off or not on:
    raise SystemExit('need R*-off.eip.txt and R*-on.eip.txt under ' + E)
print('arms: %d baseline, %d presorted' % (len(off), len(on)))

fo = sum(r['secs'] / FRAMES * 1000 for r in off) / 2
fn = sum(r['secs'] / FRAMES * 1000 for r in on) / 2
print('frame, wall clock over %d measured frames' % FRAMES)
print('  baseline  %7.3f ms   (%s)' % (fo, ', '.join('%.3f' % (r['secs']/FRAMES*1000) for r in off)))
print('  presorted %7.3f ms   (%s)' % (fn, ', '.join('%.3f' % (r['secs']/FRAMES*1000) for r in on)))
print('  delta     %+7.3f ms   (%+.2f%%)' % (fn - fo, 100 * (fn - fo) / fo))
print()

keys = set()
for r in off + on:
    keys |= set(r['syms'])
rows = []
for k in keys:
    a, b = mean(off, k), mean(on, k)
    rows.append((b - a, a, b, k))
rows.sort()

print('per symbol, ms/frame  (sampled at ~511 Hz; +-0.01 ms is one sample)')
print('%9s %9s %9s   %s' % ('baseline', 'presorted', 'delta', 'symbol'))
print('-' * 96)
for d, a, b, k in rows[:12]:
    print('%9.3f %9.3f %+9.3f   %s' % (a, b, d, k))
print('   ...')
for d, a, b, k in rows[-12:]:
    print('%9.3f %9.3f %+9.3f   %s' % (a, b, d, k))


# --- rolled up by role, which IS readable across the attribution caveat -----
GROUPS = [
    ('raster kernels (all families, all doors)',
     lambda k: ('_s4_' in k and '_asm' in k) or 'textri_' in k
               or 'texspan_' in k),
    ('per-face dispatch layer',
     lambda k: k in ('_ToriDraw_RasterModelFaceKernel',)
               or 'stock_branching' in k
               or 'TriangleFaceTexture' in k
               or 'bary_branching' in k
               or 'texshadeflat' in k or 'texshadeblend' in k
               or 'flat_screen' in k),
    ('the batcher itself',
     lambda k: 'raster_batch' in k or 'draw_faces_batched' in k),
    ('depth sort (carries the pre-sort store)',
     lambda k: 'ComputeProjectedFaceOrder' in k),
]

print()
print('rolled up by role, ms/frame')
print('%9s %9s %9s   %s' % ('baseline', 'presorted', 'delta', 'role'))
print('-' * 96)
tot_a = tot_b = 0.0
claimed = set()
for name, pred in GROUPS:
    ks = [k for k in keys if pred(k)]
    claimed |= set(ks)
    a = sum(mean(off, k) for k in ks)
    b = sum(mean(on, k) for k in ks)
    tot_a += a
    tot_b += b
    print('%9.3f %9.3f %+9.3f   %s' % (a, b, b - a, name))
rest = [k for k in keys if k not in claimed]
ra = sum(mean(off, k) for k in rest)
rb = sum(mean(on, k) for k in rest)
print('%9.3f %9.3f %+9.3f   %s' % (ra, rb, rb - ra, 'everything else'))
print('-' * 96)
print('%9.3f %9.3f %+9.3f   %s' % (tot_a + ra, tot_b + rb,
                                   (tot_b + rb) - (tot_a + ra),
                                   'total sampled in image'))
