#!/usr/bin/env python3
"""Maiden spec-pass analyses over blert Maiden event streams (stage 10).

    python3 maiden_spec_analyses.py <raw dir> > maiden_spec_pass_2026-10-02.txt

<raw dir> holds <uuid>_10.json (GET /api/v1/raids/tob/<uuid>/events?stage=10) and the
list_m<mode>_s<scale>.json challenge listings, fetched 2026-10-02 by
sources/blert_api/fetch_blert_maiden.py. Raw streams are not committed.
Prints: transmog threshold and hitpoint continuity, the T-1 aim of blood pools, which
player receives the two extra splats, blood spawn movement (as a fraction, and as runs), and
hitpoints by party size, and (appended 2026-10-03) her death timeline: killing blow, dying_a,
dying_b and despawn ticks.
"""
import collections, glob, json, math, os, sys
RAW = sys.argv[1]
MAIDEN = set(range(8360, 8366)) | set(range(10822, 10828))
SLUG = {8367, 10829}
hp = lambda v: (v >> 16, v & 0xFFFF)
lists = {}
for f in glob.glob(RAW + "/list_m*_s*.json"):
    for c in json.load(open(f)):
        lists[c["uuid"]] = c
streams = {}
for f in sorted(glob.glob(RAW + "/*_10.json")):
    u = os.path.basename(f)[:36]
    if u in lists:
        streams[u] = sorted(json.load(open(f)), key=lambda e: e["tick"])
print("raids:", len(streams), "modes/scales:", sorted((lists[u]["mode"], lists[u]["scale"]) for u in streams))

print("\n== transmog: hitpoints before/after the id change (first three changes per raid)")
n = 0; over = collections.Counter(); jump = 0
for u, ev in streams.items():
    last = prev = None; seen = 0
    for e in ev:
        if e["type"] in (7, 8) and e["npc"]["id"] in MAIDEN and "hitpoints" in e["npc"]:
            cur, mx = hp(e["npc"]["hitpoints"])
            if last is not None and e["npc"]["id"] != last and seen < 3:
                seen += 1; n += 1
                thr = (70, 50, 30)[seen - 1]
                pct = 100.0 * cur / mx
                over[pct <= thr] += 1
                if cur > prev[0]: jump += 1
            last = e["npc"]["id"]; prev = (cur, mx)
print("transmogs", n, "at or under the threshold", over[True], "over", over[False], "hitpoints higher than the tick before", jump)

print("\n== hitpoints at spawn by party size (mode, scale): maiden max, crab max, slug max")
for u, ev in streams.items():
    m = [hp(e["npc"]["hitpoints"])[1] for e in ev if e["type"] == 7 and e["npc"]["id"] in MAIDEN][:1]
    c = sorted({hp(e["npc"]["hitpoints"])[1] for e in ev if e["type"] == 7 and e["npc"]["id"] in (8366, 10828)})
    s = sorted({hp(e["npc"]["hitpoints"])[1] for e in ev if e["type"] == 7 and e["npc"]["id"] in SLUG})
    print(lists[u]["mode"], lists[u]["scale"], m, c, s)

def positions(ev):
    pos = collections.defaultdict(dict)
    for e in ev:
        if e["type"] == 4:
            pos[e["tick"]][e["player"]["name"]] = (e["xCoord"], e["yCoord"])
    return pos

def new_pool_tiles(ev, T):
    tick_sets = {e["tick"]: {(s["x"], s["y"]) for s in e["maidenBloodSplats"]} for e in ev if e["type"] == 101}
    prev = [t for t in tick_sets if t < T]
    base = tick_sets[max(prev)] if prev else set()
    new = set()
    for A in range(T + 2, T + 14):
        if A in tick_sets:
            new |= tick_sets[A] - base
    return new

