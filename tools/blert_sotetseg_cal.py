#!/usr/bin/env python3
"""SOTETSEG, Blert against ours: what the content decides, as distributions over
every room, each with a chi-square test.

  tools/blert_sotetseg_cal.py build/blert/sotetseg <our runs dir> [<our runs dir> ...]

Blert: Normal trio rooms (tools/blert_fetch_tob_rooms.py, stage 14); room tick 0
is the start. Ours: the leader's ticklog.tsv of every seed under a runs dir
(test/raids/tob_sotetseg.lua); room tick 0 is the tick before his first HUD
bar row (the bar's row trails `~tob_start_room` by a tick: the barrier's input
at t71, the first bar row at t72, his first attack at t77 = start + 6, which is
the content's `^tob_sote_first_attack_ticks`).

  first attack      the room tick of his first attack (Blert: the rooms with no
                    barrage on him before it - a barrage made it 8 in half)
  attack gap        ticks between two attacks with no maze between
  after a death     the gap that follows a death ball
  balls a death     balls between two death balls (or the start and the first)
  melee | adjacent  melee against ball when the target ended the tick before
                    within 1 of his 5x5 (the content's 1 in 2)
  maze proc         room tick and hp permille at each proc
  maze path         the mirror trail's start column, its row-to-row column
                    steps, its length in tiles
  maze length       proc to his first attack after it (player-driven: reported)

A test passes at p >= 0.01 OR a total-variation distance under 0.05.
"""
import collections
import glob
import json
import os
import sys

from scipy.stats import chi2_contingency

BOSS, MAZE_BOSS = 8388, 8387
SW = (13, 38)
SEQ_MELEE, SEQ_BALL = 8138, 8139
SCEPTRE_BARRAGE = 37   # Blert's attack type for a barrage from an ancient sceptre
DEATH_PROJ = 1604
LIT = 33035
GRID = (9, 22)


def path_stats(tiles):
    """tiles: the mirror trail (grid x, y) in time order -> start column, the
    columns of the even rows in order, the distinct tile count."""
    seen, order = set(), []
    for t in tiles:
        if t not in seen:
            seen.add(t)
            order.append(t)
    evens = {}
    for x, y in order:
        if y % 2 == 0 and y not in evens:
            evens[y] = x
    cols = [evens[y] for y in sorted(evens)]
    start = order[0][0] if order else None
    return start, cols, len(order)


def blert(d):
    rooms = []
    for f in sorted(glob.glob(d + "/*-*.json")):
        ev = sorted(json.load(open(f)), key=lambda e: e["tick"])
        attacks, procs, mazes, hp, pos = [], [], collections.defaultdict(list), {}, collections.defaultdict(dict)
        base, barraged = None, False
        glows, rec_hp = collections.defaultdict(list), {}
        for e in ev:
            t = e["tick"]
            if e["type"] == 8 and (e.get("npc") or {}).get("id") in (BOSS, MAZE_BOSS):
                h = e["npc"]["hitpoints"]
                hp[t] = (h >> 16, h & 0xFFFF)
                if base is None:
                    base = (e["xCoord"] - SW[0], e["yCoord"] - SW[1])
            if e["type"] == 4:
                pos[t][e["player"]["name"]] = (e["xCoord"], e["yCoord"])
                # the recording player's own hitpoints (source 0), cur<<16|max
                if e["player"].get("source") == 0 and "hitpoints" in e["player"]:
                    rec_hp[t] = (e["player"]["name"], e["player"]["hitpoints"] >> 16)
            if e["type"] == 5 and (e.get("attack") or {}).get("type") == SCEPTRE_BARRAGE and not attacks:
                barraged = True
            if e["type"] == 10:
                code = e["npcAttack"]["attack"]
                kind = {7: "melee", 8: "ball", 9: "death"}.get(code)
                if kind:
                    attacks.append((t, kind, e["npcAttack"].get("target")))
            if e["type"] == 130:
                procs.append(t)
            if e["type"] == 131:
                for at in e["soteMaze"].get("activeTiles") or []:
                    mazes[e["soteMaze"]["maze"]].append((at["x"], at["y"]))
                    glows[e["soteMaze"]["maze"]].append((t, at["x"], at["y"]))
        # a recorder that joined a tick late starts its whole stream a tick
        # late (its first player row on 1, his on 2): 7 of the 12 unbarraged
        # rooms "opening on 7" are that, and open on 6 counted from its first row
        late = min(pos) if pos else 0
        rooms.append(dict(attacks=attacks, procs=procs, mazes=[mazes[k] for k in sorted(mazes)], hp=hp,
                          pos=pos, base=base, barraged=barraged, late=late,
                          glows={k: glows[k] for k in glows}, up_hp=rec_hp))
    return rooms


