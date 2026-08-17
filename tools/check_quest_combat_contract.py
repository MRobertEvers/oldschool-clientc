#!/usr/bin/env python3
"""Structural regression checks for quest encounters that implementation has begun."""

from __future__ import annotations

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "OSRS-Content/osrs239-content/server/scripts"
MANIFEST = ROOT / "docs/bosses/quest_combat_manifest.json"
DELRITH = CONTENT / "quests/quest_demon/scripts/delrith.rs2"
DELRITH_NPC = CONTENT / "quests/quest_demon/configs/quest_demon.npc"
DELRITH_VARP = CONTENT / "quests/quest_demon/configs/quest_demon.varp"
ARIS = CONTENT / "areas/varrock/scripts/aris.rs2"
WITCH_SCRIPT = CONTENT / "quests/quest_ball/scripts/quest_ball_locs.rs2"
WITCH_NPC = CONTENT / "quests/quest_ball/configs/witches_house.npc"
WITCH_VARP = CONTENT / "quests/quest_ball/configs/quest_ball.varp"
ARENA = CONTENT / "quests/quest_arena/scripts/arena_encounter.rs2"
ARENA_LOCS = CONTENT / "quests/quest_arena/scripts/arena_locs.rs2"
ARENA_NPC = CONTENT / "quests/quest_arena/configs/quest_arena.npc"
ARENA_SPAWN = CONTENT / "quests/quest_arena/configs/quest_arena.spawn"
ARENA_WORLD_SPAWN = CONTENT / "areas/world/configs/m40_49.spawn"
ARENA_LADY = CONTENT / "quests/quest_arena/scripts/lady_servil.rs2"
HAZEEL_ALOMONE = CONTENT / "quests/quest_hazeelcult/scripts/alomone.rs2"
HAZEEL_LOCS = CONTENT / "quests/quest_hazeelcult/scripts/quest_hazeelcult_locs.rs2"
HAZEEL_CLIVET = CONTENT / "quests/quest_hazeelcult/scripts/clivet.rs2"
HAZEEL_CERIL = CONTENT / "quests/quest_hazeelcult/scripts/ceril_carnillean.rs2"
HAZEEL_CLAUS = CONTENT / "quests/quest_hazeelcult/scripts/claus_the_chef.rs2"
HAZEEL_NPC = CONTENT / "quests/quest_hazeelcult/configs/quest_hazeelcult.npc"
SOTN = CONTENT / "quests/quest_secretsofthenorth/scripts/secretsofthenorth.rs2"
GRAND_DEMON = CONTENT / "quests/quest_grandtree/scripts/grandtree_black_demon.rs2"
GRAND_GLOUGH = CONTENT / "quests/quest_grandtree/scripts/glough.rs2"
GRAND_FOREMAN = CONTENT / "quests/quest_grandtree/scripts/foreman.rs2"
GRAND_PILLARS = CONTENT / "quests/quest_grandtree/scripts/grandtree_locs_chest.rs2"
GRAND_ROOTS = CONTENT / "quests/quest_grandtree/scripts/grandtree_locs_roots.rs2"
GRAND_KING = CONTENT / "quests/quest_grandtree/scripts/king_narnode.rs2"
GRAND_NPC = CONTENT / "quests/quest_grandtree/configs/quest_grandtree.npc"
GRAND_VARP = CONTENT / "quests/quest_grandtree/configs/quest_grandtree.varp"
COMBAT_PARAM = CONTENT / "skill_combat/configs/combat.param"
SPELL_CONSTANTS = CONTENT / "skill_combat/configs/magic/spells.constant"
PLAYER_MAGIC = CONTENT / "skill_combat/scripts/player/player_magic.rs2"
PARAM_ALLOC = ROOT / "OSRS-Content/osrs239-content/pack/param.alloc"
SPAWN_GENERATOR = ROOT / "tools/gen_spawns.py"
MOCK_HEADER = ROOT / "src/net/mock/mock230.h"
MOCK_WORLD = ROOT / "src/net/mock/mock230_world.c"
MOCK_ENCODE = ROOT / "src/net/mock/mock230_encode.c"
MOCK_SCRIPTS = ROOT / "src/net/mock/mock230_scripts.c"
MOCK_NPC_OPS = ROOT / "src/net/mock/mock230_ops_npc.c"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def require_text(text: str, needles: tuple[str, ...], scope: str) -> None:
    for needle in needles:
        require(needle in text, f"{scope}: missing {needle!r}")


