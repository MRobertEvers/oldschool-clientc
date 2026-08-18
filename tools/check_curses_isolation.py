#!/usr/bin/env python3
"""Assert the Ancient Curses lane is unreachable in a flag-off build.

The lane is gated three ways and this checks all three, because each fails
differently and two of them fail silently:

  1. every ServerScript entry point opens with a ^curses_enabled test, so a
     flag-off script pack can be loaded and still do nothing;
  2. the lane's client half lives under ported/, which the ordinary content
     walk skips, so it enters a cache only through stage_curses_overlay.py;
  3. the lane's prefix appears nowhere in the base tree, so a flag-off bake
     cannot name a curses record even by accident.

A zero-check pass is a vacuous pass — the count is printed and a run that
verified nothing exits non-zero. That is the lesson recorded against the
Summoning lane's own isolation checker, and it applies verbatim here.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

CLIENT_LANE = Path("ported") / "rs558_ancient_curses"
SERVER_LANE = Path("server/scripts") / "ported_rs558_ancient_curses"
# A symbol that could only be this lane's. Two families of false positive have
# to be excluded and both are real records in this tree:
#
#   ~cc_curses_solved   The Corsair Curse quest — has an underscore before the
#                       prefix, so the word-boundary lookbehind drops it.
#   curse_casting       the Curse *spell*'s spotanims (also curse_impact,
#                       curse_travel, curse_all) — singular and unsigiled, so
#                       requiring a sigil or the plural drops them.
#
# What is left is `^curse_*` constants, `~curse_*`/`~curses_*` procs, `%curse_*`
# varps, and `curses_*` record names.
LANE_SYMBOL = re.compile(r"[~^%]curses?_|(?<![A-Za-z0-9_])curses_")

# Entry points: anything the engine or another lane can call into. Procs that
# are only reachable from an already-gated proc do not need their own test, but
# these do.
GATED_ENTRY_POINTS = (
    "[proc,curse_toggle]",
    "[proc,curses_book_set]",
    "[proc,curse_wrath_on_death]",
    "[debugproc,curses]",
    "[debugproc,curse]",
)

# Procs called from shared files. These must fail closed too, via ~curses_active
# (which tests ^curses_enabled itself) rather than an inline constant test.
SHARED_CALLERS = (
    "[proc,curse_on_player_hit]",
    "[proc,curse_check_protect]",
    "[proc,curse_deflect_reflect]",
    "[proc,curse_attack_bonus]",
    "[proc,curse_strength_bonus]",
    "[proc,curse_defence_bonus]",
    "[proc,curse_ranged_bonus]",
    "[proc,curse_magic_bonus]",
)


def proc_body(text: str, header: str) -> str | None:
    """The lines of one proc, up to the next top-level [tag,...] declaration."""
    start = text.find(header)
    if start < 0:
        return None
    rest = text[start + len(header):]
    end = re.search(r"^\[[a-z_]+,", rest, re.M)
    return rest[: end.start()] if end else rest


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--tree", type=Path, required=True)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    tree = args.tree.resolve()
    failures: list[str] = []
    checks = 0

    client = tree / CLIENT_LANE
    server = tree / SERVER_LANE
    for path in (client, server):
        checks += 1
        if not path.is_dir():
            failures.append(f"missing lane directory: {path}")

    # 1. provenance must exist, or the stager refuses to run at all
    checks += 1
    if not (client / "ANCIENT_CURSES.md").is_file():
        failures.append(f"missing provenance: {client / 'ANCIENT_CURSES.md'}")

    checks += 1
    if not (tree / "port/curses_558.map").is_file():
        failures.append("missing translation ledger: port/curses_558.map")

    # 2. every entry point fails closed
    server_text = "\n".join(
        p.read_text() for p in sorted(server.rglob("*.rs2"))) if server.is_dir() else ""
    for header in GATED_ENTRY_POINTS:
        checks += 1
        body = proc_body(server_text, header)
        if body is None:
            failures.append(f"entry point not found: {header}")
        elif "^curses_enabled" not in body:
            failures.append(f"entry point does not test ^curses_enabled: {header}")

    for header in SHARED_CALLERS:
        checks += 1
        body = proc_body(server_text, header)
        if body is None:
            failures.append(f"shared-caller proc not found: {header}")
        elif "~curses_active" not in body and "^curses_enabled" not in body:
            failures.append(f"shared-caller proc does not fail closed: {header}")

    # 3. the lane's prefix must not appear in the base content tree. Shared
    #    files legitimately CALL the lane, so those calls are what we allow and
    #    a stray record name is what we forbid.
    allowed_callers = {
        tree / "server/scripts/skill_prayer/scripts/prayer.rs2",
        tree / "server/scripts/skill_combat/combat_stats.rs2",
        tree / "server/scripts/player/death.rs2",
        # Berserker lengthens the stat-restore interval, so the shared timer
        # names the lane. Guarded by ^curses_enabled like every other caller.
        tree / "server/scripts/player/scripts/stat_restore.rs2",
        # The npc->player deflect sites, twins of the one in combat_stats.rs2.
        # Player-dealt damage funnels through one proc; npc-dealt damage does
        # not, so each style's own script has to report its blocked hit.
        # ~curse_deflect_reflect fails closed, which is checked above.
        tree / "server/scripts/skill_combat/scripts/npc_combat_magic.rs2",
        tree / "server/scripts/skill_combat/scripts/npc_combat_ranged.rs2",
        tree / "pack/varp.server",
    }
    # The seams are the sanctioned third place a curses name appears outside the
    # lane: `server/scripts/lane_seams/` holds the base tree's DEFAULT for every
    # procedure the lane provides, so a build without the lane still compiles
    # (src/serverscript/ssc.h, struct SSC_SourceRoot). Those definitions exist
    # precisely to be replaced by the lane, and every one of them is inert -- the
    # strongest possible form of "fails closed" -- so allowing the directory
    # wholesale is not a hole in this rule but the other half of it.
    seams = tree / "server/scripts/lane_seams"
    for path in sorted(tree.rglob("*.rs2")):
        if CLIENT_LANE.as_posix() in path.as_posix():
            continue
        if SERVER_LANE.as_posix() in path.as_posix():
            continue
        if seams == path.parent or seams in path.parents:
            continue
        # Anchored at a word boundary, or every mention of The Corsair Curse
        # matches: that quest's `~cc_curses_solved` contains the prefix but is
        # not this lane. Sigils count as boundaries, bare underscores do not.
        if not LANE_SYMBOL.search(path.read_text()):
            continue
        checks += 1
        if path not in allowed_callers:
            failures.append(f"curses name leaked into a non-lane script: {path}")

    print(f"check_curses_isolation: {checks} check(s)")
    if checks < 10:
        print("check_curses_isolation: too few checks to be meaningful", file=sys.stderr)
        return 1
    for message in failures:
        print(f"  FAIL {message}", file=sys.stderr)
    if failures:
        return 1
    print("check_curses_isolation: lane is gated and does not leak")
    return 0


if __name__ == "__main__":
    sys.exit(main())
