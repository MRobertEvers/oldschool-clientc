#!/usr/bin/env python3
"""The thieving room's chest-position table must match the real cache map.

Why this exists
----------------
`~cox_thieving_chest_tile` (cox_puzzles.rs2) is a 204-entry hand-transcribed
table -- 64 CCW + 66 THRU + 74 CW room-local (x, z) pairs -- and its own
header comment claims it is "ported VERBATIM from
docs/minigames/cox/sources/de0/ChestData.java ... and confirmed to match
ChestData's own coordinate sets tile-for-tile" using
`tools/cox_template_survey.py --find raids_thievingchest_closed`. Nothing
enforced that claim as a gate: it was a one-time manual spot-check written
into a comment, not a repeatable check.

`~cox_selftest_thieving_room_populate` (cox_selftest.rs2) LOOKS like that
gate -- it builds a scratch instance and calls `loc_find` against every
table entry -- but it cannot actually prove anything: `loc_find` (2026-08-20
audit, torirs_server_scripts.c's own SS_OP_LOC_FIND comment) only sees a
static, never-`loc_add`'d loc when the tile is inside
`ToriRSServer_SceneContains`'s built-scene window. A freshly `map_instance_
alloc`'d raid instance with no player standing in it never is, so every
`loc_find` call in that proc returns false regardless of whether the table
is right -- it was masked the whole time behind an unrelated, earlier
selftest failure (#54, Stone Guardians) and never actually ran green. See
cox_selftest.rs2's own comment on ~cox_selftest_thieving_room_populate_body
for the live-engine side of this fix; this tool is the other side, checked
against ground truth the live engine cannot reach for static geometry.

This is therefore a STATIC check, the same shape as cox_check_timers.py and
cox_check_tightrope.py: read the real per-variant chest set out of the cache
maps (the same data tools/cox_template_survey.py --find already prints) and
diff it against the table in cox_puzzles.rs2, set-for-set, no live engine
involved.

Usage:
    tools/cox_check_thieving_chests.py              # check
    tools/cox_check_thieving_chests.py --selftest    # prove it can fail
"""

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from cox_template_survey import read_jl2, read_names, square_path  # noqa: E402

ROOT = HERE.parent
CONTENT = ROOT / "OSRS-Content/osrs239-content"
PUZZLES = (
    ROOT
    / "OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/scripts/cox_puzzles.rs2"
)

PITCH = 32
# (map square, cell x, cell y) each variant's chests physically occupy --
# cox_puzzles.rs2's own header comment on ~cox_thieving_chest_tile: "the
# room's own template geometry at m51_84 cell(0,0)/cell(1,0) and m52_84
# cell(0,0)".
VARIANT_CELLS = {
    "ccw": (51, 84, 0, 0),
    "thru": (51, 84, 1, 0),
    "cw": (52, 84, 0, 0),
}

PACK_LOCAL = re.compile(r"~cox_pack_local\((\d+),\s*(\d+)\)")


def real_chest_set(names: dict, map_x: int, map_z: int, cell_x: int, cell_z: int) -> set:
    path = square_path(str(CONTENT / "maps"), map_x, map_z)
    found = set()
    for _plane, lx, lz, loc_id in read_jl2(path):
        if names.get(loc_id, "") != "raids_thievingchest_closed":
            continue
        if lx // PITCH != cell_x or lz // PITCH != cell_z:
            continue
        found.add((lx % PITCH, lz % PITCH))
    return found


def table_chest_set(puzzles_text: str, variant_header: str, stop_marker: str) -> set:
    """The (x, z) pairs `~cox_pack_local` is called with inside one variant's
    branch of `[proc,cox_thieving_chest_tile]`, found by slicing the source
    text between two markers rather than evaluating the RuneScript -- the
    same "read the shape of the text" approach cox_check_tightrope.py uses
    for the same reason: there is no RuneScript interpreter here, only a
    parser for this one table's own if-chain shape.
    """
    start = puzzles_text.index(variant_header)
    end = puzzles_text.index(stop_marker, start)
    body = puzzles_text[start:end]
    return {(int(a), int(b)) for a, b in PACK_LOCAL.findall(body)}


