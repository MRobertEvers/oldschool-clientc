#!/usr/bin/env python3
"""Deterministic contract test for the 2012 QBD combat and antifire port.

The historical inputs are deliberately pinned here instead of sampling random
combat.  That makes this test sensitive to accidental stat, bonus, duration,
protection-precedence, or handler changes while producing the same result on
every run.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


CONTENT = Path("OSRS-Content/osrs239-content")
SCRIPTS = CONTENT / "server/scripts"
QBD = SCRIPTS / "minigames/minigame_rs2012_qbd"
QBD_CONSTANTS = QBD / "configs/rs2012_qbd.constant"
QBD_NPCS = QBD / "configs/rs2012_qbd.npc"
QBD_COMBAT = QBD / "scripts/rs2012_qbd_combat.rs2"
QBD_ADDS = QBD / "scripts/rs2012_qbd_adds.rs2"
QBD_SESSION = QBD / "scripts/rs2012_qbd_session.rs2"
QBD_SELFTEST = QBD / "scripts/rs2012_qbd_selftest.rs2"
TD_PLAYER_HIT = SCRIPTS / "areas/area_rs2012_tormented_demons/scripts/rs2012_td_player_hit.rs2"
DRAGON_CLAWS = SCRIPTS / "areas/area_rs2012_tormented_demons/scripts/rs2012_dragon_claws.rs2"
ANTIFIRE_CONSTANTS = SCRIPTS / "player/configs/consumption/antifire.constant"
ANTIFIRE_SCRIPT = SCRIPTS / "player/scripts/consumption/antifire_potion.rs2"
LOGIN = SCRIPTS / "player/login.rs2"
DEATH = SCRIPTS / "player/death.rs2"


def integer_constants(text: str) -> dict[str, int]:
    result: dict[str, int] = {}
    for name, value in re.findall(r"^\^([a-zA-Z0-9_]+)\s*=\s*(-?\d+)\s*$", text, re.MULTILINE):
        result[name] = int(value)
    return result


def config_blocks(text: str) -> dict[str, dict[str, int]]:
    blocks: dict[str, dict[str, int]] = {}
    pattern = re.compile(r"^\[([^\]]+)\]\s*$\n(.*?)(?=^\[[^\]]+\]\s*$|\Z)", re.MULTILINE | re.DOTALL)
    for name, body in pattern.findall(text):
        values: dict[str, int] = {}
        for key, value in re.findall(r"^([a-zA-Z0-9_]+)=(-?\d+)\s*$", body, re.MULTILINE):
            values[key] = int(value)
        for key, value in re.findall(r"^param=([a-zA-Z0-9_]+),(-?\d+)\s*$", body, re.MULTILINE):
            values[f"param:{key}"] = int(value)
        blocks[name] = values
    return blocks


def npc_roll(level: int, bonus: int) -> int:
    """Mirror mock230's content-owned ``(level + 9) * (bonus + 64)``."""

    return (level + 9) * (bonus + 64)


def hit_chance_basis_points(attack: int, defence: int) -> int:
    """Exact two-uniform-dice chance from mock230, rounded to 0.01%."""

    if attack > defence:
        numerator = 2 * attack - defence
        denominator = 2 * attack + 2
    else:
        numerator = attack
        denominator = 2 * (defence + 1)
    return round(10000 * numerator / denominator)


