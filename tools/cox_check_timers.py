#!/usr/bin/env python3
"""Every [ai_timer,<npc>] in a script package must have an [ai_spawn,<npc>].

Why this exists
---------------
`npc_add` dispatches `[ai_spawn]` but does not arm `[ai_timer]`: the engine
gates that trigger on `timer_interval > 0` (torirs_server_world.c, phase 4) and the
field starts at zero. An `[ai_timer]` block on an npc that never calls
`npc_settimer` is therefore dead code -- no error, no warning, no partial
behaviour, and it reads as a finished boss.

Chambers of Xeric shipped 21 `[ai_timer]` hooks and zero `npc_settimer` calls.
Olm's action clock never advanced, Tekton never woke from his waiting form,
Vasa's crystal window never counted down, the Vanguards never shuffled. The
package compiled clean and its 495-line selftest passed, because that selftest
checks procs -- arithmetic -- and a proc nothing calls still returns the right
answer. See docs/minigames/cox/COX_PLAN.md S11.3 F0.

This is a static check rather than a runtime one because no script opcode can
read an npc's timer interval back: `npc_settimer` sets it and nothing gets it.
The pairing of the two triggers is the only observable the language offers.

Usage:
    tools/cox_check_timers.py [package-dir ...]     # default: minigame_cox
    tools/cox_check_timers.py --selftest            # prove the gate can fail
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONTENT = ROOT / "OSRS-Content/osrs239-content/server/scripts"
DEFAULT = [CONTENT / "minigames/minigame_cox"]

TRIGGER = re.compile(r"^\[(ai_timer|ai_spawn),([a-z_0-9]+)\]", re.M)


def scan(pkg: Path):
    """Return (timers, spawns, settimer_bodies) keyed by npc name."""
    timers, spawns, armed = {}, {}, set()
    for path in sorted(pkg.rglob("*.rs2")):
        text = path.read_text(encoding="utf-8", errors="replace")
        # CONSECUTIVE HEADERS SHARE ONE BODY. This is the whole subtlety of the
        # file format and the first version of this scanner got it wrong: it
        # split on every line starting with `[`, so in
        #
        #     [ai_spawn,a]
        #     [ai_spawn,b]
        #     npc_settimer(1);
        #
        # only `b` saw the body and `a` was reported dead. That produced 18
        # false positives on a package that was fully armed -- a gate that cries
        # wolf gets switched off, which is worse than no gate.
        headers, body = [], []
        groups = []
        for line in text.splitlines():
            match = TRIGGER.match(line)
            if match:
                if body:                       # a new stack begins
                    groups.append((headers, "\n".join(body)))
                    headers, body = [], []
                headers.append(match.groups())
            elif headers:
                body.append(line)
        if headers:
            groups.append((headers, "\n".join(body)))

        for names, body_text in groups:
            for kind, npc in names:
                (timers if kind == "ai_timer" else spawns).setdefault(npc, []).append(path.name)
                if kind == "ai_spawn" and "npc_settimer" in body_text:
                    armed.add(npc)
    return timers, spawns, armed


def check(pkgs) -> int:
    failures = 0
    for pkg in pkgs:
        timers, spawns, armed = scan(pkg)
        for npc in sorted(timers):
            if npc in armed:
                continue
            where = ", ".join(sorted(set(timers[npc])))
            reason = "no [ai_spawn] at all" if npc not in spawns else "[ai_spawn] never calls npc_settimer"
            print(f"DEAD TIMER: [ai_timer,{npc}] in {where} -- {reason}")
            failures += 1
        # The OTHER direction (P1 audit, 2026-08-20 pass): an [ai_spawn,<npc>]
        # that calls npc_settimer but has no matching [ai_timer,<npc>] block
        # anywhere. This is exactly the ice demon's own bug before this pass --
        # `[ai_spawn,raids_icedemon_noncombat] npc_settimer(1);` armed a clock
        # nothing was listening to, and the stage machine could never run
        # through normal play. The first check above cannot see this shape: it
        # only walks npcs that HAVE an [ai_timer] block, so a missing one is
        # invisible to it by construction.
        for npc in sorted(armed):
            if npc in timers:
                continue
            where = ", ".join(sorted(set(spawns[npc])))
            print(f"ARMED BUT UNHEARD: [ai_spawn,{npc}] calls npc_settimer in {where}, but no [ai_timer,{npc}] exists anywhere")
            failures += 1
        print(f"{pkg.name}: {len(timers)} ai_timer npc(s), {len(armed)} armed")
    return failures


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    pkgs = [Path(a) for a in args] if args else DEFAULT

    if "--selftest" in sys.argv:
        # A gate that cannot fail is not a gate. Copy the package, strip its
        # npc_settimer calls, and confirm the check goes red.
        import shutil
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            broken = Path(tmp) / pkgs[0].name
            shutil.copytree(pkgs[0], broken)
            for path in broken.rglob("*.rs2"):
                text = path.read_text(encoding="utf-8", errors="replace")
                path.write_text(text.replace("npc_settimer", "npc_disarmed"), encoding="utf-8")
            print("--- selftest: with every npc_settimer removed ---")
            if check([broken]) == 0:
                print("FAIL: a package with no npc_settimer passed - this gate proves nothing")
                return 1
            print("OK: the gate reports dead timers, so a clean run means something\n")

        # The other direction's own gate: delete one [ai_timer,<npc>] header
        # (renaming it so its body becomes unreachable dead text) while
        # leaving the matching [ai_spawn,<npc>] npc_settimer call intact, and
        # confirm the ARMED BUT UNHEARD check catches it. Mirrors the ice
        # demon's real P1 bug: delete or rename
        # [ai_timer,raids_icedemon_noncombat] and rerun this tool.
        with tempfile.TemporaryDirectory() as tmp:
            broken = Path(tmp) / pkgs[0].name
            shutil.copytree(pkgs[0], broken)
            target = broken / "scripts" / "cox_icedemon.rs2"
            text = target.read_text(encoding="utf-8", errors="replace")
            mutated = text.replace(
                "[ai_timer,raids_icedemon_noncombat]",
                "[ai_timer,raids_icedemon_noncombat_renamed]",
                1,
            )
            if mutated == text:
                print("FAIL: could not find [ai_timer,raids_icedemon_noncombat] to mutate")
                return 1
            target.write_text(mutated, encoding="utf-8")
            print("--- selftest: with [ai_timer,raids_icedemon_noncombat] renamed away ---")
            if check([broken]) == 0:
                print("FAIL: an armed-but-unheard timer passed - this gate proves nothing")
                return 1
            print("OK: the gate reports the armed-but-unheard timer, so a clean run means something\n")

    failures = check(pkgs)
    if failures:
        print(f"\nRESULT: {failures} dead timer(s). Add [ai_spawn,<npc>] npc_settimer(<ticks>).")
        return 1
    print("\nRESULT: every ai_timer is armed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
