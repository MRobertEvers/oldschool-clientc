#!/usr/bin/env python3
"""Static contract audit for revision-239 POH house styles."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "OSRS-Content" / "osrs239-content"
SCRIPTS = CONTENT / "server/scripts/skill_construction/scripts"
CONFIGS = CONTENT / "server/scripts/skill_construction/configs"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"poh house-style contract: FAIL: {message}")


def proc(text: str, name: str) -> str:
    match = re.search(
        rf"^\[proc,{re.escape(name)}\].*?(?=^\[)",
        text,
        re.MULTILINE | re.DOTALL,
    )
    require(match is not None, f"missing proc {name}")
    return match.group()


def main() -> None:
    constants = (CONFIGS / "construction.constant").read_text(encoding="utf-8")
    construct = (SCRIPTS / "poh_construct.rs2").read_text(encoding="utf-8")
    estate = (SCRIPTS / "poh_estate_agent.rs2").read_text(encoding="utf-8")
    rooms = (SCRIPTS / "poh_rooms.rs2").read_text(encoding="utf-8")
    build = (SCRIPTS / "poh_build.rs2").read_text(encoding="utf-8")
    runtime = (CONFIGS / "poh_runtime_generated.dbrow").read_text(encoding="utf-8")
    header = (ROOT / "src/torirsserver/torirs_server_poh.h").read_text(encoding="utf-8")
    engine = (ROOT / "src/torirsserver/torirs_server_poh.c").read_text(encoding="utf-8")
    save = (ROOT / "src/torirsserver/torirs_server_save.c").read_text(encoding="utf-8")

    styles = (
        ("basic_wood", 0, "0_29_110_0_0", 1, 5000),
        ("basic_stone", 1, "1_29_110_0_0", 10, 5000),
        ("whitewashed", 2, "2_29_110_0_0", 20, 7500),
        ("fremennik", 3, "3_29_110_0_0", 30, 10000),
        ("tropical", 4, "0_30_110_0_0", 40, 15000),
        ("fancy", 5, "1_30_110_0_0", 50, 25000),
        ("deathly", 6, "2_30_110_0_0", 25, 35000),
        ("twisted", 7, "3_30_110_0_0", 1, 0),
        ("hosidius", 8, "0_31_110_0_0", 1, 5000),
        ("cosy", 9, "1_31_110_0_0", 1, 35000),
        ("civitas", 10, "2_31_110_0_0", 70, 35000),
        ("canifis", 11, "3_31_110_0_0", 60, 10000),
    )
    for name, ordinal, origin, _, _ in styles:
        require(f"^poh_style_{name} = {ordinal}" in constants,
                f"style ordinal drifted for {name}")
        if name == "basic_wood":
            require(f"^poh_style_origin = {origin}" in constants,
                    "Basic Wood template origin drifted")
        else:
            require(
                re.search(
                    rf"%poh_house_style = \^poh_style_{name}\) return\({origin}\);",
                    construct,
                ) is not None,
                f"template origin drifted for {name}",
            )
    require("^poh_style_count = 12" in constants, "style count must be twelve")

    level_proc = proc(estate, "poh_style_level")
    cost_proc = proc(estate, "poh_style_cost")
    for name, _, _, level, cost in styles:
        if name == "basic_wood":
            require("return(1);" in level_proc, "default style level must be 1")
        elif level != 1:
            require(f"^poh_style_{name}) return({level});" in level_proc,
                    f"level drifted for {name}")
        if name == "basic_wood":
            require("return(5000);" in cost_proc, "default style cost must be 5,000")
        elif name == "twisted":
            require("^poh_style_twisted) return(0);" in cost_proc,
                    "Twisted theme must have no coin fee")
        elif cost not in (5000,):
            require(str(cost) in cost_proc and f"^poh_style_{name}" in cost_proc,
                    f"cost drifted for {name}")

    for name, *_ in styles:
        suffix = {
            "basic_wood": "rimmington",
            "basic_stone": "lumbridge",
            "whitewashed": "pollnivneach",
            "fremennik": "rellekka",
            "tropical": "brimhaven",
            "fancy": "yanille",
            "cosy": "xmas2020",
        }.get(name, name)
        for side in ("doorl", "doorr"):
            require(f"[oploc5,poh_hotspot_{side}_{suffix}]" in rooms,
                    f"missing {name} {side} Build trigger")
        require(f"[oploc5,poh_chapelwindow_hotspot_{suffix}]" in build,
                f"missing {name} Chapel-window Build trigger")
        require(f"poh_chapelwindow_hotspot_{suffix}" in construct,
                f"missing {name} Chapel-window scene projection")

    require(build.count("data=built_loc") == 0,
            "runtime data must remain generated, not embedded in scripts")
    shutter_block = re.search(
        r"\[poh_runtime_furniture_poh_chapel_window_shutters\].*?"
        r"(?=\[poh_runtime_furniture_poh_chapel_window_decorative\])",
        runtime,
        re.DOTALL,
    )
    decorative_block = re.search(
        r"\[poh_runtime_furniture_poh_chapel_window_decorative\].*?"
        r"(?=\[poh_runtime_furniture_poh_chapel_window_stainedglass\])",
        runtime,
        re.DOTALL,
    )
    require(shutter_block is not None and
            shutter_block.group().count("data=built_loc,") == 8,
            "shutter cache limitation must remain exactly eight styles")
    require(decorative_block is not None and
            decorative_block.group().count("data=built_loc,") == 72,
            "decorative windows must cover 12 styles by 6 alignments")
    window_map = proc(build, "poh_chapel_window_style_variant")
    for base in (4, 8, 12, 16, 20, 24, 36, 50, 55):
        require(f"return(calc({base} + $semantic));" in window_map,
                f"missing Chapel-window contiguous base {base}")
    for index in (28, 31, 34, 35, 47, 68, 29, 30, 32, 33, 48, 63,
                  40, 60, 41, 64, 42, 65, 43, 66, 44, 67, 45, 70,
                  46, 71, 49, 69, 54, 62, 59, 61):
        require(f"return({index});" in window_map,
                f"missing irregular Chapel-window runtime index {index}")

    for token in (
        "twisted_blueprints",
        "hosidius_blueprints",
        "^poh_style_unlock_deathly",
        "^poh_style_unlock_hosidius",
        "^poh_style_unlock_cosy",
        "[proc,poh_style_unlock_holiday]",
        "stat_base(construction)",
        "poh_state_commit",
        "inv_add(inv, twisted_blueprints, 1)",
    ):
        require(token in estate, f"missing redecoration transaction token {token!r}")

    require("TORIRSSERVER_POH_SCHEMA_VERSION = 8" in header,
            "POH schema must include style entitlements")
    require("TORIRSSERVER_POH_STYLE_MAX = 11" in header,
            "engine style bound must match the twelve cache planes")
    require("TORIRSSERVER_POH_FIELD_STYLE_UNLOCKS = 29" in header and
            "style_unlocks" in engine and "style_unlocks" in save,
            "durable style entitlements are not wired end to end")
    require("value > TORIRSSERVER_POH_STYLE_MAX" in engine,
            "engine must reject absent style ordinals")

    print(
        "poh house-style contract: 12 template planes, 24 styled doors, "
        "72 decorative-window variants, and durable blueprint entitlements"
    )


if __name__ == "__main__":
    main()
