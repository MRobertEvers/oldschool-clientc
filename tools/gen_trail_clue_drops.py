#!/usr/bin/env python3
"""Generate the per-npc clue-scroll drop table from the pinned wiki corpus.

Reads   OSRS-Content/osrs239-content/wiki/monsters/*.wikitext
        OSRS-Content/osrs239-content/npc_stats/*/*.stats     (title -> gameval)
Writes  OSRS-Content/osrs239-content/server/scripts/trail/configs/trail_clue_drops.dbrow

WHY THIS EXISTS
---------------
`drop_tables/scripts/shared_droptables.rs2` §2 records that `~trail_*cluedrop`
was deleted from all 21 LostCity sites, deliberately: "a stub that drops a clue
scroll nothing can read is worse than no clue at all", with a note that the
sites are recoverable. Clues are readable now (queue slices A1-A5), so the
drops come back — but not from those 21 sites, because this tree has something
better pinned already.

The OSRS Wiki states clue drops with a dedicated template,
`{{DropsLineClue|type=<tier>|rarity=A/B}}`, and this repo's `wiki/monsters/`
corpus holds 978 monster pages carrying 346 of them across 260 monsters. Those
are the real per-npc rates, per tier, from the source the rest of the drop
tables in this tree already cite.

THE ID JOIN IS NOT RE-DERIVED
-----------------------------
Same rule as tools/wiki_droptable.py: the id match already happened once, when
`gen_npc_stats.py` wrote `npc_stats/<shard>/<gameval>.stats` with a
`// Source: OSRS Wiki '<Title>'` line. This reads that back. Matching monsters
to npcs by name here would be a second, disagreeing answer to a question that
already has one.

KNOWN COARSENESS, stated rather than hidden
-------------------------------------------
A wiki page can describe several npc records (Hobgoblin is armed and unarmed;
Guard is several) and a clue line can sit under a `dropversion` that applies to
only some of them. This applies a page's clue drops to EVERY gameval sourced
from that page. The alternative is modelling drop versions, which the whole
`wiki_*` table family does not do either (see any of its headers). It makes a
handful of npcs drop a clue their wiki variant does not; it never invents a
rate and never moves one between tiers.

  python3 tools/gen_trail_clue_drops.py [--check]
"""

import argparse
import collections
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(ROOT, "OSRS-Content", "osrs239-content")
MONSTERS = os.path.join(CONTENT, "wiki", "monsters")
STATS = os.path.join(CONTENT, "npc_stats")
OUT = os.path.join(
    CONTENT, "server", "scripts", "trail", "configs", "trail_clue_drops.dbrow"
)

TIERS = {"beginner": 0, "easy": 1, "medium": 2, "hard": 3, "elite": 4, "master": 5}
SOURCE_RE = re.compile(r"//\s*Source: OSRS Wiki '([^']+)'")
CLUE_RE = re.compile(r"\{\{DropsLineClue\|([^}]*)\}\}")


def load_title_to_gamevals():
    """wiki page title -> [gameval], from the ledger gen_npc_stats.py wrote."""
    out = collections.defaultdict(list)
    for path in glob.glob(os.path.join(STATS, "*", "*.stats")):
        gameval = os.path.splitext(os.path.basename(path))[0]
        try:
            with open(path, encoding="utf-8", errors="replace") as handle:
                head = handle.read(4096)
        except OSError:
            continue
        match = SOURCE_RE.search(head)
        if match:
            out[match.group(1).strip()].append(gameval)
    return out


def parse_rate(raw):
    """`1/128` -> 128. Returns None for anything not a plain 1-in-N."""
    raw = raw.strip()
    match = re.match(r"^1\s*/\s*([0-9]+)$", raw)
    if match:
        return int(match.group(1))
    # A few pages write `2/128`; express as the nearest 1-in-N so the roller
    # stays a single `random(N) = 0`, and report it so the rounding is visible.
    match = re.match(r"^([0-9]+)\s*/\s*([0-9]+)$", raw)
    if match and int(match.group(1)) > 0:
        return max(1, round(int(match.group(2)) / int(match.group(1))))
    return None


