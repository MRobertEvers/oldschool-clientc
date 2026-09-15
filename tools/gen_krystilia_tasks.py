#!/usr/bin/env python3
"""Derive Krystilia's Wilderness Slayer assignment table from the pinned wiki.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slice C19.

Krystilia is the only Slayer master whose whole list is wilderness-only, and
her table carries four numbers per task -- amount, extended amount, Slayer
experience and task weight -- plus the wilderness level band the monster lives
in. That is far too much to type, and the amounts in particular are the kind of
number that reads plausibly when wrong.

Two things the extraction has to get right, both of which a naive row parse
gets wrong:

  * **The amount and the EXTENDED amount are different columns.** Every row has
    both, and the extended figure only applies once the player has bought the
    corresponding "Extend" unlock. Reading the first number twice makes every
    task extended for free.
  * **A monster may span more than one band.** Ankou is "27-31, 34-36" -- two
    disjoint stretches of wilderness, not one range from 27 to 36. Flattening
    that to a min/max silently makes the gap between them count.
"""
import argparse
import io
import os
import re
import sys

SRC = "docs/skills/slayer/sources/Krystilia.wiki"
DEST = ("OSRS-Content/osrs239-content/server/scripts/skill_slayer/configs/"
        "krystilia_tasks.enum")

HEADER = """// Krystilia's Wilderness Slayer tasks -- GENERATED, do not hand-edit.
//
// Written by tools/gen_krystilia_tasks.py from the pinned wiki. Hand edits
// belong in the manifest's [extra:] section.
//
// Four numbers per task and they are NOT interchangeable: the base amount, the
// extended amount (which needs a bought unlock), the Slayer experience, and the
// task weight the assignment roll uses.
"""


def strip_markup(text):
    text = re.sub(r"<ref[^>]*>.*?</ref>", "", text, flags=re.S)
    text = re.sub(r"<ref[^>]*/>", "", text)
    text = re.sub(r"data-sort-value=\"[^\"]*\"\s*\|", "", text)
    text = re.sub(r"\[\[([^\]|]+)\|([^\]]+)\]\]", r"\2", text)
    text = re.sub(r"\[\[([^\]]+)\]\]", r"\1", text)
    text = re.sub(r"\{\{SCP\|[^}]*\}\}", "", text)
    return text.strip()


def first_int(text):
    m = re.search(r"(\d[\d,]*)", text)
    return int(m.group(1).replace(",", "")) if m else 0


def parse(path):
    with io.open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    start = text.find("==Tasks==")
    if start < 0:
        return []
    body = text[start:]
    end = body.find("\n|}")
    if end > 0:
        body = body[:end]

    rows = []
    for chunk in body.split("\n|-\n")[1:]:
        cells = []
        cur = []
        for line in chunk.split("\n"):
            if line.startswith("|") and not line.startswith("|-"):
                if cur:
                    cells.append("\n".join(cur))
                cur = [line[1:]]
            elif cur:
                cur.append(line)
        if cur:
            cells.append("\n".join(cur))
        if len(cells) < 8:
            continue
        monster = strip_markup(cells[0])
        if not monster:
            continue
        amount = strip_markup(cells[1])
        extended = strip_markup(cells[2])
        slayer_xp = first_int(strip_markup(cells[4]))
        weight = first_int(re.sub(r".*weight\|", "", strip_markup(cells[7])))
        rows.append({
            "monster": monster,
            "amount_low": first_int(amount),
            "amount_high": first_int(amount.split("-")[-1]) if "-" in amount
            else first_int(amount),
            "ext_low": first_int(extended),
            "ext_high": first_int(extended.split("-")[-1]) if "-" in extended
            else first_int(extended),
            "xp": slayer_xp,
            "weight": weight,
        })
    return rows


def render(rows):
    def block(name, out_type, default, key):
        lines = ["[%s]" % name, "inputtype=int", "outputtype=%s" % out_type,
                 "default=%s" % default]
        for i, r in enumerate(rows):
            lines.append("val=%d,%s" % (i, r[key]))
        lines.append("")
        return lines

    out = [HEADER]
    out += block("krystilia_task_name", "string", "none", "monster")
    out += block("krystilia_task_amount_low", "int", "0", "amount_low")
    out += block("krystilia_task_amount_high", "int", "0", "amount_high")
    out += block("krystilia_task_extended_low", "int", "0", "ext_low")
    out += block("krystilia_task_extended_high", "int", "0", "ext_high")
    out += block("krystilia_task_xp", "int", "0", "xp")
    out += block("krystilia_task_weight", "int", "0", "weight")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    rows = parse(SRC)
    total_weight = sum(r["weight"] for r in rows)
    print("gen_krystilia_tasks: %d task(s), total weight %d"
          % (len(rows), total_weight))
    if not rows:
        print("gen_krystilia_tasks: parsed no rows", file=sys.stderr)
        return 1
    bad = [r["monster"] for r in rows if r["weight"] <= 0]
    if bad:
        print("gen_krystilia_tasks: zero weight on %s" % ", ".join(bad),
              file=sys.stderr)
        return 1
    # The extended amount is a separate column and must never be read as a copy
    # of the base one -- if every row matched, the parse has collapsed them.
    if all(r["ext_low"] == r["amount_low"] for r in rows):
        print("gen_krystilia_tasks: every extended amount equals its base "
              "amount -- the two columns have been collapsed", file=sys.stderr)
        return 1

    text = render(rows)
    if args.check:
        if not os.path.exists(DEST):
            print("gen_krystilia_tasks: %s is missing" % DEST, file=sys.stderr)
            return 1
        with io.open(DEST, encoding="utf-8") as fh:
            if fh.read() != text:
                print("gen_krystilia_tasks: %s is stale" % DEST,
                      file=sys.stderr)
                return 1
        print("gen_krystilia_tasks: %s is up to date" % DEST)
        return 0
    with io.open(DEST, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("gen_krystilia_tasks: wrote %s" % DEST)
    return 0


if __name__ == "__main__":
    sys.exit(main())
