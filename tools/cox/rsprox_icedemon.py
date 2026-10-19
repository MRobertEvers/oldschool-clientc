#!/usr/bin/env python3
"""The Chambers of Xeric Ice Demon room as Jagex's server sent it, from RSProx
transcripts in INSTANCE tiles.

Every recorded Ice Demon room is turned a quarter (tools/rsprox_frame_check.py),
so a translated transcript mirrors the offsets inside each chunk: transcribe
with TRANSLATE_INSTANCED_COORDS=false, and PLAYER_INFO_LOCAL_PLAYER_ONLY=false to
see a party (docs/rsprox-analysis/cox/README.md, "Instance tiles"). Translated
input is refused.

Sections (--section, default all):
  attacks  each demon_casting: the projectile, its target against every raider's
           tile, the impact graphics (offset from the target, delay), the sounds,
           the hits and the freeze, in ticks from the cast (C)
  pop      the thaw: the braziers going out, the smoke, the fiends' deaths, the
           transform and his walk, in ticks from the first smoke (P)
  fiends   each icefiend: its brazier, its attack cadence, its sound
  braziers lighting and going out: loc changes, sounds, the raider's anim
  blocks   demon_block delays against the attacker's projectile
  death    the death and what follows, in ticks from the killing hit (D)

    python3 tools/cox/rsprox_icedemon.py <raw>/archive/{3285,843,3282,3389,820}.txt
"""
import argparse
import re
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]")
TOP = re.compile(r"^ {4}\[(\w+)\] ?(.*)$")
SUB8 = re.compile(r"^ {8}\[(\w+)\] ?(.*)$")
SUB12 = re.compile(r"^ {12}\[(\w+)\] ?(.*)$")
NPC = re.compile(r"npc=\(index=(\d+), (?:id=([a-z0-9_]+) \((\d+)\)|[^()]*\(id=(\d+)\)), coord=\((\d+), (\d+), (\d+)\)")
PLAYER = re.compile(r"player=\(index=(\d+), name=([^,]*), coord=\((\d+), (\d+), (\d+)\)\)")
NEW = re.compile(r"newcoord=\((\d+), (\d+), (\d+)\)")
COORD = re.compile(r"coord=\((\d+), (\d+), (\d+)\)")
NAMEID = re.compile(r"id=(?:([a-z0-9_]+) \()?(-?\d+)\)?")
import os

DEMON = {7584, 7585}
SIZE = 2
# Raw transcripts print a sound as its bare id: name it from the content's list.
SOUND_NAMES = {}
_PACK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "OSRS-Content",
                     "osrs239-content", "pack", "4_soundeffects.pack")
if os.path.exists(_PACK):
    for _line in open(_PACK, errors="replace"):
        _k, _, _v = _line.strip().partition("=")
        if _k.isdigit():
            SOUND_NAMES[int(_k)] = _v
FIEND = 7586
INSTANCE_X = 6400


def num(text, key, default=None):
    m = re.search(r"\b" + key + r"=(-?\d+)", text)
    return int(m.group(1)) if m else default


def ident(text):
    m = NAMEID.search(text)
    if not m:
        return (None, None)
    return (m.group(1) or m.group(2), int(m.group(2)))


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


