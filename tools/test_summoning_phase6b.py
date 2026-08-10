#!/usr/bin/env python3
"""Fresh-client acceptance for Spirit terrorbird Beast-of-Burden storage."""

from __future__ import annotations

import os
import re
import shutil
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
OUT = REPO / "build/summoning-phase6b"
HELM = 1155


def run(saves: Path, clicks: str, frames: int, cheat: str = "") -> str:
    env = os.environ.copy()
    for key in ("TORIRS_NET_CHEAT", "TORIRS_SIM_CLICK_AT", "TORIRS_MAX_FRAMES"):
        env.pop(key, None)
    env.update({
        "MOCK230_SAVES": str(saves), "MOCK230_SCRIPTS": str(SCRIPTS),
        "MOCK230_CACHE": str(CACHE), "SDL_VIDEODRIVER": "dummy",
        "TORIRS_SIM_CLICK_AT": clicks, "TORIRS_MAX_FRAMES": str(frames),
        "TORIRS_NET_DEBUG": "1", "TORIRS_CLICK_DEBUG": "1",
        "TORIRS_MINIMENU_DEBUG": "1",
    })
    if cheat:
        env["TORIRS_NET_CHEAT"] = cheat
    result = subprocess.run([str(CLIENT), str(CACHE), "--manifest", str(MANIFEST), "--soft3d"],
                            cwd=REPO, env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=180, check=False)
    if result.returncode:
        raise RuntimeError(f"client exited {result.returncode}\n{result.stdout[-4000:]}")
    return result.stdout


def held(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    section = re.search(r"(?ms)^\[container\.2001\]\s*$(.*?)(?=^\[|\Z)", text)
    return section is not None and re.search(rf"(?m)^\d+\s*=\s*{HELM}\s+1\s*$", section.group(1)) is not None


def main() -> int:
    errors: list[str] = []
    checks = 0
    def expect(ok: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not ok: errors.append(message)

    expect(CLIENT.is_file() and CACHE.is_dir() and SCRIPTS.is_dir(), "missing built client/cache/scripts")
    script = (CONTENT / "server/scripts/ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2").read_text()
    expect("^summoning_bob_slots = 12" in (CONTENT / "server/scripts/ported_scape2009_summoning/configs/summoning.constant").read_text(), "terrorbird 12-slot limit absent")
    expect("[opnpc3,summoning_cohort_spirit_terrorbird_spirit_terrorbird]" in script, "real Store menu binding absent")
    if errors:
        return finish(checks, errors)
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="summoning_phase6b_") as root:
        saves = Path(root) / "saves"; saves.mkdir()
        # Actual pouch summon, NPC Store menu, then Store a real backpack helm.
        first = run(saves, "220,540,230,1;270,540,260;420,450,250,1;435,450,283;475,200,205", 505,
                    "summoning_unlock;setlevel summoning 52;summoning_spirit_terrorbird_pouch")
        (OUT / "store.log").write_text(first)
        expect("You summon a Spirit terrorbird." in first, "real pouch did not summon terrorbird")
        expect("if-opensub: iface=971" in first, "real Store menu did not open BOB interface")
        expect("Store<col=ff9040> @lre@ Bronze full helm" in first, "real BOB Store row was not clicked")
        expect(held(saves / "guest.ini"), "stored helm was not persisted in summoning_bob")
        # Dismiss from a copy that still carries the helm.  This is deliberately
        # separate from the withdraw copy: the ground spill must be sent through
        # the rev-239 enclosed-zone OBJ_ADD route, not silently dropped.
        spill_saves = Path(root) / "spill_saves"
        shutil.copytree(saves, spill_saves)
        spill = run(spill_saves, "350,707,484;430,602,439", 470)
        (OUT / "dismiss-spill.log").write_text(spill)
        expect("You dismiss your familiar." in spill, "real sidebar Dismiss did not execute")
        expect("has no OBJ_ADD -- dropping it" not in spill, "dismiss spill OBJ_ADD was dropped")
        expect(not held(spill_saves / "guest.ini"), "Dismiss did not clear the BOB container")
        # No cheat: relog restores the familiar/container; open the same menu and withdraw.
        second = run(saves, "180,450,250,1;195,450,283;235,100,80", 280)
        (OUT / "relog-withdraw.log").write_text(second)
        expect("entity_sync: npc type replacement=26016" in second, "relog did not restore terrorbird")
        expect("Withdraw<col=ff9040> @lre@ Bronze full helm" in second, "relog BOB panel did not render stored helm")
        expect(not held(saves / "guest.ini"), "real Withdraw did not clear persisted BOB item")
    return finish(checks, errors)


def finish(checks: int, errors: list[str]) -> int:
    for error in errors: print(f"test_summoning_phase6b: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase6b: {checks} checks, {len(errors)} errors ({OUT})")
    return int(bool(errors))


if __name__ == "__main__": raise SystemExit(main())