print("\n== T-1 aim: new splat tiles that sit under a player at T-1 only / T only / both (T = throw tick)")
res = collections.Counter()
for u, ev in streams.items():
    pos = positions(ev)
    for T in [e["tick"] for e in ev if e["type"] == 10 and e["npc"]["id"] in MAIDEN and e["npcAttack"]["attack"] == 2]:
        p1 = set(pos[T - 1].values()); p0 = set(pos[T].values())
        for tile in new_pool_tiles(ev, T):
            res[("T-1" if tile in p1 else "") + ("T" if tile in p0 else "") or "neither"] += 1
print(dict(res))

print("\n== extras: who gets the two extra splats (throws with 2+ players and a unique 5x5 owner)")
def d_box(p):
    return max(max(3162 - p[0], 0, p[0] - 3167), max(4444 - p[1], 0, p[1] - 4449))
tot = far = north = 0
for u, ev in streams.items():
    pos = positions(ev)
    for T in [e["tick"] for e in ev if e["type"] == 10 and e["npc"]["id"] in MAIDEN and e["npcAttack"]["attack"] == 2]:
        players = pos[T - 1]
        if len(players) < 2:
            continue
        ptiles = set(players.values())
        extras = [t for t in new_pool_tiles(ev, T) if t not in ptiles]
        if not 1 <= len(extras) <= 2:
            continue
        cand = [k for k, p in players.items() if all(max(abs(t[0] - p[0]), abs(t[1] - p[1])) <= 2 for t in extras)]
        if len(cand) != 1:
            continue
        tot += 1
        p = players[cand[0]]
        far += d_box(p) == max(d_box(q) for q in players.values())
        north += p[1] == max(q[1] for q in players.values())
print("throws", tot, "owner furthest from her hitbox", far, "owner northernmost", north)

print("\n== blood spawn movement: Chebyshev displacement between consecutive ticks")
steps = collections.Counter()
for u, ev in streams.items():
    by = collections.defaultdict(dict)
    for e in ev:
        if e["type"] in (7, 8) and e["npc"]["id"] in SLUG:
            by[e["npc"]["roomId"]][e["tick"]] = (e["xCoord"], e["yCoord"])
    for tk in by.values():
        ts = sorted(tk)
        for a, b in zip(ts, ts[1:]):
            if b - a == 1:
                steps[max(abs(tk[b][0] - tk[a][0]), abs(tk[b][1] - tk[a][1]))] += 1
print(dict(steps), "moved fraction %.3f" % (1 - steps[0] / sum(steps.values())))

print("\n== blood roll over the new raids (eligible = two attacks since the last throw)")
for mode in (11, 12):
    elig = thr = viol = 0
    for u, ev in streams.items():
        if lists[u]["mode"] != mode:
            continue
        since = 99
        for t, a in sorted({(e["tick"], e["npcAttack"]["attack"]) for e in ev if e["type"] == 10 and e["npc"]["id"] in MAIDEN}):
            b = a == 2
            if since >= 2:
                elig += 1; thr += b
            elif b:
                viol += 1
            since = 0 if b else since + 1
    print("mode", mode, "eligible", elig, "throws", thr, "violations", viol)

print("\n== blood spawn movement as runs (restated 2026-10-02): a free slug steps every tick")
# The fraction above mixes free ticks with still runs. Read each slug's consecutive pairs as
# runs: a move followed by a move, and the length of every run of ticks it stood still.
# Every still run is 5+ ticks: 5 = the ticks after Maiden dies, 32 = a freeze, 8/20/37 end the
# slug's life (frozen, then killed), 281-478 = Hard slugs stuck at 3187,4458. A free slug never
# pauses for 1-4 ticks and never steps two tiles, so the 0.337 above is not a step rate.
follow = collections.Counter(); stills = collections.Counter()
for u, ev in streams.items():
    by = collections.defaultdict(dict)
    for e in ev:
        if e["type"] in (7, 8) and e["npc"]["id"] in SLUG:
            by[e["npc"]["roomId"]][e["tick"]] = (e["xCoord"], e["yCoord"])
    for tk in by.values():
        ts = sorted(tk)
        d = [max(abs(tk[b][0] - tk[a][0]), abs(tk[b][1] - tk[a][1])) if b - a == 1 else None
             for a, b in zip(ts, ts[1:])]
        for x, y in zip(d, d[1:]):
            if x and y is not None:
                follow["move" if y else "still"] += 1
        run = 0
        for m in d + [None]:
            if m == 0:
                run += 1
            else:
                if run:
                    stills[run] += 1
                run = 0
