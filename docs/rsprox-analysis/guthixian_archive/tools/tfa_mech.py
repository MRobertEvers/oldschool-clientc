#!/usr/bin/env python3
"""Every splat on a raider in the Archive, put down to the mechanic that dealt
it, from --all-players transcripts (the party runs are spectated, so every
raider's splats are another player's and their prayers are not on the wire).

    python3 tfa_mech.py <all-players txt>... --room dreadnoth            the table
    python3 tfa_mech.py <txt>... --room dreadnoth --rows                  every splat
    python3 tfa_mech.py <txt>... --room dreadnoth --rows --only '?'       the unexplained ones

Per room, RULES[room] is an ordered list of (mechanic, test); the first test
that claims a splat names it. A test sees the splat (tick, player, tile, type,
value) and the room's events. Tiles are the transcript's (template) tiles.
"""
import argparse
import re
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]")
COORD = r"coord=\((\d+), (\d+), (\d+)\)"
NPCHDR = re.compile(r"^ {8}\[(\w+)\] npc=\(index=(\d+), id=([a-z0-9_]+) \(\d+\)(?:, multinpc=[^)]*\))?, " + COORD + r"\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?")
NPCADD = re.compile(r"^ {8}\[add\] npc=\(index=(\d+), id=([a-z0-9_]+) \(\d+\)(?:, multinpc=[^)]*\))?, " + COORD)
PLHDR = re.compile(r"^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]*), " + COORD + r"\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?")
SEQ = re.compile(r"\[sequence\] id=([a-z0-9_]+)")
SPOT = re.compile(r"\[spotanim\](?: slot=\d+,)? id=([a-z0-9_]+)")
HIT = re.compile(r"\[hit\] id=(\d+), value=(-?\d+)")
MAPANIM = re.compile(r"^ {4}\[map_anim\] id=([a-z0-9_]+).*?" + COORD)
PROJ = re.compile(r"^ {4}\[(?:map_)?proj\w*\] id=([a-z0-9_]+) \(\d+\), starttime=(-?\d+), endtime=(-?\d+)")
TARGETC = re.compile(r"^ {8}\[target\] (?:" + COORD + r")?(?:.*player=\(index=\d+, name=([^,]*))?")
SOURCE = re.compile(r"^ {8}\[source\] " + COORD)
LOCADD = re.compile(r"^ {4}\[loc_add_change\w*\] id=([a-z0-9_]+) \(\d+\), " + COORD)
LOCDEL = re.compile(r"^ {4}\[loc_del\w*\] " + COORD)
SAY = re.compile(r'\[say\] text="([^"]*)"')


class Room:
    def __init__(self):
        self.ev = defaultdict(list)    # tick -> [(kind, id, tile, extra)]
        self.splats = []               # (tick, name, tile, type, value)
        self.locs = {}                 # tile -> (loc, since)
        self.loc_hist = defaultdict(list)  # tile -> [(tick, loc or None)]
        self.pos = defaultdict(dict)   # tick -> name -> tile (after moves)
        self.npcpos = defaultdict(dict)    # tick -> npc type -> [tiles]
        self.forms = []                # (tick, new type) for every transformation

    def loc_at(self, tile, tick):
        cur = None
        for t, loc in self.loc_hist.get(tile, ()):
            if t <= tick:
                cur = loc
        return cur


