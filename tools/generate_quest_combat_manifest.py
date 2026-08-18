#!/usr/bin/env python3
"""Generate and validate the pinned quest-combat implementation ledger.

The human-readable catalogue lives in docs/bosses/quest_bosses.md.  This tool
turns its two authoritative inventory tables into a deterministic JSON file
that build tooling can consume without contacting the Wiki.  The roster count
and digest are pinned deliberately: changing, removing, or renaming a unit is
an explicit source-audit event, not a silent regeneration.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLAN = ROOT / "docs/bosses/quest_bosses.md"
OUT = ROOT / "docs/bosses/quest_combat_manifest.json"

CATALOGUE_URL = (
    "https://oldschool.runescape.wiki/w/Quests/Requirements_by_quest"
    "?oldid=15281241"
)
CATALOGUE_REVISION = 15281241
EXPECTED_QUESTS = 133
EXPECTED_MINIQUESTS = 12
# sha256 of kind, name, URL, and normalized encounter text for all 145 rows.
# Updating this is part of intentionally accepting a newly audited Wiki roster.
EXPECTED_ROSTER_SHA256 = "c22efd71a9f7814128d072ba8d7fc90eee541d91aa48ade6c9a7dd3414a3db15"

POST_REVISION_239 = {
    "Death on the Isle",
    "The Blood Moon Rises",
    "The Final Dawn",
    "Prying Times",
    "Troubled Tortugans",
    "The Red Reef",
    "Shadows of Custodia",
    "Scrambled!",
    "Learning the Ropes",
    "The Ides of Milk",
    "Fallen From Grace",
}

AUDITED_OVERRIDES: dict[str, dict[str, object]] = {
    "Demon Slayer": {
        "source_audits": [
            {
                "url": "https://oldschool.runescape.wiki/w/Demon_Slayer?oldid=15291214",
                "revision": 15291214,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Delrith?oldid=15216579",
                "revision": 15216579,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Demon_Slayer/Quick_guide?oldid=15109448",
                "revision": 15109448,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Transcript:Demon_Slayer?oldid=15263169",
                "revision": 15263169,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Silverlight?oldid=15286297",
                "revision": 15286297,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Dark_wizard?oldid=15289844",
                "revision": 15289844,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Stone_table_(Delrith)?oldid=15201851",
                "revision": 15201851,
                "retrieved": "2026-08-17",
            },
        ],
        "npc_gamevals": [
            "delrith",
            "delrith_weakened",
            "qip_ds_dark_wizard_denath",
            "qip_ds_young_dark_wizard1",
            "qip_ds_young_dark_wizard2",
            "qip_ds_young_dark_wizard3",
            "qip_ds_young_dark_wizard4",
        ],
        "item_gamevals": ["silverlight"],
        "loc_gamevals": ["qip_ds_stone_table"],
        "trigger_handlers": [
            "zone:0_50_52_24_32",
            "zone:0_50_52_24_40",
            "opnpc2:delrith",
            "apnpc2:delrith",
            "ai_queue2:delrith",
            "ai_queue3:delrith",
            "opnpc1:delrith_weakened",
        ],
        "loot_contract": "No ordinary or bones drop; successful banishment grants quest completion only.",
        "test_ids": [
            "quest-combat-contract:delrith",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The summoning ritual is narrated; full NPC/camera/audio choreography is pending.",
            "A real-client concurrent-player, death, and relog smoke is still pending.",
        ],
    },
    "Witch's House": {
        "source_audits": [
            {
                "url": "https://oldschool.runescape.wiki/w/Witch%27s_House?oldid=15168391",
                "revision": 15168391,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Witch%27s_House/Quick_guide?oldid=15291737",
                "revision": 15291737,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Witch%27s_experiment?oldid=15206938",
                "revision": 15206938,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Transcript:Witch%27s_House?oldid=15263233",
                "revision": 15263233,
                "retrieved": "2026-08-17",
            },
        ],
        "npc_gamevals": [
            "nora_t_hagg",
            "shapeshifterglob",
            "shapeshifterspider",
            "shapeshifterbear",
            "shapeshifterwolf",
            "witchrat",
        ],
        "item_gamevals": [
            "witches_doorkey",
            "witches_shedkey",
            "magnet",
            "cheese",
            "leather_gloves",
            "ball",
        ],
        "loc_gamevals": [
            "witchpot",
            "witchhousedoor",
            "magnetcbshut",
            "magnetcbopen",
            "witchmousehole",
            "witchbackdoor",
            "witchsheddoor",
            "witchfountain",
        ],
        "trigger_handlers": [
            "oplocu:witchsheddoor",
            "opobj3:ball",
            "ai_queue3:shapeshifterglob",
            "ai_queue3:shapeshifterspider",
            "ai_queue3:shapeshifterbear",
            "ai_queue3:shapeshifterwolf",
            "ai_timer:nora_t_hagg",
        ],
        "loot_contract": "All four forms have explicit null death drops; the ball is a gated post-fight ground objective, not combat loot.",
        "test_ids": ["quest-combat-contract:witches-experiment"],
        "known_gaps": [
            "The shed's live 'someone is inside' admission refusal is not implemented.",
            "The shed-specific Dwarf multicannon restriction awaits the shared cannon placement gate.",
            "A real-client concurrent-player, flee, death, and relog smoke is still pending.",
        ],
    },
    "Fight Arena": {
        "source_audits": [
            {
                "url": "https://oldschool.runescape.wiki/w/Fight_Arena?oldid=15240956",
                "revision": 15240956,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Fight_Arena/Quick_guide?oldid=14886724",
                "revision": 14886724,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Transcript:Fight_Arena?oldid=15263253",
                "revision": 15263253,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Khazard_Ogre?oldid=15199670",
                "revision": 15199670,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Khazard_Scorpion?oldid=15199669",
                "revision": 15199669,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Bouncer?oldid=15199444",
                "revision": 15199444,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/General_Khazard?oldid=15216084",
                "revision": 15216084,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Khazard_armour?oldid=15262779",
                "revision": 15262779,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Khazard_cell_keys?oldid=15185558",
                "revision": 15185558,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Khali_brew?oldid=15184623",
                "revision": 15184623,
                "retrieved": "2026-08-17",
            },
        ],
        "npc_gamevals": [
            "lady_servil",
            "arena_guard2",
            "sammy_servil",
            "sammy_servil_arena",
            "justin_servil",
            "hengrad",
            "arena_ogre",
            "arena_scorpion",
            "arena_bouncer",
            "general_khazard_arena",
        ],
        "item_gamevals": [
            "khazard_helmet",
            "khazard_platemail",
            "khazard_cellkeys",
            "khali_brew",
            "coins",
            "dorgesh_construction_bone",
            "dorgesh_construction_bone_curved",
            "vile_ashes",
        ],
        "loc_gamevals": [
            "arena_guard_chest_shut",
            "arena_guard_chest_open",
            "arena_prisondoor",
            "arena_jeremydoor",
            "fightarena_door1",
            "fightarena_door2",
        ],
        "trigger_handlers": [
            "oploc1:arena_guard_chest_shut",
            "oploc1:arena_guard_chest_open",
            "oplocu:arena_jeremydoor",
            "oploc1:fightarena_door2",
            "oploc2:fightarena_door2",
            "ai_queue3:arena_ogre",
            "ai_queue3:arena_scorpion",
            "ai_queue3:arena_bouncer",
            "ai_queue3:general_khazard_arena",
            "opnpc1:lady_servil",
        ],
        "loot_contract": "Khazard Ogre has only independent 1/400 long-bone and 1/5013 curved-bone tertiary rolls; the scorpion and General have no drops; Bouncer drops one vile ashes. Quest completion grants 1,000 coins, 12,175 Attack XP, 2,175 Thieving XP and retains the Khazard armour set.",
        "test_ids": [
            "quest-combat-contract:fight-arena",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The jail and opponent-release transitions use authored dialogue and teleports; full camera, guard-walk and temporary-door choreography remains pending.",
            "The arena-specific Dwarf multicannon placement refusal awaits the shared cannon placement gate.",
            "A real-client disguise, concurrent-player, escape, death and relog smoke is still pending.",
        ],
    },
    "Hazeel Cult": {
        "source_audits": [
            {
                "url": "https://oldschool.runescape.wiki/w/Hazeel_Cult?oldid=15285220",
                "revision": 15285220,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Hazeel_Cult/Quick_guide?oldid=15289620",
                "revision": 15289620,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Transcript:Hazeel_Cult?oldid=15263255",
                "revision": 15263255,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Alomone?oldid=15199483",
                "revision": 15199483,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Carnillean_armour?oldid=15182934",
                "revision": 15182934,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Hazeel_scroll?oldid=15187022",
                "revision": 15187022,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Chest_key_(Hazeel_Cult)?oldid=15186925",
                "revision": 15186925,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Hazeel%27s_mark?oldid=15216785",
                "revision": 15216785,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Poison_(item)?oldid=15186539",
                "revision": 15186539,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Sewer_valve?oldid=14449688",
                "revision": 14449688,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Raft_(Hazeel_Cult)?oldid=15019276",
                "revision": 15019276,
                "retrieved": "2026-08-17",
            },
        ],
        "npc_gamevals": [
            "sir_ceril_carnillean",
            "guard_carnillean",
            "butler_jones_hazeel_cultist",
            "clivet_hazeel_cultist_vis",
            "alomone_hazeel_cultist_1op",
            "alomone_hazeel_cultist_2op",
            "hazeel_cultist",
            "hazeel",
        ],
        "item_gamevals": [
            "poison",
            "mark_of_hazeel",
            "carnillean_armour",
            "carnilleanchestkey",
            "hazeel_scroll",
            "bones",
            "coins",
        ],
        "loc_gamevals": [
            "hazeelcultcave",
            "hazeelcultstairs",
            "hazeelsewerraft",
            "sewervalve1",
            "sewervalve2",
            "sewervalve3",
            "sewervalve4",
            "sewervalve5",
            "hazeel_chest_closed",
            "carnilleanrange",
            "hazeelcbshut",
            "hazeelcbopen",
            "carnilleanbookcase_knock",
            "carnilleanshutchest",
            "carnilleanopenchest",
            "carnilleancrate",
            "hazeelcoffin",
        ],
        "trigger_handlers": [
            "zone:0_40_151_48_0",
            "zone:0_40_151_48_8",
            "opnpc1:alomone_hazeel_cultist_2op",
            "ai_queue3:alomone_hazeel_cultist_2op",
            "oploc1:hazeel_chest_closed",
            "oploc1:hazeelsewerraft",
            "oploc1:sewervalve1..5",
            "oplocu:carnilleanrange",
            "oploc1:hazeelcbopen",
            "oploc1:carnilleanopenchest",
        ],
        "loot_contract": "Ceril route only: owner-private Alomone has the current exact level-13 stats and always drops one bones; Carnillean armour is searched from the nearby chest only after his death and remains drop-trickable until hand-in. The Hazeel route uses a non-attackable Alomone, poison, Hazeel's mark, chest key and Hazeel scroll, and can never receive kill credit or armour. Both endings grant 1,500 Thieving XP and 2,000 coins; Ceril's fake ending additionally grants 5 coins.",
        "test_ids": [
            "quest-combat-contract:hazeel-cult",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The resurrection and Butler-arrest scenes retain the ported dialogue/teleports; complete camera, movement, animation and audio choreography remains pending.",
            "A real-client two-route, drop-trick, concurrent-player, death and relog smoke is still pending.",
        ],
    },
    "The Grand Tree": {
        "source_audits": [
            {
                "url": "https://oldschool.runescape.wiki/w/The_Grand_Tree?oldid=15225321",
                "revision": 15225321,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/The_Grand_Tree/Quick_guide?oldid=15142936",
                "revision": 15142936,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Transcript:The_Grand_Tree?oldid=15303564",
                "revision": 15303564,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Foreman?oldid=15199793",
                "revision": 15199793,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Black_demon_(The_Grand_Tree)?oldid=15200279",
                "revision": 15200279,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Elemental_weakness?oldid=14966425",
                "revision": 14966425,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Bark_sample?oldid=15185187",
                "revision": 15185187,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Translation_book?oldid=15282283",
                "revision": 15282283,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Hazelmere%27s_scroll?oldid=15185554",
                "revision": 15185554,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Lumber_order?oldid=15185555",
                "revision": 15185555,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Glough%27s_key?oldid=15185557",
                "revision": 15185557,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Glough%27s_journal?oldid=15282348",
                "revision": 15282348,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Invasion_plans?oldid=15185556",
                "revision": 15185556,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Twig?oldid=15199781",
                "revision": 15199781,
                "retrieved": "2026-08-17",
            },
            {
                "url": "https://oldschool.runescape.wiki/w/Daconia_rock?oldid=15184741",
                "revision": 15184741,
                "retrieved": "2026-08-17",
            },
        ],
        "npc_gamevals": [
            "grandtree_narnode",
            "grandtree_hazelmere",
            "grandtree_glough",
            "grandtree_charlie",
            "grandtree_foreman",
            "grandtree_shipyardguard",
            "shipyardworker1",
            "shipyardworker2",
            "grandtree_anita",
            "grandtree_femi",
            "gnomekingsguard",
            "grandtree_blackdemon",
        ],
        "item_gamevals": [
            "grandtree_barksample",
            "grandtree_translationbook",
            "grandtree_scroll",
            "grandtree_journal",
            "grandtree_order",
            "grandtree_gloughskey",
            "grandtree_invasionplans",
            "grandtree_twigt",
            "grandtree_twigu",
            "grandtree_twigz",
            "grandtree_twigo",
            "grandtree_daconiarock",
            "bones",
            "coins",
        ],
        "loc_gamevals": [
            "grandtree_fencegate_l",
            "grandtree_fencegate_r",
            "grandtree_cupboardclosed",
            "grandtree_cupboardopen",
            "grandtree_chestclosed",
            "grandtree_climbtree",
            "grandtree_pillart",
            "grandtree_pillaru",
            "grandtree_pillarz",
            "grandtree_pillaro",
            "grandtree_trapdoorclosed",
            "largeroot_gnome",
            "largeroot2_gnome",
            "grandtree_rootdoor",
        ],
        "trigger_handlers": [
            "opnpc1:grandtree_foreman",
            "ai_queue3:grandtree_foreman",
            "oploc1:grandtree_fencegate_l/grandtree_fencegate_r",
            "oploc2:grandtree_cupboardopen",
            "oplocu:grandtree_chestclosed",
            "oplocu:grandtree_pillart..grandtree_pillaro",
            "oploc1:grandtree_trapdoorclosed",
            "ai_queue3:grandtree_blackdemon",
            "oploc1:largeroot_gnome/largeroot2_gnome",
            "opnpc1:grandtree_narnode",
        ],
        "loot_contract": "The optional level-23 Foreman always drops one bones and, only for an eligible player lacking it, the lumber order; the three-answer dialogue route grants the same order without combat. The owner-private level-172 quest Black demon has no ordinary, ashes or bones drop. Its death only advances the owner to Narnode's post-fight dialogue; one randomly selected persistent root supplies a recoverable Daconia rock. Final hand-in grants 18,400 Attack XP, 7,900 Agility XP, 2,150 Magic XP, five quest points and the documented travel/mine unlocks.",
        "test_ids": [
            "quest-combat-contract:grand-tree",
            "quest-combat-contract:elemental-weakness",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The complete route and encounter choreography retain ported dialogue, movement and teleports, but omitted camera and audio details are not yet restored.",
            "A real-client Foreman/dialogue, TUZO, concurrent-player, exact ten-minute timeout, death, cannon, water-spell and relog smoke is still pending.",
        ],
    },
    "Underground Pass": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Underground_Pass?oldid=15302567", "revision": 15302567, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Underground_Pass/Quick_guide?oldid=15302530", "revision": 15302530, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Underground_Pass?oldid=15290151", "revision": 15290151, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Doomion?oldid=15199585", "revision": 15199585, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Holthion?oldid=15199630", "revision": 15199630, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Othainian?oldid=15199584", "revision": 15199584, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Sir_Jerro?oldid=15199659", "revision": 15199659, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Sir_Carl?oldid=15199657", "revision": 15199657, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Sir_Harry?oldid=15199658", "revision": 15199658, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Kalrag?oldid=15199478", "revision": 15199478, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Disciple_of_Iban?oldid=15199382", "revision": 15199382, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Iban?oldid=14995900", "revision": 14995900, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Iban%27s_staff?oldid=15301733", "revision": 15301733, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Klank%27s_gauntlets?oldid=15183099", "revision": 15183099, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Amulet_of_doomion?oldid=15182758", "revision": 15182758, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Amulet_of_holthion?oldid=15182762", "revision": 15182762, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Amulet_of_othanian?oldid=15182766", "revision": 15182766, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Paladin%27s_badge_(Sir_Jerro)?oldid=15188396", "revision": 15188396, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Paladin%27s_badge_(Sir_Carl)?oldid=15188394", "revision": 15188394, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Paladin%27s_badge_(Sir_Harry)?oldid=15188395", "revision": 15188395, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Well_(Paladin_badges)?oldid=15255592", "revision": 15255592, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "upass_paladin1", "upass_paladin2", "upass_paladin3",
            "doomion", "holthion", "othainian", "upass_doomion_safe",
            "upass_holthion_safe", "upass_othainian_safe", "kalrag",
            "ibanmonk", "iban",
        ],
        "item_gamevals": [
            "meat_pie", "bread", "stew", "2dose1attack",
            "2doseprayerrestore", "paladinbadge1", "paladinbadge2",
            "paladinbadge3", "cave_unicorn_horn", "doomion_amulet",
            "holthion_amulet", "othainian_amulet", "ibandoll", "ibansdove",
            "ibansshadow", "ibans_ashes", "upassdwarfbrew",
            "klanks_gauntlets", "zamrobetop", "zamrobebottom", "caveorb1",
            "caveorb2", "caveorb3", "caveorb4", "woodplank", "rope",
            "bones", "vile_ashes", "ibanstaff", "brokenibanstaff",
        ],
        "loc_gamevals": [
            "cave_well", "bloodwell_upass", "cavetempledoor2l",
            "cavetempledoor2r", "upassshutchest1", "upass_cage_dummy",
            "ibantomb_left", "ibantomb_right", "upassdwarfbrewbarrel",
            "upass_templedoor_closed_left", "upass_templedoor_closed_right",
            "cave_temple_altar",
        ],
        "trigger_handlers": [
            "mapzone:0_37_151/0_36_154/1_33_71/1_33_72",
            "ai_queue3:upass_paladin1..3", "ai_queue3:doomion/holthion/othainian",
            "ai_queue3:kalrag", "ai_queue3:ibanmonk", "ai_timer:iban",
            "oplocu:bloodwell_upass", "oploc1:upassshutchest1",
            "oplocu:ibantomb_left/ibantomb_right", "oplocu:cave_temple_altar",
            "opnpc1:caveguide1",
        ],
        "loot_contract": "Every named actor and every combat/quest ground item is owner-private. A paladin's first eligible death is bones plus its badge; while the badge is owned, subsequent deaths roll the exact 128-slot table. Each named demon always drops vile ashes and conditionally its unique amulet. Kalrag and Iban drop nothing; Kalrag applies blood directly when the doll is carried and Iban is destroyed by the completed doll. Each Disciple drops bones plus both Zamorak robe pieces and never a broken staff. The finale grants one fully charged Iban's staff with no obsolete rune bundle; Koftik replaces a lost broken staff post-quest.",
        "test_ids": [
            "quest-combat-contract:underground-pass",
            "quest-combat-contract:underground-pass-static-spawns",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The dungeon keeps its five hard-coordinate legacy map squares; encounter actors, credit and drops are instance-equivalent, but the scenery route is not copied into one private dynamic map.",
            "The port retains functional route dialogue and movement while some camera and audio detail remains omitted.",
            "A real-client two-player, manual-spell, inventory-full, death, loss/reclaim, Iban-hazard and relog smoke is still pending.",
        ],
    },
    "Observatory Quest": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Observatory_Quest?oldid=15270070", "revision": 15270070, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Observatory_Quest/Quick_guide?oldid=15084388", "revision": 15084388, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Observatory_Quest?oldid=15263266", "revision": 15263266, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Goblin_guard?oldid=15290834", "revision": 15290834, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Goblin_kitchen_key?oldid=15184106", "revision": 15184106, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Chest_(Observatory_Dungeon,_key)?oldid=14450087", "revision": 14450087, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Chest_(Observatory_Dungeon,_spider)?oldid=14376384", "revision": 14376384, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Chest_(Observatory_Dungeon,_antipoison)?oldid=14770666", "revision": 14770666, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Goblin_stove?oldid=14468652", "revision": 14468652, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Lens_mould?oldid=15185521", "revision": 15185521, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Observatory_lens?oldid=15184089", "revision": 15184089, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "qip_obs_goblin_guard", "goblin_guard", "poisonspider",
        ],
        "item_gamevals": [
            "keep_key", "lens_mould", "lens", "molten_glass",
            "1doseantipoison", "bones", "rag_goblin_bone",
            "arceuus_corpse_goblin", "trail_clue_beginner",
            "trail_clue_easy_simple001", "champions_challenge_goblin",
        ],
        "loc_gamevals": [
            "qip_obs_dungeon_chest_closed/open",
            "qip_obs_dungeon_chest_closed2/open2",
            "qip_obs_dungeon_chest_closed3/open3",
            "qip_obs_keep_chest_closed/open", "shutdungeonchest/opendungeonchest",
            "keepgate_closed", "keepgate_closed_left",
            "qip_obs_dungeon_stove_top_multi", "qip_obs_dungeon_stove",
            "qip_obs_dungeon_stove_empty",
        ],
        "trigger_handlers": [
            "opnpc1:qip_obs_goblin_guard", "opnpc2:goblin_guard",
            "ai_queue3:goblin_guard", "oploc1/2:qip_obs_*chest*",
            "oploc1/2:shutdungeonchest/opendungeonchest",
            "oploc1/oplocu:keepgate_closed/keepgate_closed_left",
            "oploc1:qip_obs_dungeon_stove*", "opheldu:lens_mould/molten_glass",
            "opheld5:keep_key/lens_mould/lens",
        ],
        "loot_contract": "The shared optional guard never supplies a quest key or kill credit. It always drops bones, rolls the exact 128-slot guard table, and independently rolls the eligible Rag and Bone Man skull, 1/35 ensouled head, 1/64 beginner clue, 1/128 or easy-CA 1/121 easy clue, and 1/5,000 champion scroll. One player-random southeast chest supplies the recoverable kitchen key; seven other chests each spawn one poison spider once and the antipoison chest remains unlimited. The stove supplies at most one mould/lens pipeline at a time and restores the mould after pre-delivery destruction.",
        "test_ids": [
            "quest-combat-contract:observatory-quest",
            "quest-combat-contract:observatory-chests",
        ],
        "known_gaps": [
            "The legacy port still completes after the lens hand-off without the current dome/telescope/constellation cutscene, 2,250 Crafting XP and constellation reward; that broader quest finale remains pending.",
            "A real-client two-player lure/collision, chest-choice, spider poison, full-inventory, item-destroy/reclaim, death and relog smoke is still pending.",
        ],
    },
    "The Tourist Trap": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/The_Tourist_Trap?oldid=15267714", "revision": 15267714, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/The_Tourist_Trap/Quick_guide?oldid=15084762", "revision": 15084762, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:The_Tourist_Trap?oldid=15263267", "revision": 15263267, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Mercenary_Captain?oldid=15301893", "revision": 15301893, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Metal_key?oldid=15184553", "revision": 15184553, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Desert_Mining_Camp?oldid=15302180", "revision": 15302180, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Slave_shirt?oldid=15183224", "revision": 15183224, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Slave_robe?oldid=15183223", "revision": 15183223, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Slave_boots?oldid=15183222", "revision": 15183222, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Technical_plans?oldid=15184231", "revision": 15184231, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Prototype_dart?oldid=15185563", "revision": 15185563, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ana_in_a_barrel?oldid=15238258", "revision": 15238258, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "desertminingcaptain", "tourtrap_qip_desert_mining_merc_1",
            "tourtrap_qip_desert_mining_merc_2",
            "tourtrap_qip_desert_mining_merc_3",
            "tourtrap_qip_desert_mining_merc_4",
        ],
        "item_gamevals": [
            "bones", "metal_key", "slave_shirt", "slave_robe",
            "slave_boots", "thbedobinkey", "thcaptplans",
            "thprotodarttip", "thprotodart", "tentipineapple",
            "tourtrap_qip_barrel", "thanainabarrel", "thgoodminekey",
        ],
        "loc_gamevals": [
            "miningcampgateclosedl", "miningcampgateclosedr",
            "capt_siad_bookcase", "captain_siads_chest_closed",
            "experimental_anvil", "touristtrap_minecart", "ropepullthingy",
            "ropepullthingy2", "undergroundniceminel", "undergroundniceminer",
        ],
        "trigger_handlers": [
            "opnpc1/opnpc2/apnpc2/opnpc3:desertminingcaptain",
            "ai_queue3:desertminingcaptain",
            "oploc1/oploc2/oplocu:miningcampgateclosedl/r",
            "timer:desertrescue_mercenary_check",
        ],
        "loot_contract": "The captain is a normal shared-world level-47 NPC but only dialogue establishes an eligible one-on-one duel; direct Attack invokes the guard arrest instead. The hero always receives one owner-private bones drop and advances approached-captain to killed-captain. If no metal key exists in inventory or bank, it is added directly when space exists or placed owner-private at the death tile when full; no second death hook or generic table may duplicate it. The key opens the outer camp gate, where weapons other than pickaxes and defensive armour—not equipment in the duel—cause arrest.",
        "test_ids": [
            "quest-combat-contract:tourist-trap",
            "quest-combat-contract:tourist-trap-gate",
        ],
        "known_gaps": [
            "The complete post-gate slave disguise, Captain Siad/plans, prototype dart, pineapple, Ana barrel/cart/winch escape and two-skill reward route remains pending modernization from the available LostCity reference.",
            "A real-client direct-attack/arrest, dialogue duel, cannon/lure, concurrent-kill, key inventory/bank/full-inventory, gate equipment, death and relog smoke is still pending.",
        ],
    },
    "Watchtower": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Watchtower?oldid=15283800", "revision": 15283800, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Watchtower/Quick_guide?oldid=15263558", "revision": 15263558, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Watchtower?oldid=15263268", "revision": 15263268, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Gorad?oldid=15199459", "revision": 15199459, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ogre_tooth?oldid=15184413", "revision": 15184413, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Grew?oldid=14995884", "revision": 14995884, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ogre_relic?oldid=15184412", "revision": 15184412, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Crystal_(Watchtower)?oldid=15184426", "revision": 15184426, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Skavid_map?oldid=15211526", "revision": 15211526, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ogre_potion?oldid=15053409", "revision": 15053409, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "gorad", "grew", "toban", "og", "ogre_shaman",
            "watchtower_wizard", "city_guard", "enclave_guard",
            "skavidtalker1", "skavidtalker2", "skavidtalker3",
            "skavidtalker4", "scared_skavid", "mad_skavid",
        ],
        "item_gamevals": [
            "big_bones", "ogretooth", "relicpart1", "relicpart2",
            "relicpart3", "ogrerelic", "powering_crystal1",
            "powering_crystal2", "powering_crystal3", "powering_crystal4",
            "fingernails", "stolen_gold", "dragon_bones", "rockcake",
            "deathrune", "skavidmap", "nightshade", "ogre_potion",
            "magic_ogre_potion", "shaman_robe", "watchtowerspell",
            "arceuus_corpse_ogre", "dorgesh_construction_bone",
            "dorgesh_construction_bone_curved",
        ],
        "loc_gamevals": [
            "watchtowerbush*", "towerladder", "skavid_cave1..6",
            "tobanchest", "tobancave", "ganothbattlement",
            "shamangate/shamangate2", "rock_of_dalgroth", "watchleverup",
        ],
        "trigger_handlers": [
            "opnpc1:gorad", "ai_queue3:gorad", "queue:defeat_gorad",
            "opnpc1/opnpcu:grew", "opnpc1/opnpcu:toban/og/city_guard",
            "opnpc1/opnpcu:ogre_shaman", "oploc1:watchtower route locs",
            "opheldu:relic parts/ogre potion ingredients",
        ],
        "loot_contract": "Every eligible Gorad death gives the hero one owner-private big bones drop, independently rolls the exact 19/128 uncommon-seed table, 1/30 ensouled ogre head, 1/400 long bone and 2/10,025 curved bone. Only while Grew's spoken/helped bit range equals one, and only if the player owns no tooth and has a free inventory slot, the death queue adds one ogre tooth directly to inventory. Full inventory produces no ground tooth; another eligible kill recovers it. Giving the tooth to Grew consumes it and atomically grants relic part 2 plus the yellow crystal.",
        "test_ids": [
            "quest-combat-contract:watchtower-gorad",
            "quest-combat-contract:watchtower-tooth",
        ],
        "known_gaps": [
            "Rag and Bone Man II remains deferred and exposes no authored wishlist/submission state, so Gorad's conditional 1/4 ogre-ribs tertiary cannot yet be granted without leaking it outside that miniquest.",
            "The port still marks the gate swing, tree rope-swing and some camera/audio detail as deferred, and several guard/wizard dialogue files remain labelled thin.",
            "A real-client full-inventory, tooth loss/recovery, concurrent-kill, shaman/crystal, death and relog smoke is still pending.",
        ],
    },
    "Legends' Quest": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Legends%27_Quest?oldid=15293032", "revision": 15293032, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Legends%27_Quest/Quick_guide?oldid=15231427", "revision": 15231427, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Legends%27_Quest?oldid=15263273", "revision": 15263273, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Nezikchened?oldid=15242487", "revision": 15242487, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ranalph_Devere?oldid=15215908", "revision": 15215908, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Irvig_Senay?oldid=15215870", "revision": 15215870, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/San_Tojalon?oldid=15276414", "revision": 15276414, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Viyeldi?oldid=15276415", "revision": 15276415, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Holy_water?oldid=15290188", "revision": 15290188, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Binding_book?oldid=15303420", "revision": 15303420, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Golden_bowl?oldid=15184878", "revision": 15184878, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Yommi_tree_seeds?oldid=15184984", "revision": 15184984, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Bravery_potion?oldid=15185855", "revision": 15185855, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Yommi_tree?oldid=15201240", "revision": 15201240, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "nezikchened", "echned_zekin", "viyeldi", "ranalph_devere",
            "irvig_senay", "san_tojalon", "ungadulu_bad",
            "ungadulu_good", "gujuo", "boulder_legends",
        ],
        "item_gamevals": [
            "book_of_binding", "vial_empty", "vial_enchanted", "holy_water",
            "goldbowl_empty", "goldbowlbless_empty", "goldbowlbless_pure",
            "yommiseeds", "yommiseeds_germ", "bravery_pot", "darkdagger",
            "glowingdagger", "holyforce", "thtotempole", "thtotempolegift",
            "rune_axe", "dragon_axe", "magic_logs", "logs",
        ],
        "loc_gamevals": [
            "lqfirewall_straight/diagonal", "lgwaterpool", "fertilesoil",
            "yommitree_baby/sapling/adult/felled/trimmed/totem and rotten variants",
            "lg_totem_pole_evil", "lg_totem_pole_good", "damaged_earth",
        ],
        "trigger_handlers": [
            "opnpc2/apnpc2/ai_opplayer2/ai_applayer2/ai_queue3:nezikchened",
            "ai_queue3:ranalph_devere/irvig_senay/san_tojalon",
            "opnpcu:ungadulu_bad/echned_zekin/viyeldi",
            "opheld1:holyforce", "opheldu:book_of_binding/goldbowlbless_pure/yommiseeds",
            "oplocu:lgwaterpool/fertilesoil/Yommi states/lg_totem_pole_evil",
        ],
        "loot_contract": "All three Nezikchened versions have no ordinary, ash or bones drop. The first death advances the fire encounter and performs the authored last-ditch magic hit; the second advances the water encounter; the final advances the totem encounter. The short final route summons owner-private San Tojalon, Irvig Senay and Ranalph Devere in order before Nezikchened; completed heroes persist across retreat. The heroes' first cave versions grant their one crystal only if the player owns none, while final versions grant no loot. Holy Water is single-use, uses its custom Nezikchened maximum hit and applies one non-stacking 5% Defence drain. Authored books, daggers, force card, bowls, seeds, totems and gilded totem are interaction rewards, never generic boss drops.",
        "test_ids": [
            "quest-combat-contract:legends-nezikchened",
            "quest-combat-contract:legends-route-items",
            "quest-combat-contract:legends-yommi",
        ],
        "known_gaps": [
            "The fire wall is an era-compatible one-action route rather than the live segment-by-segment reignition presentation.",
            "Some quest-wide guard, gate, barrel-spawn, camera and audio choreography outside the encounter remains marked thin or deferred.",
            "A real-client three-fight, both dagger branches, retreat/resume, inventory-full, Yommi rot, death and relog smoke is still pending.",
        ],
    },
    "Big Chompy Bird Hunting": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Big_Chompy_Bird_Hunting?oldid=15292269", "revision": 15292269, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Big_Chompy_Bird_Hunting/Quick_guide?oldid=15274013", "revision": 15274013, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Big_Chompy_Bird_Hunting?oldid=15263278", "revision": 15263278, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Chompy_bird?oldid=15236005", "revision": 15236005, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Bloated_toad?oldid=15184286", "revision": 15184286, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ogre_bellows?oldid=15195336", "revision": 15195336, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ogre_bow?oldid=15183145", "revision": 15183145, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ogre_arrow?oldid=15185835", "revision": 15185835, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Wolf_bones?oldid=15270861", "revision": 15270861, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Raw_chompy?oldid=15183679", "revision": 15183679, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Cooked_chompy?oldid=15184210", "revision": 15184210, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Rantz?oldid=15196225", "revision": 15196225, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "rantz", "fycie", "bugs", "toad", "bloated_toad",
            "chompybird", "chompybird_dead", "wolf",
        ],
        "item_gamevals": [
            "achey_tree_logs", "wolf_bones", "wolfbone_arrowheads", "feather",
            "ogre_arrow_shaft", "ogre_headless_arrow", "ogre_arrow",
            "empty_ogre_bellows", "filled_ogre_bellow1/2/3", "bloated_toad",
            "ogre_bow", "raw_chompy", "cooked_chompy", "ruined_chompy",
            "cooked_s_chompy", "bones", "potato/onion", "equa_leaves/cabbage",
            "tomato/doogleleaves",
        ],
        "loc_gamevals": [
            "chompybird_chest/chompybird_chest_open", "swampbubbles/swampbubbles_swamp",
            "chompybird_spitroast_empty", "chompybird_spitroast/cooked/ruined",
            "rantzogrecaveentrance/rantzogrecaveexitr/rantzogrecaveexitl",
        ],
        "trigger_handlers": [
            "opnpc1/opnpcu:rantz", "opnpcu:toad", "opheld1/4/5:bloated_toad",
            "ai_queue4:bloated_toad", "opnpc5/apnpc5/ai_queue3:chompybird",
            "opnpc4:chompybird_dead", "oplocu:chompybird_spitroast_empty",
            "queue:quest_chompybird_complete",
        ],
        "loot_contract": "Each owner-private bloated toad rolls 1/5 every 25 ticks up to four times, bursts after one minute for 1-2 damage to its owner, and can attract one owner-private Chompy 3-10 tiles away. The level-6 Chompy has exact 10/5/5/3/0/0 stats, flees nearby players, expires after 100 ticks, and accepts damage only from an ogre/comp ogre bow with ogre or allowed brutal arrows using the special visible-Ranged/ammo-strength maximum-hit formula. The eligible kill advances the quest or post-quest notch count and creates a two-minute owner-private downed corpse. Pluck grants 10-30 feathers directly plus owner-private raw chompy and bones. Only the player's authored first kill, raw-carcass presentation, three requested seasonings, successful spit cooking and seasoned hand-in complete the quest.",
        "test_ids": [
            "quest-combat-contract:big-chompy-bait",
            "quest-combat-contract:big-chompy-bird",
            "quest-combat-contract:big-chompy-recipe",
        ],
        "known_gaps": [
            "Rantz's missed shot animates and advances on the authentic delay, but the bird-directed projectile and camera/sound presentation remain reduced.",
            "Post-quest Chompy hats, Western Provinces double-spawn tiers, elite pet roll and diary reward claims are outside the quest encounter and remain pending in their owning systems.",
            "A real-client bait ownership, four-roll timing, bow/ammo rejection, corpse timeout, inventory-full, cooking-failure, death and relog smoke is still pending.",
        ],
    },
    "Elemental Workshop I": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Elemental_Workshop_I?oldid=15292271", "revision": 15292271, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Elemental_Workshop_I/Quick_guide?oldid=14836472", "revision": 14836472, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Elemental_Workshop_I?oldid=15263279", "revision": 15263279, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Elemental_Workshop_II?oldid=15271178", "revision": 15271178, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Elemental_Workshop_II/Quick_guide?oldid=14955157", "revision": 14955157, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Elemental_Workshop_II?oldid=15263361", "revision": 15263361, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Earth_elemental?oldid=15266539", "revision": 15266539, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Elemental_rock?oldid=15196447", "revision": 15196447, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Elemental_ore?oldid=15183923", "revision": 15183923, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Elemental_metal?oldid=15275847", "revision": 15275847, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Elemental_shield?oldid=15205412", "revision": 15205412, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Primed_bar?oldid=15183739", "revision": 15183739, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Mind_helmet?oldid=15183129", "revision": 15183129, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Mind_shield?oldid=15205410", "revision": 15205410, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "elem1_qip_earth_elemental_rock_version_rock",
            "elem1_qip_earth_elemental_rock_version",
            "elemental_earth", "elemental_air", "elemental_water", "elemental_fire",
        ],
        "item_gamevals": [
            "elemental_workshop_shield_book/elemental_workshop_shield_book_slashed",
            "elemental_workshop_key", "elemental_workshop_lava_bowl/_full",
            "needle/thread/leather", "pickaxe", "elem1_qip_rockremains",
            "elemental_workshop_ore", "coal", "elemental_workshop_bar",
            "hammer", "elemental_shield", "elemental_workshop_helm_book",
            "elemental_workshop_2_key", "elemental_workshop_claw_book",
            "elemental_workshop_lever_book", "elem_broken_finger",
            "elem2_smallgear/medgear/biggear/spare_pipe", "elem_primed_bar",
            "elem_mind_bar", "elem_mind_helm", "elemental_mind_shield",
        ],
        "loc_gamevals": [
            "elemental_workshop_bookcase", "elemental_workshop_oddwall_l/r",
            "elemental_workshop_spiralstairs/top", "elemental_workshop_valve_1/2",
            "elemental_workshop_water_lever", "elemental_workshop_bellows_noanim",
            "elemental_workshop_air_lever", "elemental_workshop_box_1..8",
            "elemental_workshop_trough_1..5", "elemental_workshop_furnace_out/lit",
            "elemental_workshop_workbench", "elem2_stairs_door_close/open",
            "elem2_crane_track_up_empty", "elem2_press_junction_box",
            "elemental_piping_blue_broken", "elem2_wind_pin_high/low/left",
            "elem2_lever_3way", "elem2_earth_lever_1", "elem2_water_lever",
            "elem2_corkscrew", "elem2_valve_1/2", "elem2_air_lever",
            "elem_extractor_gun_no_bar/bar/bar_mind", "elem_extractor_hat",
        ],
        "trigger_handlers": [
            "opnpc1:elem1_qip_earth_elemental_rock_version_rock",
            "ai_queue3:elem1_qip_earth_elemental_rock_version",
            "oploc1:elemental_workshop_valve/bellows/water+air levers/boxes",
            "oplocu:elemental_workshop_trough/furnace/workbench",
            "oploc1/oplocu:elemental_workshop_oddwall_l/r",
            "oploc1/oplocu:EWII repair/priming/extractor apparatus",
        ],
        "loot_contract": "Mining an elemental rock requires current Mining 20 and a usable pickaxe and creates one owner-private level-35 Earth elemental with exact 35/20/35/35/10/30 stats and speed 6. Its eligible death has no roaming-elemental table: it creates owner-private one elemental rock and one elemental ore, both guaranteed. Roaming earth/air/water/fire elementals remain separate ordinary creatures. The ore is refined only through the repaired Workshop I machinery with four coal; elemental metal is consumed at the workbench for the quest shield or Workshop II claw/primed-bar route. Pre-owned elemental metal bypasses Workshop II's two optional ore fights. The extractor consumes 20 current Magic per primed mind bar; the beaten/slashed books select mind helmet/mind shield output.",
        "test_ids": [
            "quest-combat-contract:elemental-workshop-rock",
            "quest-combat-contract:elemental-workshop-machinery",
            "quest-combat-contract:elemental-workshop-ii-pipeline",
        ],
        "known_gaps": [
            "The Workshop II junction-box drag interface is represented by one deterministic solved interaction because this server has no established drag-connect widget contract.",
            "The Workshop II crane uses the exact state machine and fixed scenery with narration; moving crane/cart scenery and camera/audio presentation remain pending.",
            "A real-client two-player ownership, replacement-item, wrong-order machinery, pre-owned-bar bypass, Magic-drain, death and relog smoke is still pending.",
        ],
    },
    "Nature Spirit": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Nature_Spirit?oldid=15292274", "revision": 15292274, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Nature_Spirit/Quick_guide?oldid=15204119", "revision": 15204119, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Nature_Spirit?oldid=15263282", "revision": 15263282, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ghast?oldid=15266981", "revision": 15266981, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Druid_pouch?oldid=15286832", "revision": 15286832, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Silver_sickle_(b)?oldid=15254467", "revision": 15254467, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Bloom?oldid=15271265", "revision": 15271265, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Rotten_food?oldid=15258515", "revision": 15258515, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Mort_myre_fungus?oldid=15271259", "revision": 15271259, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Mort_myre_stem?oldid=15271270", "revision": 15271270, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Mort_myre_pear?oldid=15271273", "revision": 15271273, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "filliman_tarlock_spirit", "filliman_tarlock_ns",
            "ghast_invis", "ghast_vis", "mort_myre_gate_guard",
        ],
        "item_gamevals": [
            "meat_pie/apple_pie", "amulet_of_ghostspeak", "mirror",
            "filliman_journal", "bloom_spell/used_bloom_spell",
            "silver_sickle/silver_sickle_blessed",
            "mortmyremushroom/mortmyrebuddingstem/mortmyrepear",
            "druid_pouch_empty/druid_pouch", "rotten_food", "bones",
        ],
        "loc_gamevals": [
            "mortmyre_metalgateclosed_l/r", "druidjump_loc",
            "grotto_druidicspirit/grotto_door_druidicspirit",
            "stonedisc_ds_faith/nature/spirit",
            "log/branch/peartree_druidicspirit and blossomed variants",
            "druidic_spirit_grotto/druidic_spirit_grotto_naturealtar",
        ],
        "trigger_handlers": [
            "ai_opplayer2/opnpcu:ghast_invis",
            "opnpc2/apnpc2/ai_opplayer2/ai_queue3/ai_timer:ghast_vis",
            "opheld3:silver_sickle_blessed", "opheld1/opheldu:druid_pouch(_empty)",
            "oplocu:stonedisc_ds_nature/spirit", "oploc1:grotto door/altar",
            "queue:ghast_vis_reward/druidspirit_quest_complete",
        ],
        "loot_contract": "An invisible ghast rolls a 3/10 successful attack after the shared 26-tick delay: one eligible food becomes rotten food, or a foodless player takes 1-3 typeless damage; a filled pouch instead consumes one charge and manifests the ghast. The public level-30 visible ghast has exact 45/22/22/18/1/1 stats, speed 4, max hit 3 and 25% air weakness. It intentionally remains public: whichever player gets last-hit credit while close receives 30 Prayer XP and the public guaranteed bones plus exact 128-way weapon/rune/herb/coin/other/gem table. Only killers in the three exact quest states advance one step, preventing duplicate credit. Blossomed fungus/stem/pear contribute 1/2/3 charges, with three items consumed pear-first; the grotto respawn and full-inventory dialogue recover an empty pouch.",
        "test_ids": [
            "quest-combat-contract:nature-spirit-ghast",
            "quest-combat-contract:nature-spirit-pouch",
            "quest-combat-contract:nature-spirit-ritual",
        ],
        "known_gaps": [
            "The druid-pouch projectile and several quest camera/sound effects use the closest era-compatible spot-animation/dialogue presentation.",
            "Morytania diary attack avoidance and the post-quest Fire of Dehumidification belong to their diary/quest systems and are not yet integrated with this classic encounter.",
            "A real-client public kill-steal, pouch exhaustion, all-food/full-inventory, retreat/reversion, death and relog smoke is still pending.",
        ],
    },
    "Priest in Peril": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Priest_in_Peril?oldid=15292273", "revision": 15292273, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Priest_in_Peril/Quick_guide?oldid=15266571", "revision": 15266571, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Priest_in_Peril?oldid=15290079", "revision": 15290079, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Temple_Guardian?oldid=15237248", "revision": 15237248, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Monk_of_Zamorak_(Paterdomus)?oldid=15290091", "revision": 15290091, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Golden_key?oldid=15254446", "revision": 15254446, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Iron_key?oldid=15254444", "revision": 15254444, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Monument?oldid=15271167", "revision": 15271167, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Well_(Paterdomus)?oldid=15247737", "revision": 15247737, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Murky_water?oldid=15187401", "revision": 15187401, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Blessed_water?oldid=15302236", "revision": 15302236, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Wolfbane?oldid=15183276", "revision": 15183276, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Drezel?oldid=15271643", "revision": 15271643, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "priestperilguarddog/priestperil_guardian_model",
            "priestperilevilmonk1/2/3", "priestperiltrappedmonk/2",
            "priestperilvampire",
        ],
        "item_gamevals": [
            "pipkey_gold/pipkey_iron", "bucket_empty/bucket_murkywater/bucket_blessedwater/bucket_water",
            "pipneedle_gold/pipfeather_gold/pippot_gold/piptinderbox_gold/piphammer_gold/pipcandle_gold",
            "needle/feather/pot_empty/tinderbox/hammer/unlit_candle",
            "blankrune/blankrune_high", "monkrobetop/monkrobebottom/bones", "dagger_wolfbane",
        ],
        "loc_gamevals": [
            "priestperil_grave_base1..7", "priestperil_well",
            "pip_underground_door1/2", "pip_prisondoor",
            "priestperil_vampirecoffin", "pip_underground_wall_side_withportal",
        ],
        "trigger_handlers": [
            "zone/opnpc2/apnpc2/ai_queue3:priestperilguarddog",
            "ai_opplayer2/ai_queue3:priestperilevilmonk1/2/3",
            "oploc1/2/u:priestperil_grave_base1..7",
            "oploc1/u:priestperil_well", "oploc1:pip_underground_door1/2",
            "opnpc1/opnpcu:priestperiltrappedmonk/2", "oploc1/u/2:pip_prisondoor",
            "oploc1:pip_underground_wall_side_withportal", "opnpc1:king_roald",
            "npc_immune_to_magic:cast/barrage/powered-staff dispatch",
        ],
        "loot_contract": "The level-30 Temple Guardian is one owner-private 45/20/20/20/1/1 actor, speed 4, max hit 3, stab-based, non-aggressive, cannon-immune by private ownership, and immune to cast spells, barrage secondary hits and powered staves before any runes, charges, projectile or XP are spent. It drops nothing and advances 2->3 only for its owner after at least one point of direct player damage; recoil-only death heals/resets it. The aggressive level-17/22/30 Paterdomus monks have exact 10/8/8/12/25/1, 20/18/18/22/25/1 and 25/25/25/25/40/1 stats, speed 4, max hits 5, melee 3/magic 10, and melee 3/magic 8 respectively, 25-tick respawns, and -75% combat XP. A directly credited killer receives private bones plus one shared 20-way roll: robe top 1/20, robe bottom 1/20, nothing 18/20. The level-30 monk also guarantees one private golden key until Drezel is unlocked. The key opens the first gate without consumption and swaps at a per-player randomized monument for the iron key; six optional golden souvenirs are one-time swaps and raw theft deals 1-6 damage. Drezel converts every carried murky-water bucket, accepts any mixture of 50 unnoted rune/pure essence over partial talk or use hand-ins, then grants 1 Quest Point, 1,406 Prayer XP, Wolfbane and barrier access; Wolfbane is reclaimable when no copy is owned.",
        "test_ids": [
            "quest-combat-contract:priest-peril-guardian",
            "quest-combat-contract:priest-peril-monks",
            "quest-combat-contract:priest-peril-monuments",
            "quest-combat-contract:priest-peril-completion",
        ],
        "known_gaps": [
            "The modern encounter instance is represented by the server's owner-private actor contract inside the shared classic map square rather than a separately cloned map instance.",
            "Some cutscene camera, exact animation and sound choreography uses the closest cache-era presentation.",
            "A real-client two-player ownership, recoil-only, all magic paths, randomized monuments, lost-key recovery, partial mixed-essence, inventory-full, death and relog smoke is still pending.",
        ],
    },
    "Regicide": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Regicide?oldid=15292278", "revision": 15292278, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Regicide/Quick_guide?oldid=15245801", "revision": 15245801, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Regicide?oldid=15286135", "revision": 15286135, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Tyras_guard?oldid=15199537", "revision": 15199537, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Tyras_guard_(Catapult)?oldid=15286081", "revision": 15286081, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Fractionalising_still?oldid=15238267", "revision": 15238267, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Quicklime?oldid=15287463", "revision": 15287463, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Pot_of_quicklime?oldid=15238278", "revision": 15238278, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ground_sulphur?oldid=15238276", "revision": 15238276, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Barrel_of_naphtha?oldid=15238266", "revision": 15238266, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Barrel_bomb?oldid=15301638", "revision": 15301638, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Crystal_pendant?oldid=15184517", "revision": 15184517, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Elf_Tracker?oldid=15196298", "revision": 15196298, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Lord_Iorwerth?oldid=15275396", "revision": 15275396, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/King_Lathas?oldid=15277012", "revision": 15277012, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Arianwyn?oldid=15199389", "revision": 15199389, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "regicide_old_camp_guard", "regicide_tyras_guard/regicide_tyras_camp_guard",
            "regicide_good_elf1/regicide_evil_elf1/regicide_evil_elf2", "regicide_good_elf3",
            "lord_iorwerth", "regicide_old_camp_tracker", "regicide_tyras_lazy_guard",
        ],
        "item_gamevals": [
            "regicide_quest_kings_summons", "regicide_crystal_pendant", "regicide_alchemy",
            "regicide_sulphar/regicide_sulphar_dust", "limestone/regicide_quicklime/regicide_quicklime_dust",
            "regicide_barrel_empty/regicide_barrel_tar/regicide_barrel_naphtha",
            "regicide_barrel_naphtha_quicklime_mix/regicide_barrel_naphtha_sulphar_mix",
            "regicide_barrel_lid/regicide_barrel_lid_fused", "regicide_cloth", "tinderbox",
            "regicide_iorwerth_message", "bones and exact Tyras-guard table",
        ],
        "loc_gamevals": [
            "regicide_voyage_temple_well1/2 and entrance/exit",
            "regicide_trap_woodspring/regicide_trap_tripwire/regicide_pitfall_*",
            "regicide_logbalance1/2/3_start", "regicide_cross_over1/2/3 and Tyras-camp variants",
            "regicide_tar_collection/regicide_fractionalizing_still/regicide_furnace/regicide_loom",
            "regicide_catapult and Tyras tent/fire cutscene locs",
        ],
        "trigger_handlers": [
            "oploc1:Well of Voyage/forest hazards/dense forest/log balances",
            "zone:Idris/Arianwyn/tripwire/pitfall encounters", "opnpc2/apnpc2/ai_queue3:Tyras guards",
            "oplocu:furnace/loom/still/catapult", "if_button/if_close/softtimer:regicide_still",
            "opheldu:quicklime/sulphur/naphtha/bomb mixtures", "opheld1:summons/sealed message",
            "opnpc1:tracker/Iorwerth/King Lathas/lazy guard",
        ],
        "loot_contract": "Only the owner-private level-110 guard summoned at the authored dense-forest crossing advances 8->9; static camp guards use ordinary combat and the exact 128-way bones/equipment/runes/coins/food/gold/thread/gem table but grant no quest credit. All three variants have exact 110/85/95/100/1/0 stats, speed 5, max hit 15, stab attack and +5% combat XP. The route requires current Agility 56 (45 for logs), with spring/tripwire/pitfall damage and poison. Quicklime always forms but bare hands take 8 damage; grinding without a pot consumes it and deals non-lethal damage. The native still requires controlled tar flow, pressure and coal heat before yielding naphtha. Both powder orders, four-wool cloth, cooked-rabbit distraction and a tinderbox lead to the full catapult/tent scene. Idris, the scout attackers, the quest guard and Arianwyn are owner-private; Arianwyn must unseal the letter before Lathas grants 3 QP, 13,750 Agility XP and 15,000 coins.",
        "test_ids": [
            "quest-combat-contract:regicide-route", "quest-combat-contract:regicide-guard",
            "quest-combat-contract:regicide-bomb", "quest-combat-contract:regicide-reward",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The route uses the cache's generic obstacle destination table for successful forest crossings; trap consequences, level gates and encounter state are authored.",
            "Some Idris attack, trap and catapult camera/audio choreography uses reduced era-compatible presentation while retaining actors, items, movement and state gates.",
            "A real-client concurrent-player, trap, still-overheat, item-loss, catapult, death and relog smoke is still pending.",
        ],
    },
    "Tai Bwo Wannai Trio": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Tai_Bwo_Wannai_Trio?oldid=15265886", "revision": 15265886, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Tai_Bwo_Wannai_Trio/Quick_guide?oldid=15267185", "revision": 15267185, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Tai_Bwo_Wannai_Trio?oldid=15302415", "revision": 15302415, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Monkey?oldid=15217840", "revision": 15217840, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Tamayu?oldid=15196252", "revision": 15196252, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Tinsay?oldid=14918139", "revision": 14918139, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Timfraku?oldid=15070106", "revision": 15070106, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Tiadeche?oldid=15196408", "revision": 15196408, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/The_Shaikahan?oldid=15206313", "revision": 15206313, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "monkey", "tbwt_timfraku", "tbwt_tamayu/tbwt_tamayu_hunter/tbwt_tamayu_final_hunter",
            "tbwt_tiadeche", "tbwt_tinsay", "tbwt_lubufu", "tbwt_beast/tbwt_beast_cutscene",
        ],
        "item_gamevals": [
            "tbwt_monkey_corpse/tbwt_monkey_skin and stuffed/sandwich variants",
            "tbwt_jogre_bones/tbwt_burnt_jogre_bones and raw/cooked-paste/marinated variants",
            "tbwt_raw/cooked/poorly_cooked_karambwan and five pestle-and-mortar pastes",
            "tbwt_raw/cooked_karambwanji", "tbwt_karambwan_vessel and baited vessel",
            "tbwt_crafting_manual", "tbwt_banana/sliced-banana rum variants", "tbwt_cleaning_cloth",
            "standard and barbarian (kp) spears/hastae", "agility potion doses", "coins",
        ],
        "loc_gamevals": [
            "lubufu_karambwan", "tbwt_bamboo_door", "tbwt_tribal_statue",
            "bones_in_paste_fire", "range/fire/furnace cooking and burning dispatch",
        ],
        "trigger_handlers": [
            "opnpc2/ai_queue3:monkey", "opnpc1/opnpcu:Tamayu/Tiadeche/Tinsay/Lubufu",
            "timer/ai_timer:Tamayu and Shaikahan hunt", "opnpc2:tbwt_beast",
            "opheldu:Karambwan/paste/spear/vessel/corpse/bones item pipelines",
            "timer:light_jogre_bones", "furnace/Superheat/cooking dispatch",
            "oploc1/2:tbwt_bamboo_door", "oploc1:tbwt_tribal_statue",
            "queue:tbwt_quest_complete", "post-reward Tamayu/Tiadeche shop handlers",
        ],
        "loot_contract": "During the active quest, a directly credited Karamjan monkey kill is optional and yields one monkey corpse instead of monkey bones; adjacent melee is always dodged, while Ranged, Magic and reach weapons work. The corpse is a Tinsay ingredient after Tamayu skins it and is not quest-boss kill credit. Tamayu's Shaikahan attempt is an owner-private scripted hunt requiring exactly four agility-potion doses, an iron-or-better accepted spear/hasta and Karambwan poison; the quest has no player-killed boss drop. Tinsay requires burnt jogre bones marinated with raw Karambwanji paste, sliced banana rum and a seaweed-in-monkey-skin sandwich. Tiadeche requires a baited vessel and recoverable crafting manual. Timfraku grants only the base 2,000 coins and quest completion; the three 50,000-XP/rune-spear rewards are separately claimed from the returned brothers. After completion, the level-83 Shaikahan accepts direct damage only from (kp) spears/hastae and guarantees Shaikahan bones; Tamayu and Tiadeche shops remain inaccessible until their respective final reward states.",
        "test_ids": [
            "quest-combat-contract:tai-bwo-monkey", "quest-combat-contract:tai-bwo-tamayu",
            "quest-combat-contract:tai-bwo-item-pipelines", "quest-combat-contract:tai-bwo-postquest",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The cache-era cutscene uses fixed four-tick choreography where the source port used a generic NPC integer variable unavailable in this tree.",
            "The shared NPC poison/recoil systems do not yet reproduce the modern Shaikahan's exceptional post-quest indirect-damage paths; its quest hunt and direct (kp)-weapon immunity are complete.",
            "A real-client two-player, melee-dodge, full-inventory, wrong-paste explosion, all heating methods, shop gating, death and relog smoke is still pending.",
        ],
    },
    "Troll Stronghold": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Troll_Stronghold?oldid=15231622", "revision": 15231622, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Troll_Stronghold/Quick_guide?oldid=14728817", "revision": 14728817, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Troll_Stronghold?oldid=15263286", "revision": 15263286, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Dad?oldid=15199512", "revision": 15199512, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Troll_general?oldid=15267877", "revision": 15267877, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Twig?oldid=15199781", "revision": 15199781, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Berry?oldid=15199816", "revision": 15199816, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Prison_key_(Troll_Stronghold)?oldid=15267891", "revision": 15267891, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Cell_key_1?oldid=15185591", "revision": 15185591, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Cell_key_2?oldid=15185592", "revision": 15185592, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Godric?oldid=15031769", "revision": 15031769, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Eadgar?oldid=15003284", "revision": 15003284, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Dunstan?oldid=15289542", "revision": 15289542, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Law_talisman?oldid=15284766", "revision": 15284766, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "troll_champion", "troll_general/troll_general2/troll_general3",
            "troll_prison_guard1/_awake (Twig)", "troll_prison_guard2/_awake (Berry)",
            "troll_godric/troll_eadgar", "death_ig_commander/death_smithy", "troll_spectator1..7",
        ],
        "item_gamevals": [
            "death_climbingboots", "troll_key_prison/troll_key_godric/troll_key_eadgar",
            "law_talisman/coins", "big_bones/arceuus_corpse_troll/konar_key",
            "dorgesh_construction_bone/dorgesh_construction_bone_curved",
            "exact Troll General and mountain-troll primary tables",
        ],
        "loc_gamevals": [
            "troll_climbingrocks/top/bottom and mountain shortcuts",
            "troll_stronghold_arena entrance/exit gates", "stronghold/pass/Eadgar cave maplinks and stairs",
            "troll_stronghold_prison_door_closed", "troll_celldoor_godric/troll_celldoor_eadgar",
        ],
        "trigger_handlers": [
            "opnpc1/2/apnpc2/ai_opplayer2/ai_queue2/3:Dad", "mapzone:owner-private Dad/Godric/Eadgar",
            "ai_queue3:Troll Generals/Twig/Berry exact drop tables", "opnpc3/ai_timer:Twig/Berry",
            "oploc1/u:route/prison/cell keys", "opnpc1:Denulth/Dunstan start/reminders/completion/reclaim",
            "queue:troll_quest_complete", "combat XP multiplier and elemental-weakness params",
        ],
        "loot_contract": "Dad is an owner-private level-101, 120-HP, speed-8 crush boss with +5% combat XP, 40% Earth weakness and a 1/3 five-tile tree-swing knockback. He surrenders below 20 HP, can be spared or killed, and on death drops big bones plus independent 1/400 long-bone and 2/10025 curved-bone rolls; killing him enrages the spectators. All three level-113 generals have +7.5% XP and 20% Earth weakness; any one guarantees an owner-private prison key before prison entry when none is owned, then rolls the exact 128-way normal table plus independent 1/28 ensouled head, Konar-task 1/98 Brimstone key, long and curved bones. Twig and Berry support level-30 pickpocket success/failure/wake combat and guaranteed matching cell keys on kill; their exact 128-way table includes the full uncommon-seed and chaos-talisman gem slots plus 1/45 ensouled head, Konar-task 1/268 Brimstone key, long and curved bones. Prison/cell keys are consumed only at their authored doors. Godric is mandatory, Eadgar optional, and Dunstan grants 1 QP and a law talisman; lost replacements cost 1,000 coins.",
        "test_ids": [
            "quest-combat-contract:troll-stronghold-route", "quest-combat-contract:troll-stronghold-dad",
            "quest-combat-contract:troll-stronghold-keys", "quest-combat-contract:troll-stronghold-loot",
            "quest-combat-contract:troll-stronghold-reward", "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The 1/4 troll-bone tertiary remains gated off until Rag and Bone Man II's collection state is implemented; every unconditional and Slayer-conditional current tertiary is present.",
            "The cell escape uses cache-era tile walking and temporary door presentation rather than the modern cutscene camera/audio sequence.",
            "A real-client two-player ownership, Dad spare/kill, knockback collision, pickpocket failure, key loss, inventory-full, death and relog smoke is still pending.",
        ],
    },
    "Shades of Mort'ton": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Shades_of_Mort%27ton?oldid=15292280", "revision": 15292280, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Shades_of_Mort%27ton/Quick_guide?oldid=14988872", "revision": 14988872, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Shades_of_Mort%27ton?oldid=15263293", "revision": 15263293, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Loar_Shade?oldid=15199268", "revision": 15199268, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Shades_of_Mort%27ton_(minigame)?oldid=15299270", "revision": 15299270, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Sacred_oil?oldid=15214951", "revision": 15214951, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Pyre_logs?oldid=15185388", "revision": 15185388, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Serum_207?oldid=15183419", "revision": 15183419, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Serum_208?oldid=15214952", "revision": 15214952, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Razmire_Keelgan?oldid=15115043", "revision": 15115043, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ulsquire_Shauncy?oldid=14879578", "revision": 14879578, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Shade_key?oldid=15285193", "revision": 15285193, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Shade_Catacombs?oldid=15233566", "revision": 15233566, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Urium_Shade?oldid=15200239", "revision": 15200239, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Dampe?oldid=14826321", "revision": 14826321, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Bronze_Chest_(red)?oldid=14980834", "revision": 14980834, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Steel_Chest_(red)?oldid=14980845", "revision": 14980845, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Black_Chest_(red)?oldid=14980856", "revision": 14980856, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Silver_Chest_(red)?oldid=14980868", "revision": 14980868, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "shadeshadow_level1..6 and shade_level1..6", "shade_heaven",
            "shades_undead_zealot_1", "afflicted male/female variants",
            "mortton_razmire/mortton_ulsquire", "shades_coffin_keeper (Dampe)",
        ],
        "item_gamevals": [
            "serum2071..4/serum2081..4 and oliveoil/sacred_oil dose families",
            "thirteen pyre-log tiers from normal through rosewood",
            "shade_bones1..6 (Loar/Phrin/Riyl/Asyn/Fiyr/Urium)",
            "25 bronze/steel/black/silver/gold Shade keys",
            "five locks, broken/bronze/steel/black/silver/gold wearable coffins",
            "swamppaste/flamtaer_hammer/flamtaer_bag/fine_cloth/damned_amulet",
            "tree-wizard/bloody books and swampbark/bloodbark runescrolls",
            "easy/medium/hard/elite clues, zealot robes and keyed-chest pools",
        ],
        "loc_gamevals": [
            "Flamtaer temple wall stages, sacred-fire altar and funeral pyres",
            "all thirteen pyre-log/remains states", "Shade Catacombs entrance and four tier doors",
            "25 keyed chests", "Altar of the Damned", "Razmire builder/general shops",
        ],
        "trigger_handlers": [
            "ai_queue2/3 and ai_opplayer2:_shade rise/combat/remains/Loar credit",
            "oploc1/3/u:temple repair, sanctity and fire altar; temple attack/drain timers",
            "opheldu/oploc1/u/4:oil, pyre logs, remains and ignition",
            "oploc1/u:catacomb entrance, doors, 25 chests and Altar of the Damned",
            "opnpc1/3/u:Dampe; opheld1/3/4/5:wearable coffins",
            "quest dialogue, Serum 207/208 afflicted cures, shops and completion queue",
        ],
        "loot_contract": "Only directly credited Loar shade deaths advance the five-kill quest stage; every shade tier drops its matching remains and can apply its current 1/20 Strength drain. Sacred oil consumes the tier-correct two-to-four doses across all thirteen current pyre-log types, applies Pyromancer creation XP, and funeral burning applies exact Firemaking/Prayer XP including current Morytania diary multipliers and modern camphor/ironwood/rosewood corrections. Loar through Urium rewards use their exact coin bands and two key-band probabilities, with five-minute owner-private pedestal rewards. The completed quest gates non-consuming catacomb tier doors and 25 exact-key chests; opening is inventory-atomic, consumes one key, rolls tier locks and splitbark books/scrolls in order, gives steel-or-better bag-or-paste tertiaries, standard treasure/clues/zealot uniques and coins, and can release an undead zealot. Bleached bones each charge one later full Prayer restore at the Altar of the Damned. Dampe repairs one broken wearable coffin with a selected lock; capacities are 3/8/14/20/28 remains and Empty withdraws highest tier first.",
        "test_ids": [
            "quest-combat-contract:mortton-quest", "quest-combat-contract:mortton-shades",
            "quest-combat-contract:mortton-temple", "quest-combat-contract:mortton-pyres",
            "quest-combat-contract:mortton-catacombs", "quest-combat-contract:mortton-coffin",
        ],
        "known_gaps": [
            "Current chest table modifiers for already-owned clue scrolls and the ring-of-wealth reshaping of standard (non-lock/non-scroll) rewards are not yet represented; the current no-clue base pools and ring-adjusted lock/scroll denominators are implemented.",
            "The public Wiki labels the undead-zealot chest release only as rare and does not publish an exact rate; the implementation retains the audited cache-era 1/128 roll.",
            "The runtime represents shade attacks on temple walls with the same structural loss/degradation rules on a player timer because it lacks the reference NPC-to-location hunt mode.",
            "A real-client multiplayer, all pyre tiers, full-inventory chest, coffin equip/configure, altar charge, temple destruction, death and relog smoke is still pending.",
        ],
    },
    "The Fremennik Trials": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/The_Fremennik_Trials?oldid=15292303", "revision": 15292303, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/The_Fremennik_Trials/Quick_guide?oldid=15290109", "revision": 15290109, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:The_Fremennik_Trials?oldid=15263294", "revision": 15263294, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Koschei_the_deathless?oldid=15293789", "revision": 15293789, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/The_Draugen?oldid=15215949", "revision": 15215949, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Hunters%27_talisman?oldid=15184168", "revision": 15184168, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Fremennik_blade?oldid=15183020", "revision": 15183020, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Thorvald_the_Warrior?oldid=15239757", "revision": 15239757, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Sigli_the_Huntsman?oldid=15136941", "revision": 15136941, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Lanzig?oldid=15199225", "revision": 15199225, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Borrokar?oldid=15199667", "revision": 15199667, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Lensa?oldid=15199668", "revision": 15199668, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Freidir?oldid=15199666", "revision": 15199666, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Lyre?oldid=15183561", "revision": 15183561, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "viking_enemy1..4 (Koschei forms)", "viking_draugen/viking_draugen_safe",
            "viking_sigli/viking_thorvald/viking_peer",
            "viking_man2/viking_man3/viking_man4/viking_woman2 (Freidir/Borrokar/Lanzig/Lensa)",
        ],
        "item_gamevals": [
            "viking_draugen_talisman_uncharged/viking_draugen_talisman", "viking_sword",
            "viking_unstrung_lyre", "documented Koschei allowed/forbidden loadout categories",
            "exact 512-slot citizen primary table and 1/128 easy clue tertiary",
        ],
        "loc_gamevals": [
            "viking_warrior_ladder_down/viking_warrior_ladder_up",
            "twelve moving Draugen province anchors", "Koschei basement arena and upstairs return",
        ],
        "trigger_handlers": [
            "oploc1:Koschei ladders; opnpc2/ai_queue3:four Koschei forms",
            "timer:Koschei monitor/phase timeout; player death/logout/drop cleanup hooks",
            "opheld1/5:hunter's talismans; timer:Draugen move/timeout; ai_queue3:Draugen",
            "opnpc1:Sigli/Thorvald/Peer; opheld2:Fremennik blade",
            "ai_queue3:Freidir/Borrokar/Lanzig/Lensa exact citizen table",
        ],
        "loot_contract": "Koschei and the Draugen have null ordinary death drops and require direct credited damage. Koschei's first three forms advance immediately; form four drains Prayer and either a safe one-HP loss or victory earns the vote, while victory alone awards a Fremennik blade with private-ground full-inventory fallback. The Draugen converts exactly one uncharged hunter's talisman to charged proof, also with private-ground fallback, and Sigli consumes it. Freidir, Borrokar, Lanzig and Lensa retain public ordinary combat semantics and share an exact 512-slot primary table with 100% bones, 30/512 unstrung lyre, quest-gated Fremennik equipment and blamish oil, herbs, nature-talisman gem table and ten nothing slots, plus an independent 1/128 easy clue.",
        "test_ids": [
            "quest-combat-contract:fremennik-koschei", "quest-combat-contract:fremennik-draugen",
            "quest-combat-contract:fremennik-loadout", "quest-combat-contract:fremennik-citizen-loot",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The owner-private actors occupy the shared classic map squares rather than cloned modern instances.",
            "Some exact camera and audio choreography is reduced to messages and cache-era animation.",
            "A real-client two-player ownership, loadout matrix, safe-death, exit/teleport/logout/timeout cleanup, moving-target, inventory-full, public-loot, death and relog smoke is still pending.",
        ],
    },
    "Horror from the Deep": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Horror_from_the_Deep?oldid=15294310", "revision": 15294310, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Horror_from_the_Deep/Quick_guide?oldid=15080801", "revision": 15080801, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Horror_from_the_Deep?oldid=15263295", "revision": 15263295, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Dagannoth_(Horror_from_the_Deep)?oldid=15274475", "revision": 15274475, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Dagannoth_mother?oldid=15199457", "revision": 15199457, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Rusty_casket?oldid=15254124", "revision": 15254124, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Damaged_book?oldid=15174921", "revision": 15174921, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Jossik?oldid=15196239", "revision": 15196239, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Lighthouse_key?oldid=15183873", "revision": 15183873, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Holy_book?oldid=15300898", "revision": 15300898, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Unholy_book?oldid=15300896", "revision": 15300896, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Book_of_balance?oldid=15294284", "revision": 15294284, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "horror_dagannoth_jr1..jr4", "horror_dagganoth_aira/airb/airc",
            "horror_dagganoth_air/water/melee/earth/fire/ranged",
            "horror_lighthousekeeeper_injured/horror_lighthousekeeeper_well",
            "gunnjorn and larrissa",
        ],
        "item_gamevals": [
            "woodplank/nails/hammer", "horror_key", "swamp_tar/molten_glass/tinderbox",
            "airrune/waterrune/earthrune/firerune", "all sword and arrow categories",
            "horror_diary1/horror_diary2/horror_diary3", "bones/horror_casket",
            "unfinished_saradominbook/unfinished_zamorakbook/unfinished_guthixbook",
            "saradominbook_complete/zamorakbook_complete/guthixbook_complete",
        ],
        "loc_gamevals": [
            "horror_broken_bridge_left_spot/horror_broken_bridge_right_spot",
            "horror_lighthouse_doorway", "horror_lighthouse_cog_broken/_noop",
            "horror_bookcase", "horror_mid_left_door/horror_mid_right_door",
            "basement spawn 0_39_72_22_34 and exit 0_39_56_13_54",
        ],
        "trigger_handlers": [
            "oploc1/u:bridge, lighthouse door/lamp, bookcase and six-offering wall",
            "opnpc1:Gunnjorn key, injured Jossik encounter, well Jossik reward",
            "opnpc2/ai_queue3/4/ai_timer:juvenile and Mother phases",
            "player_hit_npc_prepare and player Magic elemental latch:Mother colour damage gate",
            "timer/player death/logout:owner-private timeout and cleanup",
            "opnpc4:Jossik lost-book recovery; opobj3:rusty-casket pickup block",
        ],
        "loot_contract": "The juvenile has null ordinary loot and a credited or recoil-only death advances directly to the Mother. The Mother also has null generic death loot: her credited death adds ordinary bones privately, grants the fragile rusty casket to inventory when possible (otherwise Jossik recovers it), completes the quest, exits the basement and awards 4,662.5 Magic XP. Jossik consumes/recover-opens the casket into one persistent choice among the three unfinished god books and replaces only the chosen book when neither its damaged nor completed form exists in inventory, bank or worn slots. A ground casket cannot be picked up.",
        "test_ids": [
            "quest-combat-contract:horror-route", "quest-combat-contract:horror-junior",
            "quest-combat-contract:horror-mother", "quest-combat-contract:horror-casket-books",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The owner-private actors occupy the shared classic lighthouse map rather than cloned modern instances.",
            "Exact modern camera and audio choreography is reduced to messages and cache-era animation.",
            "The global Twinflame staff secondary-hit mechanic is absent and is deferred to that staff's implementation; standard matching Earth spells work.",
            "A real-client two-player ownership, bridge/door/wall, all-colour/style, recoil-only, timeout/death/logout, full-inventory casket, book-loss and relog smoke is still pending.",
        ],
    },
    "Monkey Madness I": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Monkey_Madness_I?oldid=15302455", "revision": 15302455, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Monkey_Madness_I/Quick_guide?oldid=15257376", "revision": 15257376, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Monkey_Madness_I?oldid=15263300", "revision": 15263300, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Jungle_Demon?oldid=15199289", "revision": 15199289, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/10th_squad_sigil?oldid=15182884", "revision": 15182884, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Garkor?oldid=15238361", "revision": 15238361, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Waymottin?oldid=15238401", "revision": 15238401, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Zooknock?oldid=15286805", "revision": 15286805, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "mm_demon", "mm_garkor_final_battle/mm_zooknock_final_battle",
            "mm_waymottin_final_battle/mm_bunkwicket_final_battle/mm_bunkdo_final_battle",
            "mm_carado_final_battle/mm_lumo_final_battle/mm_karam_final_battle", "mm_bonzara",
            "mm_garkor/mm_zooknock/mm_waymottin",
        ],
        "item_gamevals": [
            "mm_monkey_dentures/mm_monkey_amulet_mould/gold_bar/mm_enchanted_gold_bar",
            "mm_monkey_talisman and seven valid monkey-remains/greegree variants",
            "mm_monkey_greegree_for_normal_monkey", "mm_sigil", "malicious_ashes",
            "coins/diamond and optional Daero combat-XP split",
        ],
        "loc_gamevals": [
            "Ape Atoll Dungeon Zooknock route", "Garkor/Awowogei Chapter 4 route",
            "banana-plantation arena 1_42_143", "arena exit near 0_42_43_24_32",
        ],
        "trigger_handlers": [
            "opnpcu/opnpc1:Zooknock amulet, talisman and greegree route",
            "opnpc1:Garkor induction and Waymottin sigil replacement",
            "opheld2:mm_sigil stage/Wilderness/confirmation gate and private squad spawn",
            "ai_timer/ai_applayer2/ai_queue3:mm_demon allied damage, four waves, melee and credit",
            "timer/player death/logout:arena leave and owner-private cleanup",
            "opnpc1:Garkor/Zooknock/Bonzara post-kill exit; Narnode reward and Daero training",
        ],
        "loot_contract": "The owner-private Jungle Demon has no generic table. A credited player, recoil, Retribution or simultaneous-death finish advances the quest and creates exactly one owner-private malicious ashes; allied damage can reduce it to ten Hitpoints but a would-be allied kill restores 21-42 HP (12-25% of 170) and cannot progress the quest. Narnode separately preflights three inventory slots, then grants 10,000 coins and three diamonds before the 3-QP completion. Daero's optional permanent training grants 35,000 XP to either Strength/Hitpoints or Attack/Defence and 20,000 XP to the other pair.",
        "test_ids": [
            "quest-combat-contract:mm1-route-sigil", "quest-combat-contract:mm1-demon",
            "quest-combat-contract:mm1-allied-credit", "quest-combat-contract:mm1-reward",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The owner-private actors occupy the shared classic banana-plantation map rather than a cloned modern instance.",
            "Squad contribution is represented by exact-timed hitsplats rather than autonomous NPC-to-NPC pathing.",
            "Exact camera, screen-shake and audio choreography is reduced to cache-era messages and delays.",
            "A real-client two-player ownership, sigil-gate matrix, ally heal-back, recoil/Retribution simultaneous death, wave/style, safespot, full-inventory, death, relog and exit smoke is still pending.",
        ],
    },
    "Haunted Mine": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Haunted_Mine?oldid=15292305", "revision": 15292305, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Haunted_Mine/Quick_guide?oldid=14834641", "revision": 14834641, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Haunted_Mine?oldid=15263301", "revision": 15263301, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Treus_Dayth?oldid=15234627", "revision": 15234627, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Crystal-mine_key?oldid=15183506", "revision": 15183506, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Glowing_fungus?oldid=15183507", "revision": 15183507, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Salve_shard?oldid=15183505", "revision": 15183505, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Zealot%27s_key?oldid=15183508", "revision": 15183508, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Salve_amulet?oldid=15241628", "revision": 15241628, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "saradominist_zealot", "hauntedmine_cheeky_ghost",
            "hauntedmine_boss_key/hauntedmine_boss_ghost",
            "hauntedmine_boss_cart/crane_posessed_mine",
        ],
        "item_gamevals": [
            "hauntedmine_lift_key", "glowing_fungus/ashes", "chisel",
            "hauntedmine_reward_key", "crystalshard_necklace_unstrung",
            "ball_of_wool/crystalshard_necklace", "trail_clue_beginner",
        ],
        "loc_gamevals": [
            "both hauntedmine_back_entrance routes and every authored ladder",
            "glowing_mushroom/glowing_mushroom2 and hauntedmine_puzzle_cart",
            "hauntedmine_point_lever1..8/hauntedmine_points_info",
            "hauntedmine_lift_valve/lift_side_r/lift_flooded",
            "hauntedmine_dark_stairs_top and both hauntedmine_rewarddoor leaves",
            "crystalcorner",
        ],
        "trigger_handlers": [
            "opnpc1/opnpc3:Zealot quest gate and key issue/full-inventory fallback",
            "oploc1/timer/opheld5:fungus cart, outside/drop decay and valve ghost race",
            "opnpc1/2,ai_timer,ai_queue3:owner-private Treus start/combat/credit",
            "timer:HP-scaled cart/crane hazards and room/death/logout cleanup",
            "opnpc1:crystal-mine key reclaim; oploc1:reward doors and repeatable shard",
            "opheldu:ball of wool/salve shard stringing",
        ],
        "loot_contract": "Treus Dayth has no primary table. A credited owner kill stops the machinery and independently rolls one owner-private beginner clue at 1/90. The crystal-mine key is a separate post-kill NPC interaction with inventory-full private-ground fallback and inventory/bank duplicate recovery. A chisel/current 35 Crafting outcrop interaction separately awards the first and all replacement salve shards; the key is retained, and ball of wool converts one shard to a salve amulet with no Crafting XP.",
        "test_ids": [
            "quest-combat-contract:haunted-mine-route", "quest-combat-contract:haunted-mine-dayth",
            "quest-combat-contract:haunted-mine-loot-recovery", "quest-combat-contract:haunted-mine-salve",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The owner-private actor occupies the shared classic boss room rather than a cloned modern instance.",
            "Wiki and Quest Helper publish the valve race but not its exact duration; the implementation calibrates it to 30 ticks.",
            "Wrong cart configurations preserve documented sink/return semantics without reconstructing every hidden topology/cutscene, and machinery/projectile presentation is reduced.",
            "The global Lunar String Jewellery spell is absent; ordinary ball-of-wool salve stringing is implemented.",
            "A real-client multiplayer, full route/lever, fungus lifecycle, valve race, machinery tile, combat/prayer, clue, full-inventory, death/logout/relog and post-quest shortcut smoke is pending.",
        ],
    },
    "Troll Romance": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Troll_Romance?oldid=15292383", "revision": 15292383, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Troll_Romance/Quick_guide?oldid=14845426", "revision": 14845426, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Troll_Romance?oldid=15263302", "revision": 15263302, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Arrg?oldid=15215810", "revision": 15215810, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Sled?oldid=15239793", "revision": 15239793, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Wax?oldid=15185234", "revision": 15185234, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Trollweiss?oldid=15184737", "revision": 15184737, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ug?oldid=15095285", "revision": 15095285, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Aga?oldid=15109118", "revision": 15109118, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "trollromance_ug/trollromance_aga/trollromance_arrg",
            "trollromance_arrg_attackable owner-private combat form",
            "death_smithy/death_sherpa",
        ],
        "item_gamevals": [
            "maple_logs or yew_logs/iron_bar/rope",
            "bucket_wax/swamp_tar/cake_tin/trollromance_wax",
            "trollromance_toboggon/trollromance_toboggon_waxed",
            "trollromance_rare_flower",
            "bones and exact Arrg 128-slot/ensouled-head loot",
            "uncut_diamond/uncut_ruby/uncut_emerald reward",
        ],
        "loc_gamevals": [
            "Tenzing and Dunstan route", "Trollweiss piste barriers and rare flowers",
            "shared Troll arena 0_45_56", "Ug/Aga/Arrg stronghold rooms",
        ],
        "trigger_handlers": [
            "opnpc1:Tenzing/Dunstan/Ug/Aga/Arrg dialogue and atomic handoffs",
            "opheldu/oploc1:sled wax, boostable level-28 slide and Trollweiss pickup",
            "opheld2/timer:worn sled and full-inventory teleport-loss policy",
            "opnpc2/ai_timer/ai_queue3:owner-private dual-style Arrg and credited death",
            "timer/player death/logout/login:arena and sled lifecycle cleanup",
            "opnpc1:seven-slot gem reward and quest completion",
        ],
        "loot_contract": "A credited owner-private Arrg death always creates private regular bones and exactly one private roll from the current 128-slot weapons, runes, herb, seed, material, coin or nature-talisman gem table, plus an independent private ensouled troll head at 1/45. The conditional Rag and Bone Man II troll bone at 1/4 awaits that quest's wishlist state. Ug separately preflights seven inventory spaces, then atomically grants one uncut diamond, two uncut rubies and four uncut emeralds with 8,000 Agility XP, 4,000 Strength XP and 2 QP.",
        "test_ids": [
            "quest-combat-contract:troll-romance-route",
            "quest-combat-contract:troll-romance-arrg",
            "quest-combat-contract:troll-romance-loot",
            "quest-combat-contract:troll-romance-reward",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The owner-private actor occupies the shared classic Troll arena rather than a cloned modern instance.",
            "The Wiki publishes neither Arrg's prayer-penetration chance nor the hidden accuracy modifier behind its significantly more accurate +0-bonus ranged attack; the implementation calibrates these to 1/5 penetration and a fourfold ranged roll.",
            "Rag and Bone Man II's conditional 1/4 troll bone remains deferred until that quest exposes its wishlist state.",
            "The piste uses cache-era teleports/messages rather than the complete camera and sled-crash choreography.",
            "A real-client multiplayer, all route/material orders, sled loss/rebuild, safespot, style/prayer/poison/venom, every loot band, timeout/death/logout, seven-slot reward and relog smoke is pending.",
        ],
    },
    "In Search of the Myreque": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/In_Search_of_the_Myreque?oldid=15292283", "revision": 15292283, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/In_Search_of_the_Myreque/Quick_guide?oldid=14479041", "revision": 14479041, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:In_Search_of_the_Myreque?oldid=15286926", "revision": 15286926, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Skeleton_Hellhound?oldid=15199509", "revision": 15199509, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Cyreg_Paddlehorn?oldid=15013176", "revision": 15013176, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Swamp_Boaty?oldid=15263062", "revision": 15263062, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "multi_vanstrom_stranger_entity/route_vanstrom_klause_sitting/canafis_stranger",
            "route_cyreg_paddlehorn/route_curpile_fyod",
            "route_veliaf_hurtz_parent and all five Myreque member wrappers",
            "skeleton_hellhound owner-private combat actor",
        ],
        "item_gamevals": [
            "steel_longsword/steel_sword x2/steel_mace/steel_warhammer/steel_dagger",
            "druid_pouch with five charges", "woodplank x6/hammer/nails x225",
            "coins x10 or worn ring_of_charos_unlocked",
        ],
        "loc_gamevals": [
            "route_rowboat_mortton/route_rowboat_hollows",
            "spooky_tree_base_forbridge/swamp_bridge1",
            "freedomfighterentrance and underground-door pairs",
            "route_cavewalltunnel/thrttavernbasementfalsewall/thrttavernbasementladder/thrt_tavern_trap_door",
        ],
        "trigger_handlers": [
            "opnpc1:Vanstrom start and Cyreg four-answer persuasion/material handoff",
            "oploc1/2:Charos-aware boat fares, bridge repair and hideout traversal",
            "opnpc1:three Curpile questions and five-member introduction bitfield",
            "opnpc1:atomic six-weapon Veliaf handoff and stage-81 survivor transforms",
            "opnpc2/ai_queue3/timer:owner-private no-drop Skeleton Hellhound and retry lifecycle",
            "player death/logout/login:outside-room grave, cleanup and Vanstrom/Stranger reconciliation",
            "oploc1/opnpc1:secret wall, mandatory Canifis trapdoor exit and exact XP completion",
        ],
        "loot_contract": "The owner-private level-97 Skeleton Hellhound drops nothing: no bones, rubies or generic hellhound table. A credited owner kill only advances the quest to Veliaf's escape dialogue. The final Stranger interaction separately grants 600 XP each in Attack, Defence, Strength, Hitpoints and Crafting plus 2 QP and route/fairy-ring access.",
        "test_ids": [
            "quest-combat-contract:myreque-route", "quest-combat-contract:myreque-hound",
            "quest-combat-contract:myreque-no-loot", "quest-combat-contract:myreque-death-retry",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The owner-private actor occupies the shared classic chamber rather than a cloned modern instance.",
            "Vanstrom's mist, two Blood Burst kills and disappearance use cache-stage transforms plus messages rather than the complete camera, animation and audio cutscene.",
            "The rev-239 map places a visually complete rope bridge instead of its three native unplaced rung wrappers; three separate plank-and-75-nail repairs are enforced before crossing, but per-rung visual replacement is unavailable on this map.",
            "The runtime exposes no script-readable demon attribute; the Hellhound is deliberately not marked undead, but global demonbane classification remains unavailable.",
            "A real-client multiplayer, every Cyreg/question/material/fare branch, bridge visual, safespot, poison/venom, leave/reentry, death/grave, logout/relog and reward smoke is pending.",
        ],
    },
    "Creature of Fenkenstrain": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Creature_of_Fenkenstrain?oldid=15292324", "revision": 15292324, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Creature_of_Fenkenstrain/Quick_guide?oldid=15087904", "revision": 15087904, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Creature_of_Fenkenstrain?oldid=15263305", "revision": 15263305, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Experiment?oldid=15199186", "revision": 15199186, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Cavern_key?oldid=15184609", "revision": 15184609, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Chest_(Creature_of_Fenkenstrain)?oldid=15201910", "revision": 15201910, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Grave_(Haunted_Woods)?oldid=14684724", "revision": 14684724, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "fenk_experiment_1 level-51 key-bearing wolf-woman",
            "fenk_experiment_2/fenk_experiment_3 level-25 training variants",
            "fenk_fenkenstrain and fenk_gardener dialogue actors",
        ],
        "item_gamevals": [
            "fenk_marble_amulet/fenk_obsidian_amulet/fenk_star_amulet",
            "fenk_mausoleum_key and ordinary bones",
            "fenk_head_empty/fenk_brain/fenk_head_full/fenk_torso/fenk_arms/fenk_legs",
            "fenk_shed_key/fenk_brush0..3/fenk_cane/bronzecraftwire",
            "fenk_lightning_mould/silver_bar/fenk_conductor",
        ],
        "loc_gamevals": [
            "fenk_coffin and exact 3577,9927 eastern cave landing",
            "fenk_mausoleum_door and open-by-default fenk_chest_open",
            "Ed Lestwit grave plus the exact Rolomere/Rolovanne/Rologray mausoleum graves",
            "fenk_shed_door/cupboard/cane pile and west upstairs fireplace",
            "all _smithing_furnace locations for conductor casting",
        ],
        "trigger_handlers": [
            "oplocu/oploc1:star memorial and full authored east-to-northwest cave route",
            "opnpc2/apnpc2/ai_queue3:level-51 attack gate, credited private key and ordinary bones",
            "oploc1/oplocu:consumed cavern key, permanent gate-use latch and re-entry",
            "oploc1/2:open/close/search chest with inventory-only replacement checks",
            "oploc1/2:coordinate-specific head, torso, arms and legs graves",
            "opnpc1/oploc1/opheldu:shed key, door, brush, three canes and three bronze wires",
            "oplocu/_smithing_furnace:west-chimney mould and boostable level-20 silver conductor",
        ],
        "loot_contract": "A credited level-51 Experiment kill during the quest always produces ordinary bones through the standard death handler and one owner-private cavern key, and cannot be initiated while a key is in the backpack, after the gate has been used, before hiring or after completion. Level-25 Experiments produce only their ordinary bones here. The open cave chest independently supplies an inventory-only cavern-key replacement during or after the quest; a banked key does not suppress it. No Experiment death advances the quest stage.",
        "test_ids": [
            "quest-combat-contract:fenkenstrain-route",
            "quest-combat-contract:fenkenstrain-experiment",
            "quest-combat-contract:fenkenstrain-key-recovery",
            "quest-combat-contract:fenkenstrain-parts-conductor",
        ],
        "known_gaps": [
            "Rag and Bone Man II's conditional 1/4 Experiment bone remains deferred until that quest exposes its wishlist state; it is deliberately not emitted unconditionally.",
            "The gardener identifies the exact Ed Lestwit grave but does not yet physically follow the player and provide live directional dialogue through the Haunted Woods.",
            "The final lightning/tower sequence uses the classic cache map and reduced messages rather than the complete camera, animation and audio choreography.",
            "A real-client level-51 attack/key/gate/retry, level-25 training, chest/bank/full-inventory, all grave, shed/brush/wire, furnace/boost and quest-finale smoke is pending.",
        ],
    },
    "Roving Elves": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Roving_Elves?oldid=15292285", "revision": 15292285, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Roving_Elves/Quick_guide?oldid=14458305", "revision": 14458305, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Roving_Elves?oldid=15272093", "revision": 15272093, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Moss_Guardian?oldid=15199663", "revision": 15199663, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Glarial%27s_Tomb?oldid=15267068", "revision": 15267068, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Consecration_seed?oldid=15184516", "revision": 15184516, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Crystal_growth?oldid=15201942", "revision": 15201942, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Crystal_bow?oldid=15292719", "revision": 15292719, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Crystal_shield?oldid=15291690", "revision": 15291690, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Islwyn?oldid=15199037", "revision": 15199037, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Eluned?oldid=15267892", "revision": 15267892, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Glarial%27s_pebble?oldid=15183293", "revision": 15183293, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Key_(Waterfall_Dungeon)?oldid=15185894", "revision": 15185894, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ilfeen?oldid=15292643", "revision": 15292643, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "roving_mossgiant: three shared-world level-84 Moss Guardians",
            "roving_bowyer/roving_islwyn_2ops: cache-authored Islwyn transform family",
            "roving_female_woodelf/eluned_prif: cache-authored Eluned transform family",
            "golrie: Glarial-pebble recovery actor from Waterfall Quest",
            "ilfeen: separate crystal recharge actor",
        ],
        "item_gamevals": [
            "glarials_pebble_waterfall_quest/baxtorian_key_waterfall_quest",
            "rope/spade and the post-Waterfall dungeon route",
            "roving_old_consecration_seed/roving_new_consecration_seed",
            "crystal_bow/crystal_shield with charge metadata",
            "coins, big_bones, dorgesh_construction_bone and dorgesh_construction_bone_curved",
        ],
        "loc_gamevals": [
            "glarials_tombstone_waterfall_quest and Glarial's Tomb entrance",
            "Waterfall raft/rope/ledge/door route and baxtorian_key crate",
            "baxtorian_chalice_waterfall_quest at exact 2603,9910",
            "roving_crystal_growth ritual loc",
            "cache-authored southwest Isafdar Islwyn/Eluned camp",
        ],
        "trigger_handlers": [
            "oplocu/procs:pebble entry with complete inventory-and-worn tomb restriction matrix",
            "opnpc2/apnpc2/ai_opplayer2:rechecked restriction plus crush-to-Magic fallback attack",
            "ai_queue3:credited private seed and independent bone tertiaries",
            "opnpc1:Eluned ritual, enchantment and enchanted-seed replacement",
            "oploc1/opheld1:post-Waterfall door route, spade planting and crystal growth",
            "opnpc1/queue/opnpc3:Islwyn reward choice, completion and fixed-price purchases",
            "levelrequire:completion gate for modern and legacy crystal bow/shield forms",
        ],
        "loot_contract": "Each shared-world Moss Guardian always drops big bones through its ordinary death handler. During the eligible Roving Elves stages, a credited player missing both seed forms additionally receives one owner-private old consecration seed and advances to the obtained stage. Long bones roll independently at 1/400 and curved bones at 1/5012.5; no generic moss-giant primary table applies. Islwyn separately awards a chosen crystal bow or shield with 500 charges, then sells full 2,500-charge replacements for 900,000 or 750,000 coins after completion.",
        "test_ids": [
            "quest-combat-contract:roving-tomb",
            "quest-combat-contract:roving-moss-guardian",
            "quest-combat-contract:roving-seed-ritual",
            "quest-combat-contract:roving-crystal-reward",
        ],
        "known_gaps": [
            "Dynamic locations are world-scoped in this revision, so crystal growth is shown for a 20-tick ritual window instead of persisting independently for each player.",
            "The cache-authored southwest Isafdar pair is retained; modern five-minute movement between the two clearings is not simulated.",
            "Nightmare Zone availability is advertised by the reward interface but the encounter is not yet registered with a complete Nightmare Zone implementation.",
            "Rune-pouch contents are not represented by this runtime, so an admitted rune pouch is necessarily treated as empty.",
            "The seed ritual uses cache-era messages and a growth location rather than the complete modern camera, animation and audio sequence.",
            "A real-client loadout matrix, multiplayer credit, seed loss/full-inventory, complete Waterfall traversal, reward purchase and charge/degradation smoke is pending.",
        ],
    },
    "Ghosts Ahoy": {
        "source_audits": [
            {"url": "https://oldschool.runescape.wiki/w/Ghosts_Ahoy?oldid=15297463", "revision": 15297463, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ghosts_Ahoy/Quick_guide?oldid=15297469", "revision": 15297469, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Transcript:Ghosts_Ahoy?oldid=15297814", "revision": 15297814, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Giant_lobster_(Ghosts_Ahoy)?oldid=15272821", "revision": 15272821, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Model_ship?oldid=15185508", "revision": 15185508, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Mast_(Ghosts_Ahoy)?oldid=14888479", "revision": 14888479, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Treasure_map?oldid=15297464", "revision": 15297464, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Book_of_Haricanto?oldid=15293864", "revision": 15293864, "retrieved": "2026-08-17"},
            {"url": "https://oldschool.runescape.wiki/w/Ghost_captain?oldid=14768954", "revision": 14768954, "retrieved": "2026-08-17"},
        ],
        "npc_gamevals": [
            "giant_lobster: owner-private level-32 quest lobster",
            "ahoy_oldman/ahoy_ghost_captain_1: ship-key and Dragontooth route",
            "ahoy_velorina/ahoy_necrovarus/ahoy_crone: main enchantment route",
            "ahoy_akharanu_multi/ahoy_robin: translation-manual route",
            "protester_ghostspeak_multi/protester_standardspeak_multi/ahoy_ghost_villager: petition route",
        ],
        "item_gamevals": [
            "ahoy_toy_boat/ahoy_toy_boat_repaired plus all six dye colours",
            "ahoy_chest_key and three distinct ahoy_map_scrap items",
            "ahoy_map_complete/spade/ahoy_book_of_haricanto",
            "ecto-tokens, activated ring of charos and both ghostspeak amulets",
            "seaweed/arceuus_corpse_scorpion/trail_clue_beginner lobster drops",
            "nettle-tea, signed-bow, bedsheet, petition, bone-key, robes and manual route items",
        ],
        "loc_gamevals": [
            "ahoy_mast at 3619,3543 with high/low wind overlay",
            "ahoy_chest_locked at 3619,3545 for scrap 1",
            "ahoy_rock_invisible route and north chest at 3606,3564 for scrap 2",
            "lower trigger chest at 3618,3542 and guarded chest at 3619,3542 for scrap 3",
            "Dragontooth dig tile at 3803,3530",
        ],
        "trigger_handlers": [
            "oploc1/timer/opheldu:per-player six-colour three-part mast and ship-paint puzzle",
            "oploc1/oplocu:exact locked, rock and lower wreck-chest routing",
            "oploc1:boostable rock checks, 5% run drain, success XP and advancing failure",
            "opnpc2/apnpc2/ai_queue3/timer:owner-private timed credited lobster lifecycle",
            "opheldu/opnpc1/spade proc:three-scrap map, captain fare and exact book dig",
            "player death/logout:owned lobster cleanup and retry",
        ],
        "loot_contract": "The owner-private Giant Lobster always drops one private seaweed, independently rolls an ensouled scorpion head at 1/25 and a beginner clue at the disclosed cache-era 1/128 calibration, and never invokes a generic lobster table. The third quest map scrap is recovered from the adjacent guarded chest only after a credited owner kill; it is not corpse loot. An unfinished lobster despawns after 200 ticks and restarts at full Hitpoints.",
        "test_ids": [
            "quest-combat-contract:ghosts-ahoy-flag",
            "quest-combat-contract:ghosts-ahoy-three-chests",
            "quest-combat-contract:ghosts-ahoy-lobster",
            "quest-combat-contract:ghosts-ahoy-map-captain",
            "mock230-selftest:npc-owner-visibility",
        ],
        "known_gaps": [
            "The Wiki labels the beginner clue merely Rare; 1/128 is an explicit cache-era calibration rather than a published exact denominator.",
            "Rune-Draw is a deterministic narrated win because this runtime has no Rune-Draw interface or game system.",
            "Ghost-villager support/refusal/bribe randomness and same-villager suppression are reduced to deterministic signatures.",
            "The shared classic wreck and Dragontooth maps are retained; modern camera and audio choreography is reduced.",
            "A real-client multiplayer, every flag/dye, rock result, timeout, safespot, poison/venom, recoil, loot, full-inventory, death/logout and relog smoke is pending.",
        ],
    },
}

# The two inventory rows share one physical encounter and one continuous item/
# machinery implementation. Preserve separate manifest units while binding both
# to the same pinned evidence contract.
AUDITED_OVERRIDES["Elemental Workshop II"] = dict(
    AUDITED_OVERRIDES["Elemental Workshop I"]
)

ROW = re.compile(
    r"^\| \[(?P<name>[^]]+)]\((?P<url>https://oldschool\.runescape\.wiki/w/[^)]+)\)"
    r" \| (?P<encounters>.+) \|$"
)


def normalize_markdown(value: str) -> str:
    return re.sub(r"\s+", " ", value.strip())


def slug(value: str) -> str:
    result = re.sub(r"[^a-z0-9]+", "-", value.casefold()).strip("-")
    if not result:
        raise ValueError(f"cannot create id for {value!r}")
    return result


def inventory() -> list[dict[str, object]]:
    section: str | None = None
    rows: list[dict[str, object]] = []
    for line_number, raw in enumerate(PLAN.read_text().splitlines(), 1):
        line = raw.strip()
        if line == "## 2. Combat-bearing quest inventory":
            section = "quest"
            continue
        if line == "### Miniquests with combat encounters":
            section = "miniquest"
            continue
        if line.startswith("## 3."):
            section = None
        if section is None:
            continue
        match = ROW.fullmatch(line)
        if not match:
            continue
        name = match.group("name")
        encounter = normalize_markdown(match.group("encounters"))
        blocked = name in POST_REVISION_239
        status = "blocked-cache-version" if blocked else "audit-pending"
        if name in AUDITED_OVERRIDES:
            status = "implementation-in-progress"
        row: dict[str, object] = {
            "id": f"{section}-{slug(name)}",
            "kind": section,
            "name": name,
            "wiki_url": match.group("url"),
            "encounter_summary": encounter,
            "plan_line": line_number,
            "cache_scope": "post-revision-239" if blocked else "osrs239",
            "implementation_status": status,
            "source_audits": [],
            "npc_gamevals": [],
            "item_gamevals": [],
            "loc_gamevals": [],
            "trigger_handlers": [],
            "loot_contract": "",
            "test_ids": [],
            "known_gaps": [],
        }
        row.update(AUDITED_OVERRIDES.get(name, {}))
        rows.append(row)
    return rows


def roster_digest(rows: list[dict[str, object]]) -> str:
    fields = [
        "\t".join(
            str(row[key])
            for key in ("kind", "name", "wiki_url", "encounter_summary")
        )
        for row in rows
    ]
    return hashlib.sha256("\n".join(fields).encode()).hexdigest()


def validate(rows: list[dict[str, object]]) -> str:
    counts = {
        kind: sum(row["kind"] == kind for row in rows)
        for kind in ("quest", "miniquest")
    }
    expected = {"quest": EXPECTED_QUESTS, "miniquest": EXPECTED_MINIQUESTS}
    if counts != expected:
        raise ValueError(f"inventory count changed: expected {expected}, found {counts}")

    ids = [str(row["id"]) for row in rows]
    names = [(str(row["kind"]), str(row["name"])) for row in rows]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate manifest id")
    if len(names) != len(set(names)):
        raise ValueError("duplicate inventory row")

    statuses = {
        "audit-pending",
        "implementation-in-progress",
        "verified-modern",
        "blocked-cache-version",
    }
    required_for_verified = (
        "source_audits",
        "npc_gamevals",
        "trigger_handlers",
        "loot_contract",
        "test_ids",
    )
    for row in rows:
        if row["implementation_status"] not in statuses:
            raise ValueError(f"{row['id']}: invalid implementation status")
        if row["implementation_status"] == "blocked-cache-version":
            if row["name"] not in POST_REVISION_239:
                raise ValueError(f"{row['id']}: unexpected cache-version blocker")
        if row["implementation_status"] == "verified-modern":
            missing = [key for key in required_for_verified if not row[key]]
            if missing:
                raise ValueError(f"{row['id']}: verified row missing {missing}")

    digest = roster_digest(rows)
    if EXPECTED_ROSTER_SHA256 != "TO_BE_PINNED" and digest != EXPECTED_ROSTER_SHA256:
        raise ValueError(
            "quest-combat roster changed; audit the pinned Wiki source and update "
            f"EXPECTED_ROSTER_SHA256 intentionally (found {digest})"
        )
    return digest


def payload(rows: list[dict[str, object]], digest: str) -> dict[str, object]:
    return {
        "schema_version": 1,
        "catalogue": {
            "wiki_url": CATALOGUE_URL,
            "wiki_revision": CATALOGUE_REVISION,
            "roster_sha256": digest,
            "quest_count": EXPECTED_QUESTS,
            "miniquest_count": EXPECTED_MINIQUESTS,
        },
        "status_contract": {
            "audit-pending": "Wiki/cache/runtime audit has not been completed.",
            "implementation-in-progress": "Audited implementation is being built but is not accepted.",
            "verified-modern": "All required evidence fields are populated and build/tests pass.",
            "blocked-cache-version": "Required content is newer than the accepted revision-239 cache.",
        },
        "encounters": rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail unless the checked-in manifest exactly matches the plan",
    )
    parser.add_argument(
        "--print-digest",
        action="store_true",
        help="print the normalized roster digest",
    )
    args = parser.parse_args()

    try:
        rows = inventory()
        digest = validate(rows)
    except (OSError, ValueError) as error:
        print(f"quest combat manifest: {error}", file=sys.stderr)
        return 1

    if args.print_digest:
        print(digest)

    rendered = json.dumps(payload(rows, digest), indent=2, ensure_ascii=False) + "\n"
    if args.check:
        if not OUT.exists():
            print(f"quest combat manifest: missing {OUT.relative_to(ROOT)}", file=sys.stderr)
            return 1
        if OUT.read_text() != rendered:
            print(
                "quest combat manifest: generated output is stale; run "
                "python3 tools/generate_quest_combat_manifest.py",
                file=sys.stderr,
            )
            return 1
        print(
            f"quest combat manifest: {len(rows)} inventory units, digest {digest[:12]} (ok)"
        )
        return 0

    OUT.write_text(rendered)
    print(f"wrote {OUT.relative_to(ROOT)} ({len(rows)} inventory units)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
