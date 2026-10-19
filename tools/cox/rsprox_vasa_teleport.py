#!/usr/bin/env python3
"""Vasa Nistirio's teleport special, measured in INSTANCE tiles.

Reads RSProx transcripts made with TRANSLATE_INSTANCED_COORDS=false (and
PLAYER_INFO_LOCAL_PLAYER_ONLY=false, to see a party): the player and Vasa then
share one frame, so a landing measured against his footprint is right. The
default (translated) transcripts are refused: they map every tile back to the
template it was copied from, and in a turned room Vasa's printed tile is
another corner of his footprint and chunk offsets come out mirrored -- the
first readings of the teleport (docs/rsprox-analysis/cox/vasa.md RV-S2) were
that error.

For each special (S = vasa_stun_spawn) it prints, for S-1 .. S+13, every
raider's Chebyshev distance from his middle (SW + 2), the teleport, the stun
graphic, and hits. Making the transcripts: docs/rsprox-analysis/cox/README.md.

    python3 tools/cox/rsprox_vasa_teleport.py <raw>/archive/{3285,843,3282,820}.txt
"""
import argparse
import re

TICK = re.compile(r'^\[(\d+)\]')
PLAYER = re.compile(r'player=\(index=\d+, name=([^,]+), coord=\((\d+), (\d+), \d+\)\)'
                    r'(?:, newcoord=\((\d+), (\d+), \d+\))?')
VASA = re.compile(r'npc=\(index=\d+, id=raids_vasanistirio_(?:dormant|walking|healing) \(\d+\), '
                  r'coord=\((\d+), (\d+), \d+\)')
HIT = re.compile(r'\[hit\] id=(\d+), value=(\d+)')
INSTANCE_X = 6400


def read(path):
    tick, sw, cur = 0, None, None
    specials, by_tick = [], {}
    translated = True
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            continue
        m = VASA.search(line)
        if m:
            sw = (int(m.group(1)), int(m.group(2)))
            if sw[0] >= INSTANCE_X:
                translated = False
        if 'vasa_stun_spawn' in line:
            specials.append((tick, sw))
        m = PLAYER.search(line)
        if m:
            cur = m.group(1)
            moved = m.group(4) is not None
            at = (int(m.group(4)), int(m.group(5))) if moved else (int(m.group(2)), int(m.group(3)))
            row = by_tick.setdefault(tick, {}).setdefault(cur, {'at': at, 'ev': []})
            row['at'] = at
            if '[teleport]' in line:
                row['ev'].append('TELEPORT')
            continue
        if not line.startswith('        '):
            cur = None
            continue
        if cur and cur in by_tick.get(tick, {}):
            m = HIT.search(line)
            if m:
                by_tick[tick][cur]['ev'].append('hit%s:%s' % (m.group(1), m.group(2)))
            elif 'stunned_thieving' in line:
                by_tick[tick][cur]['ev'].append('STUN')
    return specials, by_tick, translated


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('transcripts', nargs='+')
    args = ap.parse_args()
    for path in args.transcripts:
        specials, by_tick, translated = read(path)
        print('==', path.split('/')[-1])
        if translated and specials:
            print('   refused: template-translated tiles (transcribe with TRANSLATE_INSTANCED_COORDS=false)')
            continue
        for s, sw in specials:
            mid = (sw[0] + 2, sw[1] + 2)
            print('   S=%d  vasa sw %s, middle %s' % (s, sw, mid))
            last = {}
            for t in range(s - 1, s + 14):
                for name, row in by_tick.get(t, {}).items():
                    last[name] = row['at']
                cells = []
                for name in sorted(last):
                    at = last[name]
                    m = max(abs(at[0] - mid[0]), abs(at[1] - mid[1]))
                    row = by_tick.get(t, {}).get(name)
                    cells.append('%-10s m%-2d %-22s' % (name[:10], m, ','.join(row['ev']) if row else ''))
                print('     S%+-3d ' % (t - s) + ' | '.join(cells))


if __name__ == '__main__':
    main()
