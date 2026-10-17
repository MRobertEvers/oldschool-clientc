#!/usr/bin/env python3
"""VASILIAS IN HARD MODE, from Blert: what her ranged splash and magic bounce
do to the raiders who are not the target, and where real teams stand across
her colour changes.

  tools/blert_vasilias_hard.py build/blert/nylocas_hard

Rooms: tools/blert_fetch_tob_rooms.py 12 <dir> <n> 12 5 (Hard Mode fives).
Blert carries every raider's tile every tick but the hitpoints of the
RECORDING raider only, so the damage measures are the recorder's: as a
bystander at each distance from her target, or as the target with others
around it. Damage is the sum of the recorder's hitpoint drops over the
three ticks after the attack, where her hits land (a heal in the same window
hides part of a hit, never adds one); a window an earlier attack on the
recorder could land in is skipped.

  splash   ranged attacks, recorder not the target: by Chebyshev distance
           from the target's tile (0 = stacked on it), how often and how
           much the recorder lost
  bounce   the same for the magic attack
  crowd    ranged attacks on the recorder: its loss by how many raiders
           stood within 2 / 3 of it
  premove  pairs of raiders (of ten) within 2 tiles of each other, by tick
           relative to her colour change, split by the form ending
  timing   the first attack of each form: after a change, by the form
           entered, and in the opening melee form - Vasilias and every
           Prinkipas (by its own room id) apart

  tools/blert_vasilias_hard.py build/blert/nylocas    (Regular trios: timing only)
"""
import collections
import glob
import json
import sys

# Hard Mode's ids, and Regular's (8354..8357) so a Regular set's timing reads too
VAS = {10807: "spawning", 10808: "mel", 10809: "mag", 10810: "rng",
       8354: "spawning", 8355: "mel", 8356: "mag", 8357: "rng"}
PRINCE = {10803: "spawning", 10804: "mel", 10805: "mag", 10806: "rng"}
ATTACK = {4: "mel", 5: "rng", 6: "mag"}
# Blert's prayerSet bits for the overheads (read off the recorders' sets in
# each of her forms: 0x40000 in melee, 0x20000 in ranged, 0x10000 in magic)
PROTECT = {"mag": 0x10000, "rng": 0x20000, "mel": 0x40000}


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def load(f):
    ev = sorted(json.load(open(f)), key=lambda e: e["tick"])
    pos = collections.defaultdict(dict)      # tick -> name -> tile
    hp = {}                                   # tick -> recorder hp
    prayers = {}                              # tick -> recorder prayerSet
    recorder = None
    form = {}                                 # tick -> Vasilias form
    attacks = []                              # (tick, npc kind, form, style, target)
    for e in ev:
        t = e["tick"]
        if e["type"] == 4:
            p = e["player"]
            pos[t][p["name"]] = (e["xCoord"], e["yCoord"])
            if p.get("source") == 0 and "hitpoints" in p:
                recorder = p["name"]
                hp[t] = p["hitpoints"] >> 16
                prayers[t] = p.get("prayerSet", 0)
        nid = (e.get("npc") or {}).get("id")
        if nid in VAS and e["type"] in (7, 8):
            form[t] = VAS[nid]
        if e["type"] == 10 and (nid in VAS or nid in PRINCE):
            kind = "vasilias" if nid in VAS else "prince"
            attacks.append((t, kind, (VAS if nid in VAS else PRINCE)[nid],
                            ATTACK.get(e["npcAttack"]["attack"]), e["npcAttack"].get("target")))
    return pos, hp, prayers, recorder, form, attacks


def loss(hp, t0, t1):
    """The recorder's hitpoint drops over (t0, t1]."""
    out, last = 0, None
    for t in range(t0, t1 + 1):
        if t in hp:
            if last is not None and hp[t] < last:
                out += last - hp[t]
            last = hp[t]
    return out


