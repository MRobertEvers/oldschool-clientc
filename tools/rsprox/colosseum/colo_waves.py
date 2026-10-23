#!/usr/bin/env python3
"""Wave structure across Colosseum transcripts: tick-0 spawns, later spawns
(reinforcements), Minimus, the intermission, the start click.

    python3 tools/rsprox/colosseum/colo_waves.py spawns <transcript>...     # per wave: every arena add, tick from "Wave: N"
    python3 tools/rsprox/colosseum/colo_waves.py table <transcript>...      # multiset per wave + reinforcement ticks
    python3 tools/rsprox/colosseum/colo_waves.py intermission <transcript>...
"""
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rsprox_colosseum import load, waves, expand  # noqa: E402

ARENA = (1792, 3072, 1855, 3135)
SHORT = {
    12814: "archer", 12815: "seer", 12816: "berserker", 12811: "shaman", 12810: "jaguar",
    12817: "javelin", 12819: "shockwave", 12812: "minotaur", 12813: "minotaur_rf", 12818: "manticore",
    12821: "sol", 12827: "sol_seated", 12820: "pillar_npc", 12822: "doom_scorpion", 12823: "bees",
    12824: "beam_crystal", 12825: "totem", 12826: "solar_flare", 12808: "minimus", 12807: "minimus_out",
    12809: "gloria", 12828: "gib_human", 12829: "gib_manticore", 12830: "gib_minotaur",
    12831: "gib_colossi", 12832: "gib_sol", 12833: "supporter", 12834: "gladiator1", 12835: "gladiator2",
    12836: "gladiator3",
}


def in_arena(c):
    return c and ARENA[0] <= c[0] <= ARENA[2] and ARENA[1] <= c[1] <= ARENA[3]


def sid(path):
    return os.path.basename(path).replace(".txt", "")


def wave_windows(d):
    """Each wave with t_end = the next wave's t0 (or end of data)."""
    ws = waves(d)
    last = d["events"][-1]["t"] if d["events"] else 0
    for i, w in enumerate(ws):
        w["t_end"] = ws[i + 1]["t0"] if i + 1 < len(ws) else last
    return ws


def adds_in(d, t0, t1):
    out = []
    for e in d["events"]:
        if e["t"] < t0 or e["t"] >= t1:
            continue
        if e["k"] == "npc" and e["mv"] == "add" and in_arena(e["c"]):
            seq = [b[1].get("id") for b in e["b"] if b[0] == "sequence"]
            out.append((e["t"], e["nid"], tuple(e["c"]), e.get("angle"), seq[0]["id"] if seq and isinstance(seq[0], dict) else None, e["idx"]))
    return out


def cmd_spawns(paths):
    for p in paths:
        d = load(p)
        for w in wave_windows(d):
            for t, nid, c, ang, seq, idx in adds_in(d, w["t0"], w["t_done"] or w["t_end"]):
                print(f"{sid(p)}\trun{w['run']}\tw{w['wave']}\t+{t - w['t0']}\t{SHORT.get(nid, nid)}\t{c[0]},{c[1]}\t{ang}\t{seq}")


def cmd_table(paths):
    sets = defaultdict(Counter)
    reinf = defaultdict(Counter)
    n = Counter()
    act = defaultdict(Counter)
    for p in paths:
        d = load(p)
        a = d["header"].get("activity") or "-"
        for w in wave_windows(d):
            if w["t_done"] is None and w["wave"] != 12:
                continue
            n[w["wave"]] += 1
            act[w["wave"]][a.split(":")[0]] += 1
            ad = adds_in(d, w["t0"], w["t_done"] or w["t_end"])
            t0set = tuple(sorted(SHORT.get(nid, str(nid)) for t, nid, *_ in ad if t == w["t0"]))
            sets[w["wave"]][t0set] += 1
            for t, nid, *_ in ad:
                if t != w["t0"]:
                    reinf[w["wave"]][(t - w["t0"], SHORT.get(nid, str(nid)))] += 1
    for wv in sorted(n):
        print(f"== wave {wv}: {n[wv]} completed  {dict(act[wv])}")
        for s, c in sets[wv].most_common():
            print(f"   t0 x{c}: {' '.join(s)}")
        for (dt, nm), c in sorted(reinf[wv].items()):
            print(f"   +{dt} {nm} x{c}")


