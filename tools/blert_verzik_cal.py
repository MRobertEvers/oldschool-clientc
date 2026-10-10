#!/usr/bin/env python3
"""VERZIK VITUR, Blert against ours: what the content decides, as distributions
over every room, each with a chi-square test; what the players decide, reported.

  tools/blert_verzik_cal.py build/blert/verzik <our runs dir> [<our runs dir> ...]
  tools/blert_verzik_cal.py -v ...      also print every room's timeline

Blert: Normal trio rooms (tools/blert_fetch_tob_rooms.py), room tick 0 its start
(the 8369 -> 8370 change). Ours: every session's ticklog.tsv under a runs dir
(a relay log, where Verzik is the last room, or a Verzik room test), windowed
on Verzik's own slot: tick 0 is her npc_retype to 8370, the window ends at her
npc_death. Sessions whose Verzik events are identical (the same seed run twice,
rl26 / rl29, vz1 / vz2) are counted once.

How Blert gets each event (docs/minigames/theater_of_blood/sources/blert_plugin/
VerzikDataTracker.java; codes are blert's EventType / NpcAttack, event.proto),
and what of ours is read the same way:

  P1 auto (10/12)      her 8109 seq, + 1             npc_anim 8109, + 1
  phase 2 (150)        the P1 body leaves (8371)     npc_retype to 8371
  P2 body              8372 in her npc updates       npc_retype to 8372
  P2 attack (10/13-17) an 8114 seq, typed by the     npc_anim 8114 / 8116, typed
                       projectile that tick: 1583    by her projectile that tick
                       cabbage 14, 1585 zap 15,      (the same spotanims, from
                       1586 purple 16, 1591 mage     her tile); 8116 the bounce
                       (blood spell) 17; 8116 the
                       bounce 13
  reds (151, 7/8385)   Matomenos spawns              npc_spawn 8385
  Athanatos (7/8384)   spawn                         npc_spawn 8384
  nylocas (7/8381-3)   spawn                         npc_spawn 8381-8383
  phase 3 (150)        8118 on the P2 body           npc_anim 8118
  P3 attack (10/19-24) NOT seen: a cadence from the  npc_anim 8123 melee; 8124 /
                       phase event (12, then 7, 5    8125 / 14406 typed by 1593
                       enraged), typed by 1593       range, 1594 mage, 1598 the
                       range / 1594 mage, melee if   ball; 8127 webs, 8126
                       the tank was in reach; webs   yellows
                       22, yellows 23, ball 24 on
                       the slot they take
  P3 crab special      nylocas spawning in P3        npc_anim 14406, with the
                       (so a special with no crab    nylocas spawned that tick
                       is invisible to it)           (none is a count of 0)
  enrage               first tornado (7/8386)        npc_spawn 8386
  yellows (153)        1595 graphics, per tick       map_spotanim 1595
  pillar (9/8379)      its collapse                  npc_retype 8379 -> 8377
  tornado heal (155)   1602 on a player              npc_heal tob_verzik_tornado_heal
  (152, 154, 156, 157: attack style, bounce chances, dawnbringer - players')

  tested (content)                            reported (players)
  P1 first attack, P1 attack gap              P1 length, P1 attacks, pillars lost in P1
  P1 end -> P2 body, P2 body -> 1st attack    P2 length, P2 bounce share, hp at reds
  pillar collapse from the P1 end             P3 length, room length, P3 melee share
  P2 attack gap (before reds / reds phase)    tornadoes at the enrage, tornado heals
  P2 attack type (before reds / reds phase)
  zap: first attack index, cabbages before the first / next zap
  purple: first index, attacks purple to purple, Athanatos after the cast
  nylocas: a purple cast, wave - nearest cast, a P3 crab special, pooled
  reds: a wave, wave gap, -> first attack, attacks between waves, last attack -> wave
  P3: phase event -> P3 body / first attack / first special
  P3 auto gap (before / after the enrage), tornadoes -> next attack
  P3 special after a special (in rotation C W Y B), autos between specials
  webs / yellows / ball: slot (the auto before + speed) -> next auto
  yellow pools a set, P3 auto style (range or mage)

A test passes at p >= 0.01 OR a total-variation distance under 0.05.
Bounces (13) and melee (19) are where the players stand, so the P2 type mix and
the P3 style are tested without them and their rate is reported.
"""
import collections
import glob
import json
import os
import re
import sys