def ours(dirs):
    rooms = []
    for d in dirs:
        for sess in sorted(glob.glob(d + "/*/")):
            path = sess + "ticklog.tsv"
            if not os.path.exists(path):
                continue
            rows = [l.rstrip("\n").split("\t") for l in open(path) if not l.startswith("ticklog")]
            start, boss_slot, base = None, None, None
            names = {}
            attacks, procs, hp, pos = [], [], {}, collections.defaultdict(dict)
            mazes, cur_maze = [], None
            glows, cur_glow, up_hp = {}, None, collections.defaultdict(dict)
            death_launch, dealt, maxhp = set(), 0, 3000
            melee_hits = collections.defaultdict(list)
            ball_target = {}
            for r in rows:
                t, k = int(r[1]), r[2]
                if k == "npc_spawn" and int(r[4]) == BOSS:
                    boss_slot = r[3]
                    v = int(r[5])
                    base = (((v >> 14) & 0x3FFF) - SW[0], (v & 0x3FFF) - SW[1])
                if k == "hudbar" and r[4] == str(BOSS) and start is None:
                    start = t - 1
                if k == "projectile" and int(r[6]) == DEATH_PROJ:
                    death_launch.add(t)
                if k == "projectile" and int(r[6]) == 1606 and base and int(r[5]) < 0:
                    v = int(r[3])
                    if ((v >> 14) & 0x3FFF, v & 0x3FFF) == (base[0] + 15, base[1] + 40):
                        ball_target[t] = str(-int(r[5]) - 1)
                if k == "hit_player" and r[8] == str(BOSS):
                    melee_hits[t].append(r[3])
                if k == "player_tile":
                    pos[t][r[3]] = (int(r[4]), int(r[5]))
                    if r[6] != "0":
                        pos[t].pop(r[3])
                if k == "raider":
                    up_hp[t][r[3]] = int(r[4])
                if k == "hit_npc" and r[3] == boss_slot:
                    dealt += int(r[5])
                    hp[t] = (maxhp - dealt, maxhp)
                if k == "npc_retype" and r[3] == boss_slot and int(r[5]) == MAZE_BOSS:
                    procs.append(t)
                    cur_maze = []
                    mazes.append(cur_maze)
                    cur_glow = []
                    glows[len(procs) - 1] = cur_glow
                if k == "npc_retype" and r[3] == boss_slot and int(r[5]) == BOSS:
                    cur_maze = None
                    cur_glow = None
                if k == "loc_set" and int(r[4]) == LIT and cur_maze is not None:
                    v = int(r[3])
                    level = (v >> 28) & 3
                    x, z = (v >> 14) & 0x3FFF, v & 0x3FFF
                    if level == 0 and base:
                        cur_maze.append((x - base[0] - GRID[0], z - base[1] - GRID[1]))
                        cur_glow.append((t, x - base[0] - GRID[0], z - base[1] - GRID[1]))
                if k == "npc_anim" and r[3] == boss_slot and int(r[5]) in (SEQ_MELEE, SEQ_BALL):
                    kind = "melee" if int(r[5]) == SEQ_MELEE else "ball"
                    attacks.append([t, kind, None])
            if start is None or base is None:
                continue
            for a in attacks:
                if a[1] == "ball" and a[0] in death_launch:
                    a[1] = "death"
                if a[1] == "ball":
                    a[2] = ball_target.get(a[0])
                if a[1] == "melee":
                    # his melee lands on t+1 - with the ricochets and death-ball
                    # shares that also land then and carry his npc type, so the
                    # target is the one of them that ended t-1 within 1 of him
                    # (the content's licence); the first row alone credited a
                    # far ricochet victim and dropped the melee (sweep so10)
                    near = [pid for dt in (1, 0) for pid in melee_hits.get(a[0] + dt, [])
                            if pid in pos.get(a[0] - 1, {}) and adjacent(pos[a[0] - 1][pid], base)]
                    if near:
                        a[2] = near[0]
            shift = start
            rooms.append(dict(attacks=[(t - shift, k, tg) for t, k, tg in attacks], procs=[p - shift for p in procs],
                              mazes=mazes, hp={t - shift: v for t, v in hp.items()},
                              glows={k: [(t - shift, x, z) for t, x, z in g] for k, g in glows.items()},
                              up_hp={t - shift: v for t, v in up_hp.items()},
                              pos={t - shift: v for t, v in pos.items()}, base=base))
    return rooms


