#!/usr/bin/env python3
"""Regression gate for familiar auto-assist and the ordinary familiar swing.

Auto-assist is `Familiar.handleTickActions` lines 258-270: a combat familiar
joins the fight its owner is already in, without being commanded, while owner,
familiar and victim are all in a multiway zone.  Three things about it are easy
to break silently and are asserted here rather than left to be noticed in play:

  * the *set* of familiars it applies to.  It is not "every familiar" and not
    "every familiar with combat stats" — it is the source's
    `isCombatFamiliar() && !isBurdenBeast() && !isPeacefulFamiliar()`, which
    excludes every beast of burden and forager and the peaceful familiars the
    RuneScape Wiki still describes as self-defence-only.
  * the multiway conjunction, on all three parties.  Dropping any one of the
    three turns a pre-EoC rule into a post-EoC one.
  * the clause deliberately NOT ported.  2009scape falls back to the owner's
    `combat-attacker` when the owner has no target of their own, which is
    "Smarter Familiars" (2014-10-06) behaviour and five years later than this
    port's reference revision.  A future edit that "fixes" the missing fallback
    would be importing a later game into a 2009 port, so its absence is a test.

The generated table and the derived audit CSV must also agree, since the table
is what the tick reads and the CSV is what a reviewer reads.
"""

from __future__ import annotations

import csv
import re
import sys
from pathlib import Path

from summoning_script_sources import definition, script_dir


REPO = Path(__file__).resolve().parents[1]
LANE = REPO / "OSRS-Content/osrs239-content/server/scripts/ported_scape2009_summoning"
TABLE = LANE / "scripts/summoning_combat_table.rs2"
CONFIGS = LANE / "configs"
AUDIT = REPO / "docs/summoning_port/familiar_normal_combat_530.csv"

# Named rather than derived, so the test disagrees with the table when the
# table is wrong.  Peaceful familiars first — these are the ones the wiki's
# 2021 Combat Mode note calls out as previously self-defence-only — then a
# beast of burden, a forager, and a familiar whose record is not a combat one.
MUST_NOT_ASSIST = {
    "bunyip": "peaceful",
    "unicorn_stallion": "peaceful",
    "spirit_terrorbird": "peaceful beast of burden",
    "pack_yak": "peaceful beast of burden",
    "war_tortoise": "peaceful beast of burden",
    "abyssal_titan": "beast of burden",
    "thorny_snail": "beast of burden",
    "granite_crab": "forager",
    "bull_ant": "forager",
    "beaver": "forager, and not a combat record",
    "macaw": "forager, and not a combat record",
}

MUST_ASSIST = {
    "spirit_wolf": "melee",
    "dreadfowl": "magic",
    "honey_badger": "melee",
    "spirit_graahk": "melee",
    "steel_titan": "ranged",
    "iron_titan": "melee",
    "hydra": "ranged",
    "wolpertinger": "magic",
}


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def table_switch(text: str, proc: str) -> dict[int, str]:
    body = definition_from(text, proc)
    out = {}
    for line in body.splitlines():
        m = re.match(r"if \(\$type = (\d+)\) return\((.+)\);", line.strip())
        if m:
            out[int(m.group(1))] = m.group(2)
    return out


def definition_from(text: str, proc: str) -> str:
    start = text.index("[proc,%s]" % proc)
    end = text.find("\n[proc,", start + 1)
    return text[start:end if end != -1 else len(text)]


def npc_blocks() -> dict[str, str]:
    """npc name -> the body of the FIRST block declaring it, in load order.

    `walk_configs` sorts, and `mock230_content_npc` returns the first match, so
    a second block for one npc is dead.  Reading only the first is reading what
    the server reads.
    """
    out: dict[str, str] = {}
    for path in sorted(CONFIGS.glob("*.npc")):
        text = path.read_text(encoding="utf-8")
        for m in re.finditer(r"^\[(\w+)\]\n(.*?)(?=^\[|\Z)", text, re.M | re.S):
            out.setdefault(m.group(1), m.group(2))
    return out


