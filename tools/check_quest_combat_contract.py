#!/usr/bin/env python3
"""Structural regression checks for quest encounters that implementation has begun."""

from __future__ import annotations

import csv
import json
import os
import re
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
ARENA_SAMMY = CONTENT / "quests/quest_arena/scripts/sammy_servil.rs2"
ARENA_CUTSCENES = CONTENT / "quests/quest_arena/scripts/quest_arena.rs2"
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
LEGENDS_NEZI = CONTENT / "quests/quest_legends/scripts/nezikchened.rs2"
LEGENDS_NPC = CONTENT / "quests/quest_legends/configs/quest_legends.npc"
LEGENDS_VARP = CONTENT / "quests/quest_legends/configs/quest_legends.varp"
LEGENDS_VARS = CONTENT / "quests/quest_legends/configs/quest_legends.vars"
LEGENDS_UNGADULU = CONTENT / "quests/quest_legends/scripts/ungadulu.rs2"
LEGENDS_ECHNED = CONTENT / "quests/quest_legends/scripts/echned_zekin.rs2"
LEGENDS_GUJUO = CONTENT / "quests/quest_legends/scripts/gujuo.rs2"
LEGENDS_BOOK = CONTENT / "quests/quest_legends/scripts/book_of_binding.rs2"
LEGENDS_BOULDER = CONTENT / "quests/quest_legends/scripts/legends_boulder.rs2"
LEGENDS_YOMMI = CONTENT / "quests/quest_legends/scripts/legends_yommi.rs2"
LEGENDS_QUEST = CONTENT / "quests/quest_legends/scripts/quest_legends.rs2"
LEGENDS_HEROES = (
    CONTENT / "quests/quest_legends/scripts/san_tojalon.rs2",
    CONTENT / "quests/quest_legends/scripts/irvig_senay.rs2",
    CONTENT / "quests/quest_legends/scripts/ranalph_devere.rs2",
)
PLAYER_RANGED = CONTENT / "skill_combat/scripts/player/player_ranged.rs2"
CHOMPY_BIRD = CONTENT / "quests/quest_chompybird/scripts/chompy_bird.rs2"
CHOMPY_BAIT = CONTENT / "quests/quest_chompybird/scripts/bloated_toad.rs2"
CHOMPY_RANTZ = CONTENT / "quests/quest_chompybird/scripts/rantz.rs2"
CHOMPY_RECIPE = CONTENT / "quests/quest_chompybird/scripts/raw_chompy.rs2"
CHOMPY_NPC = CONTENT / "quests/quest_chompybird/configs/quest_chompybird.npc"
CHOMPY_VARN = CONTENT / "quests/quest_chompybird/configs/quest_chompybird.varn"
CHOMPY_ARROWS = CONTENT / "skill_fletching/scripts/ogre_arrows.rs2"
CHOMPY_CHEST = CONTENT / "quests/quest_chompybird/scripts/ogre_chest.rs2"
CHOMPY_TOAD = CONTENT / "quests/quest_chompybird/scripts/swamp_toad.rs2"
CHOMPY_CAVES = CONTENT / "quests/quest_chompybird/scripts/chompy_caves.rs2"
OSF_RELAY = CONTENT / "quests/quest_onesmallfavour/scripts/onesmallfavour_relay.rs2"
OSF_PUZZLES = CONTENT / "quests/quest_onesmallfavour/scripts/onesmallfavour_puzzles.rs2"
OSF_CONSTANT = CONTENT / "quests/quest_onesmallfavour/configs/onesmallfavour.constant"
OSF_VARP = CONTENT / "quests/quest_onesmallfavour/configs/onesmallfavour.varp"
OSF_NPC = CONTENT / "quests/quest_onesmallfavour/configs/onesmallfavour.npc"
OSF_GENERATED_NPC = CONTENT / "npc/configs/combat_stats.generated.npc"
OSF_DWARF_SPAWN = CONTENT / "areas/world/configs/m46_153.spawn"
CONTACT_NPC = CONTENT / "quests/quest_contact/configs/contact.npc"
CONTACT_SCARAB = CONTENT / "quests/quest_contact/scripts/contact_scarab.rs2"
CONTACT_DUNGEON = CONTENT / "quests/quest_contact/scripts/contact_dungeon.rs2"
EYEGLO_NPC = CONTENT / "quests/quest_theeyesofglouphrie/configs/eyeglo.npc"
EYEGLO_QUEST = CONTENT / "quests/quest_theeyesofglouphrie/scripts/eyeglo_quest.rs2"
SWANSONG_NPC = CONTENT / "quests/quest_swansong/configs/swansong.npc"
DREAMMENTOR_NPC = CONTENT / "quests/quest_dreammentor/configs/dreammentor.npc"
DREAMMENTOR_DREAM = CONTENT / "quests/quest_dreammentor/scripts/dreammentor_dream.rs2"
ASCENT_NPC = CONTENT / "quests/quest_ascentofarceuus/configs/ascentofarceuus.npc"
ASCENT_LOCS = CONTENT / "quests/quest_ascentofarceuus/scripts/ascentofarceuus_locs.rs2"
SWANSONG_CONSTANT = CONTENT / "quests/quest_swansong/configs/quest_swansong.constant"
SWANSONG_COLONY = CONTENT / "quests/quest_swansong/scripts/swansong_colony.rs2"
SWANSONG_FINALE = CONTENT / "quests/quest_swansong/scripts/swansong_finale.rs2"
TT_NPC = CONTENT / "quests/quest_troubledtortugans/configs/troubledtortugans.npc"
TT_FIGHTS = CONTENT / "quests/quest_troubledtortugans/scripts/troubledtortugans_fights.rs2"
TT_CORE = CONTENT / "quests/quest_troubledtortugans/scripts/troubledtortugans.rs2"
SLICE_NPC = CONTENT / "quests/quest_anothersliceofham/configs/slice.npc"
SLICE_HAMMAGE = CONTENT / "quests/quest_anothersliceofham/scripts/slice_hammage.rs2"
MAG_NPC = CONTENT / "quests/quest_meatandgreet/configs/meatandgreet.npc"
MAG_FIGHTS = CONTENT / "quests/quest_meatandgreet/scripts/meatandgreet_fights.rs2"
MAG_NPCS = CONTENT / "quests/quest_meatandgreet/scripts/meatandgreet_npcs.rs2"
PLAYER_HIT_FUNNEL = CONTENT / "skill_combat/scripts/player/player_hit_npc_prepare.rs2"
ELEM1_CORE = CONTENT / "quests/quest_elemental_workshop/scripts/quest_elemental_workshop.rs2"
ELEM1_BOOK = CONTENT / "quests/quest_elemental_workshop/scripts/elemental_workshop_shield_book.rs2"
ELEM1_DROPS = CONTENT / "quests/quest_elemental_workshop/scripts/elemental_drops.rs2"
ELEM1_NPC = CONTENT / "quests/quest_elemental_workshop/configs/quest_elemental_workshop.npc"
ELEM_GATHER = CONTENT / "quests/quest_elementalworkshopii/scripts/elem2_gather.rs2"
ELEM2_HELM = CONTENT / "quests/quest_elementalworkshopii/scripts/elem2_helm.rs2"
ELEM2_REPAIR = CONTENT / "quests/quest_elementalworkshopii/scripts/elem2_repair.rs2"
ELEM2_PRIMING = CONTENT / "quests/quest_elementalworkshopii/scripts/elem2_priming.rs2"
ELEM2_SHARED = CONTENT / "quests/quest_elementalworkshopii/scripts/elem2_shared.rs2"
NATURE_GHAST = CONTENT / "quests/quest_druidspirit/scripts/ghast.rs2"
NATURE_NPC = CONTENT / "quests/quest_druidspirit/configs/quest_druidspirit.npc"
NATURE_CORE = CONTENT / "quests/quest_druidspirit/scripts/quest_druidspirit.rs2"
NATURE_FILLIMAN = CONTENT / "quests/quest_druidspirit/scripts/filliman.rs2"
NATURE_DECAY = CONTENT / "quests/quest_druidspirit/scripts/swamp_decay.rs2"
NATURE_GROTTO_SPAWN = CONTENT / "areas/world/configs/m53_152.spawn"
PIP_GUARDIAN = CONTENT / "quests/quest_priestperil/scripts/temple_guardian.rs2"
PIP_NPC = CONTENT / "quests/quest_priestperil/configs/quest_priestperil.npc"
PIP_MONKS = CONTENT / "quests/quest_priestperil/scripts/evil_monks.rs2"
PIP_MONUMENTS = CONTENT / "quests/quest_priestperil/scripts/monuments.rs2"
PIP_WELL = CONTENT / "quests/quest_priestperil/scripts/mausoleum_interactions.rs2"
PIP_DREZEL = CONTENT / "quests/quest_priestperil/scripts/mausoleum_drezel.rs2"
PIP_TRAPPED_DREZEL = CONTENT / "quests/quest_priestperil/scripts/trapped_drezel.rs2"
PIP_COFFIN = CONTENT / "quests/quest_priestperil/scripts/vampire_coffin.rs2"
PIP_TEMPLE_DOORS = CONTENT / "quests/quest_priestperil/scripts/temple_doors.rs2"
PIP_GATES = CONTENT / "areas/area_mausoleum/scripts/gates.rs2"
PIP_KING = CONTENT / "areas/varrock/scripts/king_roald.rs2"
REG_ROUTE = CONTENT / "quests/quest_regicide/scripts/regicide_route.rs2"
REG_TRAPS = CONTENT / "quests/quest_regicide/scripts/regicide_traps.rs2"
REG_GUARD = CONTENT / "quests/quest_regicide/scripts/regicide_tyras_guard.rs2"
REG_DROP = CONTENT / "drop_tables/scripts/wiki_tyras_guard.rs2"
REG_BOMB = CONTENT / "quests/quest_regicide/scripts/regicide_bombcraft.rs2"
REG_STILL = CONTENT / "quests/quest_regicide/scripts/regicide_fractionalising_still.rs2"
REG_KING = CONTENT / "areas/area_ardougne_east/scripts/king_lathas.rs2"
REG_SHARED_STILL = CONTENT / "quests/quest_mourningsendparti/scripts/mend1_poison.rs2"
TBWT_CORE = CONTENT / "quests/quest_tbwt/scripts/quest_tbwt.rs2"
TBWT_MONKEY = CONTENT / "quests/quest_tbwt/scripts/tbwt_monkey.rs2"
TBWT_TAMAYU = CONTENT / "quests/quest_tbwt/scripts/tbwt_tamayu.rs2"
TBWT_JOGRE = CONTENT / "quests/quest_tbwt/scripts/tbwt_jogre_bones.rs2"
TBWT_SHAIKAHAN = CONTENT / "quests/quest_tbwt/scripts/tbwt_shaikahan.rs2"
TBWT_GRIND = CONTENT / "quests/quest_tbwt/configs/tbwt_grind.dbrow"
TBWT_NPC = CONTENT / "quests/quest_tbwt/configs/tbwt_npcs.npc"
TBWT_COOKING = CONTENT / "skill_cooking/scripts/cooking.rs2"
TBWT_COOKING_ROWS = CONTENT / "skill_cooking/configs/cooking_generic.dbrow"
TBWT_FIREMAKING = CONTENT / "skill_firemaking/scripts/firemaking.rs2"
TBWT_SUPERHEAT = CONTENT / "skill_magic/scripts/spells/superheat.rs2"
TBWT_POISON = CONTENT / "skill_combat/scripts/weapon_poison.rs2"
TBWT_TAMAYU_FINAL = CONTENT / "areas/area_karamja/scripts/tbwt_tamayu_final.rs2"
TBWT_TIADECHE_FINAL = CONTENT / "areas/area_karamja/scripts/tbwt_tiadeche_final.rs2"
TBWT_TINSAY_FINAL = CONTENT / "areas/area_karamja/scripts/tbwt_tinsay_final.rs2"
TBWT_TAMAYU_SHOP = CONTENT / "shop/tai_bwo_wannai/scripts/tamayus_spear_stall__1.rs2"
TBWT_TIADECHE_SHOP = CONTENT / "shop/tai_bwo_wannai/scripts/tiadeches_karambwan_stall.rs2"
TBWT_TAMAYU_STOCK = CONTENT / "shop/tai_bwo_wannai/configs/tamayus_spear_stall__1.inv"
TBWT_TIADECHE_STOCK = CONTENT / "shop/tai_bwo_wannai/configs/tiadeches_karambwan_stall.inv"
TBWT_COMBAT_NPC = CONTENT / "npc/configs/combat_stats.generated.npc"
TROLL_CORE = CONTENT / "quests/quest_troll/scripts/quest_troll.rs2"
TROLL_DAD = CONTENT / "quests/quest_troll/scripts/troll_champion.rs2"
TROLL_GUARDS = CONTENT / "quests/quest_troll/scripts/troll_stronghold_camp_guard.rs2"
TROLL_NPC = CONTENT / "quests/quest_troll/configs/quest_troll.npc"
TROLL_GENERAL_DROP = CONTENT / "drop_tables/scripts/troll_commander.rs2"
TROLL_GUARD_DROP = CONTENT / "drop_tables/scripts/mountain_troll.rs2"
TROLL_DENULTH = CONTENT / "quests/quest_death/scripts/death_denulth.rs2"
TROLL_DUNSTAN = CONTENT / "quests/quest_death/scripts/death_dunstan.rs2"
TROLL_DAD_SPAWN = CONTENT / "areas/world/configs/m45_56.spawn"
TROLL_PRISON_SPAWN = CONTENT / "areas/world/configs/m44_157.spawn"
MORTTON_CORE = CONTENT / "quests/quest_mortton/scripts/quest_mortton.rs2"
MORTTON_RAZMIRE = CONTENT / "quests/quest_mortton/scripts/razmire_keelgan.rs2"
MORTTON_ULSQUIRE = CONTENT / "quests/quest_mortton/scripts/ulsquire_shauncy.rs2"
MORTTON_AFFLICTED = CONTENT / "quests/quest_mortton/scripts/afflicted.rs2"
MORTTON_SHADES = CONTENT / "minigames/game_mortton/scripts/mortton_shades.rs2"
MORTTON_TEMPLE = CONTENT / "minigames/game_mortton/scripts/flamtaer_temple.rs2"
MORTTON_PYRE = CONTENT / "minigames/game_mortton/scripts/mortton_pyre.rs2"
MORTTON_CATACOMBS = CONTENT / "minigames/game_mortton/scripts/mortton_catacombs.rs2"
MORTTON_COFFIN = CONTENT / "minigames/game_mortton/scripts/mortton_coffin.rs2"
MORTTON_PYRE_CONFIG = CONTENT / "minigames/game_mortton/configs/flamtaer_pyre.struct"
MORTTON_SHADE_CONFIG = CONTENT / "minigames/game_mortton/configs/mortton_shades.struct"
MORTTON_COFFIN_CONFIG = CONTENT / "minigames/game_mortton/configs/mortton_coffin.inv"
VIKING_NPC = CONTENT / "quests/quest_viking/configs/quest_viking.npc"
VIKING_CONSTANT = CONTENT / "quests/quest_viking/configs/quest_viking.constant"
VIKING_VARP = CONTENT / "quests/quest_viking/configs/quest_viking.varp"
VIKING_SIGLI = CONTENT / "quests/quest_viking/scripts/viking_sigli.rs2"
VIKING_THORVALD = CONTENT / "quests/quest_viking/scripts/viking_thorvald.rs2"
VIKING_PEER = CONTENT / "quests/quest_viking/scripts/viking_peer.rs2"
VIKING_CITIZENS = CONTENT / "quests/quest_viking/scripts/viking_citizen_drops.rs2"
VIKING_GENERATED_ANIMS = CONTENT / "npc/configs/npc_anims.generated.npc"
HORROR_INTERACTIONS = CONTENT / "quests/quest_horror/scripts/horror_interactions.rs2"
HORROR_ENCOUNTER = CONTENT / "quests/quest_horror/scripts/horror_encounter.rs2"
HORROR_JOSSIK = CONTENT / "quests/quest_horror/scripts/horror_jossik.rs2"
HORROR_NPC = CONTENT / "quests/quest_horror/configs/quest_horror.npc"
HORROR_VARP = CONTENT / "quests/quest_horror/configs/quest_horror.varp"
HORROR_CONSTANT = CONTENT / "quests/quest_horror/configs/quest_horror.constant"
HORROR_GUNNJORN = CONTENT / "areas/area_barbarian_outpost/scripts/gunnjorn.rs2"
HORROR_MAGIC = CONTENT / "skill_combat/scripts/player/player_magic.rs2"
HORROR_GENERATED_ANIMS = CONTENT / "npc/configs/npc_anims.generated.npc"
HORROR_HIT_FUNNEL = CONTENT / "skill_combat/scripts/player/player_hit_npc_prepare.rs2"
MM1_DEMON = CONTENT / "quests/quest_mm/scripts/mm_demon.rs2"
MM1_NPC = CONTENT / "quests/quest_mm/configs/quest_mm.npc"
MM1_VARP = CONTENT / "quests/quest_mm/configs/quest_mm.varp"
MM1_GARKOR = CONTENT / "quests/quest_mm/scripts/mm_garkor.rs2"
MM1_ZOOKNOCK = CONTENT / "quests/quest_mm/scripts/mm_zooknock.rs2"
MM1_NARNODE = CONTENT / "quests/quest_mm/scripts/mm_narnode.rs2"
MM1_DAERO = CONTENT / "quests/quest_mm/scripts/mm_daero.rs2"
MM1_GENERATED_ANIMS = CONTENT / "npc/configs/npc_anims.generated.npc"
HMQ_DAYTH = CONTENT / "quests/quest_hauntedmine/scripts/hauntedmine_dayth.rs2"
HMQ_DUNGEON = CONTENT / "quests/quest_hauntedmine/scripts/hauntedmine_dungeon.rs2"
HMQ_ZEALOT = CONTENT / "quests/quest_hauntedmine/scripts/hauntedmine_zealot.rs2"
HMQ_NPC = CONTENT / "quests/quest_hauntedmine/configs/quest_hauntedmine.npc"
HMQ_VARP = CONTENT / "quests/quest_hauntedmine/configs/quest_hauntedmine.varp"
HMQ_CONSTANT = CONTENT / "quests/quest_hauntedmine/configs/quest_hauntedmine.constant"
HMQ_STRINGING = CONTENT / "skill_crafting/scripts/jewellery/stringing.rs2"
HMQ_GENERATED_ANIMS = CONTENT / "npc/configs/npc_anims.generated.npc"
TROLLLOVE_ARRG = CONTENT / "quests/quest_troll_love/scripts/trollromance_arrg.rs2"
TROLLLOVE_SLED = CONTENT / "quests/quest_troll_love/scripts/trollromance_sled.rs2"
TROLLLOVE_UG = CONTENT / "quests/quest_troll_love/scripts/trollromance_ug.rs2"
TROLLLOVE_NPC = CONTENT / "quests/quest_troll_love/configs/quest_troll_love.npc"
TROLLLOVE_VARP = CONTENT / "quests/quest_troll_love/configs/quest_troll_love.varp"
TROLLLOVE_CONSTANT = CONTENT / "quests/quest_troll_love/configs/quest_troll_love.constant"
TROLLLOVE_DUNSTAN = CONTENT / "quests/quest_death/scripts/death_dunstan.rs2"
TROLLLOVE_GENERATED_ANIMS = CONTENT / "npc/configs/npc_anims.generated.npc"
ROUTEQUEST_ROUTE = CONTENT / "quests/quest_routequest/scripts/routequest_start_and_route.rs2"
ROUTEQUEST_HIDEOUT = CONTENT / "quests/quest_routequest/scripts/routequest_hideout.rs2"
ROUTEQUEST_HOUND = CONTENT / "quests/quest_routequest/scripts/routequest_hound.rs2"
MDQ_KENDAL = CONTENT / "quests/quest_mountaindaughter/scripts/mountaindaughter_kendal.rs2"
ROUTEQUEST_NPC = CONTENT / "quests/quest_routequest/configs/quest_routequest.npc"
ROUTEQUEST_VARP = CONTENT / "quests/quest_routequest/configs/quest_routequest.varp"
ROUTEQUEST_CONSTANT = CONTENT / "quests/quest_routequest/configs/quest_routequest.constant"
ROUTEQUEST_MYREQUE2 = CONTENT / "quests/quest_inaidofthemyreque/scripts/myreque2_hideout.rs2"
FENK_CORE = CONTENT / "quests/quest_fenkenstrain/scripts/fenkenstrain.rs2"
FENK_FINISH = CONTENT / "quests/quest_fenkenstrain/scripts/fenkenstrain_finish.rs2"
FENK_PARTS = CONTENT / "quests/quest_fenkenstrain/scripts/fenkenstrain_parts.rs2"
FENK_LIGHTNING = CONTENT / "quests/quest_fenkenstrain/scripts/fenkenstrain_lightning.rs2"
FENK_CONSTANT = CONTENT / "quests/quest_fenkenstrain/configs/fenkenstrain.constant"
FENK_EXPERIMENT_DROP = CONTENT / "drop_tables/scripts/wiki_experiment.rs2"
FENK_SMELTING = CONTENT / "skill_smithing/scripts/smelting/smelting.rs2"
FENK_GENERATED_COMBAT = CONTENT / "npc/configs/combat_stats.generated.npc"
ROVING_CONSTANT = CONTENT / "quests/quest_rovingelves/configs/quest_rovingelves.constant"
ROVING_MOSS = CONTENT / "quests/quest_rovingelves/scripts/rovingelves_mossgiant.rs2"
ROVING_SEED = CONTENT / "quests/quest_rovingelves/scripts/rovingelves_seed.rs2"
ROVING_ELUNED = CONTENT / "quests/quest_rovingelves/scripts/rovingelves_eluned.rs2"
ROVING_ISLWYN = CONTENT / "quests/quest_rovingelves/scripts/rovingelves_islwyn.rs2"
ROVING_WATERFALL_LOCS = CONTENT / "quests/quest_waterfall/scripts/quest_waterfall_locs.rs2"
ROVING_LEVEL_REQUIRE = CONTENT / "skill_combat/scripts/levelrequire.rs2"
ROVING_GENERATED_COMBAT = CONTENT / "npc/configs/combat_stats.generated.npc"
AHOY_CONSTANT = CONTENT / "quests/quest_ghostsahoy/configs/ghostsahoy.constant"
AHOY_VARP = CONTENT / "quests/quest_ghostsahoy/configs/ghostsahoy.varp"
AHOY_NPC = CONTENT / "quests/quest_ghostsahoy/configs/ghostsahoy.npc"
AHOY_BOOK = CONTENT / "quests/quest_ghostsahoy/scripts/ahoy_book.rs2"
AHOY_HUB = CONTENT / "quests/quest_ghostsahoy/scripts/ahoy_hub.rs2"
AHOY_MANUAL = CONTENT / "quests/quest_ghostsahoy/scripts/ahoy_manual.rs2"
AHOY_ROBES = CONTENT / "quests/quest_ghostsahoy/scripts/ahoy_robes.rs2"
AHOY_SHARED = CONTENT / "quests/quest_ghostsahoy/scripts/ahoy_shared.rs2"
AHOY_COOKING = CONTENT / "skill_cooking/scripts/cooking.rs2"
AHOY_DYES = CONTENT / "skill_crafting/scripts/dye_cape.rs2"
CATEGORY_PACK = ROOT / "OSRS-Content/osrs239-content/pack/category.pack"
COMBAT_STATS = CONTENT / "skill_combat/combat_stats.rs2"
PLAYER_DEATH = CONTENT / "player/death.rs2"
PLAYER_LOGOUT = CONTENT / "player/logout.rs2"
PLAYER_LOGIN = CONTENT / "player/login.rs2"
PLAYER_DROP = CONTENT / "player/scripts/drop.rs2"
CRUMBLE_UNDEAD = CONTENT / "skill_combat/scripts/player/spells/crumble_undead.rs2"
COMBAT_XP = CONTENT / "skill_combat/combat.rs2"
SHOP_GENERATOR = ROOT / "tools/gen_shop_scripts.py"
QUEST_MANIFEST_GENERATOR = ROOT / "tools/generate_quest_combat_manifest.py"
PIP_MYREQUE_WELL = CONTENT / "quests/quest_inaidofthemyreque/scripts/myreque2_rod.rs2"
PIP_WORLD_SPAWN = CONTENT / "areas/world/configs/m53_154.spawn"
PIP_VARP_ALLOC = ROOT / "OSRS-Content/osrs239-content/pack/varp.alloc"
POWERED_STAFF = CONTENT / "skill_combat/scripts/player/gear/powered_staff.rs2"
PLAYER_HIT_FUNNEL = CONTENT / "skill_combat/scripts/player/player_hit_npc_prepare.rs2"
NPC_ALLOC = ROOT / "OSRS-Content/osrs239-content/pack/npc.alloc"
NPC_CLIENT = ROOT / "OSRS-Content/osrs239-content/pack/npc.client"
COMBAT_PARAM = CONTENT / "skill_combat/configs/combat.param"
SPELL_CONSTANTS = CONTENT / "skill_combat/configs/magic/spells.constant"
PLAYER_MAGIC = CONTENT / "skill_combat/scripts/player/player_magic.rs2"
PARAM_ALLOC = ROOT / "OSRS-Content/osrs239-content/pack/param.alloc"
SPAWN_GENERATOR = ROOT / "tools/gen_spawns.py"
MOCK_HEADER = ROOT / "src/torirsserver/torirs_server.h"
MOCK_WORLD = ROOT / "src/torirsserver/torirs_server_world.c"
# The self-test was split out of the world; its assertions are still part of
# what this contract reads, so the two are read as one text.
MOCK_WORLD_SELFTEST = ROOT / "src/torirsserver/torirs_server_world_selftest.c"
MOCK_ENCODE = ROOT / "src/torirsserver/torirs_server_encode.c"
MOCK_SCRIPTS = ROOT / "src/torirsserver/torirs_server_scripts.c"
MOCK_NPC_OPS = ROOT / "src/torirsserver/torirs_server_ops_npc.c"
MOCK_CONTENT = ROOT / "src/torirsserver/torirs_server_content.c"
MOCK_CONTENT_HEADER = ROOT / "src/torirsserver/torirs_server_content.h"
CONTENT_REGISTER = ROOT / "src/content/content_register.c"
CONTENT_INI = ROOT / "OSRS-Content/osrs239-content/content.ini"
VARN_ALLOC = ROOT / "OSRS-Content/osrs239-content/pack/varn.alloc"


def _load_var_names() -> dict[str, str]:
    """Base name -> spelled name. Var names carry their kind and id
    (`ca_tier_status_hard` is `varb<id>_ca_tier_status_hard`), so a needle
    built from a base name goes through this."""
    tree = ROOT / "OSRS-Content/osrs239-content"
    names = {}
    for path in (tree / "configs/all.varbit.compack", tree / "configs/all.varp.compack",
                 tree / "configs/all.varc.compack", tree / "pack/varp.alloc"):
        for name in re.findall(r"^\d+=(\S+)", path.read_text(errors="replace"), re.M):
            m = re.match(r"^var[bpc]\d+_(.+)$", name)
            if m:
                names[m.group(1)] = name
    return names


