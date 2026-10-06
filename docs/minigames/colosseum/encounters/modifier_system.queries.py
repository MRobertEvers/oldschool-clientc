#!/usr/bin/env python3
"""Queries behind encounters/modifier_system.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/modifier_system.queries.py
Reads sources/blert_api/*.json: overview.colosseum.waves[i] = {stage, handicap (the choice read for that wave),
options (the three offered), offset}; event 201 = DOOM hitsplat, 203 = pool object, 202 = totem heal.
An id is idx + 30 x (level - 1); idx = enum_5312 index (cache_enums_dbrows.txt:726-745).
Q1 offer shape (three distinct idx, the choice among them); Q2 option classes against the held set
(upgrade of a held one / new / illegal); Q3 per-wave offer composition; Q4 wave-12 exclusions;
Q5 Doom hitsplats per wave against held Doom level; Q6 pool events against held Reentry level;
Q7 minotaur 12813 spawns against held Red Flag."""
import collections, glob, json, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
MAXLV = {0:3,1:3,2:3,3:3,4:3,5:3,6:1,7:1,8:3,9:1,10:3,11:3,12:3,13:1}
runs = {os.path.basename(f)[:8]: json.load(open(f)) for f in sorted(glob.glob(API + "/*.json"))}
def split(i): return i % 30, i // 30 + 1
def main():
    shape = collections.Counter(); cls = collections.Counter(); per = collections.defaultdict(collections.Counter)
    chosen_in = collections.Counter(); w12 = collections.Counter(); upg_avail = collections.Counter()
    doom = collections.defaultdict(list); pool = collections.defaultdict(list); mino = []; poolx = collections.Counter()
    waves_n = 0
    for run, d in runs.items():
        held = {}
        for w in d["overview"]["colosseum"]["waves"]:
            n = w["stage"] - 99; opts = w["options"]; ch = w["handicap"]; waves_n += 1
            idxs = [split(o)[0] for o in opts]
            shape[(len(opts), len(set(idxs)))] += 1
            chosen_in[ch in opts] += 1
            ups = [i for i, l in held.items() if l < MAXLV[i]]
            if n >= 2: upg_avail[(len(ups) > 0, sum(1 for o in opts if split(o)[0] in held and split(o)[1] == held[split(o)[0]] + 1) > 0)] += 1
            for o in opts:
                i, l = split(o)
                if i in held:
                    c = "upgrade" if l == held[i] + 1 and l <= MAXLV[i] else "ILLEGAL_held"
                else:
                    c = "new_level1" if l == 1 else "ILLEGAL_new_above_1"
                cls[(c if n >= 2 else "wave1_" + c)] += 1; per[n][c] += 1
                if n == 12: w12[i] += 1
            if n >= 2 and len(ups): 
                k = sum(1 for o in opts if split(o)[0] in held and split(o)[1] == held[split(o)[0]] + 1)
                upg_avail[("upgradeable_held=%d" % len(ups), "upgrades_offered=%d" % k)] += 1
            if ch >= 0 and ch in opts:
                i, l = split(ch); held[i] = l
            evs = d["waves"].get(str(n), [])
            dl = held.get(8, 0); doom[dl].append(sum(1 for e in evs if e["type"] == 201))
            poolx[(held.get(1, 0), held.get(3, 0), any(e['type'] == 203 for e in evs))] += 1
            pool[held.get(1, 0)].append(sum(1 for e in evs if e["type"] == 203))
            mino.append((held.get(13, 0), sum(1 for e in evs if e["type"] == 7 and e["npc"]["id"] == 12813),
                         sum(1 for e in evs if e["type"] == 7 and e["npc"]["id"] == 12812)))
    print("Q0 runs", len(runs), "wave offers", waves_n)
    print("Q1 (n options, n distinct idx):count", dict(shape), " choice in options:", dict(chosen_in))
    print("Q2 option classes (wave>=2):", dict(cls))
    print("Q2b (any upgradeable held, any upgrade offered):count and (upgradeable, offered) detail", dict(upg_avail))
    print("Q4 wave-12 option idx counts", dict(sorted(w12.items())), "(0 Mantimayhem, 1 Reentry, 9 Dynamic Duo, 13 Red Flag are absent in the post/wiki)")
    print("Q5 Doom: held level -> [per wave hitsplat counts]")
    for k, v in sorted(doom.items()): print("   level", k, "waves", len(v), "max", max(v), "min", min(v), "mean %.1f" % (sum(v)/len(v)))
    print("Q6 pool events by held Reentry level:")
    for k, v in sorted(pool.items()): print("   level", k, "waves", len(v), "waves with pools", sum(1 for x in v if x), "max", max(v))
    print("Q6b waves by (held Reentry level, held Volatility level, any pool event):", dict(sorted(poolx.items(), key=str)))
    print("Q7 (held Red Flag, 12813 spawns, 12812 spawns) waves with any 12813:", [m for m in mino if m[1]])
    print("Q7b waves with a minotaur 12812 by held Red Flag:", collections.Counter((m[0], m[2] > 0) for m in mino))
