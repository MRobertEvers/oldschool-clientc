#!/usr/bin/env python3
"""yama_accuracy.py -- the recorder's Dark Demonbane accuracy on Yama, by phase and by Mystic
Might, from the outcome sound the server sends on the cast tick.

    yama_accuracy.py [idx4498 --bas transcript] [recorder name]

A cast = the recorder's player_info seq human_spellcast_demonbane (8977). Its outcome = the
area sound on that tick at Yama's 5x5: dark_demonbane_hit (5035) when the accuracy roll passed,
spellfail (227) on a splash. A splash shows no splat at all, which is why reading splats left
307 of the 827 casts unresolved (the version of this tool before 2026-11-10). Only ticks with
exactly one such sound on Yama are counted ("clean"); the validation line checks the model
against the recorder's own splats 2..4 ticks on (a damage splat after a hit sound, none after
a splash). Mystic Might is the recorder's varbit prayer_mysticmight at the cast.
"""
import collections
import os
import re
import sys

TRANSCRIPT = os.path.expanduser('~/.rsprox-index-cache/home-bas/.rsprox/binary/archive/idx4498.txt')
HEADER = re.compile(r'^\[(\d+)\]\s*$')
PLAYER = re.compile(r'player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)')
NPC = re.compile(r'npc=\(index=(\d+), id=([a-z_0-9]+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)')
SEQ = re.compile(r'\[sequence\] id=([a-z_0-9]+) \((\d+)\)')
SOUND = re.compile(r'\[sound_area\] id=(dark_demonbane_hit|spellfail) \(\d+\).*coord=\((\d+), (\d+), (\d+)\)')
HIT = re.compile(r'\[hit\] id=(\d+), value=(\d+)')
MM = re.compile(r'\[varbit\] id=prayer_mysticmight \(4127\), old=(\d+), new=(\d+)')


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else TRANSCRIPT
    me = sys.argv[2] if len(sys.argv) > 2 else 'Das Idle'
    tick = None
    block = None
    yama = {}
    casts = set()
    sounds = collections.defaultdict(list)
    splats = collections.defaultdict(list)
    phase, phase_at = 1, {}
    mm, mm_at = 0, {}
    for line in open(path, errors='replace'):
        m = HEADER.match(line)
        if m:
            tick = int(m.group(1))
            phase_at[tick] = phase
            mm_at[tick] = mm
            continue
        if tick is None:
            continue
        ind = len(line) - len(line.lstrip(' '))
        s = line.strip()
        v = MM.search(s)
        if v:
            mm = int(v.group(2))
            mm_at[tick] = mm
        sm = SOUND.search(s)
        if sm and ind == 4:
            sounds[tick].append(('hit' if sm.group(1) == 'dark_demonbane_hit' else 'splash',
                                 int(sm.group(2)), int(sm.group(3))))
        if ind == 8 and s.startswith('['):
            p, n = PLAYER.search(s), NPC.search(s)
            if p and (not n or s.find('player=') < s.find('npc=')):
                block = ('player', p.group(2))
            elif n:
                block = ('npc', n.group(2))
                if n.group(2) == 'yama':
                    yama[tick] = (int(n.group(4)), int(n.group(5)))
            else:
                block = None
            continue
        if block is None or ind != 12:
            continue
        q = SEQ.search(s)
        if q and block == ('player', me) and 'demonbane' in q.group(1):
            casts.add(tick)
        if block == ('npc', 'yama'):
            if q and q.group(2) == '12147':
                phase += 1
                phase_at[tick] = phase
            if q and q.group(2) == '12156':
                phase = 1
                phase_at[tick] = phase
            h = HIT.search(s)
            if h:
                splats[tick].append((int(h.group(1)), int(h.group(2))))

    def yama_tile(t):
        for back in range(200):
            if t - back in yama:
                return yama[t - back]
        return None

    tally = collections.Counter()
    check = collections.Counter()
    for t in sorted(casts):
        y = yama_tile(t)
        if not y:
            continue
        on = [k for k, x, z in sounds.get(t, []) if y[0] <= x <= y[0] + 4 and y[1] <= z <= y[1] + 4]
        if len(on) != 1:
            tally['not clean'] += 1
            continue
        tally[(phase_at.get(t, 0), mm_at.get(t, 0), on[0])] += 1
        own = [(i, v) for d in (2, 3, 4) for (i, v) in splats.get(t + d, []) if (i == 16 and v > 3) or i == 12]
        check[(on[0], 'damage' if any(i == 16 for i, _ in own) else ('zero' if own else 'none'))] += 1

    print('casts', len(casts), 'clean', len(casts) - tally['not clean'])
    for ph in (1, 2, 3):
        for mmv, name in ((1, 'Mystic Might'), (0, 'no prayer')):
            h, f = tally[(ph, mmv, 'hit')], tally[(ph, mmv, 'splash')]
            if h + f:
                print('phase %d %-12s hit %3d splash %3d accuracy %.3f' % (ph, name, h, f, h / (h + f)))
    for name, phases in (('phases 1-2', (1, 2)), ('phase 3', (3,))):
        h = sum(tally[(ph, 1, 'hit')] for ph in phases)
        f = sum(tally[(ph, 1, 'splash')] for ph in phases)
        print('%s, Mystic Might: %d of %d, %.3f' % (name, h, h + f, h / max(1, h + f)))
    print('validation (sound, own splat 2..4 ticks on):', dict(check))


if __name__ == '__main__':
    main()
