#!/usr/bin/env python3
"""Derive the Agility shortcut table from the pinned wiki.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slice D2.

NR's `skills/agility/shortcut/` is the single largest file set on its side --
8,242 lines -- and almost all of it is one fact repeated: this obstacle needs
level L and pays X experience. That is a table, and it belongs in a generator.

Three things the extraction has to get right, each of which is silent when
wrong:

  * **A shortcut can have more than one requirement, and they are alternatives,
    not a set.** The Yanille wall is Agility 8 + Strength 19 + Ranged 37 with a
    grapple, OR Agility 48 barehanded. Reading the first number as "the" level
    locks a barehanded player out at 48 and lets a grappler through at 8 with no
    grapple.
  * **The experience is not always a single number.** "3 (1 on failure)" is two
    values, and a shortcut with a failure case pays either. Taking the first
    integer silently drops the failure award; taking the last drops the success.
  * **A level of 1 is real.** The Lumbridge stepping stones need Agility 1, so
    "no requirement" cannot be spelled 0 and then tested with `> 0`.
"""
import argparse
import io
import os
import re
import sys

SRC = "docs/skills/agility/sources/Shortcuts.wiki"
DEST = ("OSRS-Content/osrs239-content/server/scripts/skill_agility/configs/"
        "agility_shortcuts.enum")

HEADER = """// Agility shortcuts -- GENERATED, do not hand-edit.
//
// Written by tools/gen_agility_shortcuts.py from the pinned wiki. Hand edits
// belong in the manifest's [extra:] section.
//
// One row per shortcut. `agility_shortcut_alt_level` is the SECOND way through
// where one exists (the barehanded route past a grapple obstacle, say) and 0
// where it does not -- a shortcut with two routes has two levels and they are
// alternatives, not a combined requirement.
"""


def strip_markup(text):
    text = re.sub(r"<ref[^>]*>.*?</ref>", "", text, flags=re.S)
    text = re.sub(r"<[^>]+>", " ", text)
    text = re.sub(r"\[\[([^\]|]+)\|([^\]]+)\]\]", r"\2", text)
    text = re.sub(r"\[\[([^\]]+)\]\]", r"\1", text)
    text = re.sub(r"\{\{NA\}\}", "", text)
    text = re.sub(r"\s+", " ", text)
    return text.strip()


def agility_levels(cell):
    """Every Agility requirement in the cell, in order."""
    return [int(m) for m in re.findall(r"\{\{SCP\|Agility\|(\d+)", cell)]


def xp_values(cell):
    text = strip_markup(cell)
    return [float(m) for m in re.findall(r"(\d+(?:\.\d+)?)", text)]


def parse(path):
    with io.open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    start = text.find("==List of shortcuts==")
    if start < 0:
        return []
    body = text[start:]
    end = body.find("\n|}")
    if end > 0:
        body = body[:end]

    rows = []
    for chunk in body.split("\n|-\n")[1:]:
        cells = []
        cur = None
        for line in chunk.split("\n"):
            if line.startswith("|") and not line.startswith("|-"):
                if cur is not None:
                    cells.append("\n".join(cur))
                cur = [line[1:]]
            elif cur is not None:
                cur.append(line)
        if cur is not None:
            cells.append("\n".join(cur))
        if len(cells) < 6:
            continue
        levels = agility_levels(cells[0])
        if not levels:
            continue
        name = strip_markup(cells[2])
        xp = xp_values(cells[5])
        rows.append({
            "name": name,
            "level": levels[0],
            "alt_level": levels[1] if len(levels) > 1 else 0,
            # Tenths, because 0.5 xp is a real award (the Falador wall) and an
            # integer column would round it to nothing.
            "xp_tenths": int(round(xp[0] * 10)) if xp else 0,
            "fail_tenths": int(round(xp[1] * 10)) if len(xp) > 1 else 0,
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
    out += block("agility_shortcut_name", "string", "none", "name")
    out += block("agility_shortcut_level", "int", "0", "level")
    out += block("agility_shortcut_alt_level", "int", "0", "alt_level")
    out += block("agility_shortcut_xp_tenths", "int", "0", "xp_tenths")
    out += block("agility_shortcut_fail_tenths", "int", "0", "fail_tenths")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    rows = parse(SRC)
    alts = sum(1 for r in rows if r["alt_level"])
    fails = sum(1 for r in rows if r["fail_tenths"])
    frac = sum(1 for r in rows if r["xp_tenths"] % 10)
    print("gen_agility_shortcuts: %d shortcut(s), %d with a second route, "
          "%d with a failure award, %d with fractional xp"
          % (len(rows), alts, fails, frac))
    if not rows:
        print("gen_agility_shortcuts: parsed no rows", file=sys.stderr)
        return 1
    if not alts:
        print("gen_agility_shortcuts: no shortcut has a second route -- the "
              "alternative-requirement column has collapsed", file=sys.stderr)
        return 1
    if not frac:
        print("gen_agility_shortcuts: no fractional experience survived -- "
              "tenths have been rounded away", file=sys.stderr)
        return 1
    if any(r["level"] < 1 for r in rows):
        print("gen_agility_shortcuts: a shortcut parsed to level 0; level 1 is "
              "real and 0 is not", file=sys.stderr)
        return 1

    text = render(rows)
    if args.check:
        if not os.path.exists(DEST):
            print("gen_agility_shortcuts: %s is missing" % DEST,
                  file=sys.stderr)
            return 1
        with io.open(DEST, encoding="utf-8") as fh:
            if fh.read() != text:
                print("gen_agility_shortcuts: %s is stale" % DEST,
                      file=sys.stderr)
                return 1
        print("gen_agility_shortcuts: %s is up to date" % DEST)
        return 0
    os.makedirs(os.path.dirname(DEST), exist_ok=True)
    with io.open(DEST, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("gen_agility_shortcuts: wrote %s" % DEST)
    return 0


if __name__ == "__main__":
    sys.exit(main())
