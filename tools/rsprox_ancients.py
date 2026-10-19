#!/usr/bin/env python3
"""Every Ancient Magicks combat cast the RSProx recorder made, and what Jagex's
server sent with it: the caster's seq and graphics, the projectile, the impact
graphic on each npc it struck, the synth and area sounds, the splash, and the
tick the hitsplat lands on.

    python3 tools/rsprox_ancients.py ~/.rsprox/binary/archive/*.txt
    python3 tools/rsprox_ancients.py --casts <txt>...      # one line per cast as well

A CAST is a tick on which the recorder's own player starts one of the zaros cast
seqs (1978/1979, their walkmerge forms 10091/10092, or 7753). The spell is named
by what flies or lands on that tick: an Ancient travel graphic, an Ancient impact
graphic, or the impact sound (each spell has its own; a splash lands spotanim 85
and `spellfail`). The caster's own synth (ice_cast, blood_cast, ...) names the
element, which filters out other raiders' graphics on the same tick. The 363/367
compack names are swapped against the casts: Ice Burst lands 363, Ice Blitz 367. Other raiders' casts
also put graphics on npcs; only the faced npc is the primary target, and a
burst/barrage's other victims are the npcs that got the SAME impact with the
SAME delay on that tick.

Rev-227 transcripts print bare ids; they are named from the content's compacks
(tools/rsprox_tells.py `load_names`). map_projanim_v2 heights are four times the
script's (172 = 43); they are reported in script units.
"""
import argparse
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsprox_tells import load_names  # noqa: E402

CAST_SEQS = {1978, 1979, 7753, 10091, 10092}
SPLASH = 85
# spotanim ids by spell, from all.spotanim.compack's names. Ice Burst lands 363
# and Ice Blitz 367 in the compack's naming; the casts decide which spell each is.
TRAVEL = {360: "ice_rush", 366: "ice_burst", 368: "ice_barrage",
          372: "blood_rush", 374: "blood_blitz", 378: "shadow_rush", 380: "shadow_blitz",
          384: "smoke_rush", 386: "smoke_blitz", 388: "smoke_burst", 390: "smoke_barrage"}
IMPACT = {361: "ice_rush", 363: "ice_burst", 367: "ice_blitz", 369: "ice_barrage",
          373: "blood_rush", 375: "blood_blitz", 376: "blood_burst", 377: "blood_barrage",
          379: "shadow_rush", 381: "shadow_blitz", 382: "shadow_burst", 383: "shadow_barrage",
          385: "smoke_rush", 387: "smoke_blitz", 389: "smoke_burst", 391: "smoke_barrage",
          2072: "shadow_barrage", 2110: "shadow_barrage"}
ANCIENT_GFX = set(TRAVEL) | set(IMPACT) | {362, 364, 365, 370, 371}
# the caster's own synth names the element; the impact sound names the spell
CAST_SOUND = {171: "ice", 106: "blood", 178: "shadow", 183: "smoke", 184: "smoke"}
IMPACT_SOUND = {102: "blood_barrage", 104: "blood_blitz", 105: "blood_burst", 110: "blood_rush",
                168: "ice_barrage", 169: "ice_blitz", 170: "ice_burst", 173: "ice_rush",
                175: "shadow_barrage", 176: "shadow_blitz", 177: "shadow_burst", 179: "shadow_rush",
                180: "smoke_barrage", 181: "smoke_blitz", 182: "smoke_burst", 185: "smoke_rush"}

TICK = re.compile(r"^\[(\d+)\]$")
ID = re.compile(r"id=(?:([a-z0-9_]+) \()?(-?\d+)\)?")
ENTITY = re.compile(r"^\s{8}\[(\w+)\] (npc|player)=\(index=(\d+)")
COORD = re.compile(r"coord=\((\d+), (\d+), (\d+)\)")
KV = re.compile(r"(\w+)=(-?\d+)")
NPC_REF = re.compile(r"npc=\(index=(\d+), id=(?:[a-z0-9_]+ \()?(\d+)")


def num_id(text):
    m = ID.search(text)
    return int(m.group(2)) if m else None


