#!/usr/bin/env python3
"""Deterministic structural and 10,000-seed checks for The Gauntlet."""

from __future__ import annotations

import json
import re
from collections import deque
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "OSRS-Content/osrs239-content"
GAUNTLET = BASE / "server/scripts/minigames/minigame_gauntlet"
DATA = json.loads((ROOT / "tools/data/gauntlet_contract.json").read_text())
PLAN = (ROOT / "docs/bosses/gauntlet.md").read_text()
LAYOUT = (GAUNTLET / "scripts/gauntlet_layout.rs2").read_text()
CORE = (GAUNTLET / "scripts/gauntlet.rs2").read_text()
CRAFT = (GAUNTLET / "scripts/gauntlet_craft.rs2").read_text()
GATHER = (GAUNTLET / "scripts/gauntlet_gather.rs2").read_text()
MONSTERS = (GAUNTLET / "scripts/gauntlet_monsters.rs2").read_text()
HUNLLEF = (GAUNTLET / "scripts/gauntlet_hunllef.rs2").read_text()
REWARDS = (GAUNTLET / "scripts/gauntlet_rewards.rs2").read_text()
PROGRESS = (GAUNTLET / "scripts/gauntlet_progress.rs2").read_text()
LOBBY = (GAUNTLET / "scripts/gauntlet_lobby.rs2").read_text()
MAP_STATE = (GAUNTLET / "scripts/gauntlet_map_state.rs2").read_text()
NPC_CONFIG = (GAUNTLET / "configs/gauntlet_monsters.npc").read_text()
INV_CONFIG = (GAUNTLET / "configs/gauntlet.inv").read_text()
RECIPE_INTERFACE = (BASE / "interfaces/gauntlet_recipes.if").read_text()
MAGIC = (BASE / "server/scripts/skill_magic/scripts/magic.rs2").read_text()
TELEPORT = (BASE / "server/scripts/skill_magic/scripts/spells/teleport.rs2").read_text()
HOME_TELEPORT = (BASE / "server/scripts/skill_magic/scripts/spells/home_teleport.rs2").read_text()

DIRECTIONS = {"N": (0, 1), "E": (1, 0), "S": (0, -1), "W": (-1, 0)}
OPPOSITE = {"N": "S", "E": "W", "S": "N", "W": "E"}
ALLOWED_MASKS = {
    frozenset("NESW"),
    frozenset("ESW"), frozenset("NSW"), frozenset("NEW"), frozenset("NES"),
    frozenset("SW"), frozenset("NW"), frozenset("NE"), frozenset("ES"),
}


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"{label}: missing {needle!r}")


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


def fixed(x: int, z: int) -> bool:
    return (x, z) in {(3, 3), (3, 2)}


def east(seed: int, x: int, z: int) -> bool:
    if x < 0 or x >= 6 or z < 0 or z >= 7:
        return False
    if fixed(x, z) or fixed(x + 1, z):
        return True
    if seed % 2 == 0 and 1 <= z <= 5 and 1 <= x <= 4:
        parity = (seed + z * 17) % 2
        if x % 2 == 1 - parity:
            return False
    return True


def north(seed: int, x: int, z: int) -> bool:
    if z < 0 or z >= 6 or x < 0 or x >= 7:
        return False
    if fixed(x, z) or fixed(x, z + 1):
        return True
    if seed % 2 == 1 and 1 <= x <= 5 and 1 <= z <= 4:
        parity = (seed + x * 17) % 2
        if z % 2 == 1 - parity:
            return False
    return True


def layout_masks(seed: int) -> dict[tuple[int, int], frozenset[str]]:
    masks = {}
    for x in range(7):
        for z in range(7):
            mask = set()
            if north(seed, x, z):
                mask.add("N")
            if east(seed, x, z):
                mask.add("E")
            if north(seed, x, z - 1):
                mask.add("S")
            if east(seed, x - 1, z):
                mask.add("W")
            masks[(x, z)] = frozenset(mask)
    return masks


