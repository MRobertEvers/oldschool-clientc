#!/usr/bin/env python3
"""The Chambers of Xeric Skeletal Mystics room as Jagex's server sent it, from
RSProx transcripts in INSTANCE tiles.

The recorded mystics rooms are turned three ways (3285 unturned, 3389 180, 3282 a
quarter; tools/rsprox_frame_check.py), and a translated transcript mirrors the
offsets inside each chunk of a quarter-turned room: transcribe with
TRANSLATE_INSTANCED_COORDS=false, and PLAYER_INFO_LOCAL_PLAYER_ONLY=false to see
every raider (docs/rsprox-analysis/cox/README.md, "Instance tiles"). Translated
input is refused.

Template tiles: the three portals are 1x1 locs whose template tiles 3285 (an
unturned room) prints exactly, so each room's instance -> template turn and
offset is solved from where its portals land (`room`).

Sections (--section, default all):
  attacks  each cast (a projectile from a mystic's tile): the style, its cast
           seq and graphic, the projectile against the distance, its target
           against every raider's tile at the end of C-1 and C, the impact
           graphics and sounds (the tick they are sent, their delay), the hits
           and the Defence drain, in ticks from the cast (C); the cadence
  melee    every mystic seq that is not a cast, a block or the death
  blocks   skeleton_update_defend delays against the attacker's projectile,
           and the sound of a hit on a mystic
  wake     each mystic: first seen, first attacked, first cast, first step
  movement every mystic step, against the distance to the raider
  death    the death and what follows, in ticks from the death seq (D)
  room     the mark, the portals and each mystic's first-seen tile, in
           template room-local tiles

    python3 tools/cox/rsprox_mystics.py <raw>/archive/{3285,3389,3282}.txt
"""
import argparse
import os
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
STAT = re.compile(r"\[update_stat_v2\] stat=(\w+), level=(\d+)")
VARBIT = re.compile(r"\[varbit\] id=([a-z0-9_]+)(?: \(\d+\))?, old=-?\d+, new=(-?\d+)")

MYSTIC = {7604, 7605, 7606}
SIZE = 2
INSTANCE_X = 6400
CASTS = ("fireblast_travel", "curse_travel")
IMPACTS = ("fireblast_impact", "curse_impact", "failedspell_impact")
# The ccw variant's template tiles (room-local, origin 3264, 5248), as the
# unturned 3285 prints them: the portals are 1x1, so exact.
PORTALS = {"raids_mystics_portal_start": (14, 2), "raids_mystics_portal_middle": (20, 18),
           "raids_mystics_portal_end": (7, 18)}
# Raw transcripts print a sound as its bare id: name it from the content's list.
SOUND_NAMES = {}
_PACK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "OSRS-Content",
                     "osrs239-content", "pack", "4_soundeffects.pack")
if os.path.exists(_PACK):
    for _line in open(_PACK, errors="replace"):
        _k, _, _v = _line.strip().partition("=")
        if _k.isdigit():
            SOUND_NAMES[int(_k)] = _v


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


def foot(sw, at, size=SIZE):
    """Chebyshev gap from a size-N footprint to a tile (0 = under it)."""
    dx = max(sw[0] - at[0], 0, at[0] - (sw[0] + size - 1))
    dz = max(sw[1] - at[1], 0, at[1] - (sw[1] + size - 1))
    return max(dx, dz)


def sound_name(sid):
    return "%s (%d)" % (SOUND_NAMES.get(sid, "?"), sid)


