#!/usr/bin/env python3
"""Audit and statistically sample the post-2024 Wintertodt Reward Cart.

The source table is the implementation contract: this parser fails if a row,
coefficient, quantity, category weight, or unique raw rate drifts. It then runs
100,000 deterministic searches at levels 1, 50 and 99 and checks the observed
distribution against the probabilities implied by those parsed tables.

References:
  https://oldschool.runescape.wiki/w/Wintertodt_drop_rates
  https://oldschool.runescape.wiki/w/Reward_Cart
"""

from __future__ import annotations

import argparse
import math
import random
import re
import sys
from collections import Counter
from pathlib import Path


SEARCHES_PER_LEVEL = 100_000
SEED = 0x57_49_4E_54

# item, level-1 coefficient, level-99 coefficient, min quantity, max quantity
EXPECTED_ROWS = {
    "logs": [
        ("cert_magic_logs", -60, 60, 10, 20),
        ("cert_yew_logs", -50, 90, 10, 20),
        ("cert_mahogany_logs", -40, 130, 10, 20),
        ("cert_maple_logs", 0, 160, 10, 20),
        ("cert_teak_logs", 30, 200, 10, 20),
        ("cert_willow_logs", 120, 100, 10, 20),
        ("cert_oak_logs", None, None, 10, 20),
    ],
    "gems": [
        ("cert_uncut_diamond", 10, 120, 1, 3),
        ("cert_uncut_ruby", 50, 140, 2, 4),
        ("cert_uncut_emerald", 90, 160, 1, 3),
        ("cert_uncut_sapphire", None, None, 1, 3),
    ],
    "ores": [
        ("cert_runite_ore", -60, 40, 1, 2),
        ("cert_adamantite_ore", -50, 80, 2, 3),
        ("cert_mithril_ore", -20, 140, 3, 5),
        ("cert_gold_ore", 0, 160, 8, 11),
        ("cert_coal", 0, 180, 10, 14),
        ("cert_blankrune_high", 40, 190, 20, 70),
        ("cert_silver_ore", 140, 10, 10, 12),
        ("cert_limestone", 200, -20, 3, 7),
        ("cert_iron_ore", None, None, 5, 15),
    ],
    "herbs": [
        ("cert_unidentified_torstol", -70, 40, 1, 3),
        ("cert_unidentified_dwarf_weed", -50, 50, 2, 4),
        ("cert_unidentified_lantadyme", -30, 60, 2, 4),
        ("cert_unidentified_cadantine", -10, 70, 2, 4),
        ("cert_unidentified_kwuarm", 10, 85, 2, 4),
        ("cert_unidentified_avantoe", 20, 100, 3, 5),
        ("cert_unidentified_irit", 30, 115, 3, 5),
        ("cert_unidentified_ranarr", 10, 170, 1, 3),
        ("cert_unidentified_tarromin", 70, -20, 3, 6),
        ("cert_unidentified_marentill", 100, -30, 3, 6),
        ("cert_unidentified_guam", 170, -40, 3, 6),
        ("cert_unidentified_harralander", None, None, 3, 6),
    ],
    "seeds": [
        ("spirit_tree_seed", -20, 5, 1, 1),
        ("dwarf_weed_seed", -60, 25, 1, 3),
        ("lantadyme_seed", -60, 30, 1, 3),
        ("cadantine_seed", -40, 40, 1, 3),
        ("snapdragon_seed", -15, 60, 1, 3),
        ("yew_seed", -10, 70, 1, 2),
        ("snape_grass_seed", -8, 78, 3, 7),
        ("kwuarm_seed", -10, 80, 1, 3),
        ("ranarr_seed", 0, 110, 1, 3),
        ("avantoe_seed", 0, 130, 1, 3),
        ("watermelon_seed", -10, 180, 3, 7),
        ("irit_seed", 0, 170, 1, 3),
        ("teak_seed", 10, 160, 1, 2),
        ("maple_seed", 15, 190, 1, 2),
        ("mahogany_seed", 20, 190, 1, 2),
        ("toadflax_seed", 20, 190, 1, 3),
        ("banana_tree_seed", 30, 180, 1, 2),
        ("willow_seed", 60, 120, 1, 2),
        ("harralander_seed", 80, -150, 1, 3),
        ("tarromin_seed", 150, -150, 1, 3),
        ("acorn", None, None, 1, 1),
    ],
    "fish": [
        ("cert_raw_shark", -60, 80, 6, 11),
        ("cert_raw_swordfish", -50, 100, 6, 11),
        ("cert_raw_lobster", -20, 130, 6, 11),
        ("cert_raw_tuna", 10, 160, 6, 11),
        ("cert_raw_salmon", 40, 180, 6, 11),
        ("cert_raw_anchovies", 160, 0, 6, 11),
        ("cert_raw_trout", None, None, 6, 11),
    ],
}

