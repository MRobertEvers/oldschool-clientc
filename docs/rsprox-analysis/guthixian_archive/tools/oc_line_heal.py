#!/usr/bin/env python3
"""Ocellus's phase 1 from --all-players transcripts: the raiders' line heal
(ocellus_plan.md §2c) and how each corner is reached.

    python3 oc_line_heal.py idx4301.txt idx4308.txt ...

heal: every raider heal splat (hitsplat 6) against the lines (a 3..5 chip on
a crack's closing tick) that raider took since their previous heal; the tick
it came on (their own attack?), and lone melee swings with lines banked
against the splat on him the tick after.
arrival: for each corner, the walk's first step S and last L, each volley's
seq (`_start` s / `_end` E) and his distance from the corner then.
"""
import re, collections, pickle, os, sys
TICK = re.compile(r"^\[(\d+)\]")
PL = re.compile(r"^ {8}\[(\w+)\] player=\(index=\d+, name=([^,]+), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?")
NPC = re.compile(r"^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+)")
HB = re.compile(r"^ {12}\[headbar\] id=0, fill=(\d+)")
HIT = re.compile(r"^ {12}\[hit\] id=(\d+), value=(\d+)")
SEQ = re.compile(r"^ {12}\[sequence\] id=(\S+)")
PROJSRC = re.compile(r"^ {8}\[source\] .*player=\(index=\d+, name=([^,]+)")
PROJ = re.compile(r"^ {4}\[(?:map_projanim\w*|scriptedproj_add)\] id=(\S+) .*?starttime=(\d+), endtime=(\d+)")
def parse(path):
    T = {}
    cur = None; who = None; p1 = False; started = None; ended = None; lastproj = None
    for line in open(path, errors="replace"):
        m = TICK.match(line)
        if m:
            t = int(m.group(1)); cur = T.setdefault(t, {"pl": {}, "oc": {"hits": [], "seqs": []}, "ends": 0, "proj": []}); who = None; continue
        if cur is None: continue
        if "id=tfa_ocellus_p1 " in line and started is None: started = t
        if started is not None and ended is None and ("id=tfa_ocellus_p2" in line): ended = t
        m = PL.match(line)
        if m:
            who = ("pl", m.group(2)); d = cur["pl"].setdefault(m.group(2), {"c": (int(m.group(3)), int(m.group(4))), "nc": None, "hits": [], "seq": None, "mv": m.group(1)})
            if m.group(5): d["nc"] = (int(m.group(5)), int(m.group(6)))
            continue
        m = NPC.match(line)
        if m:
            who = ("oc",) if m.group(3).startswith("tfa_ocellus") else ("npc",); continue
        if line.startswith("    ["):
            who = None
            if "floor_crack" in line and "_end" in line: cur["ends"] += 1
            m = PROJ.match(line)
            lastproj = [m.group(1), int(m.group(2)), int(m.group(3)), None] if m else None
            if m: cur["proj"].append(lastproj)
            continue
        m = PROJSRC.match(line)
        if m and lastproj is not None: lastproj[3] = m.group(1)
        if who is None: continue
        m = HIT.match(line)
        if m:
            if who[0] == "pl": cur["pl"][who[1]]["hits"].append((int(m.group(1)), int(m.group(2))))
            elif who[0] == "oc": cur["oc"]["hits"].append((int(m.group(1)), int(m.group(2))))
            continue
        m = HB.match(line)
        if m and who[0] == "pl":
            cur["pl"][who[1]].setdefault("hb", []).append(int(m.group(1))); continue
        m = SEQ.match(line)
        if m:
            if who[0] == "pl": cur["pl"][who[1]]["seq"] = m.group(1)
            elif who[0] == "oc": cur["oc"]["seqs"].append(m.group(1))
    out = {"T": T, "t0": started, "t1": ended}
    return out

DEF = ("def", "block", "parry")
MEL = ("mace", "obligator", "scythe", "kisten")
MID = (4127, 5673)