def collect():
    titles = load_title_to_gamevals()
    rows = {}
    unjoined = collections.Counter()
    odd_rates = []
    for path in sorted(glob.glob(os.path.join(MONSTERS, "*.wikitext"))):
        title = os.path.splitext(os.path.basename(path))[0]
        text = open(path, encoding="utf-8", errors="replace").read()
        found = CLUE_RE.findall(text)
        if not found:
            continue
        gamevals = titles.get(title)
        if not gamevals:
            unjoined[title] = len(found)
            continue
        for body in found:
            fields = {}
            for part in body.split("|"):
                if "=" in part:
                    key, value = part.split("=", 1)
                    fields[key.strip()] = value.strip()
            tier = TIERS.get(fields.get("type", "").lower())
            if tier is None:
                continue
            rate = parse_rate(fields.get("rarity", ""))
            if rate is None:
                odd_rates.append((title, fields.get("rarity", "")))
                continue
            if fields.get("rarity", "").strip() != "1/%d" % rate:
                odd_rates.append((title, fields.get("rarity", "")))
            for gameval in gamevals:
                key = (gameval, tier)
                # The rarest statement wins when a page states one twice: a
                # duplicate is a per-version repeat, not two chances.
                if key not in rows or rate > rows[key]:
                    rows[key] = rate
    return rows, unjoined, odd_rates


def render(rows, unjoined, odd_rates):
    lines = [
        "// Which npcs drop a clue scroll, and how often.",
        "//",
        "// GENERATED by tools/gen_trail_clue_drops.py from this repo's own pinned",
        "// wiki corpus (`wiki/monsters/`), which states clue drops with the",
        "// `{{DropsLineClue|type=<tier>|rarity=A/B}}` template. Hand edits are lost.",
        "//",
        "//     python3 tools/gen_trail_clue_drops.py",
        "//",
        "// Rolled by `~trail_clue_drop_check`, called from `~npc_default_death`.",
        "// `shared_droptables.rs2` §2 deleted the reference's 21 clue-drop sites on",
        "// the grounds that dropping a scroll nothing could read was worse than no",
        "// scroll; these are the same drops restored from a better source, now that",
        "// the scrolls read.",
        "//",
        "// `rate` is the N of a 1-in-N roll. A page stating a non-unit numerator is",
        "// converted to the nearest 1-in-N and listed at the bottom of this file, so",
        "// every rounding is visible rather than silent.",
        "",
        "[trail_clue_drop_table]",
        "table=trail_clue_drop",
    ]
    for (gameval, tier), rate in sorted(rows.items(), key=lambda kv: (kv[0][0], kv[0][1])):
        lines.append("data=drop,%s,%d,%d" % (gameval, tier, rate))
    lines.append("")
    if odd_rates:
        lines.append("// Rounded to a 1-in-N roll:")
        for title, raw in sorted(set(odd_rates)):
            lines.append("//   %s: %s" % (title, raw))
        lines.append("")
    if unjoined:
        lines.append(
            "// Wiki pages with a clue drop that no npc_stats record cites, so no"
        )
        lines.append(
            "// gameval could be joined. Not an error — the corpus covers monsters this"
        )
        lines.append("// tree has no stats ledger for:")
        for title, count in unjoined.most_common(40):
            lines.append("//   %s (%d)" % (title, count))
        if len(unjoined) > 40:
            lines.append("//   ... and %d more" % (len(unjoined) - 40))
        lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    rows, unjoined, odd_rates = collect()
    rendered = render(rows, unjoined, odd_rates)

    if args.check:
        try:
            have = open(OUT, encoding="utf-8").read()
        except FileNotFoundError:
            print("gen_trail_clue_drops: %s does not exist" % OUT, file=sys.stderr)
            return 1
        if have != rendered:
            print(
                "gen_trail_clue_drops: %s is stale — rerun the generator" % OUT,
                file=sys.stderr,
            )
            return 1
        print("gen_trail_clue_drops: up to date")
        return 0

    with open(OUT, "w", encoding="utf-8") as handle:
        handle.write(rendered)
    per_tier = collections.Counter(t for (_, t) in rows)
    print(
        "gen_trail_clue_drops: %d (npc, tier) row(s) over %d npc(s)"
        % (len(rows), len({g for (g, _) in rows}))
    )
    for name, tier in sorted(TIERS.items(), key=lambda kv: kv[1]):
        print("  %-9s %4d" % (name, per_tier.get(tier, 0)))
    print("  unjoined pages: %d, rounded rates: %d" % (len(unjoined), len(set(odd_rates))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
