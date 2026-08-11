#!/usr/bin/env python3
"""Structural regression for the bounded feature-gated familiar runtime."""

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
    # Helpers are only reachable through gated runtime entries, so test actual
    # trigger headers rather than requiring an artificial gate on pure typed
    # selector procedures.
    gated_prefixes = ("opheld", "opnpc", "oploc", "if_", "timer", "debugproc")
    for index, header in enumerate(headers):
        name = header.group(1)
        if not name.startswith(gated_prefixes):
            continue
        body_start = header.end()
        body_end = headers[index + 1].start() if index + 1 < len(headers) else len(source)
        body = source[body_start:body_end]
        # Phase 7 extends the build-wide gate with a permanent account unlock.
        # The two transition-only debug hooks must retain the build gate so a
        # feature-off server cannot mint an unlocked save.
        gate = r"\s*if \(~summoning_account_enabled = false\)"
        if name in (
            "debugproc,summoning_unlock",
            "debugproc,summoning_lock",
            "debugproc,summoning_wolf_whistle_complete",
            "oploc3,summoning_obelisk",
        ):
            gate = r"\s*if \(\^summoning_enabled = 0\)"
        expect(re.match(gate, body) is not None, f"entry point lacks a top feature gate: [{name}]")

    for token in (
        "[opheld1,summoning_spirit_wolf_pouch]",
        "[opheld4,summoning_cohort_dreadfowl_dreadfowl_pouch]",
        "~summoning_familiar_summon(^summoning_familiar_spirit_wolf, $consume);",
        "~summoning_familiar_summon(^summoning_familiar_dreadfowl, $consume);",
        "def_npc $npc = ~summoning_familiar_npc($type);",
        "npc_add(movecoord(coord, 1, 0, 0), $npc, 0);",
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
    persisted = set(
        re.findall(
            r"^\[(summoning_familiar_[^\]]+)\]\n(?:[^\n]*\n)*?scope=perm$",
            varps,
            re.MULTILINE,
        )
    )
    familiar_persisted = {
            "summoning_familiar_active",
            "summoning_familiar_type",
            "summoning_familiar_ticks",
            "summoning_familiar_special",
            "summoning_familiar_special_clock",
            "summoning_familiar_point_accumulator",
        }
    expect(
        persisted == familiar_persisted,
        "familiar state is not the six-field persisted type/timer contract",
    )
    expect(
        re.search(r"^\[summoning_unlocked\]\n(?:[^\n]*\n)*?scope=perm$", varps, re.MULTILINE)
        is not None,
        "per-account Summoning unlock is not permanent",
    )
    for token in (
        "[proc,summoning_wolf_whistle_complete]",
        "stat_advance(summoning, ^summoning_wolf_whistle_xp);",
        "inv_add(inv, summoning_wolf_whistle_gold_charm, ^summoning_wolf_whistle_gold_charms);",
        "if (%summoning_unlocked = 1) return;",
        "[oploc3,summoning_obelisk]",
        "~summoning_wolf_whistle_complete;",
    ):
        expect(token in source, f"Wolf Whistle reward contract missing: {token}")
    expect(
        all(
            re.search(rf"^\[{re.escape(name)}\]\n(?:[^\n]*\n)*?transmit=no$", varps, re.MULTILINE)
            for name in persisted
        ),
        "persisted familiar state leaks server-only varps",
    )

    client_obj = (args.tree / "ported/scape2009_summoning/configs/summoning.obj").read_text(
        encoding="utf-8"
    )
    wolf_ledger = (args.tree / "port/summoning_wolf_whistle_530.map").read_text(
        encoding="utf-8"
    )
    wolf_obj_alloc = (args.tree / "ported/scape2009_summoning/pack/obj.alloc").read_text(
        encoding="utf-8"
    )
    client_npc = (args.tree / "ported/scape2009_summoning/configs/summoning.npc").read_text(
        encoding="utf-8"
    )
    dread_obj = (
        args.tree / "ported/scape2009_summoning/configs/summoning_cohort_dreadfowl.obj"
    ).read_text(encoding="utf-8")
    dread_npc = (
        args.tree / "ported/scape2009_summoning/configs/summoning_cohort_dreadfowl.npc"
    ).read_text(encoding="utf-8")
    familiar_cs2 = (
        args.tree / "ported/scape2009_summoning/scripts/summoning_familiar_init.cs2"
    ).read_text(encoding="utf-8")
    constants = (lane / "configs/summoning.constant").read_text(encoding="utf-8")
    expect("ifop4=Summon" in client_obj, "Spirit wolf pouch does not expose its bound operation")
    obelisk = (args.tree / "ported/scape2009_summoning/configs/summoning.loc").read_text(
        encoding="utf-8"
    )
    expect(
        "op3=Begin Wolf Whistle" in obelisk,
        "Wolf Whistle has no ordinary client-visible completion interaction",
    )
    expect(
        "obj\t12527\tquest_charm_gold\t40256\tsummoning_wolf_whistle_gold_charm\tminted\tunreviewed"
        in wolf_ledger
        and "40256=summoning_wolf_whistle_gold_charm" in wolf_obj_alloc
        and "[summoning_wolf_whistle_gold_charm]" in client_obj,
        "Wolf Whistle quest-copy charm closure is not isolated and allocated",
    )
    expect("op1=Interact" in client_npc, "Spirit wolf familiar interaction is absent")
    expect("ifop4=Summon" in dread_obj, "Dreadfowl pouch does not expose opheld4")
    expect("op5=Special" not in dread_npc, "Dreadfowl retained an unadmitted Special surface")
    expect(
        "[opnpc1,summoning_cohort_dreadfowl_dreadfowl]" in source
        and "[opnpc2,summoning_cohort_dreadfowl_dreadfowl]" in source,
        "Dreadfowl call/dismiss handlers are absent",
    )
    expect(
        "~summoning_familiar_body_model(%summoning_familiar_type)" in source
        and "~summoning_familiar_ready_seq(%summoning_familiar_type)" in source
        and "if_setanim(summoning_familiar:model, $ready_seq);" in source
        and "if_settext(summoning_familiar:title, $name);" in source
        and "npc_20000" not in familiar_cs2,
        "sidebar body model, animation, or name is not selected by persisted familiar type",
    )
    for token in (
        "^summoning_dreadfowl_level = 4",
        "^summoning_dreadfowl_cost = 1",
        "^summoning_dreadfowl_lifetime = 400",
        "^summoning_dreadfowl_drain_interval = 100",
        "%summoning_familiar_point_accumulator >= $drain_interval",
        "%summoning_familiar_ticks > 0",
    ):
        expect(token in constants or token in source, f"missing Dreadfowl timer contract: {token}")

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