class Rec:
    def __init__(self, path):
        self.path = path.split("/")[-1]
        self.top = defaultdict(list)          # tick -> [(kind, text, subs)]
        self.npc = defaultdict(list)          # index -> [(tick, kind, text)]
        self.npc_id = {}                      # index -> last id
        self.npc_tile = defaultdict(dict)     # index -> tick -> (x, z) end of tick
        self.npc_add = {}                     # index -> (tick, tile)
        self.npc_del = {}                     # index -> tick
        self.pl = defaultdict(list)           # name -> [(tick, kind, text)]
        self.pl_tile = defaultdict(dict)      # name -> tick -> (x, z)
        self.stats = defaultdict(list)        # tick -> [(stat, level)] (the local raider)
        self.varbits = defaultdict(list)      # tick -> [(name, value)]
        self.local_idx = set()
        self.local_names = set()
        self.translated = False
        self.read(path)

    def read(self, path):
        tick = 0
        cur_top = None
        who = None
        for line in open(path, errors="replace"):
            if line.startswith("local player index:"):
                self.local_idx.add(int(line.split(":")[1]))
                continue
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
                if m.group(1) == "update_stat_v2":
                    s = STAT.search(line)
                    if s:
                        self.stats[tick].append((s.group(1), int(s.group(2))))
                if m.group(1) == "varbit":
                    v = VARBIT.search(line)
                    if v:
                        self.varbits[tick].append((v.group(1), int(v.group(2))))
                continue
            m = SUB8.match(line)
            if m and cur_top is not None:
                kind, text = m.group(1), m.group(2)
                cur_top[2].append((kind, text))
                n = NPC.search(text)
                p = PLAYER.search(text) if not n else None
                if n and kind in ("add", "idle", "walk", "run", "teleport", "remove", "crawl", "del"):
                    idx = int(n.group(1))
                    nid = int(n.group(3) or n.group(4))
                    self.npc_id[idx] = nid
                    c = (int(n.group(5)), int(n.group(6)))
                    nn = NEW.search(text)
                    if nn:
                        c = (int(nn.group(1)), int(nn.group(2)))
                    if kind == "add" and idx not in self.npc_add:
                        self.npc_add[idx] = (tick, c)
                    if kind == "del":
                        self.npc_del[idx] = tick
                    self.npc_tile[idx][tick] = c
                    self.npc[idx].append((tick, kind, text))
                    who = ("npc", idx)
                    if c[0] < INSTANCE_X and nid in MYSTIC:
                        self.translated = True
                elif p and kind in ("add", "idle", "walk", "run", "teleport", "remove"):
                    name = p.group(2)
                    if int(p.group(1)) in self.local_idx:
                        self.local_names.add(name)
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

    def raiders_at(self, t, near, radius=24):
        out = {}
        for name, tab in self.pl_tile.items():
            c = self.tile_at(tab, t)
            if c and near and cheb(c, near) <= radius:
                out[name] = c
        return out

    def tops(self, t, kind_prefix):
        return [(k, tx, subs) for k, tx, subs in self.top.get(t, []) if k.startswith(kind_prefix)]

    def varbit_at(self, name, t):
        best = None
        for k in sorted(self.varbits):
            if k > t:
                break
            for n, v in self.varbits[k]:
                if n == name:
                    best = v
        return best


def mystics(rec):
    return sorted(i for i, nid in rec.npc_id.items() if nid in MYSTIC)


def mname(rec, idx):
    return {7604: "a", 7605: "b", 7606: "c"}.get(rec.npc_id.get(idx), "?")


def projectiles(rec, t):
    out = []
    for k, tx, subs in rec.tops(t, "map_projanim"):
        src = tgt = None
        tgt_text = ""
        for sk, st in subs:
            if sk == "source":
                c = COORD.search(st)
                src = (int(c.group(1)), int(c.group(2))) if c else None
            elif sk == "target":
                tgt_text = st
                c = COORD.search(st)
                tgt = (int(c.group(1)), int(c.group(2))) if c else None
        name, pid = ident(tx)
        p = PLAYER.search(tgt_text)
        n = NPC.search(tgt_text)
        out.append(dict(name=name, id=pid, start=num(tx, "starttime"), end=num(tx, "endtime"),
                        angle=num(tx, "angle"), progress=num(tx, "progress"),
                        sh=num(tx, "startheight"), eh=num(tx, "endheight"), src=src, tgt=tgt,
                        tgt_player=p.group(2) if p else None, tgt_npc=int(n.group(1)) if n else None))
    return out


