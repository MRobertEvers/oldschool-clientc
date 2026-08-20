#!/usr/bin/env python3
"""Real-client acceptance for authored Summoning pouch infusion."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
CONTENT = REPO / "OSRS-Content/osrs239-content"
SUMMONING_LANE = CONTENT / "ported/scape2009_summoning"
SUMMONING_SCRIPTS = CONTENT / "server/scripts/ported_scape2009_summoning"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=REPO / "src/torirs")
    parser.add_argument("--cache", type=Path, default=REPO / "cache.osrs239.summoning")
    parser.add_argument(
        "--scripts",
        type=Path,
        default=CONTENT / "server/scripts/build_summoning",
    )
    parser.add_argument("--manifest", type=Path, default=REPO / "manifests/manifest_osrs239.ini")
    parser.add_argument("--out", type=Path, default=REPO / "build/summoning-phase4f")
    args = parser.parse_args()

    errors: list[str] = []
    checked = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal checked
        checked += 1
        if not condition:
            errors.append(message)

    for label, file_path in (
        ("embedded client", args.client),
        ("feature cache", args.cache / "main_file_cache.dat2"),
        ("feature script pack", args.scripts / "script.dat"),
        ("manifest", args.manifest),
        ("infusion interface", SUMMONING_LANE / "interfaces/summoning_infuse.if"),
        ("infusion component pack", SUMMONING_LANE / "interfaces/summoning_infuse.compack"),
    ):
        expect(file_path.is_file() and file_path.stat().st_size > 0, f"missing {label}: {file_path}")

    interface = read(SUMMONING_LANE / "interfaces/summoning_infuse.if")
    interface_pack = read(SUMMONING_LANE / "interfaces/summoning_infuse.compack")
    lane_pack = read(SUMMONING_LANE / "pack/3_interfaces.pack")
    script = read(SUMMONING_SCRIPTS / "scripts/summoning_infuse.rs2")
    points_script = read(SUMMONING_SCRIPTS / "scripts/summoning_points.rs2")
    import_manifest = read(REPO / "docs/summoning_port/spirit_wolf_import.ini")

    expect("970=summoning_infuse" in lane_pack, "the authored infusion group is not allocated")
    expect(
        "[universe]" in interface and "if3=yes" in interface and "width=360" in interface,
        "infusion window is not a fresh target IF3 universe",
    )
    expect(
        "[spirit_wolf]" in interface and "clickmask=62" in interface,
        "Spirit wolf row does not expose ordinary IF3 operations",
    )
    for operation_number, operation in enumerate(
        ("Infuse", "Infuse-5", "Infuse-10", "Infuse-X", "Infuse-All"), start=1
    ):
        expect(
            f"op{operation_number}={operation}" in interface,
            f"missing authored pouch operation: {operation}",
        )
    expect(
        "9=pouch_model" in interface_pack and "model=100002" in interface,
        "pouch output model is not authored into the panel",
    )
    expect("[oploc1,summoning_obelisk]" in points_script, "Obelisk op1 does not open infusion")
    expect(
        "%summoning_infuse_obelisk_coord = loc_coord;" in points_script,
        "oploc1 does not preserve the actual clicked obelisk",
    )
    expect(
        "if_opensub(toplevel_osrs_stretch:mainmodal, summoning_infuse, 0);" in script,
        "infusion is not mounted through the target mainmodal role alias",
    )
    for trigger in range(1, 6):
        expect(
            f"[if_button{trigger},summoning_infuse:spirit_wolf]" in script,
            f"missing server handler for pouch operation {trigger}",
        )
    expect(
        "loc_find(%summoning_infuse_obelisk_coord, summoning_obelisk)" in script,
        "button action does not recover its saved live obelisk",
    )
    expect(
        "anim(summoning_infuse_anim, 0);" in script
        and "loc_anim(summoning_obelisk_charge);" in script
        and "loc_anim(summoning_seq_8510);" in script,
        "craft does not use the imported player/active/idle animations",
    )
    # Source seq 9068 holds: framestep 27 of 41 frames, no maxloops, so the
    # client replays its tail 99 times unless the server ends it -- and a
    # playing primary seq also freezes the entity's draw position, so the
    # player is animating in place and cannot walk for minutes afterwards.
    expect(
        "anim(null, 0);" in script,
        "craft never ends the held infusion animation",
    )
    expect(
        "inv_del(inv, summoning_blank_pouch, $amount);" in script
        and "inv_del(inv, summoning_charm_gold, $amount);" in script
        and "inv_del(inv, wolf_bones, $amount);" in script
        and "summoning_spirit_wolf_infuse_shards" in script,
        "craft does not consume the complete pouch recipe",
    )
    expect(
        "stat_advance(summoning, calc($amount * ^summoning_spirit_wolf_infuse_xp));" in script,
        "craft does not award target-native tenths of Summoning XP",
    )
    expect(
        "2209" not in script and "5344" not in script,
        "craft still embeds the source obelisk coordinate",
    )
    expect(
        "sound_synth" not in script,
        "craft uses a source synth that has not crossed the audio fidelity boundary",
    )
    expect(
        "12155=blank_pouch" in import_manifest
        and "8509=obelisk_charge" in import_manifest
        and "9068=infuse_anim" in import_manifest,
        "pouch and craft-animation source imports are not documented in the manifest",
    )

    obj_alloc = SUMMONING_LANE / "pack/obj.alloc"
    seq_alloc = SUMMONING_LANE / "pack/seq.alloc"
    pouch_id = allocated_id(obj_alloc, "summoning_spirit_wolf_pouch")
    shard_id = allocated_id(obj_alloc, "summoning_shard")
    charm_id = allocated_id(obj_alloc, "summoning_charm_gold")
    blank_pouch_id = allocated_id(obj_alloc, "summoning_blank_pouch")
    charge_seq_id = allocated_id(seq_alloc, "summoning_obelisk_charge")
    infuse_seq_id = allocated_id(seq_alloc, "summoning_infuse_anim")
    idle_seq_id = allocated_id(seq_alloc, "summoning_seq_8510")
    wolf_bones_id = named_config_id(CONTENT / "configs/all.obj.compack", "wolf_bones")
    for label, value in (
        ("Spirit wolf pouch id", pouch_id),
        ("Spirit shard id", shard_id),
        ("Gold charm id", charm_id),
        ("blank pouch id", blank_pouch_id),
        ("obelisk charge sequence id", charge_seq_id),
        ("infuse sequence id", infuse_seq_id),
        ("obelisk idle sequence id", idle_seq_id),
        ("wolf bones id", wolf_bones_id),
    ):
        expect(value is not None, f"could not resolve {label} from target allocation/config")

    if errors:
        return finish(errors, checked, args.out)
    assert pouch_id is not None
    assert shard_id is not None
    assert charm_id is not None
    assert blank_pouch_id is not None
    assert charge_seq_id is not None
    assert infuse_seq_id is not None
    assert idle_seq_id is not None
    assert wolf_bones_id is not None

    args.out.mkdir(parents=True, exist_ok=True)
    render_code, render_log, render_bmp = render_panel(args)
    (args.out / "render.log").write_text(render_log, encoding="utf-8")
    expect(render_code == 0, f"render: client exited {render_code}")
    expect("SKIP" not in render_log, "render: client reported SKIP")
    expect("CS2VM2: abort" not in render_log, "render: clientscript aborted")
    expect(
        "loc_cfg 62201: name='Obelisk' size=2x2 seq=20002" in render_log,
        "render: imported runtime Obelisk config did not decode",
    )
    expect("emit_loc 62201" in render_log, "render: runtime Obelisk never reached the scene draw list")
    expect("loc=62201 size=2x2" in render_log, "render: ordinary mouse pick did not hit the Obelisk")
    expect(
        "sim_click_at: frame=240 move 465,205 right=0" in render_log,
        "render: ordinary Obelisk op1 click was not injected",
    )
    expect(
        "if-opensub: iface=970 target=0x00a10010" in render_log,
        "render: op1 did not mount the authored mainmodal panel",
    )
    expect(
        "if_setevents: com=63569927 (970:7)" in render_log,
        "render: server did not arm pouch row operations",
    )
    expect(
        "BOUNDS com=0x03ca0000 (970|0)" in render_log,
        "render: authored panel has no final layout bounds",
    )
    expect(
        "EMIT_EXIT" in render_log
        and "(970|9)" in render_log
        and "kind=5" in render_log
        and "model=100002" in render_log,
        "render: pouch model did not reach the final draw list",
    )
    expect(
        render_bmp.is_file() and render_bmp.stat().st_size > 54,
        "render: panel framebuffer is absent",
    )

    craft_code, craft_log, save_text, craft_bmp = make_pouch(args)
    (args.out / "make.log").write_text(craft_log, encoding="utf-8")
    expect(craft_code == 0, f"make: client exited {craft_code}")
    expect("SKIP" not in craft_log, "make: client reported SKIP")
    expect("CS2VM2: abort" not in craft_log, "make: clientscript aborted")
    expect(
        "loc_anim with no active loc" not in craft_log,
        "make: button action lost its interacted Obelisk",
    )
    expect(
        "sim_click_at: frame=240 move 465,205 right=0" in craft_log,
        "make: ordinary Obelisk click was not injected",
    )
    expect(
        "sim_click_at: frame=340 move 257,170 right=0" in craft_log,
        "make: ordinary pouch-row click was not injected",
    )
    expect(
        "clickdbg: send op1 target=0x3ca0007 sub=-1 state=2" in craft_log,
        "make: pouch row did not send the real IF_BUTTON1 packet",
    )
    expect(
        "if-closesub: unmount uid=0x00a10010" in craft_log,
        "make: successful craft did not close its mounted panel",
    )
    expect(
        f"player_info: sequence idx=0 id={infuse_seq_id} delay=0" in craft_log,
        "make: player craft animation did not reach the client",
    )
    # The held infusion seq has to be cancelled when the action ends, and it
    # has to be cancelled after it started: a reset the client never receives
    # leaves the player looping the animation, and frozen where they stand.
    craft_anim_at = craft_log.find(f"player_info: sequence idx=0 id={infuse_seq_id} delay=0")
    reset_at = (
        craft_log.find("player_info: sequence idx=0 id=-1 delay=0", craft_anim_at)
        if craft_anim_at >= 0
        else -1
    )
    expect(reset_at > craft_anim_at, "make: craft animation was never cancelled")
    expect(
        f"loopback: seq={infuse_seq_id} " not in craft_log,
        "make: the infusion animation replayed its frame-step loop instead of ending",
    )
    charge_marker = f"seq_bind: element=19381 seq={charge_seq_id}"
    idle_marker = f"seq_bind: element=19381 seq={idle_seq_id}"
    charge_at = craft_log.find(charge_marker)
    idle_at = craft_log.find(idle_marker, charge_at + len(charge_marker)) if charge_at >= 0 else -1
    expect(charge_at >= 0, "make: active obelisk charge animation did not reach the client")
    expect(idle_at > charge_at, "make: Obelisk did not return to idle after its charge animation")
    expect(
        "message_game: You infuse a Spirit wolf pouch." in craft_log,
        "make: pouch-production server path did not complete",
    )
    expect(
        craft_bmp.is_file() and craft_bmp.stat().st_size > 54,
        "make: production framebuffer is absent",
    )

    stats, inventory = parse_save(save_text)
    expect(
        inventory.get(pouch_id, 0) == 1,
        f"save: expected one Spirit wolf pouch {pouch_id}, got {inventory.get(pouch_id, 0)}",
    )
    for label, item_id in (
        ("Gold charm", charm_id),
        ("blank pouch", blank_pouch_id),
        ("wolf bones", wolf_bones_id),
        ("Spirit shards", shard_id),
    ):
        expect(
            inventory.get(item_id, 0) == 0,
            f"save: {label} {item_id} was not consumed ({inventory.get(item_id, 0)} remain)",
        )
    expect(
        stats.get(24) == (1, 48),
        f"save: expected Summoning stat 24 = 1 48, got {stats.get(24)}",
    )

    return finish(errors, checked, args.out)


def render_panel(args: argparse.Namespace) -> tuple[int, str, Path]:
    bmp = (args.out / "render.bmp").resolve()
    with tempfile.TemporaryDirectory(prefix="summoning_phase4f_render_saves_") as saves:
        result = run_client(
            args,
            saves,
            {
                "TORIRS_MAX_FRAMES": "340",
                "TORIRS_NET_CHEAT": "summoning_unlock;summoning_infuse_supplies",
                "TORIRS_SIM_CLICK_AT": "240,465,205",
                "TORIRS_CLICK_DEBUG": "1",
                "TORIRS_NET_DEBUG": "1",
                "TORIRS_ANIM_DEBUG": "1",
                "TORIRS_PICK_DEBUG": "all",
                "TORIRS_WORLD_PICK_DEBUG": "1",
                "TORIRS_LOC_CFG": "62201",
                "TORIRS_EMIT_LOC": "62201",
                "TORIRS_DUMP_BOUNDS": "970",
                "TORIRS_DUMP_EMIT_EXIT": "970",
                "TORIRS_EXIT_BMP": str(bmp),
            },
        )
    return result.returncode, result.stdout, bmp


def make_pouch(args: argparse.Namespace) -> tuple[int, str, str, Path]:
    bmp = (args.out / "make.bmp").resolve()
    with tempfile.TemporaryDirectory(prefix="summoning_phase4f_make_saves_") as saves:
        save_path = Path(saves) / "guest.ini"
        result = run_client(
            args,
            saves,
            {
                "TORIRS_MAX_FRAMES": "560",
                "TORIRS_NET_CHEAT": "summoning_unlock;summoning_infuse_supplies",
                # The first click is the actual runtime loc. The second is the
                # mounted row's center, after IF_OPENSUB has completed.
                "TORIRS_SIM_CLICK_AT": "240,465,205;340,257,170",
                "TORIRS_CLICK_DEBUG": "1",
                "TORIRS_NET_DEBUG": "1",
                "TORIRS_ANIM_DEBUG": "1",
                "TORIRS_PICK_DEBUG": "all",
                "TORIRS_WORLD_PICK_DEBUG": "1",
                "TORIRS_LOC_CFG": "62201",
                "TORIRS_EMIT_LOC": "62201",
                "TORIRS_EXIT_BMP": str(bmp),
            },
        )
        save_text = read(save_path)
    return result.returncode, result.stdout, save_text, bmp


def run_client(
    args: argparse.Namespace, saves: str, extra: dict[str, str]
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env.update(
        {
            "TORIRSSERVER_SAVES": saves,
            "TORIRSSERVER_SCRIPTS": str(args.scripts.resolve()),
            "TORIRSSERVER_CACHE": str(args.cache.resolve()),
            "TORIRSSERVER_EXT_DEBUG": "1",
            "SDL_VIDEODRIVER": "dummy",
        }
    )
    env.update(extra)
    return subprocess.run(
        [
            str(args.client.resolve()),
            str(args.cache.resolve()),
            "--manifest",
            str(args.manifest.resolve()),
            "--soft3d",
        ],
        cwd=REPO,
        env=env,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=120,
        check=False,
    )


def allocated_id(alloc_path: Path, name: str) -> int | None:
    for line in read(alloc_path).splitlines():
        match = re.fullmatch(r"(\d+)=" + re.escape(name), line.strip())
        if match:
            return int(match.group(1))
    return None


def named_config_id(config_path: Path, name: str) -> int | None:
    for line in read(config_path).splitlines():
        match = re.fullmatch(r"(\d+)=" + re.escape(name), line.strip())
        if match:
            return int(match.group(1))
    return None


def parse_save(text: str) -> tuple[dict[int, tuple[int, int]], dict[int, int]]:
    section = ""
    stats: dict[int, tuple[int, int]] = {}
    inventory: dict[int, int] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        match = re.fullmatch(r"(\d+)\s*=\s*(\d+)\s+(\d+)", line)
        if not match:
            continue
        left, first, second = map(int, match.groups())
        if section == "stats":
            stats[left] = (first, second)
        elif section == "inv":
            inventory[first] = inventory.get(first, 0) + second
    return stats, inventory


def read(file_path: Path) -> str:
    try:
        return file_path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return ""


def finish(errors: list[str], checked: int, out: Path) -> int:
    for error in errors:
        print(f"test_summoning_phase4f: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase4f: {checked} checks, {len(errors)} errors ({out})")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