def kv(text):
    return {k: int(v) for k, v in KV.findall(ID.sub("", text, count=1))}


def parse(path):
    """[(tick, events)]; an event is a dict with `kind`."""
    ticks = []
    me = None
    version = None
    events = None
    entity = None      # (kind, index, coord) of the last npc/player row
    top = None         # the last top-level packet event, for [source]/[target]
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            if me is None and line.startswith("local player index:"):
                me = int(line.split(":")[1])
                continue
            if version is None and line.startswith("version:"):
                version = int(float(line.split(":")[1]))
                continue
            m = TICK.match(line)
            if m and line.startswith("["):
                events = []
                ticks.append((int(m.group(1)), events))
                entity = top = None
                continue
            if events is None or not line.strip():
                continue
            indent = len(line) - len(line.lstrip(" "))
            body = line.strip()
            if indent == 4:
                entity = None
                kind = body[1:body.index("]")]
                rest = body[body.index("]") + 1:]
                top = None
                if kind.startswith("map_projanim"):
                    f = kv(rest)
                    scale = 4 if kind.endswith("v2") else 1
                    top = {"kind": "proj", "id": num_id(rest), "start": f.get("starttime"), "end": f.get("endtime"),
                           "angle": f.get("angle"), "progress": f.get("progress"),
                           "sh": f.get("startheight", 0) // scale, "eh": f.get("endheight", 0) // scale}
                    events.append(top)
                elif kind in ("synth_sound", "sound_area", "map_anim"):
                    f = kv(rest)
                    c = COORD.search(rest)
                    events.append({"kind": kind, "id": num_id(rest), "delay": f.get("delay", 0),
                                   "range": f.get("range"), "height": f.get("height", 0),
                                   "coord": tuple(map(int, c.groups())) if c else None})
                elif kind in ("opnpct", "opnpc2", "opplayert"):
                    events.append({"kind": kind, "text": rest})
                elif kind in ("varbit",):
                    f = kv(rest)
                    events.append({"kind": "varbit", "id": num_id(rest), "new": f.get("new")})
                elif kind == "message_game":
                    events.append({"kind": "message", "text": rest})
                continue
            if indent == 8:
                if top is not None and body.startswith("[source]"):
                    c = COORD.search(body)
                    top["src"] = tuple(map(int, c.groups())) if c else None
                    continue
                if top is not None and body.startswith("[target]"):
                    c = COORD.search(body)
                    top["dst"] = tuple(map(int, c.groups())) if c else None
                    n = NPC_REF.search(body)
                    top["npc"] = int(n.group(1)) if n else None
                    top["to_player"] = "player=" in body
                    continue
                m = ENTITY.match(line)
                if m:
                    c = COORD.search(body)
                    entity = (m.group(2), int(m.group(3)), tuple(map(int, c.groups())) if c else None,
                              num_id(body.split("index=", 1)[1]) if m.group(2) == "npc" else None)
                else:
                    entity = None
                continue
            if indent >= 12 and entity is not None:
                kind = body[1:body.index("]")] if body.startswith("[") else ""
                rest = body[body.index("]") + 1:] if body.startswith("[") else body
                ev = {"who": entity[0], "index": entity[1], "coord": entity[2], "type": entity[3]}
                if kind == "sequence":
                    ev.update(kind="seq", id=num_id(rest), **{k: v for k, v in kv(rest).items() if k == "delay"})
                elif kind == "spotanim":
                    f = kv(rest)
                    ev.update(kind="spotanim", id=num_id(rest), delay=f.get("delay", 0), height=f.get("height", 0),
                              slot=f.get("slot", 0))
                elif kind == "hit":
                    f = kv(rest)
                    ev.update(kind="hit", id=f.get("id") if "id=" in rest else num_id(rest), value=f.get("value"),
                              hdelay=f.get("delay", 0))
                elif kind == "face_pathingentity":
                    n = NPC_REF.search(rest)
                    ev.update(kind="face", npc=int(n.group(1)) if n else None)
                else:
                    continue
                events.append(ev)
    return me, version, ticks


def dist(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def element(spell):
    return spell.split("_")[0] if spell else None


def analyse(path, sink, casts_out, names):
    me, version, ticks = parse(path)
    if me is None:
        return
    name = os.path.basename(path)
    face = None
    by_tick = {t: ev for t, ev in ticks}
    order = [t for t, _ in ticks]
    pos = {t: i for i, t in enumerate(order)}
    for tick, events in ticks:
        for ev in events:
            if ev["kind"] == "face" and ev["who"] == "player" and ev["index"] == me:
                face = ev["npc"]
        cast_seq = [ev for ev in events if ev["kind"] == "seq" and ev["who"] == "player" and ev["index"] == me
                    and ev["id"] in CAST_SEQS]
        if not cast_seq:
            continue
        seq = cast_seq[0]["id"]
        mygfx = [(e["id"], e["height"], e["delay"]) for e in events
                 if e["kind"] == "spotanim" and e["who"] == "player" and e["index"] == me]
        synths = [(e["id"], e["delay"]) for e in events if e["kind"] == "synth_sound"]
        areas = [(e["id"], e["delay"], e["range"], e["coord"]) for e in events if e["kind"] == "sound_area"]
        elems = [CAST_SOUND[s] for s, _ in synths if s in CAST_SOUND]
        elem = elems[0] if elems else None
        named = None
        for e in events:
            if e["kind"] == "opnpct":
                m = re.search(r"magic_spellbook:([a-z_]+)", e["text"])
                if m:
                    named = m.group(1)
        if named and elem is None:
            elem = element(named)

        def ok(spell):
            return elem is None or element(spell) == elem

        projs = [e for e in events if e["kind"] == "proj" and e["id"] in TRAVEL and not e.get("to_player")
                 and ok(TRAVEL[e["id"]])]
        if face is not None and any(p.get("npc") == face for p in projs):
            projs = [p for p in projs if p.get("npc") == face]
        proj = projs[0] if projs else None
        # impact sounds: an area sound at the target, or (older revisions) a delayed synth
        isnd = [(IMPACT_SOUND[a[0]], a[1], "area") for a in areas if a[0] in IMPACT_SOUND and ok(IMPACT_SOUND[a[0]])]
        isnd += [(IMPACT_SOUND[s], d, "synth") for s, d in synths if s in IMPACT_SOUND and ok(IMPACT_SOUND[s])]
        npc_gfx = [e for e in events if e["kind"] == "spotanim" and e["who"] == "npc"
                   and ((e["id"] in IMPACT and ok(IMPACT[e["id"]])) or e["id"] == SPLASH)]
        land = None
        if proj is not None:
            land = proj["end"]
        elif isnd:
            land = Counter(d for _, d, _ in isnd).most_common(1)[0][0]
        elif npc_gfx:
            land = Counter(e["delay"] for e in npc_gfx if e["id"] != SPLASH).most_common(1)[0][0] \
                if any(e["id"] != SPLASH for e in npc_gfx) else npc_gfx[0]["delay"]
        hits = [e for e in npc_gfx if land is None or e["delay"] == land]
        primary = None
        for e in hits:
            if e["index"] == face:
                primary = e
        if primary is None and hits:
            primary = hits[0]
        spell = named
        if spell is None and primary is not None and primary["id"] in IMPACT:
            spell = IMPACT[primary["id"]]
        if spell is None and isnd:
            spell = Counter(sp for sp, d, _ in isnd if d == land).most_common(1)[0][0] \
                if any(d == land for _, d, _ in isnd) else isnd[0][0]
        if spell is None and proj is not None:
            spell = TRAVEL[proj["id"]]
        if spell is None and elem:
            spell = elem + ("_rush/blitz?" if seq in (1978, 10091) else "_burst/barrage?")
        splashed = primary is not None and primary["id"] == SPLASH
        others = [e for e in hits if primary is not None and e["index"] != primary["index"] and e["id"] != SPLASH]
        hit_after = None
        if primary is not None:
            i = pos[tick]
            for t in order[i + 1:i + 9]:
                hs = [e for e in by_tick[t] if e["kind"] == "hit" and e["who"] == "npc" and e["index"] == primary["index"]]
                if hs:
                    hit_after = (t - tick, hs[0]["value"], hs[0]["id"])
                    break
        row = {"file": name, "rev": version, "tick": tick, "seq": seq, "mygfx": mygfx, "spell": spell,
               "named": named, "splash": splashed, "primary": primary, "land": land,
               "proj": proj, "synths": synths, "areas": areas, "aoe": len(others),
               "aoe_ids": [o["id"] for o in others], "hit": hit_after, "face": face, "isnd": isnd}
        sink.append(row)
        if casts_out:
            print(fmt(row))


def any_caster(path, acc):
    """Every Ancient flight, impact graphic and impact sound at an npc, whoever cast
    it: another raider's casts show these too (their seq and cast synth do not)."""
    me, version, ticks = parse(path)
    name = os.path.basename(path)[:-4]
    for tick, events in ticks:
        ends = defaultdict(set)
        for e in events:
            if e["kind"] == "proj" and e["id"] in TRAVEL and not e.get("to_player"):
                sp = TRAVEL[e["id"]]
                acc[sp]["proj"]["start=%s angle=%s progress=%s heights=%d/%d" % (e["start"], e["angle"], e["progress"],
                                                                                e["sh"], e["eh"])] += 1
                acc[sp]["sessions"][name] += 1
                ends[sp].add(e["end"])
                if e["end"] is not None and e["start"] is not None:
                    acc[sp]["flight_mod"]["(end-start+5)%%10=%d" % ((e["end"] - e["start"] + 5) % 10)] += 1
        for e in events:
            if e["kind"] == "spotanim" and e["who"] == "npc" and e["id"] in IMPACT:
                sp = IMPACT[e["id"]]
                acc[sp]["impact"]["%d h%d" % (e["id"], e["height"])] += 1
                acc[sp]["sessions"][name] += 1
                acc[sp]["impact_delay"]["=proj end" if e["delay"] in ends[sp] else
                                        ("(46+10d)" if (e["delay"] - 46) % 10 == 0 else "d%d" % e["delay"])] += 1
            if e["kind"] == "sound_area" and e["id"] in IMPACT_SOUND:
                sp = IMPACT_SOUND[e["id"]]
                acc[sp]["sound"]["area %d r%s" % (e["id"], e["range"])] += 1
                acc[sp]["sound_delay"]["=proj end" if e["delay"] in ends[sp] else
                                       ("(46+10d)" if (e["delay"] - 46) % 10 == 0 else "d%d" % e["delay"])] += 1
                acc[sp]["sessions"][name] += 1


def fmt(r):
    p = r["proj"]
    pr = r["primary"]
    return "%s r%s t%-6d seq=%-5d spell=%-14s named=%-14s gfx=%s proj=%s impact=%s aoe=%d synth=%s area=%s hit=%s" % (
        r["file"], r["rev"], r["tick"], r["seq"], r["spell"], r["named"], r["mygfx"],
        None if not p else "%d s%s e%s a%s p%s h%d/%d d%s" % (p["id"], p["start"], p["end"], p["angle"], p["progress"],
                                                             p["sh"], p["eh"],
                                                             dist(p["src"], p["dst"]) if p.get("src") and p.get("dst") else "?"),
        None if not pr else "%d@%d+%d" % (pr["id"], pr["height"], pr["delay"]), r["aoe"], r["synths"],
        [(a[0], a[1], a[2]) for a in r["areas"]], r["hit"])


def summarise(rows, names):
    snd = names.get("sound", {})
    sp = names.get("spotanim", {})
    sq = names.get("seq", {})
    groups = defaultdict(list)
    for r in rows:
        groups[r["spell"] or "?"].append(r)
    for spell in sorted(groups, key=str):
        rs = groups[spell]
        print("=" * 100)
        print("%s: %d casts, %d splashed, sessions %s" % (spell, len(rs), sum(1 for r in rs if r["splash"]),
                                                          dict(Counter("%s(r%s)" % (r["file"][:-4], r["rev"]) for r in rs))))
        print("  named by opnpct:", dict(Counter(r["named"] for r in rs if r["named"])))
        print("  caster seq:", dict(Counter("%s (%d) r%s" % (sq.get(r["seq"], "?"), r["seq"], r["rev"]) for r in rs)))
        print("  caster gfx:", dict(Counter(tuple("%s (%d) h%d d%d" % (sp.get(g[0], "?"), g[0], g[1], g[2]) for g in r["mygfx"]) for r in rs)))
        pc = Counter()
        flights = Counter()
        for r in rs:
            p = r["proj"]
            if p:
                pc["%s (%d) start=%s angle=%s progress=%s heights=%d/%d" % (sp.get(p["id"], "?"), p["id"], p["start"], p["angle"],
                                                                          p["progress"], p["sh"], p["eh"])] += 1
                if p.get("src") and p.get("dst") and p["end"] is not None:
                    d = dist(p["src"], p["dst"])
                    flights[(d, p["end"] - p["start"])] += 1
            else:
                pc["(no projectile)"] += 1
        print("  projectile:", dict(pc))
        print("  flight (tiles, end-start):", dict(sorted(flights.items())))
        ic = Counter()
        land = Counter()
        for r in rs:
            pr = r["primary"]
            if pr:
                ic["%s (%d) h%d" % (sp.get(pr["id"], "?"), pr["id"], pr["height"])] += 1
                if r["proj"] and r["proj"]["end"] is not None:
                    land["delay-end=%d" % (pr["delay"] - r["proj"]["end"])] += 1
                else:
                    land["delay=%d" % pr["delay"]] += 1
        print("  impact on target:", dict(ic))
        print("  impact delay vs projectile end:", dict(land))
        print("  aoe extra victims:", dict(Counter(r["aoe"] for r in rs)))
        keep = set(CAST_SOUND) | set(IMPACT_SOUND) | {227}
        print("  synth (spell sounds only):", dict(Counter(tuple("%s (%d) d%s" % (snd.get(x, "?"), x,
                                                                         "L" if r["land"] is not None and d == r["land"] and d else d)
                                                                   for x, d in r["synths"] if x in keep) for r in rs)))
        ac = Counter()
        for r in rs:
            items = []
            pr = r["primary"]
            for a in r["areas"]:
                if a[0] not in keep:
                    continue
                where = "?"
                if pr and a[3] and pr["coord"]:
                    where = "target" if dist(a[3], pr["coord"]) <= 1 else "d%d" % dist(a[3], pr["coord"])
                when = "L" if r["land"] is not None and a[1] == r["land"] else "d%d" % a[1]
                items.append("%s (%d) %s r%s @%s" % (snd.get(a[0], "?"), a[0], when, a[2], where))
            ac[tuple(sorted(items))] += 1
        print("  sound_area (spell sounds only; L = the landing cycle):")
        for k, v in ac.most_common(8):
            print("     %4d  %s" % (v, list(k)))
        hc = Counter()
        for r in rs:
            if r["hit"] and r["proj"] and r["proj"]["end"] is not None:
                hc["+%d (end %d)" % (r["hit"][0], r["proj"]["end"])] += 1
            elif r["hit"]:
                hc["+%d" % r["hit"][0]] += 1
        print("  first hitsplat on target after cast (ticks):", dict(sorted(hc.items())))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("transcripts", nargs="+")
    ap.add_argument("--casts", action="store_true", help="print every cast")
    ap.add_argument("--all-casters", action="store_true",
                    help="inventory every Ancient flight/impact/impact sound at an npc, any caster")
    args = ap.parse_args()
    names = load_names()
    if args.all_casters:
        acc = defaultdict(lambda: defaultdict(Counter))
        for path in args.transcripts:
            any_caster(path, acc)
        for sp in sorted(acc):
            print("=" * 80)
            print(sp, "sessions:", dict(acc[sp]["sessions"].most_common(12)))
            for k in ("proj", "flight_mod", "impact", "impact_delay", "sound", "sound_delay"):
                if acc[sp][k]:
                    print("  %-12s %s" % (k, dict(acc[sp][k].most_common(8))))
        return
    rows = []
    for path in args.transcripts:
        analyse(path, rows, args.casts, names)
    summarise(rows, names)


if __name__ == "__main__":
    main()