def sounds(rec, t):
    out = []
    for k, tx, subs in rec.top.get(t, []):
        if k in ("sound_area", "synth_sound"):
            name, sid = ident(tx)
            c = COORD.search(tx)
            out.append((k, sid, num(tx, "delay", 0), num(tx, "range"), num(tx, "loops", 1),
                        (int(c.group(1)), int(c.group(2))) if c else None))
    return out


def npc_events(rec, idx, kind, t=None):
    return [(tt, tx) for tt, k, tx in rec.npc[idx] if k == kind and (t is None or tt == t)]


def pl_events(rec, name, kind, t=None):
    return [(tt, tx) for tt, k, tx in rec.pl[name] if k == kind and (t is None or tt == t)]


def first_raider(rec):
    """The local raider: the player whose stats the transcript carries."""
    names = sorted(rec.local_names or rec.pl_tile, key=lambda n: min(rec.pl_tile[n]))
    return names[0] if names else None


def stat_before(rec, stat, t):
    """The local raider's `stat` level at the end of tick t-1."""
    best = None
    for k in sorted(rec.stats):
        if k >= t:
            break
        for sname, lv in rec.stats[k]:
            if sname == stat:
                best = lv
    return best


def casts_of(rec, idx):
    """(tick, projectile) for every cast: a mystic spell projectile whose
    source is the mystic's tile at the end of the tick (or of the tick before,
    when it stepped that tick)."""
    out = []
    ticks = sorted(rec.npc_tile[idx])
    if not ticks:
        return out
    lo, hi = ticks[0], rec.npc_del.get(idx, ticks[-1])
    for t in range(lo, hi + 1):
        sw = rec.tile_at(rec.npc_tile[idx], t)
        for p in projectiles(rec, t):
            if p["name"] in CASTS and p["src"] == sw:
                out.append((t, p))
    return out