def check(root: Path) -> list[str]:
    errors: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            errors.append(message)

    antifire_constants = integer_constants((root / ANTIFIRE_CONSTANTS).read_text(encoding="utf-8"))
    expected_antifire = {
        "antifire_decay_interval": 30,
        "antifire_duration_units": 20,
        "super_antifire_decay_interval": 20,
        "super_antifire_duration_units": 30,
    }
    require(
        antifire_constants == expected_antifire,
        f"{ANTIFIRE_CONSTANTS}: expected {expected_antifire}, got {antifire_constants}",
    )
    require(
        antifire_constants.get("antifire_decay_interval", 0)
        * antifire_constants.get("antifire_duration_units", 0)
        == 600,
        f"{ANTIFIRE_CONSTANTS}: regular antifire must last exactly 600 ticks",
    )
    require(
        antifire_constants.get("super_antifire_decay_interval", 0)
        * antifire_constants.get("super_antifire_duration_units", 0)
        == 600,
        f"{ANTIFIRE_CONSTANTS}: 2012 super antifire must last exactly 600 ticks",
    )

    antifire = (root / ANTIFIRE_SCRIPT).read_text(encoding="utf-8")
    regular = [f"{dose}dose1antidragon" for dose in range(4, 0, -1)]
    super_ = [f"{dose}dose3antidragon" for dose in range(4, 0, -1)]
    all_doses = regular + super_
    all_script_text = {
        path.relative_to(root): path.read_text(encoding="utf-8", errors="replace")
        for path in (root / SCRIPTS).rglob("*.rs2")
    }
    for dose in all_doses:
        header = re.compile(rf"^\[opheld1,{re.escape(dose)}\](?:\s|$)", re.MULTILINE)
        owners = [path for path, text in all_script_text.items() if header.search(text)]
        count = sum(len(header.findall(text)) for text in all_script_text.values())
        require(
            count == 1 and owners == [ANTIFIRE_SCRIPT],
            f"{dose}: expected one opheld1 owned by {ANTIFIRE_SCRIPT}; count={count}, owners={owners}",
        )

    for family in ("1", "3"):
        for dose in range(4, 1, -1):
            transition = rf"case {dose}dose{family}antidragon\s*:\s*return\({dose - 1}dose{family}antidragon, {dose - 1}\);"
            require(
                re.search(transition, antifire) is not None,
                f"{ANTIFIRE_SCRIPT}: missing {dose}-to-{dose - 1} dose transition for family {family}",
            )
        require(
            f"return(vial_empty, 0);" in antifire,
            f"{ANTIFIRE_SCRIPT}: final doses must leave an empty vial",
        )

    for fragment in (
        "%super_antifire_potion = 0;\ncleartimer(super_antifire_decay);",
        "%antifire_potion = 0;\ncleartimer(antifire_decay);",
        "%antifire_potion = ^antifire_duration_units;",
        "%super_antifire_potion = ^super_antifire_duration_units;",
        "[proc,antifire_login]",
        "[proc,antifire_on_death]",
    ):
        require(fragment in antifire, f"{ANTIFIRE_SCRIPT}: missing effect contract: {fragment!r}")
    require(
        "~antifire_login;" in (root / LOGIN).read_text(encoding="utf-8"),
        f"{LOGIN}: saved antifire countdowns are not re-armed",
    )
    require(
        "~antifire_on_death;" in (root / DEATH).read_text(encoding="utf-8"),
        f"{DEATH}: antifire effects are not cleared on death",
    )

    qbd_constants = integer_constants((root / QBD_CONSTANTS).read_text(encoding="utf-8"))
    expected_damage = {
        "rs2012_qbd_melee_max": 48,
        "rs2012_qbd_ranged_max": 53,
        "rs2012_qbd_soul_melee_max": 26,
        "rs2012_qbd_worm_magic_max": 21,
        "rs2012_qbd_worm_melee_max": 20,
        "rs2012_qbd_extreme_max": 97,
        "rs2012_qbd_fire_protected_min": 10,
        "rs2012_qbd_fire_protected_span": 14,
        "rs2012_qbd_fire_prayer_min": 6,
        "rs2012_qbd_fire_prayer_span": 40,
        "rs2012_qbd_fire_unprotected_min": 70,
        "rs2012_qbd_fire_unprotected_span": 21,
        "rs2012_qbd_extreme_protected_min": 15,
        "rs2012_qbd_extreme_protected_span": 20,
        "rs2012_qbd_extreme_prayer_min": 15,
        "rs2012_qbd_extreme_prayer_span": 33,
    }
    for name, expected in expected_damage.items():
        require(
            qbd_constants.get(name) == expected,
            f"{QBD_CONSTANTS}: {name} expected {expected}, got {qbd_constants.get(name)}",
        )

    # Pool sizes: mock230's own HP scale throughout, not a separate 2012-LP
    # domain. `rs2012_qbd_lp_scale` must not exist at all.
    expected_pools = {
        "rs2012_qbd_phase_lp": 1875,
        "rs2012_qbd_hit_cap_lp": 100,
        "rs2012_qbd_soul_hp": 50,
        "rs2012_qbd_worm_hp": 65,
        "rs2012_qbd_siphon_drain": 2,
        "rs2012_qbd_siphon_heal": 4,
    }
    for name, expected in expected_pools.items():
        require(
            qbd_constants.get(name) == expected,
            f"{QBD_CONSTANTS}: {name} expected {expected}, got {qbd_constants.get(name)}",
        )
    require(
        "rs2012_qbd_lp_scale" not in qbd_constants,
        f"{QBD_CONSTANTS}: rs2012_qbd_lp_scale must stay retired",
    )

    npcs = config_blocks((root / QBD_NPCS).read_text(encoding="utf-8"))
    attack_bonus_names = ("stabattack", "slashattack", "crushattack", "magicattack", "rangeattack")
    defence_bonus_names = ("stabdefence", "slashdefence", "crushdefence", "magicdefence", "rangedefence")
    qbd_defences = {
        "rs2012_qbd_sleeping": (100, 100, 100, 100, 100),
        "rs2012_qbd_default": (100, 100, 100, 100, 100),
        "rs2012_qbd_crystal": (10, 10, 10, 200, 10),
        "rs2012_qbd_hardened": (200, 200, 200, 10, 200),
    }
    for name, defences in qbd_defences.items():
        values = npcs.get(name, {})
        require(
            (values.get("attack"), values.get("magic"), values.get("ranged"), values.get("defence"))
            == (2100, 2100, 2100, 100),
            f"{QBD_NPCS}: {name} lost the labelled 2100/100 level adapters",
        )
        require(
            tuple(values.get(f"param:{bonus}") for bonus in attack_bonus_names) == (0, 0, 0, 0, 0),
            f"{QBD_NPCS}: {name} attack bonuses no longer match open727",
        )
        require(
            tuple(values.get(f"param:{bonus}") for bonus in defence_bonus_names) == defences,
            f"{QBD_NPCS}: {name} defence bonuses expected {defences}",
        )

    add_expectations = {
        "rs2012_qbd_tortured_soul": {
            "attack": 147,
            "defence": 100,
            "attack_bonuses": (112, 100, 100, 178, 4),
            "defence_bonuses": (175, 142, 285, 235, 195),
        },
        "rs2012_qbd_giant_worm": {
            "attack": 123,
            "magic": 123,
            "defence": 100,
            "attack_bonuses": (157, 100, 100, 88, 4),
            "defence_bonuses": (107, 198, 124, 178, 157),
        },
    }
    for name, expected in add_expectations.items():
        values = npcs.get(name, {})
        for stat in ("attack", "magic", "defence"):
            if stat in expected:
                require(values.get(stat) == expected[stat], f"{QBD_NPCS}: {name} {stat} expected {expected[stat]}")
        require(
            tuple(values.get(f"param:{bonus}") for bonus in attack_bonus_names) == expected["attack_bonuses"],
            f"{QBD_NPCS}: {name} attack bonuses no longer match open727",
        )
        require(
            tuple(values.get(f"param:{bonus}") for bonus in defence_bonus_names) == expected["defence_bonuses"],
            f"{QBD_NPCS}: {name} defence bonuses no longer match open727",
        )

    # Pin the actual integer rolls produced by the compatibility policy.  The
    # first three are incoming QBD/add attacks; the last three demonstrate the
    # exact weak/default/resistant QBD ordering for player attacks.
    calibrated_rolls = {
        "qbd_zero_bonus": npc_roll(2100, 0),
        "soul_stab": npc_roll(147, 112),
        "worm_magic": npc_roll(123, 88),
        "qbd_weak_defence": npc_roll(100, 10),
        "qbd_default_defence": npc_roll(100, 100),
        "qbd_resistant_defence": npc_roll(100, 200),
    }
    expected_rolls = {
        "qbd_zero_bonus": 134976,
        "soul_stab": 27456,
        "worm_magic": 20064,
        "qbd_weak_defence": 8066,
        "qbd_default_defence": 17876,
        "qbd_resistant_defence": 28776,
    }
    require(calibrated_rolls == expected_rolls, f"NPC hit-roll calibration changed: {calibrated_rolls}")
    require(
        calibrated_rolls["qbd_weak_defence"]
        < calibrated_rolls["qbd_default_defence"]
        < calibrated_rolls["qbd_resistant_defence"],
        "QBD weak/default/resistant defence ordering regressed",
    )
    representative_defence = 21000
    require(
        {
            "qbd": hit_chance_basis_points(calibrated_rolls["qbd_zero_bonus"], representative_defence),
            "soul": hit_chance_basis_points(calibrated_rolls["soul_stab"], representative_defence),
            "worm": hit_chance_basis_points(calibrated_rolls["worm_magic"], representative_defence),
        }
        == {"qbd": 9222, "soul": 6175, "worm": 4777},
        "incoming QBD/add hit-chance calibration changed",
    )
    representative_attack = 21000
    require(
        tuple(
            hit_chance_basis_points(representative_attack, calibrated_rolls[name])
            for name in ("qbd_weak_defence", "qbd_default_defence", "qbd_resistant_defence")
        )
        == (8079, 5744, 3649),
        "QBD weak/default/resistant hit-chance calibration changed",
    )

    combat = (root / QBD_COMBAT).read_text(encoding="utf-8")
    adds = (root / QBD_ADDS).read_text(encoding="utf-8")
    session = (root / QBD_SESSION).read_text(encoding="utf-8")
    shared_hit = (root / TD_PLAYER_HIT).read_text(encoding="utf-8")
    claws = (root / DRAGON_CLAWS).read_text(encoding="utf-8")
    selftest = (root / QBD_SELFTEST).read_text(encoding="utf-8")

    # QBD, souls and worms are all authored directly on mock230's own HP
    # scale, the same as every other NPC in the tree — there is no separate
    # 2012-LP domain or ^rs2012_qbd_lp_scale conversion factor for any of
    # them. `rs2012_qbd_is_add_type` survives only because
    # rs2012_dragon_claws.rs2 still calls it directly; the shared funnel and
    # QBD's own combat script must NOT reference it or the retired scale.
    add_predicate = re.search(
        r"^\[proc,rs2012_qbd_is_add_type\]\(\)\(boolean\)\s*$\n(.*?)(?=^\[|\Z)",
        shared_hit,
        re.MULTILINE | re.DOTALL,
    )
    require(add_predicate is not None, f"{TD_PLAYER_HIT}: missing QBD-add predicate")
    for add_name in ("rs2012_qbd_tortured_soul", "rs2012_qbd_giant_worm"):
        require(
            add_predicate is not None and f"npc_type = {add_name}" in add_predicate.group(1),
            f"{TD_PLAYER_HIT}: {add_name} bypasses the QBD-add predicate",
        )
    for forbidden in ("$qbd_add", "rs2012_qbd_prepare_add_player_hit", "rs2012_qbd_lp_scale"):
        require(
            forbidden not in shared_hit,
            f"{TD_PLAYER_HIT}: QBD adds must not re-grow a separate LP-domain funnel: {forbidden!r}",
        )
    require(
        "rs2012_qbd_prepare_add_player_hit" not in combat and "rs2012_qbd_lp_scale" not in combat,
        f"{QBD_COMBAT}: mortal QBD adds must not re-grow the old LP-scale adapter",
    )
    for name in ("rs2012_qbd_tortured_soul", "rs2012_qbd_giant_worm"):
        require(
            re.search(rf"^\[ai_queue2,{name}\]\s*$", combat, re.MULTILINE) is None,
            f"{QBD_COMBAT}: {name} must fall through to the ordinary [ai_queue2,_] default, not override it",
        )

    claws_queue = re.search(
        r"^\[proc,rs2012_claws_queue_hit\].*?$\n(.*?)(?=^\[|\Z)",
        claws,
        re.MULTILINE | re.DOTALL,
    )
    require(claws_queue is not None, f"{DRAGON_CLAWS}: missing per-splat queue helper")
    if claws_queue is not None:
        body = claws_queue.group(1)
        require(
            body.count("~player_hit_npc_prepare(") == 1,
            f"{DRAGON_CLAWS}: each claw splat must enter the shared hook exactly once",
        )
        require(
            "rs2012_qbd_lp_scale" not in body and "rs2012_qbd_is_add_type" not in body,
            f"{DRAGON_CLAWS}: remaining-HP clamp must not re-grow a QBD-add LP domain",
        )

    host_probe = re.search(
        r"^\[proc,rs2012_qbd_add_hit_host_probe\]\s*$\n(.*?)(?=^\[|\Z)",
        selftest,
        re.MULTILINE | re.DOTALL,
    )
    require(host_probe is not None, f"{QBD_SELFTEST}: missing live QBD-add hit probe")
    if host_probe is not None:
        body = host_probe.group(1)
        for fragment in (
            "if (~rs2012_qbd_is_add_type = false)",
            "if (npc_type = rs2012_qbd_giant_worm) $rolled_damage = 65;",
            "$prepared, $xp_damage = ~player_hit_npc_prepare($rolled_damage, true);",
            "%mock_quest_progress = add(multiply($prepared, 1000), $xp_damage);",
            "npc_queue(2, $prepared, 0);",
        ):
            require(fragment in body, f"{QBD_SELFTEST}: incomplete live add probe: {fragment!r}")
    require(
        "$accurate = ~npc_player_hit_roll($accuracy_style);" in combat,
        f"{QBD_COMBAT}: QBD/add melee or Magic accuracy bypasses npc_player_hit_roll",
    )
    require(
        "randominc(~npc_ranged_attack_roll) > randominc(~player_defence_roll(^ranged_style))" in combat,
        f"{QBD_COMBAT}: QBD Ranged does not use the dedicated NPC ranged roll",
    )
    require(
        combat.count("~rs2012_qbd_roll_player_damage(") == 2,
        f"{QBD_COMBAT}: expected exactly the melee and ranged accuracy calls",
    )
    require(
        adds.count("~rs2012_qbd_roll_player_damage(") == 4,
        f"{QBD_ADDS}: expected two soul and both worm accuracy calls",
    )
    for fragment in (
        "$damage = ~gear_reduce_damage($damage, $style);",
        "[queue,rs2012_qbd_damage_player](npc_uid $source, int $damage, int $style)",
        "if (%rs2012_qbd_active = 0 | %rs2012_qbd_reward_ready = 1) return;",
        "%rs2012_qbd_time_damage = add(%rs2012_qbd_time_damage, $damage);",
        "queue*(rs2012_qbd_damage_player, 0)($source, $damage, $style);",
    ):
        require(fragment in combat, f"{QBD_COMBAT}: missing typed player-damage contract: {fragment!r}")
    require(
        "queue*(rs2012_qbd_damage_player, calc($duration / 30))(npc_uid, $damage, ^magic_style);" in adds,
        f"{QBD_ADDS}: worm projectile bypasses typed QBD damage",
    )
    require(
        "queue*(combat_damage_player" not in combat + adds,
        "QBD/add damage still passes through the magic-only combat_damage_player queue",
    )
    require(
        "if (%rs2012_qbd_active = 0 | %rs2012_qbd_reward_ready = 1 |\n"
        "    %rs2012_qbd_phase ! 4 | %rs2012_qbd_intermission = 1) return;" in combat,
        f"{QBD_COMBAT}: an extreme-fire pulse can escape into the reward transition",
    )
    for fragment in (
        # Rounds land at T0+4/+6/+8 (queue stores delay+1), all scheduled
        # upfront from the ai_timer context so none loses a tick to the
        # queue-drain cursor.
        "queue*(rs2012_qbd_extreme_pulse, 3)(0, npc_uid);",
        "queue*(rs2012_qbd_extreme_pulse, 5)(1, npc_uid);",
        "queue*(rs2012_qbd_extreme_pulse, 7)(2, npc_uid);",
        "[queue,rs2012_qbd_extreme_pulse](int $pulse, npc_uid $source)",
        "~rs2012_qbd_hit_player($source, $damage, ^magic_style, false);",
    ):
        require(fragment in combat, f"{QBD_COMBAT}: extreme fire lost its QBD source UID: {fragment!r}")
    for proc in ("rs2012_qbd_complete", "rs2012_qbd_clear_state"):
        match = re.search(rf"^\[proc,{proc}\]\s*$\n(.*?)(?=^\[|\Z)", session, re.MULTILINE | re.DOTALL)
        require(match is not None, f"{QBD_SESSION}: missing [{proc}] terminal path")
        body = match.group(1) if match is not None else ""
        for queue_name in ("rs2012_qbd_damage_player", "rs2012_qbd_extreme_pulse"):
            require(
                f"clearqueue({queue_name});" in body,
                f"{QBD_SESSION}: [{proc}] does not clear {queue_name}",
            )
    require(
        "multiply($damage, 75)" not in combat and "multiply($damage, 125)" not in combat,
        f"{QBD_COMBAT}: unsupported +/-25% armour damage transform double-counts form defence",
    )
    for constant in (
        "rs2012_qbd_melee_max",
        "rs2012_qbd_ranged_max",
        "rs2012_qbd_soul_melee_max",
        "rs2012_qbd_worm_magic_max",
        "rs2012_qbd_worm_melee_max",
    ):
        require(
            re.search(rf"randominc\(\^{constant}\)", combat + adds) is None,
            f"QBD/add maximum {constant} bypasses the shared accuracy helper",
        )

    # Souls and worms are created with npc_add inside a private encounter.  A
    # normal dead NPC is assigned its definition's respawn clock by mock230,
    # so both death triggers must explicitly retire these one-life additions.
    for add_name in ("rs2012_qbd_tortured_soul", "rs2012_qbd_giant_worm"):
        match = re.search(
            rf"^\[ai_queue3,{add_name}\]\s*$\n(.*?)(?=^\[|\Z)",
            adds,
            re.MULTILINE | re.DOTALL,
        )
        require(match is not None, f"{QBD_ADDS}: missing death trigger for {add_name}")
        require(
            match is not None and "npc_del;" in match.group(1),
            f"{QBD_ADDS}: {add_name} can respawn after an encounter-owned death",
        )
        require(
            match is not None
            and match.group(1).index("npc_delay(1);")
            < match.group(1).index("npc_del;"),
            f"{QBD_ADDS}: {add_name} deletes before post-drop kill credit",
        )
    require(
        npcs.get("rs2012_qbd_tortured_soul", {}).get("hitpoints") == 50,
        f"{QBD_NPCS}: tortured soul must retain its 50-LP interrupt pool",
    )
    require(
        npcs.get("rs2012_qbd_giant_worm", {}).get("hitpoints") == 65,
        f"{QBD_NPCS}: giant worm must retain its 65-LP pool (the 21-Jun-2012 650-LP figure at 1/10 scale)",
    )
    worm_death = re.search(
        r"^\[ai_queue3,rs2012_qbd_giant_worm\]\s*$\n(.*?)(?=^\[|\Z)",
        adds,
        re.MULTILINE | re.DOTALL,
    )
    require(
        worm_death is not None and "obj_add(npc_coord, bones, 1" in worm_death.group(1),
        f"{QBD_ADDS}: giant worm lost its period always-drop bones",
    )

    protection_header = "[proc,rs2012_qbd_dragonfire_protection]()(int)"
    protection = combat.split(protection_header, 1)[-1].split("[proc,rs2012_qbd_melee]", 1)[0]
    potion_check = "if (%antifire_potion > 0 | %super_antifire_potion > 0)"
    require(potion_check in protection, f"{QBD_COMBAT}: either antifire type must protect while nonzero")
    shield_pos = protection.find("inv_total(worn, antidragonbreathshield)")
    potion_pos = protection.find(potion_check)
    prayer_pos = protection.find("~check_protect_prayer(^magic_style)")
    require(
        -1 < shield_pos < potion_pos < prayer_pos,
        f"{QBD_COMBAT}: protection precedence must remain shield, either potion, then prayer",
    )
    require(
        combat.count("$protection = ^rs2012_qbd_fire_protection_potion") == 2,
        f"{QBD_COMBAT}: ordinary and extreme breath must share the sourced potion-protected branch",
    )

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="repository root (default: parent of tools/)",
    )
    args = parser.parse_args()
    root = args.repo.resolve()
    errors = check(root)
    if errors:
        for error in errors:
            print(f"rs2012-qbd-combat: ERROR: {error}", file=sys.stderr)
        return 1
    print(
        "rs2012-qbd-combat: 600-tick potions, unique handlers, sourced NPC "
        "bonuses, calibrated rolls, accuracy routing, one-life adds, maxima, "
        "LP-domain dispatch, and QBD protection OK"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
