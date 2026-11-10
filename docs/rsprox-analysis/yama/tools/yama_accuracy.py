#!/usr/bin/env python3
"""yama_accuracy.py -- the recorder's Dark Demonbane accuracy on Yama, split by who Yama's
face target is on the cast tick. Answers: does Yama's magic defence follow his primary
target's style (wiki: +60 when his target last attacked with magic, else -30)?

    yama_accuracy.py [idx4498 --bas transcript] [recorder name] [cast seq name]

A cast = the recorder's player_info seq human_spellcast_demonbane (8977); its result = the
first splat on Yama 1..5 ticks later of id 16 (own hit, value > 3: thralls hit for 1..3) or
id 12 (own zero). Face = Yama's latest [facing] player=... at the cast.
"""
import os
import re
import sys

TRANSCRIPT = os.path.expanduser('~/.rsprox-index-cache/home-bas/.rsprox/binary/archive/idx4498.txt')
HEADER = re.compile(r'^\[(\d+)\]\s*$')
HIT = re.compile(r'\[hit\] id=(\d+), value=(\d+)')
FACE = re.compile(r'\[facing\] player=\(index=(\d+), name=([^,)]*)')


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else TRANSCRIPT
    me = sys.argv[2] if len(sys.argv) > 2 else 'Das Idle'
    cast_seq = sys.argv[3] if len(sys.argv) > 3 else 'human_spellcast_demonbane'
    tick, who, blockname = None, None, None
    casts, hits = [], []
    facing, face_at, phase, phase_at = None, {}, 1, {}
    for line in open(path, errors='replace'):
        m = HEADER.match(line)
        if m:
            tick = int(m.group(1))
            continue
        if tick is None:
            continue
        s = line.strip()
        ind = len(line) - len(line.lstrip(' '))
        if ind == 8 and s.startswith('['):
            blockname = None
            if 'id=yama (14176)' in s:
                blockname = 'yama'
            elif 'name=' + me in s:
                blockname = 'me'
        if blockname == 'yama' and ind == 12:
            if 'npc_yama01_portal' in s or '(12147)' in s:
                phase += 1
            if 'npc_yama01_spawn' in s or '(12156)' in s:
                phase = 1
            f = FACE.search(s)
            if f:
                facing = f.group(2)
            h = HIT.search(s)
            if h:
                hits.append((tick, int(h.group(1)), int(h.group(2))))
        if blockname == 'me' and ind == 12 and cast_seq in s:
            casts.append(tick)
            face_at[tick] = facing
            phase_at[tick] = phase
    used = set()
    tally = {}
    for c in casts:
        res = None
        for i, (t, hid, val) in enumerate(hits):
            if t <= c or t > c + 5 or i in used:
                continue
            if hid == 12 or (hid == 16 and val > 3):
                res = (i, hid, val)
                break
        key = 'P%d ' % phase_at.get(c, 1) + ('face me' if face_at.get(c) == me else ('face other' if face_at.get(c) else 'face none'))
        row = tally.setdefault(key, {'n': 0, 'hit': 0, 'zero': 0, 'none': 0, 'sum': 0})
        row['n'] += 1
        if res is None:
            row['none'] += 1
        else:
            used.add(res[0])
            if res[1] == 12:
                row['zero'] += 1
            else:
                row['hit'] += 1
                row['sum'] += res[2]
    print('casts', len(casts), 'hits on yama', len(hits))
    for key, r in sorted(tally.items()):
        known = r['hit'] + r['zero']
        rate = r['hit'] / known if known else 0
        print(f"{key:12s} casts {r['n']:4d} hit {r['hit']:4d} zero {r['zero']:4d} "
              f"unmatched {r['none']:3d} accuracy {rate:.2f} mean hit {r['sum'] / max(1, r['hit']):.1f}")


if __name__ == '__main__':
    main()
