#!/usr/bin/env python3
"""yama_wirekinds.py -- packet KINDS of one fight, ours (TORIRS_SCRIPTRUN_WIRELOG) against a
recording window (RSProx transcript), as counts per 1000 ticks. Kinds one side never sends are
the finding; rates differ with fight length, party size and play, so they are only context.

    yama_wirekinds.py <wirelog.tsv> <ofrom> <oto> <transcript.txt> <rfrom> <rto>

Names are normalised to lower case without the _v<N> revision suffix (NPC_INFO_LARGE_V6 and
[npc_info_large_v6] are one kind); ours prints PLAYER_INFO / NPC_INFO without "large".
"""
import collections
import re
import sys

HEADER = re.compile(r'^\[(\d+)\]\s*$')
PACKET = re.compile(r'^    \[([a-z0-9_]+)\]')


def norm(name):
    n = name.lower()
    n = re.sub(r'_v\d+$', '', n)
    n = n.replace('_large', '').replace('_small', '')
    return n


def ours(path, t0, t1):
    c = collections.Counter()
    for line in open(path, errors='replace'):
        f = line.rstrip('\n').split('\t')
        if len(f) < 3:
            continue
        try:
            t = int(f[0])
        except ValueError:
            continue
        if t < t0 or t > t1:
            continue
        if f[1] == 'pkt':
            c[norm(f[2])] += 1
    return c


def rec(path, t0, t1):
    c = collections.Counter()
    tick = None
    for line in open(path, errors='replace'):
        m = HEADER.match(line)
        if m:
            tick = int(m.group(1))
            if tick > t1:
                break
            continue
        if tick is None or tick < t0:
            continue
        m = PACKET.match(line)
        if m:
            c[norm(m.group(1))] += 1
    return c


def main():
    w, ofrom, oto, tr, rfrom, rto = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4], int(sys.argv[5]), int(sys.argv[6])
    o = ours(w, ofrom, oto)
    r = rec(tr, rfrom, rto)
    on, rn = max(1, oto - ofrom), max(1, rto - rfrom)
    kinds = sorted(set(o) | set(r), key=lambda k: -(o[k] / on + r[k] / rn))
    print('%-32s %10s %10s  %s' % ('kind', 'ours/1k', 'rec/1k', 'note'))
    for k in kinds:
        note = ''
        if o[k] == 0:
            note = 'RECORDED ONLY'
        elif r[k] == 0:
            note = 'OURS ONLY'
        print('%-32s %10.1f %10.1f  %s' % (k, 1000.0 * o[k] / on, 1000.0 * r[k] / rn, note))


if __name__ == '__main__':
    main()
