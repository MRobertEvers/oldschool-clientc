#!/usr/bin/env python3
"""Generate the treasure-trail reward tables from the pinned wiki corpus.

Reads   docs/treasure_trails/sources/Reward_casket_(<tier>).wiki
        OSRS-Content/osrs239-content/configs/all.obj{,.compack}
        tools/trail_reward_aliases.tsv       (hand-owned; see below)
Writes  OSRS-Content/osrs239-content/server/scripts/trail/configs/trail_rewards.dbrow

WHY THIS IS GENERATED
---------------------
Near-Reality carries the six reward tables as 1,347 lines of hand-typed Java.
The OSRS Wiki carries the same tables as 1,238 structured `{{DropsLineReward}}`
entries with exact rarities, and 1,235 of the 1,238 item names resolve straight
to a cache obj by display name. Typing them out again would be typing out data
that is already machine-readable and already pinned in this repo.

WHAT IS FAITHFUL, AND WHAT IS NOT
---------------------------------
Faithful: the item set of each tier, each item's quantity, and the RELATIVE
weight of one item against another within a tier. All three come straight from
the pinned wikitext.

Not faithful: the absolute per-roll probability. The wiki's reward pages are
built from nested sub-tables whose rarities are conditional on reaching that
sub-table ("1/12 to hit the standard table, then 6/117 within it"), and some
entries state the composed figure while others state the local one. Measured,
the per-tier rarities sum to 1.00 (beginner), 1.55, 1.53, 1.07, 2.20 and 2.00
rather than to 1. This generator therefore treats the parsed rarity as a
RELATIVE weight and normalises it, and prints the measured sum every run so the
size of the approximation stays visible instead of being forgotten.

Fixing that properly means modelling the sub-table structure per tier, which is
its own pass; it changes how often a given item appears, never which items exist
or which is rarer than which.

NAME RESOLUTION
---------------
Display name -> obj, with three rules applied in order, because 59 display names
are carried by more than one obj:

  1. an explicit row in tools/trail_reward_aliases.tsv wins;
  2. a `trail_*` symbol wins, since these are trail rewards;
  3. otherwise the lowest id that is not in the non-canonical prefix list —
     `fake_` (grand-exchange dummies), `br_` (Bounty Hunter replicas),
     `roguetrader_`, `100guide_`, `slug2_`, `magictraining_`, `poh_shield_`,
     `hw21_`, `twitch_`, `mystery`, `easter_egg_`, `castlewars_`.

Anything still unresolved is reported and skipped, never guessed.

  python3 tools/gen_trail_rewards.py [--check]
"""

import argparse
import ast
import collections
import os
import re
import sys
from fractions import Fraction

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(ROOT, "OSRS-Content", "osrs239-content")
SOURCES = os.path.join(ROOT, "docs", "treasure_trails", "sources")
ALIASES = os.path.join(ROOT, "tools", "trail_reward_aliases.tsv")
OUT = os.path.join(
    CONTENT, "server", "scripts", "trail", "configs", "trail_rewards.dbrow"
)

TIERS = ["beginner", "easy", "medium", "hard", "elite", "master"]

# Objs that carry a real item's display name but are not that item.
NON_CANONICAL = (
    "fake_", "br_", "roguetrader_", "100guide_", "slug2_", "magictraining_",
    "poh_shield_", "hw21_", "twitch_", "mystery", "easter_egg_", "castlewars_",
    "placeholder_", "cert_",
)

# The weight denominator every tier's table is scaled to. Large enough that the
# rarest entry in any tier (about 1/22000) still rounds to a non-zero weight,
# and small enough to stay well inside the VM's int range when summed.
SCALE = 1000000


def wiki_expr(text):
    """Evaluate a `{{#expr:}}` body in exact rational arithmetic.

    Exact, not float: several rarities are of the form `1/( 1/13 * 1/125 )`,
    and evaluating those in binary floating point then rounding to a weight
    loses the distinction between neighbouring entries.
    """
    text = re.sub(r"\bround\s+\d+", "", text).strip()

    def walk(node):
        if isinstance(node, ast.Expression):
            return walk(node.body)
        if isinstance(node, ast.Constant):
            return Fraction(node.value)
        if isinstance(node, ast.BinOp):
            left, right = walk(node.left), walk(node.right)
            if isinstance(node.op, ast.Add):
                return left + right
            if isinstance(node.op, ast.Sub):
                return left - right
            if isinstance(node.op, ast.Mult):
                return left * right
            if isinstance(node.op, ast.Div):
                return left / right
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            return -walk(node.operand)
        raise ValueError(text)

    return walk(ast.parse(text, mode="eval"))