from scipy.stats import chi2_contingency

IDLE, P1, P1_WALK, P2, P3_RISE, P3, DEAD = 8369, 8370, 8371, 8372, 8373, 8374, 8375
PILLAR, PILLAR_COLLAPSING = 8379, 8377
NYLOS = (8381, 8382, 8383)      # Ischyros, Toxobolos, Hagios: the exploding crabs
ATHANATOS, MATOMENOS, TORNADO = 8384, 8385, 8386

P1_AUTO_SEQ = 8109
P2_AUTO_SEQ, P2_BOUNCE_SEQ, P2_DEATH_SEQ = 8114, 8116, 8118
P3_MELEE_SEQ, P3_MAGE_SEQ, P3_RANGE_SEQ, P3_SUMMON_SEQ = 8123, 8124, 8125, 14406
P3_YELLOWS_SEQ, P3_WEBS_SEQ = 8126, 8127
YELLOW_POOL = 1595

# blert NpcAttack (event.proto) and the projectile that names a P2 / P3 attack
A_P1, A_BOUNCE, A_CABBAGE, A_ZAP, A_PURPLE, A_MAGE = 12, 13, 14, 15, 16, 17
A_P3_MELEE, A_P3_RANGE, A_P3_MAGE, A_WEBS, A_YELLOWS, A_BALL = 19, 20, 21, 22, 23, 24
P2_PROJ = {1586: A_PURPLE, 1585: A_ZAP, 1591: A_MAGE, 1583: A_CABBAGE}   # in this order of precedence
P3_PROJ = {1593: A_P3_RANGE, 1594: A_P3_MAGE}
BALL_PROJ = 1598
NAMES = {A_P1: "auto", A_BOUNCE: "bounce", A_CABBAGE: "cabbage", A_ZAP: "zap", A_PURPLE: "purple",
         A_MAGE: "mage", A_P3_MELEE: "melee", A_P3_RANGE: "range", A_P3_MAGE: "mage", A_WEBS: "webs",
         A_YELLOWS: "yellows", A_BALL: "ball"}
SPECIAL_LETTER = {A_WEBS: "W", A_YELLOWS: "Y", A_BALL: "B"}
ROTATION = ("CW", "WY", "YB", "BC")     # crabs, webs, yellows, ball, again


def room():
    return dict(p1=[], ph2=None, p2_body=None, p2=[], nylos2=[], purples=[], reds=[], ph3=None, p3_body=None,
                p3=[], crabs=[], tornadoes=[], yellows=[], end=None, pillars=[], heals=0, reds_hp=None)


def waves(ticks):
    """Spawn ticks -> [(tick, count)], one entry a tick."""
    c = collections.Counter(ticks)
    return sorted(c.items())


