#!/usr/bin/env python3
"""Tormented Demon (Old School, 2024) behaviour from RSProx transcripts.

Splits every demon (npc 13599..13606) into lives -- spawn/summon to death -- and
measures, across all lives in the given transcripts:

  respawn     death -> del -> re-add (invisible) -> summon/transform
  engage      the shield drop (`explosion_fire` seq + spot) vs the local
              player's first attack on it
  prayer      each head-icon (sprite group 440) write: the sprite, the damage
              taken since the previous write
  shield      `shield_restore_spot` vs the player's attacks; `shield_spot` cadence
  body        `body_customisation` writes
  attacks     melee / firey_balls / spare_ribs: cadence, projectile parameters,
              distance, and the player-side sounds and graphics
  bombs       fireball_large projectiles (bind + fire bomb)

    python3 tools/rsprox/rsprox_td.py ~/.rsprox/binary/archive/{3312,1756,552}.txt [--lives N]

Rev 227 transcripts carry numeric ids; names come from our configs
(OSRS-Content/osrs239-content/configs/all.<type>.compack).
"""
import argparse
import os
import re
from collections import Counter, defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "OSRS-Content", "osrs239-content")
TD = {"13599", "13600", "13601", "13602", "13603", "13604", "13605", "13606"}

TICK = re.compile(r"^\[(\d+)\]$")
ME = re.compile(r"local player index: (\d+)")
NPC = re.compile(r"^\s+\[(add|del|idle|walk|run|teleport|crawl)\] npc=\(index=(\d+), (?:id=(?:[a-z0-9_]+ \((\d+)\)|(\d+))|[^,]*?\(id=(\d+)\)), coord=\((\d+), (\d+)")
PLAYER = re.compile(r"^\s+\[(?:add|del|idle|walk|run|teleport|crawl)\] player=\(index=(\d+), name=[^,]*, coord=\((\d+), (\d+)")
SEQ = re.compile(r"\[sequence\] id=(?:[a-z0-9_]+ \((\d+)\)|(\d+))(?:, delay=(\d+))?")
SPOT = re.compile(r"\[spotanim\] (?:slot=(\d+), )?id=(?:[a-z0-9_]+ \((\d+)\)|(\d+))(.*)$")
HIT = re.compile(r"\[hit\] id=(\d+), value=(\d+)")
HEADICON = re.compile(r"\[headicon\] headiconslot=(\d+), id=(\d+), spriteindex=(\d+)")
BODY = re.compile(r"\[body_customisation\]")
MODEL = re.compile(r"^\s+\[model\] id=(\d+)")
TRANSFORM = re.compile(r"\[transformation\] id=(?:[a-z0-9_]+ \((\d+)\)|(\d+))")
PROJ = re.compile(r"\[map_projanim_v[12]\] id=(?:[a-z0-9_]+ \((\d+)\)|(\d+)), starttime=(\d+), endtime=(\d+), angle=(\d+), progress=(\d+), startheight=(\d+), endheight=(\d+)")
SRC = re.compile(r"\[source\] coord=\((\d+), (\d+)")
DST = re.compile(r"\[target\] coord=\((\d+), (\d+), \d+\)(?:, (npc|player)=\(index=(\d+))?")
SYN = re.compile(r"\[synth_sound\] id=(\d+)(?:, delay=(\d+))?")
AREA = re.compile(r"\[sound_area\] id=(-?\d+), (.*?)coord=\((\d+), (\d+)")
MAPANIM = re.compile(r"\[map_anim\] id=(?:[a-z0-9_]+ \((\d+)\)|(\d+))(.*?)coord=\((\d+), (\d+)")


def table(kind):
    out = {}
    with open(os.path.join(ROOT, "configs", "all.%s.compack" % kind), errors="replace") as handle:
        for line in handle:
            k, _, v = line.rstrip("\n").partition("=")
            if k.isdigit():
                out[k] = v
    return out


SEQN = table("seq")
SPOTN = table("spotanim")


