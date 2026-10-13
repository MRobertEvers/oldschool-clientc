#!/usr/bin/env python3
"""Replay oracle v1: the MOVEMENT channel.

For every seat and every tick of every tick log, the measured state at T and
the inputs logged at T (they act on T+1) go through a small server model; the
predicted tile at T+1 is compared with the measured PLAYER_TILE at T+1.

Results are grouped by RULE (which branch of the model predicted) and CONTEXT
(what else happened that tick). Rows whose outcome depends on something the
log does not carry are UNOBSERVABLE, not mismatches.

usage: oracle.py <runs dir or ticklog> [...]  [--jobs N] [--show RULE] [--max N]
"""
import sys, os, glob, collections, multiprocessing

RUN_MIN = 1          # run energy needed to take a run step (units of 1/100 %)


def cheb(ax, az, bx, bz):
    return max(abs(ax - bx), abs(az - bz))


def gap(x, z, fx, fz, n):
    dx = max(fx - x, x - (fx + n - 1), 0)
    dz = max(fz - z, z - (fz + n - 1), 0)
    return max(dx, dz)


def sgn(v):
    return (v > 0) - (v < 0)


def step_toward(x, z, tx, tz):
    """The naive server step: diagonal first while both axes differ."""
    return x + sgn(tx - x), z + sgn(tz - z)


# the client's flood, open ground (collision_map.c collision_flood): neighbour
# order W, E, S, N, SW, SE, NW, NE; the route back from the goal, a waypoint at
# each turn (collision_route_backtrace)
DIRS = [(-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1)]


def bfs_route(sx, sz, dx, dz, margin=12):
    if (sx, sz) == (dx, dz):
        return []
    x0, x1 = min(sx, dx) - margin, max(sx, dx) + margin
    z0, z1 = min(sz, dz) - margin, max(sz, dz) + margin
    back = {(sx, sz): None}
    q = collections.deque([(sx, sz)])
    while q:
        x, z = q.popleft()
        if (x, z) == (dx, dz):
            break
        for ox, oz in DIRS:
            n = (x + ox, z + oz)
            if x0 <= n[0] <= x1 and z0 <= n[1] <= z1 and n not in back:
                back[n] = (-ox, -oz)
                q.append(n)
    if (dx, dz) not in back:
        return [(dx, dz)]
    pts, x, z = [(dx, dz)], dx, dz
    d = back[(x, z)]
    while (x, z) != (sx, sz):
        nd = back[(x, z)]
        if nd != d:
            d = nd
            pts.append((x, z))
        x, z = x + nd[0], z + nd[1]
    pts.reverse()
    return pts


def walk_steps(x, z, way, n):
    """n server steps along a waypoint queue (consumed in place)."""
    for _ in range(n):
        while way and way[0] == (x, z):
            way.pop(0)
        if not way:
            break
        x, z = step_toward(x, z, way[0][0], way[0][1])
    while way and way[0] == (x, z):
        way.pop(0)
    return x, z


# sizes by npc type the log never moves (no NPC_TILE row to carry them)
SIZE_FALLBACK = {}
for t in range(10822, 10827): SIZE_FALLBACK[t] = 6      # Maiden's forms
for t in (10866, 10867, 10868): SIZE_FALLBACK[t] = 5    # Sotetseg
for t in range(10766, 10773): SIZE_FALLBACK[t] = 5      # Xarpus
SIZE_FALLBACK[10811] = 3                                # a Nylocas support
REACH = {12926: 5, 12899: 7}     # toxic blowpipe (rapid), toxic trident; melee otherwise


def unpack(c):
    c = int(c)
    return (c >> 14) & 0x3fff, c & 0x3fff, c >> 28


def load(path):
    T = collections.defaultdict(lambda: collections.defaultdict(list))
    for line in open(path, errors='replace'):
        f = line.rstrip('\n').split('\t')
        if len(f) < 4 or not f[1].isdigit():
            continue
        T[int(f[1])][f[2]].append(f)
    return T


