#!/usr/bin/env python3
"""Derive the chompy hat ladder from the pinned wiki.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slice C16.

Eighteen hats at eighteen kill counts. The cache ships them as `cbhat1`..
`cbhat18` -- opaque names with no rank word and no "chompy" in them, which is
why a name search finds nothing and the count has to come from somewhere else.
It comes from here, and the fact that the wiki's table is also exactly eighteen
rows is the cross-check.

The ladder starts at **30** kills, not at the first rank. Rantz's rank ladder
has twenty-two rungs and the bottom four -- Ogre Novice, Beginner, Ogre Learner,
Learner -- have no hat at all. One-hat-per-rank invents four that do not exist.
"""
import argparse
import io
import os
import re
import sys

SRC = "docs/minigames/chompy/sources/Chompy_bird_hunting.wiki"
DEST = ("OSRS-Content/osrs239-content/server/scripts/quests/quest_chompybird/"
        "configs/chompy_hats.enum")

HEADER = """// Chompy hat ladder -- GENERATED, do not hand-edit.
//
// Written by tools/gen_chompy_hats.py from the pinned wiki. Hand edits belong
// in the manifest's [extra:] section.
//
// [cache] The hats are `cbhat1`..`cbhat18`. Eighteen objs, eighteen wiki
// thresholds, same order -- and the cache names carry no rank word, so the
// pairing cannot be checked by reading either side alone.
"""


def parse(path):
    with io.open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    start = text.find("==Chompy hats==")
    if start < 0:
        return []
    body = text[start:]
    end = body.find("\n|}")
    if end > 0:
        body = body[:end]
    rows = []
    pending = None
    for line in body.split("\n"):
        line = line.strip()
        m = re.fullmatch(r"\|([\d,]+)", line)
        if m:
            pending = int(m.group(1).replace(",", ""))
            continue
        if pending is not None and line.startswith("|{{plinkt|"):
            label = re.search(r"txt=([^}]+)", line)
            rows.append((pending, label.group(1) if label else ""))
            pending = None
    return rows


def render(rows):
    out = [HEADER, "[chompy_hat_kills]", "inputtype=int", "outputtype=int",
           "default=-1"]
    for i, (kills, _label) in enumerate(rows):
        out.append("val=%d,%d" % (i, kills))
    out.append("")
    out.append("[chompy_hat_obj]")
    out.append("inputtype=int")
    out.append("outputtype=obj")
    out.append("default=null")
    for i, _row in enumerate(rows):
        out.append("val=%d,cbhat%d" % (i, i + 1))
    out.append("")
    out.append("[chompy_hat_name]")
    out.append("inputtype=int")
    out.append("outputtype=string")
    out.append("default=none")
    for i, (_kills, label) in enumerate(rows):
        out.append("val=%d,%s" % (i, label))
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    rows = parse(SRC)
    print("gen_chompy_hats: %d hat(s), %s..%s kills"
          % (len(rows), rows[0][0] if rows else "-",
             rows[-1][0] if rows else "-"))
    if len(rows) != 18:
        print("gen_chompy_hats: expected 18 rows to match cbhat1..cbhat18, "
              "got %d" % len(rows), file=sys.stderr)
        return 1
    if [k for k, _ in rows] != sorted(k for k, _ in rows):
        print("gen_chompy_hats: thresholds are not ascending", file=sys.stderr)
        return 1

    text = render(rows)
    if args.check:
        if not os.path.exists(DEST):
            print("gen_chompy_hats: %s is missing" % DEST, file=sys.stderr)
            return 1
        with io.open(DEST, encoding="utf-8") as fh:
            if fh.read() != text:
                print("gen_chompy_hats: %s is stale" % DEST, file=sys.stderr)
                return 1
        print("gen_chompy_hats: %s is up to date" % DEST)
        return 0
    with io.open(DEST, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("gen_chompy_hats: wrote %s" % DEST)
    return 0


if __name__ == "__main__":
    sys.exit(main())