def main() -> int:
    try:
        scripts = script_dir()
        combat = definition(scripts, "proc,summoning_familiar_autoassist")
        engagement = definition(scripts, "proc,summoning_familiar_engagement")
        swing = definition(scripts, "proc,summoning_familiar_generic_combat_tick")
        tick = definition(scripts, "proc,summoning_familiar_normal_combat_tick")
        table = TABLE.read_text(encoding="utf-8")
        rows = list(csv.DictReader(AUDIT.open()))
        blocks = npc_blocks()

        expect(len(rows) == 78, "audit CSV covers %d familiars, expected 78" % len(rows))
        by_entry = {r["entry"]: r for r in rows}

        # ---- the decision runs, and runs before the per-familiar handlers ----
        expect("~summoning_familiar_autoassist;" in tick,
               "the familiar tick never calls ~summoning_familiar_autoassist")
        expect(tick.index("~summoning_familiar_autoassist;")
               < tick.index("%summoning_familiar_type = 16"),
               "auto-assist must latch the target before any handler reads it")

        # ---- the source conjunction ----
        expect("~summoning_familiar_auto_assists(%summoning_familiar_type)" in combat,
               "auto-assist does not consult the per-familiar table")
        expect(combat.count("map_multiway(") == 3,
               "auto-assist checks %d multiway coords, the source checks owner, "
               "familiar and victim" % combat.count("map_multiway("))
        expect(engagement.count("map_multiway(") == 3,
               "the shared engagement resolver must re-check all three parties")
        expect("npc_findcombat" in combat,
               "auto-assist must take the owner's own combat target")
        expect("npc_var_get(^summoning_npcvar_normal_attack_target)" in combat
               and "npc_var_set(^summoning_npcvar_normal_attack_target," in combat,
               "auto-assist must read and write the familiar's own combat-pulse "
               "victim, not re-derive it every tick")

        # ---- the 2014 clause that must stay out ----
        for banned in ("npc_findattacker", "combat_attacker", "combat-attacker"):
            expect(banned not in combat,
                   "auto-assist reintroduces the owner's attacker fallback (%s), "
                   "which is Smarter Familiars (2014-10-06) and not rev-530"
                   % banned)

        # ---- who assists ----
        assists = table_switch(table, "summoning_familiar_auto_assists")
        for entry, why in MUST_NOT_ASSIST.items():
            row = by_entry[entry]
            expect(int(row["type"]) not in assists,
                   "%s auto-assists but is %s" % (entry, why))
            expect(row["auto_assist"] == "0",
                   "%s is marked auto_assist in the audit CSV but is %s" % (entry, why))
        for entry, style in MUST_ASSIST.items():
            row = by_entry[entry]
            expect(assists.get(int(row["type"])) == "true",
                   "%s does not auto-assist" % entry)
            expect(row["style"] == style,
                   "%s swings %s, expected %s" % (entry, row["style"], style))

        # ---- table and CSV agree ----
        for row in rows:
            type_id = int(row["type"])
            assisting = row["auto_assist"] == "1"
            expect((type_id in assists) == assisting,
                   "%s: table says auto-assist %s, CSV says %s"
                   % (row["entry"], type_id in assists, assisting))

        anims = table_switch(table, "summoning_familiar_attack_anim")
        styles = table_switch(table, "summoning_familiar_attack_style")
        reaches = table_switch(table, "summoning_familiar_attack_reach")
        expected_reach = {"melee": "1", "ranged": "7", "magic": "10"}
        for row in rows:
            type_id = int(row["type"])
            if row["armed"] != "1":
                expect(type_id not in anims,
                       "%s has an attack animation but is not armed" % row["entry"])
                continue
            expect(anims.get(type_id) == row["attack_seq"],
                   "%s: table animation %r, CSV %r"
                   % (row["entry"], anims.get(type_id), row["attack_seq"]))
            expect(styles.get(type_id) == "^summoning_style_" + row["style"],
                   "%s: table style %r does not match CSV %r"
                   % (row["entry"], styles.get(type_id), row["style"]))
            expect(reaches.get(type_id) == expected_reach[row["style"]],
                   "%s: %s reach is %r, the source handler opens at %s"
                   % (row["entry"], row["style"], reaches.get(type_id),
                      expected_reach[row["style"]]))

        # ---- an armed familiar can actually roll a swing ----
        #
        # The rolls read the familiar record's own stats.  A familiar with no
        # live stat block rolls against engine defaults and hits for nothing,
        # and nothing at runtime says so — which is exactly the failure the
        # first-block-wins rule produces when a generated block lands behind a
        # hand-authored one.
        for row in rows:
            if row["armed"] != "1":
                continue
            body = blocks.get(row["npc"])
            expect(body is not None,
                   "%s swings but has no .npc block at all" % row["entry"])
            for field in ("attack=", "strength=", "defence="):
                expect(field in body,
                       "%s swings but its live block has no %s — a second block "
                       "in another file is dead, not merged"
                       % (row["entry"], field.rstrip("=")))
            if row["style"] == "magic":
                expect("magicdamage" in body or "magic=" in body,
                       "%s casts but its block has no magic profile" % row["entry"])

        # ---- the swing itself ----
        expect("~summoning_familiar_attack_anim(" in swing
               and "~summoning_familiar_attack_style(" in swing
               and "~summoning_familiar_attack_speed(" in swing
               and "~summoning_familiar_attack_reach(" in swing,
               "the generic swing must take all four of its per-familiar facts "
               "from the generated table")
        expect("~summoning_familiar_engagement($familiar)" in swing,
               "the generic swing must resolve its victim through the shared "
               "engagement gate, so the multiway rule is not restated per swing")
        expect("npc_queue(2," in swing, "the generic swing lands no damage")
        expect("sound_synth(" in swing,
               "the generic swing is the site the lane's six combat_audio "
               "attack rows were stated for")

        # ---- honey badger's uncharged tick reaches the shared swing ----
        badger = definition(scripts, "proc,summoning_honey_badger_charged_attack_tick")
        expect("(boolean)" in badger.splitlines()[0],
               "the honey badger charged tick must report whether it claimed "
               "the tick, or an uncharged badger stands still")
        expect("~summoning_honey_badger_charged_attack_tick = false" in tick,
               "an uncharged honey badger never falls through to the ordinary "
               "swing its source MultiSwingHandler gives it")
    except (AssertionError, KeyError, ValueError) as exc:
        print("FAIL: %s" % exc)
        return 1
    print("summoning auto-assist gate: %d familiars, %d auto-assist, %d armed"
          % (len(rows),
             sum(1 for r in rows if r["auto_assist"] == "1"),
             sum(1 for r in rows if r["armed"] == "1")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