class Rec:
    def __init__(self, path):
        self.path = path.split("/")[-1]
        self.top = defaultdict(list)          # tick -> [(kind, text, subs)]
        self.npc = defaultdict(list)          # index -> [(tick, kind, text)]
        self.npc_id = {}                      # index -> last id
        self.npc_tile = defaultdict(dict)     # index -> tick -> (x, z) end of tick
        self.pl = defaultdict(list)           # name -> [(tick, kind, text)]
        self.pl_tile = defaultdict(dict)      # name -> tick -> (x, z)
        self.translated = False
        self.read(path)

    def read(self, path):
        tick = 0
        cur_top = None
        who = None
        for line in open(path, errors="replace"):
            m = TICK.match(line)
            if m:
                tick = int(m.group(1))
                cur_top = who = None
                continue
            m = TOP.match(line)
            if m:
                cur_top = [m.group(1), m.group(2), []]
                self.top[tick].append(cur_top)
                who = None
                continue
            m = SUB8.match(line)
            if m and cur_top is not None:
                kind, text = m.group(1), m.group(2)
                cur_top[2].append((kind, text))
                n = NPC.search(text)
                p = PLAYER.search(text) if not n else None
                if n and kind in ("add", "idle", "walk", "run", "teleport", "remove", "crawl"):
                    idx = int(n.group(1))
                    nid = int(n.group(3) or n.group(4))
                    self.npc_id[idx] = nid
                    c = (int(n.group(5)), int(n.group(6)))
                    nn = NEW.search(text)
                    if nn:
                        c = (int(nn.group(1)), int(nn.group(2)))
                    self.npc_tile[idx][tick] = c
                    self.npc[idx].append((tick, kind, text))
                    who = ("npc", idx)
                    if c[0] < INSTANCE_X and nid in DEMON:
                        self.translated = True
                elif p and kind in ("add", "idle", "walk", "run", "teleport", "remove"):
                    name = p.group(2)
                    c = (int(p.group(3)), int(p.group(4)))
                    nn = NEW.search(text)
                    if nn:
                        c = (int(nn.group(1)), int(nn.group(2)))
                    self.pl_tile[name][tick] = c
                    self.pl[name].append((tick, kind, text))
                    who = ("pl", name)
                else:
                    who = None
                continue
            m = SUB12.match(line)
            if m and who is not None:
                kind, text = m.group(1), m.group(2)
                if who[0] == "npc":
                    self.npc[who[1]].append((tick, kind, text))
                else:
                    self.pl[who[1]].append((tick, kind, text))
                if cur_top is not None:
                    cur_top[2].append((kind, text))

    def tile_at(self, table, t):
        best = None
        for k in sorted(table):
            if k <= t:
                best = table[k]
            else:
                break
        return best

    def raiders_at(self, t, near, radius=20):
        out = {}
        for name, tab in self.pl_tile.items():
            c = self.tile_at(tab, t)
            if c and near and cheb(c, near) <= radius:
                out[name] = c
        return out

    def tops(self, t, kind_prefix):
        return [(k, tx, subs) for k, tx, subs in self.top.get(t, []) if k.startswith(kind_prefix)]


def projectiles(rec, t):
    out = []
    for k, tx, subs in rec.tops(t, "map_projanim"):
        src = tgt = None
        tgt_text = ""
        for sk, st in subs:
            if sk == "source":
                src = COORD.search(st)
                src = (int(src.group(1)), int(src.group(2))) if src else None
            elif sk == "target":
                tgt_text = st
                c = COORD.search(st)
                tgt = (int(c.group(1)), int(c.group(2))) if c else None
        name, pid = ident(tx)
        out.append(dict(name=name, id=pid, start=num(tx, "starttime"), end=num(tx, "endtime"),
                        angle=num(tx, "angle"), progress=num(tx, "progress"),
                        sh=num(tx, "startheight"), eh=num(tx, "endheight"), src=src, tgt=tgt,
                        tgt_entity="npc=" in tgt_text or "player=" in tgt_text))
    return out


def map_anims(rec, t):
    out = []
    for k, tx, subs in rec.tops(t, "map_anim"):
        name, gid = ident(tx)
        c = COORD.search(tx)
        out.append((name, num(tx, "delay", 0), num(tx, "height", 0), (int(c.group(1)), int(c.group(2)))))
    return out


def foot(sw, at, size=SIZE):
    dx = max(sw[0] - at[0], 0, at[0] - (sw[0] + size - 1))
    dz = max(sw[1] - at[1], 0, at[1] - (sw[1] + size - 1))
    return max(dx, dz)


def sounds(rec, t):
    out = []
    for k, tx, subs in rec.top.get(t, []):
        if k in ("sound_area", "synth_sound"):
            name, sid = ident(tx)
            if name is None or name.isdigit():
                name = "%s (%d)" % (SOUND_NAMES.get(sid, "?"), sid)
            c = COORD.search(tx)
            out.append((k, name, num(tx, "delay", 0), num(tx, "range"), num(tx, "loops", 1),
                        (int(c.group(1)), int(c.group(2))) if c else None))
    return out


def demon(rec):
    idxs = [i for i, nid in rec.npc_id.items() if nid in DEMON]
    return idxs


def npc_events(rec, idx, kind):
    return [(t, tx) for t, k, tx in rec.npc[idx] if k == kind]


def hits_on_raiders(rec, t):
    out = []
    for name, evs in rec.pl.items():
        for tt, k, tx in evs:
            if tt == t and k == "hit":
                out.append((name, num(tx, "id"), num(tx, "value")))
    return out