def replay(path):
    T = load(path)
    ticks = sorted(T)
    out = collections.Counter()
    samples = collections.defaultdict(list)
    npcs = {}                         # slot -> (x, z, size)
    dest = {}                         # pid -> (x, z) or None
    way = {}                          # pid -> the server's waypoint queue
    tile = {}                         # pid -> (x, z)
    size_by_type = dict(SIZE_FALLBACK)
    for t in ticks:
        for f in T[t]['npc_tile']:
            size_by_type[int(f[7])] = int(f[8])
    slot_type = {}
    for i, t in enumerate(ticks[:-1]):
        rows = T[t]
        for f in rows['npc_spawn']:
            x, z, _ = unpack(f[5])
            slot_type[f[3]] = int(f[4])
            npcs[f[3]] = (x, z, size_by_type.get(int(f[4]), 1))
        for f in rows['npc_retype']:
            slot_type[f[3]] = int(f[5])
            if f[3] in npcs:
                x, z, _ = npcs[f[3]]
                npcs[f[3]] = (x, z, size_by_type.get(int(f[5]), 1))
        for f in rows['npc_tile']:
            npcs[f[3]] = (int(f[4]), int(f[5]), int(f[8]))
        for f in rows['npc_free']:
            npcs.pop(f[3], None)
        for f in rows['player_tile']:
            tile[f[3]] = (int(f[4]), int(f[5]))
        raider = {f[3]: f for f in rows['raider']}
        inputs = collections.defaultdict(list)
        for f in rows['input']:
            inputs[f[3]].append(f)
        consumes = {f[3] for f in rows['consume']}
        nxt = T[ticks[i + 1]]
        if ticks[i + 1] != t + 1:
            continue
        actual = {f[3]: (int(f[4]), int(f[5])) for f in nxt['player_tile']}
        stunned_gfx = {f[3] for f in nxt['player_spotanim']}
        raider_next = {f[3]: f for f in nxt['raider']}
        npcs_next = dict(npcs)
        for f in nxt['npc_tile']:
            npcs_next[f[3]] = (int(f[4]), int(f[5]), int(f[8]))
        for pid, (x, z) in tile.items():
            if pid not in actual or pid not in raider:
                continue
            r = raider[pid]
            lab = r[9].split()
            kv = dict(zip(lab[0::2], lab[1::2]))
            energy = int(kv.get('energy', '0'))
            run = kv.get('run', '0') == '1' and energy >= RUN_MIN
            # the interaction in force while T+1 moves: RAIDER(T+1)'s (rows
            # are written before the inputs handled between ticks)
            rn = raider_next.get(pid)
            kvn = dict(zip(rn[9].split()[0::2], rn[9].split()[1::2])) if rn else kv
            tgt = kvn.get('tgt', '-1')
            ctx = []
            moved_click = None
            dropped = False
            for f in inputs[pid]:
                if f[4] == '-1':
                    dx, dz, _ = unpack(f[5])
                    moved_click = (dx, dz)
                elif f[4] == '-4' and 'dropped' in f[9]:
                    dropped = True
                elif f[9].startswith('[opheld'):
                    ctx.append('held')
            if pid in consumes:
                ctx.append('consume')
            if dropped:
                ctx.append('dropped')
            if moved_click is not None:
                dest[pid] = moved_click
                way[pid] = bfs_route(x, z, moved_click[0], moved_click[1])
                ctx.append('click')
            ax, az = actual[pid]
            # THE MODEL
            weapon = int(r[7]) if r[7].lstrip('-').isdigit() else -1
            reach = REACH.get(weapon, 1)
            if tgt != '-1' and tgt in npcs and moved_click is None:
                rule = 'follow'
                nx, nz, n = npcs_next.get(tgt, npcs[tgt])
                g = gap(x, z, nx, nz, n)
                cardinal = not (max(nx - x, x - (nx + n - 1), 0) > 0 and max(nz - z, z - (nz + n - 1), 0) > 0)
                if (reach == 1 and g == 1 and cardinal) or (reach > 1 and 1 <= g <= reach):
                    px, pz = x, z
                    rule = 'follow:in-reach'
                else:
                    tx = min(max(x, nx - 1), nx + n)
                    tz = min(max(z, nz - 1), nz + n)
                    rule = 'follow:chase'
                    px, pz = walk_steps(x, z, bfs_route(x, z, tx, tz), 2 if run else 1)
            elif way.get(pid):
                rule = 'walk:run' if run else 'walk:walk'
                px, pz = walk_steps(x, z, way[pid], 2 if run else 1)
            else:
                rule = 'idle'
                px, pz = x, z
            if dest.get(pid) == (ax, az):
                dest[pid] = None
            ok = (px, pz) == (ax, az)
            if not ok and cheb(ax, az, x, z) > 2:
                verdict = 'unobservable:teleport'
            elif not ok and pid in stunned_gfx:
                verdict = 'unobservable:gfx'
            else:
                verdict = 'ok' if ok else 'MISS'
            key = (rule, '+'.join(sorted(set(ctx))) or '-', verdict)
            out[key] += 1
            if verdict == 'MISS' and len(samples[key]) < 5:
                samples[key].append('%s t%d p%s @%d,%d pred %d,%d got %d,%d dest %s tgt %s run %s e%d' % (
                    os.path.basename(os.path.dirname(path)), t, pid, x, z, px, pz, ax, az, dest.get(pid), tgt, run, energy))
        for pid, a in actual.items():
            tile[pid] = a
    return out, samples


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    jobs = 8
    show = None
    for a in sys.argv[1:]:
        if a.startswith('--jobs='):
            jobs = int(a.split('=')[1])
        if a.startswith('--show='):
            show = a.split('=', 1)[1]
    paths = []
    for a in args:
        if os.path.isdir(a):
            paths += sorted(glob.glob(os.path.join(a, 'r*', 'ticklog.tsv')))
        else:
            paths.append(a)
    total = collections.Counter()
    samples = collections.defaultdict(list)
    per_log_miss = collections.Counter()
    with multiprocessing.Pool(jobs) as pool:
        for path, (c, s) in zip(paths, pool.imap(replay, paths)):
            total.update(c)
            per_log_miss[os.path.basename(os.path.dirname(path))] = sum(v for k, v in c.items() if k[2] == 'MISS')
            for k, v in s.items():
                if len(samples[k]) < 5:
                    samples[k] += v[:5 - len(samples[k])]
    by_rule = collections.defaultdict(collections.Counter)
    for (rule, ctx, verdict), n in total.items():
        by_rule[(rule, ctx)][verdict] += n
    print('%-22s %-24s %9s %9s %s' % ('rule', 'context', 'ok', 'MISS', 'unobservable'))
    for (rule, ctx), c in sorted(by_rule.items(), key=lambda kv: -kv[1]['MISS']):
        unobs = sum(v for k, v in c.items() if k.startswith('unobservable'))
        print('%-22s %-24s %9d %9d %d' % (rule, ctx, c['ok'], c['MISS'], unobs))
    print('logs', len(paths), 'total MISS', sum(v for k, v in total.items() if k[2] == 'MISS'),
          'of', sum(total.values()))
    for (rule, ctx, verdict), lst in samples.items():
        if show and not rule.startswith(show):
            continue
        for x in lst[:3]:
            print('  sample', rule, ctx, x)


if __name__ == '__main__':
    main()
