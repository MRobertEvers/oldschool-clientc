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
import json
import re
import sys
from pathlib import Path

from summoning_script_sources import definition, script_dir


REPO = Path(__file__).resolve().parents[1]
LANE = REPO / "OSRS-Content/osrs239-content/server/scripts/ported_scape2009_summoning"
TABLE = LANE / "scripts/summoning_combat_table.rs2"
CONFIGS = LANE / "configs"
AUDIT = REPO / "docs/summoning_port/familiar_normal_combat_530.csv"
BOUNDARY = REPO / "docs/summoning_port/roster_boundary_530.json"
COMBAT = REPO / "OSRS-Content/osrs239-content/server/scripts/skill_combat/combat.rs2"
CLIENT_LANE = REPO / "OSRS-Content/osrs239-content/ported/scape2009_summoning"
# The preserved, unreviewed bulk import. A record inside it reaches the built
# cache only by being named in the boundary's `admitted_review_references`.
REVIEW_PREFIX = "summoning_roster_530"

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
        # The decision itself lives in the victim-taking form; the timer entry
        # above only resolves the owner's target and delegates to it.
        decision = definition(scripts, "proc,summoning_familiar_autoassist_victim")
        engagement = definition(scripts, "proc,summoning_familiar_engagement")
        # The multiway conjunction moved here when the singles-assist flag
        # landed: this proc is now the single place that decides whether a
        # familiar may swing where it is standing, under either rule.
        allowed = definition(scripts, "proc,summoning_familiar_assist_allowed")
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
        expect("~summoning_familiar_auto_assists(%summoning_familiar_type)" in decision,
               "auto-assist does not consult the per-familiar table")
        expect(allowed.count("map_multiway(") == 3,
               "the assist gate checks %d multiway coords, the pre-EoC rule is "
               "owner, familiar and victim" % allowed.count("map_multiway("))
        expect("~summoning_familiar_assist_allowed(" in decision
               and "~summoning_familiar_assist_allowed(" in engagement,
               "both the latch and the swing must ask the same assist gate, or "
               "a familiar can latch a target it may not legally hit")
        expect("npc_findcombat" in combat,
               "auto-assist must take the owner's own combat target")
        expect("npc_var_get(^summoning_npcvar_normal_attack_target)" in decision
               and "npc_var_set(^summoning_npcvar_normal_attack_target," in decision,
               "auto-assist must read and write the familiar's own combat-pulse "
               "victim, not re-derive it every tick")

        # ---- the 2014 clause that must stay out ----
        for banned in ("npc_findattacker", "combat_attacker", "combat-attacker"):
            expect(banned not in combat and banned not in decision,
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

        # ---- the familiar turns on the tick of the click ----
        #
        # The familiar timer would latch the same target within one tick anyway,
        # so this is the difference between "my familiar joins the fight" and
        # "my familiar joins the fight after standing still for 600ms".  Both
        # paths exist: the label is the prompt one, the timer is the one that
        # answers again after the victim dies.
        combat_rs2 = COMBAT.read_text(encoding="utf-8")
        # Anchored at a line start: the file also names this label inside a
        # backticked comment a few lines above its definition.
        start = re.search(r"^\[label,player_combat_start\]\n(.*?)(?=^\[)",
                          combat_rs2, re.M | re.S).group(1)
        expect("~summoning_familiar_autoassist_on_attack;" in start,
               "the player's attack no longer latches the familiar; it would "
               "wait for the next familiar timer tick instead")
        on_attack = definition(scripts, "proc,summoning_familiar_autoassist_on_attack")
        # The restore must come after the call: resolving the familiar and the
        # victim moves the active npc, and the label swings at whatever is
        # active when this returns.
        expect("~summoning_familiar_autoassist_victim($victim);" in on_attack and
               on_attack.index("npc_finduid($victim);")
               > on_attack.index("~summoning_familiar_autoassist_victim($victim);"),
               "the combat-start hook must restore the active npc it was called "
               "with, after delegating — the label goes on to swing at it")
        victim_proc = definition(scripts, "proc,summoning_familiar_autoassist_victim")
        expect("~summoning_account_enabled = false" in victim_proc,
               "the combat-start hook reaches content that is not gated on the "
               "Summoning feature")
        expect("~summoning_familiar_assist_allowed(" in victim_proc,
               "the click path must apply the same assist gate as the timer path")

        # ---- the singles-assist flag, and the thrall rule it turns on ----
        #
        # Off, the pre-EoC rule stands alone. On, a familiar may also swing in
        # a single-way area — but only under the conditions that make OldSchool
        # thralls legal there: it joins a fight its owner is already the other
        # side of, or one its owner explicitly commanded. Losing either
        # condition turns the flag from "a second rule" into "no rule".
        expect("combat_assist_singles" in allowed,
               "the assist gate no longer consults the server flag; the singles "
               "rule is either always on or always off")
        # The last statement, not merely a mention: reading the answer and then
        # returning `true` regardless is the exact shape this must not take.
        # An "auto" assist is legal in singles when the victim is already
        # fighting the owner back (mutual retaliation) OR when nobody has
        # claimed the victim yet (`npc_hastarget = false`) — the fresh-engage
        # case that lets the familiar join on the owner's first swing instead
        # of waiting for the victim to hit back, which is the thrall rule
        # ("attacks the target its owner is attacking", not "attacks the
        # target that is attacking its owner").
        allowed_code = [l.strip() for l in allowed.splitlines()
                        if l.strip() and not l.strip().startswith("//")]
        expect("npc_combatplayer" in allowed and "npc_hastarget" in allowed and
               "if ($victim_fights_owner = true) return(true);" in allowed and
               "if ($victim_claimed = true) return(false);" in allowed and
               allowed_code[-1] == "return(true);",
               "the singles rule must RETURN whether the victim is fighting this "
               "owner or is unclaimed — without the first half a familiar becomes "
               "a second attacker on something that already had one, and without "
               "the second half auto-assist cannot join until the victim retaliates")
        expect("^summoning_attack_commanded" in allowed,
               "an owner-commanded target (Goad, a targeted special) must be "
               "allowed in singles; the owner chose that fight")
        expect(allowed.index("combat_assist_singles")
               > allowed.index("map_multiway("),
               "multiway must be answered before the flag, so turning the flag "
               "off cannot narrow the pre-EoC rule")
        goad = definition(scripts, "proc,summoning_familiar_special_target_execute")
        expect("npc_var_set(^summoning_npcvar_attack_commanded, "
               "^summoning_attack_commanded);" in goad,
               "Goad no longer marks its target as owner-commanded, so it would "
               "be refused in singles")

        # ---- every familiar flinches and dies ----
        #
        # `defend_anim` and `death_anim` are engine fields, so a familiar
        # without them is silently a creature that stands perfectly still while
        # it is beaten to death.  The counts are the source's: 43 records name
        # defence animation 0 and 7 name none, which is why this is not 78.
        boundary = json.loads(BOUNDARY.read_text(encoding="utf-8"))
        admitted = frozenset(boundary.get("admitted_review_references", []))
        with_defend = [r for r in rows if r["defend_seq"]]
        with_death = [r for r in rows if r["death_seq"]]
        expect(len(with_defend) == 28,
               "%d familiars have a defend animation, the rev-530 records give "
               "28" % len(with_defend))
        expect(len(with_death) == 72,
               "%d familiars have a death animation, the rev-530 records give "
               "72" % len(with_death))
        for row in rows:
            body = blocks.get(row["npc"], "")
            for key, param in (("defend_seq", "defend_anim"), ("death_seq", "death_anim")):
                if not row[key]:
                    continue
                expect("param=%s,%s" % (param, row[key]) in body,
                       "%s has a %s in the source but its live block does not "
                       "state it" % (row["entry"], param))

        # ---- an animation the cache does not carry is worse than none ----
        #
        # A review-only record named from content compiles — the source pack
        # resolves the name — and then is absent from the staged cache, so the
        # familiar plays nothing and nothing says why.  Every such name must be
        # individually admitted; the bare prefix must never be.
        for row in rows:
            for key in ("attack_seq", "defend_seq", "death_seq"):
                name = row[key]
                if name.startswith(REVIEW_PREFIX):
                    expect(name in admitted,
                           "%s names review-only %s, which is held out of the "
                           "feature cache" % (row["entry"], name))
        expect(REVIEW_PREFIX not in admitted,
               "the bare roster prefix is admitted, which would let the whole "
               "preserved experiment into the cache")

        # ---- the admission closure is complete ----
        #
        # A sequence without its frame archive, or an archive without its
        # skeleton, animates nothing.  Both links are derived from the records
        # themselves by the generator; this re-derives the first one from the
        # staged-source side so a hand edit to the boundary cannot widen or
        # narrow the set unnoticed.
        seq_text = (CLIENT_LANE / "configs/summoning_roster_530.seq"
                    ).read_text(encoding="latin-1")
        seq_blocks = {m.group(1): m.group(2) for m in
                      re.finditer(r"^\[(\w+)\]\n(.*?)(?=^\[|\Z)", seq_text, re.M | re.S)}
        animations = {}
        for line in (CLIENT_LANE / "pack/0_animations.pack"
                     ).read_text(encoding="latin-1").splitlines():
            if "=" in line and line.split("=")[0].isdigit():
                key, value = line.split("=", 1)
                animations[int(key)] = value.rsplit("/", 1)[-1]
        for name in sorted(n for n in admitted if n in seq_blocks):
            for frame in re.findall(r"frame=(\d+)", seq_blocks[name]):
                archive = animations.get(int(frame) >> 16)
                # An archive that is not review-only is ordinary lane content
                # and stages on its own; only a roster one needs admitting.
                expect(archive is not None and (
                    not archive.startswith(REVIEW_PREFIX) or archive in admitted),
                       "admitted sequence %s draws frames from %s, which is not "
                       "admitted — it would animate nothing"
                       % (name, archive or "an archive outside this lane"))

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
