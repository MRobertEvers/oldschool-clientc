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
UPASS = CONTENT / "quests/quest_upass/scripts/upass_encounters.rs2"
UPASS_NPC = CONTENT / "quests/quest_upass/configs/quest_upass.npc"
UPASS_CONSTANT = CONTENT / "quests/quest_upass/configs/quest_upass.constant"
UPASS_JERRO = CONTENT / "quests/quest_upass/scripts/sir_jerro.rs2"
UPASS_CARL = CONTENT / "quests/quest_upass/scripts/sir_carl.rs2"
UPASS_HARRY = CONTENT / "quests/quest_upass/scripts/sir_harry.rs2"
UPASS_DEMONS = CONTENT / "quests/quest_upass/scripts/upass_demon_drops.rs2"
UPASS_KALRAG = CONTENT / "quests/quest_upass/scripts/kalrag.rs2"
UPASS_DISCIPLE = CONTENT / "quests/quest_upass/scripts/iban_disciple.rs2"
UPASS_CAGES = CONTENT / "quests/quest_upass/scripts/upass_cages.rs2"
UPASS_TOMB = CONTENT / "quests/quest_upass/scripts/upass_tomb.rs2"
UPASS_IBAN = CONTENT / "quests/quest_upass/scripts/lord_iban.rs2"
UPASS_BLOODWELL = CONTENT / "quests/quest_upass/scripts/upass_bloodwell.rs2"
UPASS_WELL = CONTENT / "quests/quest_upass/scripts/upass_well.rs2"
UPASS_KOFTIK = CONTENT / "quests/quest_upass/scripts/koftik.rs2"
UPASS_OBSTACLES = CONTENT / "quests/quest_upass/scripts/upass_obstacles.rs2"
UPASS_WORLD_SPAWNS = (
    CONTENT / "areas/world/configs/m37_151.spawn",
    CONTENT / "areas/world/configs/m36_154.spawn",
    CONTENT / "areas/world/configs/m33_71.spawn",
    CONTENT / "areas/world/configs/m33_72.spawn",
)
OBS_GUARD = CONTENT / "quests/quest_itgronigen/scripts/goblin_guard.rs2"
OBS_NPC = CONTENT / "quests/quest_itgronigen/configs/quest_itgronigen.npc"
OBS_DUNGEON = CONTENT / "quests/quest_itgronigen/scripts/observatory_dungeon.rs2"
OBS_PROFESSOR = CONTENT / "quests/quest_itgronigen/scripts/observatory_professor.rs2"
OBS_GLASS = CONTENT / "skill_crafting/scripts/glass/glass.rs2"
OBS_GENERIC_GOBLIN = CONTENT / "drop_tables/scripts/goblin.rs2"
OBS_WORLD_SPAWN = CONTENT / "areas/world/configs/m36_146.spawn"
TOURIST_CAPTAIN = CONTENT / "quests/quest_desertrescue/scripts/mercenary_captain.rs2"
TOURIST_GATE = CONTENT / "quests/quest_desertrescue/scripts/mining_camp_gate.rs2"
TOURIST_NPC = CONTENT / "quests/quest_desertrescue/configs/desertrescue.npc"
TOURIST_VARP = CONTENT / "quests/quest_desertrescue/configs/desertrescue.varp"
TOURIST_GENERIC_DROP = CONTENT / "drop_tables/scripts/wiki_mercenary_captain.rs2"
WATCH_GORAD = CONTENT / "quests/quest_itwatchtower/scripts/gorad.rs2"
WATCH_GREW = CONTENT / "quests/quest_itwatchtower/scripts/grew.rs2"
WATCH_NPC = CONTENT / "quests/quest_itwatchtower/configs/quest_itwatchtower.npc"
NPC_ALLOC = ROOT / "OSRS-Content/osrs239-content/pack/npc.alloc"
NPC_CLIENT = ROOT / "OSRS-Content/osrs239-content/pack/npc.client"
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
    upass = [row for row in rows if row["id"] == "quest-underground-pass"]
    require(len(upass) == 1, "manifest: expected exactly one Underground Pass row")
    require(upass[0]["implementation_status"] == "implementation-in-progress",
            "Underground Pass: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(upass[0][key]), f"Underground Pass: empty evidence field {key}")
    observatory = [row for row in rows if row["id"] == "quest-observatory-quest"]
    require(len(observatory) == 1,
            "manifest: expected exactly one Observatory Quest row")
    require(observatory[0]["implementation_status"] == "implementation-in-progress",
            "Observatory Quest: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(observatory[0][key]),
                f"Observatory Quest: empty evidence field {key}")
    tourist = [row for row in rows if row["id"] == "quest-the-tourist-trap"]
    require(len(tourist) == 1,
            "manifest: expected exactly one The Tourist Trap row")
    require(tourist[0]["implementation_status"] == "implementation-in-progress",
            "The Tourist Trap: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(tourist[0][key]),
                f"The Tourist Trap: empty evidence field {key}")
    watchtower = [row for row in rows if row["id"] == "quest-watchtower"]
    require(len(watchtower) == 1,
            "manifest: expected exactly one Watchtower row")
    require(watchtower[0]["implementation_status"] == "implementation-in-progress",
            "Watchtower: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(watchtower[0][key]),
                f"Watchtower: empty evidence field {key}")


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


def check_underground_pass() -> None:
    encounter = UPASS.read_text()
    require_text(
        encounter,
        (
            "[mapzone,1_33_71]", "[mapzone,0_36_154]", "[mapzone,1_33_72]",
            "[proc,upass_spawn_paladins]", "0_37_151_56_57",
            "0_37_151_54_54", "0_37_151_58_54",
            "[proc,upass_paladin_supplies]", "inv_freespace(inv) < 7",
            "inv_add(inv, meat_pie, 2);", "inv_add(inv, bread, 2);",
            "inv_add(inv, stew, 1);", "inv_add(inv, 2dose1attack, 1);",
            "inv_add(inv, 2doseprayerrestore, 1);",
            "[proc,upass_paladin_drops_private]", "def_int $roll = random(128);",
            "[proc,upass_spawn_demons]", "1_33_71_10_18",
            "1_33_71_20_10", "1_33_71_22_21",
            "npc_changetype(upass_doomion_safe", "npc_changetype(upass_holthion_safe",
            "npc_changetype(upass_othainian_safe", "0_36_154_52_55",
            "[proc,upass_spawn_temple_actors]", "1_33_72_21_39",
            "[ai_timer,ibanmonk]", "[ai_timer,upass_paladin1]",
        ),
        "Underground Pass encounter controller",
    )
    require(encounter.count("npc_setowner;") >= 8,
            "Underground Pass: every named encounter family must be owner-private")
    require(encounter.count("obj_add_private(") == 15,
            "Underground Pass: exact private paladin table drift")
    require("obj_add(" not in encounter,
            "Underground Pass: public encounter-controller ground item restored")

    obstacle = UPASS_OBSTACLES.read_text()
    require_text(obstacle, ("[mapzone,0_37_151]", "~upass_spawn_paladins;"),
                 "Underground Pass paladin map entry")

    static_actors = {
        line.split()[0]
        for path in UPASS_WORLD_SPAWNS
        for line in path.read_text().splitlines()
        if line.strip() and not line.lstrip().startswith(("//", "="))
    }
    for actor in (
        "upass_paladin1", "upass_paladin2", "upass_paladin3", "kalrag",
        "doomion", "holthion", "othainian", "ibanmonk", "iban",
    ):
        require(actor not in static_actors,
                f"Underground Pass: public static spawn restored for {actor}")

    npc = UPASS_NPC.read_text()
    for actor, hp, attack, strength, defence, damage_type, attack_rate in (
        ("upass_paladin1", 57, 54, 54, 54, 1, 5),
        ("upass_paladin2", 57, 54, 54, 54, 1, 5),
        ("upass_paladin3", 57, 54, 54, 54, 1, 5),
        ("kalrag", 78, 78, 78, 78, 0, 4),
        ("ibanmonk", 20, 8, 8, 12, 2, 4),
        ("doomion", 87, 76, 78, 77, 1, 4),
        ("holthion", 87, 76, 78, 77, 1, 4),
        ("othainian", 87, 76, 78, 77, 1, 4),
    ):
        block = npc.split(f"[{actor}]", 1)[1].split("\n[", 1)[0]
        require_text(
            block,
            (f"hitpoints={hp}", f"attack={attack}", f"strength={strength}",
             f"defence={defence}", f"param=damagetype,{damage_type}",
             f"param=attackrate,{attack_rate}", "param=death_drop,null"),
            f"Underground Pass NPC {actor}",
        )
    for actor in ("upass_doomion_safe", "upass_holthion_safe", "upass_othainian_safe"):
        block = npc.split(f"[{actor}]", 1)[1].split("\n[", 1)[0]
        require("op2=" not in block and "huntmode=" not in block,
                f"Underground Pass: {actor} regained aggression/Attack option")
        require_text(block, ("hitpoints=87", "param=magicdefence,-10",
                             "param=death_drop,null"),
                     f"Underground Pass safe demon {actor}")
    require_text(
        NPC_ALLOC.read_text(),
        ("27403=upass_doomion_safe", "27404=upass_holthion_safe",
         "27405=upass_othainian_safe"),
        "Underground Pass safe demon allocations",
    )
    require_text(
        NPC_CLIENT.read_text(),
        ("upass_doomion_safe", "upass_holthion_safe", "upass_othainian_safe"),
        "Underground Pass safe demon client membership",
    )

    for script_path, actor, badge, respawn in (
        (UPASS_JERRO, "upass_paladin1", "paladinbadge1", "upass_respawn_paladin1"),
        (UPASS_CARL, "upass_paladin2", "paladinbadge2", "upass_respawn_paladin2"),
        (UPASS_HARRY, "upass_paladin3", "paladinbadge3", "upass_respawn_paladin3"),
    ):
        script = script_path.read_text()
        require_text(
            script,
            (f"[ai_queue3,{actor}]", "obj_add_private(npc_coord, bones, 1",
             f"~obj_gettotal({badge}) = 0", f"obj_add_private(npc_coord, {badge}, 1",
             "~upass_paladin_drops_private;", f"queue({respawn}, 100, 0);",
             "~upass_paladin_supplies;"),
            f"Underground Pass {actor}",
        )
        require("obj_add(" not in script, f"Underground Pass: {actor} public drop restored")

    demons = UPASS_DEMONS.read_text()
    require(demons.count("obj_add_private(npc_coord, vile_ashes, 1") == 3,
            "Underground Pass: every named demon must drop private vile ashes")
    require(demons.count("queue(upass_respawn_demons, 30, 0);") == 3,
            "Underground Pass: named demon respawn contract drift")
    for amulet in ("doomion_amulet", "holthion_amulet", "othainian_amulet"):
        require_text(demons, (f"~obj_gettotal({amulet}) = 0",
                              f"obj_add_private(npc_coord, {amulet}, 1"),
                     f"Underground Pass {amulet}")
    require("obj_add(" not in demons and " ashes," not in demons,
            "Underground Pass: public/regular demon ashes restored")

    kalrag = UPASS_KALRAG.read_text()
    require_text(
        kalrag,
        ("[ai_queue3,kalrag]", "~obj_gettotal(ibandoll) = 0", "player_lock();",
         "anim(human_stunned, 0);", "spotanim_pl(stunned, 124, 0);",
         "%upass_venom_on_doll = 1;", "queue(upass_respawn_kalrag, 50, 0);"),
        "Underground Pass Kalrag",
    )
    require("obj_add" not in kalrag, "Underground Pass: Kalrag must not drop loot")

    disciple = UPASS_DISCIPLE.read_text()
    require_text(
        disciple,
        ("obj_add_private(npc_coord, bones, 1", "obj_add_private(npc_coord, zamrobebottom, 1",
         "obj_add_private(npc_coord, zamrobetop, 1",
         "queue(upass_respawn_disciple, 250, npc_coord);"),
        "Underground Pass Disciple",
    )
    require("brokenibanstaff" not in disciple and "obj_add(" not in disciple,
            "Underground Pass: obsolete/public Disciple drop restored")

    cages = UPASS_CAGES.read_text()
    require_text(
        cages,
        ("inv_total(inv, ibandoll) > 0", "%upass_dove_on_doll = 1;",
         "%upass_shadow_on_doll = 1;", "inv_add(inv, ibansdove, 1);",
         "inv_add(inv, ibansshadow, 1);"),
        "Underground Pass doll auto-application",
    )
    tomb = UPASS_TOMB.read_text()
    require_text(tomb, ("%upass_ashes_on_doll = 1;", "inv_add(inv, ibanstaff, 1);",
                        "^iban_staff_max_charges", "%upass = ^upass_defeated_iban;"),
                 "Underground Pass Iban finale")
    require("inv_add(inv, deathrune" not in tomb and "inv_add(inv, firerune" not in tomb,
            "Underground Pass: removed Iban rune bundle restored")

    iban = UPASS_IBAN.read_text()
    require_text(
        iban,
        ("def_player_uid $owner = uid;", "^upass_iban_temple_lower",
         "^upass_iban_temple_upper", "distance(coord, $bolt_coord) = 0",
         "damage($owner, hitsplat_damage, ~random_range(5, 8));",
         "p_teleport(^upass_iban_temple_entrance);"),
        "Underground Pass Iban hazard",
    )
    require("huntall(" not in iban, "Underground Pass: Iban hazard may not target another player")
    require_text(
        UPASS_CONSTANT.read_text(),
        ("^upass_iban_temple_lower", "^upass_iban_temple_upper",
         "^upass_iban_temple_entrance"),
        "Underground Pass Iban temple geometry",
    )

    bloodwell = UPASS_BLOODWELL.read_text()
    for badge, bit, coord_name in (
        ("paladinbadge1", "%upass_paladinbadge_1 = 1;", "0_37_151_56_57"),
        ("paladinbadge2", "%upass_paladinbadge_2 = 1;", "0_37_151_54_54"),
        ("paladinbadge3", "%upass_paladinbadge_3 = 1;", "0_37_151_58_54"),
    ):
        require_text(bloodwell, (f"case {badge} :", bit, coord_name, "npc_del;"),
                     f"Underground Pass well {badge}")
    well = UPASS_WELL.read_text()
    require("damage(" not in well,
            "Underground Pass: post-2019 orb-well damage restored")
    require_text(
        UPASS_KOFTIK.read_text(),
        ("case ^upass_complete :", "~obj_gettotal(ibanstaff) = 0",
         "~obj_gettotal(brokenibanstaff) = 0", "inv_add(inv, brokenibanstaff, 1);"),
        "Underground Pass broken staff replacement",
    )


def check_observatory_quest() -> None:
    npc = OBS_NPC.read_text()
    require_text(
        npc,
        (
            "[goblin_guard]", "hitpoints=43", "attack=32", "strength=37",
            "defence=37", "magic=1", "ranged=1", "respawnrate=250",
            "param=attackrate,4", "param=damagetype,0", "param=stabattack,8",
            "param=slashattack,5", "param=strengthbonus,5",
            "param=huntrange,0", "param=death_drop,null",
        ),
        "Observatory Quest guard NPC",
    )

    guard = OBS_GUARD.read_text()
    require_text(
        guard,
        (
            "[opnpc1,qip_obs_goblin_guard]",
            "npc_changetype(goblin_guard, 250);",
            "npc_say(\"Oi, how dare you wake me up!\");",
            "[opnpc2,goblin_guard]", "[ai_queue3,goblin_guard]",
            "obj_add(npc_coord, bones, 1, ^lootdrop_duration);",
            "def_int $drop = random(128);", "if ($drop < 3)",
            "else if ($drop < 90)", "%rag_quest = ^rag_collecting",
            "testbit(%rag_submit, ^rag_bit_goblin) = ^false", "random(4)",
            "obj_add(npc_coord, rag_goblin_bone, 1, ^lootdrop_duration);",
            "random(35)", "arceuus_corpse_goblin", "random(64)",
            "trail_clue_beginner", "$easy_rate = 121;",
            "trail_clue_easy_simple001", "random(5000)",
            "champions_challenge_goblin",
        ),
        "Observatory Quest guard combat/drop table",
    )
    require("keep_key" not in guard,
            "Observatory Quest: guard must never drop the kitchen key")
    require("[ai_queue3,goblin_guard]" not in OBS_GENERIC_GOBLIN.read_text(),
            "Observatory Quest: generic goblin drop binding restored")

    dungeon = OBS_DUNGEON.read_text()
    require_text(
        dungeon,
        (
            "[oploc1,qip_obs_dungeon_chest_closed]",
            "[oploc1,qip_obs_dungeon_chest_closed2]",
            "[oploc1,qip_obs_dungeon_chest_closed3]",
            "~observatory_search_key_chest(0);",
            "~observatory_search_key_chest(1);",
            "~observatory_search_key_chest(2);",
            "%observatory_chestchoice ! $which",
            "~obj_gettotal(keep_key) > 0",
            "inv_add(inv, keep_key, 1);",
            "[oploc1,qip_obs_keep_chest_open]",
            "[proc,observatory_spider_chest_seen]()(boolean)",
            "npc_add(map_findsquare(coord, 1, 1, ^map_findsquare_lineofwalk), poisonspider, 1000);",
            "[oploc1,opendungeonchest]", "inv_add(inv, 1doseantipoison, 1);",
            "[oploc1,keepgate_closed]", "[oploc1,keepgate_closed_left]",
            "%observatory_gatelock = 0 & inv_total(inv, keep_key) = 0",
            "npc_find(loc_coord, qip_obs_goblin_guard, 8, 0)",
            "~door_selfstage_open;",
            "[oploc1,qip_obs_dungeon_stove_top_multi]",
            "%observatory_mould_pres = 1",
            "inv_add(inv, lens_mould, 1);",
            "[opheld5,keep_key]", "[opheld5,lens_mould]", "[opheld5,lens]",
            "%observatory_mould_pres = 0;",
        ),
        "Observatory Quest dungeon route",
    )
    for coord in (
        "0_36_146_31_30", "0_36_146_22_16", "0_36_146_8_56",
        "0_36_146_6_30", "0_36_146_44_39", "0_36_146_29_61",
        "0_36_146_52_36",
    ):
        require(dungeon.count(coord) == 2,
                f"Observatory Quest: spider chest mapping drift at {coord}")
    for seen in range(1, 8):
        require_text(dungeon, (f"%observatory_chest{seen}_seen = 1",),
                     f"Observatory Quest spider chest {seen}")
    require("npc_findhero" not in dungeon and "ai_queue3,goblin_guard" not in dungeon,
            "Observatory Quest: gate route was coupled to killing the guard")

    professor = OBS_PROFESSOR.read_text()
    require_text(
        professor,
        (
            "%observatory_chestchoice = random(3);",
            "%observatory_gatelock = 0;", "%observatory_chest1_seen = 0;",
            "%observatory_chest7_seen = 0;", "%observatory_mould_pres = 0;",
        ),
        "Observatory Quest player-random initialization",
    )
    glass = OBS_GLASS.read_text()
    require_text(
        glass,
        (
            "[opheldu,lens_mould]", "[proc,observatory_cast_lens]",
            "%itgronigen ! ^itgronigen_given_mould", "stat(crafting) < 10",
            "inv_del(inv, molten_glass, 1);", "inv_del(inv, lens_mould, 1);",
            "inv_add(inv, lens, 1);",
        ),
        "Observatory Quest lens recipe",
    )

    world_actors = {
        line.split()[0]
        for line in OBS_WORLD_SPAWN.read_text().splitlines()
        if line.strip() and not line.lstrip().startswith(("//", "="))
    }
    require("qip_obs_goblin_guard" in world_actors,
            "Observatory Quest: sleeping guard map spawn missing")
    require("goblin_guard" not in world_actors,
            "Observatory Quest: awake guard must be a temporary retype")


def check_tourist_trap() -> None:
    captain = TOURIST_CAPTAIN.read_text()
    require_text(
        captain,
        (
            "[opnpc1,desertminingcaptain]",
            "%desertrescue = ^desertrescue_approached_captain;",
            "It's a funny captain who can't fight his own battles!",
            "[label,desertrescue_captain_begin_duel]",
            "%desertrescue_captain_duel = 1;",
            "~npc_retaliate(0);",
            "[opnpc2,desertminingcaptain]",
            "[apnpc2,desertminingcaptain]",
            "[label,desertrescue_captain_attack_warning]",
            "[label,desertrescue_captain_arrest]",
            "damage(uid, hitsplat_damage, 1);",
            "[opnpc3,desertminingcaptain]",
            "[ai_queue3,desertminingcaptain]",
            "if (npc_findhero = ^false)",
            "obj_add_private(npc_coord, bones, 1",
            "%desertrescue = ^desertrescue_killed_capt;",
            "inv_total(bank, metal_key) > 0",
            "inv_add(inv, metal_key, 1);",
            "obj_add_private(npc_coord, metal_key, 1",
        ),
        "The Tourist Trap captain",
    )
    require(captain.count("damage(uid, hitsplat_damage, 1);") == 4,
            "The Tourist Trap: direct-attack arrest must apply four guard hits")
    require("inv_total(worn" not in captain,
            "The Tourist Trap: equipment restriction restored to the duel")
    require("gosub(npc_death)" not in captain,
            "The Tourist Trap: obsolete double NPC-death dispatch restored")

    npc = TOURIST_NPC.read_text()
    require_text(
        npc,
        (
            "[desertminingcaptain]", "hitpoints=80", "attack=32",
            "strength=29", "defence=32", "respawnrate=25",
            "param=attackrate,4", "param=damagetype,1",
            "param=slashattack,9", "param=strengthbonus,14",
            "param=stabdefence,17", "param=slashdefence,15",
            "param=crushdefence,19", "param=magicdefence,-3",
            "param=rangedefence,19", "param=death_drop,null",
        ),
        "The Tourist Trap captain NPC",
    )
    require_text(
        TOURIST_VARP.read_text(),
        ("[desertrescue_captain_duel]", "scope=temp"),
        "The Tourist Trap duel state",
    )
    require("[ai_queue3,desertminingcaptain]" not in TOURIST_GENERIC_DROP.read_text(),
            "The Tourist Trap: duplicate generic captain death hook restored")

    gate = TOURIST_GATE.read_text()
    require_text(
        gate,
        (
            "[oploc2,miningcampgateclosedl]",
            "[oploc2,miningcampgateclosedr]",
            "[oploc1,miningcampgateclosedl]",
            "[oploc1,miningcampgateclosedr]",
            "[oplocu,miningcampgateclosedl]",
            "[oplocu,miningcampgateclosedr]",
            "[proc,desertrescue_player_wearing_armour]()(boolean)",
            "[proc,desertrescue_obj_is_armour](obj $obj)(boolean)",
            "[proc,desertrescue_forbidden_camp_equipment]()(boolean)",
            "oc_category($weapon) ! weapon_pickaxe",
            "[proc,desertrescue_open_camp_gate](int $side)",
            "inv_total(inv, metal_key) < 1",
            "%desertrescue = ^desertrescue_entered_camp;",
            "~door_selfstage_open;",
            "[timer,desertrescue_mercenary_check]",
            "[label,desertrescue_camp_jail]",
        ),
        "The Tourist Trap camp gate",
    )


def check_watchtower() -> None:
    npc = WATCH_NPC.read_text()
    require_text(
        npc,
        (
            "[gorad]", "hitpoints=80", "attack=54", "strength=54",
            "defence=54", "magic=1", "ranged=1", "respawnrate=30",
            "param=attackrate,4", "param=damagetype,2",
            "param=crushattack,8", "param=strengthbonus,6",
            "param=stabdefence,15", "param=slashdefence,27",
            "param=crushdefence,21", "param=magicdefence,0",
            "param=rangedefence,0", "param=death_drop,null",
        ),
        "Watchtower Gorad NPC",
    )

    gorad = WATCH_GORAD.read_text()
    require_text(
        gorad,
        (
            "[opnpc1,gorad]",
            "getbit_range(%itwatchtower_bits, ^itwatchtower_spoken_grew, ^itwatchtower_helped_grew) = 1",
            "~npc_retaliate(0);",
            "[ai_queue3,gorad]", "if (npc_findhero = ^false)",
            "obj_add_private(npc_coord, big_bones, 1",
            "if (random(128) < 19)", "~watchtower_gorad_uncommon_seed;",
            "if (random(30) = 0)", "arceuus_corpse_ogre",
            "if (random(400) = 0)", "dorgesh_construction_bone",
            "if (random(10025) < 2)", "dorgesh_construction_bone_curved",
            "queue(defeat_gorad, 0, 0);", "[queue,defeat_gorad]",
            "getbit_range(%itwatchtower_bits, ^itwatchtower_spoken_grew, ^itwatchtower_helped_grew) ! 1",
            "~obj_gettotal(ogretooth) > 0", "inv_freespace(inv) = 0",
            "inv_add(inv, ogretooth, 1);",
            "[proc,watchtower_gorad_uncommon_seed]()(namedobj, int)",
            "def_int $seed = random(1048);", "if ($seed < 137)",
            "else if ($seed < 1045)", "return(torstol_seed, 1);",
        ),
        "Watchtower Gorad encounter",
    )
    require(gorad.count("return(") == 25,
            "Watchtower: exact uncommon-seed table must retain all 25 outcomes")
    require("obj_add(npc_coord" not in gorad,
            "Watchtower: Gorad drops must remain owner-private")
    require("obj_add_private(npc_coord, ogretooth" not in gorad,
            "Watchtower: quest tooth must remain a direct inventory reward")
    require("%itwatchtower < ^itwatchtower_given_relic" not in gorad,
            "Watchtower: broad legacy tooth eligibility restored")
    require("rag_ogre_bone" not in gorad,
            "Watchtower: ogre ribs must wait for Rag and Bone Man II state")

    grew = WATCH_GREW.read_text()
    require_text(
        grew,
        (
            "[opnpc1,grew]", "[label,grew_spoken]", "[opnpcu,grew]",
            "getbit_range(%itwatchtower_bits, ^itwatchtower_spoken_grew, ^itwatchtower_helped_grew) = 1",
            "inv_total(inv, ogretooth) > 0",
            "setbit_range_toint(%itwatchtower_bits, 2, ^itwatchtower_spoken_grew, ^itwatchtower_helped_grew)",
            "inv_del(inv, ogretooth, 1);", "inv_add(inv, relicpart2, 1);",
            "inv_add(inv, powering_crystal1, 1);",
        ),
        "Watchtower Grew tooth exchange",
    )


def main() -> int:
    try:
        check_manifest()
        check_delrith()
        check_owned_npc_runtime()
        check_witches_experiment()
        check_fight_arena()
        check_hazeel_cult()
        check_grand_tree()
        check_underground_pass()
        check_observatory_quest()
        check_tourist_trap()
        check_watchtower()
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"quest combat contract: {error}", file=sys.stderr)
        return 1
    print("quest combat contract: 145-unit ledger, ownership runtime, Delrith, Witch's experiment, Fight Arena, Hazeel Cult, The Grand Tree, Underground Pass, Observatory Quest, The Tourist Trap and Watchtower (ok)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
