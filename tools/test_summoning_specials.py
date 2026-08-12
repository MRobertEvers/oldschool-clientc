#!/usr/bin/env python3
"""Regression gate for the first transaction-safe Summoning specials."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from summoning_script_sources import definition, read_all, script_dir


REPO = Path(__file__).resolve().parents[1]
MOCK_HOST = REPO / "src/net/mock/mock230_scripts.c"
MOCK_CONTENT = REPO / "src/net/mock/mock230_content.c"
MOCK_INV = REPO / "src/net/mock/mock230_ops_inv.c"
MOCK_WORLD = REPO / "src/net/mock/mock230_world.c"
MOCK_HEADER = REPO / "src/net/mock/mock230.h"
MOCK_COMBAT = REPO / "src/net/mock/mock230_combat.c"
PLAYER_RANGED = REPO / "OSRS-Content/osrs239-content/server/scripts/skill_combat/scripts/player/player_ranged.rs2"
INTERFACE = REPO / (
    "OSRS-Content/osrs239-content/ported/scape2009_summoning/"
    "interfaces/summoning_familiar.if"
)
SPECIAL_MANIFEST = REPO / "docs/summoning_port/special_move_assets_530.ini"
COMBAT_PROFILES = REPO / (
    "OSRS-Content/osrs239-content/server/scripts/ported_scape2009_summoning/"
    "configs/summoning_special_combat.npc"
)


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    try:
        scripts = script_dir()
        text = read_all(scripts)
        mock_host = MOCK_HOST.read_text(encoding="utf-8")
        mock_content = MOCK_CONTENT.read_text(encoding="utf-8")
        mock_inv = MOCK_INV.read_text(encoding="utf-8")
        mock_world = MOCK_WORLD.read_text(encoding="utf-8")
        mock_header = MOCK_HEADER.read_text(encoding="utf-8")
        mock_combat = MOCK_COMBAT.read_text(encoding="utf-8")
        player_ranged = PLAYER_RANGED.read_text(encoding="utf-8")
        interface = INTERFACE.read_text(encoding="utf-8")
        special_manifest = SPECIAL_MANIFEST.read_text(encoding="utf-8")
        combat_profiles = COMBAT_PROFILES.read_text(encoding="utf-8")
        execute = definition(scripts, "proc,summoning_familiar_special_execute")
        target_execute = definition(scripts, "proc,summoning_familiar_special_target_execute")
        xp = definition(scripts, "proc,summoning_familiar_special_xp")
        target_kinds = definition(scripts, "proc,summoning_familiar_special_target_kinds")
        handler = definition(scripts, "if_button1,summoning_familiar:special")
        commit = definition(scripts, "proc,summoning_familiar_special_commit")
        validate = definition(scripts, "proc,summoning_familiar_special_validate")

        expected_xp = {
            1: 1, 2: 1, 3: 8, 4: 2, 5: 2, 6: 2, 7: 5, 8: 2, 9: 9, 10: 11, 11: 23, 12: 25, 13: 6, 14: 29, 15: 11,
            16: 16, 17: 7, 18: 7, 19: 7, 20: 7, 21: 7,
            22: 6, 23: 8, 24: 21, 25: 9, 26: 9, 27: 9, 28: 9, 29: 9, 30: 9, 31: 9,
            32: 23, 33: 47, 34: 24, 35: 11, 36: 55, 37: 11, 38: 57, 39: 57, 40: 57, 41: 58, 42: 30, 43: 19, 44: 31, 45: 25, 46: 10, 47: 7, 49: 14, 50: 15, 51: 11, 52: 50, 53: 73, 56: 15, 57: 110, 58: 16,
            48: 14, 54: 37, 55: 37, 59: 79, 60: 79, 61: 79, 62: 16, 63: 41, 64: 83, 65: 43, 66: 36, 67: 46,
            68: 56, 69: 66, 70: 76, 71: 86, 72: 18, 73: 89, 74: 45, 75: 19, 76: 47, 77: 48, 78: 49,
        }
        execute_types = {
            int(value)
            for condition in re.findall(r"if \(([^\n]*)\) \{", execute)
            for value in re.findall(r"\$type = (\d+)", condition)
        }
        target_execute_types = {
            int(value)
            for condition in re.findall(r"if \(([^\n]*)\) \{", target_execute)
            for value in re.findall(r"\$type = (\d+)", condition)
        }
        xp_rows = {
            int(kind): int(value)
            for kind, value in re.findall(r"if \(\$type = (\d+)\) return\((\d+)\);", xp)
        }
        reconstructed = {16, 50, 52, 56, 58, 64, 65}
        expect(execute_types | target_execute_types == set(expected_xp),
               "implemented special handlers drifted from their XP rows")
        scorpion = execute[execute.index("if ($type = 9)"):execute.index("if ($type = 11)")]
        expect("npc_var_set(^summoning_npcvar_charged_attack, 1);" in scorpion and
               "return(true);" in scorpion,
               "Spirit Scorpion does not arm its generation-local charge")
        expect(xp_rows == expected_xp, "special XP is not the configured tenths-of-XP value")
        expect("if (~summoning_familiar_special_execute(%summoning_familiar_type) = false) return;" in handler,
               "special resources can be committed before its operation accepts")
        expect("~summoning_familiar_special_commit(%summoning_familiar_type);" in handler,
               "the immediate path does not use the common special commit")
        expect("DreadfowlNPC.java: Dreadfowl Strike" in execute and
               "npc_findcombat = false" in execute and
               "summoning_special_move_dreadfowl_strike_projectile" in execute and
               "if ($distance > 8)" in execute and
               "npc_finduid($familiar) = false | npc_finduid($target) = false" in execute,
               "Dreadfowl Strike lacks combat-target validation, its admitted projectile, or generation revalidation")
        expect(execute.count("if ($distance > 8)") >= 3,
               "projectile combat specials no longer reject a target beyond the source eight-tile limit")
        expect("npc_combat_stat(npc_stat(magic), npc_param(magicattack))" in execute and
               "npc_defence_roll(^magic_style)" in execute and "randominc(3)" in execute,
               "Dreadfowl Strike does not use the familiar's magic roll and source max hit")
        expect("ThornySnailNPC.java: Slime Spray" in execute and
               "summoning_special_move_thorny_snail_slime_spray_projectile" in execute and
               "summoning_special_move_thorny_snail_slime_spray_impact_gfx" in execute and
               "randominc(8)" in execute,
               "Slime Spray lacks its source projectile, impact graphic, or max hit")
        expect("DesertWyrmNPC.java: Electric Lash" in execute and
               "summoning_special_move_desert_wyrm_electric_lash_projectile" in execute and
               "randominc(5)" in execute,
               "Electric Lash lacks its source projectile or max hit")
        expect("SpiritTzKihNPC.java: Fireball Assault" in execute and
               "npc_huntall($owner_coord, 8, 0);" in execute and
               "if ($examined < 2)" in execute and
               "~summoning_familiar_npc_area_target($familiar) = true" in execute and
               "random(7)" in execute and
               "npc_queue(2, $damage, add(2, divide($distance, 2)));" in execute and
               "summoning_special_move_spirit_tz_kih_fireball_assault" in execute,
               "Fireball Assault lacks its source two-candidate bound, shared NPC predicate, delayed hit, or visual")
        expect("[proc,summoning_familiar_npc_area_target]" in text and
               "if (npc_hasop(2) = false) return(false);" in text and
               "if (npc_range($source) > 8) return(false);" in text,
               "bounded familiar AoEs do not share the NPC attackability and familiar-range predicate")
        expect("SpiritKalphiteNPC.java (pre-disable): Sandstorm" in execute and
               "npc_huntall($source, 6, 0);" in execute and
               "summoning_special_move_spirit_kalphite_sandstorm" in execute and
               "summoning_special_move_spirit_kalphite_sandstorm_projectile" in execute and
               "if ($count >= 6)" in execute and "randominc(20)" in execute and
               "npc_queue(2, $damage, add(2, divide($distance, 2)));" in execute,
               "Sandstorm lacks its source pulse, bounded NPC target set, visual closure, or delayed magic hit")
        expect("GiantChinchompaNPC.java: Explode" in execute and
               "npc_say(\"Squeak!\");" in execute and
               "summoning_special_move_giant_chinchompa_explode" in execute and
               "p_delay(3);" in execute and "npc_huntall($owner_coord, 6, 0);" in execute and
               "npc_damage(hitsplat_damage, random(13));" in execute and
               "npc_var_set(^summoning_npcvar_dismiss_after_special, 1);" in execute and
               "npc_var_get(^summoning_npcvar_dismiss_after_special) = 1" in text,
               "Explode lacks its source delayed burst, generation-local teardown, or source visual closure")
        expect("SmokeDevilNPC.java: Dust Cloud" in execute and
               "npc_huntall($source, 1, 0);" in execute and
               "summoning_special_move_smoke_devil_dust_cloud" in execute and
               "spotanim_npc(summoning_special_move_geyser_titan_boil_gfx, 0, 0);" in execute and
               "npc_damage(hitsplat_damage, random(6));" in execute,
               "Dust Cloud lacks its source one-tile NPC burst, visual, or manual hit range")
        expect("VampireBatNPC.java: Vampyre Touch" in execute and
               "randominc(11)" in execute and "randominc(9) < 4" in execute and
               "if (npc_range(npc_coord) > 8)" in execute and
               "stat_heal(hitpoints, 2, 0);" in execute,
               "Vampyre Touch lacks its manual hit or 40% owner-heal branch")
        expect("ArcticBearNPC.java: Arctic Blast" in execute and
               "summoning_special_move_arctic_bear_arctic_blast_projectile" in execute and
               "summoning_special_move_arctic_bear_arctic_blast_impact_gfx" in execute and
               "randominc(15)" in execute,
               "Arctic Blast lacks its source projectile, delayed impact graphic, or maximum hit")
        expect("GraniteLobsterNPC.java: Crushing Claw" in execute and
               "summoning_special_move_granite_lobster_crushing_claw_projectile" in execute and
               "60, 40, 1, 45, 46, 32, 5" in execute and "randominc(14)" in execute,
               "Crushing Claw lacks its source ranged projectile or maximum hit")
        expect("MagpieNPC.java: Thieving Fingers" in execute and
               "summoning_special_move_magpie_thieving_fingers" in execute and
               "stat_add(thieving, 2, 0);" in execute,
               "Thieving Fingers lacks its source familiar visual or +2 Thieving boost")
        expect("BarkerToadNPC.java: Toad Bark" in execute and
               "summoning_special_move_barker_toad_toad_bark_impact_gfx" in execute and
               "randominc(8)" in execute,
               "Toad Bark lacks its source impact graphic or maximum hit")
        expect("TalonBeastNPC.java: Deadly Claw (reconstructed source gap)" in execute and
               "summoning_special_move_talon_beast_deadly_claw" in execute and
               execute.count("npc_queue(2, $first, add(2, divide($distance, 2)));") == 1,
               "Deadly Claw lacks its admitted attack sequence or first delayed hit")
        talon = execute[execute.index("if ($type = 57)"):execute.index("if ($type = 45)")]
        expect(talon.count("npc_queue(2,") == 3 and talon.count("randominc(8)") == 3 and
               "npc_combat_stat(npc_stat(magic), npc_param(magicattack))" in talon and
               "npc_defence_roll(^magic_style)" in talon,
               "Deadly Claw does not retain three independently accurate delayed magic hits")
        spike_shot = execute[execute.index("if ($type = 63)"):execute.index("if ($type = 25)")]
        expect("SpiritDagannothNPC.java: Spike Shot (reconstructed source gap)" in spike_shot and
               "summoning_special_move_spirit_dagannoth_spike_shot" in spike_shot and
               "npc_combat_stat(npc_stat(ranged), npc_param(rangeattack))" in spike_shot and
               "npc_defence_roll(^ranged_style)" in spike_shot and "randominc(18)" in spike_shot and
               "if ($damage > 0) npc_freeze(4);" in spike_shot,
               "Spike Shot lacks its ranged source profile, max hit, admitted visual, or landed stun")
        doomsphere = execute[execute.index("if ($type = 41)"):execute.index("if ($type = 10)")]
        expect("KaramthulhuOverlordNPC.java: Doomsphere Device (reconstructed source gap)" in doomsphere and
               "summoning_special_move_karamthulhu_doomsphere_device" in doomsphere and
               "npc_combat_stat(npc_stat(magic), npc_param(magicattack))" in doomsphere and
               "npc_defence_roll(^magic_style)" in doomsphere and "randominc(16)" in doomsphere,
               "Doomsphere Device lacks its period magic profile, max hit, or admitted visual")
        mantis = execute[execute.index("if ($type = 55)"):execute.index("if ($type = 10)")]
        expect("PrayingMantisNPC.java: Mantis Strike (reconstructed source gap)" in mantis and
               "summoning_special_move_praying_mantis_mantis_strike" in mantis and
               "npc_combat_stat(npc_stat(magic), npc_param(magicattack))" in mantis and
               "npc_defence_roll(^magic_style)" in mantis and "randominc(17)" in mantis and
               "npc_statsub(prayer, 3, 0);" in mantis and "if ($damage > 0)" in mantis and
               "npc_freeze(4);" in mantis,
               "Mantis Strike lacks its bounded magic hit, landed Prayer drain, bind, or admitted visual")
        expect("GeyserTitanNPC.java: Boil" in execute and
               "summoning_special_move_geyser_titan_boil_projectile" in execute and
               "spotanim_npc(summoning_special_move_geyser_titan_boil_gfx, 315, 0);" in execute and
               "divide($defbonus, 40)" in execute and "randominc($maxhit)" in execute,
               "Boil lacks its source defence-based maximum hit, projectile, or impact")
        expect("CockatriceFamiliarNPC.java: Petrifying Gaze" in execute and
               "summoning_special_move_petrifying_gaze_projectile" in execute and
               "npc_statsub($drainstat, 3, 0);" in execute and
               "if ($type = 25) $drainstat = defence;" in execute and
               "if ($type = 27) $drainstat = prayer;" in execute and
               "if ($type = 30) $drainstat = summoning;" in execute and
               "randominc(10)" in execute,
               "Petrifying Gaze lacks its shared skill-drain mapping, projectile, or source maximum hit")
        expect("MinotaurFamiliarNPC.java: Bull Rush" in execute and
               "summoning_special_move_bull_rush_projectile" in execute and
               "if ($type = 71) $maxhit = 20;" in execute and
               "random($maxhit)" in execute and "npc_freeze(4)" in execute and
               "$type != 66 & $type != 71 & random(10) < 6" in execute,
               "Bull Rush lacks its shared max-hit mapping, delayed stun, or admitted projectile")
        expect("EvilTurnipNPC.java: Evil Flames" in execute and
               "npc_statheal(hitpoints, 2, 0);" in execute and
               "npc_statsub(magic, 1, 0);" in execute and
               "summoning_special_move_evil_turnip_evil_flames_projectile" in execute,
               "Evil Flames lacks its source familiar heal, Magic drain, or projectile")
        expect("AbyssalParasiteNPC.java: Abyssal Drain" in execute and
               "npc_statsub(prayer, add(1, random(3)), 0);" in execute and
               "summoning_special_move_abyssal_parasite_abyssal_drain_projectile" in execute and
               "randominc(7)" in execute,
               "Abyssal Drain lacks its source prayer drain, projectile, or maximum hit")
        expect("SpiritJellyNPC.java: Dissolve" in execute and
               "npc_statsub(attack, 3, 0);" in execute and
               "summoning_special_move_spirit_jelly_dissolve_projectile" in execute and
               "randominc(13)" in execute,
               "Dissolve lacks its source Attack drain, projectile, or maximum hit")
        expect("SpiritLarupiaNPC.java: Rending" in execute and
               "npc_statsub(strength, 1, 0);" in execute and
               "summoning_special_move_rending_projectile" in execute and
               "random(10)" in execute,
               "Rending lacks its one-below-base Strength drain, projectile, or source maximum hit")
        expect("AbyssalTitanNPC.kt: Essence Shipment" in execute and
               "inv_total(summoning_bob, summoning_special_move_rune_essence)" in execute and
               "inv_freespace(bank) < $needed" in execute and
               "inv_moveitem_uncert(summoning_bob, bank, summoning_special_move_pure_essence, $bob_pure);" in execute and
               "summoning_special_move_abyssal_titan_essence_shipment" in execute,
               "Essence Shipment lacks its BoB totals, combined bank preflight, atomic moves, or visual")
        expect("BunyipNPC.java: Swallow Whole" in target_execute and
               "~summoning_bunyip_cooking_level(last_item)" in target_execute and
               "inv_delslot(inv, last_slot);" in target_execute and
               "summoning_special_move_bunyip_swallow_whole" in target_execute,
               "Swallow Whole lacks its source cooking gate, exact selected-item removal, or familiar visual")
        expect("PackYakNPC.java: Winter Storage" in target_execute and
               "oc_param(last_item, unbankable) = 1" in target_execute and
               "inv_itemspace2(bank, $bankitem, 1, ^max_32bit_int)" in target_execute and
               "inv_moveitem_uncert(inv, bank, last_item, 1);" in target_execute and
               "summoning_special_move_pack_yak_winter_storage_gfx" in target_execute,
               "Winter Storage lacks its bankability check, atomic note-aware move, capacity preflight, or visual")
        expect("BeaverNPC.java: Multichop" in target_execute and
               "db_find(woodcutting_trees:tree, $tree_type);" in target_execute and
               "if (npc_range($tree) > 5)" in target_execute and
               "p_delay($travel);" in target_execute and "p_delay(11);" in target_execute and
               "~summoning_beaver_log(random(11))" in target_execute,
               "Multichop lacks its source tree gate, range, timed window, or log rewards")
        expect("SpiritKyattNPC.java: Ambush" in execute and
               "npc_tele(movecoord(coord, 1, 0, 0));" in execute and
               "npc_setmode(playerfollow);" in execute,
               "Ambush lacks its source combat gate or familiar call placement")
        expect("SpiritSpiderNPC.java: Egg Spawn" in execute and
               "randominc(8)" in execute and "map_loc($tile) = false" in execute and
               "obj_add_private($tile, red_spiders_eggs, 1, ^lootdrop_duration, 100);" in execute and
               "summoning_special_move_spirit_spider_egg_spawn" in execute,
               "Egg Spawn lacks source count/tile guards, private drops, or its admitted animation")
        expect(commit.index("inv_del(inv, $scroll, 1);") < commit.index("stat_advance(summoning,"),
               "Summoning XP is not part of the successful resource commit")
        expect("~summoning_familiar_special_validate($type)" in text and
               "~summoning_familiar_special_commit($type);" in text,
               "the targeted path is not validate/execute/revalidate/commit")
        expect("stat_add(defence, 4, 0);" in execute, "Stony Shell's Defence boost is missing")
        for visual in (
            "summoning_special_move_spirit_terrorbird_tireless_run",
            "summoning_special_move_granite_crab_stony_shell",
            "summoning_special_move_albino_rat_cheese_feast",
            "summoning_special_move_war_tortoise_testudo",
            "summoning_special_move_wolpertinger_magic_focus",
            "summoning_special_move_obsidian_golem_volcanic_strength_gfx",
        ):
            expect(visual in execute, f"source familiar visual missing: {visual}")
        expect("healenergy(calc(divide(stat_base(agility), 2) * 100));" in execute,
               "Tireless Run's Agility-scaled energy restoration is missing")
        expect("if (runenergy >= 100)" in execute and
               "You already have full run energy." in execute,
               "Unburden does not reject a full run-energy bar")
        expect("if (inv_freespace(inv) < 4)" in execute and
               "inv_add(inv, cheese, 4);" in execute,
               "Cheese Feast does not atomically produce four non-stackable cheeses")
        expect("MacawNPC.java: Herbcall" in execute and
               "npc_var_get(^summoning_npcvar_special_cooldown) > map_clock" in execute and
               "p_delay(5);" in execute and "obj_add_private(npc_coord, ~summoning_macaw_herb(random(15)), 1," in execute,
               "Herbcall lacks its live-familiar cooldown, delayed private drop, or source herb roll")
        expect("FruitBatNPC.java: Fruitfall" in execute and
               "~summoning_fruit_bat_free_square($used)" in execute and
               "~summoning_fruit_bat_fruit(random(17))" in execute and
               "npc_anim(summoning_special_move_fruit_bat_fruitfall_start, 0);" in execute and
               "p_delay(3);" in execute and "p_delay(1);" in execute,
               "Fruitfall lacks its source cooldown, staged visual, private fruit table, or unique squares")
        expect("BloatedLeechNPC.java: Blood Drain" in execute and
               "~clear_poison;" in execute and
               "~summoning_bloated_leech_restore_all;" in execute and
               "damage(uid, hitsplat_damage, add(random(4), 1));" in execute and
               "[proc,summoning_bloated_leech_restore_all]" in text,
               "Blood Drain lacks its poison cure, all-skill restoration, or source self-hit")
        expect("IbisNPC.java: Fish Rain" in execute and
               "summoning_special_move_ibis_fish_rain" in execute and
               "p_delay(3);" in execute and "while ($left > 0)" in execute and
               "obj_add_private($tile, ~summoning_ibis_fish(random(4)), 1," in execute,
               "Fish Rain lacks its delayed familiar animation or private source fish drops")
        expect("SpiritCobraNPC.java: Ophidian Incubation" in text and
               "last_item = summoning_special_move_spirit_cobra_cockatrice_egg" in text and
               "inv_setslot(inv, last_slot, summoning_special_move_spirit_cobra_vulatrice, 1);" in text and
               "You can't use the special move on this item." in text,
               "Ophidian Incubation lacks its exact held-item validation or source slot replacement")
        expect("PyreLordNPC.java: Immense Heat" in target_execute and
               "last_item != gold_bar" in target_execute and
               "~jewellery_open_gold_menu;" in target_execute,
               "Immense Heat does not validate its gold-bar target or enter the jewellery flow")
        expect("SpiritWolfNPC.java: Howl" in text and
               "def_npc_uid $target = npc_uid;" in target_execute and
               "summoning_special_move_spirit_wolf_howl_projectile" in target_execute and
               "p_delay(2);" in target_execute and
               "npc_finduid($familiar) = false | npc_finduid($target) = false" in target_execute and
               "npc_walk(~movecoord_indirection(npc_coord, ~coord_direction($source, npc_coord), 3));" in target_execute,
               "Spirit Wolf Howl lacks selected-target preservation, source visuals, or delayed intimidation")
        expect("HydraNPC.java: Regrowth" in target_execute and
               "~farming_tree_stump_state($row)" in target_execute and
               "~farming_tree_set($patch, ~farming_tree_chop_state($row));" in target_execute,
               "Hydra Regrowth does not preserve the source stump-only tree transition")
        expect("UnicornStallionNPC.java: Healing Aura" in target_execute and
               ".stat_heal(hitpoints, 0, 15);" in target_execute and
               "summoning_special_move_unicorn_stallion_healing_aura_gfx" in target_execute,
               "Healing Aura does not retain its selected-player 15% source heal or familiar visual")
        expect("HoneyBadgerNPC.java + 2011 wiki: Insane Ferocity" in execute and
               "stat_add(attack, divide(multiply(stat_base(attack), 15), 100), 0);" in execute and
               "stat_sub(defence, divide(multiply(stat_base(defence), 15), 100), 0);" in execute and
               "stat_sub(magic, 5, 0);" in execute and
               "stat_sub(ranged, divide(multiply(stat_base(ranged), 10), 100), 0);" in execute and
               "npc_var_set(^summoning_npcvar_charged_attack, 1);" in execute and
               "[proc,summoning_honey_badger_charged_attack_tick]" in text and
               "if (random(2) = 0) $double = true;" in text,
               "Insane Ferocity lacks its reconstructed stat formula or one-use 50% double swing")
        expect("RavenousLocustNPC.java: Famine" in target_execute and
               "~food_data($candidate) ! null" in target_execute and
               "summoning_special_move_ravenous_locust_famine_target_gfx" in target_execute and
               "p_delay(2);" in target_execute and "p_delay(1);" in target_execute and
               "inv_delslot(inv, $food_slot);" in target_execute,
               "Famine lacks slot-ordered food membership, three-tick choreography, or one-unit removal")
        expect("Phoenix: Rise From the Ashes (period reconstruction)" in target_execute and
               "obj_type != ashes" in target_execute and
               "divide(calc(50 * sub($maximum, $before)), $maximum)" in target_execute and
               "npc_statheal(hitpoints, 0, 100);" in target_execute and
               "npc_huntall($ash_coord, 1, 0);" in target_execute and
               "if ($count < 5" in target_execute and
               "summoning_cohort_phoenix_seq_11092" in target_execute,
               "Rise From the Ashes lacks its selected-ashes transaction, inverse-health cap, full heal, or bounded burst")
        forge = target_execute[target_execute.index("if ($type = 56"):target_execute.index("if ($type = 58")]
        expect("ForgeRegentNPC.java: Inferno" in forge and
               ".inv_freespace(inv) < 1" in forge and
               "p_delay(2);" in forge and
               "damage($target, hitsplat_damage, randominc(20));" in forge and
               "inv_delslot(worn, $slot);" in forge and
               "inv_add(inv, $remove, 1);" in forge and
               "summoning_special_move_forge_regent_inferno_target_gfx" in forge,
               "Inferno lacks its delayed max-20 magic hit or atomic one-item disarm")
        ent = target_execute[target_execute.index("if ($type = 58"):target_execute.index("if ($type = 64")]
        expect("GiantEntNPC: Acorn Missile (period reconstruction)" in ent and
               "npc_huntall($anchor, 1, 0);" in ent and "if ($count < 3" in ent and
               "randominc(17)" in ent and "random(4) = 0" in ent and
               "obj_add_private(npc_coord, acorn, 1" in ent and
               "summoning_special_move_giant_ent_acorn_missile_projectile" in ent and
               "summoning_special_move_giant_ent_acorn_missile_impact_gfx" in ent,
               "Acorn Missile lacks its reconstructed three-target/max-17/acorn contract or assets")
        lava = target_execute[target_execute.index("if ($type = 64"):target_execute.index("if ($type = 65")]
        expect("LavaTitanNPC: Ebon Thunder (period reconstruction)" in lava and
               "randominc(14)" in lava and
               "%sa_energy = max(sub(%sa_energy, 100), 0);" in lava and
               "summoning_special_move_geyser_titan_boil" in lava and
               "summoning_special_move_lava_titan_ebon_thunder_gfx" in lava,
               "Ebon Thunder lacks its max-14 magic hit, exact ten-point drain, or later-source visuals")
        swamp = target_execute[target_execute.index("if ($type = 65"):target_execute.index("if ($type = 13")]
        expect("SwampTitanNPC: Swamp Plague (period reconstruction)" in swamp and
               "npc_huntall($anchor, 1, 0);" in swamp and "if ($count < 6" in swamp and
               "randominc(20)" in swamp and "random(4) = 0" in swamp and
               "npc_poison(29);" in swamp and
               "summoning_special_move_swamp_titan_swamp_plague_projectile" in swamp,
               "Swamp Plague lacks its reconstructed area/max-20/25%-severity-29 poison contract")
        expect("[proc,summoning_reconstructed_pvp_allowed]" in text and
               "map_multiway($owner) = false" in text and
               "~wilderness_level($owner)" in text and
               "distance($familiar, $target) > 8" in text and
               "abs(sub($owner_level, $target_level))" in text,
               "reconstructed player-target specials lack their shared Wilderness authorization policy")
        expect("if ($type = 50 | $type = 56) return(2);" in target_kinds and
               "if ($type = 52) return(8);" in target_kinds and
               "if ($type = 58 | $type = 64 | $type = 65) return(3);" in target_kinds,
               "the six reconstructed targeted rows do not open their exact target cursors")
        expect(all(str(kind) in execute + target_execute + target_kinds for kind in reconstructed),
               "a reconstructed row is absent from the executable surface")
        expect("SpiritMosquitoNPC.java: Pester" in execute and
               "~npc_melee_attack_roll(^stab_style)" in execute and
               "~npc_melee_maxhit" in execute and
               "summoning_special_move_spirit_mosquito_pester_gfx" in execute,
               "Spirit Mosquito Pester does not use its source melee profile and special visual")
        mosquito_profile = combat_profiles[
            combat_profiles.index("[summoning_cohort_spirit_mosquito_spirit_mosquito]"):
        ]
        expect("attack=28" in mosquito_profile and "strength=24" in mosquito_profile and
               "defence=25" in mosquito_profile and "param=stabattack,45" in mosquito_profile and
               "param=strengthbonus,53" in mosquito_profile,
               "Spirit Mosquito's source combat profile is incomplete")
        kalphite_profile = combat_profiles[
            combat_profiles.index("[summoning_cohort_spirit_kalphite_spirit_kalphite]"):]
        expect("hitpoints=350" in kalphite_profile and "attack=25" in kalphite_profile and
               "strength=25" in kalphite_profile and "defence=25" in kalphite_profile and
               "magic=25" in kalphite_profile and "ranged=25" in kalphite_profile,
               "Spirit Kalphite's source combat profile is incomplete")
        expect("SSVM_Active(state, SSVM_ENT_PLAYER)" in mock_inv and
               "SSVM_SECONDARY, &srv->players[player_slot]" in mock_host,
               "targeted-player dotted container operations no longer use the selected player")
        expect("CompostMoundNPC.java: Generate Compost" in target_execute and
               "~farming_compost_set_count($bin, ^farming_compost_capacity);" in target_execute and
               "random(10) != 1" in target_execute and
               "~farming_compost_set_next($bin, calc(date_minutes + ^farming_compost_rot_mins + random(^farming_compost_rot_span)));" in target_execute and
               "summoning_special_move_compost_mound_generate_compost" in target_execute,
               "Generate Compost does not preserve the source fill, quality, close, and visual behavior")
        expect("obj_base=47501" in special_manifest,
               "special-move object imports can collide with the existing Summoning allocation lane")
        expect("case SS_OP_RUNENERGY:" in mock_host and
               "SSVM_PushInt(state, player->run_energy / 100);" in mock_host,
               "the script-visible runenergy getter is not backed by the host")
        expect("stat_add(agility, 4, 0);" in execute and "stat_add(thieving, 4, 0);" in execute,
               "Abyssal Stealth's dual boost is missing")
        expect("add(stat_base(defence), 9)" in execute and "add(stat_base(magic), 7)" in execute,
               "capped Testudo or Magic Focus behavior is missing")
        expect("ObsidianGolemNPC.java: Volcanic Strength" in execute and
               "spotanim_npc(summoning_special_move_obsidian_golem_volcanic_strength_gfx, 0, 0);" in execute,
               "Volcanic Strength lacks the source familiar graphic")
        expect("if ($type = 59 | $type = 60 | $type = 61)" in execute,
               "the Titan's Constitution family is not shared")
        normal_tick = definition(scripts, "proc,summoning_familiar_normal_combat_tick")
        expect("%summoning_familiar_type != 76" in normal_tick and
               "map_multiway(coord)" in normal_tick and
               "npc_findcombat = false" in normal_tick and
               "npc_finduid($familiar) = false" in normal_tick and
               "npc_walk($target_coord)" in normal_tick and
               "npc_var_set(^summoning_npcvar_charged_attack, 0)" in normal_tick and
               "npc_queue(2, $extra_one, 0);" in normal_tick and
               "npc_queue(2, $extra_two, 0);" in normal_tick,
               "Iron Titan lacks a generation-safe multiway normal swing or its two charged hits")
        expect("IronTitanNPC.java: Iron Within" in execute and
               "summoning_special_move_iron_titan_swing" in execute and
               "summoning_special_move_iron_titan_iron_within_gfx" in execute,
               "Iron Within lacks its source charge state or admitted visuals")
        expect("SpiritScorpionNPC.java: Venom Shot" in execute and
               "npc_poison(1);" in text and
               "bronze_dart, iron_dart, steel_dart, mithril_dart, adamant_dart, rune_dart" in text and
               "bronze_javelin, iron_javelin, steel_javelin, mithril_javelin, adamant_javelin, rune_javelin" in text and
               "case bronze_knife" in text and
               "summoning_special_move_spirit_scorpion_venom_shot_projectile" in execute,
               "Venom Shot lacks its strict source weapon family, one-shot poison, or asset closure")
        expect("~summoning_spirit_scorpion_adjust_ranged_battle($rhand, $spirit_scorpion_target);" in player_ranged and
               player_ranged.index("~summoning_spirit_scorpion_adjust_ranged_battle") >
               player_ranged.index("~player_npc_hit_roll(%damagetype)") and
               player_ranged.index("~summoning_spirit_scorpion_adjust_ranged_battle") <
               player_ranged.index("~player_hit_npc_prepare"),
               "Spirit Scorpion's source battle adjustment is not between ranged state creation and impact preparation")
        expect("case SS_OP_NPC_POISON:" in mock_host and
               "poison_severity" in mock_header and "poison_source_gen" in mock_header and
               "mock230_combat_npc_poison_tick" in mock_world and
               "damage = (npc->poison_severity + 4) / 5;" in mock_combat and
               "npc->poison_clock = srv->tick + 30;" in mock_combat,
               "NPC poison lacks its owner-attributed 30-tick source severity timer")
        expect("StrangerPlantNPC.java: Poisonous Blast" in execute and
               "random(2) = 1) npc_poison(20);" in execute and
               "summoning_special_move_stranger_plant_poisonous_blast" in execute and
               "summoning_special_move_stranger_plant_poisonous_blast_projectile" in execute and
               "summoning_special_move_stranger_plant_poisonous_blast_impact_gfx" in execute and
               "npc_queue(2, $damage, add(2, divide($distance, 2)));" in execute,
               "Poisonous Blast lacks its corrected transaction, poison, visual, or delayed familiar hit")
        steel_tick = definition(scripts, "proc,summoning_steel_titan_normal_combat_tick")
        expect("SteelTitanNPC.java: Steel of Legends" in execute and
               "summoning_special_move_steel_titan_steel_of_legends_gfx" in execute and
               "summoning_steel_titan_normal_combat_tick" in normal_tick and
               "npc_var_set(^summoning_npcvar_steel_next_style, random(3));" in steel_tick and
               "npc_queue(2, $extra_three, $impact_delay);" in steel_tick and
               "summoning_special_move_steel_titan_projectile" in steel_tick,
               "Steel of Legends lacks its one-shot state, style cycle, three extras, or projectile")
        graahk_tick = definition(scripts, "proc,summoning_spirit_graahk_normal_combat_tick")
        expect("SpiritGraahkNPC.kt: Goad" in target_execute and
               "if ($type = 40) return(1);" in text and
               "npc_var_set(^summoning_npcvar_normal_attack_target, $target);" in target_execute and
               "npc_findcombat = false & $already_attacking = 0" in target_execute and
               "~summoning_familiar_npc_area_target($familiar) = false" in target_execute and
               "npc_var_get(^summoning_npcvar_normal_attack_target)" in graahk_tick and
               "npc_walk($target_coord);" in graahk_tick and
               "npc_var_set(^summoning_npcvar_normal_attack_target, 0);" in graahk_tick and
               "npc_anim(summoning_special_move_rending, 0);" in graahk_tick and
               "npc_queue(2, $damage, 0);" in graahk_tick,
               "Goad lacks selected-target validation or its generation-safe normal-combat lifecycle")
        graahk_profile = combat_profiles[
            combat_profiles.index("[summoning_cohort_spirit_graahk_spirit_graahk]"):]
        expect("hitpoints=810" in graahk_profile and "attack=50" in graahk_profile and
               "strength=50" in graahk_profile and "defence=50" in graahk_profile,
               "Goad's source normal-combat profile is incomplete")
        steel_profile = combat_profiles[
            combat_profiles.index("[summoning_cohort_steel_titan_steel_titan]"):]
        expect("hitpoints=750" in steel_profile and "attack=70" in steel_profile and
               "param=magicdamage,120" in steel_profile and "param=rangebonus,120" in steel_profile,
               "Steel Titan's source combat profile is incomplete")
        expect('strcmp(text, "rangebonus") == 0 || strcmp(text, "magicdamage") == 0' in mock_content,
               "Steel Titan's source ranged-strength and magic-damage params are not parsed by the host")
        iron_profile = combat_profiles[
            combat_profiles.index("[summoning_cohort_iron_titan_iron_titan]"):]
        expect("hitpoints=694" in iron_profile and "attack=65" in iron_profile and
               "param=crushattack,100" in iron_profile and "param=strengthbonus,108" in iron_profile,
               "Iron Titan's source combat profile is incomplete")
        expect("add(stat_base(hitpoints), 8)" in execute and
               "You are already at maximum hitpoints!" in execute,
               "Titan's Constitution does not validate its overheal ceiling")
        expect("if ($type = 18 | $type = 19 | $type = 20 | $type = 21)" in execute and
               "p_delay(2);" in execute and "p_telejump(0_41_35_41_34);" in execute,
               "Call to Arms does not retain its shared delayed Pest Control teleport")
        expect("summoning_special_move_call_to_arms_start" in execute and
               "summoning_special_move_call_to_arms_end" in execute and
               "npc_findowned2 = false" in execute,
               "Call to Arms lacks its remapped visuals or delayed familiar revalidation")
        overlay = interface[interface.index("[special_overlay]") : interface.index("[special_overlay_icon]")]
        expect("clickmask=129024" in overlay and "targetverb=Cast" in overlay,
               "the Summoning overlay is not a five-kind target component")
        for trigger in ("opnpct", "opplayert", "opheldt", "opobjt", "oploct"):
            expect(f"[{trigger},summoning_familiar:special_overlay]" in text,
                   f"the Summoning overlay does not route {trigger}")
        expect("^if_event_target_all" in text,
               "the server does not arm the target component's target mask")
        expect("PKTOUT_NAME_OPPLAYERT, handle_opplayert_packet" in mock_world and
               "handle_opplayert(" in mock_world,
               "decoded OPPLAYERT packets are still dropped by the world router")
        expect("uint16_t generation;" in mock_header and "target_generation" in mock_header,
               "NPC/interactions do not carry generation identity")
        expect("srv->npcs[slot].generation != generation" in mock_host,
               "npc_finduid accepts a reused NPC slot")
        expect("case SS_OP_NPC_FINDCOMBAT:" in mock_host,
               "content cannot resolve the owner's current combat target")
        expect("case SS_OP_NPC_FINDOWNED2:" in mock_host and
               "SSVM_ENT_NPC, SSVM_SECONDARY, npc" in mock_host and
               "npc_findowned2 = false" in validate,
               "target validation cannot retain a primary target while resolving the familiar")
        expect("SSVM_ENT_PLAYER, SSVM_SECONDARY, &srv->players[player_slot]" in mock_host and
               "interaction->kind == MOCK230_INTERACT_PLAYER ? interaction->npc_slot : -1" in mock_world and
               "(struct Mock230Player*)SSVM_Active(state, SSVM_ENT_PLAYER);" in mock_host,
               "selected player casts do not bind a secondary recipient for dotted target operations")
        expect("player_by_uid(struct Mock230Server* srv, int32_t uid)" in mock_host and
               "SSVM_PushInt(state, player ? player->pid + 1 : 0);" in mock_host and
               "struct Mock230Player* target_player = player_by_uid(srv, values[1]);" in mock_host and
               "mock230_world_set_active(srv, target_player);" in mock_host,
               "delayed player-target specials do not preserve uid identity for find/projectile/damage")
        expect("container_row(srv, player, inv_id)" in mock_host and
               "mock230_container_resolve(srv, player ? player : srv->active_player, inv_id)" in mock_inv,
               "dotted inventory commands do not resolve containers against the selected secondary player")
    except (AssertionError, OSError, ValueError) as exc:
        print(f"test_summoning_specials: error: {exc}", file=sys.stderr)
        return 1

    print("test_summoning_specials: target surface, generation handles, 78 enabled + 7 reconstructed rows, 0 errors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