def scan(path, events, glob):
    """events[(file, npc index)] -> [(tick, kind, data)]; glob[(file, tick)] -> [...]"""
    me = None
    tick = 0
    section = None
    actor = None
    proj = None
    myxz = None
    for line in open(path, errors="replace"):
        m = ME.search(line)
        if m:
            me = m.group(1)
        s = line.strip()
        m = TICK.match(s)
        if m and line.startswith("["):
            tick = int(m.group(1))
            continue
        if s.startswith("[player_info]"):
            section = "p"
            actor = None
        elif s.startswith("[npc_info"):
            section = "n"
            actor = None
        m = PROJ.search(line)
        if m:
            proj = {"id": m.group(1) or m.group(2), "start": int(m.group(3)), "end": int(m.group(4)),
                    "peak": int(m.group(5)), "arc": int(m.group(6)), "h0": int(m.group(7)), "h1": int(m.group(8))}
            continue
        if proj is not None:
            m = SRC.search(line)
            if m:
                proj["src"] = (int(m.group(1)), int(m.group(2)))
                continue
            m = DST.search(line)
            if m:
                proj["dst"] = (int(m.group(1)), int(m.group(2)))
                proj["kind"] = m.group(3) or "tile"
                proj["target"] = m.group(4)
                glob[(path, tick)].append(("proj", proj))
            proj = None
            continue
        m = NPC.match(line)
        if m and section == "n":
            nid = m.group(3) or m.group(4) or m.group(5)
            if nid in TD:
                actor = ("npc", m.group(2))
                events[(path, m.group(2))].append((tick, "pos", (nid, int(m.group(6)), int(m.group(7)), m.group(1))))
            else:
                actor = None
            continue
        m = PLAYER.match(line)
        if m and section == "p":
            actor = ("me", None) if m.group(1) == me else None
            if actor:
                myxz = (int(m.group(2)), int(m.group(3)))
                glob[(path, tick)].append(("mypos", myxz))
            continue
        if actor is None:
            m = SYN.search(line)
            if m:
                glob[(path, tick)].append(("synth", (m.group(1), int(m.group(2) or 0))))
                continue
            m = AREA.search(line)
            if m:
                glob[(path, tick)].append(("area", (m.group(1), m.group(2).strip(", "), (int(m.group(3)), int(m.group(4))))))
                continue
            m = MAPANIM.search(line)
            if m:
                glob[(path, tick)].append(("mapanim", (SPOTN.get(m.group(1) or m.group(2), m.group(1) or m.group(2)), m.group(3).strip(", "), (int(m.group(4)), int(m.group(5))))))
            continue
        if actor[0] == "me":
            m = SEQ.search(line)
            if m:
                glob[(path, tick)].append(("myseq", SEQN.get(m.group(1) or m.group(2), "?")))
            m = SPOT.search(line)
            if m:
                glob[(path, tick)].append(("myspot", (SPOTN.get(m.group(2) or m.group(3), "?"), m.group(4).strip(", "))))
            m = HIT.search(line)
            if m:
                glob[(path, tick)].append(("myhit", (int(m.group(1)), int(m.group(2)))))
            continue
        key = (path, actor[1])
        m = SEQ.search(line)
        if m:
            events[key].append((tick, "seq", (SEQN.get(m.group(1) or m.group(2), "?"), int(m.group(3) or 0))))
            continue
        m = SPOT.search(line)
        if m:
            events[key].append((tick, "spot", (SPOTN.get(m.group(2) or m.group(3), "?"), m.group(4).strip(", "))))
            continue
        m = HIT.search(line)
        if m:
            events[key].append((tick, "hit", (int(m.group(1)), int(m.group(2)))))
            continue
        m = HEADICON.search(line)
        if m:
            events[key].append((tick, "icon", (int(m.group(2)), int(m.group(3)))))
            continue
        if BODY.search(line):
            events[key].append((tick, "body", None))
            continue
        m = MODEL.match(line)
        if m:
            events[key].append((tick, "model", int(m.group(1))))
            continue
        m = TRANSFORM.search(line)
        if m:
            events[key].append((tick, "transform", m.group(1) or m.group(2)))


