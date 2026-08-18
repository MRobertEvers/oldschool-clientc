#!/usr/bin/env python3
"""Static contract for the Wiki-backed POH servant implementation."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "OSRS-Content/osrs239-content/server/scripts/skill_construction"
SCRIPT = CONTENT / "scripts/poh_servants.rs2"
CONSTANTS = CONTENT / "configs/construction.constant"
ENGINE = ROOT / "src/net/mock/mock230_poh.c"
HEADER = ROOT / "src/net/mock/mock230_poh.h"


def require(text: str, pattern: str, message: str) -> None:
    if re.search(pattern, text, re.MULTILINE) is None:
        raise AssertionError(message)


def main() -> int:
    script = SCRIPT.read_text()
    constants = CONSTANTS.read_text()
    engine = ENGINE.read_text()
    header = HEADER.read_text()

    # Cache varbit ordinals are sparse; accepting ordinal 2 would hide/show the
    # wrong Servants' Guild multinpc for that player.
    servant_rows = {
        "rick": (1, 20, 500, 6, 100, "shrimp"),
        "maid": (3, 25, 1000, 10, 50, "stew"),
        "cook": (5, 30, 3000, 16, 28, "pineapple_pizza"),
        "butler": (6, 40, 5000, 20, 20, "chocolate_cake"),
        "demon": (8, 50, 10000, 26, 12, "curry"),
    }
    for name, (ordinal, level, wage, capacity, ticks, food) in servant_rows.items():
        require(constants, rf"\^poh_servant_{name}\s*=\s*{ordinal}\b",
                f"{name} cache ordinal drifted")
        require(script, rf"\$type = \^poh_servant_{name}\) return\({level}\);",
                f"{name} level drifted")
        require(constants, rf"\^poh_servant_wage_{name}\s*=\s*{wage}\b",
                f"{name} wage drifted")
        require(script, rf"\$type = \^poh_servant_{name}\) return\({capacity}\);",
                f"{name} capacity drifted")
        require(constants, rf"\^poh_servant_ticks_{name}\s*=\s*{ticks}\b",
                f"{name} trip duration drifted")
        require(script, rf"\$type = \^poh_servant_{name}\) return\({food}\);",
                f"{name} food drifted")

    for obj in (
        "woodplank", "plank_oak", "plank_teak", "plank_mahogany",
        "softclay", "limestonebrick", "steel_bar", "cloth", "gold_leaf",
        "marble_block", "poh_magic_crystal",
    ):
        require(script, rf"return\({obj}\);", f"bank material {obj} is missing")

    for log, plank, cost in (
        ("logs", "woodplank", 100),
        ("oak_logs", "plank_oak", 250),
        ("teak_logs", "plank_teak", 500),
        ("mahogany_logs", "plank_mahogany", 1500),
    ):
        require(script, rf"case {log} : return\({plank}\);",
                f"{log} sawmill product drifted")
        require(constants, rf"\^poh_sawmill_[a-z]+_cost\s*=\s*{cost}\b",
                f"sawmill price {cost} is missing")

    for symbol in (
        "poh_bellpull_1", "poh_bellpull_2", "poh_bellpull_3",
        "poh_chief_servant", "poh_servant_dogsbody",
        "poh_servant_waiter_woman", "poh_servant_cook_woman",
        "poh_servant_maitre_d_man", "poh_servant_demon",
    ):
        require(script, rf"\b{symbol}\b", f"dispatch for {symbol} is missing")

    require(script, r"poh_servant_bed_count < 2", "two-bed hiring gate is missing")
    require(constants, r"poh_servant_services_per_wage\s*=\s*8",
            "eight-service wage constant is missing")
    require(script, r"poh_servant_is_bone", "bone exclusion is missing")
    require(script, r"poh_servant_table_service", "seat-aware guest service is missing")
    require(script, r"poh_servant_serve_tea", "tea service is missing")
    require(script, r"poh_servant_serve_drinks", "barrel-drink service is missing")
    require(script, r"poh_servant_repeat", "last-request replay is missing")
    require(script, r"poh_servant_guest_greet", "guest greeting is missing")
    require(script, r"poh_state_commit", "employment is not durable")
    require(header, r"MOCK230_POH_SERVANT_SERVICE_MAX\s*=\s*7",
            "engine service bound is missing")
    require(header, r"MOCK230_POH_SERVANT_TASK_MAX\s*=\s*20",
            "engine remembered-material bound is missing")
    require(engine, r"value == 0 \|\| value == 1 \|\| value == 3 \|\| value == 5",
            "engine sparse servant validation is missing")

    print("poh servant contract: 5 employees, 11 bank materials, 4 sawmill recipes, "
          "3 bell pulls, table/tea/drink service, replay, and durable wages")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"poh servant contract: {error}", file=sys.stderr)
        raise SystemExit(1)
