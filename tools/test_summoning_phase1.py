#!/usr/bin/env python3
"""Headless acceptance for the feature-flagged Summoning stat slice."""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=REPO_ROOT / "src" / "torirs")
    parser.add_argument("--flag-on", type=Path, default=REPO_ROOT / "cache.osrs239.summoning")
    parser.add_argument("--flag-off", type=Path, default=REPO_ROOT / "cache.osrs239")
    parser.add_argument("--scripts", type=Path, default=REPO_ROOT / "OSRS-Content/osrs239-content/server/scripts/build_summoning")
    parser.add_argument("--manifest", type=Path, default=REPO_ROOT / "manifest_osrs239.ini")
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "build" / "summoning-phase1")
    args = parser.parse_args()

    errors: list[str] = []
    checked = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal checked
        checked += 1
        if not condition:
            errors.append(message)

    for label, path in (
        ("client", args.client),
        ("flag-on cache", args.flag_on / "main_file_cache.dat2"),
        ("flag-off cache", args.flag_off / "main_file_cache.dat2"),
        ("feature server scripts", args.scripts / "script.dat"),
        ("manifest", args.manifest),
    ):
        expect(path.is_file(), f"missing {label}: {path}")
    if errors:
        for error in errors:
            print(f"test_summoning_phase1: error: {error}", file=sys.stderr)
        print(f"test_summoning_phase1: {checked} checks, {len(errors)} errors")
        return 1

    args.out.mkdir(parents=True, exist_ok=True)

    content_lane = (
        REPO_ROOT
        / "OSRS-Content/osrs239-content/ported/scape2009_summoning"
    )
    stats_source = (content_lane / "interfaces/stats.if").read_text(encoding="utf-8")
    icon_path = (
        REPO_ROOT
        / "OSRS-Content/osrs239-content/sprites/ported/scape2009_summoning"
        / "summoning_staticon/0.bmp"
    )
    icon_meta = icon_path.with_name("pack.meta").read_text(encoding="utf-8")
    expect(
        "[sailing]\nif3=yes\ntype=0\nx=127\ny=211" in stats_source,
        "source: Sailing moved out of its native grid cell",
    )
    expect(
        "[total]\nif3=yes\ntype=0\nx=64\ny=241\nwidth=126\nheight=30"
        in stats_source,
        "source: Total level is not a two-cell box on the scrollable ninth row",
    )
    expect(
        "[summoning_stats_cell]\nif3=yes\ntype=0\nx=1\ny=241\nwidth=62\nheight=30"
        in stats_source,
        "source: Summoning is not the first cell on the scrollable ninth row",
    )
    stats_script = (content_lane / "scripts/summoning_stats_init.cs2").read_text(
        encoding="utf-8"
    )
    expect(
        "if_setscrollsize(190, 271, $viewport7);" in stats_script
        and "if_setscrollpos(0, 0, $viewport7);" in stats_script,
        "source: Summoning stats clientscript does not create a scrollable viewport",
    )
    expect(
        'if_setonscrollwheel("summoning_stats_scroll($viewport7, event_mousey)", $viewport7);'
        in stats_script
        and (content_lane / "scripts/summoning_stats_scroll.cs2").is_file(),
        "source: Summoning stats viewport has no IF3 wheel handler",
    )
    expect(
        "onload=i:1198,i:-2147483645,i:20971553,i:25,i:2" in stats_source,
        "source: Summoning does not use the rev-530 wolf-head x nudge",
    )
    summoning_cell = stats_source.split("[summoning_stats_cell]", 1)[1]
    expect(
        "clickmask=" not in summoning_cell
        and "op1=" not in summoning_cell
        and "op2=" not in summoning_cell,
        "source: Summoning stats cell is still clickable",
    )
    expect(
        "cc_setposition(calc(3 + $int3), 4, ^setpos_abs_left, ^setpos_abs_top);"
        in stats_script,
        "source: Summoning icon is not vertically aligned with native skill icons",
    )
    expect(
        "if_setsize(126, 30, ^setsize_abs, ^setsize_abs, stats:25);"
        in stats_script,
        "source: Total XP box is not a full-height ninth-row panel",
    )
    expect(
        'if_setop(2, "View <col=ff981f><$string0></col> guide", $component0);'
        not in stats_script
        and 'if_setop(1, "Toggle <col=ff981f><$string0></col> XP", $component0);'
        not in stats_script,
        "source: Summoning clientscript still installs click operations",
    )
    expect(
        'if_setonvartransmit("summoning_stats_init($component0, $int1, $int2, $int3)'
        in stats_script,
        "source: Summoning can regain native click operations after a var transmit",
    )
    expect(
        "sprite0=25,25,22,23,0,2" in icon_meta,
        "source: Summoning icon canvas metadata is not the rev-530 sprite",
    )
    expect(
        hashlib.sha256(icon_path.read_bytes()).hexdigest()
        == "89726834d13ce73b8fff38eb34567ed2e52c7757b2d8405577e801979e4178cd",
        "source: Summoning icon pixels are not rev-530 sprite pack 222",
    )

    def run(
        name: str,
        cache: Path,
        saves: Path,
        cheat: str | None = None,
        sim_oploc: str | None = None,
        sim_wheel: str | None = None,
    ) -> str:
        bmp = args.out / f"{name}.bmp"
        log_path = args.out / f"{name}.log"
        env = os.environ.copy()
        env.update(
            {
                "MOCK230_SAVES": str(saves),
                "MOCK230_SCRIPTS": str(args.scripts),
                "SDL_VIDEODRIVER": "dummy",
                "TORIRS_MAX_FRAMES": "430",
                "TORIRS_EXIT_BMP": str(bmp),
                "TORIRS_DUMP_TREE_EXIT": "1",
                "TORIRS_SIM_CLICK_AT": "200,535,186",
            }
        )
        if cheat is not None:
            env["TORIRS_NET_CHEAT"] = cheat
        else:
            env.pop("TORIRS_NET_CHEAT", None)
        if sim_oploc is not None:
            env["TORIRS_SIM_OPLOC"] = sim_oploc
        else:
            env.pop("TORIRS_SIM_OPLOC", None)
        if sim_wheel is not None:
            env["TORIRS_SIM_WHEEL"] = sim_wheel
            env["TORIRS_TRACE_DRAG"] = "1"
        else:
            env.pop("TORIRS_SIM_WHEEL", None)
            env.pop("TORIRS_TRACE_DRAG", None)
        command = [
            str(args.client),
            str(cache),
            "--manifest",
            str(args.manifest),
            "--soft3d",
        ]
        result = subprocess.run(
            command,
            cwd=REPO_ROOT,
            env=env,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=180,
            check=False,
        )
        log_path.write_text(result.stdout, encoding="utf-8")
        expect(result.returncode == 0, f"{name}: client exited {result.returncode}")
        expect(bmp.is_file() and bmp.stat().st_size > 54, f"{name}: no non-empty BMP")
        expect("SKIP" not in result.stdout, f"{name}: headless run reported SKIP")
        return result.stdout

    with tempfile.TemporaryDirectory(prefix="summoning_phase1_") as temporary:
        root = Path(temporary)
        locked = run("account_locked", args.flag_on, Path(tempfile.mkdtemp(dir=root)))
        wolf_saves = Path(tempfile.mkdtemp(dir=root))
        wolf_complete = run(
            "wolf_whistle_complete",
            args.flag_on,
            wolf_saves,
            "tele 3224 3220",
            "180,3,3224,3220,62201",
        )
        wolf_relog = run("wolf_whistle_relog", args.flag_on, wolf_saves)
        level1 = run(
            "flag_on_level1",
            args.flag_on,
            Path(tempfile.mkdtemp(dir=root)),
            "summoning_unlock",
            sim_wheel="300,600,300,-1,1",
        )
        persistent_saves = Path(tempfile.mkdtemp(dir=root))
        level20 = run(
            "flag_on_level20",
            args.flag_on,
            persistent_saves,
            "summoning_unlock;setlevel summoning 20",
        )
        persisted = run("flag_on_persisted", args.flag_on, persistent_saves)
        flagoff = run("flag_off", args.flag_off, Path(tempfile.mkdtemp(dir=root)))

        expect('text="Total level: 33"' in locked, "account_locked: total still includes Summoning")
        wolf_save = (wolf_saves / "guest.ini").read_text(encoding="utf-8")
        expect(
            "Congratulations! You have completed Wolf Whistle!" in wolf_complete,
            "Wolf Whistle obelisk interaction did not announce itself in the real client",
        )
        expect(
            "sim_oploc: op=3 tile=3224,3220 loc=62201" in wolf_complete
            and "summoning_wolf_whistle_complete' -> debugproc" not in wolf_complete,
            "Wolf Whistle did not traverse the ordinary client OPLOC3 path",
        )
        expect(
            "24 = 4 2760" in wolf_save,
            "Wolf Whistle completion did not grant the source's 276 Summoning XP",
        )
        expect(
            "40256 275" in wolf_save,
            "Wolf Whistle completion did not persist 275 quest-copy gold charms",
        )
        expect("320<<16|34" in wolf_complete, "Wolf Whistle completion did not unlock the skill tab")
        expect("320<<16|34" in wolf_relog, "Wolf Whistle unlock did not survive relog")
        expect("320<<16|34" in level1, "flag_on_level1: Summoning cell is absent")
        expect(
            "969<<16|0" in level1
            and 'text="Summoning points: 1/1"' in level1
            and "if-opensub: target" not in level1,
            "flag_on_level1: familiar sidebar did not mount into the live gameframe",
        )
        expect(
            "dynamic graphic=229 abs=513,446 25x25 hidden=0" in level1,
            "flag_on_level1: wolf-head sprite is not rendered in row nine",
        )
        expect(
            "static font=494 color=0xffff00 text=\"Total level: 34\" abs=573,448 126x29"
            in level1,
            "flag_on_level1: two-cell Total level box does not render beside Summoning",
        )
        expect(
            "setscrollpos id=20971520 req_sy=10 max_y=10 applied_sy=10 "
            "scroll_h=271 abs_h=261" in level1,
            "flag_on_level1: mouse wheel did not scroll the Summoning stats viewport",
        )
        expect('text="Total level: 34"' in level1, "flag_on_level1: total is not 34")
        expect(
            "cheat 'setlevel summoning 20' -> debugproc not found" in level20
            and "Set stat 24 to 20." in level20,
            "flag_on_level20: named setlevel did not resolve to stat 24",
        )
        expect("320<<16|34" in level20, "flag_on_level20: Summoning cell is absent")
        expect('text="Total level: 53"' in level20, "flag_on_level20: total is not 53")
        expect('text="20"' in level20, "flag_on_level20: level 20 text is absent")
        save_text = (persistent_saves / "guest.ini").read_text(encoding="utf-8")
        expect("24 = 20 44700" in save_text, "save: stat 24 level/XP did not persist")
        expect("320<<16|34" in persisted, "flag_on_persisted: Summoning cell is absent")
        expect('text="Total level: 53"' in persisted, "flag_on_persisted: total is not 53")
        expect('text="20"' in persisted, "flag_on_persisted: level 20 text is absent")
        expect('text="Total level: 33"' not in persisted, "relog: account unlock did not persist")
        expect("320<<16|34" not in flagoff, "flag_off: Summoning cell leaked")
        expect('text="Total level: 33"' in flagoff, "flag_off: total is not 33")

    for error in errors:
        print(f"test_summoning_phase1: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase1: {checked} checks, {len(errors)} errors")
    print(f"test_summoning_phase1: seven BMPs and logs in {args.out}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