def parse_rarity(raw):
    """`11/{{#expr:12 * 33 * 10}}` -> Fraction, or the string 'always'."""
    text = raw.strip()
    # The template's own closing braces ride along on the last field.
    while text.endswith("}}") and text.count("{{") < text.count("}}"):
        text = text[:-2].strip()
    if text.lower() == "always":
        return "always"
    text = re.sub(
        r"\{\{#expr:([^{}]*)\}\}", lambda m: str(wiki_expr(m.group(1))), text
    )
    match = re.match(r"^([0-9]+)\s*/\s*(.+)$", text)
    if match:
        return Fraction(int(match.group(1))) / Fraction(match.group(2))
    return Fraction(1) / Fraction(text)


def split_fields(raw):
    """Split a template's `|`-separated fields, respecting nested `{{ }}`."""
    parts, depth, cur = [], 0, ""
    for ch in raw:
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        if ch == "|" and depth <= 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    parts.append(cur)
    out = {}
    for part in parts:
        if "=" in part:
            key, value = part.split("=", 1)
            out[key.strip()] = value.strip()
    return out


def load_objs():
    """display name (lower) -> [symbol], and symbol -> id."""
    ids = {}
    with open(os.path.join(CONTENT, "configs", "all.obj.compack"), encoding="utf-8") as h:
        for line in h:
            line = line.strip()
            if "=" in line and line.split("=", 1)[0].isdigit():
                num, name = line.split("=", 1)
                ids[name] = int(num)
    text = open(
        os.path.join(CONTENT, "configs", "all.obj"), encoding="utf-8", errors="replace"
    ).read()
    by_name = collections.defaultdict(list)
    for block in re.split(r"\n(?=\[)", text):
        head = re.match(r"\[([^\]]+)\]", block)
        if not head:
            continue
        name = re.search(r"^name=(.*)$", block, re.M)
        if not name:
            continue
        by_name[name.group(1).strip().lower()].append(head.group(1))
    return by_name, ids


def load_aliases():
    aliases = {}
    if not os.path.exists(ALIASES):
        return aliases
    with open(ALIASES, encoding="utf-8") as handle:
        for line in handle:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) >= 2:
                aliases[parts[0].strip().lower()] = parts[1].strip()
    return aliases


def resolve(name, by_name, ids, aliases):
    key = name.strip().lower()
    if key in aliases:
        return aliases[key] or None
    candidates = by_name.get(key)
    if not candidates:
        return None
    if len(candidates) == 1:
        return candidates[0]
    trail = [c for c in candidates if c.startswith("trail_")]
    if len(trail) == 1:
        return trail[0]
    clean = [c for c in candidates if not c.startswith(NON_CANONICAL)]
    if not clean:
        clean = candidates
    return min(clean, key=lambda c: ids.get(c, 1 << 30))


def read_tier(tier):
    path = os.path.join(SOURCES, "Reward_casket_(%s).wiki" % tier)
    text = open(path, encoding="utf-8").read()
    entries, always = {}, []
    for raw in re.findall(r"\{\{DropsLineReward\|(.*)$", text, re.M):
        field = split_fields(raw)
        name = field.get("name", "").strip()
        if not name:
            continue
        quantity = field.get("quantity", "1").split("&")[0].split("-")[0].strip()
        if not quantity.isdigit():
            quantity = "1"
        value = parse_rarity(field.get("rarity", ""))
        if value == "always":
            always.append(name)
            continue
        key = (name, int(quantity))
        # The same item appears in more than one sub-table; the highest stated
        # rarity is the one that describes it best.
        if key not in entries or value > entries[key]:
            entries[key] = value
    return entries, always


