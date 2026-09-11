"""Name the out-of-image samples in an EIP dump, against DLL export tables.

    python tools/bq/eipresolve_dll.py sample.eip.txt --dll ntdll.dll=ntdll.dll \
        [--dll msvcrt.dll=msvcrt.dll] [--top 30] [--ms 16.0]

WHY THIS EXISTS

The sampler bins samples outside the main image by address, and at the default
64 KB that names the DLL and nothing else. "24% of the frame is inside ntdll"
is where the question starts, not where it ends -- ntdll is the heap, the
critical sections, and the string routines all at once, and those are three
completely different findings.

Run the client with TORIDRAW_EIP_OTHER_SHIFT=6 and the dump keeps 64-byte
offsets instead. This maps each of those offsets to the nearest preceding
EXPORT of the DLL it landed in, which is as close to a function name as a
stripped system binary allows.

WHAT THE NAMES MEAN, AND DO NOT MEAN

An export table is not a symbol table. It lists the functions the DLL chose to
publish, so a sample inside a private helper is attributed to whichever export
happens to sit below it. The name is therefore a REGION, not a proof: read
"RtlAllocateHeap" as "at or after RtlAllocateHeap and before the next export".
Where that matters the distance from the export is printed, so a hit 4 KB past
its name can be treated with the suspicion it deserves.
"""

import argparse
import bisect
import struct
import sys


def read_exports(path):
    """Every exported (address_rva, name) in a PE, sorted by address."""
    with open(path, 'rb') as f:
        blob = f.read()

    if blob[:2] != b'MZ':
        raise SystemExit('%s: not a PE file' % path)
    pe_off = struct.unpack_from('<I', blob, 0x3C)[0]
    if blob[pe_off:pe_off + 4] != b'PE\0\0':
        raise SystemExit('%s: bad PE signature' % path)

    coff = pe_off + 4
    n_sections = struct.unpack_from('<H', blob, coff + 2)[0]
    opt_size = struct.unpack_from('<H', blob, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from('<H', blob, opt)[0]
    # The export directory is data directory 0; it sits after the fixed part of
    # the optional header, whose length differs between PE32 and PE32+.
    dd = opt + (96 if magic == 0x10B else 112)
    exp_rva, exp_size = struct.unpack_from('<II', blob, dd)
    if exp_rva == 0:
        return []

    sections = []
    sec = opt + opt_size
    for i in range(n_sections):
        base = sec + i * 40
        vaddr, vsize = struct.unpack_from('<II', blob, base + 12)[0], 0
        vsize = struct.unpack_from('<I', blob, base + 8)[0]
        raw_size, raw_ptr = struct.unpack_from('<II', blob, base + 16)
        sections.append((vaddr, max(vsize, raw_size), raw_ptr))

    def rva_to_off(rva):
        for vaddr, vsize, raw_ptr in sections:
            if vaddr <= rva < vaddr + vsize:
                return raw_ptr + (rva - vaddr)
        return None

    eo = rva_to_off(exp_rva)
    if eo is None:
        return []
    n_names = struct.unpack_from('<I', blob, eo + 24)[0]
    funcs_rva = struct.unpack_from('<I', blob, eo + 28)[0]
    names_rva = struct.unpack_from('<I', blob, eo + 32)[0]
    ords_rva = struct.unpack_from('<I', blob, eo + 36)[0]

    fo, no, oo = rva_to_off(funcs_rva), rva_to_off(names_rva), rva_to_off(ords_rva)
    if None in (fo, no, oo):
        return []

    out = []
    for i in range(n_names):
        name_rva = struct.unpack_from('<I', blob, no + i * 4)[0]
        idx = struct.unpack_from('<H', blob, oo + i * 2)[0]
        addr = struct.unpack_from('<I', blob, fo + idx * 4)[0]
        # A forwarder points back into the export section; it is not code.
        if exp_rva <= addr < exp_rva + exp_size:
            continue
        s = rva_to_off(name_rva)
        if s is None:
            continue
        end = blob.index(b'\0', s)
        out.append((addr, blob[s:end].decode('ascii', 'replace')))

    out.sort()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump')
    ap.add_argument('--dll', action='append', default=[],
                    help='MODULENAME=path/to/that.dll, repeatable')
    ap.add_argument('--top', type=int, default=30)
    ap.add_argument('--ms', type=float, default=0.0,
                    help='frame time, to turn shares into ms')
    args = ap.parse_args()

    tables = {}
    for spec in args.dll:
        name, _, path = spec.partition('=')
        tables[name.lower()] = (read_exports(path), [a for a, _ in read_exports(path)])

    mods = []        # (base, size, name)
    others = []      # (addr, count)
    total = 0
    other_bin = 65536
    with open(args.dump, encoding='utf-8', errors='replace') as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            if p[0] == 'MOD' and len(p) >= 4:
                mods.append((int(p[1], 16), int(p[2], 16), p[3]))
            elif p[0] == 'M' and len(p) >= 3:
                others.append((int(p[1], 16), int(p[2])))
            elif p[0] == 'samples_total':
                total = int(p[1])
            elif p[0] == 'other_bin_bytes':
                other_bin = int(p[1])

    mods.sort()
    starts = [m[0] for m in mods]

    def module_of(addr):
        i = bisect.bisect_right(starts, addr) - 1
        if i < 0:
            return None
        base, size, name = mods[i]
        return mods[i] if base <= addr < base + size else None

    rows = {}
    unnamed = {}
    for addr, count in others:
        m = module_of(addr)
        if not m:
            unnamed['(no module)'] = unnamed.get('(no module)', 0) + count
            continue
        base, _, name = m
        key = name.lower()
        if key not in tables:
            unnamed[name] = unnamed.get(name, 0) + count
            continue
        exports, addrs = tables[key]
        rva = addr - base
        i = bisect.bisect_right(addrs, rva) - 1
        if i < 0:
            label, delta = '%s+0x%x' % (name, rva), 0
        else:
            label = '%s!%s' % (name, exports[i][1])
            delta = rva - exports[i][0]
        row = rows.setdefault(label, [0, delta, delta])
        row[0] += count
        row[1] = min(row[1], delta)
        row[2] = max(row[2], delta)

    print('dump            : %s' % args.dump)
    print('samples total   : %d' % total)
    print('other bin bytes : %d%s' % (
        other_bin,
        '' if other_bin <= 64 else '   <-- too coarse to name functions;'
                                   ' rerun with TORIDRAW_EIP_OTHER_SHIFT=6'))
    print()
    print('=== outside the executable, by nearest export ===')
    hdr = '  samples   share' + ('   at %5.1fms' % args.ms if args.ms else '') + '  +off range  symbol'
    print(hdr)
    print('-' * len(hdr))
    for label, (count, dmin, dmax) in sorted(rows.items(), key=lambda kv: -kv[1][0])[:args.top]:
        share = (100.0 * count / total) if total else 0.0
        ms = ('   %8.3f ms' % (args.ms * count / total)) if args.ms and total else ''
        print('  %7d  %6.2f%%%s  %5d-%-5d  %s' % (count, share, ms, dmin, dmax, label))

    if unnamed:
        print()
        print('=== modules with no export table supplied ===')
        for name, count in sorted(unnamed.items(), key=lambda kv: -kv[1]):
            share = (100.0 * count / total) if total else 0.0
            print('  %7d  %6.2f%%  %s' % (count, share, name))


if __name__ == '__main__':
    main()
