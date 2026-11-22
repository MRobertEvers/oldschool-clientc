#!/usr/bin/env python3
"""Work out how each npc type MOVES in a recording, and fit a patrol to it.

The generated npc blocks give every type `defaultmode=wander, wanderrange=5`, and
the audits that check movement (npc_movement_audit.py `paths`) compare step-gap
histograms: they tell you THAT a type walks, not WHERE. A guard that walks one
loop round a garden and a guard that wanders five tiles from his spawn look the
same to a gap histogram. This tool reads the tiles.

    python3 tools/rsprox/npc_routes.py <transcript>... --match '<regex on npc name>'
        [--from T] [--to T] [--ticks] [--fit] [--json out.json]

Per npc type (all lives pooled, so a type that drops out of view and comes back
is still one route):

  * the class: STILL (never steps), WANDER, PATROL, SCRIPTED (steps only in a few
    bursts: a walk the content drives) -- with the evidence for it;
  * the bounding box of every tile it stood on, the spawn tiles and angles;
  * the TURN POINTS: tiles where it stopped, or changed direction, with how often
    and how long it stood there (dwell ticks), and the transitions between them;
  * for a PATROL, the cycle of turn points in order, as `patrolN=` lines for the
    content's .npc (pause = the recorded dwell, in OUR engine's units: an npc
    that arrives on tick t and steps again on t+g has pause g-2 for g >= 3,
    0 for g = 1; a gap of 2 cannot be expressed and is reported);
  * `--fit`: the cycle replayed with our engine's step rule (diagonal first, then
    the x leg, then the z leg: npc_take_step) from every recorded arrival at a
    waypoint, compared tile by tile with the recording: steps matched / total.
    A route under ~95% is not the route; read the misses (`--ticks`).

What decides the class (all lives of the type, steps only):
  * a patrol repeats: its turn points are few, each is visited many times, and
    each one has one usual successor (successor share >= 0.8);
  * a wanderer's turn points are many and scattered, each visited once or twice,
    with random successors; its idle runs between walks are long and irregular,
    and it stays inside a box of about 2*wanderrange+1 around its spawn;
  * scripted walks come in a few bursts tied to the player's clicks.
Look at the recording's own walks before trusting the class: a patrol that the
recorder only saw part of reads as a few turn points with successor share 1.

Coordinates are the transcript's. Use a `--raw` transcript for an npc inside a
copy (instance tiles are translated to the template in the default one).
"""

import argparse
import collections
import json
import re
import sys

TICK_RE = re.compile(r"^\[(\d+)\]\s*$")
NPC_RE = re.compile(
    r"^\s*\[(walk|run|crawl|teleport|telejump|idle|add|del)\]\s+npc=\(index=(\d+), id=([^\s(]+)"
    r" \((\d+)\)(?:, multinpc=[^)]*\))?[^)]*coord=\((\d+), (\d+), (\d+)\)\)"
    r"(?:, newcoord=\((\d+), (\d+), (\d+)\))?(.*)$")
ANGLE_RE = re.compile(r"spawnangle=(\w+)")
DIRS = {(0, 1): "N", (0, -1): "S", (1, 0): "E", (-1, 0): "W",
        (1, 1): "NE", (-1, 1): "NW", (1, -1): "SE", (-1, -1): "SW"}


def sgn(v):
    return (v > 0) - (v < 0)


def parse(paths, name_re, t_from, t_to):
    """Return {type_name: [life]}, life = {index, add, del, angle, track: [(tick, x, z, level, kind)]}."""
    lives = collections.defaultdict(list)
    open_lives = {}
    for path in paths:
        tick = 0
        with open(path, encoding="utf-8", errors="replace") as fh:
            for raw in fh:
                m = TICK_RE.match(raw)
                if m:
                    tick = int(m.group(1))
                    continue
                if "npc=(index=" not in raw:
                    continue
                m = NPC_RE.match(raw)
                if not m:
                    continue
                kind, index, name = m.group(1), int(m.group(2)), m.group(3)
                if not name_re.search(name):
                    continue
                if (t_from is not None and tick < t_from) or (t_to is not None and tick > t_to):
                    continue
                x, z, lv = int(m.group(5)), int(m.group(6)), int(m.group(7))
                if m.group(8):
                    x, z, lv = int(m.group(8)), int(m.group(9)), int(m.group(10))
                key = (path, index, name)
                life = open_lives.get(key)
                if kind == "add" or life is None:
                    if life is not None:
                        lives[name].append(life)
                    am = ANGLE_RE.search(m.group(11) or "")
                    life = {"path": path, "index": index, "add": tick, "del": None,
                            "angle": am.group(1) if am else None, "track": []}
                    open_lives[key] = life
                if kind == "del":
                    life["del"] = tick
                    life["track"].append((tick, int(m.group(5)), int(m.group(6)), lv, "del"))
                    lives[name].append(life)
                    del open_lives[key]
                    continue
                life["track"].append((tick, x, z, lv, kind))
    for key, life in open_lives.items():
        lives[key[2]].append(life)
    return lives


