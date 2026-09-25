#!/usr/bin/env python3
"""Structural checks for the Theatre of Blood Nylocas support pillars.

Run by the torirsserver-scripts build. This exists because the pillars are the one
npc in the raid whose hitpoints scale UPWARDS as the party shrinks, and that
inverts an assumption the rest of the raid is built on.

Jagex published both ends of the curve, two weeks after ToB launched, in the
same post that cut the first 21 Nylocas waves:

    "Players in groups of five will find that the pillar has been reduced from
     140 hitpoints to 130 hitpoints. A solo player will find that the pillar has
     been reduced from 350 hitpoints to 330 hitpoints."
        -- Theatre of Blood Changes & Deadman Summer Finals, 21 June 2018

so the curve through those two points is `hp = hp5 + step * (5 - party)` with
hp5 = 130 and step = 50, and 2/3/4-man are that interpolation.

What this defends, in order of how badly each fails in the game rather than in
the compiler:

  1. The npc record's authored `hitpoints=` is the SOLO figure, not the cache's
     five-man one. `npc_statheal` cannot raise an npc above its config base and
     `~tob_nylo_set_hp` only ever subtracts, so a base left at the five-man 130
     would silently hand every party smaller than five a five-man pillar. The
     scaling line would still compile, still run, and never move a number -- the
     worst shape a bug can have. NO SCRIPT COMMAND CAN READ ANOTHER NPC TYPE'S
     BASE, which is why this invariant lives here instead of in ~tobrun.

  2. The five-man anchors still match the cache extract those numbers came from.
     A cache refresh that moved `stat4` on 8358/10790/10811 must break this
     rather than quietly redefine what a five-man pillar is.

  3. Regular's two ends are still Jagex's published pair (130 at five, 330 at
     one). This is the one row in the table that is not an interpolation, and it
     is the anchor the other three modes' shape is argued from.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOB = ROOT / "OSRS-Content/osrs239-content/server/scripts/minigames/minigame_tob/configs"
CACHE_DUMP = ROOT / "docs/minigames/theater_of_blood/sources/cache_npc_nylocas.txt"

# (constant suffix, npc block, cache npc name, cache id)
MODES = [
    ("",       "tob_nylocas_support",       "tob_nylocas_support",       8358),
    ("_hard",  "tob_nylocas_support_hard",  "tob_nylocas_support_hard",  10811),
    ("_entry", "tob_nylocas_support_story", "tob_nylocas_support_story", 10790),
]

PARTY_MAX = 5
JAGEX_FIVE_MAN = 130
JAGEX_SOLO = 330

errors = []


def fail(msg):
    errors.append(msg)


def read_constants():
    text = (TOB / "tob.constant").read_text()
    out = {}
    for name in ("^tob_nylo_pillar_hp", "^tob_nylo_pillar_hp_hard",
                 "^tob_nylo_pillar_hp_entry", "^tob_nylo_pillar_hp_step"):
        m = re.search(r"^%s\s*=\s*(\d+)\s*$" % re.escape(name), text, re.M)
        if not m:
            fail(f"tob.constant: {name} is missing")
            continue
        out[name] = int(m.group(1))
    return out


def read_npc_hitpoints():
    text = (TOB / "tob.npc").read_text()
    out = {}
    for _, block, _, _ in MODES:
        m = re.search(r"^\[%s\]\n(.*?)(?=^\[|\Z)" % re.escape(block), text, re.M | re.S)
        if not m:
            fail(f"tob.npc: [{block}] is missing")
            continue
        hp = re.search(r"^hitpoints\s*=\s*(\d+)\s*$", m.group(1), re.M)
        if not hp:
            fail(f"tob.npc: [{block}] declares no hitpoints")
            continue
        out[block] = int(hp.group(1))
    return out


def read_cache_stat4():
    text = CACHE_DUMP.read_text()
    out = {}
    for _, _, name, npc_id in MODES:
        m = re.search(r"^\[%s\][^\[]*?^stat4=(\d+)" % re.escape(name), text, re.M | re.S)
        if not m:
            fail(f"cache dump: [{name}] has no stat4")
            continue
        out[name] = int(m.group(1))
    return out


def main():
    consts = read_constants()
    npc_hp = read_npc_hitpoints()
    cache = read_cache_stat4()
    if errors:
        report()

    step = consts.get("^tob_nylo_pillar_hp_step")

    # 3. Jagex's published pair, for Regular.
    five = consts.get("^tob_nylo_pillar_hp")
    if five != JAGEX_FIVE_MAN:
        fail(f"^tob_nylo_pillar_hp is {five}; Jagex published {JAGEX_FIVE_MAN} at five players")
    solo = five + step * (PARTY_MAX - 1) if five is not None and step is not None else None
    if solo != JAGEX_SOLO:
        fail(f"the curve gives {solo} for a solo pillar; Jagex published {JAGEX_SOLO}"
             f" (hp5={five}, step={step})")

    for suffix, block, cache_name, npc_id in MODES:
        anchor = consts.get("^tob_nylo_pillar_hp" + suffix)
        base = npc_hp.get(block)
        stat4 = cache.get(cache_name)
        if anchor is None or base is None or step is None:
            continue

        # 2. The five-man anchor is still the cache's own stat4.
        if stat4 is not None and anchor != stat4:
            fail(f"{block}: five-man anchor {anchor} != cache stat4 {stat4} on npc {npc_id}")

        # 1. The authored base is the solo figure, so every smaller party is
        #    reachable by subtraction.
        want = anchor + step * (PARTY_MAX - 1)
        if base != want:
            fail(f"[{block}] hitpoints={base}, but the solo figure is {want} "
                 f"({anchor} + {step} x {PARTY_MAX - 1}). `npc_statheal` cannot raise an npc "
                 f"above its config base, so a base below the solo figure silently gives "
                 f"small parties a five-man pillar.")

    report()


def report():
    if errors:
        print("tob pillar contract FAILED:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        sys.exit(1)
    print(f"tob pillar contract OK: 3 modes, curve hp5 + {50} x (5 - party), "
          f"Jagex 130/330 anchored, npc bases hold the solo figure")
    sys.exit(0)


if __name__ == "__main__":
    main()
