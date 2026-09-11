#!/usr/bin/env python3
"""Measure how much damage one nylocas bite takes off a Theatre of Blood support.

Nothing publishes this number - not Jagex, not the wiki, not blert - so it is
measured here from recorded live raids instead.

The lever is that blert's Nylocas event stream carries a position for every wave
nylo on every tick, and the four supports are 2x2 blocks whose two room-facing
sides are the only tiles a nylo can stand on. "Cardinally adjacent to a support
and did not move this tick" is therefore a swing opportunity, and dividing by the
cache attack rate gives the number of bites a support took in a real room.

That alone is only a rate. What turns it into a damage figure is the collapse:
when a support falls, every nylo chewing it retargets the players on that same
tick and nobody ever chews it again, while the other three keep taking bites for
the rest of the room. So a support whose bite stream stops dead while the room
carries on has collapsed, and the bites it had taken by then ARE its hitpoints -
which Jagex published (330 solo, 130 at five, the straight line between).

Usage:
    measure_tob_pillar_damage.py harvest [scale] [n]   # throttled blert fetch
    measure_tob_pillar_damage.py                       # analyse what is cached

Streams are cached in /tmp/blertdata as <uuid>_12.json. Blert is volunteer-run
and the streams are ~300 KB each: the fetcher is fixed at one request per three
seconds and a faster crawl WILL be rate-limited.
"""
import collections, datetime, glob, json, os, statistics, sys, time, urllib.request

CACHE = "/tmp/blertdata"
BASE = "https://blert.io/api/v1"
ATTACKRATE = 3                                  # [cache] nylocas attack rate
QUIET_TICKS = 40                                # a support this far short of the
                                                # room's last bite has collapsed
# 2x2 support blocks, in world coords, read off the dwell histogram of the
# recorded rooms themselves rather than off the wiki's map pins.
PILLARS = {"SW": (3290, 3291, 4243, 4244), "NW": (3290, 3291, 4253, 4254),
           "SE": (3300, 3301, 4243, 4244), "NE": (3300, 3301, 4253, 4254)}
# Jagex, 21 June 2018 (330 solo / 130 five-man) and the line through them.
SUPPORT_HP = {5: 130, 4: 180, 3: 230, 2: 280, 1: 330}
BOSS_IDS = {8355, 8356, 8357}                   # Vasilias is not a wave nylo


def adjacent(x, z, big):
    """Which support this nylo is in melee reach of, if any.

    Melee in OSRS does not reach diagonally, so only the cardinal ring counts.
    A big nylocas is 2x2 with (x, z) its south-west tile.
    """
    tiles = [(x, z), (x + 1, z), (x, z + 1), (x + 1, z + 1)] if big else [(x, z)]
    for name, (x0, x1, z0, z1) in PILLARS.items():
        for tx, tz in tiles:
            if x0 <= tx <= x1 and z0 - 1 <= tz <= z1 + 1 and not z0 <= tz <= z1:
                return name
            if z0 <= tz <= z1 and x0 - 1 <= tx <= x1 + 1 and not x0 <= tx <= x1:
                return name
    return None


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "tob-research/1.0 (mechanics study)"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=90) as r:
                body = json.loads(r.read().decode())
            time.sleep(3.0)
            return body
        except Exception as exc:
            print("  retry", exc, flush=True)
            time.sleep(15 * (attempt + 1))
    return None


def harvest(scale, want):
    os.makedirs(CACHE, exist_ok=True)
    cursor, got = None, 0
    while got < want:
        q = f"{BASE}/challenges?limit=50&type=1&mode=11&scale=eq{scale}&status=eq1"
        if cursor:
            q += f"&startTime=lt{cursor}"
        listing = fetch(q)
        if not listing:
            return
        cursor = int(datetime.datetime.fromisoformat(
            listing[-1]["startTime"].replace("Z", "+00:00")).timestamp() * 1000)
        for c in listing:
            if got >= want:
                break
            path = f"{CACHE}/{c['uuid']}_12.json"
            if os.path.exists(path):
                got += 1
                continue
            stream = fetch(f"{BASE}/raids/tob/{c['uuid']}/events?stage=12")
            if stream is None:
                continue
            json.dump(stream, open(path, "w"))
            json.dump({"scale": c["scale"], "mode": c["mode"]}, open(path + ".meta", "w"))
            got += 1
            print(f"  scale {scale}  {c['uuid'][:8]}  {got}/{want}", flush=True)


def scale_of(uuid, ev):
    meta = f"{CACHE}/{uuid}_12.json.meta"
    if os.path.exists(meta):
        return json.load(open(meta))["scale"]
    # Fallback: a small nylo carries 11 hitpoints at five, 9 at four, 8 below.
    best = 0
    for e in ev:
        n = e.get("npc")
        if n and "nylo" in n and not n["nylo"]["big"]:
            best = max(best, n["hitpoints"] & 0xFFFF)
    return {11: 5, 9: 4, 8: 3}.get(best, 0)