def blert(d):
    rooms = []
    for f in sorted(glob.glob(d + "/*-*.json")):
        ev = sorted(json.load(open(f)), key=lambda e: e["tick"])
        R = room()
        R["name"] = os.path.basename(f)[:8]
        nylo, ath, red, torn, yel = [], [], [], [], {}
        hp = {}
        for e in ev:
            t, ty = e["tick"], e["type"]
            npc = e.get("npc") or {}
            nid = npc.get("id")
            if ty == 150:
                if e["verzikPhase"] == 2 and R["ph2"] is None:
                    R["ph2"] = t
                if e["verzikPhase"] == 3 and R["ph3"] is None:
                    R["ph3"] = t
            if ty in (7, 8) and nid == P2 and R["p2_body"] is None:
                R["p2_body"] = t
            if ty in (7, 8) and nid == P3 and R["p3_body"] is None:
                R["p3_body"] = t
            if ty == 8 and nid == P2:
                h = npc["hitpoints"]
                hp[t] = (h >> 16, h & 0xFFFF)
            if ty == 10:
                a = e["npcAttack"]["attack"]
                if nid == P1 and a == A_P1:
                    R["p1"].append(t)
                elif nid == P2:
                    R["p2"].append((t, a))
                elif nid == P3:
                    R["p3"].append((t, a))
            if ty == 7 and nid in NYLOS:
                nylo.append(t)
            if ty == 7 and nid == ATHANATOS:
                ath.append(t)
            if ty == 7 and nid == MATOMENOS:
                red.append(t)
            if ty == 7 and nid == TORNADO:
                torn.append(t)
            if ty == 153:
                yel.setdefault(t, set()).update((c["x"], c["y"]) for c in e["verzikYellows"])
            if ty == 9 and nid == PILLAR:
                R["pillars"].append(t)
            if ty == 9 and nid == DEAD and R["end"] is None:
                R["end"] = t
            if ty == 155:
                R["heals"] += 1
        finish(R, nylo, ath, red, torn, yel, hp)
        rooms.append(R)
    return rooms


def finish(R, nylo, ath, red, torn, yel, hp):
    """The phase split and the set grouping both loaders share."""
    ph3 = R["ph3"] if R["ph3"] is not None else 1 << 30
    R["nylos2"] = waves([t for t in nylo if t < ph3])
    R["crabs"] = waves([t for t in nylo if t >= ph3])
    R["purples"] = sorted(ath)
    R["reds"] = waves(red)
    R["tornadoes"] = waves(torn)
    sets, last = [], None
    for t in sorted(yel):
        if last is None or t - last > 5:
            sets.append((t, len(yel[t])))
        last = t
    R["yellows"] = sets
    if R["reds"]:
        before = [v for t, v in sorted(hp.items()) if t <= R["reds"][0][0] and v[1]]
        if before:
            R["reds_hp"] = 1000 * before[-1][0] // before[-1][1]


def ours(dirs):
    rooms, seen, dupes = [], set(), 0
    for d in dirs:
        for sess in sorted(glob.glob(d.rstrip("/") + "/*/")):
            path = sess + "ticklog.tsv"
            if not os.path.exists(path):
                continue
            R = our_room(path)
            if R is None:
                continue
            key = json.dumps({k: v for k, v in R.items() if k != "name"}, sort_keys=True)
            if key in seen:
                dupes += 1
                continue
            seen.add(key)
            rooms.append(R)
    if dupes:
        print("(ours: %d sessions with a Verzik identical to one already read were skipped)" % dupes)
    return rooms


