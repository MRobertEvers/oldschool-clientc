"""Resolve an EIP-sample dump against the binary that produced it.

    python tools/bq/eipresolve.py eipsample.txt torirs.exe [--top 40] [--ms 35.1]

The sampler writes offsets from the module base and nothing else -- no symbol
table, no allocation, nothing that could perturb the thread it is suspending.
Naming those offsets is this script's whole job.

Two things make the arithmetic trivial rather than fiddly. XP does not relocate
the main image, so the addresses `nm` prints are the addresses that ran. And
the dump records the module base it actually observed, so the assumption is
checkable instead of assumed: --ms aside, every number below is a count.

Samples outside the executable are bucketed by 64 KB page, which is exactly
enough to name the DLL and not enough to name a function inside it. They are
reported as a separate table, because "38% of the frame is inside gdi32" is a
different kind of finding from "38% is inside the gouraud kernel" and merging
the two tables would hide which one you are looking at.
"""

import argparse
import bisect
import os
import subprocess
import sys

DEFAULT_NM = os.environ.get(
    'BQ_NM',
    r'C:\Users\mrobe\Documents\git_repos\oldschool-clientc\toolchains'
    r'\mingw32\bin\nm.exe')


def parse_dump(path):
    head = {}
    bins = []          # (offset, count)
    others = []        # (page_addr, count)
    mods = []          # (base, size, name)
    with open(path, 'r', errors='replace') as f:
        for line in f:
            t = line.split()
            if not t:
                continue
            if t[0] == 'T' and len(t) == 3:
                bins.append((int(t[1], 16), int(t[2])))
            elif t[0] == 'M' and len(t) == 3:
                others.append((int(t[1], 16), int(t[2])))
            elif t[0] == 'MOD' and len(t) >= 4:
                mods.append((int(t[1], 16), int(t[2], 16), ' '.join(t[3:])))
            elif t[0] == 'MODERR':
                head['moderr'] = t[1]
            elif len(t) == 2 and t[0] not in ('T', 'M'):
                head[t[0]] = t[1]
    assert bins or others, 'no samples in %s -- did the run actually sample?' % path
    return head, bins, others, mods


def load_symbols(exe, nm):
    """Sorted (address, name), text symbols only."""
    out = subprocess.check_output([nm, '--numeric-sort', '--defined-only', exe])
    syms = []
    for line in out.decode('ascii', 'replace').splitlines():
        t = line.split()
        if len(t) < 3:
            continue
        try:
            addr = int(t[0], 16)
        except ValueError:
            continue
        # T/t = text. Everything else is data and cannot hold an EIP.
        if t[1] in ('T', 't'):
            syms.append((addr, t[2]))
    assert syms, 'nm found no text symbols in %s' % exe
    syms.sort()
    return syms


def resolve(bins, syms, base):
    """Attribute every bin to the last symbol at or before it."""
    addrs = [a for a, _ in syms]
    per = {}
    unattributed = 0
    for off, count in bins:
        va = base + off
        i = bisect.bisect_right(addrs, va) - 1
        if i < 0:
            unattributed += count
            continue
        per[syms[i][1]] = per.get(syms[i][1], 0) + count
    return per, unattributed


def module_of(page, mods):
    for b, size, name in mods:
        if b <= page < b + size:
            return os.path.basename(name)
    return '?'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump')
    ap.add_argument('exe')
    ap.add_argument('--nm', default=DEFAULT_NM)
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--ms', type=float, default=None,
                    help='measured ms/frame, to turn shares into milliseconds')
    a = ap.parse_args()

    head, bins, others, mods = parse_dump(a.dump)
    base = int(head.get('module_base', '0x400000'), 16)
    total = int(head.get('samples_total', 0))
    in_image = int(head.get('samples_in_image', 0))
    failed = int(head.get('suspend_failures', 0))
    secs = float(head.get('seconds', 0.0))
    assert total > 0, 'the dump reports zero samples'

    syms = load_symbols(a.exe, a.nm)
    per, unattributed = resolve(bins, syms, base)

    print('dump          : %s' % a.dump)
    print('binary        : %s' % a.exe)
    print('module base   : 0x%08x   (nm range 0x%08x-0x%08x)'
          % (base, syms[0][0], syms[-1][0]))
    if not (syms[0][0] <= base + 0x1000 <= syms[-1][0] + 0x10000):
        print('WARNING: the dump base and the symbol table do not overlap. '
              'Either this is not the binary that ran, or the image moved.')
    print('samples       : %d over %.1f s (%.0f Hz), %d in image (%.1f%%), '
          '%d suspend failures'
          % (total, secs, total / secs if secs else 0, in_image,
             100.0 * in_image / total, failed))
    if failed > total * 0.01:
        print('WARNING: %.1f%% of suspend attempts failed; the profile is '
              'missing that share of the thread.' % (100.0 * failed / total))
    scale = (a.ms / 100.0) if a.ms else None
    print('')

    def row(name, count):
        pct = 100.0 * count / total
        if scale:
            return '%9d  %6.2f%%  %7.3f ms  %s' % (count, pct, pct * scale,
                                                   name)
        return '%9d  %6.2f%%  %s' % (count, pct, name)

    hdr = '  samples   share' + ('   at %5.1fms  symbol' % a.ms if a.ms
                                 else '  symbol')
    print('=== inside the executable ===')
    print(hdr)
    print('-' * 78)
    for name, count in sorted(per.items(), key=lambda kv: -kv[1])[:a.top]:
        print(row(name, count))
    if unattributed:
        print(row('(below the first symbol)', unattributed))

    if others:
        bymod = {}
        for page, count in others:
            bymod[module_of(page, mods)] = bymod.get(module_of(page, mods),
                                                     0) + count
        print('')
        print('=== outside the executable (64 KB pages, DLL granularity) ===')
        print(hdr)
        print('-' * 78)
        for name, count in sorted(bymod.items(), key=lambda kv: -kv[1])[:20]:
            print(row(name, count))

    lost = int(head.get('other_pages_lost', 0))
    if lost:
        print('')
        print('%d samples fell outside the page table and were dropped.' % lost)
    return 0


if __name__ == '__main__':
    sys.exit(main())
