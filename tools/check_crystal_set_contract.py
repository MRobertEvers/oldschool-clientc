#!/usr/bin/env python3
"""Fail fast if the ::~crystal_set client/server contract regresses.

The incident and its packet-level diagnosis live in
docs/CRYSTAL_SET_COMMAND.md.  This checker deliberately spans both source
trees because the original failure did too: revision-239 CS2 consumed the
command as the local ``cry`` emote before CLIENT_CHEAT, while a second global
``[debugproc,crystal_set]`` made the eventual server result ambiguous.

The client's CS2 is the cache's own, unedited: script 7304 still prefix-matches
its emote aliases, so ``::crystal_set`` plays Cry. ``::~crystal_set`` is the
command, and what this guards is that the escape keeps reaching the server.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path


# Two roots, not one. The C and the docs are always this repository's; the CS2
# and the server scripts belong to whichever OSRS-Content tree is being built,
# which is not necessarily the submodule -- TORIRSSERVER_CONTENT_DIR selects it, and
# a checker that ignored it reported the submodule's failures against a bake of
# a different tree entirely.
# The content tree renamed every `script_<id>.cs2`; this is script 73, and it
# still carries its id in a leading `// <id>` comment.
CHAT_ENTER = Path("scripts/chatdefault_onkey.cs2")
SCRIPTS = Path("server/scripts")
CRYSTAL_PROC = SCRIPTS / "skill_combat/scripts/player/crystal_set.rs2"
PACKET_TABLE = Path("src/net/rev/osrs239/packetout.h")
# The cheat dispatch lives in the world, the ::~crystal_set assertions in the
# self-test that was split out of it. Both halves are read as one text, world
# first, so the ordering check below still means what it did.
WORLD = Path("src/torirsserver/torirs_server_world.c")
WORLD_SELFTEST = Path("src/torirsserver/torirs_server_world_selftest.c")
INCIDENT_DOC = Path("docs/CRYSTAL_SET_COMMAND.md")

DEFAULT_CONTENT = Path("OSRS-Content/osrs239-content")


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


def check(root: Path, content: Path) -> list[str]:
    errors: list[str] = []

    # Content paths are reported as they were actually opened. When the tree is
    # not the submodule, that is the only thing in the message that says so.
    chat_enter = (content / CHAT_ENTER).read_text(encoding="utf-8")
    local = chat_enter.find("~torirs_emote_command(")
    server = chat_enter.find("docheat(")
    if local < 0 or server < 0 or local >= server:
        errors.append(
            f"{content / CHAT_ENTER}: expected local torirs_emote_command dispatch "
            "before the docheat server path"
        )

    definitions: list[Path] = []
    header = re.compile(r"^\s*\[debugproc,crystal_set\]\s*$", re.MULTILINE)
    for path in sorted((content / SCRIPTS).rglob("*.rs2")):
        if header.search(path.read_text(encoding="utf-8")):
            definitions.append(path.relative_to(content))
    proc_text = (content / CRYSTAL_PROC).read_text(encoding="utf-8")
    errors.extend(crystal_definition_errors(definitions, proc_text))

    packet = (root / PACKET_TABLE).read_text(encoding="utf-8")
    if not re.search(r"PKTOUT_NAME_CLIENT_CHEAT\s*,\s*34\s*,", packet):
        errors.append(f"{PACKET_TABLE}: revision-239 CLIENT_CHEAT is no longer opcode 34")

    world = ((root / WORLD).read_text(encoding="utf-8")
             + (root / WORLD_SELFTEST).read_text(encoding="utf-8"))
    for fragment in (
        "if( text[0] == '~' )",
        'static const uint8_t command[] = "~crystal_set\\n";',
        "torirsserver: cheat '%s' -> debugproc %s",
        "Command ::~%s failed — see the server log.",
        '"::~crystal_set equips the crystal helmet"',
        '"::~crystal_set equips a qualifying crystal bow"',
        '"::~crystal_set raises its own equip requirements"',
    ):
        if fragment not in world:
            errors.append(
                f"{WORLD}/{WORLD_SELFTEST.name}: "
                f"missing diagnostic/self-test guard: {fragment}")

    # Every cheat entry point strips the `~` and then calls cheat_dispatch, which
    # tries content debugprocs before the engine's built-in ladder.
    source = (root / WORLD).read_text(encoding="utf-8")
    for entry in ("\nhandle_cheat(", "\nToriRSServer_RunCheatForTest("):
        start = source.find(entry)
        end = source.find("\n}\n", start)
        body = source[start:end]
        normalize = body.find("if( text[0] == '~' )")
        dispatch = body.find("cheat_dispatch(")
        if start < 0 or normalize < 0 or dispatch < 0 or normalize > dispatch:
            errors.append(
                f"{WORLD}: {entry.strip()} must strip ::~ before calling cheat_dispatch"
            )
    start = source.find("\ncheat_dispatch(")
    body = source[start:source.find("\n}\n", start)]
    debugproc = body.find("ToriRSServer_ScriptsRunDebugproc(srv, text)")
    ladder = body.find("ToriRSServer_RunCheatLadder(")
    if start < 0 or debugproc < 0 or ladder < 0 or debugproc > ladder:
        errors.append(f"{WORLD}: cheat_dispatch must try debugprocs before the built-in ladder")

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
    parser.add_argument(
        "--content",
        type=Path,
        default=None,
        help="OSRS-Content tree to check (default: $TORIRSSERVER_CONTENT_DIR, "
        "else <repo>/OSRS-Content/osrs239-content)",
    )
    parser.add_argument("--self-test", action="store_true", help="run checker negative controls")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()

    repo = args.repo.resolve()
    content = args.content or os.environ.get("TORIRSSERVER_CONTENT_DIR") or (repo / DEFAULT_CONTENT)
    content = Path(content).resolve()
    if not content.is_dir():
        print(f"crystal-set contract: ERROR: no content tree at {content}", file=sys.stderr)
        return 1

    errors = check(repo, content)
    if errors:
        for error in errors:
            print(f"crystal-set contract: ERROR: {error}", file=sys.stderr)
        print(
            "crystal-set contract: see docs/CRYSTAL_SET_COMMAND.md before changing this path",
            file=sys.stderr,
        )
        return 1
    print(
        "crystal-set contract: pristine ::~ escape, "
        "unique debugproc, diagnostics, and semantics OK"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
