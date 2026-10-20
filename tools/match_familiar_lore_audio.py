#!/usr/bin/env python3
"""Match every familiar to the summoning sounds osrs239 already names.

osrs239's `pack/4_soundeffects.pack` names 373 records `lore_*` (ids
4461..4831, plus lore_goad_2..4 at 5346..5348). `lore_` is Jagex's internal
prefix for summoning familiars: RS3 still calls the Spirit terrorbird npc
`lore_terrorbird`. This is the summoning sound set, shipped and named, and
nothing in the port plays it yet beyond `lore_craft_pouch`.

What a name means is checked, not assumed:

  * 2009scape's own familiar `playAudio` calls
    (docs/summoning_port/familiar_sound_refs_530.csv) use rev-530 ids. By
    payload, every one of them is a `lore_*` record here: 4161 -> lore_spell,
    4164 -> lore_craft_pouch, 4214 -> lore_dreadfowl_boost,
    4265 -> lore_wolf_howl2, 4372 -> lore_healing_aura.
  * The Unicorn stallion's source combat_audio is lore_unicorn_{attack,hit,
    death} (docs/summoning_port/familiar_combat_audio_530.csv).
  * RS3 session 4285 (rev 951, 2026-10-19) summons a Granite lobster and then
    a Spirit terrorbird. Each summon is `lore_summon`, then on the familiar's
    first tick one area sound named for the creature: `lore_lobster` and
    `terrorbird_appear`. The terrorbird swings with `terrorbird_attack` on its
    seq-1010 tick. So a bare `lore_<creature>` is that familiar's appear sound,
    and a titan's `_spawn` is the same slot.

Every row carries a basis, strongest first:

  source    2009scape plays this sound for this familiar
  recorded  RS3 session 4285 plays it for this familiar
  stem      the name carries the familiar's own creature word
  alias     the name carries a sibling creature's word or a misspelling
            (polar_bear for the Arctic bear, larupita, dagganoth)
  scroll    the name is the familiar's special-move scroll name
  inferred  the name fits by meaning only; review before use

`id_530` is the rev-530 synth with the byte-identical payload (2009scape's
cache numbers these differently), so a row can be traced back to the old cache.

Writes docs/summoning_port/familiar_lore_audio_239.csv. Writes no content.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import sys
import zlib
import bz2
from pathlib import Path

from js5_cache_verify import read_group, read_index

REPO = Path(__file__).resolve().parents[1]
SOUND_PACK = REPO / "OSRS-Content" / "osrs239-content" / "pack" / "4_soundeffects.pack"
ROSTER = REPO / "docs" / "summoning_port" / "roster_assets_530.csv"
OUT = REPO / "docs" / "summoning_port" / "familiar_lore_audio_239.csv"
TARGET_CACHE = REPO / "cache.osrs239"
SOURCE_CACHE = Path.home() / "Documents" / "git_repos" / "2009scape" / "Server" / "data" / "cache"
SYNTH_ARCHIVE = 4

# entry -> [(slot, sound name, basis, note)]
MATCHES: dict[str, list[tuple[str, str, str, str]]] = {
    "spirit_wolf": [
        ("special", "lore_wolf_howl2", "source", "Howl; SpiritWolfNPC.java:111"),
        ("extra", "lore_wolf_howl", "stem", ""),
        ("extra", "lore_wolf_bark", "stem", ""),
        ("extra", "lore_wolf_bark_question", "stem", ""),
        ("extra", "lore_wolf_growl", "stem", ""),
        ("extra", "lore_wolf_whine", "stem", ""),
        ("extra", "lore_wolf_whine2", "stem", ""),
        ("extra", "lore_wolf_woofwoof_growl", "stem", ""),
    ],
    "dreadfowl": [
        ("appear", "lore_dreadfowl", "stem", ""),
        ("special", "lore_dreadfowl_strike", "scroll", "Dreadfowl strike"),
        ("special", "lore_dreadfowl_special", "stem", ""),
        ("extra", "lore_dreadfowl_boost", "source", "farming boost; 2009scape 4214"),
    ],
    "spirit_spider": [
        ("appear", "lore_spider", "stem", ""),
        ("special", "lore_eggspawn", "scroll", "Egg spawn"),
    ],
    "thorny_snail": [
        ("appear", "lore_snail", "stem", ""),
        ("attack", "lore_snail_attack", "stem", ""),
        ("defend", "lore_snail_hit", "stem", ""),
        ("death", "lore_snail_death", "stem", ""),
        ("special", "lore_slime_trail", "inferred", "Slime spray"),
        ("special_impact", "lore_slime_trail_impact", "inferred", "Slime spray"),
    ],
    "granite_crab": [
        ("appear", "lore_rockcrab", "alias", "rock crab"),
        ("special", "lore_stony_shell", "scroll", "Stony shell"),
    ],
    "spirit_mosquito": [
        ("attack", "lore_mosquito_attack", "stem", ""),
        ("defend", "lore_mosquito_hit", "stem", ""),
        ("death", "lore_mosquito_death", "stem", ""),
        ("special", "lore_harrass", "inferred", "Pester"),
    ],
    "desert_wyrm": [
        ("appear", "lore_desert_worm_burrow_up", "alias", "no bare stem; burrows up on arrival"),
        ("attack", "lore_desert_worm_attack", "alias", "worm"),
        ("defend", "lore_desert_worm_hit", "alias", "worm"),
        ("death", "lore_desert_worm_death", "alias", "worm"),
        ("special", "lore_electric_lash", "scroll", "Electric lash"),
        ("special_impact", "lore_electric_lash_impact", "scroll", "Electric lash"),
        ("extra", "lore_desert_worm_burrow_down", "alias", ""),
    ],
    "spirit_scorpion": [
        ("appear", "lore_scorpion", "stem", ""),
        ("attack", "lore_scorpion_attack", "stem", "source combat_audio is scorpion_* 3611"),
        ("defend", "lore_scorpion_hit", "stem", "source combat_audio is scorpion_* 3612"),
        ("death", "lore_scorpion_death", "stem", "source combat_audio is scorpion_* 3610"),
        ("special", "lore_envenom_weapon", "inferred", "Venom shot"),
    ],
    "spirit_tz_kih": [
        ("appear", "lore_tz_kih_spawn", "stem", ""),
        ("attack", "lore_tz_kih_attack", "stem", ""),
        ("defend", "lore_tz_kih_hit", "stem", ""),
        ("death", "lore_tz_kih_death", "stem", ""),
        ("ranged", "lore_tz_kih_range", "stem", ""),
    ],
    "albino_rat": [
        ("appear", "lore_giant_rat", "alias", "giant rat"),
        ("defend", "lore_giant_rat_hit", "alias", "source combat_audio is anger_rat_hit 705"),
        ("death", "lore_giant_rat_death", "alias", "source combat_audio is anger_rat_death 704"),
    ],
    "spirit_kalphite": [
        ("appear", "lore_kalphite", "stem", ""),
        ("special", "lore_sandstorm", "scroll", "Sandstorm"),
    ],
    "compost_mound": [
        ("appear", "lore_compost", "stem", ""),
        ("attack", "lore_compost_attack", "stem", ""),
        ("defend", "lore_compost_hit", "stem", ""),
        ("death", "lore_compost_death", "stem", ""),
        ("special", "lore_generate_compost", "scroll", "Generate compost"),
        ("special_impact", "lore_generate_compost_impact", "scroll", "Generate compost"),
    ],
    "giant_chinchompa": [
        ("appear", "lore_chinchompa", "stem", ""),
        ("attack", "lore_chinchompa_attack", "stem", ""),
        ("defend", "lore_chinchompa_hit", "stem", ""),
        ("special", "lore_chinchompa_explode", "scroll", "Explode"),
    ],
    "vampire_bat": [
        ("appear", "lore_vampire_bat", "stem", ""),
        ("special", "lore_vampire_touch", "scroll", "Vampire touch"),
    ],
    "honey_badger": [
        ("appear", "lore_badger", "stem", ""),
        ("attack", "lore_badger_attack", "stem", ""),
        ("defend", "lore_badger_hit", "stem", ""),
        ("death", "lore_badger_death", "stem", ""),
        ("special", "lore_insane_ferocity", "scroll", "Insane ferocity"),
    ],
    "beaver": [
        ("appear", "lore_beaver", "stem", ""),
        ("special", "lore_multichop", "scroll", "Multichop"),
    ],
    "void_ravager": [
        ("attack", "lore_ravager_attack", "stem", ""),
        ("defend", "lore_ravager_hit", "stem", ""),
        ("death", "lore_ravager_death", "stem", ""),
        ("extra", "lore_ravager_dig", "stem", ""),
    ],
    "void_spinner": [
        ("attack", "lore_spinner_attack", "stem", ""),
        ("defend", "lore_spinner_hit", "stem", ""),
        ("death", "lore_spinner_death", "stem", ""),
        ("extra", "lore_spinner_heal", "stem", "passive heal"),
        ("extra", "lore_spinner_spin", "stem", ""),
    ],
    "void_torcher": [
        ("appear", "lore_torcher_spawn", "stem", ""),
        ("attack", "lore_torcher_attack", "stem", ""),
        ("defend", "lore_torcher_hit", "stem", ""),
        ("death", "lore_torcher_death", "stem", ""),
        ("extra", "lore_torcher_fire", "stem", ""),
        ("extra", "lore_torcher_firebreath", "stem", ""),
    ],
    "void_shifter": [
        ("attack", "lore_shifter_attack", "stem", ""),
        ("defend", "lore_shifter_hit", "stem", ""),
        ("death", "lore_shifter_death", "stem", ""),
    ],
    "bull_ant": [
        ("appear", "lore_giant_ant", "alias", "giant ant"),
        ("defend", "lore_giant_ant_hit", "alias", "giant ant"),
        ("death", "lore_giant_ant_death", "alias", "giant ant"),
        ("special", "lore_unburden", "scroll", "Unburden"),
    ],
    "macaw": [
        ("appear", "lore_macaw", "stem", ""),
        ("special", "lore_herbcall", "scroll", "Herbcall"),
        ("extra", "lore_macaw_breeze", "stem", ""),
    ],
    "evil_turnip": [
        ("appear", "lore_evilturnip", "stem", ""),
        ("attack", "lore_evilturnip_attack", "stem", ""),
        ("defend", "lore_evilturnip_hit", "stem", ""),
        ("death", "lore_evilturnip_death", "stem", ""),
        ("special", "lore_evilflames", "scroll", "Evil flames"),
    ],
    "pyrelord": [
        ("appear", "lore_pyrefiend_spawn", "alias", "pyrefiend"),
        ("attack", "lore_pyrefiend_attack", "alias", "pyrefiend"),
        ("defend", "lore_pyrefiend_hit", "alias", "pyrefiend"),
        ("death", "lore_pyrefiend_death", "alias", "pyrefiend"),
        ("special", "lore_immense_heat", "scroll", "Immense heat"),
    ],
    "magpie": [
        ("appear", "lore_magpie", "stem", ""),
        ("special", "lore_thieving_fingers", "scroll", "Thieving fingers"),
    ],
    "spirit_terrorbird": [
        ("appear", "terrorbird_appear", "recorded", "RS3 4285 tick 351; not lore_-prefixed"),
        ("attack", "terrorbird_attack", "recorded", "RS3 4285 ticks 367/375/383 with seq 1010"),
        ("defend", "terrorbird_hit", "stem", "2009scape Terrorbird npc 138 combat_audio"),
        ("death", "terrorbird_death", "stem", "2009scape Terrorbird npc 138 combat_audio"),
        ("special", "lore_tireless_run", "scroll", "Tireless run"),
    ],
    "spirit_jelly": [
        ("attack", "lore_jelly_attack", "stem", ""),
        ("defend", "lore_jelly_hit", "stem", ""),
        ("death", "lore_jelly_death", "stem", ""),
        ("special", "lore_dissolve_cast", "scroll", "Dissolve"),
        ("special_impact", "lore_dissolve_impact", "scroll", "Dissolve"),
    ],
    "ibis": [
        ("special", "lore_fishrain_splat", "scroll", "Fish rain"),
        ("appear", "lore_stork", "inferred", "the only wading-bird name"),
    ],
    "spirit_kyatt": [
        ("attack", "lore_kyatt_attack", "stem", ""),
        ("defend", "lore_kyatt_hit", "stem", ""),
        ("death", "lore_kyatt_death", "stem", ""),
        ("special", "lore_kyatt_special", "stem", "Ambush"),
    ],
    "spirit_larupia": [
        ("attack", "lore_larupita_attack", "alias", "spelled larupita"),
        ("defend", "lore_larupita_hit", "alias", "spelled larupita"),
        ("death", "lore_larupita_death", "alias", "spelled larupita"),
        ("special", "lore_rending", "scroll", "Rending"),
    ],
    "spirit_graahk": [
        ("appear", "lore_graahk", "stem", ""),
        ("attack", "lore_graahk_attack", "stem", ""),
        ("defend", "lore_graahk_hit", "stem", ""),
        ("death", "lore_graahk_death", "stem", ""),
        ("special", "lore_graahk_special", "stem", "Goad"),
        ("special", "lore_goad", "scroll", "Goad"),
        ("special", "lore_goad_2", "scroll", "Goad; absent from the rev-530 cache"),
        ("special", "lore_goad_3", "scroll", "Goad; absent from the rev-530 cache"),
        ("special", "lore_goad_4", "scroll", "Goad; absent from the rev-530 cache"),
    ],
    "karamthulhu": [
        ("appear", "lore_karamthulhu_spawn", "stem", ""),
        ("attack", "lore_karamthulhu_attack", "stem", ""),
        ("defend", "lore_karamthulhu_defend", "stem", ""),
        ("death", "lore_karamthulhu_death", "stem", ""),
        ("special", "lore_karamthulhu_special_cast", "stem", "Doomsphere"),
        ("special_impact", "lore_karamthulhu_special_impact", "stem", "Doomsphere"),
    ],
    "smoke_devil": [
        ("appear", "lore_dust_devil", "alias", "dust devil"),
        ("attack", "lore_dust_devil_attack", "alias", "dust devil"),
        ("defend", "lore_dust_devil_hit", "alias", "dust devil"),
        ("death", "lore_dust_devil_death", "alias", "dust devil"),
        ("extra", "lore_dust_devil_fire", "alias", "attack projectile?"),
        ("extra", "lore_dust_devil_fire_impact", "alias", ""),
        ("special", "lore_dust_butt", "inferred", "Dust cloud"),
        ("special_impact", "lore_dust_butt_impact", "inferred", "Dust cloud"),
    ],
    "abyssal_lukrer": [
        ("appear", "lore_abyssal_lurker", "stem", ""),
        ("attack", "lore_abyssal_lurker_attack", "stem", ""),
        ("defend", "lore_abyssal_lurker_defend", "stem", ""),
        ("death", "lore_abyssal_lurker_death", "stem", ""),
    ],
    "spirit_cobra": [
        ("appear", "lore_snake", "alias", "snake"),
        ("attack", "lore_snake_attack", "alias", "snake"),
        ("defend", "lore_snake_hit", "alias", "snake"),
        ("death", "lore_snake_death", "alias", "snake"),
        ("special", "lore_snake_incubation", "scroll", "Ophidian incubation"),
    ],
    "stranger_plant": [
        ("appear", "lore_stranger_plant", "stem", ""),
        ("attack", "lore_stranger_plant_attack", "stem", ""),
        ("defend", "lore_stranger_plant_hit", "stem", ""),
        ("death", "lore_stranger_plant_death", "stem", ""),
        ("special", "lore_poisonous_blast", "scroll", "Poisonous blast"),
        ("special_impact", "lore_poisonous_blast_impact", "scroll", "Poisonous blast"),
    ],
    "barker_toad": [
        ("appear", "lore_barker_toad", "stem", ""),
        ("attack", "lore_barker_toad_attack", "stem", ""),
        ("defend", "lore_barker_toad_hit", "stem", ""),
        ("death", "lore_barker_toad_death", "stem", ""),
        ("special", "lore_toad_bark", "scroll", "Toad bark"),
        ("special", "lore_barker_toad_special_attack", "stem", "Toad bark"),
        ("extra", "lore_barker_toad_swallow_cannonball", "stem", "loading the cannonball"),
    ],
    "war_tortoise": [
        ("appear", "lore_war_turtle", "alias", "turtle"),
        ("attack", "lore_war_turtle_attack", "alias", "turtle"),
        ("defend", "lore_war_turtle_hit", "alias", "turtle"),
        ("death", "lore_war_turtle_death", "alias", "turtle"),
        ("special", "lore_adamantite_shell", "inferred", "Testudo"),
    ],
    "bunyip": [
        ("appear", "lore_bunyip", "stem", ""),
        ("attack", "lore_bunyip_attack", "stem", ""),
        ("defend", "lore_bunyip_hit", "stem", ""),
        ("death", "lore_bunyip_death", "stem", ""),
    ],
    "fruit_bat": [
        ("appear", "lore_fruit_bat", "stem", ""),
        ("special", "lore_fruitfall_splat", "scroll", "Fruitfall"),
    ],
    "ravenous_locust": [
        ("appear", "lore_locust", "stem", ""),
        ("special", "lore_famine", "scroll", "Famine"),
        ("extra", "lore_locust_jump", "stem", ""),
        ("extra", "lore_locust_land", "stem", ""),
    ],
    "arctic_bear": [
        ("appear", "lore_polar_bear", "alias", "polar bear"),
        ("attack", "lore_polar_bear_attack", "alias", "source combat_audio is jungle_horror_attack2 498"),
        ("defend", "lore_polar_bear_hit", "alias", "source combat_audio is jungle_horror_hit 500"),
        ("death", "lore_polar_bear_death", "alias", "source combat_audio is jungle_horror_death 499"),
        ("special", "lore_arctic_blast", "scroll", "Arctic blast"),
    ],
    "obsidian_golem": [
        ("appear", "lore_obsidian_golem_spawn", "stem", ""),
        ("attack", "lore_obsidian_golem_attack", "stem", ""),
        ("defend", "lore_obsidian_golem_hit", "stem", ""),
        ("death", "lore_obsidian_golem_death", "stem", ""),
        ("special", "lore_mountain_strength", "inferred", "Volcanic strength"),
    ],
    "granite_lobster": [
        ("appear", "lore_lobster", "recorded", "RS3 4285 tick 174, after lore_summon"),
        ("attack", "lore_lobster_attack", "stem", ""),
        ("defend", "lore_lobster_hit", "stem", ""),
        ("death", "lore_lobster_death", "stem", ""),
        ("special", "lore_crushing_claw", "scroll", "Crushing claw"),
    ],
    "praying_mantis": [
        ("appear", "lore_preying_mantis", "alias", "spelled preying"),
        ("defend", "lore_preying_mantis_hit", "alias", "spelled preying"),
        ("death", "lore_preying_mantis_death", "alias", "spelled preying"),
        ("special", "lore_mantis_strike", "scroll", "Mantis strike"),
    ],
    "forge_regent_beast": [
        ("special", "lore_inferno", "scroll", "Inferno"),
        ("special_impact", "lore_inferno_impact", "scroll", "Inferno"),
        ("attack", "lore_flaming_prince_attack", "inferred", "no forge/regent stem"),
        ("defend", "lore_flaming_prince_hit", "inferred", "no forge/regent stem"),
        ("death", "lore_flaming_prince_death", "inferred", "no forge/regent stem"),
    ],
    "talon_beast": [
        ("appear", "lore_nailbeast", "alias", "nail beast"),
        ("attack", "lore_nailbeast_attack", "alias", "nail beast"),
        ("defend", "lore_nailbeast_hit", "alias", "nail beast"),
        ("death", "lore_nailbeast_death", "alias", "nail beast"),
        ("special", "lore_deadlyclaw_cast", "scroll", "Deadly claw"),
        ("special_impact", "lore_deadlyclaw_impact", "scroll", "Deadly claw"),
    ],
    "giant_ent": [
        ("appear", "lore_ent", "stem", ""),
        ("attack", "lore_ent_attack", "stem", ""),
        ("defend", "lore_ent_defend", "stem", ""),
        ("death", "lore_ent_death", "stem", ""),
    ],
    "hydra": [
        ("attack", "lore_hydra_attack", "stem", ""),
        ("defend", "lore_hydra_hit", "stem", ""),
        ("death", "lore_hydra_death", "stem", ""),
        ("ranged", "lore_hydra_ranged", "stem", ""),
    ],
    "spirit_dagannoth": [
        ("attack", "lore_dagganoth_attack", "alias", "spelled dagganoth"),
        ("defend", "lore_dagganoth_hit", "alias", "spelled dagganoth"),
        ("death", "lore_dagganoth_death", "alias", "spelled dagganoth"),
        ("special", "lore_spike_shot", "scroll", "Spike shot"),
        ("special_impact", "lore_spike_shot_impact", "scroll", "Spike shot"),
        ("special", "lore_dagganoth_attack_special", "alias", "Spike shot"),
    ],
    "unicorn_stallion": [
        ("appear", "lore_unicorn", "stem", ""),
        ("attack", "lore_unicorn_attack", "source", "2009scape had attack/defend swapped"),
        ("defend", "lore_unicorn_hit", "source", "2009scape had attack/defend swapped"),
        ("death", "lore_unicorn_death", "source", ""),
        ("special", "lore_healing_aura", "source", "Healing aura; UnicornStallionNPC.java:49"),
    ],
    "wolpertinger": [
        ("appear", "lore_wolpertinger", "stem", ""),
        ("defend", "lore_wolpertinger_hit", "stem", ""),
        ("death", "lore_wolpertinger_death", "stem", ""),
        ("special", "lore_magic_might", "inferred", "Magic focus"),
    ],
    "pack_yak": [
        ("appear", "lore_pack_yak", "stem", ""),
        ("attack", "lore_pack_yak_attack", "stem", ""),
        ("defend", "lore_pack_yak_hit", "stem", ""),
        ("death", "lore_pack_yak_death", "stem", ""),
        ("special", "lore_winter_storage", "scroll", "Winter storage"),
    ],
    "abyssal_titan": [
        ("appear", "lore_abyssal_titan_spawn", "stem", ""),
        ("attack", "lore_abyssal_titan_attack", "stem", ""),
        ("defend", "lore_abyssal_titan_hit", "stem", ""),
        ("death", "lore_abyssal_titan_death", "stem", ""),
        ("special", "lore_abyssal_titan_bring_pouches", "stem", "Essence shipment"),
    ],
    "iron_titan": [
        ("appear", "lore_iron_titan_spawn", "stem", ""),
        ("attack", "lore_iron_titan_attack", "stem", ""),
        ("defend", "lore_iron_titan_hit", "stem", ""),
        ("death", "lore_iron_titan_death", "stem", ""),
        ("ranged", "lore_iron_titan_range", "stem", ""),
        ("extra", "lore_iron_titan_impact", "stem", ""),
        ("special", "lore_iron_within", "scroll", "Iron within"),
    ],
    "steel_titan": [
        ("appear", "lore_steel_titan_spawn", "stem", ""),
        ("attack", "lore_steel_titan_attack", "stem", ""),
        ("defend", "lore_steel_titan_hit", "stem", ""),
        ("death", "lore_steel_titan_death", "stem", ""),
        ("ranged", "lore_steel_titan_range", "stem", ""),
        ("extra", "lore_steel_titan_range_impact", "stem", ""),
        ("extra", "lore_steel_titan_melee_impact", "stem", ""),
        ("special", "lore_steel_of_legends", "scroll", "Steel of legends"),
        ("special", "lore_steel_titan_special", "stem", "Steel of legends"),
        ("special_impact", "lore_steel_titan_special_impact", "stem", "Steel of legends"),
    ],
    "lava_titan": [
        ("appear", "lore_lava_titan_spawn", "stem", ""),
        ("attack", "lore_lava_titan_attack", "stem", ""),
        ("defend", "lore_lava_titan_hit", "stem", ""),
        ("death", "lore_lava_titan_death", "stem", ""),
        ("special", "lore_lava_titan_special", "stem", "Ebon thunder"),
    ],
    "swamp_titan": [
        ("appear", "lore_swamp_titan_spawn", "stem", ""),
        ("attack", "lore_swamp_titan_attack", "stem", ""),
        ("defend", "lore_swamp_titan_hit", "stem", ""),
        ("death", "lore_swamp_titan_death", "stem", ""),
        ("special", "lore_swamp_plague", "scroll", "Swamp plague"),
        ("special_impact", "lore_swamp_plague_impact", "scroll", "Swamp plague"),
        ("extra", "lore_swamp_titan_timer", "stem", ""),
    ],
    "geyser_titan": [
        ("appear", "lore_geyser_titan_spawn", "stem", ""),
        ("attack", "lore_geyser_titan_attack", "stem", ""),
        ("defend", "lore_geyser_titan_hit", "stem", ""),
        ("death", "lore_geyser_titan_death", "stem", ""),
        ("ranged", "lore_geyser_titan_range", "stem", ""),
        ("extra", "lore_geyser_titan_impact", "stem", ""),
    ],
}

# The three elemental titans share one scroll, Titan's constitution.
for _titan in ("fire_titan", "moss_titan", "ice_titan"):
    _stem = "lore_" + _titan
    MATCHES[_titan] = [
        ("appear", _stem + "_spawn", "stem", ""),
        ("attack", _stem + "_attack", "stem", ""),
        ("defend", _stem + "_hit", "stem", ""),
        ("death", _stem + "_death", "stem", ""),
        ("special", "lore_titans_constitution", "scroll", "Titan's constitution"),
    ]

# The six minotaurs share their stem and the Bull rush scroll.
for _metal in ("bronze", "iron", "steel", "mithril", "adamant", "rune"):
    MATCHES[_metal + "_minotaur"] = [
        ("appear", "lore_minotaur", "stem", ""),
        ("attack", "lore_minotaur_attack", "stem", ""),
        ("defend", "lore_minotaur_hit", "stem", ""),
        ("death", "lore_minotaur_death", "stem", ""),
        ("special", "lore_bull_rush", "scroll", "Bull rush"),
        ("special_impact", "lore_bull_rush_impact", "scroll", "Bull rush"),
        ("special", "lore_minotaur_special", "stem", "Bull rush"),
    ]

# Seven cockatrice variants, seven numbered triples. The numbering is taken in
# npc-id order (cockatrice 6875 .. vulatrice 6887); nothing in either cache
# ties a number to a variant, so that order is unverified.
for _n, _variant in enumerate(
    ("cockatrice", "guthatrice", "saratrice", "zamatrice", "pengatrice", "coraxatrice", "vulatrice"), 1
):
    _note = "variant order unverified"
    MATCHES["spirit_" + _variant] = [
        ("attack", f"lore_cockatrice_{_n}_attack", "stem", _note),
        ("defend", f"lore_cockatrice_{_n}_hit", "stem", _note),
        ("death", f"lore_cockatrice_{_n}_death", "stem", _note),
        ("special", "lore_petrify", "scroll", "Petrifying gaze"),
    ]

# Familiars with no named sound of their own in the cache.
NONE = ("bloated_leech", "abyssal_parasite", "phoenix")

# Named lore_ records that belong to no familiar: pets, the obelisk, and the
# generic summoning flow. Listed so the ledger accounts for all 373.
NOT_A_FAMILIAR = {
    "lore_summon": "every summon, before the familiar's appear sound (RS3 4285)",
    "lore_spell": "special-move cast; 2009scape 4161, Familiar.java:497",
    "lore_spell2": "generic", "lore_spell_impact": "generic",
    "lore_craft_pouch": "pouch infusion; already played by summoning_infuse.rs2",
    "lore_creature_create_item": "a forager or beast producing an item",
    "lore_skill_boost": "a familiar's invisible skill boost (RS3 4285 tick 258)",
    "lore_bg_obelisk": "obelisk", "lore_charging": "obelisk recharge", "lore_prayer": "",
    "lore_no_pets": "", "lore_flyaway": "", "lore_flyback": "",
    "lore_lava_assault": "", "lore_obsidian_thunder": "",
    "lore_obsidian_thunder_impact": "", "lore_overwhelmingwaters_cast": "",
    "lore_overwhelmingwaters_impact": "", "lore_nesting_cast": "", "lore_truffle_hunt": "",
    "lore_squawk": "", "lore_twitter": "", "lore_smallbird_twitter": "",
}


def die(message: str) -> "NoReturn":  # type: ignore[name-defined]
    print(f"match_familiar_lore_audio: {message}", file=sys.stderr)
    sys.exit(1)


def load_sound_names() -> dict[str, int]:
    by_name: dict[str, int] = {}
    for line in SOUND_PACK.read_text(encoding="utf-8").splitlines():
        m = re.match(r"(\d+)=(\S+)$", line.strip())
        if m:
            by_name[m.group(2)] = int(m.group(1))
    return by_name


def unpack(container: bytes) -> bytes:
    kind = container[0]
    length = int.from_bytes(container[1:5], "big")
    if kind == 0:
        return container[5 : 5 + length]
    body = container[9 : 9 + length]
    if kind == 1:
        return bz2.decompress(b"BZh1" + body)
    if kind == 2:
        return zlib.decompress(body, 31)
    die(f"unknown container compression {kind}")


def synth_hashes(cache: Path) -> dict[int, str]:
    out: dict[int, str] = {}
    with open(cache / "main_file_cache.dat2", "rb") as dat2:
        for group, size, sector in read_index(cache / f"main_file_cache.idx{SYNTH_ARCHIVE}"):
            blob = read_group(dat2, SYNTH_ARCHIVE, group, size, sector)
            if blob is None:
                die(f"{cache}: synth {group} has a broken sector chain")
            out[group] = hashlib.sha256(unpack(blob)).hexdigest()
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", type=Path, default=OUT)
    args = parser.parse_args()

    names = load_sound_names()
    roster = {}
    with ROSTER.open(newline="") as handle:
        for row in csv.DictReader(handle):
            if row["entity_kind"] == "familiar":
                roster[row["entry"]] = row
    unmatched = set(roster) - set(MATCHES) - set(NONE)
    if unmatched:
        die(f"familiars with no ledger decision: {sorted(unmatched)}")
    unknown = (set(MATCHES) | set(NONE)) - set(roster)
    if unknown:
        die(f"ledger entries not in the roster: {sorted(unknown)}")

    target = synth_hashes(TARGET_CACHE)
    source_by_hash: dict[str, list[int]] = {}
    for group, digest in synth_hashes(SOURCE_CACHE).items():
        source_by_hash.setdefault(digest, []).append(group)

    used: set[str] = set()
    rows = []
    for entry in sorted(roster, key=lambda e: int(roster[e]["source_npc"])):
        for slot, name, basis, note in MATCHES.get(entry, []):
            if name not in names:
                die(f"{entry}: {name} is not a name in {SOUND_PACK.name}")
            sound = names[name]
            if sound not in target:
                die(f"{entry}: {name} ({sound}) is named but absent from {TARGET_CACHE.name}")
            old = source_by_hash.get(target[sound], [])
            used.add(name)
            rows.append([entry, roster[entry]["source_npc"], slot, name, sound,
                         ",".join(map(str, old)) or "absent", basis, note])
        if entry in NONE:
            rows.append([entry, roster[entry]["source_npc"], "none", "", "", "", "none",
                         "no named sound for this creature"])

    pets = sorted(n for n in names if n.startswith("lore_") and n not in used and n not in NOT_A_FAMILIAR)
    with args.out.open("w", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["entry", "source_npc", "slot", "name", "osrs239_id", "id_530", "basis", "note"])
        writer.writerows(rows)

    lore_total = sum(1 for n in names if n.startswith("lore_"))
    print(f"{len(rows)} rows for {len(roster)} familiars -> {args.out.relative_to(REPO)}")
    print(f"lore_ names: {lore_total}; matched to a familiar {len([n for n in used if n.startswith('lore_')])}; "
          f"generic {len(NOT_A_FAMILIAR)}; left (pets etc.) {len(pets)}")
    print("left:", " ".join(pets))
    return 0


if __name__ == "__main__":
    sys.exit(main())