def check_manifest() -> None:
    data = json.loads(MANIFEST.read_text())
    rows = data["encounters"]
    require(len(rows) == 145, "manifest: expected 145 quest/miniquest units")
    matches = [row for row in rows if row["id"] == "quest-demon-slayer"]
    require(len(matches) == 1, "manifest: expected exactly one Demon Slayer row")
    row = matches[0]
    require(row["implementation_status"] == "implementation-in-progress", "Demon Slayer: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(row[key]), f"Demon Slayer: empty evidence field {key}")
    witch = [row for row in rows if row["id"] == "quest-witch-s-house"]
    require(len(witch) == 1, "manifest: expected exactly one Witch's House row")
    require(witch[0]["implementation_status"] == "implementation-in-progress",
            "Witch's House: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(witch[0][key]), f"Witch's House: empty evidence field {key}")
    arena = [row for row in rows if row["id"] == "quest-fight-arena"]
    require(len(arena) == 1, "manifest: expected exactly one Fight Arena row")
    require(arena[0]["implementation_status"] == "implementation-in-progress",
            "Fight Arena: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(arena[0][key]), f"Fight Arena: empty evidence field {key}")
    hazeel = [row for row in rows if row["id"] == "quest-hazeel-cult"]
    require(len(hazeel) == 1, "manifest: expected exactly one Hazeel Cult row")
    require(hazeel[0]["implementation_status"] == "implementation-in-progress",
            "Hazeel Cult: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(hazeel[0][key]), f"Hazeel Cult: empty evidence field {key}")
    grand = [row for row in rows if row["id"] == "quest-the-grand-tree"]
    require(len(grand) == 1, "manifest: expected exactly one The Grand Tree row")
    require(grand[0]["implementation_status"] == "implementation-in-progress",
            "The Grand Tree: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(grand[0][key]), f"The Grand Tree: empty evidence field {key}")


def check_delrith() -> None:
    script = DELRITH.read_text()
    require_text(
        script,
        (
            "[zone,0_50_52_24_32]",
            "[zone,0_50_52_24_40]",
            "[proc,demon_slayer_init_incantation]",
            "[proc,demon_slayer_incantation_word](int $word)(string)",
            "[proc,demon_slayer_incantation_text]()(string)",
            "[proc,demon_slayer_choose_incantation_word](string $position)(int)",
            "add(random(5), 1)",
            "def_int $word_5 = ~demon_slayer_choose_incantation_word(\"fifth\");",
            "$word_5 = %demon_incantation_5",
            "[opnpc2,delrith]",
            "[apnpc2,delrith]",
            "[ai_queue2,delrith]",
            "$damage = calc($damage * 2);",
            "[ai_queue3,delrith]",
            "npc_setowner;",
            "%demonstart = ^demon_silverlight",
            "inv_total(worn, silverlight) > 0",
            "npc_changetype(delrith_weakened, 500);",
            "[opnpc1,delrith_weakened]",
            "queue(demon_slayer_start_incantation, 0, npc_uid);",
            "[queue,demon_slayer_start_incantation](npc_uid $delrith)",
            "npc_finduid($delrith)",
            "npc_statheal(hitpoints, 0, 100);",
            "npc_del;",
            "queue(demon_slayer_complete, 1, 0);",
        ),
        "Delrith",
    )
    require("npc_find(coord, delrith_weakened" not in script, "Delrith: ambiguous radius lookup restored")
    require("~p_choice4(" not in script, "Delrith: fixed whole-chant menu restored")
    require("Zaree" not in script, "Delrith: non-incantation word restored")

    npc = DELRITH_NPC.read_text()
    require_text(
        npc,
        ("[delrith]", "hitpoints=7", "param=attackrate,6", "param=damagetype,2", "[delrith_weakened]"),
        "Delrith NPC config",
    )
    require(npc.count("param=death_drop,null") == 2, "Delrith NPC config: both forms must have null drops")

    varp = DELRITH_VARP.read_text()
    require_text(
        varp,
        (
            "[demon_delrith_engaged]",
            "scope=temp",
            "[demon_incantation_1]",
            "[demon_incantation_2]",
            "[demon_incantation_3]",
            "[demon_incantation_4]",
            "[demon_incantation_5]",
        ),
        "Delrith engagement/incantation state",
    )

    aris = ARIS.read_text()
    require_text(
        aris,
        (
            "[label,demon_slayer_aris_quest_start]",
            "~demon_slayer_init_incantation;",
            "[label,demon_slayer_aris_incantation]",
            "~demon_slayer_incantation_text",
            "~chatnpc_anim(^chat_happy, $incantation);",
        ),
        "Aris incantation reminder",
    )
    require(
        "Carlem... Aber... Camerinthum... Purchai... Gabindo" not in aris,
        "Aris: fixed incantation reminder restored",
    )


def check_owned_npc_runtime() -> None:
    header = MOCK_HEADER.read_text()
    world = MOCK_WORLD.read_text()
    encode = MOCK_ENCODE.read_text()
    scripts = MOCK_SCRIPTS.read_text()
    npc_ops = MOCK_NPC_OPS.read_text()
    require_text(
        header,
        ("mock230_world_npc_visible_to", "owned npcs are private to the exact login"),
        "owned NPC API",
    )
    require_text(
        world,
        (
            "mock230_world_npc_visible_to(",
            "mock230_world_npc_owner(srv, npc) == player",
            "an owned npc is hidden from another player",
            "abandoned private encounter actor",
        ),
        "owned NPC lifecycle",
    )
    require(encode.count("mock230_world_npc_visible_to(srv, npc, player)") >= 4,
            "owned NPC encoding: both wire paths must filter tracked and added NPCs")
    require(scripts.count("mock230_world_npc_visible_to") >= 4,
            "owned NPC script lookup: find-all/find/finduid visibility gates missing")
    require(npc_ops.count("mock230_world_npc_visible_to") >= 2,
            "owned NPC iterator lookup: zone/hunt visibility gates missing")


def check_witches_experiment() -> None:
    script = WITCH_SCRIPT.read_text()
    require_text(
        script,
        (
            "[proc,ball_experiment_spawn]",
            "npc_setowner;",
            "[proc,ball_experiment_reset]",
            "[ai_queue3,shapeshifterglob]",
            "npc_changetype(shapeshifterspider, 500);",
            "[ai_queue3,shapeshifterspider]",
            "npc_changetype(shapeshifterbear, 500);",
            "[ai_queue3,shapeshifterbear]",
            "npc_changetype(shapeshifterwolf, 500);",
            "[ai_queue3,shapeshifterwolf]",
            "@defeat_witches_experiment;",
            "%ballquest = ^ball_defeated_experiment;",
            "%ball_shed_unlocked = 1;",
        ),
        "Witch's experiment",
    )
    require(script.count("npc_statheal(hitpoints, 0, 100);") == 6,
            "Witch's experiment: each intermediate/no-hero path must refill HP")
    require("npc_add(^ball_experiment_spawn_coord, shapeshifterspider" not in script,
            "Witch's experiment: spider must transform, not respawn")
    require("npc_add(^ball_experiment_spawn_coord, shapeshifterbear" not in script,
            "Witch's experiment: bear must transform, not respawn")
    require("npc_add(^ball_experiment_spawn_coord, shapeshifterwolf" not in script,
            "Witch's experiment: wolf must transform, not respawn")

    npc = WITCH_NPC.read_text()
    for form, hp, attack, defence, strength, damage_type in (
        ("shapeshifterglob", 21, 18, 19, 10, 2),
        ("shapeshifterspider", 31, 28, 29, 20, 2),
        ("shapeshifterbear", 41, 38, 39, 30, 1),
        ("shapeshifterwolf", 51, 48, 49, 40, 0),
    ):
        require_text(
            npc,
            (
                f"[{form}]",
                f"hitpoints={hp}",
                f"attack={attack}",
                f"defence={defence}",
                f"strength={strength}",
                "param=attackrate,4",
                f"param=damagetype,{damage_type}",
            ),
            f"Witch's experiment NPC {form}",
        )
    require(npc.count("param=death_drop,null") == 4,
            "Witch's experiment: all four forms must suppress ordinary drops")
    require_text(WITCH_VARP.read_text(), ("[ball_shed_unlocked]", "scope=temp"),
                 "Witch's House shed lock state")


def check_fight_arena() -> None:
    encounter = ARENA.read_text()
    require_text(
        encounter,
        (
            "[label,arena_start_ogre]",
            "npc_add(0_40_49_44_29, arena_ogre, 32000);",
            "[label,arena_start_scorpion]",
            "npc_add(0_40_49_44_23, arena_scorpion, 32000);",
            "[label,arena_start_bouncer]",
            "npc_add(0_40_49_44_26, arena_bouncer, 32000);",
            "[label,arena_start_general]",
            "npc_add(0_40_49_44_18, general_khazard_arena, 32000);",
            "[label,arena_after_ogre]",
            "%arenaquest = ^arena_sent_jail;",
            "[label,arena_general_after_bouncer]",
            "%arenaquest = ^arena_freed_servils;",
            "[proc,arena_remove_owned_fighters]",
            "[ai_queue3,arena_ogre]",
            "[ai_queue3,arena_scorpion]",
            "[ai_queue3,arena_bouncer]",
            "[ai_queue3,general_khazard_arena]",
            "obj_add(npc_coord, dorgesh_construction_bone, 1, ^lootdrop_duration);",
            "obj_add(npc_coord, dorgesh_construction_bone_curved, 1, ^lootdrop_duration);",
            "obj_add(npc_coord, vile_ashes, 1, ^lootdrop_duration);",
        ),
        "Fight Arena encounter",
    )
    require(encounter.count("npc_setowner;") == 4,
            "Fight Arena: every dynamically spawned combat type must be owner-private")
    require("random(400) = 0" in encounter and "random(5013) = 0" in encounter,
            "Fight Arena: Ogre tertiary drop rates drifted")

    locs = ARENA_LOCS.read_text()
    require_text(
        locs,
        (
            "[oploc1,arena_guard_chest_shut]",
            "[oploc1,arena_guard_chest_open]",
            "[oplocu,arena_jeremydoor]",
            "[label,arena_free_sammy]",
            "[oploc1,fightarena_door2]",
            "[oploc2,fightarena_door2]",
            "[label,arena_enter_current_round]",
            "[label,arena_escape]",
        ),
        "Fight Arena route",
    )

    npc = ARENA_NPC.read_text()
    for name, hp, attack, strength, defence, rate, style in (
        ("arena_ogre", 60, 54, 53, 53, 6, 2),
        ("arena_scorpion", 40, 40, 39, 34, 4, 0),
        ("arena_bouncer", 116, 120, 120, 120, 4, 0),
        ("general_khazard_arena", 170, 75, 78, 80, 4, 1),
    ):
        require_text(
            npc,
            (
                f"[{name}]",
                f"hitpoints={hp}",
                f"attack={attack}",
                f"strength={strength}",
                f"defence={defence}",
                f"param=attackrate,{rate}",
                f"param=damagetype,{style}",
                "param=death_drop,null",
            ),
            f"Fight Arena NPC {name}",
        )
    require_text(npc, ("[general_khazard_arena]", "op2=Attack", "vislevel=142"),
                 "Fight Arena General Khazard")

    curated = ARENA_SPAWN.read_text()
    for actor in ("lady_servil", "arena_guard2", "sammy_servil", "sammy_servil_arena", "justin_servil"):
        require(actor in curated, f"Fight Arena spawn: missing {actor}")
    world_spawn = ARENA_WORLD_SPAWN.read_text()
    for actor in ("arena_ogre", "arena_scorpion", "arena_bouncer"):
        require(actor not in world_spawn, f"Fight Arena: public combat spawn restored for {actor}")
    require_text(
        SPAWN_GENERATOR.read_text(),
        ("NPC_SPAWN_EXCLUSIONS", "scripted owner-private encounter actor",
         '("arena_scorpion", 2608, 3159, 0)',
         '("arena_bouncer", 2608, 3162, 0)',
         '("arena_ogre", 2608, 3165, 0)'),
        "Fight Arena spawn regeneration",
    )

    lady = ARENA_LADY.read_text()
    require_text(
        lady,
        (
            "%arenaquest = ^arena_complete;",
            "%arenaquest = ^arena_complete_defeated_genkhazard;",
            "inv_add(inv, coins, 1000);",
            "stat_advance(attack, 121750);",
            "stat_advance(thieving, 21750);",
            "~quest_complete_rewards(quest_fightarena",
        ),
        "Fight Arena rewards",
    )


def check_hazeel_cult() -> None:
    alomone = HAZEEL_ALOMONE.read_text()
    require_text(
        alomone,
        (
            "[zone,0_40_151_48_0]",
            "[zone,0_40_151_48_8]",
            "[proc,hazeelcult_spawn_alomone]",
            "npc_add(0_40_151_48_7, alomone_hazeel_cultist_2op, 32000);",
            "npc_add(0_40_151_48_7, alomone_hazeel_cultist_1op, 32000);",
            "[proc,hazeelcult_alomone_talk]",
            "[ai_queue3,alomone_hazeel_cultist_2op]",
            "obj_add(npc_coord, bones, 1, ^lootdrop_duration);",
            "%hazeelcultquest = ^hazeelcult_finished_side_task;",
            "npc_add(0_40_151_47_5, hazeel, 100);",
        ),
        "Hazeel Cult Alomone",
    )
    require(alomone.count("npc_setowner;") == 3,
            "Hazeel Cult: both Alomone route variants and ritual Hazeel must be owner-private")
    require("obj_add(npc_coord, carnillean_armour" not in alomone,
            "Hazeel Cult: pre-2023 Alomone armour drop restored")

    npc = HAZEEL_NPC.read_text()
    require_text(
        npc,
        (
            "[alomone_hazeel_cultist_2op]",
            "hitpoints=25",
            "attack=10",
            "strength=10",
            "defence=4",
            "param=attackrate,4",
            "param=damagetype,2",
            "param=death_drop,null",
        ),
        "Hazeel Cult Alomone NPC",
    )

    locs = HAZEEL_LOCS.read_text()
    require_text(
        locs,
        (
            "[oploc1,hazeelsewerraft]",
            "~hazeelcult_spawn_alomone;",
            "[oploc1,hazeel_chest_closed]",
            "Get away from that chest!",
            "~obj_gettotal(carnillean_armour) > 0",
            "inv_freespace(inv) < 1",
            "inv_add(inv, carnillean_armour, 1);",
            "[oploc1,carnilleanopenchest]",
            "inv_add(inv, hazeel_scroll, 1);",
        ),
        "Hazeel Cult route locs",
    )
    require_text(
        HAZEEL_CLAUS.read_text(),
        ("[oplocu,carnilleanrange]", "inv_del(inv, poison, 1);"),
        "Hazeel Cult poison range",
    )

    clivet = HAZEEL_CLIVET.read_text()
    require_text(
        clivet,
        (
            "[zone,0_40_151_0_16]",
            "[proc,hazeelcult_spawn_clivet]",
            "npc_add(0_40_151_6_19, clivet_hazeel_cultist_vis, 32000);",
            "npc_setowner;",
            "[proc,hazeelcult_clivet_talk]",
        ),
        "Hazeel Cult Clivet lifecycle",
    )
    require_text(
        HAZEEL_CERIL.read_text(),
        (
            "[proc,hazeelcult_spawn_mansion_actors]",
            "npc_add(0_40_51_6_6, sir_ceril_carnillean, 32000);",
            "npc_add(0_40_51_10_8, guard_carnillean, 32000);",
            "npc_add(0_40_51_8_7, butler_jones_hazeel_cultist, 32000);",
        ),
        "Hazeel Cult mansion actor lifecycle",
    )
    require_text(
        SOTN.read_text(),
        ("~hazeelcult_alomone_talk;", "~hazeelcult_clivet_talk;"),
        "Hazeel Cult / Secrets of the North variant delegation",
    )


def check_grand_tree() -> None:
    glough = GRAND_GLOUGH.read_text()
    require_text(
        glough,
        (
            "[label,grandtree_glough_cutscene]",
            "npc_add(movecoord(coord, 3, 0, 0), grandtree_glough, 500);",
            "npc_setowner;",
            "def_coord $demon_spawn = 0_38_154_47_11;",
            "npc_add($demon_spawn, grandtree_blackdemon, 1000);",
            "if (npc_find($demon_spawn, grandtree_blackdemon, 20, 0) = false)",
        ),
        "The Grand Tree Glough/demon spawn",
    )
    require(glough.count("npc_setowner;") >= 2,
            "The Grand Tree: Glough and Black demon must both be owner-private")

    demon = GRAND_DEMON.read_text()
    require_text(
        demon,
        (
            "[ai_queue3,grandtree_blackdemon]",
            "if (p_finduid(uid) = true)",
            "[label,grandtree_defeat_black_demon]",
            "%grandtree = ^grandtree_defeated_black_demon;",
        ),
        "The Grand Tree Black demon death",
    )
    require("[ai_timer,grandtree_blackdemon]" not in demon,
            "The Grand Tree: distance/hero timer may not shorten the exact 1,000-tick lifetime")
    require("@black_demon_drop_table" not in demon,
            "The Grand Tree: generic Black demon loot restored")

    npc = GRAND_NPC.read_text()
    require_text(
        npc,
        (
            "[grandtree_blackdemon]",
            "hitpoints=157",
            "attack=145",
            "strength=148",
            "defence=152",
            "param=attackrate,4",
            "param=damagetype,1",
            "param=elemental_weakness,^element_water",
            "param=elemental_weakness_percent,40",
            "[grandtree_foreman]",
            "hitpoints=20",
            "attack=20",
            "strength=20",
            "defence=20",
            "respawnrate=5",
        ),
        "The Grand Tree NPC overlays",
    )
    require(npc.count("param=death_drop,null") == 2,
            "The Grand Tree: both custom death handlers must suppress automatic drops")

    foreman = GRAND_FOREMAN.read_text()
    require_text(
        foreman,
        (
            "[opnpc1,grandtree_foreman]",
            "Sadly his wife is no longer with us!",
            "He loves worm holes.",
            "Anita.",
            "[ai_queue3,grandtree_foreman]",
            "obj_add(npc_coord, bones, 1, ^lootdrop_duration);",
            "obj_add(npc_coord, grandtree_order, 1, ^lootdrop_duration);",
        ),
        "The Grand Tree Foreman routes",
    )

    pillars = GRAND_PILLARS.read_text()
    require_text(
        pillars,
        (
            "case grandtree_pillart : $expected = grandtree_twigt; $bit = 1;",
            "case grandtree_pillaru : $expected = grandtree_twigu; $bit = 2;",
            "case grandtree_pillarz : $expected = grandtree_twigz; $bit = 4;",
            "case grandtree_pillaro : $expected = grandtree_twigo; $bit = 8;",
            "%grandtree_tuzo_mask = or(%grandtree_tuzo_mask, $bit);",
            "if (%grandtree_tuzo_mask = 15)",
            "%grandtree = ^grandtree_unlocked_trapdoor;",
        ),
        "The Grand Tree TUZO gate",
    )
    require_text(GRAND_VARP.read_text(), ("[grandtree_tuzo_mask]", "scope=temp"),
                 "The Grand Tree TUZO state")

    require_text(
        GRAND_ROOTS.read_text(),
        (
            "%daconia_rock_root",
            "~obj_gettotal(grandtree_daconiarock) = 0",
            "inv_freespace(inv) = 0",
            "inv_add(inv, grandtree_daconiarock, 1);",
        ),
        "The Grand Tree Daconia root",
    )
    require_text(
        GRAND_KING.read_text(),
        (
            "%daconia_rock_root = ~random_range(1, 15);",
            "stat_advance(agility, 79000);",
            "stat_advance(attack, 184000);",
            "stat_advance(magic, 21500);",
            "~quest_complete_rewards(quest_grandtree",
        ),
        "The Grand Tree finale",
    )

    require_text(
        COMBAT_PARAM.read_text(),
        ("[elemental_weakness]", "[elemental_weakness_percent]"),
        "elemental weakness params",
    )
    require_text(
        SPELL_CONSTANTS.read_text(),
        ("^element_air = 1", "^element_water = 2", "^element_earth = 3", "^element_fire = 4"),
        "elemental weakness constants",
    )
    magic = PLAYER_MAGIC.read_text()
    require_text(
        magic,
        (
            "[proc,magic_spell_base_maxhit]",
            "[proc,elemental_spell_element]",
            "^water_strike, ^water_bolt, ^water_blast, ^water_wave, ^water_surge",
            "[proc,npc_elemental_weakness]",
            "[proc,pvm_spell_hit_roll]",
            "$attack_roll = add($attack_roll, divide(multiply($attack_roll, $weakness), 100));",
            "def_int $base_maxhit = ~magic_spell_base_maxhit($spell_data);",
            "$maxhit = add($maxhit, divide(multiply($base_maxhit, $weakness), 100));",
        ),
        "elemental weakness combat mechanics",
    )
    require("2738=elemental_weakness" in PARAM_ALLOC.read_text() and
            "2739=elemental_weakness_percent" in PARAM_ALLOC.read_text(),
            "elemental weakness param allocation drift")


def main() -> int:
    try:
        check_manifest()
        check_delrith()
        check_owned_npc_runtime()
        check_witches_experiment()
        check_fight_arena()
        check_hazeel_cult()
        check_grand_tree()
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"quest combat contract: {error}", file=sys.stderr)
        return 1
    print("quest combat contract: 145-unit ledger, ownership runtime, Delrith, Witch's experiment, Fight Arena, Hazeel Cult and The Grand Tree (ok)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