def parse(path):
    r = Room()
    tick = 0
    cur = None
    lastproj = None
    where = {}
    npcwhere = {}
    for line in open(path, errors="replace"):
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            cur = None
            continue
        m = NPCADD.match(line)
        if m:
            tile = (int(m.group(3)), int(m.group(4)))
            npcwhere[m.group(1)] = (m.group(2), tile)
            cur = ("npc", m.group(2), tile)
            r.ev[tick].append(("add", m.group(2), tile, None))
            r.npcpos[tick].setdefault(m.group(2), []).append(tile)
            continue
        m = NPCHDR.match(line)
        if m:
            tile = (int(m.group(4)), int(m.group(5)))
            if m.group(7):
                tile = (int(m.group(7)), int(m.group(8)))
            npcwhere[m.group(2)] = (m.group(3), tile)
            cur = ("npc", m.group(3), tile)
            r.npcpos[tick].setdefault(m.group(3), []).append(tile)
            continue
        m = PLHDR.match(line)
        if m:
            tile = (int(m.group(4)), int(m.group(5)))
            if m.group(7):
                tile = (int(m.group(7)), int(m.group(8)))
            where[m.group(3)] = tile
            r.pos[tick][m.group(3)] = tile
            cur = ("pl", m.group(3), tile)
            continue
        if line.startswith("    [") and not line.startswith("        "):
            cur = None
            lastproj = None
            m = MAPANIM.match(line)
            if m:
                r.ev[tick].append(("map", m.group(1), (int(m.group(2)), int(m.group(3))), None))
                continue
            m = PROJ.match(line)
            if m:
                lastproj = {"id": m.group(1), "start": int(m.group(2)), "end": int(m.group(3)),
                            "tick": tick, "target": None, "player": None, "source": None}
                r.ev[tick].append(("proj", m.group(1), None, lastproj))
                continue
            m = LOCADD.match(line)
            if m:
                tile = (int(m.group(2)), int(m.group(3)))
                r.loc_hist[tile].append((tick, m.group(1)))
                r.ev[tick].append(("loc", m.group(1), tile, None))
                continue
            m = LOCDEL.match(line)
            if m:
                r.loc_hist[(int(m.group(1)), int(m.group(2)))].append((tick, None))
            continue
        if lastproj is not None:
            m = TARGETC.match(line)
            if m and line.startswith("        [target]"):
                if m.group(1):
                    lastproj["target"] = (int(m.group(1)), int(m.group(2)))
                if m.group(4):
                    lastproj["player"] = m.group(4)
                continue
            m = SOURCE.match(line)
            if m:
                lastproj["source"] = (int(m.group(1)), int(m.group(2)))
                continue
        if not cur or not line.startswith("            "):
            continue
        s = line.strip()
        if cur[0] == "npc":
            m = SEQ.match(s)
            if m:
                r.ev[tick].append(("seq", f"{cur[1]} {m.group(1)}", cur[2], None))
            m = SPOT.match(s)
            if m:
                r.ev[tick].append(("spot", f"{cur[1]} {m.group(1)}", cur[2], None))
            m = SAY.search(s)
            if m:
                r.ev[tick].append(("say", f"{cur[1]} {m.group(1)}", cur[2], None))
            m = re.match(r"\[transformation\] id=([a-z0-9_]+)", s)
            if m:
                r.ev[tick].append(("form", m.group(1), cur[2], None))
                r.forms.append((tick, m.group(1)))
        else:
            m = HIT.match(s)
            if m:
                r.splats.append((tick, cur[1], where.get(cur[1]), int(m.group(1)), int(m.group(2))))
            m = SPOT.match(s)
            if m:
                r.ev[tick].append(("plspot", m.group(1), cur[2], cur[1]))
    return r


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1])) if a and b else 99


def box(sw, size, tile):
    """Chebyshev distance from a tile to a size x size box at sw."""
    if not sw or not tile:
        return 99
    dx = max(sw[0] - tile[0], 0, tile[0] - (sw[0] + size - 1))
    dz = max(sw[1] - tile[1], 0, tile[1] - (sw[1] + size - 1))
    return max(dx, dz)


def events(r, tick, kind=None, ident=None):
    for e in r.ev.get(tick, ()):
        if kind and e[0] != kind:
            continue
        if ident and not re.search(ident, e[1]):
            continue
        yield e


def landing(p):
    return p["tick"] + (p["end"] + 15) // 30