def render(tables, report):
    lines = [
        "// Treasure Trails — what comes out of a reward casket.",
        "//",
        "// GENERATED by tools/gen_trail_rewards.py from the pinned wikitext in",
        "// docs/treasure_trails/sources/. Hand edits are lost on the next run; to",
        "// correct an item, add a row to tools/trail_reward_aliases.tsv, and to",
        "// correct a rate, fix the wiki snapshot and regenerate.",
        "//",
        "//     python3 tools/gen_trail_rewards.py",
        "//",
        "// Six rows on the existing `drop_table` schema, walked by",
        "// `~roll_on_drop_table` (drop_tables/scripts/drop_table.rs2) — the same",
        "// roller the gem rocks use. A casket rolls it several times; how many is",
        "// `^trail_rolls_<tier>_min/max` in trail.constant, and those are the wiki's",
        "// numbers.",
        "//",
        "// THE ONE APPROXIMATION. The weights below are RELATIVE, not absolute. The",
        "// wiki's reward pages are nested sub-tables whose rarities are conditional",
        "// on reaching the sub-table, and the per-tier rarities sum to the figures",
        "// in the per-tier comments rather than to 1. So which items exist, their",
        "// quantities, and which is rarer than which are all the wiki's; how often a",
        "// casket yields any particular one is scaled. Modelling the sub-table",
        "// structure is its own pass and would change only the last of those three.",
        "",
    ]
    for tier in TIERS:
        rows, total, raw_sum, dropped = tables[tier]
        lines.append(
            "// %s: %d entries, wiki rarities summing to %.5f (see above), %d unresolved."
            % (tier, len(rows), raw_sum, dropped)
        )
        lines.append("[trail_reward_%s]" % tier)
        lines.append("table=drop_table")
        lines.append("data=total,%d" % total)
        for symbol, quantity, weight in rows:
            lines.append("data=drop,%s,%d,%d" % (symbol, quantity, weight))
        lines.append("")
    lines.extend(report)
    lines.append("")
    return "\n".join(lines)


def build():
    by_name, ids = load_objs()
    aliases = load_aliases()
    tables, report, unresolved = {}, [], collections.Counter()
    for tier in TIERS:
        entries, always = read_tier(tier)
        raw_sum = float(sum(entries.values()))
        rows, dropped = [], 0
        for (name, quantity), value in sorted(
            entries.items(), key=lambda kv: (-kv[1], kv[0])
        ):
            symbol = resolve(name, by_name, ids, aliases)
            if not symbol:
                unresolved[name] += 1
                dropped += 1
                continue
            weight = int(value / Fraction(raw_sum).limit_denominator(10 ** 6) * SCALE)
            rows.append((symbol, quantity, max(weight, 1)))
        total = sum(w for _, _, w in rows)
        tables[tier] = (rows, total, raw_sum, dropped)
        if always:
            report.append(
                "// %s: %d 'always' entries are NOT in the table — they are"
                % (tier, len(always))
            )
            report.append(
                "//   milestone or tertiary rewards, not roll results: %s"
                % ", ".join(sorted(set(always)))
            )
    if unresolved:
        report.append("//")
        report.append("// Unresolved item names, skipped rather than guessed. Add a row to")
        report.append("// tools/trail_reward_aliases.tsv to bind one:")
        for name, count in unresolved.most_common():
            report.append("//   %s (%d)" % (name, count))
    return tables, report, unresolved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    tables, report, unresolved = build()
    rendered = render(tables, report)

    if args.check:
        try:
            have = open(OUT, encoding="utf-8").read()
        except FileNotFoundError:
            print("gen_trail_rewards: %s does not exist" % OUT, file=sys.stderr)
            return 1
        if have != rendered:
            print(
                "gen_trail_rewards: %s is stale — rerun tools/gen_trail_rewards.py" % OUT,
                file=sys.stderr,
            )
            return 1
        print("gen_trail_rewards: up to date")
        return 0

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as handle:
        handle.write(rendered)
    for tier in TIERS:
        rows, total, raw_sum, dropped = tables[tier]
        print(
            "  %-9s %4d entries  total %8d  wiki sum %.5f  unresolved %d"
            % (tier, len(rows), total, raw_sum, dropped)
        )
    if unresolved:
        print("  unresolved names: %s" % ", ".join(unresolved))
    print("gen_trail_rewards: -> %s" % os.path.relpath(OUT, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
