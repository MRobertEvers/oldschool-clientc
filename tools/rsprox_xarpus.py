#!/usr/bin/env python3
"""Xarpus as Jagex's server sent him, read out of RSProx transcripts: the
phases and their hand-offs, the exhumeds and their heal orbs, the acid spit
and its pools, the stare, the guano, and his death.

    python3 tools/rsprox_xarpus.py ~/.rsprox/binary/archive/{3299,3285,788}.txt
    python3 tools/rsprox_xarpus.py --section exhumed FILES...

Parsing is tools/rsprox_nylocas.py's; player tiles and projectile rows are
tools/rsprox_maiden.py's. Heights print in our units (a v2 packet's raw / 4).
Tile offsets are from Xarpus's south-west tile (he is 5x5).
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsprox_nylocas as N  # noqa: E402
import rsprox_maiden as M  # noqa: E402

ANCHOR = "8338-8341"
ID = re.compile(r"\bid=(?:[^\s,(]+ \()?(-?\d+)")
ROT = re.compile(r"rotation=(-?\d+)")


def nid(text):
    m = ID.search(text)
    return int(m.group(1)) if m else None


def nrot(text):
    m = ROT.search(text)
    return int(m.group(1)) if m else None


def boss(f):
    for n in list(f.npcs.values()) + f.retired:
        if n.types[0][1].startswith("tob_xarpus_static"):
            return n
    return None


def first(b, kind, pat=""):
    for t, k, d in b.events:
        if k == kind and re.search(pat, d):
            return t
    return None


def all_of(b, kind, pat=""):
    return [t for t, k, d in b.events if k == kind and re.search(pat, d)]


NAMES = {}


def tops(f, prefix, pat=None):
    """Top-level events of a kind; `pat` is matched against the text with any
    bare loc / spotanim / sound id named (rev 227 prints ids only)."""
    out = []
    for t in sorted(f.top):
        for kind, text, subs in f.top[t]:
            full = text
            if pat is not None and kind.startswith(("loc_add", "map_anim", "sound")):
                space = "loc" if kind.startswith("loc") else ("spotanim" if kind == "map_anim" else "sound")
                full = text + " " + (N.named(text, space, NAMES) or "")
            if kind.startswith(prefix) and (pat is None or re.search(pat, full)):
                out.append((t, kind, text, subs))
    return out


def named(text, space, names):
    return N.named(text, space, names) or ""


def show(label, c):
    print("  %s: %s" % (label, dict(sorted(c.items(), key=lambda kv: repr(kv[0]).zfill(12)))))


def sw_of(b, t):
    c = b.coord_at(t)
    return c


def party(f, t=None):
    """Raiders with an orb on the HUD: the varbits 6442..6446 seen non-zero in the window."""
    seen = set()
    for tt in f.top:
        for kind, text, subs in f.top[tt]:
            if kind == "varbit":
                m = re.search(r"\((644[2-6])\), old=-?\d+, new=(-?\d+)", text) or re.search(r"id=(644[2-6]), old=-?\d+, new=(-?\d+)", text)
                if m and int(m.group(2)) > 0:
                    seen.add(m.group(1))
    return len(seen)


def hud_permille(f, t):
    """The HUD bar (varbit 6448, permille of his pool) last set at or before `t`."""
    best = None
    for tt in sorted(f.top):
        if tt > t:
            break
        for kind, text, subs in f.top[tt]:
            if kind == "varbit" and "6448" in text:
                m = re.search(r"new=(-?\d+)", text)
                if m:
                    best = int(m.group(1))
    return best


def hp_fill(b, t):
    fill = None
    for tt, k, d in b.events:
        if tt > t:
            break
        if k == "headbar":
            fill = d
    return fill


# ---------------------------------------------------------------- phases
def phases(fights, names):
    rows = defaultdict(Counter)
    for f in fights:
        b = boss(f)
        if not b:
            continue
        add = b.added
        feed = first(b, "retype", "feeding")
        combat = first(b, "retype", "combat")
        dead = first(b, "retype", "xarpus_death")
        fly = first(b, "seq", "fly_up")
        spits = all_of(b, "seq", "attack_ranged")
        say = first(b, "say", "Scree")
        stare = [t for t, _, text, _ in tops(f, "message_game", "begins to stare")]
        exdel = [t for t, _, text, _ in tops(f, "loc_del") if "shape=22" in text]
        exadd = [t for t, _, text, _ in tops(f, "loc_add", "tob_xarpus_exhumed")]
        songs = [(t, nid(text)) for t, _, text, _ in tops(f, "midi_song")]
        if feed is not None and add is not None:
            rows["add -> start (feeding)"][feed - add] += 1
        if feed is not None and exadd:
            rows["start -> first exhumed"][exadd[0] - feed] += 1
        if combat is not None and exadd:
            last_close = max([t for t in exdel if t <= combat] or [0])
            rows["last exhumed closes -> combat"][combat - last_close] += 1
        if fly is not None and combat is not None:
            rows["fly_up seq - combat retype"][fly - combat] += 1
        if combat is not None and spits:
            rows["combat -> first spit seq"][spits[0] - combat] += 1
        if say is not None:
            rows["screech: HUD permille at the screech"][hud_permille(f, say)] += 1
            if stare:
                rows["screech -> 'begins to stare'"][stare[0] - say] += 1
            p2 = [t for t in spits if t < say]
            if p2:
                rows["last P2 spit -> screech"][say - p2[-1]] += 1
        if feed is not None:
            for t, i in songs:
                rows["song after start"][(i, t - feed)] += 1
        rows["party"][party(f, (feed or add or f.span[0]) + 1)] += 1
    print("== phases")
    for k, c in rows.items():
        show(k, c)


# ---------------------------------------------------------------- exhumed
def exhumed(fights, names):
    count = Counter()
    gap = Counter()
    life = Counter()
    first_orb = Counter()
    orb_gap = Counter()
    orbs_per = Counter()
    shape = Counter()
    flight = Counter()
    target = Counter()
    heal_land = Counter()
    heal_val = Counter()
    shot_delay = Counter()
    swallow = Counter()
    offs = Counter()
    blocked = Counter()
    stood = Counter()
    for f in fights:
        b = boss(f)
        if not b:
            continue
        feed = first(b, "retype", "feeding")
        combat = first(b, "retype", "combat")
        if feed is None or combat is None:
            continue
        adds = [(t, N.coord_of(text)) for t, _, text, _ in tops(f, "loc_add", "tob_xarpus_exhumed") if feed <= t <= combat]
        dels = [(t, N.coord_of(text)) for t, _, text, _ in tops(f, "loc_del") if "shape=22" in text and feed <= t <= combat + 2]
        n = party(f, feed + 1)
        count[(n, len(adds))] += 1
        for (a, _), (c, _) in zip(adds, adds[1:]):
            gap[(n, c - a)] += 1
        heals = [(t, d) for t, k, d in b.events if k == "hit" and "id=6," in d]
        hit_ticks = Counter(t for t, _ in heals)
        for t, d in heals:
            heal_val[(n, N.num(d, "value"))] += 1
        sw = b.coord_at(feed)
        for a, at in adds:
            end = next((t for t, c in dels if c == at and t > a), None)
            if end is not None:
                life[end - a] += 1
            if sw:
                offs[(at[0] - sw[0], at[1] - sw[1])] += 1
            orbs = []
            for t in range(a, (end or a + 20) + 1):
                for row in M.proj_rows(f, t, names):
                    if row[0] and "energyorb" in row[0] and row[7] == at:
                        orbs.append((t, row))
            orbs_per[len(orbs)] += 1
            if orbs:
                first_orb[orbs[0][0] - a] += 1
                for (x, _), (y, _) in zip(orbs, orbs[1:]):
                    orb_gap[y - x] += 1
            for t, row in orbs:
                g, st, en, ang, prog, sh, eh, src, tgt, pl = row
                shape[(st, ang, prog, sh, eh)] += 1
                d = M.cheb(src, tgt)
                flight[(d, en)] += 1
                if sw:
                    target[(tgt[0] - sw[0], tgt[1] - sw[1])] += 1
                landed = [dt for dt in range(0, 5) if hit_ticks.get(t + dt)]
                heal_land[(en, landed[0] if landed else None)] += 1
                for kind, text, subs in f.top.get(t, []):
                    if kind == "sound_area" and nid(text) == 3231:
                        shot_delay[(en, N.num(text, "delay") or 0)] += 1
            # a raider on the exhumed: does it still send orbs?
            for t in range(a, (end or a) + 1):
                if at in M.player_tiles(f, t + 1):
                    sent = any(row[0] and "energyorb" in row[0] and row[7] == at for row in M.proj_rows(f, t, names))
                    stood["orb that tick" if sent else "no orb that tick"] += 1
        for t, k, text, _ in tops(f, "sound_area", r"id=3956\b"):
            if feed <= t <= combat + 5:
                swallow[(N.num(text, "delay") or 0, "heal same tick" if hit_ticks.get(t) else "no heal")] += 1
    print("== exhumed")
    show("(party, exhumeds)", count)
    show("(party, add gap)", gap)
    show("life add -> del", life)
    show("orbs per exhumed", orbs_per)
    show("add -> first orb", first_orb)
    show("orb -> orb", orb_gap)
    show("orb (start, angle, progress, sh, eh)", shape)
    show("orb (cheb distance, end)", flight)
    show("orb target from SW", target)
    show("orb (end, ticks to the heal splat)", heal_land)
    show("(party, heal per orb)", heal_val)
    show("health_shots sound (end, delay)", shot_delay)
    show("swallowing sound (delay, heal)", swallow)
    show("exhumed tile from SW", offs)
    show("raider standing on it", stood)


# ---------------------------------------------------------------- spit
def spit(fights, names):
    gaps = Counter()
    per_seq = Counter()
    main = Counter()
    flight = Counter()
    src = Counter()
    aim = Counter()
    pool = Counter()
    rot = Counter()
    splash = Counter()
    splash_fl = Counter()
    sounds = Counter()
    anims = Counter()
    hits = Counter()
    seqd = Counter()
    for f in fights:
        b = boss(f)
        if not b:
            continue
        combat = first(b, "retype", "combat")
        say = first(b, "say", "Scree")
        if combat is None:
            continue
        stop = say or b.zero_hit or f.span[1]
        seqs = [(t, d) for t, k, d in b.events if k == "seq" and "attack_ranged" in d and combat <= t < stop]
        for (x, _), (y, _) in zip(seqs, seqs[1:]):
            gaps[y - x] += 1
        for t, d in seqs:
            seqd[d] += 1
            rows = M.proj_rows(f, t, names)
            spits = [r for r in rows if r[0] and "acidspit" in r[0] and r[1] >= 20]
            per_seq[len(spits)] += 1
            sw = b.coord_at(t)
            players = M.player_tiles(f, t)
            for r in spits:
                g, st, en, ang, prog, sh, eh, s, tg, pl = r
                main[(st, ang, prog, sh, eh, "player" if pl else "tile")] += 1
                flight[(M.cheb(s, tg), en)] += 1
                if sw:
                    src[(s[0] - sw[0], s[1] - sw[1])] += 1
                if players:
                    aim[min(M.cheb(tg, p) for p in players)] += 1
                land = None
                for dt in range(0, 12):
                    for kind, text, subs in f.top.get(t + dt, []):
                        if kind.startswith("loc_add") and "acidpool" in (named(text, "loc", names) + text) and N.coord_of(text) == tg and land is None:
                            land = dt
                            rot[nrot(text)] += 1
                        if kind == "map_anim" and "acidsplash" in named(text, "spotanim", names) and N.coord_of(text) == tg:
                            anims[("acidsplash", dt, N.num(text, "delay") or 0)] += 1
                        if kind == "sound_area" and N.coord_of(text) == tg:
                            sounds[(named(text, "sound", names) or text[:20], dt, N.num(text, "delay") or 0)] += 1
                pool[(en, land)] += 1
                # the splash: start-15 acidspits leaving the landing tile
                for dt in range(0, 12):
                    for r2 in M.proj_rows(f, t + dt, names):
                        if r2[0] and "acidspit" in r2[0] and r2[1] == 15 and r2[7] == tg:
                            splash[("dt", dt)] += 1
                            splash[("shape", r2[3], r2[4], r2[5], r2[6])] += 1
                            splash_fl[(M.cheb(r2[7], r2[8]), r2[2])] += 1
    print("== spit (P2: combat retype to the screech)")
    show("seq -> seq", gaps)
    show("seq detail", seqd)
    show("main spits per seq", per_seq)
    show("(start, angle, progress, sh, eh, target)", main)
    show("(cheb source->target, end)", flight)
    show("source from SW", src)
    show("target to nearest raider", aim)
    show("(end, pool loc_add after the seq)", pool)
    show("pool rotation", rot)
    show("splash", splash)
    show("splash (cheb, end)", splash_fl)
    show("anims at the target (name, dt, delay)", anims)
    show("sounds at the target (name, dt, delay)", sounds)


def pools(fights, names):
    """A raider on acid: the hit, its sound and graphic, and the cadence."""
    hit = Counter()
    gap = Counter()
    sound = Counter()
    gfx = Counter()
    life = Counter()
    for f in fights:
        b = boss(f)
        if not b:
            continue
        combat = first(b, "retype", "combat")
        if combat is None:
            continue
        mine = defaultdict(list)
        path = os.path.join(os.path.expanduser("~/.rsprox/binary/archive"), f.path)
        # the local player's hitsplats come from player_info; use the top list of sounds/spotanims
        for t, k, text, _ in tops(f, "sound_area", r"id=3944\b"):
            sound[("player_acid_burn delay", N.num(text, "delay") or 0)] += 1
            mine["burn"].append(t)
        for (x, y) in zip(mine["burn"], mine["burn"][1:]):
            if y - x < 10:
                gap[y - x] += 1
        adds = {}
        for t, k, text, _ in tops(f, "loc_add", "acidpool"):
            adds.setdefault(N.coord_of(text), t)
        ends = [(t, N.coord_of(text), named(text, "spotanim", names)) for t, k, text, _ in tops(f, "map_anim", "acidpool_end")]
        dz = b.zero_hit
        for t, c, g in ends:
            if dz:
                life[("end after zero hit", t - dz)] += 1
    print("== pools")
    show("acid burn sound", sound)
    show("burn -> burn (same raider not known; < 10)", gap)
    show("pool end graphics after the killing hit", life)


def me_hits(f):
    """[(tick, id, value)] of the local player's own hitsplats in the window."""
    key = ("hits", f.path, f.span)
    if key in _ME:
        return _ME[key]
    out = []
    path = os.path.join(os.path.expanduser("~/.rsprox/binary/archive"), f.path)
    t = 0
    inside = False
    with open(path, encoding="utf-8", errors="replace") as h:
        for line in h:
            m = N.TICK.match(line.strip())
            if m and line.startswith("["):
                t = int(m.group(1))
                inside = False
                continue
            if t < f.span[0] - 2 or t > f.span[1] + 2:
                continue
            st = line.strip()
            if st.startswith("[player_info]"):
                inside = True
                continue
            if line.startswith("    [") and not line.startswith("     "):
                inside = False
            if inside and st.startswith("[hit]"):
                out.append((t, nid(st), N.num(st, "value")))
    _ME[key] = out
    return out


