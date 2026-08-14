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
        storage="item_var", depletion="none", max_charges=16383,
        charge_source="Filled with darts + Zulrah's scales (1 scale per dart, up to 16,383 of each)",
        drain_event="1 dart per shot; 1 scale per shot at a 2/3 chance (1/3 chance to skip)",
        status="implemented",
        note="CORRECTED: this status was wrong -- checked directly (grepped player_ranged.rs2, zero hits) "
             "rather than trusting the prior claim, and confirmed by pvm_toxic_blowpipe.rs2's own header, "
             "which already stated the gap: no blowpipe-specific ammo model existed anywhere, the weapon "
             "silently fell through the generic 'thrown: rhand is the ammo' branch and never ran out. Built "
             "a full shared dual-resource library, gear/blowpipe_ammo.rs2, covering all 5 blowpipes "
             "(Toxic/Blazing/Camphor/Ironwood/Rosewood) at once: two independent item_var counters (dart "
             "count keyed by whichever dart obj is loaded; scale count keyed by the scale obj itself), Fill "
             "(opheldu, dispatched on last_useitem), Check, Uncharge/Unload, and real per-shot consumption "
             "wired into player_ranged.rs2's check_ammo/use_weapon plus both blowpipe specials (which "
             "already checked ammo existed but never spent any). Found and fixed a real bug along the way: "
             "this language has no statement-level discard, and a bare ~charges_item_drain(...) call leaks "
             "its return value on the VM's int stack -- caught by a 200-shot test loop overflowing the "
             "256-entry stack, then found and fixed the same pattern in 5 other files (celestial_ring.rs2, "
             "celestial_signet.rs2 x2, echo_boots.rs2, god_iban.rs2 -- the last one pre-existing from an "
             "earlier session, not introduced by this pass). NOT implemented: the loaded dart's own Ranged "
             "Strength bonus (it isn't in the quiver, so the existing equipment-bonus summing never sees "
             "it), Unload-vs-Uncharge as distinct ops, and the empty-pipe Dismantle-for-scales option.",
    ),
    "Abyssal tentacle": dict(
        storage="item_var", depletion="revert", max_charges=10000,
        charge_source="Combine a Kraken tentacle with an Abyssal whip (opheldu, either use-order)",
        drain_event="1 charge per attack, regardless of whether it lands (not 'per successful hit')",
        status="implemented",
        note="gear/abyssal_tentacle.rs2/.constant. This ledger's own prior note was wrong on two points, "
             "both corrected against the live wiki directly: charges do NOT come from an 'Abyssal orphan' "
             "pet (no such mechanic exists) but from combining a Kraken tentacle + Abyssal whip, both of "
             "which already exist and are already used elsewhere in this tree (abyssal_whip has its own "
             "special attack in specs/pvm_abyssal_whip.rs2) -- unlike Saradomin's blessed sword, which "
             "punted on its own creation recipe for lack of anything to model it on, this one is "
             "implemented; and the drain is per-attack-attempt, not per-landed-hit, matching Saradomin's "
             "blessed sword's own '10,000 hits' (not 'successful hits') shape rather than Blood fury's. "
             "Same drain hook (player_hit_npc_prepare) and self-gating shape as Saradomin's blessed sword. "
             "Dissolve (ifop5) is a separate, one-way, no-refund destroy, not the same as the automatic "
             "0-charge revert to Kraken tentacle. NOT implemented: combining the charges of two existing "
             "tentacles up to a 20,000 cap -- the wiki's own secondary, rarer path, not the main recipe.",
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
        drain_event="1 charge per swing (regardless of hit success)",
        status="implemented",
        note="Wired into player_hit_npc_prepare (skill_combat/scripts/player/gear/saradomins_blessed_sword.rs2), "
             "the same global funnel blood_fury.rs2 uses. Two cache obj ids for one charge pool ('Sara's "
             "blessed sword (full)' at max, 'Saradomin's blessed sword' the instant any charge is spent, not "
             "two separate tiers) -- swapping between them preserves the count via a read-swap-restore, since "
             "charges_item_revert deliberately does NOT carry vars across an obj_id change. Manual Revert and "
             "auto-degrade-at-zero both go to Saradomin's tear. NOT implemented: creating the sword at all "
             "(combining Saradomin sword + Saradomin's tear) -- zero combine logic exists anywhere in this "
             "tree, the same gap blood_fury.rs2 states for its own family.",
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
        status="implemented",
        note="Cosmetic ornament-kit recolour of Venator bow, sharing its exact mechanic -- both wired "
             "together in skill_combat/scripts/player/gear/venator_bow.rs2, the same shape "
             "blade_of_saeldor/bow_of_faerdhinen share crystal_weapon_drain. See Venator bow's own note "
             "for the bounce-passive gap this does not depend on.",
    ),
    "Eclipse moon chestplate": dict(
        storage="item_var", depletion="degrade_step", max_charges=3000,
        charge_source="Full charge on drop from the Lunar Chest (Neypotzli); repaired by Bob/Dunstan or self-repaired at an armour stand",
        drain_event="1 charge lost per 54 seconds spent in combat while worn (time-based, not per-hit)",
        status="implemented",
        note="CORRECTED: 'no time-based combat-degrade ticker exists' is no longer true after this session's "
             "Barrows work built exactly that (skill_combat/scripts/player/gear/barrows_degrade.rs2) and "
             "confirmed the mechanism (settimer/cleartimer, 90-tick repeating, self-stopping when combat "
             "activity stops). moon_degrade.rs2 reuses the identical shape for all 3 moon sets x 3 pieces "
             "at once: a fresh(no Check)->degraded(has Check) free/instant transition on first combat entry, "
             "then a flat 3000-point item_var countdown (1 per 90 ticks) to broken -- simpler than Barrows' "
             "4-quarter ladder since moon armour has no intermediate named tiers, just the two states the "
             "cache's own ifop3 presence already distinguishes. Wired into the same two funnels "
             "(player_hit_npc_prepare, combat_stats.rs2's playerhit_n_melee) barrows_degrade_enter uses. NOT "
             "implemented: repairing (Bob/Dunstan/armour stand), same scope note as Barrows.",
    ),
    "Eclipse moon helm": dict(
        storage="item_var", depletion="degrade_step", max_charges=3000,
        charge_source="Same as Eclipse moon chestplate", drain_event="1 charge per 54 seconds in combat while worn",
        status="implemented", note="Same shared library as Eclipse moon chestplate; see its note.",
    ),
    "Eclipse moon tassets": dict(
        storage="item_var", depletion="degrade_step", max_charges=3000,
        charge_source="Same as Eclipse moon chestplate", drain_event="1 charge per 54 seconds in combat while worn",
        status="implemented", note="Same shared library as Eclipse moon chestplate; see its note.",
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
        status="implemented",
        note="player_varp, same shape as bracelet_of_slaughter/dodgy_necklace. Wired into "
             "skill_slayer/scripts/slayer_kill.rs2's slayer_on_npc_kill, doubling the real %if1 task-count "
             "decrement instead of a synthetic hook. Crumbles at 0 charges; can never be worn alongside "
             "bracelet of slaughter (same ^wearpos_hands slot). NOT implemented: elite Combat Achievements' "
             "10-percent regen-instead-of-crumble reward -- no Combat Achievements system exists in this tree.",
    ),
    "Eye of ayak": dict(
        storage="item_var", depletion="revert", max_charges=50000,
        charge_source="2 death + 1 chaos rune per charge, or 1 demon tear per charge (cannot mix types)",
        drain_event="1 charge per cast of the built-in spell",
        status="implemented",
        note="gear/eye_of_ayak.rs2/.constant, second weapon on the shared powered-staff dispatch (gear/"
             "powered_staff.rs2) after Sanguinesti staff -- own max-hit formula (floor(Magic/3) - 6, "
             "different from Sanguinesti staff's, checked directly not assumed identical). Charging is a "
             "genuine ifop3=Charge menu option on the uncharged form only (no Charge op on the charged "
             "form at all in this cache's own record -- topping up a partially-drained Eye of ayak is not "
             "possible here, only a fresh charge from empty). NOT implemented: the wiki's 'cannot mix "
             "rune-based and demon tear charges' exclusivity -- both materials add to the same pool here. "
             "Special attack (Soul Rend, specs/pvm_eye_of_ayak.rs2) is untouched by this, spec-energy gated "
             "as before. Found and fixed a real bug while adding this: an early draft declared `$use` in "
             "two different branches of the same charge proc -- ServerScript's def_int is proc-scoped, not "
             "block-scoped, so the second declaration silently read back 0 instead of erroring, and the "
             "rune-charging path refused to charge anything despite affordable materials. Caught by "
             "bisecting with encoded return values (mock230's headless harness doesn't surface mes() output "
             "outside the chargesrun OK/FAIL convention) before landing, not shipped.",
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
        storage="item_var", depletion="none", max_charges=16000,
        charge_source="1 unnoted big bones + 1 law rune per charge (obj's own ifop3=Charge, a menu action "
                       "that consumes materials already in the inventory -- not opheldu)",
        drain_event="1 charge per teleport to Bryophyta's or Obor's lair, chosen from a Rub submenu",
        status="implemented",
        note="general/scripts/enchanted_jewellry/giantsoul_amulet.rs2/.constant. The prior pass's blocker "
             "-- no SS_OP exposing the wire's already-decoded Rub-submenu click index "
             "(player->last_subop) to content -- is now closed: added SS_OP_LAST_SUBOP as a genuinely new, "
             "non-reference-backed opcode via gen_opcode_meta.py's existing EXTRA_OPCODES mechanism (the "
             "same sanctioned extension point IF_SETEVENTS/IF_OPENTOP/the map-instance band etc. already "
             "use for rev-230/239 surface LostCity's reference never had), regenerated ss_opcode.h/"
             "ss_meta.gen.h properly rather than hand-edited, and added the read-side case in "
             "mock230_scripts.c. Confirmed end to end: sscompile resolves a bare `last_subop` reference in "
             ".rs2 content, and mutation-testing the cap-clamp/out-of-range guards goes red correctly. "
             "depletion=none, not revert: the obj record has no Uncharge option and nothing forces a swap "
             "at 0 (matches this ledger's own 'no refund on Uncharge' phrasing -- corrected from the prior "
             "pass's revert guess). NOT implemented: the third destination, Royal Titans (Branda and "
             "Eldric) -- that boss has no spawned content anywhere in this tree (only a generated "
             "animation-catalog entry), unlike Bryophyta/Obor, which both have real arena coordinates via "
             "their own minigame_bryophyta/minigame_obor .constant files. Selecting it is guarded to a "
             "clean 'not open yet' message and drains no charge, mutation-tested. The teleport SUCCESS "
             "path itself is untested for the same unsafe-in-::chargesrun reason Chronicle/Cowbell "
             "amulet's own notes give; the destination-dispatch was deliberately restructured into three "
             "self-contained branches (each with its own return) specifically so the untestable success "
             "path can never be reached by a broken guard during mutation testing.",
    ),
    "Gloves of silence": dict(
        storage="player_varp", depletion="destroy", max_charges=62,
        charge_source="2 dark kebbit fur + 600gp; repaired at 64 Crafting",
        drain_event="1 charge per FAILED pickpocket attempt while worn",
        status="implemented",
        note="player_varp, same shape as ring of recoil/dodgy necklace. CORRECTED research note: the real "
             "effect is a +5% success-chance boost on the pickpocket ROLL itself (widens the stat_random "
             "low/high bounds by 13/256, the boost's percentage-point value on that scale), not a "
             "dodgy-necklace-style fail-consequence override -- wired into "
             "skill_thieving/scripts/thieving.rs2's check_if_success_pick_pocket (the roll) and pick_pocket's "
             "fail branch (the drain, same site dodgy_necklace.rs2 hooks). NOT implemented: repairing via "
             "dark kebbit fur + thread/knife/needle (a from-scratch use-item interaction, not a charges gap) "
             "and the Ardougne diary interaction (medium/hard diary completion is tracked for real in "
             "interface_diaries/scripts/diaries.rs2, but nothing resolves \"is the player in Ardougne\").",
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
        status="implemented",
        note="general/scripts/enchanted_jewellry/horn_of_plenty.rs2/.constant/.varp. `~horn_of_plenty_catch` "
             "is now called from all 17 Hunter catch sites this tree has (every `stat_advance(hunter, ...)` "
             "call across skill_hunter/scripts/'s 13 files with one -- bird_snare/box_trap/butterfly x2/"
             "common_trail/deadfall/desert_jungle_trail x2/falconry/impling/magic_box/net_trap/pitfall x3/"
             "polar_trail; hunter_traps.rs2/imp_box.rs2/rabbit_hole.rs2 have none of their own), each a "
             "single-line insertion right after the XP grant, found by an exhaustive grep pass rather than "
             "assumed to only live in a couple of files. Self-gates on the Toggle, worn state, and charge "
             "count the same way blood_fury_drain is safe to call unconditionally from every combat hit. "
             "Charging is NOT implemented: the `gryphon feather` obj this recipe needs does not exist "
             "anywhere in this cache, so Uncharge is a documented no-op rather than a refund, and the "
             "+4-instead-of-+2 Hunter catch-chance boost itself (a separate integration point at wherever "
             "base catch chance is computed) is also not implemented -- the charge count/drain mechanism is "
             "complete and tested even though the bonus effect it's supposed to unlock isn't.",
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
        storage="item_var", depletion="none", max_charges=16383,
        charge_source="Fletched from 2 camphor logs + squid beak; loaded with up to mithril darts (no scales needed)",
        drain_event="1 dart consumed per shot",
        status="implemented",
        note="Same shared library as Toxic blowpipe (gear/blowpipe_ammo.rs2); see its note. Wiki confirmed "
             "directly (not assumed): 'unlike the toxic blowpipe, it does NOT need to be charged with "
             "Zulrah's scales to use it' -- ~blowpipe_needs_scales gates the scale check/drain/fill so this "
             "fires on darts alone.",
    ),
    "Celestial ring": dict(
        storage="item_var", depletion="none", max_charges=10000,
        charge_source="Charged with stardust from Shooting Stars, 1:1", drain_event="1 charge per ore mined",
        status="implemented",
        note="Wired into skill_mining/scripts/mining.rs2's two ore-success sites, the same site "
             "crystal_pickaxe's own drain already used (a real, small choke point -- unlike Hunter's ~15 "
             "scattered trap files). Op numbers differ between uncharged (ifop2=Wear/ifop3=Charge) and "
             "charged (ifop1=Wear/ifop2=Check/ifop3=Charge/ifop5=Uncharge) forms, checked directly rather "
             "than assumed. NOT implemented: the 'up to adamantite, with some exceptions' ore-tier cap -- "
             "mining_table has no ore-tier column, so the 1/10 proc applies to every successful mine "
             "uniformly; and the quantity-prompted Charge UX, simplified to 'consume all available stardust "
             "up to the cap in one click.' Both stated, not silently dropped.",
    ),
    "Celestial signet": dict(
        storage="item_var", depletion="none", max_charges=10000,
        charge_source="Ring + elven signet + 100 crystal shards + 1000 stardust at a singing bowl; recharged with stardust only",
        drain_event="Same mining drain as celestial ring, plus a 10% chance to save a crystal-equipment charge",
        status="implemented",
        note="Same wiring as Celestial ring (same mining hook, same op-number verification, same "
             "not-implemented ore-tier-cap/quantity-Charge-UX notes) plus the elven signet half: a 10% "
             "chance to skip a crystal item's own charge spend, wired into "
             "minigame_gauntlet/scripts/crystal_equipment.rs2's crystal_drain_one -- the one shared drain "
             "primitive every crystal item (armour, tools, and per the wiki's own explicit exclusion, all "
             "weapons except blade of Saeldor/bow of Faerdhinen) already calls. NOT implemented: creating "
             "the signet (ring + elven signet + shards + stardust at a singing bowl) -- a crafting recipe, "
             "not a charges gap, same as this session's other missing creation paths.",
    ),
    "Chronicle": dict(
        storage="item_var", depletion="none", max_charges=1000,
        charge_source="Diango's Toy Store, charged with teleport cards (150gp each)",
        drain_event="1 charge per teleport to Champions' Guild",
        status="implemented",
        note="Teleports to Charlie the Tramp's real, verified spawn coordinate (areas/world/configs/"
             "m50_52.spawn: 'tramppg 3208 3391 0') rather than a guessed tile -- no hand-authored "
             "loc-placement data exists in this tree to read the guild door's own coordinate from (locs are "
             "cache-baked, not source scripts), so this grounds the destination in the wiki's own stated "
             "landmark ('a short distance from Charlie the Tramp') and a real spawn record instead. Gated "
             "on Wilderness level per the wiki. NOT implemented: charging via Diango's shop (150gp/teleport "
             "card) and the 'retains charges if bought back' shop-memory behaviour -- both shop-purchase "
             "interactions, not charges gaps, the same distinction this session draws for other missing "
             "acquisition paths. Spawn pre-charged to test.",
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
        status="implemented",
        note="Teleports to a real, verified cow spawn coordinate (areas/world/configs/m50_51.spawn: 'cow "
             "3254 3267 0') rather than a guessed tile, the same reasoning Chronicle's own destination uses. "
             "Gated at Wilderness level 20 per the wiki. NOT implemented: the milking-speed passive (6 vs 7 "
             "ticks while worn), the separate 'Ring' option (calls nearby cows, unrelated to charges), and "
             "re-obtaining a lost amulet from Gillie Groats (an NPC dialogue grant, not a charges gap).",
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
             "comment confirms) -- but it had NO Check/Charge/Dismantle binding at all until fixed 2026-08-13 "
             "(clicking Check hit the engine's unbound-trigger fallback; even crystal_bow's own Check would "
             "have looked up the wrong obj's charges had a _2500 variant reached it). CORRECTED research "
             "error: the combat-swing drain (~crystal_weapon_drain, gauntlet_hunllef.rs2) is NOT Gauntlet-only "
             "-- it is called unconditionally from rs2012_td_player_hit.rs2's player_hit_npc_prepare, the one "
             "global player-to-npc damage funnel every melee/ranged/magic hit goes through (confirmed by "
             "reading that proc directly, not trusting the file it lives in). It only special-cases behaviour "
             "*inside* an active Hunllef fight; outside one it drains on every ordinary hit, and always did.",
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
        note="Fixed 2026-08-13: crystal_halberd_2500 (the Islwyn-bought id) had ZERO bindings anywhere -- "
             "added to crystal_equipment.rs2's Charge/Check/Dismantle case lists and gauntlet_hunllef.rs2's "
             "combat-drain equality check. Combat-swing drain is NOT Gauntlet-only (same correction as "
             "Crystal bow) -- ~crystal_weapon_drain fires from the one global player-to-npc damage funnel.",
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
        note="Fixed 2026-08-13: crystal_shield_2500 had ZERO bindings, same gap as crystal_halberd_2500, same "
             "fix (case lists + drain equality check, and the drain now checks either hand -- the original "
             "code always drained the literal crystal_shield obj regardless of which hand actually held it). "
             "Combat-drain scope corrected the same way as Crystal bow -- not Gauntlet-only.",
    ),
    "Echo boots": dict(
        storage="item_var", depletion="none", max_charges=60000,
        charge_source="Guardian boots + echo crystal (Fortis Colosseum drop); each extra crystal adds 6,000 charges",
        drain_event="1 charge per recoil proc (1 damage back to any attacker in a 3x3), toggleable",
        status="implemented",
        note="Wired into combat_stats.rs2 right after ~ring_of_recoil_check ($damage), the real 'player "
             "takes damage' funnel. NOT implemented: the AoE part of the effect (recoiling every target in "
             "a 3x3, not just the one attacker) -- ring_of_recoil_check's own %aggressive_npc single-target "
             "model is the only precedent for a reactive-damage effect in this tree; there is no "
             "multi-target 'who else is near and hostile' query to extend. Wires the single-target case "
             "(recoil the one attacker for 1 damage) so the charge count is not permanently unusable, the "
             "same tradeoff Venator bow's note makes for its own missing bounce passive. Stays equipped "
             "with no effect at 0 charges (a genuine 'none' depletion, confirmed by a later changelog note); "
             "only the manual Revert (to guardian boots, losing the crystal) converts it.",
    ),
    "Infernal axe": dict(
        storage="item_var", depletion="revert", max_charges=5000,
        charge_source="Smouldering stone used on a Dragon axe; recharged the same way",
        drain_event="1/3 chance per log chopped to consume the log for half Firemaking xp instead of a normal burn",
        status="implemented",
        note="general/scripts/enchanted_jewellry/infernal_axe.rs2, same shape as Infernal pickaxe: present-"
             "check covers worn OR backpack, recharge is a full refill (not per-item), one-way revert to the "
             "untradeable empty obj at 0. Hooked into skill_woodcutting/scripts/woodcut.rs2's `get_logs` "
             "label; Firemaking xp read from the real `firemaking_log_xp` table (unlike Infernal pickaxe's "
             "own Mining-xp stand-in, this tree already has real per-log Firemaking xp data) and halved. Logs "
             "the table doesn't know (redwood/teak/mahogany) get 0 xp and are correctly never destroyed -- a "
             "guard, mutation-tested. Does NOT cover trailblazer_axe/trailblazer_reloaded_axe (cosmetic "
             "League recolours) -- same pre-existing gap Infernal pickaxe's own implementation already has "
             "for trailblazer_pickaxe, not reopened here.",
    ),
    "Infernal harpoon": dict(
        storage="item_var", depletion="revert", max_charges=5000,
        charge_source="Smouldering stone used on a Dragon harpoon; recharged the same way",
        drain_event="1 charge consumed each time a fish is auto-cooked while harpooning",
        status="implemented",
        note="general/scripts/enchanted_jewellry/infernal_harpoon.rs2, same shape as Infernal axe. Hooked "
             "into skill_fishing/scripts/fishing.rs2's shared `fish_roll` (both branches), gated on "
             "`$equipment = harpoon` exactly like the pre-existing crystal harpoon drain call beside it -- "
             "covers memberfish.rs2's shark catches too since those route through the same shared proc. "
             "Cooking xp read from the real `cooking_generic` dbtable (indexed by the raw fish obj) and "
             "halved. Raw shark has no row in that table at all (its own file header marks members-fish "
             "content deferred), so shark is correctly never destroyed -- happens to match the wiki's real "
             "big-fish exclusion by construction, not by an actual exclusion list; mutation-tested. Same "
             "trailblazer-recolour gap as Infernal axe.",
    ),
    "Infernal pickaxe": dict(
        storage="item_var", depletion="revert", max_charges=5000,
        charge_source="Smouldering stone used on a Dragon pickaxe; recharged the same way",
        drain_event="1 charge consumed each time the pickaxe's mining effect activates",
        status="implemented",
        note="Wired into skill_mining/scripts/mining.rs2's two ore-success sites, alongside Celestial "
             "ring/signet's own procs at the same site. Present-check covers worn OR anywhere in the "
             "backpack (wiki explicit: 'equipped or in the inventory'), unlike every other item this "
             "session's worn-only checks. Recharging (smouldering stone or another dragon pickaxe) is a "
             "full refill to 5,000, not a per-item increment -- confirmed against the wiki's own cost table "
             "rather than assumed from sibling items. Auto-reverts one-way to the untradeable-forever empty "
             "obj id at 0 (wiki explicit it can never revert back to a dragon pickaxe). Does NOT cover "
             "infernal_axe/infernal_harpoon (same family, different skills -- Woodcutting/Fishing hit-"
             "success sites this pass did not locate) or the Trailblazer League recolours of any of the "
             "three. NOT implemented: the real per-ore Smithing XP table (uses half the row's Mining XP as "
             "a stand-in, almost certainly the wrong absolute number since the two skills' XP scales are "
             "unrelated) and the 'does not consume ore at certain spots' exclusion list (Motherlode Mine, "
             "Volcanic Mine, amethyst, gem rocks, crashed stars, dense runestone, punishment rocks) -- "
             "mining_table has no ore-tier column to gate on, same limitation Celestial ring's own note "
             "states for its 'up to adamantite' cap.",
    ),
    "Ironwood blowpipe": dict(
        storage="item_var", depletion="none", max_charges=16383,
        charge_source="Darts used directly on the blowpipe (up to adamant); no scales needed",
        drain_event="1 dart consumed per shot",
        status="implemented",
        note="Same shared library as Toxic blowpipe (gear/blowpipe_ammo.rs2); see its note. Confirmed no "
             "scales needed directly against this item's own wiki page.",
    ),
    "Rosewood blowpipe": dict(
        storage="item_var", depletion="none", max_charges=16383,
        charge_source="Darts used directly on the blowpipe (up to rune); no scales needed",
        drain_event="1 dart consumed per shot; special attack fires two shots for 25% special energy",
        status="implemented",
        note="Same shared library as Toxic blowpipe (gear/blowpipe_ammo.rs2); see its note. Its special "
             "attack (specs/pvm_rosewood_blowpipe.rs2, player_special_attack.rs2 case 63) already called "
             "the ammo check but never drained anything for either of its two shots -- fixed alongside the "
             "rest of the ammo model, calling ~blowpipe_consume once per shot rather than left as a second "
             "gap.",
    ),
    "Kharedst's memoirs": dict(
        storage="player_varp", depletion="none", max_charges=100,
        charge_source="each of 5 torn pages used on the book grants +20 charges AND unlocks its own "
                       "destination; a separate Old Memorial rune top-up is not implemented (see note)",
        drain_event="1 charge per Reminisce teleport to an unlocked Kourend-house destination",
        status="implemented",
        note="general/scripts/enchanted_jewellry/kharedst_memoirs.rs2/.constant/.varp, sixth family built "
             "on SS_OP_LAST_SUBOP. The book has a single obj id throughout (no charged/uncharged pair, "
             "unlike every other item this session) -- confirmed player_varp directly, both for the "
             "charge count and for five separate per-page unlock flags. The Old Memorial (a rune-cost "
             "top-up beyond what pages grant) has zero content anywhere in this tree -- no loc, no npc -- "
             "so it is not implemented; what IS implemented is the wiki's other charge source, which is "
             "self-contained and fully testable on its own: adding a page both unlocks its destination and "
             "directly grants +20 charges. Destination coordinates came from the wiki's own per-page "
             "teleport pins, cross-checked against a real regional-NPC spawn for all 5 (which also "
             "confirmed the page->destination mapping, since the wiki names destinations by teleport "
             "title, not by house, while the obj's own page ids are house-named). Each of the 5 page-adding "
             "actions is its own dedicated proc (not one shared helper taking 'which varp') since this "
             "language's procs are call-by-value with no verified precedent for a varp-typed parameter "
             "writing through to the global it names -- checked directly rather than assumed after an "
             "initial draft guessed the pass-by-reference shape would work.",
    ),
    "Merfolk trident": dict(
        storage="item_var", depletion="none", max_charges=10,
        charge_source="Up to 10 pufferfish used on the trident",
        drain_event="1 charge consumed per 'Channel' use (regain underwater breath)",
        status="implemented",
        note="general/scripts/enchanted_jewellry/merfolk_trident.rs2/.constant. The charge gate/drain "
             "itself is real and tested (Channel checks charges, drains exactly 1, refuses at 0) -- the "
             "downstream effect it triggers (restoring underwater breath) is a documented no-op since this "
             "tree has no generic underwater-breath meter at all (only quest-scripted one-off diving "
             "scenes), the same 'the charge mechanic is complete even though a secondary bonus effect "
             "isn't' scope line this session draws for several other items (Arclight's infusion meter, "
             "Tonalztics' auto-attack range). Charging is NOT implemented: the `pufferfish` obj this recipe "
             "needs does not exist anywhere in this cache. Spawn a merfolk_trident with charges set "
             "directly to test.",
    ),
    "Pendant of ates": dict(
        storage="item_var", depletion="revert", max_charges=1000,
        charge_source="Frozen tears (1:1), untradeable rare drop from Frost nagua / Amoxliatl",
        drain_event="1 charge per teleport to one of six unlocked Varlamore destinations",
        status="implemented",
        note="general/scripts/enchanted_jewellry/pendant_of_ates.rs2/.constant, fifth family built on "
             "SS_OP_LAST_SUBOP. Wiki explicit: charges are NOT shared across multiple pendants owned -- "
             "item_var confirmed. Charging is opheldu (a tear used ON the pendant, matching the obj "
             "record's own missing Charge menu option), 1:1, capped at 1,000; Uncharge refunds the exact "
             "remaining count. Unlike every other Rub-submenu item this session (Xeric's talisman/Ring of "
             "shadows/Sailors' amulet all have at least one destination open by default), this one has "
             "ZERO: \"No teleports are available by default. To unlock locations, a player must first "
             "activate the statue of ates at each location\" -- no statue-of-ates content exists anywhere "
             "in this tree, so no destination could ever be legitimately opened here. All six are guarded "
             "to a clean refusal; unlike the other four items, no coordinate was even derived for any "
             "destination (checked first: only 2 of the 6 wiki-pin map squares have spawn data loaded in "
             "this tree at all, and it would have been dead code regardless of coordinates, since nothing "
             "can unlock any of them). The one item this session built where the full teleport-dispatch "
             "proc, including every destination branch, IS safe to exercise end to end in ::chargesrun --  "
             "no live `~player_teleport_normal` branch exists at all.",
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
        charge_source="1 blood+soul+death+law rune per charge, used on the ring (obj's own ifop4=Charge, a "
                       "menu action, not opheldu)",
        drain_event="1 charge per Teleport to the Ancient Vault (default); the other 4 destinations are "
                     "boss-drop-tablet unlocks with no spawned content anywhere in this tree",
        status="implemented",
        note="general/scripts/enchanted_jewellry/ring_of_shadows.rs2/.constant, third family built on "
             "SS_OP_LAST_SUBOP (see Giantsoul amulet). Ancient Vault reuses Desert Treasure II's own "
             "`^dt2_vault_coord` (deserttreasureii.constant), already used by that quest's own "
             "`p_teleport`. Ghorrock Dungeon/The Scar/Lassar Undercity/The Stranglewood are all Desert "
             "Treasure II post-quest boss lairs -- none of those bosses exist anywhere in this tree "
             "(checked directly), so all four are guarded to a clean refusal and drain no charge. Charging "
             "needs a MATCHED SET of 4 different runes per charge (not a single material like this "
             "session's other resource-charged items), verified against the obj record's own $have = "
             "min-of-four shape. Uncharge (ifop5) refunds all four rune types 1:1 with remaining charges "
             "(\"uncharging the ring does refund the runes\" -- checked directly rather than assumed from "
             "Cowbell amulet/Xeric's talisman's own single-material refund shape).",
    ),
    "Sailors' amulet": dict(
        storage="item_var", depletion="none", max_charges=10000,
        charge_source="1 law + 10 water runes per 10 charges, used on the amulet (opheldu)",
        drain_event="1 charge per Teleport to The Pandemonium (default); Port Roberts/Red Rock/Deepfin "
                     "Point all require a Sailing skill level + inspecting that destination's own marker",
        status="implemented",
        note="general/scripts/enchanted_jewellry/sailors_amulet.rs2/.constant, fourth family built on "
             "SS_OP_LAST_SUBOP. The Pandemonium reuses quest_pandemonium's own `^pan_pand_dock` coordinate. "
             "Charging is BATCHED (10 charges per 1 law + 10 water runes), not 1:1 like this session's "
             "other rune-charged items -- verified against the wiki's own wording rather than assumed "
             "uniform, and the room/afford calc is floor-divided per batch so a remainder under 10 charges "
             "of headroom genuinely cannot be topped off by a partial batch (matches the real batched "
             "mechanic, not a bug). Port Roberts/Red Rock/Deepfin Point all need a Sailing skill level "
             "(50/52/67) this tree has no Sailing skill to check at all -- guarded to a clean refusal. Red "
             "Rock's own coordinate does exist in this tree (`^trr_red_rock`, redreef.constant) but is "
             "deliberately NOT wired here: granting free access to a level-gated teleport this tree cannot "
             "verify the level for would be an incorrect implementation, not merely an incomplete one -- "
             "stated explicitly since it is the one destination this file could technically reach but "
             "chooses not to. depletion=none matches the wiki's own \"the amulet cannot be uncharged\" -- "
             "there is no Uncharge op on the obj record either.",
    ),
    "Serpentine helm": dict(
        storage="item_var", depletion="revert", max_charges=11000,
        charge_source="Zulrah's scales used on the helm (11,000 = full charge); bank Configure-Charges supported",
        drain_event="10 scales on entering combat, another 10 every 90 ticks (54s) while combat continues",
        status="implemented",
        note="skill_combat/scripts/player/gear/zulrah_item_charges.rs2/.constant/.varp -- shared library "
             "covering all 3 helm colours (obj ids are all serpentine_helm[_charged][_cyan|_red], not "
             "separate 'magma_helm'/'tanzanite_helm' families as an earlier pass assumed) + Toxic staff of "
             "the dead (see that entry). Reuses this session's own Barrows/Moon degrade-on-combat timer "
             "shape verbatim (~barrows_degrade_enter/[timer,barrows_degrade]'s pattern) rather than "
             "reinventing one -- the repeating-every-90-ticks behaviour (not a one-shot 10+10) was verified "
             "against the wiki's own '666.67 scales/hour of continuous combat' figure, which only holds if "
             "the burn repeats indefinitely (3600s/54s * 10 = 666.7, matching; 11000/666.67 = 16.5 hours, "
             "matching the stated full-charge duration) -- the ledger's prior 'another 10 if still in combat "
             "after 90 ticks' phrasing undersold this as a one-time second burn. Charging is opheldu (scales "
             "used directly on the item, 1:1), Uncharge (ifop5) returns the exact remaining count as unnoted "
             "scales. Hooked into the same attack-dealt/damage-taken funnels Barrows/Moon already use. NOT "
             "implemented: Dismantle/Restore (mutagen removal, cosmetic-only, unrelated to charges).",
    ),
    "Magma helm": dict(
        storage="item_var", depletion="revert", max_charges=11000,
        charge_source="Same as Serpentine helm -- magma-mutagen recolour, mechanically identical",
        drain_event="Same as Serpentine helm",
        status="implemented",
        note="Colour variant of Serpentine helm sharing one mechanic (obj serpentine_helm_charged_red) -- see "
             "that entry for the full mechanism.",
    ),
    "Tanzanite helm": dict(
        storage="item_var", depletion="revert", max_charges=11000,
        charge_source="Same as Serpentine helm -- cyan-mutagen recolour, mechanically identical",
        drain_event="Same as Serpentine helm",
        status="implemented",
        note="Colour variant of Serpentine helm sharing one mechanic (obj serpentine_helm_charged_cyan) -- "
             "see that entry for the full mechanism.",
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
        status="implemented",
        note="Wired into player_hit_npc_prepare (general/scripts/enchanted_jewellry/blood_fury.rs2), "
             "the same global melee/ranged/magic damage funnel crystal_weapon_drain uses, gated on "
             "melee style since the wiki is explicit this amulet only drains there. Auto-reverts to "
             "enchanted_onyx_amulet at 0 charges (blood shard lost); the Revert option does the same "
             "on demand. NOT implemented: creating a blood_amulet at all -- combining Amulet of fury + "
             "Blood shard, and topping up with further shards, plus the bank Configure-Charges QoL "
             "option. Zero combine logic exists anywhere in this tree for this whole family (also "
             "true of Ring of suffering (r), Necklace of anguish, Tormented bracelet, Amulet of "
             "avarice) -- a from-scratch crafting feature, not a charges gap. Spawn to test.",
    ),
    "Amulet of bounty": dict(
        storage="player_varp", depletion="destroy", max_charges=10,
        charge_source="Opal amulet + Lvl-1 Enchant; no separate refill -- only reset via Break or hitting 0",
        drain_event="25% chance to use 1 seed instead of 3 when planting an allotment, 1 charge per proc",
        status="implemented",
        note="player_varp, same shape as ring of recoil / dodgy necklace. Wired into "
             "skill_farming/scripts/farming_plant.rs2's farming_plant_allot, which overrides the real "
             "seed_count db field it reads right before spending it. Break (opheld4) destroys the worn "
             "amulet and resets the account-wide counter, distinct from the amulet simply leaving play "
             "with charges still on it.",
    ),
    "Arclight": dict(
        storage="item_var", depletion="revert", max_charges=10000,
        charge_source="Darklight + shards (obj cata_shard), opheldu batches of 3 shards = 1,000 charges",
        drain_event="1 charge per successful hit, excluding special attack",
        status="implemented",
        note="gear/arclight.rs2/.constant/.varp. The 'Ancient shard' obj exists under a different literal "
             "name than expected (cata_shard, not ancient_shard) -- found by reading the record right after "
             "arclight's own in all.obj, not by the name search that first came up empty. Special-attack "
             "exclusion needed a new mechanism this session's other weapon drains never did: "
             "pvm_arclight.rs2 sets %arclight_special_active around its own call into the shared "
             "player_hit_npc_prepare funnel, and arclight_drain skips while that flag is set -- mutation-"
             "tested. NOT implemented: the Catacombs-of-Kourend altar location requirement for the initial "
             "Darklight->Arclight conversion (no such loc exists anywhere in this tree, simplified to a "
             "plain opheldu combine) and the separate 'infusion'/Emberlight-upgrade progress meter.",
    ),
    "Ash sanctifier": dict(
        storage="item_var", depletion="none", max_charges=2147483647,
        charge_source="Death runes used on it, 10 charges per rune",
        drain_event="1 charge per demonic ashes auto-purified on npc-kill drop while carried (toggle: Activity)",
        status="charges_only",
        note="general/scripts/enchanted_jewellry/ash_sanctifier.rs2/.constant/.varp now implements charge "
             "storage, Check, the Activity toggle (account-wide player_varp -- there is no second real obj "
             "id to spend on a per-item boolean the way the charge count itself uses the item's own id), "
             "and Uncharge, all independently correct and tested. Left as charges_only rather than flipped "
             "because the actual charge-CONSUMPTION event -- auto-purifying ashes on an npc-kill drop -- "
             "still has nothing to hook into: no drop-interception point exists anywhere in this tree's "
             "loot pipeline (bury_bone.rs2/bone_xp.rs2 are manual-only). A ring that can only ever charge "
             "up and never spend is not a complete charges implementation.",
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
        note="Wired the same way as Crystal bow/halberd/shield (NOT Crystal helm/body/legs, which use the "
             "separate, genuinely Gauntlet-only crystal_armour_drain): gauntlet_hunllef.rs2's "
             "crystal_weapon_drain (includes blade_of_saeldor) is called from the GLOBAL player_hit_npc_prepare "
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
        storage="item_var", depletion="none", max_charges=16383,
        charge_source="Toxic blowpipe (empty) + Trailblazer reloaded blowpipe ornament kit, filled like a toxic blowpipe",
        drain_event="1 dart (and its charge) consumed per shot; refuses to fire empty",
        status="implemented",
        note="Same shared library as Toxic blowpipe (gear/blowpipe_ammo.rs2), needs scales the same way. "
             "NOT verified: whether Toxic blowpipe's own special attack (pvm_toxic_blowpipe.rs2, sa_kind "
             "case 56) actually dispatches for this ornament id -- that is player_special_attack.rs2's own "
             "param-based routing table, a pre-existing concern this pass did not need to touch since "
             "pvm_toxic_blowpipe_sa reads $rhand generically rather than hardcoding an obj id; unconfirmed "
             "whether the ornament's own sa_kind param maps to the same case.",
    ),
    "Blood moon chestplate": dict(
        storage="item_var", depletion="revert", max_charges=3000,
        charge_source="Lunar Chest (Neypotzli) drop; the pristine 'new' id converts to the charged 'degraded' id on first wear, starting full",
        drain_event="1 charge per 54 seconds spent in combat while worn",
        status="implemented",
        note="CORRECTED: the in-combat timer subsystem this note said didn't exist was built this session "
             "(skill_combat/scripts/player/gear/barrows_degrade.rs2, generalized to moon_degrade.rs2). Same "
             "shared library as Eclipse moon chestplate's own note describes -- all 9 moon-armour pieces "
             "(Eclipse/Blood/Blue x chestplate/helm/tassets) wired together.",
    ),
    "Blood moon helm": dict(
        storage="item_var", depletion="revert", max_charges=3000,
        charge_source="Same as Blood moon chestplate", drain_event="1 charge per 54 seconds in combat while worn",
        status="implemented", note="Same shared library as Eclipse moon chestplate; see its note.",
    ),
    "Blood moon tassets": dict(
        storage="item_var", depletion="revert", max_charges=3000,
        charge_source="Same as Blood moon chestplate", drain_event="1 charge per 54 seconds in combat while worn",
        status="implemented", note="Same shared library as Eclipse moon chestplate; see its note.",
    ),
    "Blue moon chestplate": dict(
        storage="item_var", depletion="revert", max_charges=3000,
        charge_source="Lunar Chest drop; activates to the charged/degraded id on first wear, full at 3,000",
        drain_event="1 charge per 54 seconds in combat while worn",
        status="implemented",
        note="Wiki title 'Blue moon' but the cache's internal family name is 'frost_moon' (pre-rename "
             "datamined name) -- ladder is frost_moon_chestplate -> _degraded -> _broken, wired the same as "
             "Eclipse moon chestplate; see its note.",
    ),
    "Blue moon helm": dict(
        storage="item_var", depletion="revert", max_charges=3000,
        charge_source="Same as Blue moon chestplate", drain_event="1 charge per 54 seconds in combat while worn",
        status="implemented", note="Same shared library as Eclipse moon chestplate; see its note.",
    ),
    "Blue moon tassets": dict(
        storage="item_var", depletion="revert", max_charges=3000,
        charge_source="Same as Blue moon chestplate", drain_event="1 charge per 54 seconds in combat while worn",
        status="implemented", note="Same shared library as Eclipse moon chestplate; see its note.",
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
        note="general/scripts/enchanted_jewellry/bonecrusher.rs2/.constant/.varp now implements charge "
             "storage, Check, the Activity toggle, and Uncharge, same shape as Ash sanctifier (see that "
             "entry's note for the shared reasoning). A first draft wrongly copied Ash sanctifier's own "
             "death-rune material for this item too; re-checked directly against the live wiki and fixed "
             "before landing -- the real material is ecto-tokens, 25 charges each, not death runes. Same "
             "missing subsystem as Ash sanctifier for the drain itself -- no on-npc-death auto-consume/"
             "drop-interception hook exists anywhere in this tree. skill_prayer/scripts/bone_xp.rs2's "
             "bone_prayer_exp is the reusable per-bone XP table for whoever wires it.",
    ),
    "Book of the dead": dict(
        storage="item_var", depletion="none", max_charges=250,
        charge_source="Recharged at the Old Memorial: 1 law + 1 body + 1 mind + 1 soul rune per charge, 10 Magic XP each",
        drain_event="1 charge per teleport (Reminisce option, 5 destinations, all unlocked unconditionally)",
        status="implemented",
        note="general/scripts/enchanted_jewellry/book_of_the_dead.rs2/.constant. Confirms the prior pass's "
             "flagged item_var call was correct (this ledger's own note already reasoned it out from the "
             "wiki's phrasing). Reuses Kharedst's memoirs' own destination coordinates directly by name "
             "(same five Kourend-house teleports) -- unlike the memoirs, all five are always available "
             "(obtaining a book of the dead already implies every page was present on the memoirs it "
             "upgraded from), so there is no per-destination unlock state to track here. NOT implemented: "
             "the Old Memorial recharge (same real gap kharedst_memoirs.rs2 already states -- no such loc/"
             "npc exists anywhere in this tree) and the quest-driven Kharedst's memoirs -> book of the dead "
             "upgrade transition itself; a freshly spawned book needs its starting charge count set "
             "directly for testing, the same 'spawn to test' precedent this session's other creation-gap "
             "items already accept.",
    ),
    "Bracelet of slaughter": dict(
        storage="player_varp", depletion="destroy", max_charges=30,
        charge_source="Topaz bracelet + Lvl-3 Enchant; no separate refill -- only reset via Break or hitting 0",
        drain_event="25% chance per Slayer-task kill to not decrement the task's remaining kill count (full XP still granted), 1 charge per proc",
        status="implemented",
        note="player_varp, same shape as expeditious_bracelet/dodgy_necklace. Wired into "
             "skill_slayer/scripts/slayer_kill.rs2's slayer_on_npc_kill, skipping the real %if1 task-count "
             "decrement instead of a synthetic hook (XP is awarded separately in the same proc, unaffected). "
             "The Jad/Zuk exclusion needs no special-casing: both are boss tasks (%if1=-1) that already "
             "return before reaching the decrement, so the effect is never even asked. Crumbles at 0 "
             "charges; can never be worn alongside expeditious bracelet (same ^wearpos_hands slot). NOT "
             "implemented: elite Combat Achievements' regen reward -- see Expeditious bracelet's own note.",
    ),
    "Sanguinesti staff": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Charged with blood runes (2 per charge) via bank Configure-Charges or direct use",
        drain_event="1 charge per cast of the staff's built-in spell",
        status="implemented",
        note="gear/sanguinesti_staff.rs2/.constant. The powered-staff auto-attack combat mode this ledger's "
             "own prior note correctly identified as entirely missing now exists: combat.rs2's "
             "`[label,player_combat_start]` gained one narrow, additive early-exit "
             "(`~powered_staff_worn`/`@player_powered_staff_attack`) ahead of the existing ranged/autocast/"
             "melee branches, none of which were modified -- verified by running the full selftest in both "
             "plain and MOCK230_GEARRUN=1 modes and confirming the exact same 12-failure pristine baseline "
             "before and after. Max hit is the wiki's own flat formula (floor(Magic level / 3), not derived "
             "from magic_spell_table since this weapon has no spell row) fed through the same "
             "npc_max_dealt/player_npc_hit_roll/player_hit_npc_prepare pipeline every other attack style "
             "already uses. Eye of ayak and Holy sanguinesti staff (see their own entries) now also share "
             "this dispatch, each with its own max-hit formula/obj mapping verified rather than assumed "
             "identical; Tumeken's shadow/Warped sceptre still have no obj anywhere in this cache. NOT "
             "implemented: the Life leech passive (a secondary bonus effect, same scope line Arclight's "
             "infusion meter/Tonalztics' auto-attack range draw).",
    ),
    "Holy sanguinesti staff": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Charged with blood runes (2 per charge) via bank Configure-Charges or direct use",
        drain_event="1 charge per cast of the staff's built-in spell",
        status="implemented",
        note="sanguinesti_staff_or/sanguinesti_staff_uncharged_or -- NOT a mere cosmetic Ornament Kit "
             "recolour as an earlier pass in this session wrongly assumed (the same dismissal this ledger "
             "applies to actual recolours elsewhere): verified directly against all.obj that "
             "sanguinesti_staff_or is a real, separately-created item ('attaching a holy ornament kit to an "
             "uncharged Sanguinesti staff') with its own name/examine text and its own charge pool, "
             "mechanically identical to the base staff (same params: magicattack=25, attackrate=4, "
             "weapon_attackrange=7). gear/sanguinesti_staff.rs2 was generalized into a shared library "
             "covering both obj-id pairs via two mapping helpers (~sanguinesti_staff_charged_obj/"
             "~sanguinesti_staff_uncharged_obj), and gear/powered_staff.rs2's three dispatch procs "
             "(~powered_staff_worn/~powered_staff_maxhit/~powered_staff_uncharged_obj) were extended to "
             "recognize sanguinesti_staff_or alongside sanguinesti_staff and eye_of_ayak, sharing the "
             "base staff's exact max-hit formula per the wiki's 'no additional stat bonuses'. Verified with "
             "a new chargesrun_holy_sanguinesti_staff_charge_and_worn selftest proc (charge/cap/worn/maxhit/"
             "revert) plus a mutation test on ~sanguinesti_staff_charged_obj's new branch (confirmed red with "
             "the right FAIL message, reverted). Full selftest re-run in both plain and MOCK230_GEARRUN=1 "
             "modes, same 12-failure pristine baseline before and after. NOT implemented: the Life leech "
             "passive, same scope line as the base staff's own note.",
    ),
    "Slayer's staff": dict(
        storage="item_var", depletion="revert", max_charges=2500,
        charge_source="Slayer's enchantment used on a Slayer's staff (raises Magic req to 75)",
        drain_event="1 charge per Magic Dart cast that lands on a monster killed as part of the player's Slayer task",
        status="implemented",
        note="Wired into skill_combat/scripts/player/spells/magic_dart.rs2's pvm_magic_dart, reusing "
             "skill_slayer/scripts/slayer_kill.rs2's slayer_npc_matches_task (which already reads the "
             "ambient npc_category/%if1-3 task state with no npc parameter) to gate on the current target "
             "matching the player's Slayer task. Drains once per cast at a task-matching target regardless "
             "of hit success -- the wiki's 'casts against monsters' reads as attempts, not landed hits, "
             "unlike blood fury/Saradomin's blessed sword. NOT implemented: the actual power increase Magic "
             "Dart gets from the enchanted staff -- pvm_magic_dart.rs2's maxhit formula has no staff-tier "
             "branch. Tracking charges correctly does not depend on that gameplay effect existing, the same "
             "tradeoff Venator bow's/Echo boots' own notes make for their missing effects.",
    ),
    "Tome of earth": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Soiled pages, 20 charges/page, up to 1,000 pages",
        drain_event="1 charge per cast of Earth Strike/Bolt/Blast/Wave with the tome worn; other earth-rune "
                     "spells stay free (infinite runes, no drain)",
        status="implemented",
        note="general/scripts/enchanted_jewellry/tome_of_elements.rs2 (shared library for all 3 tomes) + "
             ".constant. The prior note's claim that 'pvm_spell_cast has no off-hand-tome branch to extend' "
             "was correct but beside the point -- the real generalizable hook was shared/skill_magic's own "
             "`staff_runes` (magic.rs2), which already exists to grant a worn staff's free rune and just "
             "needed a second, independent wearpos_lhand-keyed check added alongside the wearpos_rhand one "
             "for the free-rune grant. Charge drain is separate, hooked into pvm_spell_cast directly (it "
             "already receives $spell_data) gated by an explicit 12-spell allowlist (spells.constant has no "
             "Surge tier in this tree's spellbook, so only Strike/Bolt/Blast/Wave per element exist to gate "
             "on). No searing-page alt material, no +10%/+50% damage bonus (bonus-stat scope line, not a "
             "charges concern -- same cut every other item this session makes). Auto-revert to the "
             "_uncharged obj id at 0 charges is this implementation's own choice (id-ladder, matching "
             "Infernal pickaxe/Cowbell amulet) -- the wiki names no explicit Uncharge option for tomes and "
             "does not state whether a live 0-charge tome auto-swaps id; not otherwise verifiable from what's "
             "in this tree. `::chargesrun` covers charge/cap, rune-grant gating (charged+element+worn only), "
             "and drain/deplete-revert; mutation-tested by widening the fire spell allowlist and confirming "
             "the drain-gate check goes red.",
    ),
    "Tome of fire": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Burnt pages (wint_burnt_page), 20 charges/page, up to 1,000 pages",
        drain_event="1 charge per cast of Fire Strike/Bolt/Blast/Wave with the tome worn; other fire-rune "
                     "spells stay free",
        status="implemented",
        note="Same shared library as Tome of earth (see that entry's note for the full mechanism). Wiki "
             "also names an alternate searing-page charging material and a minimum-hit bonus from it -- not "
             "implemented, bonus/alt-material scope cut.",
    ),
    "Tome of water": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Soaked pages, 20 charges/page, up to 1,000 pages",
        drain_event="1 charge per cast of Water Strike/Bolt/Blast/Wave with the tome worn; other water-rune "
                     "spells stay free",
        status="implemented",
        note="Same shared library as Tome of earth (see that entry's note for the full mechanism).",
    ),
    "Tonalztics of ralos": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Sunfire splinters, 1 splinter = 1 charge",
        drain_event="1 charge per throw (standard attack or the Division special), regardless of hits landed",
        status="implemented",
        note="gear/tonalztics_of_ralos.rs2/.constant. specs/pvm_tonalztics_of_ralos_charged.rs2's own "
             "special already calls ~pvm_tonalztics_hit twice, each independently reaching "
             "player_hit_npc_prepare -- so hooking the drain there once covers the standard throw AND the "
             "special's double-throw with no separate special-attack wiring needed, unlike Arclight (which "
             "needed a dedicated exclusion flag for the opposite reason: excluding its special, not "
             "including it). Charging is an explicit ifop4=Charge menu option, not opheldu (confirmed from "
             "the obj record).",
    ),
    "Toxic staff of the dead": dict(
        storage="item_var", depletion="revert", max_charges=11000,
        charge_source="Zulrah's scales used on the staff, 1 scale = 1 charge",
        drain_event="10 scales on entering combat, another 10 every 90 ticks while combat continues",
        status="implemented",
        note="Same shared library as Serpentine helm (obj toxic_sotd/toxic_sotd_charged) -- see that entry "
             "for the full mechanism. specwep.rs2:198-234's pre-existing special attack is untouched by "
             "this. Unlike the helms, the uncharged obj (toxic_sotd) still has ifop2=Wield (a weaker "
             "Staff of the dead, not unusable) -- confirmed from the obj record rather than assumed "
             "uniform with the helms.",
    ),
    "Tumeken's shadow": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Soul + chaos runes (2 soul + 5 chaos per charge; aether substitutes for soul, not refunded on uncharge)",
        drain_event="1 charge per cast of the built-in spell",
        status="implemented",
        note="gear/tumekens_shadow.rs2/.constant, third weapon on the shared powered-staff dispatch (gear/"
             "powered_staff.rs2). CORRECTS this ledger's own prior claim (grouped with Warped sceptre under "
             "'no obj anywhere in this cache') -- re-verified directly against all.obj after the same wrong "
             "dismissal was caught and fixed for Holy sanguinesti staff: tumekens_shadow/"
             "tumekens_shadow_uncharged are real, present records. Max hit floor(magic/3) + 1, verified "
             "against the wiki's own level-99 example (34), fetched live rather than assumed. Charging "
             "consumes soul runes first, falling back to aether only when soul is insufficient (like Eye of "
             "ayak's own two-material simplification, does not track which type funded which charge, so "
             "Uncharge always refunds the soul-side as soulrune). NOT implemented: the weapon's own x3/x4 "
             "magic-damage-bonus passive (a much larger mechanic reaching into the whole magic-damage-bonus "
             "pipeline, same scope line as Arclight's infusion meter/Life leech). Selftest coverage added "
             "and mutation-tested (the aether-fallback guard); full selftest re-run in both plain and "
             "MOCK230_GEARRUN=1 modes, same 12-failure pristine baseline before and after.",
    ),
    "Venator bow": dict(
        storage="item_var", depletion="revert", max_charges=50000,
        charge_source="Ancient essence, 1 essence = 1 charge",
        drain_event="1 charge per attack, regardless of how many targets the arrow bounces to",
        status="implemented",
        note="Wired into player_ranged.rs2 right next to ~wildy_weapon_consume (the wilderness-weapon "
             "precedent), but NOT wilderness-gated -- the venator bow works anywhere. Uncharge returns all "
             "remaining charges as ancient essence (wiki explicit), unlike a plain Revert. Also covers Echo "
             "venator bow (same file, see its own note). The bow's bounce/ricochet gameplay is NOT "
             "implemented (player_ranged.rs2 is single-target only) -- a combat-mechanics gap this charge "
             "wiring is independent of: the bow still correctly drains 1 charge per shot either way.",
    ),
    "Warped sceptre": dict(
        storage="item_var", depletion="revert", max_charges=20000,
        charge_source="Chaos + earth runes (2 chaos + 5 earth per charge; combination runes accepted since 14 May 2025)",
        drain_event="1 charge per cast of the built-in spell",
        status="implemented",
        note="gear/warped_sceptre.rs2/.constant, fourth weapon on the shared powered-staff dispatch. "
             "CORRECTS this ledger's own prior claim of no obj in this cache -- re-verified directly "
             "against all.obj, same as Tumeken's shadow: warped_sceptre/warped_sceptre_uncharged are real, "
             "present records. Unlike Eye of ayak, the charged form itself carries ifop4=Charge, so a "
             "partially-drained sceptre can be topped up directly, not just charged fresh -- covered by its "
             "own selftest check and mutation-tested. Max hit floor((8*magic + 96) / 37), derived by solving "
             "the wiki's two stated data points (16 at level 62, 24 at level 99) against its own garbled "
             "formula text (a fraction rendered with no division bar); both endpoints check out exactly, not "
             "assumed. NOT implemented: 'combination rune' charging (accepted since 14 May 2025, only plain "
             "chaosrune/earthrune here) and the Wilderness on-death charge-protection carve-out. Full "
             "selftest re-run in both plain and MOCK230_GEARRUN=1 modes, same 12-failure pristine baseline "
             "before and after.",
    ),
    "Xeric's talisman": dict(
        storage="item_var", depletion="revert", max_charges=1000,
        charge_source="Lizardman fangs, 1 fang = 1 charge, used directly on the talisman",
        drain_event="1 charge per teleport to an unlocked Kourend destination, chosen from a Rub submenu",
        status="implemented",
        note="general/scripts/enchanted_jewellry/xeric_talisman.rs2/.constant, built on the SS_OP_LAST_SUBOP "
             "opcode added for Giantsoul amulet (see that entry for the mechanism). Destination coordinates "
             "came from the wiki's own TeleportLocationLine (x,y) pins, converted to "
             "plane_mapX_mapZ_localX_localZ and cross-checked against a real nearby spawn already in this "
             "tree for all 5 (Shayzien/Hosidius/Lovakengj/Kourend-guard/Quidamortem-minecart spawns each "
             "land within ~40 tiles of the computed square) rather than trusted from the pin math alone. "
             "Xeric's Honour (5th destination) has no unlock path in this tree -- no ancient tablet obj, no "
             "Chambers of Xeric raid content anywhere -- guarded to a clean refusal, mutation-tested, same "
             "shape as Giantsoul amulet's Royal Titans. Charging is opheldu (a fang used ON the talisman, "
             "not a Charge menu option -- the obj record has none, unlike Giantsoul amulet, confirmed "
             "directly rather than assumed uniform between the two). Uncharge (ifop5) returns the exact "
             "remaining count as unnoted fangs, matching Cowbell amulet's own shape. Teleport SUCCESS is "
             "untested for the established unsafe-in-::chargesrun reason (Chronicle/Cowbell amulet/"
             "Giantsoul amulet's own notes); destinations are five self-contained branches for the same "
             "reason Giantsoul amulet's are.",
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
        charge_source="Lumbridge & Draynor Diary reward tiers (easy/medium/hard/elite = rings 1-4); "
                       "refills daily -- corrected from an earlier pass's wrong 'Fremennik Trials' claim, "
                       "re-checked directly against the live wiki",
        drain_event="Actually THREE independent daily counters, not one: 30 free Low/High Alchemy casts "
                     "(ring 2+; High Alch needs ring 4), a separate run-energy-restore count (3/4/3 for "
                     "rings 2/3/4), and a separate cabbage-patch-teleport count (3/day on ring 2, "
                     "unlimited on ring 3+) -- not a Tutorial Island teleport, which this ledger's earlier "
                     "pass wrongly assumed",
        status="charges_only",
        note="SS_OP_DATE_RUNEDAY (§3b) has landed (see Falador shield, now implemented with it), so that is "
             "no longer the blocker. The real blocker: Low/High Level Alchemy is not implemented anywhere "
             "in this tree at all -- spell ids exist in skill_magic/configs/magic.constant but no .rs2 "
             "destroys an item and grants coins for either spell, so the ring's headline mechanic has "
             "nothing to hook into. The energy-restore and teleport counters are individually tractable "
             "(same daily-reset shape as Falador shield) but were not built standalone since they are not "
             "this family's defining mechanic.",
    ),
    "Falador shield": dict(
        storage="player_varp", depletion="none", max_charges=2,
        charge_source="Falador Achievement Diary reward tiers (easy/medium/hard/elite)",
        drain_event="1 use per Recharge Prayer click; max_charges/day is tier-dependent "
                     "(1/1/1/2 for easy/medium/hard/elite)",
        status="implemented",
        note="general/scripts/enchanted_jewellry/falador_shield.rs2/.constant/.varp. "
             "SS_OP_DATE_RUNEDAY (§3b) has landed since this note was last written -- "
             "selftest.rs2's own selftest_date_runeday already exercised it, this is the first content "
             "caller. Each tier restores a different percent of base prayer (25/50/100/100 for easy/"
             "medium/hard/elite, all verified per-tier from the live wiki rather than assumed uniform) "
             "as an add-capped-at-base, not a set-to-%. Account-scoped (player_varp), same reasoning "
             "Amulet of bounty/Dodgy necklace's own varp files give. \"Recharging at full prayer does not "
             "consume a daily use\" (30 Apr 2015) is a real guard, mutation-tested, as is the daily-cap "
             "guard itself. NOT implemented: Explorer's ring, which this ledger's neighbouring entry also "
             "named as needing this opcode -- that family additionally needs a working Low/High Level "
             "Alchemy spell, which does not exist anywhere in this tree's magic content (spell ids are "
             "declared in magic.constant but no .rs2 implements either spell), a materially larger gap "
             "than Falador shield's own self-contained prayer-restore mechanic.",
    ),
    # -- id_ladder + item_var hybrid: all 6 Barrows sets (24 pieces) ------
    "Dharok's helm": dict(
        storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows minigame reward; repaired via Bob/Dunstan dialogue or a player-owned-house armour stand",
        drain_event="1 point per 90 ticks (54s) while in combat, plus 1 immediately on entering combat; 250 points per quarter-tier",
        status="implemented",
        note="CORRECTED research error: the original note claimed the Barrows minigame not existing meant "
             "there was nowhere for degradation to fire from. Fetched the wiki's 'Barrows equipment' page "
             "directly (not cached under wiki/items/ -- it's a set-level page) and confirmed degradation is "
             "purely combat-time-based, everywhere in the game, with no dependency on the minigame at all: "
             "'immediately degrade by 1 point when the player enters combat, and deduct one point for every "
             "54 seconds (90 ticks) whilst still in combat.' Wired as a shared library, "
             "skill_combat/scripts/player/gear/barrows_degrade.rs2, covering all 6 sets x 4 pieces at once: "
             "a 120-case obj-id chain (5 tiers x 24 pieces) plus a per-piece item_var point counter (0-250 "
             "within the current quarter -- unset=0=quarter just started, same counts-up convention this "
             "session's other player_varps use). The fresh(unnamed)->100 transition is free/instant on first "
             "combat entry, matching the wiki's own wording; the 4 transitions after that (100->75->50->25->0) "
             "cost 250 points each, totalling 1000 points = 15 hours of combat exactly. Entering combat is "
             "hooked from BOTH player_hit_npc_prepare (attacking) and combat_stats.rs2's playerhit_n_melee "
             "(being attacked, including a miss or a fully-prayed 0 -- wiki explicit this still counts) via a "
             "settimer/cleartimer repeating 90-tick timer that self-stops once no combat activity has "
             "happened in the last 90 ticks. All 4 pieces in a worn set degrade independently and "
             "simultaneously. NOT implemented: repairing (Bob/Dunstan NPC dialogue, or the player-owned-"
             "house armour stand) -- this only tracks and applies degradation, the same 'charges, not the "
             "surrounding feature' scope this session's other items use for their own gaps. A piece that "
             "reaches 0 stays worn until removed (wiki explicit), not force-unequipped.",
    ),
    "Dharok's platebody": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Dharok's platelegs": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Dharok's greataxe": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Ahrim's hood": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Ahrim's robetop": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Ahrim's robeskirt": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Ahrim's staff": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Guthan's helm": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Guthan's platebody": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Guthan's chainskirt": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Guthan's warspear": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Karil's coif": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Karil's leathertop": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Karil's leatherskirt": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Karil's crossbow": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Torag's helm": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Torag's platebody": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Torag's platelegs": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Torag's hammers": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Verac's helm": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Verac's brassard": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Verac's plateskirt": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Verac's flail": dict(storage="id_ladder", depletion="degrade_step", max_charges=100,
        charge_source="Barrows", drain_event="Same as Dharok's helm", status="implemented",
        note="Same shared library as Dharok's helm; see its note."),
    "Slayer ring": dict(
        storage="id_ladder", depletion="degrade_step", max_charges=8,
        charge_source="Made from Enchanted gem via the Slayer ring recipe",
        drain_event="1 charge per teleport (5 Slayer dungeons via Rub, no Wilderness rub-teleport on this "
                     "cache's own subaction list)",
        status="charges_only",
        note="general/scripts/enchanted_jewellry/slayer_ring.rs2 now implements the 8-tier degrade ladder "
             "(mirroring ring_of_dueling.rs2's own pre-existing switch shape) and Check, both directly "
             "tested. Still charges_only: none of the five teleport destinations (Stronghold Slayer Cave/ "
             "Slayer Tower/Fremennik Slayer Dungeon/Tarn's Lair/Dark Beasts) has a coordinate groundable "
             "against real spawn data in this tree (only one weak single-NPC match turned up for one of "
             "the five, not enough to trust), so the degrade step this file implements is currently "
             "unreachable through real play -- guarded to a clean refusal instead. Crafting a ring at all "
             "is also unimplemented: the `enchanted_gem` obj this recipe needs, and the obj a ring reverts "
             "to at 0 charges, does not exist anywhere in this cache (checked directly under several likely "
             "names) -- reverting at 0 deletes the ring outright rather than guessing a gem obj id. Spawn a "
             "slayer_ring_8 to test the degrade ladder.",
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
