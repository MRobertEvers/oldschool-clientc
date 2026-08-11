#!/usr/bin/env python3
"""Real-client acceptance for the admitted Phase-5b Dreadfowl cohort.

This is deliberately separate from :mod:`test_summoning_phase5b`: that
structural prerequisite preserves the review-only roster fingerprint and
proves feature-cache staging. This proof starts a fresh player, uses the
ordinary Dreadfowl-pouch inventory action, and then uses copies of *that*
saved player to exercise persistence without a debug summon shortcut.

The rev239 dynamic-inventory route is intentionally a little non-obvious:
the visible ``ifop4=Summon`` row has native action 2231/op 5, serializes as
``IF_BUTTONX op=6``, and the server's inbound fan-out canonically dispatches
it as ``OPHELD4``. All three observations are acceptance requirements.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
CONTENT = REPO / "OSRS-Content/osrs239-content"
LANE = CONTENT / "ported/scape2009_summoning"
SCRIPT_LANE = CONTENT / "server/scripts/ported_scape2009_summoning"

DREAD_NPC = "summoning_cohort_dreadfowl_dreadfowl"
DREAD_POUCH = "summoning_cohort_dreadfowl_dreadfowl_pouch"
DREAD_NPC_ID = 26000
DREAD_POUCH_ID = 46000
DREAD_BODY_MODEL = 120000
DREAD_HEAD_MODEL = 120001
DREAD_POUCH_MODEL = 120002
DREAD_READY_SEQ = 23000
DREAD_WALK_SEQ = 23001
DREAD_SUMMON_SPOTANIM = 20001

SIDEBAR_GROUP = 969
SIDEBAR_TITLE = (SIDEBAR_GROUP << 16) | 22
SIDEBAR_HEAD = (SIDEBAR_GROUP << 16) | 13
SIDEBAR_CALL = (SIDEBAR_GROUP << 16) | 27
SIDEBAR_DISMISS = (SIDEBAR_GROUP << 16) | 29

VARP_ACTIVE = 6226
VARP_ACCUMULATOR = 6227
VARP_TICKS = 6230
VARP_TYPE = 6257
DREAD_TYPE = 2
SUMMONING_STAT = 24


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=REPO / "src/torirs")
    parser.add_argument("--cache", type=Path, default=REPO / "cache.osrs239.summoning")
    parser.add_argument(
        "--scripts", type=Path, default=CONTENT / "server/scripts/build_summoning"
    )
    parser.add_argument("--manifest", type=Path, default=REPO / "manifest_osrs239.ini")
    parser.add_argument(
        "--out", type=Path, default=REPO / "build/summoning-phase5b-runtime"
    )
    args = parser.parse_args()

    errors: list[str] = []
    checked = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal checked
        checked += 1
        if not condition:
            errors.append(message)

    static_contract(expect)
    for label, path in (
        ("normal embedded client", args.client),
        ("feature cache", args.cache / "main_file_cache.dat2"),
        ("feature script pack", args.scripts / "script.dat"),
        ("manifest", args.manifest),
    ):
        expect(path.is_file() and path.stat().st_size > 0, f"missing {label}: {path}")
    if errors:
        return finish(errors, checked, args.out)

    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="summoning_phase5b_runtime_") as root:
        root_path = Path(root)
        actual_saves = root_path / "actual_pouch_saves"
        actual_saves.mkdir()
        actual = run_client(
            args,
            actual_saves,
            {
                # This is provisioning only. The following UI click must consume
                # the item through the real opheld4 server trigger.
                "TORIRS_NET_CHEAT": "summoning_unlock;setlevel summoning 4;summoning_dreadfowl_pouch",
                "TORIRS_MAX_FRAMES": "480",
                # Open the actual sidebar after the real item action so the
                # exit frame proves the active portrait before the separate
                # Call/Dismiss clone intentionally hides it.
                "TORIRS_SIM_CLICK_AT": "220,540,230,1;270,540,260;350,635,185;410,677,226",
                "MOCK230_VERBOSE": "1",
                "TORIRS_MINIMENU_DEBUG": "1",
                "TORIRS_CLICK_DEBUG": "1",
                "TORIRS_NET_DEBUG": "1",
                "TORIRS_NPC_HEAD_DEBUG": "1",
                "TORIRS_ANIM_DEBUG": "1",
                "TORIRS_MODEL_FMT_DEBUG": "1",
                "TORIRS_DUMP_BOUNDS": str(SIDEBAR_GROUP),
                "TORIRS_DUMP_EMIT_EXIT": str(SIDEBAR_GROUP),
                "TORIRS_EXIT_BMP": str((args.out / "actual-pouch.bmp").resolve()),
            },
        )
        write_log(args.out / "actual-pouch.log", actual.stdout)
        expect(actual.returncode == 0, f"actual pouch: client exited {actual.returncode}")
        expect("SKIP" not in actual.stdout, "actual pouch: client reported SKIP")
        expect("CS2VM2: abort" not in actual.stdout, "actual pouch: clientscript aborted")
        expect(
            (args.out / "actual-pouch.bmp").is_file()
            and (args.out / "actual-pouch.bmp").stat().st_size > 54,
            "actual pouch: no framebuffer was emitted",
        )
        assert_actual_pouch_path(actual.stdout, expect)
        assert_live_dreadfowl(
            actual.stdout, "actual pouch", expect, sidebar_expected=True, sidebar_at_exit=True
        )

        actual_state = parse_save(actual_saves / "guest.ini")
        expect(
            actual_state.inventory.get(DREAD_POUCH_ID, 0) == 0,
            "actual pouch: Dreadfowl pouch remained after the Summon action",
        )
        expect(
            actual_state.varps.get(VARP_ACTIVE) == 1,
            f"actual pouch: active varp {VARP_ACTIVE} is not 1",
        )
        expect(
            actual_state.varps.get(VARP_TYPE) == DREAD_TYPE,
            f"actual pouch: familiar type varp {VARP_TYPE} is not Dreadfowl ({DREAD_TYPE})",
        )
        actual_ticks = actual_state.varps.get(VARP_TICKS, -1)
        actual_acc = actual_state.varps.get(VARP_ACCUMULATOR, -1)
        expect(
            0 < actual_ticks <= 400,
            f"actual pouch: lifetime is {actual_ticks}, not a live <=400 Dreadfowl timer",
        )
        expect(0 <= actual_acc < 100, f"actual pouch: point accumulator is {actual_acc}, expected 0..99")
        actual_stat = actual_state.stats.get(SUMMONING_STAT)
        expect(
            actual_stat is not None and actual_stat[0] == 3,
            f"actual pouch: one initial Summoning point was not consumed (stat 24={actual_stat})",
        )

        # Each restart begins from a copy of the genuine pouch-created save.
        # Altering the temporary timer/accumulator only compresses a 100-tick
        # cadence observation; it never creates a familiar or invokes summon.
        cadence_control = clone_dread_save(actual_saves, root_path / "cadence_control", 0)
        control_before = parse_save(cadence_control / "guest.ini")
        control = run_client(
            args,
            cadence_control,
            runtime_env(args.out / "cadence-control.bmp", 460),
        )
        write_log(args.out / "cadence-control.log", control.stdout)
        expect(control.returncode == 0, f"cadence control: client exited {control.returncode}")
        expect("CS2VM2: abort" not in control.stdout, "cadence control: clientscript aborted")
        assert_live_dreadfowl(control.stdout, "cadence control relog", expect)
        assert_cadence(
            "cadence control",
            control_before,
            parse_save(cadence_control / "guest.ini"),
            0,
            0,
            expect,
        )

        cadence_boundary = clone_dread_save(actual_saves, root_path / "cadence_boundary", 99)
        boundary_before = parse_save(cadence_boundary / "guest.ini")
        boundary = run_client(
            args,
            cadence_boundary,
            runtime_env(args.out / "cadence-boundary.bmp", 460),
        )
        write_log(args.out / "cadence-boundary.log", boundary.stdout)
        expect(boundary.returncode == 0, f"cadence boundary: client exited {boundary.returncode}")
        expect("CS2VM2: abort" not in boundary.stdout, "cadence boundary: clientscript aborted")
        assert_live_dreadfowl(boundary.stdout, "cadence boundary relog", expect)
        assert_cadence(
            "cadence boundary",
            boundary_before,
            parse_save(cadence_boundary / "guest.ini"),
            99,
            1,
            expect,
        )

        # The final clone proves that the real sidebar controls operate after a
        # no-cheat relog. It is intentionally last: Dismiss clears the state.
        buttons_saves = clone_dread_save(actual_saves, root_path / "buttons", 0)
        buttons = run_client(
            args,
            buttons_saves,
            {
                **runtime_env(args.out / "buttons.bmp", 620),
                # Open Equipment, use its top-right familiar entry button,
                # then exercise Call and Dismiss in the mounted familiar view.
                "TORIRS_SIM_CLICK_AT": "350,635,185;430,677,226;500,550,440;560,600,440",
                "TORIRS_CLICK_DEBUG": "1",
            },
        )
        write_log(args.out / "buttons.log", buttons.stdout)
        expect(buttons.returncode == 0, f"sidebar buttons: client exited {buttons.returncode}")
        expect("CS2VM2: abort" not in buttons.stdout, "sidebar buttons: clientscript aborted")
        assert_live_dreadfowl(buttons.stdout, "sidebar relog", expect, sidebar_expected=True)
        assert_sidebar_buttons(buttons.stdout, expect)
        buttons_state = parse_save(buttons_saves / "guest.ini")
        for label, varp in (
            ("active", VARP_ACTIVE),
            ("type", VARP_TYPE),
            ("ticks", VARP_TICKS),
            ("accumulator", VARP_ACCUMULATOR),
        ):
            expect(
                buttons_state.varps.get(varp, 0) == 0,
                f"sidebar buttons: Dismiss did not clear {label} varp {varp}",
            )

    return finish(errors, checked, args.out)


def static_contract(expect: object) -> None:
    """Check authored bindings which the current client cannot trace live.

    The client emits the applied ready sequence but not a separate walk-track
    transition. Both target sequences and their model/head mappings are thus
    asserted from the exact admitted configuration; the real session proves
    the type, body, ready bind, and composed sidebar head actually load.
    """
    check = expect
    npc_path = LANE / "configs/summoning_cohort_dreadfowl.npc"
    obj_path = LANE / "configs/summoning_cohort_dreadfowl.obj"
    script_path = SCRIPT_LANE / "scripts/summoning_spirit_wolf.rs2"
    constants_path = SCRIPT_LANE / "configs/summoning.constant"
    varp_cfg_path = SCRIPT_LANE / "configs/summoning.varp"
    alloc_path = CONTENT / "pack/varp.alloc"
    seq_alloc_path = LANE / "pack/seq.alloc"

    files = (
        ("Dreadfowl NPC config", npc_path),
        ("Dreadfowl pouch config", obj_path),
        ("Summoning runtime script", script_path),
        ("Summoning constants", constants_path),
        ("Summoning varp config", varp_cfg_path),
        ("varp allocation", alloc_path),
        ("sequence allocation", seq_alloc_path),
    )
    for label, path in files:
        check(path.is_file(), f"missing {label}: {path}")
    if not all(path.is_file() for _, path in files):
        return

    npc = config_props(npc_path, DREAD_NPC)
    pouch = config_props(obj_path, DREAD_POUCH)
    for key, value in (
        ("name", "Dreadfowl"),
        ("model1", str(DREAD_BODY_MODEL)),
        ("head1", str(DREAD_HEAD_MODEL)),
        ("readyanim", "summoning_cohort_dreadfowl_seq_5386"),
        ("walkanim", "summoning_cohort_dreadfowl_seq_7808"),
    ):
        check(npc.get(key) == value, f"Dreadfowl NPC {key}={npc.get(key)!r}, expected {value!r}")
    check(pouch.get("name") == "Dreadfowl pouch", "Dreadfowl pouch has the wrong display name")
    check(pouch.get("model") == str(DREAD_POUCH_MODEL), "Dreadfowl pouch does not bind model 120002")
    check(pouch.get("ifop4") == "Summon", "Dreadfowl pouch is not authored as ifop4=Summon")

    seq_alloc = read(seq_alloc_path)
    check(
        f"{DREAD_READY_SEQ}=summoning_cohort_dreadfowl_seq_5386" in seq_alloc,
        "Dreadfowl ready animation is not allocated at target sequence 23000",
    )
    check(
        f"{DREAD_WALK_SEQ}=summoning_cohort_dreadfowl_seq_7808" in seq_alloc,
        "Dreadfowl walk animation is not allocated at target sequence 23001",
    )

    script = read(script_path)
    check(
        f"[opheld4,{DREAD_POUCH}]" in script and "~summoning_dreadfowl_summon(true);" in script,
        "Dreadfowl pouch lacks its canonical opheld4 handler",
    )
    grant = block(script, "[debugproc,summoning_dreadfowl_pouch]")
    check(
        f"inv_add(inv, {DREAD_POUCH}, 1);" in grant,
        "Dreadfowl debug hook does not provision its real pouch",
    )
    check(
        "summoning_dreadfowl_summon" not in grant,
        "Dreadfowl debug hook bypasses the real pouch interaction",
    )
    check(
        "~summoning_familiar_body_model(%summoning_familiar_type)" in script
        and "~summoning_familiar_ready_seq(%summoning_familiar_type)" in script
        and "if_setanim(summoning_familiar:model, $ready_seq);" in script
        and 'if_settext(summoning_familiar:title, $name);' in script,
        "sidebar does not bind the selected familiar body, ready sequence, and title",
    )
    check(
        "npc_add(movecoord(coord, 1, 0, 0), $npc, 0);" in script
        and "[proc,summoning_login]" in script,
        "login does not reconstruct the selected familiar NPC",
    )
    check(
        "npc_tele(movecoord(coord, 1, 0, 0));" in script
        and "npc_setmode(playerfollow);" in script,
        "Call does not move the familiar back into player-follow state",
    )

    constants = parse_constants(constants_path)
    for name, value in (
        ("^summoning_dreadfowl_level", 4),
        ("^summoning_dreadfowl_cost", 1),
        ("^summoning_dreadfowl_lifetime", 400),
        ("^summoning_dreadfowl_drain_interval", 100),
    ):
        check(constants.get(name) == value, f"{name}={constants.get(name)!r}, expected {value}")

    alloc = parse_alloc(alloc_path)
    for name, value in (
        ("summoning_familiar_active", VARP_ACTIVE),
        ("summoning_familiar_point_accumulator", VARP_ACCUMULATOR),
        ("summoning_familiar_ticks", VARP_TICKS),
        ("summoning_familiar_type", VARP_TYPE),
    ):
        check(alloc.get(name) == value, f"varp allocation {name}={alloc.get(name)!r}, expected {value}")
    varp_cfg = read(varp_cfg_path)
    for name in (
        "summoning_familiar_active",
        "summoning_familiar_point_accumulator",
        "summoning_familiar_ticks",
        "summoning_familiar_type",
    ):
        check(
            re.search(rf"(?ms)^\[{re.escape(name)}\]\s*.*?^scope=perm\s*$", varp_cfg) is not None,
            f"persisted familiar varp {name} is not scope=perm",
        )


def assert_actual_pouch_path(log: str, expect: object) -> None:
    check = expect
    check(
        re.search(
            r'row\[3\] action=2231 op=5 kind=2 id=\d+ "Summon @lre@ Dreadfowl pouch"', log
        ) is not None,
        "actual pouch: visible Summon row is not native action=2231/op=5",
    )
    check(
        "mock230: <- IF_BUTTONX 149:0 sub=0 obj=46000 op=6 subop=-1" in log,
        "actual pouch: Summon did not serialize as the rev239 dynamic IF_BUTTONX op=6",
    )
    check(
        re.search(r"mock230: <- OPHELD4 obj=46000 \(Dreadfowl pouch\) slot=0 com=149\|0", log)
        is not None,
        "actual pouch: IF_BUTTONX op=6 was not canonically dispatched as OPHELD4",
    )
    check(
        "message_game: You summon a Dreadfowl." in log,
        "actual pouch: canonical OPHELD4 did not complete Dreadfowl summon",
    )
    check(
        f"entity_spotanim: combine id={DREAD_SUMMON_SPOTANIM}" in log,
        "actual pouch: Dreadfowl did not render the small familiar-arrival graphic",
    )
    check("OPHELD5 obj=46000" not in log, "actual pouch: dynamic Summon was misrouted to OPHELD5")
    check(
        "message_game: You drop the Dreadfowl pouch." not in log,
        "actual pouch: visible Summon row dropped the pouch instead",
    )


def assert_live_dreadfowl(
    log: str,
    label: str,
    expect: object,
    *,
    sidebar_expected: bool = False,
    sidebar_at_exit: bool = False,
) -> None:
    check = expect
    # NPC_INFO first creates the wire slot with type 0, then its extended
    # CHANGETYPE installs Dreadfowl.  The latter is the meaningful live type
    # assertion for the normal server route (support a direct typed spawn too
    # so this remains correct if that packet shape is later simplified).
    typed = re.search(
        r"entity_sync: npc type replacement=26000 element=(\d+) .*model=installed", log
    )
    direct = re.search(r"spawn_npc: npc=26000 element=(\d+) ", log)
    element = typed.group(1) if typed is not None else (direct.group(1) if direct is not None else None)
    check(element is not None, f"{label}: no live NPC type 26000 was installed")
    if element is not None:
        check(
            re.search(rf"seq_bind: element={element} seq=23000 .*kind=1 vbones=[1-9]\d*", log)
            is not None,
            f"{label}: Dreadfowl body did not bind playable ready sequence 23000",
        )
    check(
        f"model_fmt: id={DREAD_BODY_MODEL} " in log,
        f"{label}: Dreadfowl body model {DREAD_BODY_MODEL} was not loaded by the client",
    )
    # The familiar tab intentionally uses the complete animated body model,
    # not the NPC chathead. The body-model load above covers both world and UI;
    # the final draw-list assertion below proves the UI scene received it.
    if sidebar_expected:
        check(
            f"if_settext: com={SIDEBAR_TITLE} text='Dreadfowl' applied=1" in log,
            f"{label}: sidebar title was not synchronized to Dreadfowl",
        )
        check(
            f"if_sethide: com={SIDEBAR_HEAD} hide=0 applied=1" in log,
            f"{label}: sidebar Dreadfowl head was not made visible",
        )
    if sidebar_at_exit:
        check(
            re.search(r"EMIT_EXIT.*kind=5.*\(969\|13\).*model=\d+", log) is not None,
            f"{label}: composed Dreadfowl sidebar head did not reach the final draw list",
        )


def assert_cadence(
    label: str,
    before: "SavedState",
    after: "SavedState",
    initial_accumulator: int,
    expected_point_loss: int,
    expect: object,
) -> None:
    check = expect
    ticks_before = before.varps.get(VARP_TICKS, -1)
    ticks_after = after.varps.get(VARP_TICKS, -1)
    elapsed = ticks_before - ticks_after
    check(
        1 <= elapsed < 100,
        f"{label}: observed {elapsed} timer ticks; expected a bounded 1..99 no-cheat relog run",
    )
    current_before = before.stats.get(SUMMONING_STAT, (-999999, 0))[0]
    current_after = after.stats.get(SUMMONING_STAT, (-999999, 0))[0]
    check(
        current_after == current_before - expected_point_loss,
        f"{label}: Summoning points {current_before}->{current_after}, expected loss {expected_point_loss}",
    )
    expected_acc = initial_accumulator + elapsed - expected_point_loss * 100
    check(
        after.varps.get(VARP_ACCUMULATOR) == expected_acc,
        f"{label}: accumulator {after.varps.get(VARP_ACCUMULATOR)}, expected {expected_acc}",
    )
    check(after.varps.get(VARP_ACTIVE) == 1, f"{label}: relog lost familiar active state")
    check(after.varps.get(VARP_TYPE) == DREAD_TYPE, f"{label}: relog lost Dreadfowl type state")


def assert_sidebar_buttons(log: str, expect: object) -> None:
    check = expect
    check(
        f"entity_spotanim: combine id={DREAD_SUMMON_SPOTANIM}" in log,
        "sidebar buttons: Call did not replay the small familiar-arrival graphic",
    )
    check(
        "clickdbg: send op1 target=0xa1003f sub=-1 state=2" in log,
        "sidebar buttons: did not open the Equipment tab",
    )
    check(
        "clickdbg: send op1 target=0x183001f sub=-1 state=2" in log,
        "sidebar buttons: did not use the Equipment-mounted familiar entry button",
    )
    check(
        f"clickdbg: send op1 target=0x{SIDEBAR_CALL:x} sub=-1 state=2" in log,
        "sidebar buttons: Call did not send its real IF_BUTTON1 packet",
    )
    check(
        "message_game: You call your familiar." in log,
        "sidebar buttons: Call did not execute the Dreadfowl movement handler",
    )
    check(
        f"clickdbg: send op1 target=0x{SIDEBAR_DISMISS:x} sub=-1 state=2" in log,
        "sidebar buttons: Dismiss did not send its real IF_BUTTON1 packet",
    )
    check(
        "message_game: You dismiss your familiar." in log,
        "sidebar buttons: Dismiss did not execute the familiar handler",
    )
    check(
        f"if_sethide: com={SIDEBAR_HEAD} hide=1 applied=1" in log,
        "sidebar buttons: Dismiss did not hide the Dreadfowl sidebar head",
    )


def runtime_env(bmp: Path, max_frames: int) -> dict[str, str]:
    """No-cheat environment for a persisted-state relog."""
    return {
        "TORIRS_MAX_FRAMES": str(max_frames),
        "MOCK230_VERBOSE": "1",
        "TORIRS_NET_DEBUG": "1",
        "TORIRS_NPC_HEAD_DEBUG": "1",
        "TORIRS_ANIM_DEBUG": "1",
        "TORIRS_MODEL_FMT_DEBUG": "1",
        "TORIRS_DUMP_BOUNDS": str(SIDEBAR_GROUP),
        "TORIRS_DUMP_EMIT_EXIT": str(SIDEBAR_GROUP),
        "TORIRS_EXIT_BMP": str(bmp.resolve()),
    }


def run_client(
    args: argparse.Namespace, saves: Path, extra: dict[str, str]
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    # A caller's shell must not accidentally turn a no-cheat relog into a
    # debug-created state. Keep the deliberately supplied case values only.
    for key in (
        "TORIRS_NET_CHEAT",
        "TORIRS_SIM_CLICK_AT",
        "TORIRS_MAX_FRAMES",
        "TORIRS_CLICK_DEBUG",
        "TORIRS_MINIMENU_DEBUG",
        "TORIRS_NET_DEBUG",
        "TORIRS_NPC_HEAD_DEBUG",
        "TORIRS_ANIM_DEBUG",
        "TORIRS_MODEL_FMT_DEBUG",
        "TORIRS_DUMP_BOUNDS",
        "TORIRS_DUMP_EMIT_EXIT",
        "TORIRS_EXIT_BMP",
        "MOCK230_VERBOSE",
    ):
        env.pop(key, None)
    env.update(
        {
            "MOCK230_SAVES": str(saves.resolve()),
            "MOCK230_SCRIPTS": str(args.scripts.resolve()),
            "MOCK230_CACHE": str(args.cache.resolve()),
            "SDL_VIDEODRIVER": "dummy",
        }
    )
    env.update(extra)
    try:
        return subprocess.run(
            [str(args.client), str(args.cache), "--manifest", str(args.manifest), "--soft3d"],
            cwd=REPO,
            env=env,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=180,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", "replace")
        return subprocess.CompletedProcess(exc.cmd, 124, output)


class SavedState:
    def __init__(
        self,
        stats: dict[int, tuple[int, int]],
        inventory: dict[int, int],
        varps: dict[int, int],
    ) -> None:
        self.stats = stats
        self.inventory = inventory
        self.varps = varps


def parse_save(path: Path) -> SavedState:
    text = read(path)
    section = ""
    stats: dict[int, tuple[int, int]] = {}
    inventory: dict[int, int] = {}
    varps: dict[int, int] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        match = re.fullmatch(r"(\d+)\s*=\s*(-?\d+)(?:\s+(-?\d+))?(?:\s+(-?\d+))?", line)
        if not match:
            continue
        key = int(match.group(1))
        first = int(match.group(2))
        second = int(match.group(3)) if match.group(3) is not None else 0
        third = int(match.group(4)) if match.group(4) is not None else None
        if section == "stats" and match.group(3) is not None:
            # Current saves use ``boosted xp``. Accept the former
            # ``level boosted xp`` spelling too, keeping boosted as current.
            stats[key] = (first if third is None else second, second if third is None else third)
        elif section == "inv" and match.group(3) is not None:
            inventory[first] = inventory.get(first, 0) + second
        elif section == "varps":
            varps[key] = first
    return SavedState(stats, inventory, varps)


def clone_dread_save(source: Path, destination: Path, accumulator: int) -> Path:
    shutil.copytree(source, destination)
    save = destination / "guest.ini"
    set_ini_values(
        save,
        "varps",
        {
            VARP_ACTIVE: 1,
            VARP_ACCUMULATOR: accumulator,
            VARP_TICKS: 200,
            VARP_TYPE: DREAD_TYPE,
        },
    )
    return destination


def set_ini_values(path: Path, section: str, values: dict[int, int]) -> None:
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    section_index = next(
        (i for i, raw in enumerate(lines) if raw.strip() == f"[{section}]"), None
    )
    if section_index is None:
        if lines and not lines[-1].endswith("\n"):
            lines[-1] += "\n"
        lines.extend([f"\n[{section}]\n", "; test-owned value overrides\n"])
        section_index = len(lines) - 2
    end = next(
        (i for i in range(section_index + 1, len(lines)) if lines[i].strip().startswith("[")),
        len(lines),
    )
    for key, value in values.items():
        matcher = re.compile(rf"^(\s*{key}\s*=\s*).*$")
        found = next(
            (i for i in range(section_index + 1, end) if matcher.match(lines[i].rstrip("\r\n"))),
            None,
        )
        if found is None:
            lines.insert(end, f"{key} = {value}\n")
            end += 1
        else:
            suffix = "\n" if lines[found].endswith("\n") else ""
            lines[found] = f"{key} = {value}{suffix}"
    path.write_text("".join(lines), encoding="utf-8")


def config_props(path: Path, record: str) -> dict[str, str]:
    current: str | None = None
    props: dict[str, str] = {}
    for raw in read(path).splitlines():
        line = raw.strip()
        if not line or line.startswith("//"):
            continue
        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1]
            continue
        if current == record and "=" in line:
            key, value = line.split("=", 1)
            props[key.strip()] = value.strip()
    return props


def parse_constants(path: Path) -> dict[str, int]:
    result: dict[str, int] = {}
    for raw in read(path).splitlines():
        match = re.fullmatch(r"\s*(\^\w+)\s*=\s*(-?\d+)\s*", raw)
        if match:
            result[match.group(1)] = int(match.group(2))
    return result


def parse_alloc(path: Path) -> dict[str, int]:
    result: dict[str, int] = {}
    for raw in read(path).splitlines():
        match = re.fullmatch(r"\s*(\d+)\s*=\s*(\S+)\s*", raw)
        if match:
            result[match.group(2)] = int(match.group(1))
    return result


def block(text: str, header: str) -> str:
    start = text.find(header)
    if start < 0:
        return ""
    end = text.find("\n[", start + len(header))
    return text[start:] if end < 0 else text[start:end]


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return ""


def write_log(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def finish(errors: list[str], checked: int, out: Path) -> int:
    for error in errors:
        print(f"test_summoning_phase5b_runtime: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase5b_runtime: {checked} checks, {len(errors)} errors")
    print(f"test_summoning_phase5b_runtime: logs and framebuffers in {out}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