def adjacent(p, base):
    gx = max(base[0] + SW[0] - p[0], p[0] - (base[0] + SW[0] + 4), 0)
    gz = max(base[1] + SW[1] - p[1], p[1] - (base[1] + SW[1] + 4), 0)
    return max(gx, gz) <= 1


def measures(rooms, blert_side):
    M = collections.defaultdict(collections.Counter)
    for R in rooms:
        A = R["attacks"]
        # a barrage on him before his first attack moved it 6 -> 8 in 36 of 70
        # Blert rooms (none: 74 of 79 on 6); our kit never barrages him, so
        # the rooms compared are the unbarraged ones (the content models no
        # barrage delay - a kit that barrages Sotetseg must add it)
        if A and not R.get("barraged"):
            M["first attack (room tick)"][A[0][0] - R.get("late", 0)] += 1
        procs = R["procs"]
        for (ta, ka, _), (tb, kb, _) in zip(A, A[1:]):
            if any(ta < p <= tb for p in procs):
                continue
            if ka == "death":
                M["gap after a death ball"][tb - ta] += 1
            else:
                M["attack gap"][tb - ta] += 1
        # from the start or a death ball to the next death ball, with no maze
        # between (a maze holds the counter: the stretches after one are short)
        n, clean = 0, True
        for t, k, _ in A:
            if any(p - 1 <= t <= p + 1 for p in procs):
                clean = False
            if k == "ball":
                n += 1
            elif k == "death":
                if clean:
                    M["balls before a death ball (no maze between)"][n] += 1
                n, clean = 0, True
        # melee | the target within 1 at the end of the tick before
        for t, k, tg in A:
            if k not in ("melee", "ball") or tg is None:
                continue
            p = (R["pos"].get(t - 1) or {}).get(tg)
            if p is None or R["base"] is None:
                continue
            if adjacent(p, R["base"]):
                M["melee | target adjacent"][k] += 1
        for i, p in enumerate(procs[:2]):
            M["maze %d proc (room tick, 10s)" % (i + 1)][(p // 10) * 10] += 1
            # the last reading BEFORE the proc tick: Blert's npc row on the proc
            # tick carries the bar as the tick before ended
            hs = [v for t, v in sorted(R["hp"].items()) if t < p]
            if hs:
                c, m = hs[-1]
                M["maze %d proc hp (permille, 10s)" % (i + 1)][(1000 * c // m) // 10 * 10] += 1
            after = [t for t, _, _ in A if t > p]
            if after:
                M["maze length: proc to his next attack (report)"][after[0] - p] += 1
        maze_measures(R, M)
        for tiles in R["mazes"]:
            start, cols, length = path_stats(tiles)
            if start is None or len(cols) < 8:
                continue
            M["maze path start column"][start] += 1
            for a, b in zip(cols, cols[1:]):
                M["maze path row step |ds|"][abs(b - a)] += 1
            M["maze path length (5s)"][(length // 5) * 5] += 1
    return M


def maze_measures(R, M):
    """THE MAZE AS IT IS PLAYED, from the arena: the glow (the runner's tile,
    one a tick) and the team up top. Blert's glow is event 131, ours the
    level-0 33035 loc rows; positions are each side's player rows on the arena
    (the runner, in the realm, is not on them)."""
    base = R["base"]
    if base is None:
        return
    attacks = [t for t, _, _ in R["attacks"]]
    for i, P in enumerate(R["procs"][:2]):
        g = R["glows"].get(i) or []
        if not g:
            continue
        # the runner lands a tile south of the path and steps on: the first
        # glow is on row 0 (content: ~tob_sote_send_party)
        M["maze first glow row"][g[0][2]] += 1
        M["maze proc -> first glow (report)"][g[0][0] - P] += 1
        M["maze glow first -> last (report, 5s)"][((g[-1][0] - g[0][0]) // 5) * 5] += 1
        after = [a for a in attacks if a > g[-1][0]]
        if after:
            M["maze last glow -> his next attack (report)"][after[0] - g[-1][0]] += 1
        end = g[-1][0]
        trail = set()
        first_on, row3, off, lost = {}, {}, 0, 0
        gi = 0
        # from the landing: before the teleport (proc+3) the team is still at
        # its fight tiles, some on the grid's top row
        for t in range(P + 4, end + 8):
            while gi < len(g) and g[gi][0] <= t:
                trail.add((g[gi][1], g[gi][2]))
                gi += 1
            for name, (x, z) in (R["pos"].get(t) or {}).items():
                c, r = x - base[0] - GRID[0], z - base[1] - GRID[1]
                if 0 <= c <= 13 and 0 <= r <= 14:
                    first_on.setdefault(name, t)
                    if r >= 3:
                        row3.setdefault(name, t)
                    if t <= end and (c, r) not in trail:
                        off += 1
        for name, t in first_on.items():
            M["maze team on the grid: proc -> (report)"][t - P] += 1
        if row3:
            # the arena tornado rises when one of the team reaches row 3
            M["maze tornado trigger: proc -> (report)"][min(row3.values()) - P] += 1
        M["maze team ends on unglowed grid tiles (report)"][min(off, 3)] += 1
        # damage to the team up top while the maze runs (rags, the tornado)
        ups = collections.defaultdict(list)
        for t in range(P + 4, end + 5):
            v = R["up_hp"].get(t)
            if v is None:
                continue
            for name, h in ([v] if isinstance(v, tuple) else v.items()):
                if name in first_on:
                    ups[name].append(h)
        for name, hs in ups.items():
            lost = sum(max(0, a - b) for a, b in zip(hs, hs[1:]))
            M["maze up-top hp lost (report, 10s)"][(lost // 10) * 10] += 1


def compare(name, a, b, test=True):
    keys = sorted(set(a) | set(b), key=lambda k: (str(type(k)), k))
    na, nb = sum(a.values()), sum(b.values())
    if na == 0 or nb == 0:
        print("  %-46s blert n=%d ours n=%d  (no data on one side)" % (name, na, nb))
        return None
    tvd = 0.5 * sum(abs(a[k] / na - b[k] / nb) for k in keys)
    p = None
    if test and len(keys) > 1:
        try:
            p = chi2_contingency([[a[k] for k in keys], [b[k] for k in keys]])[1]
        except ValueError:
            p = None
    ok = (p is not None and p >= 0.01) or tvd < 0.05
    top = lambda c, n: " ".join("%s:%d%%" % (k, round(100 * v / n)) for k, v in c.most_common(6))
    print("  %-46s %-6s tvd %.3f p %s  (blert n=%d ours n=%d)" % (
        name, ("MATCH" if ok else "DIFFER") if test else "report", tvd, "%.3g" % p if p is not None else "-", na, nb))
    print("      blert %s" % top(a, na))
    print("      ours  %s" % top(b, nb))
    return ok


if __name__ == "__main__":
    B = measures(blert(sys.argv[1]), True)
    O = measures(ours(sys.argv[2:]), False)
    results = {}
    for name in sorted(set(B) | set(O)):
        # the proc's hp is REPORTED, not tested: Blert's npc hitpoints trail
        # the real bar by ~4 ticks round a maze (98 of its first mazes read
        # <= 66.6% only 4 ticks AFTER the proc), so its "last reading before
        # the proc" is several ticks old; ours procs 1 tick after the crossing
        # hit, 16 of 16 (the content checks every npc turn)
        player = ("(report" in name or name.startswith("maze 1 proc") or name.startswith("maze 2 proc"))
        r = compare(name, B[name], O[name], test=not player)
        if r is not None and not player:
            results[name] = r
    bad = [k for k, v in results.items() if not v]
    print("== %d measures tested, %d MATCH, %d DIFFER" % (len(results), len(results) - len(bad), len(bad)))
    for k in bad:
        print("   DIFFER:", k)
