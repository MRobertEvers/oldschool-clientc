#!/usr/bin/env python3
"""Fail fast if the ::crystal_set client/server contract regresses.

The incident and its packet-level diagnosis live in
docs/CRYSTAL_SET_COMMAND.md.  This checker deliberately spans both source
trees because the original failure did too: revision-239 CS2 consumed the
command as the local ``cry`` emote before CLIENT_CHEAT, while a second global
``[debugproc,crystal_set]`` made the eventual server result ambiguous.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


CLIENT = Path("OSRS-Content/osrs239-content/scripts/script_7304.cs2")
CHAT_ENTER = Path("OSRS-Content/osrs239-content/scripts/script_73.cs2")
SCRIPTS = Path("OSRS-Content/osrs239-content/server/scripts")
CRYSTAL_PROC = SCRIPTS / "skill_combat/scripts/player/crystal_set.rs2"
PACKET_TABLE = Path("src/net/rev/osrs239/packetout.h")
WORLD = Path("src/net/mock/mock230_world.c")
INCIDENT_DOC = Path("docs/CRYSTAL_SET_COMMAND.md")


def client_errors(text: str) -> list[str]:
    errors: list[str] = []
    marker = "$string0 = lowercase($string0);"
    if marker not in text:
        return ["script 7304 no longer has its normalized-command boundary"]
    command_body = text.split(marker, 1)[1]
    command_body = command_body.split("if (cc_find", 1)[0]
    if re.search(r"string_indexof_string\s*\(\s*\$string0", command_body):
        errors.append(
            "local emote aliases use prefix matching; this makes crystal_set match cry"
        )
    aliases = set(
        re.findall(r'compare\s*\(\s*\$string0\s*,\s*"([^"]+)"\s*\)\s*=\s*0', command_body)
    )
    if "cry" not in aliases:
        errors.append('the exact local alias compare($string0, "cry") = 0 is missing')
    return errors


def crystal_definition_errors(definitions: list[Path], text: str) -> list[str]:
    errors: list[str] = []
    if definitions != [CRYSTAL_PROC]:
        rendered = ", ".join(str(path) for path in definitions) or "none"
        errors.append(
            "expected exactly one [debugproc,crystal_set] at "
            f"{CRYSTAL_PROC}; found: {rendered}"
        )
    required = (
        "[proc,crystal_set_debug]",
        "stat_advance(ranged, ^cheat_xp_level99);",
        "stat_advance(agility, ^cheat_xp_level99);",
        "inv_setslot(worn, ^wearpos_hat, crystal_helmet, 1);",
        "inv_setslot(worn, ^wearpos_torso, crystal_chestplate, 1);",
        "inv_setslot(worn, ^wearpos_legs, crystal_platelegs, 1);",
        "inv_setslot(worn, ^wearpos_rhand, bow_of_faerdhinen_infinite, 1);",
        "~equipment_refresh;",
    )
    for fragment in required:
        if fragment not in text:
            errors.append(f"canonical crystal_set lost required behavior: {fragment}")
    return errors


def run_self_test() -> None:
    good_client = (
        '$string0 = lowercase($string0);\n'
        'if (compare($string0, "cry") = 0) {}\n'
        'if (cc_find(interface_216:2, 16) = ^true) {}\n'
    )
    assert not client_errors(good_client)
    bad_client = good_client.replace(
        'compare($string0, "cry")', 'string_indexof_string($string0, "cry", 0)'
    )
    assert client_errors(bad_client), "negative control: prefix matching must fail"

    good_proc = "\n".join(
        (
            "[proc,crystal_set_debug]",
            "stat_advance(ranged, ^cheat_xp_level99);",
            "stat_advance(agility, ^cheat_xp_level99);",
            "inv_setslot(worn, ^wearpos_hat, crystal_helmet, 1);",
            "inv_setslot(worn, ^wearpos_torso, crystal_chestplate, 1);",
            "inv_setslot(worn, ^wearpos_legs, crystal_platelegs, 1);",
            "inv_setslot(worn, ^wearpos_rhand, bow_of_faerdhinen_infinite, 1);",
            "~equipment_refresh;",
        )
    )
    assert not crystal_definition_errors([CRYSTAL_PROC], good_proc)
    assert crystal_definition_errors(
        [CRYSTAL_PROC, Path("somewhere/obsolete.rs2")], good_proc
    ), "negative control: duplicate debugproc must fail"


def check(root: Path) -> list[str]:
    errors: list[str] = []

    client = (root / CLIENT).read_text(encoding="utf-8")
    errors.extend(f"{CLIENT}: {item}" for item in client_errors(client))

    chat_enter = (root / CHAT_ENTER).read_text(encoding="utf-8")
    local = chat_enter.find("~script7304(")
    server = chat_enter.find("docheat(")
    if local < 0 or server < 0 or local >= server:
        errors.append(
            f"{CHAT_ENTER}: expected local script7304 dispatch before the docheat server path"
        )

    definitions: list[Path] = []
    header = re.compile(r"^\s*\[debugproc,crystal_set\]\s*$", re.MULTILINE)
    for path in sorted((root / SCRIPTS).rglob("*.rs2")):
        if header.search(path.read_text(encoding="utf-8")):
            definitions.append(path.relative_to(root))
    proc_text = (root / CRYSTAL_PROC).read_text(encoding="utf-8")
    errors.extend(crystal_definition_errors(definitions, proc_text))

    packet = (root / PACKET_TABLE).read_text(encoding="utf-8")
    if not re.search(r"PKTOUT_NAME_CLIENT_CHEAT\s*,\s*34\s*,", packet):
        errors.append(f"{PACKET_TABLE}: revision-239 CLIENT_CHEAT is no longer opcode 34")

    world = (root / WORLD).read_text(encoding="utf-8")
    for fragment in (
        "mock230: cheat '%s' -> debugproc %s",
        "Command ::%s failed — see the server log.",
        'mock230_scripts_run_debugproc(&srv, "crystal_set") ==',
        '"::crystal_set equips the crystal helmet"',
        '"::crystal_set equips a qualifying crystal bow"',
        '"::crystal_set raises its own equip requirements"',
    ):
        if fragment not in world:
            errors.append(f"{WORLD}: missing diagnostic/self-test guard: {fragment}")

    if not (root / INCIDENT_DOC).is_file():
        errors.append(f"{INCIDENT_DOC}: canonical incident guide is missing")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="repository root (default: parent of tools/)",
    )
    parser.add_argument("--self-test", action="store_true", help="run checker negative controls")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
    errors = check(args.repo.resolve())
    if errors:
        for error in errors:
            print(f"crystal-set contract: ERROR: {error}", file=sys.stderr)
        print(
            "crystal-set contract: see docs/CRYSTAL_SET_COMMAND.md before changing this path",
            file=sys.stderr,
        )
        return 1
    print("crystal-set contract: exact client fallthrough, unique debugproc, diagnostics, and semantics OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
