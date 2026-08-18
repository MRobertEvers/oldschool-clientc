#!/usr/bin/env python3
"""Static contract audit for the functional POH League Hall."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "OSRS-Content" / "osrs239-content"
SCRIPT = CONTENT / "server/scripts/skill_construction/scripts/poh_league_hall.rs2"
BUILD = CONTENT / "server/scripts/skill_construction/scripts/poh_build.rs2"
DISPATCH = CONTENT / "server/scripts/skill_construction/scripts/poh_chapel_functions.rs2"
RUNTIME = CONTENT / "server/scripts/skill_construction/configs/poh_runtime_generated.dbrow"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"poh league hall contract: FAIL: {message}")


def block(text: str, start: str, end: str) -> str:
    match = re.search(
        rf"^\[{re.escape(start)}\]\n(.*?)(?=^\[{re.escape(end)}\]\n)",
        text,
        re.MULTILINE | re.DOTALL,
    )
    require(match is not None, f"missing runtime block {start}")
    return match.group(1)


def main() -> None:
    script = SCRIPT.read_text(encoding="utf-8")
    build = BUILD.read_text(encoding="utf-8")
    dispatch = DISPATCH.read_text(encoding="utf-8")
    runtime = RUNTIME.read_text(encoding="utf-8")

    trophy_block = re.search(
        r"\[proc,poh_league_trophy_obj\].*?\nreturn\(null\);",
        script,
        re.DOTALL,
    )
    require(trophy_block is not None, "missing trophy object map")
    trophy_cases = re.findall(r"case (\d+) : return\(([^)]+_trophy)\);", trophy_block.group())
    require([int(index) for index, _ in trophy_cases] == list(range(1, 43)),
            "trophy map must cover semantic variants 1..42 exactly")
    require(len({obj for _, obj in trophy_cases}) == 42,
            "all 42 League trophies must be unique")

    banner_cases = re.findall(
        r"case [1-6] : return\(([^)]+_banner)\);",
        re.search(r"\[proc,poh_league_banner_obj\].*?return\(null\);", script, re.DOTALL).group(),
    )
    require(len(banner_cases) == 6 and len(set(banner_cases)) == 6,
            "all six League banners must be mapped")

    for helper in ("head", "top", "legs", "boots"):
        helper_block = re.search(
            rf"\[proc,poh_league_outfit_{helper}\].*?return\(null\);",
            script,
            re.DOTALL,
        )
        require(helper_block is not None, f"missing outfit {helper} map")
        variants = [int(value) for value in re.findall(r"case (\d+) : return", helper_block.group())]
        require(variants == list(range(1, 19)),
                f"outfit {helper} map must cover variants 1..18 exactly")
    special_block = re.search(
        r"\[proc,poh_league_outfit_special\].*?return\(null\);",
        script,
        re.DOTALL,
    )
    require(special_block is not None and
            [int(value) for value in re.findall(r"case (\d+) : return", special_block.group())]
            == [3, 6, 9, 12, 15, 18],
            "every tier-three outfit must require its matching special item")

    thresholds = {
        1: (10, 80, 510, 1770, 5270, 10820, 20830),
        2: (100, 510, 2000, 6480, 18720, 35680, 56310),
        3: (100, 480, 1660, 5475, 15575, 31980, 52545),
        4: (2500, 5000, 10000, 18000, 28000, 42000, 56000),
        5: (2000, 4000, 10000, 20000, 30000, 45000, 60000),
        6: (2000, 4000, 10000, 22000, 32000, 47500, 65000),
    }
    for league, values in thresholds.items():
        marker = "if" if league == 1 else "else if"
        segment = re.search(
            rf"{marker} \(\$league = {league}\) \{{(.*?)\n\}}",
            script,
            re.DOTALL,
        )
        require(segment is not None, f"missing League {league} trophy thresholds")
        actual = tuple(int(value) for value in re.findall(r"\$points >= (\d+)", segment.group(1)))
        require(actual == values, f"League {league} trophy thresholds drifted: {actual}")

    runtime_expectations = (
        ("poh_runtime_furniture_poh_leaguehall_trophy_pedestal_simple",
         "poh_runtime_furniture_poh_leaguehall_trophy_pedestal_decorative", 129),
        ("poh_runtime_furniture_poh_leaguehall_trophy_pedestal_decorative",
         "poh_runtime_furniture_poh_leaguehall_rug_simple", 129),
        ("poh_runtime_furniture_poh_leaguehall_outfitstand_oak",
         "poh_runtime_furniture_poh_leaguehall_outfitstand_mahogany", 19),
        ("poh_runtime_furniture_poh_leaguehall_outfitstand_mahogany",
         "poh_runtime_furniture_poh_leaguehall_statue_simple", 19),
        ("poh_runtime_furniture_poh_leaguehall_bannerstand_simple",
         "poh_runtime_furniture_poh_leaguehall_bannerstand_decorative", 7),
        ("poh_runtime_furniture_poh_leaguehall_bannerstand_decorative",
         "poh_runtime_furniture_poh_leaguehall_outfitstand_oak", 7),
    )
    for start, end, expected in runtime_expectations:
        actual = block(runtime, start, end).count("data=built_loc,")
        require(actual == expected, f"{start} has {actual} locs, expected {expected}")

    for token in (
        "poh_state_commit",
        "poh_league_trophy_is_mounted",
        "~obj_gettotal($trophy)",
        "poh_leaguehall_use_on_furniture",
        "poh_leaguehall_op1",
        "Historical top-100 rankings and League Firsts require the global League archive",
    ):
        require(token in script, f"missing functional contract token {token!r}")
    require("~poh_leaguehall_op1 = true" in dispatch and
            "~poh_leaguehall_use_on_furniture = true" in dispatch,
            "category-wide built-furniture dispatch is not wired")
    require("~poh_leaguehall_take_at($local_x, $local_z) = true" in build,
            "take/remove dispatch is not wired")
    require(build.count("Remove the displayed trophy before removing the pedestal.") == 3,
            "all three occupied pedestals must block furniture removal")
    require("Remove the displayed banner before removing the stand." in build and
            "Remove the displayed outfit before removing the stand." in build,
            "occupied banner/outfit stands must block furniture removal")

    wiki_links = set(re.findall(r"https://oldschool\.runescape\.wiki/w/[^\s]+", script))
    require(len(wiki_links) >= 12, "League Hall implementation needs extensive Wiki provenance")

    print(
        "poh league hall contract: 42 trophies, 6 banners, 18 outfits, "
        "6 point histories, and 3 durable pedestals"
    )


if __name__ == "__main__":
    main()