def bites_per_support(ev, first_bite_on):
    """(support -> tick -> [small bites, big bites]) for one recorded room."""
    track, big = collections.defaultdict(dict), {}
    for e in ev:
        n = e.get("npc")
        if e["type"] not in (7, 8, 9) or not n or "roomId" not in n:
            continue
        if n.get("id") in BOSS_IDS:
            continue
        track[n["roomId"]][e.get("tick", 0)] = (e["xCoord"], e["yCoord"])
        if "nylo" in n:
            big[n["roomId"]] = n["nylo"]["big"]

    bites = collections.defaultdict(lambda: collections.defaultdict(lambda: [0, 0]))
    for rid, d in track.items():
        is_big = big.get(rid, False)
        ticks = sorted(d)
        parked_at, parked_for = None, 0
        for i, t in enumerate(ticks):
            here = adjacent(d[t][0], d[t][1], is_big)
            moved = i > 0 and d[ticks[i - 1]] != d[t]
            if here and not moved and here == parked_at:
                parked_for += 1
            else:
                parked_at, parked_for = (here, 1) if here else (None, 0)
            if parked_at and parked_for >= first_bite_on and \
                    (parked_for - first_bite_on) % ATTACKRATE == 0:
                bites[parked_at][t][1 if is_big else 0] += 1
    return bites


def analyse(first_bite_on):
    collapses, survivors, rooms = [], collections.defaultdict(list), collections.Counter()
    for f in sorted(glob.glob(f"{CACHE}/*_12.json")):
        uuid = os.path.basename(f)[:36]
        ev = json.load(open(f))
        if not isinstance(ev, list) or not ev:
            continue
        scale = scale_of(uuid, ev)
        if scale not in SUPPORT_HP:
            continue
        bites = bites_per_support(ev, first_bite_on)
        if len(bites) < 4:
            continue
        rooms[scale] += 1
        room_end = max(max(series) for series in bites.values())
        for support, series in bites.items():
            small = sum(v[0] for v in series.values())
            large = sum(v[1] for v in series.values())
            if room_end - max(series) >= QUIET_TICKS:
                collapses.append((uuid, scale, support, max(series), small, large))
            else:
                survivors[scale].append(small + large)
    return collapses, survivors, rooms


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "harvest":
        harvest(int(sys.argv[2]) if len(sys.argv) > 2 else 1,
                int(sys.argv[3]) if len(sys.argv) > 3 else 25)
        return

    print("A nylocas' first bite lands on the Nth tick it has been parked against a")
    print("support. N is not known, so the answer is reported across the plausible range.\n")
    print(f"{'N':>3} {'collapses':>10} {'median bites at collapse':>25} {'sd':>4} "
          f"{'DAMAGE PER BITE':>17}")
    headline = None
    for n in (1, 2, 3, 4):
        collapses, survivors, rooms = analyse(n)
        if not collapses:
            continue
        per = [SUPPORT_HP[s] / (sm + lg) for _, s, _, _, sm, lg in collapses]
        counts = [sm + lg for *_, sm, lg in collapses]
        print(f"{n:3} {len(collapses):10} {statistics.median(counts):25.0f} "
              f"{statistics.pstdev(counts):4.0f} {statistics.median(per):17.2f}")
        if n == 3:
            headline = (collapses, survivors, rooms, per)
    if not headline:
        print("\nno collapses in the cached streams - run `harvest 1 30` for solo rooms,")
        print("where the supports carry 330 hitpoints against the same wave table")
        return

    collapses, survivors, rooms, per = headline
    print(f"\nrooms analysed, by scale: {dict(rooms)}")
    print(f"\n{'raid':10} {'scale':>5} {'support':>7} {'tick':>5} {'small':>6} {'big':>5} "
          f"{'hp':>4} {'hp/bite':>8}")
    for uuid, scale, support, tick, small, large in collapses:
        print(f"{uuid[:8]:10} {scale:5} {support:>7} {tick:5} {small:6} {large:5} "
              f"{SUPPORT_HP[scale]:4} {SUPPORT_HP[scale] / (small + large):8.3f}")

    # Do big nylocas bite harder? Least squares on the collapse sums answers it
    # without needing to see a single hitsplat.
    sxx = sum(s * s for *_, s, b in collapses)
    sxy = sum(s * b for *_, s, b in collapses)
    syy = sum(b * b for *_, s, b in collapses)
    sxh = sum(s * SUPPORT_HP[sc] for _, sc, _, _, s, b in collapses)
    syh = sum(b * SUPPORT_HP[sc] for _, sc, _, _, s, b in collapses)
    det = sxx * syy - sxy * sxy
    print(f"\nuniform damage       : {statistics.median(per):.2f} per bite "
          f"(sd {statistics.pstdev(per):.3f}, n {len(per)})")
    if det:
        ds = (sxh * syy - syh * sxy) / det
        db = (sxx * syh - sxy * sxh) / det
        print(f"small and big apart  : small {ds:.2f}, big {db:.2f}  -> big/small {db / ds:.2f}")
    print("\nrate at which the worst-hit support in a room would fall, per candidate damage")
    print(f"{'scale':>5} {'rooms':>6} " + "".join(f"{d:>8}" for d in (0.9, 1.0, 1.5, 2.0, 3.0)))
    worst = collections.defaultdict(list)
    for _, sc, _, _, s, b in collapses:
        worst[sc].append(s + b)
    for sc, v in survivors.items():
        worst[sc] += v
    for sc in sorted(worst, reverse=True):
        v = worst[sc]
        row = f"{sc:5} {len(v):6} "
        for d in (0.9, 1.0, 1.5, 2.0, 3.0):
            row += f"{100 * sum(1 for w in v if w * d >= SUPPORT_HP[sc]) / len(v):7.0f}%"
        print(row)


if __name__ == "__main__":
    main()
