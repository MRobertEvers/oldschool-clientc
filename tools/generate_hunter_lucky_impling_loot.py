#!/usr/bin/env python3
"""Generate the Lucky impling's exact single-roll clue reward tables.

The Lucky impling chooses easy, medium, hard, elite, or master with equal
probability, then makes one reward roll from that tier.  The OSRS Wiki lists
the unconditional probability of every result, so each tier can be flattened
into an integer-weighted table without implementing clue-scroll progression.

Run this from the repository root.  It intentionally requires network access;
the generated dbrow file is checked in and has no runtime network dependency.
"""

from __future__ import annotations

import argparse
import ast
from collections import defaultdict
from fractions import Fraction
import json
import math
from pathlib import Path
import re
import urllib.parse
import urllib.request


TIERS = ("easy", "medium", "hard", "elite", "master")
EXPECTED = {
    "easy": (211, 617_760),
    "medium": (192, 23_181_180),
    "hard": (233, 6_971_250),
    "elite": (182, 1_699_970_250),
    "master": (173, 1_498_508_880),
}
USER_AGENT = "3draster Hunter implementation (Lucky impling table generator)"


def fetch_json(url: str) -> object:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def wiki_reward_source(tier: str) -> str:
    query = urllib.parse.urlencode(
        {
            "action": "parse",
            "page": f"Reward casket ({tier})",
            "prop": "wikitext",
            "format": "json",
        }
    )
    parsed = fetch_json("https://oldschool.runescape.wiki/api.php?" + query)
    source = parsed["parse"]["wikitext"]["*"]
    source = source[source.index("==Rewards==") : source.index("===Tertiary===")]

    # Lucky implings roll the casket's ordinary reward list, not a Mimic fight.
    mimic = source.find("====Mimic rewards====")
    if mimic >= 0:
        next_section = source.find("\n===", mimic + 5)
        source = source[:mimic] + source[next_section:]
    return source


def split_template(line: str) -> dict[str, str]:
    body = line[2:-2]
    parts: list[str] = []
    start = 0
    depth = 0
    index = 0
    while index < len(body):
        if body.startswith("{{", index):
            depth += 1
            index += 2
            continue
        if body.startswith("}}", index):
            depth -= 1
            index += 2
            continue
        if body[index] == "|" and depth == 0:
            parts.append(body[start:index])
            start = index + 1
        index += 1
    parts.append(body[start:])
    return dict(part.split("=", 1) for part in parts[1:] if "=" in part)


def reward_rows(tier: str) -> list[dict[str, str]]:
    rows = [
        split_template(line)
        for line in wiki_reward_source(tier).splitlines()
        if line.startswith("{{DropsLineReward|")
    ]

    # These three entries are one hard-clue result, awarded together.
    grouped: list[dict[str, str]] = []
    saw_super_potion_bundle = False
    for row in rows:
        if "dual-drop" in row.get("raritynotes", ""):
            if saw_super_potion_bundle:
                continue
            saw_super_potion_bundle = True
            row["bundle"] = "1"
        grouped.append(row)
    return grouped


def arithmetic(expression: str) -> Fraction:
    tree = ast.parse(expression.strip().replace("−", "-"), mode="eval")

    def evaluate(node: ast.AST) -> Fraction:
        if isinstance(node, ast.Expression):
            return evaluate(node.body)
        if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
            return Fraction(str(node.value))
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            return -evaluate(node.operand)
        if isinstance(node, ast.BinOp):
            left = evaluate(node.left)
            right = evaluate(node.right)
            if isinstance(node.op, ast.Add):
                return left + right
            if isinstance(node.op, ast.Sub):
                return left - right
            if isinstance(node.op, ast.Mult):
                return left * right
            if isinstance(node.op, ast.Div):
                return left / right
        raise ValueError(f"unsupported Wiki arithmetic: {ast.dump(node)}")

    return evaluate(tree)


def probability(rarity: str) -> Fraction:
    # The Wiki renders many exact probabilities as a rounded one-over value.
    # Preserve the unrounded expression inside that presentation wrapper.
    match = re.fullmatch(r"1/\{\{#expr:1/\((.*)\) round 1\}\}", rarity)
    if match:
        return arithmetic(match.group(1))

    while "{{#expr:" in rarity:
        start = rarity.rfind("{{#expr:")
        end = rarity.find("}}", start)
        expression = rarity[start + 8 : end].strip()
        if expression.endswith(" round 1"):
            expression = expression[:-8].strip()
        value = arithmetic(expression)
        rarity = (
            rarity[:start]
            + f"({value.numerator}/{value.denominator})"
            + rarity[end + 2 :]
        )
    return arithmetic(rarity)


