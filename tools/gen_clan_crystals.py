#!/usr/bin/env python3
"""Derive Prifddinas' clan-crystal recolour table from the cache.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slice C7.

Wearing a clan's crystal recolours crystal equipment into that clan's livery.
The cache ships every variant as its own obj, named `<base>_<clan>`, so the
table is a fact of the cache and not something to type: this walks
`configs/all.obj.compack` and emits `clan_crystal.enum`.

The asymmetry is the point. Eight clans have a crown and a clan crystal, but
**seven** have armour recolours and **seven** have weapon recolours, and they
are not the same seven -- meilyr has no armour and hefin has no weapons. A
hand-written 8x5 table would name objs the cache does not have. Run with
--check in CI so the day a cache adds the missing variants, the tree is told
rather than silently staying seven wide.
"""
import argparse
import io
import os
import re
import sys

CLANS = ["amlodd", "cadarn", "crwys", "hefin", "iorwerth", "ithell", "meilyr",
         "trahaearn"]

# The base objs a clan crystal can recolour. `crystal_crown` and
# `prif_clan_crystal` are per-clan by construction rather than recolours of a
# neutral base, so they are listed separately below.
BASES = [
    "crystal_helmet",
    "crystal_helmet_inactive",
    "crystal_chestplate",
    "crystal_chestplate_inactive",
    "crystal_platelegs",
    "crystal_platelegs_inactive",
    "blade_of_saeldor_infinite",
    "bow_of_faerdhinen_infinite",
]

HEADER = """// Prifddinas clan-crystal recolours -- GENERATED, do not hand-edit.
//
// Written by tools/gen_clan_crystals.py from configs/all.obj.compack. Hand
// edits belong in the manifest's [extra:] section, not here.
//
// Each row keys a (base obj, clan) pair to the clan's variant. A pair the cache
// does not ship is simply absent -- see the generator's docstring for which,
// and why that asymmetry is load-bearing.
"""


def read_names(content_dir):
    path = os.path.join(content_dir, "configs", "all.obj.compack")
    names = set()
    with io.open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if "=" not in line:
                continue
            _, _, name = line.partition("=")
            names.add(name)
    return names


def build(names):
    rows = []
    for base in BASES:
        for clan in CLANS:
            variant = "%s_%s" % (base, clan)
            if variant in names:
                rows.append((base, clan, variant))
    crowns = [("crystal_crown", c, "crystal_crown_%s" % c) for c in CLANS
              if "crystal_crown_%s" % c in names]
    keys = [("prif_clan_crystal", c, "prif_clan_crystal_%s" % c) for c in CLANS
            if "prif_clan_crystal_%s" % c in names]
    return rows, crowns, keys


# The enum key is `base_index * len(CLANS) + clan_index`, so a script can
# compute it from the two things it knows instead of carrying a second lookup.
# A pair the cache does not ship is simply absent and the enum's `default=null`
# answers for it -- which is what lets `~clan_crystal_variant` say "this clan
# has no version of that item" without a separate table of exceptions.
ALL_BASES = BASES + ["crystal_crown", "prif_clan_crystal"]


def key_for(base, clan):
    return ALL_BASES.index(base) * len(CLANS) + CLANS.index(clan)


def render(rows, crowns, keys):
    out = [HEADER]
    out.append("[clan_crystal_variant]")
    out.append("inputtype=int")
    out.append("outputtype=obj")
    out.append("default=null")
    for base, clan, variant in rows + crowns + keys:
        out.append("val=%d,%s" % (key_for(base, clan), variant))
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--content", default="OSRS-Content/osrs239-content")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    names = read_names(args.content)
    rows, crowns, keys = build(names)

    armour_clans = sorted({c for b, c, _ in rows if b.startswith("crystal_")})
    weapon_clans = sorted({c for b, c, _ in rows if not b.startswith("crystal_")})

    print("clan crystals: %d clan(s) with a crown, %d with a clan crystal"
          % (len(crowns), len(keys)))
    print("  armour recolours: %d clan(s) -- %s"
          % (len(armour_clans), " ".join(armour_clans)))
    print("  weapon recolours: %d clan(s) -- %s"
          % (len(weapon_clans), " ".join(weapon_clans)))

    missing_armour = sorted(set(CLANS) - set(armour_clans))
    missing_weapon = sorted(set(CLANS) - set(weapon_clans))
    if missing_armour:
        print("  no armour variant: %s" % " ".join(missing_armour))
    if missing_weapon:
        print("  no weapon variant: %s" % " ".join(missing_weapon))

    if len(crowns) != len(CLANS) or len(keys) != len(CLANS):
        print("gen_clan_crystals: every clan must have a crown and a clan "
              "crystal; the cache disagrees", file=sys.stderr)
        return 1

    dest = os.path.join(args.content, "server", "scripts", "areas",
                        "area_prifddinas", "configs", "clan_crystal.enum")
    text = render(rows, crowns, keys)
    if args.check:
        if not os.path.exists(dest):
            print("gen_clan_crystals: %s is missing" % dest, file=sys.stderr)
            return 1
        with io.open(dest, encoding="utf-8") as fh:
            if fh.read() != text:
                print("gen_clan_crystals: %s is stale -- re-run without --check"
                      % dest, file=sys.stderr)
                return 1
        print("gen_clan_crystals: %s is up to date" % dest)
        return 0
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with io.open(dest, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("gen_clan_crystals: wrote %s (%d row(s))"
          % (dest, len(rows) + len(crowns) + len(keys)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
