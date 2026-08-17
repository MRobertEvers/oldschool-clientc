#!/usr/bin/env python3
"""Deterministic structural checks for the revision-239 God Wars contract.

This complements script compilation: it pins encounter thresholds, special
attack geometry, contribution arithmetic, unique ratios, and the complete
classic combat ledger so a syntactically valid simplification cannot silently
replace one of those rules.
"""

from __future__ import annotations

import csv
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GWD = ROOT / "OSRS-Content/osrs239-content/server/scripts/areas/area_godwars"
NEX = (GWD / "scripts/godwars_nex.rs2").read_text()
NEX_DROPS = (GWD / "scripts/godwars_nex_drops.rs2").read_text()
PRISON = (GWD / "scripts/godwars_prison.rs2").read_text()
PRISON_DROPS = (GWD / "scripts/godwars_prison_drops.rs2").read_text()
FROZEN = (GWD / "scripts/godwars_frozen_door.rs2").read_text()
ENTRANCE = (GWD / "scripts/godwars_entrance.rs2").read_text()
BOSSES = (GWD / "scripts/godwars_bosses.rs2").read_text()
DROPS = (GWD / "scripts/godwars_drops.rs2").read_text()
CHAMBER = (GWD / "scripts/godwars_chamber.rs2").read_text()
PRIVATE = (GWD / "scripts/godwars_private.rs2").read_text()
AMBIENT = (GWD / "scripts/godwars_ambient.rs2").read_text()
NPC = (GWD / "configs/godwars.npc").read_text()
LOC = (GWD / "configs/godwars.loc").read_text()
CONSTANT = (GWD / "configs/godwars.constant").read_text()
COMBAT_PARAM = (
    ROOT / "OSRS-Content/osrs239-content/server/scripts/skill_combat/configs/combat.param"
).read_text()
RANGED_COMBAT = (
    ROOT
    / "OSRS-Content/osrs239-content/server/scripts/skill_combat/scripts/npc_combat_ranged.rs2"
).read_text()
CONTENT_ENGINE = (ROOT / "src/net/mock/mock230_content.c").read_text()
MANIFEST = ROOT / "OSRS-Content/osrs239-content/wiki/godwars_combat_manifest.csv"


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