def steps_of(life):
    """[(tick, from, to, kind)] for every tile change in the life."""
    out = []
    prev = None
    for tick, x, z, lv, kind in life["track"]:
        if kind == "del":
            break
        cur = (x, z, lv)
        if prev is not None and cur != prev and kind in ("walk", "run", "crawl", "teleport", "telejump"):
            out.append((tick, prev, cur, kind))
        prev = cur
    return out


def turn_points(life):
    """Split a life's walk into legs; a leg ends where the npc stood still or turned.

    Returns [(tile, arrive_tick, leave_tick or None)] in order. leave_tick is the
    tick of the first step away from the tile.
    """
    st = [s for s in steps_of(life) if s[3] in ("walk", "run", "crawl")]
    pts = []
    for i, (tick, a, b, kind) in enumerate(st):
        d = (sgn(b[0] - a[0]), sgn(b[1] - a[1]))
        if i + 1 < len(st):
            nt, na, nb, _ = st[i + 1]
            nd = (sgn(nb[0] - na[0]), sgn(nb[1] - na[1]))
            gap = nt - tick
            if na != b:  # a jump in between (teleport): leg ends here
                pts.append((b, tick, None))
            elif gap > (2 if kind == "run" else 1) or nd != d:
                pts.append((b, tick, nt))
        else:
            pts.append((b, tick, None))
    return pts


def classify(lives):
    all_steps = []
    pts = []
    for life in lives:
        s = steps_of(life)
        all_steps.extend(s)
        pts.extend(turn_points(life))
    walks = [s for s in all_steps if s[3] in ("walk", "run", "crawl")]
    if not walks:
        return "STILL", {}, pts
    visits = collections.Counter(p[0] for p in pts)
    succ = collections.defaultdict(collections.Counter)
    for life in lives:
        tp = turn_points(life)
        for (a, _, la), (b, _, _) in zip(tp, tp[1:]):
            if la is not None:
                succ[a][b] += 1
    shares = []
    for a, c in succ.items():
        tot = sum(c.values())
        if tot >= 2:
            shares.append(c.most_common(1)[0][1] / tot)
    share = sum(shares) / len(shares) if shares else 0.0
    repeated = sum(1 for v in visits.values() if v >= 3)
    ev = {"steps": len(walks), "turn_points": len(visits), "visited>=3": repeated,
          "successor_share": round(share, 2)}
    # a few bursts of steps, long silences: content-driven
    ticks = sorted(s[0] for s in walks)
    bursts = 1 + sum(1 for a, b in zip(ticks, ticks[1:]) if b - a > 30)
    ev["bursts"] = bursts
    if repeated >= 2 and share >= 0.8 and len(visits) <= 3 * repeated + 4:
        return "PATROL", ev, pts
    if len(walks) <= 40 and bursts <= 3:
        return "SCRIPTED", ev, pts
    return "WANDER", ev, pts


def per_tick(life):
    """{tick: (x, z, level)} for every tick of the life (a tile holds until the next line)."""
    track = [(t, x, z, lv) for (t, x, z, lv, k) in life["track"] if k != "del"]
    if not track:
        return {}
    out = {}
    j = 0
    cur = track[0][1:]
    for t in range(track[0][0], track[-1][0] + 1):
        while j < len(track) and track[j][0] <= t:
            cur = track[j][1:]
            j += 1
        out[t] = cur
    return out