VAR_NAMES = _load_var_names()


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
    legends = [row for row in rows if row["id"] == "quest-legends-quest"]
    require(len(legends) == 1,
            "manifest: expected exactly one Legends' Quest row")
    require(legends[0]["implementation_status"] == "implementation-in-progress",
            "Legends' Quest: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(legends[0][key]),
                f"Legends' Quest: empty evidence field {key}")
    chompy = [row for row in rows if row["id"] == "quest-big-chompy-bird-hunting"]
    require(len(chompy) == 1,
            "manifest: expected exactly one Big Chompy Bird Hunting row")
    require(chompy[0]["implementation_status"] == "implementation-in-progress",
            "Big Chompy Bird Hunting: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(chompy[0][key]),
                f"Big Chompy Bird Hunting: empty evidence field {key}")
    for quest_id, name in (
        ("quest-elemental-workshop-i", "Elemental Workshop I"),
        ("quest-elemental-workshop-ii", "Elemental Workshop II"),
    ):
        elemental = [row for row in rows if row["id"] == quest_id]
        require(len(elemental) == 1, f"manifest: expected exactly one {name} row")
        require(elemental[0]["implementation_status"] == "implementation-in-progress",
                f"{name}: status drift")
        for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
            require(bool(elemental[0][key]), f"{name}: empty evidence field {key}")
    nature = [row for row in rows if row["id"] == "quest-nature-spirit"]
    require(len(nature) == 1, "manifest: expected exactly one Nature Spirit row")
    require(nature[0]["implementation_status"] == "implementation-in-progress",
            "Nature Spirit: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(nature[0][key]), f"Nature Spirit: empty evidence field {key}")
    priest = [row for row in rows if row["id"] == "quest-priest-in-peril"]
    require(len(priest) == 1, "manifest: expected exactly one Priest in Peril row")
    require(priest[0]["implementation_status"] == "implementation-in-progress",
            "Priest in Peril: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(priest[0][key]), f"Priest in Peril: empty evidence field {key}")
    regicide = [row for row in rows if row["id"] == "quest-regicide"]
    require(len(regicide) == 1, "manifest: expected exactly one Regicide row")
    require(regicide[0]["implementation_status"] == "implementation-in-progress",
            "Regicide: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(regicide[0][key]), f"Regicide: empty evidence field {key}")
    tbwt = [row for row in rows if row["id"] == "quest-tai-bwo-wannai-trio"]
    require(len(tbwt) == 1, "manifest: expected exactly one Tai Bwo Wannai Trio row")
    require(tbwt[0]["implementation_status"] == "implementation-in-progress",
            "Tai Bwo Wannai Trio: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals", "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(tbwt[0][key]), f"Tai Bwo Wannai Trio: empty evidence field {key}")
    revisions = {audit["revision"] for audit in tbwt[0]["source_audits"]}
    require({15265886, 15267185, 15302415, 15217840, 15196252,
             14918139, 15070106, 15196408, 15206313} <= revisions,
            "Tai Bwo Wannai Trio: pinned Wiki audit set drifted")
    troll = [row for row in rows if row["id"] == "quest-troll-stronghold"]
    require(len(troll) == 1, "manifest: expected exactly one Troll Stronghold row")
    require(troll[0]["implementation_status"] == "implementation-in-progress",
            "Troll Stronghold: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals",
                "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(troll[0][key]), f"Troll Stronghold: empty evidence field {key}")
    revisions = {audit["revision"] for audit in troll[0]["source_audits"]}
    require({15231622, 14728817, 15263286, 15199512, 15267877,
             15199781, 15199816, 15267891, 15185591, 15185592,
             15031769, 15003284, 15289542, 15284766} <= revisions,
            "Troll Stronghold: pinned Wiki audit set drifted")
    mortton = [row for row in rows if row["id"] == "quest-shades-of-mort-ton"]
    require(len(mortton) == 1, "manifest: expected exactly one Shades of Mort'ton row")
    require(mortton[0]["implementation_status"] == "implementation-in-progress",
            "Shades of Mort'ton: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals",
                "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(mortton[0][key]), f"Shades of Mort'ton: empty evidence field {key}")
    revisions = {audit["revision"] for audit in mortton[0]["source_audits"]}
    require({15292280, 14988872, 15263293, 15199268, 15299270,
             15214951, 15185388, 15183419, 15214952, 15115043,
             14879578, 15285193, 15233566, 15200239} <= revisions,
            "Shades of Mort'ton: pinned Wiki audit set drifted")
    fremennik = [row for row in rows if row["id"] == "quest-the-fremennik-trials"]
    require(len(fremennik) == 1,
            "manifest: expected exactly one The Fremennik Trials row")
    require(fremennik[0]["implementation_status"] == "implementation-in-progress",
            "The Fremennik Trials: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals",
                "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(fremennik[0][key]),
                f"The Fremennik Trials: empty evidence field {key}")
    revisions = {audit["revision"] for audit in fremennik[0]["source_audits"]}
    require({15292303, 15290109, 15263294, 15293789, 15215949,
             15184168, 15183020, 15239757, 15136941, 15199225,
             15199667, 15199668, 15199666, 15183561} <= revisions,
            "The Fremennik Trials: pinned Wiki audit set drifted")
    horror = [row for row in rows if row["id"] == "quest-horror-from-the-deep"]
    require(len(horror) == 1,
            "manifest: expected exactly one Horror from the Deep row")
    require(horror[0]["implementation_status"] == "implementation-in-progress",
            "Horror from the Deep: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals",
                "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(horror[0][key]),
                f"Horror from the Deep: empty evidence field {key}")
    revisions = {audit["revision"] for audit in horror[0]["source_audits"]}
    require({15294310, 15080801, 15263295, 15274475, 15199457,
             15254124, 15174921, 15196239, 15183873, 15300898,
             15300896, 15294284} <= revisions,
            "Horror from the Deep: pinned Wiki audit set drifted")
    mm1 = [row for row in rows if row["id"] == "quest-monkey-madness-i"]
    require(len(mm1) == 1,
            "manifest: expected exactly one Monkey Madness I row")
    require(mm1[0]["implementation_status"] == "implementation-in-progress",
            "Monkey Madness I: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals",
                "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(mm1[0][key]), f"Monkey Madness I: empty evidence field {key}")
    revisions = {audit["revision"] for audit in mm1[0]["source_audits"]}
    require({15302455, 15257376, 15263300, 15199289, 15182884,
             15238361, 15238401, 15286805} <= revisions,
            "Monkey Madness I: pinned Wiki audit set drifted")
    haunted = [row for row in rows if row["id"] == "quest-haunted-mine"]
    require(len(haunted) == 1,
            "manifest: expected exactly one Haunted Mine row")
    require(haunted[0]["implementation_status"] == "implementation-in-progress",
            "Haunted Mine: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals",
                "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(haunted[0][key]), f"Haunted Mine: empty evidence field {key}")
    revisions = {audit["revision"] for audit in haunted[0]["source_audits"]}
    require({15292305, 14834641, 15263301, 15234627, 15183506,
             15183507, 15183505, 15183508, 15241628} <= revisions,
            "Haunted Mine: pinned Wiki audit set drifted")
    troll_love = [row for row in rows if row["id"] == "quest-troll-romance"]
    require(len(troll_love) == 1,
            "manifest: expected exactly one Troll Romance row")
    require(troll_love[0]["implementation_status"] == "implementation-in-progress",
            "Troll Romance: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals",
                "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(troll_love[0][key]), f"Troll Romance: empty evidence field {key}")
    revisions = {audit["revision"] for audit in troll_love[0]["source_audits"]}
    require({15292383, 14845426, 15263302, 15215810, 15239793,
             15185234, 15184737, 15095285, 15109118} <= revisions,
            "Troll Romance: pinned Wiki audit set drifted")
    myreque = [row for row in rows if row["id"] == "quest-in-search-of-the-myreque"]
    require(len(myreque) == 1,
            "manifest: expected exactly one In Search of the Myreque row")
    require(myreque[0]["implementation_status"] == "implementation-in-progress",
            "In Search of the Myreque: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals",
                "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(myreque[0][key]),
                f"In Search of the Myreque: empty evidence field {key}")
    revisions = {audit["revision"] for audit in myreque[0]["source_audits"]}
    require({15292283, 14479041, 15286926, 15199509, 15013176, 15263062} <= revisions,
            "In Search of the Myreque: pinned Wiki audit set drifted")
    fenk = [row for row in rows if row["id"] == "quest-creature-of-fenkenstrain"]
    require(len(fenk) == 1,
            "manifest: expected exactly one Creature of Fenkenstrain row")
    require(fenk[0]["implementation_status"] == "implementation-in-progress",
            "Creature of Fenkenstrain: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals",
                "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(fenk[0][key]),
                f"Creature of Fenkenstrain: empty evidence field {key}")
    revisions = {audit["revision"] for audit in fenk[0]["source_audits"]}
    require({15292324, 15087904, 15263305, 15199186, 15184609,
             15201910, 14684724} <= revisions,
            "Creature of Fenkenstrain: pinned Wiki audit set drifted")
    roving = [row for row in rows if row["id"] == "quest-roving-elves"]
    require(len(roving) == 1,
            "manifest: expected exactly one Roving Elves row")
    require(roving[0]["implementation_status"] == "implementation-in-progress",
            "Roving Elves: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals",
                "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(roving[0][key]), f"Roving Elves: empty evidence field {key}")
    revisions = {audit["revision"] for audit in roving[0]["source_audits"]}
    require({15292285, 14458305, 15272093, 15199663, 15267068,
             15184516, 15201942, 15292719, 15291690} <= revisions,
            "Roving Elves: pinned Wiki audit set drifted")
    ahoy = [row for row in rows if row["id"] == "quest-ghosts-ahoy"]
    require(len(ahoy) == 1,
            "manifest: expected exactly one Ghosts Ahoy row")
    require(ahoy[0]["implementation_status"] == "implementation-in-progress",
            "Ghosts Ahoy: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals",
                "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(ahoy[0][key]), f"Ghosts Ahoy: empty evidence field {key}")
    revisions = {audit["revision"] for audit in ahoy[0]["source_audits"]}
    require({15297463, 15297469, 15297814, 15272821, 15185508,
             14888479, 15297464, 15293864, 14768954} <= revisions,
            "Ghosts Ahoy: pinned Wiki audit set drifted")
    osf = [row for row in rows if row["id"] == "quest-one-small-favour"]
    require(len(osf) == 1,
            "manifest: expected exactly one One Small Favour row")
    require(osf[0]["implementation_status"] == "implementation-in-progress",
            "One Small Favour: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals",
                "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(osf[0][key]),
                f"One Small Favour: empty evidence field {key}")
    urls = {audit["url"] for audit in osf[0]["source_audits"]}
    require({
        "https://oldschool.runescape.wiki/w/One_Small_Favour",
        "https://oldschool.runescape.wiki/w/One_Small_Favour/Quick_guide",
        "https://oldschool.runescape.wiki/w/Slagilith",
        "https://oldschool.runescape.wiki/w/Dwarf_gang_member",
        "https://oldschool.runescape.wiki/w/Animate_rock_scroll",
    } <= urls, "One Small Favour: live Wiki audit set drifted")
    contact = [row for row in rows if row["id"] == "quest-contact"]
    require(len(contact) == 1, "manifest: expected exactly one Contact! row")
    require(contact[0]["implementation_status"] == "implementation-in-progress",
            "Contact!: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals",
                "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(contact[0][key]), f"Contact!: empty evidence field {key}")
    revisions = {audit["revision"] for audit in contact[0]["source_audits"]}
    require({15328051, 15327950, 15233716, 15281959, 15281960, 15200671} <= revisions,
            "Contact!: pinned Wiki audit set drifted")
    swan = [row for row in rows if row["id"] == "quest-swan-song"]
    require(len(swan) == 1, "manifest: expected exactly one Swan Song row")
    require(swan[0]["implementation_status"] == "implementation-in-progress",
            "Swan Song: status drift")
    for key in ("source_audits", "npc_gamevals", "item_gamevals", "loc_gamevals",
                "trigger_handlers", "loot_contract", "test_ids", "known_gaps"):
        require(bool(swan[0][key]), f"Swan Song: empty evidence field {key}")
    revisions = {audit["revision"] for audit in swan[0]["source_audits"]}
    require({15215925, 15329222, 15359363} <= revisions,
            "Swan Song: pinned Wiki audit set drifted")


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
            "[proc,demon_slayer_incantation_ready]()(boolean)",
            "%varb2562_delrith_incantation_1 = random(5);",
            "def_int $word_5 = ~demon_slayer_choose_incantation_word(\"fifth\");",
            "calc($word_5 - 1) = %varb2566_delrith_incantation_5",
            "[opnpc2,delrith]",
            "@player_combat_start;",
            "[apnpc2,delrith]",
            "@player_combat_start_ap;",
            "[ai_queue2,delrith]",
            "$damage = calc($damage * 2);",
            "[ai_queue3,delrith]",
            "npc_setowner;",
            "~demon_slayer_ready_for_delrith = false",
            "inv_total(worn, silverlight) > 0",
            "npc_changetype(delrith_weakened, 500);",
            "[opnpc1,delrith_weakened]",
            "queue(demon_slayer_start_incantation, 0, npc_uid);",
            "[queue,demon_slayer_start_incantation](npc_uid $delrith)",
            "npc_finduid($delrith)",
            "npc_statheal(hitpoints, 0, 100);",
            "npc_del;",
            "~demon_slayer_commit_complete;",
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
            "[varp6628_demon_delrith_engaged]",
            "scope=temp",
            "[varp6632_demon_incantation_1]",
            "[varp6633_demon_incantation_2]",
            "[varp6634_demon_incantation_3]",
            "[varp6635_demon_incantation_4]",
            "[varp6636_demon_incantation_5]",
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
    world = MOCK_WORLD.read_text() + MOCK_WORLD_SELFTEST.read_text()
    encode = MOCK_ENCODE.read_text()
    scripts = MOCK_SCRIPTS.read_text()
    npc_ops = MOCK_NPC_OPS.read_text()
    require_text(
        header,
        ("ToriRSServer_WorldNpcVisibleTo", "owned npcs are private to the exact login"),
        "owned NPC API",
    )
    require_text(
        world,
        (
            "ToriRSServer_WorldNpcVisibleTo(",
            "ToriRSServer_WorldNpcOwner(srv, npc) == player",
            "an owned npc is hidden from another player",
            "abandoned private encounter actor",
        ),
        "owned NPC lifecycle",
    )
    require(encode.count("ToriRSServer_WorldNpcVisibleTo(srv, npc, player)") >= 4,
            "owned NPC encoding: both wire paths must filter tracked and added NPCs")
    require(scripts.count("ToriRSServer_WorldNpcVisibleTo") >= 4,
            "owned NPC script lookup: find-all/find/finduid visibility gates missing")
    require(npc_ops.count("ToriRSServer_WorldNpcVisibleTo") >= 2,
            "owned NPC iterator lookup: zone/hunt visibility gates missing")

    require_text(
        CONTENT_REGISTER.read_text(),
        ('{ "varn",', "CONTENT_NAMES_AUTHORED", '{ "vars",'),
        "NPC/world variable namespace registry",
    )
    require_text(
        CONTENT_INI.read_text(),
        ("[namespace:varn]", "[namespace:vars]", "names     = authored"),
        "NPC/world variable namespace declarations",
    )
    require_text(
        MOCK_CONTENT_HEADER.read_text(),
        ("TORIRSSERVER_PACK_VARN", "TORIRSSERVER_PACK_VARS"),
        "NPC/world variable pack kinds",
    )
    require_text(
        MOCK_CONTENT.read_text(),
        (
            '[TORIRSSERVER_PACK_VARN] = "varn"',
            '[TORIRSSERVER_PACK_VARS] = "vars"',
            "highest >= TORIRSSERVER_NPC_VAR_MAX",
            "highest >= TORIRSSERVER_VARS_COUNT",
        ),
        "NPC/world variable allocation bounds",
    )
    require_text(
        scripts,
        (
            "case SS_OP_PUSH_VARN:", "npc->script_vars[varn]",
            "case SS_OP_POP_VARN:", "push_varn with no active npc",
            "pop_varn with no active npc",
        ),
        "NPC-local variable VM",
    )
    require_text(
        VARN_ALLOC.read_text(),
        ("=chompy_baiter", "=chompy_target_toad"),
        "NPC-local variable allocation ledger",
    )


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
            "%varp226_ballquest = ^ball_defeated_experiment;",
            "%varp6660_ball_shed_unlocked = 1;",
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
    require_text(WITCH_VARP.read_text(), ("[varp6660_ball_shed_unlocked]", "scope=temp"),
                 "Witch's House shed lock state")


def check_fight_arena() -> None:
    encounter = ARENA.read_text()
    require_text(
        encounter,
        (
            "[label,arena_start_ogre]",
            "@ogre_attack_justin;",
            "[label,arena_start_scorpion]",
            "@arena_release_scorp;",
            "[label,arena_start_bouncer]",
            "@arena_release_bouncer;",
            "[label,arena_start_general]",
            "npc_add(0_40_49_44_18, general_khazard_arena, 32000);",
            "[label,arena_after_ogre]",
            "%varp17_arenaquest = ^arena_sent_jail;",
            "[label,arena_general_after_bouncer]",
            "%varp17_arenaquest = ^arena_freed_servils;",
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
    # The ogre, scorpion and Bouncer are created by the pen cutscenes LostCity
    # scripts (sammy_servil.rs2 ogre_attack_justin = jeremy_servil.rs2:95-146;
    # quest_arena.rs2 arena_release_scorp/_bouncer = quest_arena.rs2:193-265),
    # in LostCity's pen tiles; the General by arena_spawn_general. Each one's
    # npc_add must be followed at once by npc_setowner.
    sammy_text = ARENA_SAMMY.read_text()
    cutscenes = ARENA_CUTSCENES.read_text()
    require_text(sammy_text, ("[label,ogre_attack_justin]", "def_coord $ogre_to = 0_40_49_46_29;"),
                 "Fight Arena ogre-pen cutscene")
    require_text(cutscenes, ("[label,arena_release_scorp]", "def_coord $scorp_spawn = 0_40_49_47_23;",
                             "[label,arena_release_bouncer]", "def_coord $bouncer_spawn = 0_40_49_47_26;"),
                 "Fight Arena pen cutscenes")
    for text, spawn in (
        (sammy_text, "npc_add(movecoord($ogre_to, 1, 0, 0), arena_ogre, 32000);"),
        (cutscenes, "npc_add($scorp_spawn, arena_scorpion, 32000);"),
        (cutscenes, "npc_add($bouncer_spawn, arena_bouncer, 32000);"),
        (encounter, "npc_add(0_40_49_44_18, general_khazard_arena, 32000);"),
    ):
        require(text.count(spawn) == 1, f"Fight Arena: expected one {spawn}")
        after = text.split(spawn, 1)[1].lstrip().splitlines()[0].strip()
        require(after == "npc_setowner;",
                f"Fight Arena: {spawn} not followed by npc_setowner; (every dynamically spawned combat type must be owner-private)")
    require("random(400) = 0" in encounter and "random(5013) = 0" in encounter,
            "Fight Arena: Ogre tertiary drop rates drifted")

    locs = ARENA_LOCS.read_text()
    require_text(
        locs,
        (
            "[oploc1,arena_guard_chest_shut]",
            "[label,arena_search_chest]",
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

    # The curated spawn rows are read as ROWS (first word of every non-comment
    # line after "==== NPC ===="): a substring test passed on a comment that
    # merely named an actor. The arena Sammy is not a spawn row at all since
    # seam29 -- the rev-239 shell sammy_servil_arena draws the op-less
    # sammy_servil_vis_noop, so ~arena_spawn_sammy places an owner-private
    # sammy_servil_vis instead (sammy_servil.rs2) and every round start calls it.
    curated_rows = set()
    in_npc = False
    for line in ARENA_SPAWN.read_text().splitlines():
        stripped = line.strip()
        if stripped.startswith("===="):
            in_npc = stripped.strip("= ").upper() == "NPC"
            continue
        if not in_npc or not stripped or stripped.startswith("//"):
            continue
        curated_rows.add(stripped.split()[0])
    for actor in ("lady_servil", "arena_guard2", "sammy_servil", "justin_servil"):
        require(actor in curated_rows, f"Fight Arena spawn: missing {actor} spawn row")
    require("sammy_servil_arena" not in curated_rows,
            "Fight Arena spawn: sammy_servil_arena shell placed (its op-less form cannot be talked to)")
    require_text(
        ARENA_SAMMY.read_text(),
        ("[proc,arena_spawn_sammy]", "npc_add(0_40_49_42_18, sammy_servil_vis, 32000);",
         "npc_setowner;", "[opnpc1,sammy_servil_vis]"),
        "Fight Arena owner-private Sammy",
    )
    require(ARENA.read_text().count("~arena_spawn_sammy;") >= 4,
            "Fight Arena: every round start must call ~arena_spawn_sammy")
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
            "%varp17_arenaquest = ^arena_complete;",
            "%varp17_arenaquest = ^arena_complete_defeated_genkhazard;",
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
            "npc_add(^hazeelcult_alomone_coord, alomone_hazeel_cultist_2op, 32000);",
            "npc_add(^hazeelcult_alomone_coord, alomone_hazeel_cultist_1op, 32000);",
            "[proc,hazeelcult_alomone_talk]",
            "[ai_queue3,alomone_hazeel_cultist_2op]",
            "obj_add(npc_coord, bones, 1, ^lootdrop_duration);",
            "%varp223_hazeelcultquest = ^hazeelcult_finished_side_task;",
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
            "~hazeelcult_try_give(carnillean_armour, 1)",
            "[oploc1,carnilleanopenchest]",
            "~hazeelcult_try_give(hazeel_scroll, 1)",
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
            "npc_add(^hazeelcult_clivet_coord, clivet_hazeel_cultist, 32000);",
            "npc_find(^hazeelcult_clivet_coord, clivet_hazeel_cultist_vis, 12, 0)",
            "npc_setowner;",
            "[proc,hazeelcult_clivet_talk]",
        ),
        "Hazeel Cult Clivet lifecycle",
    )
    require_text(
        HAZEEL_CERIL.read_text(),
        (
            "[proc,hazeelcult_spawn_mansion_actors]",
            "npc_add(^hazeelcult_ceril_coord, sir_ceril_carnillean, 32000);",
            "npc_add(^hazeelcult_guard_coord, guard_carnillean, 32000);",
            "npc_add(^hazeelcult_jones_coord, butler_jones_hazeel_cultist, 32000);",
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
            "%varp150_grandtree = ^grandtree_defeated_black_demon;",
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
            "%varp6674_grandtree_tuzo_mask = or(%varp6674_grandtree_tuzo_mask, $bit);",
            "if (%varp6674_grandtree_tuzo_mask = 15)",
            "%varp150_grandtree = ^grandtree_unlocked_trapdoor;",
        ),
        "The Grand Tree TUZO gate",
    )
    require_text(GRAND_VARP.read_text(), ("[varp6674_grandtree_tuzo_mask]", "scope=temp"),
                 "The Grand Tree TUZO state")

    require_text(
        GRAND_ROOTS.read_text(),
        (
            "%varp5869_daconia_rock_root",
            "~obj_gettotal(grandtree_daconiarock) = 0",
            "inv_freespace(inv) = 0",
            "inv_add(inv, grandtree_daconiarock, 1);",
        ),
        "The Grand Tree Daconia root",
    )
    require_text(
        GRAND_KING.read_text(),
        (
            "%varp5869_daconia_rock_root = ~random_range(1, 15);",
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
         "%varb9115_upass_venom_on_doll = 1;", "queue(upass_respawn_kalrag, 50, 0);"),
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
        ("inv_total(inv, ibandoll) > 0", "%varb9116_upass_dove_on_doll = 1;",
         "%varb9118_upass_shadow_on_doll = 1;", "inv_add(inv, ibansdove, 1);",
         "inv_add(inv, ibansshadow, 1);"),
        "Underground Pass doll auto-application",
    )
    tomb = UPASS_TOMB.read_text()
    require_text(tomb, ("%varb9117_upass_ashes_on_doll = 1;", "inv_add(inv, ibanstaff, 1);",
                        "^iban_staff_max_charges", "%varp161_upass = ^upass_defeated_iban;"),
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
        ("paladinbadge1", "%varb9128_upass_paladinbadge_1 = 1;", "0_37_151_56_57"),
        ("paladinbadge2", "%varb9129_upass_paladinbadge_2 = 1;", "0_37_151_54_54"),
        ("paladinbadge3", "%varb9130_upass_paladinbadge_3 = 1;", "0_37_151_58_54"),
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
            "else if ($drop < 90)", "%varp714_rag_quest = ^rag_collecting",
            "testbit(%varp6208_rag_submit, ^rag_bit_goblin) = ^false", "random(4)",
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
            "%varb3827_observatory_chestchoice ! $which",
            "~obj_gettotal(keep_key) > 0",
            "inv_add(inv, keep_key, 1);",
            "[oploc1,qip_obs_keep_chest_open]",
            "[proc,observatory_spider_chest_seen]()(boolean)",
            "npc_add(map_findsquare(coord, 1, 1, ^map_findsquare_lineofwalk), poisonspider, 1000);",
            "[oploc1,opendungeonchest]", "inv_add(inv, 1doseantipoison, 1);",
            "[oploc1,keepgate_closed]", "[oploc1,keepgate_closed_left]",
            "%varb3826_observatory_gatelock = 0 & inv_total(inv, keep_key) = 0",
            "npc_find(loc_coord, qip_obs_goblin_guard, 8, 0)",
            "~door_selfstage_open;",
            "[oploc1,qip_obs_dungeon_stove_top_multi]",
            "%varb3837_observatory_mould_pres = 1",
            "inv_add(inv, lens_mould, 1);",
            "[opheld5,keep_key]", "[opheld5,lens_mould]", "[opheld5,lens]",
            "%varb3837_observatory_mould_pres = 0;",
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
        require_text(dungeon, (f"%{VAR_NAMES[f'observatory_chest{seen}_seen']} = 1",),
                     f"Observatory Quest spider chest {seen}")
    require("npc_findhero" not in dungeon and "ai_queue3,goblin_guard" not in dungeon,
            "Observatory Quest: gate route was coupled to killing the guard")

    professor = OBS_PROFESSOR.read_text()
    require_text(
        professor,
        (
            "%varb3827_observatory_chestchoice = random(3);",
            "%varb3826_observatory_gatelock = 0;", "%varb3829_observatory_chest1_seen = 0;",
            "%varb3835_observatory_chest7_seen = 0;", "%varb3837_observatory_mould_pres = 0;",
        ),
        "Observatory Quest player-random initialization",
    )
    glass = OBS_GLASS.read_text()
    require_text(
        glass,
        (
            "[opheldu,lens_mould]", "[proc,observatory_cast_lens]",
            "%varp112_itgronigen ! ^itgronigen_given_mould", "stat(crafting) < 10",
            "inv_del(inv, molten_glass, 1);", "mat2cost = No",
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
            "%varp197_desertrescue = ^desertrescue_approached_captain;",
            "It's a funny captain who can't fight his own battles!",
            "[label,desertrescue_captain_begin_duel]",
            "%varp6704_desertrescue_captain_duel = 1;",
            "~npc_retaliate(0);",
            "[opnpc2,desertminingcaptain]",
            "[apnpc2,desertminingcaptain]",
            "[label,desertrescue_captain_attack_warning]",
            "npc_setmode(playerescape);",
            "[label,desertrescue_captain_randompunish]",
            "getbit_range(%varp5981_desertrescue_map_mechanisms, 0, 1)",
            "setbit_range_toint(%varp5981_desertrescue_map_mechanisms, $next_stage, 0, 1);",
            "~desertrescue_find_camp_merc(coord, 7, 0)",
            "pretends to start hitting you.",
            "gives you a sharp kick.",
            "damage(uid, hitsplat_damage, 1);",
            "~objbox(bronze_scimitar,",
            "Okay men, we need to teach this person a thing or two",
            "~desertrescue_dump_in_desert;",
            "~mercenary_attack;",
            "[label,desertrescue_captain_you_there]",
            "[opnpc3,desertminingcaptain]",
            "[ai_queue3,desertminingcaptain]",
            "if (npc_findhero = ^false)",
            "obj_add_private(npc_coord, bones, 1",
            "%varp197_desertrescue = ^desertrescue_killed_capt;",
            "inv_total(bank, metal_key) > 0",
            "inv_add(inv, metal_key, 1);",
            "obj_add_private(npc_coord, metal_key, 1",
        ),
        "The Tourist Trap captain",
    )
    # seam23: LostCity [label,mercenary_capt_randompunish] -- four escalating
    # responses keyed on map_mechanisms bits 0-1, one kick of damage (stage 1),
    # the desert dump only at stage 3 -- replaced the port's single
    # four-hit arrest. Every insult reaches it; a direct Attack is
    # ~mercenary_capt_attack's ~mercenary_attack, never the ladder.
    require("[label,desertrescue_captain_arrest]" not in captain,
            "The Tourist Trap: the four-hit arrest replaced every insult response again")
    require(captain.count("damage(uid, hitsplat_damage, 1);") == 1,
            "The Tourist Trap: randompunish deals exactly one kick (stage 1)")
    require(captain.count("@desertrescue_captain_randompunish;") == 4,
            "The Tourist Trap: ugly, sand-in-ears, scare and the fist/sorry pair must each reach randompunish")
    require(captain.count("~desertrescue_dump_in_desert;") == 1,
            "The Tourist Trap: only randompunish stage 3 dumps the player in the desert")
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
        ("[varp6704_desertrescue_captain_duel]", "scope=temp"),
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
            "%varp197_desertrescue = ^desertrescue_entered_camp;",
            "~desertrescue_cross_wall($gate_coord, $gate_angle);",
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
            "getbit_range(%varp6190_itwatchtower_bits, ^itwatchtower_spoken_grew, ^itwatchtower_helped_grew) = 1",
            "~npc_retaliate(0);",
            "[ai_queue3,gorad]", "if (npc_findhero = ^false)",
            "obj_add_private(npc_coord, big_bones, 1",
            "if (random(128) < 19)", "~watchtower_gorad_uncommon_seed;",
            "if (random(30) = 0)", "arceuus_corpse_ogre",
            "if (random(400) = 0)", "dorgesh_construction_bone",
            "if (random(10025) < 2)", "dorgesh_construction_bone_curved",
            "queue(defeat_gorad, 0, 0);", "[queue,defeat_gorad]",
            "getbit_range(%varp6190_itwatchtower_bits, ^itwatchtower_spoken_grew, ^itwatchtower_helped_grew) ! 1",
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
    require("%varp212_itwatchtower < ^itwatchtower_given_relic" not in gorad,
            "Watchtower: broad legacy tooth eligibility restored")
    require("rag_ogre_bone" not in gorad,
            "Watchtower: ogre ribs must wait for Rag and Bone Man II state")

    grew = WATCH_GREW.read_text()
    require_text(
        grew,
        (
            "[opnpc1,grew]", "[label,grew_spoken]", "[opnpcu,grew]",
            "getbit_range(%varp6190_itwatchtower_bits, ^itwatchtower_spoken_grew, ^itwatchtower_helped_grew) = 1",
            "inv_total(inv, ogretooth) > 0",
            "setbit_range_toint(%varp6190_itwatchtower_bits, 2, ^itwatchtower_spoken_grew, ^itwatchtower_helped_grew)",
            "inv_del(inv, ogretooth, 1);", "inv_add(inv, relicpart2, 1);",
            "inv_add(inv, powering_crystal1, 1);",
        ),
        "Watchtower Grew tooth exchange",
    )


def check_legends_quest() -> None:
    npc = LEGENDS_NPC.read_text()
    require_text(
        npc,
        (
            "[nezikchened]", "hitpoints=150", "attack=165", "strength=168",
            "defence=167", "magic=160", "ranged=160", "respawnrate=500",
            "param=attackrate,5", "param=damagetype,1",
            "param=magic_maxhit,18", "param=death_drop,null",
        ),
        "Legends' Quest Nezikchened NPC",
    )

    nezi = LEGENDS_NEZI.read_text()
    require_text(
        nezi,
        (
            "[opnpc2,nezikchened]", "[apnpc2,nezikchened]",
            "[ai_opplayer2,nezikchened]", "[ai_applayer2,nezikchened]",
            "~npc_cast_spell_with_forced_max_hit(^fire_blast, 5, 18);",
            "[proc,legends_nezikchened_dagger_attack]()(boolean)",
            "random(10) ! 0", "stat_random(agility, 0, 254)",
            "max(divide(stat(hitpoints), 4), 1)",
            "[ai_queue3,nezikchened]", "if (npc_findhero = ^false)",
            "%varp139_legendsquest = ^legends_defeated_nezikchened_fire;",
            "damage(uid, hitsplat_damage, randominc(20));",
            "%varp139_legendsquest = ^legends_defeated_nezikchened_water;",
            "%varp139_legendsquest = ^legends_defeated_nezikchened_final;",
            "[label,summon_nezi_part3]", "[label,legends_nezi_summon_ancient_hero]",
            "npc_add(map_findsquare(coord, 1, 2, ^map_findsquare_lineofwalk), san_tojalon, 500);",
            "npc_add(map_findsquare(coord, 1, 2, ^map_findsquare_lineofwalk), irvig_senay, 500);",
            "npc_add(map_findsquare(coord, 1, 2, ^map_findsquare_lineofwalk), ranalph_devere, 500);",
            "npc_add(map_findsquare(coord, 1, 3, ^map_findsquare_lineofwalk), nezikchened, 500);",
            "stat_sub(prayer, 0, 75);",
        ),
        "Legends' Quest Nezikchened phases",
    )
    # One npc_setowner per npc_add: the first defeat no longer spawns a second Ungadulu
    # (LostCity nezikchened.rs2 [queue,nezikchened_defeat] spawns none), so the count is 4.
    require(nezi.count("npc_add(") >= 4 and nezi.count("npc_setowner;") == nezi.count("npc_add("),
            "Legends' Quest: every dynamic Nezikchened/hero spawn must be owner-private")
    require("obj_add(" not in nezi and "obj_add_private(" not in nezi,
            "Legends' Quest: Nezikchened must remain no-loot")

    # LostCity quest_legends/scripts/ungadulu.rs2 [opnpcu,ungadulu_good]: the
    # Book of Binding used on Ungadulu summons the first Nezikchened.
    ungadulu = LEGENDS_UNGADULU.read_text()
    require_text(
        ungadulu,
        (
            "[opnpcu,ungadulu_good]", "last_useitem = book_of_binding",
            "%varp139_legendsquest = ^legends_summoned_nezikchened_fire;",
            "npc_add(map_findsquare(coord, 1, 2, ^map_findsquare_lineofwalk), nezikchened, 500);",
            "npc_setowner;", "stat_sub(prayer, 0, 90);",
        ),
        "Legends' Quest Ungadulu summons Nezikchened",
    )

    # LostCity quest_legends.rs2 :1094-1300 (the Yommi tree), split out here.
    yommi = LEGENDS_YOMMI.read_text()
    require_text(
        yommi,
        (
            "[oplocu,fertilesoil]", "last_useitem ! yommiseeds_germ",
            "stat(herblore) < 45", "stat(woodcutting) < 50",
            "%varp139_legendsquest < ^legends_sacred_water_collected",
            "[oplocu,yommitree_sapling]", "[oplocu,yommitree_adult]",
            "[oplocu,yommitree_felled]", "[oplocu,yommitree_trimmed]",
            "[oploc1,yommitree_totem]", "[proc,legends_yommi_axe](obj $axe)(boolean)",
            "rune_axe, dragon_axe", "[proc,legends_get_yommi_planter]()(player_uid)",
            "[proc,legends_set_yommi_planter]", "random(10)",
            "inv_add(inv, magic_logs, 1);",
        ),
        "Legends' Quest Yommi route",
    )
    require(yommi.count("world_delay(49);") == 5,
            "Legends' Quest: all five timed Yommi rot windows must remain")

    echned = LEGENDS_ECHNED.read_text()
    require_text(
        echned,
        (
            "[opheld1,holyforce]", "[label,legends_use_holy_force]",
            "stat_sub(prayer, divide(stat(prayer), 2), 0);",
            "npc_statsub(magic, 0, 50);", "[label,summon_nezi_dagger]",
            "stat_sub(prayer, min(18, stat(prayer)), 0);", "npc_setowner;",
        ),
        "Legends' Quest second-route split",
    )

    # LostCity book_of_binding.rs2 [label,enchant_vials]: 5 Prayer and 5 Magic
    # per vial, the 1/2/N count menu.
    book = LEGENDS_BOOK.read_text()
    require_text(
        book,
        (
            "[opheldu,book_of_binding]", "last_useitem ! vial_empty",
            "stat(magic) < 10", "stat(prayer) < 10",
            "stat_sub(prayer, calc(5 * $count), 0);", "stat_sub(magic, calc(5 * $count), 0);",
            "inv_del(inv, vial_empty, $count);", "inv_add(inv, vial_enchanted, $count);",
        ),
        "Legends' Quest enchanted-vial recipe",
    )

    # LostCity quest_legends.rs2 [opheldu,goldbowlbless_pure] and
    # [oplocu,lg_ord_totem_pole].
    quest = LEGENDS_QUEST.read_text()
    require_text(
        quest,
        (
            "[oplocu,lg_ord_totem_pole]", "last_useitem = thtotempole",
            "@summon_nezi_part3;", "%varp139_legendsquest = ^legends_replaced_totem;",
            "npc_setowner;", "[opheldu,goldbowlbless_pure]",
            "getbit_range(%varp6202_legends_bits, ^legends_golden_bowl_uses_start, ^legends_golden_bowl_uses_end)",
            "inv_add(inv, holy_water, 1);", "if ($bowl_uses >= 9)",
        ),
        "Legends' Quest bowl and final totem",
    )

    gujuo = LEGENDS_GUJUO.read_text()
    require_text(gujuo, ("thtotempolegift",), "Legends' Quest Gujuo's totem gift")

    boulder = LEGENDS_BOULDER.read_text()
    require_text(
        boulder,
        (
            "[oplocu,lgwaterpool]", "last_useitem = goldbowlbless_empty",
            "%varp139_legendsquest = ^legends_sacred_water_collected;",
            "last_useitem = vial_enchanted", "inv_add(inv, holy_water, 1);",
        ),
        "Legends' Quest deep sacred-water source",
    )

    vars_text = LEGENDS_VARS.read_text()
    for spot in range(1, 7):
        require_text(vars_text, (f"[yommi_spot{spot}]", "type=player_uid"),
                     f"Legends' Quest Yommi plot {spot}")

    for path, crystal, bit in zip(
        LEGENDS_HEROES,
        ("heartcrystal_sectiona", "heartcrystal_sectionb", "heartcrystal_sectionc"),
        ("legends_defeated_san_final", "legends_defeated_irvig_final", "legends_defeated_ranalph_final"),
    ):
        hero = path.read_text()
        require_text(hero, ("if (npc_findhero = ^false)", f"^{bit}",
                            f"inv_add(inv, {crystal}, 1);", "@summon_nezi_part3;"),
                     f"Legends' Quest hero {path.stem}")

    ranged = PLAYER_RANGED.read_text()
    require_text(
        ranged,
        (
            "$rhand = holy_water & npc_type = nezikchened & $hit = true",
            "oc_param(holy_water, rangebonus_ammo)",
            "multiply($holy_base, 16)", "add(divide(multiply($holy_base, 16), 10), 5)",
            "npc_statsub(defence, 0, 5);",
            "%varp6732_legends_nezikchened_holy_water = 2;",
        ),
        "Legends' Quest Holy Water combat formula",
    )

    require_text(
        LEGENDS_VARP.read_text(),
        ("[varp6731_legends_nezikchened_dagger_used]", "[varp6732_legends_nezikchened_holy_water]"),
        "Legends' Quest attempt state",
    )


def check_big_chompy() -> None:
    npc = CHOMPY_NPC.read_text()
    require_text(
        npc,
        (
            "[chompybird]", "hitpoints=10", "attack=5", "strength=5",
            "defence=3", "magic=0", "ranged=0", "respawnrate=500",
            "param=attackrate,0", "param=death_drop,null",
            "[chompybird_dead]",
        ),
        "Big Chompy Bird Hunting NPC config",
    )

    bait = CHOMPY_BAIT.read_text()
    require_text(
        bait,
        (
            "[opheld1,bloated_toad]", "npc_findexact(coord, bloated_toad)",
            "npc_add(coord, bloated_toad, 101);", "npc_setowner;",
            "%chompy_baiter = $baiter;", "npc_queue(4, 0, 25);",
            "[ai_queue4,bloated_toad]", "last_int = 3 | last_int = 13",
            "random(5) = 1", "npc_queue(4, $next_state, 25);",
            "~spawn_chompy_bird($bait_coord, $baiter);",
            "damage(uid, hitsplat_damage, add(random(2), 1));",
        ),
        "Big Chompy Bird Hunting bait cycle",
    )
    require("[proc,spawn_chompy_bird]" not in bait,
            "Big Chompy Bird Hunting: obsolete thin bird spawn restored")

    bird = CHOMPY_BIRD.read_text()
    require_text(
        bird,
        (
            "[proc,spawn_chompy_bird](coord $bait, player_uid $baiter)",
            "map_findsquare($bait, 3, 10, ^map_findsquare_lineofsight)",
            "npc_add($spawn, chompybird, 100);", "npc_setowner;",
            "queue(chompy_rantz_misses, add(15, random(10)), 0);",
            "[ai_queue4,chompybird]", "npc_find($here, bloated_toad, 10",
            "[ai_timer,chompybird]", "if (npc_hastarget = true)",
            "npc_setmode(playerescape);",
            "[apnpc5,chompybird]", "[opnpc5,chompybird]",
            "$bow ! ogre_bow & $bow ! zogre_bow",
            "[proc,chompy_valid_ammo](obj $ammo)(boolean)",
            "multiply(add(stat(ranged), 10), add(oc_param($ammo, rangebonus_ammo), 64))",
            "~player_hit_npc_prepare($damage, $hit);",
            "~player_ranged_use_weapon($bow, $ammo)",
            "[ai_queue3,chompybird]", "queue(chompybird_kill, 0, 0);",
            "npc_add($death, chompybird_dead, 200);", "[queue,chompybird_kill]",
            "%varp293_chompybird = ^chompybird_player_killed_chompy;",
            "[opnpc4,chompybird_dead]", "add(random(21), 10)",
            "obj_add_private(npc_coord, raw_chompy, 1",
            "obj_add_private(npc_coord, bones, 1",
        ),
        "Big Chompy Bird Hunting bird/corpse cycle",
    )
    require("gosub(npc_death)" not in bird,
            "Big Chompy Bird Hunting: generic bird drop hook restored")
    require(bird.count("npc_setowner;") == 2,
            "Big Chompy Bird Hunting: bird and corpse must remain owner-private")

    rantz = CHOMPY_RANTZ.read_text()
    require_text(
        rantz,
        (
            "[proc,chompy_rantz_dialogue]", "inv_total(inv, ogre_arrow) < 6",
            "inv_del(inv, ogre_arrow, 6);",
            "%varp293_chompybird = ^chompybird_given_arrows;",
            "%varp293_chompybird = ^chompybird_shown_toad;",
            "%varp293_chompybird = ^chompybird_rantz_gave_player_bow;",
            "%varp293_chompybird = ^chompybird_told_to_cook_chompy;",
            "[opnpcu,rantz]", "[label,hand_chompy_to_rantz]",
            "inv_del(inv, cooked_s_chompy, 1);",
            "[queue,quest_chompybird_complete]",
            "stat_advance(fletching, 2620);", "stat_advance(cooking, 14700);",
            "stat_advance(ranged, 7350);",
            "~quest_complete_rewards(quest_bigchompybirdhunting",
        ),
        "Big Chompy Bird Hunting Rantz route",
    )
    require_text(
        OSF_RELAY.read_text(),
        ("[opnpc1,rantz]", "~rfd_ogre_rantz_dialogue;", "~chompy_rantz_dialogue;"),
        "Big Chompy Bird Hunting shared Rantz dispatcher",
    )

    recipe = CHOMPY_RECIPE.read_text()
    require_text(
        recipe,
        (
            "[oplocu,chompybird_spitroast_empty]", "stat(cooking) < 30",
            "[label,cook_chompy_quest]",
            "^chompybird_varbit_bugs_flavour_start",
            "^chompybird_varbit_fycie_flavour_start",
            "def_obj $rantz_item = potato;", "$rantz_item = onion;",
            "$bugs_item = cabbage;", "$fycie_item = doogleleaves;",
            "stat_random(cooking, 200, 255)", "inv_add(inv, ruined_chompy, 1);",
            "inv_add(inv, cooked_s_chompy, 1);",
            "%varp293_chompybird = ^chompybird_chompy_cooked;",
        ),
        "Big Chompy Bird Hunting spit recipe",
    )

    require_text(
        CHOMPY_ARROWS.read_text(),
        ("[opheldu,wolf_bones]", "[label,make_ogre_arrows]",
         "%varp6192_chompybird_kills = setbit(%varp6192_chompybird_kills, ^chompybird_varbit_made_arrows);"),
        "Big Chompy Bird Hunting arrow recipe",
    )
    require_text(CHOMPY_CHEST.read_text(),
                 ("[oploc1,chompybird_chest]", "inv_add(inv, empty_ogre_bellows, 1);"),
                 "Big Chompy Bird Hunting bellows chest")
    require_text(CHOMPY_TOAD.read_text(),
                 ("[opnpcu,toad]", "inv_total(inv, bloated_toad) >= 3",
                  "~reduce_ogre_bellows($used_bellow);"),
                 "Big Chompy Bird Hunting toad inflation")
    require_text(CHOMPY_CAVES.read_text(),
                 ("[oplocu,swampbubbles]", "[oplocu,swampbubbles_swamp]",
                  "inv_add(inv, filled_ogre_bellow3, 1);"),
                 "Big Chompy Bird Hunting swamp gas")
    require_text(CHOMPY_VARN.read_text(),
                 ("[chompy_baiter]", "type=player_uid", "[chompy_target_toad]", "type=npc_uid"),
                 "Big Chompy Bird Hunting actor state")


def check_elemental_workshops() -> None:
    npc = ELEM1_NPC.read_text()
    require_text(
        npc,
        (
            "[elem1_qip_earth_elemental_rock_version]", "hitpoints=35",
            "attack=20", "strength=35", "defence=35", "magic=10",
            "ranged=30", "param=attackrate,6",
            "param=death_drop,elem1_qip_rockremains",
        ),
        "Elemental Workshop mined Earth elemental config",
    )

    gather = ELEM_GATHER.read_text()
    require_text(
        gather,
        (
            "[opnpc1,elem1_qip_earth_elemental_rock_version_rock]",
            "stat(mining) < 20", "~pickaxe_checker;",
            "npc_add(npc_coord, elem1_qip_earth_elemental_rock_version, 500);",
            "npc_setowner;", "~npc_retaliate(0);",
            "[oplocu,elemental_workshop_furnace_out]",
            "[oplocu,elemental_workshop_furnace_lit]",
            "~elem1_furnace(last_useitem);",
        ),
        "Elemental Workshop rock activation and shared furnace",
    )
    require(gather.count("npc_setowner;") == 1,
            "Elemental Workshop: mined elemental must have one owner assignment")

    drops = ELEM1_DROPS.read_text()
    ore_drop = drops.split("[ai_queue3,elem1_qip_earth_elemental_rock_version]", 1)[1]
    ore_drop = ore_drop.split("[ai_queue3,elemental_earth]", 1)[0]
    require_text(
        ore_drop,
        (
            "npc_findhero = ^false", "obj_add_private(npc_coord, elem1_qip_rockremains, 1",
            "obj_add_private(npc_coord, elemental_workshop_ore, 1",
        ),
        "Elemental Workshop guaranteed private ore drop",
    )
    require("obj_add(" not in ore_drop,
            "Elemental Workshop: mined elemental restored a public drop")

    book = ELEM1_BOOK.read_text()
    require_text(
        book,
        (
            "inv_del(inv, elemental_workshop_shield_book, 1);",
            "inv_add(inv, elemental_workshop_shield_book_slashed, 1);",
            "inv_add(inv, elemental_workshop_key, 1);",
            "[opheld1,elemental_workshop_shield_book_slashed]",
        ),
        "Elemental Workshop battered/slashed book",
    )

    core = ELEM1_CORE.read_text()
    require_text(
        core,
        (
            "[oploc1,elemental_workshop_bookcase]",
            "[oploc1,elemental_workshop_valve_1_red]",
            "coordx(loc_coord) > 2719", "%varb2059_elemental_workshop_gate2 =",
            "[oploc1,elemental_workshop_water_lever]",
            "%varb2058_elemental_workshop_gate1 = 1 & %varb2059_elemental_workshop_gate2 = 1",
            "[oploc1,elemental_workshop_bellows_noanim]",
            "stat(crafting) < 20", "inv_del(inv, thread, 1);",
            "inv_del(inv, leather, 1);", "[oploc1,elemental_workshop_air_lever]",
            "[oploc1,elemental_workshop_box_1]",
            "[oplocu,elemental_workshop_trough_1]",
            "[oplocu,elemental_workshop_trough_5]",
            "[proc,elem1_furnace](obj $used)", "inv_del(inv, coal, 4);",
            "inv_add(inv, elemental_workshop_bar, 1);",
            "[proc,elem1_make_shield]", "inv_add(inv, elemental_shield, 1);",
            "%varb2067_elemental_workshop_finished = 1;",
            "stat_advance(crafting, 50000);", "stat_advance(smithing, 50000);",
            "~quest_complete_rewards(quest_elementalworkshop1",
        ),
        "Elemental Workshop I machinery and reward",
    )

    helm = ELEM2_HELM.read_text()
    require_text(
        helm,
        (
            "[oploc1,elem_extractor_hat]", "stat(magic) < 20",
            "stat_sub(magic, 20, 0);", "[oplocu,elemental_workshop_workbench]",
            "~elem1_make_shield;", "inv_add(inv, elem_mind_helm, 1);",
            "inv_add(inv, elemental_mind_shield, 1);", "~elem2_finish;",
        ),
        "Elemental Workshop II extractor and equipment selection",
    )
    require_text(
        ELEM2_REPAIR.read_text(),
        (
            "[oploc1,elem2_maintenance_book_crate]", "[proc,elem2_make_claw]",
            "[oploc1,elem2_press_junction_box]", "[oplocu,elemental_piping_blue_broken]",
            "[oplocu,elem2_wind_pin_high]", "[oplocu,elem2_wind_pin_low]",
            "[oplocu,elem2_wind_pin_left]",
        ),
        "Elemental Workshop II repairs",
    )
    require_text(
        ELEM2_PRIMING.read_text(),
        (
            "[opnpcu,elem2_cart_npc_empty]", "[oploc1,elem2_lever_3way]",
            "[oploc1,elem2_earth_lever_1]", "[oploc1,elem2_water_lever]",
            "[oploc1,elem2_corkscrew]", "[oploc1,elem2_valve_1]",
            "[oploc1,elem2_valve_2]", "[oploc1,elem2_air_lever]",
            "inv_add(inv, elem_primed_bar, 1);",
        ),
        "Elemental Workshop II priming state machine",
    )
    require_text(
        ELEM2_SHARED.read_text(),
        ("stat_advance(smithing, ^elem2_reward_smithing_xp);",
         "stat_advance(crafting, ^elem2_reward_crafting_xp);",
         "~quest_complete_rewards(quest_elementalworkshop2", "elem_mind_helm"),
        "Elemental Workshop II reward",
    )


def check_nature_spirit() -> None:
    npc = NATURE_NPC.read_text()
    for ghast in ("ghast_invis", "ghast_vis"):
        block = npc.split(f"[{ghast}]", 1)[1]
        if ghast == "ghast_invis":
            block = block.split("[ghast_vis]", 1)[0]
        require_text(
            block,
            (
                "hitpoints=45", "attack=22", "strength=22", "defence=18",
                "magic=1", "ranged=1", "respawnrate=40",
                "param=attackrate,4", "param=damagetype,0",
                "param=elemental_weakness,^element_air",
                "param=elemental_weakness_percent,25", "param=death_drop,null",
            ),
            f"Nature Spirit {ghast} config",
        )
        if ghast == "ghast_invis":
            # The engine reads only `huntmode=aggressive`; LostCity's
            # `aggressive_melee` names a hunt config here does not have and
            # parsed to HUNT_NONE, so no ghast ever attacked (2026-09-29).
            require_text(
                block, ("huntmode=aggressive\n", "param=huntrange,5"),
                "Nature Spirit ghast_invis aggression",
            )

    ghast = NATURE_GHAST.read_text()
    require_text(
        ghast,
        (
            "add(%varp6217_ghast_delay, 26) > map_clock", "def_int $rand = random(10);",
            "if ($rand < 3)", "~random_range(1, 3)",
            "[opnpcu,ghast_invis]", "~player_in_combat_check = false",
            "inv_del(inv, druid_pouch, 1);", "inv_add(inv, druid_pouch_empty, 1);",
            "npc_changetype(ghast_vis, 500);", "npc_setmode(opplayer2);",
            "[ai_timer,ghast_vis]", "npc_changetype(ghast_invis, 500);",
        ),
        "Nature Spirit ghast reveal cycle",
    )
    require("npc_setowner" not in ghast,
            "Nature Spirit: ordinary ghasts must remain public")
    require_text(
        ghast,
        (
            "obj_add(npc_coord, bones, 1, ^lootdrop_duration);",
            "def_int $random = random(128);", "$random < 47",
            "obj_add(npc_coord, ~randomherb, ^lootdrop_duration);",
            "$random < 125", "obj_add(npc_coord, ~randomjewel, ^lootdrop_duration);",
            "stat_advance(prayer, 300);",
            "%varp307_druidspirit = ^druidspirit_killed_ghast1;",
            "%varp307_druidspirit = ^druidspirit_killed_ghast2;",
            "%varp307_druidspirit = ^druidspirit_killed_ghast3;",
            # npc_findhero reads the active npc, which npc_del removes: asked
            # after the removal it aborted the death queue (2026-09-29).
            "def_int $hero = npc_findhero;", "if ($hero = ^false)",
        ),
        "Nature Spirit public loot and exact kill credit",
    )
    require_text(
        ghast,
        (
            "[proc,ghast_check_food]()(boolean)",
            "inv_setslot(inv, $slot, rotten_food, 1);",
            "oc_category($consumable) = pies", "inv_add(inv, piedish, 1);",
            "oc_category($slotobj) = cooked_meat",
            "oc_category($slotobj) = cooked_fish",
            "oc_category($slotobj) = misc_food",
            "oc_category($slotobj) = pies",
            "oc_category($slotobj) = pizzas",
            "oc_category($slotobj) = filled_potatoes",
        ),
        "Nature Spirit rotten-food coverage",
    )

    core = NATURE_CORE.read_text()
    require_text(
        core,
        (
            "[opheld3,silver_sickle_blessed]", "~random_range(1, 7)",
            "stat_sub(prayer, $pray_pts, 0);",
            "[label,druid_pouch_fill](int $empty_pouch)",
            "min(sub(3, $total_count), inv_total(inv, mortmyrepear))",
            "multiply(3, $count)", "inv_total(inv, mortmyrebuddingstem)",
            "multiply(2, $count)", "inv_total(inv, mortmyremushroom)",
            "if ($total_count = 3)", "inv_add(inv, druid_pouch, $pts);",
            "[oplocu,stonedisc_ds_nature]", "[oplocu,stonedisc_ds_spirit]",
            "[oploc1,druidic_spirit_grotto_naturealtar]",
            "stat_base(prayer) + 2",
        ),
        "Nature Spirit ritual, Bloom, pouch and reward pipeline",
    )
    filliman = NATURE_FILLIMAN.read_text()
    require_text(
        filliman,
        (
            "%varp307_druidspirit >= ^druidspirit_blessed_sickle",
            "%varp307_druidspirit < ^druidspirit_added_pouch",
            "inv_total(inv, druid_pouch_empty) = 0",
            "inv_total(inv, druid_pouch) = 0",
            "if (inv_freespace(inv) = 0)",
            "inv_add(inv, druid_pouch_empty, 1);",
            "queue(druidspirit_quest_complete, 0, 0);",
            "stat_advance(crafting, 30000);", "stat_advance(hitpoints, 20000);",
            "stat_advance(defence, 20000);",
            "~quest_complete_rewards(quest_naturespirit",
        ),
        "Nature Spirit Filliman transformation and pouch recovery",
    )
    spawn = NATURE_GROTTO_SPAWN.read_text()
    require("druid_pouch_empty                             3443  9741 1 1" in spawn,
            "Nature Spirit: grotto must respawn the empty pouch")
    require("druid_pouch                                   3443  9741 1 1" not in spawn,
            "Nature Spirit: grotto restored an incorrectly charged pouch")
    require_text(
        NATURE_DECAY.read_text(),
        ("settimer(swamp_decay, 200);", "damage(uid, hitsplat_damage, 3);"),
        "Nature Spirit swamp decay",
    )


def check_priest_in_peril() -> None:
    npc = PIP_NPC.read_text()
    guardian_block = npc.split("[priestperilguarddog]", 1)[1].split(
        "[priestperilevilmonk1]", 1
    )[0]
    require_text(
        guardian_block,
        (
            "hitpoints=45", "attack=20", "strength=20", "defence=20",
            "magic=1", "ranged=1", "respawnrate=500",
            "param=attackrate,4", "param=damagetype,0", "param=death_drop,null",
        ),
        "Priest in Peril Temple Guardian config",
    )
    require("huntmode=aggressive" not in guardian_block,
            "Priest in Peril: Temple Guardian must remain non-aggressive")

    monk_contracts = {
        "priestperilevilmonk1": (
            "hitpoints=20", "attack=18", "strength=18", "defence=22",
            "magic=25", "ranged=1", "respawnrate=25", "huntmode=aggressive",
            "param=attackrate,4", "param=damagetype,2", "param=death_drop,null",
        ),
        "priestperilevilmonk2": (
            "hitpoints=10", "attack=8", "strength=8", "defence=12",
            "magic=25", "ranged=1", "respawnrate=25", "huntmode=aggressive",
            "param=attackrate,4", "param=damagetype,2",
            "param=strengthbonus,124", "param=death_drop,null",
        ),
        "priestperilevilmonk3": (
            "hitpoints=25", "attack=25", "strength=25", "defence=25",
            "magic=40", "ranged=1", "respawnrate=25", "huntmode=aggressive",
            "param=attackrate,4", "param=damagetype,2", "param=death_drop,null",
        ),
    }
    for index, (monk, needles) in enumerate(monk_contracts.items()):
        block = npc.split(f"[{monk}]", 1)[1]
        later = list(monk_contracts)[index + 1:]
        if later:
            block = block.split(f"[{later[0]}]", 1)[0]
        require_text(block, needles, f"Priest in Peril {monk} config")

    guardian = PIP_GUARDIAN.read_text()
    require_text(
        guardian,
        (
            "[zone,0_53_154_8_40]", "[zone,0_53_154_8_48]",
            "%varp302_priestperil ! ^priestperil_agree_to_kill_dog",
            "npc_add(0_53_154_13_46, priestperilguarddog, 32000);",
            "npc_setowner;", "[opnpc2,priestperilguarddog]",
            "[apnpc2,priestperilguarddog]", "@player_combat_start;",
            "@player_combat_start_ap;", "[ai_queue3,priestperilguarddog]",
            "if (npc_findhero = ^false)", "npc_statheal(hitpoints, 0, 100);",
            "%varp302_priestperil = ^priestperil_killed_dog;",
        ),
        "Priest in Peril owner-private Guardian lifecycle",
    )
    require("obj_add" not in guardian,
            "Priest in Peril: Temple Guardian must not drop loot")
    require("priestperilguarddog" in SPAWN_GENERATOR.read_text(),
            "Priest in Peril: spawn generator must exclude the public Guardian")
    require("priestperilguarddog" not in PIP_WORLD_SPAWN.read_text(),
            "Priest in Peril: public Guardian remained in generated world spawns")

    magic = PLAYER_MAGIC.read_text()
    require_text(
        magic,
        (
            "[proc,npc_immune_to_magic]()(boolean)",
            "if (npc_type = priestperilguarddog)",
            "if (~npc_immune_to_magic = true)",
            "npc_stat(hitpoints) > 0 & ~npc_immune_to_magic = false",
        ),
        "Priest in Peril Guardian spell immunity",
    )
    require_text(
        POWERED_STAFF.read_text(),
        ("if (~npc_immune_to_magic = true)", "Your spells do not seem to affect it."),
        "Priest in Peril Guardian powered-staff immunity",
    )

    monks = PIP_MONKS.read_text()
    require_text(
        monks,
        (
            "[ai_opplayer2,priestperilevilmonk1]", "~npc_zap_attack(10, npc_param(attackrate));",
            "[ai_opplayer2,priestperilevilmonk2]", "~npc_meleeattack;",
            "[ai_opplayer2,priestperilevilmonk3]", "~npc_zap_attack(8, npc_param(attackrate));",
            "if (npc_findhero = ^false)",
            "obj_add_private(npc_coord, bones, 1, ^lootdrop_duration, 100);",
            "def_int $robe = random(20);", "if ($robe = 0)",
            "obj_add_private(npc_coord, monkrobetop, 1, ^lootdrop_duration, 100);",
            "else if ($robe = 1)",
            "obj_add_private(npc_coord, monkrobebottom, 1, ^lootdrop_duration, 100);",
            "npc_type = priestperilevilmonk3 & %varp302_priestperil < ^priestperil_unlocked_drezel",
            "obj_add_private(npc_coord, pipkey_gold, 1, ^lootdrop_duration, 100);",
        ),
        "Priest in Peril monk AI and private loot",
    )
    require_text(
        PLAYER_HIT_FUNNEL.read_text(),
        (
            "npc_type = priestperilevilmonk1 | npc_type = priestperilevilmonk2 | npc_type = priestperilevilmonk3",
            "$xp_damage = divide($prepared, 4);",
        ),
        "Priest in Peril monk -75 percent XP",
    )

    monuments = PIP_MONUMENTS.read_text()
    require_text(
        monuments,
        (
            "[oploc1,priestperil_grave_base1]", "[oploc1,priestperil_grave_base7]",
            "[oploc2,priestperil_grave_base1]", "[oplocu,priestperil_grave_base7]",
            "testbit(%varp6733_priestperil_mausoleum, 21)",
            "setbit_range_toint(%varp6733_priestperil_mausoleum, random(100), 22, 28)",
            "multiply($grave_no, 17)", "modulo($seed, 7)",
            "if ($grave_item = pipkey_iron)", "last_useitem ! pipkey_gold",
            "if (~obj_gettotal(pipkey_iron) > 0)",
            "inv_del(inv, pipkey_gold, 1);", "inv_add(inv, pipkey_iron, 1);",
            "setbit(%varp6733_priestperil_mausoleum, $swap_bit)",
            "damage(uid, hitsplat_damage, add(random(6), 1));",
        ),
        "Priest in Peril randomized monument swaps",
    )
    require("6733=varp6733_priestperil_mausoleum" in PIP_VARP_ALLOC.read_text(),
            "Priest in Peril: durable monument varp allocation missing")

    gates = PIP_GATES.read_text()
    require_text(
        gates,
        (
            "[oploc1,pip_underground_door1]", "testbit(%varp6733_priestperil_mausoleum, 20)",
            "inv_total(inv, pipkey_gold) = 0", "setbit(%varp6733_priestperil_mausoleum, 20)",
            "[oploc1,pip_underground_door2]",
            "~check_priest_peril_gate(^priestperil_meet_in_mausoleum);",
        ),
        "Priest in Peril durable gate unlock",
    )

    require_text(
        PIP_KING.read_text(),
        (
            "[label,roald_priestperil_dialogue]", "%varp302_priestperil = ^priestperil_started;",
            "%varp302_priestperil = ^priestperil_return_to_drezel;",
            "%varp302_priestperil = ^priestperil_poured_blessed_water",
        ),
        "Priest in Peril King Roald start and return path",
    )
    require_text(
        PIP_TEMPLE_DOORS.read_text(),
        ("[label,templedoors_agree_help]", "%varp302_priestperil = ^priestperil_agree_to_kill_dog;"),
        "Priest in Peril temple deception",
    )
    require_text(
        PIP_TRAPPED_DREZEL.read_text(),
        (
            "[oplocu,pip_prisondoor]", "inv_del(inv, pipkey_iron, 1);",
            "%varp302_priestperil = ^priestperil_unlocked_drezel;", "def_int $water = inv_total(inv, bucket_murkywater);",
            "inv_del(inv, bucket_murkywater, $water);", "inv_add(inv, bucket_blessedwater, $water);",
        ),
        "Priest in Peril cell and all-bucket blessing",
    )
    require_text(
        PIP_COFFIN.read_text(),
        (
            "last_useitem = bucket_blessedwater", "%varp302_priestperil = ^priestperil_poured_blessed_water;",
            "inv_del(inv, bucket_blessedwater, 1);", "inv_add(inv, bucket_empty, 1);",
        ),
        "Priest in Peril vampyre containment",
    )

    well = PIP_WELL.read_text()
    require_text(
        well,
        (
            "[oploc1,priestperil_well]", "[proc,priestperil_use_well]",
            "inv_del(inv, bucket_empty, 1);", "inv_add(inv, bucket_murkywater, 1);",
            "inv_add(inv, bucket_water, 1);", "[oploc1,pip_underground_wall_side_withportal]",
            "%varp302_priestperil = ^priestperil_access_holy_barrier", "p_telejump(0_53_54_31_29);",
            "[label,priestperil_barrier_blocked]", "[oploc1,pipeastsidetrapdoor]",
            "[oploc1,pipeastsidetrapdoor_open]", "p_telejump(0_53_154_48_31);",
        ),
        "Priest in Peril well and holy barrier",
    )
    myreque_well = PIP_MYREQUE_WELL.read_text()
    require(myreque_well.count("[oplocu,priestperil_well]") == 1,
            "Priest in Peril: shared well must have one item-use trigger")
    require_text(
        myreque_well,
        ("if (last_useitem = bucket_empty)", "~priestperil_use_well;", "last_useitem != burgh_rod_command2"),
        "Priest in Peril/In Aid of the Myreque shared well dispatch",
    )

    drezel = PIP_DREZEL.read_text()
    require_text(
        drezel,
        (
            "[opnpc1,priestperiltrappedmonk2]", "[opnpcu,priestperiltrappedmonk2]",
            "add(inv_total(inv, blankrune), inv_total(inv, blankrune_high))",
            "def_int $take_rune = min($to_give, $have_rune);", "inv_del(inv, blankrune, $take_rune);",
            "inv_del(inv, blankrune_high, $take_pure);", "%varp302_priestperil = add(%varp302_priestperil, $given);",
            "stat_advance(prayer, ^priestperil_reward_prayer_xp);",
            "inv_add(inv, dagger_wolfbane, 1);", "~quest_complete_rewards(quest_priestinperil",
            "if (~obj_gettotal(dagger_wolfbane) = 0)", "%varp302_priestperil = ^priestperil_access_holy_barrier;",
        ),
        "Priest in Peril essence, reward, reclaim and barrier access",
    )


def check_regicide() -> None:
    route = REG_ROUTE.read_text()
    require_text(
        route,
        (
            "[oploc1,regicide_voyage_temple_well1]", "p_teleport(0_36_150_39_22);",
            "[zone,0_36_50_8_16]", "@regicide_idris_encounter;",
            "npc_add(map_findsquare(coord, 1, 3, ^map_findsquare_lineofwalk), regicide_good_elf1, 200);",
            "npc_setowner;", "[oploc1,regicide_cross_over2_tyras_camp]",
            "stat(agility) < 56", "regicide_old_camp_guard, 500", "regicide_cross_over3", "^regicide_entered_camp",
            "[zone,0_40_51_24_32]", "@regicide_arianwyn_encounter;",
            "%varp328_regicide_quest = ^regicide_spoken_arianwyn;", "[opheld1,regicide_iorwerth_message]",
        ),
        "Regicide Well, Idris, private guard and Arianwyn route",
    )

    traps = REG_TRAPS.read_text()
    require_text(
        traps,
        (
            "[oploc1,regicide_trap_woodspring]", "damage(uid, hitsplat_damage, 8);",
            "[oploc1,regicide_trap_tripwire]", "queue(poison_player, 0, 10);",
            "[oploc1,regicide_pitfall_corner]", "damage(uid, hitsplat_damage, 15);",
            "[oploc1,regicide_logbalance1_start]", "stat(agility) < 45",
            "[timer,regicide_tripwire_walk]", "[timer,regicide_pitfall_walk]",
        ),
        "Regicide forest hazards",
    )

    guard = REG_GUARD.read_text()
    drops = REG_DROP.read_text()
    require_text(
        guard,
        (
            "[ai_queue3,regicide_tyras_guard]\n@wiki_tyras_guard_drop;",
            "[ai_queue3,regicide_tyras_camp_guard]", "queue(regicide_quest_guard_defeated, 0, 0);",
            "@wiki_tyras_guard_drop;",
            "[queue,regicide_quest_guard_defeated]", "%varp328_regicide_quest = ^regicide_defeated_guard;",
        ),
        "Regicide encounter-specific guard credit",
    )
    require("regicide_tyras_guard_defeated" not in guard,
            "Regicide: generic guard death can still satisfy quest stage")
    require_text(
        drops,
        (
            "[ai_queue3,regicide_old_camp_guard]", "queue(regicide_quest_guard_defeated, 0, 0);",
            "def_int $dropint = random(128);", "obj_add(npc_coord, ~randomjewel, ^lootdrop_duration);",
        ),
        "Regicide exact Tyras guard loot",
    )

    bomb = REG_BOMB.read_text()
    still = REG_STILL.read_text()
    require_text(
        bomb,
        (
            "inv_getobj(worn, ^wearpos_hands) = null", "damage(uid, hitsplat_damage, 8);",
            "stat(crafting) < 10", "inv_total(inv, ball_of_wool) < 4",
            "inv_total(inv, tinderbox) = 0", "anim(regicide_catapultwind, 0);",
            "loc_anim(fire_catapult);", "regicide_barrelflight", "regicide_tent_human_fire",
            "%varp328_regicide_quest = ^regicide_killed_tyras;",
        ),
        "Regicide bomb and catapult pipeline",
    )
    require_text(
        still,
        (
            "[oplocu,regicide_fractionalizing_still]", "[if_close,regicide_still]",
            "[if_button,regicide_still:regicide_add_coal]", "[softtimer,regicide_still_progress]",
            "%varp330_regicide_still_total >= 26", "inv_add(inv, regicide_barrel_naphtha, 1);",
        ),
        "Regicide fractionalising still",
    )
    shared_still = REG_SHARED_STILL.read_text()
    require("if (inv_total(inv, regicide_barrel_tar) >= 1)" not in shared_still,
            "Regicide: shared MEP handler restored the ten-coal still shortcut")
    grind = (CONTENT / "skill_herblore/scripts/grind_ingredient.rs2").read_text()
    require_text(
        grind,
        (
            "if ($grindable = regicide_quicklime)", "inv_total(inv, pot_empty) = 0",
            "min(5, sub(stat(hitpoints), 1))", "inv_add(inv, regicide_quicklime_dust, 1);",
        ),
        "Regicide quicklime grinding",
    )
    xp = PLAYER_HIT_FUNNEL.read_text()
    require_text(
        xp,
        ("npc_type = regicide_old_camp_guard", "npc_type = regicide_tyras_camp_guard",
         "npc_type = regicide_tyras_guard", "$xp_damage = scale(105, 100, $prepared);"),
        "Regicide Tyras guard XP modifier",
    )
    generator = SPAWN_GENERATOR.read_text()
    require(generator.count('(\"regicide_old_camp_guard\",') == 3,
            "Regicide: spawn generator must exclude all three public old-camp guards")
    king = REG_KING.read_text()
    require_text(
        king,
        (
            "case ^regicide_killed_tyras, ^regicide_reported_iorwerth :", "case ^regicide_spoken_arianwyn :",
            "stat_advance(agility, 137500);", "inv_add(inv, coins, 15000);",
            "~quest_complete_rewards(quest_regicide", "%varp328_regicide_quest = ^regicide_complete;",
        ),
        "Regicide Arianwyn gate and reward",
    )


def check_tai_bwo_wannai_trio() -> None:
    monkey = TBWT_MONKEY.read_text()
    require_text(
        monkey,
        (
            "[opnpc2,monkey]", "~player_attackrange(inv_getobj(worn, ^wearpos_rhand))",
            "%varp320_tbwt_main >= ^tbwt_started & %varp320_tbwt_main < ^tbwt_complete & $attackrange <= 1",
            "npc_setmode(playerescape);", "@player_combat_start;", "[ai_queue3,monkey]",
            "obj_add(npc_coord, tbwt_monkey_corpse, 1, ^lootdrop_duration);",
            "obj_add(npc_coord, mm_normal_monkey_bones, 1, ^lootdrop_duration);",
        ),
        "Tai Bwo Wannai Trio monkey",
    )

    core = TBWT_CORE.read_text()
    require_text(
        core,
        (
            "[proc,tbwt_is_kp_spear]", "brut_rune_spear_kp",
            "[proc,tbwt_is_acceptable_tamayu_spear]", "iron_spear, iron_spear_p",
            "dragon_spear, dragon_spear_p", "brut_iron_spear, brut_iron_spear_p",
            "[proc,tbwt_kp_weapon_for]", "case black_spear : return(tbwt_black_spear_kp);",
            "case brut_rune_spear : return(brut_rune_spear_kp);",
            "[opheldu,tbwt_poisonous_karambwan_paste]", "inv_setslot(inv, last_useslot, $product, 1);",
            "[oploc1,tbwt_bamboo_door]", "~door_selfstage_open;",
            "[oploc2,tbwt_bamboo_door]", "~door_selfstage_close;",
            "%varp6054_tbwt_tinsay < ^tbwt_tinsay_claimed_final_reward | %varp321_tbwt_tiadeche < ^tbwt_tiadeche_claimed_final_reward",
            "@pray_at_altar(stat_base(prayer));", "[queue,tbwt_quest_complete]",
            # The real scroll (wiki File:Tai_Bwo_Wannai_Trio_reward_scroll.png) lists
            # the brothers' four XP rewards and no coins line; seam b49-seam2.
            '"5000 Fishing XP|5000 Cooking XP|2500 Attack XP|2500 Strength XP", tbwt_raw_karambwan);',
        ),
        "Tai Bwo Wannai Trio core item, door, altar and reward contract",
    )
    require("black_spear, black_spear_p" not in core,
            "Tai Bwo Wannai Trio: black spear restored to Tamayu acceptance list")

    tamayu = TBWT_TAMAYU.read_text()
    require_text(
        tamayu,
        (
            "sub(4, $current)", "~set_tbwt_tamayu_agility_count(add($doses, $current));",
            "~tbwt_is_acceptable_tamayu_spear($spear)", "~tbwt_is_kp_spear($spear)",
            "testbit(%varp6051_tbwt_flags, ^tbwt_tamayu_received_acceptable_spear) = ^true",
            "testbit(%varp6051_tbwt_flags, ^tbwt_tamayu_received_kp_spear) = ^true",
            "npc_add(^tbwt_tamayu_hunter_cutscene_spawn, tbwt_tamayu_hunter, 200);",
            "npc_add(^tbwt_shaikahan_cutscene_spawn, tbwt_beast_cutscene, 200);",
            "npc_add(^tbwt_tamayu_hunter_cutscene_final_spawn, tbwt_tamayu_final_hunter, 100);",
            "npc_add(^tbwt_shaikahan_cutscene_final_spawn, tbwt_beast_cutscene, 100);",
            "sound_synth(beast_hit, 1, 0);", "%varp6053_tbwt_tamayu = ^tbwt_tamayu_complete;",
        ),
        "Tai Bwo Wannai Trio Tamayu hunt",
    )
    require(tamayu.count("npc_setowner;") >= 4,
            "Tai Bwo Wannai Trio: every hunt actor must be owner-private")
    spawn_generator = SPAWN_GENERATOR.read_text()
    for actor in ("tbwt_tamayu_hunter", "tbwt_tamayu_final_hunter", "tbwt_beast_cutscene"):
        require(f'(\"{actor}\",' in spawn_generator,
                f"Tai Bwo Wannai Trio: spawn generator must exclude {actor}")

    jogre = TBWT_JOGRE.read_text()
    require_text(
        jogre,
        (
            "[label,tbwt_smelt_jogre_bones]", "stat_advance(cooking, 250);",
            "[label,tbwt_jogre_bones_superheat]", "~delete_spell_runes(~get_spell_data(^superheat_item));",
            "~give_spell_xp(~get_spell_data(^superheat_item));",
            "[label,light_jogre_bones_inv](int $slot)", "stat(firemaking) < 30",
            "walktrigger(clear_jogre_timer);", "stat_advance(firemaking, 900);",
            "loc_add($fire_coord, bones_in_paste_fire", "obj_addall($fire_coord, tbwt_burnt_jogre_bones, 1, 100);",
            "[label,cook_pasty_jogre_bones](obj $bones)", "tbwt_burnt_jogre_bones_marinated_in_karambwanji",
            "[label,explode_cooked_jogre_bones](obj $bones)", "inv_add(inv, ashes, 1);",
            "damage(uid, hitsplat_damage, 2);",
        ),
        "Tai Bwo Wannai Trio jogre-bone pipeline",
    )
    require_text(TBWT_FIREMAKING.read_text(),
                 ("last_useitem = tbwt_jogre_bones", "@light_jogre_bones_inv(last_useslot);"),
                 "Tai Bwo Wannai Trio Firemaking hook")
    require_text(TBWT_SUPERHEAT.read_text(),
                 ("if ($ore1 = tbwt_jogre_bones)", "@tbwt_jogre_bones_superheat;"),
                 "Tai Bwo Wannai Trio Superheat hook")

    grind = TBWT_GRIND.read_text()
    for source, product in (
        ("tbwt_raw_karambwan", "tbwt_raw_karambwan_paste"),
        ("tbwt_poorly_cooked_karambwan", "tbwt_poisonous_karambwan_paste"),
        ("tbwt_cooked_karambwan", "tbwt_cooked_karambwan_paste"),
        ("tbwt_raw_karambwanji", "tbwt_raw_karambwanji_paste"),
        ("tbwt_cooked_karambwanji", "tbwt_cooked_karambwanji_paste"),
    ):
        require_text(grind, (f"data=input,{source}", f"data=output,{product}"),
                     f"Tai Bwo Wannai Trio grind row {source}")

    cooking = TBWT_COOKING.read_text()
    require_text(
        cooking,
        (
            "[label,cook_tbwt_karambwan](category $source)",
            "%varp6054_tbwt_tinsay >= ^tbwt_tinsay_claimed_final_reward & stat(cooking) >= 30",
            '"Cook it thoroughly."', "stat_random(cooking, 70, 256)",
            "inv_add(inv, tbwt_burnt_karambwan, 1);", "inv_add(inv, tbwt_cooked_karambwan, 1);",
            "stat_advance(cooking, 1900);", "inv_add(inv, tbwt_poorly_cooked_karambwan, 1);",
            "stat_advance(cooking, 800);", "@cook_pasty_jogre_bones(last_useitem);",
            "@explode_cooked_jogre_bones(last_useitem);",
        ),
        "Tai Bwo Wannai Trio Karambwan cooking",
    )
    require_text(
        TBWT_COOKING_ROWS.read_text(),
        ("data=uncooked,tbwt_raw_karambwanji", "data=cooked,tbwt_cooked_karambwanji",
         "data=experience,100", "data=uncooked,tbwt_raw_karambwan", "data=burnt,tbwt_burnt_karambwan"),
        "Tai Bwo Wannai Trio cooking discovery rows",
    )
    poison = TBWT_POISON.read_text()
    require_text(poison,
                 ("case tbwt_dragon_spear_kp : return(dragon_spear);",
                  "case brut_bronze_spear_kp : return(brut_bronze_spear);",
                  "case brut_rune_spear_kp : return(brut_rune_spear);"),
                 "Tai Bwo Wannai Trio cleaning cloth reverse map")

    tamayu_final = TBWT_TAMAYU_FINAL.read_text()
    tiadeche_final = TBWT_TIADECHE_FINAL.read_text()
    tinsay_final = TBWT_TINSAY_FINAL.read_text()
    require_text(tamayu_final,
                 ("inv_freespace(inv) < 1", "stat_advance(attack, 25000);",
                  "stat_advance(strength, 25000);", "inv_add(inv, tbwt_rune_spear_kp, 1);",
                  "else @tamayus_spear_stall__1_open;"),
                 "Tai Bwo Wannai Trio Tamayu reward and shop gate")
    require_text(tiadeche_final,
                 ("stat_advance(fishing, 50000);", "else @tiadeches_karambwan_stall_open;"),
                 "Tai Bwo Wannai Trio Tiadeche reward and shop gate")
    require("properly cook Karambwan" not in tiadeche_final,
            "Tai Bwo Wannai Trio: Tiadeche incorrectly teaches modern Karambwan cooking")
    require_text(tinsay_final,
                 ("stat_advance(cooking, 50000);", "teaches you how to cook Karambwan thoroughly"),
                 "Tai Bwo Wannai Trio Tinsay cooking lesson")

    tamayu_shop = TBWT_TAMAYU_SHOP.read_text()
    tiadeche_shop = TBWT_TIADECHE_SHOP.read_text()
    require("[opnpc3," not in tamayu_shop and "[opnpc3," not in tiadeche_shop,
            "Tai Bwo Wannai Trio: generated shops must not bind pre-quest/cutscene owners")
    require_text(SHOP_GENERATOR.read_text(),
                 ('"tamayus_spear_stall__1": []', '"tiadeches_karambwan_stall": []',
                  "QUEST_GATED_OWNER_OVERRIDES.get(shop_key, discovered_owners)"),
                 "Tai Bwo Wannai Trio durable shop generation gate")
    require_text(TBWT_TAMAYU_STOCK.read_text(),
                 ("stock1=tbwt_bronze_spear_kp,10,10", "stock6=tbwt_rune_spear_kp,0,100",
                  "stock7=tbwt_cleaning_cloth,10,5"),
                 "Tai Bwo Wannai Trio Tamayu stock")
    require_text(TBWT_TIADECHE_STOCK.read_text(),
                 ("stock1=tbwt_raw_karambwan,10,10", "stock2=tbwt_raw_karambwanji,50,10",
                  "stock3=tbwt_karambwan_vessel,2,100"),
                 "Tai Bwo Wannai Trio Tiadeche stock")
    require_text(TBWT_NPC.read_text(),
                 ("[tbwt_tamayu_multinpc_house]", "multinpc7=tbwt_tamayu_final"),
                 "Tai Bwo Wannai Trio returned Tamayu form")

    shaikahan = TBWT_SHAIKAHAN.read_text()
    require_text(shaikahan,
                 ("[opnpc2,tbwt_beast]", "%varp320_tbwt_main < ^tbwt_complete", "@player_combat_start;"),
                 "Tai Bwo Wannai Trio post-quest Shaikahan gate")
    require_text(PLAYER_HIT_FUNNEL.read_text(),
                 ("npc_type = tbwt_beast & ~tbwt_is_kp_spear", "$prepared = 0;"),
                 "Tai Bwo Wannai Trio Shaikahan direct-damage immunity")
    require_text(TBWT_COMBAT_NPC.read_text(),
                 ("[tbwt_beast]", "hitpoints=100", "attack=80", "strength=80", "defence=25",
                  "param=attackrate,4", "param=damagetype,1", "param=death_drop,tbwt_beast_bones"),
                 "Tai Bwo Wannai Trio Shaikahan combat row")


def check_troll_stronghold() -> None:
    core = TROLL_CORE.read_text()
    require_text(
        core,
        (
            "[oploc1,troll_climbingrocks]", "%varp317_troll_quest < ^troll_started",
            "stat(agility) < 15", "inv_total(worn, death_climbingboots) = 0",
            "[mapzone,0_45_56]", "[proc,troll_ensure_dad]", "npc_setowner;",
            "[oploc1,troll_stronghold_arena_exit_left]", "%varb3_troll_accepted_challenge = ^true",
            "[oploc1,troll_stronghold_prison_door_closed]", "inv_del(inv, troll_key_prison, 1);",
            "[mapzone,0_44_157]", "[proc,troll_ensure_prisoners]",
            "npc_add(0_44_157_11_29, troll_godric, 32000);",
            "npc_add(0_44_157_11_33, troll_eadgar, 32000);",
            "[label,troll_unlock_cell_1]", "%varp317_troll_quest = ^troll_freed_godric;",
            "inv_del(inv, troll_key_godric, 1);", "[label,troll_unlock_cell_2]",
            "%varb0_troll_freed_eadgar = ^true;", "inv_del(inv, troll_key_eadgar, 1);",
            "[proc,troll_npc_forcewalk]", "[queue,troll_quest_complete]",
            "inv_freespace(inv) < 1", "%varp317_troll_quest = ^troll_complete;",
            "inv_add(inv, law_talisman, 1);", "~quest_complete_rewards(quest_trollstronghold",
        ),
        "Troll Stronghold route, private prisoners and reward",
    )
    require(core.count("npc_setowner;") >= 3,
            "Troll Stronghold: Dad and both prisoners must be owner-private")

    dad = TROLL_DAD.read_text()
    require_text(
        dad,
        (
            "[opnpc2,troll_champion]", "[apnpc2,troll_champion]",
            "[ai_opplayer2,troll_champion]", "random(3) = 0",
            "%varp5756_aggressive_npc = npc_uid;", "movecoord($centre, $step_x, 0, $step_z)",
            "npc_attackdelay(8);", "[ai_queue2,troll_champion]",
            "$damage = max(0, sub(npc_stat(hitpoints), 19));",
            "[label,troll_dad_surrender]", "%varp317_troll_quest = ^troll_defeated_dad;",
            "I'm not done yet! Prepare to die!", "%varb5_troll_to_the_death = ^true;",
            "[ai_queue3,troll_champion]", "obj_add(npc_coord, big_bones, 1",
            "random(400) = 0", "random(10025) < 2", "troll_spectator7",
            "npc_setmode(opplayer2);",
        ),
        "Troll Stronghold Dad combat contract",
    )

    guards = TROLL_GUARDS.read_text()
    require_text(
        guards,
        (
            "[ai_timer,troll_prison_guard1]", "[ai_timer,troll_prison_guard2]",
            "npc_changetype_keepall(troll_prison_guard1_awake, 500)",
            "npc_changetype_keepall(troll_prison_guard2_awake, 500)",
            "[opnpc3,troll_prison_guard1]", "troll_key_godric",
            "[opnpc3,troll_prison_guard2]", "troll_key_eadgar",
            "stat(thieving) < 30", "stat_random(thieving, 60, 300) = false",
            "npc_setmode(opplayer2);", "inv_total(inv, $key) > 0",
            "inv_freespace(inv) = 0", "sound_synth(pick, 1, 0);",
        ),
        "Troll Stronghold Twig/Berry wake and pickpocket contract",
    )

    general_drop = TROLL_GENERAL_DROP.read_text()
    require_text(
        general_drop,
        (
            "[ai_queue3,troll_general]", "[ai_queue3,troll_general2]",
            "[ai_queue3,troll_general3]", "~obj_gettotal(troll_key_prison) = 0",
            "obj_add_private(npc_coord, troll_key_prison, 1", "random(128)",
            "~troll_gem_drop(true)", "random(28) = 0", "random(98) = 0",
            "random(400) = 0", "random(10025) < 2",
        ),
        "Troll Stronghold general key and drop contract",
    )
    guard_drop = TROLL_GUARD_DROP.read_text()
    require_text(
        guard_drop,
        (
            "[ai_queue3,troll_prison_guard1]", "[ai_queue3,troll_prison_guard2]",
            "obj_add_private(npc_coord, troll_key_godric, 1",
            "obj_add_private(npc_coord, troll_key_eadgar, 1", "random(128)",
            "~troll_uncommon_seed", "[proc,troll_uncommon_seed]", "random(1048)",
            "~troll_gem_drop(false)", "[proc,troll_gem_drop]", "random(45) = 0",
            "random(268) = 0", "random(400) = 0", "random(10025) < 2",
        ),
        "Troll Stronghold Twig/Berry key and drop contract",
    )

    overlay = TROLL_NPC.read_text()
    require_text(
        overlay,
        (
            "[troll_champion]", "timer=1", "param=elemental_weakness_percent,40",
            "param=combat_xp_multiplier,1050", "param=death_drop,null",
            "[troll_general]", "[troll_general2]", "[troll_general3]",
            "param=elemental_weakness_percent,20", "param=combat_xp_multiplier,1075",
            "[troll_prison_guard1]", "[troll_prison_guard1_awake]",
            "[troll_prison_guard2]", "[troll_prison_guard2_awake]",
        ),
        "Troll Stronghold NPC overlays",
    )
    require_text(
        TBWT_COMBAT_NPC.read_text(),
        (
            "[troll_champion]", "hitpoints=120", "attack=60", "strength=120", "defence=50",
            "param=attackrate,8", "[troll_general]", "hitpoints=140",
            "attack=70", "strength=140", "defence=40", "param=attackrate,4",
            "[troll_prison_guard1]", "hitpoints=90", "attack=40",
            "strength=90", "defence=25", "param=attackrate,6",
        ),
        "Troll Stronghold generated combat rows",
    )
    require_text(COMBAT_PARAM.read_text(),
                 ("[combat_xp_multiplier]", "default=1000"),
                 "Troll Stronghold combat-XP parameter")
    require_text(COMBAT_XP.read_text(),
                 ("scale(npc_param(combat_xp_multiplier), 1000, multiply($damage, 10))",),
                 "Troll Stronghold combat-XP multiplier application")

    generator = SPAWN_GENERATOR.read_text()
    for actor, x, z in (("troll_champion", 2911, 3612),
                        ("troll_godric", 2827, 10077),
                        ("troll_eadgar", 2829, 10083)):
        require(f'(\"{actor}\", {x}, {z}, 0)' in generator,
                f"Troll Stronghold: spawn generator must exclude {actor}")
    require("troll_champion" not in TROLL_DAD_SPAWN.read_text(),
            "Troll Stronghold: Dad must not have a public static spawn")
    prison_spawns = TROLL_PRISON_SPAWN.read_text()
    require("troll_godric" not in prison_spawns and "troll_eadgar" not in prison_spawns,
            "Troll Stronghold: prisoners must not have public static spawns")

    require_text(
        TROLL_DENULTH.read_text(),
        ("%varp317_troll_quest >= ^troll_started", "[label,denulth_troll]",
         "%varp317_troll_quest = ^troll_started;", "[label,denulth_trollquest]"),
        "Troll Stronghold Denulth start and reminders",
    )
    require_text(
        TROLL_DUNSTAN.read_text(),
        ("%varp317_troll_quest = ^troll_freed_godric", "inv_freespace(inv) = 0",
         "queue(troll_quest_complete, 0, 0);", "~obj_gettotal(law_talisman) = 0",
         "[label,dunstan_lawtali]", "inv_total(inv, coins) < 1000",
         "inv_del(inv, coins, 1000);", "inv_add(inv, law_talisman, 1);"),
        "Troll Stronghold Dunstan completion and replacement",
    )


def check_shades_of_mortton() -> None:
    core = MORTTON_CORE.read_text()
    require_text(
        core,
        (
            "[proc,mortton_mix_serum]", "~attempt_brew_potion($ashes_slot, $tarromin_slot)",
            "%varp339_morttonquest = ^mortton_made_serum;", "[queue,mortton_quest_complete]",
            "%varp339_morttonquest = ^mortton_quest_complete;", "stat_advance(crafting,20000);",
            "stat_advance(herblore,20000);", "shadekey_silver_purple",
        ),
        "Shades of Mort'ton quest and reward route",
    )

    shades = MORTTON_SHADES.read_text()
    require_text(
        shades,
        (
            "[ai_queue3,_shade]", "npc_param(death_drop)",
            "npc_type = shadeshadow_level1 | npc_type = shade_level1",
            "queue(mortton_quest_shade_kill, 0, 0);", "[queue,mortton_quest_shade_kill]",
            "%varp339_morttonquest = ^mortton_killed_5_shades;", "random(20) = 0",
            "stat_sub(strength, 1, 0);", "%varp6753_temple_sanctity = min(3000",
        ),
        "Shades of Mort'ton shade combat, remains and five-kill credit",
    )

    temple = MORTTON_TEMPLE.read_text()
    require_text(
        temple,
        (
            "[oploc1,_temple_wall]", "stat(crafting) < 20",
            "inv_total(inv, hammer) < 1 & inv_total(inv, flamtaer_hammer) < 1",
            "[proc,mortton_temple_crafting_roll]", "$crafting_bonus = 40",
            "~mortton_has_temple_timber", "inv_del(inv, swamppaste, 5);",
            "[timer,sanctity_drain]", "[timer,mortton_temple_attack]",
            "%current_temple_build = max(0, sub(%current_temple_build, 20));",
            "[proc,mortton_degrade_temple_wall]", "[oploc1,templefire_altar_nofire]",
            "%varp339_morttonquest = ^mortton_created_sacred_oil;",
        ),
        "Shades of Mort'ton Flamtaer temple contract",
    )

    pyre = MORTTON_PYRE.read_text()
    require_text(
        pyre,
        (
            "[label,create_sacred_logs]", "pyre_required_doses",
            "[proc,mortton_pyromancer_xp]", "if ($bonus = 20) $bonus = 25;",
            "[oploc1,temple_pyre]", "[proc,mortton_best_pyre_logs]",
            "[oploc1,_pyre_loaded]", "[proc,mortton_best_pyre_remains]",
            "[oploc1,_pyre_remains_loaded]", "[label,light_funeral_pyre]",
            "%varb4490_morytania_diary_elite_complete", "%varb4489_morytania_diary_hard_complete",
            "[proc,mortton_pyre_prayer_xp]", "[proc,give_shade_rewards]",
            "obj_add_private($coord, $reward, $count, 500, 500);",
        ),
        "Shades of Mort'ton pyre, XP and private reward contract",
    )
    pyre_config = MORTTON_PYRE_CONFIG.read_text()
    for section, level, fire_xp, doses in (
        ("pyre_logs", 5, 505, 2), ("pyre_oak_logs", 20, 700, 2),
        ("pyre_willow_logs", 35, 1000, 3), ("pyre_teak_logs", 40, 1200, 3),
        ("pyre_arctic_logs", 47, 1585, 2), ("pyre_maple_logs", 50, 1750, 3),
        ("pyre_mahogany_logs", 55, 2100, 3), ("pyre_yew_logs", 65, 2550, 4),
        ("pyre_camphor_logs", 71, 3200, 4), ("pyre_magic_logs", 80, 4045, 4),
        ("pyre_ironwood_logs", 85, 4350, 4), ("pyre_redwood_logs", 95, 5000, 4),
        ("pyre_rosewood_logs", 97, 5900, 4),
    ):
        require_text(pyre_config,
                     (f"[{section}]", f"param=pyre_level,{level}",
                      f"param=pyre_fm_experience,{fire_xp}",
                      f"param=pyre_required_doses,{doses}"),
                     f"Shades of Mort'ton {section} pyre row")

    shade_config = MORTTON_SHADE_CONFIG.read_text()
    for section, low, high, low_key, high_key in (
        ("loar_shades", 200, 300, 0, 790),
        ("phrin_shades", 400, 500, 125, 665),
        ("riyl_shades", 600, 700, 125, 665),
        ("asyn_shades", 800, 900, 282, 508),
        ("fiyr_shades", 2000, 4000, 634, 156),
        ("urium_shades", 2000, 7000, 790, 0),
    ):
        require_text(shade_config,
                     (f"[{section}]", f"param=shades_low_coin,{low}",
                      f"param=shades_high_coin,{high}",
                      f"param=shades_low_key_chance,{low_key}",
                      f"param=shades_high_key_chance,{high_key}"),
                     f"Shades of Mort'ton {section} reward row")

    catacombs = MORTTON_CATACOMBS.read_text()
    require_text(
        catacombs,
        (
            "[label,enter_shade_catacombs]", "%varp339_morttonquest < ^mortton_quest_complete",
            "[oploc1,shadelair_steeldoor]", "[oploc1,shadelair_blackdoor]",
            "[oploc1,shadelair_silverdoor]", "[oploc1,shadelair_golddoor]",
            "[label,open_shade_chest]", "add(inv_freespace(inv), 1) < $slots_needed",
            "inv_del(inv, $key, 1);", "~mortton_chest_preroll(shades_lock_bronze, 0)",
            "~mortton_chest_preroll(shades_lock_steel, 599)",
            "~mortton_chest_preroll(shades_lock_black, 186)",
            "~mortton_chest_preroll(shades_lock_silver, 76)",
            "~mortton_chest_preroll(shades_lock_gold, 54)",
            "sub(63, $wealth_bonus)", "[proc,mortton_splitbark_reward]",
            "~obj_gettotal(flamtaer_bag) = 0 & random(2) = 0",
            "[oplocu,shade_lair_temple_altar]", "%varp6754_mortton_altar_charges = add",
            "[oploc1,shade_lair_temple_altar]", "stat_heal(prayer, stat_base(prayer), 0);",
        ),
        "Shades of Mort'ton catacombs, chest and Altar of the Damned contract",
    )
    require(catacombs.count("@open_shade_chest(") == 50,
            "Shades of Mort'ton: all 25 chests need open and item-use handlers")

    coffin = MORTTON_COFFIN.read_text()
    require_text(
        coffin,
        (
            "[opnpc1,shades_coffin_keeper]", "[proc,mortton_dampe_repair]",
            "[proc,mortton_coffin_fill]", "~mortton_coffin_fill(3)",
            "~mortton_coffin_fill(8)", "~mortton_coffin_fill(14)",
            "~mortton_coffin_fill(20)", "~mortton_coffin_fill(28)",
            "[proc,mortton_coffin_empty]", "shade_bones6", "shade_bones1",
            "[proc,mortton_coffin_remove_lock]", "[proc,mortton_coffin_destroy]",
            "inv_clear(mortton_coffin_storage);",
        ),
        "Shades of Mort'ton Dampe and wearable coffin contract",
    )
    require_text(MORTTON_COFFIN_CONFIG.read_text(),
                 ("[mortton_coffin_storage]", "size=28"),
                 "Shades of Mort'ton private coffin storage")

    require_text(MORTTON_AFFLICTED.read_text(),
                 ("[queue,207_cure_affliction_reward]", "[queue,208_cure_affliction_reward]",
                  "inv_add(inv, shadekey_bronze_bloodred, 1);"),
                 "Shades of Mort'ton afflicted serum reward tables")
    require_text(MORTTON_RAZMIRE.read_text(),
                 ("%varp339_morttonquest = ^mortton_kill_shades;", "[label,razmire_general_open]",
                  "[label,razmire_building_open]"),
                 "Shades of Mort'ton Razmire route and shops")
    require_text(MORTTON_ULSQUIRE.read_text(),
                 ("%varp339_morttonquest = ^mortton_shades_to_ulsquire;",
                  "queue(mortton_quest_complete, 0, 0);", "inv_add(inv, oliveoil3, 1);"),
                 "Shades of Mort'ton Ulsquire route")


def check_fremennik_trials() -> None:
    npc = VIKING_NPC.read_text()
    for form, hp, attack, strength, defence, speed in (
        ("viking_enemy1", 30, 20, 20, 20, 4),
        ("viking_enemy2", 50, 40, 40, 40, 4),
        ("viking_enemy3", 70, 60, 60, 60, 4),
        ("viking_enemy4", 255, 255, 5, 255, 1),
    ):
        start = npc.index(f"[{form}]")
        end = npc.find("\n[", start + 1)
        block = npc[start:] if end == -1 else npc[start:end]
        require_text(
            block,
            (f"hitpoints={hp}", f"attack={attack}", f"strength={strength}",
             f"defence={defence}", f"param=attackrate,{speed}",
             "param=death_drop,null"),
            f"The Fremennik Trials {form} combat row",
        )
    start = npc.index("[viking_draugen]")
    end = npc.find("\n[", start + 1)
    draugen_block = npc[start:] if end == -1 else npc[start:end]
    require_text(
        draugen_block,
        ("hitpoints=60", "attack=60", "strength=60", "defence=60",
         "param=attackrate,4", "param=stabdefence,100", "param=slashdefence,100",
         "param=crushdefence,100", "param=magicdefence,500",
         "param=rangedefence,500", "param=elemental_weakness,^element_air",
         "param=elemental_weakness_percent,50", "param=combat_xp_multiplier,1025",
         "param=undead,^true", "param=death_drop,null"),
        "The Fremennik Trials Draugen combat row",
    )

    constants = VIKING_CONSTANT.read_text()
    require_text(
        constants,
        ("^viking_koschei_phase_timeout = 1000", "^viking_draugen_spot_count = 12",
         "^viking_draugen_move_delay = 80", "^viking_draugen_lifetime = 1000",
         "^viking_draugen_reveal_range = 3"),
        "The Fremennik Trials encounter constants",
    )
    require_text(
        VIKING_VARP.read_text(),
        ("[varp6757_viking_draugen_spot]", "[varp6756_viking_draugen_active]",
         "[varp6758_viking_koschei_active]", "[varp6759_viking_koschei_phase]"),
        "The Fremennik Trials private encounter state",
    )

    sigli = VIKING_SIGLI.read_text()
    require_text(
        sigli,
        ("[opheld1,viking_draugen_talisman_uncharged]",
         "distance(coord, $target) <= ^viking_draugen_reveal_range",
         "npc_add($target, viking_draugen, ^viking_draugen_lifetime);",
         "npc_setowner;", "settimer(viking_draugen_move, ^viking_draugen_move_delay);",
         "[timer,viking_draugen_timeout]", "[ai_queue3,viking_draugen]",
         "if (npc_findhero = ^false)", "inv_del(inv, viking_draugen_talisman_uncharged, 1);",
         "inv_add(inv, viking_draugen_talisman, 1);",
         "obj_add_private(coord, viking_draugen_talisman, 1, ^lootdrop_duration, 100);",
         "inv_del(inv, viking_draugen_talisman, 1);", "[proc,viking_draugen_direction]",
         "north-east", "north-west", "south-east", "south-west"),
        "The Fremennik Trials Sigli and Draugen controller",
    )
    require(sigli.count("case ") == 12,
            "The Fremennik Trials: Draugen must have twelve moving anchors")

    thorvald = VIKING_THORVALD.read_text()
    require_text(
        thorvald,
        ("[oploc2,viking_warrior_ladder]", "~viking_koschei_forbidden_loadout",
         "%varp6759_viking_koschei_phase = 0;", "~viking_koschei_spawn(1);",
         "[ai_queue3,viking_enemy1]", "~viking_koschei_spawn(2);",
         "[ai_queue3,viking_enemy2]", "~viking_koschei_spawn(3);",
         "[ai_queue3,viking_enemy3]", "~viking_koschei_spawn(4);",
         "[ai_queue3,viking_enemy4]", "stat_sub(prayer, stat(prayer), 0);",
         "if (stat(hitpoints) <= 1)", "~viking_koschei_finish(false);",
         "~set_viking_thorvald_progress(^thorvald_complete);",
         "inv_add(inv, viking_sword, 1);",
         "obj_add_private(^viking_koschei_upstairs, viking_sword, 1, ^lootdrop_duration, 100);",
         "[proc,viking_koschei_try_safe_death]()(boolean)",
         "[proc,viking_koschei_on_logout]", "[proc,viking_koschei_drop_blocked]()(boolean)",
         "[opheld2,viking_sword]", "if (%varp347_viking ! ^viking_complete)",
         "case magic_runes, firemaking_logs, arrowheads"),
        "The Fremennik Trials Thorvald and Koschei controller",
    )
    for form in ("viking_enemy1", "viking_enemy2", "viking_enemy3", "viking_enemy4"):
        marker = f"[ai_queue3,{form}]"
        start = thorvald.index(marker)
        end = thorvald.find("\n[", start + 1)
        block = thorvald[start:] if end == -1 else thorvald[start:end]
        require("if (npc_findhero = ^false)" in block,
                f"The Fremennik Trials: {form} must reject uncredited death")

    require_text(
        VIKING_PEER.read_text(),
        ("[proc,viking_peer_can_deposit_all]()(boolean)", "~wint_is_activity_item($obj)",
         "if (inv_freespace(bank) >= $needed)", "[proc,viking_peer_deposit_all]",
         "inv_moveitem_uncert(inv, bank", "inv_moveitem_uncert(worn, bank"),
        "The Fremennik Trials atomic Peer bank-all",
    )

    citizens = VIKING_CITIZENS.read_text()
    for actor in ("viking_man2", "viking_man3", "viking_man4", "viking_woman2"):
        require(f"[ai_queue3,{actor}]" in citizens,
                f"The Fremennik Trials: missing citizen drop binding for {actor}")
    require_text(
        citizens,
        ("obj_add(npc_coord, bones, 1", "def_int $roll = random(512);",
         "$roll < 100", "bronze_warhammer", "$roll < 170", "iron_warhammer",
         "%varp347_viking = ^viking_complete", "viking_sword", "viking_shield", "viking_helmet",
         "$roll < 183", "~randomherb", "copper_ore, 5", "tin_ore, 5", "iron_ore, 5",
         "coal, 1", "steel_bar, 1", "coins, 6", "coins, 15", "coins, 16",
         "coins, 20", "coins, 38", "tinderbox", "vial_empty", "vial_water",
         "bucket_empty", "$roll < 490", "viking_unstrung_lyre", "beer",
         "eye_of_newt", "snape_grass", "jangerberries", "%varp188_heroquest = ^hero_complete",
         "blamish_oil", "$roll < 502", "~troll_gem_drop(true)",
         "if (random(128) = 0)", "trail_clue_easy_simple001"),
        "The Fremennik Trials citizen 512-slot and tertiary loot contract",
    )

    require_text(PLAYER_DEATH.read_text(), ("~viking_koschei_try_safe_death",),
                 "The Fremennik Trials safe-death hook")
    require_text(PLAYER_LOGOUT.read_text(), ("~viking_koschei_on_logout;",),
                 "The Fremennik Trials logout hook")
    require_text(PLAYER_DROP.read_text(), ("~viking_koschei_drop_blocked",),
                 "The Fremennik Trials arena drop gate")
    require_text(CRUMBLE_UNDEAD.read_text(),
                 ("if (npc_type = viking_draugen)",
                  "Crumble Undead has no effect on the Draugen."),
                 "The Fremennik Trials Draugen spell exception")

    generated = VIKING_GENERATED_ANIMS.read_text()
    for actor in ("viking_enemy1", "viking_enemy2", "viking_enemy3", "viking_enemy4",
                  "viking_draugen", "viking_draugen_safe"):
        require(f"[{actor}]" not in generated,
                f"The Fremennik Trials: authored {actor} must not be duplicated by generated animation config")


def check_horror_from_the_deep() -> None:
    route = HORROR_INTERACTIONS.read_text()
    require_text(
        route,
        ("[oplocu,horror_broken_bridge_left_spot]",
         "[oplocu,horror_broken_bridge_right_spot]", "if ($item ! woodplank)",
         "inv_total(inv, hammer) = 0", "inv_total(inv, nails) < ^horror_bridge_nails_each",
         "inv_del(inv, woodplank, 1);", "inv_del(inv, nails, ^horror_bridge_nails_each);",
         "[oploc1,horror_lighthouse_doorway]", "inv_del(inv, horror_key, 1);",
         "p_teleport(0_38_71_13_51);",
         "[oploc1,horror_bookcase]", "horror_diary1", "horror_diary2",
         "horror_diary3", "[oplocu,horror_lighthouse_cog_broken]",
         "last_useitem = swamp_tar", "last_useitem = molten_glass", "last_useitem = tinderbox",
         "[oplocu,horror_mid_left_door]", "[oplocu,horror_mid_right_door]",
         "$obj = airrune", "$obj = waterrune", "$obj = earthrune",
         "$obj = firerune", "~horror_is_sword($obj)",
         "~horror_is_arrow($obj)", "inv_del(inv, $sword, 1);", "inv_del(inv, $arrow, 1);",
         "case weapon_slash_sword, weapon_stab_sword, weapon_2h_sword",
         "case arrows, arrows_dragon, ammo_ogre_arrow, ammo_training_arrow",
         "[oploc1,horror_far_right_door]", "[opobj3,horror_casket]"),
        "Horror from the Deep route and item consumption",
    )

    npc = HORROR_NPC.read_text()
    junior_forms = (
        "horror_dagannoth_jr1", "horror_dagannoth_jr2",
        "horror_dagannoth_jr3", "horror_dagannoth_jr4",
    )
    mother_forms = (
        "horror_dagganoth_aira", "horror_dagganoth_airb",
        "horror_dagganoth_airc", "horror_dagganoth_air",
        "horror_dagganoth_water", "horror_dagganoth_melee",
        "horror_dagganoth_earth", "horror_dagganoth_fire",
        "horror_dagganoth_ranged",
    )
    for actor in junior_forms + mother_forms:
        start = npc.index(f"[{actor}]")
        end = npc.find("\n[", start + 1)
        block = npc[start:] if end == -1 else npc[start:end]
        require_text(
            block,
            ("hitpoints=120", "attack=78", "strength=78", "defence=81",
             "magic=1", "ranged=50", "param=attackrate,4",
             "param=strengthbonus,9", "param=elemental_weakness,^element_earth",
             "param=elemental_weakness_percent,35", "param=death_drop,null"),
            f"Horror from the Deep {actor} combat row",
        )
        if actor in junior_forms:
            require_text(
                block,
                ("param=stabdefence,0", "param=slashdefence,0",
                 "param=crushdefence,0", "param=magicdefence,0",
                 "param=rangedefence,0"),
                f"Horror from the Deep {actor} zero defences",
            )
        else:
            require_text(
                block,
                ("param=stabdefence,150", "param=slashdefence,150",
                 "param=crushdefence,150", "param=magicdefence,50",
                 "param=rangedefence,50", "param=combat_xp_multiplier,1050"),
                f"Horror from the Deep {actor} Mother defences",
            )

    require_text(
        HORROR_CONSTANT.read_text(),
        ("^horror_boss_spawn = 0_39_72_22_34",
         "^horror_boss_exit = 0_39_56_13_54", "^horror_boss_lifetime = 1000",
         "^horror_junior_regen_ticks = 20", "^horror_mother_colour_ticks = 30",
         "^horror_bridge_nails_each = 30"),
        "Horror from the Deep encounter constants",
    )
    require_text(
        HORROR_VARP.read_text(),
        ("[varp6775_horror_boss_active]", "[varp6776_horror_magic_element]", "[varp6777_horror_reward_book]",
         "scope=temp", "scope=perm"),
        "Horror from the Deep private encounter and reward state",
    )

    encounter = HORROR_ENCOUNTER.read_text()
    require_text(
        encounter,
        ("[queue,horror_spawn_junior]", "npc_add($start, horror_dagannoth_jr1, 1000);",
         "npc_setowner;", "[ai_timer,horror_dagannoth_jr4]",
         "npc_statheal(hitpoints, 1, 0);", "npc_settimer(20);",
         "[timer,horror_timeout]", "settimer(horror_timeout, 1000);",
         "[ai_queue3,horror_dagannoth_jr4]", "if (npc_findhero = ^false)",
         "%varb34_horrorquest = ^horror_defeated_dagjr;",
         "npc_add($start, horror_dagganoth_aira, 1000);",
         "[ai_timer,horror_dagganoth_air]", "~horror_mother_change(horror_dagganoth_water);",
         "[ai_timer,horror_dagganoth_water]", "~horror_mother_change(horror_dagganoth_melee);",
         "[ai_timer,horror_dagganoth_melee]", "~horror_mother_change(horror_dagganoth_earth);",
         "[ai_timer,horror_dagganoth_earth]", "~horror_mother_change(horror_dagganoth_fire);",
         "[ai_timer,horror_dagganoth_fire]", "~horror_mother_change(horror_dagganoth_ranged);",
         "[ai_timer,horror_dagganoth_ranged]", "~horror_mother_change(horror_dagganoth_air);",
         "npc_settimer(30);", "def_int $hit1 = 0;", "def_int $hit2 = 0;",
         "$hit1 = randominc(12);", "$hit2 = randominc(12);",
         "~playerhit_n_ranged(true, $hit1, $duration);",
         "~playerhit_n_ranged(true, $hit2, $duration);",
         "obj_add_private($drop, bones, 1", "inv_add(inv, horror_casket, 1);",
         "%varb34_horrorquest = ^horror_complete;", "p_teleport(0_39_156_19_17);",
         "stat_advance(magic, 46625);", "stat_advance(strength, 46625);",
         "stat_advance(ranged, 46625);", "~quest_complete_rewards(quest_horrorfromthedeep",
         "[proc,horror_abort]", "[proc,horror_on_logout]", "[proc,horror_on_death]",
         "[proc,horror_mother_prepare_hit]", "case horror_dagganoth_air",
         "%varp6776_horror_magic_element = ^element_air", "case horror_dagganoth_water",
         "%varp6776_horror_magic_element = ^element_water", "case horror_dagganoth_melee",
         "%varp6295_damagetype = ^melee_style", "case horror_dagganoth_earth",
         "%varp6776_horror_magic_element = ^element_earth", "case horror_dagganoth_fire",
         "%varp6776_horror_magic_element = ^element_fire", "case horror_dagganoth_ranged",
         "%varp6295_damagetype = ^ranged_style", "return(0);"),
        "Horror from the Deep owner-private combat and reward controller",
    )

    require_text(
        HORROR_JOSSIK.read_text(),
        ("[opnpc1,horror_lighthousekeeeper_well]", "%varp6777_horror_reward_book = 1;",
         "%varp6777_horror_reward_book = 2;", "%varp6777_horror_reward_book = 3;",
         "~p_choice3_header(\"Saradomin\", 1, \"Zamorak\", 2, \"Guthix\", 3",
         "unfinished_saradominbook", "unfinished_zamorakbook", "unfinished_guthixbook",
         "inv_del(inv, horror_casket, 1);", "[opnpc4,horror_lighthousekeeeper_well]",
         "[proc,horror_has_reward_book]()(boolean)", "saradominbook_complete",
         "zamorakbook_complete", "guthixbook_complete", "inv_total(bank",
         "inv_total(worn"),
        "Horror from the Deep Jossik casket, choice and loss recovery",
    )
    require_text(
        HORROR_GUNNJORN.read_text(),
        ("[opnpc1,gunnjorn]", "~obj_gettotal(horror_key) = 0",
         "inv_add(inv, horror_key, 1);"),
        "Horror from the Deep lighthouse-key issue and replacement",
    )
    require_text(
        HORROR_HIT_FUNNEL.read_text(),
        ("~horror_mother_is_type = true", "~horror_mother_prepare_hit($prepared, $hit_success)"),
        "Horror from the Deep shared hit funnel",
    )
    require_text(
        HORROR_MAGIC.read_text(),
        ("%varp6776_horror_magic_element = ~elemental_spell_element($spell);",
         "%varp6776_horror_magic_element = ^element_none;"),
        "Horror from the Deep elemental spell latch",
    )
    require_text(PLAYER_DEATH.read_text(), ("~horror_on_death;",),
                 "Horror from the Deep death cleanup hook")
    require_text(PLAYER_LOGOUT.read_text(), ("~horror_on_logout;",),
                 "Horror from the Deep logout cleanup hook")

    generated = HORROR_GENERATED_ANIMS.read_text()
    for actor in junior_forms + mother_forms:
        require(f"[{actor}]" not in generated,
                f"Horror from the Deep: authored {actor} must not be duplicated by generated animation config")


def check_monkey_madness_i() -> None:
    require_text(
        MM1_ZOOKNOCK.read_text(),
        ("[opnpcu,mm_zooknock]", "case mm_monkey_dentures", "case gold_bar",
         "case mm_monkey_amulet_mould", "inv_add(inv, mm_enchanted_gold_bar, 1);",
         "case mm_small_ninja_monkey_bones, mm_medium_ninja_monkey_bones",
         "case mm_monkey_talisman", "def_namedobj $talisman = enum(int, namedobj, mm_bones_mapping",
         "inv_total(inv, mm_sigil) = 0", "inv_total(worn, mm_sigil) = 0",
         "inv_total(bank, mm_sigil) = 0", "inv_freespace(inv) < 1",
         "inv_add(inv, mm_sigil, 1);", "Waymottin hands you a replica"),
        "Monkey Madness I Zooknock item chain and Waymottin recovery",
    )
    require_text(
        MM1_GARKOR.read_text(),
        ("case ^garkor_need_correct_disguise", "~mm_wearing_greegree = true",
         "mm_monkey_greegree_for_normal_monkey", "%varb118_varbit_118 = ^awowogei_complete_mission",
         "case ^garkor_learned_plan", "%varb126_mm_garkor = ^garkor_joined_10th_squad;",
         "inv_freespace(inv) < 1", "inv_add(inv, mm_sigil, 1);",
         "inv_total(bank, mm_sigil) = 0"),
        "Monkey Madness I disguise, alliance and sigil induction",
    )

    npc = MM1_NPC.read_text()
    require_text(
        npc,
        ("[mm_demon]", "hitpoints=170", "attack=170", "strength=170",
         "defence=170", "magic=170", "ranged=1", "param=attackrate,6",
         "param=stabattack,50", "param=slashattack,50", "param=strengthbonus,50",
         "param=stabdefence,0", "param=slashdefence,50", "param=crushdefence,0",
         "param=magicdefence,50", "param=rangedefence,0",
         "param=elemental_weakness,^element_fire",
         "param=elemental_weakness_percent,25", "param=combat_xp_multiplier,1075",
         "param=death_drop,null"),
        "Monkey Madness I Jungle Demon exact combat row",
    )
    require_text(
        MM1_VARP.read_text(),
        ("[varp6783_mm_demon_active]", "protect=no", "transmit=no", "scope=temp"),
        "Monkey Madness I private encounter state",
    )

    demon = MM1_DEMON.read_text()
    require_text(
        demon,
        ("[opheld2,mm_sigil]", "%varp365_mm_main ! ^monkeymadness_completed_ch3",
         "%varb126_mm_garkor ! ^garkor_joined_10th_squad", "~wilderness_level(coord) > 0",
         "~p_choice2(\"Let the sigil teleport you\", 1, \"Not yet\", 2)",
         "p_telejump(1_42_143_14_21);", "[proc,mm_demon_spawn_squad]",
         "mm_garkor_final_battle", "mm_zooknock_final_battle",
         "mm_waymottin_final_battle", "mm_bunkwicket_final_battle",
         "mm_bunkdo_final_battle", "mm_carado_final_battle",
         "mm_lumo_final_battle", "mm_karam_final_battle", "mm_bonzara",
         "npc_add($spawn, $actor, 2000);", "npc_add($spawn, mm_demon, 2000);",
         "npc_setowner;", "[ai_timer,mm_demon]", "npc_settimer(6);",
         "def_int $gnome_hit = add(random(8), 3);",
         "npc_statheal(hitpoints, add(random(22), 21), 0);",
         "min($gnome_hit, sub(npc_stat(hitpoints), 10))",
         "case 0 : $spell = ^wind_wave", "case 1 : $spell = ^water_wave",
         "case 2 : $spell = ^earth_wave", "default : $spell = ^fire_wave",
         "npc_range(coord) < 3", "~npc_meleeattack;",
         "~npc_cast_spell_with_forced_max_hit($spell, 6, 32);",
         "[ai_queue3,mm_demon]", "if (npc_findhero = ^false)",
         "def_npc_uid $dead_demon = npc_uid;", "%varp6783_mm_demon_active = 2;",
         "%varp365_mm_main = ^monkeymadness_defeated_demon;",
         "obj_add_private(npc_coord, malicious_ashes, 1",
         "npc_finduid($dead_demon);", "~npc_default_death;",
         "[timer,mm_demon_monitor]", "[proc,mm_demon_cleanup]",
         "[proc,mm_demon_on_death]", "[proc,mm_demon_on_logout]",
         "[opnpc1,mm_garkor_final_battle]", "[opnpc1,mm_zooknock_final_battle]",
         "[opnpc1,mm_bonzara]"),
        "Monkey Madness I owner-private final battle controller",
    )
    require(demon.count("~mm_demon_spawn_squad_member(") == 9,
            "Monkey Madness I: final battle must spawn all nine support actors")

    require_text(
        MM1_NARNODE.read_text(),
        ("[label,mm_narnode_reward]", "if (inv_freespace(inv) < 3)",
         "inv_add(inv, coins, 10000);", "inv_add(inv, diamond, 3);",
         "[queue,mm_quest_complete]", "%varp365_mm_main = ^monkeymadness_complete;",
         "~quest_complete_rewards(quest_monkeymadness1"),
        "Monkey Madness I Narnode reward and completion",
    )
    require_text(
        MM1_DAERO.read_text(),
        ("[label,daero_training]", "%varp365_mm_main = ^monkeymadness_complete_training;",
         "stat_advance(attack, 200000);", "stat_advance(defence, 200000);",
         "stat_advance(strength, 350000);", "stat_advance(hitpoints, 350000);",
         "stat_advance(attack, 350000);", "stat_advance(defence, 350000);",
         "stat_advance(strength, 200000);", "stat_advance(hitpoints, 200000);"),
        "Monkey Madness I optional Daero training split",
    )
    require_text(PLAYER_DEATH.read_text(), ("~mm_demon_on_death;",),
                 "Monkey Madness I death cleanup hook")
    require_text(PLAYER_LOGOUT.read_text(), ("~mm_demon_on_logout;",),
                 "Monkey Madness I logout cleanup hook")
    require("[mm_demon]" not in MM1_GENERATED_ANIMS.read_text(),
            "Monkey Madness I: authored Jungle Demon must not be duplicated by generated animation config")


def check_haunted_mine() -> None:
    require_text(
        HMQ_ZEALOT.read_text(),
        ("[opnpc1,saradominist_zealot]", "%varp302_priestperil < ^priestperil_complete",
         "[opnpc3,saradominist_zealot]",
         "inv_total(bank, hauntedmine_lift_key) > 0", "inv_freespace(inv) < 1",
         "obj_add_private(npc_coord, hauntedmine_lift_key, 1"),
        "Haunted Mine Zealot requirements and key recovery",
    )
    require_text(
        HMQ_DUNGEON.read_text(),
        ("[oploc1,glowing_mushroom]", "[oploc1,glowing_mushroom2]",
         "settimer(hmq_fungus_monitor, 1);", "[opheld5,glowing_fungus]",
         "obj_add_private(coord, ashes, 1", "[proc,hmq_inside_mine]()(boolean)",
         "%varb2396_hauntedmine_endcart_fungus = 0;", "Oh dear, the mine cart seems to have sunk.",
         "How useful, it's come right back to where it started.",
         "[proc,hmq_levers_correct]()(boolean)", "[oploc1,hauntedmine_lift_valve]",
         "inv_total(inv, hauntedmine_lift_key) < 1", "hauntedmine_cheeky_ghost",
         "settimer(hmq_lift_ghost, ^hmq_lift_race_ticks);", "[timer,hmq_lift_ghost]",
         "%varb2393_hauntedmine_liftpoweredonce = 1;", "cleartimer(hmq_lift_ghost);"),
        "Haunted Mine fungus, cart and valve/lift route",
    )
    require_text(
        HMQ_CONSTANT.read_text(),
        ("^hmq_lift_race_ticks = 80", "^hmq_daythroom_min",
         "^hmq_daythroom_max", "^hmq_dayth_shift_1", "^hmq_dayth_shift_8",
         "^hmq_crystalroom_min", "^hmq_crystalroom_max"),
        "Haunted Mine encounter coordinates and calibrated race",
    )
    npc = HMQ_NPC.read_text()
    require_text(
        npc,
        ("[hauntedmine_boss_ghost]", "hitpoints=100", "attack=70", "strength=70",
         "defence=100", "magic=1", "ranged=1", "param=attackrate,4",
         "param=stabdefence,5", "param=slashdefence,5", "param=crushdefence,5",
         "param=magicdefence,-5", "param=rangedefence,5",
         "param=elemental_weakness,^element_air",
         "param=elemental_weakness_percent,25", "param=undead,^true",
         "param=death_drop,null"),
        "Haunted Mine exact Treus Dayth combat row",
    )
    require_text(
        HMQ_VARP.read_text(),
        ("[varp6792_hmq_dayth_active]", "[varp6793_hmq_dayth_crane_cd]", "protect=no",
         "transmit=no", "scope=temp"),
        "Haunted Mine private encounter state",
    )
    dayth = HMQ_DAYTH.read_text()
    require_text(
        dayth,
        ("[opnpc1,hauntedmine_boss_key]", "npc_add(^hmq_dayth_key_coord, hauntedmine_boss_ghost, 2000);",
         "npc_setowner;", "[ai_timer,hauntedmine_boss_ghost]", "randominc(15)",
         "%varb4117_prayer_protectfrommissiles = ^true", "randominc(8)",
         "%varb4118_prayer_protectfrommelee = ^true", "[proc,hmq_dayth_shift]",
         "if (npc_stat(hitpoints) <= 25)", "[timer,hmq_dayth_hazards]",
         "crane_posessed_mine", "randominc(10)", "[proc,hmq_on_dayth_track]()(boolean)",
         "randominc(9)", "[ai_queue3,hauntedmine_boss_ghost]",
         "def_npc_uid $dead_dayth = npc_uid;", "%varp382_hauntedmine = ^hmq_dayth_killed;",
         "stat(crafting) < ^hmq_req_crafting",
         "if (random(90) = 0)", "trail_clue_beginner", "[proc,hmq_dayth_cleanup]",
         "[proc,hmq_dayth_on_death]", "[proc,hmq_dayth_on_logout]",
         "inv_total(bank, hauntedmine_reward_key) > 0", "obj_add_private(npc_coord, hauntedmine_reward_key, 1",
         "[oploc1,hauntedmine_rewarddoor_l]", "[oploc1,hauntedmine_rewarddoor_r]",
         "~door_selfstage_open;", "[oploc1,crystalcorner]", "stat(crafting) < ^hmq_req_crafting",
         "inv_add(inv, crystalshard_necklace_unstrung, 1);",
         "stat_advance(strength, ^hmq_reward_strength_xp);"),
        "Haunted Mine owner-private Treus, machinery, loot and shard controller",
    )
    require("~ring_of_recoil_check" not in dayth,
            "Haunted Mine: Treus damage must not permit ring recoil")
    require_text(
        HMQ_STRINGING.read_text(),
        ("case crystalshard_necklace_unstrung", "[opheldu,crystalshard_necklace_unstrung]",
         "p_delay(2);", "inv_del(inv, ball_of_wool, 1);",
         "inv_add(inv, crystalshard_necklace, 1);"),
        "Haunted Mine salve amulet stringing",
    )
    require_text(PLAYER_DEATH.read_text(), ("~hmq_dayth_on_death;",),
                 "Haunted Mine death cleanup hook")
    require_text(PLAYER_LOGOUT.read_text(), ("~hmq_dayth_on_logout;",),
                 "Haunted Mine logout cleanup hook")
    require_text(PLAYER_LOGIN.read_text(), ("~hmq_fungus_login;",),
                 "Haunted Mine fungus login monitor")
    require("[hauntedmine_boss_ghost]" not in HMQ_GENERATED_ANIMS.read_text(),
            "Haunted Mine: authored Treus must not be duplicated by generated animation config")


def check_troll_romance() -> None:
    require_text(
        TROLLLOVE_CONSTANT.read_text(),
        ("^troll_love_slide_agility = 28", "^troll_love_arrg_lifetime = 500",
         "^troll_love_arrg_prayer_penetrate = 5",
         "^troll_love_arrg_ranged_accuracy_scale = 4",
         "^troll_love_arrg_melee_max = 38", "^troll_love_arrg_ranged_max = 30",
         "^troll_love_arena_min", "^troll_love_arena_max"),
        "Troll Romance encounter constants",
    )
    npc = TROLLLOVE_NPC.read_text()
    require_text(
        npc,
        ("[trollromance_arrg_attackable]", "hitpoints=140", "attack=70",
         "strength=140", "defence=40", "magic=0", "ranged=70",
         "param=attackrate,4", "param=damagetype,^slash_style",
         "param=slashattack,60", "param=strengthbonus,100",
         "param=stabdefence,35", "param=slashdefence,60",
         "param=crushdefence,35", "param=magicdefence,200",
         "param=rangedefence,200", "param=elemental_weakness,^element_earth",
         "param=elemental_weakness_percent,50",
         "param=combat_xp_multiplier,1075", "param=death_drop,null"),
        "Troll Romance exact Arrg combat row",
    )
    require_text(
        TROLLLOVE_VARP.read_text(),
        ("[varp6804_troll_love_arrg_active]", "[varp6805_troll_love_sled_riding]",
         "protect=no", "transmit=no", "scope=temp"),
        "Troll Romance private session state",
    )
    arrg = TROLLLOVE_ARRG.read_text()
    require_text(
        arrg,
        ("This is not a safe death", "npc_add(^troll_love_arrg_spawn, trollromance_arrg_attackable",
         "npc_setowner;", "[ai_timer,trollromance_arrg_attackable]",
         "~playerhit_n_melee_bypass_prayer(^slash_style, $damage);",
         "~playerhit_n_ranged($check_prayer, $damage, $duration);",
         "multiply(~npc_ranged_attack_roll, ^troll_love_arrg_ranged_accuracy_scale)",
         "[ai_queue3,trollromance_arrg_attackable]",
         "%varp385_troll_love = ^troll_love_defeated_arrg;",
         "[proc,trollromance_arrg_drop_table](coord $where)",
         "obj_add_private($where, bones, 1", "def_int $roll = random(128);",
         "else if ($roll < 123)", "~troll_gem_drop(true)",
         "if (random(45) = 0) obj_add_private($where, arceuus_corpse_troll",
         "[timer,trollromance_arrg_monitor]", "[proc,trollromance_arrg_cleanup]",
         "[proc,trollromance_on_death]", "[proc,trollromance_on_logout]"),
        "Troll Romance owner-private Arrg, loot and lifecycle",
    )
    require("@troll_drop_table" not in arrg,
            "Troll Romance: Arrg must not use the public generic troll table")
    require_text(
        TROLLLOVE_SLED.read_text(),
        ("inv_del(inv, cake_tin, 1);", "stat(agility) < ^troll_love_slide_agility",
         "[opheld2,trollromance_toboggon_waxed]", "[timer,trollromance_sled_monitor]",
         "inv_del(worn, trollromance_toboggon_waxed, 1);",
         "[proc,trollromance_has_sled]()(boolean)", "[proc,trollromance_on_login]"),
        "Troll Romance wax, slide and sled-loss route",
    )
    require_text(
        TROLLLOVE_DUNSTAN.read_text(),
        ("~trollromance_has_sled = false", "Lost it, did you?",
         "if (%varp385_troll_love < ^troll_love_dunstan_made_sled)"),
        "Troll Romance sled replacement",
    )
    require_text(
        TROLLLOVE_UG.read_text(),
        ("inv_total(worn, trollromance_rare_flower) > 0",
         "inv_freespace(inv) < 7", "inv_add(inv, uncut_diamond, 1);",
         "inv_add(inv, uncut_ruby, 2);", "inv_add(inv, uncut_emerald, 4);",
         "stat_advance(agility, 80000);", "stat_advance(strength, 40000);"),
        "Troll Romance flower and atomic reward",
    )
    require_text(COMBAT_STATS.read_text(),
                 ("[proc,playerhit_n_melee_bypass_prayer]", "~playerhit_n_melee_apply"),
                 "Troll Romance prayer-penetrating melee funnel")
    require_text(PLAYER_DEATH.read_text(), ("~trollromance_on_death;",),
                 "Troll Romance death cleanup hook")
    require_text(PLAYER_LOGOUT.read_text(), ("~trollromance_on_logout;",),
                 "Troll Romance logout cleanup hook")
    require_text(PLAYER_LOGIN.read_text(), ("~trollromance_on_login;",),
                 "Troll Romance login sled monitor")
    require("[trollromance_arrg_attackable]" not in TROLLLOVE_GENERATED_ANIMS.read_text(),
            "Troll Romance: authored Arrg must not be duplicated by generated animation config")


def check_in_search_of_the_myreque() -> None:
    require_text(
        ROUTEQUEST_CONSTANT.read_text(),
        ("^routequest_agility_req = 25", "^routequest_pouch_charges = 5",
         "^routequest_boat_fee = 10", "^routequest_nails_per_rung = 75",
         "^routequest_mortton_boat_land", "^routequest_hollows_boat_land",
         "^routequest_chamber_outside", "^routequest_hound_lifetime = 500"),
        "In Search of the Myreque route constants",
    )
    require_text(
        ROUTEQUEST_NPC.read_text(),
        ("[skeleton_hellhound]", "hitpoints=55", "attack=70", "strength=110",
         "defence=100", "magic=1", "ranged=1", "param=attackrate,4",
         "param=damagetype,^crush_style", "param=stabdefence,0",
         "param=slashdefence,0", "param=crushdefence,0", "param=magicdefence,0",
         "param=rangedefence,0", "param=elemental_weakness,^element_earth",
         "param=elemental_weakness_percent,35", "param=death_drop,null"),
        "In Search of the Myreque exact Skeleton Hellhound row",
    )
    require("param=undead" not in ROUTEQUEST_NPC.read_text(),
            "In Search of the Myreque: Skeleton Hellhound must not be undead")
    require_text(
        ROUTEQUEST_VARP.read_text(),
        ("[varp6816_routequest_hound_active]", "[varp6817_routequest_hound_death]",
         "protect=no", "transmit=no", "scope=temp"),
        "In Search of the Myreque private encounter state",
    )
    route = ROUTEQUEST_ROUTE.read_text()
    require_text(
        route,
        ("stat(agility) < ^routequest_agility_req", "[opnpc1,route_cyreg_paddlehorn]",
         "inv_total(inv, steel_longsword) < 1", "inv_total(inv, steel_sword) < 2",
         "%varp387_routequest = ^routequest_boatman_agreed", "inv_total(inv, druid_pouch) < ^routequest_pouch_charges",
         "inv_del(inv, woodplank, ^routequest_boat_planks);",
         "[oploc1,route_rowboat_mortton]", "[oploc2,route_rowboat_mortton]",
         "inv_total(worn, ring_of_charos_unlocked) = 0", "inv_del(inv, coins, ^routequest_boat_fee);",
         "[oploc1,route_rowboat_hollows]", "[oploc1,swamp_bridge1]",
         "inv_del(inv, nails, ^routequest_nails_per_rung);", "[proc,routequest_cross_rung]",
         "[opnpc1,route_curpile_fyod]", "[proc,routequest_curpile_challenge]",
         "~routequest_boat_trip(^routequest_hollows_boat_land, swamp_boatjourney);",
         "[oploc1,freedomfighterentrancel]", "[oploc1,freedomfighterundergroundentrancel]"),
        "In Search of the Myreque Cyreg, boat, bridge and Curpile route",
    )
    hideout = ROUTEQUEST_HIDEOUT.read_text() + "\n" + ROUTEQUEST_HOUND.read_text()
    require_text(
        hideout,
        ("[proc,routequest_veliaf]", "getbit_range(%varp6203_routequest_myreque_bits",
         "inv_del(inv, steel_longsword, 1);", "inv_del(inv, steel_sword, 2);",
         "inv_del(inv, steel_mace, 1);", "inv_del(inv, steel_warhammer, 1);",
         "inv_del(inv, steel_dagger, 1);", "%varp387_routequest = ^routequest_ambush;",
         "%varb396_thsfm_vanstrom_hide = 1;", "npc_add(^routequest_hound_spawn, skeleton_hellhound",
         "npc_setowner;", "npc_setmode(opplayer2);", "[ai_queue3,skeleton_hellhound]",
         "%varp387_routequest = ^routequest_saved_myreque;", "It drops nothing",
         "[timer,routequest_hound_monitor]", "[proc,routequest_hound_cleanup]",
         "[proc,routequest_on_death]", "%varp6817_routequest_hound_death = 1;",
         "[proc,routequest_on_logout]", "[proc,routequest_on_login]",
         "inzone(^routequest_chamber_min, ^routequest_chamber_max, coord) = true",
         "[oploc1,thrttavernbasementfalsewall]", "[oploc1,thrttavernbasementladder]",
         "[oploc1,thrt_tavern_trap_door]", "[opnpc1,canafis_stranger]",
         "stat_advance(attack, 6000);", "stat_advance(defence, 6000);",
         "stat_advance(strength, 6000);", "stat_advance(hitpoints, 6000);",
         "stat_advance(crafting, 6000);"),
        "In Search of the Myreque member, Hellhound, escape and reward route",
    )
    require("obj_add" not in hideout,
            "In Search of the Myreque: Skeleton Hellhound must have no loot path")
    # The hound is a melee monster (wiki Skeleton_Hellhound oldid 15199509: crush,
    # speed 4, max hit 12): it swings through the default [ai_opplayer2,_]. Its own
    # [ai_opplayer2] that only set applayer2 (no [ai_applayer2] handler) spent every
    # swing on a mode change (seam applayer2_npcs_that_never_swing, b56).
    hound = ROUTEQUEST_HOUND.read_text()
    require("[ai_opplayer2,skeleton_hellhound]" not in hound
            and "[ai_spawn,skeleton_hellhound]" not in hound
            and "npc_setmode(applayer2);" not in hound,
            "In Search of the Myreque: the Skeleton Hellhound must not be put in the ranged applayer2 mode")
    require_text(
        ROUTEQUEST_MYREQUE2.read_text(),
        ("%varp387_routequest < ^routequest_complete", "~routequest_veliaf;",
         "~routequest_polmafi;", "~routequest_ivan;"),
        "In Search of the Myreque additive In Aid NPC dispatch",
    )
    require_text(PLAYER_DEATH.read_text(),
                 ("~routequest_on_death;", "$death_coord = ^routequest_chamber_outside;"),
                 "In Search of the Myreque outside-room grave hook")
    require_text(PLAYER_LOGOUT.read_text(), ("~routequest_on_logout;",),
                 "In Search of the Myreque logout cleanup hook")
    require_text(PLAYER_LOGIN.read_text(), ("~routequest_on_login;",),
                 "In Search of the Myreque login reconciliation hook")
    alloc = PIP_VARP_ALLOC.read_text()
    require("6816=varp6816_routequest_hound_active" in alloc and "6817=varp6817_routequest_hound_death" in alloc,
            "In Search of the Myreque: private varp allocations drifted")


def _npc_block(text: str, name: str) -> list[str]:
    """The `key=value` lines of one `[name]` block of a .npc file."""
    lines: list[str] = []
    inside = False
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("[") and line.endswith("]"):
            inside = line == f"[{name}]"
            continue
        if inside and line and not line.startswith("//"):
            lines.append(line)
    return lines


def check_contact() -> None:
    """Contact!'s Giant Scarab is a real level-191 fight (sampler b53 round 3:
    with no block it fought on npc_default.npc's 10 hp and died in 7 ticks).
    Wiki Giant_Scarab_(Contact!) oldid 15328051; summons Locust_rider oldid
    15281959 / Scarab_Mage oldid 15281960 (the Contact! versions, ids 799-801)."""
    npc = CONTACT_NPC.read_text()
    boss = _npc_block(npc, "contact_scarab_boss")
    for line in ("hitpoints=130", "attack=169", "strength=190", "defence=169",
                 "ranged=190", "huntmode=aggressive", "param=attackrate,4",
                 "param=damagetype,^stab_style", "param=strengthbonus,0",
                 "param=rangebonus,0", "param=stabdefence,70", "param=slashdefence,99",
                 "param=crushdefence,99", "param=magicdefence,159",
                 "param=rangedefence,103", "param=poison_severity,41",
                 "param=death_drop,null"):
        require(line in boss, f"Contact!: [contact_scarab_boss] lacks `{line}`")
    for name, needles in (
        ("contact_locust_bow_b", ("hitpoints=20", "attack=90", "strength=90", "defence=20",
                                  "ranged=90", "param=attackrate,6",
                                  "param=damagetype,^ranged_style", "param=rangeattack,80")),
        ("contact_locust_lance_b", ("hitpoints=20", "attack=90", "strength=90", "defence=20",
                                    "param=attackrate,4", "param=damagetype,^crush_style",
                                    "param=strengthbonus,30")),
        ("contact_insectoid_mage_b", ("hitpoints=20", "attack=90", "strength=90", "defence=10",
                                      "magic=70", "param=attackrate,15",
                                      "param=damagetype,^magic_style", "param=magic_maxhit,17")),
    ):
        block = _npc_block(npc, name)
        for line in needles:
            require(line in block, f"Contact!: [{name}] lacks `{line}`")
    scarab = CONTACT_SCARAB.read_text()
    require_text(
        scarab,
        ("[proc,contact_spawn_scarab](int $handle)",
         "npc_add($spot, contact_scarab_boss, 3000);",
         "contact_insectoid_mage_b, 3000);", "contact_locust_lance_b, 3000);",
         "contact_locust_bow_b, 3000);",
         "[opnpc2,contact_scarab_boss]", "[opnpc2,contact_insectoid_mage_b]",
         "[opnpc2,contact_locust_lance_b]", "[opnpc2,contact_locust_bow_b]",
         "[ai_opplayer2,contact_scarab_boss]", "if (npc_range(coord) <= 1) {",
         "~npc_meleeattack;", "~npc_rangeattack;", "~contact_scarab_mystic_poison;",
         "[proc,contact_scarab_mystic_poison]",
         "The vast scarab clacks its mandibles and you are mystically poisoned",
         "%varp102_poison = npc_param(poison_severity);",
         "[ai_opplayer2,contact_locust_bow_b]", "[ai_opplayer2,contact_insectoid_mage_b]",
         "~npc_generic_magicattack;",
         "[ai_queue3,contact_scarab_boss]", "obj_add($spot, contact_keris, 1, ^lootdrop_duration);",
         "%varb3274_contact = ^contact_scarab_killed;", "~npc_default_death;",
         "npc_type = contact_insectoid_mage_b | npc_type = contact_locust_lance_b | npc_type = contact_locust_bow_b"),
        "Contact! Giant Scarab fight",
    )
    for dungeon_form in ("contact_insectoid_mage, ", "contact_locust_lance, ", "contact_locust_bow, "):
        require(dungeon_form not in scarab,
                f"Contact!: the fight spawns the dungeon form `{dungeon_form.strip(', ')}`, not the Contact! summon")
    require_text(
        CONTACT_DUNGEON.read_text(),
        ("[proc,contact_enter_private_chasm]", "~map_instance_from_square(^contact_chasm_template);",
         "~contact_spawn_scarab($handle);"),
        "Contact! owner-private chasm",
    )


def check_anothersliceofham() -> None:
    """Another Slice of H.A.M.'s tower pair and Sigmund are real fights (with no
    block they fought on npc_default.npc's 10 hp). Wiki H.A.M._Mage oldid
    15199436, H.A.M._Archer oldid 15199383, Sigmund oldid 15285834 (Another
    Slice of H.A.M. version, level 64)."""
    npc = SLICE_NPC.read_text()
    for name, needles in (
        ("slice_ham_mage", ("hitpoints=35", "defence=30", "magic=30", "huntmode=aggressive",
                            "param=attackrate,10", "param=damagetype,^magic_style",
                            "param=magicattack,8", "param=magic_maxhit,4",
                            "param=stabdefence,5", "param=slashdefence,5",
                            "param=crushdefence,0", "param=death_drop,null")),
        ("slice_ham_archer", ("hitpoints=35", "defence=30", "ranged=30", "huntmode=aggressive",
                              "param=attackrate,10", "param=damagetype,^ranged_style",
                              "param=rangeattack,0", "param=stabdefence,5",
                              "param=slashdefence,5", "param=death_drop,null")),
    ):
        block = _npc_block(npc, name)
        for line in needles:
            require(line in block, f"Another Slice of H.A.M.: [{name}] lacks `{line}`")
    for name in ("slice_sigmund_showdown", "slice_sigmund_melee", "slice_sigmund_ranged",
                 "slice_sigmund_magic", "slice_sigmund_noprayer"):
        block = _npc_block(npc, name)
        for line in ("hitpoints=70", "attack=55", "strength=55", "defence=44",
                     "huntmode=aggressive", "param=attackrate,4",
                     "param=damagetype,^crush_style", "param=magicattack,8",
                     "param=death_drop,null"):
            require(line in block, f"Another Slice of H.A.M.: [{name}] lacks `{line}`")
    require_text(
        SLICE_HAMMAGE.read_text(),
        ("[ai_opplayer2,slice_ham_mage]\n~npc_generic_magicattack;",
         "[ai_opplayer2,slice_ham_archer]\n~npc_rangeattack;"),
        "Another Slice of H.A.M. slice_hammage.rs2",
    )


def check_ascent_of_arceuus() -> None:
    """The Ascent of Arceuus: the five Tormented Souls and the Trapped Soul are
    real fights (with no block they fought on npc_default.npc's 10 hp and died in
    a tick). Wiki Tormented_Soul oldid 15329160 (ids 8512/8513), Trapped_Soul
    oldid 15215953 (id 8514)."""
    npc = ASCENT_NPC.read_text()
    for name, needles in (
        ("arcquest_ghost1", ("hitpoints=20", "attack=16", "strength=12", "defence=10",
                             "huntmode=aggressive", "param=attackrate,4",
                             "param=damagetype,^crush_style", "param=strengthbonus,0",
                             "param=death_drop,null")),
        ("arcquest_ghost2", ("hitpoints=20", "attack=16", "strength=12", "defence=10",
                             "huntmode=aggressive", "param=attackrate,4",
                             "param=damagetype,^crush_style", "param=strengthbonus,0",
                             "param=death_drop,null")),
        ("arcquest_soul", ("hitpoints=30", "attack=25", "strength=29", "defence=20",
                           "huntmode=aggressive", "param=attackrate,4",
                           "param=damagetype,^crush_style", "param=strengthbonus,0",
                           "param=death_drop,bones")),
    ):
        block = _npc_block(npc, name)
        for line in needles:
            require(line in block, f"Ascent of Arceuus: [{name}] lacks `{line}`")
    require_text(
        ASCENT_LOCS.read_text(),
        ("[opnpc2,arcquest_ghost1]", "[opnpc2,arcquest_ghost2]", "[opnpc2,arcquest_soul]",
         "@player_combat_start;", "[ai_queue3,arcquest_ghost1]", "[ai_queue3,arcquest_ghost2]",
         "[ai_queue3,arcquest_soul]", "~aoa_ghost_died;", "npc_add(^aoa_soul_coord, arcquest_soul, 500);",
         "npc_setowner;", "~map_instance_from_square(^aoa_tower_template);",
         "~aoa_spawn_tower_souls($handle);"),
        "Ascent of Arceuus fights",
    )


def check_dreammentor() -> None:
    """Dream Mentor's dream bosses are real fights (The Everlasting, The
    Untouchable, The Illusive and A Doubt spawned on npc_default.npc's 10 hp and
    died in a tick; only The Inadequacy had a block). Wiki The_Everlasting
    oldid 15199540, The_Untouchable 15199541, The_Illusive 15199542, A_Doubt
    15199544, The_Inadequacy 15292423 (cache stat1..4 = attack/defence/
    strength/hitpoints agree page for page)."""
    npc = DREAMMENTOR_NPC.read_text()
    for name, needles in (
        ("dream_everlasting", ("hitpoints=230", "attack=187", "strength=231", "defence=120",
                               "huntmode=aggressive", "param=attackrate,6",
                               "param=damagetype,^crush_style", "param=death_drop,null")),
        ("dream_untouchable", ("hitpoints=90", "attack=187", "strength=255", "defence=434",
                               "param=attackrate,6", "param=damagetype,^crush_style",
                               "param=death_drop,null")),
        ("dream_illusive", ("hitpoints=144", "attack=72", "strength=71", "defence=105",
                            "huntmode=aggressive", "param=attackrate,6",
                            "param=damagetype,^crush_style", "param=death_drop,null")),
        ("dream_razorbeast", ("hitpoints=59", "attack=72", "strength=58", "defence=85",
                              "huntmode=aggressive", "param=attackrate,6",
                              "param=strengthbonus,16", "param=death_drop,bones")),
    ):
        block = _npc_block(npc, name)
        for line in needles:
            require(line in block, f"Dream Mentor: [{name}] lacks `{line}`")
    require("huntmode=aggressive" not in _npc_block(npc, "dream_untouchable"),
            "Dream Mentor: The Untouchable is aggressive (the wiki says it is not)")
    dream = DREAMMENTOR_DREAM.read_text()
    require_text(
        dream,
        ("[opnpc2,dream_inadequacy]", "[opnpc2,dream_everlasting]", "[opnpc2,dream_untouchable]",
         "[opnpc2,dream_illusive]", "[opnpc2,dream_razorbeast]",
         "[ai_queue3,dream_inadequacy]\nif (npc_findhero = true) {\n    queue(dreammentor_boss_down, 3, 1);",
         "[ai_queue3,dream_illusive]\nif (npc_findhero = true) {\n    queue(dreammentor_boss_down, 3, 4);",
         "[queue,dreammentor_boss_down](int $stage)",
         "[ai_applayer2,dream_inadequacy]", "[ai_opplayer2,dream_inadequacy]",
         "npc_add(~dreammentor_arena_at($handle, 32, 30), dream_everlasting, 1000);",
         "npc_add(~dreammentor_arena_at($handle, 32, 30), dream_untouchable, 1000);",
         "npc_add(~dreammentor_arena_at($handle, 32, 30), dream_illusive, 1000);",
         "map_instance_from_square(^dreammentor_arena_coord)",
         "[proc,dreammentor_on_damage](int $dealt)", "npc_queue(4, 0, 4);"),
        "Dream Mentor dreammentor_dream.rs2",
    )
    # An npc script may not suspend a player: the death handlers only queue.
    for body in dream.split("[ai_queue3,")[1:]:
        head = body.split("\n\n", 1)[0]
        require("mesbox" not in head and "p_teleport" not in head and "p_delay" not in head,
                "Dream Mentor: an [ai_queue3] death handler suspends or moves the player")


def check_swansong() -> None:
    """Swan Song's Sea Troll Queen and level-79 sea trolls are real fights
    (sampler matthew-mbp-m4-b54 round 4: with no block they fought on
    npc_default.npc's 10 hp and the Queen died to one Fire Blast). Wiki
    Sea_Troll_Queen oldid 15215925, Sea_troll oldid 15329222 (Level 79, id
    4308); Swan_Song oldid 15359363 for the untimed ambush and the fixed Queen."""
    npc = SWANSONG_NPC.read_text()
    queen = _npc_block(npc, "swan_seatroll_queen")
    for line in ("hitpoints=200", "attack=100", "strength=70", "defence=100", "magic=150",
                 "ranged=1", "huntmode=aggressive", "wanderrange=0",
                 "param=attackrange,10", "param=attackrate,4",
                 "param=damagetype,^crush_style", "param=crushattack,100",
                 "param=strengthbonus,40", "param=magic_maxhit,37",
                 "param=stabdefence,20", "param=slashdefence,40", "param=crushdefence,40",
                 "param=magicdefence,40", "param=rangedefence,0",
                 "param=proj_travel,waterwave_travel", "param=death_drop,null"):
        require(line in queen, f"Swan Song: [swan_seatroll_queen] lacks `{line}`")
    troll = _npc_block(npc, "swan_troll_ambush")
    for line in ("hitpoints=100", "attack=60", "strength=60", "defence=60",
                 "huntmode=aggressive", "param=attackrate,3", "param=damagetype,^crush_style",
                 "param=strengthbonus,0", "param=death_drop,bones"):
        require(line in troll, f"Swan Song: [swan_troll_ambush] lacks `{line}`")
    require_text(
        SWANSONG_CONSTANT.read_text(),
        ("^ssq_fight_duration = 3000", "^ssq_queen_melee_maxhit = 16",
         "^ssq_queen_prayer_drain = 21", "^ssq_trolls_needed = 3"),
        "Swan Song constants",
    )
    colony = SWANSONG_COLONY.read_text()
    require_text(
        colony,
        ("if (%varb2098_swansong = ^ssq_ready_to_fight) {\n    ~ssq_spawn_entrance_ambush;",
         "[proc,ssq_spawn_entrance_ambush]",
         "if (npc_find(^ssq_entrance_ambush_coord, swan_troll_ambush, 10, 0) = true) {",
         "def_int $owed = calc(^ssq_trolls_needed - %varb2107_swansong_trolls);",
         "npc_add(^ssq_entrance_ambush_coord, swan_troll_ambush, ^ssq_fight_duration);",
         "npc_add(movecoord(^ssq_entrance_ambush_coord, 2, 0, 1), swan_troll_ambush, ^ssq_fight_duration);",
         "npc_add(movecoord(^ssq_entrance_ambush_coord, -1, 0, 2), swan_troll_ambush, ^ssq_fight_duration);",
         "[opnpc2,swan_troll_ambush]", "[ai_queue3,swan_troll_ambush]",
         "%varb2098_swansong = ^ssq_trolls_beaten;",
         "npc_add(^ssq_fish_coord, swan_troll_ambush, ^ssq_fight_duration);",
         "~ssq_queen_ensure;"),
        "Swan Song entrance ambush",
    )
    require("swan_troll_ambush, 50);" not in colony,
            "Swan Song: a sea troll is added for 50 ticks again (wiki: the ambush and the fishing troll stay until killed)")
    require("%varb2111_swansong_ambush = 0) {" not in colony,
            "Swan Song: the one-shot ambush flag gates the spawn again (a despawned ambush soft-locks stage 40)")
    finale = SWANSONG_FINALE.read_text()
    require_text(
        finale,
        ("[proc,ssq_queen_ensure]",
         "npc_add(^ssq_queen_coord, swan_seatroll_queen, ^ssq_fight_duration);",
         "[opnpc2,swan_seatroll_queen]", "[ai_opplayer2,swan_seatroll_queen]",
         "if (npc_range(coord) <= 1) {", "~ssq_queen_melee;", "~ssq_queen_water_wave;",
         "~ssq_queen_prayer_drain;", "[proc,ssq_queen_overhead_prayer]()(boolean)",
         "~playerhit_n_melee(^crush_style, randominc(^ssq_queen_melee_maxhit));",
         "~npc_generic_magicattack;", "npc_anim(swan_queen_spellcast, 0);",
         "stat_sub(prayer, min(^ssq_queen_prayer_drain, stat(prayer)), 0);",
         "[ai_queue3,swan_seatroll_queen]", "%varb2098_swansong = ^ssq_queen_dead;",
         "[debugproc,swansong_queen_hp]", "[debugproc,swansong_troll_hp]"),
        "Swan Song Sea Troll Queen",
    )
    require("swan_seatroll_queen, 100);" not in finale,
            "Swan Song: the Queen is added for 100 ticks again (a despawn soft-locks stage 170)")


def check_troubledtortugans() -> None:
    """Troubled Tortugans' two gryphons are real fights (with no block both would
    fight on npc_default.npc's 10 hp). Wiki Gryphon_(Troubled_Tortugans) oldid
    15200682, Shellbane_gryphon_(Troubled_Tortugans) oldid 15267129; the cache
    stat1..6 agree."""
    npc = TT_NPC.read_text()
    gryphon = _npc_block(npc, "tt_conch_gryphon")
    for line in ("hitpoints=110", "attack=65", "strength=75", "defence=50", "magic=50",
                 "huntmode=aggressive", "param=attackrate,5", "param=damagetype,^crush_style",
                 "param=crushdefence,40", "param=magicdefence,100", "param=death_drop,null"):
        require(line in gryphon, f"Troubled Tortugans: [tt_conch_gryphon] lacks `{line}`")
    boss = _npc_block(npc, "tt_pearl_gryphon")
    for line in ("hitpoints=400", "attack=160", "strength=210", "defence=120", "ranged=150",
                 "magic=100", "huntmode=aggressive", "param=attackrate,5", "param=rangeattack,20",
                 "param=crushdefence,40", "param=magicdefence,100", "param=death_drop,null"):
        require(line in boss, f"Troubled Tortugans: [tt_pearl_gryphon] lacks `{line}`")
    fights = TT_FIGHTS.read_text()
    require_text(
        fights,
        ("[opnpc2,tt_conch_gryphon]", "[opnpc2,tt_pearl_gryphon]", "@player_combat_start;",
         "[ai_opplayer2,tt_conch_gryphon]", "[ai_opplayer2,tt_pearl_gryphon]",
         "[ai_queue3,tt_conch_gryphon]", "[ai_queue3,tt_pearl_gryphon]",
         "npc_add(^ttq_cave_gryphon_coord, tt_conch_gryphon, ^ttq_fight_duration);",
         "npc_add(^ttq_pearl_gryphon_coord, tt_pearl_gryphon, ^ttq_fight_duration);",
         "%varb18321_tt = ^ttq_elder3;", "%varb18321_tt = ^ttq_korel2;",
         "~playerhit_n_melee_bypass_prayer", "~npc_default_death;"),
        "Troubled Tortugans gryphon fights",
    )
    require_text(TT_CORE.read_text(),
                 ("[oploc1,tt_lair_entrance]", "~ttq_spawn_cave_gryphon;", "[label,ttq_mooring_disembark]",
                  "~ttq_spawn_pearl;"),
                 "Troubled Tortugans fight entry points")


def check_twilightspromise() -> None:
    """Twilight's Promise: Mezan and the eight Teomat cultists are real fights (with
    no block they fought on npc_default.npc's 10 hp and died in a tick). Wiki
    Knight_of_Varlamore_(Twilight's_Promise) oldid 15200510, Cultist oldid 15326762,
    Twilight's_Promise oldid 15356498."""
    base = ROOT / "OSRS-Content/osrs239-content/server/scripts/quests/quest_twilightspromise"
    npc = (base / "configs/twilightspromise.npc").read_text()
    for name, needles in (
        ("vmq2_knight_6_combat", ("hitpoints=100", "attack=70", "strength=70", "defence=40",
                                  "magic=20", "param=attackrate,5", "param=damagetype,^slash_style",
                                  "param=strengthbonus,0", "param=stabdefence,28",
                                  "param=slashdefence,26", "param=crushdefence,10",
                                  "param=magicdefence,5", "param=death_drop,null")),
    ) + tuple(
        (f"vmq2_cultist_{kind}", ("hitpoints=25", "attack=30", "strength=35", "defence=30",
                                   "param=attackrate,4", "param=damagetype,^stab_style",
                                   "param=strengthbonus,0", "param=crushdefence,30",
                                   "param=death_drop,null"))
        for kind in ("m_1", "m_2", "m_3", "f_1", "f_2", "f_3")
    ):
        block = _npc_block(npc, name)
        for line in needles:
            require(line in block, f"Twilight's Promise: [{name}] lacks `{line}`")
    colosseum = (base / "scripts/twilightspromise_colosseum.rs2").read_text()
    require_text(
        colosseum,
        ("[proc,tp_mezan_start]", "~map_instance_from_square(^tp_arena_template);",
         "npc_add(map_instance_coord($handle, ^tp_arena_knight_x, ^tp_arena_knight_z, 0), vmq2_knight_6_combat",
         "[opnpc2,vmq2_knight_6_combat]", "~npc_retaliate(0);", "@player_combat_start;",
         "[proc,tp_mezan_prepare_hit]", "%tp_mezan_streak >= ^tp_mezan_streak",
         "[ai_queue3,vmq2_knight_6_combat]", "%varb9832_vmq2_colosseum_knight = 2;",
         "queue(tp_mezan_leave, 4, 0);", "~npc_default_death;"),
        "Twilight's Promise colosseum script",
    )
    funnel = (ROOT / "OSRS-Content/osrs239-content/server/scripts/skill_combat/scripts/player/player_hit_npc_prepare.rs2").read_text()
    require("~tp_mezan_prepare_hit($prepared, $style, $hit_success)" in funnel,
            "Twilight's Promise: player_hit_npc_prepare does not call ~tp_mezan_prepare_hit")
    battle = (base / "scripts/twilightspromise_battle.rs2").read_text()
    require_text(
        battle,
        ("[proc,tp_battle_start]", "~map_instance_from_square(^tp_battle_template);",
         "npc_add(map_findsquare(movecoord($landing, 5, 0, 0), 0, 2, ^map_findsquare_lineofwalk), vmq2_cultist_m_1",
         "npc_setmode(opplayer2);", "[opnpc2,vmq2_cultist_m_1]", "[opnpc2,vmq2_cultist_f_3]",
         "[ai_queue3,vmq2_cultist_m_1]", "[ai_queue3,vmq2_cultist_f_3]",
         "%varp7291_tp_battle_kills >= ^tp_cultists_total", "queue(tp_battle_won, 3, 0);",
         "[queue,tp_battle_won]", "~map_instance_release_here;", "%varb9649_vmq2 = ^tp_finish;"),
        "Twilight's Promise battle script",
    )


def check_meatandgreet() -> None:
    """Meat and Greet's Dire Wolf Alpha, its pups and the arena Minotaur are real
    fights (with no block they fought on npc_default.npc's 10 hp and died in a
    tick, as Contact!'s scarab and Swan Song's Queen did). Wiki Dire_Wolf_Alpha
    oldid 15208071, Dire_Wolf oldid 15208070 (Meat and Greet version, id 13813),
    Minotaur_(Meat_and_Greet) oldid 15200556, Meat_and_Greet oldid 15355341."""
    npc = MAG_NPC.read_text()
    for name, needles in (
        ("mag_direwolf", ("hitpoints=100", "attack=120", "strength=110", "defence=80",
                          "magic=40", "huntmode=aggressive", "forcemulti=yes",
                          "param=attackrate,4", "param=damagetype,^stab_style",
                          "param=strengthbonus,0", "param=death_drop,null")),
        ("mag_direwolf_minion", ("hitpoints=10", "attack=75", "strength=75", "defence=70",
                                 "ranged=20", "param=attackrate,4",
                                 "param=damagetype,^ranged_style", "param=rangeattack,500",
                                 "param=death_drop,null")),
        ("mag_minotaur", ("hitpoints=240", "attack=140", "strength=110", "defence=100",
                          "magic=100", "huntmode=aggressive", "param=attackrate,5",
                          "param=damagetype,^crush_style", "param=crushattack,60",
                          "param=strengthbonus,10", "param=magicattack,300",
                          "param=magic_maxhit,14", "param=stabdefence,30",
                          "param=slashdefence,20", "param=crushdefence,50",
                          "param=magicdefence,80", "param=death_drop,null")),
    ):
        block = _npc_block(npc, name)
        for line in needles:
            require(line in block, f"Meat and Greet: [{name}] lacks `{line}`")
    fights = MAG_FIGHTS.read_text()
    require_text(
        fights,
        ("[proc,mg_enter_den]", "~map_instance_from_square(^mg_den_template);",
         "npc_add(map_instance_coord($handle, ^mg_den_alpha_x, ^mg_den_alpha_z, 1), mag_direwolf",
         "[opnpc2,mag_direwolf]", "[opnpc2,mag_direwolf_minion]", "[opnpc2,mag_minotaur]",
         "[ai_opplayer2,mag_direwolf]", "~npc_meleeattack;", "npc_add(movecoord(npc_coord, random(3), 0, -1), mag_direwolf_minion",
         "[ai_opplayer2,mag_direwolf_minion]", "~npc_rangeattack;",
         "[ai_queue3,mag_direwolf]", "%varb11184_mag_meat = ^mg_meat_kill;",
         "obj_add($spot, wolf_bones, 1, ^lootdrop_duration);", "npc_del;",
         "[proc,mg_enter_arena]", "~map_instance_from_square(^mg_arena_template);",
         "npc_add($spot, mag_minotaur, 12000);", "[ai_opplayer2,mag_minotaur]",
         "npc_say(\"Moo!\");", "npc_say(\"Moooooo!\");", "~npc_generic_magicattack;",
         "npc_attackdelay($rate);", "[ai_queue3,mag_minotaur]", "%varb11182_mag = ^mg_lelia3;",
         "~npc_default_death;"),
        "Meat and Greet fights",
    )
    # The quest never hands the kebabs over by script alone: the stage writes are
    # dialogue and fight consequences.
    npcs = MAG_NPCS.read_text()
    require_text(npcs, ("[proc,mg_pin_dial]", "if_openmain(number_pad);",
                        "runclientscript*(torirs_numpad_build)", "p_countdialog_noprompt;",
                        "^mg_pin_code"),
                 "Meat and Greet pin pad")


def check_creature_of_fenkenstrain() -> None:
    require_text(
        FENK_CONSTANT.read_text(),
        ("^fenk_experiment_cave = 0_55_155_57_7",
         "^fenk_ed_grave = 0_56_54_24_35",
         "^fenk_mausoleum_torso_grave = 0_54_55_46_56",
         "^fenk_mausoleum_arms_grave = 0_54_55_48_57",
         "^fenk_mausoleum_legs_grave = 0_54_55_50_56"),
        "Creature of Fenkenstrain exact route constants",
    )
    core = FENK_CORE.read_text()
    require_text(
        core,
        ("Partial completion of The Restless Ghost", "%varp107_prieststart < ^priest_started",
         "%varp302_priestperil < ^fenk_pip_gate", "They are checked at conductor casting"),
        "Creature of Fenkenstrain requirements",
    )
    require_text(FENK_FINISH.read_text(),
                 ("stat(thieving) < ^fenk_thieve_req", "stat_advance(thieving, ^fenk_thieve_xp);"),
                 "Creature of Fenkenstrain final boostable Thieving gate")
    parts = FENK_PARTS.read_text()
    require_text(
        parts,
        ("[oploc1,fenk_coffin]", "p_teleport(^fenk_experiment_cave);",
         "[opnpc2,fenk_experiment_1]", "[apnpc2,fenk_experiment_1]",
         "[proc,fenk_can_attack_key_experiment]()(boolean)",
         "%varb199_fenk_unlocked_cavern = 1 | inv_total(inv, fenk_mausoleum_key) > 0",
         "You don't have the heart to kill the poor creature again.",
         "[ai_queue3,fenk_experiment_1]",
         "obj_add_private(npc_coord, fenk_mausoleum_key, 1, ^lootdrop_duration, 100);",
         "~npc_default_death;", "[oploc1,fenk_mausoleum_door]",
         "inv_del(inv, fenk_mausoleum_key, 1);", "%varb199_fenk_unlocked_cavern = 1;",
         "[oploc1,fenk_chest_open]", "inv_total(inv, fenk_mausoleum_key) > 0",
         "inv_freespace(inv) < 1", "You take a key out of the chest.",
         "loc_coord = ^fenk_ed_grave", "loc_coord = ^fenk_mausoleum_torso_grave",
         "loc_coord = ^fenk_mausoleum_arms_grave", "loc_coord = ^fenk_mausoleum_legs_grave"),
        "Creature of Fenkenstrain cave, key and body-part route",
    )
    require("npc_add(^fenk_experiment_cave" not in parts,
            "Creature of Fenkenstrain: memorial must not duplicate a static Experiment")
    generic_drop = FENK_EXPERIMENT_DROP.read_text()
    require_text(
        generic_drop,
        ("[ai_queue3,fenk_experiment_2]", "[ai_queue3,fenk_experiment_3]",
         "~npc_default_death;", "conditional 1/4 Experiment bone remains deferred"),
        "Creature of Fenkenstrain level-25 ordinary drops",
    )
    require("fenk_mausoleum_key" not in generic_drop and "rag_experiment_bone" not in generic_drop,
            "Creature of Fenkenstrain: level-25 Experiments must not leak quest keys or deferred bones")
    require_text(
        FENK_GENERATED_COMBAT.read_text(),
        ("[fenk_experiment_1]", "hitpoints=40", "attack=40", "strength=50",
         "defence=50", "magic=1", "ranged=1", "respawnrate=8",
         "param=attackrate,4", "param=damagetype,0", "param=stabattack,0",
         "param=stabdefence,0", "param=slashdefence,0", "param=crushdefence,0",
         "param=magicdefence,0", "param=rangedefence,0", "param=death_drop,bones"),
        "Creature of Fenkenstrain level-51 Experiment combat row",
    )
    require_text(
        FENK_LIGHTNING.read_text(),
        ("[oploc1,fenk_shed_door]", "inv_total(inv, fenk_shed_key) < 1",
         "%varb200_fenk_unlocked_shed = 1;", "inv_add(inv, fenk_cane, 1);",
         "inv_total(inv, bronzecraftwire) < 1", "inv_del(inv, bronzecraftwire, 1);",
         "if (loc_coord ! 1_55_55_24_35)", "[proc,fenk_try_cast_conductor]()(boolean)",
         "stat(crafting) < ^fenk_craft_req", "inv_del(inv, silver_bar, 1);",
         "stat_advance(crafting, 500);"),
        "Creature of Fenkenstrain shed, brush and conductor route",
    )
    require_text(
        FENK_SMELTING.read_text(),
        ("case silver_bar :", "if (~fenk_try_cast_conductor = true)", "@craft_silver;"),
        "Creature of Fenkenstrain real-furnace integration",
    )


def check_roving_elves() -> None:
    require_text(
        ROVING_CONSTANT.read_text(),
        (
            "^rovingelves_islwyn_coord = 0_35_49_51_11",
            "^rovingelves_eluned_coord = 0_35_49_49_9",
            "^rovingelves_chalice_coord = 0_40_154_43_54",
            "^rovingelves_bow_price = 900000",
            "^rovingelves_shield_price = 750000",
            "^rovingelves_reward_charges = 500",
            "^rovingelves_full_charges = 2500",
        ),
        "Roving Elves exact route, reward and charge constants",
    )
    require_text(
        CATEGORY_PACK.read_text(),
        (
            "46=trail_clue_easy", "47=trail_clue_hard",
            "48=trail_clue_hard_challenge", "50=trail_clue_hard_puzzle",
            "51=trail_clue_medium", "52=trail_clue_medium_challenge",
            "180=trail_clue_medium_key", "750=trail_clue_elite_step",
            "959=trail_clue_master_puzzle",
        ),
        "Roving Elves tomb clue-category coverage",
    )
    waterfall = ROVING_WATERFALL_LOCS.read_text()
    require_text(
        waterfall,
        (
            "[proc,waterfall_tomb_item_forbidden]",
            "case magic_runes, firemaking_logs, arrowheads",
            "weapon_pickaxe", "trail_clue_master_puzzle",
            "case arrow_shaft, feather, headless_arrow, bow_string, knife, needle",
            "if ($obj = bh_rune_pouch | $obj = goblin_rpg)",
            "case monkrobetop, monkrobebottom",
            "[proc,waterfall_tomb_forbidden_loadout]",
            "while ($slot < inv_size(inv))", "while ($slot < inv_size(worn))",
            "[proc,waterfall_can_enter_glarials_tomb]",
            "[oplocu,glarials_tombstone_waterfall_quest]",
            "if (%varp65_waterfall_quest = ^waterfall_complete | inv_total(worn, glarials_amulet_waterfall_quest) > 0 | inv_total(inv, glarials_amulet_waterfall_quest) > 0)",
            "[oploc1,baxtorian_crate_waterfall_quest]",
            "inv_add(inv, baxtorian_key_waterfall_quest, 1);",
        ),
        "Roving Elves Glarial tomb and post-Waterfall route",
    )
    generated = ROVING_GENERATED_COMBAT.read_text()
    moss_row = generated.split("[roving_mossgiant]", 1)[1].split("\n[", 1)[0]
    require_text(
        moss_row,
        (
            "hitpoints=120", "attack=60", "strength=60", "defence=60",
            "magic=1", "ranged=1", "respawnrate=10", "huntmode=aggressive",
            "param=attackrate,6", "param=damagetype,2",
            "param=crushattack,66", "param=strengthbonus,62",
            "param=stabdefence,0", "param=slashdefence,0",
            "param=crushdefence,0", "param=magicdefence,0",
            "param=rangedefence,0", "param=death_drop,big_bones",
        ),
        "Roving Elves Moss Guardian combat row",
    )
    moss = ROVING_MOSS.read_text()
    require_text(
        moss,
        (
            "[opnpc2,roving_mossgiant]", "[apnpc2,roving_mossgiant]",
            "~waterfall_tomb_forbidden_loadout()",
            "[ai_opplayer2,roving_mossgiant]",
            "def_int $attack_roll = ~npc_melee_attack_roll(^crush_style);",
            "~player_defence_roll(^crush_style)",
            "~player_defence_roll(^magic_style)",
            "spotanim_pl(roving_mossgiant_impact, 92, 0);",
            "~playerhit_n_melee_bypass_prayer(^magic_style",
            "npc_attackdelay(6);", "[ai_queue3,roving_mossgiant]",
            "npc_findhero", "p_finduid(uid)",
            "obj_add_private(npc_coord, roving_old_consecration_seed, 1",
            "%varp6262_rovingelves_quest = ^rovingelves_obtained_old_seed;",
            "if (random(400) = 0)", "dorgesh_construction_bone",
            "if (random(10025) < 2)", "dorgesh_construction_bone_curved",
            "~npc_default_death;",
        ),
        "Roving Elves credited Guardian combat and loot",
    )
    require("@wiki_moss_giant_drop" not in moss and "@moss_giant_drop" not in moss,
            "Roving Elves: Moss Guardian must not use a generic moss-giant table")
    eluned = ROVING_ELUNED.read_text()
    require_text(
        eluned,
        (
            "[proc,rovingelves_eluned_login]", "%varb9050_roving_female_woodelf = 2;",
            "[opnpc1,roving_female_woodelf]", "[opnpc1,eluned_prif]",
            "inv_del(inv, roving_old_consecration_seed, 1);",
            "inv_add(inv, roving_new_consecration_seed, 1);",
            "if (inv_total(inv, roving_new_consecration_seed) = 0)",
            "if (inv_freespace(inv) < 1)",
        ),
        "Roving Elves Eluned transform, enchantment and recovery",
    )
    require_text(
        ROVING_SEED.read_text(),
        (
            "[opheld1,roving_new_consecration_seed]",
            "if (inv_total(inv, spade) < 1)",
            "inzone(^rovingelves_chalice_zone_min, ^rovingelves_chalice_zone_max, coord)",
            "loc_find(^rovingelves_chalice_coord, baxtorian_chalice_waterfall_quest)",
            "inv_del(inv, roving_new_consecration_seed, 1);",
            "%varp6262_rovingelves_quest = ^rovingelves_seed_planted;",
            "loc_add(^rovingelves_chalice_coord, roving_crystal_growth, 0, centrepiece_straight, 20);",
        ),
        "Roving Elves spade, Chalice and growth ritual",
    )
    islwyn = ROVING_ISLWYN.read_text()
    require_text(
        islwyn,
        (
            "[proc,rovingelves_islwyn_login]", "%varb9051_roving_bowyer = 2;",
            "if (%varp328_regicide_quest ! ^regicide_complete | %varp65_waterfall_quest ! ^waterfall_complete)",
            "~p_choice3(\"Shields are for wimps! Give me the bow!\"",
            "inv_add(inv, crystal_bow, 1);",
            "inv_add(inv, crystal_shield, 1);",
            "~crystal_set_charges(inv, crystal_bow, ^rovingelves_reward_charges);",
            "~crystal_set_charges(inv, crystal_shield, ^rovingelves_reward_charges);",
            "[opnpc3,roving_bowyer]", "[opnpc3,roving_islwyn_2ops]",
            "def_int $price = ^rovingelves_bow_price;",
            "$price = ^rovingelves_shield_price;",
            "~crystal_set_charges(inv, $item, ^rovingelves_full_charges);",
        ),
        "Roving Elves crystal reward and fixed-price purchases",
    )
    require("npc_add(" not in islwyn and "npc_add(" not in eluned,
            "Roving Elves: cache-authored camp NPCs must not be duplicated")
    require_text(
        ROVING_LEVEL_REQUIRE.read_text(),
        (
            "case crystal_bow, crystal_bow_2500, crystal_shield, crystal_shield_2500",
            "roving_crystal_bow_new", "roving_crystal_shield_new",
            "if (%varp6262_rovingelves_quest < ^rovingelves_complete)",
        ),
        "Roving Elves crystal equipment completion gate",
    )


def check_ghosts_ahoy() -> None:
    require_text(
        AHOY_CONSTANT.read_text(),
        (
            "^ahoy_req_agility = 25", "^ahoy_req_cooking = 20",
            "^ahoy_captain_chest_coord = 1_56_55_35_25",
            "^ahoy_rock_chest_coord = 0_56_55_22_44",
            "^ahoy_lobster_chest_coord = 0_56_55_34_22",
            "^ahoy_lobster_loot_coord = 0_56_55_35_22",
            "^ahoy_lobster_spawn_coord = 0_56_55_33_22",
            "^ahoy_lobster_lifetime = 200",
            "^ahoy_lobster_head_rate = 25",
            "^ahoy_lobster_clue_rate = 128",
            "^ahoy_dig_book_coord = 0_59_55_27_10",
            "^ahoy_boat_fare = 25",
        ),
        "Ghosts Ahoy exact wreck, lobster and captain constants",
    )
    require_text(
        AHOY_VARP.read_text(),
        (
            "[varp6827_ahoy_lobster_active]", "scope=temp",
            "[varp6828_ahoy_flag_top]", "[varp6829_ahoy_flag_bottom]", "[varp6830_ahoy_flag_skull]",
            "[varp6831_ahoy_ship_top]", "[varp6832_ahoy_ship_bottom]", "[varp6833_ahoy_ship_skull]",
            "[varp6834_ahoy_dragontooth_pass]", "[varp6835_ahoy_captain_chest_unlocked]",
        ),
        "Ghosts Ahoy private persistent puzzle state",
    )
    require_text(
        AHOY_NPC.read_text(),
        (
            "[giant_lobster]", "hitpoints=32", "attack=18", "strength=26",
            "defence=40", "magic=1", "ranged=1", "huntmode=aggressive",
            "param=attackrate,4", "param=damagetype,^stab_style",
            "param=stabattack,0", "param=strengthbonus,0",
            "param=stabdefence,5", "param=slashdefence,15",
            "param=crushdefence,5", "param=magicdefence,0",
            "param=rangedefence,5", "param=death_drop,null",
        ),
        "Ghosts Ahoy exact Giant Lobster combat row",
    )
    require_text(
        AHOY_HUB.read_text(),
        (
            "[proc,ahoy_boil_nettle_tea]()(boolean)",
            "if (stat(cooking) < ^ahoy_req_cooking)",
            "slayerguide_slayer_gloves", "ranger_gloves",
            "magictraining_infinitygloves", "lunar_moonclan_gloves",
            "granite_gloves", "lunar_gloves", "beekeeper_gloves",
            "obj_add_private(coord, ahoy_bone_key, 1",
            "inv_add(inv, ashes, 1);",
            "~ahoy_give(ahoy_toy_boat);",
        ),
        "Ghosts Ahoy prerequisites, tea and unique-item recovery",
    )
    book = AHOY_BOOK.read_text()
    require_text(
        book,
        (
            "[proc,ahoy_flag_init]", "add(1, random(6))",
            "[proc,ahoy_dye_colour](obj $dye)(int)",
            "case reddye", "case bluedye", "case yellowdye",
            "case orangedye", "case greendye", "case purpledye",
            "if_openoverlay(ahoy_windspeed)", "[timer,ahoy_wind]",
            "%varp6831_ahoy_ship_top ! %varp6828_ahoy_flag_top",
            "if (loc_coord = ^ahoy_rock_chest_coord)",
            "if (loc_coord ! ^ahoy_lobster_chest_coord)",
            "if (loc_coord ! ^ahoy_lobster_loot_coord)",
            "healenergy(-500);", "stat_random(agility, 120, 260)",
            "stat_advance(agility, 100);", "p_teleport(loc_coord);",
        ),
        "Ghosts Ahoy per-player flag, three-chest and rock contract",
    )
    require_text(
        book,
        (
            "npc_add(^ahoy_lobster_spawn_coord, giant_lobster, ^ahoy_lobster_lifetime);",
            "npc_setowner;", "npc_setmode(opplayer2);", "hint_npc;",
            "[opnpc2,giant_lobster]", "[apnpc2,giant_lobster]",
            "[ai_queue3,giant_lobster]", "npc_findhero", "p_finduid(uid)",
            "%varb215_ahoy_killed_lobster = 1;",
            "obj_add_private($drop, seaweed, 1",
            "random(^ahoy_lobster_head_rate)", "arceuus_corpse_scorpion",
            "random(^ahoy_lobster_clue_rate)", "trail_clue_beginner",
            "[timer,ahoy_lobster_monitor]", "~ahoy_cleanup_lobster;",
            "[proc,ahoy_on_death]", "[proc,ahoy_on_logout]",
        ),
        "Ghosts Ahoy owner-private timed lobster and complete loot",
    )
    # The lobster is a melee monster (wiki Giant_lobster_(Ghosts_Ahoy) oldid 15272821:
    # Stab, speed 4, max hit 4): it retaliates through the default [ai_queue1,_]
    # opplayer2 rung. An applayer2 override with no [ai_applayer2] handler made it
    # hit once and never again (seam ghostsahoy_giant_lobster_never_swings, b56).
    require("[ai_queue1,giant_lobster]" not in book and "[ai_spawn,giant_lobster]" not in book
            and "npc_setmode(applayer2);" not in book,
            "Ghosts Ahoy: the giant lobster must not be put in the ranged applayer2 mode")
    # The Kendal is a melee monster (wiki The_Kendal oldid 15199460: crush, max hit
    # 9): spawned in opplayer2 and retaliating through the default [ai_queue1,_].
    # The applayer2 overrides with no [ai_applayer2] handler kept it from ever
    # swinging (seam applayer2_npcs_that_never_swing, b56).
    kendal = MDQ_KENDAL.read_text()
    require_text(
        kendal,
        ("npc_add(^mdq_kendal_coord, mdaughter_bearman_fighter, ^mdq_kendal_lifetime);",
         "npc_setowner;", "npc_setmode(opplayer2);"),
        "Mountain Daughter private Kendal spawn",
    )
    require("[ai_queue1,mdaughter_bearman_fighter]" not in kendal
            and "[ai_spawn,mdaughter_bearman_fighter]" not in kendal
            and "npc_setmode(applayer2);" not in kendal,
            "Mountain Daughter: the Kendal must not be put in the ranged applayer2 mode")
    require("@wiki_lobster_drop" not in book and "@lobster_drop" not in book,
            "Ghosts Ahoy: quest lobster must not leak a generic lobster table")
    require_text(
        book,
        (
            "inv_total(inv, ahoy_map_scrap_1) < 1",
            "inv_total(inv, ahoy_map_scrap_2) < 1",
            "inv_total(inv, ahoy_map_scrap_3) < 1",
            "inv_add(inv, ahoy_map_complete, 1);",
            "inv_total(worn, ring_of_charos_unlocked) > 0", "$fare = 10;",
            "inv_total(inv, ectotoken) < 500", "%varp6834_ahoy_dragontooth_pass = 1;",
            "distance(coord, ^ahoy_dragontooth_dock_coord) <= 24",
            "coord = ^ahoy_dig_book_coord", "inv_add(inv, ahoy_book_of_haricanto, 1);",
        ),
        "Ghosts Ahoy map, captain fare and exact dig route",
    )
    require_text(
        AHOY_MANUAL.read_text(),
        ("I've lost the bow you signed for Ak-Haranu.",
         "inv_total(bank, oak_longbow_signed) < 1",
         "inv_add(inv, oak_longbow_signed, 1);"),
        "Ghosts Ahoy signed-bow recovery",
    )
    require_text(
        AHOY_ROBES.read_text(),
        ("I can replace the bedsheet when you have a free inventory slot.",
         "I can replace the petition when you have a free inventory slot.",
         "obj_add_private(coord, ahoy_robes_of_necrovarus, 1",
         "inv_total(bank, ahoy_robes_of_necrovarus) >= 1"),
        "Ghosts Ahoy bedsheet, petition and robes recovery",
    )
    require_text(
        AHOY_SHARED.read_text(),
        ("%varp107_prieststart < ^priest_complete", "%varp302_priestperil < ^priestperil_complete",
         "%varp302_priestperil = ^priestperil_complete;", "%varp6828_ahoy_flag_top = 0;",
         "%varp6834_ahoy_dragontooth_pass = 0;", "%varp6835_ahoy_captain_chest_unlocked = 0;"),
        "Ghosts Ahoy deterministic reset coverage",
    )
    require_text(AHOY_COOKING.read_text(), ("if (last_useitem = bowl_nettlewater)", "~ahoy_boil_nettle_tea;"),
                 "Ghosts Ahoy shared cooking-fire integration")
    require(AHOY_DYES.read_text().count("case ahoy_toy_boat_repaired :") == 6,
            "Ghosts Ahoy: all six dye triggers must reach the repaired ship")
    require_text(PLAYER_DEATH.read_text(), ("~ahoy_on_death;",),
                 "Ghosts Ahoy death cleanup")
    require_text(PLAYER_LOGOUT.read_text(), ("~ahoy_on_logout;",),
                 "Ghosts Ahoy logout cleanup")


def check_one_small_favour() -> None:
    require_text(
        OSF_CONSTANT.read_text(),
        (
            "^osf_slagilith_fight = 145",
            "^osf_slagilith_defeated = 150",
            "^osf_petra_freed = 152",
            "^osf_gang_dwarf_1_bit = 0",
            "^osf_gang_dwarf_2_bit = 1",
            "^osf_gang_dwarf_3_bit = 2",
            "^osf_gang_complete_mask = 7",
            "^osf_slagilith_lifetime = 500",
            "^osf_slagilith_coord = 0_40_153_57_45",
        ),
        "One Small Favour exact encounter constants",
    )
    require_text(
        OSF_VARP.read_text(),
        ("[varp6868_osf_gang_kills]", "[varp6869_osf_slagilith_active]", "scope=temp"),
        "One Small Favour owner and dwarf state",
    )
    require(OSF_VARP.read_text().count("scope=temp") == 2,
            "One Small Favour: both encounter latches must reset on logout")
    require_text(
        OSF_NPC.read_text(),
        (
            "[slagilith]", "hitpoints=60", "attack=60", "strength=120",
            "defence=75", "magic=1", "ranged=1",
            "huntmode=aggressive_melee", "param=attackrate,6",
            "param=damagetype,2", "param=crushattack,10",
            "param=strengthbonus,0", "param=stabdefence,50",
            "param=slashdefence,50", "param=crushdefence,5",
            "param=magicdefence,5", "param=rangedefence,50",
            "param=combat_xp_multiplier,4000", "param=death_drop,null",
        ),
        "One Small Favour exact Slagilith combat row",
    )
    puzzles = OSF_PUZZLES.read_text()
    require_text(
        puzzles,
        (
            "[oplocu,favour_lady_in_wall]",
            "if (last_useitem ! favour_animate_rock)",
            "%varp416_onesmallfavour = ^osf_slagilith_fight;",
            "~osf_spawn_slagilith;",
            "if (%varp6869_osf_slagilith_active = 1)",
            "if (%varp416_onesmallfavour = ^osf_slagilith_defeated)",
            "npc_add(^osf_slagilith_coord, favour_petra, ^osf_petra_lifetime);",
            "npc_setowner;",
            "%varp416_onesmallfavour = ^osf_petra_freed;",
        ),
        "One Small Favour retained-scroll cast, retry and Petra release",
    )
    require("inv_del(inv, favour_animate_rock" not in puzzles,
            "One Small Favour: animate-rock scroll must survive both casts")
    relay = OSF_RELAY.read_text()
    require_text(
        relay,
        (
            "%varp6868_osf_gang_kills = 0;",
            "[opnpc2,favour_gangster_dwarf]",
            "[opnpc2,favour_gangster_dwarf_2]",
            "[opnpc2,favour_gangster_dwarf_3]",
            "[apnpc2,favour_gangster_dwarf]",
            "[apnpc2,favour_gangster_dwarf_2]",
            "[apnpc2,favour_gangster_dwarf_3]",
            "[proc,osf_record_gang_kill](int $bit)",
            "%varp6868_osf_gang_kills = setbit(%varp6868_osf_gang_kills, $bit);",
            "if (%varp6868_osf_gang_kills = ^osf_gang_complete_mask)",
            "~osf_record_gang_kill(^osf_gang_dwarf_1_bit);",
            "~osf_record_gang_kill(^osf_gang_dwarf_2_bit);",
            "~osf_record_gang_kill(^osf_gang_dwarf_3_bit);",
            "%varp416_onesmallfavour = ^osf_hammerspike_done;",
        ),
        "One Small Favour distinct all-three dwarf credit",
    )
    require_text(
        OSF_DWARF_SPAWN.read_text(),
        ("favour_gangster_dwarf", "favour_gangster_dwarf_2",
         "favour_gangster_dwarf_3"),
        "One Small Favour cache-authored dwarf gang spawns",
    )
    generated = OSF_GENERATED_NPC.read_text()
    for npc, row in (
        ("favour_gangster_dwarf", ("hitpoints=40", "attack=40", "strength=40", "defence=35")),
        ("favour_gangster_dwarf_2", ("hitpoints=25", "attack=80", "strength=30", "defence=25")),
        ("favour_gangster_dwarf_3", ("hitpoints=25", "attack=30", "strength=60", "defence=57")),
    ):
        start = generated.index(f"[{npc}]")
        block = generated[start:generated.find("\n[", start + 1)]
        require_text(block, row + ("param=attackrate,5", "param=death_drop,bones"),
                     f"One Small Favour {npc} exact row")
    require_text(
        relay,
        (
            "npc_add(^osf_slagilith_coord, slagilith, ^osf_slagilith_lifetime);",
            "npc_setowner;", "npc_setmode(opplayer2);", "hint_npc;",
            "[opnpc2,slagilith]", "[apnpc2,slagilith]",
            "[ai_queue3,slagilith]", "npc_findhero", "p_finduid(uid)",
            "%varp416_onesmallfavour = ^osf_slagilith_defeated;",
            "obj_add_private($drop, adamantite_ore, 1",
            "obj_add_private($drop, uncut_ruby, 1",
            "obj_add_private($drop, uncut_diamond, 1",
            "[timer,osf_slagilith_monitor]", "~osf_cleanup_slagilith;",
            "[proc,osf_on_death]", "[proc,osf_on_logout]",
            "[proc,osf_slagilith_prepare_hit]",
            "oc_category(inv_getobj(worn, ^wearpos_rhand)) = weapon_pickaxe",
            "return(divide($rolled_damage, 3));",
        ),
        "One Small Favour private Slagilith lifecycle, weakness and loot",
    )
    # The Slagilith is a melee monster (wiki Slagilith oldid 15258101: crush, max
    # hit 13): it retaliates through the default [ai_queue1,_] opplayer2 rung. An
    # applayer2 override with no [ai_applayer2] handler kept it from ever swinging
    # (seam applayer2_npcs_that_never_swing, b56).
    require("[ai_queue1,slagilith]" not in relay and "[ai_spawn,slagilith]" not in relay
            and "npc_setmode(applayer2);" not in relay,
            "One Small Favour: the Slagilith must not be put in the ranged applayer2 mode")
    require_text(
        PLAYER_HIT_FUNNEL.read_text(),
        (
            "def_boolean $slagilith = false;",
            "$slagilith_xp_damage = $prepared;",
            "$prepared = ~osf_slagilith_prepare_hit($prepared, $hit_success);",
            "$xp_damage = min($slagilith_xp_damage, npc_stat(hitpoints));",
        ),
        "One Small Favour shared pre-reduction XP funnel",
    )
    require_text(PLAYER_DEATH.read_text(), ("~osf_on_death;",),
                 "One Small Favour death cleanup")
    require_text(PLAYER_LOGOUT.read_text(), ("~osf_on_logout;",),
                 "One Small Favour logout cleanup")

# ---------------------------------------------------------------------------
# Repo-wide sweeps (seam20, 2026-09-27).
#
# Everything above checks named encounters, so a quest nobody had written a
# check for yet could not fail it: parity2b found the giant lobster, The
# Kendal, Slagilith and the Hammerspike gang by driving them, and the Giant Sea
# Snake was still sitting there. These two sweeps walk the whole pack instead.
# ---------------------------------------------------------------------------

SCRIPT_HEADER = re.compile(r"^\[([a-z0-9_]+),([^\]]+)\]")
SCRIPT_CALL = re.compile(r"([~@])([a-z0-9_]+)")

# Reaching any of these IS the player's half of an attack. The first two are
# the wildcard's own jumps (LostCity player_combat.rs2:1-2); the other three
# are the style entry points [label,player_combat_start] dispatches to, which
# LostCity's lathastrainingogre binding (combat_training_camp.rs2) calls
# directly. They are terminals: the sweep does not descend into them, since
# they call ~npc_retaliate themselves on every hit.
COMBAT_START_TERMINALS = frozenset((
    "player_combat_start", "player_combat_start_ap",
    "player_melee_attack", "player_ranged_attack", "player_magic_attack",
))

# A binding that retaliates without starting the fight ON PURPOSE, each with the
# reason. Nothing else may: the default for an Attack binding is to attack.
COMBAT_START_EXEMPT = {
    # Another Slice of H.A.M.: Sigmund's protection prayer turns every style
    # away (the label switches his prayer and says so) until the ancient mace
    # special changes him to slice_sigmund_noprayer, whose binding attacks.
    ("opnpc2", "slice_sigmund_showdown"): "prayer blocks every style until the mace special",
    ("opnpc2", "slice_sigmund_melee"): "prayer blocks every style until the mace special",
    ("opnpc2", "slice_sigmund_ranged"): "prayer blocks every style until the mace special",
    ("opnpc2", "slice_sigmund_magic"): "prayer blocks every style until the mace special",
    # ... and their AP halves (seam21), or a bow or staff skips the prayer.
    ("apnpc2", "slice_sigmund_showdown"): "prayer blocks every style until the mace special",
    ("apnpc2", "slice_sigmund_melee"): "prayer blocks every style until the mace special",
    ("apnpc2", "slice_sigmund_ranged"): "prayer blocks every style until the mace special",
    ("apnpc2", "slice_sigmund_magic"): "prayer blocks every style until the mace special",
}


def _strip_comment(line: str) -> str:
    return line.split("//", 1)[0]


def load_script_blocks() -> dict[tuple[str, str], list[tuple[str, int, str]]]:
    """Every `[trigger,name]` script in the pack's source, keyed by both halves."""
    blocks: dict[tuple[str, str], list[tuple[str, int, str]]] = {}
    for path in sorted(CONTENT.rglob("*.rs2")):
        relative = path.relative_to(CONTENT)
        if relative.parts and relative.parts[0].startswith("build"):
            continue
        current: list | None = None
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            match = SCRIPT_HEADER.match(line)
            if match:
                name = match.group(2).split("(", 1)[0]
                current = [str(relative), number, [_strip_comment(line[match.end():])]]
                blocks.setdefault((match.group(1), name), []).append(current)
            elif current is not None:
                current[2].append(_strip_comment(line))
    return {key: [(f, n, "\n".join(body)) for f, n, body in value]
            for key, value in blocks.items()}


def opnpc2_combat_start_sweep() -> tuple[list[tuple], list[tuple], int]:
    """(misses, exempt, handler count) over every name-specific op/apnpc2."""
    blocks = load_script_blocks()
    memo: dict[tuple[str, str], tuple[bool, bool]] = {}

    def reach(kind: str, name: str, stack: frozenset) -> tuple[bool, bool]:
        # (reaches a combat-start terminal, reaches ~npc_retaliate)
        if name in COMBAT_START_TERMINALS:
            return True, False
        if kind == "~" and name == "npc_retaliate":
            return False, True
        key = (kind, name)
        if key in memo:
            return memo[key]
        if key in stack:
            return False, False
        trigger = "proc" if kind == "~" else "label"
        result = (False, False)
        for _, _, body in blocks.get((trigger, name), []):
            got = scan(body, stack | {key})
            result = (result[0] or got[0], result[1] or got[1])
        memo[key] = result
        return result

    def scan(text: str, stack: frozenset) -> tuple[bool, bool]:
        starts = retaliates = False
        for kind, name in SCRIPT_CALL.findall(text):
            got = reach(kind, name, stack)
            starts |= got[0]
            retaliates |= got[1]
        return starts, retaliates

    misses = []
    exempt = []
    count = 0
    for (trigger, name), found in sorted(blocks.items()):
        if trigger not in ("opnpc2", "apnpc2") or name == "_":
            continue
        for path, line, body in found:
            count += 1
            starts, retaliates = scan(body, frozenset())
            # A binding that hands the Attack back with `p_opnpc(2)` re-queues
            # ITSELF (the same [opnpc2,<npc>] fires again) unless it changed
            # what the npc is first (`npc_changetype`: Sire's sleeping ->
            # awake, which the new type's own binding then serves). Seam pass
            # 30: the three battle mages ended both halves in `p_opnpc(2)`, no
            # swing ever landed ('hp no bar -> no bar'), and this sweep passed
            # them because they never call ~npc_retaliate either.
            reenters = (re.search(r"\bp_opnpc\(\s*2\s*\)", body) is not None
                        and "npc_changetype" not in body)
            if (retaliates or reenters) and not starts:
                row = (trigger, name, path, line)
                if (trigger, name) in COMBAT_START_EXEMPT:
                    exempt.append(row + (COMBAT_START_EXEMPT[(trigger, name)],))
                else:
                    misses.append(row)
    return misses, exempt, count


def check_opnpc2_combat_start() -> None:
    """QUEST_AUTHORING.md trap 31, over the whole pack.

    A name-specific `[opnpc2,<npc>]`/`[apnpc2,<npc>]` replaces the wildcard
    `[opnpc2,_] @player_combat_start;` / `[apnpc2,_] @player_combat_start_ap;`
    (skill_combat/combat.rs2; LostCity player_combat.rs2:1-2) entirely. A
    binding that reaches `~npc_retaliate` (the NPC's half: fight back) but no
    combat start (the PLAYER's half) is an Attack that never swings -- no
    hitsplat, no health bar, a boss nobody can kill. Every LostCity binding
    that attacks ends in the jump (grandtree_black_demon.rs2, irvig_senay.rs2,
    tut_giant_rat.rs2).
    """
    misses, _, _ = opnpc2_combat_start_sweep()
    require(not misses,
            "trap 31: [op/apnpc2] bindings reach ~npc_retaliate but never "
            "@player_combat_start/_ap (add the jump, or an exemption with its "
            "reason):\n  " + "\n  ".join(f"[{t},{n}] {p}:{l}" for t, n, p, l in misses))


# ---------------------------------------------------------------------------
# The AP twin (seam21, 2026-09-27).
#
# `[apnpc2,_] @player_combat_start_ap;` (skill_combat/combat.rs2; LostCity
# player_combat.rs2:2) claims every Attack made from range: a bow, a staff or a
# reach weapon fires from `apRange` tiles away and the `[opnpc2,<npc>]` binding
# never runs. So an op binding that REFUSES -- "Doomion is no longer hostile
# towards you", "Deal with my guards first!", Sigmund's protection prayer -- is
# only a melee gate unless the npc has its own `[apnpc2,<npc>]` carrying the
# same gate. LostCity pairs every gated attack binding that way:
# grandtree_black_demon.rs2:1-13, game_trail/hard/zamorak_wizard.rs2:1-13 (the
# same `if` in both, then @player_combat_start / @player_combat_start_ap) and
# druidspirit/ghast.rs2:47-60. A melee weapon never takes the AP path (its
# `weapon_attackrange` is 0, so player_combat_start_ap hands it to the op), so
# a gate that only refuses melee is not bypassed -- those are the exemptions.
# ---------------------------------------------------------------------------

SCRIPT_IF = re.compile(r"\bif\s*\((.*?)\)\s*\{", re.S)

# Gated op bindings whose AP half is the wildcard ON PURPOSE, each with its reason.
APNPC2_TWIN_EXEMPT = {
    "lathastrainingogre": "melee-only refusal; ranged is the path the gate lets "
                          "through, and LostCity combat_training_camp.rs2:62 has "
                          "no [apnpc2] twin either",
    "monkey": "melee-only refusal (Tai Bwo Wannai Trio: 'Adjacent melee attacks "
              "are dodged ... ranged, Magic, and reach weapons can kill one', "
              "tbwt_monkey.rs2:1-3); the wildcard AP half is the intended kill",
}


def _label_closure(blocks, text: str) -> str:
    """`text` plus the body of every @label it reaches (not procs: a proc's
    `return` is its own), stopping at the combat-start terminals."""
    parts = [text]
    seen: set[str] = set()
    pending = [text]
    while pending:
        for kind, name in SCRIPT_CALL.findall(pending.pop()):
            if kind != "@" or name in seen or name in COMBAT_START_TERMINALS:
                continue
            seen.add(name)
            for _, _, body in blocks.get(("label", name), []):
                parts.append(body)
                pending.append(body)
    return "\n".join(parts)


def _gate_conditions(text: str) -> list[str]:
    return [" ".join(cond.split()) for cond in SCRIPT_IF.findall(text)]


def apnpc2_twin_sweep() -> tuple[list[tuple], list[tuple], int]:
    """(misses, exempt, gated count): every GATED name-specific [opnpc2,X] --
    an attack binding (it reaches a combat start or ~npc_retaliate) that can
    refuse (a `return` in it or a label it jumps to, or no combat start at all)
    -- needs an [apnpc2,X] whose own text (labels followed) carries every one
    of its `if (...)` conditions."""
    blocks = load_script_blocks()
    memo: dict[tuple[str, str], tuple[bool, bool]] = {}

    def reach(kind: str, name: str, stack: frozenset) -> tuple[bool, bool]:
        if name in COMBAT_START_TERMINALS:
            return True, False
        if kind == "~" and name == "npc_retaliate":
            return False, True
        key = (kind, name)
        if key in memo:
            return memo[key]
        if key in stack:
            return False, False
        trigger = "proc" if kind == "~" else "label"
        result = (False, False)
        for _, _, body in blocks.get((trigger, name), []):
            got = scan(body, stack | {key})
            result = (result[0] or got[0], result[1] or got[1])
        memo[key] = result
        return result

    def scan(text: str, stack: frozenset) -> tuple[bool, bool]:
        starts = retaliates = False
        for kind, name in SCRIPT_CALL.findall(text):
            got = reach(kind, name, stack)
            starts |= got[0]
            retaliates |= got[1]
        return starts, retaliates

    misses = []
    exempt = []
    gated = 0
    for (trigger, name), found in sorted(blocks.items()):
        if trigger != "opnpc2" or name == "_":
            continue
        for path, line, body in found:
            starts, retaliates = scan(body, frozenset())
            if not (starts or retaliates):
                continue  # op 2 is not Attack here (Bank, Talk-to, ...)
            closure = _label_closure(blocks, body)
            if starts and not re.search(r"\breturn\b", closure):
                continue  # always attacks: the wildcard AP half is the same fight
            gated += 1
            row = (name, path, line)
            if name in APNPC2_TWIN_EXEMPT:
                exempt.append(row + (APNPC2_TWIN_EXEMPT[name],))
                continue
            twins = blocks.get(("apnpc2", name))
            if not twins:
                misses.append(row + ("no [apnpc2] twin",))
                continue
            twin_text = "\n".join(_label_closure(blocks, b) for _, _, b in twins)
            twin_conditions = set(_gate_conditions(twin_text))
            lost = [c for c in _gate_conditions(closure) if c not in twin_conditions]
            if lost:
                misses.append(row + ("[apnpc2] twin lacks the gate: if (" +
                                     ") / if (".join(lost) + ")",))
    return misses, exempt, gated


def check_apnpc2_twins() -> None:
    """QUEST_AUTHORING.md trap 31, the AP half: a gated [opnpc2,X] needs a
    gated [apnpc2,X], or a ranged/magic attacker skips the gate."""
    misses, _, _ = apnpc2_twin_sweep()
    require(not misses,
            "trap 31 (AP half): gated [opnpc2] bindings a ranged or magic attack "
            "bypasses through the wildcard [apnpc2,_] (add an [apnpc2] twin with "
            "the same gate, or an APNPC2_TWIN_EXEMPT entry with its reason):\n  " +
            "\n  ".join(f"[opnpc2,{n}] {p}:{l}: {why}" for n, p, l, why in misses))


# The inventory's `varp` column names an AUXILIARY varp for these rows, not the
# one the quest's completion is written to. The replacement is the var the
# quest assigns its own `^<const_complete>` to (or, for a tier 1 row, the varp
# its test's `quest.bind` reads).
# needs: tools/quest_gate/quest_inventory.tsv -- correct these rows at source.
PROGRESS_VARP_OVERRIDES = {
    "quest_chompybird": ("varp293_chompybird",),                    # was chompybird_kills
    "quest_cog": ("varp10_cogquest",),                             # was cog_bits; cog.lua binds cogquest
    "quest_crest": ("varp148_crestquest",),                         # was crest_spells_levers_gauntlets
    "quest_desertrescue": ("varp197_desertrescue",),                # was desertrescue_map_mechanisms
    "quest_eadgar": ("varp335_eadgar_quest",),                      # was eadgar_bits; eadgar.lua binds it
    "quest_elemental_workshop": ("varb2067_elemental_workshop_finished",),  # elemental_workshop.lua binds it
    "quest_horror": ("varb34_horrorquest",),                       # was horror_boss_active
    "quest_onesmallfavour": ("varp416_onesmallfavour",),            # was osf_gang_kills (temp by design)
    "quest_routequest": ("varp387_routequest",),                    # was routequest_myreque_bits
    "quest_totem": ("varp200_totemquest",),                         # was handelmort_traps_disabled
    "quest_zombiequeen": ("varp116_zombiequeen",),                  # was zq_map_mechanisms
}
QUEST_INVENTORY = ROOT / "tools/quest_gate/quest_inventory.tsv"
ALL_VARP = ROOT / "OSRS-Content/osrs239-content/configs/all.varp"
ALL_VARBIT = ROOT / "OSRS-Content/osrs239-content/configs/all.varbit"
VARP_ALLOC = ROOT / "OSRS-Content/osrs239-content/pack/varp.alloc"


def load_content_varp_defs() -> dict[str, dict]:
    """What load_varp_config (torirs_server_content.c) ends up answering.

    walk_configs visits server/scripts in sorted name order and
    ToriRSServer_ContentVarp returns the FIRST def for an id, so a later
    duplicate section is ignored here too. A varp no .varp file declares has
    no def at all: never transmitted, never saved.
    """
    defs: dict[str, dict] = {}

    def walk(directory: Path) -> None:
        for name in sorted(os.listdir(directory)):
            if name.startswith("."):
                continue
            path = directory / name
            if path.is_dir():
                walk(path)
            elif name.endswith(".varp"):
                current = None
                for raw in path.read_text(errors="replace").splitlines():
                    line = _strip_comment(raw).strip()
                    if not line:
                        continue
                    header = re.match(r"^\[([^\]]+)\]$", line)
                    if header:
                        symbol = header.group(1)
                        current = None
                        if symbol not in defs:
                            current = defs[symbol] = {
                                "file": str(path.relative_to(CONTENT)),
                                "transmit": False, "perm": False}
                        continue
                    if current is None or "=" not in line:
                        continue
                    key, value = (part.strip() for part in line.split("=", 1))
                    if key == "transmit":
                        current["transmit"] = value == "yes"
                    elif key == "scope":
                        current["perm"] = value == "perm"

    walk(CONTENT)
    return defs


def quest_progress_varp_sweep() -> tuple[list[tuple], int]:
    """(misses, vars checked): each quest's progress varp must transmit and persist."""
    basevar: dict[str, str] = {}
    current = None
    for raw in ALL_VARBIT.read_text(errors="replace").splitlines():
        line = raw.strip()
        header = re.match(r"^\[([^\]]+)\]$", line)
        if header:
            current = header.group(1)
        elif line.startswith("basevar=") and current:
            basevar[current] = line.split("=", 1)[1].strip()
    known = set(re.findall(r"^\[([^\]]+)\]", ALL_VARP.read_text(errors="replace"), re.M))
    known |= set(re.findall(r"^\d+=([a-z0-9_]+)", VARP_ALLOC.read_text(errors="replace"), re.M))
    defs = load_content_varp_defs()
    misses = []
    checked = 0
    with QUEST_INVENTORY.open() as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            quest = row["dir"]
            names = PROGRESS_VARP_OVERRIDES.get(quest) or tuple(
                re.findall(r"%([a-z0-9_]+)", row["varp"]))
            require(bool(names), f"{quest}: quest_inventory.tsv names no progress varp")
            for name in names:
                carrier = basevar.get(name, name)
                require(carrier in known,
                        f"{quest}: progress var %{name} is neither a varbit nor a varp")
                checked += 1
                found = defs.get(carrier)
                if not found or not found["transmit"] or not found["perm"]:
                    via = f" (varbit on [{carrier}])" if carrier != name else ""
                    state = ("undeclared in server/scripts/**/*.varp" if not found else
                             f"{found['file']}: transmit={'yes' if found['transmit'] else 'no'}"
                             f" scope={'perm' if found['perm'] else 'temp'}")
                    misses.append((quest, name, via, state))
    return misses, checked


def check_theeyesofglouphrie() -> None:
    """The Eyes of Glouphrie's six Evil Creatures are real one-hit fights (with
    no block they spawned on npc_default.npc's 10 hp). Wiki Evil_Creature oldid
    15349482: 1 hitpoint, 1/1/1/1/1, every bonus 0, crush, speed 4, max hit 1,
    not aggressive, Bones always. Both the multi-npc wrapper the quest spawns
    (eyeglo_fluffie_N) and its attackable leaf (eyeglo_fluffie_evil_N) carry it.

    They FIGHT BACK (seam matthew-mbp-m4-b55-seam1): the same infobox's max hit
    1 and "Attacking" sound, and the cache's eyeglo_fluffie_attack seq. Max hit
    1 is strength=1 + strengthbonus 0 through [proc,npc_melee_maxhit]; a
    `retaliate=no`, or any strength / strengthbonus that rolls above 1, is the
    regression this refuses."""
    npc = EYEGLO_NPC.read_text()
    for number in range(1, 7):
        for name in (f"eyeglo_fluffie_{number}", f"eyeglo_fluffie_evil_{number}"):
            block = _npc_block(npc, name)
            for line in ("hitpoints=1", "attack=1", "strength=1", "defence=1", "magic=1",
                         "ranged=1", "param=attackrate,4", "param=damagetype,^crush_style",
                         "param=crushattack,0", "param=strengthbonus,0", "param=stabdefence,0",
                         "param=slashdefence,0", "param=crushdefence,0",
                         "param=magicdefence,0", "param=rangedefence,0",
                         "param=death_drop,bones"):
                require(line in block, f"The Eyes of Glouphrie: [{name}] lacks `{line}`")
            require("huntmode=aggressive" not in block,
                    f"The Eyes of Glouphrie: [{name}] is aggressive; the wiki says No")
            require("retaliate=no" not in block,
                    f"The Eyes of Glouphrie: [{name}] says retaliate=no; wiki Evil_Creature "
                    "oldid 15349482 gives max hit 1 and an attack sound -- they fight back")
            strengths = [line for line in block if line.startswith("strength=")]
            require(strengths == ["strength=1"],
                    f"The Eyes of Glouphrie: [{name}] strength lines {strengths}; max hit must be 1")
    anims = (CONTENT / "npc/configs/npc_anims.generated.npc").read_text()
    for number in range(1, 7):
        block = _npc_block(anims, f"eyeglo_fluffie_{number}")
        require("param=attack_anim,eyeglo_fluffie_attack" in block,
                f"The Eyes of Glouphrie: [eyeglo_fluffie_{number}] lost its cache swing "
                "eyeglo_fluffie_attack (npc_anims.generated.npc)")
    quest = EYEGLO_QUEST.read_text()
    needles = []
    for number in range(1, 7):
        needles += [f"[opnpc2,eyeglo_fluffie_{number}]", f"[ai_queue3,eyeglo_fluffie_{number}]"]
    needles += ["@player_combat_start;", "~eyeglo_check_creatures_done;", "~npc_default_death;",
                "%varb2497_eyeglo_quest = ^eyeglo_creatures_defeated;",
                "[proc,eyeglo_reveal_creatures]"]
    require_text(quest, tuple(needles), "The Eyes of Glouphrie kill chain")


def check_taleoftherighteous() -> None:
    """Tale of the Righteous' Corrupt Lizardman is a real fight (with no block it
    fought on npc_default.npc's 10 hp). Wiki Corrupt_Lizardman oldid 15200061
    (id 8000): combat 46, 50 hitpoints, 38/38/38, ranged 38, attack bonus 22,
    strength bonus 20, ranged 22/20, defence stab -10 / slash 25, crush, attack
    speed 4, max hit 6, aggressive, poisonous, Bones and 3 Xerician fabric always;
    the cache record agrees (stat1..5 38/38/38/50/38). It is met in the player's
    own copy of m18_155, and its death writes stage 10 from the npc's own queue."""
    base = CONTENT / "quests/quest_taleoftherighteous"
    block = _npc_block((base / "configs/taleoftherighteous.npc").read_text(),
                       "shayzienquest_lizardman_boss")
    for line in ("hitpoints=50", "attack=38", "strength=38", "defence=38", "magic=1",
                 "ranged=38", "huntmode=aggressive", "param=huntrange,5",
                 "param=attackrate,4", "param=damagetype,^crush_style",
                 "param=crushattack,22", "param=strengthbonus,20",
                 "param=rangeattack,22", "param=rangebonus,20",
                 "param=stabdefence,-10", "param=slashdefence,25",
                 "param=crushdefence,0", "param=magicdefence,0", "param=rangedefence,0",
                 "param=poison_severity,16", "param=death_drop,bones"):
        require(line in block, f"Tale of the Righteous: [shayzienquest_lizardman_boss] lacks `{line}`")
    require_text(
        (base / "scripts/tor_cave.rs2").read_text(),
        ("[opnpc2,shayzienquest_lizardman_boss]\n@player_combat_start;",
         "[apnpc2,shayzienquest_lizardman_boss]\n@player_combat_start_ap;",
         "[ai_queue3,shayzienquest_lizardman_boss]",
         "%varb6358_shayzienquest = ^tor_altar;",
         "obj_add(npc_coord, xeric_fabric, 3, ^lootdrop_duration);",
         "~npc_default_death;",
         "npc_add(~tor_cave_tile($handle, 1172, 9949), shayzienquest_lizardman_boss, 3000);",
         "~map_instance_from_square(^tor_cave_template)"),
        "Tale of the Righteous cave fight",
    )
    archive = (base / "scripts/tor_archive.rs2").read_text()
    require_text(
        archive,
        ("[opnpc1,shayzienquest_puzzle_piece]", "[opnpc2,shayzienquest_puzzle_piece]",
         "[proc,tor_device_cast](int $spell, dbrow $spell_data)",
         "~map_instance_from_square(^tor_prison_template)"),
        "Tale of the Righteous strange device",
    )
    require("~pvm_spell_fail" not in archive.split("[proc,tor_device_cast]")[1].split("[proc,tor_device_hit]")[0],
            "Tale of the Righteous: the device's cast calls ~pvm_spell_fail, which retaliates and walks it at the player")
    require("~tor_device_cast($spell, $spell_data);" in
            (CONTENT / "skill_combat/scripts/player/player_magic.rs2").read_text(),
            "Tale of the Righteous: ~pvm_default_spell no longer hands the Strange Device its spells")


def check_quest_progress_varps() -> None:
    """A quest's progress varp (or its varbit's carrier) is transmit=yes scope=perm.

    ToriRSServer_WorldMarkVarp sends only `transmit=yes` varps and
    torirs_server_save.c saves only `scope=perm` ones, and an undeclared varp is
    neither -- the quest list, the journal and `t.var.server` read a confident
    0 and every logout resets the quest (One Small Favour, parity2b).
    QUEST_AUTHORING.md: declare the carrier protect=no / transmit=yes /
    scope=perm in the quest's own configs/*.varp; LostCity declares every
    quest progress varp scope=perm (quest_cook.varp `[cookquest]`).
    """
    misses, _ = quest_progress_varp_sweep()
    require(not misses,
            "quest progress varps must be transmit=yes scope=perm:\n  " +
            "\n  ".join(f"{q}: %{n}{via}: {state}" for q, n, via, state in misses))


def print_sweeps() -> int:
    misses, exempt, count = opnpc2_combat_start_sweep()
    print(f"trap 31 sweep: {count} name-specific [opnpc2]/[apnpc2] bindings, "
          f"{len(misses)} miss(es), {len(exempt)} exempt")
    for trigger, name, path, line in misses:
        print(f"  MISS [{trigger},{name}] {path}:{line}")
    for trigger, name, path, line, reason in exempt:
        print(f"  exempt [{trigger},{name}] {path}:{line} -- {reason}")
    twin_misses, twin_exempt, gated = apnpc2_twin_sweep()
    print(f"apnpc2 twin sweep: {gated} gated [opnpc2] attack bindings, "
          f"{len(twin_misses)} miss(es), {len(twin_exempt)} exempt")
    for name, path, line, why in twin_misses:
        print(f"  MISS [opnpc2,{name}] {path}:{line} -- {why}")
    for name, path, line, reason in twin_exempt:
        print(f"  exempt [opnpc2,{name}] {path}:{line} -- {reason}")
    varp_misses, checked = quest_progress_varp_sweep()
    print(f"progress varp sweep: {checked} progress var(s), {len(varp_misses)} miss(es)")
    for quest, name, via, state in varp_misses:
        print(f"  MISS {quest}: %{name}{via}: {state}")
    return 1 if misses or twin_misses or varp_misses else 0


def main() -> int:
    if "--sweeps" in sys.argv[1:]:
        return print_sweeps()
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
        check_legends_quest()
        check_big_chompy()
        check_elemental_workshops()
        check_nature_spirit()
        check_priest_in_peril()
        check_regicide()
        check_tai_bwo_wannai_trio()
        check_troll_stronghold()
        check_shades_of_mortton()
        check_fremennik_trials()
        check_horror_from_the_deep()
        check_monkey_madness_i()
        check_haunted_mine()
        check_troll_romance()
        check_in_search_of_the_myreque()
        check_creature_of_fenkenstrain()
        check_roving_elves()
        check_ghosts_ahoy()
        check_one_small_favour()
        check_contact()
        check_swansong()
        check_troubledtortugans()
        check_ascent_of_arceuus()
        check_anothersliceofham()
        check_meatandgreet()
        check_twilightspromise()
        check_theeyesofglouphrie()
        check_dreammentor()
        check_taleoftherighteous()
        check_opnpc2_combat_start()
        check_apnpc2_twins()
        check_quest_progress_varps()
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"quest combat contract: {error}", file=sys.stderr)
        return 1
    print("quest combat contract: 145-unit ledger, ownership runtime, Delrith, Witch's experiment, Fight Arena, Hazeel Cult, The Grand Tree, Underground Pass, Observatory Quest, The Tourist Trap, Watchtower, Legends' Quest, Big Chompy Bird Hunting, Elemental Workshops I/II, Nature Spirit, Priest in Peril, Regicide, Tai Bwo Wannai Trio, Troll Stronghold, Shades of Mort'ton, The Fremennik Trials, Horror from the Deep, Monkey Madness I, Haunted Mine, Troll Romance, In Search of the Myreque, Creature of Fenkenstrain, Roving Elves, Ghosts Ahoy, One Small Favour, Contact!, Swan Song, Troubled Tortugans, The Ascent of Arceuus, Another Slice of H.A.M., Meat and Greet, Twilight's Promise, The Eyes of Glouphrie, Dream Mentor and Tale of the Righteous, plus the repo-wide trap 31 [opnpc2]/[apnpc2] combat-start sweep, the gated-[opnpc2] [apnpc2]-twin sweep and the quest progress-varp transmit/perm sweep (ok)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
