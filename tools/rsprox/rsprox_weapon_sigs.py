#!/usr/bin/env python3
"""What every raider's weapon sends, per sequence, from RSProx transcripts.

Reads `--all-players --bas` transcripts (`rsprox_index.py get <id> --all-players
--bas`). Each player's weapon is the last `wearpos=righthand` in their
appearance block (the block is read before the same block's sequence, so a
switch and the swing it makes are attributed to the new weapon). Every
sequence a player plays that is not a BAS one is an action; for each
(weapon, sequence) this prints what came on that tick at least a quarter of
the time: the player's spotanims, map anims within two tiles, projectiles
from their tile (with `end ~ a + b*d` and a table by d, the Chebyshev
distance to the shot's target coord -- an npc's centre; the exact rules are
fitted by the nearest tile in docs/rsprox-analysis/guthixian_archive/
raid_weapons.md), area sounds at their tile and, for the recorder alone (the
default transcript's player), synth sounds. The
Fractured Archive lobby is skipped.

    python3 tools/rsprox/rsprox_weapon_sigs.py ~/.rsprox-index-cache/home-all-bas/.rsprox/binary/archive/idx43*.txt

Our side of the same table: tools/rsprox/weapon_tells.py.
"""
import collections
import glob
import re
import sys

RE_TICK = re.compile(r'^\[(\d+)\]')
RE_PLAYER = re.compile(r'^        \[(\w+)\] player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
RE_WEAR = re.compile(r'wearpos=(righthand|lefthand), id=(\S+) \((\d+)\)')
RE_SEQ = re.compile(r'^            \[sequence\] id=(\S+) \((\d+)\)(?:, delay=(\d+))?')
RE_SPOT = re.compile(r'^            \[spotanim\] (.*)')
RE_PROJ = re.compile(r'^    \[(map_projanim_v\d|projanim_specific_v\d)\] id=(\S+) \((\d+)\), (.*)')
RE_SRC = re.compile(r'^        \[source\] coord=\((\d+), (\d+), (\d+)\)(.*)')
RE_TGT = re.compile(r'^        \[target\] (.*)')
RE_AREA = re.compile(r'^    \[sound_area\] (.*)')
RE_SYNTH = re.compile(r'^    \[synth_sound\] (.*)')
RE_EQBLOCK = re.compile(r'^                \[equipment\]')


def in_lobby(x, z):
    return 4096 <= x < 4160 and 5504 <= z < 5568


def main(paths, recorder_hint=None):
    sig = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
    sessions = collections.defaultdict(set)
    per = collections.defaultdict(collections.Counter)
    fits = collections.defaultdict(list)
    seqn = collections.Counter()
    for path in paths:
        sid = re.search(r'idx(\d+)', path).group(1)
        weapon = {}
        pos = {}
        tick = 0
        cur = None
        in_eq = False
        eq_rh = None
        actions = {}  # (tick, name) -> weapon
        actseq = {}
        pend_proj = None
        tick_events = collections.defaultdict(list)
        recorder = None
        try:
            for dl in open(path.replace('home-all-bas', 'home-default'), errors='replace'):
                dm = re.search(r'player=\(index=\d+, name=([^,]+), coord', dl)
                if dm:
                    recorder = dm.group(1)
                    break
        except OSError:
            pass
        raw = open(path, errors='replace').read().split('\n')
        # A player's [appearance] subtree first in its block: a weapon switch
        # and the swing it makes share a block, appearance printed last.
        lines = []
        i = 0
        while i < len(raw):
            line = raw[i]
            if RE_PLAYER.match(line):
                j = i + 1
                while j < len(raw) and raw[j].startswith('         '):
                    j += 1
                body = raw[i + 1:j]
                app, rest, k = [], [], 0
                while k < len(body):
                    if body[k].startswith('            [appearance]'):
                        app.append(body[k])
                        k += 1
                        while k < len(body) and body[k].startswith('             '):
                            app.append(body[k])
                            k += 1
                        continue
                    rest.append(body[k])
                    k += 1
                lines.append(line)
                lines.extend(app)
                lines.extend(rest)
                i = j
                continue
            lines.append(line)
            i += 1
        # recorder: the first [details] name in a player_info or a login name
        for line in lines:
            m = RE_TICK.match(line)
            if m:
                tick = int(m.group(1))
                continue
            m = RE_PLAYER.match(line)
            if m:
                cur = m.group(3)
                x, z = int(m.group(4)), int(m.group(5))
                if m.group(7):
                    x, z = int(m.group(7)), int(m.group(8))
                pos[cur] = (x, z)
                in_eq = False
                continue
            if line.startswith('        [') and not line.startswith('         '):
                cur = None if 'player=' not in line else cur
            if RE_EQBLOCK.match(line):
                in_eq = True
                eq_rh = None
                continue
            if in_eq:
                m = RE_WEAR.search(line)
                if m and cur:
                    if m.group(1) == 'righthand':
                        weapon[cur] = m.group(2)
                    continue
                if not line.startswith('                    - '):
                    in_eq = False
            m = RE_SEQ.match(line)
            if m and cur:
                name = m.group(1)
                if re.search(r'(walk|ready|_run|turn|idle)(_[fblr])?$|^human_(walk|run|ready|turn)', name):
                    continue
                p = pos.get(cur)
                if not p or in_lobby(*p):
                    continue
                w = weapon.get(cur, '?')
                actions[(tick, cur)] = w
                actseq[(tick, cur)] = f'{name}({m.group(2)}) d={m.group(3) or 0}'
                seqn[(w, actseq[(tick, cur)])] += 1
                sig[w]['seq'][f'{name}({m.group(2)}) d={m.group(3) or 0}'] += 1
                sessions[w].add(sid)
                continue
            m = RE_SPOT.match(line)
            if m and cur:
                tick_events[(tick, cur)].append(('spot', m.group(1)))
                continue
            m = RE_PROJ.match(line)
            if m:
                pend_proj = (m.group(2), m.group(3), m.group(4))
                continue
            m = RE_SRC.match(line)
            if m and pend_proj:
                sx, sz = int(m.group(1)), int(m.group(2))
                src_extra = m.group(4)
                pend_proj = pend_proj + (sx, sz, src_extra)
                continue
            m = RE_TGT.match(line)
            if m and pend_proj and len(pend_proj) == 6:
                name, num, params, sx, sz, extra = pend_proj
                tgt = 'npc' if 'npc=' in m.group(1) else ('player' if 'player=' in m.group(1) else 'tile')
                tm = re.match(r'coord=\((\d+), (\d+)', m.group(1))
                dist = max(abs(int(tm.group(1)) - sx), abs(int(tm.group(2)) - sz)) if tm else -1
                em = re.search(r'endtime=(\d+)', params)
                shape = re.sub(r'endtime=\d+, ', '', params)
                for who, p in pos.items():
                    if p == (sx, sz) and 'npc=' not in extra:
                        tick_events[(tick, who)].append(('proj', f'{name}({num}) {shape} ->{tgt}', dist, int(em.group(1)) if em else -1))
                pend_proj = None
                continue
            m = re.match(r'^    \[map_anim\] id=(\S+) \((\d+)\)(.*), coord=\((\d+), (\d+)', line)
            if m:
                mx, mz = int(m.group(4)), int(m.group(5))
                for who, p in pos.items():
                    if max(abs(p[0] - mx), abs(p[1] - mz)) <= 2:
                        tick_events[(tick, who)].append(('map', f'{m.group(1)}({m.group(2)}){m.group(3)} off={mx - p[0]},{mz - p[1]}'))
                continue
            m = RE_AREA.match(line)
            if m:
                cm = re.search(r'coord=\((\d+), (\d+)', m.group(1))
                if cm:
                    for who, p in pos.items():
                        if p == (int(cm.group(1)), int(cm.group(2))):
                            tick_events[(tick, who)].append(('area', re.sub(r', coord=\([^)]*\)', '', m.group(1))))
                continue
            m = RE_SYNTH.match(line)
            if m and recorder:
                tick_events[(tick, recorder)].append(('synth', m.group(1)))
        for (t, who), w in actions.items():
            sq = actseq.get((t, who))
            seen = set()
            for ev in tick_events.get((t, who), []):
                kind, text = ev[0], ev[1]
                if (kind, text) in seen:
                    continue
                seen.add((kind, text))
                per[(w, sq)][(kind, text)] += 1
                if kind == 'proj':
                    fits[(w, sq, text)].append((ev[2], ev[3]))
    for w in sorted(sig):
        total = sum(sig[w]['seq'].values())
        print(f'== {w}  actions={total}  sessions={" ".join(sorted(sessions[w]))}')
        for sq, n in sorted(((k[1], v) for k, v in seqn.items() if k[0] == w), key=lambda kv: -kv[1]):
            if n < 3 or re.search(r'block|eat\(|death|yama|ezekiel|resurrect|selfimbue|spikedback|drink|def\(|_def_|parry', sq):
                continue
            print(f'   {n:5}  {sq}')
            for (kind, text), c in per[(w, sq)].most_common():
                if c < max(2, n * 0.25):
                    continue
                extra = ''
                if kind == 'proj':
                    pts = [p for p in fits[(w, sq, text)] if p[0] >= 0 and p[1] >= 0]
                    if len(pts) >= 2:
                        import statistics
                        ds = [p[0] for p in pts]; es = [p[1] for p in pts]
                        if len(set(ds)) > 1:
                            b = statistics.covariance(ds, es) / statistics.variance(ds)
                            a = statistics.mean(es) - b * statistics.mean(ds)
                            byd = collections.defaultdict(collections.Counter)
                            for d_, e_ in pts: byd[d_][e_] += 1
                            tab = ' '.join(f'{d_}:{byd[d_].most_common(1)[0][0]}' for d_ in sorted(byd)[:8])
                            extra = f'  end~{a:.1f}+{b:.2f}*d  [{tab}]'
                        else:
                            extra = f'  end={collections.Counter(es).most_common(1)[0][0]} d={ds[0]}'
                print(f'          {kind:5} {c:5}  {text}{extra}')


if __name__ == '__main__':
    main(sys.argv[1:])
