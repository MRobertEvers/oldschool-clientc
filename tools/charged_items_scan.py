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
    "Black mask": dict(
        storage="id_ladder", depletion="degrade_step", max_charges=10,
        charge_source="Dropped by Cave horrors at (10)",
        drain_event="1 charge per kill made using the slayer-task-boosted effect",
        status="charges_only", note="Ladder exists; slayer-task-boost drain not wired.",
    ),
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
