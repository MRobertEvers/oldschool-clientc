#!/usr/bin/env python3
"""Non-vacuous structural checks for the feature-gated Spirit wolf slice."""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=REPO / "OSRS-Content/osrs239-content")
    parser.add_argument("--off", type=Path)
    parser.add_argument("--on", type=Path)
    args = parser.parse_args()
    if args.off is None:
        args.off = args.tree / "server/scripts/build"
    if args.on is None:
        args.on = args.tree / "server/scripts/build_summoning"

    errors: list[str] = []
    checked = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal checked
        checked += 1
        if not condition:
            errors.append(message)

    lane = args.tree / "server/scripts/ported_scape2009_summoning"
    script_path = lane / "scripts/summoning_spirit_wolf.rs2"
    source = script_path.read_text(encoding="utf-8")
    headers = list(re.finditer(r"^\[([^\]]+)\](?:\([^\n]*)?\n", source, re.MULTILINE))
    expect(len(headers) >= 10, "too few callable entry points were inspected")
    for index, header in enumerate(headers):
        body_start = header.end()
        body_end = headers[index + 1].start() if index + 1 < len(headers) else len(source)
        body = source[body_start:body_end]
        expect(
            re.match(r"\s*if \(\^summoning_enabled = 0\)", body) is not None,
            f"entry point lacks a top feature gate: [{header.group(1)}]",
        )

    for token in (
        "[opheld1,summoning_spirit_wolf_pouch]",
        "npc_add(movecoord(coord, 1, 0, 0), summoning_spirit_wolf, 0);",
        "npc_setowner;",
        "npc_setmode(playerfollow);",
        "[timer,summoning_tick]",
        "npc_findowned",
        "npc_tele(",
        "npc_del;",
        "[debugproc,summoning_expire]",
        "~summoning_on_death;",
    ):
        haystack = source
        if token == "~summoning_on_death;":
            haystack = (args.tree / "server/scripts/player/death.rs2").read_text(encoding="utf-8")
        expect(token in haystack, f"missing gameplay contract: {token}")

    for rel, token in (
        ("server/scripts/player/login.rs2", "~summoning_login;"),
        ("server/scripts/player/logout.rs2", "~summoning_logout;"),
        ("server/scripts/player/death.rs2", "~summoning_on_death;"),
    ):
        expect(token in (args.tree / rel).read_text(encoding="utf-8"), f"missing lifecycle hook {token}")

    varps = (lane / "configs/summoning.varp").read_text(encoding="utf-8")
    expect(varps.count("scope=perm") == 5, "familiar state is not five persisted varps")
    expect(varps.count("transmit=no") == 5, "familiar state leaks server-only varps")

    client_obj = (args.tree / "ported/scape2009_summoning/configs/summoning.obj").read_text(
        encoding="utf-8"
    )
    client_npc = (args.tree / "ported/scape2009_summoning/configs/summoning.npc").read_text(
        encoding="utf-8"
    )
    expect("ifop1=Summon" in client_obj, "pouch does not expose the bound opheld1 verb")
    expect("op1=Call" in client_npc and "op2=Dismiss" in client_npc, "familiar ops are absent")

    for label, directory, value in (("off", args.off, 0), ("on", args.on, 1)):
        dat = directory / "script.dat"
        idx = directory / "script.idx"
        expect(dat.is_file() and dat.stat().st_size > 0, f"{label} script.dat is absent")
        expect(idx.is_file() and idx.stat().st_size > 0, f"{label} script.idx is absent")
        staged = REPO / f"build/summoning-constants-{label}/ported_scape2009_summoning/configs/summoning_feature.constant"
        expect(
            staged.read_text(encoding="utf-8").rstrip().endswith(f"^summoning_enabled = {value}"),
            f"{label} constant has the wrong value",
        )
    expect(sha256(args.off / "script.dat") != sha256(args.on / "script.dat"), "flag did not change bytecode")

    for error in errors:
        print(f"test_summoning_phase3: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase3: {checked} checks, {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
