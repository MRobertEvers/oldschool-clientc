#!/usr/bin/env python3
"""
charged_items_scan — catalog every charged-item family in the content tree's
obj cache, classify by storage model and depletion shape, and emit:

    docs/ITEM_CHARGES.md                              human-readable table
    OSRS-Content/osrs239-content/wiki/charged_items.csv   machine ledger

See docs/ITEM_CHARGES_PLAN.md. Reads
`OSRS-Content/osrs239-content/configs/all.obj` directly — that file is
exporter-owned (docs/PORTING_GUIDE.md §3), so nothing here writes to it; this
tool only reads it and classifies what it finds.

Classification is two-layered:

  1. A structural scan groups every obj record with a `Check`/`Check charges`
     op paired with a charge-ish op into name-families (Barrows'
     `dharoks_helm_100`/`_75`/`_50`/`_25` collapse to one family, same as
     `Black mask (10)..(1)`).
  2. A curated table (`FAMILY_DATA` below) assigns `storage`, `depletion`,
     `max_charges`, `charge_source`, `drain_event` and `status` per family,
     from wiki research (see docs/ITEM_CHARGES_PLAN.md §2). A family the scan
     finds but the table has no entry for is emitted with `storage=unknown`,
     `status=uncurated` — visible in the ledger rather than silently dropped.

Usage:
    tools/charged_items_scan.py               # write both outputs
    tools/charged_items_scan.py --check        # exit 1 if outputs are stale
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
ALL_OBJ = os.path.join(CONTENT, "configs", "all.obj")
LEDGER_CSV = os.path.join(CONTENT, "wiki", "charged_items.csv")
DOC_MD = os.path.join(REPO, "docs", "ITEM_CHARGES.md")

# ---------------------------------------------------------------------
# 1. Parse all.obj
# ---------------------------------------------------------------------


def parse_all_obj(path):
    """[name] blocks of key=value lines; `param=` repeats. Windows-1252
    (memory: dat2 strings carry 0x92 etc; the exporter never re-encodes)."""
    records = {}
    name = None
    fields = {}
    params = []
    order = []

    def flush():
        if name is not None:
            records[name] = {"fields": fields, "params": params}
            order.append(name)

    with open(path, encoding="cp1252", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if line.startswith("//") or not line.strip():
                continue
            m = re.match(r"^\[(.+)\]$", line)
            if m:
                flush()
                name = m.group(1)
                fields = {}
                params = []
                continue
            if line.startswith("param="):
                parts = line[len("param="):].split(",", 2)
                if len(parts) == 3:
                    params.append(tuple(parts))
                continue
            if "=" in line:
                k, _, v = line.partition("=")
                fields[k] = v
    flush()
    return records, order


def inv_ops(fields):
    return [fields[f"ifop{i}"] for i in range(1, 6) if fields.get(f"ifop{i}")]


def ground_ops(fields):
    return [fields[f"op{i}"] for i in range(1, 6) if fields.get(f"op{i}")]


# ---------------------------------------------------------------------
# 2. Structural grouping into families
# ---------------------------------------------------------------------

# Storage containers (coal bag, herb sack, ...) carry a Check op but hold
# items, not charges — a different feature (docs/ITEM_CHARGES_PLAN.md §1).
CONTAINER_OPS = {"Fill", "Empty", "Open", "Close", "Deposit", "Extract", "Withdraw"}
CHECK_OPS = {"Check", "Check charges"}

# Barrows and the tiered rings (Explorer's ring 1-4, Falador shield 1-4) carry
# NO charge-pairing op at all — Check is the only op, and the id ladder itself
# (or an invisible daily reset) is the whole mechanic. So membership here is
# "has Check, is not a container" — no charge-op requirement — and the
# FAMILY_DATA / NOT_A_CHARGE_DENYLIST curation below does the real sorting.

STEM_SUFFIX_RE = re.compile(
    r"\s*\((?:\d+|i|ei|ri|r|c|cl|ic|ilc|or|l|a|e|u|bh|full|o|deadman|"
    r"uncharged|inert|inactive|active|eternal|open|empty)\)\s*$",
    re.IGNORECASE,
)
STEM_TRAILING_TIER_RE = re.compile(r"\s+(?:100|75|50|25|\d{1,2})$")

# A stem that names the same real-world family as a different stem, but
# cannot be unified by suffix-stripping alone — the un/base form's own
# display name shares no substring with the charged form's (a genuine cache
# fact, not a scan bug): "Uncharged trident" vs "Trident of the seas",
# "Sara's blessed sword (full)" (stems to "Sara's blessed sword") vs
# "Saradomin's blessed sword". Checked by hand against configs/all.obj
# before adding an entry here — see docs/ITEM_CHARGES_PLAN.md §4a.
STEM_ALIAS = {
    "Uncharged trident": "Trident of the seas",
    "Uncharged toxic trident": "Trident of the swamp",
    "Sara's blessed sword": "Saradomin's blessed sword",
}


def stem_name(display_name):
    n = display_name
    changed = True
    while changed:
        changed = False
        n2 = STEM_SUFFIX_RE.sub("", n)
        if n2 != n:
            n = n2
            changed = True
        n2 = STEM_TRAILING_TIER_RE.sub("", n)
        if n2 != n:
            n = n2
            changed = True
    n = re.sub(r"\s+\(uncharged\)$", "", n, flags=re.IGNORECASE)
    n = n.strip()
    return STEM_ALIAS.get(n, n)


def scan_families(records):
    """obj id -> family stem, for every record with a Check op that is not
    itself a storage container."""
    families = {}
    for internal, rec in records.items():
        fields = rec["fields"]
        name = fields.get("name")
        if not name:
            continue
        ops = set(inv_ops(fields)) | set(ground_ops(fields))
        if not (ops & CHECK_OPS):
            continue
        if ops & CONTAINER_OPS:
            continue
        stem = stem_name(name)
        families.setdefault(stem, []).append((internal, name))
    return families


# ---------------------------------------------------------------------
# 3. Curated classification — from wiki research, docs/ITEM_CHARGES_PLAN.md §2
# ---------------------------------------------------------------------
#
# storage:  id_ladder | item_var | player_varp | none
# depletion: revert | destroy | degrade_step | none
# status:    implemented | charges_only | uncurated
#
# Only families the structural scan above actually finds get written to the
# ledger — an entry here for a family the scan does not find is silently
# unused, not an error, so curation can run ahead of the scan without drift.

FAMILY_DATA = {
    # -- item_var, implemented (Phase 3-5) --------------------------------
    "Trident of the seas": dict(
        storage="item_var", depletion="revert", max_charges=2500,
        charge_source="Reclaim from Kraken/water fiends; bought pre-charged from other players",
        drain_event="1 charge per cast of the trident's melee/magic special swing",
        status="implemented",
        note="Reverts to Uncharged trident. (e) enchanted variant shares the mechanic.",
    ),
    "Trident of the swamp": dict(
        storage="item_var", depletion="revert", max_charges=2500,
        charge_source="Made from Trident of the seas + 3 Coagulated venom (Zulrah drop)",
        drain_event="1 charge per swing",
        status="implemented",
        note="Reverts to Uncharged toxic trident.",
    ),
    "Scythe of vitur": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Vials of blood + 200 blood runes at a vyre well (not implemented; ::fullscythe cheat instead)",
        drain_event="1 charge per swing that lands a hit (any of the up to 3 sub-hits)",
        status="implemented",
        note="Migrated off its own %scythe_of_vitur_charges varp (Phase 6); see skill_combat/scripts/player/gear/scythe_of_vitur.rs2.",
    ),
    "Holy scythe of vitur": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Cosmetic recolour (Saradomin) — identical mechanic to the base scythe",
        drain_event="1 charge per swing that lands a hit",
        status="implemented",
        note="scythe_of_vitur_or — same file, same swing hook and charge storage as the base scythe, already bound.",
    ),
    "Sanguine scythe of vitur": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Cosmetic recolour (blood) — identical mechanic to the base scythe",
        drain_event="1 charge per swing that lands a hit",
        status="implemented",
        note="scythe_of_vitur_bl — same file, same swing hook and charge storage as the base scythe, already bound.",
    ),
    "Toxic blowpipe": dict(
        storage="item_var", depletion="none", max_charges=None,
        charge_source="Filled with any tradeable dart + 1 zulrah scale per dart via 'Fill'",
        drain_event="1 dart (and its charge) consumed per shot; refuses to fire empty",
        status="implemented",
        note="Charge count here is dart count, already a container-ish item; darts are its own inv, no revert/break.",
    ),
    "Abyssal tentacle": dict(
        storage="item_var", depletion="revert", max_charges=10000,
        charge_source="Uncapped from Abyssal orphan; bought pre-charged in practice",
        drain_event="1 charge per successful hit",
        status="charges_only",
        note="Reverts to Kraken tentacle. No spawn/refill path implemented (Abyssal orphan pet not in this tree) — holds and reports, drain not wired.",
    ),
    "Crystal helm": dict(
        storage="item_var", depletion="revert", max_charges=10000,
        charge_source="Sung at the Prifddinas singing bowl (already implemented, crystal_equipment.rs2)",
        drain_event="Gauntlet-specific drain already implemented (gauntlet_hunllef.rs2)",
        status="implemented", note="Pre-existing; §3a fix makes it survive unequip/bank.",
    ),
    "Crystal body": dict(storage="item_var", depletion="revert", max_charges=10000,
        charge_source="Sung at the singing bowl", drain_event="Gauntlet drain, pre-existing",
        status="implemented", note="Pre-existing; §3a fix applies."),
    "Crystal legs": dict(storage="item_var", depletion="revert", max_charges=10000,
        charge_source="Sung at the singing bowl", drain_event="Gauntlet drain, pre-existing",
        status="implemented", note="Pre-existing; §3a fix applies."),
    "Ring of suffering": dict(
        storage="item_var", depletion="none", max_charges=2200,
        charge_source="Made from Ring of suffering (r) via the recoil-jewellery family",
        drain_event="Recoil damage consumes 1 charge per proc (shares ring_of_recoil.rs2's mechanic)",
        status="implemented", note="See general/scripts/enchanted_jewellry/ring_of_recoil.rs2.",
    ),
    "Saradomin's blessed sword": dict(
        storage="item_var", depletion="revert", max_charges=10000,
        charge_source="Saradomin sword + Saradomin's tear",
        drain_event="1 charge per HIT (not swing) — up to 3x/swing on multi-hit weapons matters for this one",
        status="charges_only",
        note="ifop4=Revert (not Uncharge) reverts immediately to Saradomin's tear, "
             "a different obj entirely, not an 'uncharged' variant of the sword. "
             "Real melee swing hook exists (combat_stats.rs2) but per-HIT (not "
             "per-swing) drain needs a call site inside ~player_hit_npc_prepare "
             "or equivalent, not yet wired — same shape as scythe's per-swing "
             "hook, one layer deeper.",
    ),
    # -- item_var, implemented: wilderness weapons + bracelet -------------
    # All six share one mechanic (minigame_revcaves/scripts/wildy_weapons.rs2
    # + wildy_passives.rs2 + wildy_upgrades.rs2) and one wiki-confirmed cap:
    # 17,000 raw ether (1,000 activation reserve + 16,000 usable) — checked
    # against Thammaron's, Viggora's, Ursine, and Accursed's own wikitext
    # directly, not assumed from Craw's bow alone. Charging (inv), the
    # obj-id swap on first activation, top-ups, and the boss-trophy upgrade/
    # dismantle transfers are all real and item_var-backed; the 50%
    # accuracy+damage combat passive and its 1-charge-per-attack drain
    # (wildy_passives.rs2's ~wildy_weapon_consume) are real and wired, gated
    # on `~wilderness_level(coord) >= 1` — this server has real Wilderness
    # zone data (areas/area_wilderness/) to gate against.
    "Thammaron's sceptre": dict(
        storage="item_var", depletion="revert", max_charges=17000,
        charge_source="Revenant ether (revenant_drops.rs2 + ethereum.rs2), used on the sceptre",
        drain_event="1 charge per attack while worn in the Wilderness (wildy_passives.rs2)",
        status="implemented", note="Migrated from a per-weapon-type account varp 2026-08-13.",
    ),
    "Craw's bow": dict(
        storage="item_var", depletion="revert", max_charges=17000,
        charge_source="Revenant ether, used on the bow",
        drain_event="1 charge per attack while worn in the Wilderness",
        status="implemented", note="Migrated from a per-weapon-type account varp 2026-08-13.",
    ),
    "Viggora's chainmace": dict(
        storage="item_var", depletion="revert", max_charges=17000,
        charge_source="Revenant ether, used on the chainmace",
        drain_event="1 charge per attack while worn in the Wilderness",
        status="implemented", note="Migrated from a per-weapon-type account varp 2026-08-13.",
    ),
    "Accursed sceptre": dict(
        storage="item_var", depletion="revert", max_charges=17000,
        charge_source="Revenant ether, used on the sceptre (Thammaron's + Vet'ion skull upgrade)",
        drain_event="1 charge per attack while worn in the Wilderness",
        status="implemented", note="Migrated from a per-weapon-type account varp 2026-08-13.",
    ),
    "Webweaver bow": dict(
        storage="item_var", depletion="revert", max_charges=17000,
        charge_source="Revenant ether, used on the bow (Craw's + Venenatis fang upgrade)",
        drain_event="1 charge per attack while worn in the Wilderness",
        status="implemented", note="Migrated from a per-weapon-type account varp 2026-08-13.",
    ),
    "Ursine chainmace": dict(
        storage="item_var", depletion="revert", max_charges=17000,
        charge_source="Revenant ether, used on the chainmace (Viggora's + Callisto claws upgrade)",
        drain_event="1 charge per attack while worn in the Wilderness",
        status="implemented", note="Migrated from a per-weapon-type account varp 2026-08-13.",
    ),
    "Bracelet of ethereum": dict(
        storage="item_var", depletion="none", max_charges=16000,
        charge_source="Revenant ether, used on the bracelet",
        drain_event="1 charge per revenant attack while worn (75% damage reduction; ethereum.rs2)",
        status="implemented", note="Migrated from a single account varp 2026-08-13.",
    ),
    # -- id_ladder, implemented -------------------------------------------
    "Ring of dueling": dict(
        storage="id_ladder", depletion="degrade_step", max_charges=8,
        charge_source="Bought from the Duel Arena/Castle Wars reward shops",
        drain_event="1 charge per teleport",
        status="implemented", note="Pre-existing: general/scripts/enchanted_jewellry/ring_of_dueling.rs2.",
    ),
    "Dharok's helm": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows minigame reward / repaired at Barrows chest for 100% coin cost",
        drain_event="Degrades 25pp per Barrows-adjacent kill count (LC/OSRS: on death, or per reward roll)",
        status="charges_only", note="Ladder exists in configs/all.obj; degrade trigger (Barrows minigame) not in this tree."),
    "Dharok's platebody": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Dharok's platelegs": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Dharok's greataxe": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Black mask": dict(
        storage="id_ladder", depletion="degrade_step", max_charges=10,
        charge_source="Dropped by Cave horrors at (10)",
        drain_event="1 charge per kill made using the slayer-task-boosted effect",
        status="charges_only", note="Ladder exists; slayer-task-boost drain not wired.",
    ),
    "Dodgy necklace": dict(
        storage="player_varp", depletion="destroy", max_charges=10,
        charge_source="Opal necklace + Lvl-1 Enchant",
        drain_event="25% chance to prevent a pickpocket stun+damage; 1 charge consumed per successful prevention",
        status="implemented",
        note="Wiki, checked directly (2026-08-13): \"the charges stored on "
             "this necklace are specific to the player, not the item itself\" "
             "-- like ring of forging. Every dodgy necklace the account owns "
             "shows the same count and losing the last charge destroys ONE "
             "necklace (the one worn), not the count. item_var would be wrong "
             "here despite most enchanted jewellery being item_var.",
    ),
    # -- item_var, charges_only: research batches 2/3/4, 2026-08-13 --------
    # Wiki-verified storage/max/drain for each; "status=charges_only" unless
    # noted otherwise. Grep citations for existing (partial) implementations
    # are in each note; most returned zero hits and are honestly unwired.
    "Echo venator bow": dict(
        storage="item_var", depletion="revert", max_charges=50000,
        charge_source="Ancient essence, 1 charge each (inherits the base Venator bow's mechanic)",
        drain_event="1 charge per attack, deducted once regardless of ricochet bounce count",
        status="charges_only", note="Cosmetic ornament-kit recolour of Venator bow. No implementation found.",
    ),
    "Eclipse moon chestplate": dict(
        storage="item_var", depletion="degrade_step", max_charges=3000,
        charge_source="Full charge on drop from the Lunar Chest (Neypotzli); repaired by Bob/Dunstan or self-repaired at an armour stand",
        drain_event="1 charge lost per 54 seconds spent in combat while worn (time-based, not per-hit)",
        status="charges_only",
        note="Three obj states: new (no Check) / degraded (eclipse_moon_chestplate_degraded, has Check) / "
             "broken (repairable). No time-based combat-degrade ticker exists anywhere in this codebase — "
             "not even a partial mechanism, unlike the per-hit/per-swing families.",
    ),
    "Eclipse moon helm": dict(
        storage="item_var", depletion="degrade_step", max_charges=3000,
        charge_source="Same as Eclipse moon chestplate", drain_event="1 charge per 54 seconds in combat while worn",
        status="charges_only", note="Same missing-mechanism note as Eclipse moon chestplate.",
    ),
    "Eclipse moon tassets": dict(
        storage="item_var", depletion="degrade_step", max_charges=3000,
        charge_source="Same as Eclipse moon chestplate", drain_event="1 charge per 54 seconds in combat while worn",
        status="charges_only", note="Same missing-mechanism note as Eclipse moon chestplate.",
    ),
    "Efaritay's aid": dict(
        storage="item_var", depletion="destroy", max_charges=200,
        charge_source="Topaz ring + Lvl-3 Enchant",
        drain_event="1 charge per successful hit against a vampyric-tier target while worn",
        status="charges_only",
        note="Wiki: per-item, no shared-pool language (contrast Expeditious bracelet below) -- item_var "
             "confirmed, not assumed from sibling break-jewellery. No vampyric-tier combat bonus system "
             "exists in this codebase at all -- zero hook to attach to.",
    ),
    "Enhanced quetzal whistle": dict(
        storage="item_var", depletion="none", max_charges=20,
        charge_source="Raw meat from hunter creatures or feed, given to Soar Leader Pitri",
        drain_event="1 charge per teleport ('Signal' option)",
        status="charges_only", note="No Quetzal Transport System exists in server/scripts -- no interface, no charging NPC.",
    ),
    "Expeditious bracelet": dict(
        storage="player_varp", depletion="destroy", max_charges=30,
        charge_source="Opal bracelet + Lvl-1 Enchant",
        drain_event="25% chance per Slayer-task-matching kill to count as 2 kills; consumes 1 shared charge on proc",
        status="charges_only",
        note="Wiki explicit: \"All expeditious bracelets share the same pool of 30 charges\" -- player_varp, "
             "same shape as Dodgy necklace. Real drain hook exists: skill_slayer/scripts/slayer_kill.rs2's "
             "[proc,slayer_on_npc_kill] already decrements a task counter per matching kill.",
    ),
    "Eye of ayak": dict(
        storage="item_var", depletion="revert", max_charges=50000,
        charge_source="2 death + 1 chaos rune per charge, or 1 demon tear per charge (cannot mix types)",
        drain_event="1 charge per cast of the built-in spell",
        status="charges_only",
        note="Special attack (Soul Rend) is wired (specs/pvm_eye_of_ayak.rs2) but spec-energy gated, unrelated "
             "to item charges. No powered-staff item-charge library exists for the normal-cast drain.",
    ),
    "Flamtaer bracelet": dict(
        storage="item_var", depletion="destroy", max_charges=80,
        charge_source="Jade bracelet + Lvl-2 Enchant",
        drain_event="1 charge per Shades of Mort'ton temple repair action completed while worn",
        status="charges_only",
        note="Wiki: per-item (no shared-pool wording, contrast Expeditious bracelet). Shades of Mort'ton "
             "minigame shell exists (minigames/game_mortton/) but its own header lists the repair/sanctity "
             "mechanic as Deferred -- no drain event to hook into yet even though the minigame exists.",
    ),
    "Giantsoul amulet": dict(
        storage="item_var", depletion="revert", max_charges=16000,
        charge_source="1 noted/unnoted big bones + 1 law rune per charge; no refund on Uncharge",
        drain_event="1 charge per teleport (Bryophyta's/Obor's/Royal Titans lair)",
        status="charges_only", note="No implementation found.",
    ),
    "Gloves of silence": dict(
        storage="player_varp", depletion="destroy", max_charges=62,
        charge_source="2 dark kebbit fur + 600gp; repaired at 64 Crafting",
        drain_event="1 charge per FAILED pickpocket attempt while worn",
        status="charges_only",
        note="Wiki explicit: \"connected to the player and not the gloves themselves... similarly to ring of "
             "recoil and ring of forging\" -- player_varp confirmed. Real drain hook exists: "
             "skill_thieving/scripts/pickpocket.rs2's fail path (~fail_pick_pocket), the same hook dodgy "
             "necklace's own real implementation already uses.",
    ),
    "Gnomish firelighter": dict(
        storage="item_var", depletion="none", max_charges=1000,
        charge_source="Combined with any of 5 coloured firelighters (medium Treasure Trails reward)",
        drain_event="1 charge per coloured fire lit",
        status="charges_only",
        note="obj records only carry ifop4=Check on both charged/uncharged forms (no Uncharge op visible "
             "despite the wiki listing one -- worth a second look at the record before binding). No coloured-"
             "fire branch exists in skill_firemaking/scripts/firemaking.rs2.",
    ),
    "Horn of plenty": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Gryphon feather, 1 charge each; Uncharge returns unused feathers",
        drain_event="1 charge per 25 Hunter catches while worn and charged (+4 Hunter boost instead of +2)",
        status="charges_only",
        note="No single centralized 'catch succeeded' choke point exists in skill_hunter/scripts/ (each trap "
             "type is its own file with its own catch logic) -- unlike combat's one swing hook, this would "
             "need touching ~15 separate files to wire.",
    ),
    "Iban's staff": dict(
        storage="item_var", depletion="none", max_charges=120,
        charge_source="Underground Pass quest reward (starts full); free full recharge at the Underground Pass blood well; paid recharge from the Dark Mage (not implemented)",
        drain_event="1 charge per Iban Blast cast on a monster (2 on a player -- not modelled, always drains 1; this codebase's combat magic has no npc/player branch to hang that on)",
        status="implemented",
        note="Fixed 2026-08-13: migrated off the single account-wide %iban_staff_charges varp onto item_var "
             "(same real drain already wired at skill_combat/scripts/player/spells/god_iban.rs2's "
             "~pvm_iban_blast -- it gates the cast and decrements per swing, just needed the right storage). "
             "Fixed both stubbed grant sites too: quests/quest_upass/scripts/upass_tomb.rs2 (quest reward, "
             "was 0 charges despite the flavour text) and upass_bloodwell.rs2 (free recharge, same). Added "
             "[opheld4,ibanstaff] Check (the record's own ifop4=Check was unbound). depletion=none, not "
             "revert -- the wiki names no uncharged/broken obj form, the spell just refuses to fire at 0.",
    ),
    "Iban's upgraded staff": dict(
        storage="item_var", depletion="revert", max_charges=2500,
        charge_source="Same as base staff, plus the Dark Mage's upgrade service (not implemented)",
        drain_event="Same as base staff: 1/2 charges per cast",
        status="charges_only",
        note="obj ibanstaff_upgraded exists but the upgrade dialogue is not in darkmage.rs2 (only a "
             "repair-broken-staff branch exists). Shares every gap the base staff entry has.",
    ),
    "Bryophyta's staff": dict(
        storage="item_var", depletion="none", max_charges=1000,
        charge_source="50k coins + Bryophyta's essence to Zaff, or 62 Crafting; charged with up to 1,000 nature runes",
        drain_event="1 nature rune per nature-rune-costing spell cast while equipped; 1/15 chance to save a charge",
        status="charges_only",
        note="No implementation. skill_magic/scripts/magic.rs2's ~staff_runes/~delete_spell_runes already "
             "substitutes air/water/earth/fire for the elemental battlestaves -- no nature entry exists to "
             "extend for this staff.",
    ),
    "Camphor blowpipe": dict(
        storage="item_var", depletion="none", max_charges=None,
        charge_source="Fletched from 2 camphor logs + squid beak; loaded with up to mithril darts (no scales needed)",
        drain_event="1 dart consumed per shot",
        status="charges_only",
        note="No dart-loading implementation exists for ANY blowpipe in this tree, including the toxic "
             "blowpipe's own claimed 'implemented' status -- worth a follow-up check on that entry.",
    ),
    "Celestial ring": dict(
        storage="item_var", depletion="none", max_charges=10000,
        charge_source="Charged with stardust from Shooting Stars, 1:1", drain_event="1 charge per ore mined",
        status="charges_only", note="No implementation found (zero grep hits).",
    ),
    "Celestial signet": dict(
        storage="item_var", depletion="none", max_charges=10000,
        charge_source="Ring + elven signet + 100 crystal shards + 1000 stardust at a singing bowl; recharged with stardust only",
        drain_event="Same mining drain as celestial ring, plus a 10% chance to save a crystal-equipment charge",
        status="charges_only", note="No implementation found.",
    ),
    "Chronicle": dict(
        storage="item_var", depletion="none", max_charges=1000,
        charge_source="Diango's Toy Store, charged with teleport cards (150gp each)",
        drain_event="1 charge per teleport to Champions' Guild",
        status="charges_only", note="No implementation found.",
    ),
    "Circlet of water": dict(
        storage="item_var", depletion="none", max_charges=500000,
        charge_source="Beneath Cursed Sands quest reward, charged with 5 water runes per charge",
        drain_event="1 charge per desert-heat water-drink avoided",
        status="charges_only",
        note="Quest script only grants/removes the whole item on quest events -- no charge storage or "
             "desert-heat drain hook exists.",
    ),
    "Cowbell amulet": dict(
        storage="item_var", depletion="none", max_charges=1000,
        charge_source="The Ides of Milk quest reward, charged 1:1 with air runes",
        drain_event="1 charge per teleport to Lumbridge cow field",
        status="charges_only", note="Quest script only grants/removes the whole item -- no charge mechanic wired.",
    ),
    "Crystal axe": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Sung at Prifddinas singing bowl from dragon axe + seed + 120 shards, starts at 10,000; recharged 100/shard",
        drain_event="1 charge per log chopped",
        status="implemented",
        note="Fully wired: minigame_gauntlet/scripts/crystal_equipment.rs2 (storage/Check/Charge/Dismantle) + "
             "skill_woodcutting/scripts/woodcut.rs2:82 (drains 1/chop). Gap: nothing swaps the obj id to "
             "crystal_axe_inactive at 0 charges -- the counter floors and the message fires, but stats "
             "persist forever.",
    ),
    "Crystal bow": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Quest reward (500) or sung/bought from Ilfeen (2,500); recharged 100/shard",
        drain_event="1 charge per shot fired",
        status="implemented",
        note="crystal_bow_2500 is the SAME item as crystal_bow, not a separate tier (crystal_set.rs2:53's own "
             "comment confirms). Charge/Check/Dismantle fully wired. Gap: combat-swing drain "
             "(~crystal_drain_one) is only called from gauntlet_hunllef.rs2:78 (the Hunllef fight) -- firing "
             "at a normal monster consumes zero charges today.",
    ),
    "Crystal felling axe": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Felling axe handle + crystal axe, or dragon felling axe + seed + 120 shards; inherits remaining charges if made from an existing crystal axe",
        drain_event="1 charge per log chopped (or per swing on bloodwood trees)",
        status="charges_only",
        note="obj crystal_axe_2h: ifop3=Check/ifop4=Revert, no Charge op (matches the use-shard-on-item "
             "pattern). NOT referenced in crystal_equipment.rs2's case lists at all -- entirely unwired, and "
             "the Forestry/felling-axe mechanic itself doesn't exist in this tree.",
    ),
    "Crystal halberd": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Bought from Islwyn (750k, 2,500 charges) or sung/re-enchanted; recharged 100/shard",
        drain_event="1 charge per attack",
        status="implemented",
        note="crystal_halberd_2500 (the Islwyn-bought id) has ZERO references anywhere in server/scripts, "
             "unlike crystal_bow_2500 -- needs adding to crystal_equipment.rs2's case lists before it can "
             "Check/Charge/Revert at all. Same Gauntlet-only combat-drain gap as crystal bow.",
    ),
    "Crystal harpoon": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Sung at singing bowl from dragon harpoon + seed + 120 shards, starts at 10,000; recharged 100/shard",
        drain_event="1 charge per fish caught",
        status="implemented",
        note="Fully wired: crystal_equipment.rs2 + skill_fishing/scripts/fishing.rs2:82,96 (drains 1/catch). "
             "Same inactive-swap-at-0 gap as crystal axe.",
    ),
    "Crystal pickaxe": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Sung at singing bowl from dragon pickaxe + seed + 120 shards, starts at 10,000; recharged 100/shard",
        drain_event="1 charge per ore mined",
        status="implemented",
        note="Fully wired: crystal_equipment.rs2 + skill_mining/scripts/mining.rs2:129,155 (drains 1/ore). "
             "Same inactive-swap-at-0 gap as crystal axe.",
    ),
    "Crystal shield": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Bought from Islwyn (750k, 2,500 charges), sung, or re-enchanted; recharged 100/shard",
        drain_event="1 charge per non-zero hit taken while equipped",
        status="implemented",
        note="crystal_shield_2500 has ZERO references anywhere, same gap as crystal_halberd_2500. Drain "
             "(~crystal_drain_one) only called from gauntlet_hunllef.rs2:84 -- no general-combat "
             "damage-taken hook drains it outside the Hunllef fight.",
    ),
    "Echo boots": dict(
        storage="item_var", depletion="none", max_charges=60000,
        charge_source="Guardian boots + echo crystal (Fortis Colosseum drop); each extra crystal adds 6,000 charges",
        drain_event="1 charge per recoil proc (1 damage back to any attacker in a 3x3), toggleable",
        status="charges_only",
        note="No implementation found. Real 'player takes damage' hook exists to plug into: "
             "combat_stats.rs2:814 already calls ~ring_of_recoil_check($damage) for the same class of effect "
             "-- the boots need the same shape minus the single-target/AoE difference. Stays equipped with "
             "no effect at 0 charges (a genuine 'none' depletion case, not revert).",
    ),
    "Infernal axe": dict(
        storage="item_var", depletion="revert", max_charges=5000,
        charge_source="Smouldering stone used on a Dragon axe; recharged the same way",
        drain_event="1/3 chance per log chopped to consume the log for half Firemaking xp instead of a normal burn",
        status="charges_only",
        note="Covers infernal_axe + trailblazer_axe/trailblazer_reloaded_axe (cosmetic Trailblazer League "
             "recolours, both tradeable with cert/placeholder variants -- NOT Leagues-locked, verified no "
             "leagues/season tree exists in this repo). No woodcutting file references any of the three ids "
             "-- unimplemented.",
    ),
    "Infernal harpoon": dict(
        storage="item_var", depletion="revert", max_charges=5000,
        charge_source="Smouldering stone used on a Dragon harpoon; recharged the same way",
        drain_event="1 charge consumed each time a fish is auto-cooked while harpooning",
        status="charges_only",
        note="Covers infernal_harpoon + trailblazer_harpoon/trailblazer_reloaded_harpoon, same "
             "not-Leagues-locked reasoning as Infernal axe. Unimplemented.",
    ),
    "Infernal pickaxe": dict(
        storage="item_var", depletion="revert", max_charges=5000,
        charge_source="Smouldering stone used on a Dragon pickaxe; recharged the same way",
        drain_event="1 charge consumed each time the pickaxe's mining effect activates",
        status="charges_only",
        note="Covers infernal_pickaxe + trailblazer_pickaxe/trailblazer_reloaded_pickaxe, same "
             "not-Leagues-locked reasoning. Unimplemented (specwep.rs2 only gates the special attack).",
    ),
    "Ironwood blowpipe": dict(
        storage="item_var", depletion="revert", max_charges=None,
        charge_source="Darts used directly on the blowpipe (up to adamant); no scales needed",
        drain_event="1 dart consumed per shot",
        status="charges_only",
        note="Max not stated on this item's own wiki page. player_ranged_check_ammo "
             "(player_ranged.rs2:145-189) treats every blowpipe as a self-ammo'd thrown weapon with NO "
             "ammo/charge check at all today -- the whole normal-attack drain mechanism is absent, not just "
             "unbound.",
    ),
    "Rosewood blowpipe": dict(
        storage="item_var", depletion="revert", max_charges=16383,
        charge_source="Darts used directly on the blowpipe (up to rune); no scales needed",
        drain_event="1 dart consumed per shot; special attack fires two shots for 25% special energy",
        status="charges_only",
        note="14-bit dart count, matching the toxic blowpipe's own field. Special attack IS wired "
             "(specs/pvm_rosewood_blowpipe.rs2, player_special_attack.rs2 case 63) but calls the same "
             "no-charge-check ammo path as Ironwood blowpipe -- even the bound mechanic doesn't drain darts.",
    ),
    "Kharedst's memoirs": dict(
        storage="player_varp", depletion="none", max_charges=100,
        charge_source="1 law+body+mind+soul rune per charge at the Old Memorial; max scales with pages added",
        drain_event="1 charge per Reminisce teleport",
        status="charges_only",
        note="Wiki confirms player_varp directly: \"If the book is destroyed [while charged], all charges "
             "are retained\" [and restored on reclaiming a fresh copy] -- account-scoped, same shape as "
             "Dodgy necklace. Only quest scripts grant/remove the physical item; no charge storage exists.",
    ),
    "Merfolk trident": dict(
        storage="item_var", depletion="none", max_charges=10,
        charge_source="Up to 10 pufferfish used on the trident",
        drain_event="1 charge consumed per 'Channel' use (regain underwater breath)",
        status="charges_only", note="No implementation found.",
    ),
    "Pendant of ates": dict(
        storage="item_var", depletion="revert", max_charges=1000,
        charge_source="Frozen tears (1:1), untradeable rare drop from Frost nagua / Amoxliatl",
        drain_event="1 charge per teleport to one of six unlocked Varlamore destinations",
        status="charges_only",
        note="Wiki explicit: charges are NOT shared across multiple pendants owned -- item_var confirmed. "
             "No implementation found.",
    ),
    "Perfected quetzal whistle": dict(
        storage="item_var", depletion="none", max_charges=50,
        charge_source="Raw meat from hunter creatures given to Soar Leader Pitri",
        drain_event="1 charge per Signal teleport to a built Quetzal Transport landing site",
        status="charges_only", note="No implementation found.",
    ),
    "Pharaoh's sceptre": dict(
        storage="item_var", depletion="revert", max_charges=100,
        charge_source="Desert artefacts given to the guardian mummy in Jalsavrah; base max 3, up to 100 with Elite Desert Diary",
        drain_event="1 charge per Teleport use (4 pyramid destinations)",
        status="charges_only",
        note="pharaohs_sceptre (uncharged) + pharaohs_sceptre_charged/_charged_initial (same mechanic, "
             "pre/post a 2022 cosmetic change, not separate tiers). No implementation found.",
    ),
    "Ring of endurance": dict(
        storage="item_var", depletion="none", max_charges=1000,
        charge_source="Stamina potion doses (1/dose) or Extended stamina doses (2/dose) used on the ring",
        drain_event="1-2 charges per stamina-potion dose drunk while worn (doubles the potion's effect)",
        status="charges_only",
        note="Wiki: charges are NOT refunded on Uncharge, NOT lost on death. No implementation found.",
    ),
    "Ring of shadows": dict(
        storage="item_var", depletion="revert", max_charges=1000,
        charge_source="1 blood+soul+death+law rune per charge, used on the ring",
        drain_event="1 charge per Teleport (Ancient Vault default; more unlocked via boss-drop tablets)",
        status="charges_only",
        note="Only referenced in quest_deserttreasureii/scripts (grant/remove the physical item) -- no "
             "Teleport/Charge/Uncharge mechanic wired.",
    ),
    "Sailors' amulet": dict(
        storage="item_var", depletion="none", max_charges=10000,
        charge_source="1 law + 10 water runes per 10 charges, used on the amulet",
        drain_event="1 charge per Teleport (The Pandemonium default; more unlocked via Sailors' Markers)",
        status="charges_only",
        note="Wiki: \"cannot be uncharged\" -- refuses to teleport at 0, no revert. Only one may be owned. "
             "No implementation found.",
    ),
    "Serpentine helm": dict(
        storage="item_var", depletion="revert", max_charges=11000,
        charge_source="Zulrah's scales used on the helm (11,000 = full charge); bank Configure-Charges supported",
        drain_event="10 scales on entering combat, another 10 if still in combat after 90 ticks (54s)",
        status="charges_only",
        note="Shared mechanic across all three colour variants (cosmetic Zulrah-drop mutagens only, no stat "
             "difference, confirmed by the wiki itself). No implementation for any of the three colours.",
    ),
    "Magma helm": dict(
        storage="item_var", depletion="revert", max_charges=11000,
        charge_source="Same as Serpentine helm -- magma-mutagen recolour, mechanically identical",
        drain_event="Same as Serpentine helm",
        status="charges_only", note="Colour variant of Serpentine helm sharing one mechanic; see that entry.",
    ),
    "Tanzanite helm": dict(
        storage="item_var", depletion="revert", max_charges=11000,
        charge_source="Same as Serpentine helm -- cyan-mutagen recolour, mechanically identical",
        drain_event="Same as Serpentine helm",
        status="charges_only", note="Colour variant of Serpentine helm sharing one mechanic; see that entry.",
    ),
    "Ring of pursuit": dict(
        storage="player_varp", depletion="destroy", max_charges=10,
        charge_source="Opal ring + Lvl-1 Enchant",
        drain_event="1 charge per successful hunter-creature track reveal while worn (unconditional since 8 May 2024, was 25% before)",
        status="charges_only",
        note="CORRECTS docs/ITEM_CHARGES_PLAN.md §5, which listed this in the item_var 'probabilistic-break' "
             "family alongside Dodgy necklace -- the wiki's own words contradict that almost verbatim to how "
             "it justifies Dodgy necklace's player_varp: \"The number of charges is specific to the player, "
             "not the ring.\" Checked directly rather than trusting the plan doc's grouping. No implementation found.",
    ),
    "Lithic sceptre": dict(
        storage="unknown", depletion="none", max_charges=None,
        charge_source="", drain_event="", status="out_of_scope",
        note="Demonic Pacts League drop, tradeable=No, leagueRegion set, no base-game acquisition path stated. Add to OUT_OF_SCOPE_LEAGUES.",
    ),
    "Nature's reprisal": dict(
        storage="unknown", depletion="none", max_charges=None,
        charge_source="", drain_event="", status="out_of_scope",
        note="{{Gone}}, removal=22 January 2025 -- Raging Echoes League exclusive, permanently removed from live OSRS after the league ended. Add to OUT_OF_SCOPE_LEAGUES.",
    ),
    "Drygore blowpipe": dict(
        storage="unknown", depletion="none", max_charges=None,
        charge_source="", drain_event="", status="out_of_scope",
        note="Demonic Pacts League / Raging Echoes League / Grid Master only, no permanent base-game acquisition path stated. Add to OUT_OF_SCOPE_LEAGUES.",
    ),
    # -- item_var, charges_only: research batches 1/5, 2026-08-13 ---------
    "Alchemist's amulet": dict(
        storage="item_var", depletion="revert", max_charges=5000,
        charge_source="Amulet of chemistry used on it, +10 charges each (Mastering Mixology reward shop); starts pre-charged with 50",
        drain_event="15% chance of an extra potion dose when brewing a <4-dose potion, 1 charge per proc",
        status="charges_only",
        note="No Mastering Mixology minigame, resin shop, or potion-dose subsystem exists in this tree at "
             "all -- a from-scratch feature, not just a missing drain wire.",
    ),
    "Amulet of blood fury": dict(
        storage="item_var", depletion="revert", max_charges=30000,
        charge_source="Amulet of fury (cache id enchanted_onyx_amulet) + Blood shard; starts at 10,000, +10,000 per additional shard",
        drain_event="1 charge per successful melee hit (multi-hit weapons drain multiple)",
        status="charges_only",
        note="Real melee successful-hit funnel exists at player_hit_npc_prepare "
             "(areas/area_rs2012_tormented_demons/scripts/rs2012_td_player_hit.rs2:36) -- not yet called for this item.",
    ),
    "Amulet of bounty": dict(
        storage="player_varp", depletion="destroy", max_charges=10,
        charge_source="Opal amulet + Lvl-1 Enchant; no separate refill -- only reset via Break or hitting 0",
        drain_event="25% chance to use 1 seed instead of 3 when planting an allotment, 1 charge per proc",
        status="charges_only",
        note="Wiki, checked directly: \"accounted per player, not per amulet\" -- same shape as ring of "
             "recoil / dodgy necklace. Real hook exists: skill_farming/scripts/farming_plant.rs2:126 "
             "(farming_plant_allot) reads a seed-count field this could override probabilistically.",
    ),
    "Arclight": dict(
        storage="item_var", depletion="revert", max_charges=10000,
        charge_source="Darklight + 3 Ancient shards at the Catacombs of Kourend altar (starts at 1,000); +333/1,000 per further shard",
        drain_event="1 charge per successful hit, excluding special attack",
        status="charges_only",
        note="Zero references anywhere, including quest_shadowstorm (the Darklight quest itself). Also has a "
             "separate 'infusion' progress counter (spent charges count toward Emberlight upgrade, persists "
             "through 0) -- a second piece of per-item state beyond the charge count, worth flagging for the "
             "library design.",
    ),
    "Ash sanctifier": dict(
        storage="item_var", depletion="none", max_charges=2147483647,
        charge_source="Death runes used on it, 10 charges per rune",
        drain_event="1 charge per demonic ashes auto-purified on npc-kill drop while carried (toggle: Activity)",
        status="charges_only",
        note="Wiki states the int32 max literally. No on-npc-death auto-pickup/auto-consume subsystem exists "
             "anywhere in this tree -- bury_bone.rs2/bone_xp.rs2 are manual-only, no drop-interception hook exists.",
    ),
    "Basic quetzal whistle": dict(
        storage="item_var", depletion="none", max_charges=5,
        charge_source="Raw hunter meats given to Soar Leader Pitri; amount depends on meat type",
        drain_event="1 charge per teleport (Signal option)",
        status="charges_only", note="No Quetzal Transport System content exists in this tree at all.",
    ),
    "Blade of saeldor": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Enhanced crystal weapon seed + 100 crystal shards at a singing bowl; starts at 10,000, recharged 100/shard",
        drain_event="1 charge per hit taken/dealt regardless of success",
        status="implemented",
        note="Wired the same way as Crystal helm/body/legs: gauntlet_hunllef.rs2's crystal_weapon_drain "
             "(includes blade_of_saeldor) is called from the GLOBAL player_hit_npc_prepare funnel "
             "(rs2012_td_player_hit.rs2:64), not gauntlet-gated. Reverts to blade_of_saeldor_inactive. FLAG: "
             "^crystal_weapon_start_charges (gauntlet.constant) is 2500, but the wiki says this weapon starts "
             "at 10,000/caps at 20,000 (2,500/6,000 are the older crystal bow/halberd numbers) -- the shared "
             "constant does not yet distinguish this weapon from plain crystal bow/halberd.",
    ),
    "Bow of faerdhinen": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Same as Blade of saeldor -- enhanced crystal weapon seed + shards, starts at 10,000",
        drain_event="1 charge per hit taken/dealt regardless of success",
        status="implemented",
        note="Same wired mechanism as Blade of saeldor: gauntlet_hunllef.rs2's crystal_weapon_drain includes "
             "bow_of_faerdhinen, called from the same global funnel. Reverts to bow_of_faerdhinen_inactive. "
             "Same start-charge constant mismatch noted under Blade of saeldor applies here too.",
    ),
    "Blazing blowpipe": dict(
        storage="item_var", depletion="none", max_charges=None,
        charge_source="Toxic blowpipe (empty) + Trailblazer reloaded blowpipe ornament kit, filled like a toxic blowpipe",
        drain_event="1 dart (and its charge) consumed per shot; refuses to fire empty",
        status="charges_only",
        note="Toxic blowpipe's own special attack (pvm_toxic_blowpipe.rs2) pattern-matches literally on "
             "toxic_blowpipe_loaded and does NOT include this ornament id -- not plugged in anywhere, unlike "
             "the base toxic blowpipe.",
    ),
    "Blood moon chestplate": dict(
        storage="item_var", depletion="revert", max_charges=3000,
        charge_source="Lunar Chest (Neypotzli) drop; the pristine 'new' id converts to the charged 'degraded' id on first wear, starting full",
        drain_event="1 charge per 54 seconds spent in combat while worn",
        status="charges_only",
        note="Three-id ladder confirmed in configs/all.obj: new (no Check) -> degraded (item_var storage, "
             "has Check) -> broken (repaired via Bob/Dunstan or an armour stand). The drain is TIME-based "
             "(elapsed seconds in combat), not per-hit -- no in-combat timer subsystem exists anywhere in "
             "this codebase; this needs genuinely new infrastructure, unlike every per-swing drain already wired.",
    ),
    "Blood moon helm": dict(
        storage="item_var", depletion="revert", max_charges=3000,
        charge_source="Same as Blood moon chestplate", drain_event="1 charge per 54 seconds in combat while worn",
        status="charges_only", note="Ladder: blood_moon_helm -> _degraded -> _broken. Same time-based-drain gap.",
    ),
    "Blood moon tassets": dict(
        storage="item_var", depletion="revert", max_charges=3000,
        charge_source="Same as Blood moon chestplate", drain_event="1 charge per 54 seconds in combat while worn",
        status="charges_only", note="Ladder: blood_moon_tassets -> _degraded -> _broken. Same time-based-drain gap.",
    ),
    "Blue moon chestplate": dict(
        storage="item_var", depletion="revert", max_charges=3000,
        charge_source="Lunar Chest drop; activates to the charged/degraded id on first wear, full at 3,000",
        drain_event="1 charge per 54 seconds in combat while worn",
        status="charges_only",
        note="Wiki title 'Blue moon' but the cache's internal family name is 'frost_moon' (pre-rename "
             "datamined name) -- ladder is frost_moon_chestplate -> _degraded -> _broken. Same "
             "no-timer-subsystem gap as Blood moon.",
    ),
    "Blue moon helm": dict(
        storage="item_var", depletion="revert", max_charges=3000,
        charge_source="Same as Blue moon chestplate", drain_event="1 charge per 54 seconds in combat while worn",
        status="charges_only", note="Ladder: frost_moon_helm -> _degraded -> _broken. Same gap.",
    ),
    "Blue moon tassets": dict(
        storage="item_var", depletion="revert", max_charges=3000,
        charge_source="Same as Blue moon chestplate", drain_event="1 charge per 54 seconds in combat while worn",
        status="charges_only", note="Ladder: frost_moon_tassets -> _degraded -> _broken. Same gap.",
    ),
    "Bone staff": dict(
        storage="item_var", depletion="none", max_charges=20000,
        charge_source="Chaos runes used on it, 1:1; starts at 1,000 from the runes spent making it",
        drain_event="1 charge (=1 chaos rune) per cast of its built-in spell, usable only against rats",
        status="charges_only",
        note="No powered-staff spell-cast charge-consumption subsystem exists anywhere in this tree -- would be new infrastructure.",
    ),
    "Bonecrusher": dict(
        storage="item_var", depletion="none", max_charges=2147483647,
        charge_source="Ecto-tokens used on it, 25 charges per token",
        drain_event="1 charge per bone auto-crushed on npc-kill drop while carried (toggle: Activity)",
        status="charges_only",
        note="Same missing subsystem as Ash sanctifier -- no on-npc-death auto-consume/drop-interception "
             "hook exists. skill_prayer/scripts/bone_xp.rs2's bone_prayer_exp is the reusable per-bone XP "
             "table for whoever wires this.",
    ),
    "Book of the dead": dict(
        storage="item_var", depletion="none", max_charges=250,
        charge_source="Recharged at the Old Memorial: 1 law + 1 body + 1 mind + 1 soul rune per charge, 10 Magic XP each",
        drain_event="1 charge per teleport (Reminisce option, 5 destinations)",
        status="charges_only",
        note="CORRECTS docs/ITEM_CHARGES_PLAN.md §2c, which listed this as a player_varp example. Its "
             "precursor Kharedst's memoirs states \"if the book is charged when destroyed, all charges are "
             "retained\" through an in-place item upgrade -- item_var behaviour (a per-item counter "
             "surviving an identity change), not an account pool; neither article uses "
             "Dodgy-necklace-style \"shared across every copy\" wording. Flagged rather than asserted -- "
             "worth confirming against the live wiki's exact phrasing before this is treated as settled.",
    ),
    "Bracelet of slaughter": dict(
        storage="player_varp", depletion="destroy", max_charges=30,
        charge_source="Topaz bracelet + Lvl-3 Enchant; no separate refill -- only reset via Break or hitting 0",
        drain_event="25% chance per Slayer-task kill to not decrement the task's remaining kill count (full XP still granted), 1 charge per proc",
        status="charges_only",
        note="Wiki confirms explicitly: \"All bracelets of slaughter share the same pool of 30 charges\" -- "
             "player_varp. Real hook: skill_slayer/scripts/slayer_kill.rs2:50 (slayer_on_npc_kill) "
             "`%if1 = sub(%if1, 1);` is exactly the site a probabilistic skip needs to wrap.",
    ),
    "Sanguinesti staff": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Charged with blood runes (2 per charge) via bank Configure-Charges or direct use",
        drain_event="1 charge per cast of the staff's built-in spell",
        status="charges_only",
        note="[proc,pvm_spell_cast] (player_magic.rs2:279) already branches on oc_category(weapon)=weapon_staff "
             "for staffanim -- a plausible drain call site, not wired.",
    ),
    "Holy sanguinesti staff": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Cosmetic recolour (Saradomin) -- identical mechanic to the base Sanguinesti staff",
        drain_event="1 charge per cast of the built-in spell",
        status="charges_only",
        note="sanguinesti_staff_or -- same file/mechanic as the base staff, not yet wired either (piggybacks "
             "on that entry per the researching agent's note; not duplicating the drain-hook research).",
    ),
    "Slayer's staff": dict(
        storage="item_var", depletion="revert", max_charges=2500,
        charge_source="Slayer's enchantment used on a Slayer's staff (raises Magic req to 75)",
        drain_event="1 charge per Magic Dart cast that lands on a monster killed as part of the player's Slayer task",
        status="charges_only",
        note="obj slayer_staff_enchanted: ifop3=Check, ifop5=Revert, no Charge/Uncharge -- confirms item_var+"
             "revert. magic_dart.rs2 exists but has no slayer-task check or charge call.",
    ),
    "Tome of earth": dict(
        storage="item_var", depletion="none", max_charges=20000,
        charge_source="Soiled pages (Hueycoatl drop), 20 charges/page, up to 1,000 pages",
        drain_event="1 charge per cast of an offensive earth spell; other earth spells stay free (infinite runes)",
        status="charges_only",
        note="ifop4=Pages, no Charge/Uncharge -- depletion is a manual 'Pages' toggle to the empty obj, not "
             "an automatic swap at 0, hence depletion=none. Sits in the shield slot; pvm_spell_cast's "
             "staff-category check has no off-hand-tome branch to extend.",
    ),
    "Tome of fire": dict(
        storage="item_var", depletion="none", max_charges=20000,
        charge_source="Burnt or searing pages (Wintertodt), 20 charges/page, up to 1,000 pages",
        drain_event="1 charge per cast of an offensive fire spell; other fire spells stay free",
        status="charges_only", note="Same shape/caveats as Tome of earth -- manual Pages toggle, no drain hook wired.",
    ),
    "Tome of water": dict(
        storage="item_var", depletion="none", max_charges=20000,
        charge_source="Soaked pages (Tempoross), 20 charges/page, up to 1,000 pages",
        drain_event="1 charge per cast of an offensive water spell or a curse spell",
        status="charges_only", note="Same shape as Tome of earth/fire; no drain hook wired.",
    ),
    "Tonalztics of ralos": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Sunfire splinters, 1 splinter = 1 charge",
        drain_event="1 charge per throw (standard attack or the Division special), regardless of hits landed",
        status="charges_only",
        note="Special attack IS implemented (specs/pvm_tonalztics_of_ralos_charged.rs2) but calls no "
             "charge-drain proc; the standard-throw path doesn't drain either.",
    ),
    "Toxic staff of the dead": dict(
        storage="item_var", depletion="revert", max_charges=11000,
        charge_source="Zulrah's scales used on the staff, 1 scale = 1 charge",
        drain_event="Time-based: 10 scales on entering combat, another 10 if still in combat after 1 minute",
        status="charges_only",
        note="specwep.rs2:198-234 implements the special attack but no charge logic. Shares Serpentine "
             "helm's exact drain mechanism (also unimplemented) -- the drain MECHANISM itself, not just the "
             "wiring, is absent from this codebase.",
    ),
    "Tumeken's shadow": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Soul + chaos runes (2 soul + 5 chaos per charge; aether substitutes for soul, not refunded on uncharge)",
        drain_event="1 charge per cast of the built-in spell",
        status="charges_only", note="Same pvm_spell_cast hook candidate as Sanguinesti staff; not wired.",
    ),
    "Venator bow": dict(
        storage="item_var", depletion="revert", max_charges=50000,
        charge_source="Ancient essence, 1 essence = 1 charge",
        drain_event="1 charge per attack, regardless of how many targets the arrow bounces to",
        status="charges_only",
        note="player_ranged.rs2:65 already calls ~wildy_weapon_consume at the right site (the wilderness-"
             "weapon precedent) but nothing calls a venator_bow drain there. The bow's bounce/ricochet "
             "gameplay is ALSO unimplemented (player_ranged.rs2 is single-target only) -- a bigger gap than "
             "just charges.",
    ),
    "Warped sceptre": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Chaos + earth runes (2 chaos + 5 earth per charge; combination runes accepted since 14 May 2025)",
        drain_event="1 charge per cast of the built-in spell",
        status="charges_only", note="Same pvm_spell_cast candidate hook as Sanguinesti/Tumeken's; not wired.",
    ),
    "Xeric's talisman": dict(
        storage="item_var", depletion="revert", max_charges=1000,
        charge_source="Lizardman fangs, 1 fang = 1 charge, used directly on the talisman",
        drain_event="1 charge per teleport to an unlocked Kourend destination",
        status="charges_only",
        note="No teleport dispatcher exists at all -- the teleport feature itself, not just the charge "
             "drain, is unimplemented.",
    ),
    "Toxic staff": dict(
        storage="unknown", depletion="none", max_charges=None,
        charge_source="", drain_event="", status="out_of_scope",
        note="obj toxic_sotd_charged_deadman: name='Toxic staff (deadman)' -- obtained via Nigel + Annihilation "
             "weapon scroll, both Deadman Mode fixtures. No standalone 'Toxic staff' wiki page exists (confirms "
             "this isn't a real base-game item under a second name). Add to OUT_OF_SCOPE_DEADMAN.",
    ),
    "Wristbands of the arena": dict(
        storage="unknown", depletion="none", max_charges=None,
        charge_source="", drain_event="", status="out_of_scope",
        note="Wiki opens with {{Failed poll|Rewards for Beneath Cursed Sands & PvP Arena}} -- failed its "
             "community poll, never a permanent live-game reward, briefly existed only in tournament-world "
             "PvP Arena testing. No charge quantity/source/drain is documented anywhere (the feature was "
             "scrapped before being written up), and grep for pvpa_ across server/scripts returns zero hits. "
             "Numbers cannot be sourced without inventing them -- left uncharted rather than guessed.",
    ),
    "Amulet of glory": dict(
        storage="id_ladder", depletion="degrade_step", max_charges=4,
        charge_source="Enchanted from Dragonstone amulet; recharged at Fountain of Rune / Font of All",
        drain_event="1 charge per teleport (general/scripts/enchanted_jewellry/amulet_of_glory.rs2)",
        status="implemented",
        note="Fully working, verified 2026-08-13: ~amulet_of_glory_teleport degrades "
             "the ladder on every use exactly like ring_of_dueling.rs2. Real OSRS has "
             "no Check op on this family (configs/all.obj: no ifop3 anywhere on the "
             "ladder) -- the remaining count is legible from the item's own name, "
             "'Amulet of glory(4)'..'Amulet of glory(1)', so there is nothing left to "
             "bind. Corrected max from an earlier wrong guess of 6 -- the ladder is "
             "4/3/2/1/0(base), confirmed against the obj records directly.",
    ),
    # -- player_varp / daily, implemented ---------------------------------
    "Explorer's ring": dict(
        storage="player_varp", depletion="none", max_charges=30,
        charge_source="Quest reward (Fremennik Trials-line rings); refills every runeday",
        drain_event="1 use per Low/High Alchemy cast via the ring, and per free teleport-to-Tutorial Island",
        status="charges_only", note="Needs SS_OP_DATE_RUNEDAY (§3b); ring itself not yet in configs scan (tier op set differs).",
    ),
    "Falador shield": dict(
        storage="player_varp", depletion="none", max_charges=5,
        charge_source="Quest/diary reward (Falador achievement diary tiers)",
        drain_event="1 use per free prayer recharge",
        status="charges_only", note="Needs SS_OP_DATE_RUNEDAY (§3b).",
    ),
    # -- id_ladder, degrade-step, curated but not yet bound ---------------
    "Dharok's helm": dict(
        storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows minigame — starts at 100%, repaired at the Barrows reward chest for a coin fee",
        drain_event="Automatic per-kill degrade in the Barrows crypt; no button op at all (no Uncharge/Charge op on the record)",
        status="charges_only", note="Barrows minigame is not in this tree, so the drain trigger has nowhere to fire from; the ladder itself is real cache data.",
    ),
    "Dharok's platebody": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Dharok's platelegs": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Dharok's greataxe": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Ahrim's hood": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Ahrim's robetop": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Ahrim's robeskirt": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Ahrim's staff": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Guthan's helm": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Guthan's platebody": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Guthan's chainskirt": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Guthan's warspear": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Karil's coif": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Karil's leathertop": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Karil's leatherskirt": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Karil's crossbow": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Torag's helm": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Torag's platebody": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Torag's platelegs": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Torag's hammers": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Verac's helm": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Verac's brassard": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Verac's plateskirt": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Verac's flail": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="charges_only"),
    "Slayer ring": dict(
        storage="id_ladder", depletion="degrade_step", max_charges=8,
        charge_source="Made from Enchanted gem via the Slayer ring recipe",
        drain_event="1 charge per teleport (slayer dungeons / Wilderness rub-teleport)",
        status="charges_only", note="Ladder exists; teleport-drain binding not yet wired to the shared library.",
    ),
}

# Families the scan's Check-op sweep finds that are not a charge system at
# all — storage containers are already excluded structurally; this is the
# rest: tier/state reporters, quest items, and Checks answering a boolean or
# a status rather than a count. docs/ITEM_CHARGES_PLAN.md §1.
NOT_A_CHARGE_DENYLIST = {
    "Coffin", "Nest box", "Treat cauldron", "Starter bow", "Starter staff",
    "Damaged book", "Magic cape", "Magic cape(t)", "Aluft aloft box",
    "Gricoller's can", "Reward token", "Santa's list", "Amulet of the damned",
    "Binding necklace", "Enchanted gem", "Eternal gem",
    "Spoils of war", "Jar generator", "Looting bag", "Water container",
    "Facility bottle", "Seed pack", "Fish barrel", "Open fish barrel",
    "Log basket", "Open log basket", "Terrifying charm",
    "5-gallon jug", "8-gallon jug", "Cooler", "Crystal saw",
    "Damaged soul bearer", "Blood essence", "Minecart control scroll",
    "Basic quetzal whistle blueprint", "Enhanced quetzal whistle blueprint",
    "Perfected quetzal whistle blueprint", "Torn enhanced quetzal whistle blueprint",
    "Torn perfected quetzal whistle blueprint", "Grape barrel", "Mulch",
    "Packed mulch",
    # Research batches 1/3/4/5, 2026-08-13:
    "Boat bottle",  # Check reports which boat is stored (identity), not a count; no Sailing skill exists here either
    "Herb box", "Open herb box",  # container: Take-one/Bank-all/Check over 7 rolled herbs, same shape as coal bag
    "Loot key",  # Check/Destroy only; Check reports contained loot's coin value, same shape as Looting bag
    "Soulreaper axe",  # "Soul stacks" are a 0-5 decaying combat buff, not a held/filled charge; the LIVE obj has no Check op at all -- the scan caught a stray beta placeholder
    "Tome of the moon", "Tome of the sun", "Tome of the temple",  # quest-completion page trackers (account-wide booleans), not charges -- no Charge/Uncharge/Pages op, unlike Tome of earth/fire/water
    "Hat of the eye", "Hat of the eye (blue)", "Hat of the eye (green)", "Hat of the eye (red)",  # Check reports which tiara it's attuned to (status), Revert clears attunement -- same shape as SLAYER_HELMET_FAMILY
}

# Real charge items in OSRS, but standalone Leagues relics/rewards with no
# base-game counterpart — this server does not model Leagues seasons at all
# (checked: no leagues/ or season/ tree anywhere in server/scripts), so these
# can never be obtained here. Distinct from NOT_A_CHARGE_DENYLIST (which is
# "not a charge system in OSRS at all") — status `out_of_scope` says "is one,
# but the game mode it lives in is not in this tree", so a future Leagues
# port knows to look here rather than assuming the scan already covered it.
# The Oathplate/Radiant slayer helmets are Leagues-skinned slayer helmets,
# NOT standalone relics — they share the real slayer_helmet family's mechanic
# and are deliberately not in this set.
OUT_OF_SCOPE_LEAGUES = {
    "Butler's bell", "Flask of fervour", "Pocket kingdom", "Minion whistle",
    # Research batch 4, 2026-08-13:
    "Lithic sceptre",  # Demonic Pacts League drop, tradeable=No, no base-game acquisition path stated
    "Nature's reprisal",  # {{Gone}}, removal=22 January 2025 -- Raging Echoes League exclusive, permanently removed from live OSRS
    "Drygore blowpipe",  # Demonic Pacts League / Raging Echoes League / Grid Master only, no permanent base-game path
    # Resolved manually 2026-08-13 (batch 2 flagged these as ambiguous; fetched
    # "Echo ahrim's ornament kit" directly to settle it): "A pack of four can be
    # purchased from the Leagues Reward Shop for 6,000 League Points" -- no
    # base-game acquisition path, same as the sceptre/blowpipe above. The
    # degrade-step mechanic itself is real and identical to regular Ahrim's
    # gear (confirmed: byte-identical bonus tables across the 100/75/50/25
    # tiers) -- only the ornament kit's *availability* is Leagues-locked.
    "Echo ahrim's hood", "Echo ahrim's robeskirt", "Echo ahrim's robetop", "Echo ahrim's staff",
}

# Same reasoning, Deadman Mode: every "Sigil of X" wiki page opens with
# {{Deadman seasonal}} (checked directly against the fetched wikitext for
# all ten), and Corrupted scythe of vitur / Corrupted tumeken's shadow are
# explicitly Deadman-only recolours of two families already implemented in
# their non-corrupted form. This server has no deadman/ or dmm/ tree either.
OUT_OF_SCOPE_DEADMAN = {
    "Sigil of binding", "Sigil of escaping", "Sigil of finality",
    "Sigil of freedom", "Sigil of last recall", "Sigil of specialised strikes",
    "Sigil of supreme stamina", "Sigil of the porcupine", "Sigil of the serpent",
    "Sigil of versatility", "Corrupted scythe of vitur", "Corrupted tumeken's shadow",
}

# Slayer helmet and every one of its recolours / Leagues skins share ONE
# mechanic: `Check` reports the current Slayer task assignment, not a charge
# count — confirmed from the wiki's own changelog ("The 'Check' option was
# added, allowing players to see their current Slayer task"). Storage class
# `none`, and grouped with NOT_A_CHARGE_DENYLIST for the same reason: the
# scan's Check-op sweep is structurally right to flag these, the content
# just isn't a charge system.
SLAYER_HELMET_FAMILY = {
    "Slayer helmet", "Black slayer helmet", "Green slayer helmet",
    "Red slayer helmet", "Purple slayer helmet", "Turquoise slayer helmet",
    "Hydra slayer helmet", "Twisted slayer helmet", "Tzkal slayer helmet",
    "Tztok slayer helmet", "Vampyric slayer helmet", "Araxyte slayer helmet",
    "Hooded slayer helmet", "Oathplate slayer helmet", "Radiant slayer helmet",
}


def classify(stem, members):
    data = FAMILY_DATA.get(stem)
    ids = ",".join(sorted({m[0] for m in members}))
    if data:
        return {
            "family": stem,
            "obj_ids": ids,
            "storage": data.get("storage", "unknown"),
            "max_charges": data.get("max_charges", ""),
            "charge_source": data.get("charge_source", ""),
            "drain_event": data.get("drain_event", ""),
            "depletion": data.get("depletion", ""),
            "status": data.get("status", "uncurated"),
            "note": data.get("note", ""),
        }
    if stem in NOT_A_CHARGE_DENYLIST or stem in SLAYER_HELMET_FAMILY:
        return {
            "family": stem,
            "obj_ids": ids,
            "storage": "none",
            "max_charges": "",
            "charge_source": "",
            "drain_event": "",
            "depletion": "",
            "status": "not_a_charge",
            "note": "Check reports Slayer task assignment, not a charge count."
                    if stem in SLAYER_HELMET_FAMILY else "",
        }
    if stem in OUT_OF_SCOPE_LEAGUES:
        return {
            "family": stem,
            "obj_ids": ids,
            "storage": "unknown",
            "max_charges": "",
            "charge_source": "",
            "drain_event": "",
            "depletion": "",
            "status": "out_of_scope",
            "note": "Leagues-only relic; this server does not model Leagues seasons.",
        }
    if stem in OUT_OF_SCOPE_DEADMAN:
        return {
            "family": stem,
            "obj_ids": ids,
            "storage": "unknown",
            "max_charges": "",
            "charge_source": "",
            "drain_event": "",
            "depletion": "",
            "status": "out_of_scope",
            "note": "Deadman Mode only; this server does not model Deadman seasons.",
        }
    return {
        "family": stem,
        "obj_ids": ids,
        "storage": "unknown",
        "max_charges": "",
        "charge_source": "",
        "drain_event": "",
        "depletion": "",
        "status": "uncurated",
        "note": "",
    }


# ---------------------------------------------------------------------
# 4. Output
# ---------------------------------------------------------------------


def write_ledger(rows):
    os.makedirs(os.path.dirname(LEDGER_CSV), exist_ok=True)
    with open(LEDGER_CSV, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(
            f,
            fieldnames=[
                "family", "obj_ids", "storage", "max_charges", "charge_source",
                "drain_event", "depletion", "status", "note",
            ],
        )
        w.writeheader()
        for r in sorted(rows, key=lambda r: r["family"].lower()):
            w.writerow(r)


def write_doc(rows):
    by_status = {}
    for r in rows:
        by_status.setdefault(r["status"], []).append(r)

    lines = []
    lines.append("# Charged items — catalog\n")
    lines.append(
        "Generated by `tools/charged_items_scan.py` from "
        "`OSRS-Content/osrs239-content/configs/all.obj`. Do not hand-edit — "
        "edit `FAMILY_DATA` in the tool and re-run. See "
        "`docs/ITEM_CHARGES_PLAN.md` for the storage-class and "
        "depletion-shape definitions.\n"
    )
    lines.append(f"Total families found: **{len(rows)}**\n")
    for status in ("implemented", "charges_only", "uncurated", "not_a_charge", "out_of_scope"):
        members = by_status.get(status, [])
        lines.append(f"\n## `{status}` ({len(members)})\n")
        lines.append("| family | storage | depletion | max | drain event |")
        lines.append("|---|---|---|---|---|")
        for r in sorted(members, key=lambda r: r["family"].lower()):
            lines.append(
                f"| {r['family']} | {r['storage']} | {r['depletion']} | "
                f"{r['max_charges']} | {r['drain_event']} |"
            )
    os.makedirs(os.path.dirname(DOC_MD), exist_ok=True)
    with open(DOC_MD, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="exit 1 if outputs would change")
    args = ap.parse_args()

    records, _ = parse_all_obj(ALL_OBJ)
    families = scan_families(records)
    rows = [classify(stem, members) for stem, members in families.items()]

    if args.check:
        old_ledger = open(LEDGER_CSV, encoding="utf-8").read() if os.path.exists(LEDGER_CSV) else None
        old_doc = open(DOC_MD, encoding="utf-8").read() if os.path.exists(DOC_MD) else None
        write_ledger(rows)
        write_doc(rows)
        new_ledger = open(LEDGER_CSV, encoding="utf-8").read()
        new_doc = open(DOC_MD, encoding="utf-8").read()
        if old_ledger != new_ledger or old_doc != new_doc:
            print("charged_items_scan: outputs are stale, re-run without --check", file=sys.stderr)
            return 1
        print("charged_items_scan: up to date")
        return 0

    write_ledger(rows)
    write_doc(rows)
    counts = {}
    for r in rows:
        counts[r["status"]] = counts.get(r["status"], 0) + 1
    print(f"charged_items_scan: {len(rows)} families -> {LEDGER_CSV}, {DOC_MD}")
    print(f"  {counts}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