def source_tables(puzzles_text: str) -> dict:
    proc_start = puzzles_text.index("[proc,cox_thieving_chest_tile]")
    proc_end = puzzles_text.index("[proc,cox_thieving_chest_count]")
    body = puzzles_text[proc_start:proc_end]
    ccw = table_chest_set(body, "cox_variant_ccw", "cox_variant_thru")
    thru = table_chest_set(body, "cox_variant_thru", "// $variant = ^cox_variant_cw")
    cw_start = body.index("// $variant = ^cox_variant_cw")
    cw = {(int(a), int(b)) for a, b in PACK_LOCAL.findall(body[cw_start:])}
    return {"ccw": ccw, "thru": thru, "cw": cw}


def check(puzzles_text: str) -> list:
    failures = []
    names = read_names(str(CONTENT / "configs/all.loc.compack"))
    tables = source_tables(puzzles_text)
    for variant, (map_x, map_z, cell_x, cell_z) in VARIANT_CELLS.items():
        real = real_chest_set(names, map_x, map_z, cell_x, cell_z)
        table = tables[variant]
        missing = real - table
        extra = table - real
        if missing:
            failures.append(
                f"{variant}: {len(missing)} real chest(s) at m{map_x}_{map_z} "
                f"cell {cell_x}/{cell_z} have no ~cox_pack_local entry: {sorted(missing)}"
            )
        if extra:
            failures.append(
                f"{variant}: {len(extra)} ~cox_pack_local entr(y/ies) do not match a "
                f"real chest at m{map_x}_{map_z} cell {cell_x}/{cell_z}: {sorted(extra)}"
            )
    return failures


def run() -> int:
    failures = check(PUZZLES.read_text(encoding="utf-8"))
    for f in failures:
        print(f)
    if failures:
        print(f"\nRESULT: {len(failures)} thieving chest table mismatch(es).")
        return 1
    print("RESULT: all 204 thieving chest table entries (64+66+74) match the cache map exactly.")
    return 0


def selftest() -> int:
    base = PUZZLES.read_text(encoding="utf-8")
    all_ok = True

    print("--- selftest: each mutation must turn this gate red ---")

    # 1. Drop one real CCW entry (19, 3) -> point it somewhere no chest is.
    mutated = base.replace(
        "if ($i = 0) { return(~cox_pack_local(19, 3)); }",
        "if ($i = 0) { return(~cox_pack_local(19, 4)); }",
        1,
    )
    if mutated == base:
        print("FAIL: mutation 'retarget one CCW entry' did not change the text")
        all_ok = False
    else:
        failures = check(mutated)
        if failures:
            print(f"OK: 'retarget one CCW entry' -> RED ({len(failures)} finding(s))")
        else:
            print("FAIL: 'retarget one CCW entry' -> stayed GREEN, this gate cannot catch it")
            all_ok = False

    # 2. Duplicate a THRU entry (drop coverage of one real chest, same count).
    mutated = base.replace(
        "if ($i = 1) { return(~cox_pack_local(9, 3)); }",
        "if ($i = 1) { return(~cox_pack_local(12, 2)); }",
        1,
    )
    if mutated == base:
        print("FAIL: mutation 'duplicate one THRU entry' did not change the text")
        all_ok = False
    else:
        failures = check(mutated)
        if failures:
            print(f"OK: 'duplicate one THRU entry' -> RED ({len(failures)} finding(s))")
        else:
            print("FAIL: 'duplicate one THRU entry' -> stayed GREEN, this gate cannot catch it")
            all_ok = False

    base_failures = check(base)
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
    return run()


if __name__ == "__main__":
    sys.exit(main())
