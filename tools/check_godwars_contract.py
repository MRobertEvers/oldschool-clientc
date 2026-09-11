#!/usr/bin/env python3
"""Deterministic structural checks for the revision-239 God Wars contract.

This complements script compilation: it pins encounter thresholds, special
attack geometry, contribution arithmetic, unique ratios, and the complete
classic combat ledger so a syntactically valid simplification cannot silently
replace one of those rules.
"""

from __future__ import annotations

import csv
import hashlib
import re
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GWD = ROOT / "OSRS-Content/osrs239-content/server/scripts/areas/area_godwars"
NEX = (GWD / "scripts/godwars_nex.rs2").read_text()
NEX_DROPS = (GWD / "scripts/godwars_nex_drops.rs2").read_text()
PRISON = (GWD / "scripts/godwars_prison.rs2").read_text()
PRISON_DROPS = (GWD / "scripts/godwars_prison_drops.rs2").read_text()
AMBIENT_DROPS = (GWD / "scripts/godwars_ambient_drops.rs2").read_text()
DROPS = (GWD / "scripts/godwars_drops.rs2").read_text()
WIKI_IMP = (
    ROOT / "OSRS-Content/osrs239-content/server/scripts/drop_tables/scripts/wiki_imp.rs2"
).read_text()
FROZEN = (GWD / "scripts/godwars_frozen_door.rs2").read_text()
ENTRANCE = (GWD / "scripts/godwars_entrance.rs2").read_text()
BOSSES = (GWD / "scripts/godwars_bosses.rs2").read_text()
CHAMBER = (GWD / "scripts/godwars_chamber.rs2").read_text()
PRIVATE = (GWD / "scripts/godwars_private.rs2").read_text()
AMBIENT = (GWD / "scripts/godwars_ambient.rs2").read_text()
AGGRESSION = (GWD / "scripts/godwars_aggression.rs2").read_text()
GODSWORD = (GWD / "scripts/godwars_godsword.rs2").read_text()
GWD_VARP = (GWD / "configs/godwars.varp").read_text()
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
ANCIENT_GODSWORD = (
    ROOT
    / "OSRS-Content/osrs239-content/server/scripts/skill_combat/scripts/player/specs/pvm_ancient_godsword.rs2"
).read_text()
SARADOMIN_GODSWORD = (
    ROOT
    / "OSRS-Content/osrs239-content/server/scripts/skill_combat/scripts/player/specs/pvm_sgs.rs2"
).read_text()
ZAMORAK_GODSWORD = (
    ROOT
    / "OSRS-Content/osrs239-content/server/scripts/skill_combat/scripts/player/specs/pvm_zgs.rs2"
).read_text()
PLAYER_LOGIN = (
    ROOT / "OSRS-Content/osrs239-content/server/scripts/player/login.rs2"
).read_text()
PLAYER_LOGOUT = (
    ROOT / "OSRS-Content/osrs239-content/server/scripts/player/logout.rs2"
).read_text()
PLAYER_DEATH = (
    ROOT / "OSRS-Content/osrs239-content/server/scripts/player/death.rs2"
).read_text()
SLAYER_LEVEL_GATE = (
    ROOT
    / "OSRS-Content/osrs239-content/server/scripts/skill_slayer/scripts/slayer_level_gate.rs2"
).read_text()
CONTENT_ENGINE = (ROOT / "src/torirsserver/torirs_server_content.c").read_text()
SCRIPT_HOST = (ROOT / "src/torirsserver/torirs_server_scripts.c").read_text()
MANIFEST = ROOT / "OSRS-Content/osrs239-content/wiki/godwars_combat_manifest.csv"


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"{label}: missing {needle!r}")


