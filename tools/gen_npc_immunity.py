#!/usr/bin/env python3
"""
gen_npc_immunity -- state every npc's poison and venom immunity from its wiki
infobox.

    tools/gen_npc_immunity.py            # report only
    tools/gen_npc_immunity.py --write    # rewrite the generated .npc file

Source: the cached `Infobox Monster` pages (OSRS-Content/osrs-content/wiki/
monsters), parsed by wiki_infobox, joined to this cache's npc ids by each
version block's own `id`/`idN` list -- never by name (docs/NPC_WIKI_STATS_PLAN.md
§1). Two infobox rows answer the question:

    |poisonresistance = 100      -> param=poison_immune,yes  (or more: 200 on three pages)
    |venomresistance  = 100      -> param=venom_immune,yes
    |venomresistance  = Poison   -> param=venom_immune,yes  (poisonable: "venomous
                                    weapons will inflict regular poison instead",
                                    wiki Venom)

A 0 or an absent row is poisonable / envenomable, which is both params' declared
default (combat.param), so nothing is written for it.

Only npcs that already have a server record get a block: a block for a cache npc
the server never described would make a new record that states nothing else,
which `cachepack --defaults=error` refuses. The server's records are the blocks
of `_explicit/configs/server_fields.generated.npc`, which states every one.

The output is a `.generated.` file, so it merges BELOW hand-authored blocks
(cp_merge_rank_for): a raid's own `param=poison_immune` line wins over the one
here, and agrees with it.
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs-content")
COMPACK = os.path.join(CONTENT, "configs", "all.npc.compack")
SERVER_FIELDS = os.path.join(CONTENT, "server", "scripts", "_explicit", "configs",
                             "server_fields.generated.npc")
OUT = os.path.join(CONTENT, "server", "scripts", "npc", "configs", "npc_immunity.generated.npc")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wiki_infobox as wi  # noqa: E402

BLOCK_RE = re.compile(r"^\[(.+)\]$")


def load_compack() -> dict[int, str]:
    out: dict[int, str] = {}
    with open(COMPACK, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("//") or "=" not in line:
                continue
            key, name = line.split("=", 1)
            if key.isdigit():
                out[int(key)] = name
    return out


def load_server_records() -> set[str]:
    names = set()
    with open(SERVER_FIELDS, encoding="latin-1") as f:
        for line in f:
            m = BLOCK_RE.match(line.strip())
            if m:
                names.add(m.group(1))
    return names


def resistance(fields: dict[str, str], key: str) -> str | None:
    """The row's cleaned value, lower-cased, or None when the row is absent or
    empty."""
    raw = fields.get(key)
    if raw is None:
        return None
    value = wi.clean_text(raw).strip().lower()
    return value or None


def immune(value: str | None) -> bool:
    """100% or more. Three pages state 200 (Maggot King, Lowerniel Drakan,
    Wyrd): more than full resistance is still none taken."""
    return value is not None and value.isdigit() and int(value) >= 100


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()

    names = load_compack()
    records = load_server_records()

    # id -> (poison_immune, venom_immune, page) from the first page naming it.
    answers: dict[int, tuple[bool, bool, str]] = {}
    conflicts: list[str] = []
    odd_values: dict[str, int] = {}
    for path in sorted(glob.glob(os.path.join(wi.MONSTERS_DIR, "*.wikitext"))):
        title = os.path.basename(path)[: -len(".wikitext")]
        with open(path, encoding="utf-8") as f:
            versions = wi.parse_page(f.read())
        for version in versions:
            poison = resistance(version["fields"], "poisonresistance")
            venom = resistance(version["fields"], "venomresistance")
            for value in (poison, venom):
                if value not in (None, "poison") and not value.isdigit():
                    odd_values[value] = odd_values.get(value, 0) + 1
            answer = (immune(poison), venom == "poison" or immune(venom), title)
            for npc_id in version["ids"]:
                if npc_id in answers and answers[npc_id][:2] != answer[:2]:
                    conflicts.append("npc %d: %r says %s, %r says %s" % (
                        npc_id, answers[npc_id][2], answers[npc_id][:2], title, answer[:2]))
                    continue
                answers.setdefault(npc_id, answer)

    blocks: list[tuple[str, int, bool, bool, str]] = []
    no_record = 0
    for npc_id, (poison, venom, title) in sorted(answers.items()):
        if not poison and not venom:
            continue
        name = names.get(npc_id)
        if name is None or name not in records:
            no_record += 1
            continue
        blocks.append((name, npc_id, poison, venom, title))

    print("wiki ids with an answer: %d" % len(answers))
    print("immune to something, with a server record: %d (poison %d, venom %d)" % (
        len(blocks), sum(1 for b in blocks if b[2]), sum(1 for b in blocks if b[3])))
    print("immune to something, no server record (skipped): %d" % no_record)
    print("ids two pages disagree on (first page kept): %d" % len(conflicts))
    for line in conflicts[:20]:
        print("  " + line)
    if odd_values:
        print("resistance values read as 'not immune': %s" % odd_values)

    if not args.write:
        return

    out = [
        "// Generated by tools/gen_npc_immunity.py --write. Re-running rewrites this file.",
        "//",
        "// Every npc whose wiki infobox says it cannot be poisoned (`Poison: 100%",
        "// resistance` -> poison_immune) or envenomed (`Venom: 100% resistance` or",
        "// `Venom: Poison` -> venom_immune), joined to this cache's npc ids by each",
        "// version block's own id list. Read by `~npc_inflict_poison` /",
        "// `~npc_inflict_venom` (skill_combat/scripts/poison.rs2). An npc absent here",
        "// is poisonable and envenomable, which is both params' declared default.",
        "//",
        "// A `.generated.` file: it merges below hand-authored blocks.",
        "//",
        "// %d npcs." % len(blocks),
        "",
    ]
    for name, npc_id, poison, venom, title in sorted(blocks):
        out.append("[%s]" % name)
        if poison:
            out.append("param=poison_immune,yes // %s (npc %d)" % (title, npc_id))
        if venom:
            out.append("param=venom_immune,yes // %s (npc %d)" % (title, npc_id))
        out.append("")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(out).rstrip() + "\n")
    print("wrote %s" % OUT)


if __name__ == "__main__":
    main()