def script_body(text: str, trigger: str, subject: str) -> str:
    match = re.search(
        rf"^\[{re.escape(trigger)},{re.escape(subject)}\][^\n]*\n(.*?)(?=^\[|\Z)",
        text,
        flags=re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise AssertionError(f"missing [{trigger},{subject}]")
    return match.group(1)


def scaled(pool: int, damage: int, total: int, mvp: bool) -> int:
    quantity = pool * damage // total
    return quantity * 11 // 10 if mvp else quantity


def main() -> None:
    with MANIFEST.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    npcs = {row["gameval"] for row in rows}
    assert len(npcs) == 69, f"classic roster drifted to {len(npcs)} NPCs"
    assert len(rows) == 126, f"classic attack ledger drifted to {len(rows)} rows"
    for row in rows:
        assert all(value.strip() for value in row.values()), f"blank manifest field: {row}"
        assert row["wiki_url"].startswith("https://oldschool.runescape.wiki/w/")
        for handler in ("attack_handler", "drop_handler"):
            assert (ROOT / row[handler]).is_file(), f"missing {handler}: {row[handler]}"

    # Phase floors, five-attack special cadence, and forced melee sequence.
    for floor in (2720, 2040, 1360, 680):
        require(NEX, f"$floor = {floor};", f"Nex phase floor {floor}")
    for mage in ("fumus", "umbra", "cruor", "glacies"):
        require(NEX, f"^nex_{mage}", f"Nex {mage} transition movement")
    require(NEX, "npc_var_set(^nex_var_melee_chain, 2);", "Nex melee triple")
    require(NEX, "[proc,nex_select_standard_target]", "Nex nearest-player selection")
    require(NEX, "[proc,nex_player_defence_bonus]", "Nex raw defence tie-break")
    require(NEX, "return($crushdefence)", "Nex melee defence tie-break")
    require(NEX, "return($rangedefence)", "Nex ranged defence tie-break")
    require(NEX, "return($magicdefence)", "Nex magic defence tie-break")
    require(NEX, "[proc,nex_player_highest_attack_bonus]", "Choke attack-bonus drain")
    require(NEX, "%nex_cough_until = add(map_clock, ^nex_cough_ticks);", "Choke duration reset")
    require(NEX, "[ai_timer,nex]", "eight-tick out-of-combat leap")
    require(NEX, "npc_settimer(8);", "Nex leap cadence")
    require(NEX, "modulo($attacks, ^nex_special_every) = 0", "Nex special cadence")
    require(NEX, "def_int $which = random(2);", "randomised Nex specials")
    require(NEX, "if ($team >= 8)", "eight-player Blood Siphon")
    require(NEX, "npc_type = nex_prison_blood_reaver_boss", "reaver consumption")
    require(NEX, "huntall($target, 1, 0);", "3x3 barrage/prison area")
    require(NEX, "randominc(75)", "Ice Prison maximum")
    require(NEX, "oc_category($weapon) = weapon_salamander", "Ice Prison salamander")
    require(NEX, "~player_attack_roll(%damagetype)", "Ice Prison accuracy roll")
    require(NEX, "add($dx, $dz) = 1", "Ice Prison adjacent displacement")
    require(LOC, "[nex_icicle_1]\nop1=Attack", "attackable outer prison")
    require(CONSTANT, "^nex_prison_defence_level", "named Ice Prison defence")
    require(NEX, "randominc(60)", "Containment maximum")
    require(NEX, "randominc(80)", "Blood Sacrifice maximum")
    require(NEX, "def_int $dash = random(4);", "Smoke Dash direction")
    require(NEX, "p_exactmove(coord, $away", "Smoke Dash forced movement")
    require(NEX, "[proc,nex_try_smoke_drag]", "Smoke Drag")
    require(NEX, "def_int $chance = 4;", "Smoke Drag base chance")
    require(NEX, "if (~check_protect_prayer(^magic_style) = true) { $chance = 8; }", "Smoke Drag prayer chance")
    require(NEX, "$phase = ^nex_phase_smoke & $attacks = 1", "opening Choke")
    shadow = proc_body(NEX, "nex_special_shadow")
    require(shadow, "huntall(npc_coord, ^nex_room_range, 0);", "one Shadow Smash per player")
    require(NEX, "[queue,nex_darkness_tick]", "repeating Embrace Darkness")
    require(NEX, "queue*(nex_darkness_tick, 1)", "one-tick darkness cadence")
    require(CONSTANT, "^nex_darkness_ticks = 25", "Embrace Darkness escape window")
    require(NEX, "stat_sub(prayer, ~gwd_prayer_drain_amount(5), 0)", "Zaros magic Prayer drain")
    require(NEX, "$protected_at_cast = true", "Zaros projectile-time prayer rule")
    require(NEX, "npc_type = nex_soulsplit", "Soul Split healing")
    require(NEX, "npc_type = nex_deflect", "Deflect Melee")

    # Kree's blue attack is ranged-magic: magic accuracy, ranged defence and
    # Protect from Missiles. Both tornado colours share successful knockback,
    # and the Aviansie melee gate preserves the halberd/salamander exceptions.
    kree_magic = proc_body(CHAMBER, "gwd_aoe_kree_magic")
    require(kree_magic, "randominc(~npc_magic_attack_roll)", "Kree magic accuracy")
    require(kree_magic, "~player_defence_roll(^ranged_style)", "Kree ranged defence")
    require(kree_magic, "~check_protect_prayer(^ranged_style)", "Kree ranged prayer")
    require(kree_magic, "queue(gwd_kree_knockback", "Kree magic knockback")
    require(BOSSES, "weapon_polearm", "Aviansie halberd exception")
    require(BOSSES, "weapon_salamander", "Aviansie salamander exception")
    require(BOSSES, "[proc,gwd_prayer_drain_amount]", "spectral Prayer drain helper")
    require(BOSSES, "inv_getobj(worn, ^wearpos_lhand) = spectral", "spectral shield check")
    require(BOSSES, "$prayer_drain = divide(stat(prayer), 4);", "spectral K'ril special")
    assert NEX.count("~gwd_prayer_drain_amount") >= 6
    war = proc_body(AMBIENT, "gwd_war_swing")
    require(war, "npc_param(proj_launch)", "ambient launch spot animation")
    require(war, "npc_param(proj_travel)", "ambient travel projectile")
    require(war, "npc_param(proj_impact)", "ambient impact spot animation")
    require(COMBAT_PARAM, "[proj_impact]", "impact spot-animation parameter")
    require(RANGED_COMBAT, "npc_param(rangebonus_ammo)", "NPC ammunition strength")
    for param in ("rangebonus_ammo", "poison_severity", "proj_launch", "proj_travel", "proj_impact"):
        require(CONTENT_ENGINE, f'"{param}"', f"runtime NPC parameter {param}")

    # The classic tables are exact integer partitions, not approximate rarity
    # labels. Pin every ordinary-table boundary and each nested unique divisor.
    classic_main_bounds = {
        "gwd_drop_graardor_main": [8, 16, 24, 30, 38, 46, 54, 62, 70, 78, 86, 96],
        "gwd_drop_kree_main": [8, 16, 24, 32, 40, 48, 56, 64, 72, 73, 74, 84],
        "gwd_drop_zilyana_main": [8, 16, 24, 32, 40, 46, 54, 62, 70, 78, 86, 87, 97],
        "gwd_drop_kril_main": [8, 16, 24, 31, 33, 41, 49, 57, 65, 73, 81, 91],
        "gwd_bodyguard_bandos_main": [7, 15, 23, 31, 39, 47, 113, 121, 123],
        "gwd_bodyguard_armadyl_main": [7, 15, 23, 31, 39, 109, 117],
        "gwd_bodyguard_saradomin_main": [8, 16, 24, 32, 40, 102, 110, 117],
        "gwd_bodyguard_zamorak_main": [7, 15, 23, 31, 97, 105, 113, 121, 123],
    }
    for name, expected in classic_main_bounds.items():
        actual = [int(value) for value in re.findall(r"\$roll < (\d+)", proc_body(DROPS, name))]
        assert actual == expected, f"{name}: boundaries {actual}, expected {expected}"
    boss_unique_contracts = {
        "godwars_bandos_avatar": ("bandos_chestplate", "random(3)", "random(4)", "random(2)"),
        "godwars_armadyl_avatar": ("armadyl_chestplate", "random(3)", "random(4)", "random(2)"),
        "godwars_saradomin_avatar": ("saradomin_sword", "saradomin_light", "random(4)", "random(2)"),
        "godwars_zamorak_avatar": ("steam_battlestaff", "zamorak_spear", "random(4)", "random(2)"),
    }
    for subject, needles in boss_unique_contracts.items():
        body = script_body(DROPS, "ai_queue3", subject)
        require(body, "random(127)", f"{subject} 127-slot table")
        for needle in needles:
            require(body, needle, f"{subject} unique table")
    for name in (
        "gwd_bodyguard_bandos", "gwd_bodyguard_armadyl",
        "gwd_bodyguard_saradomin", "gwd_bodyguard_zamorak",
    ):
        require(proc_body(DROPS, name), "random(127)", f"{name} 127-slot table")
    require(proc_body(DROPS, "gwd_bodyguard_shard"), "random(12)", "bodyguard shard divisor")

    # CA tier rewards lower all five barriers. Existing essence has priority
    # over a one-use ecumenical key; the obsolete three-charge varp is unused.
    required = proc_body(ENTRANCE, "gwd_kc_required")
    for tier, amount in (("hard", 35), ("elite", 30), ("master", 25), ("grandmaster", 15)):
        require(required, f"%ca_tier_status_{tier} = 2", f"{tier} GWD KC reward")
        require(required, f"return({amount});", f"{tier} GWD KC amount")
    assert "gwd_ecumenical_charges" not in ENTRANCE
    assert "gwd_ecumenical_charges" not in FROZEN
    access = proc_body(ENTRANCE, "gwd_has_chamber_access")
    assert access.index("~gwd_kc_of($faction) >= ~gwd_kc_required") < access.index("inv_total(inv, ecumenical_key)")
    assert FROZEN.index("%godwars_counter_zaros >= $need") < FROZEN.index("inv_total(inv, ecumenical_key)")
    require(ENTRANCE, "stat_base(agility) < ^gwd_agility_rope", "unboostable Saradomin access")
    require(ENTRANCE, "stat_base(strength) < ^gwd_str_bandos_door", "unboostable Bandos access")
    require(ENTRANCE, "stat_base(ranged) < ^gwd_range_grapple", "unboostable Armadyl access")
    require(ENTRANCE, "stat(strength) < ^gwd_strength_boulder", "boostable entrance boulder")
    require(CONSTANT, "^gwd_agility_rope = 70", "Saradomin Agility requirement")
    # All four map squares must keep the HUD live across internal boundaries,
    # then clear every faction counter only after a real dungeon departure.
    for square in ("0_44_82", "0_44_83", "0_45_82", "0_45_83"):
        require(ENTRANCE, f"[mapzone,{square}] ~gwd_enter_dungeon;", f"GWD overlay enter {square}")
        require(ENTRANCE, f"[mapzoneexit,{square}] ~gwd_leave_dungeon;", f"GWD cleanup exit {square}")
    leave = proc_body(ENTRANCE, "gwd_leave_dungeon")
    for faction in ("armadyl", "bandos", "saradomin", "zamorak", "zaros"):
        require(leave, f"%godwars_counter_{faction} = 0;", f"{faction} KC departure reset")
    require(leave, "if (~gwd_in_dungeon = true)", "internal map-square boundary guard")
    require(leave, "~gwd_overlay_close;", "GWD overlay departure cleanup")
    require(ENTRANCE, "[oploc3,godwars_dungeon_door_normal]", "public boss-room Peek")
    require(ENTRANCE, "[oploc3,godwars_dungeon_door_private]", "private-door public-room Peek")
    peek = proc_body(ENTRANCE, "gwd_peek_chamber")
    require(peek, "huntall($centre, 20, 0);", "boss-room occupancy scan")
    require(peek, "if (inzone($sw, $ne, coord) = true)", "boss-room occupancy bounds")
    require(peek, "p_finduid($viewer);", "Peek viewer context restoration")
    # Hard CA personal rooms: exact tier fees, cloned map ownership, complete
    # quartet roster, delayed respawns, and every terminal release path.
    require(ENTRANCE, "~gwd_private_enter($private_faction);", "private-room door entry")
    for tier, fee in (("grandmaster", 75000), ("master", 100000), ("elite", 125000)):
        require(PRIVATE, f"%ca_tier_status_{tier} = 2", f"{tier} private fee tier")
        require(PRIVATE, f"return({fee});", f"{tier} private fee")
    require(PRIVATE, "return(150000);", "Hard private fee")
    require(PRIVATE, "~map_instance_from_square", "private-room map clone")
    require(PRIVATE, "%map_instance_handle = $handle;", "private-room ownership")
    for actor in (
        "godwars_armadyl_avatar", "godwars_armadyl_bodyguard_skree",
        "godwars_armadyl_bodyguard_geerin", "godwars_armadyl_bodyguard_kilisa",
        "godwars_bandos_avatar", "godwars_sergeant_goblin1",
        "godwars_sergeant_goblin2", "godwars_sergeant_goblin3",
        "godwars_saradomin_avatar", "godwars_saradomin_unicorn",
        "godwars_saradomin_lion", "godwars_saradomin_centaur",
        "godwars_zamorak_avatar", "godwars_ancient_greater_demon",
        "godwars_ancient_lesser_demon", "godwars_ancient_black_demon",
    ):
        require(PRIVATE, f"$type = {actor}", f"private spawn {actor}")
    require(PRIVATE, "queue(gwd_private_respawn_npc", "private actor respawn")
    require(PRIVATE, "[softtimer,gwd_private_lifecycle]", "private departure watchdog")
    for terminal in ("gwd_private_altar_exit", "gwd_private_on_death", "gwd_private_on_logout"):
        require(PRIVATE, f"[proc,{terminal}]", f"private terminal {terminal}")
    require(CHAMBER, "map_instance_coord($handle", "instance-relative chamber bounds")
    require(CONSTANT, "^gwd_private_loot_duration = 30000", "three-hour private loot")
    require(FROZEN, "[oploc2,nex_fight_barrier_outer_priv]", "private Nex barrier option")
    require(FROZEN, "~gwd_nex_private_enter;", "private Nex entry dispatch")
    nex_private = proc_body(PRIVATE, "gwd_nex_private_enter")
    require(nex_private, "def_int $fee = 100000;", "private Nex fixed fee")
    require(nex_private, "~gwd_consume_chamber_access(5);", "private Nex essence/key use")
    for actor in ("nex_spawning", "nex_smokemage", "nex_shadowmage", "nex_bloodmage", "nex_icemage"):
        require(PRIVATE, f"$type = {actor}", f"private Nex spawn {actor}")
    require(PRIVATE, "queue(gwd_nex_private_reset", "private Nex respawn")
    require(NEX, "[proc,nex_room_coord]", "instance-relative Nex coordinates")
    for coordinate in ("centre", "fumus", "umbra", "cruor", "glacies"):
        direct = re.compile(rf"(?<!nex_room_coord\()\^nex_{coordinate}\b")
        assert not direct.search(NEX), f"Nex {coordinate} bypasses instance translation"
    for line in NEX_DROPS.splitlines():
        if re.search(r"\bobj_add(?:_private)?\(", line):
            require(line, "~gwd_loot_duration(", "Nex instance-aware loot duration")
    require(PRISON, "~gwd_in_bounds(^gwd_nex_chamber_sw", "private Nex death bank")
    # Frozen surface and its permanent, player-owned fire. Pin both the
    # ten-tick environmental effect and the exact Wiki construction recipe.
    require(ENTRANCE, "[mapzone,0_45_58]", "GWD surface chill entry")
    require(ENTRANCE, "[timer,gwd_surface_chill]", "GWD surface chill timer")
    require(CONSTANT, "^gwd_surface_chill_ticks = 10", "surface chill cadence")
    require(ENTRANCE, "if (%my2arm_fire_gwd = 0)", "Fire of Unseasonal Warmth immunity")
    assert ENTRANCE.count("~gwd_chill_skill(") == 23
    require(ENTRANCE, "%sa_energy = 0;", "surface chill special drain")
    require(ENTRANCE, "healenergy(-10000);", "surface chill run drain")
    require(ENTRANCE, "[oploc1,my2arm_fire_pit_empty]", "unseasonal fire build option")
    for material, amount in (
        ("plank_mahogany", 2),
        ("steel_bar", 2),
        ("red_salt", 100),
        ("blue_salt", 50),
        ("green_salt", 300),
    ):
        require(ENTRANCE, f"inv_del(inv, {material}, {amount});", f"unseasonal fire {material}")
    require(ENTRANCE, "%my2arm_status < ^mf_complete", "unseasonal fire quest gate")
    require(ENTRANCE, "stat_advance(construction, 6000);", "unseasonal fire Construction XP")
    require(ENTRANCE, "stat_advance(firemaking, 3000);", "unseasonal fire Firemaking XP")
    for tier in ("easy", "medium", "hard", "elite", "master", "grandmaster"):
        require(ENTRANCE, f"[opheld3,ca_offhand_{tier}]", f"Ghommal hilt {tier} teleport")
    hilt = proc_body(ENTRANCE, "gwd_hilt_teleport")
    require(hilt, "$limit = 3;", "Ghommal hilt 1 daily limit")
    require(hilt, "$limit = 5;", "Ghommal hilt 2 daily limit")
    require(hilt, "%gwd_hilt_reset_day ! date_runeday()", "Ghommal hilt daily reset")
    require(hilt, "%ca_teleport_count_trollheim = calc(%ca_teleport_count_trollheim + 1)", "Ghommal hilt usage")
    require(hilt, "map_findsquare(^gwd_entrance_stand", "Ghommal hilt boulder destination")
    bridge = proc_body(ENTRANCE, "gwd_ice_bridge")
    require(bridge, "stat_base(hitpoints) < ^gwd_hp_ice_bridge", "unboostable Zamorak HP gate")
    require(bridge, "anim(godwars_human_swim_double, 0);", "Zamorak bridge swim animation")
    require(bridge, "sound_synth(godwars_swim, 1, 1);", "Zamorak bridge swim sound")
    require(bridge, "p_exactmove(coord, $destination, 0, 90, $direction);", "Zamorak bridge exact movement")
    require(bridge, "stat_sub(prayer, stat(prayer), 0);", "Zamorak bridge Prayer drain")
    require(ENTRANCE, "[opheld1,saradomin_light]", "Saradomin's light consume option")
    require(ENTRANCE, "inv_del(inv, saradomin_light, 1);", "Saradomin's light consumption")
    require(ENTRANCE, "%godwars_saradomin_light = 1;", "permanent Zamorak darkness removal")
    require(ENTRANCE, "anim(xbows_human_fire_and_climb_grapple_fast, 0);", "Armadyl grapple animation")
    require(ENTRANCE, "spotanim_pl(xbows_fire_and_climbed_grapple_spot_anim_fast, 0, 0);", "Armadyl grapple player graphic")
    require(ENTRANCE, "projanim_map($grapple_start, $grapple_destination, xbows_grapple_proj", "Armadyl grapple projectile")
    require(ENTRANCE, "p_exactmove($grapple_start, $grapple_destination, 60, 210", "Armadyl grapple exact movement")
    gong = proc_body(ENTRANCE, "gwd_gong_animation")
    for tool in ("hammer", "imcando_hammer", "dragon_warhammer", "elder_maul"):
        require(gong, f"inv_total(inv, {tool})", f"Bandos gong {tool}")
    for metal in ("bronze", "iron", "steel", "black", "white", "mithril", "adamant", "rune"):
        require(gong, f"godwars_hammer_gong_{metal}warhammer", f"Bandos gong {metal} animation")
    require(ENTRANCE, "anim($gong, 0);", "Bandos selected gong animation")

    # The public barrier must both admit the player and demand-spawn the actor.
    assert FROZEN.count("~nex_spawn_if_needed;") == 2
    require(NEX, "[ai_spawn,nex_spawning]", "Nex introduction actor")
    require(NEX, "npc_changetype(nex, ^max_32bit_int);", "Nex intro transform")

    # Wrath is a delayed 5x5, max-50 hit. Unique switch must retain 1:2:2:2:2:3.
    require(NEX_DROPS, "huntall($death, 2, 0);", "Wrath 5x5 warning")
    require(NEX_DROPS, "if (distance(coord, $death) > 2)", "Wrath 5x5 landing")
    require(NEX_DROPS, "randominc(50)", "Wrath maximum")
    unique = proc_body(NEX_DROPS, "nex_drop_unique_private_roll")
    require(proc_body(NEX_DROPS, "nex_drop_unique_private"),
            "~nex_drop_unique_private_roll($where, random(12));",
            "Nex unique random-roll wrapper")
    cases = re.findall(r"case ([0-9, ]+) : obj_add_private\([^,]+, ([a-zA-Z0-9_]+)", unique)
    weights = {item: len([n for n in labels.split(",") if n.strip()]) for labels, item in cases}
    assert sorted(weights.values()) == [1, 2, 2, 2, 2, 3], weights
    assert sum(weights.values()) == 12

    # Contribution quantities use integer floor then the documented MVP 10%.
    assert scaled(100, 25, 100, False) == 25
    assert scaled(100, 50, 100, True) == 55
    assert scaled(85, 40, 100, False) == 34
    require(NEX_DROPS, "^nex_loot_minimum_damage", "Nex eligibility floor")
    require(NEX_DROPS, "random(43000) < $chance", "1/43 contribution unique")
    require(NEX_DROPS, "random(500000) < $chance", "1/500 contribution pet")
    require(NEX_DROPS, "random(48000) <", "1/48 contribution elite clue")

    # Each Ancient Prison main table receives rolls 0..125 after its two
    # top-level 1/128 ceremonial/gem slots. Thresholds must be monotonic and
    # leave a final else branch to cover the tail without gaps.
    for name in (
        "gwd_prison_warrior_main",
        "gwd_prison_ranger_main",
        "gwd_prison_mage_main",
        "gwd_prison_reaver_main",
    ):
        body = proc_body(PRISON_DROPS, name)
        bounds = [int(value) for value in re.findall(r"\$roll < (\d+)", body)]
        assert bounds == sorted(set(bounds)), f"{name}: non-monotonic thresholds"
        assert bounds and bounds[-1] <= 125 and "else {" in body, name

    for effect in (
        "queue(poison_player, 0, 16)",
        "stat_sub(attack, 15, 0)",
        "npc_statheal(hitpoints, max(divide($amount, 5), 1), 0)",
        "queue(npc_freeze_player, 0, 6)",
    ):
        require(PRISON, effect, "Zarosian spiritual mage effects")
    for sound in (
        "nex2021_blood_reaver_attack",
        "nex2021_blood_reaver_defend",
        "nex2021_blood_reaver_death",
    ):
        require(NPC, sound, "Blood Reaver audiovisual contract")

    print("God Wars contract OK: 69 NPCs, 126 attacks, Nex/Prison invariants")


if __name__ == "__main__":
    main()