# ---------------------------------------------------------------------------
# The rules. Each: (mechanic, fn(r, splat) -> bool). The splat is
# (tick, name, tile, type, value).
# ---------------------------------------------------------------------------
def proj_at_player(ident, slack=1):
    def f(r, s):
        for t in range(s[0] - 12, s[0] + 1):
            for e in events(r, t, "proj", ident):
                p = e[3]
                if p["player"] == s[1] and abs(landing(p) - s[0]) <= slack:
                    return True
        return False
    return f


def proj_on_tile(ident, slack=0, reach=0):
    def f(r, s):
        for t in range(s[0] - 12, s[0] + 1):
            for e in events(r, t, "proj", ident):
                p = e[3]
                if p["target"] and cheb(p["target"], s[2]) <= reach and abs(landing(p) - s[0]) <= slack:
                    return True
        return False
    return f


def seq_then(ident, dts, reach=None, size=1, everyone=True):
    """An npc seq at splat - dt; the raider within `reach` of its box (any
    raider if reach is None)."""
    def f(r, s):
        for dt in dts:
            for e in events(r, s[0] - dt, "seq", ident):
                if reach is None or box(e[2], size, s[2]) <= reach:
                    return True
        return False
    return f


def spot_then(ident, dts, reach=None, size=1):
    def f(r, s):
        for dt in dts:
            for e in events(r, s[0] - dt, "spot", ident):
                if reach is None or box(e[2], size, s[2]) <= reach:
                    return True
        return False
    return f


def map_on_tile(ident, dts, reach=0):
    def f(r, s):
        for dt in dts:
            for e in events(r, s[0] - dt, "map", ident):
                if cheb(e[2], s[2]) <= reach:
                    return True
        return False
    return f


def on_loc(ident):
    def f(r, s):
        loc = r.loc_at(s[2], s[0] - 1) if s[2] else None
        return bool(loc and re.search(ident, loc))
    return f


def npc_at(r, typ, tick):
    for t in range(tick, tick - 30, -1):
        tiles = r.npcpos.get(t, {}).get(typ)
        if tiles:
            return tiles[-1]
    return None


def under(typ, size, ident, dt):
    """A seq `ident` at splat - dt, and the raider inside the npc's box then."""
    def f(r, s):
        for e in events(r, s[0] - dt, "seq", ident):
            return box(npc_at(r, typ, s[0]), size, s[2]) == 0
        return False
    return f


def on_segment(a, b, t):
    """t on the straight (row, column or diagonal) segment from a to b."""
    if not a or not b or not t:
        return False
    dx, dz = b[0] - a[0], b[1] - a[1]
    n = max(abs(dx), abs(dz))
    if n == 0:
        return t == a
    for i in range(n + 1):
        if (a[0] + round(dx * i / n), a[1] + round(dz * i / n)) == t:
            return True
    return False


def proj_line(ident, dts):
    """A map projectile thrown at splat - dt whose source..target segment the
    raider stands on."""
    def f(r, s):
        for dt in dts:
            for e in events(r, s[0] - dt, "proj", ident):
                p = e[3]
                if on_segment(p["source"], p["target"], s[2]):
                    return True
        return False
    return f


def splat_type(*types):
    def f(r, s):
        return s[3] in types
    return f


def both(*fs):
    def f(r, s):
        return all(g(r, s) for g in fs)
    return f


# The raiders' own kit: a ruby bolt's Blood Forfeit costs its shooter (the
# spot on the npc it hit, that tick or the one before).
KIT = [("kit: ruby bolt (the shooter's own)", spot_then(r"xbows_blood_sacrifice", [0, 1]))]