def cache_objects(path: Path) -> list[dict[str, str]]:
    records: list[dict[str, str]] = []
    record: dict[str, str] | None = None
    with path.open(encoding="latin1") as stream:
        for line in stream:
            section = re.fullmatch(r"\[([^]]+)\]\n?", line)
            if section:
                if record is not None:
                    records.append(record)
                record = {"symbol": section.group(1)}
            elif record is not None and "=" in line:
                key, value = line.rstrip("\n").split("=", 1)
                record[key] = value
    if record is not None:
        records.append(record)
    return records


def exchange_item_ids() -> dict[str, list[int]]:
    mapping = fetch_json("https://prices.runescape.wiki/api/v1/osrs/mapping")
    by_name: dict[str, list[int]] = defaultdict(list)
    for item in mapping:
        by_name[item["name"]].append(item["id"])
    by_name["Coins"].append(995)
    return by_name


def object_symbol(
    name: str,
    noted: bool,
    ids_by_name: dict[str, list[int]],
    objects: list[dict[str, str]],
) -> str:
    ids = ids_by_name.get(name, [])
    if len(ids) != 1:
        raise ValueError(f"expected one exchange/cache ID for {name!r}, got {ids}")
    item_id = ids[0]
    if item_id >= len(objects):
        raise ValueError(f"cache does not contain item {item_id} ({name})")
    record = objects[item_id]
    if noted:
        symbol = record.get("certlink")
        if symbol is None:
            raise ValueError(f"cache item {item_id} ({name}) has no noted variant")
        return symbol
    return record["symbol"]


def quantity(value: str) -> tuple[int, int, bool]:
    noted = value.endswith(" (noted)")
    value = value.removesuffix(" (noted)").replace(",", "")
    if "-" in value:
        minimum, maximum = value.split("-", 1)
        return int(minimum), int(maximum), noted
    amount = int(value)
    return amount, amount, noted


def generate(objects_path: Path) -> str:
    objects = cache_objects(objects_path)
    ids_by_name = exchange_item_ids()
    output = [
        "// Generated by tools/generate_hunter_lucky_impling_loot.py.",
        "// Source: OSRS Wiki reward-casket pages; ordinary per-roll rewards only.",
        "// Tiers: 0 easy, 1 medium, 2 hard, 3 elite, 4 master.",
        "",
    ]

    for tier_index, tier in enumerate(TIERS):
        rows = reward_rows(tier)
        probabilities = [probability(row["rarity"]) for row in rows]
        total = sum(probabilities, Fraction())
        if total != 1:
            raise ValueError(f"{tier} probabilities sum to {total}, not 1")
        denominator = math.lcm(*(value.denominator for value in probabilities))
        expected_rows, expected_denominator = EXPECTED[tier]
        if (len(rows), denominator) != (expected_rows, expected_denominator):
            raise ValueError(
                f"{tier} Wiki table changed: got {(len(rows), denominator)}, "
                f"expected {(expected_rows, expected_denominator)}"
            )

        for index, (row, chance) in enumerate(zip(rows, probabilities)):
            minimum, maximum, noted = quantity(row["quantity"])
            symbol = object_symbol(row["name"], noted, ids_by_name, objects)
            weight = chance * denominator
            if weight.denominator != 1:
                raise ValueError(f"non-integral {tier} weight for {row['name']}: {weight}")
            output.extend(
                [
                    f"[hunter_lucky_{tier}_{index:03d}]",
                    "table=hunter_lucky_impling_loot",
                    f"data=tier,{tier_index}",
                    f"data=product,{symbol}",
                    f"data=amount,{minimum}",
                    *( [f"data=amountmax,{maximum}"] if maximum != minimum else [] ),
                    f"data=weight,{weight.numerator}",
                    *( ["data=bundle,1"] if row.get("bundle") == "1" else [] ),
                    "",
                ]
            )
    return "\n".join(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--objects",
        type=Path,
        default=Path("OSRS-Content/osrs239-content/configs/all.obj"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(
            "OSRS-Content/osrs239-content/server/scripts/skill_hunter/configs/"
            "lucky_impling_loot.dbrow"
        ),
    )
    args = parser.parse_args()
    args.output.write_text(generate(args.objects), encoding="utf-8")


if __name__ == "__main__":
    main()
