#!/usr/bin/env python3
"""Merge the per-chunk CS2 opcode telemetry from tools/perf/cs2_sweep.sh.

Each chunk file is a *cumulative* snapshot of that client's counters, so a
client that survived several chunks contributes the same observations more than
once. Counts are therefore taken as the maximum across the chunks of one client
run rather than summed — but a relaunched client starts from zero, so the
maximum has to be taken per contiguous run and those summed. Detecting a
relaunch is easy: the count for a given opcode drops.

The deltas merge as a union: min of the minima, max of the maxima. That is the
useful part — an opcode whose observed delta is not constant across the whole
corpus does not have a fixed signature at all, and this is what says so.

    cs2_merge.py <dir> <out.csv>
"""
import csv
import os
import sys


def main():
    directory, out_path = sys.argv[1], sys.argv[2]
    files = sorted(f for f in os.listdir(directory) if f.startswith('chunk_'))
    if not files:
        print('no chunk files', file=sys.stderr)
        return 1

    total = {}     # opcode -> count carried from completed runs
    run = {}       # opcode -> count within the current run
    stats = {}     # opcode -> [imin, imax, smin, smax, lmin, lmax, unhandled]

    prev_sum = -1
    for name in files:
        rows = list(csv.DictReader(open(os.path.join(directory, name))))
        this_sum = sum(int(r['count']) for r in rows)
        if this_sum < prev_sum:
            # The client restarted: bank what the previous one saw.
            for op, n in run.items():
                total[op] = total.get(op, 0) + n
            run = {}
        prev_sum = this_sum

        for r in rows:
            op = int(r['opcode'])
            run[op] = int(r['count'])
            vals = [int(r['int_min']), int(r['int_max']),
                    int(r['str_min']), int(r['str_max']),
                    int(r['long_min']), int(r['long_max'])]
            unh = int(r['unhandled'])
            if op not in stats:
                stats[op] = vals + [unh]
            else:
                s = stats[op]
                if int(r['count']) > 0:
                    s[0] = min(s[0], vals[0]); s[1] = max(s[1], vals[1])
                    s[2] = min(s[2], vals[2]); s[3] = max(s[3], vals[3])
                    s[4] = min(s[4], vals[4]); s[5] = max(s[5], vals[5])
                s[6] = max(s[6], unh)

    for op, n in run.items():
        total[op] = total.get(op, 0) + n

    with open(out_path, 'w') as fp:
        fp.write('opcode,count,int_min,int_max,str_min,str_max,long_min,long_max,unhandled\n')
        for op in sorted(stats):
            s = stats[op]
            fp.write(f'{op},{total.get(op, 0)},{s[0]},{s[1]},{s[2]},{s[3]},{s[4]},{s[5]},{s[6]}\n')
    print(f'merged {len(files)} chunks, {len(stats)} opcodes -> {out_path}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
