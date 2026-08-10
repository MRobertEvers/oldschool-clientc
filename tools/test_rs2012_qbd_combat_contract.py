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
            == (2100, 2100, 2100, 1),
            f"{QBD_NPCS}: {name} lost the labelled 2100/1 level adapters",
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
            "defence": 1,
            "attack_bonuses": (112, 100, 100, 178, 4),
            "defence_bonuses": (175, 142, 285, 235, 195),
        },
        "rs2012_qbd_giant_worm": {
            "magic": 123,
            "defence": 1,
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
        "qbd_weak_defence": npc_roll(1, 10),
        "qbd_default_defence": npc_roll(1, 100),
        "qbd_resistant_defence": npc_roll(1, 200),
    }
    expected_rolls = {
        "qbd_zero_bonus": 134976,
        "soul_stab": 27456,
        "worm_magic": 20064,
        "qbd_weak_defence": 740,
        "qbd_default_defence": 1640,
        "qbd_resistant_defence": 2640,
    }
    require(calibrated_rolls == expected_rolls, f"NPC hit-roll calibration changed: {calibrated_rolls}")
    require(
        calibrated_rolls["qbd_weak_defence"]
        < calibrated_rolls["qbd_default_defence"]
        < calibrated_rolls["qbd_resistant_defence"],
        "QBD weak/default/resistant defence ordering regressed",
    )

    combat = (root / QBD_COMBAT).read_text(encoding="utf-8")
    adds = (root / QBD_ADDS).read_text(encoding="utf-8")
    require(
        "if (~npc_player_hit_roll($accuracy_style) = false) return(0);" in combat,
        f"{QBD_COMBAT}: QBD/add accuracy no longer routes through npc_player_hit_roll",
    )
    require(
        combat.count("~rs2012_qbd_roll_player_damage(") == 2,
        f"{QBD_COMBAT}: expected exactly the melee and ranged accuracy calls",
    )
    require(
        adds.count("~rs2012_qbd_roll_player_damage(") == 3,
        f"{QBD_ADDS}: expected two soul and one worm accuracy calls",
    )
    for constant in (
        "rs2012_qbd_melee_max",
        "rs2012_qbd_ranged_max",
        "rs2012_qbd_soul_melee_max",
        "rs2012_qbd_worm_magic_max",
    ):
        require(
            re.search(rf"randominc\(\^{constant}\)", combat + adds) is None,
            f"QBD/add maximum {constant} bypasses the shared accuracy helper",
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
        "bonuses, calibrated rolls, accuracy routing, maxima, and QBD protection OK"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