def literal_drop_ledger(filename: str, text: str) -> list[tuple[str, ...]]:
    """Return context/object/quantity rows for literal authored obj_add calls."""
    rows: list[tuple[str, ...]] = []
    context = ""
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("["):
            context = line
        for match in re.finditer(r"\b(obj_add(?:_private)?)\(", line):
            depth = 1
            cursor = match.end()
            start = cursor
            while cursor < len(line) and depth:
                if line[cursor] == "(":
                    depth += 1
                elif line[cursor] == ")":
                    depth -= 1
                cursor += 1
            if depth:
                continue
            body = line[start:cursor - 1]
            args: list[str] = []
            arg_start = 0
            nested = 0
            for index, char in enumerate(body):
                if char == "(":
                    nested += 1
                elif char == ")":
                    nested -= 1
                elif char == "," and nested == 0:
                    args.append(body[arg_start:index].strip())
                    arg_start = index + 1
            args.append(body[arg_start:].strip())
            if len(args) < 3 or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", args[1]):
                continue
            rows.append((filename, context, match.group(1), args[1],
                         " ".join(args[2].split())))
    return rows


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
        rf"^\[{re.escape(trigger)},{re.escape(subject)}\][^\n]*\n"
        rf"(?:^\[[^\n]+\]\n)*(.*?)(?=^\[|\Z)",
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

    # A repeated trigger header is ambiguous even when adjacent declarations
    # happen to compile into one trigger chain. Every scoped player-attack
    # override must be declared exactly once.
    attack_overrides = re.findall(r"^\[opnpc2,(godwars_[^\]]+)\]$", BOSSES, re.MULTILINE)
    duplicates = sorted({name for name in attack_overrides if attack_overrides.count(name) > 1})
    assert not duplicates, f"duplicate GWD attack override(s): {', '.join(duplicates)}"
    for faction, count in (("bandos", 11), ("saradomin", 10), ("zamorak", 9)):
        wrapper = proc_body(BOSSES, f"gwd_try_cry_{faction}")
        require(wrapper, f"~gwd_cry_{faction}_roll(random({count}));", f"{faction} cry selector")
        require(BOSSES, f"[proc,gwd_cry_{faction}_roll]", f"{faction} deterministic cries")
    require(proc_body(BOSSES, "gwd_try_cry_armadyl"), "~gwd_cry_armadyl;", "Armadyl cry selector")
    require(BOSSES, "[proc,gwd_cry_armadyl]", "Armadyl deterministic cry")
    for faction, token in (
        (1, "armadyl"), (2, "bandos"), (3, "saradomin"),
        (4, "zamorak"), (5, "~gwd_is_zaros_item"),
    ):
        require(AGGRESSION, token, f"faction {faction} god-item classification")
    require(AGGRESSION, 'string_indexof_string("ancient mace", $name)', "Ancient mace Zaros exclusion")
    require(AGGRESSION, 'string_indexof_string("unholy", $name) < 0', "Zamorak/Saradomin holy-name separation")
    require(AGGRESSION, "~gwd_god_item_count($faction) > 0", "live worn-item protection lookup")
    slayer_override = proc_body(SLAYER_LEVEL_GATE, "slayer_level_override")
    for npc, level in (("godwars_pyrefiend_1", 30), ("godwars_bloodveld", 50)):
        require(slayer_override, f"npc_type = {npc}", f"{npc} Slayer override")
        require(slayer_override, f"return({level});", f"{npc} Slayer level {level}")

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
    turmoil = proc_body(NEX, "nex_turmoil_transfer")
    require(CONSTANT, "^nex_turmoil_drain_levels = 2", "named Turmoil drain quantum")
    for stat in ("attack", "strength", "defence", "ranged", "magic"):
        require(turmoil, f"stat_sub({stat}, ^nex_turmoil_drain_levels, 0);",
                f"Turmoil player {stat} drain")
        require(turmoil, f"npc_statheal({stat}, ${stat}_drain, 0);",
                f"Turmoil Nex {stat} transfer")
    require(turmoil, "nex_spot_leech_projanim_defence", "Turmoil leech projectile")
    require(turmoil, "nex_spot_leech_impact_spotanim_defence", "Turmoil leech impact")
    require(turmoil, "mah4_curses_leech_launch", "Turmoil leech launch sound")
    require(turmoil, "mah4_curses_leech_impact", "Turmoil leech impact sound")
    damage_player = proc_body(NEX, "nex_damage_player")
    require(damage_player, "npc_var_get(^nex_var_phase) = ^nex_phase_zaros",
            "Turmoil final-phase gate")
    require(damage_player, "~nex_turmoil_transfer($source);", "Turmoil landed-hit dispatch")
    for cry in (
        "Fill my soul with smoke!", "Fumus, don't fail me!",
        "Umbra, don't fail me!", "Cruor, don't fail me!",
        "Glacies, don't fail me!", "Darken my shadow!",
        "Flood my lungs with blood!", "Infuse me with the power of ice!",
        "NOW, THE POWER OF ZAROS!", "Let the virus flow through you!",
        "There is... NO ESCAPE!", "Fear the shadow!", "Embrace darkness!",
        "A siphon will solve this!", "I demand a blood sacrifice!",
        "Contain this!", "Die now, in a prison of ice!",
    ):
        require(NEX, f'npc_say("{cry}")', f"Nex cry {cry}")
    require(NEX_DROPS, 'npc_say("Taste my wrath!")', "Nex Wrath cry")
    score = proc_body(NEX_DROPS, "nex_finish_personal_score")
    require(score, "%total_nex_kills = calc(%total_nex_kills + 1);", "Nex kill scoreboard")
    require(score, "%nex_personal_best_ticks = $duration;", "Nex personal-best scoreboard")
    require(score, "~nex_clear_contribution;", "Nex scored contribution cleanup")
    contribution_cleanup = proc_body(NEX, "nex_clear_contribution")
    for state in ("nex_contribution_uid", "nex_contribution_damage", "nex_encounter_start"):
        require(contribution_cleanup, f"%{state} = 0;", f"Nex cleanup clears {state}")
    require(NEX_DROPS, "else if (%nex_contribution_uid = $dead)",
            "sub-threshold Nex contribution cleanup")
    require(proc_body(FROZEN, "gwd_nex_barrier"), "~nex_clear_contribution;",
            "public Nex arena-exit contribution cleanup")
    require(proc_body(FROZEN, "gwd_frozen_door_leave"), "~nex_clear_contribution;",
            "Ancient Prison departure contribution cleanup")
    private_clear = proc_body(PRIVATE, "gwd_private_clear_player")
    require(private_clear, "~nex_clear_contribution;",
            "private player contribution cleanup")
    for terminal in ("gwd_private_release", "gwd_private_on_death"):
        require(proc_body(PRIVATE, terminal), "~gwd_private_clear_player(",
                f"{terminal} player cleanup routing")
    require(proc_body(PRIVATE, "gwd_private_on_logout"), "~gwd_private_release(",
            "private logout cleanup routing")

    # Kree's blue attack is ranged-magic: magic accuracy, ranged defence and
    # Protect from Missiles. Both tornado colours share successful knockback,
    # and the Aviansie melee gate preserves the halberd/salamander exceptions.
    kree_magic = proc_body(CHAMBER, "gwd_aoe_kree_magic")
    require(kree_magic, "randominc(~npc_magic_attack_roll)", "Kree magic accuracy")
    require(kree_magic, "~player_defence_roll(^ranged_style)", "Kree ranged defence")
    require(kree_magic, "~check_protect_prayer(^ranged_style)", "Kree ranged prayer")
    require(kree_magic, "queue(gwd_kree_knockback", "Kree magic knockback")
    require(BOSSES, "sound_synth(godwars_armadyl_magic_whirlwind, 1, 0);", "Kree magic whirlwind sound")
    require(BOSSES, "weapon_polearm", "Aviansie halberd exception")
    require(BOSSES, "weapon_salamander", "Aviansie salamander exception")
    require(BOSSES, "[proc,gwd_prayer_drain_amount]", "spectral Prayer drain helper")
    require(BOSSES, "inv_getobj(worn, ^wearpos_lhand) = spectral", "spectral shield check")
    require(BOSSES, "$prayer_drain = divide(stat(prayer), 4);", "spectral K'ril special")
    bodyguard_ranged = proc_body(BOSSES, "gwd_bodyguard_ranged")
    require(bodyguard_ranged, "~gwd_bodyguard_ranged_hit", "bodyguard ranged accuracy seam")
    bodyguard_ranged_hit = proc_body(BOSSES, "gwd_bodyguard_ranged_hit")
    require(bodyguard_ranged_hit, "randominc(~npc_ranged_attack_roll)",
            "bodyguard ranged attack roll")
    require(bodyguard_ranged_hit, "randominc(~player_defence_roll(^ranged_style))",
            "bodyguard ranged defence roll")
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
    primary_selector = "~gwd_primary_roll_127"
    require(
        proc_body(DROPS, "gwd_primary_roll_127"),
        "random(127)",
        "shared classic 127-slot selector",
    )
    for subject, needles in boss_unique_contracts.items():
        body = script_body(DROPS, "ai_queue3", subject)
        require(body, primary_selector, f"{subject} 127-slot table")
        for needle in needles:
            require(body, needle, f"{subject} unique table")
    for name in (
        "gwd_bodyguard_bandos", "gwd_bodyguard_armadyl",
        "gwd_bodyguard_saradomin", "gwd_bodyguard_zamorak",
    ):
        require(proc_body(DROPS, name), primary_selector, f"{name} 127-slot table")
    require(proc_body(DROPS, "gwd_bodyguard_shard"), "random(12)", "bodyguard shard divisor")
    boss_tertiary = proc_body(DROPS, "gwd_boss_tertiary_code")
    require(boss_tertiary, "random(~gwd_boss_elite_clue_rate)",
            "classic boss conditional elite clue rate")
    require(boss_tertiary, "random(5000)", "classic boss pet rate")
    boss_award = proc_body(DROPS, "gwd_drop_boss_tertiary_roll")
    require(boss_award, "modulo($code, 2) = 1", "independent elite clue bit")
    require(boss_award, "$code >= 2", "independent pet bit")
    require(
        proc_body(DROPS, "gwd_bodyguard_tertiary_code"),
        "random(128)",
        "classic bodyguard hard clue rate",
    )

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
    take_access = proc_body(ENTRANCE, "gwd_take_chamber_access")
    assert take_access.index("~gwd_kc_of($faction) >= $need") < take_access.index("inv_total(inv, ecumenical_key)")
    require(take_access, "return(true);", "atomic GWD entry commit")
    require(take_access, "return(false);", "duplicate GWD entry refusal")
    require(proc_body(ENTRANCE, "gwd_open_chamber"),
            "~gwd_take_chamber_access($f)", "public chamber atomic entry")
    require(proc_body(FROZEN, "gwd_nex_barrier"),
            "~gwd_take_chamber_access(5)", "public Nex atomic entry")
    for name, faction in (("gwd_private_enter", "$faction"),
                          ("gwd_nex_private_enter", "5")):
        body = proc_body(PRIVATE, name)
        require(body, f"~gwd_take_chamber_access({faction})", f"{name} atomic entry")
        assert body.index(f"~gwd_take_chamber_access({faction})") < body.index("inv_del(inv, coins, $fee)")
        require(body, "map_instance_free($handle);", f"{name} failed-commit release")
    access_level = proc_body(ENTRANCE, "gwd_access_level")
    require(access_level, "stat($skill) >= $required", "boostable GWD access predicate")
    require(access_level, "stat_base($skill) >= $required", "base-level GWD access predicate")
    for call, label in (
        ("~gwd_access_level(agility, ^gwd_agility_rope, false)", "unboostable Saradomin access"),
        ("~gwd_access_level(strength, ^gwd_strength_boulder, true)", "boostable entrance boulder"),
        ("~gwd_access_level(agility, ^gwd_agility_crack, true)", "boostable entrance crack"),
        ("~gwd_access_level(hitpoints, ^gwd_hp_ice_bridge, false)", "unboostable Zamorak HP gate"),
        ("~gwd_access_level(strength, ^gwd_str_bandos_door, false)", "unboostable Bandos access"),
        ("~gwd_access_level(ranged, ^gwd_range_grapple, false)", "unboostable Armadyl access"),
    ):
        require(ENTRANCE, call, label)
    for skill, boostable in (("agility", "false"), ("ranged", "false"),
                             ("strength", "false"), ("hitpoints", "true")):
        require(FROZEN, f"~gwd_access_level({skill}, 70, {boostable})", f"Frozen Door {skill} gate")
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
    require(FROZEN, "~gwd_nex_private_join;", "private Nex friend-join dispatch")
    require(FROZEN, "Join a friend's private Nex room.", "private Nex friend-join choice")
    require(CONSTANT, "^gwd_nex_private_cap = 20", "private Nex population cap")
    nex_private = proc_body(PRIVATE, "gwd_nex_private_enter")
    require(nex_private, "def_int $fee = 100000;", "private Nex fixed fee")
    require(nex_private, "~gwd_take_chamber_access(5)", "private Nex essence/key use")
    for actor in ("nex_spawning", "nex_smokemage", "nex_shadowmage", "nex_bloodmage", "nex_icemage"):
        require(PRIVATE, f"$type = {actor}", f"private Nex spawn {actor}")
    require(PRIVATE, "queue(gwd_nex_private_reset", "private Nex respawn")
    nex_join = proc_body(PRIVATE, "gwd_nex_private_join")
    for needle, label in (
        ('p_namedialog("Enter the private Nex room owner\'s name:")', "owner-name dialogue"),
        ("def_string $host_name = last_string;", "owner-name reply"),
        ("p_findmutualfriend($host_name)", "mutual online friend lookup"),
        ("map_instance_owner($handle) ! $owner", "creator ownership validation"),
        ("huntall(~gwd_private_coord($handle, ^nex_centre), 64, 0);", "instance population scan"),
        ("~gwd_nex_private_has_space($population)", "twenty-player cap gate"),
        ("~gwd_take_chamber_access(5)", "guest essence/key consumption"),
        ("%map_instance_handle = $handle;", "guest room handle binding"),
        ("%gwd_private_faction = 5;", "guest Nex-room binding"),
    ):
        require(nex_join, needle, f"private Nex friend join {label}")
    nex_has_space = proc_body(PRIVATE, "gwd_nex_private_has_space")
    require(nex_has_space, "if ($population < ^gwd_nex_private_cap) { return(true); }",
            "private Nex strict population cap")
    require(nex_has_space, "return(false);", "private Nex full-room rejection")
    private_release = proc_body(PRIVATE, "gwd_private_release")
    require(private_release, "if ($owner ! uid)", "private guest departure branch")
    require(private_release, "~gwd_private_clear_player($handle, $faction);", "private guest cleanup")
    require(private_release, "~gwd_private_eject_guests($handle, $faction, $owner);", "private owner guest ejection")
    private_reset = proc_body(PRIVATE, "gwd_nex_private_schedule_reset")
    require(private_reset, "map_instance_owner($handle)", "private Nex owner reset routing")
    require(private_reset, "p_finduid($owner)", "private Nex owner reset context")
    require(SCRIPT_HOST, "case SS_OP_P_NAMEDIALOG:", "runtime name-dialogue opcode")
    require(SCRIPT_HOST, "case SS_OP_LAST_STRING:", "runtime last-string opcode")
    require(SCRIPT_HOST, "case SS_OP_P_FINDMUTUALFRIEND:", "runtime mutual-friend opcode")
    assert SCRIPT_HOST.count("ToriRSServer_FriendsIsFriend(") >= 2, "mutual-friend lookup must be reciprocal"
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
    require(bridge, "~gwd_access_level(hitpoints, ^gwd_hp_ice_bridge, false)", "unboostable Zamorak HP gate")
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

    # Complete Godsword construction and the Ancient variant's delayed PvM
    # special. The queue stores source delay+1, so source 7 is eight real ticks.
    for part in (
        "godwars_godsword_blade1", "godwars_godsword_blade2",
        "godwars_godsword_blade3", "godwars_godsword_blade1+2",
        "godwars_godsword_blade1+3", "godwars_godsword_blade2+3",
    ):
        require(GODSWORD, part, f"Godsword shard route {part}")
    require(GODSWORD, "stat(smithing) < 80", "Godsword Smithing level")
    require(GODSWORD, "~has_smithing_hammer", "Godsword hammer gate")
    require(GODSWORD, "stat_advance(smithing, 2000)", "Godsword 200 XP completion")
    for sword in ("ags", "bgs", "sgs", "zgs", "ancient_godsword"):
        require(GODSWORD, f"[opheld3,{sword}]", f"{sword} dismantle option")
    require(
        GODSWORD,
        "$sword = ancient_godsword & %ancient_godsword_mark_count > 0",
        "active Blood Sacrifice dismantle lock",
    )
    require(GWD_VARP, "[ancient_godsword_mark_count]", "Blood Sacrifice mark count")
    mark_decl = GWD_VARP.split("[ancient_godsword_mark_count]", 1)[1].split("[", 1)[0]
    require(mark_decl, "scope=temp", "Blood Sacrifice temporary state")
    require(ANCIENT_GODSWORD, "anim(ngs_special_player, 0);", "Blood Sacrifice animation 9171")
    require(ANCIENT_GODSWORD, "spotanim_pl(ngs_special_spotanim, 0, 0);", "Blood Sacrifice graphic 1996")
    require(ANCIENT_GODSWORD, "sound_synth(blood_sacrifice, 1, 0);", "Blood Sacrifice sound 2911")
    require(ANCIENT_GODSWORD, "multiply(~player_attack_roll(%damagetype), 2)", "Blood Sacrifice accuracy")
    require(ANCIENT_GODSWORD, "scale(110, 100, %com_maxhit)", "Blood Sacrifice primary damage")
    require(
        ANCIENT_GODSWORD,
        "queue*(ancient_godsword_sacrifice, 7)(npc_uid, npc_basestat(hitpoints), map_instance_find(coord));",
        "Blood Sacrifice eight-real-tick queue",
    )
    sacrifice = script_body(ANCIENT_GODSWORD, "queue", "ancient_godsword_sacrifice")
    require(sacrifice, "map_instance_find(coord) ! $instance", "attacker instance cancellation")
    require(sacrifice, "map_instance_find(npc_coord) ! $instance", "target instance cancellation")
    require(sacrifice, "distance(coord, npc_coord) >= 5", "Blood Sacrifice escape radius")
    require(sacrifice, "def_int $dealt = min(25, npc_stat(hitpoints));", "Blood Sacrifice typeless cap")
    require(sacrifice, "npc_damage(hitsplat_damage, $dealt);", "Blood Sacrifice typeless damage")
    heal = proc_body(ANCIENT_GODSWORD, "ancient_godsword_heal_amount")
    require(heal, "multiply($base_hitpoints, 15)", "Blood Sacrifice 15% heal")
    require(heal, "min(25", "Blood Sacrifice NPC heal cap")
    require(heal, "$damage_dealt", "Blood Sacrifice actual-damage heal cap")
    clear_marks = proc_body(ANCIENT_GODSWORD, "ancient_godsword_clear_marks")
    require(clear_marks, "clearqueue(ancient_godsword_sacrifice);", "Blood Sacrifice queue cleanup")
    require(clear_marks, "%ancient_godsword_mark_count = 0;", "Blood Sacrifice count cleanup")
    require(PLAYER_LOGIN, "~ancient_godsword_clear_marks;", "Blood Sacrifice login cleanup")
    require(PLAYER_LOGOUT, "~ancient_godsword_clear_marks;", "Blood Sacrifice logout cleanup")
    require(PLAYER_DEATH, "~ancient_godsword_clear_marks;", "Blood Sacrifice death cleanup")

    # Healing Blade restores from the successful potential-damage roll, not
    # capped/actual damage, and retains its guaranteed restoration minima.
    require(SARADOMIN_GODSWORD, "multiply(~player_attack_roll(%damagetype), 2)", "Healing Blade accuracy")
    require(SARADOMIN_GODSWORD, "scale(110, 100, %com_maxhit)", "Healing Blade primary damage")
    require(SARADOMIN_GODSWORD, "if ($hit = true)", "Healing Blade successful-hit restoration")
    require(SARADOMIN_GODSWORD, "~pvm_sgs_heal_amount($damage)", "Healing Blade pre-overkill HP basis")
    require(SARADOMIN_GODSWORD, "~pvm_sgs_prayer_amount($damage)", "Healing Blade pre-overkill Prayer basis")
    require(proc_body(SARADOMIN_GODSWORD, "pvm_sgs_heal_amount"), "max(10", "Healing Blade HP minimum")
    require(proc_body(SARADOMIN_GODSWORD, "pvm_sgs_prayer_amount"), "max(5", "Healing Blade Prayer minimum")
    require(SARADOMIN_GODSWORD, "anim(sgs_special_player, 0);", "Healing Blade animation 7640")
    require(SARADOMIN_GODSWORD, "spotanim_pl(dh_sword_update_saradomin_special_spotanim, 0, 0);", "Healing Blade graphic 1209")
    require(SARADOMIN_GODSWORD, "sound_synth(godwars_godsword_special_attack, 1, 0);", "Healing Blade sound 3869")
    require(ZAMORAK_GODSWORD, "~pvm_zgs_freeze_ticks($hit)", "Ice Cleave accuracy-result freeze")
    require(proc_body(ZAMORAK_GODSWORD, "pvm_zgs_freeze_ticks"), "return(32);", "Ice Cleave successful-zero freeze")
    require(ZAMORAK_GODSWORD, "spotanim_npc(ice_barrage_impact, 92, 0);", "Ice Cleave impact graphic")
    require(ZAMORAK_GODSWORD, "npc_freeze($freeze_ticks);", "Ice Cleave 32-tick freeze")

    # The public barrier must both admit the player and demand-spawn the actor.
    assert FROZEN.count("~nex_spawn_if_needed;") == 1
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
    personal = proc_body(NEX_DROPS, "nex_personal_loot_code")
    require(personal, "random(43000) < $unique_chance", "1/43 contribution unique")
    require(personal, "random(500000) < $unique_chance", "1/500 contribution pet")
    require(personal, "random(48000) < $base_chance", "1/48 contribution elite clue")
    require(personal, "$code = calc($code + 2);", "independent Nex pet bit")
    require(personal, "$code = calc($code + 4);", "independent Nex clue bit")
    award = proc_body(NEX_DROPS, "nex_award_personal_loot_roll")
    require(award, "modulo($code, 2) = 1", "Nex unique award bit")
    require(award, "modulo(divide($code, 2), 2) = 1", "Nex pet award bit")
    require(award, "if ($code >= 4)", "Nex clue award bit")
    require(award, "~nex_drop_unique_private($where);", "Nex unique award")
    require(award, "nexpet", "Nex pet award")
    require(award, "~gwd_clue_drop_obj(4, trail_elite_emote_exp1, $boxed)",
            "Nex conditional elite clue/box award")
    require(award, "~gwd_record_pet(nexpet);", "Nex pet insurance/log award")
    death = script_body(NEX_DROPS, "ai_queue3", "nex")
    require(death, "def_int $base_chance = divide(multiply($damage, 1000), $total);",
            "Nex base contribution chance")
    require(death, "~nex_personal_loot_code($chance, $base_chance)",
            "Nex personal selector invocation")
    require(death, "~nex_award_personal_loot_roll($death,",
            "Nex personal award invocation")
    require(SCRIPT_HOST, "ToriRSServer_WorldObjAddPrivate(srv, player,",
            "hunt-player private floor-object ownership")
    require(SCRIPT_HOST, "srv, player, (int)values[1], (int)values[2]",
            "hunt-player private loot-tracker ownership")

    # Every custom death handler must either emit its configured remains or
    # explicitly document why its exact Wiki table supersedes the cache param.
    require(AMBIENT_DROPS, "npc_param(death_drop) ! null",
            "ambient configured-remains null guard")
    require(AMBIENT_DROPS, "obj_add(npc_coord, npc_param(death_drop), 1",
            "ambient configured-remains drop")
    require(AMBIENT_DROPS, "no-death-drop: every handler in this file delegates",
            "ambient delegated-remains audit waiver")
    require(DROPS, "no-death-drop: every classic boss/bodyguard handler below states",
            "classic exact Always-row audit waiver")
    for remains in ("big_bones", "bones", "infernal_ashes", "malicious_ashes"):
        require(DROPS, f"obj_add($where, {remains}, 1",
                f"classic explicit {remains} Always row")
    require(NEX, "no-death-drop: Fumus, Umbra, Cruor and Glacies",
            "Nex phase-mage null-remains waiver")
    require(NEX_DROPS, "no-death-drop: Nex uses contribution loot",
            "Nex null-remains waiver")
    require(PRISON_DROPS, "obj_add($where, npc_param(death_drop), 1",
            "ordinary Blood Reaver configured malicious ashes")
    for soldier in ("warrior", "ranger", "mage"):
        require(PRISON_DROPS,
                f"no-death-drop: this Zarosian {soldier}'s cache death_drop is null",
                f"Zarosian {soldier} null-remains waiver")

    # Exact inclusive quantity expressions across the four authored GWD drop
    # files. Counts matter: losing one of two identical rows is still drift.
    quantity_text = DROPS + AMBIENT_DROPS + PRISON_DROPS + NEX_DROPS
    actual_ranges = Counter(
        (int(low), int(high))
        for low, high in re.findall(
            r"~random_range\((\d+),\s*(\d+)\)", quantity_text
        )
    )
    expected_ranges = Counter({
        (1, 2): 1, (1, 4): 4, (1, 5): 1, (1, 10): 5,
        (1, 11): 1, (1, 16): 1, (1, 10000): 1, (2, 5): 1,
        (2, 7): 2, (2, 30): 1, (3, 64): 1, (3, 80): 1,
        (3, 102): 1, (4, 13): 1, (4, 64): 1, (5, 9): 1,
        (5, 10): 5, (5, 204): 1, (6, 39): 1, (6, 100): 1,
        (8, 13): 1, (10, 15): 1, (11, 29): 1, (12, 283): 1,
        (15, 20): 4, (20, 25): 1, (23, 2354): 1, (25, 30): 1,
        (30, 60): 1, (35, 40): 1, (38, 43): 1, (38, 98): 1,
        (40, 45): 2, (40, 70): 1, (42, 2252): 1, (62, 67): 1,
        (65, 70): 1, (80, 85): 2, (84, 876): 1, (85, 95): 1,
        (85, 986): 1, (86, 592): 1, (95, 100): 7,
        (100, 105): 1, (100, 150): 1, (115, 120): 1,
        (120, 125): 1, (123, 10000): 1, (145, 150): 1,
        (193, 10000): 1, (295, 300): 1, (400, 499): 1,
        (586, 601): 1, (1000, 1100): 3, (1300, 1337): 4,
        (1300, 1400): 4, (1400, 1500): 4, (6900, 6942): 1,
        (8539, 26748): 1, (19500, 20000): 1,
        (20100, 20600): 2, (20500, 21000): 2,
    })
    assert actual_ranges == expected_ranges, (
        f"GWD quantity-range ledger drift: {actual_ranges - expected_ranges}; "
        f"missing {expected_ranges - actual_ranges}"
    )
    # Pin the complete authored literal item/quantity/noted-state mapping, not
    # just branch thresholds. Context is part of each row, so moving an item to
    # another NPC/table also fails this gate. The 335 rows include 35 explicit
    # cert_* (noted) mappings; dynamic tuple suppliers are covered separately.
    literal_rows: list[tuple[str, ...]] = []
    for filename, source in (
        ("godwars_drops.rs2", DROPS),
        ("godwars_ambient_drops.rs2", AMBIENT_DROPS),
        ("godwars_prison_drops.rs2", PRISON_DROPS),
        ("godwars_nex_drops.rs2", NEX_DROPS),
        ("wiki_imp.rs2", WIKI_IMP),
    ):
        literal_rows.extend(literal_drop_ledger(filename, source))
    literal_canonical = "\n".join("|".join(row) for row in sorted(literal_rows))
    assert len(literal_rows) == 335, len(literal_rows)
    assert sum(row[3].startswith("cert_") for row in literal_rows) == 35
    assert hashlib.sha256(literal_canonical.encode()).hexdigest() == (
        "3e0179fe636343e5c76ad2b7557ee26e825ab3c109fb458bff8f416bd9c87a29"
    ), "GWD literal item/quantity/noted-state ledger drift"
    paired_rows = {
        "gwd_drop_kree_main": (
            "obj_add($where, 3doserangerspotion, 3",
            "obj_add($where, 3dose2defense, 3",
        ),
        "gwd_drop_zilyana_main": (
            "obj_add($where, 3dosepotionofsaradomin, 3",
            "obj_add($where, 4dose2restore, 3",
            "obj_add($where, 3dose2defense, 3",
            "obj_add($where, 3dose1magic, 3",
        ),
        "gwd_drop_kril_main": (
            "obj_add($where, 3dose2attack, 3",
            "obj_add($where, 3dose2strength, 3",
            "obj_add($where, 3dose2restore, 3",
            "obj_add($where, 3dosepotionofzamorak, 3",
        ),
    }
    for proc, rows in paired_rows.items():
        body = proc_body(DROPS, proc)
        for row in rows:
            require(body, row, f"{proc} paired fixed quantity")
    armadyl_ammo = proc_body(DROPS, "gwd_bodyguard_armadyl_main")
    assert armadyl_ammo.count("calc($feathers + 90)") == 2

    # Conditional Brimstone tertiaries are neither part of the 127-way main
    # table nor unconditional: all four generals use the capped 1/50 rate,
    # while the three Armadyl substitutes retain their level-derived rates.
    brimstone = proc_body(DROPS, "gwd_brimstone_key_allowed")
    require(brimstone, "$master ! ^slayer_master_konar",
            "Brimstone Konar prerequisite")
    require(brimstone, "$matches = false", "Brimstone matching-task prerequisite")
    require(brimstone, "$roll ! 0", "Brimstone selected-roll prerequisite")
    assert DROPS.count("~gwd_drop_brimstone_key($where, 50);") == 4
    armadyl_brimstone = proc_body(
        DROPS, "gwd_armadyl_bodyguard_brimstone_denominator"
    )
    for npc, denominator in (
        ("godwars_armadyl_bodyguard_skree", 92),
        ("godwars_armadyl_bodyguard_geerin", 91),
    ):
        require(armadyl_brimstone, f"npc_type = {npc}",
                f"{npc} Brimstone identity")
        require(armadyl_brimstone, f"return({denominator});",
                f"{npc} Brimstone denominator")
    require(armadyl_brimstone, "return(89);", "Flight Kilisa Brimstone denominator")
    require(proc_body(DROPS, "gwd_bodyguard_armadyl"),
            "~gwd_drop_brimstone_key($where, ~gwd_armadyl_bodyguard_brimstone_denominator);",
            "Armadyl bodyguard Brimstone tertiary")
    frozen_piece = proc_body(DROPS, "gwd_drop_frozen_key_piece")
    require(frozen_piece, "%gwd_frozen_door_state < 1",
            "frozen-key inactive-miniquest suppression")
    require(frozen_piece, "inv_total(inv, nex_frozen_key)",
            "assembled frozen-key inventory suppression")
    require(frozen_piece, "inv_total(bank, nex_frozen_key)",
            "assembled frozen-key bank suppression")
    require(frozen_piece, "inv_total(inv, $piece)",
            "owned frozen-key piece suppression")
    for needle in (
        "%rag_quest = ^rag_collecting",
        "testbit(%rag_submit, ^rag_bit_goblin) = ^false",
        "inv_total(inv, rag_goblin_bone) = 0",
        "inv_total(bank, rag_goblin_bone) = 0",
    ):
        require(AMBIENT_DROPS, needle, "Rag goblin-bone prerequisite")
    clue_rate = proc_body(DROPS, "gwd_boss_elite_clue_rate")
    for tier in ("elite", "master", "grandmaster"):
        require(clue_rate, f"%ca_tier_status_{tier} = 2",
                f"classic boss elite clue {tier} reward")
    require(clue_rate, "return(237);", "rewarded classic boss elite clue rate")
    require(clue_rate, "return(250);", "base classic boss elite clue rate")

    # Global prerequisites used by GWD-authored tertiaries. X Marks the Spot
    # changes each tier to its stackable scroll-box object; champion rolls need
    # 32 QP and suppress already-defeated champions; all five GWD pets write
    # their insurance bit and pass through the collection catalogue.
    clue_obj = proc_body(DROPS, "gwd_clue_drop_obj")
    for tier, clue, box in (
        (0, "trail_clue_beginner", "league_clue_box_beginner"),
        (1, "trail_clue_easy_simple001", "league_clue_box_easy"),
        (2, "trail_clue_medium_map001", "league_clue_box_medium"),
        (3, "trail_clue_hard_map001", "league_clue_box_hard"),
        (4, "trail_elite_emote_exp1", "league_clue_box_elite"),
    ):
        if tier < 4:
            require(clue_obj, f"$tier = {tier}", f"clue tier {tier} selector")
        require(clue_obj, box, f"clue tier {tier} box")
        require(DROPS + AMBIENT_DROPS + PRISON_DROPS + NEX_DROPS,
                clue, f"clue tier {tier} exact unboxed object")
    require(clue_obj, "return($unboxed);", "exact unboxed clue preservation")
    require(proc_body(DROPS, "gwd_drop_clue"),
            "%cluequest >= ^xmarks_complete", "X Marks clue-box prerequisite")
    champion = proc_body(DROPS, "gwd_champion_scroll_allowed")
    require(champion, "$quest_points < 32", "champion-scroll QP prerequisite")
    require(champion, "$defeated ! 0", "champion duplicate suppression")
    champion_award = proc_body(DROPS, "gwd_drop_champion_scroll")
    require(champion_award, "would have received a Champion's scroll",
            "champion duplicate feedback")
    for defeated in ("goblin", "hobgoblin", "imp", "jogre"):
        require(DROPS + AMBIENT_DROPS + WIKI_IMP,
                f"%champions_defeated_{defeated}",
                f"{defeated} champion completion prerequisite")
    pet = proc_body(DROPS, "gwd_record_pet")
    for varbit in (
        "armadylpet", "bandospet", "saradominpet", "zamorakpet", "nex"
    ):
        require(pet, f"%pet_insurance_{varbit} = 1;",
                f"{varbit} automatic pet insurance")
    require(pet, "~collection_earn_if_catalogued($pet, 1);",
            "GWD pet collection logging")
    require(proc_body(DROPS, "gwd_boss_tertiary_code"),
            "random(~gwd_boss_elite_clue_rate)",
            "classic boss conditional elite clue selector")

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
        "npc_statheal(hitpoints, max(divide($rolled_damage, 5), 1), 0)",
        "queue(npc_freeze_player, 0, 6)",
    ):
        require(PRISON, effect, "Zarosian spiritual mage effects")
    require(
        proc_body(PRISON, "gwd_prison_mage_attack"),
        "~gwd_prison_mage_attack_roll(random(4));",
        "Zarosian spiritual mage random-roll wrapper",
    )
    mage_roll = proc_body(PRISON, "gwd_prison_mage_attack_roll")
    require(
        mage_roll,
        "if ($accurate = true) {\n    $rolled_damage = randominc(38);",
        "Zarosian spiritual mage preserves the pre-prayer damage roll",
    )
    require(
        mage_roll,
        "queue*(gwd_prison_mage_land, $delay)(npc_uid, $spell, $damage, $impact, $accurate, $rolled_damage);",
        "Zarosian spiritual mage carries pre-prayer damage to landing",
    )
    for spell in ("nex_smoke_attack_proj", "nex_shadow_attack_proj", "nex_blood_attack_proj", "nex_ice_attack_proj"):
        require(mage_roll, spell, f"Zarosian spiritual mage {spell} path")
    for sound in (
        "nex2021_blood_reaver_attack",
        "nex2021_blood_reaver_defend",
        "nex2021_blood_reaver_death",
    ):
        require(NPC, sound, "Blood Reaver audiovisual contract")

    print("God Wars contract OK: 69 NPCs, 126 attacks, Godswords/Nex/Prison invariants")


if __name__ == "__main__":
    main()
