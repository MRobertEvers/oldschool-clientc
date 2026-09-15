#!/usr/bin/env python3
"""Surface every npc whose attack style the generator did not recognise.

`gen_npc_stats.py` maps the wiki's "attack style" onto a `damagetype`. When it
meets a spelling it does not know it falls back to

    return "physical", "crushattack", 2, "no recognized attack style (...)"

and records the note in that npc's `.stats` ledger. The note is honest and it is
written down -- but it lands in one of 40 files under `npc_stats/`, while the
value it explains lands in `combat_stats.generated.npc` where it looks like any
other deliberate choice. Nobody reads 40 ledgers.

That is how `chaos_fanatic` and `smoke_devil_boss` ended up swinging crush at a
magic attack: the wiki writes them as "Ranged magic" and "Magical ranged", both
unrecognised, both defaulted, both recorded somewhere nobody looked. Those two
are fixed in the generator; this prints the rest so the next one is noticed
before a contract checker stumbles over it.

Not every entry here is a bug. Dragonfire genuinely is not a damagetype -- it is
its own mechanic -- so "crush" is wrong for it in a way that needs a design
decision rather than a lookup-table row. The point is that the list is visible.
"""
import argparse
import collections
import glob
import io
import os
import re
import sys

LEDGERS = "OSRS-Content/osrs239-content/npc_stats"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                   help="exit non-zero if any npc defaulted (advisory; off by "
                        "default because the list is known and triaged)")
    args = ap.parse_args()

    by_style = collections.defaultdict(list)
    for path in sorted(glob.glob(LEDGERS + "/*/*.stats")):
        text = io.open(path, encoding="utf-8", errors="replace").read()
        m = re.search(r"no recognized attack style \((.*?)\); defaulted to crush",
                      text)
        if not m:
            continue
        raw = m.group(1)
        # The ledger records the whole list the wiki gave, e.g.
        # "['magical ranged (100% prayer penetration)']". Strip the python
        # repr and the parenthetical so spellings that differ only by a note
        # group together -- otherwise "magical ranged" and "magical ranged
        # (100% prayer penetration)" look like two separate problems when they
        # are one missing lookup row.
        for style in re.findall(r"'([^']+)'", raw) or [raw]:
            style = re.sub(r"\s*\(.*?\)", "", style).strip().lower()
            if style:
                by_style[style].append(os.path.basename(path)[:-6])

    total = sum(len(v) for v in by_style.values())
    print("check_npc_style_defaults: %d npc(s) fell back to crush across %d "
          "unrecognised style spelling(s)" % (total, len(by_style)))
    for style, npcs in sorted(by_style.items(), key=lambda kv: -len(kv[1])):
        sample = ", ".join(sorted(npcs)[:3])
        more = "" if len(npcs) <= 3 else " (+%d more)" % (len(npcs) - 3)
        print("  %-22s %3d npc(s): %s%s" % (style, len(npcs), sample, more))

    if not by_style:
        print("  none -- every attack style in the corpus is recognised")
    if args.check and by_style:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
