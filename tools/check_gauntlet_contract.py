#!/usr/bin/env python3
"""Deterministic structural and 10,000-seed checks for The Gauntlet."""

from __future__ import annotations

import json
import re
from collections import deque
from pathlib import Path

import config_text

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "OSRS-Content/osrs-content"
GAUNTLET = BASE / "server/scripts/minigames/minigame_gauntlet"
DATA = json.loads((ROOT / "tools/data/gauntlet_contract.json").read_text())
PLAN = (ROOT / "docs/bosses/gauntlet.md").read_text()
LAYOUT = (GAUNTLET / "scripts/gauntlet_layout.rs2").read_text()
CORE = (GAUNTLET / "scripts/gauntlet.rs2").read_text()
CRAFT = (GAUNTLET / "scripts/gauntlet_craft.rs2").read_text()
GATHER = (GAUNTLET / "scripts/gauntlet_gather.rs2").read_text()
MONSTERS = (GAUNTLET / "scripts/gauntlet_monsters.rs2").read_text()
ROOMS = (GAUNTLET / "scripts/gauntlet_rooms.rs2").read_text()
HUNLLEF = (GAUNTLET / "scripts/gauntlet_hunllef.rs2").read_text()
SELFTEST = (GAUNTLET / "scripts/gauntlet_selftest.rs2").read_text()
REWARDS = (GAUNTLET / "scripts/gauntlet_rewards.rs2").read_text()
PROGRESS = (GAUNTLET / "scripts/gauntlet_progress.rs2").read_text()
LOBBY = (GAUNTLET / "scripts/gauntlet_lobby.rs2").read_text()
MAP_STATE = (GAUNTLET / "scripts/gauntlet_map_state.rs2").read_text()
NPC_CONFIG = config_text.read_text(GAUNTLET / "configs/gauntlet_monsters.npc")
INV_CONFIG = config_text.read_text(GAUNTLET / "configs/gauntlet.inv")
RECIPE_INTERFACE = (BASE / "interfaces/gauntlet_recipes.if").read_text()
COSTUME_ROOM = (
    BASE / "server/scripts/skill_construction/scripts/poh_costume_room.rs2"
).read_text()
MAGIC = (BASE / "server/scripts/skill_magic/scripts/magic.rs2").read_text()
TELEPORT = (BASE / "server/scripts/skill_magic/scripts/spells/teleport.rs2").read_text()
HOME_TELEPORT = (BASE / "server/scripts/skill_magic/scripts/spells/home_teleport.rs2").read_text()

DIRECTIONS = {"N": (0, 1), "E": (1, 0), "S": (0, -1), "W": (-1, 0)}
OPPOSITE = {"N": "S", "E": "W", "S": "N", "W": "E"}


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"{label}: missing {needle!r}")