def cmd_intermission(paths):
    """Per wave end: the done tick D, Minimus add, modal open, choice clicks, confirm, next wave."""
    for p in paths:
        d = load(p)
        ws = wave_windows(d)
        ev = d["events"]
        for i, w in enumerate(ws):
            if w["t_done"] is None or i + 1 >= len(ws) or ws[i + 1]["run"] != w["run"]:
                continue
            D = w["t_done"]
            N = ws[i + 1]["t0"]
            rows = []
            for e in ev:
                if e["t"] < D or e["t"] > N:
                    continue
                if e["k"] == "npc" and e["nid"] == 12808 and e["mv"] in ("add", "del"):
                    rows.append(f"{e['mv']}minimus@{e['t'] - D}{tuple(e['c'][:2])}")
                elif e["k"] == "ui" and e["op"] in ("if_opensub", "if_closesub") and "colosseum" in e["raw"]:
                    rows.append(f"{e['op'][3:]}@{e['t'] - D}")
                elif e["k"] == "ui" and e["op"] == "if_buttonx" and "colosseum" in e["raw"]:
                    rows.append(f"click:{e['raw'].split(' ')[0].split(':')[-1]}@{e['t'] - D}")
                elif e["k"] == "ui" and e["op"].startswith("opnpc") and "colosseum_master" in e["raw"]:
                    rows.append(f"{e['op']}@{e['t'] - D}")
            print(f"{sid(p)}\tw{w['wave']}->{ws[i + 1]['wave']}\tD={D}\tnext=+{N - D}\t" + " ".join(rows))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return
    cmd, paths = sys.argv[1], expand(sys.argv[2:])
    {"spawns": cmd_spawns, "table": cmd_table, "intermission": cmd_intermission}[cmd](paths)


if __name__ == "__main__":
    main()


RANKV = {0: 4588, 1: 9792, 2: 9791, 3: 9799, 4: 9790, 5: 9798, 8: 10681, 10: 9797, 11: 9795, 12: 9796}
MODNAME = {0: "mantimayhem", 1: "reentry", 2: "bees", 3: "volatility", 4: "blasphemy", 5: "relentless",
           6: "quartet", 7: "totemic", 8: "doom", 9: "dynamic_duo", 10: "solarflare", 11: "myopia",
           12: "frailty", 13: "red_flag"}


def waves_with_mods(d):
    """wave_windows(d) with w['mods'] = {mod index: level} held through the wave: the
    rank varbits at the wave's start for levelled ones, the next 4931 mask for the rest."""
    import re
    ws = wave_windows(d)
    vb = {}
    marks = []   # (tick, ranks dict)
    masks = []   # (tick, mask)
    for e in d["events"]:
        if e["k"] == "var" and e.get("vk") == "varbit":
            vb[e["id"]] = e["new"]
        elif e["k"] == "cs" and e["id"] == 4931:
            m = re.search(r"values=\[(.*)\]", e["raw"])
            if m:
                v = [int(x) for x in m.group(1).split(", ")]
                masks.append((e["t"], v[7]))
        elif e["k"] == "msg":
            marks.append((e["t"], {i: vb.get(v, 0) for i, v in RANKV.items()}))
    for w in ws:
        ranks = {}
        for t, r in marks:
            if t <= w["t0"]:
                ranks = r
        mask = None
        for t, m in masks:
            if t > w["t0"]:
                mask = m
                break
        mods = {i: l for i, l in ranks.items() if l > 0}
        if mask is not None:
            for i in range(14):
                if mask & (1 << i) and i not in mods:
                    mods[i] = 1
        w["mods"] = mods
    return ws