def check_layout_seeds() -> None:
    for _mode in ("normal", "corrupted"):
        for seed in range(1, 10_001):
            masks = layout_masks(seed)
            assert masks[(3, 3)] == frozenset("NESW")
            assert masks[(3, 2)] == frozenset("NESW")
            assert all(mask in ALLOWED_MASKS for mask in masks.values())
            for (x, z), mask in masks.items():
                for direction in mask:
                    dx, dz = DIRECTIONS[direction]
                    other = (x + dx, z + dz)
                    assert other in masks, f"seed {seed}: exterior door {(x, z)} {direction}"
                    assert OPPOSITE[direction] in masks[other], (
                        f"seed {seed}: one-way door {(x, z)} {direction}"
                    )
            seen = {(3, 2)}
            queue = deque(seen)
            while queue:
                x, z = queue.popleft()
                for direction in masks[(x, z)]:
                    dx, dz = DIRECTIONS[direction]
                    other = (x + dx, z + dz)
                    if other not in seen:
                        seen.add(other)
                        queue.append(other)
            assert len(seen) == 49, f"seed {seed}: only {len(seen)} rooms connected"


def check_content_contract() -> None:
    for url in DATA["authority"].values():
        require(PLAN, url, "Gauntlet plan authority")
    for needle in DATA["recipe_contract_fragments"]:
        require(CRAFT, needle, "recipe contract")
    assert RECIPE_INTERFACE.count("op1=Sing crystal") == 22
    for entry in range(1, 23):
        require(CRAFT, f"[if_button,gauntlet_recipes:entry{entry}]", f"native recipe entry {entry}")
    for needle in (
        "[proc,gauntlet_recipe_weapon]", "[proc,gauntlet_recipe_armour]",
        "[proc,gauntlet_recipe_vial]", "[proc,gauntlet_recipe_teleport]",
        "[proc,gauntlet_recipe_fish]", "[proc,gauntlet_shard_recipe_has_space]",
        "inv_total(inv, $shard) = $cost", "[proc,gauntlet_sing_tick]",
        "anim(eyeglo_singing_bowl_human, 0);", "p_delay(1);",
    ):
        require(CRAFT, needle, "native recipe dispatch")
    for needle in (
        "%gauntlet_combo_tick ! map_clock", "%gauntlet_combo_tick = map_clock;",
        "%gauntlet_eat_delay = add(map_clock, $delay);", "%action_delay = %gauntlet_eat_delay;",
    ):
        require(CRAFT, needle, "crystal-paddlefish combo timing")
    for npc in DATA["npc_roster"]:
        require(GATHER, f"[ai_queue3,{npc}]", f"{npc} death handler")
        require(GATHER, npc, f"{npc} spawn")
    for npc in ("crystal_dragon", "crystal_dark_beast", "crystal_dragon_hm", "crystal_dark_beast_hm"):
        require(MONSTERS, f"[ai_opplayer2,{npc}]", f"{npc} combat AI")
        require(NPC_CONFIG, f"[{npc}]", f"{npc} NPC overlay")
    for npc in DATA["npc_roster"]:
        require(MONSTERS, f"[ai_spawn,{npc}]", f"{npc} heartbeat startup")
        require(MONSTERS, f"[ai_timer,{npc}]", f"{npc} aggression")
    require(MONSTERS, "[ai_spawn,crystal_hunllef_crystals]", "tornado heartbeat startup")
    require(MONSTERS, "npc_settimer(1);", "Gauntlet actor heartbeat")
    for needle in DATA["drop_contract_fragments"]:
        require(GATHER, needle, "enemy drop contract")
    for needle in (
        "[proc,gauntlet_spawn_random_resources]", "[proc,gauntlet_spawn_resource]",
        "$count = random(6);", "$count = random(3);", "random(5)",
        "loc_change($depleted, ^max_32bit_int);",
        "obj_add_private(npc_coord, $item, $amount, ^lootdrop_duration, 100);",
        "if (inv_freespace(inv) < $spaces)",
        "[proc,gauntlet_light_node_group]", "~gauntlet_light_node_group($clicked);",
    ):
        require(GATHER, needle, "room resource population")

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
    require(prepare, "%pet_insurance_gauntlet = 0", "insured pet ownership")
    require(prepare, "%pet_menagerie_gauntlet = 0", "menagerie pet ownership")
    cape_owned = proc_body(REWARDS, "gauntlet_owns_cape")
    require(cape_owned, "inv_total(worn, gauntlet_crystalline_cape)", "worn cape ownership")
    require(cape_owned, "%gauntlet_cape_rack = 1", "cape-rack ownership")
    require(prepare, "~gauntlet_owns_cape = false", "cape-rack ownership")
    require(INV_CONFIG, "[gauntlet_pending_reward]\nsize=12", "immutable pending rewards")
    deliver = proc_body(REWARDS, "gauntlet_deliver_reward")
    require(deliver, "oc_tradeable($item) = true", "untradeable reward retention")
    require(deliver, "Your unclaimed reward remains safely in the chest.", "pending reward continuation")
    require(deliver, "~gauntlet_pet_award", "automatic Youngllef follower award")

    ids = {int(value) for value in re.findall(r"~ca_task_complete\((\d+)\)", PROGRESS)}
    assert ids == set(DATA["combat_achievement_ids"]), f"CA ids drifted: {sorted(ids)}"
    for needle in (
        "%gauntlet_layout_seed = add(random(32767), 1);",
        "~gauntlet_layout_pick($dx, $dz)", "map_instance_build($handle);",
        "inv_movetoslot(worn, gauntlet_holding_worn, $slot, $slot);",
        "inv_movetoslot(gauntlet_holding_worn, worn, $slot, $slot);",
        "midi_song(^gauntlet_music_track);", "midi_song(-1);",
        "%summoning_pet_active = 1", "%gauntlet_pet_active = 1", "npc_findowned = true",
        "You are already in the starting room.", "~gauntlet_award_completion_cape;",
    ):
        require(CORE, needle, "session/layout contract")
    for needle in (
        "[proc,gauntlet_pet_spawn]", "[timer,gauntlet_pet_follow]",
        "[opheld5,gauntletpet]", "[opnpc4,gauntlet_pet]",
        "[opnpc3,gauntlet_pet]", "[opnpc1,gauntlet_pet]",
        "%total_completed_gauntlet_hm > 0", "%pet_insurance_gauntlet = 1",
        "[oplocu,poh_cos_room_cape_rack_oak]", "[oploc1,poh_cos_room_cape_rack_magic_stone]",
    ):
        require(PROGRESS, needle, "Youngllef follower lifecycle")
    constants = (GAUNTLET / "configs" / "gauntlet.constant").read_text()
    require(constants, "^gauntlet_music_track = 650", "cache music dbrow contract")
    for needle in (
        "[proc,gauntlet_layout_east]", "[proc,gauntlet_layout_north]",
        "[proc,gauntlet_layout_pick]", "modulo(%gauntlet_layout_seed, 2)",
    ):
        require(LAYOUT, needle, "seeded layout contract")
    for needle in (
        "[proc,gauntlet_floor_place]", "[proc,gauntlet_floor_damage_tick]",
        "[proc,gauntlet_spawn_tornadoes]", "[queue,gauntlet_commit_hunllef]",
        "%gauntlet_offpray_hits >= 6", "%gauntlet_hunllef_attacks < 4",
    ):
        require(HUNLLEF, needle, "Hunllef state machine")
    for needle in (
        "[oploc1,gauntlet_scoreboard]", "[oploc1,gauntlet_deposit_box]",
        "if_openmain(gauntlet_recipes)", "%gauntlet_bryn_intro = 1",
    ):
        require(LOBBY, needle, "lobby interaction")
    require(CORE, "npc_findexact(^gauntlet_bryn_coord, gauntlet_instructor)", "Bryn introduction gate")
    for needle in (
        "[if_button,gauntlet_overlay:timer]", "if_opensub(toplevel_osrs_stretch:mainmodal, gauntlet_map, 0)",
        "~gauntlet_mark_room(%gauntlet_start)",
    ):
        require(MAP_STATE + CORE, needle, "map/UI contract")
    for text in (MAGIC, TELEPORT, HOME_TELEPORT):
        require(text, "%player_in_gauntlet = 1", "Gauntlet teleport block")


def main() -> None:
    check_cache_room_manifest()
    check_layout_seeds()
    check_content_contract()
    print("Gauntlet contract OK: cache archetypes, 20,000 layouts, recipes, NPCs, drops, rewards, CAs, lifecycle and Hunllef")


if __name__ == "__main__":
    main()
