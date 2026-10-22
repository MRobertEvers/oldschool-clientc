"""Who cast each freeze on Dreadnoth: at the cast tick C, every player with a casting
sequence (name has cast/spell/zaros), their weapon (last [equipment] righthand), and his
next step N - C. usage: dn_caster.py <all-players --bas txt>..."""
import re, sys
TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]+)')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+) \((\d+)\)')
SEQ = re.compile(r'^ {12}\[sequence\] id=(\w+)')
RH = re.compile(r'^ {20}- wearpos=righthand, id=(\w+)')
SPOT = re.compile(r'^ {12}\[spotanim\](?: slot=\d+,)? id=(\w+) \(\d+\)(?:, delay=(\d+))?')
for path in sys.argv[1:]:
    sess = re.search(r'idx(\d+)', path).group(1)
    tick = 0; who = None; boss = False; weapon = {}; casts = {}; frz = []; crawl = []
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m: tick = int(m.group(1)); who = None; boss = False; continue
        m = PL.match(line)
        if m: who = m.group(3); boss = False; continue
        m = NPC.match(line)
        if m:
            who = None; boss = m.group(3) == 'tfa_jelly_boss'
            if boss and m.group(1) == 'crawl': crawl.append(tick)
            continue
        if who:
            m = RH.match(line)
            if m: weapon[who] = m.group(1); continue
            m = SEQ.match(line)
            if m and re.search(r'cast|spell|zaros', m.group(1)): casts.setdefault(tick, []).append((who, m.group(1), weapon.get(who)))
        if boss:
            m = SPOT.match(line)
            if m and m.group(1) in ('ice_barrage_impact', 'vfx_scorching_bow_impact_01'): frz.append((tick, m.group(1)))
    for C, name in frz:
        after = [c for c in crawl if c > C]
        N = after[0] if after else None
        cs = [f"{w}:{s}:{wp}" for w, s, wp in casts.get(C, []) + casts.get(C - 1, [])]
        print(f"{sess} {name[:10]} C={C} N-C={N - C if N else '-'}  casters {cs}")