_ME = {}


def quadrant(c, centre):
    dx, dz = c[0] - centre[0], c[1] - centre[1]
    if dx >= 0 and dz >= 0:
        return "NE"
    if dx >= 0:
        return "SE"
    if dz < 0:
        return "SW"
    return "NW"


def splash(fights, names):
    per_landing = Counter()
    at_me = Counter()
    land = Counter()
    rot_q = Counter()
    hit_me = Counter()
    hit_when = Counter()
    for f in fights:
        b = boss(f)
        if not b:
            continue
        combat = first(b, "retype", "combat")
        if combat is None:
            continue
        n = party(f)
        sw = b.coord_at(combat)
        centre = (sw[0] + 2, sw[1] + 2)
        pools = {}
        for t, k, text, _ in tops(f, "loc_add", "acidpool"):
            c = N.coord_of(text)
            pools.setdefault(c, t)
            rot_q[(quadrant(c, centre), nrot(text))] += 1
        spit_targets = {}
        k = 0
        for t, kk, d in b.events:
            if kk == "seq" and "attack_ranged" in d and "d=" not in d:
                for r in M.proj_rows(f, t, names):
                    if r[0] and "acidspit" in r[0] and r[1] == 30:
                        spit_targets[(r[8], t + r[2] // 30)] = k
                        k += 1
        for t in sorted(f.top):
            rows = [r for r in M.proj_rows(f, t, names) if r[0] and "acidspit" in r[0] and r[1] == 15]
            if not rows:
                continue
            src = rows[0][7]
            idx = None
            for (tg, lt), kk in spit_targets.items():
                if tg == src and abs(lt - t) <= 1:
                    idx = kk
            per_landing[(n, "first spit" if idx == 0 else ("spit" if idx is not None else "not a spit landing"), len(rows))] += 1
            me = M.player_tiles(f, t)
            for r in rows:
                tg = r[8]
                at_me["splash at my tile" if tg in me else ("within 1 of me" if any(M.cheb(tg, p) <= 1 for p in me) else "elsewhere")] += 1
                lt = t + r[2] // 30
                got = []
                for dt in (0, 1):
                    for kind, text, subs in f.top.get(lt + dt, []):
                        if N.coord_of(text) == tg:
                            nm = named(text, "loc" if kind.startswith("loc") else ("spotanim" if kind == "map_anim" else "sound"), names)
                            got.append("%s+%d" % (nm or kind, dt))
                before = tg in pools and pools[tg] < lt
                land[("pooled before" if before else "fresh tile", tuple(sorted(set(got))))] += 1
        for t, i, v in me_hits(f):
            if t < combat:
                continue
            hit_me[i] += 1
            if i in (65, 66, 68):
                me_t = M.player_tiles(f, t)
                on_pool = any(c in pools and pools[c] < t for c in me_t)
                landed = any(N.coord_of(text) and any(M.cheb(N.coord_of(text), c) <= 1 for c in me_t)
                             for kind, text, subs in f.top.get(t, []) if kind.startswith("loc_add") and "acidpool" in (named(text, "loc", names) + text))
                hit_when[("on an old pool" if on_pool else "not on a pool", "pool landed within 1 this tick" if landed else "-", v)] += 1
    # the small poison hits not explained by a landing next to me: their spacing
    for f in fights:
        b = boss(f)
        if not b:
            continue
        combat = first(b, "retype", "combat")
        if combat is None:
            continue
        smalls = [t for t, i, v in me_hits(f) if i in (65, 66, 68) and v < 20 and t >= combat]
        for x, y in zip(smalls, smalls[1:]):
            hit_when[("small hit -> small hit", y - x)] += 1
        msgs = [t for t, _, text, _ in tops(f, "message_game", "been poisoned")]
        for t in msgs:
            nxt = [x for x in smalls if x >= t]
            hit_when[("poisoned msg -> next small hit", (nxt[0] - t) if nxt else None)] += 1
    print("== splash (acid thrown from a landing)")
    show("(party, which landing, splashes)", per_landing)
    show("aimed", at_me)
    show("at the splash's landing tick", land)
    show("pool (quadrant from his centre, rotation)", rot_q)
    show("my hitsplats in P2/P3 by id", hit_me)
    show("my poison hits (where, landing, value)", hit_when)


# ---------------------------------------------------------------- stare
def stare(fights, names):
    turn = Counter()
    faces = Counter()
    ret = Counter()
    retp = Counter()
    seqd = Counter()
    for f in fights:
        b = boss(f)
        if not b:
            continue
        say = first(b, "say", "Scree")
        if say is None:
            continue
        stop = b.zero_hit or f.span[1]
        fa = [(t, d) for t, k, d in b.events if k in ("face_angle", "facing", "face_coord") and say <= t <= stop]
        for (x, _), (y, _) in zip(fa, fa[1:]):
            turn[y - x] += 1
        for t, d in fa[:4]:
            faces[d[:40]] += 1
        mine = me_hits(f)
        for t, k, text, _ in tops(f, "message_game", "staring in your direction"):
            if not (say <= t <= stop):
                continue
            sw = b.coord_at(t)
            rows = [r for r in M.proj_rows(f, t, names) if r[0] and "acidspit" in r[0] and r[9]]
            ret[len(rows)] += 1
            for r in rows:
                fp = max(0, max(sw[0] - r[8][0], r[8][0] - (sw[0] + 4))) if sw else None
                fz = max(0, max(sw[1] - r[8][1], r[8][1] - (sw[1] + 4))) if sw else None
                retp[("start", r[1], "end-start", r[2] - r[1], "footprint dist", max(fp, fz) if sw else None)] += 1
                retp[("shape", r[3], r[4], r[5], r[6])] += 1
            for tt, kk, dd in b.events:
                if tt == t and kk == "seq":
                    seqd[("seq", dd)] += 1
            for kind, tx, subs in f.top.get(t, []):
                if kind == "sound_area":
                    seqd[("sound", named(tx, "sound", names), N.num(tx, "delay"), N.num(tx, "range"))] += 1
            big = [(tt - t, i, v) for tt, i, v in mine if 0 <= tt - t <= 4 and v >= 20]
            seqd[("my big hits (dt, id)", tuple((d, i) for d, i, v in big))] += 1
            seqd[("projectiles vs big hits", len(rows), len(big))] += 1
            # the hit that drew it: my last hit on him, and his facing
            faced = [dd for tt, kk, dd in b.events if kk == "face_angle" and tt <= t]
            seqd[("his facing", faced[-1] if faced else None)] += 1
    print("== stare (the screech to his killing hit)")
    show("turn -> turn", turn)
    show("first turn details", faces)
    show("retaliation projectiles per message", ret)
    show("retaliation (dt, start, end, angle, progress, sh, eh)", retp)
    show("seqs around a retaliation (dt, seq)", seqd)


# ---------------------------------------------------------------- guano
def guano(fights, names):
    rows = Counter()
    when = Counter()
    where = Counter()
    hits = Counter()
    snd = Counter()
    for f in fights:
        b = boss(f)
        if not b:
            continue
        combat = first(b, "retype", "combat")
        say = first(b, "say", "Scree")
        mine = me_hits(f)
        for t in sorted(f.top):
            gs = [r for r in M.proj_rows(f, t, names) if r[0] and "guano" in r[0]]
            if not gs:
                continue
            phase = "P1" if combat and t < combat else ("P2" if not say or t < say else "P3")
            when[(phase, len(gs))] += 1
            sw = b.coord_at(t)
            me = M.player_tiles(f, t)
            for r in gs:
                rows[(r[1], r[2], r[3], r[4], r[5], r[6])] += 1
                if sw:
                    where[("source from SW", r[7][0] - sw[0], r[7][1] - sw[1])] += 1
            for c in me:
                if sw:
                    dx, dz = c[0] - sw[0], c[1] - sw[1]
                    inside = 0 <= dx < 5 and 0 <= dz < 5
                    where[("me under him" if inside else "me outside", "source=me" if gs[0][7] == c else "source!=me")] += 1
                    if gs[0][7] == c:
                        hits[tuple((tt - t, i, v) for tt, i, v in mine if 0 <= tt - t <= 3)] += 1
            for kind, text, subs in f.top.get(t, []):
                if kind.startswith("sound"):
                    snd[(named(text, "sound", names), N.num(text, "delay"), N.num(text, "loops"))] += 1
    for f in fights:
        b = boss(f)
        if not b:
            continue
        spits = all_of(b, "seq", "attack_ranged")
        gts = sorted(t for t in f.top if any(r[0] and "guano" in r[0] for r in M.proj_rows(f, t, names)))
        for t in gts:
            prev = [x for x in spits if x <= t]
            nxt = [x for x in spits if x > t]
            if prev and nxt:
                where[("guano after last spit, next spit after guano", t - prev[-1], nxt[0] - t)] += 1
        for x, y in zip(gts, gts[1:]):
            where[("guano -> guano", y - x)] += 1
    print("== guano")
    show("(phase, projectiles that tick)", when)
    show("(start, end, angle, progress, sh, eh)", rows)
    show("where", where)
    show("my hits after a guano on me (dt, id, value)", hits)
    show("sounds on a guano tick", snd)


def acid(fights, names):
    """The local raider and the pools: standing, crossing, and what it costs."""
    out = Counter()
    for f in fights:
        b = boss(f)
        if not b:
            continue
        combat = first(b, "retype", "combat")
        if combat is None:
            continue
        stop = b.zero_hit or f.span[1]
        pools = {}
        for t, k, text, _ in tops(f, "loc_add", "acidpool"):
            pools.setdefault(N.coord_of(text), t)
        hits = defaultdict(list)
        for t, i, v in me_hits(f):
            hits[t].append((i, v))
        prev = None
        for t in range(combat, stop):
            me = list(M.player_tiles(f, t + 1))   # where I stood at the END of t
            if not me:
                continue
            c = me[0]
            on = c in pools and pools[c] <= t
            if on:
                kind = "stayed" if prev == c else "stepped on"
                got = [(dt, i, v) for dt in (0, 1, 2) for i, v in hits.get(t + dt, []) if i in (65, 66, 68)]
                out[(kind, tuple((dt, v < 20) for dt, i, v in got))] += 1
            prev = c
    print("== acid (the local raider standing on a pool at the end of tick t; hits at t+dt, small?)")
    show("", out)


# ---------------------------------------------------------------- death
def death(fights, names):
    print("== death (T = his killing hit)")
    for f in fights:
        b = boss(f)
        if not b or b.zero_hit is None:
            continue
        z = b.zero_hit
        out = []
        for t, k, d in b.events:
            if z - 1 <= t <= z + 12 and k in ("seq", "retype", "del", "say", "spotanim"):
                out.append("%+d %s %s" % (t - z, k, d))
        seen = Counter()
        for t in range(z - 1, z + 14):
            for kind, text, _ in f.top.get(t, []):
                if kind == "message_game" and "complete!" in text:
                    out.append("%+d complete" % (t - z))
                if kind in ("midi_song_v2", "midi_jingle"):
                    out.append("%+d %s %s" % (t - z, kind, re.sub(r", fade.*|, length.*", "", text)))
                if kind.startswith("obj_add"):
                    out.append("%+d %s" % (t - z, text[:40]))
                if kind == "sound_area" and nid(text) in (4007, 3549):
                    out.append("%+d %s d=%s" % (t - z, named(text, "sound", names), N.num(text, "delay")))
                if kind == "map_anim" and "acidpool_end" in named(text, "spotanim", names):
                    seen[t - z] += 1
                if kind.startswith("loc_del"):
                    seen[("del", t - z)] += 1
        out.append("pool ends %s" % dict(sorted((k, v) for k, v in seen.items() if not isinstance(k, tuple))))
        out.append("loc_dels %s" % dict(sorted((k[1], v) for k, v in seen.items() if isinstance(k, tuple))))
        print("  %s %d: %s" % (f.path, z, " | ".join(out)))


def main():
    secs = ["phases", "exhumed", "spit", "splash", "acid", "pools", "stare", "guano", "death"]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--section", action="append", choices=secs)
    ap.add_argument("--anchor", default=ANCHOR)
    ap.add_argument("transcripts", nargs="+")
    args = ap.parse_args()
    names = N.load_names()
    NAMES.update(names)
    fights = N.load(args.transcripts, names, args.anchor)
    for s in args.section or secs:
        globals()[s](fights, names)


if __name__ == "__main__":
    main()