def q8():
    """Q8 healing totem 12825 spawns: hitpoints packed (current << 16) | base; fraction of every other npc at the tick before spawn."""
    life = []; frac = []; runs_held = 0; bees = 0; scorp = 0; tot_waves = collections.Counter()
    for run, d in runs.items():
        held = {}
        for w in d["overview"]["colosseum"]["waves"]:
            n = w["stage"] - 99; ch = w["handicap"]; held[ch % 30] = ch // 30 + 1
            ev = d["waves"].get(str(n), [])
            bees += sum(1 for e in ev if "npc" in e and e["npc"]["id"] == 12823); scorp += sum(1 for e in ev if "npc" in e and e["npc"]["id"] == 12822)
            sp = [e for e in ev if e["type"] == 7 and e["npc"]["id"] == 12825]; dt = [e for e in ev if e["type"] == 9 and e["npc"]["id"] == 12825]
            tot_waves[(held.get(7, 0), len(sp) > 0)] += 1
            for a in sp:
                b = [x for x in dt if x["tick"] >= a["tick"] and x["xCoord"] == a["xCoord"] and x["yCoord"] == a["yCoord"]]
                if b: life.append(min(x["tick"] for x in b) - a["tick"])
                last = {}
                for x in ev:
                    if x["type"] == 8 and x["tick"] < a["tick"] and x["npc"]["id"] not in (12825, 12824): last[x["npc"]["roomId"]] = (x["tick"], x["npc"]["id"], x["npc"]["hitpoints"])
                dead = {x["npc"]["roomId"] for x in ev if x["type"] == 9 and x["tick"] < a["tick"]}
                cand = [((h >> 16) / max(1, h & 0xffff), nid) for rid, (_, nid, h) in last.items() if rid not in dead and (h >> 16) > 0]
                best = min(cand) if cand else None
                frac.append(best)
    print("Q8 (held Totemic, any totem spawn) waves:", dict(tot_waves))
    print("Q8 totem lifetimes spawn->death (ticks), n=%d:" % len(life), sorted(life))
    print("Q8 lowest hitpoint fraction among npcs alive (not yet dead, hp>0) at the tick before each totem spawn (fraction, npc id):", [(round(f[0], 2), f[1]) for f in frac if f])
    print("Q8 bee swarm 12823 and doom scorpion 12822 events in the sample:", bees, scorp)
def q9():
    """Q9 Quartet (idx 6) and Dynamic Duo (idx 9): tick-0 Fremennik (12814 archer, 12815 seer, 12816 berserker) and shockwave (12819) spawns per wave by held modifier."""
    fre = collections.Counter(); sw = collections.Counter(); tiles = []
    for run, d in runs.items():
        held = {}
        for w in d["overview"]["colosseum"]["waves"]:
            n = w["stage"] - 99; ch = w["handicap"]; held[ch % 30] = ch // 30 + 1
            ev = d["waves"].get(str(n), [])
            if n == 12: continue
            f0 = sorted(e["npc"]["id"] for e in ev if e["type"] == 7 and e["tick"] == 0 and e["npc"]["id"] in (12814, 12815, 12816))
            fre[(bool(held.get(6)), len(f0))] += 1
            if n in (7, 8, 11):
                s0 = [(e["xCoord"], e["yCoord"]) for e in ev if e["type"] == 7 and e["tick"] == 0 and e["npc"]["id"] == 12819]
                sw[(bool(held.get(9)), n, len(s0))] += 1
                if held.get(9): tiles.append((run, n, s0))
    print("Q9 (Quartet held, tick-0 Fremennik count):waves", dict(sorted(fre.items())))
    print("Q9 (Dynamic Duo held, wave, tick-0 shockwave count):waves", dict(sorted(sw.items())))
    print("Q9 Dynamic Duo waves' shockwave tick-0 tiles:", tiles)
if __name__ == "__main__": main(); q8(); q9()
