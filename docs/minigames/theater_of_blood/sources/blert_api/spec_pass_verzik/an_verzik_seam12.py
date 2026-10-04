#!/usr/bin/env python3
"""Seam12 (tob_verzik_p3_against_sources): two questions settled from the Blert Verzik pull.

Input: the raw stage-15 streams spec_pass_verzik/fetch_verzik*.py writes (62 raids, fetched
2026-10-02; not committed). Usage: python3 an_verzik_seam12.py <dir of raw streams>

(d) The blood spell under Protect from Magic. Casts are NPC_ATTACK 17 with a target. Kept: every
    raider within one tile of the target prays Protect from Magic (prayerSet bit 16) from the cast
    tick to +5, every raider's stream is present, Verzik's hitpoints are known T..T+5, and no
    non-Verzik npc dies T-1..T+6 (a Matomenos reaching her heals her too). Reported: the
    hitpoints those raiders lost T..T+5, and her hitpoints change on T+2 (the tick the heal shows).
(b) The enrage. Raids where her P3 hitpoints went back above 20 percent after the first tornado
    spawn; the P3 attack events (19 melee, 20 range, 21 mage, 18 = unidentified, the plugin's
    clock with no projectile seen) inside that window and their gaps.
"""
import collections, glob, json, os, sys

B = sys.argv[1]
PFM = 1 << 16
P2 = {8372, 10850, 10833}
P3 = {8374, 10852, 10835}
TORN = {8386, 10846, 10863}
files = sorted(glob.glob(os.path.join(B, "*.json")))
print("raids", len(files))
# ---- (d)
res = collections.Counter(); lost = collections.Counter(); heal2 = collections.defaultdict(list)
for f in files:
    ev = json.load(open(f)); mode = os.path.basename(f)[:2]
    vh = {}; ps = collections.defaultdict(dict)
    for e in ev:
        if e["type"] in (7, 8) and e["npc"]["id"] in P2 and e["npc"].get("hitpoints") is not None:
            vh[e["tick"]] = e["npc"]["hitpoints"] >> 16
        if e["type"] == 4:
            p = e["player"]; ps[p["name"]][e["tick"]] = (p.get("hitpoints"), p.get("prayerSet"), e["xCoord"], e["yCoord"])
    for e in ev:
        if not (e["type"] == 10 and e["npcAttack"].get("attack") == 17):
            continue
        T = e["tick"]; tgt = e["npcAttack"].get("target"); res["casts"] += 1
        if tgt not in ps or T not in ps[tgt]:
            res["skip_no_target_stream"] += 1; continue
        tx, ty = ps[tgt][T][2], ps[tgt][T][3]
        near = [n for n, s in ps.items() if T in s and max(abs(s[T][2] - tx), abs(s[T][3] - ty)) <= 1]
        if any(ps[n].get(t) is None or ps[n][t][1] is None or not (ps[n][t][1] & PFM) for n in near for t in range(T, T + 6)):
            res["skip_someone_unprayed_or_gap"] += 1; continue
        if not all(T in s and T + 5 in s for s in ps.values()):
            res["skip_not_all_streams"] += 1; continue
        if any(t not in vh for t in range(T, T + 6)):
            res["skip_no_verzik_hp"] += 1; continue
        if any(x["type"] == 9 and x["npc"]["id"] not in P2 and T - 1 <= x["tick"] <= T + 6 for x in ev):
            res["skip_npc_death_near"] += 1; continue
        res["kept"] += 1
        d = 0
        for n in near:
            hs = [ps[n][t][0] >> 16 for t in range(T, T + 6) if ps[n][t][0] is not None]
            d += sum(min(0, b - a) for a, b in zip(hs, hs[1:]))
        lost[d] += 1
        heal2[(mode, len(near))].append(vh[T + 2] - vh[T + 1])
print("(d) blood spell casts:", dict(res))
print("(d) hitpoints lost by the praying raiders beside the target, T..T+5:", sorted(lost.items()))
for k in sorted(heal2):
    pos = [h for h in heal2[k] if h > 0]
    if pos:
        print("(d) mode %s, %d raider(s) in the 3x3: %d casts, %d positive T+2 heals, mean %.1f, %d over 11, max %d, values %s"
              % (k[0], k[1], len(heal2[k]), len(pos), sum(pos) / len(pos), sum(1 for h in pos if h > 11), max(pos), sorted(set(pos))))
# ---- (b)
n_raid = 0; n_above = 0; unident = collections.Counter(); gaps_above = collections.Counter()
for f in files:
    ev = json.load(open(f)); name = os.path.basename(f)[:13]
    t0s = [e["tick"] for e in ev if e["type"] == 7 and e["npc"]["id"] in TORN]
    for e in ev:
        if e["type"] == 10 and e["npc"]["id"] in P3:
            unident[e["npcAttack"]["attack"]] += 1
    if not t0s:
        continue
    n_raid += 1; t0 = min(t0s)
    hp = {e["tick"]: (e["npc"]["hitpoints"] >> 16, e["npc"]["hitpoints"] & 0xFFFF) for e in ev
          if e["type"] in (7, 8) and e["npc"]["id"] in P3 and e["npc"].get("hitpoints")}
    ab = [t for t, (c, b) in sorted(hp.items()) if t >= t0 and b and 1000 * c > 200 * b]
    if not ab:
        continue
    n_above += 1
    atk = [(e["tick"], e["npcAttack"]["attack"]) for e in ev if e["type"] == 10 and e["npc"]["id"] in P3 and ab[0] - 10 <= e["tick"] <= ab[-1] + 10]
    plain = [(t, a) for t, a in atk if a in (18, 19, 20, 21)]
    for (ta, aa), (tb, ab_) in zip(plain, plain[1:]):
        gaps_above[tb - ta] += 1
    print("(b) %s tornado t%d, above 20%% on %d ticks (t%d..t%d, peak %.1f%%): attacks %s"
          % (name, t0, len(ab), ab[0], ab[-1], max(100.0 * hp[t][0] / hp[t][1] for t in ab), atk))
print("(b) raids with tornadoes %d; back above 20%% after the enrage in %d" % (n_raid, n_above))
print("(b) gaps between consecutive plain P3 attacks in those windows (specials between show as long gaps):", sorted(gaps_above.items()))
print("(b) P3 attack ids over all raids (18 = no projectile matched the plugin's predicted tick):", sorted(unident.items()))