short = sum(n * k for n, k in stills.items() if n < 5)
print("slug lives", sum(len(set(e["npc"]["roomId"] for e in ev if e["type"] in (7, 8) and e["npc"]["id"] in SLUG)) for ev in streams.values()),
      "moves followed by a move on the next tick", follow["move"], "of", follow["move"] + follow["still"])
print("still runs (length, count)", sorted(stills.items()))
print("still runs of 1-4 ticks", sum(k for n, k in stills.items() if n < 5),
      "two-tile steps", sum(k for n, k in steps.items() if n >= 2),
      "free-tick step rate (ticks outside still runs of 5+) %d of %d" % (steps[1], steps[1] + short))

print("\n== death timeline (appended 2026-10-03, seam4): killing blow, dying_a, dying_b, despawn")
# Per raid: the last player attack on her live form (atk), the tick her live form reads 0
# hitpoints if the recorder ever shows it (hp0), the first tick of the dying_a id 8364/10826
# (A), of the dying_b id 8365/10827 (B), and her NPC_DEATH event (D). Every other hit in
# these streams shows on the tick after the attack, so when no hp0 is recorded the killing
# hit shows on the dying_a tick itself. The cache seqs (maiden_death_a 90 cycles = 3 ticks,
# maiden_death_b 120 = 4) are the animations' lengths, not the ticks between these ids.
LIVE = MAIDEN - {8364, 8365, 10826, 10827}
DYING_A = {8364, 10826}; DYING_B = {8365, 10827}
ab = collections.Counter(); bd = collections.Counter(); ad = collections.Counter()
kill_a = collections.Counter(); kill_d = collections.Counter()
for u, ev in streams.items():
    her = [e for e in ev if "npc" in e and e["npc"]["id"] in MAIDEN]
    A = min(e["tick"] for e in her if e["npc"]["id"] in DYING_A)
    B = min(e["tick"] for e in her if e["npc"]["id"] in DYING_B)
    D = [e["tick"] for e in her if e["type"] == 9]
    D = D[0] if len(D) == 1 else None
    hp0 = [e["tick"] for e in her if e["type"] == 8 and e["npc"]["id"] in LIVE
           and "hitpoints" in e["npc"] and hp(e["npc"]["hitpoints"])[0] == 0]
    hp0 = min(hp0) if hp0 else None
    atk = max(e["tick"] for e in ev if e["type"] == 5 and e["tick"] < A
              and e.get("attack", {}).get("target", {}).get("id") in LIVE)
    K = hp0 if hp0 is not None else A          # the tick the killing hit shows
    ab[B - A] += 1; bd[None if D is None else D - B] += 1; ad[None if D is None else D - A] += 1
    kill_a[A - K] += 1; kill_d[None if D is None else D - K] += 1
    print(u[:8], "mode", lists[u]["mode"], "scale", lists[u]["scale"], "atk", atk, "hp0", hp0,
          "dying_a", A, "dying_b", B, "despawn", D, "(K = %d: dying_a K+%d dying_b K+%d despawn K+%s)"
          % (K, A - K, B - K, None if D is None else D - K))
print("dying_a -> dying_b", dict(ab), "dying_b -> despawn", dict(bd), "dying_a -> despawn", dict(ad))
print("raids that record 0 hitpoints on her live form", sum(1 for u in streams
      if any(e["type"] == 8 and e["npc"]["id"] in LIVE and "hitpoints" in e["npc"] and hp(e["npc"]["hitpoints"])[0] == 0
             for e in streams[u] if "npc" in e)), "of", len(streams))
print("killing hit -> dying_a", dict(kill_a), "killing hit -> despawn", dict(kill_d))
