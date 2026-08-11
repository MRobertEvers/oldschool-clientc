#!/usr/bin/env python3
"""Regression gate for the first transaction-safe Summoning specials."""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
SERVER = REPO / (
    "OSRS-Content/osrs239-content/server/scripts/ported_scape2009_summoning/"
    "scripts/summoning_spirit_wolf.rs2"
)
MOCK_HOST = REPO / "src/net/mock/mock230_scripts.c"
MOCK_WORLD = REPO / "src/net/mock/mock230_world.c"
MOCK_HEADER = REPO / "src/net/mock/mock230.h"
INTERFACE = REPO / (
    "OSRS-Content/osrs239-content/ported/scape2009_summoning/"
    "interfaces/summoning_familiar.if"
)
SPECIAL_MANIFEST = REPO / "docs/summoning_port/special_move_assets_530.ini"


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    try:
        text = SERVER.read_text(encoding="utf-8")
        mock_host = MOCK_HOST.read_text(encoding="utf-8")
        mock_world = MOCK_WORLD.read_text(encoding="utf-8")
        mock_header = MOCK_HEADER.read_text(encoding="utf-8")
        interface = INTERFACE.read_text(encoding="utf-8")
        special_manifest = SPECIAL_MANIFEST.read_text(encoding="utf-8")
        execute = text[
            text.index("[proc,summoning_familiar_special_execute]") : text.index("[proc,summoning_familiar_special_xp]")
        ]
        target_execute = text[
            text.index("[proc,summoning_familiar_special_target_execute]") : text.index("[proc,summoning_familiar_special_target_dispatch]")
        ]
        xp = text[
            text.index("[proc,summoning_familiar_special_xp]") : text.index("[proc,summoning_familiar_special_target_kinds]")
        ]
        handler = text[text.index("[if_button1,summoning_familiar:special]") : text.index("[opnpct,summoning_familiar:special_overlay]")]
        commit = text[text.index("[proc,summoning_familiar_special_commit]") : text.index("[proc,summoning_familiar_special_target_execute]")]
        validate = text[text.index("[proc,summoning_familiar_special_validate]") : text.index("[proc,summoning_familiar_special_commit]")]

        expected_xp = {
            1: 1, 2: 1, 3: 8, 4: 2, 5: 2, 6: 2, 8: 2, 11: 23, 13: 6, 15: 11,
            18: 7, 19: 7, 20: 7, 21: 7,
            22: 6, 23: 8, 32: 23, 33: 47, 34: 24, 37: 11, 38: 57, 43: 19, 44: 31, 46: 10, 47: 7, 49: 14, 51: 11, 53: 73,
            54: 37, 59: 79, 60: 79, 61: 79, 62: 16, 72: 18, 74: 45,
        }
        execute_types = {
            int(value)
            for condition in re.findall(r"if \(([^)]*)\) \{", execute)
            for value in re.findall(r"\$type = (\d+)", condition)
        }
        target_execute_types = {
            int(value)
            for condition in re.findall(r"if \(([^)]*)\) \{", target_execute)
            for value in re.findall(r"\$type = (\d+)", condition)
        }
        xp_rows = {
            int(kind): int(value)
            for kind, value in re.findall(r"if \(\$type = (\d+)\) return\((\d+)\);", xp)
        }
        expect(execute_types | target_execute_types == set(expected_xp),
               "implemented special handlers drifted from their XP rows")
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
    except (AssertionError, OSError, ValueError) as exc:
        print(f"test_summoning_specials: error: {exc}", file=sys.stderr)
        return 1

    print("test_summoning_specials: target surface, generation handles, transaction and 35 source-backed rows, 0 errors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