RULES = {
    "dreadnoth": KIT + [
        ("slam (J+1, every raider)", seq_then(r"tfa_jelly_boss dreadnoth_jump01$", [1])),
        ("slam shot proj01", proj_at_player(r"vfx_dreadnoth_ranged_proj01")),
        ("landing, under his 5x5", under("tfa_jelly_boss", 5, r"tfa_jelly_boss dreadnoth_jump02$", 3)),
        ("landing (jump02 +3, every raider)", seq_then(r"tfa_jelly_boss dreadnoth_jump02$", [3])),
        ("landing proj02 on the tile", proj_on_tile(r"vfx_dreadnoth_ranged_proj02", slack=1)),
        ("melee", seq_then(r"tfa_jelly_boss dreadnoth_melee01$", [0], reach=2, size=5)),
        ("blood tile", both(on_loc(r"tfa_jelly_boss_damage_tile"), lambda r, s: s[4] > 0)),
        ("jelly swing", seq_then(r"tfa_jelly_minion jelly_attack$", [0], reach=2)),
    ],
    "xarth": KIT + [
        ("burn (type 74)", splat_type(74)),
        ("fire tile (type 67)", splat_type(67)),
        ("fireball at the raider", proj_at_player(r"luc2_undead_demon_fireball_proj_big")),
        ("arrow at the raider", proj_at_player(r"vfx_xarth_attack_ranged_projectile_01")),
        ("melee", seq_then(r"tfa_xarth xarth_attack_melee_01$", [0, 1], reach=1, size=3)),
        ("the AoE (aoe seq +4)", seq_then(r"tfa_xarth xarth_attack_aoe_01$", [4])),
        ("the blast (blast seq +0..2)", seq_then(r"tfa_xarth xarth_attack_flame_blast_01$", [0, 1, 2])),
    ],
    "guardian": KIT + [
        ("heal (type 6)", splat_type(6)),
        ("poison (type 65)", splat_type(65)),
        ("ranged shot at the raider", proj_at_player(r"vfx_guardian_ranged_projanim01")),
        ("melee (beside him)", seq_then(r"tfa_guardian npc_archive_guardian_melee_attack01$", [0, 1], reach=1, size=5)),
        ("swoosh (+0/+1)", seq_then(r"npc_archive_guardian_swoosh_attack01$", [0, 1], reach=1, size=5)),
        ("tentacle (beside one)", lambda r, s: any(box(t, 1, s[2]) <= 1 for dt in (0, 1) for t in r.npcpos.get(s[0] - dt, {}).get("archive_guardian_tentacle", []))),
    ],
    "ezekiel": KIT + [
        ("heal (type 6)", splat_type(6)),
        ("sword burn (type 67)", splat_type(67)),
        ("star (explosion on the tile, +0..5)", map_on_tile(r"vfx_ezekiel_star_explosion01", [0, 1, 2, 3, 4, 5], 0)),
        ("melee wave (dust on the tile)", map_on_tile(r"tfa_wave_dust$", [0], 0)),
        ("special (special01 +3/+5)", seq_then(r"tfa_ezekiel npc_ezekiel_special01$", [3, 5])),
        ("sword (beside a sword)", lambda r, s: any(box(t, 1, s[2]) <= 1 for dt in (0, 1) for t in r.npcpos.get(s[0] - dt, {}).get("tfa_ezekiel_sword", []))),
        ("melee (seq +0)", seq_then(r"tfa_ezekiel npc_ezekiel_melee01$", [0])),
    ],
    "bork": KIT + [
        ("heal (type 6)", splat_type(6)),
        ("napalm tile (type 67)", splat_type(67)),
        ("burn (type 74)", splat_type(74)),
        ("smork shot: range", proj_at_player(r"vfx_smork_range_attack_projanim01")),
        ("smork shot: mage", proj_at_player(r"vfx_smork_mage_attack_projanim01")),
        ("big bomb (on its tile)", proj_on_tile(r"cannonball_travel_big", slack=1, reach=1)),
        ("napalm landing (on its tile)", map_on_tile(r"vfx_smork_napalm_(fire|splat)_spotanim01", [0], 0)),
        ("slam (stomp dust, L+17..26)", both(map_on_tile(r"tfa_wave_dust_short", [0], 0),
                                            lambda r, s: any(True for dt in range(1, 12) for e in events(r, s[0] - dt, "seq", r"tfa_bork npc_bork_enraged_stomp_attack01$")))),
        ("drum beat (dust on the tile)", map_on_tile(r"tfa_wave_dust_short", [0], 0)),
        ("drum beat (gong +0/+2, beside him)", seq_then(r"tfa_bork npc_bork_gong_melee01$", [0, 2], reach=1, size=4)),
    ],
    "ocellus1": KIT + [
        ("heal (type 6)", splat_type(6)),
        ("p1 strike (explode_floor +4)", lambda r, s: any(True for e in events(r, s[0] - 4, "map", r"vfx_ocellus_p1_explode_floor"))),
        ("p1 line chip (on a crack)", map_on_tile(r"vfx_ocellus_p1_floor_crack", [0, 1, 2], 0)),
        ("p1 volley: magic shot", proj_at_player(r"vfx_ocellus_magic_projanim01")),
        ("p1 volley: fire shot", proj_at_player(r"vfx_ocellus_fire_attack01_projanim")),
        ("p1 volley: water shot", proj_at_player(r"vfx_ocellus_water_attack01_projanim")),
        ("p1 projectile at the raider (other)", proj_at_player(r"vfx_ocellus")),
    ],
    "ocellus2": KIT + [
        ("heal (type 6)", splat_type(6)),
        ("p2 fired line (on the portal shot's row)", proj_line(r"vfx_ocellus_portal_projectile", [0, 1, 2])),
        ("p2 pool (aoe_dodge on the tile)", map_on_tile(r"vfx_ocellus_p2_aoe_dodge", [0, 1, 2, 3, 4, 5, 6], 0)),
    ],
    "ocellus3": KIT + [
        ("heal (type 6)", splat_type(6)),
        ("p3 void patch (on the loc)", both(on_loc(r"tfa_ocellus_void_degen"), lambda r, s: s[4] > 0)),
        ("p3 spin loop/out (on the seq)", seq_then(r"tfa_ocellus_p3 npc_ocellus_phase3_prayer_(loop|out)$", [0])),
        ("p3 unleash out (+0/+1)", seq_then(r"tfa_ocellus_p3 npc_ocellus_phase3_unleash_out$", [0, 1])),
        ("p3 slash (spot tick, on the tile)", map_on_tile(r"vfx_ocellus_p3_shadow_slash_spotanim01", [0], 0)),
        ("p3 superior (spot tick, on the tile)", map_on_tile(r"vfx_ocellus_p3_shadow_slash_superior_spotanim01", [0], 0)),
        ("p3 melee basic (beside him)", seq_then(r"tfa_ocellus_p3 npc_ocellus_phase3_melee_basic$", [0], reach=1, size=3)),
        ("p3 melee slash (+4)", seq_then(r"tfa_ocellus_p3 npc_ocellus_phase3_melee_slash$", [4])),
        ("p3 spin (prayer_in +1)", seq_then(r"tfa_ocellus_p3 npc_ocellus_phase3_prayer_in$", [1])),
        ("p3 slash (+3/+4 on the tile)", map_on_tile(r"vfx_ocellus_p3_shadow_slash_spotanim01", [3, 4], 0)),
        ("p3 superior slash (+5, the band)", map_on_tile(r"vfx_ocellus_p3_shadow_slash_superior_spotanim01", [5], 1)),
        ("p3 void bomb (impact on the tile)", map_on_tile(r"vfx_ocellus_quadrant_bomb_impact_01", [0, 1], 1)),
        ("p3 falling debris (+2 on the tile)", map_on_tile(r"vfx_ocellus_falling_debris", [2], 0)),
        ("p3 volley magic shot", proj_at_player(r"vfx_ocellus_magic_projanim01")),
        ("p3 projectile at the raider (other)", proj_at_player(r"vfx_ocellus")),
        ("p3 unleash pools", map_on_tile(r"vfx_ocellus_p3_unleash|vfx_ocellus_p2_aoe", [0, 1, 2, 3, 4], 0)),
    ],
    "ocellus4": KIT + [
        ("heal (type 6)", splat_type(6)),
        ("p4 wind (levitating +0)", spot_then(r"vfx_ocellus_soul_fragment_ocellus_levitating_idle", [0])),
        ("p4 hybrid (+5)", spot_then(r"vfx_ocellus_phase3_melee_hybrid_(magic|ranged)", [5])),
        ("p4 rift despawn", spot_then(r"vfx_ocellus_voidflare_(buff|default)_despawn", [0, 1, 2, 3, 4])),
    ],
    "ocellus": KIT + [
        ("heal (type 6)", splat_type(6)),
        ("p1 strike (explode_floor +4)", lambda r, s: any(True for e in events(r, s[0] - 4, "map", r"vfx_ocellus_p1_explode_floor"))),
        ("p1 line chip (on a crack)", map_on_tile(r"vfx_ocellus_p1_floor_crack", [0, 1, 2], 0)),
        ("p1 volley: magic shot", proj_at_player(r"vfx_ocellus_magic_projanim01")),
        ("p1 volley: fire shot", proj_at_player(r"vfx_ocellus_fire_attack01_projanim")),
        ("p1 volley: water shot", proj_at_player(r"vfx_ocellus_water_attack01_projanim")),
        ("p1/p3 projectile at the raider (other)", proj_at_player(r"vfx_ocellus")),
        ("p2 pool (aoe_dodge +4 on the tile)", map_on_tile(r"vfx_ocellus_p2_aoe_dodge", [4], 0)),
        ("p2 fired line (portal projectile on the tile)", proj_on_tile(r"vfx_ocellus_portal_projectile", slack=1)),
        ("p3 slash (+3/+4 on the tile)", map_on_tile(r"vfx_ocellus_p3_shadow_slash_spotanim01", [3, 4], 0)),
        ("p3 superior slash (+5 on the tile)", map_on_tile(r"vfx_ocellus_p3_shadow_slash_superior_spotanim01", [5], 0)),
        ("p3 void bomb (impact on the tile)", map_on_tile(r"vfx_ocellus_quadrant_bomb_impact_01", [0, 1], 1)),
        ("p3 falling debris (+2 on the tile)", map_on_tile(r"vfx_ocellus_falling_debris", [2], 0)),
        ("p3 melee basic (beside him)", seq_then(r"tfa_ocellus_p3 npc_ocellus_phase3_melee_basic$", [0], reach=1, size=3)),
        ("p3 melee slash (+4)", seq_then(r"tfa_ocellus_p3 npc_ocellus_phase3_melee_slash$", [4])),
        ("p3 spin (prayer_in +1)", seq_then(r"tfa_ocellus_p3 npc_ocellus_phase3_prayer_in$", [1])),
        ("p3 hybrid (+5)", spot_then(r"vfx_ocellus_phase3_melee_hybrid_(magic|ranged)", [5])),
        ("p4 wind (levitating +0)", spot_then(r"vfx_ocellus_soul_fragment_ocellus_levitating_idle", [0])),
        ("p3 unleash pools", map_on_tile(r"vfx_ocellus_p3_unleash|vfx_ocellus_p2_aoe", [0, 1, 2, 3, 4], 0)),
    ],
}
ROOMS = {
    "guardian": r"tfa_guardian",
    "ezekiel": r"tfa_ezekiel",
    "bork": r"tfa_bork|tfa_smork",
    "dreadnoth": r"tfa_jelly_boss",
    "xarth": r"tfa_xarth$",
    "ocellus": r"tfa_ocellus_(p1|p2|p3)",
    "ocellus1": r"tfa_ocellus_p1$",
    "ocellus2": r"tfa_ocellus_p2$",
    "ocellus3": r"tfa_ocellus_p3$",
    "ocellus4": r"tfa_ocellus_p3$",
}


