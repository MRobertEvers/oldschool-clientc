#!/usr/bin/env python3
"""M11 / M20 analysis over the streams fetched by harvest_mazes.py.

M11 — Sotetseg maze structure.  blert's TOB_SOTE_MAZE_PATH events carry the
overworld tiles as they light up, in maze grid coordinates (x 0-13, y 0-14).
Only the currently-lit tiles are reported, so a maze is the union over its
events and is sampled, not complete.  Two filters make it usable:

  * fragments with no odd-row tile at all are the *underworld* trail (blert
    records the chosen player's own tiles when the recorder is the one sent
    to the shadow realm) - a straight line at x=6, not a maze;
  * only fragments carrying exactly one tile on each of the eight even rows
    are kept, which is enough to recover the whole vertical skeleton.

M20 — Verzik phase timings: the gap from each phase event to the phase npc's
id change, and from both of those to the first attack of the phase.

    python3 analyze_mazes.py /tmp/blertmaze
"""
import collections, glob, json, math, os, random, sys

EVEN = list(range(0, 15, 2))


def mazes(root):
    """Yield (mode, scale, raid, columns) for every usable maze."""
    for f in sorted(glob.glob(os.path.join(root, "sote", "*.json"))):
        mode, scale, raid = os.path.basename(f)[:-5].split("_", 2)
        try:
            ev = json.load(open(f))
        except ValueError:
            continue
        frags, cur, last = [], None, None
        for e in ev:
            if e["type"] == 130:                       # maze proc: a new maze
                if cur:
                    frags.append(cur)
                cur, last = [], e["tick"]
                continue
            tiles = (e.get("soteMaze") or {}).get("activeTiles") or []
            if not tiles:
                continue
            if cur is None or (last is not None and e["tick"] - last > 20):
                if cur:
                    frags.append(cur)
                cur = []
            cur += [(t["x"], t["y"]) for t in tiles]
            last = e["tick"]
        if cur:
            frags.append(cur)
        for fr in frags:
            if not fr or not any(y % 2 for _, y in fr):
                continue                                # underworld trail
            rows = collections.defaultdict(set)
            for x, y in fr:
                rows[y].add(x)
            if not all(len(rows.get(y, ())) == 1 for y in EVEN):
                continue                                # incomplete skeleton
            col = [next(iter(rows[y])) for y in EVEN]
            if [b - a for a, b in zip(col, col[1:])].count(0) == 7:
                continue                                # residual artefact
            yield mode, scale, raid, col, rows


def devqhp_zero_rate(trials=200000):
    """P(no lateral move) under devqhp's clamped-uniform draw."""
    zeros = total = 0
    for _ in range(trials):
        x = random.randint(1, 13)
        for _ in range(7):
            nx = random.randint(max(x - 5, 0), min(x + 5, 13))
            zeros += nx == x
            total += 1
            x = nx
    return zeros / total


def analyse_mazes(root):
    runs_on = collections.Counter()
    dx = collections.Counter()
    ends = collections.Counter()
    starts = collections.Counter()
    seqs = []
    for _, _, _, col, rows in mazes(root):
        for y, xs in rows.items():
            if len(xs) > 1:
                runs_on["odd" if y % 2 else "even"] += 1
        d = [b - a for a, b in zip(col, col[1:])]
        seqs.append(d)
        for v in d:
            dx[v] += 1
        starts[col[0]] += 1
        ends[col[-1]] += 1

    n = sum(len(s) for s in seqs)
    z = sum(1 for s in seqs for v in s if v == 0)
    p, base = z / n, devqhp_zero_rate()
    se = math.sqrt(p * (1 - p) / n)
    pairs = sum(1 for s in seqs for a, b in zip(s, s[1:]) if a == 0 == b)
    pair_n = sum(len(s) - 1 for s in seqs)

    print(f"mazes {len(seqs)}   steps {n}")
    print(f"rows carrying a horizontal run: {dict(runs_on)}")
    print(f"max |dx| = {max(abs(k) for k in dx)}")
    print(f"no-lateral-move steps: {z}/{n} = {p:.1%} "
          f"(95% CI {p - 1.96 * se:.1%}-{p + 1.96 * se:.1%}); "
          f"devqhp's draw predicts {base:.1%}")
    print(f"  implied skipped-turn rate q = {(p - base) / (1 - base):.1%}")
    # A skipped turn (Zenyte's double-height vertical) forces exactly one zero
    # and can never be adjacent to another forced zero, so the two hypotheses
    # -- "a draw that favours 0" vs "an occasional skipped turn" -- predict
    # different rates of adjacent zero pairs.
    q = (p - base) / (1 - base)
    skip_share = q / p
    exp_indep = p * p * pair_n
    exp_skip = p * (skip_share * base + (1 - skip_share) * p) * pair_n
    print(f"  adjacent zero pairs {pairs}/{pair_n} = {pairs / pair_n:.1%}; "
          f"expected {exp_indep:.0f} if zeros are independent draws, "
          f"{exp_skip:.0f} under the skipped-turn model "
          f"(sd ~{math.sqrt(exp_indep):.0f}) -- weakly favours skipped turns")
    print(f"start column (first revealed row): {dict(sorted(starts.items()))}")
    print(f"far column   (last revealed row) : {dict(sorted(ends.items()))}")


def analyse_verzik(root):
    gaps = collections.defaultdict(collections.Counter)
    for f in sorted(glob.glob(os.path.join(root, "verzik", "*.json"))):
        try:
            ev = json.load(open(f))
        except ValueError:
            continue
        ph = {e["ph"]: e["tick"] for e in ev if e["type"] == 150 and e.get("ph")}
        if 3 not in ph:
            continue
        seen = collections.defaultdict(list)
        for e in ev:
            if e["type"] == 8 and e.get("id"):
                seen[e["id"]].append(e["tick"])
        atk = sorted(e["tick"] for e in ev if e["type"] == 10 and e.get("atk"))
        for phase in (2, 3):
            if phase not in ph:
                continue
            lo = ph[phase]
            hi = ph[3] if phase == 2 else 10 ** 9
            ids = [(min(t), i) for i, t in seen.items()
                   if lo <= min(t) < hi and len(t) > 20]
            hits = [t for t in atk if t >= lo and t < hi]
            if not ids or not hits:
                continue
            spawn = min(ids)[0]
            gaps[f"P{phase}: phase event -> npc id"][spawn - lo] += 1
            gaps[f"P{phase}: phase event -> first attack"][hits[0] - lo] += 1
            gaps[f"P{phase}: npc id -> first attack"][hits[0] - spawn] += 1
    for k in sorted(gaps):
        print(f"{k}: {dict(sorted(gaps[k].items()))}")


if __name__ == "__main__":
    root = sys.argv[1] if len(sys.argv) > 1 else "/tmp/blertmaze"
    analyse_mazes(root)
    print()
    analyse_verzik(root)