CATEGORY_WEIGHTS = {
    "logs": 3,
    "gems": 3,
    "ores": 3,
    "herbs": 3,
    "seeds": 3,
    "fish": 3,
    "coins": 5,
    "saltpetre": 1,
    "dynamite": 1,
}

UNIQUE_DENOMINATORS = [
    ("phoenixpet", 5_000),
    ("dragon_axe", 10_000),
    ("tome_of_fire_uncharged", 1_000),
    ("pyromancer_gloves", 150),
    ("wint_torch", 150),
    ("pyromancer_outfit", 150),
    ("wint_burnt_page", 45),
]


def proc_body(text: str, name: str) -> str:
    match = re.search(
        rf"^\[proc,{re.escape(name)}\][^\n]*\n(.*?)(?=^\[|\Z)",
        text,
        flags=re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise AssertionError(f"missing proc {name}")
    return match.group(1)


def parse_quantity(expr: str) -> tuple[int, int]:
    expr = expr.strip()
    if expr.isdigit():
        value = int(expr)
        return value, value
    match = re.fullmatch(r"~wint_reward_quantity\((-?\d+),\s*(-?\d+)\)", expr)
    if not match:
        raise AssertionError(f"unrecognised quantity expression {expr!r}")
    return int(match.group(1)), int(match.group(2))


def parse_rows(text: str, category: str) -> list[tuple[str, int | None, int | None, int, int]]:
    body = proc_body(text, f"wint_reward_{category}")
    rows = []
    row_re = re.compile(
        r"if \(~wint_reward_row\(\$level,\s*(-?\d+),\s*(-?\d+)\) = true\) "
        r"return\((\w+),\s*([^;]+)\);"
    )
    for low, high, item, quantity in row_re.findall(body):
        minimum, maximum = parse_quantity(quantity)
        rows.append((item, int(low), int(high), minimum, maximum))
    fallback = re.search(r"^return\((\w+),\s*([^;]+)\);", body, flags=re.MULTILINE)
    if not fallback:
        raise AssertionError(f"{category}: missing fallback row")
    minimum, maximum = parse_quantity(fallback.group(2))
    rows.append((fallback.group(1), None, None, minimum, maximum))
    return rows


def threshold(level: int, low: int, high: int) -> int:
    # Python // is mathematical floor, matching wint_floor_div.
    return max(0, min(256, (low * 98 + (high - low) * (level - 1)) // 98 + 1))


def choose_row(rng: random.Random, rows, level: int) -> str:
    for item, low, high, _minimum, _maximum in rows[:-1]:
        if rng.randrange(256) < threshold(level, low, high):
            return item
    return rows[-1][0]


def row_probabilities(rows, level: int) -> dict[str, float]:
    remaining = 1.0
    probabilities = {}
    for item, low, high, _minimum, _maximum in rows[:-1]:
        selected = remaining * threshold(level, low, high) / 256.0
        probabilities[item] = selected
        remaining -= selected
    probabilities[rows[-1][0]] = remaining
    return probabilities


def within_sampling_error(actual: int, expected: float) -> bool:
    # Fixed seed makes this reproducible; the generous bound is still tight
    # enough to catch a shifted denominator, weight, or interpolation curve.
    sigma = math.sqrt(max(expected, 1.0))
    return abs(actual - expected) <= 7.0 * sigma + 4.0


def assert_source_contract(text: str, rows) -> None:
    for category, expected in EXPECTED_ROWS.items():
        if rows[category] != expected:
            raise AssertionError(
                f"{category} table drifted\n  got:  {rows[category]}\n  want: {expected}"
            )

    material = proc_body(text, "wint_reward_material")
    required_material = [
        "def_int $slot = random(25);",
        "if ($slot < 3) return(~wint_reward_logs);",
        "if ($slot < 6) return(~wint_reward_gems);",
        "if ($slot < 9) return(~wint_reward_ores);",
        "if ($slot < 12) return(~wint_reward_herbs);",
        "if ($slot < 15) return(~wint_reward_seeds);",
        "if ($slot < 18) return(~wint_reward_fish);",
        "if ($slot < 23) return(coins, ~wint_reward_quantity(2000, 4999));",
        "if ($slot = 23) return(cert_hosidius_saltpetre, ~wint_reward_quantity(3, 5));",
        "return(cert_lovakengj_dynamite_fused, ~wint_reward_quantity(3, 5));",
    ]
    for line in required_material:
        if line not in material:
            raise AssertionError(f"material category contract missing {line!r}")

    roll = proc_body(text, "wint_reward_roll")
    required_unique = [
        "random(5000) < 15",
        "random(5000) = 0",
        "random(10000) = 0",
        "random(1000) = 0",
        "random(150) = 0",
        "random(45) = 0",
        "~wint_reward_quantity(7, 29)",
    ]
    for expression in required_unique:
        if expression not in roll:
            raise AssertionError(f"unique reward contract missing {expression!r}")
    if roll.count("random(150) = 0") != 3:
        raise AssertionError("unique reward chain must contain exactly three 1/150 rolls")


def simulate(rows) -> None:
    rng = random.Random(SEED)
    total = SEARCHES_PER_LEVEL * 3
    unique_counts = Counter()
    category_counts = Counter()
    row_counts = Counter()

    for level in (1, 50, 99):
        for _ in range(SEARCHES_PER_LEVEL):
            selected = None
            for name, denominator in UNIQUE_DENOMINATORS:
                if rng.randrange(denominator) == 0:
                    selected = name
                    break
            if selected is not None:
                unique_counts[selected] += 1
                continue

            slot = rng.randrange(25)
            if slot < 18:
                category = ("logs", "gems", "ores", "herbs", "seeds", "fish")[slot // 3]
                category_counts[(level, category)] += 1
                item = choose_row(rng, rows[category], level)
                row_counts[(level, category, item)] += 1
            elif slot < 23:
                category_counts[(level, "coins")] += 1
            elif slot == 23:
                category_counts[(level, "saltpetre")] += 1
            else:
                category_counts[(level, "dynamite")] += 1

    remaining = 1.0
    for name, denominator in UNIQUE_DENOMINATORS:
        expected = total * remaining / denominator
        if not within_sampling_error(unique_counts[name], expected):
            raise AssertionError(
                f"unique {name}: observed {unique_counts[name]}, expected {expected:.1f}"
            )
        remaining *= 1.0 - 1.0 / denominator

    for level in (1, 50, 99):
        for category, weight in CATEGORY_WEIGHTS.items():
            expected = SEARCHES_PER_LEVEL * remaining * weight / 25.0
            actual = category_counts[(level, category)]
            if not within_sampling_error(actual, expected):
                raise AssertionError(
                    f"level {level} {category}: observed {actual}, expected {expected:.1f}"
                )

        for category, table in rows.items():
            material_rolls = SEARCHES_PER_LEVEL * remaining * 3.0 / 25.0
            probabilities = row_probabilities(table, level)
            for item, probability in probabilities.items():
                expected = material_rolls * probability
                # Very rare rows are proven reachable analytically below; a
                # tiny Monte Carlo expectation is not a useful pass/fail test.
                if expected < 9.0:
                    continue
                actual = row_counts[(level, category, item)]
                if not within_sampling_error(actual, expected):
                    raise AssertionError(
                        f"level {level} {category}/{item}: observed {actual}, "
                        f"expected {expected:.1f}"
                    )

    for category, table in rows.items():
        for item, low, high, _minimum, _maximum in table[:-1]:
            if not any(threshold(level, low, high) > 0 for level in (1, 50, 99)):
                raise AssertionError(f"{category}/{item} is unreachable at audited levels")

    print(
        "wintertodt_reward_audit: source contract passed; "
        f"sampled {total:,} searches (100,000 each at levels 1, 50, and 99)"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tree", default="OSRS-Content/osrs239-content")
    args = parser.parse_args()
    path = (
        Path(args.tree)
        / "server/scripts/minigames/minigame_wintertodt/scripts/wintertodt_rewards.rs2"
    )
    if not path.exists():
        print(f"wintertodt_reward_audit: missing {path}", file=sys.stderr)
        return 1
    try:
        text = path.read_text(encoding="utf-8")
        rows = {category: parse_rows(text, category) for category in EXPECTED_ROWS}
        assert_source_contract(text, rows)
        simulate(rows)
    except AssertionError as error:
        print(f"wintertodt_reward_audit: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