def our_room(path):
    rows = [l.rstrip("\n").split("\t") for l in open(path) if not l.startswith("ticklog")]
    R = room()
    R["name"] = path.split("/")[-3] + "/" + path.split("/")[-2]
    start = slot = None
    form = None
    anims = collections.defaultdict(list)    # tick -> her seqs
    projs = collections.defaultdict(list)    # tick -> (src, spotanim)
    nylo, ath, red, torn, yel, hp = [], [], [], [], {}, {}
    summons = []
    for r in rows:
        t, k = int(r[1]), r[2]
        if start is None:
            if k == "npc_retype" and r[5] == str(P1):
                start, slot, form = t, r[3], P1
            elif k == "npc_spawn" and r[4] == str(P1):
                start, slot, form = t, r[3], P1
            if start is None:
                continue
        if R["end"] is not None and t > R["end"] + 2:
            break
        if k == "npc_retype" and r[3] == slot:
            form = int(r[5])
            if form == P1_WALK and R["ph2"] is None:
                R["ph2"] = t
            if form == P2 and R["p2_body"] is None:
                R["p2_body"] = t
            if form == P3 and R["p3_body"] is None:
                R["p3_body"] = t
            if form == DEAD and R["end"] is None:
                R["end"] = t
            if form == IDLE:
                break
        if k == "npc_death" and r[3] == slot and R["end"] is None:
            R["end"] = t
        if k == "npc_retype" and r[4] == str(PILLAR) and r[5] == str(PILLAR_COLLAPSING):
            R["pillars"].append(t)
        if k == "npc_anim" and r[3] == slot:
            anims[t].append(int(r[5]))
            if int(r[5]) == P3_SUMMON_SEQ:
                summons.append(t)
            if int(r[5]) == P2_DEATH_SEQ and R["ph3"] is None:
                R["ph3"] = t
        if k == "projectile":
            projs[t].append((r[3], int(r[6])))
        if k == "npc_spawn":
            nid = int(r[4])
            if nid in NYLOS:
                nylo.append(t)
            elif nid == ATHANATOS:
                ath.append(t)
            elif nid == MATOMENOS:
                red.append(t)
            elif nid == TORNADO:
                torn.append(t)
        if k == "map_spotanim" and r[4] == str(YELLOW_POOL):
            yel.setdefault(t, set()).add(r[3])
        if k == "npc_heal" and r[3] == slot and "tornado_heal" in r[9]:
            R["heals"] += 1
        if k == "hudbar" and r[3] == slot and r[4] == str(P2):
            m = re.match(r"hud (\d+)/(\d+)", r[9])
            if m:
                hp[t] = (int(m.group(1)), int(m.group(2)))
    if start is None:
        return None
    ph3 = R["ph3"] if R["ph3"] is not None else 1 << 30
    p3_from = R["p3_body"] if R["p3_body"] is not None else 1 << 30
    # her P2 projectiles all leave one tile (she does not move): the commonest
    # source of a P2 attack spotanim, so a zap bouncing between players is not hers
    src = collections.Counter(s for t in anims for s, g in projs[t] if g in P2_PROJ and R["p2_body"] and
                              R["p2_body"] <= t < ph3)
    her = src.most_common(1)[0][0] if src else None
    for t in sorted(anims):
        seqs = anims[t]
        mine = [g for s, g in projs[t]]
        if P1_AUTO_SEQ in seqs and (R["ph2"] is None or t < R["ph2"]):
            R["p1"].append(t + 1)
        elif R["p2_body"] is not None and R["p2_body"] <= t < ph3:
            if P2_BOUNCE_SEQ in seqs:
                R["p2"].append((t, A_BOUNCE))
            elif P2_AUTO_SEQ in seqs:
                hers = [g for s, g in projs[t] if s == her]
                for g, a in P2_PROJ.items():
                    if g in hers:
                        R["p2"].append((t, a))
                        break
        elif t >= p3_from:
            if P3_WEBS_SEQ in seqs:
                R["p3"].append((t, A_WEBS))
            elif P3_YELLOWS_SEQ in seqs:
                R["p3"].append((t, A_YELLOWS))
            elif P3_MELEE_SEQ in seqs:
                R["p3"].append((t, A_P3_MELEE))
            elif any(s in seqs for s in (P3_MAGE_SEQ, P3_RANGE_SEQ, P3_SUMMON_SEQ)):
                styles = [P3_PROJ[g] for g in mine if g in P3_PROJ]
                if styles:
                    R["p3"].append((t, styles[0]))
                elif BALL_PROJ in mine:
                    R["p3"].append((t, A_BALL))
                elif P3_SUMMON_SEQ in seqs:
                    R["p3"].append((t, A_P3_MELEE))     # crabs riding a melee
    # one web / yellows special a cast (the seq may be re-sent while it runs)
    p3, last = [], {}
    for t, a in R["p3"]:
        if a in (A_WEBS, A_YELLOWS) and a in last and t - last[a] < 20:
            continue
        last[a] = t
        p3.append((t, a))
    R["p3"] = p3
    finish(R, nylo, ath, red, torn, yel, hp)
    # a crab special is her summon seq, crabs or none (Blert sees only spawns)
    spawned = collections.Counter(t for t in nylo if t >= ph3)
    R["crabs"] = [(t, spawned[t]) for t in summons if t >= ph3]
    sh = lambda v: None if v is None else v - start
    for k in ("ph2", "p2_body", "ph3", "p3_body", "end"):
        R[k] = sh(R[k])
    for k in ("p1", "purples", "pillars"):
        R[k] = [t - start for t in R[k]]
    for k in ("p2", "p3", "nylos2", "crabs", "reds", "tornadoes", "yellows"):
        R[k] = [(t - start, v) for t, v in R[k]]
    return R