def end_tick(r):
    for t in sorted(r.ev):
        for e in r.ev[t]:
            if e[0] == "say" and e[1].startswith("tfa_ocellus_p3 End..."):
                return t
    return None


def windows(r, pattern):
    rx = re.compile(pattern)
    ticks = sorted(t for t, d in r.npcpos.items() if any(rx.match(x) for x in d))
    out = []
    for t in ticks:
        if out and t - out[-1][1] <= 20:
            out[-1][1] = t
        else:
            out.append([t, t])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("txt", nargs="+")
    ap.add_argument("--room", required=True)
    ap.add_argument("--rows", action="store_true")
    ap.add_argument("--only")
    ap.add_argument("--group", help="regex of tells to group the unexplained splats by (the nearest, dt 0..8)")
    a = ap.parse_args()
    table = defaultdict(list)
    rows = []
    for path in a.txt:
        r = parse(path)
        name = path.rsplit("/", 1)[-1]
        wins = windows(r, ROOMS[a.room])
        # Ocellus's phases by his transformations (the npc header keeps his
        # first type): p1 .. p2 .. p3, phase 4 from "End...".
        if a.room in ("ocellus1", "ocellus2", "ocellus3", "ocellus4"):
            room_w = windows(r, r"tfa_ocellus_")
            fr = {}
            for t, f in r.forms:
                fr.setdefault(f, t)
            p1, p2, p3 = fr.get("tfa_ocellus_p1"), fr.get("tfa_ocellus_p2"), fr.get("tfa_ocellus_p3")
            if not p1:
                # his first form comes with "Now... ascension..." (a re-add, no transformation)
                p1 = next((t for t in sorted(r.ev) for e in r.ev[t] if e[0] == "say" and "Now... ascension" in e[1]), None)
            last = max((w[1] for w in room_w), default=0)
            end = end_tick(r) or last
            spans = {"ocellus1": (p1, p2), "ocellus2": (p2, p3), "ocellus3": (p3, end - 1), "ocellus4": (end, last)}
            a0, a1 = spans[a.room]
            wins = [[a0, a1]] if a0 and a1 else []
        # Phase 3 runs to "End..."; phase 4 is the same form after it.
        for w0, w1 in wins:
            for s in r.splats:
                if not (w0 <= s[0] <= w1 + (0 if a.room.startswith('ocellus') else 4)) or s[3] not in (13, 17, 12, 16, 67, 74, 65, 5, 6):
                    continue
                mech = "?"
                for label, fn in RULES.get(a.room, ()):
                    if fn(r, s):
                        mech = label
                        break
                if mech == "?" and a.group:
                    best = None
                    for dt in range(0, 9):
                        for e in r.ev.get(s[0] - dt, ()):
                            ident = e[1] if e[0] != "proj" else "proj " + e[1] + (" ->me" if e[3]["player"] == s[1] else "")
                            if e[0] in ("seq", "map", "spot", "proj", "say", "loc") and re.search(a.group, ident):
                                best = f"{ident} @-{dt}"
                                break
                        if best:
                            break
                    mech = "? " + (best or "(nothing)")
                table[mech].append(s)
                if a.rows and (not a.only or a.only == mech):
                    near = []
                    for dt in range(0, 7):
                        for e in r.ev.get(s[0] - dt, ()):
                            if e[0] in ("seq", "map", "spot") or (e[0] == "proj"):
                                ident = e[1] if e[0] != "proj" else f"proj {e[1]}->{(e[3]['player'] or e[3]['target'])} L{landing(e[3]) - s[0]:+d}"
                                near.append(f"{ident}@-{dt}")
                    rows.append(f"{name} [{s[0]}] {s[1]:14s} {str(s[2]):14s} t{s[3]:<3d} {s[4]:3d}  {mech:28s} " + ", ".join(near[:8]))
    if a.rows:
        print("\n".join(rows))
        return
    for mech, ss in sorted(table.items(), key=lambda x: -len(x[1])):
        v = [s[4] for s in ss]
        nz = sorted(x for x in v if x > 0)
        types = Counter(s[3] for s in ss)
        print(f"{mech:36s} n={len(v):4d} zero={len(v) - len(nz):4d} max={max(v):3d} types={dict(types)}")
        print(f"{'':36s} nonzero: {dict(sorted(Counter(nz).items()))}")


main()
