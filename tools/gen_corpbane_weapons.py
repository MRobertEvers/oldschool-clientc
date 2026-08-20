#!/usr/bin/env python3
"""Derive the Corpbane weapon list from the pinned wiki.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slice B12.

Mod Ash, 30 August 2023, quoted on the wiki:

    "They're both classified as spears internally. The Corp actually uses a
     separate parameter called **corpbane** that's applied to individual
     spears/halberds that have been enabled against it."

So this is a **per-weapon flag, not a weapon category**. That distinction is the
whole slice: the dragon hasta is a spear and is NOT corpbane -- the wiki's
Changes section records it losing full damage on 5 June 2019 -- so an
implementation that tests "is this a spear" pays full damage on a weapon the
game halves. The list has to be enumerated, which is why it is generated.

The wiki's own note also matters: poisoned variants of these weapons are
corpbane too, "though the Corporeal Beast is immune to both poison and venom".
"""
import argparse
import io
import os
import re
import sys

SRC = "docs/bosses/wiki/Corpbane_weapons.wiki"
DEST = ("OSRS-Content/osrs239-content/server/scripts/bosses/boss_corp/configs/"
        "corpbane.generated.enum")

HEADER = """// Corpbane weapons -- GENERATED, do not hand-edit.
//
// Written by tools/gen_corpbane_weapons.py from the pinned wiki. Hand edits
// belong in the manifest's [extra:] section.
//
// [jagex] Mod Ash: the Corporeal Beast reads a per-weapon `corpbane` flag, NOT
// a weapon category -- the dragon hasta is a spear and is not on this list.
"""


def parse(path):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    start = text.find("==List of weapons==")
    if start < 0:
        return []
    body = text[start:]
    names = []
    for block in re.finditer(r"\{\{Infotable Bonuses\n(.*?)\}\}", body, re.S):
        for line in block.group(1).split("\n"):
            line = line.strip()
            if not line.startswith("|"):
                continue
            name = line[1:].split("#")[0].strip()
            if not name or "=" in name:
                continue
            if name not in names:
                names.append(name)
    return names


def slug(text):
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")


def render(names):
    out = [HEADER, "[corpbane_weapon_name]", "inputtype=int",
           "outputtype=string", "default=none"]
    for i, n in enumerate(names):
        out.append("val=%d,%s" % (i, n))
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    names = parse(SRC)
    spears = [n for n in names if "spear" in n.lower()]
    poles = [n for n in names if "halberd" in n.lower() or "hasta" in n.lower()]
    print("gen_corpbane_weapons: %d weapon(s) -- %d spear(s), %d polearm(s)"
          % (len(names), len(spears), len(poles)))
    if not names:
        print("gen_corpbane_weapons: parsed no weapons", file=sys.stderr)
        return 1
    # The dragon hasta is the canary: it is a spear, it is NOT corpbane, and an
    # implementation that tests the weapon CATEGORY instead of this list gets it
    # wrong. If it ever appears here, either the wiki changed or the parse has
    # started scooping up rows it should not.
    hasta = [n for n in names if "hasta" in n.lower()]
    if hasta:
        print("gen_corpbane_weapons: %s parsed as corpbane -- the dragon hasta "
              "lost full damage on 5 June 2019 and must not be on this list"
              % ", ".join(hasta), file=sys.stderr)
        return 1

    text = render(names)
    if args.check:
        if not os.path.exists(DEST):
            print("gen_corpbane_weapons: %s is missing" % DEST, file=sys.stderr)
            return 1
        with io.open(DEST, encoding="utf-8") as fh:
            if fh.read() != text:
                print("gen_corpbane_weapons: %s is stale" % DEST,
                      file=sys.stderr)
                return 1
        print("gen_corpbane_weapons: %s is up to date" % DEST)
        return 0
    os.makedirs(os.path.dirname(DEST), exist_ok=True)
    with io.open(DEST, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("gen_corpbane_weapons: wrote %s" % DEST)
    return 0


if __name__ == "__main__":
    sys.exit(main())
