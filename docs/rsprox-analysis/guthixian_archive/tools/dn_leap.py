"""Each enrage leap (teleport + jump02): his new 5x5 vs every raider's tile at the
start of the tick and the end of the previous ones; the faced target."""
import re, sys
TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
FACEP = re.compile(r'^ {12}\[facing\] player=\(index=(\d+), name=([^,]+)')
for path in sys.argv[1:]:
    sess = re.search(r'idx(\d+)', path).group(1)
    tick = 0; pos = {}; hist = {}; names = {}; target = None; inb = False
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m: tick = int(m.group(1)); hist[tick] = {}; inb = False; continue
        m = PL.match(line)
        if m:
            idx = int(m.group(2)); names[idx] = m.group(3); inb = False
            c = (int(m.group(4)), int(m.group(5))); n = (int(m.group(7)), int(m.group(8))) if m.group(7) else c
            hist[tick][idx] = c; pos[idx] = n; continue
        m = NPC.match(line)
        if m:
            inb = m.group(3) == 'tfa_jelly_boss'
            if inb and m.group(1) == 'teleport':
                to = (int(m.group(8)), int(m.group(9))); mid = (to[0] + 2, to[1] + 2)
                rows = []
                for idx, c in hist[tick].items():
                    if abs(c[0] - mid[0]) > 20: continue
                    off = (c[0] - mid[0], c[1] - mid[1])
                    prev = hist.get(tick - 1, {}).get(idx)
                    offp = (prev[0] - mid[0], prev[1] - mid[1]) if prev else None
                    rows.append(f"{names[idx][:10]}{'*' if idx == target else ''} {off}/{offp}")
                print(f"{sess} [{tick}] to sw {to}  " + '  '.join(rows))
            continue
        if inb:
            m = FACEP.match(line)
            if m: target = int(m.group(1))