def p3_events(R):
    """Her P3 attacks and specials in order: (tick, kind), kind an attack code
    or "C" for a crab special (an attack on the tick nylocas spawn)."""
    crab_ticks = {t for t, _ in R["crabs"]}
    out = []
    for t, a in R["p3"]:
        out.append((t, "C" if t in crab_ticks and a in (A_P3_MELEE, A_P3_RANGE, A_P3_MAGE) else a))
    seen = {t for t, _ in out}
    out += [(t, "C") for t in crab_ticks if t not in seen]
    return sorted(out, key=lambda x: x[0])


def measures(rooms):
    M = collections.defaultdict(collections.Counter)
    for R in rooms:
        # ---- P1
        if R["p1"]:
            M["P1 first attack (room tick)"][R["p1"][0]] += 1
            for a, b in zip(R["p1"], R["p1"][1:]):
                M["P1 attack gap"][b - a] += 1
        if R["ph2"] is not None:
            M["P1 length (report, 10s)"][R["ph2"] // 10 * 10] += 1
            M["P1 attacks (report)"][len(R["p1"])] += 1
            M["pillars lost during P1 (report)"][sum(1 for t in R["pillars"] if t < R["ph2"] - 2)] += 1
            for t in R["pillars"]:
                if t >= R["ph2"] - 2:
                    M["pillar collapse from the P1 end"][t - R["ph2"]] += 1
            if R["p2_body"] is not None:
                M["P1 end -> P2 body"][R["p2_body"] - R["ph2"]] += 1
        # ---- P2
        p2 = R["p2"]
        if R["p2_body"] is not None and p2:
            M["P2 body -> first P2 attack"][p2[0][0] - R["p2_body"]] += 1
        reds = R["reds"]
        reds0 = reds[0][0] if reds else 1 << 30
        pre = [x for x in p2 if x[0] < reds0]
        during = [x for x in p2 if x[0] > reds0]
        for name, seq in (("before reds", pre), ("reds phase", during)):
            for (a, _), (b, _) in zip(seq, seq[1:]):
                M["P2 attack gap, %s" % name][b - a] += 1
            for t, a in seq:
                if a != A_BOUNCE:
                    M["P2 attack type, %s" % name][NAMES[a]] += 1
                M["P2 bounce share x100, %s (report)" % name]["bounce" if a == A_BOUNCE else "other"] += 1
        idx = [i for i, (t, a) in enumerate(pre) if a == A_ZAP]
        if idx:
            M["P2 first zap (attack index)"][idx[0]] += 1
        # the zap's own counter: cabbages since the last zap (or the phase
        # start); purples, bounces and blood spells are not counted by it
        n, first = 0, True
        for t, a in p2:
            if a == A_ZAP:
                M["P2 cabbages before the %s zap" % ("first" if first else "next")][n] += 1
                n, first = 0, False
            elif a == A_CABBAGE:
                n += 1
        idx = [i for i, (t, a) in enumerate(p2) if a == A_PURPLE]
        if idx:
            M["P2 first purple (attack index)"][idx[0]] += 1
            for a, b in zip(idx, idx[1:]):
                M["P2 attacks purple to purple"][b - a] += 1
        casts = [t for t, a in p2 if a == A_PURPLE]
        for t in R["purples"]:
            c = [t - x for x in casts if 0 <= t - x <= 12]
            if c:
                M["Athanatos spawn after the purple cast"][min(c)] += 1
        for c in casts:
            n = sum(n for t, n in R["nylos2"] if c - 4 <= t <= c)
            M["P2 nylocas a purple cast"][n] += 1
            M["nylocas a summon, P2 and P3 pooled"][n] += 1
        for t, n in R["nylos2"]:
            if casts:
                M["P2 nylocas wave - nearest purple cast"][min((t - x for x in casts), key=abs)] += 1
        if reds:
            for t, n in reds:
                M["reds a wave"][n] += 1
            for (a, _), (b, _) in zip(reds, reds[1:]):
                M["reds wave gap"][b - a] += 1
            ends = [t for t, _ in reds] + [R["ph3"] if R["ph3"] is not None else 1 << 30]
            for i, (t, _) in enumerate(reds):
                after = [x for x, _ in p2 if t < x < ends[i + 1]]
                if after:
                    M["reds wave -> first attack"][after[0] - t] += 1
                if i + 1 < len(reds):
                    M["attacks between red waves"][len(after)] += 1
                before = [x for x, _ in p2 if x < t]
                if before:
                    M["%s reds wave <- last attack" % ("first" if i == 0 else "later")][t - before[-1]] += 1
            if R["reds_hp"] is not None:
                M["P2 hp permille at the first reds (report, 10s)"][R["reds_hp"] // 10 * 10] += 1
        if R["p2_body"] is not None and R["ph3"] is not None:
            M["P2 length (report, 20s)"][(R["ph3"] - R["p2_body"]) // 20 * 20] += 1
        # ---- P3
        if R["ph3"] is None:
            continue
        if R["p3_body"] is not None:
            M["P3 phase event -> P3 body"][R["p3_body"] - R["ph3"]] += 1
        ev = [x for x in p3_events(R) if x[0] > R["ph3"]]
        if R["end"] is not None:
            M["P3 length (report, 20s)"][(R["end"] - R["ph3"]) // 20 * 20] += 1
            M["room length (report, 20s)"][R["end"] // 20 * 20] += 1
        if not ev:
            continue
        M["P3 phase event -> first attack"][ev[0][0] - R["ph3"]] += 1
        spec = [(t, k) for t, k in ev if k == "C" or k in SPECIAL_LETTER]
        if spec:
            M["P3 phase event -> first special"][spec[0][0] - R["ph3"]] += 1
            M["P3 first special"][SPECIAL_LETTER.get(spec[0][1], "C")] += 1
        letter = lambda k: SPECIAL_LETTER.get(k, "C")
        for (a, ka), (b, kb) in zip(spec, spec[1:]):
            pair = letter(ka) + letter(kb)
            M["P3 special after a special"]["in rotation" if pair in ROTATION else pair] += 1
            M["P3 autos between specials"][sum(1 for t, k in ev if a < t < b)] += 1
        for t, n in R["crabs"]:
            if t > R["ph3"]:
                M["P3 crabs a crab special"][n] += 1
                M["nylocas a summon, P2 and P3 pooled"][n] += 1
        enr = R["tornadoes"][0][0] if R["tornadoes"] else 1 << 30
        autos = (A_P3_MELEE, A_P3_RANGE, A_P3_MAGE, "C")
        for (a, ka), (b, kb) in zip(ev, ev[1:]):
            if ka in autos and kb in autos and not a < enr <= b:
                M["P3 auto gap, %s the enrage" % ("after" if a >= enr else "before")][b - a] += 1
        for i, (t, k) in enumerate(ev):
            # the special's slot is the auto before it + her attack speed:
            # Blert puts the special there by its cadence model, and ours
            # starts there (the web spin is played on arriving, ticks later)
            if k in SPECIAL_LETTER and 0 < i < len(ev) - 1:
                slot = ev[i - 1][0] + (5 if ev[i - 1][0] >= enr else 7)
                M["P3 %s slot -> next auto" % NAMES[k]][ev[i + 1][0] - slot] += 1
            if k in (A_P3_RANGE, A_P3_MAGE):
                M["P3 auto style, range or mage"][NAMES[k]] += 1
            if k in (A_P3_MELEE, A_P3_RANGE, A_P3_MAGE):
                M["P3 melee share x100 (report)"]["melee" if k == A_P3_MELEE else "other"] += 1
        if R["tornadoes"]:
            M["tornadoes at the enrage (report)"][R["tornadoes"][0][1]] += 1
            nxt = [t for t, k in ev if t > enr]
            if nxt:
                M["tornadoes -> next attack"][nxt[0] - enr] += 1
        M["tornado heals a room (report)"][min(R["heals"], 10)] += 1
        for t, n in R["yellows"]:
            M["yellow pools a set"][n] += 1
    return M


def compare(name, a, b, test=True):
    keys = sorted(set(a) | set(b), key=lambda k: (str(type(k)), k))
    na, nb = sum(a.values()), sum(b.values())
    if na == 0 or nb == 0:
        print("  %-50s blert n=%d ours n=%d  (no data on one side)" % (name, na, nb))
        return None
    tvd = 0.5 * sum(abs(a[k] / na - b[k] / nb) for k in keys)
    p = None
    if test and len(keys) > 1:
        try:
            p = chi2_contingency([[a[k] for k in keys], [b[k] for k in keys]])[1]
        except ValueError:
            p = None
    ok = (p is not None and p >= 0.01) or tvd < 0.05
    top = lambda c, n: " ".join("%s:%d%%" % (k, round(100 * v / n)) for k, v in c.most_common(8))
    print("  %-50s %-6s tvd %.3f p %s  (blert n=%d ours n=%d)" % (
        name, ("MATCH" if ok else "DIFFER") if test else "report", tvd, "%.3g" % p if p is not None else "-", na, nb))
    print("      blert %s" % top(a, na))
    print("      ours  %s" % top(b, nb))
    return ok


def timeline(R):
    out = ["P2body:%s" % R["p2_body"], "PH2:%s" % R["ph2"], "PH3:%s" % R["ph3"], "P3body:%s" % R["p3_body"],
           "end:%s" % R["end"]]
    out += ["%d:%s" % (t, NAMES[a]) for t, a in R["p2"][:12]]
    out += ["%d:reds%d" % x for x in R["reds"][:3]]
    out += ["%d:%s" % (t, k if k == "C" else NAMES[k]) for t, k in p3_events(R)]
    out += ["%d:torn%d" % x for x in R["tornadoes"][:1]]
    return " ".join(out)


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "-v"]
    verbose = "-v" in sys.argv[1:]
    if len(args) < 2:
        sys.exit(__doc__)
    BR, OR = blert(args[0]), ours(args[1:])
    print("== %d Blert rooms, %d of ours" % (len(BR), len(OR)))
    if verbose:
        for R in BR + OR:
            print("  %-12s %s" % (R["name"], timeline(R)))
    B, O = measures(BR), measures(OR)
    results = {}
    for name in sorted(set(B) | set(O)):
        test = "(report" not in name
        r = compare(name, B[name], O[name], test=test)
        if r is not None and test:
            results[name] = r
    bad = [k for k, v in results.items() if not v]
    print("== %d measures tested, %d MATCH, %d DIFFER" % (len(results), len(results) - len(bad), len(bad)))
    for k in bad:
        print("   DIFFER:", k)
