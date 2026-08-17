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
EXPECTED_ROSTER_SHA256 = "1d491e0ecfbac72402d41cb22b911131a08fa7a7e92b02cc00674a14a9c41ec2"

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