def lives(evs):
    """Split one npc index's events into lives: from add/transform to death seq."""
    out = []
    cur = None
    for t, k, d in evs:
        if k == "pos" and d[3] == "add":
            if cur:
                out.append(cur)
            cur = {"add": t, "type": d[0], "ev": []}
        if cur is None:
            continue
        cur["ev"].append((t, k, d))
        if k == "seq" and d[0].endswith("_death"):
            cur["death"] = t
        if k == "pos" and d[3] == "del":
            cur["del"] = t
    if cur:
        out.append(cur)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--lives", type=int, default=0, help="print the first N lives in full")
    a = ap.parse_args()
    events = defaultdict(list)
    glob = defaultdict(list)
    for p in a.paths:
        scan(p, events, glob)
    allives = []
    for key, evs in events.items():
        for life in lives(evs):
            life["key"] = key
            allives.append(life)
    allives.sort(key=lambda l: (l["key"][0], l["add"]))
    C = defaultdict(Counter)
    shown = 0
    for life in allives:
        path, idx = life["key"]
        ev = life["ev"]
        seqs = [(t, d[0], d[1]) for t, k, d in ev if k == "seq"]
        spots = [(t, d[0]) for t, k, d in ev if k == "spot"]
        hits = [(t, d[1]) for t, k, d in ev if k == "hit" and d[0] in (16, 12, 43)]
        icons = [(t, d[1]) for t, k, d in ev if k == "icon"]
        summon = [t for t, s, _ in seqs if s.endswith("_summon")]
        boom = [t for t, s, _ in seqs if s.endswith("_explosion_fire")]
        death = life.get("death")
        # respawn: this life's add vs the previous death of the same index
        C["life types"][life["type"]] += 1
        # attacks
        for t, s, dl in seqs:
            if s.endswith(("_melee", "_firey_balls", "_spare_ribs")):
                tags = []
                for kind, data in glob.get((path, t), []):
                    if kind == "proj" and data.get("target") is not None:
                        pid = SPOTN.get(data["id"], data["id"])
                        if "luc2" in pid:
                            pos = [d for tt, kk, d in ev if kk == "pos" and tt <= t]
                            dist = -1
                            if pos:
                                _, nx, nz, _ = pos[-1]
                                dist = max(abs(data["dst"][0] - (nx + 2)), abs(data["dst"][1] - (nz + 2)))
                            C["projectile " + s.split("_")[-1]]["%s start=%d flight=%d peak=%d arc=%d h=%d->%d @dist %d" % (
                                pid, data["start"], data["end"] - data["start"], data["peak"], data["arc"], data["h0"], data["h1"], dist)] += 1
                    if kind == "synth":
                        tags.append("synth %s d%d" % data)
                    if kind == "myspot":
                        tags.append("myspot %s %s" % data)
                for tag in tags:
                    C["attack " + s.split("undead_demon_")[-1] + " same-tick"][tag] += 1
                C["attack counts"][s.split("undead_demon_")[-1]] += 1
        att = sorted(t for t, s, _ in seqs if s.endswith(("_melee", "_firey_balls", "_spare_ribs")))
        for x, y in zip(att, att[1:]):
            C["attack gap"][y - x] += 1
        # engage: explosion_fire vs my first attack (bow) at or before it
        if boom:
            e = boom[0]
            mine = [tt for (pp, tt), lst in glob.items() if pp == path and life["add"] <= tt <= e + 3
                    for kind, d in lst if kind == "myseq" and d in ("human_bow", "drygore_blowpipe_attack")]
            first_hit = [t for t, _ in hits if t >= e]
            C["engage: explosion_fire - spawn"][e - (summon[0] if summon else life["add"])] += 0  # keep key
            if first_hit:
                C["engage: first hit on it - explosion_fire"][first_hit[0] - e] += 1
            later = [t for t in boom[1:]]
            for t in later:
                C["explosion_fire again, ticks after the first"][t - e] += 1
            icons_after = [t for t, _ in icons if t > e]
            if icons_after:
                C["engage: first icon write - explosion_fire"][icons_after[0] - e] += 1
            sh = [t for t, n in spots if n.endswith("_shield_restore_spot") and t > e]
            if sh:
                C["engage: first shield_restore - explosion_fire"][sh[0] - e] += 1
        # shield_spot cadence
        loops = [t for t, n in spots if n.endswith("_shield_spot")]
        for x, y in zip(loops, loops[1:]):
            C["shield_spot gap"][y - x] += 1
        # prayer: damage between icon writes
        prev = None
        for t, sprite in icons:
            if prev is not None:
                dmg = sum(v for tt, v in hits if prev < tt <= t)
                C["icon write: damage since previous write (rounded to 10)"][dmg // 10 * 10] += 1
            prev = t
        # death / respawn
        if death is not None:
            C["death -> del"][life.get("del", -1) - death] += 1
        # print
        if shown < a.lives:
            shown += 1
            print("== life %s npc %s added %d (%s)" % (os.path.basename(path), idx, life["add"], life["type"]))
            for t, k, d in ev:
                if k == "pos" and d[3] not in ("add", "del"):
                    continue
                print("  %+5d %-9s %s" % (t - life["add"], k, d))
    # respawn: per index, death -> next add
    for key, evs in events.items():
        ls = lives(evs)
        for x, y in zip(ls, ls[1:]):
            if x.get("death") is not None:
                C["death -> next add"][y["add"] - x["death"]] += 1
                sm = [t for t, k, d in y["ev"] if k == "seq" and d[0].endswith("_summon")]
                if sm:
                    C["next add -> summon"][sm[0] - y["add"]] += 1
    for name in sorted(C):
        print("==", name)
        for k, v in sorted(C[name].items(), key=lambda kv: -kv[1])[:14]:
            print("   %5d  %s" % (v, k))
    segments(a.paths)


# ------------------------------------------------------------------ segments
#
# Two rules the solver rests on (docs/rsprox-analysis/tormented_demons/solver.md):
#
#   prayer read      for each fireball / rib shard at the recorder, the recorder's
#                    protect prayer for that style at the end of T-1, T and T+3, and
#                    whether the T+4 splat did damage: which tick's prayer counts
#   segment style    each life split at its explosions (engage, bombs): the swing
#                    styles in order with the recorder's gap to the 3x3 at T-1, the
#                    first style after an engage by that gap, mixed segments, and the
#                    demon's first step after the explosion by style
#
# Rev 227 transcripts carry numbers; both spellings are matched.

SEG_SEQ = {"11387": "X", "11388": "F", "11389": "R", "11392": "M", "11394": "D", "11395": "S"}
SEG_PRAY = re.compile(r"\[varbit\] id=(?:prayer_protectfrom(magic|missiles|melee) \(\d+\)|(411[678])), old=\d+, new=(\d)")
SEG_PRAYN = {"4116": "magic", "4117": "missiles", "4118": "melee"}
SEG_PL = re.compile(r"^\s+\[(?:add|del|idle|walk|run|teleport|crawl)\] player=\(index=(\d+), name=[^,]*, coord=\((\d+), (\d+), \d+\)\)(?:, newcoord=\((\d+), (\d+))?")
SEG_NPC = re.compile(r"^\s+\[(add|del|idle|walk|run|teleport|crawl)\] npc=\(index=(\d+), (.*?)coord=\((\d+), (\d+), \d+\)[^)]*\)(?:, newcoord=\((\d+), (\d+))?")
SEG_TD = re.compile(r"ormented|\b1359[9]\b|\b1360[0-6]\b")
SEG_TGT = re.compile(r"\[target\].*player=\(index=(\d+)")


def seg_gap(p, n):
    dx = max(n[0] - p[0], p[0] - (n[0] + 2), 0)
    dz = max(n[1] - p[1], p[1] - (n[1] + 2), 0)
    g = max(dx, dz)
    if g == 1 and dx and dz:
        return "d"
    return str(min(g, 9))


def seg_scan(path):
    me = None
    tick = 0
    section = None
    actor = None
    cur = None
    lastproj = ""
    pray = {"magic": 0, "missiles": 0, "melee": 0}
    out = {"pray": {}, "mypos": {}, "ev": defaultdict(list), "proj": [], "melee_at_me": set(), "myhit": defaultdict(list)}
    npos = {}
    mine = False
    for line in open(path, errors="replace"):
        m = ME.search(line)
        if m:
            me = m.group(1)
        s = line.strip()
        m = TICK.match(s)
        if m and line.startswith("["):
            out["pray"][tick] = dict(pray)
            out["mypos"][tick] = cur
            tick = int(m.group(1))
            continue
        m = SEG_PRAY.search(line)
        if m:
            pray[m.group(1) or SEG_PRAYN[m.group(2)]] = int(m.group(3))
            continue
        if "[synth_sound] id=9219" in s:
            out["melee_at_me"].add(tick)
        if s.startswith("[player_info]"):
            section = "p"
            continue
        if s.startswith("[npc_info"):
            section = "n"
            continue
        if s.startswith("[map_projanim"):
            section = "proj"
            lastproj = s
            continue
        if section == "proj":
            m = SEG_TGT.search(line)
            if m and m.group(1) == me:
                if "id=2853," in lastproj or "fireball_proj" in lastproj:
                    out["proj"].append((tick, "magic"))
                elif "id=2857," in lastproj or "rib_bone_shard_projectile" in lastproj:
                    out["proj"].append((tick, "missiles"))
            continue
        m = SEG_PL.match(line)
        if m and section == "p":
            mine = m.group(1) == me
            if mine:
                cur = (int(m.group(4) or m.group(2)), int(m.group(5) or m.group(3)))
            continue
        if section == "p" and mine:
            m = HIT.search(line)
            if m:
                out["myhit"][tick].append(int(m.group(2)))
            continue
        m = SEG_NPC.match(line)
        if m and section == "n":
            actor = m.group(2) if SEG_TD.search(m.group(3)) else None
            if actor:
                npos[actor] = (int(m.group(6) or m.group(4)), int(m.group(7) or m.group(5)))
                if m.group(1) in ("walk", "run"):
                    out["ev"][actor].append((tick, "W", npos[actor]))
            continue
        if actor:
            m = SEQ.search(line)
            if m:
                k = SEG_SEQ.get(m.group(1) or m.group(2))
                if k:
                    out["ev"][actor].append((tick, k, npos[actor]))
    out["pray"][tick] = dict(pray)
    out["mypos"][tick] = cur
    return out


def segments(paths):
    C = defaultdict(Counter)
    for path in paths:
        d = seg_scan(path)
        pray = d["pray"]
        for T, kind in d["proj"]:
            p0, p1, p3 = (pray.get(t, {}).get(kind) for t in (T - 1, T, T + 3))
            splat = d["myhit"].get(T + 4)
            hit = "no splat" if not splat else ("damage" if sum(splat) > 0 else "0")
            C["prayer read: %s prayer at end of T-1 / T / T+3, T+4 splat" % kind]["%s / %s / %s, %s" % (p0, p1, p3, hit)] += 1
        multi = os.path.basename(path) in ("1696.txt", "552.txt")
        for idx, lst in d["ev"].items():
            X = None
            first = None
            walked = None
            styles = []
            xgap = None

            def flush():
                if X is None or not styles:
                    return
                if not multi:
                    if len(set(styles)) > 1:
                        C["segment style: single-demon segments"]["mixed"] += 1
                    else:
                        C["segment style: single-demon segments"]["one style"] += 1
                    if X == "engage":
                        C["segment style: first style after the engage, by gap at X"]["gap %s: %s" % (xgap, styles[0])] += 1
                        C["segment style: first style after the engage"][styles[0]] += 1
                    C["segment style: first step after X, " + styles[0]][
                        "none" if walked is None else "X + %d" % min(walked, 9)] += 1

            for t, k, n in lst:
                if k in "XSD":
                    flush()
                    if k == "X":
                        X = "engage" if first is None else "bomb"
                        first = t
                        p = d["mypos"].get(t - 1)
                        xgap = seg_gap(p, n) if p else "?"
                    else:
                        X, first = None, None
                    walked = None
                    styles = []
                    continue
                if X is None:
                    continue
                if k == "W":
                    if walked is None and not styles:
                        walked = t - first
                    continue
                at_me = (k == "M" and t in d["melee_at_me"]) or (k in "FR" and any(
                    pt == t for pt, _ in d["proj"]))
                if at_me:
                    styles.append(k)
            flush()
    for name in sorted(C):
        print("==", name)
        for k, v in sorted(C[name].items(), key=lambda kv: -kv[1])[:24]:
            print("   %5d  %s" % (v, k))


if __name__ == "__main__":
    main()