def section_attacks(recs):
    print("== attacks: C = the demon_casting tick")
    proj_shapes = Counter()
    impact_layout = defaultdict(Counter)
    impact_tick = defaultdict(Counter)
    snd = defaultdict(Counter)
    lock = Counter()
    hit_dist = defaultdict(Counter)
    freeze = Counter()
    gaps = Counter()
    seq_on_cast = Counter()
    for rec in recs:
        for idx in demon(rec):
            ticks = sorted(rec.npc_tile[idx])
            lo, hi = ticks[0], ticks[-1]
            # an attack = an ice projectile leaving his south-west tile (his
            # cast seq is not always sent: a block landing the same tick wins)
            casts = []
            for t in range(lo, hi + 1):
                sw = rec.tile_at(rec.npc_tile[idx], t)
                if any(p["name"] and "ice" in p["name"] and p["src"] == sw for p in projectiles(rec, t)):
                    casts.append(t)
            for a, b in zip(casts, casts[1:]):
                gaps[b - a] += 1
            for c in casts:
                sw = rec.tile_at(rec.npc_tile[idx], c)
                centre = (sw[0] + 1, sw[1] + 1) if sw else None
                ps = [p for p in projectiles(rec, c) if p["name"] and "ice" in p["name"] and p["src"] == sw]
                seqs = [tx for t, tx in npc_events(rec, idx, "sequence") if t == c]
                seq_on_cast[tuple(s.split(" (")[0].replace("id=", "") + (" delayed" if "delay=" in s else "") for s in seqs) or ("none",)] += 1
                for p in ps:
                    kind = "burst" if "burst" in p["name"] else "boulder"
                    srcrel = (p["src"][0] - sw[0], p["src"][1] - sw[1]) if p["src"] and sw else None
                    d = foot(sw, p["tgt"]) if sw else None
                    proj_shapes[(kind, p["name"], p["start"], p["end"], d, p["angle"], p["progress"], p["sh"], p["eh"], srcrel, p["tgt_entity"])] += 1
                    # the target against every raider's tile, end of C-1 and end of C
                    prev = rec.raiders_at(c - 1, centre)
                    now = rec.raiders_at(c, centre)
                    on_prev = [n for n, t in prev.items() if t == p["tgt"]]
                    on_now = [n for n, t in now.items() if t == p["tgt"]]
                    lock[(kind, "on a raider's end-of-C-1 tile" if on_prev else ("on a raider's end-of-C tile" if on_now else "neither"))] += 1
                    land = c + p["end"] // 30
                    for dt in range(0, 6):
                        for name, delay, height, at in map_anims(rec, c + dt):
                            if name and ("ice_blitz" in name or "iceball_hit" in name):
                                rel = (at[0] - p["tgt"][0], at[1] - p["tgt"][1])
                                if cheb(rel, (0, 0)) <= 2:
                                    impact_layout[kind][(rel, delay, height, name)] += 1
                                    impact_tick[kind][(dt, "end//30 = %d" % (p["end"] // 30))] += 1
                        for s in sounds(rec, c + dt):
                            k, name, delay, rng, loops, at = s
                            if name and any(w in name for w in ("ice", "catapult", "cauldron", "paralyze")):
                                where = "synth" if at is None else ("target" if at == p["tgt"] else ("demon sw" if at == sw else ("demon centre" if at == centre else "rel tgt %s" % ((at[0] - p["tgt"][0], at[1] - p["tgt"][1]),))))
                                snd[kind][("C+%d" % dt, name, "delay %d" % delay, "range %s" % rng, "loops %d" % loops, where)] += 1
                        for name, hid, val in hits_on_raiders(rec, c + dt):
                            pt = rec.tile_at(rec.pl_tile[name], c + dt - 1)
                            dd = cheb(pt, p["tgt"]) if pt else None
                            hit_dist[kind][("C+%d" % dt, "dist %s" % dd, "hitsplat %s" % hid)] += 1
                        for name, evs in rec.pl.items():
                            for tt, k2, tx in evs:
                                if tt == c + dt and k2 in ("spotanim", "sequence") and dt <= 5:
                                    pt = rec.tile_at(rec.pl_tile[name], c + dt - 1)
                                    freeze[(kind, "C+%d" % dt, "raider %s" % k2, tx.split(",")[0][:40],
                                            "dist %s" % (cheb(pt, p["tgt"]) if pt else None))] += 1
                        for k2, tx, subs in rec.top.get(c + dt, []):
                            if k2 == "message" and "frozen" in tx:
                                freeze[(kind, "C+%d" % dt, "message frozen")] += 1
    print("  attack gaps:", dict(sorted(gaps.items())))
    print("  his seq on the attack tick:", dict(seq_on_cast))
    print("  projectiles (kind, name, start, end, target's distance from his 2x2, angle, progress, heights raw, source rel his sw, entity target):")
    for k, v in sorted(proj_shapes.items(), key=lambda kv: (kv[0][0], kv[0][4] or 0)):
        print("    %4d %s" % (v, k))
    print("  target:", dict(lock))
    for kind in impact_layout:
        print("  %s impacts (offset from target, delay, height, graphic):" % kind)
        for k, v in sorted(impact_layout[kind].items(), key=lambda kv: (kv[0][1], kv[0][0])):
            print("    %4d %s" % (v, k))
        print("  %s impact tick:" % kind, dict(impact_tick[kind]))
        print("  %s sounds:" % kind)
        for k, v in sorted(snd[kind].items()):
            print("    %4d %s" % (v, k))
        print("  %s hits on raiders (tick, raider's end-of-previous-tick distance from the target, hitsplat):" % kind)
        for k, v in sorted(hit_dist[kind].items()):
            print("    %4d %s" % (v, k))
    print("  raider seqs/spotanims and the freeze message after an attack:")
    for k, v in sorted(freeze.items()):
        print("    %4d %s" % (v, k))


def section_pop(recs):
    print("== pop: P = the first smokepuff_large")
    for rec in recs:
        for idx in demon(rec):
            ticks = sorted(rec.npc_tile[idx])
            smoke = sorted(t for t in rec.top if ticks[0] <= t <= ticks[-1]
                           for name, d, h, at in map_anims(rec, t) if name and "smokepuff" in name)
            if not smoke:
                continue
            P = smoke[0]
            sw = rec.tile_at(rec.npc_tile[idx], P)
            print("-- %s P=%d, his sw at P %s" % (rec.path, P, sw))
            for t in range(P - 3, P + 14):
                bits = []
                for name, delay, height, at in map_anims(rec, t):
                    if name and "smokepuff" in name:
                        bits.append("%s d=%d h=%d at sw%+d,%+d" % (name, delay, height, at[0] - sw[0], at[1] - sw[1]))
                for s in sounds(rec, t):
                    k, name, delay, rng, loops, at = s
                    if name and any(w in name for w in ("smoke", "icefiend_death", "fire", "demon")):
                        bits.append("%s %s d=%d r=%s loops=%d %s" % (k, name, delay, rng, loops,
                                    "at sw%+d,%+d" % (at[0] - sw[0], at[1] - sw[1]) if at else ""))
                for k, tx, subs in rec.top.get(t, []):
                    if k.startswith("loc_add") and "brazier" in tx:
                        c = COORD.search(tx)
                        bits.append("%s at sw%+d,%+d" % (ident(tx)[0], int(c.group(1)) - sw[0], int(c.group(2)) - sw[1]))
                    if k == "message":
                        bits.append("msg " + tx[:70])
                for i2 in rec.npc:
                    for tt, k, tx in rec.npc[i2]:
                        if tt == t and k in ("sequence", "transformation", "spotanim", "say") :
                            bits.append("%s %s %s" % (rec.npc_id.get(i2), k, tx[:60]))
                        if tt == t and k in ("walk", "run", "teleport") and rec.npc_id.get(i2) in DEMON:
                            nn = NEW.search(tx)
                            bits.append("demon %s -> sw%+d,%+d" % (k, int(nn.group(1)) - sw[0], int(nn.group(2)) - sw[1]))
                        if tt == t and k == "remove" and rec.npc_id.get(i2) == FIEND:
                            bits.append("icefiend removed")
                if bits:
                    print("   P%+d  %s" % (t - P, " | ".join(bits)))


def section_fiends(recs):
    print("== fiends")
    gaps = Counter()
    snd = Counter()
    for rec in recs:
        for idx, nid in rec.npc_id.items():
            if nid != FIEND:
                continue
            seqs = [t for t, tx in npc_events(rec, idx, "sequence") if "pyrefiend_attack" in tx or "(1582)" in tx]
            for a, b in zip(seqs, seqs[1:]):
                gaps[b - a] += 1
            ft = rec.tile_at(rec.npc_tile[idx], seqs[0]) if seqs else None
            for t in seqs:
                for s in sounds(rec, t):
                    k, name, delay, rng, loops, at = s
                    if name and "icefiend" in name:
                        rel = (at[0] - ft[0], at[1] - ft[1]) if at and ft else None
                        snd[(name, delay, rng, loops, "at the fiend" if rel == (0, 0) else "rel %s" % (rel,))] += 1
    print("  attack gaps:", dict(sorted(gaps.items())))
    print("  attack sounds:", dict(snd))


def section_braziers(recs):
    print("== braziers")
    seen = Counter()
    for rec in recs:
        lit = {}
        for t in sorted(rec.top):
            for k, tx, subs in rec.top[t]:
                if k.startswith("loc_add") and "brazier" in tx:
                    c = COORD.search(tx)
                    at = (int(c.group(1)), int(c.group(2)))
                    state = "lit" if "brazier_lit" in tx else "unlit"
                    if lit.get(at) != state:
                        same = []
                        for s in sounds(rec, t):
                            kk, name, delay, rng, loops, sat = s
                            if sat == at or kk == "synth_sound":
                                same.append("%s %s d=%d r=%s" % (kk, name, delay, rng))
                        seq = [tx2 for name, evs in rec.pl.items() for tt, k2, tx2 in evs if tt == t and k2 == "sequence"]
                        seen[("-> " + state, tuple(sorted(set(same))), tuple(sorted(set(s[:40] for s in seq))))] += 1
                        lit[at] = state
            for s in sounds(rec, t):
                kk, name, delay, rng, loops, sat = s
                if name and "crackling" in name:
                    seen[("crackling sound", "at a brazier" if sat in lit else "elsewhere", lit.get(sat))] += 1
    for k, v in sorted(seen.items(), key=lambda kv: -kv[1]):
        print("  %4d %s" % (v, k))


def section_blocks(recs):
    print("== blocks: demon_block delay vs the attacker's projectile")
    rows = Counter()
    for rec in recs:
        for idx in demon(rec):
            for t, tx in npc_events(rec, idx, "sequence"):
                if "demon_block" not in tx:
                    continue
                d = num(tx, "delay", 0)
                ps = [p for p in projectiles(rec, t) if p["tgt_entity"]]
                ends = sorted(set((p["name"], p["end"]) for p in ps))
                spots = [tx2 for tt, k, tx2 in rec.npc[idx] if tt == t and k == "spotanim"]
                rows[("delay %d" % d, tuple(ends), tuple(s[:50] for s in spots))] += 1
    for k, v in sorted(rows.items()):
        print("  %4d %s" % (v, k))


def section_death(recs):
    print("== death: D = the demon_death seq")
    for rec in recs:
        for idx in demon(rec):
            ds = [t for t, tx in npc_events(rec, idx, "sequence") if "demon_death" in tx]
            if not ds:
                continue
            D = ds[0]
            sw = rec.tile_at(rec.npc_tile[idx], D)
            print("-- %s D=%d" % (rec.path, D))
            for t in range(D - 1, D + 8):
                bits = []
                for k, tx, subs in rec.top.get(t, []):
                    if k.startswith("loc_add") or k.startswith("loc_del") or k.startswith("obj_add") or k in ("message", "midi_song_v2", "midi_jingle", "sound_area", "synth_sound", "map_anim"):
                        c = COORD.search(tx)
                        rel = (" at sw%+d,%+d" % (int(c.group(1)) - sw[0], int(c.group(2)) - sw[1])) if c else ""
                        bits.append("%s %s%s" % (k, tx[:70], rel))
                for tt, k, tx in rec.npc[idx]:
                    if tt == t and k in ("sequence", "remove", "transformation", "hit"):
                        bits.append("demon %s %s" % (k, tx[:50]))
                if bits:
                    print("   D%+d  %s" % (t - D, " | ".join(bits)))


SECTIONS = {"attacks": section_attacks, "pop": section_pop, "fiends": section_fiends,
            "braziers": section_braziers, "blocks": section_blocks, "death": section_death}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("transcripts", nargs="+")
    ap.add_argument("--section", choices=sorted(SECTIONS))
    args = ap.parse_args()
    recs = []
    for path in args.transcripts:
        rec = Rec(path)
        if rec.translated:
            print("%s: refused, template-translated tiles (TRANSLATE_INSTANCED_COORDS=false)" % rec.path)
            continue
        recs.append(rec)
    for name in ([args.section] if args.section else ["attacks", "pop", "fiends", "braziers", "blocks", "death"]):
        SECTIONS[name](recs)


if __name__ == "__main__":
    main()