def section_attacks(recs):
    print("== attacks: C = the cast tick (a spell projectile from the mystic's tile), L = C + end // 30")
    shapes = Counter()
    flight = defaultdict(Counter)
    lock = Counter()
    seq_on_c = Counter()
    gfx_on_c = Counter()
    impacts = Counter()
    snd = Counter()
    on_land = Counter()
    land = Counter()
    explained = Counter()
    drain = Counter()
    gaps = Counter()
    prayer = defaultdict(list)
    for rec in recs:
        local = first_raider(rec)
        lands = defaultdict(list)         # (raider, tick) -> [(style, landed?)]
        for idx in mystics(rec):
            cs = casts_of(rec, idx)
            for (a, _), (b, _) in zip(cs, cs[1:]):
                gaps[b - a] += 1
            for c, p in cs:
                sw = rec.tile_at(rec.npc_tile[idx], c)
                style = "fire" if p["name"] == "fireblast_travel" else "curse"
                seqs = [tx for t, tx in npc_events(rec, idx, "sequence", c)]
                seq_on_c[(style, tuple(ident(s)[0] + (" delay=%d" % num(s, "delay") if num(s, "delay") else "") for s in seqs) or ("none",))] += 1
                for t, tx in npc_events(rec, idx, "spotanim", c):
                    if num(tx, "delay", 0) == 0:
                        gfx_on_c[(style, ident(tx)[0], "height %s" % num(tx, "height"), "delay 0")] += 1
                srcrel = (p["src"][0] - sw[0], p["src"][1] - sw[1])
                g = foot(sw, p["tgt"])
                shapes[(style, p["name"], p["start"], p["angle"], p["progress"], p["sh"], p["eh"],
                        "src sw%+d,%+d" % srcrel, "target player" if p["tgt_player"] else "target tile")] += 1
                flight["gap"][(style, g, p["end"])] += 1
                flight["sw cheb"][(style, cheb(sw, p["tgt"]), p["end"])] += 1
                flight["centre cheb x2"][(style, max(abs(2 * p["tgt"][0] - (2 * sw[0] + 1)), abs(2 * p["tgt"][1] - (2 * sw[1] + 1))), p["end"])] += 1
                prev = rec.raiders_at(c - 1, sw)
                now = rec.raiders_at(c, sw)
                on_prev = [n for n, tile in prev.items() if tile == p["tgt"]]
                on_now = [n for n, tile in now.items() if tile == p["tgt"]]
                lock[("on the target's end-of-C-1 tile" if p["tgt_player"] in on_prev else
                      ("on its end-of-C tile only" if p["tgt_player"] in on_now else "neither"),
                      "raiders in view %d" % len(prev))] += 1
                who = p["tgt_player"]
                L = c + p["end"] // 30
                landed = None
                for tt, tx in pl_events(rec, who, "spotanim", c):
                    nm = ident(tx)[0]
                    if nm in IMPACTS and num(tx, "delay", 0) == p["end"]:
                        impacts[(style, nm, "sent on C", "delay = end", "height %s" % num(tx, "height"))] += 1
                        landed = nm != "failedspell_impact"
                if landed is None:
                    impacts[(style, "no impact graphic with delay = end on C")] += 1
                for k, sid, delay, rng, loops, at in sounds(rec, c):
                    if sid in (155, 156, 127, 3012, 227) and (delay == 0 or delay == p["end"]):
                        where = "synth" if at is None else "at sw%+d,%+d" % (at[0] - sw[0], at[1] - sw[1])
                        snd[(style, "landed" if landed else "splash", "C", k, sound_name(sid),
                             "delay %s" % ("= end" if delay == p["end"] and delay else delay), "range %s" % rng, where)] += 1
                for tt, tx in pl_events(rec, who, "spotanim", L):
                    if ident(tx)[0] in IMPACTS and num(tx, "delay", 0) == 0:
                        on_land[(style, "impact graphic re-sent on L")] += 1
                lands[(who, L)].append((style, landed, c, g, p["end"], idx))
                pfm = rec.varbit_at("prayer_protectfrommagic", c - 1)
                hit_on_l = pl_events(rec, who, "hit", L)
                land[(style, "landed" if landed else "splash", "a hitsplat on L" if hit_on_l else "no hitsplat on L",
                      "L = wiki 1+(1+gap)/3" if L - c == 1 + (1 + g) // 3 else "L != wiki")] += 1
                if landed and hit_on_l and len(lands[(who, L)]) == 1:
                    prayer[(style, "PfM" if pfm == 1 else ("no PfM" if pfm == 0 else "prayer unknown"))].append(num(hit_on_l[0][1], "value"))
                if landed and style == "curse" and who == local:
                    before = stat_before(rec, "defence", L)
                    after = dict(rec.stats.get(L, [])).get("defence")
                    drain[("defence %s -> %s on L" % (before, after if after is not None else "unchanged"),
                           "drop %s" % (before - after if after is not None and before is not None else 0))] += 1
        # every hitsplat on a raider within reach of a mystic cast: explained by a landing?
        for name in rec.pl:
            for t, tx in pl_events(rec, name, "hit"):
                ls = lands.get((name, t), [])
                if not any(l[1] for l in ls):
                    near = any(rec.npc_add.get(i, (1 << 30,))[0] <= t <= rec.npc_del.get(i, -1) for i in mystics(rec))
                    if near:
                        explained[("a hitsplat with no landed cast on its tick", rec.path, t, num(tx, "id"), num(tx, "value"))] += 1
                else:
                    explained[("a hitsplat on a landed cast's L",)] += 1
    print("  cast gaps (same mystic):", dict(sorted(gaps.items())))
    print("  mystic seq on C:")
    for k, v in sorted(seq_on_c.items()):
        print("    %4d %s" % (v, k))
    print("  mystic graphic on C (undelayed):")
    for k, v in sorted(gfx_on_c.items()):
        print("    %4d %s" % (v, k))
    print("  projectiles (style, name, start, angle, progress, heights raw, source, target):")
    for k, v in sorted(shapes.items()):
        print("    %4d %s" % (v, k))
    for measure in ("gap", "sw cheb", "centre cheb x2"):
        print("  flight end by %s (style, distance, end):" % measure)
        for k, v in sorted(flight[measure].items(), key=lambda kv: (kv[0][1], kv[0][0], kv[0][2])):
            print("    %4d %s" % (v, k))
    print("  target:", dict(lock))
    print("  impact graphic on the target:")
    for k, v in sorted(impacts.items()):
        print("    %4d %s" % (v, k))
    print("  sent on L:", dict(on_land) or "nothing (graphic and sound go out on C, delayed)")
    print("  sounds on C:")
    for k, v in sorted(snd.items()):
        print("    %4d %s" % (v, k))
    print("  the landing:")
    for k, v in sorted(land.items()):
        print("    %4d %s" % (v, k))
    print("  hitsplats on raiders while mystics live:")
    for k, v in sorted(explained.items()):
        print("    %4d %s" % (v, k))
    print("  damage by prayer at the end of C-1 (landed casts alone on their L):")
    for k, v in sorted(prayer.items()):
        print("    %s n=%d max=%s values=%s" % (k, len(v), max(v) if v else None, sorted(v)))
    print("  landed Vulnerability on the local raider:")
    for k, v in sorted(drain.items()):
        print("    %4d %s" % (v, k))


def section_melee(recs):
    print("== melee: every mystic seq that is not a cast, a block or the death")
    other = Counter()
    nohit = Counter()
    for rec in recs:
        for idx in mystics(rec):
            for t, tx in npc_events(rec, idx, "sequence"):
                nm = ident(tx)[0]
                if nm not in ("skeleton_update_mage_casting_swansong", "skeleton_update_defend", "skeleton_update_death"):
                    other[(nm, rec.path)] += 1
        cast_lands = set()
        for idx in mystics(rec):
            for c, p in casts_of(rec, idx):
                for dt in range(1, 6):
                    cast_lands.add((p["tgt_player"], c + dt))
        for name in rec.pl:
            for t, tx in pl_events(rec, name, "hit"):
                near = [i for i in mystics(rec) if rec.tile_at(rec.npc_tile[i], t) and foot(rec.tile_at(rec.npc_tile[i], t), rec.tile_at(rec.pl_tile[name], t)) <= 1
                        and rec.npc_add.get(i, (1 << 30,))[0] <= t <= rec.npc_del.get(i, 1 << 30)]
                if near and (name, t) not in cast_lands:
                    nohit[(rec.path, t, num(tx, "value"))] += 1
    print("  other seqs:", dict(other) or "none")
    print("  hits on a raider beside a mystic with no cast landing:", dict(nohit) or "none")


def section_blocks(recs):
    print("== blocks: skeleton_update_defend delay against the attacker's projectile")
    rows = Counter()
    hit_snd = Counter()
    for rec in recs:
        for idx in mystics(rec):
            for t, tx in npc_events(rec, idx, "sequence"):
                if "skeleton_update_defend" not in tx:
                    continue
                d = num(tx, "delay", 0)
                ends = sorted(set((p["name"], p["end"]) for p in projectiles(rec, t) if p["tgt_npc"] == idx))
                rows[("delay %d" % d, "= projectile end" if any(e == d for _, e in ends) else "projectiles %s" % (ends,))] += 1
            for t, tx in npc_events(rec, idx, "hit"):
                s = [sound_name(sid) for k, sid, delay, rng, loops, at in sounds(rec, t) if sid == 779]
                hit_snd[("hit on a mystic", "skelly_hit (779) synth that tick" if s else "no 779")] += 1
    for k, v in sorted(rows.items()):
        print("  %4d %s" % (v, k))
    for k, v in sorted(hit_snd.items()):
        print("  %4d %s" % (v, k))


def section_wake(recs):
    print("== wake: per mystic, ticks from when it was first sent")
    first_hit_to_cast = Counter()
    cast_before_op = Counter()
    for rec in recs:
        print("-- %s" % rec.path)
        for idx in mystics(rec):
            add_t, add_tile = rec.npc_add.get(idx, (None, None))
            if add_t is None:
                continue
            ops = sorted(t for t in rec.top for k, tx, subs in rec.top[t] if k.startswith("opnpc") and ("index=%d," % idx) in tx)
            proj_at = sorted(t for t in rec.top for p in projectiles(rec, t) if p["tgt_npc"] == idx)
            cs = casts_of(rec, idx)
            steps = [t for t, k, tx in rec.npc[idx] if k in ("walk", "run")]
            faces = [t for t, tx in npc_events(rec, idx, "face_pathingentity")]
            hits_on = sorted(t for t, tx in npc_events(rec, idx, "hit"))
            first_cast = cs[0][0] if cs else None
            raider = cs[0][1]["tgt_player"] if cs else first_raider(rec)
            gap = None
            if first_cast is not None:
                gap = foot(rec.tile_at(rec.npc_tile[idx], first_cast), rec.tile_at(rec.pl_tile[raider], first_cast - 1))
            h = hits_on[0] if hits_on else None
            print("  %s: sent %d; first op on it %s, first projectile at it %s, first hit on it H=%s, first face %s, first step %s, first cast %s (H%+d, raider gap %s)" % (
                mname(rec, idx), add_t, ops[0] if ops else None, proj_at[0] if proj_at else None, h,
                faces[0] if faces else None, steps[0] if steps else None, first_cast,
                (first_cast - h) if (first_cast is not None and h is not None) else 0, gap))
            first_hit_to_cast[(first_cast - h) if (first_cast is not None and h is not None) else None] += 1
            cast_before_op[("cast before any attack on it" if (first_cast is not None and ops and first_cast < ops[0]) else "first cast after it was attacked")] += 1
    print("  first cast against the first hit on it (ticks):", dict(first_hit_to_cast))
    print("  aggression:", dict(cast_before_op))


def section_movement(recs):
    print("== movement: every step (tick, toward the raider?, the raider's gap before -> after)")
    summary = Counter()
    for rec in recs:
        print("-- %s" % rec.path)
        for idx in mystics(rec):
            cs = set(c for c, p in casts_of(rec, idx))
            hit_on = sorted(t for t, tx in npc_events(rec, idx, "hit"))
            bits = []
            for t, k, tx in rec.npc[idx]:
                if k not in ("walk", "run"):
                    continue
                before = rec.tile_at(rec.npc_tile[idx], t - 1)
                after = rec.npc_tile[idx][t]
                raiders = rec.raiders_at(t - 1, before)
                if not raiders:
                    continue
                name, pt = min(raiders.items(), key=lambda kv: foot(before, kv[1]))
                g0, g1 = foot(before, pt), foot(after, pt)
                attacked = bool(hit_on) and hit_on[0] <= t
                toward = "toward" if g1 < g0 else ("away" if g1 > g0 else "level")
                summary[(toward, "after its first hit" if attacked else "before any hit on it")] += 1
                bits.append("%d %s %d->%d%s" % (t, toward, g0, g1, " cast" if t in cs else ""))
            print("  %s: %s" % (mname(rec, idx), ", ".join(bits) or "no steps"))
    for k, v in sorted(summary.items()):
        print("  %4d %s" % (v, k))


def section_death(recs):
    print("== death: D = the skeleton_update_death seq")
    for rec in recs:
        for idx in mystics(rec):
            ds = [t for t, tx in npc_events(rec, idx, "sequence") if "skeleton_update_death" in tx]
            if not ds:
                continue
            D = ds[0]
            sw = rec.tile_at(rec.npc_tile[idx], D)
            living = [i for i in mystics(rec) if i != idx and not any("skeleton_update_death" in tx and t <= D for t, tx in npc_events(rec, i, "sequence"))]
            print("-- %s %s D=%d sw %s, %d others alive" % (rec.path, mname(rec, idx), D, sw, len(living)))
            for t in range(D, D + 8):
                bits = []
                for k, tx, subs in rec.top.get(t, []):
                    if k.startswith("loc_add") or k.startswith("loc_del") or k.startswith("obj_add") or k.startswith("message"):
                        c = COORD.search(tx)
                        rel = (" at sw%+d,%+d" % (int(c.group(1)) - sw[0], int(c.group(2)) - sw[1])) if c else ""
                        bits.append("%s %s%s" % (k, re.sub(r", coord=\([^)]*\)", "", tx)[:110], rel))
                for k, sid, delay, rng, loops, at in sounds(rec, t):
                    if sid in (777, 779):
                        bits.append("%s %s" % (k, sound_name(sid)))
                for tt, k, tx in rec.npc[idx]:
                    if tt == t and k in ("sequence", "del", "hit", "enabled_ops", "transformation"):
                        bits.append("mystic %s %s" % (k, tx[:50]))
                if bits:
                    print("   D%+d  %s" % (t - D, " | ".join(bits)))


ROT = {
    0: lambda x, z: (x, z),
    1: lambda x, z: (z, 31 - x),
    2: lambda x, z: (31 - x, 31 - z),
    3: lambda x, z: (31 - z, x),
}


def room_frame(rec):
    """(turn, offset) with instance = ROT[turn](template local) + offset,
    solved from where the room's three portals land; None before the clear."""
    seen = {}
    for t in sorted(rec.top):
        for k, tx, subs in rec.top[t]:
            if k.startswith("loc_add"):
                nm = ident(tx)[0]
                if nm in PORTALS and nm not in seen:
                    c = COORD.search(tx)
                    seen[nm] = (int(c.group(1)), int(c.group(2)))
    if len(seen) < 3:
        return None
    for turn, f in ROT.items():
        offs = set()
        for nm, inst in seen.items():
            r = f(*PORTALS[nm])
            offs.add((inst[0] - r[0], inst[1] - r[1]))
        if len(offs) == 1:
            return turn, offs.pop()
    return None


def to_template(frame, inst, size=1):
    """The template SW tile of a size-N thing whose instance SW tile is `inst`."""
    turn, off = frame
    inv = {0: 0, 1: 3, 2: 2, 3: 1}[turn]
    corners = [(inst[0] + dx - off[0], inst[1] + dz - off[1]) for dx in (0, size - 1) for dz in (0, size - 1)]
    back = [ROT[inv](x, z) for x, z in corners]
    return (min(b[0] for b in back), min(b[1] for b in back))


def section_room(recs):
    print("== room: template room-local tiles (ccw variant, origin 3264, 5248)")
    for rec in recs:
        frame = room_frame(rec)
        if frame is None:
            print("-- %s: no portals, frame unknown" % rec.path)
            continue
        print("-- %s turn %d (quarter turns), offset %s" % (rec.path, frame[0], frame[1]))
        for t in sorted(rec.top):
            for k, tx, subs in rec.top[t]:
                if k.startswith("loc_add"):
                    nm = ident(tx)[0]
                    if nm in PORTALS or nm == "raids_skeletalmystics_symbol" or (nm == "invisible_type8_nonblocking" and any(t == rec.npc_del.get(i) for i in mystics(rec))):
                        c = COORD.search(tx)
                        inst = (int(c.group(1)), int(c.group(2)))
                        size = 2 if nm == "raids_skeletalmystics_symbol" else 1
                        print("  %d %s rotation %s: template %s" % (t, nm, num(tx, "rotation"), to_template(frame, inst, size)))
        for idx in mystics(rec):
            add_t, add_tile = rec.npc_add[idx]
            moved = [t for t, k, tx in rec.npc[idx] if k in ("walk", "run") and t <= add_t]
            print("  %s first sent %d at template sw %s (creationcycle %s)" % (
                mname(rec, idx), add_t, to_template(frame, add_tile, SIZE),
                num([tx for t, k, tx in rec.npc[idx] if k == "add"][0], "creationcycle")))


SECTIONS = {"attacks": section_attacks, "melee": section_melee, "blocks": section_blocks, "wake": section_wake,
            "movement": section_movement, "death": section_death, "room": section_room}
ORDER = ["attacks", "melee", "blocks", "wake", "movement", "death", "room"]


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
    for name in ([args.section] if args.section else ORDER):
        SECTIONS[name](recs)


if __name__ == "__main__":
    main()
