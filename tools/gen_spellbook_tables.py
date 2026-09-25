#!/usr/bin/env python3
"""Derive all four spellbooks from the pinned wiki.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slices D4-D7.

Each spell's own page carries an `{{Infobox Spell}}` with the level, the
spellbook, the experience and the rune cost. The spellbook articles do NOT --
they transclude a navbox that lists names only -- so the data has to come from
the individual spell pages, all pinned under `sources/spells/`.

Three things the extraction must not flatten:

  * **The rune cost is a set, not a number.** `{{RuneReq|Astral=2|Cosmic=2|
    Law=1}}` is three runes at three counts. A spell recorded as "5 runes" can
    never be cast correctly.
  * **Experience can be fractional.** Several utility spells award e.g. 61.5,
    and an integer column rounds that away.
  * **A page's `spellbook` field is authoritative over the navbox that led to
    it.** A handful of spells appear in more than one book's navigation; the
    infobox says which book actually owns the entry.
"""
import argparse
import glob
import io
import os
import re
import sys

SRC = "docs/skills/magic/sources/spells"

# The infobox's own `spellbook` value, verbatim -- and it is NOT the name the
# article uses. The standard spellbook's pages say `spellbook = Normal`, not
# "Standard"; "Ancient" is Ancient Magicks. Two spells say `all`/`All` (they are
# castable from every book) and are deliberately excluded rather than filed
# under one.
BOOKS = ("Normal", "Lunar", "Arceuus", "Ancient")
DEST = ("OSRS-Content/osrs239-content/server/scripts/skill_magic/configs/"
        "spellbooks.generated.enum")

HEADER = """// Lunar and Arceuus spellbooks -- GENERATED, do not hand-edit.
//
// Written by tools/gen_spellbook_tables.py from the per-spell pages pinned in
// docs/skills/magic/sources/spells/. Hand edits belong in the manifest's
// [extra:] section.
//
// Experience is in TENTHS: several spells award a fractional amount and an
// integer column rounds it to nothing. Rune costs are a `rune=count` list, not
// a total -- a spell recorded as "five runes" cannot be cast.
"""


def field(text, name):
    m = re.search(r"^\|\s*%s\s*=\s*(.*)$" % name, text, re.M)
    return m.group(1).strip() if m else ""


def runes(text):
    m = re.search(r"\{\{RuneReq\|([^}]*)\}\}", text)
    if not m:
        return []
    out = []
    for part in m.group(1).split("|"):
        if "=" not in part:
            continue
        rune, _, count = part.partition("=")
        rune = rune.strip().lower()
        try:
            out.append((rune, int(count.strip())))
        except ValueError:
            continue
    return out


def parse():
    rows = []
    for path in sorted(glob.glob(SRC + "/*.wiki")):
        text = io.open(path, encoding="utf-8", errors="replace").read()
        if "Infobox Spell" not in text:
            continue
        book = field(text, "spellbook").strip()
        if book not in BOOKS:
            continue
        name = field(text, "name") or os.path.basename(path)[:-5]
        level = field(text, "level")
        exp = field(text, "exp")
        lvl = int(re.search(r"\d+", level).group(0)) if re.search(r"\d+", level) else 0
        m = re.search(r"\d+(?:\.\d+)?", exp)
        tenths = int(round(float(m.group(0)) * 10)) if m else 0
        rs = runes(text)
        rows.append({"name": name, "book": book, "level": lvl,
                     "exp_tenths": tenths, "runes": rs})
    rows.sort(key=lambda r: (r["book"], r["level"], r["name"]))
    return rows


def render(rows):
    out = [HEADER]

    def block(name, out_type, default, fn):
        lines = ["[%s]" % name, "inputtype=int", "outputtype=%s" % out_type,
                 "default=%s" % default]
        for i, r in enumerate(rows):
            lines.append("val=%d,%s" % (i, fn(r)))
        lines.append("")
        return lines

    out += block("spellbook_spell_name", "string", "none", lambda r: r["name"])
    out += block("spellbook_spell_book", "string", "none", lambda r: r["book"])
    out += block("spellbook_spell_level", "int", "0", lambda r: r["level"])
    out += block("spellbook_spell_xp_tenths", "int", "0",
                 lambda r: r["exp_tenths"])
    out += block("spellbook_spell_rune_count", "int", "0",
                 lambda r: len(r["runes"]))
    # One row per (spell, rune slot), keyed spell*4+slot: no spell in either
    # book needs more than four distinct runes.
    lines = ["[spellbook_spell_rune]", "inputtype=int", "outputtype=string",
             "default=none"]
    amounts = ["[spellbook_spell_rune_amount]", "inputtype=int",
               "outputtype=int", "default=0"]
    for i, r in enumerate(rows):
        for j, (rune, count) in enumerate(r["runes"][:4]):
            lines.append("val=%d,%s" % (i * 4 + j, rune))
            amounts.append("val=%d,%d" % (i * 4 + j, count))
    lines.append("")
    amounts.append("")
    out += lines + amounts
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    rows = parse()
    per_book = {b: sum(1 for r in rows if r["book"] == b) for b in BOOKS}
    frac = sum(1 for r in rows if r["exp_tenths"] % 10)
    multi = sum(1 for r in rows if len(r["runes"]) > 1)
    widest = max((len(r["runes"]) for r in rows), default=0)
    print("gen_spellbook_tables: %d spell(s) -- %s; "
          "%d with fractional xp, %d with more than one rune, widest %d"
          % (len(rows), ", ".join("%d %s" % (per_book[b], b) for b in BOOKS),
             frac, multi, widest))

    empty = [b for b in BOOKS if not per_book[b]]
    if empty:
        print("gen_spellbook_tables: spellbook(s) with no spells: %s -- a "
              "renamed page or a redirect looks exactly like this"
              % ", ".join(empty), file=sys.stderr)
        return 1
    if not multi:
        print("gen_spellbook_tables: no spell has more than one rune -- the "
              "rune set has collapsed to a single value", file=sys.stderr)
        return 1
    if widest > 4:
        print("gen_spellbook_tables: a spell needs %d runes but the key packs "
              "4 per spell -- widen the stride" % widest, file=sys.stderr)
        return 1
    if not frac:
        print("gen_spellbook_tables: no fractional experience survived -- "
              "tenths have been rounded away", file=sys.stderr)
        return 1

    text = render(rows)
    if args.check:
        if not os.path.exists(DEST):
            print("gen_spellbook_tables: %s is missing" % DEST, file=sys.stderr)
            return 1
        with io.open(DEST, encoding="utf-8") as fh:
            if fh.read() != text:
                print("gen_spellbook_tables: %s is stale" % DEST,
                      file=sys.stderr)
                return 1
        print("gen_spellbook_tables: %s is up to date" % DEST)
        return 0
    with io.open(DEST, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("gen_spellbook_tables: wrote %s" % DEST)
    return 0


if __name__ == "__main__":
    sys.exit(main())