def find_period(pos, lo=4, hi=1200):
    """Smallest P with pos[t] == pos[t+P] on >= 97% of the overlapping ticks (>= 2P of them)."""
    ticks = sorted(pos)
    if len(ticks) < 2 * lo:
        return None, 0.0
    t0, t1 = ticks[0], ticks[-1]
    best = (None, 0.0)
    for P in range(lo, min(hi, (t1 - t0) // 2) + 1):
        same = tot = 0
        for t in range(t0, t1 - P + 1):
            tot += 1
            same += pos[t] == pos[t + P]
        if tot >= P and same / tot >= 0.97:
            # reject a period that never moves (a standing npc)
            if len({pos[t] for t in range(t0, t0 + P)}) > 1:
                return P, same / tot
        if tot and same / tot > best[1]:
            best = (P, same / tot)
    return None, best[1]


def naive_path(a, b):
    """Our npc_take_step with nothing in the way: diagonal first, then x, then z."""
    out = []
    x, z = a
    while (x, z) != b:
        x += sgn(b[0] - x)
        z += sgn(b[1] - z)
        out.append((x, z))
    return out


def fit_waypoints(period):
    """Cover one period (a list of (x, z) per tick, starting on a dwell) with waypoints.

    A dwell (the npc on one tile for g >= 2 ticks) is a waypoint with pause g - 2 (our
    engine; g = 2 cannot be expressed: flagged). Between dwells, the fewest waypoints
    whose naive paths retrace the recorded tiles exactly. Returns [(x, z, pause, gap)].
    """
    n = len(period)
    # runs of equal tiles: (tile, length)
    runs = []
    for p in period:
        if runs and runs[-1][0] == p:
            runs[-1][1] += 1
        else:
            runs.append([p, 1])
    # the period starts on the first tick of a dwell; a run that wraps joins the first
    if len(runs) > 1 and runs[-1][0] == runs[0][0]:
        runs[0][1] += runs[-1][1]
        runs.pop()
    wps = []
    i = 0
    while i < len(runs):
        tile, g = runs[i]
        if g >= 2:
            wps.append((tile[0], tile[1], (g - 2) if g >= 3 else 0, g))
        # walk forward from this tile as far as one naive leg reproduces
        j = i + 1
        while j < len(runs):
            k = j
            best = None
            while k < len(runs):
                want = [r[0] for r in runs[j:k + 1]]
                if naive_path(tile, runs[k][0]) != want:
                    break
                best = k
                if runs[k][1] >= 2:
                    break
                k += 1
            if best is None:
                # cannot be walked even one tile naively (a jump): its own waypoint
                best = j
            if runs[best][1] >= 2:
                break
            t2 = runs[best][0]
            wps.append((t2[0], t2[1], 0, 1))
            tile = t2
            j = best + 1
        i = j
    return wps


def simulate(wps, start, ticks):
    """Our patrol engine from `start` (standing on wps[-1], just arrived): ticks positions."""
    pos = []
    cur = start
    idx = 0
    pause = 0
    arrived = True
    prev_pause = wps[-1][2]
    while len(pos) < ticks:
        if arrived:
            pause = prev_pause
            arrived = False
            if pause > 0:
                pos.append(cur)
                continue
        elif pause > 0:
            pause -= 1
            pos.append(cur)
            continue
        dest = (wps[idx][0], wps[idx][1])
        cur = (cur[0] + sgn(dest[0] - cur[0]), cur[1] + sgn(dest[1] - cur[1]))
        if cur == dest:
            arrived = True
            prev_pause = wps[idx][2]
            idx = (idx + 1) % len(wps)
        pos.append(cur)
    return pos


def pooled(lives):
    """The slot with the most steps, every life of it on absolute ticks: {tick: (x, z, level)}.

    A guard that drops out of view and is re-added is the same npc: its patrol kept
    going while the recorder could not see it, so its lives fold onto one period.
    """
    by_index = collections.defaultdict(list)
    for life in lives:
        by_index[(life["path"], life["index"])].append(life)
    key = max(by_index, key=lambda k: sum(len(steps_of(l)) for l in by_index[k]))
    pos = {}
    for life in by_index[key]:
        pos.update(per_tick(life))
    return key, pos


def find_period_sparse(pos, lo=4, hi=1500, need=0.90):
    """Smallest P with pos[t] == pos[t+P] on >= `need` of the ticks where both are seen.

    90%, not 100%: a guard that stops to catch the player, or a patrol that is
    nudged by a player standing on its route, drops a few pairs and shifts its
    phase afterwards. The per-life scoring below says how much of it is the route.
    """
    ticks = sorted(pos)
    if len(ticks) < 2 * lo:
        return None, 0.0
    span = ticks[-1] - ticks[0]
    for P in range(lo, min(hi, span) + 1):
        same = tot = 0
        moved = set()
        for t in ticks:
            u = pos.get(t + P)
            if u is None:
                continue
            tot += 1
            same += pos[t][:2] == u[:2]
            moved.add(pos[t][:2])
        if tot >= max(P, 40) and same / tot >= need and len(moved) > 1:
            return P, same / tot
    return None, 0.0


def life_tracks(lives, key):
    out = []
    for life in lives:
        if (life["path"], life["index"]) != key:
            continue
        lp = per_tick(life)
        if len(lp) >= 2:
            out.append(lp)
    return out


def score_template(tracks, period):
    """Per track, the best phase: (matched, total, [(start, end) off-route runs])."""
    P = len(period)
    m_all = n_all = 0
    offs = []
    for lp in tracks:
        lt = sorted(lp)
        best = (-1, 0)
        for ph in range(P):
            m = 0
            for t in lt:
                m += lp[t][:2] == period[(t - lt[0] + ph) % P]
            if m > best[0]:
                best = (m, ph)
        m, ph = best
        m_all += m
        n_all += len(lt)
        run = None
        for t in lt:
            bad = lp[t][:2] != period[(t - lt[0] + ph) % P]
            if bad and run is None:
                run = [t, t]
            elif bad:
                run[1] = t
            elif run is not None:
                offs.append(tuple(run))
                run = None
        if run:
            offs.append(tuple(run))
    return m_all, n_all, offs


def fit_type(lives):
    """Fit a patrol to the type's busiest slot; score it on every view of that slot."""
    key, pos = pooled(lives)
    P, conf = find_period_sparse(pos)
    if not P:
        return None
    tracks = life_tracks(lives, key)
    # candidate templates: every fully seen window of P ticks (sampled), else the fold
    cands = []
    for lp in tracks:
        lt = sorted(lp)
        for i in range(0, len(lt) - P + 1, max(1, P // 8)):
            cands.append([lp[t][:2] for t in lt[i:i + P]])
    if not cands:
        ticks = sorted(pos)
        votes = [collections.Counter() for _ in range(P)]
        for t in ticks:
            votes[(t - ticks[0]) % P][pos[t][:2]] += 1
        if any(not v for v in votes):
            return {"period": P, "confidence": conf, "unseen": sum(1 for v in votes if not v),
                    "level": pos[ticks[0]][2]}
        cands = [[v.most_common(1)[0][0] for v in votes]]
    if len(cands) > 40:
        step = len(cands) / 40.0
        cands = [cands[int(i * step)] for i in range(40)]
    best = None
    for c in cands:
        m, n, offs = score_template(tracks, c)
        if best is None or m > best[0]:
            best = (m, n, offs, c)
    on_m, on_n, off, period = best
    # rotate to the first tick of the longest dwell
    best_len, best_i = 0, None
    for i in range(P):
        if period[i - 1] == period[i]:
            continue
        j = 0
        while j < P and period[(i + j) % P] == period[i]:
            j += 1
        if j > best_len:
            best_len, best_i = j, i
    if best_i is None:
        return None
    period = period[best_i:] + period[:best_i]
    wps = fit_waypoints(period)
    k = next((i for i, w in enumerate(wps) if (w[0], w[1]) == period[0] and w[3] >= 2), None)
    if k is None:
        return None
    wps = wps[k + 1:] + wps[:k + 1]
    sim = simulate(wps, period[0], P * 3)
    sim_period = ([period[0]] + sim)[:P]
    engine_ok = sum(1 for a, b in zip(sim_period, period) if a == b)
    lv = next(iter(pos.values()))[2]
    return {"period": P, "confidence": conf, "waypoints": wps, "engine": (engine_ok, P),
            "on": (on_m, on_n), "off": off, "period_tiles": period, "level": lv, "slot": key[1]}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--match", required=True, help="regex on the npc name")
    ap.add_argument("--from", dest="t_from", type=int)
    ap.add_argument("--to", dest="t_to", type=int)
    ap.add_argument("--ticks", action="store_true", help="print every life's steps")
    ap.add_argument("--fit", action="store_true", help="replay the fitted patrol against the recording")
    ap.add_argument("--json")
    a = ap.parse_args()
    lives = parse(a.paths, re.compile(a.match), a.t_from, a.t_to)
    report = {}
    for name in sorted(lives):
        ls = lives[name]
        cls, ev, pts = classify(ls)
        tiles = [(x, z) for life in ls for (_, x, z, _, k) in life["track"] if k != "del"]
        xs = [t[0] for t in tiles]
        zs = [t[1] for t in tiles]
        spawns = collections.Counter((life["track"][0][1], life["track"][0][2]) for life in ls if life["track"])
        angles = collections.Counter(life["angle"] for life in ls)
        jumps = collections.Counter(
            (s[1][:2], s[2][:2]) for life in ls for s in steps_of(life) if s[3] in ("teleport", "telejump"))
        print(f"== {name}  lives={len(ls)}  class={cls}  {ev}")
        if tiles:
            print(f"   box x {min(xs)}..{max(xs)}  z {min(zs)}..{max(zs)}  level {ls[0]['track'][0][3]}")
        print(f"   first tiles seen: {dict(spawns.most_common(6))}  spawnangle: {dict(angles)}")
        if jumps:
            print(f"   jumps (teleport/telejump): {dict(jumps.most_common(6))}")
        entry = {"class": cls, "evidence": ev, "lives": len(ls)}
        visits = collections.Counter(p[0] for p in pts)
        if cls == "WANDER":
            print(f"   turn points: {len(visits)} (top: "
                  + ", ".join(f"{p[0]},{p[1]} x{n}" for p, n in visits.most_common(8)) + ")")
            # a wanderer: the box it keeps to, the radius that box implies round its
            # middle, how far one walk goes, and how long it stands between walks
            w, h = max(xs) - min(xs), max(zs) - min(zs)
            legs, idles = [], []
            for life in ls:
                st = [x for x in steps_of(life) if x[3] in ("walk", "run")]
                run = 0
                for (t1, _, _, _), (t2, _, _, _) in zip(st, st[1:]):
                    run += 1
                    if t2 - t1 > 1:
                        legs.append(run)
                        idles.append(t2 - t1 - 1)
                        run = 0
            mid = ((min(xs) + max(xs)) // 2, (min(zs) + max(zs)) // 2)
            def hist(v):
                c = collections.Counter(v)
                return ", ".join(f"{k}:{c[k]}" for k in sorted(c)[:12])
            print(f"   WANDER: box {w + 1}x{h + 1} round {mid[0]},{mid[1]} -> wanderrange >= "
                  f"{max((w + 1) // 2, (h + 1) // 2)} (compare the spawn tile: the radius is from it);"
                  f" tiles per walk {{{hist(legs)}}}; ticks standing between walks"
                  f" median {sorted(idles)[len(idles) // 2] if idles else '-'} {{{hist(idles)}}}")
            entry["wander"] = {"box": [min(xs), min(zs), max(xs), max(zs)], "legs": legs, "idles": idles}
        if cls != "STILL":
            fit = fit_type(ls)
            if fit is None:
                print("   no period: not a fixed route (or too little of it seen)")
            else:
                lv = fit["level"]
                if "unseen" in fit:
                    print(f"   period {fit['period']} ticks ({fit['confidence']:.0%}) but {fit['unseen']} of its"
                          f" ticks never seen: a longer recording, or read the route by hand")
                    report[name] = entry
                    continue
                print(f"   ROUTE (slot {fit['slot']}): period {fit['period']} ticks (self-match "
                      f"{fit['confidence']:.0%}); our engine retraces {fit['engine'][0]}/{fit['engine'][1]};"
                      f" the slot on it {fit['on'][0]}/{fit['on'][1]} ticks seen"
                      f" ({100.0 * fit['on'][0] / max(1, fit['on'][1]):.1f}%)")
                for i, (x, z, pause, g) in enumerate(fit["waypoints"]):
                    note = "  <- gap 2: our engine cannot pause one tick" if g == 2 else ""
                    print(f"     patrol{i + 1}={lv}_{x // 64}_{z // 64}_{x % 64}_{z % 64},{pause}"
                          f"   // {x},{z}" + (f" stands {g} ticks" if g >= 2 else "") + note)
                if fit["off"]:
                    print("     off the route: " + ", ".join(
                        f"[{a}]" if a == b else f"[{a}..{b}]" for a, b in fit["off"][:10])
                          + (" ..." if len(fit["off"]) > 10 else ""))
                tot_m, tot_n = fit["on"]
                entry["fit"] = {"period": fit["period"], "waypoints": fit["waypoints"],
                                "engine": fit["engine"], "lives_match": [tot_m, tot_n]}
        if a.ticks:
            for life in ls:
                st = steps_of(life)
                print(f"   life index={life['index']} [{life['add']}..{life['del']}] "
                      f"{len(st)} steps")
                for p in turn_points(life):
                    print(f"     stop {p[0][0]},{p[0][1]} at [{p[1]}] leave [{p[2]}]")
        report[name] = entry
    if a.json:
        with open(a.json, "w") as fh:
            json.dump(report, fh, indent=1)


if __name__ == "__main__":
    sys.exit(main())
