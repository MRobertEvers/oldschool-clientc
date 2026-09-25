#!/usr/bin/env python3
"""Static checks for the Tightrope (DEATHLY_ROOM) puzzle that ::coxrun cannot
express, because they are about the SHAPE of the source text, not the value
an arithmetic proc returns.

Why this exists
----------------
Three things about this room can be silently wrong while every proc-level
selftest in cox_selftest.rs2 still returns the right numbers:

  1. The two [oploc1,...] trigger blocks (raids_tightrope_end,
     raids_tightrope_keystone_loc) can be missing entirely -- the underlying
     procs (~cox_tightrope_wake, ~cox_tightrope_take_keystone) would still
     compile and still pass their own selftest, because ::coxrun calls them
     directly. A player clicking the loc in a real raid would get nothing.
     This is exactly the "the code is right but nothing reaches it" trap
     cox_points.rs2 names for ~cox_award_damage.

  2. ~cox_tightrope_clear_guards can regress from `npc_findall(coord, type,
     distance, checkvis)` back to `npc_findallany(coord, distance, checkvis)`
     -- the pre-fix §11.2 bug this pass corrected. Both compile; the second
     silently drops the type filter.

  3. cox.npc's raids_tightrope_ranger/mage can pick up huntmode=aggressive
     (which would contradict the wiki's "aggressive = No") or lose
     retaliate=yes (which the deliberate design in cox.npc's own comment on
     this block explains is required, not optional, since ai_opplayer2 is the
     sole scripted swing and it is gated by the native attack_clock).

None of these are values a `~cox_selftest_*` proc call can observe -- they are
about which blocks exist and what fields a config record carries. Modelled on
tools/cox_check_timers.py, which is the same kind of gap for [ai_timer].

Usage:
    tools/cox_check_tightrope.py              # check
    tools/cox_check_tightrope.py --selftest    # prove each check can fail
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PKG = ROOT / "OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox"
PUZZLES = PKG / "scripts/cox_puzzles.rs2"
NPC_CONFIG = PKG / "configs/cox.npc"

TRIGGER = re.compile(r"^\[([a-z_0-9]+),([a-z_0-9]+)\]", re.M)
NPC_HEADER = re.compile(r"^\[([a-z_0-9]+)\]", re.M)


def proc_body(text: str, header: str) -> str:
    """Text of the first block whose header line is exactly `header`, up to
    (not including) the next `[...]` header. Empty string if not found."""
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if line.strip() == header:
            start = i + 1
            break
    if start is None:
        return ""
    body = []
    for line in lines[start:]:
        if line.startswith("["):
            break
        body.append(line)
    return "\n".join(body)


def npc_block(text: str, name: str) -> str:
    """Text of the [name] npc record: its field lines only, stopping at the
    first blank line. An .npc record's fields are always contiguous with no
    blank lines between them (every record in this file is separated from
    the next by a blank line, sometimes followed by a comment block
    documenting the NEXT record) -- stopping only at the next literal `[...]`
    header, as proc_body does for multi-line proc bodies, pulls that trailing
    comment prose into this record's text. A false positive bit exactly this
    way: raids_tightrope_mage's parsed "block" ran through the crab section's
    leading comment (which discusses `huntmode=aggressive` for a DIFFERENT
    npc) and the substring check flagged it against the wrong record."""
    lines = text.splitlines()
    start = None
    header = f"[{name}]"
    for i, line in enumerate(lines):
        if line.strip() == header:
            start = i + 1
            break
    if start is None:
        return ""
    body = []
    for line in lines[start:]:
        if line.startswith("[") or line.strip() == "":
            break
        body.append(line)
    return "\n".join(body)


def check_triggers(puzzles_text: str) -> list:
    failures = []
    headers = {m.group(2) for m in TRIGGER.finditer(puzzles_text) if m.group(1) == "oploc1"}
    for loc in ("raids_tightrope_end", "raids_tightrope_keystone_loc", "raids_tightrope_barrier"):
        if loc not in headers:
            failures.append(f"MISSING TRIGGER: no [oploc1,{loc}] block in {PUZZLES.name}")
    return failures


def check_clear_guards_type_filtered(puzzles_text: str) -> list:
    failures = []
    body = proc_body(puzzles_text, "[proc,cox_tightrope_clear_guards](coord $origin)")
    if not body:
        failures.append("MISSING PROC: [proc,cox_tightrope_clear_guards] not found")
        return failures
    if "npc_findallany(" in body:
        failures.append(
            "UNFILTERED FIND: ~cox_tightrope_clear_guards calls npc_findallany "
            "(no type argument) instead of npc_findall(coord, type, distance, 0)"
        )
    find_calls = re.findall(r"npc_findall\(([^)]*)\)", body)
    if len(find_calls) < 2:
        failures.append(
            f"EXPECTED 2 npc_findall CALLS (ranger + mage), found {len(find_calls)}"
        )
    for args in find_calls:
        parts = [p.strip() for p in args.split(",")]
        if len(parts) != 4:
            failures.append(f"npc_findall({args}) does not have 4 arguments (coord, type, distance, checkvis)")
            continue
        if parts[3] != "0":
            failures.append(f"npc_findall({args}) checkvis argument is '{parts[3]}', wanted literal 0")
    return failures


def check_npc_flags(npc_text: str) -> list:
    failures = []
    for name in ("raids_tightrope_ranger", "raids_tightrope_mage"):
        block = npc_block(npc_text, name)
        if not block:
            failures.append(f"MISSING RECORD: [{name}] not found in {NPC_CONFIG.name}")
            continue
        if "huntmode=aggressive" in block:
            failures.append(
                f"[{name}] has huntmode=aggressive -- contradicts the wiki's "
                "'aggressive = No'; the wake is scripted (~cox_tightrope_wake), "
                "not proximity-based"
            )
        if "retaliate=yes" not in block:
            failures.append(
                f"[{name}] is missing retaliate=yes -- required because its "
                "damage (ai_opplayer2) is gated by the native attack_clock, "
                "not a second independent timer; see the comment above this "
                "block in cox.npc"
            )
    return failures


def check(puzzles_text: str, npc_text: str) -> list:
    failures = []
    failures += check_triggers(puzzles_text)
    failures += check_clear_guards_type_filtered(puzzles_text)
    failures += check_npc_flags(npc_text)
    return failures


def run(puzzles_path: Path, npc_path: Path) -> int:
    failures = check(puzzles_path.read_text(encoding="utf-8"), npc_path.read_text(encoding="utf-8"))
    for f in failures:
        print(f)
    if failures:
        print(f"\nRESULT: {len(failures)} tightrope wiring problem(s).")
        return 1
    print("RESULT: tightrope triggers, findall filter and npc flags all check out.")
    return 0


def selftest() -> int:
    import shutil
    import tempfile

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        puzzles = tmp / PUZZLES.name
        npc = tmp / NPC_CONFIG.name
        base_puzzles = PUZZLES.read_text(encoding="utf-8")
        base_npc = NPC_CONFIG.read_text(encoding="utf-8")

        cases = []

        # 1. Comment out the Cross trigger header.
        cases.append((
            "drop [oploc1,raids_tightrope_end]",
            base_puzzles.replace("[oploc1,raids_tightrope_end]", "// [oploc1,raids_tightrope_end]"),
            base_npc,
        ))
        # 2. Comment out the Take trigger header.
        cases.append((
            "drop [oploc1,raids_tightrope_keystone_loc]",
            base_puzzles.replace(
                "[oploc1,raids_tightrope_keystone_loc]", "// [oploc1,raids_tightrope_keystone_loc]"
            ),
            base_npc,
        ))
        # 3. Revert the type-filter fix back to the pre-fix bug shape.
        cases.append((
            "revert clear_guards to npc_findallany",
            base_puzzles.replace(
                "npc_findall($origin, raids_tightrope_ranger, ^cox_tightrope_range, 0);",
                "npc_findallany($origin, raids_tightrope_ranger, ^cox_tightrope_range);",
            ).replace(
                "npc_findall($origin, raids_tightrope_mage, ^cox_tightrope_range, 0);",
                "npc_findallany($origin, raids_tightrope_mage, ^cox_tightrope_range);",
            ),
            base_npc,
        ))
        # 4. Drop the checkvis=0 argument (a narrower version of the same bug).
        cases.append((
            "drop clear_guards checkvis argument",
            base_puzzles.replace(
                "npc_findall($origin, raids_tightrope_ranger, ^cox_tightrope_range, 0);",
                "npc_findall($origin, raids_tightrope_ranger, ^cox_tightrope_range);",
            ),
            base_npc,
        ))
        # 5. Add huntmode=aggressive to the ranger.
        cases.append((
            "add huntmode=aggressive to raids_tightrope_ranger",
            base_puzzles,
            base_npc.replace(
                "[raids_tightrope_ranger]\nhitpoints=120",
                "[raids_tightrope_ranger]\nhuntmode=aggressive\nhitpoints=120",
            ),
        ))
        # 6. Drop retaliate=yes from the mage.
        mage_block = (
            "[raids_tightrope_mage]\n"
            "hitpoints=120\n"
            "attack=1\n"
            "strength=1\n"
            "defence=155\n"
            "magic=210\n"
            "ranged=155\n"
            "param=attackrate,4\n"
            "param=attackrange,10\n"
            "retaliate=yes\n"
            "givechase=no"
        )
        assert mage_block in base_npc, "cox.npc's raids_tightrope_mage block no longer matches this selftest's copy"
        cases.append((
            "drop retaliate=yes from raids_tightrope_mage",
            base_puzzles,
            base_npc.replace(mage_block, mage_block.replace("retaliate=yes\n", "")),
        ))

        print("--- selftest: each mutation must turn this gate red ---")
        all_ok = True
        for label, mutated_puzzles, mutated_npc in cases:
            if mutated_puzzles == base_puzzles and mutated_npc == base_npc:
                print(f"FAIL: mutation '{label}' did not change the text -- the case itself is broken")
                all_ok = False
                continue
            failures = check(mutated_puzzles, mutated_npc)
            if failures:
                print(f"OK: '{label}' -> RED ({len(failures)} finding(s))")
            else:
                print(f"FAIL: '{label}' -> stayed GREEN, this gate cannot catch it")
                all_ok = False

        # And the unmutated tree must be green, or the whole exercise is moot.
        base_failures = check(base_puzzles, base_npc)
        if base_failures:
            print("FAIL: the UNMUTATED tree is already red:")
            for f in base_failures:
                print(f"  {f}")
            all_ok = False
        else:
            print("OK: the unmutated tree is green")

        return 0 if all_ok else 1


def main() -> int:
    if "--selftest" in sys.argv:
        return selftest()
    return run(PUZZLES, NPC_CONFIG)


if __name__ == "__main__":
    sys.exit(main())