def is_attack(s):
    return s is not None and s != "-1" and not any(k in s for k in DEF)


def heal_report(paths):
    by_lines = collections.defaultdict(collections.Counter)
    unbanked = []
    on_attack = collections.Counter()
    swing = collections.Counter()
    for p in paths:
        d = parse(p)
        T = d["T"]
        bank = collections.defaultdict(list)
        for t in range(d["t0"], d["t1"] + 1):
            r = T.get(t)
            if not r:
                continue
            for n, pl in r["pl"].items():
                # the line closes before the raider's own turn
                bank[n] += [v for (k, v) in pl["hits"] if k in (16, 17) and 3 <= v <= 5 and r["ends"]]
                heal = sum(v for (k, v) in pl["hits"] if k == 6)
                s = pl["seq"]
                if is_attack(s) and any(k in s for k in MEL) and bank[n]:
                    solo = sum(1 for q in r["pl"].values() if is_attack(q["seq"]) and any(k in q["seq"] for k in MEL)) == 1
                    nxt = [h for h in T.get(t + 1, {"oc": {"hits": []}})["oc"]["hits"] if h[0] != 6]
                    if solo:
                        swing[("healed" if heal else "not", "damage after" if any(h[1] > 0 for h in nxt) else "0 only" if nxt else "nothing")] += 1
                if heal:
                    on_attack[is_attack(s)] += 1
                    if not bank[n]:
                        unbanked.append((os.path.basename(p), n, t, heal))
                    hb = pl.get("hb", [])
                    if bank[n] and hb and hb[0] <= 27:
                        by_lines[len(bank[n])][heal] += 1
                    bank[n] = []
    print("heals on the raider's attack tick:", dict(on_attack))
    print("heals with no line banked:", unbanked)
    print("lone melee swings with lines banked (healed?, his splat the tick after):", dict(swing))
    for k in sorted(by_lines):
        c = by_lines[k]
        tot = sum(c.values())
        print(f"  {k} line(s), raider off full: n {tot} mean {sum(a * b for a, b in c.items()) / tot:.2f}", sorted(c.items()))


def arrival_report(paths):
    for p in paths:
        tick = 0
        cur = None
        pos = {}
        vol = []
        mv = []
        for line in open(p, errors="replace"):
            m = TICK.match(line)
            if m:
                tick = int(m.group(1))
                continue
            m = re.match(r"^ {8}\[(\w+)\] npc=\(index=\d+, id=(\w+) \(\d+\), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?", line)
            if m:
                cur = m.group(2)
                if cur == "tfa_ocellus_p1":
                    a = (int(m.group(3)) - MID[0], int(m.group(4)) - MID[1])
                    b = (int(m.group(5)) - MID[0], int(m.group(6)) - MID[1]) if m.group(5) else a
                    pos[tick] = b
                    if m.group(1) in ("walk", "run"):
                        mv.append(tick)
                continue
            if line.startswith("    ["):
                cur = None
            if cur == "tfa_ocellus_p1" and "[sequence]" in line and "basic_projectile" in line:
                vol.append((tick, "E" if "_end" in line else "s"))
        walks = []
        w = []
        for e in mv:
            if w and e - w[-1] > 2:
                walks.append(w)
                w = []
            w.append(e)
        if w:
            walks.append(w)
        for w in walks:
            S, L = w[0], w[-1]
            dest = pos[L]
            out = []
            for t, k in vol:
                if S - 1 <= t <= L + 6:
                    q = pos[max(u for u in pos if u <= t)]
                    out.append(f"S+{t - S}{k}(d{max(abs(q[0] - dest[0]), abs(q[1] - dest[1]))})")
            print(os.path.basename(p), "to", dest, "steps", len(w), " ".join(out))


if __name__ == "__main__":
    heal_report(sys.argv[1:])
    arrival_report(sys.argv[1:])
