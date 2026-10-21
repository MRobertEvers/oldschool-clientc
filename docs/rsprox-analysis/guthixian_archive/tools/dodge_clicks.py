"""The recorder's walk clicks against what the server did with them: dodge (a roll or
strafe seq with an exact move), walk/run, or nothing. For a `home-click` transcript
(MOVE_GAMECLICK filter on). Used on The Blood Moon Rises' Lowerniel Drakan fight (index
4262 = rsprox.net 4288), the model for the Archive's Ezekiel dodge.

    python3 dodge_clicks.py <click txt> <dodge seq regex> [--from T --to T] [--list]
"""
import sys, re, collections
path, seqre = sys.argv[1], re.compile(sys.argv[2])
args = sys.argv[3:]
t0 = int(args[args.index('--from') + 1]) if '--from' in args else 0
t1 = int(args[args.index('--to') + 1]) if '--to' in args else 10**9
local = None; tick = None; cur = False
clicks = {}; moves = {}; seqs = collections.defaultdict(list); xms = {}; anims = collections.defaultdict(list)
for l in open(path, errors='replace'):
    m = re.match(r'^local player index: (\d+)', l)
    if m: local = m[1]
    m = re.match(r'^\[(\d+)\]$', l)
    if m: tick = int(m[1]); cur = False; continue
    if tick is None or not (t0 <= tick <= t1): continue
    m = re.match(r'^ {4}\[(move_gameclick|move_minimapclick)\] coord=\((\d+), (\d+)', l)
    if m: clicks[tick] = (int(m[2]), int(m[3])); continue
    m = re.match(r'^ {4}\[map_anim\] id=(\w+)', l)
    if m: anims[tick].append(m[1])
    m = re.match(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=[^,]+, coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?', l)
    if m:
        cur = m[2] == local
        if cur:
            frm = (int(m[3]), int(m[4])); to = (int(m[5]), int(m[6])) if m[5] else frm
            moves[tick] = (m[1], frm, to)
        continue
    if re.match(r'^ {4,8}\[', l) and not l.startswith(' ' * 12): cur = False
    if cur:
        m = re.match(r'^ {12}\[sequence\] id=(\w+)', l)
        if m: seqs[tick].append(m[1])
        m = re.match(r'^ {12}\[exactmove\] (.*)', l)
        if m: xms[tick] = m[1]
def cheb(a, b): return max(abs(a[0] - b[0]), abs(a[1] - b[1]))
pos = None; tab = collections.Counter()
ticks = sorted(set(clicks) | set(moves))
last = {}
for t in ticks:
    if t in moves: last[t] = moves[t]
for t in sorted(clicks):
    before = [moves[k][2] for k in sorted(moves) if k < t]
    at = before[-1] if before else None
    if at is None: continue
    c = clicks[t]
    out = None
    for k in (t, t + 1):
        dodge = [s for s in seqs.get(k, []) if seqre.search(s)]
        if dodge:
            out = (k - t, dodge[0], moves[k][1], moves[k][2], xms.get(k)); break
        if k in moves and moves[k][0] in ('walk', 'run'):
            out = (k - t, moves[k][0], moves[k][1], moves[k][2], None); break
    d = cheb(at, c)
    kind = out[1] if out else 'none'
    moved = cheb(out[2], out[3]) if out else 0
    reached = out and out[3] == c
    tab[(kind if not seqre.search(kind) else 'DODGE ' + kind, f'click {d}', f'moved {moved}', 'to the click' if reached else 'short of it', f'+{out[0]}' if out else '')] += 1
    if '--list' in args: print(t, 'at', at, 'click', c, 'd', d, '->', out)
for k, v in sorted(tab.items(), key=lambda x: (x[0][0], x[0][1])): print(v, k)
