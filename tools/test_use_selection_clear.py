#!/usr/bin/env python3
"""An armed "Use"/cast selection must be dropped by a click anywhere else.

Arming "Use <item>" (or a targeted spell) puts the client in a mode where the
next click acts on the selection.  Every way of ending that mode has to actually
end it, or the client is stuck: while armed, "Walk here" is suppressed and the
world rows are replaced by use/cast rows, so a selection that never retires
makes the whole game read as unclickable.

The two clicks driven here are the ones that reach no doAction tail on their
own:

  * the minimap, a builtin widget with no component id, which travels as a
    chrome gesture and bypasses both the component and world click paths;
  * bare ground while armed, where "Walk here" is suppressed and the scratch
    menu comes back Cancel-only, so no row runs.

Both are asserted through the TORIRS_CLICK_DEBUG state trace rather than pixels:
the armed highlight is painted by cache scripts (IF_SETONTARGETENTER), so a
screenshot cannot tell "still armed" from "armed and not drawn", while
`selarm:`/`selclear:` report the client state itself.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]

# Sidebar/inventory hit points in the fixed 765x503 layout.
TAB_INVENTORY = (645, 185)
SLOT0 = (578, 228)
USE_ROW = (540, 244)  # first row of the right-click menu opened on SLOT0
MINIMAP = (700, 80)
BARE_GROUND = (250, 300)

# Boot time varies a lot run to run (cache warmth), and a click released before
# login lands on a half-built gameframe and silently does nothing.  Everything
# is pushed well past the slowest observed login rather than tuned to the
# fastest: an under-budgeted run reports "never armed", not a pass.
CLICKS = [
    (700, *TAB_INVENTORY, 0),
    (760, *SLOT0, 1),
    (775, *USE_ROW, 0),
    (820, *MINIMAP, 0),
    (860, *SLOT0, 1),
    (875, *USE_ROW, 0),
    (920, *BARE_GROUND, 0),
]
MAX_FRAMES = 1500


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=REPO / "src/torirs")
    parser.add_argument("--cache", type=Path, default=REPO / "cache.osrs239.summoning")
    parser.add_argument(
        "--scripts",
        type=Path,
        default=REPO / "OSRS-Content/osrs239-content/server/scripts/build_summoning",
    )
    parser.add_argument("--manifest", type=Path, default=REPO / "manifest_osrs239.ini")
    parser.add_argument("--out", type=Path, default=REPO / "build/use-selection-clear")
    args = parser.parse_args()

    required = (
        ("client", args.client),
        ("cache", args.cache / "main_file_cache.dat2"),
        ("server scripts", args.scripts / "script.dat"),
        ("manifest", args.manifest),
    )
    missing = [f"missing {label}: {path}" for label, path in required if not path.exists()]
    if missing:
        for line in missing:
            print(f"FAIL {line}")
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    click_spec = ";".join(f"{f},{x},{y},{r}" for f, x, y, r in CLICKS)

    with tempfile.TemporaryDirectory(prefix="use_selection_saves_") as saves:
        env = os.environ.copy()
        env.update(
            {
                "TORIRSSERVER_SAVES": saves,
                "TORIRSSERVER_SCRIPTS": str(args.scripts.resolve()),
                "TORIRSSERVER_CACHE": str(args.cache.resolve()),
                "SDL_VIDEODRIVER": "dummy",
                "SDL_AUDIODRIVER": "dummy",
                "TORIRS_MAX_FRAMES": str(MAX_FRAMES),
                "TORIRS_NET_CHEAT": "give bronze_sword;give shark",
                "TORIRS_CLICK_DEBUG": "1",
                "TORIRS_SIM_CLICK_AT": click_spec,
            }
        )
        result = subprocess.run(
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
            timeout=600,
            check=False,
        )

    log_path = args.out / "use-selection-clear.log"
    log_path.write_text(result.stdout, encoding="utf-8")

    # The trace is a state machine, so order matters: each arm must be followed
    # by a clear before the next arm. A run that armed twice and cleared twice
    # but in the wrong order is not a pass.
    events = [
        ("arm" if line.startswith("selarm:") else "clear", line.strip())
        for line in result.stdout.splitlines()
        if line.startswith(("selarm:", "selclear:"))
    ]
    kinds = [kind for kind, _ in events]

    failures = []
    if not kinds:
        # Never armed means the clicks landed on a login screen, not on the
        # inventory. By far the most common cause is a client built without the
        # server linked in, which boots fine and simply never logs in — named
        # explicitly because the symptom otherwise reads as a clearing bug.
        # Reported as a failure, never skipped: a skip would read as a pass.
        why = (
            "client was built without the embedded server "
            "(rebuild with `make -C src EMBED_SERVER=1`)"
            if "no embedded server" in result.stdout
            else "client never finished logging in; see the log"
        )
        failures.append(f"the Use selection was never armed — {why}")
    elif kinds != ["arm", "clear", "arm", "clear"]:
        failures.append(f"expected arm/clear/arm/clear, got {kinds}")

    print(f"log: {log_path}")
    for kind, line in events:
        print(f"  {line}")
    if failures:
        for line in failures:
            print(f"FAIL {line}")
        return 1
    print("PASS minimap and bare-ground clicks both retired the armed selection")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
