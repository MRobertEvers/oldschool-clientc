#!/usr/bin/env python3
"""Fresh-client acceptance for the bounded Clockwork-cat pet lifecycle."""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CONTENT = REPO / "OSRS-Content/osrs239-content"
CLIENT = REPO / "src/torirs"
CACHE = REPO / "cache.osrs239.summoning"
SCRIPTS = CONTENT / "server/scripts/build_summoning"
MANIFEST = REPO / "manifest_osrs239.ini"
OUT = REPO / "build/summoning-phase7d"
NPC, ITEM, BODY, HEAD, READY = 27300, 47300, 123000, 123001, 25300


def run(saves: Path, clicks: str, frames: int, cheat: str = "") -> str:
    env = os.environ.copy()
    for key in ("TORIRS_NET_CHEAT", "TORIRS_SIM_CLICK_AT", "TORIRS_MAX_FRAMES"):
        env.pop(key, None)
    env.update({
        "MOCK230_SAVES": str(saves), "MOCK230_SCRIPTS": str(SCRIPTS),
        "MOCK230_CACHE": str(CACHE), "SDL_VIDEODRIVER": "dummy",
        "TORIRS_SIM_CLICK_AT": clicks, "TORIRS_MAX_FRAMES": str(frames),
        "TORIRS_NET_DEBUG": "1", "TORIRS_CLICK_DEBUG": "1",
        "TORIRS_MINIMENU_DEBUG": "1", "TORIRS_ANIM_DEBUG": "1",
        "TORIRS_MODEL_FMT_DEBUG": "1",
    })
    if cheat:
        env["TORIRS_NET_CHEAT"] = cheat
    result = subprocess.run([str(CLIENT), str(CACHE), "--manifest", str(MANIFEST), "--soft3d"],
                            cwd=REPO, env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=180, check=False)
    if result.returncode:
        raise RuntimeError(f"client exited {result.returncode}\n{result.stdout[-4000:]}")
    return result.stdout


def save_varp(path: Path, varp: int) -> int | None:
    text = path.read_text(encoding="utf-8")
    match = re.search(rf"(?m)^\s*{varp}\s*=\s*(-?\d+)\s*$", text)
    return int(match.group(1)) if match else None


def main() -> int:
    errors: list[str] = []
    checks = 0

    def expect(ok: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not ok:
            errors.append(message)

    ledger = CONTENT / "port/summoning_clockwork_cat_530.map"
    script_root = CONTENT / "server/scripts/ported_scape2009_summoning/scripts"
    pet_script_path = script_root / "summoning_pet_clockwork_cat.rs2"
    core_script_path = script_root / "summoning_core.rs2"
    for path in (
        CLIENT,
        CACHE / "main_file_cache.dat2",
        SCRIPTS / "script.dat",
        ledger,
        pet_script_path,
        core_script_path,
    ):
        expect(path.is_file(), f"missing required input {path}")
    if errors:
        return finish(checks, errors)
    rows = ledger.read_text(encoding="utf-8")
    for row in (
        "npc\t3598\tpet_clockwork_cat\t27300\tsummoning_pet_clockwork_cat",
        "obj\t7771\tpet_clockwork_cat_item\t47300\tsummoning_pet_clockwork_cat_item",
        "model\t34024\tmodel_34024\t123000",
        "model\t34040\tmodel_34040\t123001",
        "model\t34083\tmodel_34083\t123002",
        "seq\t9158\tseq_9158\t25300",
        "seq\t9157\tseq_9157\t25301",
        "frame_archive\t2260\tanimset_2260\t25300",
        "framemap\t2035\tframemap_2035\t11300",
    ):
        expect(row in rows, f"ledger lacks {row}")
    script = "\n".join(
        path.read_text(encoding="utf-8") for path in (pet_script_path, core_script_path)
    )
    for token in (
        "[opheld5,summoning_pet_clockwork_cat_item]",
        "[opnpc1,summoning_pet_clockwork_cat]",
        "[opnpc5,summoning_pet_clockwork_cat]",
        "[proc,summoning_pet_clockwork_cat_release]",
        "[proc,summoning_pet_clockwork_cat_pickup]",
        "[proc,summoning_on_death]",
        "~summoning_pet_clockwork_cat_pickup(false);",
        "[proc,summoning_login]",
        "[proc,summoning_logout]",
    ):
        expect(token in script, f"pet lifecycle binding missing {token}")
    expect("summoning_familiar_ticks" not in script[script.index("[proc,summoning_pet_clockwork_cat_release]"):script.index("[proc,summoning_pet_clear_state]")], "pet release reused familiar timer state")
    if errors:
        return finish(checks, errors)

    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="summoning_phase7d_") as root:
        saves = Path(root) / "saves"; saves.mkdir()
        # Provision only. The right-click Release row is then selected through
        # the normal inventory menu and canonical OPHELD5 path.
        release = run(saves, "220,540,230,1;270,540,260", 380,
                      "summoning_unlock;summoning_clockwork_cat")
        (OUT / "release.log").write_text(release)
        expect('"Release @lre@ Clockwork cat"' in release, "real Release row was absent")
        expect("You release your Clockwork cat." in release, "real Release did not execute")
        expect(f"entity_sync: npc type replacement={NPC}" in release, "released pet did not reach the client")
        expect(f"model_fmt: id={BODY} " in release, "released pet body model was not loaded")
        expect(re.search(rf"seq_bind: element=\d+ seq={READY} .*kind=1", release) is not None, "released pet ready animation was not bound")
        guest = saves / "guest.ini"
        expect(save_varp(guest, 6259) == 1 and save_varp(guest, 6260) == 1, "pet active/type state did not persist after release")

        relog = run(saves, "", 260)
        (OUT / "relog.log").write_text(relog)
        expect(f"entity_sync: npc type replacement={NPC}" in relog, "relog did not reconstruct the persisted pet")
        expect("You release your Clockwork cat." not in relog, "relog reconstructed pet by replaying release")

        # ::die enters [playerdeath,_], so this is the ordinary death trigger,
        # not a direct pet-state shortcut. A Clockwork cat runs off instead of
        # returning an item, preserving its independent pet lifecycle.
        death = run(saves, "", 420, "summoning_clockwork_cat_die")
        (OUT / "death.log").write_text(death)
        expect("Oh dear, you are dead!" in death, "death trigger did not run")
        expect("No familiar" in death, "death did not synchronize the cleared pet state to the sidebar")
        expect(
            save_varp(guest, 6259) in (None, 0) and save_varp(guest, 6260) in (None, 0),
            "death did not clear persisted pet state",
        )
    return finish(checks, errors)


def finish(checks: int, errors: list[str]) -> int:
    for error in errors:
        print(f"test_summoning_phase7d: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase7d: {checks} checks, {len(errors)} errors ({OUT})")
    return int(bool(errors))


if __name__ == "__main__":
    raise SystemExit(main())