def main(d):
    splash = collections.defaultdict(list)    # (kind, style, dist) -> [loss]
    crowd = collections.defaultdict(list)     # (n within 2, n within 3) -> [loss]
    premove = collections.defaultdict(lambda: collections.defaultdict(list))
    first_after = collections.Counter()
    near = []
    last_before = collections.Counter()
    rooms = 0
    for f in sorted(glob.glob(d + "/*-*.json")):
        pos, hp, prayers, me, form, attacks = load(f)
        if me is None or not attacks:
            continue
        rooms += 1
        by_kind = collections.defaultdict(list)
        for a in attacks:
            by_kind[a[1]].append(a)
        mine = [a[0] for a in attacks if a[4] == me]
        for kind, al in by_kind.items():
            for i, (t, _, frm, style, target) in enumerate(al):
                if style is None or target is None:
                    continue
                # her hits land 1..3 ticks after the attack (the recorder's own
                # drops as her target), so a window that an earlier attack ON
                # the recorder could still be landing in is not a bystander's
                if target != me and any(t - 3 <= tm < t for tm in mine):
                    continue
                end = t + 3
                # where everyone stood when she chose: the end of the tick before
                P = pos.get(t - 1) or pos.get(t) or {}
                if target not in P or me not in P:
                    continue
                dmg = loss(hp, t, end)
                if target == me:
                    if kind == "vasilias" and style == "rng":
                        n2 = sum(1 for n, p in P.items() if n != me and cheb(p, P[me]) <= 2)
                        n3 = sum(1 for n, p in P.items() if n != me and cheb(p, P[me]) <= 3)
                        crowd[(n2, n3)].append(dmg)
                else:
                    dist = min(cheb(P[me], P[target]), 6)
                    splash[(kind, style, dist)].append(dmg)
                    prayed = bool(prayers.get(t, 0) & PROTECT[style])
                    if dist <= 3:
                        caught = sum(1 for n, q in P.items() if n != target and cheb(q, P[target]) <= 2)
                        near.append((kind, style, dist, prayed, dmg, caught, f.split("/")[-1][:8], t))
        # the colour changes and the team's spread around them
        seq = []
        for t in sorted(form):
            if not seq or seq[-1][1] != form[t]:
                seq.append((t, form[t]))
        coloured = [(t, c) for t, c in seq if c != "spawning"]
        vas_att = sorted(a[0] for a in attacks if a[1] == "vasilias")
        for (ta, ca), (tb, cb) in zip(coloured, coloured[1:]):
            for k in range(-6, 5):
                P = pos.get(tb + k)
                if not P or len(P) < 2:
                    continue
                names = sorted(P)
                close = sum(1 for i in range(len(names)) for j in range(i + 1, len(names))
                            if cheb(P[names[i]], P[names[j]]) <= 2)
                premove[ca + "->" + cb][k].append(close)
            att = [t for t in vas_att if t >= tb]
            if att:
                first_after[att[0] - tb] += 1
            before = [t for t in vas_att if ta <= t < tb]
            if before:
                last_before[tb - before[-1]] += 1
    print("rooms with a recorder and her attacks: %d" % rooms)
    print("\n== the recorder's loss as a BYSTANDER, by distance from the target (0 = on its tile)")
    for kind in ("vasilias", "prince"):
        for style in ("rng", "mag", "mel"):
            rows = [(dist, splash[(kind, style, dist)]) for dist in range(7) if splash[(kind, style, dist)]]
            if not rows:
                continue
            print("  %s %s" % (kind, style))
            for dist, L in rows:
                hit = sum(1 for x in L if x > 0)
                print("    dist %s%d  n %3d  hit %3d%%  mean %5.1f  max %3d" % (
                    ">=" if dist == 6 else "  ", dist, len(L), round(100 * hit / len(L)), sum(L) / len(L), max(L)))
    print("\n== every Vasilias ranged bystander within 3 of the target: (dist, praying Missiles, loss)")
    for kind, style, dist, prayed, dmg, caught, room, t in sorted(near):
        if kind == "vasilias" and style == "rng" and (dist <= 2 or dmg > 0):
            print("    dist %d  %-9s loss %3d  caught %d   %s t%d" % (
                dist, "prayed" if prayed else "unprayed", dmg, caught, room, t))
    print("\n== the recorder's loss as her RANGED target, by raiders within 2 / within 3 of it")
    for k in sorted(crowd):
        L = crowd[k]
        print("  within2 %d within3 %d  n %3d  mean %5.1f  max %3d" % (k[0], k[1], len(L), sum(L) / len(L), max(L)))
    print("\n== her first attack after a colour change (ticks)")
    n = sum(first_after.values())
    print("  " + "  ".join("+%d: %d" % (k, v) for k, v in sorted(first_after.items())) + "  (n %d)" % n)
    print("\n== her last attack in a form, ticks before the change")
    n = sum(last_before.values())
    print("  " + "  ".join("-%d: %d" % (k, v) for k, v in sorted(last_before.items())) + "  (n %d)" % n)
    print("\n== pairs of raiders within 2 tiles (mean of 10 at five), tick relative to the change")
    for key in sorted(premove):
        row = premove[key]
        cells = ["%+d:%.1f" % (k, sum(row[k]) / len(row[k])) for k in sorted(row)]
        print("  %-9s n %3d  %s" % (key, len(row[0]) if row[0] else 0, " ".join(cells)))


def timing(d):
    """First attack of each form, by npc (Vasilias, each Prinkipas apart)."""
    first = collections.defaultdict(collections.Counter)
    opening = collections.defaultdict(collections.Counter)
    for f in sorted(glob.glob(d + "/*-*.json")):
        forms = collections.defaultdict(dict)
        att = collections.defaultdict(list)
        for e in json.load(open(f)):
            n = e.get("npc") or {}
            nid = n.get("id")
            if nid not in VAS and nid not in PRINCE:
                continue
            kind = "vasilias" if nid in VAS else "prince"
            key = (kind, n.get("roomId"))
            if e["type"] in (7, 8):
                forms[key][e["tick"]] = (VAS if nid in VAS else PRINCE)[nid]
            if e["type"] == 10:
                att[key].append(e["tick"])
        for key, form in forms.items():
            seq = []
            for t in sorted(form):
                if not seq or seq[-1][1] != form[t]:
                    seq.append((t, form[t]))
            coloured = [(t, c) for t, c in seq if c != "spawning"]
            for i in range(len(coloured) - 1):
                ta, ca = coloured[i]
                tb = coloured[i + 1][0]
                a = sorted(t for t in att[key] if ta <= t < tb)
                if not a:
                    continue
                if i == 0:
                    opening[key[0]][a[0] - ta] += 1
                else:
                    first[(key[0], ca)][a[0] - ta] += 1
    print("\n== the first attack of a form (ticks after the form began)")
    for kind in ("vasilias", "prince"):
        if opening[kind]:
            print("  %-8s opening melee  %s" % (kind, "  ".join("+%d %d" % kv for kv in sorted(opening[kind].items()))))
        for style in ("mel", "mag", "rng"):
            c = first[(kind, style)]
            if c:
                print("  %-8s into %-6s  %s  (n %d)" % (kind, style, "  ".join("+%d %d" % kv for kv in sorted(c.items())),
                                                       sum(c.values())))


if __name__ == "__main__":
    main(sys.argv[1])
    timing(sys.argv[1])