def npc_block(text: str, name: str) -> str:
    match = re.search(
        rf"^\[{re.escape(name)}\]\n(.*?)(?=^\[|\Z)",
        text,
        flags=re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise AssertionError(f"missing npc block [{name}]")
    return match.group(1)


def proc_body(text: str, name: str) -> str:
    match = re.search(
        rf"^\[proc,{re.escape(name)}\][^\n]*\n(.*?)(?=^\[|\Z)",
        text,
        flags=re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise AssertionError(f"missing proc {name}")
    return match.group(1)


def measured_room_masks(path: Path, ids: set[int]) -> dict[tuple[int, int], set[str]]:
    masks: dict[tuple[int, int], set[str]] = {}
    line_re = re.compile(r"^1 (\d+) (\d+): (\d+) ")
    for line in path.read_text().splitlines():
        match = line_re.match(line)
        if not match or int(match.group(3)) not in ids:
            continue
        x, z = int(match.group(1)), int(match.group(2))
        room = (x // 16, z // 16)
        lx, lz = x % 16, z % 16
        mask = masks.setdefault(room, set())
        if lz == 0:
            mask.add("S")
        if lz == 14:
            mask.add("N")
        if lx == 0:
            mask.add("W")
        if lx == 14:
            mask.add("E")
    return masks


def check_cache_room_manifest() -> None:
    room_data = DATA["room_library"]
    for mode, filename, ids_key in (
        ("normal", room_data["normal_map"], "normal_unlit_door_ids"),
        ("corrupted", room_data["corrupted_map"], "corrupted_unlit_door_ids"),
    ):
        masks = measured_room_masks(BASE / "maps" / filename, set(room_data[ids_key]))
        for source_x, expected in room_data["archetypes"].items():
            for source_z in range(4):
                actual = masks.get((int(source_x), source_z), set())
                assert actual == set(expected), (
                    f"{mode} source room ({source_x},{source_z}) mask "
                    f"{''.join(sorted(actual))}, expected {expected}"
                )


# The grid since the RSProx rebuild (OSRS-Content c69dd8cb85,
# docs/rsprox-analysis/gauntlet/README.md §3): every room joins every
# neighbour, edges and corners are library rooms turned so their missing doors
# face out of the grid, and the start room is one of the four boss neighbours.
# This mirrors gauntlet_layout.rs2 ~gauntlet_room_template / ~gauntlet_room_hash;
# check_content_contract pins the lines it mirrors.
GRID = 7
BOSS_ROOM = 24
START_ROOMS = (17, 23, 25, 31)
CW = {"N": "E", "E": "S", "S": "W", "W": "N"}
TURNS = {"none": 0, "cw": 1, "half": 2, "ccw": 3}


def room_hash(seed: int, room: int, salt: int, n: int) -> int:
    h = seed * 31 + room * 977 + salt * 131
    h = (h + h // 7) % 65521
    return h % n


def room_template(seed: int, start: int, room: int) -> tuple[int, int, int]:
    if room in (BOSS_ROOM, start):
        return 3, 3 if room == BOSS_ROOM else 2, TURNS["none"]
    x, z, last = room % GRID, room // GRID, GRID - 1
    row = room_hash(seed, room, 1, 4)
    edge_x, edge_z = x in (0, last), z in (0, last)
    if edge_x and edge_z:
        if x == last and z == last:
            return 2, row, TURNS["none"]
        if x == last:
            return 2, row, TURNS["cw"]
        if z == 0:
            return 2, row, TURNS["half"]
        return 2, row, TURNS["ccw"]
    if edge_x or edge_z:
        if z == last:
            return 1, row, TURNS["none"]
        if x == last:
            return 1, row, TURNS["cw"]
        if z == 0:
            return 1, row, TURNS["half"]
        return 1, row, TURNS["ccw"]
    return 0, row, room_hash(seed, room, 2, 4)


def turned(mask: str, turn: int) -> frozenset[str]:
    sides = set(mask)
    for _ in range(turn):
        sides = {CW[side] for side in sides}
    return frozenset(sides)


def layout_masks(seed: int, start: int) -> dict[tuple[int, int], frozenset[str]]:
    archetypes = DATA["room_library"]["archetypes"]
    masks = {}
    for room in range(GRID * GRID):
        lib_x, lib_z, turn = room_template(seed, start, room)
        assert 0 <= lib_z < 4, f"seed {seed}: room {room} library row {lib_z}"
        # Column 3 is the start room (a door on every side) and the boss room
        # (a barrier on every side): both open all four ways, never turned.
        mask = "NESW" if lib_x == 3 else archetypes[str(lib_x)]
        masks[(room % GRID, room // GRID)] = turned(mask, turn)
    return masks


def check_layout_seeds() -> None:
    for seed in range(1, 10_001):
        start = START_ROOMS[seed % 4]
        masks = layout_masks(seed, start)
        for (x, z), mask in masks.items():
            for direction in "NESW":
                dx, dz = DIRECTIONS[direction]
                other = (x + dx, z + dz)
                if other not in masks:
                    assert direction not in mask, f"seed {seed}: exterior door {(x, z)} {direction}"
                    continue
                assert direction in mask, f"seed {seed}: {(x, z)} closed to its neighbour {direction}"
                assert OPPOSITE[direction] in masks[other], f"seed {seed}: one-way door {(x, z)} {direction}"
        seen = {(start % GRID, start // GRID)}
        queue = deque(seen)
        while queue:
            x, z = queue.popleft()
            for direction in masks[(x, z)]:
                dx, dz = DIRECTIONS[direction]
                other = (x + dx, z + dz)
                if other not in seen:
                    seen.add(other)
                    queue.append(other)
        assert len(seen) == GRID * GRID, f"seed {seed}: only {len(seen)} rooms connected"


def check_content_contract() -> None:
    for url in DATA["authority"].values():
        require(PLAN, url, "Gauntlet plan authority")
    for needle in DATA["recipe_contract_fragments"]:
        require(CRAFT, needle, "recipe contract")
    # The lobby's recipe book (if_openmain(gauntlet_recipes), below) still lists
    # the 22 cache recipes.
    assert RECIPE_INTERFACE.count("op1=Sing crystal") == 22
    # Singing at the bowl is the skillmulti make menu (mode 22) capped at the
    # free slots, as recorded (RSProx 3268 [1912..1923], 1144: 97 menus;
    # OSRS-Content c69dd8cb85). It replaced per-entry [if_button] handlers.
    for needle in (
        "[oploc1,gauntlet_singing_bowl] ~gauntlet_sing;",
        "[oploc1,gauntlet_singing_bowl_hm] ~gauntlet_sing;",
        "~skill_multi_capped(\"What would you like to make?\", 22, max(1, inv_freespace(inv)),",
        "[proc,gauntlet_recipe](obj $item)(int, int, int, obj, int)",
        "[proc,gauntlet_can_make](obj $item)(boolean)",
        "[proc,gauntlet_make_one](obj $item)",
        "~mesbox(\"You do not have the materials required to make that.\");",
        "~mesbox(\"You do not have all of the required materials and requirements to make anything.\");",
        "if ($shards > 0) inv_del(inv, ~gauntlet_shard_obj, $shards);",
        "sound_synth(eyeglo_bowl_ring, 1, 0);",
    ):
        require(CRAFT, needle, "singing-bowl make menu")
    # The paddlefish pair eats on the shared consume timers (waves seam pass 4):
    # paddlefish standard food, crystal paddlefish combo food with attack delay 2
    # and eat delay 3 (wiki Template:Fast foods table, pinned at
    # docs/minigames/inferno/sources/wiki/wiki_Template_Fast_foods_table.wikitext:509-518).
    for needle in (
        "[opheld1,gauntlet_food]", "[opheld1,gauntlet_combo_food]", "[opheld1,gauntlet_combo_food_hm]",
        "if (~consume_food_ready($combo) = false) {",
        "def_int $attack_delay = 3;", "$attack_delay = 2;",
        "~consume_food_taken(^eat_delay, $attack_delay, $combo);",
    ):
        require(CRAFT, needle, "crystal-paddlefish combo timing")
    for gone in ("%varp7356_gauntlet_combo_tick = map_clock;", "%varp7357_gauntlet_eat_delay = add(map_clock"):
        assert gone not in CRAFT, f"crystal-paddlefish combo timing: the Gauntlet-only eat timer {gone} is back"
    for npc in DATA["npc_roster"]:
        require(GATHER, f"[ai_queue3,{npc}]", f"{npc} death handler")
        require(ROOMS, f"return({npc});", f"{npc} spawn")
    for npc in ("crystal_dragon", "crystal_dark_beast", "crystal_dragon_hm", "crystal_dark_beast_hm"):
        require(MONSTERS, f"[ai_opplayer2,{npc}]", f"{npc} combat AI")
        require(NPC_CONFIG, f"[{npc}]", f"{npc} NPC overlay")
    # Aggression is each npc's own hunt (c69dd8cb85 retired the [ai_timer] that
    # searched the instance every tick); the tornadoes move on the player's
    # boss tick, not an npc heartbeat.
    for npc in DATA["npc_roster"]:
        block = npc_block(NPC_CONFIG, npc)
        require(block, "huntmode=aggressive", f"{npc} aggression")
        require(block, "param=huntrange,6", f"{npc} hunt range")
    require(HUNLLEF, "~gauntlet_tornadoes_tick;", "tornado movement on the boss tick")
    require(HUNLLEF, "[proc,gauntlet_tornadoes_move]", "tornado movement on the boss tick")
    for needle in DATA["drop_contract_fragments"]:
        require(GATHER, needle, "enemy drop contract")
    # Rooms fill on the tick their Node door is lit, by tier odds and the
    # resource spots measured from 214 reveals (c69dd8cb85, README §3).
    for needle in (
        "[proc,gauntlet_door_light]", "~gauntlet_reveal($behind);",
        "~gauntlet_room_populate($room);", "~gauntlet_light_corridor($room, $side);",
        "[proc,gauntlet_pick_guarantees]", "[proc,gauntlet_roll_tier]",
        "[proc,gauntlet_room_resources]", "[proc,gauntlet_add_node]",
        "$r = random(104);", "$r = random(47);", "$r = random(33);", "$r = random(30);",
        "random(5)", "[proc,gauntlet_room_cardinal_edge]",
    ):
        require(ROOMS, needle, "room population")
    for needle in (
        "[proc,gauntlet_spot_count]", "[proc,gauntlet_spot]", "[proc,gauntlet_spot_turned]",
    ):
        require(LAYOUT, needle, "room resource spots")
    for needle in (
        "loc_change($depleted, 0);",
        "obj_add_private(npc_coord, $item, $amount, ^lootdrop_duration, 100);",
        "[proc,gauntlet_has_room](obj $item)(boolean)",
        "if (inv_freespace(inv) > 0) return(true);",
    ):
        require(GATHER, needle, "node depletion and drops")

    junk = proc_body(REWARDS, "gauntlet_roll_junk")
    for obj in DATA["junk_rewards"]:
        require(junk, obj, "junk reward table")
    incomplete = proc_body(REWARDS, "gauntlet_roll_incomplete")
    require(incomplete, "random(27)", "incomplete reward denominator")
    for fragment in DATA["incomplete_reward_fragments"]:
        require(incomplete, fragment, "incomplete reward row")
    main = proc_body(REWARDS, "gauntlet_roll_main")
    require(main, "random(24)", "main reward denominator")
    for obj in DATA["main_reward_objects"]:
        require(main, obj, "main reward table")
    prepare = proc_body(REWARDS, "gauntlet_prepare_reward")
    for obj in DATA["tertiary_rewards"]:
        require(prepare, obj, "tertiary reward table")
    for denominator in (
        "^gauntlet_seed_rate", "^gauntlet_hm_seed_rate",
        "^gauntlet_enhanced_rate", "^gauntlet_hm_enhanced_rate",
        "^gauntlet_pet_rate", "^gauntlet_hm_pet_rate",
    ):
        require(prepare, denominator, "tertiary denominator")
    require(prepare, "if ($hm = 1) { $clue_rate = 19; } else { $clue_rate = 23; }", "Elite clue-rate reward")
    require(prepare, "inv_total(collection_transmit, gauntletpet)", "durable pet ownership")
    require(prepare, "%varb8439_pet_insurance_gauntlet = 0", "insured pet ownership")
    require(prepare, "%varb8441_pet_menagerie_gauntlet = 0", "menagerie pet ownership")
    cape_owned = proc_body(REWARDS, "gauntlet_owns_cape")
    require(cape_owned, "inv_total(worn, gauntlet_crystalline_cape)", "worn cape ownership")
    require(cape_owned, "%varp7373_gauntlet_cape_rack = 1", "cape-rack ownership")
    require(prepare, "~gauntlet_owns_cape = false", "cape-rack ownership")
    require(INV_CONFIG, "[gauntlet_pending_reward]\nsize=12", "immutable pending rewards")
    deliver = proc_body(REWARDS, "gauntlet_deliver_reward")
    require(deliver, "oc_tradeable($item) = true", "untradeable reward retention")
    # c69dd8cb85 dropped an invented line; what keeps the rest is the varbit,
    # cleared only once the pending container is empty.
    require(deliver, "if (~gauntlet_reward_is_empty = true) {\n    %varb9179_gauntlet_reward_available = 0;",
            "pending reward continuation")
    require(deliver, "~gauntlet_pet_award", "automatic Youngllef follower award")

    ids = {int(value) for value in re.findall(r"~ca_task_complete\((\d+)\)", PROGRESS)}
    assert ids == set(DATA["combat_achievement_ids"]), f"CA ids drifted: {sorted(ids)}"
    for needle in (
        "%varp7343_gauntlet_layout_seed = add(random(32767), 1);",
        "def_int $handle = ~gauntlet_build_instance($template);",
        "inv_movetoslot(worn, gauntlet_holding_worn, $slot, $slot);",
        "inv_movetoslot(gauntlet_holding_worn, worn, $slot, $slot);",
        "midi_song(^gauntlet_music_track);", "midi_song(^gauntlet_lobby_music_leave);",
        "%varp6934_summoning_pet_active = 1", "%varp7364_gauntlet_pet_active = 1", "npc_findowned = true",
        "p_teleport(~gauntlet_room_coord(%varb9291_gauntlet_start, 6, 9));",
        # The start room: one of the four boss neighbours (check_layout_seeds).
        "def_int $start = 17;", "if ($r = 1) $start = 23;",
        "else if ($r = 2) $start = 25;", "else if ($r = 3) $start = 31;",
    ):
        require(CORE, needle, "session/layout contract")
    require(HUNLLEF, "~gauntlet_award_completion_cape;", "completion cape on a Corrupted kill")
    for needle in (
        "[proc,gauntlet_pet_spawn]", "[timer,gauntlet_pet_follow]",
        "[opheld5,gauntletpet]", "[opnpc4,gauntlet_pet]",
        "[opnpc3,gauntlet_pet]", "[opnpc1,gauntlet_pet]",
        "%varp2354_total_completed_gauntlet_hm > 0", "%varb8439_pet_insurance_gauntlet = 1",
    ):
        require(PROGRESS, needle, "Youngllef follower lifecycle")
    # The Gauntlet cape's own storage hook was retired in favor of the
    # cache-enum Costume Room; `%gauntlet_cape_rack` survives only as the
    # one-time migration flag Construction imports on cape-rack open.
    require(PROGRESS, "%varp7373_gauntlet_cape_rack", "cape-rack migration flag retained")
    for needle in (
        "[proc,poh_costume_open]", "%varp7373_gauntlet_cape_rack = 1",
        "inv_add(poh_costumes, gauntlet_crystalline_cape, 1)",
        "%varp7373_gauntlet_cape_rack = 0",
    ):
        require(COSTUME_ROOM, needle, "Gauntlet cape costume-room migration")
    constants = (GAUNTLET / "configs" / "gauntlet.constant").read_text()
    require(constants, "^gauntlet_music_track = 650", "cache music dbrow contract")
    # The lines check_layout_seeds mirrors.
    for needle in (
        "[proc,gauntlet_room_template](int $room)(int, int, int)",
        "def_int $h = add(multiply(%varp7343_gauntlet_layout_seed, 31), add(multiply($room, 977), multiply($salt, 131)));",
        "$h = modulo(add($h, divide($h, 7)), 65521);",
        "def_int $row = ~gauntlet_room_hash($room, 1, ^gauntlet_lib_rooms);",
        "if ($x = $last & $z = $last) return(2, $row, ^map_instance_turn_none);",
        "if ($x = $last) return(2, $row, ^map_instance_turn_cw);",
        "if ($z = 0) return(2, $row, ^map_instance_turn_half);",
        "return(2, $row, ^map_instance_turn_ccw);",
        "if ($z = $last) return(1, $row, ^map_instance_turn_none);",
        "if ($x = $last) return(1, $row, ^map_instance_turn_cw);",
        "if ($z = 0) return(1, $row, ^map_instance_turn_half);",
        "return(1, $row, ^map_instance_turn_ccw);",
        "return(0, $row, ~gauntlet_room_hash($room, 2, 4));",
        "map_instance_build($handle);",
    ):
        require(LAYOUT, needle, "seeded layout contract")
    for needle in (
        "[proc,gauntlet_floor_place]", "[proc,gauntlet_floor_tick]", "[proc,gauntlet_floor_damage]",
        "[proc,gauntlet_tornadoes_spawn]", "[proc,gauntlet_boss_begin]",
        "%varp6771_gauntlet_offpray_hits >= ^gauntlet_hunllef_offpray_hits",
        "%varp6769_gauntlet_hunllef_attacks < ^gauntlet_hunllef_style_attacks",
        "if (%varp9101_gauntlet_hunllef_tick <= 0) {\n    %varp9101_gauntlet_hunllef_tick = ^gauntlet_hunllef_rate;",
    ):
        require(HUNLLEF, needle, "Hunllef state machine")
    require(CORE, "~gauntlet_boss_begin;", "Hunllef committed at the barrier")
    for needle in (
        "^gauntlet_hunllef_rate = 5", "^gauntlet_hunllef_style_attacks = 4",
        "^gauntlet_hunllef_offpray_hits = 6", "^gauntlet_tornado_delay = 3",
        "^gauntlet_tornado_life = 20", "^gauntlet_floor_first = 7",
    ):
        require(constants, needle, "Hunllef clock constants (RSProx, README §7)")
    # The tornado wave is a clock, not a dice roll, and the floor pulses rather
    # than staying lit. Both were the other way round before the Near-Reality
    # pass and both are invisible in a short test — a `random(6)` tornado and a
    # gapless floor still look like a working fight.
    # Since c69dd8cb85 the special is counted in attacks (first at 5..10, then
    # 8..13 after the last), and the floor cycles warn -> hit -> clear on
    # per-third durations from the 19 recorded patterns.
    for needle in (
        "if (~gauntlet_special_due = true) {", "[proc,gauntlet_special_due]()(boolean)",
        "def_int $c = %varp7577_gauntlet_tornado_cd;",
        "if ($c < 8) return(false);", "if ($c >= 13) return(true);",
        "%varp7577_gauntlet_tornado_cd = add(%varp7577_gauntlet_tornado_cd, 1);",
        "[proc,gauntlet_floor_warn_ticks]", "[proc,gauntlet_floor_pattern_count]",
        "[proc,gauntlet_floor_rect_count]", "[proc,gauntlet_floor_rect]",
        "[proc,gauntlet_hunllef_trample]",
    ):
        require(HUNLLEF, needle, "Hunllef tornado clock / floor pattern sets")
    require(proc_body(HUNLLEF, "gauntlet_hunllef_special"), "%varp7577_gauntlet_tornado_cd = 0;",
            "the special resets the attack count")
    # Seq 8418 is the tornado summon and 8419 is the one projectile animation
    # the Hunllef plays for BOTH ranged and magic. Using 8418 for the magic
    # attack made every cast read as an incoming tornado wave.
    require(HUNLLEF, "[proc,gauntlet_hunllef_special]\nnpc_anim(hunllef_attack_special, 0);",
            "the special animation belongs to the tornado summon")
    for attack in ("gauntlet_hunllef_act", "gauntlet_hunllef_magic", "gauntlet_hunllef_prayer"):
        assert "hunllef_attack_special" not in proc_body(HUNLLEF, attack), (
            f"{attack}: the magic attack must not play the tornado summon animation"
        )
    require(proc_body(HUNLLEF, "gauntlet_hunllef_act"), "npc_anim(hunllef_attack_ranged, 0);",
            "both standard styles fire hunllef_attack_ranged (14/14 fights)")
    for needle in (
        "[proc,gauntlet_selftest_hit_keeps_target]",
        "[proc,gauntlet_selftest_floor_rects]",
        "[proc,gauntlet_selftest_room_turns]",
        "[proc,gauntlet_selftest_spots]",
    ):
        require(SELFTEST, needle, "::gauntletrun encounter checks")
    for needle in (
        "[oploc1,gauntlet_scoreboard]", "[oploc1,gauntlet_deposit_box]",
        "if_openmain(gauntlet_recipes)", "%varp7374_gauntlet_bryn_intro = 1",
    ):
        require(LOBBY, needle, "lobby interaction")
    # Before Bryn's talk the entrance answers with his own page (RSProx 1144 [3241]).
    require(CORE, "if (%varp7374_gauntlet_bryn_intro = 0) {\n    ~chatnpc_specific_anim(\"Bryn\", gauntlet_instructor, ^chat_angry,",
            "Bryn introduction gate")
    for needle in (
        "[if_button,gauntlet_overlay:timer]", "[if_button,gauntlet_overlay:timer_text]",
        "if_openmain(gauntlet_map);", "[if_button,gauntlet_map:close_button]",
        "~gauntlet_map_mark($start);", "~gauntlet_map_mark(^gauntlet_boss_room);",
        "[proc,gauntlet_map_mark](int $room)",
    ):
        require(MAP_STATE + CORE, needle, "map/UI contract")
    for text in (MAGIC, TELEPORT, HOME_TELEPORT):
        require(text, "%varb9178_player_in_gauntlet = 1", "Gauntlet teleport block")


def main() -> None:
    check_cache_room_manifest()
    check_layout_seeds()
    check_content_contract()
    print("Gauntlet contract OK: cache archetypes, 10,000 layouts, recipes, NPCs, drops, rewards, CAs, lifecycle and Hunllef")


if __name__ == "__main__":
    main()
