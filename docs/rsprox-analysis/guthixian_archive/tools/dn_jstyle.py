"""Expected misses of Dreadnoth's melee per attack style against the raiders it hit
(clean sample: no jellies, not on blood, any prayer), from each raider's worn kit at the
swing (BAS transcript equipment, cache obj stab/slash/crushdefence) and an assumed
Defence of 99 (+ the boost below). Attack roll: (200 + 9) * (bonus + 64). P(hit) is
OSRS's: A > D ? 1 - (D+2)/(2(A+1)) : A/(2(D+1)). A splat of 0 is a miss or a 0 roll
(1/31)."""
import re, sys, collections
OBJ = '/Users/matthewevers/Documents/git_repos/3draster/OSRS-Content/osrs-content/configs/all.obj'
defs = {}; cur = None
for line in open(OBJ, 'rb'):
    line = line.decode('latin-1').strip()
    if line.startswith('['): cur = line[1:-1]; continue
    m = re.match(r'param=(stab|slash|crush)defence,(-?\d+)', line)
    if m and cur: defs.setdefault(cur, {})[m.group(1)] = int(m.group(2))
TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
SEQ = re.compile(r'^ {12}\[sequence\] id=(\w+)')
HIT = re.compile(r'^ {12}\[hit\] id=(\d+), value=(-?\d+)')
EQ = re.compile(r'^ {20}- wearpos=(\w+), id=(\w+)')
APP = re.compile(r'^ {12}\[appearance\]')
def boxd(sw, p):
    f = lambda v, lo: lo - v if v < lo else (v - (lo + 4) if v > lo + 4 else 0)
    return max(f(p[0], sw[0]), f(p[1], sw[1]))
def phit(A, D): return 1 - (D + 2) / (2 * (A + 1)) if A > D else A / (2 * (D + 1))
BONUS = int(sys.argv[1]); DEFLVL = int(sys.argv[2]); paths = sys.argv[3:]
exp = collections.Counter(); obs = 0; n = 0
for path in paths:
    tick = 0; pos = {}; start = {}; who = None; npc = None; kit = collections.defaultdict(dict); inapp = None
    hits = collections.defaultdict(list); jel = set(); pray = {}; jat = None; swings = []; blood = set(); kitat = {}
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m: tick = int(m.group(1)); start = dict(pos); who = None; npc = None; continue
        m = PL.match(line)
        if m:
            idx = int(m.group(2)); who = idx; npc = None
            c = (int(m.group(4)), int(m.group(5))); start.setdefault(idx, c)
            pos[idx] = (int(m.group(7)), int(m.group(8))) if m.group(7) else c; continue
        m = NPC.match(line)
        if m:
            who = None; npc = m.group(3)
            if npc == 'tfa_jelly_minion':
                if m.group(1) == 'del': jel.discard(m.group(2))
                else: jel.add(m.group(2))
            if npc == 'tfa_jelly_minion': jat = (int(m.group(8)), int(m.group(9))) if m.group(8) else (int(m.group(5)), int(m.group(6)))
            continue
        mb = re.match(r'^ {4}\[loc_add_change\w*\] id=tfa_jelly_boss_damage_tile \(\d+\), coord=\((\d+), (\d+)', line)
        if mb: blood.add((int(mb.group(1)), int(mb.group(2)))); continue
        if who is not None:
            if APP.match(line): kit[who] = {}; pray[who] = -1; continue
            ms = re.match(r'^ {16}\[status\] overheadicon=(\d+)', line)
            if ms: pray[who] = int(ms.group(1)); continue
            m = EQ.match(line)
            if m: kit[who][m.group(1)] = m.group(2); continue
            m = HIT.match(line)
            if m: hits[(tick, who)].append(int(m.group(2)))
            continue
        if who is None and npc == 'tfa_jelly_minion':
            m = SEQ.match(line)
            if m and m.group(1) == 'jelly_attack':
                swings.append((tick, jat, dict(start), {i: dict(k) for i, k in kit.items()}, set(blood), dict(pray)))
    for t, sw, st, kits, bl, pr in swings:
        for i, p in st.items():
            if abs(p[0] - sw[0]) + abs(p[1] - sw[1]) != 1 or p in bl or pr.get(i, -1) == 0: continue
            hs = hits.get((t, i), [])
            if not hs: continue
            n += 1; obs += hs[0] == 0
            for style in ('stab', 'slash', 'crush'):
                bonus = sum(defs.get(o, {}).get(style, 0) for o in kits.get(i, {}).values())
                D = (DEFLVL + 8) * (bonus + 64); A = (200 + 9) * (BONUS + 64)
                ph = phit(A, D)
                exp[style] += (1 - ph)
print(f"attack bonus {BONUS}, defence level {DEFLVL}: n={n} recorded zeros {obs}; expected " + ', '.join(f"{s} {v:.1f}" for s, v in exp.items()))
