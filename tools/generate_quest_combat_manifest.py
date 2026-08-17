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
}

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
